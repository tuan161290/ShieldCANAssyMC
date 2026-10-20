#include "pch.h"
#include "Ui/ManualPageAxisRow.h"

#include "Base/Async.h"
#include "Communication/IAxis.h"
#include "Controller/McControl.h"
#include "Infor/AxisId.h"
#include "Lang/Loc.h"
#include "Ui/Dialogs.h"
#include "Ui/JogControl.h"
#include "Ui/UiUtil.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Documents;
using namespace winrt::Microsoft::UI::Xaml::Media;
namespace WF = winrt::Windows::Foundation;

namespace ui::manualpage
{
    namespace
    {
        /// <summary>Giữ nút Brake Off đủ chừng này thì nhả phanh KHẨN CẤP (hoạt động cả khi Alarm/Servo OFF — để đẩy tay
        /// được trục Z lúc sự cố, Tuấn yêu cầu 2026-09-04).</summary>
        constexpr auto EmergencyHoldThreshold = std::chrono::seconds(3);

        // Ô một nhóm chức năng của bảng trục (AxisGroupCellStyle của bản WPF): vạch dọc mảnh bên trái ngăn nhóm,
        // Margin 5 trái, Padding 5,3,0,3. Bề rộng = tổng nút bên trong + 6.
        Border GroupCell(double width)
        {
            Border cell;
            cell.Width(width);
            cell.BorderBrush(Res(L"DividerBrush"));
            cell.BorderThickness(ThicknessHelper::FromLengths(1, 0, 0, 0));
            cell.Margin(ThicknessHelper::FromLengths(5, 0, 0, 0));
            cell.Padding(ThicknessHelper::FromLengths(5, 3, 0, 3));
            return cell;
        }

        StackPanel Horizontal()
        {
            StackPanel panel;
            panel.Orientation(Orientation::Horizontal);
            return panel;
        }

        Thickness Left(double px) { return ThicknessHelper::FromLengths(px, 0, 0, 0); }

        FrameworkElement FindDescendant(DependencyObject const& parent, std::wstring_view name, int depth)
        {
            int count = VisualTreeHelper::GetChildrenCount(parent);
            for (int i = 0; i < count; ++i)
            {
                auto child = VisualTreeHelper::GetChild(parent, i);
                if (auto fe = child.try_as<FrameworkElement>(); fe && fe.Name() == name) return fe;
                if (depth > 0)
                    if (auto found = FindDescendant(child, name, depth - 1)) return found;
            }
            return nullptr;
        }

        /// <summary>
        /// NumberBox hẹp (UI-SPEC §9.7, spin ẩn): TextBox bên trong có MinWidth 64 của theme và nút "xoá chữ" ~30 px — ở bề
        /// rộng 44/70 px hai thứ đó ăn hết chỗ của con số. Gỡ cả hai khi template đã dựng. Commit giá trị lúc rời ô / Enter —
        /// bấm nút bên cạnh cũng làm ô mất focus trước khi Click chạy (nên không có lỗi "0." của bản WPF).
        /// </summary>
        NumberBox CompactNumberBox(double width, double value, int decimals, wchar_t const* tip)
        {
            NumberBox box;
            ConfigureNumberBox(box, decimals);
            box.Width(width);
            box.MinWidth(0);
            box.VerticalAlignment(VerticalAlignment::Center);
            box.Value(value);
            if (tip) ToolTipService::SetToolTip(box, box_value(tip));

            auto token = std::make_shared<winrt::event_token>();
            *token = box.Loaded([token](WF::IInspectable const& sender, RoutedEventArgs const&) {
                auto nb = sender.as<NumberBox>();
                nb.Loaded(*token);
                nb.ApplyTemplate();
                auto input = FindDescendant(nb, L"InputBox", 6).try_as<TextBox>();
                if (!input) return;
                input.MinWidth(0);
                input.Padding(ThicknessHelper::FromLengths(6, 8, 4, 0));
                input.ApplyTemplate();
                if (auto del = FindDescendant(input, L"DeleteButton", 6).try_as<Button>())
                {
                    del.MinWidth(0);
                    del.Width(0);
                    del.MaxWidth(0);
                    del.Margin(ThicknessHelper::FromUniformLength(0));
                    del.Padding(ThicknessHelper::FromUniformLength(0));
                    del.Opacity(0);
                    del.IsHitTestVisible(false);
                    del.IsTabStop(false);
                }
            });
            return box;
        }

        /// <summary>Thân một lệnh dài trên luồng nền: bị huỷ (EMG/STOP) là đường bình thường, lỗi khác thì báo.</summary>
        void RunGuarded(std::string const& what, std::function<void()> const& fn)
        {
            try { fn(); }
            catch (const ::base::OperationCanceled&) {}
            catch (const std::exception& ex)
            {
                Dialogs::Error(what + ": " + ex.what());
            }
        }

        /// <summary>Thân một event handler trên luồng UI: exception không được lọt ra ranh giới WinRT.</summary>
        template <class F>
        void UiGuarded(F&& fn)
        {
            try { fn(); }
            catch (const std::exception& ex) { Dialogs::Error(ex.what()); }
            catch (winrt::hresult_error const& ex) { Dialogs::Error(winrt::to_string(ex.message())); }
        }

        /// <summary>Giá trị ô số; ô bị xoá trắng (NaN) thì trả lại giá trị cũ vào ô và dùng giá trị cũ.</summary>
        double ReadBox(NumberBox const& box, double& last)
        {
            double v = box.Value();
            if (std::isnan(v)) box.Value(last);
            else last = v;
            return last;
        }
    }

    // ══════════ Dựng ══════════

    std::shared_ptr<AxisRow> AxisRow::Create(std::shared_ptr<sca::IAxis> axis, ManualLampBrushes const& brushes)
    {
        auto row = std::make_shared<AxisRow>(std::move(axis));
        row->Build(brushes);   // handler bắt weak_from_this — phải dựng SAU khi shared_ptr đã có
        row->Update();
        return row;
    }

    AxisRow::AxisRow(std::shared_ptr<sca::IAxis> axis)
        : m_axis(std::move(axis)), m_name(m_axis->Name())
    {
        m_hasBrake = m_name == sca::AxisId::Z1 || m_name == sca::AxisId::Z2;
        // Có cữ cứng POT/NOT — mọi trục trừ A1/A2 (trục xoay tự do, không có hành trình để đặt cữ).
        m_hasHardLimit = m_name != sca::AxisId::A1 && m_name != sca::AxisId::A2;
    }

    void AxisRow::Build(ManualLampBrushes const& brushes)
    {
        std::weak_ptr<AxisRow> weak = weak_from_this();
        auto cfg = m_axis->Config();

        m_root = Border();
        m_root.BorderBrush(Res(L"DividerBrush"));
        m_root.BorderThickness(ThicknessHelper::FromLengths(0, 0, 0, 1));
        StackPanel line = Horizontal();

        // ── Trục / Vị trí / Cữ ──
        TextBlock name;
        name.Width(AxisColumns::Axis);
        name.Text(H(m_name));
        name.FontSize(14);
        name.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
        name.VerticalAlignment(VerticalAlignment::Center);
        line.Children().Append(name);

        TextBlock pos;
        pos.Width(AxisColumns::Position);
        pos.VerticalAlignment(VerticalAlignment::Center);
        pos.FontFamily(FontFamily(L"Consolas"));
        pos.FontSize(15);
        m_posRun = Run();
        Run gap;
        gap.Text(L" ");
        m_unitRun = Run();
        m_unitRun.FontSize(10);
        m_unitRun.Foreground(Res(L"TextSecondaryBrush"));
        pos.Inlines().Append(m_posRun);
        pos.Inlines().Append(gap);
        pos.Inlines().Append(m_unitRun);
        line.Children().Append(pos);

        // POT/NOT — sống theo Axis.FLS/RLS; ẩn ở A1/A2. Grid bọc ngoài giữ nguyên bề rộng cột kể cả khi 2 đèn bị ẩn.
        Grid limitCell;
        limitCell.Width(AxisColumns::Limit);
        limitCell.VerticalAlignment(VerticalAlignment::Center);
        StackPanel limitPanel;
        for (int i = 0; i < 2; ++i)
        {
            StackPanel lampLine = Horizontal();
            if (i == 0) lampLine.Margin(ThicknessHelper::FromLengths(0, 0, 0, 2));
            ManualLamp& lamp = i == 0 ? m_pot : m_not;
            lampLine.Children().Append(lamp.Build(10, brushes));
            TextBlock caption;
            caption.Text(i == 0 ? L"POT" : L"NOT");
            caption.FontSize(9);
            caption.Margin(Left(4));
            caption.VerticalAlignment(VerticalAlignment::Center);
            caption.Foreground(Res(L"TextSecondaryBrush"));
            lampLine.Children().Append(caption);
            limitPanel.Children().Append(lampLine);
        }
        if (!m_hasHardLimit) limitPanel.Visibility(Visibility::Collapsed);
        limitCell.Children().Append(limitPanel);
        line.Children().Append(limitCell);

        // ── Servo: SVO (MỘT nút toggle theo Axis.ServoOn THẬT) / RST / Brake Off (Z1/Z2, đặt CUỐI nhóm để cột không lệch) ──
        {
            Border cell = GroupCell(AxisColumns::Servo);
            StackPanel group = Horizontal();
            m_svoButton = ManualCompactButton(L"SVO off", 66);
            m_svoButton.Click([weak](auto&&, auto&&) { if (auto self = weak.lock()) self->OnServoToggle(); });
            m_rstButton = ManualCompactButton(L"RST", 48, T("Manual.RstTip").c_str());
            m_rstButton.Margin(Left(3));
            m_rstButton.Click([weak](auto&&, auto&&) { if (auto self = weak.lock()) self->OnResetAlarm(); });
            group.Children().Append(m_svoButton);
            group.Children().Append(m_rstButton);
            if (m_hasBrake)
            {
                m_brakeButton = ManualCompactButton(L"Brake Off", 76, T("Manual.BrakeReleaseTip").c_str());
                m_brakeButton.Margin(Left(3));
                // Không gắn Click: Click chỉ bắn lúc NHẢ, không phân biệt được đang giữ hay đã buông.
                AttachHold(m_brakeButton,
                           [weak] { if (auto self = weak.lock()) self->BrakeHoldStart(); },
                           [weak] { if (auto self = weak.lock()) self->BrakeHoldEnd(); });
                group.Children().Append(m_brakeButton);

                m_brakeHoldTimer = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
                m_brakeHoldTimer.Interval(std::chrono::milliseconds(100));
                m_brakeHoldTimer.IsRepeating(true);
                m_brakeHoldTimer.Tick([weak](auto&&, auto&&) { if (auto self = weak.lock()) self->BrakeHoldTick(); });
            }
            cell.Child(group);
            line.Children().Append(cell);
        }

        // ── Origin — đèn ORG gộp vào chính nút (xanh đặc + glow khi Axis.OrgOk) ──
        {
            Border cell = GroupCell(AxisColumns::Origin);
            m_homeButton = ManualCompactButton(L"Origin", 58);
            m_homeButton.HorizontalAlignment(HorizontalAlignment::Left);
            m_homeButton.Click([weak](auto&&, auto&&) { if (auto self = weak.lock()) self->OnHome(); });
            cell.Child(m_homeButton);
            line.Children().Append(cell);
        }

        // ── Jog — giữ để chạy, nhả là dừng (McFunction::Jog, tốc độ theo cờ Slow/Fast chung) ──
        {
            Border cell = GroupCell(AxisColumns::Jog);
            StackPanel group = Horizontal();
            Button minus = ManualCompactButton(L"−", 42, T("Manual.JogMinusTip").c_str());
            Button plus = ManualCompactButton(L"+", 42, T("Manual.JogPlusTip").c_str());
            plus.Margin(Left(3));
            AttachJog(minus, m_name, -1);
            AttachJog(plus, m_name, 1);
            group.Children().Append(minus);
            group.Children().Append(plus);
            cell.Child(group);
            line.Children().Append(cell);
        }

        // ── Inching (Move Increment) — dịch một BƯỚC từ vị trí hiện tại ──
        {
            Border cell = GroupCell(AxisColumns::Inching);
            StackPanel group = Horizontal();
            // Bước mặc định nhỏ vừa tay cho từng loại trục, chỉnh được ở ô ngay cạnh nút.
            double step = cfg && cfg->Unit == "deg" ? 1.0 : 0.1;
            m_stepBox = CompactNumberBox(44, step, 3, T("Manual.StepSizeTip").c_str());
            m_incMinusButton = ManualCompactButton(L"−", 38, T("Manual.IncMinusTip").c_str());
            m_incMinusButton.Margin(Left(3));
            m_incMinusButton.Click([weak](auto&&, auto&&) { if (auto self = weak.lock()) self->OnInch(-1); });
            m_incPlusButton = ManualCompactButton(L"+", 38, T("Manual.IncPlusTip").c_str());
            m_incPlusButton.Margin(Left(3));
            m_incPlusButton.Click([weak](auto&&, auto&&) { if (auto self = weak.lock()) self->OnInch(1); });
            group.Children().Append(m_stepBox);
            group.Children().Append(m_incMinusButton);
            group.Children().Append(m_incPlusButton);
            cell.Child(group);
            line.Children().Append(cell);
        }

        // ── Move Abs — chạy tới toạ độ tuyệt đối gõ tay (mặc định StandbyPos) ──
        {
            Border cell = GroupCell(AxisColumns::MoveAbs);
            StackPanel group = Horizontal();
            m_targetBox = CompactNumberBox(70, cfg ? cfg->StandbyPos : 0.0, 3, T("Manual.TargetPosTip").c_str());
            m_moveButton = ManualCompactButton(L"Move", 58, T("Manual.MoveTip").c_str());
            m_moveButton.Margin(Left(3));
            m_moveButton.Click([weak](auto&&, auto&&) { if (auto self = weak.lock()) self->OnMove(); });
            group.Children().Append(m_targetBox);
            group.Children().Append(m_moveButton);
            cell.Child(group);
            line.Children().Append(cell);
        }

        // ── Stop riêng trục này — không bao giờ bị khoá ──
        {
            Border cell = GroupCell(AxisColumns::Stop);
            Button stop = ManualCompactButton(L"Stop", 56);
            stop.HorizontalAlignment(HorizontalAlignment::Left);
            stop.Click([weak](auto&&, auto&&) { if (auto self = weak.lock()) self->OnStop(); });
            cell.Child(stop);
            line.Children().Append(cell);
        }

        m_root.Child(line);
    }

    // ══════════ Cập nhật ══════════

    void AxisRow::Update()
    {
        if (!m_root) return;
        const sca::IAxis& axis = *m_axis;

        winrt::hstring pos = Num(axis.CurrentPos(), 3);
        if (pos != m_shownPos)
        {
            m_shownPos = pos;
            m_posRun.Text(pos);
        }

        // Unit sửa được ở trang Setting (cùng đối tượng cấu hình) — đọc lại mỗi nhịp, rẻ.
        if (auto cfg = axis.Config(); cfg && cfg->Unit != m_shownUnit)
        {
            m_shownUnit = cfg->Unit;
            m_unitRun.Text(H(cfg->Unit));
        }

        // POT/NOT — đọc sống Axis.FLS/RLS: thấy đúng lúc driver nhả bit cữ.
        if (m_hasHardLimit)
        {
            m_pot.Set(axis.FLS());
            m_not.Set(axis.RLS());
        }

        // SVO: chữ và màu theo Axis.ServoOn THẬT (driver xác nhận), không phải cờ lạc quan phía UI.
        int servo = axis.ServoOn() ? 1 : 0;
        if (servo != m_shownServo)
        {
            m_shownServo = servo;
            m_svoButton.Content(box_value(servo ? L"SVO on" : L"SVO off"));
            SetTint(m_svoButton, servo ? Tint::Green : Tint::None);
        }
        int alarm = axis.Alarm() ? 1 : 0;
        if (alarm != m_shownAlarm)
        {
            m_shownAlarm = alarm;
            SetTint(m_rstButton, alarm ? Tint::Red : Tint::None);
        }
        int org = axis.OrgOk() ? 1 : 0;
        if (org != m_shownOrg)
        {
            m_shownOrg = org;
            SetTint(m_homeButton, org ? Tint::Green : Tint::None);
        }

        ApplyBusy(m_busy->load());
    }

    void AxisRow::SetBrakeReleased(bool doOn)
    {
        if (!m_hasBrake) return;
        m_brakeReleased = doOn;
        ApplyBrakeTint();
    }

    // Thứ tự ưu tiên như bản WPF (DataTrigger đặt sau thắng): đỏ (đã nhả khẩn cấp) > cam (đang đếm 3 s) > xanh (DO nhả).
    void AxisRow::ApplyBrakeTint()
    {
        if (!m_brakeButton) return;
        Tint tint = m_emergencyArmed ? Tint::Red
                  : m_emergencyArming ? Tint::Caution
                  : m_brakeReleased ? Tint::Green
                  : Tint::None;
        if ((int)tint == m_shownBrakeTint) return;
        m_shownBrakeTint = (int)tint;
        SetTint(m_brakeButton, tint);
    }

    void AxisRow::ApplyBusy(bool busy)
    {
        int value = busy ? 1 : 0;
        if (value == m_shownBusy) return;
        m_shownBusy = value;
        for (auto const& button : { m_svoButton, m_rstButton, m_homeButton, m_incMinusButton, m_incPlusButton, m_moveButton })
            button.IsEnabled(!busy);
    }

    void AxisRow::RunBlocking(std::function<void()> work)
    {
        if (m_busy->exchange(true)) return;
        ApplyBusy(true);
        std::weak_ptr<AxisRow> weak = weak_from_this();
        RunBackgroundThenUi(std::move(work), [busy = m_busy, weak] {
            busy->store(false);
            if (auto self = weak.lock()) self->ApplyBusy(false);
        });
    }

    // ══════════ Lệnh ══════════

    // MỘT nút toggle: tự quyết định bật/tắt theo Axis.ServoOn THẬT. Alarm còn sống thì không cho bật — phải RST trước
    // ("xử lý xong lỗi mới cho phép SON", Tuấn 2026-09-04). Gián tiếp chặn luôn phanh: UpdateBrakeOutputs chỉ nhả khi
    // ServoOn — trừ nhả phanh khẩn cấp.
    void AxisRow::OnServoToggle()
    {
        UiGuarded([&] {
            auto axis = m_axis;
            std::string what = "SVO " + m_name;
            if (axis->ServoOn())
            {
                RunBlocking([axis, what] { RunGuarded(what, [&] { axis->SetServoOn(false); }); });
                return;
            }
            if (axis->Alarm())
            {
                sca::McControl::Status().SetMessage(sca::Loc::F("Manual.AxisAlarmNoServo", m_name));
                return;
            }
            RunBlocking([axis, what] { RunGuarded(what, [&] { axis->SetServoOn(true); }); });
        });
    }

    void AxisRow::OnResetAlarm()
    {
        UiGuarded([&] {
            auto axis = m_axis;
            std::string what = "RST " + m_name;
            RunBlocking([axis, what] { RunGuarded(what, [&] { axis->ResetAlarm(); }); });
        });
    }

    // CanMoveAutonomous (không phải CanMove trơn) — Origin X/Y1/Y2 bấm riêng lẻ từ trang Manual còn đòi Z1/Z2 đã về gốc
    // THẬT (OrgOk), không chỉ đang đứng trong vùng đọc là an toàn (Tuấn 2026-09-07).
    void AxisRow::OnHome()
    {
        UiGuarded([&] {
            auto func = sca::McControl::Func();
            if (!func) return;
            std::string reason;
            if (!func->CanMoveAutonomous(m_name, reason))
            {
                sca::McControl::Status().SetMessage(sca::Loc::F("Manual.HomeRefused", m_name, reason));
                return;
            }
            auto axis = m_axis;
            RunBlocking([axis, what = "Origin " + m_name] {
                RunGuarded(what, [&] {
                    auto safety = sca::McControl::Safety();
                    if (!safety) return;
                    sca::McControl::Motion().Home(axis, safety->AbortToken());
                });
            });
        });
    }

    // CanMoveAutonomous trước (McFunction::Move chỉ tự kiểm CanMove) — Move Abs bấm riêng lẻ còn đòi Z1/Z2 đã về gốc THẬT,
    // giống điều kiện của Origin (Tuấn 2026-09-07).
    void AxisRow::OnMove()
    {
        UiGuarded([&] {
            auto func = sca::McControl::Func();
            if (!func) return;
            double last = m_targetBox.Value();
            if (std::isnan(last)) last = m_axis->Config() ? m_axis->Config()->StandbyPos : 0.0;
            double target = ReadBox(m_targetBox, last);
            std::string reason;
            if (!func->CanMoveAutonomous(m_name, reason))
            {
                sca::McControl::Status().SetMessage(sca::Loc::F("Manual.MoveRefused", m_name, reason));
                return;
            }
            auto axis = m_axis;
            RunBlocking([axis, target, what = "Move " + m_name] {
                RunGuarded(what, [&] {
                    auto func = sca::McControl::Func();
                    auto safety = sca::McControl::Safety();
                    if (!func || !safety) return;
                    func->Move(axis->Name(), target, axis->Config()->MoveSpeed, safety->AbortToken());
                });
            });
        });
    }

    // Inching — dịch một BƯỚC tính từ vị trí HIỆN TẠI, khác Move Abs. McFunction::MoveInc tự kiểm CanMove (ghi lý do vào
    // Status.Message); Fastech dùng lệnh gốc của driver, Panasonic bù bằng phần mềm — trang không cần biết.
    void AxisRow::OnInch(int dir)
    {
        UiGuarded([&] {
            double last = m_stepBox.Value();
            if (std::isnan(last)) last = m_axis->Config() && m_axis->Config()->Unit == "deg" ? 1.0 : 0.1;
            double step = ReadBox(m_stepBox, last);
            auto axis = m_axis;
            double delta = dir * step;
            RunBlocking([axis, delta, what = "Inching " + m_name] {
                RunGuarded(what, [&] {
                    auto func = sca::McControl::Func();
                    auto safety = sca::McControl::Safety();
                    if (!func || !safety) return;
                    func->MoveInc(axis->Name(), delta, axis->Config()->MoveSpeed, safety->AbortToken());
                });
            });
        });
    }

    // Cùng hàng đợi với lệnh jog: không có lệnh jog nào đang xếp hàng lại chạy SAU lệnh dừng.
    void AxisRow::OnStop()
    {
        auto axis = m_axis;
        PostMotionCommand([axis] { axis->Stop(); });
    }

    // ══════════ Brake Off — bấm nhanh / giữ 3 s ══════════

    // Đang nhả khẩn cấp thì cú nhấn này là TẮT — khoá ngay lúc nhấn, không đợi giữ hay nhả (khoá là chiều an toàn). Chưa
    // nhả khẩn cấp thì bắt đầu đếm giờ: đủ 3 s (BrakeHoldTick) mới bật khẩn cấp; buông trước đó = bấm nhanh bình thường.
    void AxisRow::BrakeHoldStart()
    {
        if (!m_hasBrake) return;
        UiGuarded([&] {
            auto func = sca::McControl::Func();
            if (m_emergencyArmed)
            {
                if (func) func->SetEmergencyBrakeRelease(m_name, false);
                m_emergencyArmed = false;
                m_emergencyArming = false;
                sca::McControl::Status().SetMessage(sca::Loc::F("Manual.EmergencyBrakeLocked", m_name));
                ApplyBrakeTint();
                return;
            }

            m_holding = true;
            m_brakeHoldStartedAt = std::chrono::steady_clock::now();
            m_emergencyArming = true;
            ApplyBrakeTint();
            if (m_brakeHoldTimer) m_brakeHoldTimer.Start();
        });
    }

    void AxisRow::BrakeHoldTick()
    {
        if (!m_holding)
        {
            if (m_brakeHoldTimer) m_brakeHoldTimer.Stop();
            return;
        }
        if (std::chrono::steady_clock::now() - m_brakeHoldStartedAt < EmergencyHoldThreshold) return;

        UiGuarded([&] {
            m_emergencyArming = false;
            m_emergencyArmed = true;
            if (auto func = sca::McControl::Func()) func->SetEmergencyBrakeRelease(m_name, true);
            sca::McControl::Status().SetMessage(sca::Loc::F("Manual.EmergencyBrakeReleased", m_name));
        });

        // Toggle đã bật xong — dừng đếm ở đây, không chờ nhả/mất capture nữa.
        m_holding = false;
        if (m_brakeHoldTimer) m_brakeHoldTimer.Stop();
        ApplyBrakeTint();
    }

    // Nhả nút hoặc mất capture. Chỉ xử lý nếu thật sự đang giữ — thiếu điều kiện này thì rê chuột ngang qua cũng nhả
    // phanh (lỗi thật, 2026-09-04). Tới đây với m_holding = true nghĩa là CHƯA đủ 3 s → luôn là bấm nhanh bình thường.
    void AxisRow::BrakeHoldEnd()
    {
        if (!m_hasBrake || !m_holding) return;
        m_holding = false;
        if (m_brakeHoldTimer) m_brakeHoldTimer.Stop();

        UiGuarded([&] {
            if (auto func = sca::McControl::Func()) func->RequestBrakeRelease(m_name);
        });
        m_emergencyArming = false;
        ApplyBrakeTint();
    }
}
