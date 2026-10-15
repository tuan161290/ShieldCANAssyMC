using System;
using System.Diagnostics;
using System.IO.Ports;
using System.Threading;
using System.Threading.Tasks;
using ServoControl.Models;
using ServoControl.Services;
using ShieldCanAssy.Core.Infor;
using ShieldCanAssy.Core.Lang;
using ShieldCanAssy.Utility;
using ShieldCanAssy.Utility.Log;

namespace ShieldCanAssy.Core.Communication
{
    /// <summary>
    /// Một trục Panasonic MINAS A6SF qua Modbus RTU, bọc <c>ServoControl.ServoService</c>.
    ///
    /// <para><b>Mỗi trục một cổng COM riêng, mỗi trục một <c>ServoService</c> riêng</b>
    /// (PLAN.md §1.1). <c>ServoService</c> vốn quản được nhiều trục trên một bus RS485, nhưng
    /// máy này tách bus nên mỗi instance chỉ khai báo đúng một trục và <c>UnitId</c> của cả 5
    /// trục đều có thể để là 1. Đổi lại: 5 luồng polling chạy song song, phải đo lại thời gian
    /// quét thực tế khi có máy.</para>
    ///
    /// <para><b>Trạng thái đọc từ luồng monitor của ServoService</b>, không hỏi Modbus trong
    /// <see cref="Refresh"/>. <c>Refresh</c> được gọi mỗi 20 ms từ vòng điều khiển — hỏi Modbus
    /// ở đó sẽ khoá vòng điều khiển theo tốc độ đường truyền nối tiếp.</para>
    ///
    /// <para><b>Chưa kiểm chứng trên thiết bị thật.</b> Viết trước để khi máy về chỉ còn cấu
    /// hình driver và xác định chiều/giới hạn, không phải vừa gõ code vừa đứng cạnh máy đang
    /// bật servo. Hai việc chắc chắn phải làm ở P1: (1) dựng bảng block trong PANATERM — số hiệu
    /// block là tham số của driver, phần mềm chỉ kích chúng; (2) cấu hình tỉ số truyền điện tử
    /// (Pr0.08/Pr0.09) sao cho ra đúng 1000 xung/mm — <see cref="AxisScale"/> đã cố định con số
    /// này ở phía phần mềm, không phải đo trên máy. Cách về gốc (DOG + xung Z hay kiểu khác) là
    /// tham số bên trong driver (Pr5.xx); <see cref="HomeAsync"/> chỉ gọi <c>StartHome</c> rồi
    /// chờ <c>ORG_CMP</c>, không cần biết driver về gốc bằng cách nào.</para>
    /// </summary>
    public class PanasonicAxis : Bindable, IAxis
    {
        private readonly AxisConfig _cfg;
        private readonly AxisScale _scale;
        private readonly ServoService _servo;
        private readonly int _unitId;

        /// <summary>
        /// Chính đối tượng cấu hình đã trao cho thư viện lúc <see cref="Connect"/> — giữ lại để
        /// đồng bộ thời gian tăng/giảm tốc trước mỗi lệnh chạy.
        ///
        /// <para>Tốc độ đi kèm từng lệnh nên sửa ở trang Setting là ăn ngay; còn accel/decel nằm
        /// trong cấu hình chỉ đọc một lần lúc kết nối, không cập nhật thì phải khởi động lại app
        /// mới có tác dụng — trái với mong đợi của người dùng khi hai ô nằm cùng một bảng.</para>
        ///
        /// <para>Đồng bộ trước mỗi lệnh KHÔNG có nghĩa là ghi xuống driver trước mỗi lệnh: thư viện
        /// chỉ ghi thanh ghi acc/dec khi con số đổi (sửa ở Setting, hoặc Jog vừa ghi đè cùng ô), vì
        /// độ dốc ramp đã cố định theo mức Auto cao — xem <see cref="MoveAbsAsync"/>.</para>
        /// </summary>
        private ServoAxisConfig _axisCfg;

        /// <summary>
        /// Khoá lệnh — CHỈ dùng <c>SemaphoreSlim</c> (chờ được bằng await), KHÔNG dùng
        /// <c>lock</c>/<c>Monitor</c> nữa. Bản cũ dùng <c>lock (_cmdLock) { task.GetAwaiter().
        /// GetResult() }</c>: nút Jog/Stop gọi thẳng từ sự kiện chuột trên UI thread
        /// (<c>ManualView.xaml.cs</c>), nên khi RS485 treo (driver báo lỗi, không phản hồi) thì
        /// UI thread bị khoá cứng theo — cả app đơ, không bấm được gì (Tuấn báo lỗi, 2026-09-08).
        /// Giữ nguyên MỘT khoá dùng chung cho MỌI lệnh ghi Modbus (Jog/Stop/Home/MoveAbs/SVO
        /// on-off/Reset alarm) — không jog được trong lúc đang Home/MoveAbs và ngược lại.
        ///
        /// <para><see cref="Disconnect"/> cũng xin khoá này trước khi đóng cổng COM — đóng cổng
        /// giữa lúc một lệnh khác đang gửi/đợi phản hồi trên CÙNG SerialPort dễ làm driver/bus
        /// RS485 kẹt tới khi rút/cắm lại nguồn, không kết nối lại được (Tuấn lo ngại lúc đóng
        /// app, 2026-09-10). <see cref="Core.Controller.McControl.Shutdown"/> đã huỷ
        /// <c>Safety.AbortToken</c> trước khi gọi tới đây nên các lệnh đang chờ vị trí (Home/
        /// MoveAbs) thoát nhanh, không giữ khoá lâu.</para>
        /// </summary>
        private readonly SemaphoreSlim _cmdLock = new SemaphoreSlim(1, 1);

        public PanasonicAxis(AxisConfig cfg)
        {
            _cfg = cfg;
            _scale = new AxisScale(cfg);
            _unitId = cfg.UnitId;
            _servo = new ServoService();
        }

        public string Name { get { return _cfg.Name; } }
        public AxisConfig Config { get { return _cfg; } }

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

        /// <summary>
        /// Cờ về gốc của PHẦN MỀM, tách khỏi <c>ORG_CMP</c> của driver.
        ///
        /// <para><c>ORG_CMP</c> chỉ biết driver đã từng về gốc kể từ lần bật nguồn của NÓ; nó
        /// không biết phần mềm vừa khởi động lại, không biết ai vừa đổi tham số scale. Tin nó là
        /// tin rằng mọi thứ ngoài driver đều không đổi (PLAN.md §4.1). Tắt/bật servo thì KHÔNG
        /// xoá cờ này (Tuấn bỏ 2026-09-26).</para>
        /// </summary>
        private bool _softHomed;

        // ───────── Mất kết nối chớp nhoáng ≠ mất kết nối thật ─────────
        //
        // Trước 14/09/2026: Refresh() mở đầu bằng "chưa kết nối thì thôi", mà chính nó lại là chỗ duy nhất
        // đặt lại cờ kết nối. Vòng quét Modbus của thư viện chỉ cần lỗi MỘT nhịp (timeout, sai CRC, nhiễu trên
        // cáp USB-RS485) là cờ hạ xuống, và từ đó App không bao giờ hỏi lại nữa — dù thư viện vẫn poll đều và
        // đã tự dựng cờ lên lại ở nhịp sau (ServoService.PollAxisState đặt IsConnected = true ở dòng cuối).
        // Trục chết tới khi mở lại app: đúng triệu chứng Tuấn báo 2026-09-14 ("Y1 hay mất kết nối").
        //
        // Nay đếm số vòng quét LỖI LIÊN TIẾP như IoConnect. Ngưỡng rộng hơn bản Fastech vì ở đây App đọc bộ
        // nhớ đệm của thư viện: một nhịp poll hỏng còn treo cờ tới lượt quét kế của chính thư viện
        // (MonitorIntervalMs đặt trong Connect, cộng thời gian 5 giao dịch Modbus của trục đó).
        // 30/09: 15 → 50. Y1 lỗi đọc quá 300 ms rồi đọc lại được ngay nhịp thử kế (log 09:51:01) — vẫn dừng máy oan.
        private const int FailLimit = 50;     // 50 × 20 ms = 1 s

        /// <summary>Đang mất kết nối thì giãn nhịp hỏi lại — xem bản Fastech.</summary>
        private const int RetryMs = 500;

        /// <summary>Đã mở được cổng chưa (Connect thành công). Khác <see cref="IsConnected"/> = "đang đọc được".</summary>
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
                var config = new ServoConfig
                {
                    ComPort = _cfg.ComPort,
                    BaudRate = _cfg.BaudRate,
                    // A6SF đấu tay 115200/Even/1 stop bit (Tuấn chốt, 2026-09-08) — ghi tường
                    // minh thay vì trông cậy mặc định của ServoConfig: StopBits mặc định của thư
                    // viện là Two (chỉnh riêng cho project AutoScrews), sai với máy này.
                    Parity = Parity.Even,
                    StopBits = StopBits.One,
                    // Nghỉ giữa hai lượt đọc trạng thái. App chỉ biết trục "đã tới" ở lượt đọc kế
                    // tiếp sau khi trục dừng, nên khoảng nghỉ này là thời gian chết cuối mỗi lệnh chạy
                    // (Tuấn yêu cầu 2026-09-25). Trước đây 50 — Task.Delay làm tròn lên bội 15.6 ms của
                    // đồng hồ Windows nên thực tế ~63 ms; 10 thành ~16 ms, đặt nhỏ hơn cũng vẫn ~16 ms.
                    // Mỗi trục một cổng COM nên đọc dày không lấn trục nào khác. Cái giá: lệnh mới dễ
                    // gặp một lượt đọc đang dở hơn và phải chờ nó xong. 2026-10-01 Tuấn nới lên 30 (~31 ms
                    // thực tế) cho đường RS485 bớt dày, đi cùng thời gian chờ phản hồi 100 ms bên dưới.
                    MonitorIntervalMs = 30,
                    // Chờ phản hồi Modbus tối đa 100 ms thay vì 1000 ms mặc định của FluentModbus (Tuấn
                    // 2026-10-01). Mất một gói phản hồi thì vòng đọc giữ khoá cổng suốt thời gian chờ: trạng
                    // thái trục đứng yên, lệnh mới xếp hàng sau — máy khựng ~1 s sau khi Z nâng xong. Phản
                    // hồi bình thường ở 115200 về trong vài ms.
                    ResponseTimeoutMs = 100,
                    // QUAN TRỌNG: ServoService.MoveAbsolute/MoveMultiAxis tự kiểm tra IsAtPosition
                    // bằng CHÍNH giá trị này TRƯỚC KHI ghi bất cứ thanh ghi nào (kể cả tốc độ) —
                    // nếu đang trong dung sai thì return ngay, im lặng, không gửi gì xuống driver.
                    // Trước đây KHÔNG gán field này, nên nó luôn ở mặc định cứng của thư viện
                    // (50 xung = 0.05mm) bất kể AxisConfig.PositionTolerancePulse đặt bao nhiêu —
                    // đây là nguyên nhân thật của việc Inching 0.02mm/0.05mm "không di chuyển
                    // được" — Tuấn phát hiện, 2026-09-09. Đồng bộ thẳng với PositionTolerancePulse
                    // để chỉ còn MỘT chỗ cấu hình dung sai, không phải nhớ sửa 2 nơi.
                    // (Ban đầu tưởng đây cũng là nguyên nhân "đổi tốc độ không có tác dụng" —
                    // KHÔNG PHẢI, cái đó là do ô tốc độ sai, xem AbsoluteVelocityIndex bên dưới.)
                    PositionTolerance = _cfg.PositionTolerancePulse,
                    // Từ 2026-10-15 ngưỡng BỎ QUA lệnh tách riêng, nhỏ hơn nhiều (5 xung = 0.005 mm): system.json đặt
                    // PositionTolerancePulse 50 thì lệnh 0.05 mm Y1 giữa hai lần hút cùng hàng không tới driver → 2302.
                    SkipMoveTolerance = LibrarySkipTolerance(_cfg)
                };
                _axisCfg = new ServoAxisConfig
                {
                    Name = _cfg.Name,
                    UnitId = _unitId,
                    HomeBlock = _cfg.HomeBlock,
                    AbsoluteBlock = _cfg.AbsoluteBlock,
                    JogPositiveBlock = _cfg.JogPositiveBlock,
                    JogNegativeBlock = _cfg.JogNegativeBlock,
                    // Ô tốc độ của block ABS — mặc định thư viện là 1 (ô của Jog), sai với bảng
                    // block máy này nên MoveAbs không bao giờ nhận tốc độ mới. Xem
                    // AxisConfig.AbsoluteVelocityIndex.
                    AbsoluteVelocityIndex = _cfg.AbsoluteVelocityIndex,
                    // Thời gian tăng/giảm tốc thật (ms) — chỉnh được ở trang Setting. Kẹp về dải
                    // thanh ghi cho phép ngay tại đây để một con số gõ nhầm (0 hoặc âm) không đi
                    // xuống driver thành lệnh tăng tốc tức thời.
                    AbsoluteAccelMs = ClampRampMs(_cfg.AccelTimeMs),
                    AbsoluteDecelMs = ClampRampMs(_cfg.DecelTimeMs),
                    AbsoluteRampRpm = _scale.ToRpm(_cfg.MoveSpeed)
                };
                config.Axes.Add(_axisCfg);

                if (!_servo.Connect(config))
                {
                    LogWorker.Instance.RecordLog(
                        Name + ": không mở được " + _cfg.ComPort, Utility.Log.LogLevel.Error, Name);
                    IsConnected = false;
                    return false;
                }

                _servo.StartMonitoring();

                // A6SF bỏ qua MỌI lệnh điều khiển gửi qua Modbus (SON, Jog, Home, MoveAbs, Alarm
                // Clear...) tới khi driver được cấp "quyền điều khiển Modbus" — ghi 0x0055 vào
                // thanh ghi 0x4300 (SX-DSV03042, xem A6SFRegisters.MODBUS_RIGHTS). Thiếu bước này
                // thì lệnh vẫn gửi đi, Modbus ACK bình thường (không lỗi nào cả) — driver chỉ ÂM
                // THẦM không làm gì, quyền điều khiển vẫn thuộc về I/O phần cứng (CN X5) hoặc bảng
                // điều khiển tại chỗ. Đúng triệu chứng Tuấn báo: "bấm SVO ON, lệnh gửi đi rồi
                // nhưng servo không lên" (2026-09-09).
                //
                // .Wait(timeout) chứ KHÔNG GetAwaiter().GetResult() trần: một lần đọc Modbus treo
                // trên phần cứng thật (SerialPort của .NET có thể treo vô thời hạn bất kể
                // ReadTimeout đã đặt, tuỳ driver/cáp USB-RS485) sẽ khoá luôn McControl.Init() —
                // app chạy nhưng không bao giờ hiện được MainWindow (Tuấn báo lỗi, 2026-09-09, sau
                // đúng một lần deploy có dòng GetResult() trần này). Không xin được quyền trong
                // 2s thì vẫn tiếp tục kết nối — SVO ON có thể chưa dùng được ngay, còn hơn app
                // không mở lên được.
                if (!_servo.AcquireModbusRights(_unitId).Wait(2000))
                {
                    LogWorker.Instance.RecordLog(
                        Name + ": xin quyền điều khiển Modbus quá 2s, driver có thể không phản hồi " +
                        "— vẫn tiếp tục kết nối, SVO ON/Jog/Home có thể chưa có tác dụng tới khi xin " +
                        "được quyền (tự thử lại ở lần Connect() sau)",
                        Utility.Log.LogLevel.Warning, Name);
                }

                _opened = true;
                _sinceRead.Restart();
                IsConnected = true;
                LogWorker.Instance.RecordLog(
                    Name + ": đã kết nối " + _cfg.ComPort + " @ " + _cfg.BaudRate + ", UnitId " + _unitId,
                    Utility.Log.LogLevel.Info, Name);
                return true;
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
            // Xin _cmdLock trước khi đụng vào cổng — xem doc-comment của field này. .Wait(timeout)
            // chứ không chờ vô hạn: phòng driver treo thật (không phản hồi) thì việc đóng app vẫn
            // phải kết thúc được, không đơ theo. gotLock=false (hết giờ mà chưa xin được) vẫn cứ
            // đóng cổng — trì hoãn thêm cũng không giải quyết được driver đã treo, thà đóng còn
            // hơn treo cả app.
            bool gotLock = false;
            try { gotLock = _cmdLock.Wait(3000); } catch { }
            try
            {
                // Trả quyền điều khiển Modbus lại cho I/O phần cứng/bảng điều khiển tại chỗ trước
                // khi đóng cổng — xem AcquireModbusRights ở Connect(). Phải làm TRƯỚC
                // _servo.Disconnect() vì cần cổng còn mở để gửi được lệnh ghi này. .Wait(timeout)
                // chứ không GetResult() trần — cùng lý do đã sửa ở Connect().
                try { _servo.ReleaseModbusRights(_unitId).Wait(2000); } catch { }
                try { _servo.StopMonitoring(); } catch { }
                try { _servo.Disconnect(); } catch { }
            }
            finally
            {
                if (gotLock) _cmdLock.Release();
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

            ServoAxisState st;
            try { st = _servo.GetAxisState(_unitId); }
            catch (Exception ex) { ReadFailed(ex.Message); return; }

            if (st == null) { ReadFailed("thư viện chưa có trạng thái của UnitId " + _unitId); return; }
            if (!st.IsConnected) { ReadFailed("vòng quét Modbus của thư viện đang lỗi"); return; }

            // Số đọc ra chỉ dùng khi vòng quét của thư viện đang lành — nhánh trên đã loại bộ nhớ đệm cũ.
            ReadOk();
            ServoOn = st.SON;
            Alarm = st.ALM;
            Busy = st.BUSY;
            FLS = st.POT;
            RLS = st.NOT;
            ErrorCode = st.ErrorCode;
            CurrentPos = _scale.ToUnit(st.ActualPosition);

            // Servo OFF không làm mất gốc (Tuấn bỏ 2026-09-26): driver vẫn đếm encoder khi tắt servo, trục có
            // tụt hay bị đẩy thì vị trí đọc về vẫn đúng. Alarm thì vẫn mất gốc — encoder error, quá tốc…
            if (st.ALM && _softHomed) _softHomed = false;

            OrgOk = _softHomed && st.ORG_CMP;
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


        // ───────────────────────── lệnh ─────────────────────────

        public async Task<bool> SetServoOnAsync(bool on)
        {
            if (!IsConnected) return false;
            try
            {
                // Qua chung _cmdLock với Jog/Stop/Home/MoveAbs — SON/SOFF cũng là một lệnh ghi
                // Modbus trên cùng cổng, trước đây bỏ sót khoá này (xem doc-comment _cmdLock).
                await _cmdLock.WaitAsync().ConfigureAwait(false);
                try
                {
                    if (on) await _servo.ServoOn(_unitId).ConfigureAwait(false);
                    else await _servo.ServoOff(_unitId).ConfigureAwait(false);
                }
                finally { _cmdLock.Release(); }

                if (!on) return true;   // không làm mất gốc — xem Refresh

                // Chờ driver báo sẵn sàng — bật servo là thao tác có thể thất bại (kẹt phanh,
                // thiếu nguồn động lực), nên phải xác nhận chứ không phát lệnh rồi đi tiếp.
                var sw = Stopwatch.StartNew();
                while (sw.ElapsedMilliseconds < 5000)
                {
                    var st = _servo.GetAxisState(_unitId);
                    if (st != null && st.SON && st.S_RDY) return true;
                    if (st != null && st.ALM) return false;
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

            try
            {
                await _cmdLock.WaitAsync(ct).ConfigureAwait(false);
                try { await _servo.StartHome(_unitId).ConfigureAwait(false); }
                finally { _cmdLock.Release(); }
            }
            catch (Exception ex)
            {
                AlarmManager.RaiseAxisEvent(Name, AxisFault.HomeTimeout, Loc.F("Axis.HomeSendFail", ex.Message));
                return false;
            }

            var sw = Stopwatch.StartNew();
            while (sw.ElapsedMilliseconds < _cfg.HomeTimeoutMs)
            {
                if (ct.IsCancellationRequested) { Stop(); return false; }

                var st = _servo.GetAxisState(_unitId);
                if (st == null) { await Task.Delay(20, ct).ConfigureAwait(false); continue; }

                if (st.ALM)
                {
                    AlarmManager.RaiseAxisEvent(Name, AxisFault.HomeTimeout,
                        Loc.F("Axis.HomeDriverAlarm", st.ErrorCode.ToString("X4")));
                    return false;
                }

                // Xác nhận bằng ORG_CMP, không phải bằng BUSY xuống 0: BUSY tắt cả khi lệnh bị
                // huỷ giữa chừng, và khi đó trục dừng ở một chỗ bất kỳ chứ không phải ở gốc.
                if (st.ORG_CMP && !st.BUSY)
                {
                    // Lấy vị trí thẳng từ trạng thái thư viện (đọc ngay sau ORG_CMP), không đợi Refresh —
                    // CurrentPos cũ là số trước khi driver đặt lại gốc (26/09: log Z1 −67.069 mm khi trục hiện 0).
                    CurrentPos = _scale.ToUnit(st.ActualPosition);

                    _softHomed = true;
                    OrgOk = true;
                    LogWorker.Instance.RecordLog(
                        string.Format("{0}: về gốc xong sau {1:0.0} s, vị trí {2:0.000}{3}",
                            Name, sw.Elapsed.TotalSeconds, CurrentPos, _cfg.Unit),
                        Utility.Log.LogLevel.Info, Name);
                    return true;
                }

                await Task.Delay(20, ct).ConfigureAwait(false);
            }

            Stop();
            AlarmManager.RaiseAxisEvent(Name, AxisFault.HomeTimeout,
                Loc.F("Axis.HomeTimeoutOrg", _cfg.HomeTimeoutMs));
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

            int target = _scale.ToPulse(pos);
            ushort rpm = _scale.ToRpm(speed);

            // Đọc lại accel/decel từ cấu hình sống ngay trước khi phát lệnh — trang Setting sửa
            // thẳng vào đối tượng AxisConfig này, nên sửa xong là lệnh kế tiếp dùng luôn giá trị
            // mới, không phải khởi động lại app.
            //
            // AccelTimeMs là thời gian ramp ở mức Auto CAO; lệnh chậm hơn dùng cùng gia tốc đó (Tuấn
            // 2026-09-25). Trước đây thư viện nhân lại độ dốc theo tốc độ TỪNG lệnh, nên Z hai chặng
            // (cao/thấp xen kẽ) phải ghi lại acc/dec ở mọi lệnh — mỗi lần ghi là thời gian chết trước
            // khi trục chạy. Độ dốc cố định thì thanh ghi chỉ đổi khi số ở Setting đổi.
            //
            // gentle: ramp theo tốc độ chính lệnh này — Y2 tới điểm bù lúc đặt, cho hàng trên tray lắp ráp khỏi xê
            // dịch (Tuấn 2026-09-30). Chạy chậm mà giữ gia tốc cũ thì cú giật lúc khởi động/dừng vẫn y nguyên.
            if (_axisCfg != null)
            {
                ushort highRpm = _scale.ToRpm(_cfg.MoveSpeed);
                _axisCfg.AbsoluteAccelMs = ClampRampMs(_cfg.AccelTimeMs);
                _axisCfg.AbsoluteDecelMs = ClampRampMs(_cfg.DecelTimeMs);
                _axisCfg.AbsoluteRampRpm = gentle ? Math.Min(rpm, highRpm) : highRpm;
            }

            var send = await SendMoveAsync(target, rpm, ct).ConfigureAwait(false);
            if (send == ESend.Failed) return false;
            return await WaitMotionAsync(pos, target, rpm, send == ESend.Sent, ct).ConfigureAwait(false);
        }

        /// <summary>Lệnh chạy: lỗi phát lệnh / thư viện không gửi (đã ở đích, driver chưa sẵn sàng) / đã xuống driver.</summary>
        private enum ESend { Failed, Skipped, Sent }

        private async Task<ESend> SendMoveAsync(int target, ushort rpm, CancellationToken ct)
        {
            try
            {
                await _cmdLock.WaitAsync(ct).ConfigureAwait(false);
                bool sent;
                try { sent = await _servo.MoveAbsolute(_unitId, target, rpm).ConfigureAwait(false); }
                finally { _cmdLock.Release(); }
                return sent ? ESend.Sent : ESend.Skipped;
            }
            catch (OperationCanceledException) { throw; }   // STOP/huỷ lúc chờ khoá — không phải lỗi phát lệnh
            catch (Exception ex)
            {
                AlarmManager.RaiseAxisEvent(Name, AxisFault.MoveTimeout, Loc.F("Axis.SendFail", ex.Message));
                return ESend.Failed;
            }
        }

        /// <summary>
        /// Dung sai mà thư viện dùng để BỎ QUA lệnh chạy (<c>ServoConfig.SkipMoveTolerance</c>) — tách khỏi dung sai "đã
        /// tới" (Tuấn 2026-10-15: "nhận lệnh chính xác 0.01"). Kẹp không quá <see cref="AxisConfig.PositionTolerancePulse"/>:
        /// thư viện bỏ một lệnh mà App vẫn thấy chưa tới thì App gửi lại vô ích rồi báo 2302 — đúng lỗi Y1 hôm 15/10.
        /// </summary>
        public static int LibrarySkipTolerance(AxisConfig cfg)
        {
            return Math.Max(0, Math.Min(cfg.SkipMoveTolerancePulse, cfg.PositionTolerancePulse));
        }

        /// <summary>Số lượt đọc trạng thái thành công của driver tới lúc này — xem <c>ServoAxisState.PollCount</c>.</summary>
        private int PollNow()
        {
            var st = _servo.GetAxisState(_unitId);
            return st != null ? st.PollCount : 0;
        }

        /// <summary>Gửi lại tối đa từng này lần khi driver không nhận lệnh chạy; quá thì báo lỗi ngay, không đợi hết 30 s.</summary>
        public const int MaxMoveResends = 3;

        public enum EMoveWait { Done, Wait, Resend, Lost }

        /// <summary>
        /// Một lượt xét lệnh chạy tuyệt đối đang chờ (Tuấn 2026-10-01, theo cách AutoScrews làm). Tách thành hàm thuần cho
        /// self-test khoá được luật mà không cần driver thật.
        /// <list type="bullet">
        /// <item><b>Xong</b> = vào dung sai và driver hết BUSY — KHÔNG chờ INP. Trước đây chờ cả INP: trục đã tới nơi mà
        /// driver còn báo INP muộn, chặng nâng Z cùng quãng lúc 0.3 s lúc 0.6 s. Rung dư sau khi dừng không ảnh hưởng ảnh
        /// chụp: trước khi kích Flow có camera đã có <c>McFunction.WaitSettleAsync</c>.</item>
        /// <item><b>Gửi lại</b> khi hai lượt đọc trọn vẹn SAU lúc gửi vẫn thấy driver không BUSY, trục chưa tới và CHƯA HỀ
        /// nhúc nhích khỏi chỗ đứng lúc gửi: driver không nhận lệnh (mất khung Modbus — ServoService nuốt lỗi ghi; 30/09 09:42
        /// Z1 đứng yên rồi quá 30 s). Phải đủ hai lượt đọc mới sau lúc gửi: số đọc trước đó là của trước lệnh, gửi lại lúc
        /// trục đang chạy là chồng lệnh. Phải chưa nhúc nhích: BUSY tắt khi driver phát xong lệnh, trục chạy nhanh có thể còn
        /// đang về đích ngoài dung sai — đó là đang tới, không phải mất lệnh.</item>
        /// <item><b>Lệnh đã xuống driver</b> (<paramref name="sent"/>) thì chưa đủ hai lượt đọc sau lúc gửi chưa được coi là
        /// xong, kể cả khi đang trong dung sai (Tuấn 2026-10-15): thư viện nay gửi cả lệnh 0.01 mm, nằm gọn trong dung sai
        /// "đã tới" 0.05 mm — coi là xong ngay thì App đi tiếp (ghi toạ độ cho vision…) trong lúc trục còn đang nhích. Thư
        /// viện KHÔNG gửi (đã ở đích) thì xong ngay, không tốn thêm lượt đọc nào.</item>
        /// </list>
        /// </summary>
        public static EMoveWait JudgeMove(bool there, bool busy, int pollsSinceSend, bool movedSinceSend, int resends, bool sent)
        {
            if (there && !busy && (!sent || pollsSinceSend >= 2)) return EMoveWait.Done;
            if (busy || pollsSinceSend < 2 || movedSinceSend) return EMoveWait.Wait;
            return resends < MaxMoveResends ? EMoveWait.Resend : EMoveWait.Lost;
        }

        /// <summary>
        /// "Đã tới" — so bằng XUNG (Tuấn 2026-10-15). Trước đây so bằng mm số thực: lệch đúng 50 xung thì thư viện cho là đã
        /// tới, không gửi gì xuống driver, còn App tính 46.520 − 46.470 = 0.0500000004 &gt; 0.05 là chưa tới → gửi lại 3 lần,
        /// lần nào thư viện cũng bỏ qua → 2302. Gặp ở Y1 khi hai tay hút cùng hàng mà lưới hai tay lệch nhau ~0.05 mm. Nay
        /// thư viện chỉ bỏ qua trong <see cref="LibrarySkipTolerance"/> (≤ dung sai này) nên thư viện bỏ ⇒ App thấy đã tới.
        /// </summary>
        public static bool WithinPulses(int actualPulse, int targetPulse, int tolPulse)
        {
            return Math.Abs((long)actualPulse - targetPulse) <= Math.Max(1, tolPulse);
        }

        /// <summary>
        /// Di chuyển TƯƠNG ĐỐI (Inching). Driver Panasonic hiện chỉ có 4 block cấu hình sẵn qua
        /// PANATERM — Home/Abs/Jog+/Jog− (xem <see cref="AxisConfig.HomeBlock"/>/<see cref="AxisConfig.AbsoluteBlock"/>/
        /// Jog*Block) — CHƯA có block lệnh tương đối (khác Fastech, có sẵn FAS_MoveSingleAxisIncPos).
        /// Bù bằng phần mềm: cộng dồn vào vị trí hiện tại rồi gọi lại đúng Move Abs — nếu sau này
        /// lập thêm một block Inc riêng trong PANATERM thì đổi thẳng ở đây, chỗ gọi (McFunction/
        /// AxisRowVm) không phải đổi gì.
        /// </summary>
        public Task<bool> MoveIncAsync(double delta, double speed, CancellationToken ct)
            => MoveAbsAsync(CurrentPos + delta, speed, ct);

        /// <summary>
        /// Đưa thời gian tăng/giảm tốc gõ ở trang Setting về dải dùng được.
        ///
        /// <para>Số ≤ 0 KHÔNG hạ về 1ms mà quay lại 250ms mặc định: 0 là con số gõ nhầm hoặc ô
        /// còn trống, mà hiểu nó theo nghĩa đen thì thành lệnh tăng tốc gần như tức thời — hỏng
        /// máy hoặc bung alarm quá dòng. Rơi về giá trị máy vẫn chạy bấy lâu nay là hướng sai an
        /// toàn hơn hẳn.</para>
        /// </summary>
        private static ushort ClampRampMs(int ms)
        {
            if (ms <= 0) return 250;
            return ms > 10000 ? (ushort)10000 : (ushort)ms;
        }

        /// <summary>Chờ lệnh chạy tới nơi — luật ở <see cref="JudgeMove"/>.</summary>
        private async Task<bool> WaitMotionAsync(double target, int targetPulse, ushort rpm, bool sent, CancellationToken ct)
        {
            var sw = Stopwatch.StartNew();
            int tol = _cfg.PositionTolerancePulse;
            int sentPoll = PollNow();
            var st0 = _servo.GetAxisState(_unitId);
            int sentFrom = st0 != null ? st0.ActualPosition : _scale.ToPulse(CurrentPos);   // chỗ đứng lúc gửi
            int resends = 0;

            while (sw.ElapsedMilliseconds < _cfg.MoveTimeoutMs)
            {
                if (ct.IsCancellationRequested) { Stop(); return false; }

                var st = _servo.GetAxisState(_unitId);
                if (st == null) { await Task.Delay(10, ct).ConfigureAwait(false); continue; }

                // Đọc số lượt đọc TRƯỚC BUSY/vị trí: đủ hai lượt mới thì BUSY/vị trí đọc sau đó chắc chắn là của sau lệnh.
                int polls = unchecked(st.PollCount - sentPoll);
                bool busy = st.BUSY;
                double now = _scale.ToUnit(st.ActualPosition);

                if (st.ALM)
                {
                    AlarmManager.RaiseAxis(Name, AxisFault.DriverAlarm, "0x" + st.ErrorCode.ToString("X4"));
                    return false;
                }
                if (st.POT || st.NOT)
                {
                    AlarmManager.RaiseAxis(Name, AxisFault.HardLimit, st.POT ? "POT" : "NOT");
                    return false;
                }

                bool there = WithinPulses(st.ActualPosition, targetPulse, tol);
                bool moved = !WithinPulses(st.ActualPosition, sentFrom, tol);
                var verdict = JudgeMove(there, busy, polls, moved, resends, sent);

                if (verdict == EMoveWait.Resend)
                {
                    resends++;
                    LogWorker.Instance.RecordLog(string.Format(
                        "{0}: driver chưa nhận lệnh chạy tới {1:0.000}{2} (không BUSY, đang ở {3:0.000}{2}) — gửi lại lần {4}",
                        Name, target, _cfg.Unit, now, resends), Utility.Log.LogLevel.Warning, Name);
                    var send = await SendMoveAsync(targetPulse, rpm, ct).ConfigureAwait(false);
                    if (send == ESend.Failed) return false;
                    sent = send == ESend.Sent;
                    sentPoll = PollNow();
                    sentFrom = st.ActualPosition;
                    continue;
                }
                if (verdict == EMoveWait.Lost)
                {
                    AlarmManager.RaiseAxisEvent(Name, AxisFault.MoveTimeout, Loc.F(
                        "Axis.MoveLost",
                        resends, target, _cfg.Unit, now));
                    return false;
                }

                if (verdict == EMoveWait.Done)
                {
                    // Ghi luôn con số VỪA ĐỌC vào CurrentPos (Tuấn phát hiện 2026-09-19).
                    //
                    // CurrentPos bình thường do vòng quét Refresh() nuôi, mà vòng quét Modbus đi
                    // lần lượt 7 trục nên có thể chậm hơn lệnh chạy này hàng chục ms. Ai đọc
                    // encoder NGAY sau khi MoveAbsAsync trả về — VmCycle.AxisGlobals ghi biến cho
                    // VisionMaster, Routine_Calib đọc vị trí lúc hút — sẽ lấy phải số của trước
                    // khi chạy, trong khi camera đã chụp ở vị trí mới. Sai lệch đó KHÔNG bị phép
                    // đối chiếu echo bắt được, vì App gửi số cũ thì Flow cũng trả lại đúng số cũ.
                    //
                    // Con số này là số tươi nhất có thể có: chính nó vừa được dùng để kết luận
                    // "đã tới nơi" ngay ở dòng trên.
                    CurrentPos = now;
                    return true;
                }

                await Task.Delay(10, ct).ConfigureAwait(false);
            }

            Stop();
            AlarmManager.RaiseAxisEvent(Name, AxisFault.MoveTimeout,
                Loc.F("Axis.MoveTimeout",
                    _cfg.MoveTimeoutMs, target, _cfg.Unit, CurrentPos));
            return false;
        }

        /// <summary>
        /// Jog/Stop là <c>void</c> (interface <c>IAxis</c>) và bị gọi THẲNG từ sự kiện chuột trên
        /// UI thread (<c>ManualView.xaml.cs</c> Jog_Down/Up) — không được chờ (await) hay chặn
        /// (GetResult) ở đây, phải bắn nền bằng <c>Task.Run</c> rồi quay về ngay. Vẫn qua chung
        /// <see cref="_cmdLock"/> với Home/MoveAbs để giữ đúng thứ tự lệnh trên bus RS485.
        /// </summary>
        public void Jog(int dir, double speed)
        {
            if (!IsConnected) return;
            ushort rpm = _scale.ToRpm(speed);
            Task.Run(async () =>
            {
                try
                {
                    await _cmdLock.WaitAsync().ConfigureAwait(false);
                    try
                    {
                        if (dir > 0) await _servo.JogPositive(_unitId, rpm).ConfigureAwait(false);
                        else if (dir < 0) await _servo.JogNegative(_unitId, rpm).ConfigureAwait(false);
                        else await _servo.DecelerateStop(_unitId).ConfigureAwait(false);
                    }
                    finally { _cmdLock.Release(); }
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
            Task.Run(async () =>
            {
                try
                {
                    await _cmdLock.WaitAsync().ConfigureAwait(false);
                    try { await _servo.DecelerateStop(_unitId).ConfigureAwait(false); }
                    finally { _cmdLock.Release(); }
                }
                catch { }
            });
        }

        public async Task<bool> ResetAlarmAsync()
        {
            if (!IsConnected) return false;
            try
            {
                // Qua chung _cmdLock — cùng lý do SetServoOnAsync (xem doc-comment _cmdLock).
                await _cmdLock.WaitAsync().ConfigureAwait(false);
                try { await _servo.AlarmClear(_unitId).ConfigureAwait(false); }
                finally { _cmdLock.Release(); }

                // Xoá alarm của driver thường kèm mất vị trí — bắt về gốc lại (PLAN.md §4.1).
                _softHomed = false;
                OrgOk = false;

                await Task.Delay(200).ConfigureAwait(false);
                var st = _servo.GetAxisState(_unitId);
                return st != null && !st.ALM;
            }
            catch (Exception ex)
            {
                LogWorker.Instance.RecordLog(Name + ": ResetAlarm lỗi — " + ex.Message,
                    Utility.Log.LogLevel.Error, Name);
                return false;
            }
        }
    }
}
