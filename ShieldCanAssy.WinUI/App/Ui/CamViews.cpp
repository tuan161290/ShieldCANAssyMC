#include "pch.h"
#include "Ui/CamViews.h"

#include <cmath>
#include <format>

#include "Base/Text.h"
#include "Communication/ImageData.h"
#include "Communication/VisionConnect.h"
#include "Controller/McControl.h"
#include "Controller/VisionReadout.h"
#include "Data/McDataManager.h"
#include "Data/SystemData.h"
#include "Lang/Loc.h"
#include "Ui/AppHost.h"
#include "Ui/UiUtil.h"
#include "Utility/Log/LogWorker.h"
#include "Vision/VisionMasterDevice.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace
{
    std::string OrWaiting(std::string s) { return s.empty() ? sca::Loc::T("Main.NotTriggered") : s; }

    std::string BindKey(int loadCount, const std::string& path) { return std::to_string(loadCount) + "|" + path; }

    /// <summary>Đường dẫn Flow/module của ba khung (Tool 1, Tool 2, Top) — ô ở Setting: "T1" = cả Flow T1, "T1.Module" =
    /// module trong Flow T1, trống = cả Flow (VisionOutputNames::RenderPath).</summary>
    std::array<std::string, 3> RenderPaths()
    {
        auto sys = sca::McDataManager::SysData();
        sca::VisionOutputNames n = sys ? sys->VisionOutputs : sca::VisionOutputNames{};
        return { sca::VisionOutputNames::RenderPath(n.FlowAlignBottomTool1, n.RenderTool1),
                 sca::VisionOutputNames::RenderPath(n.FlowAlignBottomTool2, n.RenderTool2),
                 sca::VisionOutputNames::RenderPath(n.FlowLocateTop, n.RenderTopDown) };
    }

    /// <summary>Nền khung ảnh CAM = màu card nhìn thấy trên máy (SetBackground của SDK chỉ nhận màu đặc).</summary>
    constexpr const wchar_t* RenderBackground = L"#F1F1F1";

    /// <summary>Đếm chung mọi lần gắn của mọi bộ khung (chỉ luồng UI) — xem CamViews.h.</summary>
    unsigned g_bindSerial = 0;
}

namespace ui
{
    // ══════════ Dựng ══════════

    void CamViews::Build(Border const& tool1, Border const& tool2, Border const& top)
    {
        if (m_built) return;
        m_built = true;
        tool1.Child(m_cams[Tool1].Build(std::wstring(T("Main.CamTool1"))));
        tool2.Child(m_cams[Tool2].Build(std::wstring(T("Main.CamTool2"))));
        top.Child(m_cams[Top].Build(std::wstring(T("Main.CamTop"))));

        auto waiting = std::make_shared<sca::ReadoutCard>();
        waiting->Note = sca::Loc::T("Main.NotTriggered");
        m_waitingCard = waiting;
        for (int i = 0; i < (int)m_cams.size(); ++i)
        {
            m_cams[i].SetReadout(m_waitingCard, sca::Loc::T("Main.NotTriggered"));
            // Bấm tiêu đề bảng: đổi Ofs/Tgt ↔ bảng tách Đích cho CẢ HAI tay (ReadoutHeader_Click → ToggleBreakdown).
            m_cams[i].OnReadoutHeaderTapped = [this] {
                m_showBreakdown = !m_showBreakdown;
                SyncToolCards();
            };
            m_cams[i].OnToolbarClicked = [this, i] { ToggleToolbar(i); };
        }
        if (m_coreReady && m_visible) QueueVm(true);
    }

    void CamViews::SetCoreReady()
    {
        m_coreReady = true;
        if (m_visible) QueueVm(true);
    }

    void CamViews::SetVisible(bool visible)
    {
        m_visible = visible;
        // Bộ khung kia đã gắn sau mình: gắn lại cả ba khi hiện ra.
        if (visible && m_bindSerial != 0 && m_bindSerial != g_bindSerial)
            for (MainCamFrame& f : m_cams) f.BoundKey.clear();
        ApplyPaneVisibility();
        if (visible) QueueVm(true);
    }

    void CamViews::Update()
    {
        if (!m_built || m_closing) return;
        UpdateCams();
        QueueVm(false);
    }

    // ══════════ Bảng số + ảnh gần nhất ══════════

    std::shared_ptr<const sca::ReadoutCard> CamViews::Shown(std::shared_ptr<const sca::ReadoutCard> const& c) const
    {
        if (!c) return m_waitingCard;
        return m_showBreakdown && c->Breakdown ? c->Breakdown : c;
    }

    void CamViews::SyncToolCards()
    {
        if (m_shotSample && !sca::VisionReadout::Tool1Card())
            m_cams[Tool1].SetReadout(Shown(m_shotSample), "Self-test sample readout");
        else
            m_cams[Tool1].SetReadout(Shown(sca::VisionReadout::Tool1Card()), OrWaiting(sca::VisionReadout::Tool1()));
        m_cams[Tool2].SetReadout(Shown(sca::VisionReadout::Tool2Card()), OrWaiting(sca::VisionReadout::Tool2()));
    }

    void CamViews::UpdateCams()
    {
        SyncToolCards();
        auto top = sca::VisionReadout::TopCard();
        m_cams[Top].SetReadout(top ? top : m_waitingCard, OrWaiting(sca::VisionReadout::Top()));

        if (m_shotSample) return;   // --shot: ảnh mẫu đã đặt, không có Vision thật
        if (auto vision = sca::McControl::Vision())
        {
            m_cams[Tool1].SetImage(vision->LastBottomUpImageTool1());
            m_cams[Tool2].SetImage(vision->LastBottomUpImageTool2());
            m_cams[Top].SetImage(vision->LastTopDownImage());
        }
    }

    void CamViews::InjectShotSample()
    {
        if (m_shotSample || !m_built) return;
        auto card = std::make_shared<sca::ReadoutCard>();
        card->Cell = "Cell (2,3)";
        card->Columns = { "X", "Y", "\xCE\xB8" };
        card->Rows = { { "Ofs", { "0.012", "-0.034", "0.15" } }, { "Tgt", { "182.410", "96.205", "90.15" } } };
        card->Note = "Sample";
        m_shotSample = card;

        auto img = std::make_shared<sca::ImageData>();
        img->Width = 320; img->Height = 240; img->Channels = 1; img->Stride = 320;
        img->Pixels.resize((size_t)img->Stride * img->Height);
        for (int y = 0; y < img->Height; ++y)
            for (int x = 0; x < img->Width; ++x)
                img->Pixels[(size_t)y * img->Stride + x] = (uint8_t)(std::abs(x - y) < 3 ? 255 : 60 + x * 150 / 319);
        m_cams[Tool1].SetImage(img);
    }

    // ══════════ VmRenderControl của VisionMaster (khi vision là VisionMaster thật) ══════════

    // Control VM gọi sang vServerApp và COM bơm thông điệp trong lúc chờ — gọi trong Loaded/Tick/Click là XAML tự kết thúc
    // app (0xC000027B). Nên chỉ xếp hàng (ui::QueueVmWork); gộp các lần gọi dồn dập thành một.
    void CamViews::QueueVm(bool force)
    {
        m_vmForce = m_vmForce || force;
        if (m_vmQueued || m_closing || !m_coreReady) return;
        m_vmQueued = true;
        QueueVmWork([this, life = std::weak_ptr<char>(m_life)] {
            if (life.expired()) return;
            m_vmQueued = false;
            // Đang ở giữa một lời gọi VM (nó bơm thông điệp nên việc này chạy lồng vào): bỏ, nhịp sau xếp lại.
            if (m_vmBusy || m_closing) return;
            const bool force = std::exchange(m_vmForce, false);
            m_vmBusy = true;
            try
            {
                EnsureVmPanes();
                UpdateRenderBindings(force);
            }
            catch (...) { m_vmBusy = false; throw; }
            m_vmBusy = false;
        });
    }

    // Tạo ba control lần đầu bộ khung hiện. Mô phỏng (FakeVision) / --shot: không có VisionMaster — khung chỉ có ảnh gần
    // nhất và chữ "(no image)". Bản C++ không chạm VisionMaster khi mô phỏng (CONTROLLER-NOTES).
    void CamViews::EnsureVmPanes()
    {
        if (m_vmAttempted || !m_built || !m_visible || !m_coreReady) return;
        m_vmAttempted = true;

        if (AppHost::Options().ShotMode()) return;
        auto vc = std::dynamic_pointer_cast<sca::VisionConnect>(sca::McControl::Vision());
        if (!vc || vc->IsSimulation()) return;

        std::string error;
        for (int i = 0; i < (int)m_cams.size(); ++i)
        {
            MainCamFrame& f = m_cams[i];
            if (f.Pane.Create(f.RenderHost(), vmkind::Render, &error))
            {
                ApplyRenderLook(i);
                Show(f.ToolbarButton(), true);
                continue;
            }
            // Thường là chưa đăng ký ActiveX của VisionMaster — ba khung cùng lý do, không thử tiếp.
            for (MainCamFrame& g : m_cams)
                if (!g.Pane.Created()) g.ShowVmError("Cannot create VisionMaster view: " + error);
            sca::LogWorker::Instance().RecordLog(m_logTag + ": cannot create CAM view - " + error, sca::LogLevel::Warning, "Vision");
            break;
        }
        for (const MainCamFrame& f : m_cams) m_vmActive = m_vmActive || f.Pane.Created();

        ApplyPaneVisibility();
        UpdateRenderBindings(true);
    }

    void CamViews::ApplyPaneVisibility()
    {
        if (!m_vmActive) return;
        const int show = m_visible && !m_closing ? 1 : 0;
        for (MainCamFrame& f : m_cams)
        {
            if (!f.Pane.Created() || show == f.PaneShown) continue;
            f.PaneShown = show;
            f.Pane.SetVisible(show == 1);
        }
    }

    // Gắn ba control vào module đặt ở trang Setting (mặc định cả Flow AlignBottom_Tool1, AlignBottom_Tool2, LocateTop). Chỉ
    // gắn SAU khi McControl nạp xong solution (nạp xong thì callback kết quả mọi module bị tắt — gắn trước là bị tắt theo);
    // nạp lại (VisionLoadCount đổi) hay đổi tên ở Setting thì gắn lại. Chưa gắn đủ thì thử lại mỗi giây (timer 1 s của WPF).
    void CamViews::UpdateRenderBindings(bool force)
    {
        if (!m_vmActive || !m_visible || m_closing) return;

        const int load = sca::McControl::VisionLoadCount();
        const auto paths = RenderPaths();

        bool allBound = true;
        for (int i = 0; i < (int)m_cams.size(); ++i)
            if (m_cams[i].Pane.Created() && m_cams[i].BoundKey != BindKey(load, paths[i])) allBound = false;
        if (allBound) return;

        const auto now = std::chrono::steady_clock::now();
        if (!force && now - m_lastBindTry < std::chrono::seconds(1)) return;
        m_lastBindTry = now;

        auto vc = std::dynamic_pointer_cast<sca::VisionConnect>(sca::McControl::Vision());
        std::shared_ptr<sca::VisionMasterDevice> device =
            vc ? std::dynamic_pointer_cast<sca::VisionMasterDevice>(vc->Device()) : nullptr;
        const bool ready = load > 0 && device != nullptr && vc->IsSolutionLoaded();

        std::string diag = ready ? "" : ", waiting for solution load";
        if (ready)
        {
            for (int i = 0; i < (int)m_cams.size(); ++i)
            {
                if (!m_cams[i].Pane.Created()) continue;
                const bool ok = BindOne(i, *device, paths[i], load);
                diag += std::format(", '{}'={}", paths[i], ok ? "bound" : "NOT found");
            }
        }

        // Log một lần cho mỗi bộ tên — đủ biết kẹt ở đâu (chưa nạp solution, hay sai tên module), không rác log mỗi giây.
        if (diag != m_loggedVisionDiagnostic)
        {
            m_loggedVisionDiagnostic = diag;
            sca::LogWorker::Instance().RecordLog(m_logTag + ": CAM views" + diag, sca::LogLevel::Info, "Vision");
        }
    }

    // Gán Flow/module <path> cho khung; đã gán đúng đường dẫn đó trong lần nạp đó thì thôi. False khi solution chưa có
    // Flow/module đó, hoặc SDK đang bận chạy Flow (RenderSource không chờ) — lần sau thử lại.
    bool CamViews::BindOne(int index, sca::VisionMasterDevice& device, std::string const& path, int loadCount)
    {
        MainCamFrame& f = m_cams[index];
        const std::string key = BindKey(loadCount, path);
        if (f.BoundKey == key) return true;
        if (!f.Pane.Created() || path.empty()) return false;

        // Khung chỉ vẽ được module ĐANG BẬT callback kết quả (bản WPF EnableResultCallbacks, sửa lỗi 30/09): bật trước
        // khi gán; cả Flow thì bật mọi module con. SDK bận/chưa nạp → thử lại lần sau; lỗi từng module chỉ log như C#.
        std::string enableError;
        if (!device.EnableResultCallbacks(path, enableError)) return false;
        if (!enableError.empty() && f.CallbackFailLoggedKey != key)
        {
            f.CallbackFailLoggedKey = key;
            sca::LogWorker::Instance().RecordLog(
                std::format("{}: CAM view '{}' - can't enable result callbacks: {}", m_logTag, path, enableError),
                sca::LogLevel::Warning, "Vision");
        }

        // Con trỏ thuộc về SDK: đưa cho control NGAY, không cất giữ.
        void* source = device.RenderSource(path);
        if (source == nullptr) return false;

        HRESULT hr = f.Pane.Host().CallPtr(vmdisp::RenderSetParamsInfo, source);
        if (FAILED(hr))
        {
            if (f.FailLoggedKey != key)
            {
                f.FailLoggedKey = key;
                sca::LogWorker::Instance().RecordLog(
                    std::format("{}: CAM view '{}' - SetParamsInfo hr=0x{:08X} {}", m_logTag, path, (unsigned)hr,
                                ::base::Narrow(f.Pane.Host().LastError())),
                    sca::LogLevel::Warning, "Vision");
            }
            return false;
        }
        f.BoundKey = key;
        m_bindSerial = ++g_bindSerial;
        ApplyRenderLook(index);   // gọi lại sau mỗi lần gắn — SDK có thể trả nền/thanh công cụ về mặc định
        return true;
    }

    void CamViews::ApplyRenderLook(int index)
    {
        MainCamFrame& f = m_cams[index];
        if (!f.Pane.Created()) return;
        HRESULT hr = f.Pane.Host().CallStr(vmdisp::RenderSetBackground, RenderBackground);
        if (SUCCEEDED(hr)) hr = f.Pane.Host().CallBool(vmdisp::RenderSetToolbarVisible, f.ToolbarShown);
        if (FAILED(hr))
        {
            std::string why = ::base::Narrow(f.Pane.Host().LastError());
            if (why == m_loggedRenderLookError) return;
            m_loggedRenderLookError = why;
            sca::LogWorker::Instance().RecordLog(m_logTag + ": cannot set CAM view background/toolbar - " + why,
                                                 sca::LogLevel::Warning, "Vision");
        }
    }

    // ☰: ẩn/hiện thanh công cụ của khung (Tuấn 2026-10-07). Lời gọi vào control qua hàng đợi, không gọi trong Click.
    void CamViews::ToggleToolbar(int index)
    {
        MainCamFrame& f = m_cams[index];
        f.ToolbarShown = !f.ToolbarShown;
        SetTint(f.ToolbarButton(), f.ToolbarShown ? Tint::Accent : Tint::None);
        QueueVmWork([this, life = std::weak_ptr<char>(m_life), index] {
            if (life.expired() || m_closing || m_vmBusy) return;
            ApplyRenderLook(index);
        });
    }

    // ══════════ Vòng đời ══════════

    void CamViews::OnWindowMoved()
    {
        for (MainCamFrame& f : m_cams) f.Pane.Sync();
    }

    void CamViews::OnModalChanged(bool modalOpen)
    {
        for (MainCamFrame& f : m_cams) f.Pane.SetEnabled(!modalOpen);
    }

    void CamViews::Dispose()
    {
        m_closing = true;
        for (MainCamFrame& f : m_cams) f.Pane.Dispose();
    }
}
