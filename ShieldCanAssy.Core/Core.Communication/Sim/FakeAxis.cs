using System;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using ShieldCanAssy.Core.Infor;
using ShieldCanAssy.Core.Lang;
using ShieldCanAssy.Utility;

namespace ShieldCanAssy.Core.Communication.Sim
{
    /// <summary>
    /// Trục mô phỏng — cho phép viết và kiểm thử toàn bộ chuỗi Origin, chuỗi Auto, xử lý NG và
    /// Pause/Resume trước khi có phần cứng (PLAN.md §10, Simulation mode).
    ///
    /// <para>Mô phỏng theo thời gian thật: vị trí tiến dần về đích với tốc độ đã lệnh, cập nhật
    /// trong <see cref="Refresh"/> đúng như driver thật được vòng quét đọc trạng thái. Nhờ vậy
    /// timeout, interlock và điều kiện chờ chạy trên FakeAxis có ý nghĩa thật, chứ không phải
    /// hoàn tất tức thời làm mọi thứ luôn "đúng".</para>
    ///
    /// <para>Các cờ <c>Sim*</c> để bơm lỗi từ trang Manual: chúng là cách duy nhất kiểm thử được
    /// nhánh xử lý alarm mà không phải rút cáp máy thật.</para>
    /// </summary>
    public class FakeAxis : Bindable, IAxis
    {
        private enum Mode { Idle, MoveAbs, Jog, Home }

        private readonly Stopwatch _clock = Stopwatch.StartNew();
        private readonly Random _rng;
        private double _lastTickSec;

        private Mode _mode = Mode.Idle;
        private double _target;
        private double _speed;
        private int _jogDir;
        private double _homeTarget;

        /// <summary>
        /// Cữ hành trình cứng đặt cách giới hạn mềm một khoảng, đúng như bố trí thật: soft limit
        /// là hàng rào của phần mềm, cảm biến cứng nằm sau nó vài mm làm lớp cuối cùng.
        /// </summary>
        private const double HardLimitMargin = 5.0;

        /// <summary>Số lệnh chạy dịu (ramp theo tốc độ lệnh) và tốc độ của lệnh gần nhất — cho self-test.</summary>
        public int GentleMoves;
        public double LastGentleSpeed;

        public FakeAxis(AxisConfig config)
        {
            Config = config;
            Name = config.Name;
            _rng = new Random(Name.GetHashCode());

            _pos = config.StandbyPos;

            // Máy thật lúc bật nguồn không nằm sẵn ở gốc. Nếu vị trí chờ trùng gốc thì đặt lệch
            // ra để chuỗi về gốc có quãng đường thật sự phải chạy — không thì bước O2 hoàn tất
            // tức thời và chốt chặn Z_SAFE không được kiểm thử gì cả.
            if (Math.Abs(_pos - config.HomeOffset) < 1e-6)
                _pos = Clamp(config.HomeOffset - config.HomeDir * 15.0);
        }

        /// <summary>Trục quay tự do không có hàng rào hành trình để mà kẹp vào.</summary>
        private double Clamp(double v)
            => Config.WrapDegrees ? v
             : v < Config.SoftLimitNeg ? Config.SoftLimitNeg
             : v > Config.SoftLimitPos ? Config.SoftLimitPos
             : v;

        public string Name { get; }
        public AxisConfig Config { get; }

        // ── Cờ bơm lỗi cho mô phỏng ──
        public bool SimDisconnected { get; set; }

        /// <summary>
        /// Bật/tắt qua nút ALM ở trang Manual. Phải tự bắn PropertyChanged cho cả chính nó lẫn
        /// <see cref="Alarm"/> — khác <c>FastechAxis</c>/<c>PanasonicAxis</c> (nơi <c>Alarm</c> là
        /// field có SetProperty riêng), ở đây <c>Alarm</c> chỉ là phép chiếu qua cờ này nên không
        /// tự thông báo được nếu cờ gán thẳng bằng auto-property.
        /// </summary>
        private bool _simDriverAlarm;
        public bool SimDriverAlarm
        {
            get => _simDriverAlarm;
            set { if (SetProperty(ref _simDriverAlarm, value)) OnPropertyChanged(nameof(Alarm)); }
        }
        /// <summary>Chạy chậm 20 lần để kích hoạt nhánh timeout — không nhân <see cref="SpeedFactor"/>.</summary>
        public bool SimStall { get; set; }

        /// <summary>
        /// Nhân tốc độ mọi trục giả. App chạy thường để 1 (đúng tốc độ thật); self-test đặt rất lớn để trục tới đích trong
        /// một vòng quét (Tuấn 2026-09-29 — thời gian self-test gần như toàn là chờ trục giả chạy).
        /// </summary>
        public static double SpeedFactor { get; set; } = 1.0;

        private bool _connected;
        public bool IsConnected { get => _connected && !SimDisconnected; private set => SetProperty(ref _connected, value); }

        private bool _servoOn;
        public bool ServoOn { get => _servoOn; private set => SetProperty(ref _servoOn, value); }

        private bool _busy;
        public bool Busy { get => _busy; private set => SetProperty(ref _busy, value); }

        public bool Alarm => SimDriverAlarm;

        private bool _orgOk;
        public bool OrgOk { get => _orgOk; private set => SetProperty(ref _orgOk, value); }

        public bool FLS => !Config.WrapDegrees && _pos >= Config.SoftLimitPos + HardLimitMargin;
        public bool RLS => !Config.WrapDegrees && _pos <= Config.SoftLimitNeg - HardLimitMargin;

        /// <summary>
        /// Vị trí THÔ, chưa quấn — tương ứng bộ đếm của driver thật trong <c>FastechAxis</c>.
        /// Mô phỏng phải giữ nó chưa quấn thì phép "đi đường ngắn" mới có gì để đo.
        /// </summary>
        private double _pos;

        /// <summary>Toạ độ máy: trục quay thì quấn về [0, 360), trục thẳng thì đúng vị trí thô.</summary>
        public double CurrentPos
        {
            get => Config.WrapDegrees ? AxisAngle.Wrap(_pos) : _pos;
            private set => SetProperty(ref _pos, value);
        }

        public int ErrorCode => SimDriverAlarm ? 0x1601 : 0;

        public bool Connect()
        {
            IsConnected = true;
            _lastTickSec = _clock.Elapsed.TotalSeconds;
            return true;
        }

        public void Disconnect()
        {
            IsConnected = false;
            ServoOn = false;
            OrgOk = false;
        }

        public Task<bool> SetServoOnAsync(bool on)
        {
            if (!IsConnected) return Task.FromResult(false);
            ServoOn = on;   // tắt servo không làm mất gốc — khớp §4.1 (Tuấn bỏ quy tắc cũ 2026-09-26)
            return Task.FromResult(true);
        }

        public async Task<bool> HomeAsync(CancellationToken ct)
        {
            if (!IsConnected || !ServoOn || Alarm) return false;

            OrgOk = false;

            // Cảm biến gốc nằm ở HomeOffset, trong hành trình cho phép — trục chạy TỚI nó chứ
            // không chạy một quãng cố định rồi tuyên bố đã về gốc. Mô phỏng kiểu "chạy 25 mm rồi
            // nhảy về gốc" từng làm trục vọt qua cả cữ hành trình cứng và sinh alarm giả.
            _homeTarget = Clamp(Config.HomeOffset);
            // Về gốc luôn chạy ở tốc độ CHẬM cố định — không theo nút chọn Nhanh/Chậm của thao
            // tác tay, an toàn không được phép phụ thuộc việc operator đang để chế độ nào.
            _speed = Config.JogSpeedSlow;
            _mode = Mode.Home;
            Busy = true;

            var sw = Stopwatch.StartNew();
            try
            {
                while (_mode == Mode.Home)
                {
                    if (ct.IsCancellationRequested) { Abort(); return false; }

                    if (sw.ElapsedMilliseconds > Config.HomeTimeoutMs)
                    {
                        Abort();
                        AlarmManager.RaiseAxisEvent(Name, AxisFault.HomeTimeout,
                            Loc.F("Axis.TimeoutMs", Config.HomeTimeoutMs));
                        return false;
                    }

                    if (Alarm)
                    {
                        Abort();
                        AlarmManager.RaiseAxis(Name, AxisFault.DriverAlarm, $"0x{ErrorCode:X4}");
                        return false;
                    }

                    await Task.Delay(20, CancellationToken.None).ConfigureAwait(false);
                }
            }
            finally
            {
                Busy = false;
            }

            return OrgOk;
        }

        /// <summary>
        /// Chạy tuyệt đối. Với trục quay, đích được quy về vòng gần vị trí hiện tại nhất — mô
        /// phỏng phải đi đúng đường ngắn như <c>FastechAxis</c>, nếu không thì phép đo thời gian
        /// chu kỳ và mọi kiểm thử về góc trong <c>--selftest</c> đều chạy trên hành vi không có
        /// thật.
        /// </summary>
        public Task<bool> MoveAbsAsync(double pos, double speed, CancellationToken ct, bool gentle = false)
        {
            if (gentle) { GentleMoves++; LastGentleSpeed = speed; }
            double raw = Config.WrapDegrees
                ? _pos + AxisAngle.ShortestDelta(AxisAngle.Wrap(_pos), pos)
                : pos;
            return RunAbsAsync(raw, pos, speed, ct);
        }

        /// <summary><paramref name="rawTarget"/> là đích trong hệ chưa quấn; <paramref name="shownPos"/>
        /// là con số người gọi đã yêu cầu, chỉ dùng cho thông báo lỗi và phép kiểm tra cuối.</summary>
        private async Task<bool> RunAbsAsync(double rawTarget, double shownPos, double speed, CancellationToken ct)
        {
            double pos = shownPos;
            if (!IsConnected || !ServoOn || Alarm) return false;

            if (!Config.InSoftLimit(pos))
            {
                AlarmManager.RaiseAxisEvent(Name, AxisFault.SoftLimit,
                    Loc.F("Axis.SoftLimitOutSim", pos, Config.Unit, Config.SoftLimitNeg, Config.SoftLimitPos));
                return false;
            }
            if (!OrgOk)
            {
                AlarmManager.RaiseAxisEvent(Name, AxisFault.NotHomed);
                return false;
            }

            _target = rawTarget;
            _speed = Math.Max(0.1, speed);
            _mode = Mode.MoveAbs;
            Busy = true;

            var sw = Stopwatch.StartNew();
            try
            {
                while (_mode == Mode.MoveAbs)
                {
                    if (ct.IsCancellationRequested) { Abort(); return false; }

                    if (sw.ElapsedMilliseconds > Config.MoveTimeoutMs)
                    {
                        Abort();
                        AlarmManager.RaiseAxisEvent(Name, AxisFault.MoveTimeout,
                            Loc.F("Axis.SimRemain", Math.Abs(_target - _pos), Config.Unit));
                        return false;
                    }

                    if (Alarm)
                    {
                        Abort();
                        AlarmManager.RaiseAxis(Name, AxisFault.DriverAlarm, $"0x{ErrorCode:X4}");
                        return false;
                    }

                    await Task.Delay(10, CancellationToken.None).ConfigureAwait(false);
                }
            }
            finally
            {
                Busy = false;
            }

            return Config.WrapDegrees
                ? AxisAngle.Distance(CurrentPos, pos) < 0.01
                : Math.Abs(_pos - pos) < 0.01;
        }

        /// <summary>
        /// Di chuyển TƯƠNG ĐỐI (Inching) — cộng dồn rồi chạy tới đó.
        ///
        /// <para>Đi thẳng vào <see cref="RunAbsAsync"/> chứ không qua <see cref="MoveAbsAsync"/>:
        /// trục quay phải nhích đúng <paramref name="delta"/> đã yêu cầu, kể cả khi nó lớn hơn nửa
        /// vòng. Qua Move Abs thì phép "đi đường ngắn" sẽ đổi một lệnh nhích +200° thành −160° —
        /// đúng tư thế cuối nhưng sai hẳn thao tác người vận hành vừa bấm.</para>
        /// </summary>
        public Task<bool> MoveIncAsync(double delta, double speed, CancellationToken ct)
            => RunAbsAsync(_pos + delta, CurrentPos + delta, speed, ct);

        public void Jog(int dir, double speed)
        {
            if (!IsConnected || !ServoOn || Alarm) return;

            if (dir == 0)
            {
                if (_mode == Mode.Jog) { _mode = Mode.Idle; Busy = false; }
                _jogDir = 0;
                return;
            }

            _jogDir = Math.Sign(dir);
            _speed = Math.Max(0.1, speed);
            _mode = Mode.Jog;
            Busy = true;
        }

        public void Stop() => Abort();

        public Task<bool> ResetAlarmAsync()
        {
            SimDriverAlarm = false;
            AlarmManager.ClearConditionAxis(Name, AxisFault.DriverAlarm);
            return Task.FromResult(true);
        }

        private void Abort()
        {
            _mode = Mode.Idle;
            _jogDir = 0;
            Busy = false;
        }

        /// <summary>Bước mô phỏng theo thời gian thực. MotionConnect gọi mỗi vòng quét.</summary>
        public void Refresh()
        {
            double now = _clock.Elapsed.TotalSeconds;
            double dt = now - _lastTickSec;
            _lastTickSec = now;

            if (dt <= 0 || dt > 1.0) dt = 0.05;      // bỏ qua bước nhảy khi debug dừng lâu
            if (!IsConnected || !ServoOn) return;

            double v = _speed * (SimStall ? 0.05 : SpeedFactor);
            double step = v * dt;

            switch (_mode)
            {
                case Mode.MoveAbs:
                {
                    double remain = _target - _pos;
                    if (Math.Abs(remain) <= step)
                    {
                        // Sai số lặp lại ±5 µm — để logic phía trên không vô tình phụ thuộc
                        // vào việc trục dừng đúng tuyệt đối, thứ không bao giờ xảy ra thật.
                        CurrentPos = _target + (_rng.NextDouble() - 0.5) * 0.01;
                        _mode = Mode.Idle;
                    }
                    else
                    {
                        CurrentPos = _pos + Math.Sign(remain) * step;
                    }
                    break;
                }

                case Mode.Jog:
                {
                    double next = _pos + _jogDir * step;
                    // Trục quay jog được vô hạn theo cả hai chiều — không có cữ để dừng lại.
                    if (!Config.WrapDegrees)
                    {
                        if (next > Config.SoftLimitPos) { next = Config.SoftLimitPos; Abort(); }
                        if (next < Config.SoftLimitNeg) { next = Config.SoftLimitNeg; Abort(); }
                    }
                    CurrentPos = next;
                    break;
                }

                case Mode.Home:
                {
                    double remain = _homeTarget - _pos;
                    if (Math.Abs(remain) <= step)
                    {
                        CurrentPos = Config.HomeOffset;
                        OrgOk = true;
                        _mode = Mode.Idle;
                    }
                    else
                    {
                        CurrentPos = _pos + Math.Sign(remain) * step;
                    }
                    break;
                }
            }
        }
    }
}
