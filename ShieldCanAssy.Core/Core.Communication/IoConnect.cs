using System;
using System.Diagnostics;
using System.Net;
using FASTECH;
using ShieldCanAssy.Core.Data;
using ShieldCanAssy.Core.Infor;
using ShieldCanAssy.Core.Lang;
using ShieldCanAssy.Lib.Fastech;
using ShieldCanAssy.Utility;
using ShieldCanAssy.Utility.Log;

namespace ShieldCanAssy.Core.Communication
{
    /// <summary>
    /// Card I/O Ezi-IO Ethernet thật: <b>hai module riêng</b>, 32Di và 32Do, mỗi cái một IP và
    /// một <c>iBdID</c> (xem <see cref="FastechBoardId"/>).
    ///
    /// <para><b>Ảnh DI chụp một lần mỗi vòng quét.</b> Toàn bộ code phía trên đọc từ ảnh đó chứ
    /// không gọi UDP từng bit. Ngoài chuyện tốc độ, điều này bảo đảm mọi kiểm tra trong cùng
    /// một vòng nhìn thấy <b>cùng một thời điểm</b> — đọc EMG và đọc cửa cách nhau vài ms có
    /// thể cho ra một tổ hợp trạng thái chưa từng tồn tại thật.</para>
    ///
    /// <para><b>DO ghi theo mặt nạ, chỉ ghi khi có thay đổi.</b> <c>FAS_SetOutput</c> nhận cặp
    /// mặt nạ set/clear nên ghi được cả 32 bit trong một gói. <b>Chưa đọc được ảnh DO module
    /// đang giữ thì không ghi gì</b> — xem <see cref="SyncOutputsFromModule"/>.</para>
    ///
    /// <para><b>Mất kết nối là điều kiện dừng khẩn</b> (PLAN.md §3.1): EMG và rèm sáng đều đọc
    /// qua card này, nên mất nó là mù hoàn toàn. Lớp này chỉ báo <see cref="IsConnected"/> =
    /// false; việc dừng máy do <c>SafetyMonitor</c> quyết định.</para>
    /// </summary>
    public class IoConnect : Bindable, IIoDevice
    {
        private readonly SystemData _sys;
        private readonly object _lock = new object();

        private readonly bool[] _di = new bool[IoMap.DiCount];
        private readonly bool[] _diPrev = new bool[IoMap.DiCount];
        private readonly bool[] _do = new bool[IoMap.DoCount];

        private uint _doWanted;
        private uint _doWritten;
        private bool _doDirty = true;

        /// <summary>Đã đọc ảnh DO thật từ module chưa. Chưa đọc thì <see cref="Flush"/> không
        /// được ghi: <c>_doWanted</c> lúc đó chỉ là số 0 mặc định, ghi xuống là tắt hết.</summary>
        private bool _doSynced;

        private bool _diOk, _doOk;
        private int _failStreak;

        /// <summary>Số vòng quét lỗi liên tiếp trước khi coi là mất kết nối. Một gói UDP rơi là
        /// bình thường; ba vòng liên tiếp thì không.</summary>
        private const int FailLimit = 3;

        public IoConnect(SystemData sys)
        {
            _sys = sys;
        }

        private bool _connected;
        public bool IsConnected { get { return _connected; } private set { SetProperty(ref _connected, value); } }

        // ───────────────────────── kết nối ─────────────────────────

        public bool Connect()
        {
            _diOk = Open(_sys.IoDiIpAddress, FastechBoardId.Io32Di, "32Di");
            _doOk = Open(_sys.IoDoIpAddress, FastechBoardId.Io32Do, "32Do");

            // Đọc lại ảnh DO module đang giữ TRƯỚC khi ghi bất cứ gì xuống (Tuấn phát hiện,
            // 2026-09-13: khởi động lại app thì mọi DO về 0). Bản cũ xoá sạch ở đây với lý do
            // "lúc khởi tạo chưa có sản phẩm trên tay hút" — sai từ khi Auto chạy tiếp được sau
            // khi mở lại app: tắt van hút lúc này là thả rơi sản phẩm vào trong máy, đúng điều
            // §3.1 quy tắc 3 cấm. DO nào được tính lại mỗi vòng quét (đèn tháp, còi, đèn nút,
            // nhả phanh) thì vòng quét đầu tự ghi đè; DO nghiệp vụ (hút, thổi, đèn camera) giữ
            // nguyên tới khi phần mềm chủ động đổi.
            lock (_lock) _doSynced = false;
            if (_doOk) _doOk = SyncOutputsFromModule(true);

            IsConnected = _diOk && _doOk;
            _failStreak = 0;
            return IsConnected;
        }

        /// <summary>
        /// Lấy ảnh DO module đang giữ làm điểm xuất phát cho <c>_doWanted</c>, để lần ghi đầu
        /// tiên chỉ đổi đúng những bit phần mềm chủ động đổi. Đọc hỏng (rơi gói UDP) thì
        /// <see cref="Refresh"/> đọc lại ở vòng sau; trong lúc đó không ghi DO nào và card bị
        /// coi là mất kết nối, giống như khi ghi hỏng.
        /// </summary>
        private bool SyncOutputsFromModule(bool logFailure)
        {
            uint output = 0, status = 0;
            int rc;
            try { rc = EziMOTIONPlusELib.FAS_GetOutput(FastechBoardId.Io32Do, ref output, ref status); }
            catch { rc = -1; }

            if (rc != 0)
            {
                if (logFailure)
                    LogWorker.Instance.RecordLog(
                        "Ezi-IO 32Do: không đọc được ngõ ra hiện tại (mã " + rc + ") — chưa ghi DO nào, đọc lại ở vòng quét",
                        Utility.Log.LogLevel.Error, "IO");
                return false;
            }

            lock (_lock)
            {
                _doWanted = output;
                _doWritten = output;
                _doDirty = false;
                _doSynced = true;
                for (int i = 0; i < IoMap.DoCount; i++)
                    _do[i] = (output & (1u << i)) != 0;
            }

            LogWorker.Instance.RecordLog(
                "Ezi-IO 32Do: giữ nguyên ngõ ra đang có trên module — 0x" + output.ToString("X8"),
                Utility.Log.LogLevel.Info, "IO");
            return true;
        }

        private bool Open(string ipText, int bdId, string what)
        {
            IPAddress ip;
            if (!IPAddress.TryParse(ipText, out ip))
            {
                LogWorker.Instance.RecordLog("Ezi-IO " + what + ": IP không hợp lệ — " + ipText,
                    Utility.Log.LogLevel.Error, "IO");
                return false;
            }

            try
            {
                EziMOTIONPlusELib.FAS_Close(bdId);
                if (!EziMOTIONPlusELib.FAS_Connect(ip, bdId))
                {
                    LogWorker.Instance.RecordLog("Ezi-IO " + what + ": không kết nối được " + ipText,
                        Utility.Log.LogLevel.Error, "IO");
                    return false;
                }

                LogWorker.Instance.RecordLog(
                    "Ezi-IO " + what + ": đã kết nối " + ipText + " (bdID " + bdId + ")",
                    Utility.Log.LogLevel.Info, "IO");
                return true;
            }
            catch (DllNotFoundException)
            {
                LogWorker.Instance.RecordLog("Ezi-IO " + what + ": không tìm thấy EziMOTIONPlusE.dll cạnh exe",
                    Utility.Log.LogLevel.Error, "IO");
                return false;
            }
            catch (Exception ex)
            {
                LogWorker.Instance.RecordLog("Ezi-IO " + what + ": lỗi kết nối — " + ex.Message,
                    Utility.Log.LogLevel.Error, "IO");
                return false;
            }
        }

        public void Disconnect()
        {
            try { EziMOTIONPlusELib.FAS_Close(FastechBoardId.Io32Di); } catch { }
            try { EziMOTIONPlusELib.FAS_Close(FastechBoardId.Io32Do); } catch { }
            _diOk = _doOk = false;
            lock (_lock) _doSynced = false;
            IsConnected = false;
        }

        // ───────────────────────── vòng quét ─────────────────────────

        public void Refresh()
        {
            bool ok = ReadInputs();
            bool synced;
            lock (_lock) synced = _doSynced;
            if (!synced) SyncOutputsFromModule(false);
            ok &= Flush();

            if (ok) _failStreak = 0;
            else _failStreak++;

            IsConnected = _failStreak < FailLimit;
        }

        private bool ReadInputs()
        {
            uint input = 0, latch = 0;
            int rc;
            try { rc = EziMOTIONPlusELib.FAS_GetInput(FastechBoardId.Io32Di, ref input, ref latch); }
            catch { return false; }

            if (rc != 0) return false;

            lock (_lock)
            {
                Array.Copy(_di, _diPrev, IoMap.DiCount);
                for (int i = 0; i < IoMap.DiCount; i++)
                    _di[i] = (input & (1u << i)) != 0;
            }
            return true;
        }

        private bool Flush()
        {
            uint want;
            lock (_lock)
            {
                if (!_doSynced) return false;
                if (!_doDirty && _doWanted == _doWritten) return true;
                want = _doWanted;
            }

            uint setMask = want;
            uint clrMask = ~want;

            int rc;
            try { rc = EziMOTIONPlusELib.FAS_SetOutput(FastechBoardId.Io32Do, setMask, clrMask); }
            catch { return false; }

            if (rc != 0)
            {
                AlarmManager.Raise(AlarmCode.IoWriteFailed, "IO", Loc.F("Comm.FasReturned", "FAS_SetOutput", rc));
                return false;
            }

            AlarmManager.ClearCondition(AlarmCode.IoWriteFailed);
            lock (_lock)
            {
                _doWritten = want;
                _doDirty = false;
                for (int i = 0; i < IoMap.DoCount; i++)
                    _do[i] = (want & (1u << i)) != 0;
            }
            return true;
        }

        // ───────────────────────── đọc / ghi bit ─────────────────────────

        public bool GetDi(int bit)
        {
            lock (_lock) return bit >= 0 && bit < IoMap.DiCount && _di[bit];
        }

        public bool GetDo(int bit)
        {
            lock (_lock) return bit >= 0 && bit < IoMap.DoCount && _do[bit];
        }

        public void SetDo(int bit, bool value)
        {
            if (bit < 0 || bit >= IoMap.DoCount) return;
            lock (_lock)
            {
                uint mask = 1u << bit;
                uint next = value ? (_doWanted | mask) : (_doWanted & ~mask);
                if (next == _doWanted) return;
                _doWanted = next;
                _doDirty = true;
            }
        }

        public bool RisingEdge(int bit)
        {
            lock (_lock) return bit >= 0 && bit < IoMap.DiCount && _di[bit] && !_diPrev[bit];
        }

        public bool FallingEdge(int bit)
        {
            lock (_lock) return bit >= 0 && bit < IoMap.DiCount && !_di[bit] && _diPrev[bit];
        }

        /// <summary>Ảnh DI thô 32 bit — hiện trên trang Manual để đối chiếu với sơ đồ điện.</summary>
        public uint RawDi
        {
            get
            {
                uint v = 0;
                lock (_lock)
                    for (int i = 0; i < IoMap.DiCount; i++)
                        if (_di[i]) v |= 1u << i;
                return v;
            }
        }

        public uint RawDo { get { lock (_lock) return _doWanted; } }
    }
}
