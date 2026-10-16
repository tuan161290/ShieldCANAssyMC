using System;
using System.Threading;
using ShieldCanAssy.Core.Communication;
using ShieldCanAssy.Core.Data;
using ShieldCanAssy.Core.Infor;
using ShieldCanAssy.Core.Lang;
using ShieldCanAssy.Utility.Log;

namespace ShieldCanAssy.Core.Controller
{
    /// <summary>
    /// Giám sát an toàn — PLAN.md §3.3.
    ///
    /// <para>Chạy trong vòng điều khiển với chu kỳ ≤ 20 ms và có quyền ép trạng thái sang
    /// EMG/ALARM bất cứ lúc nào bằng cách huỷ <see cref="AbortToken"/>. Các routine
    /// <b>không</b> tự kiểm tra an toàn rải rác — chúng chỉ observe token này. Nhờ vậy chỉ có
    /// đúng một chỗ quyết định khi nào máy phải dừng, và thêm một điều kiện an toàn mới không
    /// phải đi sửa mọi routine.</para>
    ///
    /// <para>Nhắc lại: đây là lớp DỪNG CHU TRÌNH, không phải lớp bảo vệ. Việc cắt nguồn động lực
    /// do relay an toàn cứng đảm nhiệm (§3.1).</para>
    ///
    /// <para><b>Cảm biến cửa đã tháo khỏi máy (Tuấn xác nhận, 2026-09-09).</b> Mọi điều kiện an
    /// toàn cửa (DoorA/DoorB, lệch 2 kênh, cấm chuyển động khi cửa mở, ghì tốc độ khi cửa mở) đã
    /// gỡ khỏi lớp này — không còn là bypass tạm thời, mà bỏ hẳn vì phần cứng không còn tồn tại.
    /// EMG và rèm sáng không liên quan gì đến thay đổi này, vẫn giữ nguyên.</para>
    /// </summary>
    public class SafetyMonitor
    {
        private readonly MotionConnect _motion;
        private readonly McStatus _status;
        private readonly Func<SystemData> _sys;

        private CancellationTokenSource _cts = new CancellationTokenSource();
        private readonly object _ctsLock = new object();

        private IIoDevice _io;

        public SafetyMonitor(MotionConnect motion, McStatus status, Func<SystemData> sys)
        {
            _motion = motion;
            _status = status;
            _sys = sys;
        }

        public void AttachIo(IIoDevice io) => _io = io;

        /// <summary>
        /// Gọi trên vòng quét đúng lúc rèm sáng vừa bị cắt, TRƯỚC khi <see cref="SafeToMove"/> tắt — McControl tạm dừng
        /// chu trình như STOP (tắt bit RUN), nên lệnh trục của chu trình đứng chờ chứ không báo lỗi. Xem <c>McControl.HoldForCurtain</c>.
        /// </summary>
        public Action CurtainBroken { get; set; }

        private bool _curtainWasOk = true;

        /// <summary>Token chung mà mọi routine phải observe. Bị huỷ ngay khi có điều kiện dừng.</summary>
        public CancellationToken AbortToken
        {
            get { lock (_ctsLock) return _cts.Token; }
        }

        /// <summary>true khi mọi điều kiện an toàn đều đạt và không có alarm Error/Fatal.</summary>
        public bool SafeToMove { get; private set; }

        /// <summary>Lý do gần nhất khiến máy không được phép chạy — hiện thẳng lên UI. Vòng quét chỉ ghi KHOÁ chữ,
        /// dịch lúc đọc theo ngôn ngữ đang chọn.</summary>
        public string BlockReason => Loc.T(_blockKey);
        private volatile string _blockKey = "Mc.BlockNotInit";

        /// <summary>Cấp token mới sau khi đã xử lý xong lần dừng trước.</summary>
        public void RenewToken()
        {
            lock (_ctsLock)
            {
                if (!_cts.IsCancellationRequested) return;
                _cts.Dispose();
                _cts = new CancellationTokenSource();
            }
        }

        /// <summary>Huỷ do người vận hành (bấm STOP khi đang về gốc), không phải do lỗi an toàn.</summary>
        public void RequestAbort(string reason) => Abort(reason);

        private void Abort(string reason)
        {
            lock (_ctsLock)
            {
                if (_cts.IsCancellationRequested) return;
                _cts.Cancel();
            }
            LogWorker.Instance.RecordLog("Huỷ mọi chuyển động: " + reason,
                Utility.Log.LogLevel.Error, "Safety");
            _motion.StopAll();
        }

        /// <summary>Một vòng đánh giá. Gọi từ vòng điều khiển của McControl.</summary>
        public void Evaluate()
        {
            var sys = _sys();

            bool emgOk = _io != null && _io.IsConnected && _io.GetDi(IoMap.EmgOk);
            bool airOk = !sys.HasAirSensor || (_io != null && _io.GetDi(IoMap.AirOk));
            bool ioOk = _io != null && _io.IsConnected;
            bool curtainOk = !sys.HasLightCurtain || (_io != null && _io.GetDi(IoMap.LightCurtainOk));

            _status.EmgOk = emgOk;
            _status.LightCurtainOk = curtainOk;
            _status.AirOk = airOk;
            _status.IoConnected = ioOk;
            _status.MotionConnected = _motion.AllConnected;

            // ── Card I/O ──
            //
            // Trên máy này EMG và rèm sáng đều đọc qua Ezi-IO (quyết định của chủ đầu tư, xem
            // PLAN.md §3.1). Hệ quả trực tiếp: MẤT KẾT NỐI EZI-IO LÀ MÙ HOÀN TOÀN — không còn
            // đường nào biết EMG có đang bị nhấn hay rèm có bị cắt hay không. Nên mất kết nối
            // phải được xử lý như chính sự cố tệ nhất mà nó che mất: dừng ngay mọi chuyển động,
            // không chỉ chặn lệnh mới.
            if (!ioOk)
            {
                AlarmManager.Raise(AlarmCode.IoDisconnected);
                Abort("mất kết nối card I/O — không còn đọc được EMG và rèm sáng");
            }
            else
            {
                AlarmManager.ClearCondition(AlarmCode.IoDisconnected);
            }

            // ── EMG (NC: 0 = đã nhấn hoặc đứt dây) ──
            if (ioOk && !emgOk)
            {
                AlarmManager.Raise(AlarmCode.EmergencyStop);
                Abort("EMG");
            }
            else
            {
                AlarmManager.ClearCondition(AlarmCode.EmergencyStop);
            }

            // ── Rèm sáng ──
            // Rèm bị cắt thì DỪNG NGAY mọi trục dù đang ở chế độ nào, vì người có thể thò tay qua rèm bất cứ lúc nào — kể
            // cả lúc thợ đang jog tay ở Manual — và chặn lệnh mới tới khi rèm thông. Chu trình Auto thì tạm dừng như STOP,
            // rèm thông rồi bấm Start chạy tiếp đúng chỗ dở (Tuấn 2026-10-01) — không alarm, không RESET. Rèm
            // thông trở lại cũng không tự chạy: phải có người bấm Start.
            // Chỉ ghi Nhật ký khi có trục đang chạy thật sự bị dừng — máy đứng yên mà đi qua rèm thì không ghi (Tuấn 2026-10-07).
            if (ioOk && !curtainOk)
            {
                if (_curtainWasOk)
                {
                    _curtainWasOk = false;
                    if (_motion.AnyBusy)
                        LogWorker.Instance.RecordLog("Rèm sáng bị tác động — dừng mọi chuyển động", Utility.Log.LogLevel.Warning, "Safety");
                    CurtainBroken?.Invoke();
                    _motion.StopAll();
                }
            }
            else if (ioOk)
            {
                _curtainWasOk = true;
            }

            // ── Áp suất khí ──
            if (ioOk && !airOk) AlarmManager.Raise(AlarmCode.AirPressureLow);
            else AlarmManager.ClearCondition(AlarmCode.AirPressureLow);

            // ── Trục có alarm thì dừng luôn, không chờ routine phát hiện ──
            if (_motion.AnyAlarm || !_motion.AllConnected)
            {
                if (_status.State == EMcState.Running || _status.State == EMcState.Homing)
                    Abort("trục báo lỗi hoặc mất kết nối");
            }

            // ── Kết luận ──
            _status.SafetyChainOk = emgOk && curtainOk && airOk;
            SafeToMove = ComputeSafeToMove(sys, ioOk, emgOk, curtainOk, airOk);
        }

        private bool ComputeSafeToMove(SystemData sys, bool ioOk, bool emgOk, bool curtainOk, bool airOk)
        {
            if (!ioOk)              { _blockKey = "Mc.BlockIoLost"; return false; }
            if (!emgOk)             { _blockKey = "Mc.BlockEmg"; return false; }
            if (!curtainOk)         { _blockKey = "Mc.BlockCurtain"; return false; }
            if (AlarmManager.HasError) { _blockKey = "Mc.BlockAlarm"; return false; }
            if (!_motion.AllConnected) { _blockKey = "Mc.BlockAxisLost"; return false; }
            if (_motion.AnyAlarm)   { _blockKey = "Mc.BlockAxisAlarm"; return false; }
            if (!airOk)             { _blockKey = "Mc.BlockAir"; return false; }

            _blockKey = "";
            return true;
        }
    }
}
