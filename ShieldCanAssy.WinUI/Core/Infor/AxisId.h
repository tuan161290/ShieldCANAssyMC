#pragma once
#include <array>
#include <string_view>

namespace sca
{
    /// <summary>
    /// Tên 7 trục dùng thống nhất toàn hệ thống: khoá tra cứu cấu hình, nguồn phát alarm,
    /// và khoá của MotionConnect. Dùng hằng số thay cho chuỗi rời rạc để gõ sai tên trục
    /// là lỗi biên dịch chứ không phải một alarm im lặng lúc chạy máy.
    /// </summary>
    struct AxisId
    {
        static constexpr const char* X  = "X";
        static constexpr const char* Z1 = "Z1";
        static constexpr const char* Z2 = "Z2";
        static constexpr const char* Y1 = "Y1";
        static constexpr const char* Y2 = "Y2";
        static constexpr const char* A1 = "A1";
        static constexpr const char* A2 = "A2";

        /// <summary>Thứ tự cố định — dùng cho vòng lặp và thứ tự hiển thị (bảng cấu hình trục
        /// trang Setting, danh sách MotionConnect.AxisList...). Tuấn chốt thứ tự này 2026-09-04.</summary>
        static constexpr std::array<const char*, 7> All = { "X", "Y1", "Y2", "Z1", "Z2", "A1", "A2" };

        /// <summary>Hai trục Z, cần cho chốt chặn Z_SAFE (PLAN.md §5.1).</summary>
        static constexpr std::array<const char*, 2> ZAxes = { "Z1", "Z2" };

        /// <summary>Các trục nằm trong mặt phẳng ngang — chỉ được chạy khi cả Z1, Z2 ≤ Z_SAFE + 1 mm (Z
        /// dương là chiều đi xuống, xem MotionConnect.ZSafeOk).</summary>
        static constexpr std::array<const char*, 3> PlanarAxes = { "X", "Y1", "Y2" };

        static constexpr std::array<const char*, 2> RotateAxes = { "A1", "A2" };

        /// <summary>Chỉ số 0..6 theo thứ tự <see cref="All"/>; -1 nếu không phải tên trục hợp lệ
        /// (Array.IndexOf(All, name) của bản C#).</summary>
        static int IndexOf(std::string_view name)
        {
            for (size_t i = 0; i < All.size(); ++i)
                if (name == All[i]) return (int)i;
            return -1;
        }

        static bool IsValid(std::string_view name) { return IndexOf(name) >= 0; }
    };
}
