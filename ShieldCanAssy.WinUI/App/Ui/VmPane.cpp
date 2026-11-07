#include "pch.h"
#include "Ui/VmPane.h"

#include "Base/Text.h"
#include <dwmapi.h>
#include "Ui/UiUtil.h"
#include "Utility/Log/LogWorker.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;

namespace
{
    // ATL cần đúng một đối tượng module cho cả tiến trình thì AtlAxWin mới hoạt động.
    class AppAtlModule : public ATL::CAtlModuleT<AppAtlModule> {};
    AppAtlModule g_atlModule;

    HWND g_mainHwnd = nullptr;
    bool g_holdOffscreen = false;
    constexpr LONG OffscreenDx = -30000;
}

namespace ui
{
    namespace vmkind
    {
        const VmControlKind Render     = { L"{313241A6-62F9-464B-BEDE-F514827E2F4B}", 0xA, 0x20 };
        const VmControlKind GlobalTool = { L"{B60D44E7-1726-42AA-9ED5-D5D512A6C1F2}", 0x1, 0x2 };
        const VmControlKind Procedure  = { L"{4919FA4C-F224-4C1E-917C-89B7F37AAE90}", 0x1, 0x7 };
        const VmControlKind MainView   = { L"{650FFBDC-5E28-4314-8901-7F02E6145414}", 0x1, 0x7 };
    }

    void SetMainHwnd(HWND hwnd) { g_mainHwnd = hwnd; }

    HWND MainHwnd() { return g_mainHwnd; }

    void QueueVmWork(std::function<void()> work)
    {
        if (!work) return;
        PostUi([work = std::move(work)] {
            try { work(); }
            catch (winrt::hresult_error const& e)
            {
                sca::LogWorker::Instance().RecordLog("VisionMaster control call failed: " + ::base::Narrow(e.message()),
                                                     sca::LogLevel::Warning, "Vision");
            }
            catch (std::exception const& e)
            {
                sca::LogWorker::Instance().RecordLog(std::string("VisionMaster control call failed: ") + e.what(),
                                                     sca::LogLevel::Warning, "Vision");
            }
            catch (...)
            {
                sca::LogWorker::Instance().RecordLog("VisionMaster control call failed (SDK exception)",
                                                     sca::LogLevel::Warning, "Vision");
            }
        });
    }

    VmPane::~VmPane()
    {
        try { Dispose(); } catch (...) {}
    }

    bool VmPane::Create(FrameworkElement const& placeholder, const VmControlKind& kind, std::string* error)
    {
        if (m_created) return true;
        auto fail = [&](std::string why) {
            sca::LogWorker::Instance().RecordLog("VisionMaster control: " + why, sca::LogLevel::Error, "Vision");
            if (error != nullptr) *error = std::move(why);
            return false;
        };

        if (g_mainHwnd == nullptr) return fail("main window has no HWND yet");
        if (!placeholder) return fail("no placeholder");

        static bool axInitialised = false;
        if (!axInitialised)
        {
            AtlAxWinInit();
            axInitialised = true;
        }

        HRESULT hr = m_host.Create(g_mainHwnd, kind.Clsid);
        if (FAILED(hr) || !m_host.Valid())
        {
            std::string detail = ::base::Narrow(m_host.LastError());
            m_host.Destroy();
            return fail(std::format("cannot create ActiveX {} (hr=0x{:08X}{}{}) — check VisionMaster 4.4 is installed and "
                                    "ComRegister.bat was run", ::base::Narrow(kind.Clsid), (unsigned)hr,
                                    detail.empty() ? "" : ", ", detail));
        }

        // Control tự khởi tạo khi được hỏi con trỏ đối tượng; với trình soạn flow phải gọi TRƯỚC khi nạp solution.
        HRESULT hrInit = S_OK;
        m_host.CallI8(kind.GetObjectPointer, &hrInit);
        if (FAILED(hrInit))
            sca::LogWorker::Instance().RecordLog(
                std::format("VisionMaster control {}: GetObjectPointer hr=0x{:08X} {}", ::base::Narrow(kind.Clsid),
                            (unsigned)hrInit, ::base::Narrow(m_host.LastError())),
                sca::LogLevel::Warning, "Vision");

        m_host.ConvertToPopup();   // bắt buộc: cửa sổ con bị lớp composition của WinUI che
        RoundPopupCorners();

        m_kind = kind;
        m_bleedDip = wcscmp(kind.Clsid, vmkind::Render.Clsid) == 0 ? 3.0 : 0.0;
        m_placeholder = placeholder;
        m_created = true;
        m_lastRect = RECT{ -1, -1, -1, -1 };
        m_lastShown = true;        // ConvertToPopup vừa hiện popup

        // LayoutUpdated bắn rất dày (mọi lượt bố cục của cả cây) — Sync chỉ di chuyển popup khi khung thật sự đổi.
        m_layoutToken = placeholder.LayoutUpdated([this](auto&&, auto&&) { Sync(); });
        m_sizeToken = placeholder.SizeChanged([this](auto&&, auto&&) { Sync(); });
        SyncNow();   // Create đã chạy ngoài sự kiện XAML (xem VmPane.h)
        if (!m_wantEnabled) m_host.Enable(false);   // lớp phủ modal mở trong lúc đang tạo
        return true;
    }

    // Bo góc nhỏ (4 px, như khung ảnh trong XAML) + viền xám nhạt 1 px, kiểu control Windows 11 (Tuấn 06/10). Popup là
    // cửa sổ cấp cao nhất nên DWM bo và khử răng cưa được, cắt cả control ActiveX bên trong. Windows 10 bỏ qua hai thuộc
    // tính này — popup giữ góc vuông.
    void VmPane::RoundPopupCorners()
    {
        HWND popup = m_host.Popup();
        if (popup == nullptr) return;
        DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUNDSMALL;
        DwmSetWindowAttribute(popup, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof corner);
        COLORREF border = RGB(0xE0, 0xE0, 0xE0);   // ControlStrokeBrush; không đặt thì DWM có thể lấy màu accent
        DwmSetWindowAttribute(popup, DWMWA_BORDER_COLOR, &border, sizeof border);
    }

    void VmPane::Sync()
    {
        if (!m_created || m_syncQueued) return;
        m_syncQueued = true;
        PostUi([this, life = std::weak_ptr<char>(m_life)] {
            if (life.expired()) return;
            m_syncQueued = false;
            SyncNow();
        });
    }

    void VmPane::SyncNow()
    {
        if (!m_created || !m_placeholder) return;

        bool show = m_visible && m_placeholder.ActualWidth() >= 2 && m_placeholder.ActualHeight() >= 2 &&
                    m_placeholder.XamlRoot() != nullptr;
        RECT r{};
        if (show)
        {
            try
            {
                auto p = m_placeholder.TransformToVisual(nullptr).TransformPoint({ 0, 0 });   // DIP so với gốc XAML
                double s = m_placeholder.XamlRoot().RasterizationScale();                      // 1.25 ở tỉ lệ 125%
                m_host.SetBleed((int)std::lround(m_bleedDip * s));
                r = RECT{ (LONG)std::lround(p.X * s), (LONG)std::lround(p.Y * s),
                          (LONG)std::lround((p.X + m_placeholder.ActualWidth()) * s),
                          (LONG)std::lround((p.Y + m_placeholder.ActualHeight()) * s) };
            }
            catch (winrt::hresult_error const&)
            {
                show = false;   // ô giữ chỗ đã rời cây hiển thị
            }
        }

        // Popup là cửa sổ cấp cao nhất, không bị cửa sổ chủ cắt: thu nhỏ cửa sổ thì ô giữ chỗ ra ngoài vùng client (XAML
        // cắt) nhưng popup vẫn hiện nguyên chỗ cũ (Tuấn 2026-10-25). Cắt popup theo vùng client; không còn phần nào
        // nhìn thấy thì ẩn.
        RECT clip{};
        if (show)
        {
            RECT client{};
            GetClientRect(g_mainHwnd, &client);
            if (!IntersectRect(&clip, &r, &client)) show = false;
        }

        if (!show)
        {
            if (m_lastShown)
            {
                m_host.Show(false);
                m_lastShown = false;
            }
            return;
        }

        // Toạ độ popup là toạ độ MÀN HÌNH, nên cửa sổ chính di chuyển cũng phải đặt lại dù khung trong
        // cửa sổ không đổi: so cả gốc client của cửa sổ chủ. So thêm khung đầy đủ r: cùng phần hiện nhưng control
        // lệch khác bên trong popup thì vẫn phải đặt lại.
        if (g_holdOffscreen)
        {
            OffsetRect(&r, OffscreenDx, 0);
            OffsetRect(&clip, OffscreenDx, 0);
        }
        POINT origin{ 0, 0 };
        ClientToScreen(g_mainHwnd, &origin);
        RECT screen{ clip.left + origin.x, clip.top + origin.y, clip.right + origin.x, clip.bottom + origin.y };
        if (m_lastShown && EqualRect(&screen, &m_lastRect) && EqualRect(&r, &m_lastFull)) return;

        m_host.MoveInOwnerClient(r, clip);   // ClientToScreen(owner) + SetWindowPos(popup, …, SWP_NOACTIVATE | SWP_SHOWWINDOW)
        m_lastRect = screen;
        m_lastFull = r;
        m_lastShown = true;
    }

    void VmPane::SetVisible(bool visible)
    {
        m_visible = visible;
        Sync();
    }

    void VmPane::HoldOffscreen(bool hold) { g_holdOffscreen = hold; }

    void VmPane::SetEnabled(bool enabled)
    {
        m_wantEnabled = enabled;
        if (!m_created || m_enableQueued) return;
        m_enableQueued = true;
        PostUi([this, life = std::weak_ptr<char>(m_life)] {
            if (life.expired()) return;
            m_enableQueued = false;
            if (m_created) m_host.Enable(m_wantEnabled);
        });
    }

    void VmPane::Dispose()
    {
        if (!m_created) return;
        m_created = false;

        if (m_placeholder)
        {
            m_placeholder.LayoutUpdated(m_layoutToken);
            m_placeholder.SizeChanged(m_sizeToken);
            m_placeholder = nullptr;
        }

        // Thứ tự đã kiểm chứng ở VmWinUiProbe: Dispose qua DISPID rồi mới DestroyWindow.
        HRESULT hr = m_host.CallVoid(m_kind.Dispose);
        if (FAILED(hr))
            sca::LogWorker::Instance().RecordLog(
                std::format("VisionMaster control {}: Dispose hr=0x{:08X}", ::base::Narrow(m_kind.Clsid), (unsigned)hr),
                sca::LogLevel::Warning, "Vision");
        m_host.Destroy();
    }
}
