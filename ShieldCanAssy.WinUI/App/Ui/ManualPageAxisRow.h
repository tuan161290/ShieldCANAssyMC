#pragma once
// Trang Manual — MỘT HÀNG TRỤC của bảng trục (thay AxisRowVm + AxisRowTemplate của bản WPF Assy, UI-SPEC §3.2 MN-13…25).
// Bản riêng của trang Manual Assy thay cho ui::ManualAxisRow dùng chung (bản đó là của máy Slit: không có nút Brake Off,
// cột khác bề rộng). Mỗi hàng giữ control của mình và giá trị đã hiện lần trước; Update() chỉ đụng control khi giá trị ĐỔI.
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "Ui/Controls.h"

namespace sca { class IAxis; }

namespace ui::manualpage
{
    /// <summary>Bề rộng các cột — PHẢI khớp dòng tiêu đề cột trong ManualPage.xaml (bản WPF: AxisRowTemplate).</summary>
    struct AxisColumns
    {
        static constexpr double Axis = 30;
        static constexpr double Position = 84;
        static constexpr double Limit = 40;
        static constexpr double Servo = 202;
        static constexpr double Origin = 64;
        static constexpr double Jog = 93;
        static constexpr double Inching = 132;
        static constexpr double MoveAbs = 137;
        static constexpr double Stop = 62;
    };

    /// <summary>
    /// Một hàng: Trục | Vị trí | Cữ (POT/NOT) | Servo (SVO toggle, RST, Brake Off — Z1/Z2) | Origin | Jog −/+ | Inching
    /// (bước, −, +) | Move Abs (đích, Move) | Stop.
    ///
    /// <para>Lệnh dài (SVO, RST, Origin, Move, Inching) chạy trên luồng nền; trong lúc lệnh của HÀNG NÀY còn chạy các nút
    /// lệnh dài của hàng bị khoá. Jog, Stop và Brake Off không bao giờ bị khoá.</para>
    ///
    /// <para><b>Brake Off</b> (chỉ Z1/Z2): bấm nhanh (nhả trước 3 s) = nghi thức bình thường
    /// (<c>McFunction::RequestBrakeRelease</c>, đòi ServoOn); GIỮ đủ 3 s = nhả phanh KHẨN CẤP kiểu toggle
    /// (<c>SetEmergencyBrakeRelease(true)</c>), giữ nguyên dù buông tay; đang nhả khẩn cấp mà bấm lại thì khoá ngay lúc nhấn.
    /// Bắt chuột lúc nhấn (ui::AttachHold): con trỏ trượt khỏi nút trong lúc vẫn giữ không bị coi là buông; rê chuột ngang
    /// qua nút không bao giờ nhả phanh.</para>
    /// </summary>
    class AxisRow : public std::enable_shared_from_this<AxisRow>
    {
    public:
        static std::shared_ptr<AxisRow> Create(std::shared_ptr<sca::IAxis> axis, ManualLampBrushes const& brushes);

        explicit AxisRow(std::shared_ptr<sca::IAxis> axis);
        AxisRow(const AxisRow&) = delete;
        AxisRow& operator=(const AxisRow&) = delete;

        winrt::Microsoft::UI::Xaml::FrameworkElement Root() const { return m_root; }
        const std::shared_ptr<sca::IAxis>& Axis() const { return m_axis; }
        const std::string& Name() const { return m_name; }

        /// <summary>Trục có phanh — chỉ Z1/Z2 (bản WPF HasBrakeButton).</summary>
        bool HasBrakeButton() const { return m_hasBrake; }

        /// <summary>Một nhịp OnTick (luồng UI): chỉ đọc getter rẻ của IAxis, chỉ ghi control khi giá trị đổi.</summary>
        void Update();

        /// <summary>Đèn trên nút Brake Off — trang đọc THẲNG DO thật (DoBrakeReleaseZ1/Z2) mỗi nhịp rồi bơm vào đây, không
        /// suy luận lại từ ServoOn/Alarm (bản WPF ManualViewModel.Refresh → BrakeReleased).</summary>
        void SetBrakeReleased(bool doOn);

    private:
        void Build(ManualLampBrushes const& brushes);

        void OnServoToggle();
        void OnResetAlarm();
        void OnHome();
        void OnMove();
        void OnInch(int dir);
        void OnStop();

        void BrakeHoldStart();
        void BrakeHoldTick();
        void BrakeHoldEnd();
        void ApplyBrakeTint();

        /// <summary>Chạy một lệnh dài của hàng trên luồng nền; bỏ qua nếu hàng đang có lệnh chạy.</summary>
        void RunBlocking(std::function<void()> work);
        void ApplyBusy(bool busy);

        std::shared_ptr<sca::IAxis> m_axis;
        std::string m_name;
        bool m_hasBrake{ false };
        bool m_hasHardLimit{ true };
        /// <summary>Cờ bận của hàng — chia sẻ với việc nền (không bắt <c>this</c> vào luồng nền).</summary>
        std::shared_ptr<std::atomic<bool>> m_busy = std::make_shared<std::atomic<bool>>(false);

        winrt::Microsoft::UI::Xaml::Controls::Border m_root{ nullptr };
        winrt::Microsoft::UI::Xaml::Documents::Run m_posRun{ nullptr };
        winrt::Microsoft::UI::Xaml::Documents::Run m_unitRun{ nullptr };
        ManualLamp m_pot;
        ManualLamp m_not;
        winrt::Microsoft::UI::Xaml::Controls::Button m_svoButton{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Button m_rstButton{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Button m_brakeButton{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Button m_homeButton{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Button m_incMinusButton{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Button m_incPlusButton{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Button m_moveButton{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::NumberBox m_stepBox{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::NumberBox m_targetBox{ nullptr };

        // ── Brake Off: giữ 3 s = nhả khẩn cấp (bản WPF AxisRowVm._holding/_brakeHoldStartedAt/EmergencyArming/Armed) ──
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_brakeHoldTimer{ nullptr };
        std::chrono::steady_clock::time_point m_brakeHoldStartedAt{};
        /// <summary>Đang thật sự giữ nút (giữa nhấn và nhả/mất capture) — phân biệt với rê chuột ngang qua.</summary>
        bool m_holding{ false };
        /// <summary>Đang giữ nhưng CHƯA đủ 3 s — tô cam để operator biết đang đếm giờ.</summary>
        bool m_emergencyArming{ false };
        /// <summary>Đã giữ đủ 3 s, đang nhả phanh khẩn cấp (toggle) — tô đỏ, chỉ tắt khi bấm lại.</summary>
        bool m_emergencyArmed{ false };
        bool m_brakeReleased{ false };
        int m_shownBrakeTint{ -1 };

        // Giá trị đã hiện lần trước (-1 = chưa hiện).
        int m_shownServo{ -1 };
        int m_shownAlarm{ -1 };
        int m_shownOrg{ -1 };
        int m_shownBusy{ -1 };
        winrt::hstring m_shownPos{ L"\x01" };
        std::string m_shownUnit{ "\x01" };
    };
}
