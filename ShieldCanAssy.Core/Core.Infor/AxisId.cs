using System;

namespace ShieldCanAssy.Core.Infor
{
    /// <summary>
    /// Tên 7 trục dùng thống nhất toàn hệ thống: khoá tra cứu cấu hình, nguồn phát alarm,
    /// và khoá của MotionConnect. Dùng hằng số thay cho chuỗi rời rạc để gõ sai tên trục
    /// là lỗi biên dịch chứ không phải một alarm im lặng lúc chạy máy.
    /// </summary>
    public static class AxisId
    {
        public const string X  = "X";
        public const string Z1 = "Z1";
        public const string Z2 = "Z2";
        public const string Y1 = "Y1";
        public const string Y2 = "Y2";
        public const string A1 = "A1";
        public const string A2 = "A2";

        /// <summary>Thứ tự cố định — dùng cho vòng lặp và thứ tự hiển thị (bảng cấu hình trục
        /// trang Setting, danh sách MotionConnect.AxisList...). Tuấn chốt thứ tự này 2026-09-04.</summary>
        public static readonly string[] All = { X, Y1, Y2, Z1, Z2, A1, A2 };

        /// <summary>Hai trục Z, cần cho chốt chặn Z_SAFE (PLAN.md §5.1).</summary>
        public static readonly string[] ZAxes = { Z1, Z2 };

        /// <summary>Các trục nằm trong mặt phẳng ngang — chỉ được chạy khi cả Z1, Z2 ≤ Z_SAFE + 1 mm (Z
        /// dương là chiều đi xuống, xem MotionConnect.ZSafeOk).</summary>
        public static readonly string[] PlanarAxes = { X, Y1, Y2 };

        public static readonly string[] RotateAxes = { A1, A2 };

        public static bool IsValid(string name) => Array.IndexOf(All, name) >= 0;
    }
}
