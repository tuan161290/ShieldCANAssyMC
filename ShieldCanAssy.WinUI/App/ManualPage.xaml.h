#pragma once
#include "ManualPage.g.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "Ui/Controls.h"
#include "Ui/ManualPageAxisRow.h"
#include "Ui/Page.h"

namespace sca { class FastechAxis; class IIoDevice; }

namespace winrt::ShieldCanAssy::implementation
{
    /// <summary>
    /// Trang Manual (UI-SPEC §3): điều khiển đầy đủ từng trục (SVO/RST/Brake Off/Origin/Jog/Inching/Move/Stop), bảng đèn
    /// I/O, bảng mô phỏng và chẩn đoán phần cứng — bản WinUI của ManualView + ManualViewModel + AxisRowVm (WPF Assy).
    /// Hợp đồng với MainWindow: xem Ui/Page.h.
    ///
    /// <para>Bảng mô phỏng không phải đồ chơi: nó là cách duy nhất chạy được các nhánh EMG, rèm sáng và mất kết nối mà
    /// không phải rút cáp máy thật.</para>
    /// </summary>
    struct ManualPage : ManualPageT<ManualPage>, ui::IPage
    {
        ManualPage() = default;

        void OnRootLoaded(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        void OnStopAllClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnResetClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnServoOnAllClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnBrakeAllClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnOriginClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnCamBtmLampClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnCamTopLampClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        void OnSimStartClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSimStopClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSimEmgClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSimCurtainClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSimAirClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSimTray1Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSimTray2Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        // ── ui::IPage ──
        void OnCoreReady() override;
        void OnTick() override;
        /// <summary>--shot: ảnh 2 cuộn cột trái xuống cuối (SIM PANEL, HW DIAGNOSTICS).</summary>
        int ShotVariants() const override { return 2; }
        void ShowShotVariant(int index) override;

    private:
        void BuildStatic();
        void BuildAxisRows();
        void BuildSimAxisGrid();
        void UpdateCommands(const std::shared_ptr<sca::IIoDevice>& io);
        void UpdateBanners();
        void UpdateIo(const std::shared_ptr<sca::IIoDevice>& io);
        void UpdateDiagnostics(uint32_t di, uint32_t doo);

        bool m_loaded{ false };
        bool m_coreReady{ false };
        bool m_rowsBuilt{ false };
        ui::ManualLampBrushes m_lampBrushes;

        // ── Bảng trục ──
        std::vector<std::shared_ptr<ui::manualpage::AxisRow>> m_rows;
        /// <summary>Servo On (tất cả) đang chạy trên luồng nền — bấm lại lúc đó bị bỏ qua.</summary>
        std::shared_ptr<std::atomic<bool>> m_servoAllBusy = std::make_shared<std::atomic<bool>>(false);

        // ── Trạng thái đã hiện lần trước (-1 / "\x01" = chưa hiện) ──
        int m_shownAllServo{ -1 };
        int m_shownAllBrake{ -1 };
        int m_shownOrgOk{ -1 };
        int m_shownZSafe{ -1 };
        std::string m_shownBlockReason{ "\x01" };
        std::string m_shownMessage{ "\x01" };
        const void* m_shownIo{ reinterpret_cast<const void*>(1) };

        // ── Bảng I/O ──
        ui::IoLampList m_diList;
        ui::IoLampList m_doList;

        // ── Chẩn đoán ──
        std::string m_shownRawDi{ "\x01" };
        std::string m_shownRawDo{ "\x01" };
        std::vector<std::shared_ptr<sca::FastechAxis>> m_fastechAxes;
        std::vector<Microsoft::UI::Xaml::Controls::TextBlock> m_fastechLines;
        std::vector<std::string> m_shownFastechLines;
    };
}

namespace winrt::ShieldCanAssy::factory_implementation
{
    struct ManualPage : ManualPageT<ManualPage, implementation::ManualPage>
    {
    };
}
