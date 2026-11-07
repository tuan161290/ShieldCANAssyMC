#pragma once
#include "MainWindow.g.h"

#include "Infor/AlarmData.h"
#include "Ui/AlarmTable.h"
#include "Ui/Dialogs.h"
#include "Ui/Page.h"
#include "Ui/StartupBoard.h"

namespace winrt::ShieldCanAssy::implementation
{
    /// <summary>
    /// Shell (UI-SPEC §1): thanh tiêu đề (tên máy, model, toạ độ 7 trục, nền theo đèn tháp), điều hướng 6 tab
    /// (RadioButton kiểu NavTabButtonStyle của WPF) + RESET, thanh trạng thái (SIMULATION, đèn Motion/IO/Vision, gốc dữ liệu), banner alarm (InfoBar trong Popup
    /// có cửa sổ riêng) và bảng alarm đang tác động (ModalOverlay). Một DispatcherQueueTimer 100 ms đọc trạng thái từ Core
    /// và gọi OnTick() của trang đang hiện — thay cho binding tới McStatus (Bindable) của bản WPF.
    /// </summary>
    struct MainWindow : MainWindowT<MainWindow>
    {
        /// <summary>Thứ tự khớp thứ tự trang trong MainWindow.xaml và ShellSection của WPF.</summary>
        enum class Section { Main = 0, Manual, Teaching, Setting, VisionEdit, Alarm, Count };

        MainWindow();

        void OnRootLoaded(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnNavChecked(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnResetClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnUserClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnStartupContinueClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        void ShowSection(Section section);
        /// <summary>Cấp đang đăng nhập đủ mở trang này không (chỉ Vision Edit đòi Engineer — Tuấn 2026-10-21).</summary>
        static bool CanOpen(Section section);
        /// <summary>Vừa xuống cấp (đăng xuất, tự đăng xuất): đang ở trang không còn quyền thì về Main.</summary>
        void LeaveLockedSection();
        /// <summary>Nút Login/Logout và khoá trên tab Vision Edit theo cấp hiện tại.</summary>
        void UpdateUserState();
        /// <summary>Tự đăng xuất khi không thao tác đủ số phút (UserAccess::LogoutIfIdle).</summary>
        void CheckIdleLogout();
        void OnCoreReady();
        void UpdateStartup();
        /// <summary>Đóng dashboard khởi động: mở khoá shell, trả popup VisionMaster về chỗ, banner alarm được hiện.</summary>
        void FinishStartup();
        void UpdateShell();
        void BuildAxisCells();
        void ApplyCaptionInset();
        ui::IPage* PageOf(Section section);
        Microsoft::UI::Xaml::UIElement ElementOf(Section section);
        Microsoft::UI::Xaml::Controls::RadioButton NavOf(Section section);

        // ── Banner alarm (nổi giữa cửa sổ, không chặn thao tác) và bảng chi tiết alarm (modal) ──
        void BuildBanner();
        void OnAlarmRaised(sca::AlarmData const& alarm);
        void HideBanner();
        void UpdateBanner();
        void PositionBanner();
        void ToggleAlarmPanel();
        void UpdateAlarmPanel();

        void OnTick();
        void OnAppWindowChanged(Microsoft::UI::Windowing::AppWindow const&, Microsoft::UI::Windowing::AppWindowChangedEventArgs const&);
        void OnClosed(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::WindowEventArgs const&);
        void Shutdown();
        winrt::fire_and_forget RunScreenshots();

        HWND m_hwnd{};
        Section m_section{ Section::Main };
        bool m_shutdown{ false };
        bool m_coreReady{ false };
        bool m_startupActive{ false };
        ui::StartupBoard m_startup;
        /// <summary>Đang đặt tab điều hướng từ code — Checked lúc đó không phải người bấm.</summary>
        bool m_navSync{ false };
        // DispatcherQueueTimer, KHÔNG phải DispatcherTimer của XAML: Tick của DispatcherTimer chạy bên trong vùng XAML chặn
        // gọi lồng, mà OnTick các trang có thể gọi control VisionMaster (bơm thông điệp COM) → XAML fail-fast 0xC000027B
        // (dump 06/10). Tick của DispatcherQueueTimer chạy ở vòng thông điệp, ngoài XAML.
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_timer{ nullptr };
        bool m_inTick{ false };
        std::vector<Microsoft::UI::Xaml::Controls::TextBlock> m_axisValues;
        int m_lastTowerLamp{ -1 };
        int m_alarmTabState{ -1 };
        int m_lastSim{ -1 };
        uint64_t m_userVersion{ 0 };

        int m_modalToken{ 0 };
        int m_alarmRaisedToken{ 0 };
        int m_autoRefusedToken{ 0 };

        // Banner: chỉ Error/Fatal mới bật; tự ẩn sau 10 giây (đếm lại mỗi alarm mới); ✕ thì ẩn ngay.
        Microsoft::UI::Xaml::Controls::Primitives::Popup m_bannerPopup{ nullptr };
        Microsoft::UI::Xaml::Controls::InfoBar m_banner{ nullptr };
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_bannerTimer{ nullptr };
        bool m_bannerVisible{ false };
        std::string m_bannerDisplay;

        ui::ModalOverlay m_alarmPanel;
        ui::AlarmTable m_alarmPanelTable;
        Microsoft::UI::Xaml::Controls::TextBlock m_alarmPanelMessage{ nullptr };
        uint64_t m_alarmPanelVersion{ ~0ull };
    };
}

namespace winrt::ShieldCanAssy::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
