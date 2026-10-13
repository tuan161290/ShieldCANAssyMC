using System;
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
    /// Chuỗi về gốc — PLAN.md §4.3, theo nguyên tắc "nâng lên trước, tách ra sau".
    ///
    /// <para>Hai nguy cơ va chạm duy nhất của máy là (a) đầu hút còn thấp mà X/Y chạy ngang,
    /// (b) đầu hút xoay khi còn nằm trong hốc tray. Bước O2 cộng chốt chặn O3 khử cả hai, và
    /// O3 là bước ĐỌC LẠI VỊ TRÍ THỰC chứ không phải tin vào việc lệnh O2 đã trả về thành công.</para>
    /// </summary>
    public class Routine_Origin
    {
        private readonly MotionConnect _motion;
        private readonly McFunction _func;
        private readonly McStatus _status;
        private readonly Func<SystemData> _sys;

        public Routine_Origin(MotionConnect motion, McFunction func, McStatus status, Func<SystemData> sys)
        {
            _motion = motion;
            _func = func;
            _status = status;
            _sys = sys;
        }

        public async Task<bool> RunAsync(CancellationToken ct)
        {
            var sys = _sys();
            _status.OrgOk = false;

            try
            {
                // KHÔNG gọi Abort() ở đây — Abort() tự đặt Message bằng một câu chung chung, đè
                // mất lý do cụ thể mà Fail() đã ghi bên trong PreCheckAsync (ví dụ lý do phanh
                // Z1/Z2). Tự làm phần còn lại của Abort() mà không đụng vào Message.
                if (!await PreCheckAsync().ConfigureAwait(false))
                {
                    _motion.StopAll();
                    _status.OrgOk = false;
                    _status.OriginStep = EOriginState.Aborted;
                    return false;
                }

                Step(EOriginState.O1_ServoOn);
                if (!await ServoOnAllAsync(ct).ConfigureAwait(false)) return Abort(Loc.T("Origin.ServoOnFailed"));

                Step(EOriginState.O2_HomeZ);
                if (!await HomeParallelAsync(AxisId.ZAxes, ct).ConfigureAwait(false)) return Abort(Loc.T("Origin.HomeZFailed"));

                Step(EOriginState.O3_GateZSafe);
                if (!GateZSafe()) return Abort(Loc.T("Origin.ZNotSafe"));

                // A1/A2 đã xác nhận có cảm biến gốc và xoay tự do (§11 #10) nên mặc định về gốc
                // song song — và CÙNG LÚC với X/Y1/Y2 (Tuấn 2026-09-30, không đợi A xong mới chạy X/Y): Z đã qua
                // chốt O3 nên đầu hút ở cao độ an toàn, xoay trong lúc gantry chạy ngang không va gì — Auto cũng
                // xoay A song song với X/Y. Cờ HomeRotateSequential vẫn còn để đổi được nếu sau này gá đầu hút
                // to ra tới mức xoay đồng thời là va nhau — bật cờ thì quay về A tuần tự rồi mới X/Y như cũ.
                if (sys.HomeRotateSequential)
                {
                    Step(EOriginState.O4_HomeA);
                    if (!await HomeSequentialAsync(AxisId.RotateAxes, ct).ConfigureAwait(false)) return Abort(Loc.T("Origin.HomeAFailed"));
                    Step(EOriginState.O5_HomeXY);
                    if (!await HomeParallelAsync(AxisId.PlanarAxes, ct).ConfigureAwait(false)) return Abort(Loc.T("Origin.HomeXYFailed"));
                }
                else
                {
                    Step(EOriginState.O5_HomeXY);
                    if (!await HomeParallelAsync(AxisId.RotateAxes.Concat(AxisId.PlanarAxes).ToArray(), ct).ConfigureAwait(false))
                        return Abort(Loc.T("Origin.HomeAXYFailed"));
                }

                Step(EOriginState.O6_Standby);
                if (!await MoveStandbyAsync(ct).ConfigureAwait(false)) return Abort(Loc.T("Origin.StandbyFailed"));

                Step(EOriginState.O7_Finish);
                _status.OrgOk = _motion.AllHomed;
                LogWorker.Instance.RecordLog(
                    _status.OrgOk ? "Về gốc hoàn tất" : "Về gốc kết thúc nhưng còn trục chưa xong",
                    _status.OrgOk ? Utility.Log.LogLevel.Info : Utility.Log.LogLevel.Warning, "Origin");
                return _status.OrgOk;
            }
            catch (OperationCanceledException)
            {
                return Abort(Loc.T("Mc.Cancelled"));
            }
            catch (Exception ex)
            {
                AlarmManager.RaiseEvent(AlarmCode.RoutineCrashed, "Origin", ex.Message);   // sự kiện: RESET gỡ được (30/09)
                return Abort(Loc.F("Mc.Unexpected", ex.Message));
            }
        }

        /// <summary>
        /// Init (Tuấn 2026-09-29): đưa máy về vị trí chờ mà KHÔNG về gốc lại — nâng Z lên cao độ an toàn trước, rồi mọi trục
        /// về StandbyPos như bước cuối của Về gốc (O6). Dùng sau khi tạm dừng, chuyển MANUAL và thao tác tay, trước khi chạy tiếp.
        /// </summary>
        public async Task<bool> InitAsync(CancellationToken ct)
        {
            try
            {
                if (!_motion.AllHomed) { _status.Message = Loc.T("Mc.InitNotHomed"); return false; }
                if (!await _func.EnsureZSafeAsync(ct).ConfigureAwait(false))
                {
                    _status.Message = Loc.T("Origin.InitZFailed");
                    return false;
                }
                if (!await MoveStandbyAsync(ct).ConfigureAwait(false))
                {
                    _status.Message = Loc.T("Origin.InitStandbyFailed");
                    return false;
                }
                LogWorker.Instance.RecordLog("Init xong — mọi trục ở vị trí chờ", Utility.Log.LogLevel.Info, "Origin");
                return true;
            }
            catch (OperationCanceledException)
            {
                _status.Message = Loc.T("Origin.InitCancelled");
                return false;
            }
            catch (Exception ex)
            {
                // Không bắt thì task lỗi, đoạn chạy sau Init (McControl.DoInit) không chạy và máy kẹt ở trạng thái về gốc.
                AlarmManager.RaiseEvent(AlarmCode.RoutineCrashed, "Origin", ex.Message);
                _status.Message = Loc.F("Origin.InitUnexpected", ex.Message);
                return false;
            }
        }

        // ─────────────── O0 ───────────────

        /// <summary>Toàn bộ danh sách phải PASS mới đi tiếp — PLAN.md §4.2.</summary>
        private Task<bool> PreCheckAsync()
        {
            Step(EOriginState.O0_PreCheck);

            if (!_motion.AllConnected) return Fail(Loc.T("Origin.AxisNotConnected"));
            if (_motion.AnyAlarm) return Fail(Loc.T("Origin.AxisAlarm"));
            if (!_status.EmgOk) return Fail(Loc.T("Mc.BlockEmg"));
            if (!_status.LightCurtainOk) return Fail(Loc.T("Mc.BlockCurtain"));
            if (!_status.AirOk) return Fail(Loc.T("Mc.BlockAir"));

            // Z1/Z2 phải đã qua nghi thức thao tác tay TRƯỚC khi được phép về gốc: vào trang
            // Manual, bật servo, rồi bấm "Mở phanh" — Về gốc KHÔNG được tự bật servo/nhả phanh
            // hộ operator (Tuấn yêu cầu, 2026-09-04). O1 (ServoOnAllAsync) vẫn bật servo cho các
            // trục còn lại như cũ; với Z1/Z2 thì tới đây phải đã bật sẵn rồi.
            if (!_func.BrakeReady(AxisId.Z1) || !_func.BrakeReady(AxisId.Z2))
                return Fail(Loc.T("Origin.BrakeNotReleased"));

            // KHÔNG chặn khi tay hút còn sản phẩm (Tuấn bỏ 2026-09-26): về gốc nâng Z lên trước rồi mới chạy
            // ngang, sản phẩm đi theo tay hút như lúc chạy Auto. Hàng còn trên tay vẫn bị chặn ở START/Arm
            // (ResumeIntegrityOk, NozzleClearPending) cho tới khi xử lý xong.

            if (_status.State == EMcState.Running) return Fail(Loc.T("Origin.AutoRunning"));

            return Task.FromResult(true);
        }

        private Task<bool> Fail(string reason)
        {
            _status.Message = Loc.F("Mc.CannotHome", reason);
            LogWorker.Instance.RecordLog(_status.Message, Utility.Log.LogLevel.Warning, "Origin");
            return Task.FromResult(false);
        }

        // ─────────────── O1 ───────────────

        private async Task<bool> ServoOnAllAsync(CancellationToken ct)
        {
            foreach (var axis in _motion.AxisList)
            {
                if (ct.IsCancellationRequested) return false;
                if (!await axis.SetServoOnAsync(true).ConfigureAwait(false))
                {
                    AlarmManager.RaiseAxisEvent(axis.Name, AxisFault.ServoOffUnexpected, Loc.T("Origin.ServoOnDetail"));
                    return false;
                }
            }

            // Chờ driver báo sẵn sàng thay vì giả định servo on là dùng được ngay.
            var sw = System.Diagnostics.Stopwatch.StartNew();
            while (!_motion.AllServoOn)
            {
                if (ct.IsCancellationRequested) return false;
                if (sw.ElapsedMilliseconds > 5000) return false;
                await Task.Delay(20, CancellationToken.None).ConfigureAwait(false);
            }
            return true;
        }

        // ─────────────── O2 / O4 / O5 ───────────────

        private async Task<bool> HomeParallelAsync(string[] names, CancellationToken ct)
        {
            var axes = names.Select(n => _motion[n]).Where(a => a != null).ToArray();
            if (axes.Length == 0) return true;

            LogWorker.Instance.RecordLog("Về gốc song song: " + string.Join(", ", names),
                Utility.Log.LogLevel.Info, "Origin");

            // Một CancellationTokenSource liên kết: trục nào hỏng thì huỷ luôn các trục còn lại,
            // thay vì để chúng chạy tiếp trong khi máy đã ở trạng thái không xác định.
            using (var linked = CancellationTokenSource.CreateLinkedTokenSource(ct))
            {
                var tasks = axes.Select(async a =>
                {
                    bool ok = await _motion.HomeAsync(a, linked.Token).ConfigureAwait(false);
                    if (!ok) linked.Cancel();
                    return ok;
                }).ToArray();

                var results = await Task.WhenAll(tasks).ConfigureAwait(false);
                return results.All(r => r);
            }
        }

        private async Task<bool> HomeSequentialAsync(string[] names, CancellationToken ct)
        {
            foreach (var name in names)
            {
                var axis = _motion[name];
                if (axis == null) continue;
                if (!await _motion.HomeAsync(axis, ct).ConfigureAwait(false)) return false;
            }
            return true;
        }

        // ─────────────── O3 ───────────────

        /// <summary>
        /// Chốt chặn: đọc LẠI vị trí thực của cả hai trục Z và cờ về gốc. Không đạt thì huỷ toàn
        /// bộ, không đi tiếp. Đây là chỗ duy nhất ngăn X/Y chạy ngang khi đầu hút còn thấp.
        /// </summary>
        private bool GateZSafe()
        {
            var z1 = _motion[AxisId.Z1];
            var z2 = _motion[AxisId.Z2];

            if (z1 == null || z2 == null) return false;
            if (!z1.OrgOk || !z2.OrgOk)
            {
                _status.Message = Loc.T("Origin.GateNotHomed");
                return false;
            }
            if (!_motion.ZSafeOk)
            {
                _status.Message = Loc.F("Origin.GateZPos", z1.CurrentPos, z2.CurrentPos, _motion.ZSafeLimit);
                return false;
            }

            LogWorker.Instance.RecordLog(
                $"Chốt chặn Z đạt: Z1={z1.CurrentPos:0.00}, Z2={z2.CurrentPos:0.00} ≤ {_motion.ZSafeLimit:0.00}",
                Utility.Log.LogLevel.Info, "Origin");
            return true;
        }

        // ─────────────── O6 ───────────────

        private async Task<bool> MoveStandbyAsync(CancellationToken ct)
        {
            foreach (var axis in _motion.AxisList)
            {
                if (ct.IsCancellationRequested) return false;
                var cfg = axis.Config;
                if (Math.Abs(axis.CurrentPos - cfg.StandbyPos) < 0.01) continue;
                if (!await _func.MoveAsync(axis.Name, cfg.StandbyPos, cfg.MoveSpeed, ct).ConfigureAwait(false))
                    return false;
            }
            return true;
        }

        // ─────────────── tiện ích ───────────────

        private void Step(EOriginState step)
        {
            _status.OriginStep = step;
            LogWorker.Instance.RecordLog("Origin " + step, Utility.Log.LogLevel.Info, "Origin");
        }

        /// <summary>
        /// Huỷ giữa chừng — PLAN.md §4.4. Không bao giờ để trạng thái "về gốc dở" được đánh dấu
        /// là đã về gốc: <c>OrgOk</c> phải về false và ghi rõ trục nào ở bước nào gây huỷ.
        /// </summary>
        private bool Abort(string reason)
        {
            _motion.StopAll();
            _status.OrgOk = false;
            _status.Message = Loc.F("Origin.AbortedAtStep", _status.OriginStep, reason);
            _status.OriginStep = EOriginState.Aborted;
            LogWorker.Instance.RecordLog(_status.Message, Utility.Log.LogLevel.Error, "Origin");
            return false;
        }
    }
}
