using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using ShieldCanAssy.Core.Lang;
using ShieldCanAssy.SysConfig;
using ShieldCanAssy.Utility;
using ShieldCanAssy.Utility.Log;

namespace ShieldCanAssy.Core.Infor
{
    /// <summary>
    /// Quản lý alarm — PLAN.md §7.
    ///
    /// <para><b>Hai cờ tách biệt.</b> Một alarm active có <c>ConditionActive</c> (nguyên nhân vật
    /// lý còn hay hết) và bản thân việc nó còn nằm trong danh sách (đã chốt/latched). Bên phát
    /// alarm gọi <see cref="Raise"/> liên tục khi điều kiện còn đúng và <see cref="ClearCondition"/>
    /// khi hết. RESET chỉ gỡ được alarm đã hết nguyên nhân — đúng quy tắc "không tự động xoá"
    /// ở §7.4, và cũng là lý do bấm RESET lúc EMG còn nhấn thì không có gì xảy ra.</para>
    /// </summary>
    public static class AlarmManager
    {
        private static readonly object _lock = new object();
        private static readonly Dictionary<int, AlarmData> _active = new Dictionary<int, AlarmData>();
        private static DateTime _lastRaise = DateTime.MinValue;

        /// <summary>Cửa sổ gom cụm để xác định alarm gốc — PLAN.md §7.3.</summary>
        private static readonly TimeSpan RootCauseWindow = TimeSpan.FromMilliseconds(200);

        /// <summary>Danh sách alarm đang active, bind thẳng lên UI. Chỉ chạm từ UI thread.</summary>
        public static ObservableCollection<AlarmData> ActiveAlarms { get; } = new ObservableCollection<AlarmData>();

        /// <summary>Lịch sử phiên chạy hiện tại (đã ghi ra CSV song song).</summary>
        public static ObservableCollection<AlarmData> History { get; } = new ObservableCollection<AlarmData>();

        public static int MaxHistory { get; set; } = 200;

        /// <summary>Đổi ngôn ngữ thì alarm đã nằm trong danh sách/lịch sử cũng đổi chữ theo (2026-10-09). Đăng ký một lần,
        /// AlarmManager sống suốt đời App.</summary>
        static AlarmManager()
        {
            Loc.LanguageChanged += () => UiThread.Run(() =>
            {
                foreach (var a in ActiveAlarms.Union(History)) a.NotifyLanguageChanged();
            });
        }

        /// <summary>Bắn khi có alarm MỚI (không bắn khi alarm cũ được raise lại).</summary>
        public static event Action<AlarmData> AlarmRaised;

        public static bool HasFatal   { get { lock (_lock) return _active.Values.Any(a => a.Level == EAlarmLevel.Fatal); } }
        public static bool HasError   { get { lock (_lock) return _active.Values.Any(a => a.Level >= EAlarmLevel.Error); } }
        public static bool HasWarning { get { lock (_lock) return _active.Values.Any(a => a.Level == EAlarmLevel.Warning); } }
        public static int  ActiveCount { get { lock (_lock) return _active.Count; } }

        /// <summary>Alarm gốc của cụm đang active — cái UI cần hiện to nhất.</summary>
        public static AlarmData RootCause
        {
            get
            {
                lock (_lock)
                {
                    return _active.Values
                        .OrderByDescending(a => a.IsRootCause)
                        .ThenByDescending(a => a.Level)
                        .ThenBy(a => a.RaisedAt)
                        .FirstOrDefault();
                }
            }
        }

        /// <summary>
        /// Phát alarm. Gọi lại nhiều lần với cùng mã là an toàn — chỉ làm mới
        /// <see cref="AlarmData.ConditionActive"/>, không nhân bản dòng hay spam log.
        /// </summary>
        public static AlarmData Raise(int code, string source = null, string detail = null)
        {
            AlarmData alarm;
            bool isNew;

            lock (_lock)
            {
                if (_active.TryGetValue(code, out alarm))
                {
                    alarm.ConditionActive = true;
                    if (!string.IsNullOrEmpty(detail)) alarm.Detail = detail;
                    return alarm;
                }

                var def = AlarmCatalog.Get(code);
                var now = DateTime.Now;

                alarm = new AlarmData
                {
                    Code = code,
                    Level = def.Level,
                    Source = source ?? SourceFromCode(code),
                    MessageVi = def.Vi,
                    MessageEn = def.En,
                    Cause = def.Cause,
                    CauseEn = def.CauseEn,
                    Remedy = def.Remedy,
                    RemedyEn = def.RemedyEn,
                    Detail = detail,
                    RaisedAt = now,
                    ConditionActive = true,
                    IsRootCause = (now - _lastRaise) > RootCauseWindow
                };

                _lastRaise = now;
                _active[code] = alarm;
                isNew = true;
            }

            if (isNew)
            {
                LogWorker.Instance.RecordLog(
                    $"ALARM {code} [{alarm.Level}] {alarm.MessageVi}" +
                    (string.IsNullOrEmpty(detail) ? "" : " — " + detail) +
                    (alarm.IsRootCause ? "  (nguyên nhân gốc)" : ""),
                    alarm.Level >= EAlarmLevel.Error ? Utility.Log.LogLevel.Error : Utility.Log.LogLevel.Warning,
                    alarm.Source);

                AppendCsv(alarm);

                var a = alarm;
                UiThread.Run(() =>
                {
                    ActiveAlarms.Add(a);
                    History.Add(a);
                    while (History.Count > MaxHistory) History.RemoveAt(0);
                });

                AlarmRaised?.Invoke(alarm);
            }

            return alarm;
        }

        public static AlarmData RaiseAxis(string axisName, AxisFault fault, string detail = null)
            => Raise(AlarmCode.ForAxis(axisName, fault), axisName, detail);

        /// <summary>
        /// Phát alarm cho một SỰ KIỆN đã xảy ra xong — một lần thử Home/Move/SVO ON cụ thể đã
        /// thất bại — khác hẳn <see cref="Raise"/> dùng cho một ĐIỀU KIỆN đang tiếp diễn (cửa mở,
        /// đang chạm hard limit, mất kết nối...) mà một vòng quét riêng liên tục Raise lại lúc
        /// còn đúng và <see cref="ClearCondition"/> lúc hết.
        ///
        /// <para><b>Vì sao cần hàm riêng (bug thật, phát hiện 2026-09-09):</b> một sự kiện như
        /// "về gốc thất bại" không có gì để tiếp tục theo dõi — thời điểm Raise được gọi thì
        /// nguyên nhân đã xong rồi (lệnh đã trả về, vòng lặp đã thoát). Nếu gọi <see cref="Raise"/>
        /// trơn cho loại này thì <c>ConditionActive</c> mãi mãi là <c>true</c> vì không nơi nào
        /// gọi <c>ClearCondition</c> cho nó — <see cref="Reset"/> không bao giờ gỡ được, và ở mức
        /// Error/Fatal thì <c>HasError</c> khoá <c>SafeToMove</c> CẢ MÁY vĩnh viễn, chỉ thoát được
        /// bằng cách khởi động lại app. Đây chính xác là lỗi Tuấn gặp: Z1 chạm hard limit lúc về
        /// gốc → alarm 210x "về gốc thất bại" (HomeTimeout) mắc kẹt vĩnh viễn → SafeToMove khoá cả
        /// máy → Jog để rời khỏi hard limit cũng bị chặn theo → hard limit không bao giờ tự hết →
        /// vòng lặp bế tắc, RESET không thoát được.</para>
        ///
        /// <para>Đánh dấu <c>ConditionActive=false</c> NGAY sau khi Raise là an toàn: trạng thái an
        /// toàn THẬT (chưa về gốc, servo chưa ON, driver đang ALM...) đã có cờ riêng liên tục theo
        /// dõi (<c>OrgOk</c>, <c>ServoOn</c>, <c>axis.Alarm</c>...) giữ nguyên tác dụng chặn —
        /// alarm này chỉ để ghi log/thông báo cho operator biết ĐÃ có một lần thất bại, RESET chỉ
        /// xoá dòng thông báo đó chứ không tự ý bỏ qua điều kiện an toàn nào.</para>
        /// </summary>
        public static AlarmData RaiseEvent(int code, string source = null, string detail = null)
        {
            var alarm = Raise(code, source, detail);
            ClearCondition(code);
            return alarm;
        }

        public static AlarmData RaiseAxisEvent(string axisName, AxisFault fault, string detail = null)
            => RaiseEvent(AlarmCode.ForAxis(axisName, fault), axisName, detail);

        /// <summary>
        /// Báo nguyên nhân đã hết. Alarm vẫn nằm lại chờ RESET, ở MỌI mức — kể cả Warning (Tuấn chốt
        /// 2026-09-26: lỗi nào cũng phải bấm RESET để xác nhận trước khi thao tác tiếp). Trước đó Warning
        /// tự biến mất ngay khi hết nguyên nhân. Warning mà máy tự chạy tiếp thì dùng <see cref="Dismiss"/>.
        /// </summary>
        public static void ClearCondition(int code)
        {
            lock (_lock)
            {
                AlarmData alarm;
                if (_active.TryGetValue(code, out alarm)) alarm.ConditionActive = false;
            }
        }

        /// <summary>
        /// Gỡ hẳn alarm khỏi danh sách, không chờ RESET — cho Warning mà máy tự chạy tiếp được (hút hụt sang ô khác,
        /// soi hỏng trả về ô nguồn, bỏ ô lệch lưới, đã nạp tray mới): Tuấn chốt 2026-09-29 loại này không cần bấm RESET
        /// bằng tay. Vẫn còn trong log, alarm.csv và lịch sử. Warning làm máy dừng thì vẫn dùng ClearCondition.
        /// </summary>
        public static void Dismiss(int code)
        {
            AlarmData alarm;
            lock (_lock)
            {
                if (!_active.TryGetValue(code, out alarm)) return;
                _active.Remove(code);
                alarm.ConditionActive = false;
                alarm.ClearedAt = DateTime.Now;
                if (_active.Count == 0) _lastRaise = DateTime.MinValue;
            }
            UiThread.Run(() => ActiveAlarms.Remove(alarm));
        }

        /// <summary>
        /// Còn alarm đã hết nguyên nhân mà operator chưa bấm RESET xác nhận. McControl chặn vào chạy
        /// (Cặp A, Cặp B) khi còn alarm loại này. Alarm CÒN nguyên nhân thì RESET chưa gỡ được nên không
        /// tính ở đây — không thì máy kẹt, vd. 4090 hiệu chuẩn quá hạn chỉ hết khi chạy sau lần hiệu chuẩn lại.
        /// </summary>
        public static AlarmData PendingReset
        {
            get
            {
                lock (_lock)
                    return _active.Values.Where(a => !a.ConditionActive)
                                  .OrderByDescending(a => a.Level).ThenBy(a => a.RaisedAt).FirstOrDefault();
            }
        }

        public static void ClearConditionAxis(string axisName, AxisFault fault)
            => ClearCondition(AlarmCode.ForAxis(axisName, fault));

        public static bool IsActive(int code)
        {
            lock (_lock) return _active.ContainsKey(code);
        }

        /// <summary>
        /// Lấy alarm đang active theo mã. Đọc thẳng từ danh sách trong lõi chứ không qua
        /// <see cref="ActiveAlarms"/> — collection kia được cập nhật qua UI thread nên chưa
        /// chắc đã kịp phản ánh alarm vừa phát từ thread nền.
        /// </summary>
        public static AlarmData Get(int code)
        {
            lock (_lock)
            {
                AlarmData a;
                return _active.TryGetValue(code, out a) ? a : null;
            }
        }

        /// <summary>
        /// Operator bấm RESET. Gỡ mọi alarm đã hết nguyên nhân; trả về danh sách alarm KHÔNG gỡ
        /// được để UI nói rõ vì sao thay vì im lặng không phản ứng (§7.4).
        /// </summary>
        public static List<AlarmData> Reset()
        {
            var removed = new List<AlarmData>();
            List<AlarmData> stuck;

            lock (_lock)
            {
                foreach (var kv in _active.ToList())
                {
                    if (kv.Value.ConditionActive) continue;
                    _active.Remove(kv.Key);
                    kv.Value.ClearedAt = DateTime.Now;
                    removed.Add(kv.Value);
                }
                stuck = _active.Values.ToList();

                // Xoá sạch rồi thì sự cố kế tiếp là một cụm MỚI, phải được đánh dấu là nguyên
                // nhân gốc. Không đặt lại mốc thời gian thì alarm đầu tiên sau RESET có thể bị
                // gom nhầm vào cụm cũ và không alarm nào được coi là gốc.
                if (_active.Count == 0) _lastRaise = DateTime.MinValue;
            }

            if (removed.Count > 0)
            {
                UiThread.Run(() =>
                {
                    foreach (var a in removed) ActiveAlarms.Remove(a);
                });
                LogWorker.Instance.RecordLog(
                    $"RESET: xoá {removed.Count} alarm ({string.Join(", ", removed.Select(a => a.Code))})",
                    Utility.Log.LogLevel.Info, "Alarm");
            }

            if (stuck.Count > 0)
            {
                LogWorker.Instance.RecordLog(
                    "RESET: còn " + stuck.Count + " alarm chưa hết nguyên nhân — " +
                    string.Join("; ", stuck.Select(a => $"{a.Code} {a.MessageVi}")),
                    Utility.Log.LogLevel.Warning, "Alarm");
            }

            return stuck;
        }

        /// <summary>Xoá sạch không điều kiện. Chỉ dùng khi khởi tạo lại hệ thống hoặc trong test.</summary>
        public static void ForceClearAll()
        {
            lock (_lock)
            {
                _active.Clear();
                _lastRaise = DateTime.MinValue;
            }
            UiThread.Run(ActiveAlarms.Clear);
        }

        private static string SourceFromCode(int code)
        {
            if (code >= 1100 && code < 1200) return "Safety";
            if (code >= 2000 && code < 2700)
            {
                string axis; AxisFault f;
                return AlarmCode.TryParseAxis(code, out axis, out f) ? axis : "Motion";
            }
            if (code >= 3000 && code < 3100) return "IO";
            if (code >= 4000 && code < 4100) return "Vision";
            if (code >= 5000 && code < 5100) return "Auto";
            if (code >= 6000 && code < 6100) return "Data";
            return "System";
        }

        private static readonly ConcurrentQueue<AlarmData> _csvQueue = new ConcurrentQueue<AlarmData>();
        private static int _csvDraining;

        /// <summary>
        /// Xếp hàng ghi alarm ra CSV, ghi thật trên thread nền.
        ///
        /// <para>Ghi thẳng ở đây là sai: <see cref="Raise"/> được gọi từ vòng quét an toàn chu kỳ
        /// 20 ms, và câu lệnh chạy NGAY SAU nó thường là lệnh dừng máy
        /// (<c>SafetyMonitor.Abort</c>). Một lần đĩa khựng vài chục ms — chuyện bình thường trên
        /// Windows khi có antivirus quét hoặc ổ mạng chập chờn — sẽ dời đúng cái lệnh dừng đó.
        /// Hàng đợi giữ nguyên thứ tự dòng vì chỉ có một thread rút hàng tại một thời điểm.</para>
        /// </summary>
        private static void AppendCsv(AlarmData alarm)
        {
            _csvQueue.Enqueue(alarm);
            if (Interlocked.Exchange(ref _csvDraining, 1) == 0)
                ThreadPool.QueueUserWorkItem(_ => DrainCsv());
        }

        private static void DrainCsv()
        {
            try
            {
                AlarmData a;
                while (_csvQueue.TryDequeue(out a)) WriteCsvLine(a);
            }
            catch
            {
                // Không được để lỗi ghi CSV làm hỏng đường phát alarm — alarm vẫn phải lên UI.
            }
            finally
            {
                Interlocked.Exchange(ref _csvDraining, 0);
                // Dòng được xếp hàng đúng lúc vòng lặp vừa thoát sẽ nằm lại nếu không kiểm lại.
                if (!_csvQueue.IsEmpty && Interlocked.Exchange(ref _csvDraining, 1) == 0)
                    ThreadPool.QueueUserWorkItem(_ => DrainCsv());
            }
        }

        /// <summary>Ghi nốt phần còn trong hàng đợi. Gọi lúc thoát app.</summary>
        public static void FlushCsv()
        {
            try
            {
                AlarmData a;
                while (_csvQueue.TryDequeue(out a)) WriteCsvLine(a);
            }
            catch { }
        }

        private static void WriteCsvLine(AlarmData alarm)
        {
            string dir = Path.Combine(AppPaths.LogDir, DateTime.Now.ToString("yyyyMMdd"));
            Directory.CreateDirectory(dir);
            string file = Path.Combine(dir, "alarm.csv");

            bool isNewFile = !File.Exists(file);
            using (var sw = new StreamWriter(file, append: true, encoding: new UTF8Encoding(true)))
            {
                if (isNewFile) sw.WriteLine(AlarmData.CsvHeader);
                sw.WriteLine(alarm.ToCsvLine());
            }
        }
    }
}
