using System;
using System.Diagnostics;
using System.Net;
using System.Threading;
using System.Threading.Tasks;
using FASTECH;
using ShieldCanAssy.Core.Infor;
using ShieldCanAssy.Core.Lang;
using ShieldCanAssy.Lib.Fastech;
using ShieldCanAssy.Utility;
using ShieldCanAssy.Utility.Log;

namespace ShieldCanAssy.Core.Communication
{
    /// <summary>
    /// Một trục Fastech Ezi-SERVO Plus-E qua Ethernet (A1 hoặc A2), bọc <c>EziMOTIONPlusELib</c>.
    ///
    /// <para><b>Khác Panasonic ở hai điểm quan trọng.</b> (1) Thư viện Fastech là P/Invoke đồng
    /// bộ và <b>không</b> có luồng monitor riêng, nên <see cref="Refresh"/> phải tự gọi
    /// <c>FAS_GetAllStatus</c> — một lần gọi UDP mỗi 20 ms cho mỗi trục. (2) Tốc độ tính bằng
    /// <b>xung/giây</b> chứ không phải rpm.</para>
    ///
    /// <para><b>Cảnh báo về bảng bit trạng thái:</b> xem <see cref="EziAxisStatus"/>. Vị trí và
    /// tốc độ lấy từ trường số nguyên riêng của <c>FAS_GetAllStatus</c> nên không phụ thuộc
    /// bảng bit; chỉ các cờ boolean mới cần xác minh khi có máy.</para>
    ///
    /// <para><b>Số hiệu board (<c>iBdID</c>) là định danh nội bộ của thư viện, không phải IP.</b>
    /// Mọi module Fastech trong cùng process — 2 trục xoay và 2 card I/O — phải mang
    /// <c>iBdID</c> khác nhau, nếu không <c>FAS_Connect</c> của cái sau sẽ đè lên cái trước và
    /// triệu chứng là "trục A2 điều khiển ra card I/O". Bảng phân bổ nằm ở
    /// <see cref="FastechBoardId"/>.</para>
    /// </summary>
    public class FastechAxis : Bindable, IAxis
    {
        private readonly AxisConfig _cfg;
        private readonly AxisScale _scale;
        private readonly int _bdId;
        private readonly object _cmdLock = new object();

        private uint _rawStatus;

        /// <summary>
        /// Vị trí THÔ đọc từ driver, tính bằng xung — bộ đếm riêng của driver, chưa cộng
        /// <see cref="AxisConfig.HomeOffset"/> và chưa quấn về [0, 360).
        ///
        /// <para>Giữ lại con số thô là điều kiện bắt buộc để trục quay chạy đúng: từ một góc đã
        /// quấn (ví dụ 10°) không thể biết driver đang ở vòng nào, mà lệnh chạy tuyệt đối thì
        /// cần đúng một con số xung. Mọi phép chọn vòng gần nhất đều đo từ đây.</para>
        /// </summary>
        private int _rawPulse;

        public FastechAxis(AxisConfig cfg)
        {
            _cfg = cfg;
            _scale = new AxisScale(cfg);
            _bdId = cfg.BoardId;
        }

        public string Name { get { return _cfg.Name; } }
        public AxisConfig Config { get { return _cfg; } }

        /// <summary>Thanh ghi trạng thái thô — hiện trên trang Manual để xác minh bảng bit.</summary>
        public uint RawStatus { get { return _rawStatus; } }
        public string RawStatusText { get { return EziAxisStatus.Describe(_rawStatus); } }

        private bool _connected;
        public bool IsConnected { get { return _connected; } private set { SetProperty(ref _connected, value); } }

        private bool _servoOn;
        public bool ServoOn { get { return _servoOn; } private set { SetProperty(ref _servoOn, value); } }

        private bool _busy;
        public bool Busy { get { return _busy; } private set { SetProperty(ref _busy, value); } }

        private bool _alarm;
        public bool Alarm { get { return _alarm; } private set { SetProperty(ref _alarm, value); } }

        private bool _orgOk;
        public bool OrgOk { get { return _orgOk; } private set { SetProperty(ref _orgOk, value); } }

        private bool _fls;
        public bool FLS { get { return _fls; } private set { SetProperty(ref _fls, value); } }

        private bool _rls;
        public bool RLS { get { return _rls; } private set { SetProperty(ref _rls, value); } }

        private double _pos;
        public double CurrentPos { get { return _pos; } private set { SetProperty(ref _pos, value); } }

        private int _errorCode;
        public int ErrorCode { get { return _errorCode; } private set { SetProperty(ref _errorCode, value); } }

        /// <summary>Cờ về gốc của phần mềm — cùng lý do như PanasonicAxis (PLAN.md §4.1).</summary>
        private bool _softHomed;

        // ───────── Mất kết nối chớp nhoáng ≠ mất kết nối thật ─────────
        //
        // Trước 14/09/2026: MỘT lần FAS_GetAllStatus lỗi là IsConnected = false, mà Refresh() lại mở đầu bằng
        // "chưa kết nối thì thôi" — không còn đường nào đọc lại, kể cả khi driver trả lời bình thường ngay sau
        // đó. Connect() chỉ chạy đúng một lần lúc McControl.Init(), nên trục CHẾT tới khi mở lại app. Đây chính
        // là triệu chứng Tuấn báo 2026-09-14: "A1 hay báo mất kết nối, tắt app bật lại là bình thường".
        //
        // Nay làm như IoConnect: đếm số vòng quét LỖI LIÊN TIẾP, và vẫn đọc tiếp khi đang mất kết nối. Giao thức
        // là UDP — rơi một gói là chuyện thường, năm vòng liên tiếp thì không.
        private const int FailLimit = 5;      // 5 × 20 ms = 100 ms

        /// <summary>Đang mất kết nối thì giãn nhịp đọc: lúc rút cáp thật, mỗi lần gọi thư viện phải chờ hết
        /// timeout của chính nó — gọi mỗi vòng 20 ms sẽ kéo chậm cả vòng quét an toàn.</summary>
        private const int RetryMs = 500;

        /// <summary>Đã mở được socket chưa (Connect thành công). Khác <see cref="IsConnected"/> = "đang đọc
        /// được": Refresh chỉ nghỉ hẳn khi CHƯA mở, không nghỉ khi đang mất kết nối.</summary>
        private bool _opened;
        private int _failStreak;
        private readonly Stopwatch _sinceRead = new Stopwatch();


        // ───────────────────────── kết nối ─────────────────────────

        public bool Connect()
        {
            _opened = false;
            _failStreak = 0;
            try
            {
                IPAddress ip;
                if (!IPAddress.TryParse(_cfg.IpAddress, out ip))
                {
                    LogWorker.Instance.RecordLog(Name + ": IP không hợp lệ — " + _cfg.IpAddress,
                        Utility.Log.LogLevel.Error, Name);
                    IsConnected = false;
                    return false;
                }

                // FAS_Connect lần đầu trong phiên hay hỏng (~230 ms rồi trả false) dù mở lại app vài
                // giây sau là được — log 23–26/09 chỉ thấy ở A1, thiết bị Fastech nối đầu tiên. Thử
                // lại vài lần thay vì bắt mở lại app; Refresh không tự nối lại khi Connect hỏng.
                const int ConnectRetries = 5;
                const int ConnectRetryDelayMs = 200;
                bool connected = false;
                int connectAttempts = 0;
                for (int attempt = 0; attempt < ConnectRetries; attempt++)
                {
                    connectAttempts++;
                    EziMOTIONPlusELib.FAS_Close(_bdId);
                    if (EziMOTIONPlusELib.FAS_Connect(ip, _bdId)) { connected = true; break; }
                    if (attempt < ConnectRetries - 1) Thread.Sleep(ConnectRetryDelayMs);
                }

                if (!connected)
                {
                    LogWorker.Instance.RecordLog(
                        Name + ": không kết nối được " + _cfg.IpAddress + " (bdID " + _bdId +
                        ", đã thử " + ConnectRetries + " lần)",
                        Utility.Log.LogLevel.Error, Name);
                    IsConnected = false;
                    return false;
                }

                // FAS_Connect chỉ mở socket UDP; nó thành công cả khi đầu kia không có thiết bị
                // nào. FAS_IsSlaveExist mới là câu hỏi "có ai trả lời không" — nhưng hỏi ngay lúc
                // app vừa khởi động thì driver Ezi thường CHƯA kịp sẵn sàng trả lời (đang tự khởi
                // động Ethernet stack của chính nó), nên hay báo "mất kết nối" oan dù cáp/nguồn
                // đều ổn (Tuấn báo lỗi, 2026-09-08). Thử lại vài lần thay vì hỏi đúng một lần rồi
                // kết luận ngay — driver trả lời được là dừng thử ngay, không phải lúc nào cũng
                // chờ đủ 1.2s này.
                const int SlaveExistRetries = 6;
                const int SlaveExistRetryDelayMs = 200;
                bool slaveExists = false;
                for (int attempt = 0; attempt < SlaveExistRetries; attempt++)
                {
                    if (EziMOTIONPlusELib.FAS_IsSlaveExist(_bdId) != 0) { slaveExists = true; break; }
                    if (attempt < SlaveExistRetries - 1) Thread.Sleep(SlaveExistRetryDelayMs);
                }

                if (!slaveExists)
                {
                    LogWorker.Instance.RecordLog(
                        Name + ": mở được socket nhưng không có thiết bị trả lời ở " + _cfg.IpAddress +
                        " (đã thử " + SlaveExistRetries + " lần)",
                        Utility.Log.LogLevel.Error, Name);
                    EziMOTIONPlusELib.FAS_Close(_bdId);
                    IsConnected = false;
                    return false;
                }

                _opened = true;
                _sinceRead.Restart();
                IsConnected = true;
                LogWorker.Instance.RecordLog(Name + ": đã kết nối " + _cfg.IpAddress + " (bdID " + _bdId + ")" +
                    (connectAttempts > 1 ? " sau " + connectAttempts + " lần thử" : ""),
                    Utility.Log.LogLevel.Info, Name);
                return true;
            }
            catch (DllNotFoundException)
            {
                // Chạy trên máy chưa có EziMOTIONPlusE.dll: báo rõ chứ không để ngoại lệ trần
                // làm sập vòng khởi tạo.
                LogWorker.Instance.RecordLog(
                    Name + ": không tìm thấy EziMOTIONPlusE.dll cạnh exe",
                    Utility.Log.LogLevel.Error, Name);
                IsConnected = false;
                return false;
            }
            catch (Exception ex)
            {
                LogWorker.Instance.RecordLog(Name + ": lỗi kết nối — " + ex.Message,
                    Utility.Log.LogLevel.Error, Name);
                IsConnected = false;
                return false;
            }
        }

        public void Disconnect()
        {
            // Qua chung _cmdLock với mọi lệnh khác — đóng board giữa lúc một lệnh khác đang gửi
            // P/Invoke tới CÙNG bdId dễ để lại trạng thái lỡ dở (Tuấn lo ngại lúc đóng app,
            // 2026-09-10). Khác PanasonicAxis: lock đồng bộ, không cần timeout vì mọi lệnh ở đây
            // đều là gọi P/Invoke nhanh, không có bước chờ Modbus nối tiếp kiểu servo Panasonic.
            lock (_cmdLock)
            {
                try { EziMOTIONPlusELib.FAS_Close(_bdId); } catch { }
            }
            _opened = false;
            _failStreak = 0;
            IsConnected = false;
            _softHomed = false;
            OrgOk = false;
        }

        // ───────────────────────── đọc trạng thái ─────────────────────────

        public void Refresh()
        {
            if (!_opened) return;
            if (!IsConnected && _sinceRead.IsRunning && _sinceRead.ElapsedMilliseconds < RetryMs) return;
            _sinceRead.Restart();

            uint inStatus = 0, outStatus = 0, axisStatus = 0;
            int cmdPos = 0, actPos = 0, posErr = 0, actVel = 0;
            ushort itemNo = 0;

            int rc;
            try
            {
                rc = EziMOTIONPlusELib.FAS_GetAllStatus(_bdId, ref inStatus, ref outStatus,
                    ref axisStatus, ref cmdPos, ref actPos, ref posErr, ref actVel, ref itemNo);
            }
            catch (Exception ex) { ReadFailed(ex.Message); return; }

            if (rc != 0) { ReadFailed("FAS_GetAllStatus trả mã " + rc); return; }

            ReadOk();
            _rawStatus = axisStatus;
            OnPropertyChanged(nameof(RawStatus));
            OnPropertyChanged(nameof(RawStatusText));

            // Vị trí là trường số nguyên riêng — đúng kể cả khi bảng bit chưa xác minh.
            _rawPulse = actPos;
            CurrentPos = ToMachine(actPos);

            ServoOn = EziAxisStatus.Has(axisStatus, EziAxisStatus.ServoOn);
            Alarm = (axisStatus & EziAxisStatus.AnyErrorMask) != 0;
            Busy = (axisStatus & EziAxisStatus.BusyMask) != 0;
            FLS = EziAxisStatus.Has(axisStatus, EziAxisStatus.HwPosLimit);
            RLS = EziAxisStatus.Has(axisStatus, EziAxisStatus.HwNegLimit);
            ErrorCode = (int)(axisStatus & EziAxisStatus.AnyErrorMask);

            // Servo OFF không làm mất gốc (Tuấn bỏ 2026-09-26) — driver vẫn đếm encoder. Alarm thì vẫn mất.
            if (Alarm) _softHomed = false;

            OrgOk = _softHomed && EziAxisStatus.Has(axisStatus, EziAxisStatus.OriginReturnOk);
        }

        /// <summary>Một vòng đọc lỗi. Chỉ hạ cờ kết nối khi đã lỗi liên tiếp đủ <see cref="FailLimit"/> vòng.</summary>
        private void ReadFailed(string why)
        {
            _failStreak++;
            if (_failStreak < FailLimit) return;
            if (IsConnected)
                LogWorker.Instance.RecordLog(
                    Name + ": mất kết nối sau " + _failStreak + " vòng đọc lỗi liên tiếp (" + why +
                    ") — vẫn thử đọc lại mỗi " + RetryMs + " ms, không cần mở lại app",
                    Utility.Log.LogLevel.Warning, Name);
            IsConnected = false;
        }

        /// <summary>Một vòng đọc được. Báo lại một dòng nếu vừa đi qua một đoạn mất kết nối.</summary>
        private void ReadOk()
        {
            if (!IsConnected && _opened)
                LogWorker.Instance.RecordLog(Name + ": đọc lại được, kết nối trở lại bình thường",
                    Utility.Log.LogLevel.Info, Name);
            _failStreak = 0;
            IsConnected = true;
        }


        // ───────────────────────── quy đổi toạ độ ─────────────────────────

        /// <summary>
        /// Xung thô của driver → toạ độ máy. Với trục quay: cộng offset gốc rồi quấn về [0, 360).
        ///
        /// <para>Cảm biến gốc của A1/A2 <b>không</b> nằm đúng tư thế gắp khay input — nó nằm ở đâu
        /// là do lắp đặt. <see cref="AxisConfig.HomeOffset"/> là góc máy TẠI cảm biến gốc đó, nên
        /// cộng nó vào là đủ để 0° của app trở thành tư thế gắp thật. Cách đo: về gốc, jog tới tư
        /// thế gắp, đọc góc đang hiện là θ, đặt HomeOffset = (360 − θ) mod 360.</para>
        /// </summary>
        private double ToMachine(int pulse)
        {
            double deg = _scale.ToUnit(pulse);
            if (!_cfg.WrapDegrees) return deg;
            return AxisAngle.Wrap(deg + _cfg.HomeOffset);
        }

        /// <summary>
        /// Toạ độ máy → xung tuyệt đối để gửi cho driver.
        ///
        /// <para>Với trục quay, một góc ứng với vô số con số xung cách nhau đúng một vòng. Chọn
        /// con số <b>gần vị trí hiện tại nhất</b> — đó chính là điều làm cho lệnh "xoay tới 10°"
        /// khi đang ở 350° quay +20° chứ không tháo ngược −340°. Điểm mốc <c>basePulse</c> luôn
        /// tính lại từ góc chứ không cộng dồn, nên sai số làm tròn không tích luỹ qua các lần
        /// chạy.</para>
        /// </summary>
        private int TargetPulse(double pos)
        {
            if (!_cfg.WrapDegrees) return _scale.ToPulse(pos);

            int perRev = _scale.PulsePerRev;
            int basePulse = _scale.ToPulse(AxisAngle.Wrap(pos - _cfg.HomeOffset));

            // k = số vòng phải cộng thêm để basePulse về sát vị trí hiện tại. Làm tròn nửa lên,
            // nên đúng nửa vòng thì đi chiều dương — cùng quy ước hoà với AxisAngle.ShortestDelta.
            int k = (int)Math.Floor((_rawPulse - basePulse) / (double)perRev + 0.5);
            return basePulse + k * perRev;
        }

        // ───────────────────────── lệnh ─────────────────────────

        public async Task<bool> SetServoOnAsync(bool on)
        {
            if (!IsConnected) return false;
            try
            {
                int rc;
                lock (_cmdLock) rc = EziMOTIONPlusELib.FAS_ServoEnable(_bdId, on ? 1 : 0);
                if (rc != 0) return false;

                if (!on) return true;   // không làm mất gốc — xem Refresh

                var sw = Stopwatch.StartNew();
                while (sw.ElapsedMilliseconds < 5000)
                {
                    uint st = 0;
                    lock (_cmdLock) EziMOTIONPlusELib.FAS_GetAxisStatus(_bdId, ref st);
                    if (EziAxisStatus.Has(st, EziAxisStatus.ServoOn)) return true;
                    if ((st & EziAxisStatus.AnyErrorMask) != 0) return false;
                    await Task.Delay(20).ConfigureAwait(false);
                }
                return false;
            }
            catch (Exception ex)
            {
                LogWorker.Instance.RecordLog(Name + ": SetServoOn lỗi — " + ex.Message,
                    Utility.Log.LogLevel.Error, Name);
                return false;
            }
        }

        public async Task<bool> HomeAsync(CancellationToken ct)
        {
            if (!IsConnected) return false;

            _softHomed = false;
            OrgOk = false;

            int rc;
            lock (_cmdLock) rc = EziMOTIONPlusELib.FAS_MoveOriginSingleAxis(_bdId);
            if (rc != 0)
            {
                AlarmManager.RaiseAxisEvent(Name, AxisFault.HomeTimeout, Loc.F("Comm.FasReturned", "FAS_MoveOriginSingleAxis", rc));
                return false;
            }

            var sw = Stopwatch.StartNew();
            while (sw.ElapsedMilliseconds < _cfg.HomeTimeoutMs)
            {
                if (ct.IsCancellationRequested) { Stop(); return false; }
                await Task.Delay(20, ct).ConfigureAwait(false);

                uint st = 0;
                lock (_cmdLock) EziMOTIONPlusELib.FAS_GetAxisStatus(_bdId, ref st);

                if ((st & EziAxisStatus.AnyErrorMask) != 0)
                {
                    AlarmManager.RaiseAxisEvent(Name, AxisFault.HomeTimeout,
                        Loc.F("Axis.HomeDriverError", EziAxisStatus.Describe(st)));
                    return false;
                }

                if (EziAxisStatus.Has(st, EziAxisStatus.OriginReturnOk) &&
                    !EziAxisStatus.Has(st, EziAxisStatus.OriginReturning))
                {
                    _softHomed = true;
                    OrgOk = true;
                    LogWorker.Instance.RecordLog(
                        string.Format("{0}: về gốc xong sau {1:0.0} s", Name, sw.Elapsed.TotalSeconds),
                        Utility.Log.LogLevel.Info, Name);
                    return true;
                }
            }

            Stop();
            AlarmManager.RaiseAxisEvent(Name, AxisFault.HomeTimeout, Loc.F("Axis.TimeoutMs", _cfg.HomeTimeoutMs));
            return false;
        }

        public async Task<bool> MoveAbsAsync(double pos, double speed, CancellationToken ct, bool gentle = false)
        {
            if (!IsConnected) return false;

            if (!_cfg.InSoftLimit(pos))
            {
                AlarmManager.RaiseAxisEvent(Name, AxisFault.SoftLimit,
                    Loc.F("Axis.SoftLimitOut",
                        pos, _cfg.Unit, _cfg.SoftLimitNeg, _cfg.SoftLimitPos));
                return false;
            }

            int target = TargetPulse(pos);
            uint vel = _scale.ToPulsePerSec(speed);

            int rc;
            lock (_cmdLock) rc = EziMOTIONPlusELib.FAS_MoveSingleAxisAbsPos(_bdId, target, vel);
            if (rc != 0)
            {
                AlarmManager.RaiseAxisEvent(Name, AxisFault.MoveTimeout, Loc.F("Comm.FasReturned", "FAS_MoveSingleAxisAbsPos", rc));
                return false;
            }

            return await WaitReachedAsync(target, ct).ConfigureAwait(false);
        }

        /// <summary>
        /// Di chuyển TƯƠNG ĐỐI (Inching) — dùng lệnh gốc FAS_MoveSingleAxisIncPos của driver
        /// thay vì tự cộng dồn rồi gọi lại Move Abs (khác Panasonic, chưa có lệnh gốc tương ứng).
        /// Soft limit vẫn kiểm theo đích TUYỆT ĐỐI quy ra từ vị trí hiện tại, và dùng chung vòng
        /// chờ với Move Abs (<see cref="WaitReachedAsync"/>).
        /// </summary>
        public async Task<bool> MoveIncAsync(double delta, double speed, CancellationToken ct)
        {
            if (!IsConnected) return false;

            double pos = CurrentPos + delta;
            if (!_cfg.InSoftLimit(pos))
            {
                AlarmManager.RaiseAxisEvent(Name, AxisFault.SoftLimit,
                    Loc.F("Axis.SoftLimitOut",
                        pos, _cfg.Unit, _cfg.SoftLimitNeg, _cfg.SoftLimitPos));
                return false;
            }

            int incPulse = _scale.ToPulse(delta);
            uint vel = _scale.ToPulsePerSec(speed);
            int target = _rawPulse + incPulse;

            int rc;
            lock (_cmdLock) rc = EziMOTIONPlusELib.FAS_MoveSingleAxisIncPos(_bdId, incPulse, vel);
            if (rc != 0)
            {
                AlarmManager.RaiseAxisEvent(Name, AxisFault.MoveTimeout, Loc.F("Comm.FasReturned", "FAS_MoveSingleAxisIncPos", rc));
                return false;
            }

            return await WaitReachedAsync(target, ct).ConfigureAwait(false);
        }

        /// <summary>
        /// Vòng chờ chung cho Move Abs/Move Inc — cả hai chỉ khác ở lệnh KHỞI ĐỘNG chuyển động,
        /// còn chờ tới đích thì giống hệt nhau.
        ///
        /// <para>So sánh bằng <b>xung</b>, không bằng đơn vị kỹ thuật: với trục quay, toạ độ đã
        /// quấn về [0, 360) nên phép trừ thẳng cho kết quả vô nghĩa ngay tại chỗ nối vòng — đích
        /// 0° và vị trí 359.99° chênh nhau 0.01° thật nhưng trừ ra 359.99. Đối chiếu trên con số
        /// thô thì không có chỗ nối nào để mà sai.</para>
        /// </summary>
        private async Task<bool> WaitReachedAsync(int targetPulse, CancellationToken ct)
        {
            var sw = Stopwatch.StartNew();
            int tol = Math.Max(1, _cfg.PositionTolerancePulse);

            while (sw.ElapsedMilliseconds < _cfg.MoveTimeoutMs)
            {
                if (ct.IsCancellationRequested) { Stop(); return false; }
                await Task.Delay(10, ct).ConfigureAwait(false);

                uint st = 0;
                int actPos = 0;
                lock (_cmdLock)
                {
                    EziMOTIONPlusELib.FAS_GetAxisStatus(_bdId, ref st);
                    EziMOTIONPlusELib.FAS_GetActualPos(_bdId, ref actPos);
                }

                if ((st & EziAxisStatus.AnyErrorMask) != 0)
                {
                    AlarmManager.RaiseAxis(Name, AxisFault.DriverAlarm, EziAxisStatus.Describe(st));
                    return false;
                }
                if (EziAxisStatus.Has(st, EziAxisStatus.HwPosLimit) ||
                    EziAxisStatus.Has(st, EziAxisStatus.HwNegLimit))
                {
                    AlarmManager.RaiseAxis(Name, AxisFault.HardLimit,
                        EziAxisStatus.Has(st, EziAxisStatus.HwPosLimit) ? Loc.T("Axis.HwLimitPos") : Loc.T("Axis.HwLimitNeg"));
                    return false;
                }

                if (!EziAxisStatus.Has(st, EziAxisStatus.Motioning) &&
                    Math.Abs((long)actPos - targetPulse) <= tol)
                {
                    // Ghi luôn con số vừa đọc vào CurrentPos — xem ghi chú dài ở
                    // PanasonicAxis.WaitInPositionAsync (Tuấn phát hiện 2026-09-19).
                    CurrentPos = ToMachine(actPos);
                    return true;
                }
            }

            Stop();
            AlarmManager.RaiseAxisEvent(Name, AxisFault.MoveTimeout,
                Loc.F("Axis.MoveTimeout",
                    _cfg.MoveTimeoutMs, ToMachine(targetPulse), _cfg.Unit, CurrentPos));
            return false;
        }

        /// <summary>
        /// Jog/Stop bị gọi THẲNG từ sự kiện chuột trên UI thread (<c>ManualView.xaml.cs</c>
        /// Jog_Down/Up) — bắn nền bằng <c>Task.Run</c> thay vì gọi thẳng
        /// <c>EziMOTIONPlusELib.FAS_*</c> tại chỗ: card/driver treo (đúng lúc báo lỗi) sẽ khoá
        /// cứng UI thread nếu gọi đồng bộ ở đây, y hệt lỗi vừa sửa ở PanasonicAxis (Tuấn báo lỗi,
        /// 2026-09-08).
        /// </summary>
        public void Jog(int dir, double speed)
        {
            if (!IsConnected) return;
            uint pulsePerSec = _scale.ToPulsePerSec(speed);
            Task.Run(() =>
            {
                try
                {
                    lock (_cmdLock)
                    {
                        if (dir == 0) EziMOTIONPlusELib.FAS_MoveStop(_bdId);
                        else EziMOTIONPlusELib.FAS_MoveVelocity(_bdId, pulsePerSec, dir > 0 ? 1 : 0);
                    }
                }
                catch (Exception ex)
                {
                    LogWorker.Instance.RecordLog(Name + ": Jog lỗi — " + ex.Message,
                        Utility.Log.LogLevel.Error, Name);
                }
            });
        }

        public void Stop()
        {
            if (!IsConnected) return;
            Task.Run(() =>
            {
                try { lock (_cmdLock) EziMOTIONPlusELib.FAS_MoveStop(_bdId); } catch { }
            });
        }

        public async Task<bool> ResetAlarmAsync()
        {
            if (!IsConnected) return false;
            try
            {
                int rc;
                lock (_cmdLock) rc = EziMOTIONPlusELib.FAS_ServoAlarmReset(_bdId);

                _softHomed = false;
                OrgOk = false;

                await Task.Delay(200).ConfigureAwait(false);
                return rc == 0;
            }
            catch { return false; }
        }
    }
}
