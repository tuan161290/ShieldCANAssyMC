using System;

namespace ShieldCanAssy.Core.Communication
{
    /// <summary>
    /// Số học góc cho trục quay tự do (A1/A2) — quấn toạ độ về <b>[0, 360)</b> và luôn đi
    /// đường ngắn.
    ///
    /// <para><b>Vì sao cần tới cả một lớp riêng.</b> Trục tịnh tiến có hai đầu hành trình nên
    /// toạ độ là một trục số thẳng: 350 luôn xa 10 đúng 340. Trục quay thì không — 350° và 10°
    /// chỉ cách nhau 20°, và cùng một tư thế vật lý có vô số con số biểu diễn (10, 370, −350…).
    /// Nếu để mỗi nơi tự xử lý chuyện đó thì sẽ có chỗ quên, và triệu chứng là đầu hút quay
    /// gần trọn vòng để tới một góc cách nó 20°, hoặc lệnh bị soft limit chặn oan khi phép bù
    /// của vision đẩy góc vượt 360.</para>
    ///
    /// <para><b>Quy ước hoà:</b> đúng nửa vòng (±180°) thì đi chiều DƯƠNG. Phải chốt một chiều
    /// cố định, không thì cùng một lệnh có thể quay hai hướng khác nhau giữa hai lần chạy.</para>
    /// </summary>
    public static class AxisAngle
    {
        public const double FullTurn = 360.0;

        /// <summary>
        /// Quấn về [0, 360): −10 → 350, 370 → 10, 360 → 0.
        ///
        /// <para>NaN/vô cực trả 0 thay vì lan ra ngoài: giá trị hỏng lọt tới
        /// <c>AxisScale.ToPulse</c> sẽ thành một lệnh chạy tới toạ độ vô nghĩa.</para>
        /// </summary>
        public static double Wrap(double deg)
        {
            if (double.IsNaN(deg) || double.IsInfinity(deg)) return 0.0;

            double w = deg % FullTurn;
            if (w < 0) w += FullTurn;

            // Một góc âm rất nhỏ (−0.0000001°, thứ luôn xuất hiện vì trục dừng quanh đích chứ
            // không đúng đích) quấn ra 359.9999999 — cùng một tư thế với 0° nhưng đọc lên thì
            // như cách nhau gần trọn vòng. Kéo về 0 hẳn.
            //
            // Ngưỡng 1e-6 độ là chọn có cơ sở: nhỏ hơn một xung (0.018°) tới bốn bậc độ lớn, nên
            // không thể gộp nhầm hai tư thế mà máy phân biệt được; đồng thời đủ lớn để nuốt trọn
            // sai số dấu phẩy động của phép chia dư ở gần 360.
            if (FullTurn - w < 1e-6) w = 0.0;
            return w;
        }

        /// <summary>
        /// Góc ngắn nhất đi từ <paramref name="fromDeg"/> tới <paramref name="toDeg"/>,
        /// nằm trong (−180, +180]. Dương = cùng chiều tăng toạ độ của trục.
        /// </summary>
        public static double ShortestDelta(double fromDeg, double toDeg)
        {
            double d = Wrap(toDeg - fromDeg);
            return d > 180.0 ? d - FullTurn : d;
        }

        /// <summary>Khoảng cách góc, [0, 180] — dùng thay <c>Math.Abs(a − b)</c> khi so hai góc.</summary>
        public static double Distance(double aDeg, double bDeg)
        {
            return Math.Abs(ShortestDelta(aDeg, bDeg));
        }
    }
}
