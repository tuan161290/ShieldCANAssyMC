#pragma once
#include "MainPage.g.h"

#include <array>
#include <chrono>
#include <memory>
#include <string>

#include "Ui/Controls.h"
#include "Ui/MainPageCamFrame.h"
#include "Ui/Page.h"
#include "Ui/TrayCard.h"

namespace sca { struct ReadoutCard; class VisionMasterDevice; }

namespace winrt::ShieldCanAssy::implementation
{
    /// <summary>
    /// Trang Main (UI-SPEC §2): card Operation (công tắc AUTO/MANUAL, RUN, STOP, Origin, Init, RESET, Mute, các dải nhắc),
    /// card Production (bộ đếm, NG, Cycle/Tack Time, UPH, bước, bù gần nhất), hai card tray, ba khung CAM và log trực
    /// tiếp. Bản WPF: Views\MainView.xaml(.cs) + ViewModels\MainViewModel.cs + TrayGridViewModel.cs — property của
    /// view-model là thành viên ở đây, cập nhật trong OnTick().
    ///
    /// <para>Khung CAM: ảnh gần nhất của Vision luôn được cập nhật; khi vision là VisionMaster thật (VisionConnect không mô
    /// phỏng, không chế độ --shot) thêm một VmRenderControl (ui::VmPane) phủ trên ảnh, gắn vào Flow/module đặt ở trang
    /// Setting như MainView.xaml.cs.</para>
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
        enum { CamTool1 = 0, CamTool2 = 1, CamTop = 2 };

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
        void UpdateCams();
        /// <summary>Bảng của hai khung bottom-up theo kiểu đang chọn (Ofs/Tgt ↔ bảng tách Đích) — SyncToolCards.</summary>
        void SyncToolCards();
        std::shared_ptr<const sca::ReadoutCard> Shown(std::shared_ptr<const sca::ReadoutCard> const& card) const;
        void InjectShotSample();

        // ── VisionMaster: mọi lời gọi vào control đi qua QueueVm (ngoài sự kiện XAML, xem Ui/VmPane.h) ──
        void QueueVm(bool force);
        void EnsureVmPanes();
        void ApplyPaneVisibility();
        void UpdateRenderBindings(bool force);
        bool BindOne(int index, sca::VisionMasterDevice& device, std::string const& path, int loadCount);
        /// <summary>Nền đặc màu card + thanh công cụ theo nút ở góc card (ApplyRenderLook của WPF).</summary>
        void ApplyRenderLook(int index);
        void ToggleToolbar(int index);

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
        std::array<ui::MainCamFrame, 3> m_cams;
        ui::LogListView m_log;
        bool m_logShown{ false };   // mở app là ẩn (Tuấn 2026-10-21) — khớp trạng thái đầu trong MainPage.xaml
        Microsoft::UI::Xaml::Media::Animation::Storyboard m_logAnim{ nullptr };
        /// <summary>Bảng "Not triggered" (WaitingCard của MainViewModel).</summary>
        std::shared_ptr<const sca::ReadoutCard> m_waitingCard;
        /// <summary>Bấm tiêu đề bảng bottom-up: Đích tách phần — chung cho cả hai tay, không lưu.</summary>
        bool m_showBreakdown{ false };
        std::shared_ptr<const sca::ReadoutCard> m_shotSample;

        // VisionMaster
        bool m_vmAttempted{ false };
        bool m_vmActive{ false };
        bool m_vmQueued{ false };
        bool m_vmForce{ false };
        bool m_vmBusy{ false };
        std::chrono::steady_clock::time_point m_lastBindTry{};
        std::string m_loggedVisionDiagnostic;
        std::string m_loggedRenderLookError;
    };
}

namespace winrt::ShieldCanAssy::factory_implementation
{
    struct MainPage : MainPageT<MainPage, implementation::MainPage>
    {
    };
}
