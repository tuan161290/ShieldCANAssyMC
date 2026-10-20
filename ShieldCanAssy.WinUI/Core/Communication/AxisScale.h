#pragma once
#include "Communication/AxisConfig.h"

#include <cstdint>
#include <memory>

namespace sca
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
    ///
    /// <para>Giữ <c>shared_ptr</c> tới cấu hình (như tham chiếu C#): sửa AxisConfig ở trang Setting thì phép quy đổi
    /// theo ngay.</para>
    /// </summary>
    class AxisScale
    {
    public:
        /// <summary>cfg null → ném std::invalid_argument (ArgumentNullException của bản C#).</summary>
        explicit AxisScale(std::shared_ptr<const AxisConfig> cfg);

        /// <summary>Tiện cho test / chỗ chỉ có bản sao: giữ bản sao riêng của cấu hình.</summary>
        explicit AxisScale(const AxisConfig& cfg);

        /// <summary>Số xung trên một đơn vị, đã chặn giá trị vô nghĩa.</summary>
        double PulsePerUnit() const;

        double UnitPerRev() const;

        /// <summary>
        /// Số xung của một vòng 360° — chỉ có nghĩa với trục quay (<see cref="AxisConfig::WrapDegrees"/>).
        ///
        /// <para>Phải là SỐ NGUYÊN và phải tròn: mọi phép "đưa góc về vòng gần nhất" đều cộng
        /// trừ bội số của nó, nên một sai số nhỏ ở đây sẽ tích luỹ thành lệch góc thật sau
        /// nhiều vòng. A1/A2: 18000 xung/vòng động cơ × 2 vòng động cơ cho 1 vòng đầu hút
        /// (hộp số 1/2) = <b>36000 xung/360°</b>, tương ứng 100 xung/độ chẵn.</para>
        /// </summary>
        int PulsePerRev() const;

        /// <summary>mm (hoặc độ) → xung. Làm tròn về số nguyên gần nhất (nửa xa số 0), không cắt cụt.</summary>
        int ToPulse(double unit) const;

        /// <summary>Xung → mm (hoặc độ).</summary>
        double ToUnit(int pulse) const;

        /// <summary>
        /// Tốc độ đơn vị/giây → rpm, kẹp trong [1, MaxRpm].
        ///
        /// <para>Kẹp sàn ở 1 rpm là chủ ý: làm tròn xuống 0 sẽ thành lệnh "chạy với tốc độ 0",
        /// mà driver hiểu là đứng yên chứ không phải chạy chậm — trục không nhúc nhích và lệnh
        /// treo cho tới khi timeout, rất khó đoán ra nguyên nhân.</para>
        /// </summary>
        uint16_t ToRpm(double unitPerSec) const;

        /// <summary>Tốc độ đơn vị/giây → xung/giây, dạng tốc độ mà Fastech nhận (<c>lVelocity</c>).
        /// Kẹp sàn ở 1 xung/s với cùng lý do như <see cref="ToRpm"/>.</summary>
        uint32_t ToPulsePerSec(double unitPerSec) const;

        /// <summary>rpm → đơn vị/giây, để hiển thị tốc độ đọc về từ driver.</summary>
        double FromRpm(double rpm) const;

    private:
        std::shared_ptr<const AxisConfig> m_cfg;
    };
}
