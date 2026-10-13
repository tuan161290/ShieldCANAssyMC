using System;
using System.Collections.Concurrent;
using System.Collections.ObjectModel;
using System.IO;
using System.Text;
using System.Threading;

namespace ShieldCanAssy.Utility.Log
{
    /// <summary>
    /// Ghi log ra file theo ngày, không chặn thread gọi.
    ///
    /// <para><b>Ghi nối, không ghi đè.</b> Bản LogWorker của project HousingCorrosion serialize
    /// lại TOÀN BỘ danh sách log thành JSON và ghi đè cả file sau MỖI dòng log — chi phí O(n²)
    /// theo số dòng trong ngày. Máy này chạy liên tục và ghi log dày (mỗi ô tray vài dòng, cộng
    /// log an toàn/alarm), nên ở đây dùng ghi nối từng dòng TSV: chi phí O(1) mỗi dòng và không
    /// bao giờ mất log cũ nếu app chết giữa chừng.</para>
    ///
    /// <para>Một thread nền duy nhất rút hàng đợi và ghi file, nên thứ tự dòng trong file luôn
    /// đúng thứ tự thời gian dù nhiều thread cùng gọi <see cref="RecordLog"/>.</para>
    /// </summary>
    public sealed class LogWorker
    {
        private static readonly Lazy<LogWorker> _instance = new Lazy<LogWorker>(() => new LogWorker());
        public static LogWorker Instance => _instance.Value;

        private readonly ConcurrentQueue<LogData> _queue = new ConcurrentQueue<LogData>();
        private readonly AutoResetEvent _signal = new AutoResetEvent(false);
        private readonly object _fileLock = new object();

        private Thread _writer;
        private volatile bool _running;
        private string _rootPath;
        private StreamWriter _stream;
        private DateTime _streamDate = DateTime.MinValue;

        /// <summary>Danh sách hiển thị trên trang Log. Chỉ được chạm từ UI thread.</summary>
        public ObservableCollection<LogData> LogList { get; } = new ObservableCollection<LogData>();

        /// <summary>Số dòng giữ trong bộ nhớ cho UI. File trên đĩa giữ đủ, không bị cắt.</summary>
        public int MaxInMemoryLines { get; set; } = 500;

        public LogLevel MinLevel { get; set; } = LogLevel.Info;

        private LogWorker() { }

        public void Init(string rootPath)
        {
            _rootPath = rootPath;
            if (_running) return;

            _running = true;
            _writer = new Thread(WriteLoop)
            {
                IsBackground = true,
                Name = "LogWriter",
                Priority = ThreadPriority.BelowNormal
            };
            _writer.Start();
        }

        public void RecordLog(string message, LogLevel level = LogLevel.Info, string source = null)
        {
            if (level < MinLevel) return;

            var entry = new LogData
            {
                UpdateTime = DateTime.Now,
                Level = level,
                Source = source ?? "System",
                Message = message
            };

            _queue.Enqueue(entry);
            _signal.Set();

            UiThread.Run(() =>
            {
                // Chèn ở đầu — mới nhất lên trên, khỏi phải cuộn xuống mới thấy dòng vừa ghi.
                // Dòng cũ nhất giờ nằm ở CUỐI danh sách nên cắt bớt cũng phải đổi chiều theo.
                LogList.Insert(0, entry);
                while (LogList.Count > MaxInMemoryLines)
                    LogList.RemoveAt(LogList.Count - 1);
            });
        }

        /// <summary>Ép ghi hết hàng đợi xuống đĩa. Gọi trước khi thoát app hoặc khi vào EMG/ALARM.</summary>
        public void Flush()
        {
            _signal.Set();
            // Thread nền ghi theo lô; chờ ngắn là đủ cho hàng đợi cỡ vài trăm dòng.
            for (int i = 0; i < 50 && !_queue.IsEmpty; i++)
                Thread.Sleep(10);

            lock (_fileLock)
            {
                try { _stream?.Flush(); } catch { }
            }
        }

        public void Shutdown()
        {
            _running = false;
            _signal.Set();
            try { _writer?.Join(2000); } catch { }

            lock (_fileLock)
            {
                try { _stream?.Flush(); _stream?.Dispose(); } catch { }
                _stream = null;
            }
        }

        private void WriteLoop()
        {
            while (_running)
            {
                _signal.WaitOne(500);
                DrainQueue();
            }
            DrainQueue();
        }

        private void DrainQueue()
        {
            LogData entry;
            bool wrote = false;

            while (_queue.TryDequeue(out entry))
            {
                try
                {
                    lock (_fileLock)
                    {
                        EnsureStream(entry.UpdateTime);
                        _stream?.WriteLine(entry.ToLine());
                        wrote = true;
                    }
                }
                catch
                {
                    // Ghi log mà ném exception thì không có chỗ nào để báo — nuốt và bỏ dòng đó.
                    // Đĩa đầy / mất quyền ghi được phát hiện riêng bằng alarm 1003 (SafetyMonitor).
                }
            }

            if (wrote)
            {
                lock (_fileLock)
                {
                    try { _stream?.Flush(); } catch { }
                }
            }
        }

        /// <summary>Mở file của ngày tương ứng, tự sang file mới khi qua nửa đêm.</summary>
        private void EnsureStream(DateTime when)
        {
            if (_rootPath == null) return;
            if (_stream != null && _streamDate == when.Date) return;

            try { _stream?.Flush(); _stream?.Dispose(); } catch { }
            _stream = null;

            string dir = Path.Combine(_rootPath, "Log", when.ToString("yyyyMMdd"));
            Directory.CreateDirectory(dir);
            string file = Path.Combine(dir, "app.log");

            // BOM chỉ ghi khi tạo file mới. Không có BOM thì Notepad và các công cụ Windows đọc
            // theo bảng mã ANSI và tiếng Việt thành ký tự rác; ghi BOM mỗi lần mở để ghi nối lại
            // chèn rác vào giữa file.
            bool isNew = !File.Exists(file) || new FileInfo(file).Length == 0;

            _stream = new StreamWriter(
                new FileStream(file, FileMode.Append, FileAccess.Write, FileShare.ReadWrite),
                new UTF8Encoding(encoderShouldEmitUTF8Identifier: isNew));
            _streamDate = when.Date;
        }

        /// <summary>Đọc lại file log của một ngày — dùng cho trang Log xem lịch sử.</summary>
        public static System.Collections.Generic.List<LogData> ReadDay(string rootPath, DateTime day)
        {
            var result = new System.Collections.Generic.List<LogData>();
            string file = Path.Combine(rootPath, "Log", day.ToString("yyyyMMdd"), "app.log");
            if (!File.Exists(file)) return result;

            try
            {
                using (var fs = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
                using (var sr = new StreamReader(fs))
                {
                    string line;
                    while ((line = sr.ReadLine()) != null)
                    {
                        if (LogData.TryParseLine(line, out var d))
                            result.Add(d);
                    }
                }
            }
            catch { }

            return result;
        }
    }
}
