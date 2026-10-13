using System;

namespace ShieldCanAssy.Utility.Log
{
    public class LogData
    {
        public DateTime UpdateTime { get; set; }
        public LogLevel Level { get; set; }

        /// <summary>Nguồn phát: "X", "Z1", "Auto", "Safety", "Vision", "System"...</summary>
        public string Source { get; set; }

        public string Message { get; set; }

        /// <summary>Một dòng trong file log — TSV, thời gian đứng trước để sort/grep được.</summary>
        public string ToLine()
        {
            return string.Concat(
                UpdateTime.ToString("yyyy-MM-dd HH:mm:ss.fff"), "\t",
                Level.ToString().ToUpperInvariant(), "\t",
                Clean(Source), "\t",
                Clean(Message));
        }

        public static bool TryParseLine(string line, out LogData data)
        {
            data = null;
            if (string.IsNullOrWhiteSpace(line)) return false;

            var parts = line.Split('\t');
            if (parts.Length < 4) return false;
            if (!DateTime.TryParse(parts[0], out var t)) return false;

            LogLevel level;
            if (!Enum.TryParse(parts[1], true, out level)) level = LogLevel.Info;

            data = new LogData { UpdateTime = t, Level = level, Source = parts[2], Message = parts[3] };
            return true;
        }

        /// <summary>Tab và xuống dòng trong nội dung sẽ phá cấu trúc TSV — thay bằng khoảng trắng.</summary>
        private static string Clean(string s)
        {
            if (string.IsNullOrEmpty(s)) return string.Empty;
            return s.Replace('\t', ' ').Replace('\r', ' ').Replace('\n', ' ');
        }
    }
}
