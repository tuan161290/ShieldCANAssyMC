using System;
using ShieldCanAssy.Core.Lang;
using ShieldCanAssy.Utility;

namespace ShieldCanAssy.Core.Infor
{
    /// <summary>Một alarm đang active hoặc một dòng lịch sử — PLAN.md §7.3.</summary>
    public class AlarmData : Bindable
    {
        public int Code { get; set; }
        public EAlarmLevel Level { get; set; }

        /// <summary>Nguồn phát: "Z1", "Vision", "Safety", "System"...</summary>
        public string Source { get; set; }

        // Bản lưu: MessageVi/Cause/Remedy là tiếng Việt — log và alarm.csv luôn dùng bản này. Màn hình đọc
        // Message/CauseText/RemedyText/Display bên dưới, theo ngôn ngữ đang chọn (2026-10-09).
        public string MessageVi { get; set; }
        public string MessageEn { get; set; }
        public string Cause { get; set; }
        public string CauseEn { get; set; }
        public string Remedy { get; set; }
        public string RemedyEn { get; set; }

        /// <summary>Nội dung theo ngôn ngữ đang chọn; thiếu bản tiếng Anh thì hiện tiếng Việt.</summary>
        public string Message => Pick(MessageVi, MessageEn);
        public string CauseText => Pick(Cause, CauseEn);
        public string RemedyText => Pick(Remedy, RemedyEn);

        private static string Pick(string vi, string en)
            => Loc.Language == ELanguage.En && !string.IsNullOrEmpty(en) ? en : vi;

        /// <summary>Báo giao diện đọc lại các chữ theo ngôn ngữ — <see cref="AlarmManager"/> gọi khi đổi ngôn ngữ.</summary>
        public void NotifyLanguageChanged()
        {
            OnPropertyChanged(nameof(Message));
            OnPropertyChanged(nameof(CauseText));
            OnPropertyChanged(nameof(RemedyText));
            OnPropertyChanged(nameof(Display));
        }

        /// <summary>Dữ liệu động kèm theo: ErrorCode của driver, vị trí, score vision...</summary>
        public string Detail { get; set; }

        public DateTime RaisedAt { get; set; }
        public DateTime? ClearedAt { get; set; }

        /// <summary>
        /// Nguyên nhân vật lý còn tồn tại hay không. Do bên phát alarm cập nhật liên tục
        /// (SafetyMonitor quét mỗi 20 ms). RESET chỉ xoá được alarm có cờ này = false.
        /// </summary>
        private bool _conditionActive = true;
        public bool ConditionActive
        {
            get => _conditionActive;
            set => SetProperty(ref _conditionActive, value);
        }

        /// <summary>
        /// Alarm đầu tiên của một cụm phát sinh trong vòng 200 ms. Một sự cố thật thường kéo
        /// theo chùm alarm phụ (mất điện driver X → mất kết nối → timeout → rơi sản phẩm);
        /// cờ này cho UI đưa alarm gốc lên đầu thay vì để operator đi sửa cái cuối cùng.
        /// </summary>
        public bool IsRootCause { get; set; }

        public string Display => $"[{Code}] {Message}" + (string.IsNullOrEmpty(Detail) ? "" : $" — {Detail}");

        public string ToCsvLine()
        {
            return string.Join(",",
                Csv(RaisedAt.ToString("yyyy-MM-dd HH:mm:ss.fff")),
                Csv(ClearedAt?.ToString("yyyy-MM-dd HH:mm:ss.fff") ?? ""),
                Code.ToString(),
                Csv(Level.ToString()),
                Csv(Source),
                Csv(IsRootCause ? "ROOT" : ""),
                Csv(MessageVi),
                Csv(Detail));
        }

        public static string CsvHeader => "RaisedAt,ClearedAt,Code,Level,Source,RootCause,Message,Detail";

        private static string Csv(string s)
        {
            if (string.IsNullOrEmpty(s)) return "";
            if (s.IndexOfAny(new[] { ',', '"', '\n', '\r' }) < 0) return s;
            return "\"" + s.Replace("\"", "\"\"") + "\"";
        }
    }
}
