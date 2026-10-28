#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <fstream>

#include "AlarmPage.xaml.h"
#include "MainPage.xaml.h"
#include "ManualPage.xaml.h"
#include "SettingPage.xaml.h"
#include "TeachingPage.xaml.h"
#include "VisionEditPage.xaml.h"

#include "Base/Text.h"
#include "Controller/McControl.h"
#include "Data/McDataManager.h"
#include "Infor/AlarmManager.h"
#include "Infor/AxisId.h"
#include "Lang/Loc.h"
#include "SysConfig/AppPaths.h"
#include "SysConfig/UserAccess.h"
#include "Ui/AppHost.h"
#include "Ui/JogControl.h"
#include "Ui/LoginDialog.h"
#include "Ui/StatusColors.h"
#include "Ui/UiUtil.h"
#include "Ui/VmPane.h"
#include "Utility/Log/LogWorker.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Controls::Primitives;
using namespace winrt::Microsoft::UI::Xaml::Media;
namespace WF = winrt::Windows::Foundation;

namespace winrt::ShieldCanAssy::implementation
{
    MainWindow::MainWindow()
    {
        // InitializeComponent do mã sinh tự gọi sau khi dựng xong.
        ExtendsContentIntoTitleBar(true);
        auto titleBar = AppWindow().TitleBar();
        titleBar.PreferredHeightOption(Microsoft::UI::Windowing::TitleBarHeightOption::Tall);
        // Nền nút cửa sổ trong suốt để màu đèn tháp của thanh tiêu đề lộ ra (SH-1).
        titleBar.ButtonBackgroundColor(winrt::Windows::UI::Color{ 0, 0, 0, 0 });
        titleBar.ButtonInactiveBackgroundColor(winrt::Windows::UI::Color{ 0, 0, 0, 0 });
        AppWindow().Changed({ this, &MainWindow::OnAppWindowChanged });
        Closed({ this, &MainWindow::OnClosed });

        auto sys = sca::McDataManager::SysData();
        Title(ui::H(sys->MachineName + " — " + sys->MachineCode));
    }

    void MainWindow::OnRootLoaded(WF::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_hwnd) return;
        auto native = this->m_inner.as<::IWindowNative>();
        native->get_WindowHandle(&m_hwnd);
        ui::SetMainHwnd(m_hwnd);

        SetTitleBar(TitleBarArea());
        ApplyCaptionInset();
        ui::ModalOverlay::Attach(Root().XamlRoot());
        m_modalToken = ui::ModalOverlay::ModalChanged().Subscribe([this](bool open) {
            if (open) ui::StopActiveJogs();   // lớp phủ che nút jog trước khi kịp nhả
            if (!m_coreReady) return;
            if (auto page = PageOf(m_section)) page->OnModalChanged(open);
        });

        std::string root = ::base::PathToU8(sca::AppPaths::Root());
        ui::SetText(DataRootText(), sca::Loc::T("Shell.DataPrefix") + root);
        ToolTipService::SetToolTip(DataRootText(), box_value(ui::H(root)));
        BuildAxisCells();
        BuildBanner();

        // AlarmRaised chỉ bắn cho alarm THẬT SỰ mới (không bắn khi raise lại alarm cũ) — đúng ý "có việc mới thì hiện
        // lại". Bắn từ luồng nền (vòng quét an toàn) nên phải chuyển về luồng UI.
        auto weak = get_weak();
        m_alarmRaisedToken = sca::AlarmManager::AlarmRaised().Subscribe([weak](sca::AlarmData alarm) {
            ui::PostUi([weak, alarm] {
                if (auto self = weak.get()) self->OnAlarmRaised(alarm);
            });
        });

        // SH-23 Bấm AUTO mà chưa đủ điều kiện: máy ở lại MANUAL, hộp thoại nêu lý do. Bắn từ vòng quét — Dialogs::Warn tự
        // chuyển về luồng UI, không chặn vòng quét.
        m_autoRefusedToken = sca::McControl::AutoModeRefused().Subscribe([](std::string why) {
            ui::Dialogs::Warn(std::move(why), sca::Loc::T("Shell.AutoRefusedTitle"));
        });

        ShowSection(Section::Main);
        UpdateShell();

        // Nhịp đọc trạng thái = nhịp vòng điều khiển (ControlTickMs — nơi CurrentPos của trục được làm mới), kẹp 33..100
        // ms. Bản WPF bind thẳng CurrentPos nên số trục đổi theo từng vòng quét; nhịp cố định 100 ms trước đây làm toạ độ
        // trục chạy chậm hơn hẳn WPF ở ControlTickMs 50 (Tuấn 2026-10-18).
        int tickMs = 100;
        if (auto sys = sca::McDataManager::SysData()) tickMs = std::clamp(sys->ControlTickMs, 33, 100);
        m_timer = DispatcherQueue().CreateTimer();
        m_timer.Interval(std::chrono::milliseconds(tickMs));
        m_timer.IsRepeating(true);
        m_timer.Tick([this](auto&&, auto&&) { OnTick(); });
        m_timer.Start();

        if (!ui::AppHost::Options().ShotMode())
        {
            // KHÔNG Maximize ngay trong Loaded: ShowWindow bơm thông điệp đồng bộ, việc PostUi của luồng Init (OnCoreReady…)
            // chạy lồng trong sự kiện XAML → XAML fail-fast 0xC000027B / E_UNEXPECTED (2026-10-18, bắt bằng debugger: stack
            // OnRootLoaded → NtUserShowWindow → CoreMessaging → RoFailFast). Chỉ xảy ra khi Init xong trước Loaded nên lúc
            // có lúc không. Hoãn sang một lượt hàng đợi riêng, ngoài khung sự kiện XAML.
            DispatcherQueue().TryEnqueue(Microsoft::UI::Dispatching::DispatcherQueuePriority::Low, [weak] {
                auto self = weak.get();
                if (!self || self->m_shutdown) return;
                if (auto presenter = self->AppWindow().Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>())
                {
                    presenter.Maximize();
                    bool maximized = presenter.State() == Microsoft::UI::Windowing::OverlappedPresenterState::Maximized;
                    sca::LogWorker::Instance().RecordLog(maximized ? "Window maximized" : "Window: maximize requested but window is not maximized",
                                                         maximized ? sca::LogLevel::Info : sca::LogLevel::Warning, "UI");
                }
                self->ApplyCaptionInset();
            });
        }
        else
        {
            // Acrylic không thuộc cây XAML nên không vào ảnh chụp — tô nền đặc cho ảnh giống thật.
            Root().Background(ui::Res(L"BackgroundBrush"));
        }
    }

    // Cột cuối của thanh tiêu đề chừa chỗ cho ba nút cửa sổ của hệ thống (RightInset tính theo pixel thật). Tall: mỗi nút
    // 48 DIP (đo trên máy lắp ráp 2026-10-18 bằng UI Automation: 60 px ở 125%) → ít nhất 144. RightInset có lúc chỉ bằng
    // MỘT nút (cột còn 48 DIP, logo Hoyeon nằm dưới nút Maximize — Tuấn 2026-10-18) nên lấy số lớn hơn.
    void MainWindow::ApplyCaptionInset()
    {
        constexpr double MinCaptionDip = 144;
        try
        {
            double scale = Root().XamlRoot() ? Root().XamlRoot().RasterizationScale() : 1.0;
            double inset = AppWindow().TitleBar().RightInset() / (scale > 0 ? scale : 1.0);
            CaptionColumn().Width(GridLengthHelper::FromPixels(std::max(inset, MinCaptionDip)));
        }
        catch (winrt::hresult_error const&) {}
    }

    // SH-5: mỗi trục một ô rộng cố định 58: tên (10, Secondary) trên, toạ độ (12 SemiBold) dưới.
    void MainWindow::BuildAxisCells()
    {
        AxisPanel().Children().Clear();
        m_axisValues.clear();
        for (const char* axisName : sca::AxisId::All)
        {
            Border cell;
            cell.Width(58);
            cell.BorderBrush(ui::Res(L"DividerBrush"));
            cell.BorderThickness(ThicknessHelper::FromLengths(1, 0, 0, 0));
            cell.Padding(ThicknessHelper::FromLengths(0, 1, 0, 1));

            StackPanel stack;
            TextBlock name;
            name.Text(ui::H(axisName));
            name.FontSize(10);
            name.HorizontalAlignment(HorizontalAlignment::Center);
            name.Foreground(ui::Res(L"TextSecondaryBrush"));
            TextBlock value;
            value.Text(L"0.000");
            value.FontSize(12);
            value.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
            value.TextAlignment(TextAlignment::Center);
            value.HorizontalAlignment(HorizontalAlignment::Stretch);
            stack.Children().Append(name);
            stack.Children().Append(value);
            cell.Child(stack);

            AxisPanel().Children().Append(cell);
            m_axisValues.push_back(value);
        }
    }

    ui::IPage* MainWindow::PageOf(Section section)
    {
        switch (section)
        {
        case Section::Main:       return get_self<MainPage>(PageMain());
        case Section::Manual:     return get_self<ManualPage>(PageManual());
        case Section::Teaching:   return get_self<TeachingPage>(PageTeaching());
        case Section::Setting:    return get_self<SettingPage>(PageSetting());
        case Section::VisionEdit: return get_self<VisionEditPage>(PageVisionEdit());
        case Section::Alarm:      return get_self<AlarmPage>(PageAlarm());
        default:                  return nullptr;
        }
    }

    UIElement MainWindow::ElementOf(Section section)
    {
        switch (section)
        {
        case Section::Main:       return PageMain();
        case Section::Manual:     return PageManual();
        case Section::Teaching:   return PageTeaching();
        case Section::Setting:    return PageSetting();
        case Section::VisionEdit: return PageVisionEdit();
        case Section::Alarm:      return PageAlarm();
        default:                  return nullptr;
        }
    }

    RadioButton MainWindow::NavOf(Section section)
    {
        switch (section)
        {
        case Section::Main:       return NavMain();
        case Section::Manual:     return NavManual();
        case Section::Teaching:   return NavTeaching();
        case Section::Setting:    return NavSetting();
        case Section::VisionEdit: return NavVisionEdit();
        case Section::Alarm:      return NavAlarm();
        default:                  return nullptr;
        }
    }

    // SH-9: StopActiveJogs, trang cũ OnShown(false), trang mới OnShown(true) + OnTick(). Trước khi Core sẵn sàng chỉ đổi
    // trang hiển thị; các trang nhận OnShown sau OnCoreReady.
    void MainWindow::ShowSection(Section section)
    {
        Section previous = m_section;
        m_section = section;

        for (int i = 0; i < (int)Section::Count; ++i)
            ui::Show(ElementOf((Section)i), (Section)i == section);

        if (auto item = NavOf(section); item && !(item.IsChecked() && item.IsChecked().Value()))
        {
            m_navSync = true;
            item.IsChecked(true);
            m_navSync = false;
        }

        if (previous != section) ui::StopActiveJogs();   // nút jog của trang cũ có thể đang được giữ
        if (!m_coreReady) return;
        if (previous != section)
            if (auto page = PageOf(previous)) page->OnShown(false);
        if (auto page = PageOf(section))
        {
            page->OnShown(true);
            page->OnTick();
        }
    }

    void MainWindow::OnNavChecked(WF::IInspectable const& sender, RoutedEventArgs const&)
    {
        if (m_navSync || m_shutdown) return;
        auto checkedItem = sender.try_as<RadioButton>();
        if (!checkedItem) return;
        for (int i = 0; i < (int)Section::Count; ++i)
        {
            Section target = (Section)i;
            if (NavOf(target) != checkedItem) continue;
            if (target == m_section) return;
            if (!CanOpen(target))
            {
                // Chưa đủ quyền: tab quay về trang đang mở, hỏi mật khẩu; đúng mới chuyển.
                if (auto current = NavOf(m_section))
                {
                    m_navSync = true;
                    current.IsChecked(true);
                    m_navSync = false;
                }
                ui::LoginDialog::ShowLogin([weak = get_weak(), target](bool ok) {
                    auto self = weak.get();
                    if (!self || self->m_shutdown) return;
                    self->UpdateUserState();
                    if (ok && CanOpen(target)) self->ShowSection(target);
                });
                return;
            }
            ShowSection(target);
            return;
        }
    }

    bool MainWindow::CanOpen(Section section)
    {
        sca::EUserLevel need = section == Section::VisionEdit ? sca::EUserLevel::Engineer : sca::EUserLevel::Operator;
        return sca::UserAccess::IsAtLeast(need);
    }

    void MainWindow::LeaveLockedSection()
    {
        if (!CanOpen(m_section)) ShowSection(Section::Main);
    }

    // SH-11 RESET — chung một đường với nút RESET của bảng alarm, trang Alarm và nút RESET vật lý.
    void MainWindow::OnResetClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        sca::McControl::RequestReset();
    }

    // Login → hộp mật khẩu Engineer; đang là Engineer → đăng xuất ngay (không hỏi lại), rời trang bị khoá.
    void MainWindow::OnUserClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        if (sca::UserAccess::IsAtLeast(sca::EUserLevel::Engineer))
        {
            sca::UserAccess::Logout("by user");
            LeaveLockedSection();
            UpdateUserState();
            return;
        }
        ui::LoginDialog::ShowLogin([weak = get_weak()](bool) {
            if (auto self = weak.get(); self && !self->m_shutdown) self->UpdateUserState();
        });
    }

    void MainWindow::UpdateUserState()
    {
        m_userVersion = sca::UserAccess::Version();
        bool engineer = sca::UserAccess::IsAtLeast(sca::EUserLevel::Engineer);
        ui::SetText(UserText(), engineer ? L"Logout" : L"Login");
        // Xanh đặc khi đang ở cấp cao — nhìn là biết máy đang mở quyền (như Origin xong ở trang Main: không quầng).
        ui::SetTint(UserButton(), engineer ? ui::Tint::Blue : ui::Tint::None, false);
        ToolTipService::SetToolTip(UserButton(), box_value(engineer ? ui::H(sca::Loc::F("User.LoggedInTip", sca::UserAccess::IdleLogoutMin()))
                                                                    : ui::T("User.LoginTip")));
        VisionEditLockIcon().Glyph(engineer ? L"\xE785" : L"\xE72E");   // Unlock / Lock
        ToolTipService::SetToolTip(NavVisionEdit(), engineer ? nullptr : box_value(ui::T("User.VisionEditLockedTip")));
    }

    // Tự đăng xuất (Tuấn 2026-10-21: 10 phút không thao tác). GetLastInputInfo đếm mọi chuột/phím/chạm của phiên Windows —
    // kể cả bên trong control VisionMaster (popup ActiveX, sự kiện không đi qua cây XAML), nên người đang sửa Flow không bị
    // đăng xuất giữa chừng. Không xét lúc có hộp thoại đang mở hay Vision Edit đang chạy một chuỗi hiệu chuẩn: chuỗi chạy
    // xong mới tính, đủ giờ thì đăng xuất ngay sau đó.
    void MainWindow::CheckIdleLogout()
    {
        if (sca::UserAccess::Level() == sca::EUserLevel::Operator || ui::ModalOverlay::AnyOpen()) return;
        if (m_coreReady && get_self<VisionEditPage>(PageVisionEdit())->IsBusy()) return;
        LASTINPUTINFO input{ sizeof(LASTINPUTINFO), 0 };
        if (!GetLastInputInfo(&input)) return;
        auto idle = std::chrono::milliseconds(GetTickCount() - input.dwTime);   // DWORD: tự đúng khi bộ đếm quay vòng
        if (sca::UserAccess::LogoutIfIdle(idle)) LeaveLockedSection();
    }

    void MainWindow::OnCoreReady()
    {
        m_coreReady = true;
        ui::Show(StartupPanel(), false);
        for (int i = 0; i < (int)Section::Count; ++i)
            if (auto page = PageOf((Section)i)) page->OnCoreReady();
        if (auto page = PageOf(m_section))
        {
            page->OnShown(true);
            page->OnTick();
        }
        if (ui::AppHost::Options().ShotMode()) RunScreenshots();
    }

    void MainWindow::UpdateShell()
    {
        sca::McStatus& status = sca::McControl::Status();

        ui::SetText(MachineNameText(), sca::McDataManager::SysData()->MachineName);
        ui::SetText(ModelText(), "Model: " + status.ModelName());

        // SH-1 nền thanh tiêu đề theo đèn tháp (nguồn sự thật, cùng màu đèn tháp vật lý; nháy bỏ qua).
        sca::ETowerLamp lamp = status.TowerLamp();
        if (m_lastTowerLamp != (int)lamp)
        {
            m_lastTowerLamp = (int)lamp;
            TitleBarArea().Background(ui::Solid(ui::TowerLampTintColor(lamp)));
            TitleBarArea().BorderBrush(ui::Solid(ui::TowerLampColor(lamp)));
        }

        // SH-5 toạ độ 7 trục — danh sách trục chỉ đọc được sau McControl::Init.
        if (m_coreReady)
        {
            const auto& axes = sca::McControl::Motion().AxisList();
            for (size_t i = 0; i < m_axisValues.size() && i < axes.size(); ++i)
                if (axes[i]) ui::SetText(m_axisValues[i], ui::Num(axes[i]->CurrentPos(), 3));
        }

        // SH-13 / SH-14 thanh trạng thái.
        int sim = status.Simulation() || sca::McDataManager::SysData()->SimulationMode ? 1 : 0;
        if (sim != m_lastSim)
        {
            m_lastSim = sim;
            ui::Show(SimBadge(), sim != 0);
        }
        auto lampBrush = [](bool on) { return ui::Res(on ? L"StatusOnBrush" : L"StatusOffBrush"); };
        auto setLamp = [&](winrt::Microsoft::UI::Xaml::Shapes::Ellipse const& e, bool on) {
            auto want = lampBrush(on);
            if (e.Fill() != want) e.Fill(want);
        };
        setLamp(MotionLamp(), status.MotionConnected());
        setLamp(IoLamp(), status.IoConnected());
        setLamp(VisionLamp(), status.VisionConnected());

        bool hasAlarms = sca::AlarmManager::ActiveCount() > 0;

        // SH-11 RESET — cam đặc + quầng khi không có alarm, đỏ + quầng khi có.
        ui::SetTint(ShellResetButton(), hasAlarms ? ui::Tint::Red : ui::Tint::Caution, true);

        // SH-10 mục Alarm — có alarm thì nền đỏ + quầng sáng, chữ và biểu tượng trắng.
        if (m_alarmTabState != (hasAlarms ? 1 : 0))
        {
            m_alarmTabState = hasAlarms ? 1 : 0;
            AlarmTabFill().Background(hasAlarms ? ui::Res(L"RedOnBrush") : nullptr);
            ui::SetGlow(AlarmTabGlow(), hasAlarms ? 0xFFFF4D5E : 0);
            if (hasAlarms)
            {
                AlarmTabText().Foreground(ui::Res(L"TextOnAccentBrush"));
                AlarmTabIcon().Foreground(ui::Res(L"TextOnAccentBrush"));
            }
            else
            {
                AlarmTabText().ClearValue(TextBlock::ForegroundProperty());
                AlarmTabIcon().ClearValue(IconElement::ForegroundProperty());
            }
        }

        CheckIdleLogout();
        if (m_userVersion != sca::UserAccess::Version()) UpdateUserState();

        UpdateBanner();
        UpdateAlarmPanel();
    }

    // ══════════ SH-16..18 Banner alarm — nổi giữa cửa sổ, không chiếm chỗ trong bố cục ══════════
    // Chỉ to bằng nội dung, không có lớp nền phủ, nên phần còn lại vẫn bấm được — khác bảng chi tiết, CỐ Ý chặn hết để
    // bắt xử lý ngay. InfoBar nằm trong Popup không ràng buộc (HWND riêng) để nổi trên cả control VisionMaster.
    void MainWindow::BuildBanner()
    {
        m_banner = InfoBar();
        m_banner.Severity(InfoBarSeverity::Error);
        m_banner.IsOpen(true);
        m_banner.IsClosable(true);
        m_banner.MinWidth(440);
        m_banner.MaxWidth(900);
        // Nền ĐẶC đỏ, chữ trắng, viền trắng 1 px (lightweight styling + thuộc tính trực tiếp).
        auto white = ui::Solid(0xFFFFFFFF);
        auto red = ui::Res(L"AlarmBannerBrush");
        auto res = m_banner.Resources();
        res.Insert(box_value(L"InfoBarErrorSeverityBackgroundBrush"), red);
        res.Insert(box_value(L"InfoBarTitleForeground"), white);
        res.Insert(box_value(L"InfoBarMessageForeground"), white);
        res.Insert(box_value(L"InfoBarErrorSeverityIconBackground"), white);
        res.Insert(box_value(L"InfoBarErrorSeverityIconForeground"), red);
        m_banner.Background(red);
        m_banner.Foreground(white);
        m_banner.BorderBrush(white);
        m_banner.BorderThickness(ThicknessHelper::FromUniformLength(1));
        ToolTipService::SetToolTip(m_banner, nullptr);

        Button details;   // SH-17
        details.Content(box_value(ui::T("Shell.Details")));
        details.Click([this](auto&&, auto&&) { ToggleAlarmPanel(); });
        m_banner.ActionButton(details);

        // SH-18 ✕ = ẩn: dừng đồng hồ, ẩn ngay; alarm vẫn còn, hiện lại khi có Error/Fatal mới.
        m_banner.CloseButtonClick([this](auto&&, auto&&) { HideBanner(); });
        m_banner.Closing([](InfoBar const& sender, InfoBarClosingEventArgs const& e) {
            e.Cancel(true);   // InfoBar tự đóng thì lần sau không mở lại được — Popup mới là thứ ẩn/hiện
            (void)sender;
        });
        m_banner.SizeChanged([this](auto&&, auto&&) { PositionBanner(); });

        // Popup là cây hiển thị riêng, không thừa hưởng font của cửa sổ chính.
        ContentControl host;
        host.FontFamily(FontFamily(L"Segoe UI"));
        host.Content(m_banner);
        host.Shadow(ThemeShadow());
        host.Translation({ 0, 0, 28 });

        m_bannerPopup = Popup();
        m_bannerPopup.XamlRoot(Root().XamlRoot());
        m_bannerPopup.ShouldConstrainToRootBounds(false);
        m_bannerPopup.IsLightDismissEnabled(false);
        m_bannerPopup.Child(host);

        // Tự ẩn sau 10 s nếu không có alarm mới — đủ để đọc, không đứng chắn thao tác lâu.
        m_bannerTimer = DispatcherQueue().CreateTimer();
        m_bannerTimer.Interval(std::chrono::seconds(10));
        m_bannerTimer.IsRepeating(false);
        m_bannerTimer.Tick([this](auto&&, auto&&) { HideBanner(); });
    }

    void MainWindow::HideBanner()
    {
        if (m_bannerTimer) m_bannerTimer.Stop();
        m_bannerVisible = false;
        UpdateBanner();
    }

    void MainWindow::OnAlarmRaised(sca::AlarmData const& alarm)
    {
        if (m_shutdown) return;
        // Chỉ Error/Fatal mới bật banner (Tuấn 2026-09-29): Warning thì máy vẫn chạy tiếp — vẫn nằm trong danh sách alarm
        // và nút RESET vẫn sáng.
        if (alarm.Level < sca::EAlarmLevel::Error) return;
        m_bannerDisplay = alarm.Display();   // banner hiện đúng alarm vừa bật nó, không phải dòng đầu danh sách
        m_bannerVisible = true;
        m_bannerTimer.Stop();
        m_bannerTimer.Start();
        UpdateBanner();
    }

    void MainWindow::UpdateBanner()
    {
        if (!m_bannerPopup) return;
        int count = sca::AlarmManager::ActiveCount();
        // Điều kiện hiện thật sự: còn alarm VÀ chưa bị ẩn/hết giờ. Chế độ chụp ảnh không mở cửa sổ nổi nào.
        bool show = count > 0 && m_bannerVisible && !ui::AppHost::Options().ShotMode();
        if (!show && count == 0 && m_bannerVisible)
        {
            m_bannerVisible = false;
            m_bannerTimer.Stop();
        }
        if (show)
        {
            winrt::hstring title = ui::H(sca::Loc::F("Shell.ActiveAlarmCount", count));
            if (m_banner.Title() != title) m_banner.Title(title);
            winrt::hstring message = ui::H(m_bannerDisplay);
            if (m_banner.Message() != message) m_banner.Message(message);
        }
        if (m_bannerPopup.IsOpen() != show)
        {
            m_bannerPopup.IsOpen(show);
            if (show) PositionBanner();
        }
    }

    void MainWindow::PositionBanner()
    {
        if (!m_bannerPopup || !m_bannerPopup.IsOpen()) return;
        auto size = Root().XamlRoot().Size();
        double width = m_banner.ActualWidth() > 0 ? m_banner.ActualWidth() : m_banner.MinWidth();
        double height = m_banner.ActualHeight() > 0 ? m_banner.ActualHeight() : 64.0;
        m_bannerPopup.HorizontalOffset(std::max(0.0, (size.Width - width) / 2));
        m_bannerPopup.VerticalOffset(std::max(0.0, (size.Height - height) / 2));
    }

    // ══════════ SH-19..22 Bảng alarm đang tác động — kiểu ContentDialog (ModalOverlay để nổi trên VisionMaster) ══════════
    void MainWindow::ToggleAlarmPanel()
    {
        if (m_alarmPanel.IsOpen())
        {
            m_alarmPanel.Close();
            return;
        }

        Border card;
        card.Width(900);
        card.MaxHeight(560);
        card.Height(560);
        card.Background(ui::Res(L"SurfaceBrush"));
        card.BorderBrush(ui::Res(L"CardStrokeBrush"));
        card.BorderThickness(ThicknessHelper::FromUniformLength(1));
        card.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));

        Grid layout;
        RowDefinition titleRow; titleRow.Height(GridLengthHelper::Auto());
        RowDefinition tableRow; tableRow.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        RowDefinition barRow; barRow.Height(GridLengthHelper::Auto());
        layout.RowDefinitions().Append(titleRow);
        layout.RowDefinitions().Append(tableRow);
        layout.RowDefinitions().Append(barRow);

        TextBlock title;
        title.Text(ui::T("Shell.ActiveAlarmList"));
        title.FontSize(20);
        title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        title.Margin(ThicknessHelper::FromLengths(24, 20, 24, 12));

        m_alarmPanelTable = ui::AlarmTable();
        FrameworkElement table = m_alarmPanelTable.Build(ui::AlarmTableKind::Active);
        table.Margin(ThicknessHelper::FromLengths(16, 0, 16, 16));

        Border bar;
        bar.Background(ui::Res(L"BackgroundBrush"));
        bar.BorderBrush(ui::Res(L"DividerBrush"));
        bar.BorderThickness(ThicknessHelper::FromLengths(0, 1, 0, 0));
        bar.CornerRadius(CornerRadius{ 0, 0, 8, 8 });
        bar.Padding(ThicknessHelper::FromLengths(24, 16, 24, 16));
        Grid barLayout;
        ColumnDefinition messageColumn; messageColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        ColumnDefinition actionColumn; actionColumn.Width(GridLengthHelper::Auto());
        barLayout.ColumnDefinitions().Append(messageColumn);
        barLayout.ColumnDefinitions().Append(actionColumn);
        m_alarmPanelMessage = TextBlock();   // SH-20
        m_alarmPanelMessage.VerticalAlignment(VerticalAlignment::Center);
        m_alarmPanelMessage.Foreground(ui::Res(L"CriticalBrush"));
        m_alarmPanelMessage.Margin(ThicknessHelper::FromLengths(0, 0, 16, 0));
        m_alarmPanelMessage.MaxWidth(480);
        m_alarmPanelMessage.HorizontalAlignment(HorizontalAlignment::Left);
        m_alarmPanelMessage.TextWrapping(TextWrapping::Wrap);
        StackPanel actions;
        actions.Orientation(Orientation::Horizontal);
        actions.Spacing(8);
        Button reset;   // SH-21
        reset.Content(box_value(L"RESET"));
        reset.Width(120);
        reset.Height(40);
        reset.Style(ui::ResStyle(L"PrimaryButton"));
        reset.Click([](auto&&, auto&&) { sca::McControl::RequestReset(); });
        Button close;   // SH-22
        close.Content(box_value(ui::T("Shell.Close")));
        close.Width(100);
        close.Height(40);
        close.Click([this](auto&&, auto&&) { m_alarmPanel.Close(); });
        actions.Children().Append(reset);
        actions.Children().Append(close);
        Grid::SetColumn(m_alarmPanelMessage, 0);
        Grid::SetColumn(actions, 1);
        barLayout.Children().Append(m_alarmPanelMessage);
        barLayout.Children().Append(actions);
        bar.Child(barLayout);

        Grid::SetRow(title, 0);
        Grid::SetRow(table, 1);
        Grid::SetRow(bar, 2);
        layout.Children().Append(title);
        layout.Children().Append(table);
        layout.Children().Append(bar);
        card.Child(layout);

        m_alarmPanelVersion = ~0ull;
        m_alarmPanel.Open(card);
        UpdateAlarmPanel();
    }

    void MainWindow::UpdateAlarmPanel()
    {
        if (!m_alarmPanel.IsOpen()) return;
        uint64_t version = sca::AlarmManager::Version();
        if (version != m_alarmPanelVersion)
        {
            m_alarmPanelVersion = version;
            m_alarmPanelTable.Update(sca::AlarmManager::ActiveAlarms());
        }
        if (m_alarmPanelMessage) ui::SetText(m_alarmPanelMessage, sca::McControl::Status().Message());
    }

    void MainWindow::OnTick()
    {
        // Một lời gọi trong nhịp (vd. control VisionMaster) bơm thông điệp thì nhịp kế có thể tới lồng vào: bỏ nhịp đó.
        if (m_shutdown || m_inTick) return;
        m_inTick = true;
        struct Reset { bool& flag; ~Reset() { flag = false; } } reset{ m_inTick };
        if (!m_coreReady && ui::AppHost::CoreReady()) OnCoreReady();
        UpdateShell();
        if (m_coreReady)
            if (auto page = PageOf(m_section)) page->OnTick();
    }

    void MainWindow::OnAppWindowChanged(Microsoft::UI::Windowing::AppWindow const&,
                                        Microsoft::UI::Windowing::AppWindowChangedEventArgs const& e)
    {
        if (!m_hwnd || m_shutdown) return;
        if (e.DidPositionChange() || e.DidSizeChange())
        {
            // Popup sở hữu (control VisionMaster, lớp phủ) không tự đi theo cửa sổ chủ.
            if (m_coreReady)
                if (auto page = PageOf(m_section)) page->OnWindowMoved();
            m_alarmPanel.Resize();
            PositionBanner();
        }
        if (e.DidSizeChange()) ApplyCaptionInset();
    }

    // Trình tự tắt (UI-SPEC §1, VmWinUiProbe): timer → popup XAML → hộp thoại → control VisionMaster (OnClosing của các
    // trang) → McControl::Shutdown → VisionMasterDevice::DisposeSdk → lưu dữ liệu (AppHost::Shutdown).
    void MainWindow::Shutdown()
    {
        if (m_shutdown) return;
        m_shutdown = true;

        if (m_timer)
        {
            m_timer.Stop();
            m_timer = nullptr;
        }
        if (m_bannerTimer) m_bannerTimer.Stop();
        sca::AlarmManager::AlarmRaised().Unsubscribe(m_alarmRaisedToken);
        sca::McControl::AutoModeRefused().Unsubscribe(m_autoRefusedToken);
        ui::ModalOverlay::ModalChanged().Unsubscribe(m_modalToken);
        ui::StopActiveJogs();

        if (m_bannerPopup) m_bannerPopup.IsOpen(false);
        m_alarmPanel.Close();
        ui::LoginDialog::Close();
        ui::Dialogs::CloseAll();

        for (int i = 0; i < (int)Section::Count; ++i)
            if (auto page = PageOf((Section)i)) page->OnClosing();

        ui::AppHost::Shutdown();
    }

    void MainWindow::OnClosed(WF::IInspectable const&, WindowEventArgs const&)
    {
        Shutdown();
    }

    // --shot <thư mục>: chụp từng trang bằng RenderTargetBitmap (không cần cửa sổ nằm trong màn hình, không đụng chuột/bàn
    // phím) rồi tự thoát. Bắt đầu sau OnCoreReady. Trang có nhiều trạng thái hiển thị (IPage::ShotVariants) được chụp thêm
    // <tên>-2, <tên>-3, …
    winrt::fire_and_forget MainWindow::RunScreenshots()
    {
        using namespace winrt::Windows::Graphics::Imaging;
        using namespace winrt::Windows::Storage::Streams;
        using winrt::Microsoft::UI::Xaml::Media::Imaging::RenderTargetBitmap;

        auto lifetime = get_strong();
        winrt::apartment_context uiContext;
        auto dir = ::base::U8Path(ui::AppHost::Options().ScreenshotDir);
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);

        static const wchar_t* names[] = { L"1-main", L"2-manual", L"3-teaching", L"4-setting", L"5-visionedit", L"6-alarm" };
        for (int i = 0; i < (int)Section::Count; ++i)
        {
            auto page = PageOf((Section)i);
            int variants = page ? page->ShotVariants() : 1;
            for (int variant = 0; variant < variants; ++variant)
            {
                if (variant == 0) ShowSection((Section)i);
                else page->ShowShotVariant(variant);
                co_await winrt::resume_after(std::chrono::milliseconds(variant == 0 ? 1200 : 400));
                co_await uiContext;
                if (m_shutdown) co_return;

                RenderTargetBitmap bitmap;
                co_await bitmap.RenderAsync(Root());
                IBuffer pixels = co_await bitmap.GetPixelsAsync();

                InMemoryRandomAccessStream stream;
                BitmapEncoder encoder = co_await BitmapEncoder::CreateAsync(BitmapEncoder::PngEncoderId(), stream);
                encoder.SetPixelData(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied,
                                     (uint32_t)bitmap.PixelWidth(), (uint32_t)bitmap.PixelHeight(), 96.0, 96.0,
                                     winrt::array_view<const uint8_t>(pixels.data(), pixels.data() + pixels.Length()));
                co_await encoder.FlushAsync();

                DataReader reader(stream.GetInputStreamAt(0));
                uint32_t size = (uint32_t)stream.Size();
                co_await reader.LoadAsync(size);
                std::vector<uint8_t> bytes(size);
                reader.ReadBytes(bytes);

                std::wstring file = std::wstring(names[i]) + (variant == 0 ? L"" : L"-" + std::to_wstring(variant + 1)) + L".png";
                std::ofstream out(dir / file, std::ios::binary | std::ios::trunc);
                out.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize)bytes.size());
            }
        }

        co_await uiContext;
        Close();
    }
}
