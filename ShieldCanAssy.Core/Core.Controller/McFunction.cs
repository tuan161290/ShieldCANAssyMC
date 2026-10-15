using System;
using System.Diagnostics;
using System.Globalization;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using ShieldCanAssy.Core.Communication;
using ShieldCanAssy.Core.Data;
using ShieldCanAssy.Core.Infor;
using ShieldCanAssy.Core.Lang;
using ShieldCanAssy.Utility.Log;

namespace ShieldCanAssy.Core.Controller
{
    /// <summary>
    /// Lớp phát lệnh chuyển động cấp cao. <b>Toàn bộ interlock nằm ở đây</b>, không nằm trong
    /// routine (PLAN.md §5.1) — routine chỉ mô tả trình tự công việc, còn "được phép chạy hay
    /// không" là câu hỏi chỉ có một chỗ trả lời. Thêm một điều kiện chống va mới thì sửa ở đây
    /// là mọi routine đều được bảo vệ.
    /// </summary>
    public class McFunction
    {
        private readonly MotionConnect _motion;
        private readonly SafetyMonitor _safety;
        private readonly McStatus _status;
        private readonly Func<SystemData> _sys;

        /// <summary>Đồng hồ ĐƠN ĐIỆU cho <see cref="WaitSettleAsync"/> — không dùng DateTime.Now,
        /// đổi giờ hệ thống hay giờ mùa hè không được phép làm lệch phép chờ này.</summary>
        private readonly Stopwatch _clock = Stopwatch.StartNew();
        private long _lastMoveEndedMs = long.MinValue;

        public McFunction(MotionConnect motion, SafetyMonitor safety, McStatus status, Func<SystemData> sys)
        {
            _motion = motion;
            _safety = safety;
            _status = status;
            _sys = sys;
        }

        public IIoDevice Io { get; set; }

        // ── Phanh Z1/Z2 (DO qua Ezi-IO, KHÔNG dùng tín hiệu phanh tích hợp của driver
        // Panasonic — Tuấn quyết định, 2026-09-04) ──
        //
        // Phanh KHÔNG tự nhả chỉ vì bật servo — operator phải chủ động vào Manual, bật servo,
        // rồi bấm "Mở phanh" MỘT LẦN; sau đó phanh giữ nhả liên tục xuyên suốt Về gốc VÀ chu
        // trình Auto tiếp theo, vì cờ yêu cầu chỉ mất đi khi Alarm hoặc Servo OFF thật sự xảy ra
        // — không phải vì đổi Mode hay chuyển bước routine (Tuấn yêu cầu, 2026-09-04). Không có
        // đường nào khác (kể cả Routine_Auto/Routine_Origin) tự đặt cờ này hộ operator được.

        /// <summary>Trạng thái phanh của một trục: cờ operator đã bấm "Mở phanh" chưa, đồng hồ
        /// tính thời gian đã nhả liên tục (cho <see cref="BrakeReleaseDelayMs"/>), và cờ nhả phanh
        /// KHẨN CẤP kiểu toggle (<see cref="SetEmergencyBrakeRelease"/>).</summary>
        private class BrakeState
        {
            public volatile bool Requested;
            public volatile bool EmergencyReleaseArmed;
            public readonly Stopwatch ReleaseTimer = new Stopwatch();
        }

        private readonly BrakeState _brakeZ1 = new BrakeState();
        private readonly BrakeState _brakeZ2 = new BrakeState();

        /// <summary>Thời gian tối thiểu từ lúc ra lệnh nhả phanh (DO lên 1) tới lúc coi là ĐÃ NHẢ
        /// THẬT và cho phép di chuyển — phanh cơ có độ trễ nhả vật lý, DO lên 1 không có nghĩa
        /// đã nhả xong ngay lập tức. Ước lượng an toàn, CHƯA đo trên máy thật — chỉnh lại ở P1.</summary>
        private const int BrakeReleaseDelayMs = 300;

        /// <summary>
        /// Nút "Mở phanh" trên trang Manual — operator phải CHỦ ĐỘNG bấm sau khi đã bật servo,
        /// phanh KHÔNG tự nhả chỉ vì ServoOn (Tuấn yêu cầu, 2026-09-04). Gọi được từ bất kỳ
        /// thread nào (UI); vòng quét mới thực sự áp dụng qua <see cref="UpdateBrakeOutputs"/>.
        /// Bấm lúc trục chưa ServoOn không có tác dụng gì — <see cref="UpdateBrakeOutput"/> chỉ
        /// đọc cờ này khi trục đã sẵn sàng, và tự xoá cờ ngay khi mất ServoOn/có Alarm.
        /// </summary>
        public void RequestBrakeRelease(string axisName)
        {
            if (axisName == AxisId.Z1) _brakeZ1.Requested = true;
            else if (axisName == AxisId.Z2) _brakeZ2.Requested = true;
        }

        /// <summary>
        /// Nhả phanh KHẨN CẤP kiểu TOGGLE — để đẩy tay được trục Z ngay cả khi Alarm hoặc Servo
        /// OFF (Tuấn yêu cầu, 2026-09-04): sự cố thường xảy ra đúng lúc servo đã mất, nên đường
        /// bình thường (<see cref="RequestBrakeRelease"/>, đòi ServoOn) không dùng được lúc cần
        /// nhất.
        ///
        /// <para><b>Đổi từ dead-man sang toggle (Tuấn yêu cầu, 2026-09-09, đã xác nhận rủi ro qua
        /// AskUserQuestion).</b> Bật (<paramref name="armed"/>=true) thì phanh nhả và GIỮ NGUYÊN
        /// dù buông tay/UI mất focus — KHÔNG tự khoá lại, chỉ tắt khi gọi lại hàm này với
        /// <paramref name="armed"/>=false. Khác bản dead-man cũ trước đó (đã bỏ): bản cũ tự khoá
        /// lại trong vài trăm ms nếu ngừng "ping" liên tục; bản này im lặng giữ nguyên trạng thái
        /// tới khi operator chủ động tắt — rủi ro trục rơi tự do nếu quên tắt đã được operator
        /// chấp nhận.</para>
        ///
        /// <para>KHÔNG kiểm tra ServoOn/Alarm/SafeToMove — đây chính là điểm khác <see
        /// cref="RequestBrakeRelease"/>, chủ ý để dùng được trong lúc có sự cố. Không tự cho phép
        /// chuyển động bằng động cơ: <see cref="CanMove"/> vẫn từ chối bình thường khi Servo OFF,
        /// nhả khẩn cấp chỉ mở phanh cơ để operator tự đẩy tay, không phát lệnh chạy nào cả.</para>
        /// </summary>
        public void SetEmergencyBrakeRelease(string axisName, bool armed)
        {
            if (axisName == AxisId.Z1) _brakeZ1.EmergencyReleaseArmed = armed;
            else if (axisName == AxisId.Z2) _brakeZ2.EmergencyReleaseArmed = armed;
        }

        public bool IsEmergencyBrakeReleaseArmed(string axisName)
        {
            if (axisName == AxisId.Z1) return _brakeZ1.EmergencyReleaseArmed;
            if (axisName == AxisId.Z2) return _brakeZ2.EmergencyReleaseArmed;
            return false;
        }

        /// <summary>
        /// Điều khiển 2 tín hiệu DO nhả phanh Z1/Z2 — gọi mỗi vòng quét từ McControl.Loop(), đây
        /// là ĐƯỜNG DUY NHẤT ghi hai DO này. Nhả bình thường khi ĐỦ CẢ BA: ServoOn, không Alarm,
        /// VÀ operator đã bấm "Mở phanh" — TỰ ĐỘNG khoá lại và XOÁ LUÔN yêu cầu ngay khi Alarm
        /// hoặc Servo OFF, buộc phải bấm lại lần sau (Tuấn yêu cầu, 2026-09-04). HOẶC nhả khi
        /// đang bật cờ khẩn cấp — nhánh này KHÔNG cần ServoOn/không Alarm, chủ ý để dùng lúc gặp
        /// sự cố (xem <see cref="SetEmergencyBrakeRelease"/>).
        /// </summary>
        public void UpdateBrakeOutputs()
        {
            UpdateBrakeOutput(AxisId.Z1, IoMap.DoBrakeReleaseZ1, _brakeZ1);
            UpdateBrakeOutput(AxisId.Z2, IoMap.DoBrakeReleaseZ2, _brakeZ2);
        }

        private void UpdateBrakeOutput(string axisName, int doBit, BrakeState state)
        {
            if (Io == null) return;

            var axis = _motion[axisName];
            bool ready = axis != null && axis.IsConnected && axis.ServoOn && !axis.Alarm;

            // Mất điều kiện servo là xoá luôn yêu cầu — không được để yêu cầu cũ "sống sót" qua
            // một lần Alarm/ServoOff rồi tự nhả lại khi ServoOn quay về, phải bấm nút lại từ đầu.
            // Đây cũng chính là "đảm bảo xử lý xong lỗi mới cho phép nhả phanh" — Alarm xoá cờ
            // ngay, và cờ chỉ đặt lại được bằng nút bấm tay sau khi operator đã RST xong.
            if (!ready) state.Requested = false;

            bool normalRelease = ready && state.Requested;
            bool emergencyRelease = state.EmergencyReleaseArmed;

            bool release = normalRelease || emergencyRelease;

            if (release) { if (!state.ReleaseTimer.IsRunning) state.ReleaseTimer.Restart(); }
            else state.ReleaseTimer.Reset();

            Io.SetDo(doBit, release);
        }

        /// <summary>Phanh của trục này (nếu có) đã nhả đủ lâu để coi là an toàn di chuyển chưa —
        /// trục không phanh (khác Z1/Z2) luôn true. Public vì <c>Routine_Origin</c> cũng cần hỏi
        /// điều kiện này trước khi cho Về gốc chạy (Tuấn yêu cầu, 2026-09-04).</summary>
        public bool BrakeReady(string axisName)
        {
            BrakeState state = axisName == AxisId.Z1 ? _brakeZ1
                              : axisName == AxisId.Z2 ? _brakeZ2
                              : null;
            return state == null || (state.ReleaseTimer.IsRunning && state.ReleaseTimer.ElapsedMilliseconds >= BrakeReleaseDelayMs);
        }

        /// <summary>
        /// Kiểm tra được phép chạy trục này chưa. Trả về false kèm lý do đọc được để UI hiện
        /// thẳng cho operator thay vì im lặng không phản ứng.
        /// </summary>
        public bool CanMove(string axisName, out string reason)
        {
            reason = null;

            var axis = _motion[axisName];
            if (axis == null) { reason = Loc.F("Mc.NoAxis", axisName); return false; }

            if (!_safety.SafeToMove)
            {
                reason = _safety.BlockReason;
                return false;
            }
            if (!axis.IsConnected)   { reason = Loc.F("Mc.AxisDisconnected", axisName); return false; }
            if (axis.Alarm)          { reason = Loc.F("Mc.AxisAlarm", axisName); return false; }
            if (!axis.ServoOn)       { reason = Loc.F("Mc.AxisServoOff", axisName); return false; }

            // Cảnh báo mở phanh trước khi thao tác (Tuấn yêu cầu, 2026-09-04) — chỉ Z1/Z2 có
            // phanh; DO nhả phanh do UpdateBrakeOutputs tự bật theo ServoOn/Alarm, ở đây chỉ chờ
            // đủ BrakeReleaseDelayMs rồi mới cho di chuyển thật, không suy diễn phanh đã nhả
            // ngay khi ServoOn vừa lên.
            if (!BrakeReady(axisName)) { reason = Loc.F("Mc.AxisBrakeLocked", axisName); return false; }

            // ── Chốt chặn chống va (PLAN.md §5.1) ──
            // X, Y1, Y2 chỉ được chạy khi CẢ HAI trục Z ≤ Z_SAFE + 1 mm. Kiểm tra bằng vị trí
            // thực đọc từ encoder, không phải bằng việc lệnh nâng Z trước đó đã trả về thành công.
            if (AxisId.PlanarAxes.Contains(axisName) && !_motion.ZSafeOk)
            {
                var z1 = _motion[AxisId.Z1];
                var z2 = _motion[AxisId.Z2];
                reason = Loc.F("Mc.ZNotSafe", z1?.CurrentPos, z2?.CurrentPos, _motion.ZSafeLimit);
                return false;
            }

            return true;
        }

        /// <summary>
        /// Cùng chốt chặn với <see cref="CanMove"/>, cộng thêm một điều kiện riêng cho nút "Về
        /// gốc"/"Move Abs" bấm RIÊNG LẺ từng trục ở trang Manual (<c>AxisRowVm</c>): Z1 VÀ Z2
        /// phải đã <c>OrgOk</c> (đã về gốc THẬT), không chỉ đang đọc vị trí "trong vùng an toàn"
        /// theo <see cref="MotionConnect.ZSafeOk"/>. CurrentPos của một trục CHƯA về gốc là số
        /// rác từ lúc bật nguồn — ZSafeOk theo nó có thể ĐÚNG NGẪU NHIÊN dù Z thật không hề an
        /// toàn. Cùng nguyên tắc với <c>Routine_Origin.GateZSafe()</c> (O3, PLAN.md §4.3), áp
        /// cho thao tác TAY từng trục một trên trang Manual.
        ///
        /// <para><b>CỐ Ý không đưa vào <see cref="MoveAsync"/></b> — hàm đó dùng chung cho cả
        /// <c>Routine_Auto</c>/<c>Routine_Calib</c>, những nơi tự quản lý trình tự Z/X/Y của
        /// riêng mình và có thể hợp lệ gọi MoveAsync đúng lúc một trục Z đang tạm mất OrgOk giữa
        /// chừng (ví dụ nhánh NG/Resume). Áp thẳng vào MoveAsync từng làm Routine_Auto mô phỏng
        /// bị treo (Tuấn yêu cầu, 2026-09-07 — phát hiện lúc kiểm bằng self-test). Chỉ
        /// <c>AxisRowVm.HomeCommand</c>/<c>MoveCommand</c> gọi hàm này trước khi tự đi tiếp; Jog
        /// và Inching (Move Increment) vẫn dùng thẳng <see cref="CanMove"/> — operator giữ tay/
        /// bấm từng bước nhỏ và tự dừng được ngay lúc thấy bất thường, không đi tự động tới đích
        /// như Về gốc/Move Abs.</para>
        /// </summary>
        public bool CanMoveAutonomous(string axisName, out string reason)
        {
            if (!CanMove(axisName, out reason)) return false;
            if (!_runBit) { reason = HeldReason; return false; }

            if (AxisId.PlanarAxes.Contains(axisName))
            {
                var z1 = _motion[AxisId.Z1];
                var z2 = _motion[AxisId.Z2];
                if (z1 == null || !z1.OrgOk || z2 == null || !z2.OrgOk)
                {
                    reason = Loc.F("Mc.ZMustHomeFirst", axisName);
                    return false;
                }
            }

            return true;
        }

        public Task<bool> MoveAsync(string axisName, double pos, double speed, CancellationToken ct)
            => MoveHoldableAsync(axisName, pos, speed, ct, true);

        /// <summary>Như <see cref="MoveAsync"/> nhưng gia tốc giảm theo tốc độ lệnh — xem <c>IAxis.MoveAbsAsync(gentle)</c>.</summary>
        public Task<bool> MoveGentleAsync(string axisName, double pos, double speed, CancellationToken ct)
            => MoveHoldableAsync(axisName, pos, speed, ct, true, true);

        // ── Bit RUN (Tuấn 2026-09-29) ──
        //
        // STOP tắt bit: mọi trục nhận lệnh dừng ngay, chu trình KHÔNG thoát mà đứng chờ — ở đầu vòng Routine_Auto, hoặc
        // ngay trong lệnh chạy đang dở. Start bật lại bit thì lệnh dở đó được phát lại tới đúng đích cũ, nên chu trình
        // không bao giờ phải làm lại một bước từ giữa chừng (Z đang hạ, van hút đang mở…). Chỉ trục dừng — hút, thổi,
        // soi đang làm thì làm nốt.

        private readonly object _runLock = new object();
        private volatile bool _runBit = true;
        private CancellationTokenSource _runCts = new CancellationTokenSource();

        public bool RunBit => _runBit;

        /// <summary>
        /// Luồng đang chạy là chu trình Auto/chạy rà — chỉ lệnh của chu trình được chờ bit RUN. Lệnh bấm tay (Teaching,
        /// Manual, Calib) lúc tạm dừng bị từ chối, không xếp hàng rồi chạy cùng lúc với chu trình khi bấm Start.
        /// </summary>
        private static readonly AsyncLocal<bool> _inCycle = new AsyncLocal<bool>();

        /// <summary>Gọi ở đầu RunAsync của chu trình — đánh dấu mọi lệnh chạy trục phát ra từ luồng đó.</summary>
        public static void MarkCycleFlow() { _inCycle.Value = true; }

        private static string HeldReason => Loc.T("Mc.HeldReason");

        public void SetRunBit(bool on)
        {
            lock (_runLock)
            {
                if (_runBit == on) return;
                _runBit = on;
                if (on) { _runCts.Dispose(); _runCts = new CancellationTokenSource(); }
                else _runCts.Cancel();
            }
            if (!on) _motion.StopAll();
        }

        /// <summary>Chờ tới khi bit RUN bật.</summary>
        public async Task WaitRunBitAsync(CancellationToken ct)
        {
            while (!_runBit) await Task.Delay(20, ct).ConfigureAwait(false);
        }

        /// <summary>
        /// Một lệnh chạy tuyệt đối theo bit RUN: bit tắt thì chờ, bị tắt giữa hành trình thì chờ bật lại rồi phát lại
        /// đúng lệnh cũ. <paramref name="interlock"/> = false chỉ cho lệnh nâng Z của <see cref="EnsureZSafeAsync"/>.
        /// </summary>
        private async Task<bool> MoveHoldableAsync(string axisName, double pos, double speed, CancellationToken ct, bool interlock, bool gentle = false)
        {
            if (!_runBit && !_inCycle.Value)
            {
                _status.Message = Loc.F("Mc.CannotMoveAxis", axisName, HeldReason);
                return false;
            }

            var axis = _motion[axisName];
            while (true)
            {
                await WaitRunBitAsync(ct).ConfigureAwait(false);

                string reason;
                if (interlock && !CanMove(axisName, out reason))
                {
                    // Rèm sáng vừa bị cắt giữa lúc qua chờ bit và lúc xét chốt: bit RUN đã tắt trước khi SafeToMove tắt
                    // (SafetyMonitor.CurtainBroken), nên quay lại chờ Start như STOP thay vì báo lỗi dừng chu trình.
                    if (!_runBit && _inCycle.Value) continue;
                    _status.Message = Loc.F("Mc.CannotMoveAxis", axisName, reason);
                    LogWorker.Instance.RecordLog(_status.Message, Utility.Log.LogLevel.Warning, axisName);
                    return false;
                }

                CancellationTokenSource run;
                lock (_runLock) run = _runCts;
                bool ok;
                try
                {
                    using (var linked = CancellationTokenSource.CreateLinkedTokenSource(ct, run.Token))
                        ok = await axis.MoveAbsAsync(pos, speed, linked.Token, gentle).ConfigureAwait(false);
                }
                catch (OperationCanceledException) when (!ct.IsCancellationRequested && run.IsCancellationRequested)
                {
                    ok = false;
                }
                MarkMoveEnded();

                if (ok) return true;
                if (ct.IsCancellationRequested || !run.IsCancellationRequested) return false;
                // Bit RUN tắt giữa hành trình: quay lại chờ bật rồi phát lại lệnh.
            }
        }

        /// <summary>
        /// Ghi mốc thời gian vừa kết thúc một lệnh chạy. Đánh dấu cả khi lệnh THẤT BẠI: trục vẫn
        /// đã nhúc nhích rồi dừng, vẫn còn rung như thường.
        /// </summary>
        private void MarkMoveEnded() { _lastMoveEndedMs = _clock.ElapsedMilliseconds; }

        /// <summary>
        /// Còn phải chờ bao nhiêu ms nữa cho đủ <paramref name="settleMs"/> kể từ lúc lệnh chạy
        /// cuối cùng kết thúc. Tách thành hàm thuần để bài tự kiểm tra khoá được luật mà không
        /// phải đo thời gian thật (đo thời gian là nguồn của những bài chập chờn).
        /// </summary>
        public static int SettleRemainingMs(long lastMoveEndedMs, long nowMs, int settleMs)
        {
            if (settleMs <= 0 || lastMoveEndedMs == long.MinValue) return 0;
            long left = settleMs - (nowMs - lastMoveEndedMs);
            if (left <= 0) return 0;
            return left > settleMs ? settleMs : (int)left;
        }

        /// <summary>
        /// Chờ cơ khí thôi rung sau lệnh chạy cuối cùng, trước khi ĐỌC ENCODER và kích Flow có
        /// camera <paramref name="cam"/> — xem <see cref="SystemData.BottomSettleMs"/>. Gọi TRƯỚC khi dựng bộ biến toàn
        /// cục, không phải trước lúc kích Flow: con số encoder ghi vào biến phải tả đúng vị trí
        /// lúc camera phơi sáng, mà nó được đọc ở bước dựng biến.
        /// </summary>
        public async Task WaitSettleAsync(EVmCamera cam, CancellationToken ct)
        {
            int left = SettleRemainingMs(_lastMoveEndedMs, _clock.ElapsedMilliseconds, _sys().SettleMsFor(cam));
            if (left > 0) await Task.Delay(left, ct).ConfigureAwait(false);
        }

        /// <summary>
        /// Điểm giảm tốc có được dùng hay không. Tách riêng để bài tự kiểm tra khoá được luật này
        /// mà không phải đo thời gian chạy (đo thời gian là nguồn của những bài chập chờn).
        ///
        /// <para>Z dương là chiều ĐI XUỐNG, nên chặng giảm tốc chỉ có nghĩa khi điểm giảm tốc nằm
        /// HẲN giữa chỗ đang đứng và đích: chưa teach (≤ 0), teach nhầm xuống dưới cả đích, hoặc Z
        /// đã ở dưới nó rồi thì đều hạ một mạch.</para>
        /// </summary>
        public static bool UseTwoStageZ(double now, double slowAt, double target)
        {
            return slowAt > 0 && now < slowAt && slowAt < target;
        }

        /// <summary>
        /// Chặng chậm có được dùng hay không khi ĐI LÊN. Đi lên là đi ngược, nên dùng đúng phép xét của chiều
        /// xuống với "chỗ đang đứng" và "đích" đổi chỗ cho nhau — một luật, không phải hai, nên không có đường
        /// nào để hai chiều lệch nhau về sau.
        /// </summary>
        public static bool UseTwoStageZUp(double now, double slowAt, double target)
        {
            return UseTwoStageZ(target, slowAt, now);
        }

        /// <summary>
        /// Hạ một trục Z xuống cao độ làm việc (hút hoặc đặt) qua HAI chặng: mức Auto CAO tới
        /// <paramref name="slowAt"/>, rồi mức Auto THẤP nốt đoạn còn lại (Tuấn yêu cầu 2026-09-18).
        /// Cả hai chặng đều đi qua <see cref="MoveAsync"/> nên vẫn đủ chốt chặn CanMove như mọi
        /// lệnh chạy khác.
        ///
        /// <para>Không dùng được điểm giảm tốc (xem <see cref="UseTwoStageZ"/>) thì hạ một mạch ở
        /// mức cao — đúng hành vi trước khi có tính năng này.</para>
        ///
        /// <para>Chỉ dùng cho chiều ĐI XUỐNG. Nâng Z lên vẫn chạy thẳng mức cao: lúc đó đầu hút
        /// rời khỏi sản phẩm, không còn gì để va.</para>
        /// </summary>
        public async Task<bool> MoveZDownAsync(string zAxis, double target, double slowAt, CancellationToken ct)
        {
            var cfg = _sys().GetAxis(zAxis);
            var axis = _motion[zAxis];
            double now = axis != null ? axis.CurrentPos : double.NaN;

            if (!UseTwoStageZ(now, slowAt, target))
                return await MoveAsync(zAxis, target, cfg.MoveSpeed, ct).ConfigureAwait(false);

            if (!await MoveAsync(zAxis, slowAt, cfg.MoveSpeed, ct).ConfigureAwait(false)) return false;
            return await MoveAsync(zAxis, target, cfg.MoveSpeedLow, ct).ConfigureAwait(false);
        }

        /// <summary>
        /// Nâng Z RA KHỎI hốc tray theo hai mức — <b>gương của <see cref="MoveZDownAsync"/></b>: chạy mức Auto
        /// THẤP từ cao độ hút/đặt lên tới điểm giảm tốc, rồi mức Auto CAO nốt đoạn còn lại (Tuấn yêu cầu
        /// 2026-09-24).
        ///
        /// <para><b>Vì sao nâng cũng phải chậm.</b> Lý do y hệt chiều xuống, chỉ khác dấu: nhấc nhanh khỏi hốc
        /// thì lúc hút sản phẩm dễ xê dịch trên đầu hút (cùng mối lo đã sinh ra
        /// <see cref="SystemData.VacuumSettleMs"/>), còn lúc đặt thì luồng khí và cái gờ dễ kéo theo con vừa
        /// nhả. Ra khỏi miệng hốc rồi thì không còn gì để va, nên đoạn trên chạy nhanh.</para>
        ///
        /// <para>Chưa teach điểm giảm tốc (= 0), hoặc điểm đó không nằm giữa chỗ đang đứng và đích, thì nâng
        /// một mạch ở mức cao — đúng hành vi trước 24/09.</para>
        /// </summary>
        public async Task<bool> MoveZUpAsync(string zAxis, double target, double slowAt, CancellationToken ct)
        {
            var cfg = _sys().GetAxis(zAxis);
            var axis = _motion[zAxis];
            double now = axis != null ? axis.CurrentPos : double.NaN;

            if (!UseTwoStageZUp(now, slowAt, target))
                return await MoveAsync(zAxis, target, cfg.MoveSpeed, ct).ConfigureAwait(false);

            if (!await MoveAsync(zAxis, slowAt, cfg.MoveSpeedLow, ct).ConfigureAwait(false)) return false;
            return await MoveAsync(zAxis, target, cfg.MoveSpeed, ct).ConfigureAwait(false);
        }

        /// <summary>Di chuyển TƯƠNG ĐỐI (Inching) — dùng cho nút Move −/Move + ở trang Manual,
        /// tiện nudge từng bước nhỏ lúc Calibration. Cùng chốt chặn CanMove với
        /// <see cref="MoveAsync"/>; chỉ khác ở chỗ IAxis tự quyết định lệnh gốc tương ứng
        /// (xem <see cref="IAxis.MoveIncAsync"/>).</summary>
        public async Task<bool> MoveIncAsync(string axisName, double delta, double speed, CancellationToken ct)
        {
            string reason = _runBit ? null : HeldReason;
            if (reason != null || !CanMove(axisName, out reason))
            {
                _status.Message = Loc.F("Mc.CannotMoveAxis", axisName, reason);
                LogWorker.Instance.RecordLog(_status.Message, Utility.Log.LogLevel.Warning, axisName);
                return false;
            }

            var axis = _motion[axisName];
            bool ok = await axis.MoveIncAsync(delta, speed, ct).ConfigureAwait(false);
            MarkMoveEnded();
            return ok;
        }

        /// <summary>
        /// Chọn Nhanh/Chậm cho jog tay — CHUNG cho cả 7 trục, không phải cấu hình riêng từng
        /// trục (Tuấn yêu cầu, 2026-09-05). Chỉ ảnh hưởng <see cref="Jog"/> khi gọi không kèm
        /// speed rõ ràng; Về gốc/MoveAbs/Auto không đọc cờ này.
        /// </summary>
        public bool JogFast { get; set; }

        public void Jog(string axisName, int dir, double speed = 0)
        {
            string reason;
            if (dir != 0 && !_runBit)
            {
                _status.Message = Loc.F("Mc.CannotJogAxis", axisName, HeldReason);
                return;
            }
            if (dir != 0 && !CanMove(axisName, out reason))
            {
                _status.Message = Loc.F("Mc.CannotJogAxis", axisName, reason);
                return;
            }

            var axis = _motion[axisName];
            if (axis == null) return;

            double v = speed > 0 ? speed : (JogFast ? axis.Config.JogSpeedFast : axis.Config.JogSpeedSlow);
            axis.Jog(dir, v);
        }

        /// <summary>
        /// Nâng cả hai trục Z lên trên cao độ an toàn. Đây là điều kiện tiên quyết của mọi
        /// chuyển động ngang, nên nó có hàm riêng thay vì để mỗi routine tự viết lại.
        /// </summary>
        public async Task<bool> EnsureZSafeAsync(CancellationToken ct)
        {
            if (_motion.ZSafeOk) return true;

            // "An toàn" là vị trí NHỎ (đã rút lên) — xem MotionConnect.ZSafeOk. Dừng đúng Z_SAFE: chốt chặn
            // đã nới MotionConnect.ZSafeMargin nên không đứng ngay biên.
            double target = _motion.ZSafe;
            var tasks = AxisId.ZAxes
                .Select(n => _motion[n])
                .Where(a => a != null && a.CurrentPos > target)
                .Select(a => MoveHoldableAsync(a.Name, target, a.Config.MoveSpeed, ct, false))
                .ToArray();

            if (tasks.Length == 0) return _motion.ZSafeOk;

            var results = await Task.WhenAll(tasks).ConfigureAwait(false);
            return results.All(r => r) && _motion.ZSafeOk;
        }

        /// <summary>
        /// Di chuyển tới một ô: nâng Z an toàn trước, rồi chạy trục hàng và trục cột song song.
        /// Interlock Z_SAFE đã nằm trong <see cref="CanMove"/> nên quên bước nâng cũng không va
        /// — nhưng vẫn nâng chủ động để không tạo ra lỗi vô cớ.
        ///
        /// <para>Dùng chung cho <c>Routine_Auto</c> (hút/đặt) và trang Teaching (chạy thử tới ô
        /// vừa tính) — cùng một thao tác "tới toạ độ (X, hàng) trong mặt phẳng tray", không có
        /// lý do viết hai lần.</para>
        /// </summary>
        /// <param name="slowRow">Trục hàng chạy mức Auto thấp, gia tốc giảm theo — như Y2 tới điểm đặt đã bù trong
        /// <c>Routine_Auto.PlaceAsync</c>. Các nút tới điểm đặt/điểm đích ở trang Vision Edit bật cờ này.</param>
        public async Task<bool> MoveToCellAsync(string rowAxis, double rowPos, double colPos, CancellationToken ct, bool slowRow = false)
        {
            if (!await EnsureZSafeAsync(ct).ConfigureAwait(false)) return false;

            var t1 = MoveRowAsync(rowAxis, rowPos, slowRow, ct);
            var t2 = MoveAsync(AxisId.X, colPos, _sys().GetAxis(AxisId.X).MoveSpeed, ct);
            var r = await Task.WhenAll(t1, t2).ConfigureAwait(false);
            return r[0] && r[1];
        }

        /// <summary>
        /// Như <see cref="MoveToCellAsync"/> nhưng xoay luôn trục A của tay <paramref name="pickNozzle"/> về
        /// <b>góc hút</b> <paramref name="pickAngleDeg"/> (<see cref="Data.Model.ModelData.PickAngle"/>, teach ở
        /// trang Teaching). Gọi ở MỌI chỗ hạ Z xuống hút sản phẩm.
        ///
        /// <para><b>Vì sao bắt buộc.</b> Đầu hút có gờ ôm lấy sản phẩm. Xuống hút trong lúc A còn đang ở góc
        /// xoay của lệnh đặt trước đó thì gờ không ăn khớp với sản phẩm nằm trong hốc tray: đầu hút kênh, hở
        /// khí, chân không không lên tới giá trị đặt trên đồng hồ và không nhấc được sản phẩm (Tuấn nêu
        /// 2026-09-23). Trước ngày đó không chỗ nào đưa A về 0 — chu trình Auto đặt xong để A nguyên ở
        /// <c>CmdA</c> rồi đi hút con tiếp theo bằng chính góc ấy.</para>
        ///
        /// <para>Xoay SAU khi Z đã lên cao độ an toàn — xoay lúc đầu hút còn nằm trong hốc tray là cạ vào
        /// thành hốc — và chạy SONG SONG với X/Y nên không tốn thêm thời gian chu kỳ. Trục quay tự đi đường
        /// ngắn (<see cref="AxisAngle"/>), nên A đang ở 350° mà góc hút là 0 thì lùi 10°, không quay gần
        /// trọn vòng.</para>
        /// </summary>
        public async Task<bool> MoveToCellForPickAsync(int pickNozzle, double pickAngleDeg, string rowAxis, double rowPos, double colPos,
                                                       CancellationToken ct, bool slowRow = false)
        {
            if (!await EnsureZSafeAsync(ct).ConfigureAwait(false)) return false;

            string aName = pickNozzle == 1 ? AxisId.A1 : AxisId.A2;
            var t1 = MoveRowAsync(rowAxis, rowPos, slowRow, ct);
            var t2 = MoveAsync(AxisId.X, colPos, _sys().GetAxis(AxisId.X).MoveSpeed, ct);
            var t3 = MoveAsync(aName, AxisAngle.Wrap(pickAngleDeg), _sys().GetAxis(aName).MoveSpeed, ct);
            var r = await Task.WhenAll(t1, t2, t3).ConfigureAwait(false);
            return r[0] && r[1] && r[2];
        }

        private Task<bool> MoveRowAsync(string rowAxis, double pos, bool slow, CancellationToken ct)
        {
            var cfg = _sys().GetAxis(rowAxis);
            return slow ? MoveGentleAsync(rowAxis, pos, cfg.MoveSpeedLow, ct) : MoveAsync(rowAxis, pos, cfg.MoveSpeed, ct);
        }

        public void StopAll() => _motion.StopAll();

        // ── I/O nghiệp vụ ──
        //
        // Van hút KHÔNG bị cắt khi EMG (§11 #11 đã xác nhận mạch khí giữ nguyên trạng thái), và
        // phần mềm cũng không được tự tắt nó ở bất kỳ đường dừng nào — SafetyMonitor.Abort,
        // McControl.DoStop và Routine_Origin.Abort đều chỉ dừng trục. Cắt chân không lúc máy
        // đang dừng khẩn là làm rơi sản phẩm vào trong máy: hỏng hàng, và thành vật cản cho
        // lần về gốc kế tiếp. Sản phẩm còn trên tay hút được xử lý có kiểm soát ở §3.7.

        public void SetVacuum(int nozzle, bool on)
        {
            if (Io == null) return;
            Io.SetDo(nozzle == 1 ? IoMap.DoVacuum1 : IoMap.DoVacuum2, on);
            if (on) Io.SetDo(nozzle == 1 ? IoMap.DoBlow1 : IoMap.DoBlow2, false);
        }

        public void SetBlow(int nozzle, bool on)
        {
            if (Io == null) return;
            if (on) Io.SetDo(nozzle == 1 ? IoMap.DoVacuum1 : IoMap.DoVacuum2, false);
            Io.SetDo(nozzle == 1 ? IoMap.DoBlow1 : IoMap.DoBlow2, on);
        }

        public bool HasProduct(int nozzle)
            => Io != null && Io.GetDi(nozzle == 1 ? IoMap.Vacuum1 : IoMap.Vacuum2);

        /// <summary>Cảm biến có tray đầu vào thứ <paramref name="tray"/> (0 = Tray 1 phía trong, 1 = Tray 2 phía ngoài).</summary>
        public bool HasTrayIn(int tray) => Io != null && Io.GetDi(tray == 1 ? IoMap.TrayIn2Present : IoMap.TrayIn1Present);
        public bool HasTrayAssy() => Io != null && Io.GetDi(IoMap.TrayAssyPresent);
    }
}
