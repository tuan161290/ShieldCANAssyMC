using System;

namespace ShieldCanAssy.Core.Communication
{
    /// <summary>
    /// Quy đổi giữa đơn vị kỹ thuật (mm hoặc độ) và đơn vị của driver (xung, rpm).
    ///
    /// <para><c>PulsePerUnit</c> là <b>hằng số cố định theo thiết kế</b>, không phải số đo được
    /// trên máy: 1000 xung/mm cho trục tịnh tiến Panasonic (mm 3 chữ số thập phân, 12.345 mm ↔
    /// 12345 xung), 36000 xung/360° đầu hút cho trục xoay Fastech. Driver được cấu hình tỉ số truyền
    /// điện tử (electronic gear) để cho ra đúng con số này — việc của P1 là cấu hình driver
    /// khớp hằng số đã chốt, không phải đo rồi suy ra nó.</para>
    ///
    /// <para><c>UnitPerRev</c> thì khác — đây LÀ một đại lượng cơ khí thật (bước vít me, tỉ số
    /// hộp giảm tốc), chỉ dùng để đổi tốc độ mm/s sang rpm. Sai nó không tạo sai số vị trí, chỉ
    /// làm tốc độ hiển thị lệch tốc độ thật.</para>
    /// </summary>
    public class AxisScale
    {
        private readonly AxisConfig _cfg;

        public AxisScale(AxisConfig cfg)
        {
            if (cfg == null) throw new ArgumentNullException("cfg");
            _cfg = cfg;
        }

        /// <summary>Số xung trên một đơn vị, đã chặn giá trị vô nghĩa.</summary>
        public double PulsePerUnit
        {
            get { return _cfg.PulsePerUnit > 1e-9 ? _cfg.PulsePerUnit : 1.0; }
        }

        public double UnitPerRev
        {
            get { return _cfg.UnitPerRev > 1e-9 ? _cfg.UnitPerRev : 1.0; }
        }

        /// <summary>
        /// Số xung của một vòng 360° — chỉ có nghĩa với trục quay
        /// (<see cref="AxisConfig.WrapDegrees"/>).
        ///
        /// <para>Phải là SỐ NGUYÊN và phải tròn: mọi phép "đưa góc về vòng gần nhất" đều cộng
        /// trừ bội số của nó, nên một sai số nhỏ ở đây sẽ tích luỹ thành lệch góc thật sau
        /// nhiều vòng. A1/A2: 18000 xung/vòng động cơ × 2 vòng động cơ cho 1 vòng đầu hút
        /// (hộp số 1/2) = <b>36000 xung/360°</b>, tương ứng 100 xung/độ chẵn.</para>
        /// </summary>
        public int PulsePerRev
        {
            get
            {
                double p = PulsePerUnit * 360.0;
                if (p < 1.0) return 1;
                if (p > int.MaxValue) return int.MaxValue;
                return (int)Math.Round(p, MidpointRounding.AwayFromZero);
            }
        }

        /// <summary>mm (hoặc độ) → xung. Làm tròn về số nguyên gần nhất, không cắt cụt.</summary>
        public int ToPulse(double unit)
        {
            double p = unit * PulsePerUnit;

            // Cắt ở biên int thay vì để tràn âm: tràn biến một lệnh đi xa thành một lệnh đi
            // ngược hết cỡ, đúng kiểu lỗi không ai ngờ tới khi nhìn code.
            if (p > int.MaxValue) return int.MaxValue;
            if (p < int.MinValue) return int.MinValue;
            return (int)Math.Round(p, MidpointRounding.AwayFromZero);
        }

        /// <summary>Xung → mm (hoặc độ).</summary>
        public double ToUnit(int pulse)
        {
            return pulse / PulsePerUnit;
        }

        /// <summary>
        /// Tốc độ đơn vị/giây → rpm, kẹp trong [1, MaxRpm].
        ///
        /// <para>Kẹp sàn ở 1 rpm là chủ ý: làm tròn xuống 0 sẽ thành lệnh "chạy với tốc độ 0",
        /// mà driver hiểu là đứng yên chứ không phải chạy chậm — trục không nhúc nhích và lệnh
        /// treo cho tới khi timeout, rất khó đoán ra nguyên nhân.</para>
        /// </summary>
        public ushort ToRpm(double unitPerSec)
        {
            double rpm = Math.Abs(unitPerSec) / UnitPerRev * 60.0;
            int r = (int)Math.Round(rpm, MidpointRounding.AwayFromZero);

            int max = _cfg.MaxRpm > 0 ? _cfg.MaxRpm : 3000;
            if (r > max) r = max;
            if (r < 1) r = 1;
            return (ushort)r;
        }

        /// <summary>
        /// Tốc độ đơn vị/giây → xung/giây, dạng tốc độ mà Fastech nhận (<c>lVelocity</c>).
        /// Kẹp sàn ở 1 xung/s với cùng lý do như <see cref="ToRpm"/>.
        /// </summary>
        public uint ToPulsePerSec(double unitPerSec)
        {
            double pps = Math.Abs(unitPerSec) * PulsePerUnit;
            if (pps > uint.MaxValue) return uint.MaxValue;
            uint v = (uint)Math.Round(pps, MidpointRounding.AwayFromZero);
            return v < 1 ? 1u : v;
        }

        /// <summary>rpm → đơn vị/giây, để hiển thị tốc độ đọc về từ driver.</summary>
        public double FromRpm(double rpm)
        {
            return rpm * UnitPerRev / 60.0;
        }
    }
}
