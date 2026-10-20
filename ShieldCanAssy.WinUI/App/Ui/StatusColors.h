#pragma once
// Bảng màu trạng thái — thay các IValueConverter của bản WPF (Converters\StatusConverters.cs). Trả ARGB;
// dùng ui::Solid(argb) để ra brush.
#include <cstdint>

#include "Infor/Enums.h"

namespace ui
{
    /// <summary>Mức alarm → màu nền nhãn/dòng.</summary>
    inline uint32_t AlarmLevelColor(sca::EAlarmLevel level)
    {
        switch (level)
        {
        case sca::EAlarmLevel::Fatal:   return 0xFFC42B1C;
        case sca::EAlarmLevel::Error:   return 0xFFE94949;
        case sca::EAlarmLevel::Warning: return 0xFF9D5D00;
        default:                        return 0xFF606060;
        }
    }

    /// <summary>
    /// Đèn tháp → màu đặc cho nhãn trạng thái ở thanh tiêu đề. CỐ Ý lấy thẳng từ Status.TowerLamp (nguồn
    /// sự thật, do McControl tính) thay vì tự suy lại từ EMcState lần hai — tránh nhãn trên màn hình và
    /// đèn tháp vật lý nói hai màu khác nhau cho cùng một trạng thái máy. Blink chỉ là hiệu ứng nháy trên
    /// đèn vật lý; nhãn tĩnh nên gộp về màu gốc.
    /// </summary>
    inline uint32_t TowerLampColor(sca::ETowerLamp lamp)
    {
        switch (lamp)
        {
        case sca::ETowerLamp::Green:
        case sca::ETowerLamp::GreenBlink:  return 0xFF389853;
        case sca::ETowerLamp::Red:
        case sca::ETowerLamp::RedBlink:    return 0xFFC42B1C;
        case sca::ETowerLamp::Yellow:
        case sca::ETowerLamp::YellowBlink: return 0xFF9D5D00;
        default:                           return 0xFF606060;
        }
    }

    /// <summary>Cùng tông với TowerLampColor nhưng chỉ là lớp phủ mờ — nền của nhãn kiểu Windows 11.</summary>
    inline uint32_t TowerLampTintColor(sca::ETowerLamp lamp)
    {
        return (TowerLampColor(lamp) & 0x00FFFFFF) | 0x1C000000;
    }

    /// <summary>Một bóng của đèn tháp vẽ trên màn hình: which = 'R' / 'Y' / 'G'.</summary>
    inline uint32_t TowerBulbColor(sca::ETowerLamp lamp, char which)
    {
        const uint32_t off = 0xFF3A3A3A;
        switch (which)
        {
        case 'R': return (lamp == sca::ETowerLamp::Red || lamp == sca::ETowerLamp::RedBlink) ? 0xFFE94949 : off;
        case 'Y': return (lamp == sca::ETowerLamp::Yellow || lamp == sca::ETowerLamp::YellowBlink) ? 0xFFFFC800 : off;
        case 'G': return (lamp == sca::ETowerLamp::Green || lamp == sca::ETowerLamp::GreenBlink) ? 0xFF389853 : off;
        default:  return off;
        }
    }

    /// <summary>Trạng thái máy → màu banner trạng thái ở trang Main.</summary>
    inline uint32_t McStateColor(sca::EMcState state)
    {
        switch (state)
        {
        case sca::EMcState::Running:      return 0xFF389853;
        case sca::EMcState::Idle:         return 0xFF0F7B0F;
        case sca::EMcState::Armed:        return 0xFF9D5D00;
        case sca::EMcState::Paused:
        case sca::EMcState::Held:
        case sca::EMcState::Pausing:
        case sca::EMcState::WaitOperator: return 0xFF9D5D00;
        case sca::EMcState::Alarm:        return 0xFFC42B1C;
        case sca::EMcState::Emg:          return 0xFF8B0000;
        case sca::EMcState::Homing:       return 0xFF0078D4;
        default:                          return 0xFF606060;
        }
    }
}
