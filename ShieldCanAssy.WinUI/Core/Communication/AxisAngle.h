#pragma once
#include <cmath>
#include <format>
#include <string>

namespace sca
{
    /// <summary>
    /// Số học góc cho trục quay tự do (A1/A2) — quấn toạ độ về <b>[0, 360)</b> và luôn đi đường ngắn. Hiển thị thì có dấu
    /// (−180, +180] — <see cref="SignedText"/>.
    /// Trục quay không phải trục số thẳng: 350° và 10° chỉ cách nhau 20°, và cùng một tư thế có vô
    /// số con số biểu diễn (10, 370, −350…). Gom về một chỗ để không nơi nào quên xử lý.
    ///
    /// <para><b>Quy ước hoà:</b> đúng nửa vòng (±180°) thì đi chiều DƯƠNG — phải chốt một chiều cố
    /// định, không thì cùng một lệnh có thể quay hai hướng khác nhau giữa hai lần chạy.</para>
    /// </summary>
    struct AxisAngle
    {
        static constexpr double FullTurn = 360.0;

        /// <summary>Quấn về [0, 360): −10 → 350, 370 → 10, 360 → 0. NaN/vô cực trả 0 thay vì lan ra
        /// ngoài: giá trị hỏng lọt tới <c>AxisScale::ToPulse</c> sẽ thành một lệnh chạy tới toạ độ vô nghĩa.</summary>
        static double Wrap(double deg)
        {
            if (std::isnan(deg) || std::isinf(deg)) return 0.0;

            double w = std::fmod(deg, FullTurn);
            if (w < 0) w += FullTurn;

            // Góc âm rất nhỏ (−0.0000001°, luôn xuất hiện vì trục dừng quanh đích) quấn ra 359.9999999
            // — cùng tư thế với 0° nhưng đọc như cách gần trọn vòng. Kéo về 0 hẳn. Ngưỡng 1e-6 độ nhỏ
            // hơn một xung (0.018°) bốn bậc độ lớn nên không gộp nhầm hai tư thế phân biệt được.
            if (FullTurn - w < 1e-6) w = 0.0;
            return w;
        }

        /// <summary>Góc ngắn nhất đi từ fromDeg tới toDeg, trong (−180, +180]. Dương = chiều tăng toạ độ.</summary>
        static double ShortestDelta(double fromDeg, double toDeg)
        {
            double d = Wrap(toDeg - fromDeg);
            return d > 180.0 ? d - FullTurn : d;
        }

        /// <summary>Khoảng cách góc, [0, 180] — dùng thay abs(a − b) khi so hai góc.</summary>
        static double Distance(double aDeg, double bDeg)
        {
            return std::fabs(ShortestDelta(aDeg, bDeg));
        }

        /// <summary>Góc về (−180, +180] — CHỈ để hiển thị (Tuấn 2026-10-30): 359 → −1, cùng tư thế. Mọi phép tính và dữ
        /// liệu lưu vẫn dùng [0, 360) (Wrap); trục tự đi đường ngắn nên gõ −1 hay 359 cũng như nhau.</summary>
        static double Signed(double deg)
        {
            double w = Wrap(deg);
            return w > 180.0 ? w - FullTurn : w;
        }

        /// <summary>Chữ hiển thị toạ độ trục A1/A2: "+1.000", "-1.000", "0.000" (không ra "-0.000" / "+0.000" khi làm tròn
        /// về 0).</summary>
        static std::string SignedText(double deg, int decimals)
        {
            if (decimals < 0) decimals = 0;
            double s = Signed(deg);
            if (std::fabs(s) < 0.5 * std::pow(10.0, -decimals)) return std::format("{:.{}f}", 0.0, decimals);
            return std::format("{:+.{}f}", s, decimals);
        }
    };
}
