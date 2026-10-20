#include "Fastech/EziAxisStatus.h"

#include "Lang/Loc.h"

#include <array>
#include <format>
#include <utility>

namespace sca
{
    namespace
    {
        using E = EziAxisStatus;

        constexpr std::array<std::pair<uint32_t, const char*>, 31> Names = { {
            { E::ErrorAll,        "ERR_ALL" },
            { E::HwPosLimit,      "HW_LIMIT+" },
            { E::HwNegLimit,      "HW_LIMIT−" },
            { E::SwPosLimit,      "SW_LIMIT+" },
            { E::SwNegLimit,      "SW_LIMIT−" },
            { E::PosOverflow,     "POS_OVERFLOW" },
            { E::OverCurrent,     "OVER_CURRENT" },
            { E::OverSpeed,       "OVER_SPEED" },
            { E::PosTracking,     "POS_TRACKING" },
            { E::OverLoad,        "OVER_LOAD" },
            { E::OverHeat,        "OVER_HEAT" },
            { E::BackEmf,         "BACK_EMF" },
            { E::MotorPower,      "MOTOR_POWER" },
            { E::InPositionErr,   "INPOS_ERR" },
            { E::SystemErr,       "SYSTEM_ERR" },
            { E::EmergencyStop,   "EMG" },
            { E::SlowStop,        "SLOW_STOP" },
            { E::OriginReturning, "ORG_RUNNING" },
            { E::InPosition,      "IN_POSITION" },
            { E::ServoOn,         "SERVO_ON" },
            { E::AlarmReset,      "ALARM_RESET" },
            { E::PtStopped,       "PT_STOPPED" },
            { E::OriginSensor,    "ORG_SENSOR" },
            { E::ZPulse,          "Z_PULSE" },
            { E::OriginReturnOk,  "ORG_OK" },
            { E::MotionDir,       "DIR" },
            { E::MotionPause,     "PAUSE" },
            { E::MotionAccel,     "ACCEL" },
            { E::MotionDecel,     "DECEL" },
            { E::MotionConst,     "CONST" },
            { E::Motioning,       "MOTIONING" },
        } };
    }

    std::string EziAxisStatus::Describe(uint32_t status)
    {
        if (status == 0) return Loc::T("Axis.NoStatusBits");

        std::string sb;
        for (const auto& kv : Names)
        {
            if ((status & kv.first) == 0) continue;
            if (!sb.empty()) sb += ' ';
            sb += kv.second;
        }

        // Bit lạ không có trong bảng: hiện ra thay vì nuốt, vì nó chính là dấu hiệu bảng bit
        // đang sai hoặc firmware khác phiên bản.
        uint32_t known = 0;
        for (const auto& kv : Names) known |= kv.first;
        uint32_t unknown = status & ~known;
        if (unknown != 0) sb += std::format(" +0x{:08X}", unknown);

        return sb;
    }
}
