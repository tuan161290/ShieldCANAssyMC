using System;
using System.Threading.Tasks;
using System.Windows.Input;
using System.Windows.Threading;
using ShieldCanAssy.Core.Communication;
using ShieldCanAssy.Core.Communication.Sim;
using ShieldCanAssy.Core.Controller;
using ShieldCanAssy.Core.Infor;
using ShieldCanAssy.Core.Lang;
using ShieldCanAssy.Wpf.Mvvm;

namespace ShieldCanAssy.Wpf.ViewModels
{
    /// <summary>Một dòng trục trên trang Manual.</summary>
    public class AxisRowVm : ObservableObject
    {
        /// <summary>Giữ nút "Mở phanh" đủ chừng này thì kích hoạt nhả phanh KHẨN CẤP (hoạt động
        /// cả khi Alarm/Servo OFF — để đẩy tay được trục Z lúc sự cố, Tuấn yêu cầu, 2026-09-04).</summary>
        private static readonly TimeSpan EmergencyHoldThreshold = TimeSpan.FromSeconds(3);

        private readonly DispatcherTimer _brakeHoldTimer;
        private DateTime _brakeHoldStartedAt;

        /// <summary>Đang thật sự giữ chuột xuống (giữa Down và Up/Leave) — PHÂN BIỆT với
        /// <see cref="MouseEventArgs"/> MouseLeave có thể tự bắn khi chuột chỉ LƯỚT QUA nút rồi ra,
        /// không hề bấm xuống. Thiếu cờ này thì <see cref="BrakeHoldEnd"/> vẫn cứ nhả phanh mỗi lần
        /// chuột rê ngang qua — lỗi thật đã xảy ra, Tuấn phát hiện 2026-09-04 (hover cũng nhả DO).</summary>
        private bool _holding;

        public AxisRowVm(IAxis axis)
        {
            Axis = axis;
            TargetPos = axis.Config.StandbyPos;
            // Bước nhảy mặc định cho Move Increment — nhỏ vừa tay cho từng loại trục, chỉnh lại
            // được ở ô nhập ngay cạnh nút (Tuấn yêu cầu, 2026-09-06, tiện cho công việc Calibration:
            // nudge từng bước nhỏ thay vì phải gõ toạ độ tuyệt đối hoặc giữ Jog liên tục).
            StepSize = axis.Config.Unit == "deg" ? 1.0 : 0.1;

            // Đảm bảo "xử lý xong lỗi mới cho phép SON" (Tuấn yêu cầu, 2026-09-04): Alarm còn
            // sống thì không cho bật servo — phải bấm RST (ResetAlarmCommand) trước. Đây cũng
            // gián tiếp chặn luôn phanh: UpdateBrakeOutputs chỉ nhả khi ServoOn, nên chưa RST thì
            // không có đường nào nhả được phanh — TRỪ nhả phanh khẩn cấp (xem BrakeHoldStart).
            //
            // MỘT nút toggle thay vì 2 nút on/off tách riêng (Tuấn yêu cầu, 2026-09-04): tự quyết
            // định bật/tắt dựa theo Axis.ServoOn THẬT (trạng thái xác nhận từ driver), không phải
            // cờ lạc quan phía UI — nút chỉ đổi hình sau khi driver thật sự xác nhận đã đổi.
            ServoToggleCommand = new RelayCommand(async () =>
            {
                if (Axis.ServoOn)
                {
                    await Axis.SetServoOnAsync(false);
                    return;
                }
                if (Axis.Alarm)
                {
                    McControl.Status.Message = Loc.F("Manual.AxisAlarmNoServo", Name);
                    return;
                }
                await Axis.SetServoOnAsync(true);
            });
            HomeCommand = new RelayCommand(async () =>
            {
                string reason;
                // CanMoveAutonomous (không phải CanMove trơn) — Về gốc X/Y1/Y2 bấm riêng lẻ từ
                // trang Manual còn đòi Z1/Z2 đã về gốc THẬT (OrgOk), không chỉ đang đứng trong
                // vùng đọc là an toàn (Tuấn yêu cầu, 2026-09-07). Jog/Inching của Axis này không
                // đi qua đây nên không bị ảnh hưởng.
                if (!McControl.Func.CanMoveAutonomous(Axis.Name, out reason))
                {
                    McControl.Status.Message = Loc.F("Manual.HomeRefused", Axis.Name, reason);
                    return;
                }
                await McControl.Motion.HomeAsync(Axis, McControl.Safety.AbortToken);
            });
            MoveCommand = new RelayCommand(async () =>
            {
                // CanMoveAutonomous (không phải CanMove trơn, thứ McFunction.MoveAsync đã tự
                // kiểm bên trong) — Move Abs bấm riêng lẻ từ trang Manual còn đòi Z1/Z2 đã về
                // gốc THẬT, giống hệt điều kiện của HomeCommand ở trên (Tuấn yêu cầu, 2026-09-07).
                string reason;
                if (!McControl.Func.CanMoveAutonomous(Axis.Name, out reason))
                {
                    McControl.Status.Message = Loc.F("Manual.MoveRefused", Axis.Name, reason);
                    return;
                }
                await McControl.Func.MoveAsync(Axis.Name, TargetPos, Axis.Config.MoveSpeed, McControl.Safety.AbortToken);
            });
            // Inching (Move Increment) — dịch một BƯỚC (StepSize) tính từ vị trí HIỆN TẠI, khác
            // Move Abs (đi thẳng tới TargetPos gõ tay). Đi qua McFunction.MoveIncAsync → IAxis.
            // MoveIncAsync: Fastech (A1/A2) dùng lệnh gốc FAS_MoveSingleAxisIncPos của driver,
            // Panasonic (X/Y/Z) chưa có block tương đối nên IAxis tự bù bằng phần mềm — ViewModel
            // không cần biết trục nào dùng cách nào (Tuấn chỉ ra, 2026-09-07: đây gọi là "Inching",
            // và phải tận dụng lệnh gốc của driver ở nơi có, không tự cộng dồn rồi gọi Move Abs).
            IncrementMinusCommand = new RelayCommand(async () => await MoveIncrementAsync(-1));
            IncrementPlusCommand = new RelayCommand(async () => await MoveIncrementAsync(1));
            StopCommand = new RelayCommand(() => Axis.Stop());
            ResetAlarmCommand = new RelayCommand(async () => await Axis.ResetAlarmAsync());

            _brakeHoldTimer = new DispatcherTimer(DispatcherPriority.Normal) { Interval = TimeSpan.FromMilliseconds(100) };
            _brakeHoldTimer.Tick += (_, __) => BrakeHoldTick();

            ToggleSimAlarmCommand = new RelayCommand(() => { if (Sim != null) { Sim.SimDriverAlarm = !Sim.SimDriverAlarm; OnPropertyChanged(nameof(SimAlarmOn)); } });
            ToggleSimDisconnectCommand = new RelayCommand(() => { if (Sim != null) { Sim.SimDisconnected = !Sim.SimDisconnected; OnPropertyChanged(nameof(SimDisconnectOn)); } });
        }

        public IAxis Axis { get; }
        public string Name => Axis.Name;
        public string Unit => Axis.Config.Unit;

        /// <summary>Trục có phanh — chỉ Z1/Z2 (Tuấn xác nhận, 2026-09-04). Dùng để hiện nút
        /// "Mở phanh" chỉ ở 2 dòng này.</summary>
        public bool HasBrakeButton => Name == AxisId.Z1 || Name == AxisId.Z2;

        /// <summary>Có cữ giới hạn cứng POT/NOT vật lý — mọi trục trừ A1/A2 (trục xoay tự do,
        /// không có hành trình để đặt cữ kiểu này). Dùng để ẩn 2 đèn POT/NOT ở dòng A1/A2 trên
        /// trang Manual (Tuấn yêu cầu, 2026-09-09, sau khi phát hiện hard limit không tự hết —
        /// cần đèn sống để xem đúng lúc nào cữ nhả trong lúc Jog).</summary>
        public bool HasHardLimitSwitch => Name != AxisId.A1 && Name != AxisId.A2;

        /// <summary>Đèn trên nút "Mở phanh" — đọc thẳng trạng thái DO thật (McFunction.BrakeReady),
        /// do ManualViewModel.Refresh() bơm vào mỗi 150ms, không tự suy luận ở đây.</summary>
        private bool _brakeReleased;
        public bool BrakeReleased { get => _brakeReleased; set => SetProperty(ref _brakeReleased, value); }

        /// <summary>Đang giữ nút nhưng CHƯA đủ 3s — cảnh báo sớm để operator biết đang đếm giờ.</summary>
        private bool _emergencyArming;
        public bool EmergencyArming { get => _emergencyArming; private set => SetProperty(ref _emergencyArming, value); }

        /// <summary>
        /// Đã giữ đủ 3s, đang nhả phanh khẩn cấp — TOGGLE (Tuấn yêu cầu, 2026-09-09, đã xác nhận
        /// rủi ro qua AskUserQuestion), KHÔNG còn là dead-man như trước: giữ NGUYÊN dù buông tay,
        /// chỉ tắt khi bấm lại nút này (xem <see cref="BrakeHoldStart"/>). Xem
        /// <see cref="McFunction.SetEmergencyBrakeRelease"/> cho cơ chế phía Core.
        /// </summary>
        private bool _emergencyArmed;
        public bool EmergencyArmed { get => _emergencyArmed; private set => SetProperty(ref _emergencyArmed, value); }

        /// <summary>
        /// Bấm nút "Mở phanh". Nếu ĐANG nhả khẩn cấp (<see cref="EmergencyArmed"/>) thì cú bấm
        /// này là TẮT — khoá lại ngay lúc Down, không cần đợi giữ hay đợi Up (khoá là chiều an
        /// toàn, làm ngay khi biết ý định là đủ). Nếu CHƯA nhả khẩn cấp thì bắt đầu đếm giờ như cũ
        /// — giữ đủ <see cref="EmergencyHoldThreshold"/> (xem <see cref="BrakeHoldTick"/>) mới bật
        /// khẩn cấp; buông tay trước đó thì <see cref="BrakeHoldEnd"/> coi như bấm nhanh bình
        /// thường (Tuấn yêu cầu, 2026-09-04).
        /// </summary>
        public void BrakeHoldStart()
        {
            if (!HasBrakeButton) return;

            if (EmergencyArmed)
            {
                McControl.Func.SetEmergencyBrakeRelease(Name, false);
                EmergencyArmed = false;
                EmergencyArming = false;
                McControl.Status.Message = Loc.F("Manual.EmergencyBrakeLocked", Name);
                return;
            }

            _holding = true;
            _brakeHoldStartedAt = DateTime.UtcNow;
            EmergencyArming = true;
            _brakeHoldTimer.Start();
        }

        private void BrakeHoldTick()
        {
            if (DateTime.UtcNow - _brakeHoldStartedAt < EmergencyHoldThreshold) return;

            EmergencyArming = false;
            EmergencyArmed = true;
            McControl.Func.SetEmergencyBrakeRelease(Name, true);
            McControl.Status.Message = Loc.F("Manual.EmergencyBrakeReleased", Name);

            // Toggle đã bật xong việc của nó rồi — dừng đếm ở đây, không cần chờ Up/LostCapture
            // nữa (khác bản dead-man cũ vẫn phải tiếp tục "ping" trong lúc còn giữ).
            _holding = false;
            _brakeHoldTimer.Stop();
        }

        /// <summary>
        /// Buông nút "Mở phanh" (Up) hoặc mất capture chuột (LostMouseCapture — con trỏ trôi ra
        /// khỏi nút trong lúc vẫn giữ chuột trái, xem ManualView.xaml.cs). Chỉ xử lý nếu thật sự
        /// đang giữ (<see cref="_holding"/>) — thiếu điều kiện này thì rê chuột ngang qua nút cũng
        /// gọi RequestBrakeRelease, nhả phanh dù không hề bấm (lỗi thật, Tuấn phát hiện 2026-09-04).
        /// Tới được đây với <c>_holding=true</c> nghĩa là CHƯA đủ 3s (nếu đã đủ thì
        /// <see cref="BrakeHoldTick"/> đã tự đặt <c>_holding=false</c> rồi) — luôn là bấm nhanh
        /// bình thường, không cần kiểm tra EmergencyArmed nữa (khác bản dead-man cũ).
        /// </summary>
        public void BrakeHoldEnd()
        {
            if (!HasBrakeButton || !_holding) return;
            _holding = false;
            _brakeHoldTimer.Stop();

            McControl.Func.RequestBrakeRelease(Name);
            EmergencyArming = false;
        }

        private FakeAxis Sim => Axis as FakeAxis;
        public bool IsSimulated => Sim != null;

        public bool SimAlarmOn => Sim != null && Sim.SimDriverAlarm;
        public bool SimDisconnectOn => Sim != null && Sim.SimDisconnected;

        private double _targetPos;
        public double TargetPos { get => _targetPos; set => SetProperty(ref _targetPos, value); }

        /// <summary>Bước nhảy của Inching (đơn vị theo <see cref="Unit"/>) — chỉnh được ở UI.</summary>
        private double _stepSize;
        public double StepSize { get => _stepSize; set => SetProperty(ref _stepSize, value); }

        private Task MoveIncrementAsync(int dir)
            => McControl.Func.MoveIncAsync(Axis.Name, dir * StepSize, Axis.Config.MoveSpeed, McControl.Safety.AbortToken);

        public void JogStart(int dir) => McControl.Func.Jog(Name, dir);
        public void JogStop() => McControl.Func.Jog(Name, 0);

        public ICommand ServoToggleCommand { get; }
        public ICommand HomeCommand { get; }
        public ICommand MoveCommand { get; }
        public ICommand IncrementMinusCommand { get; }
        public ICommand IncrementPlusCommand { get; }
        public ICommand StopCommand { get; }
        public ICommand ResetAlarmCommand { get; }
        public ICommand ToggleSimAlarmCommand { get; }
        public ICommand ToggleSimDisconnectCommand { get; }
    }
}
