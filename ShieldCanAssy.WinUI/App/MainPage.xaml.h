#pragma once
#include "MainPage.g.h"

#include <array>
#include <chrono>
#include <memory>
#include <string>

#include "Ui/CamViews.h"
#include "Ui/Controls.h"
#include "Ui/Page.h"
#include "Ui/TrayCard.h"

namespace winrt::ShieldCanAssy::implementation
{
    /// <summary>
    /// Trang Main (UI-SPEC §2): card Operation (công tắc AUTO/MANUAL, RUN, STOP, Origin, Init, RESET, Mute, các dải nhắc),
    /// card Production (bộ đếm, NG, Cycle/Tack Time, UPH, bước, bù gần nhất), hai card tray, ba khung CAM và log trực
    /// tiếp. Bản WPF: Views\MainView.xaml(.cs) + ViewModels\MainViewModel.cs + TrayGridViewModel.cs — property của
    /// view-model là thành viên ở đây, cập nhật trong OnTick().
    ///
    /// <para>Khung CAM (ui::CamViews, dùng chung với Vision Edit): ảnh gần nhất của Vision luôn được cập nhật; khi vision là
    /// VisionMaster thật (VisionConnect không mô phỏng, không chế độ --shot) thêm một VmRenderControl (ui::VmPane) phủ trên
    /// ảnh, gắn vào Flow/module đặt ở trang Setting như MainView.xaml.cs.</para>
    /// </summary>
    struct MainPage : MainPageT<MainPage>, ui::IPage
    {
        MainPage() = default;

        void OnRootLoaded(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnModeClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnModeTrackSizeChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&);
        void OnRunClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnStopClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        winrt::fire_and_forget OnOriginClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnResetClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnMuteClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnClearCountersClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnToggleLogClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        void OnCoreReady() override;
        void OnTick() override;
        void OnShown(bool visible) override;
        void OnWindowMoved() override;
        void OnModalChanged(bool modalOpen) override;
        void OnClosing() override;

    private:
        void UpdateOperation();
        void UpdateRunButton(int state);
        /// <summary>Con trượt AUTO/MANUAL sang nửa ứng với chế độ ĐANG áp dụng — trượt 150 ms khi chế độ đổi, đặt thẳng khi
        /// đổi cỡ (PlaceModeThumb của MainView.xaml.cs).</summary>
        void PlaceModeThumb(bool animate);
        void UpdateCounters();
        /// <summary>MainViewModel.RefreshAssyTrayClock — Cycle/Last Cycle/Tack/Last Tack Time và UPH.</summary>
        void RefreshAssyTrayClock();
        void UpdateTrays();
        void SyncTray(bool input);

        bool m_loaded{ false };
        bool m_coreReady{ false };
        bool m_visible{ false };
        bool m_closing{ false };

        // Operation
        int m_modeShown{ -1 };      // EMcMode con trượt đang đứng; -1 = chưa đặt
        int m_runVisual{ -1 };      // 0 đứng (xám), 1 Armed (xanh nháy), 2 Running (xanh + quầng)
        Microsoft::UI::Xaml::Media::Animation::Storyboard m_armedBlink{ nullptr };
        Microsoft::UI::Xaml::Media::Animation::Storyboard m_modeSlide{ nullptr };
        Microsoft::UI::Xaml::Media::Animation::DoubleAnimation m_modeSlideX{ nullptr };
        int m_ngStreakShown{ -1 };
        /// <summary>Popup Origin đang mở hoặc đang Servo On + nhả phanh trên luồng nền — bấm Origin lại lúc đó bị bỏ qua.</summary>
        std::shared_ptr<std::atomic<bool>> m_originPrep = std::make_shared<std::atomic<bool>>(false);

        // Tray + CAM + log
        ui::TrayCard m_inputCard;
        ui::TrayCard m_assyCard;
        ui::CamViews m_cams{ "Main" };
        ui::LogListView m_log;
        bool m_logShown{ false };   // mở app là ẩn (Tuấn 2026-10-21) — khớp trạng thái đầu trong MainPage.xaml
        Microsoft::UI::Xaml::Media::Animation::Storyboard m_logAnim{ nullptr };
    };
}

namespace winrt::ShieldCanAssy::factory_implementation
{
    struct MainPage : MainPageT<MainPage, implementation::MainPage>
    {
    };
}
