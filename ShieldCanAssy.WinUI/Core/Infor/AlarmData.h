#pragma once
#include "Infor/Enums.h"
#include "Utility/DateTimeText.h"

#include <format>
#include <optional>
#include <string>

namespace sca
{
    /// <summary>Một alarm đang active hoặc một dòng lịch sử — PLAN.md §7.3. AlarmManager trả về
    /// BẢN CHỤP (copy) của kiểu này; sửa bản chụp không ảnh hưởng alarm trong lõi.</summary>
    struct AlarmData
    {
        int Code{ 0 };
        EAlarmLevel Level{ EAlarmLevel::Info };

        /// <summary>Nguồn phát: "Z1", "Vision", "Safety", "System"...</summary>
        std::string Source;

        // Bản lưu: MessageVi/Cause/Remedy là tiếng Việt — alarm.csv luôn dùng bản này (như bản C#). Màn hình đọc
        // Message/CauseText/RemedyText/Display bên dưới (2026-10-09).
        std::string MessageVi;
        std::string MessageEn;
        std::string Cause;
        std::string CauseEn;
        std::string Remedy;
        std::string RemedyEn;

        /// <summary>Nội dung theo ngôn ngữ hiển thị. Bản C++ chỉ có tiếng Anh (PORTING.md §3) nên luôn chọn bản
        /// tiếng Anh; thiếu bản tiếng Anh thì hiện tiếng Việt — đúng Pick() của bản C# khi Loc.Language = En.</summary>
        std::string Message() const { return Pick(MessageVi, MessageEn); }
        std::string CauseText() const { return Pick(Cause, CauseEn); }
        std::string RemedyText() const { return Pick(Remedy, RemedyEn); }

        /// <summary>Dữ liệu động kèm theo: ErrorCode của driver, vị trí, score vision...</summary>
        std::string Detail;

        TimePoint RaisedAt{};
        std::optional<TimePoint> ClearedAt;

        /// <summary>Nguyên nhân vật lý còn tồn tại hay không. Do bên phát alarm cập nhật liên tục
        /// (SafetyMonitor quét mỗi 20 ms). RESET chỉ xoá được alarm có cờ này = false.</summary>
        bool ConditionActive{ true };

        /// <summary>Alarm đầu tiên của một cụm phát sinh trong vòng 200 ms. Một sự cố thật thường kéo
        /// theo chùm alarm phụ (mất điện driver X → mất kết nối → timeout → rơi sản phẩm);
        /// cờ này cho UI đưa alarm gốc lên đầu thay vì để operator đi sửa cái cuối cùng.</summary>
        bool IsRootCause{ false };

        std::string Display() const
        {
            return std::format("[{}] {}", Code, Message()) + (Detail.empty() ? "" : " — " + Detail);
        }

        std::string ToCsvLine() const;

        static constexpr const char* CsvHeader = "RaisedAt,ClearedAt,Code,Level,Source,RootCause,Message,Detail";

    private:
        static std::string Pick(const std::string& vi, const std::string& en) { return !en.empty() ? en : vi; }
    };
}
