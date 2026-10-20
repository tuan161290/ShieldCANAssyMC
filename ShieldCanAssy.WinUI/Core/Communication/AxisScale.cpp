#include "Communication/AxisScale.h"

#include <climits>
#include <cmath>
#include <stdexcept>

namespace sca
{
    namespace
    {
        /// <summary>(int)x của C# (unchecked) sau Math.Round: NaN hoặc ngoài dải int ra int.MinValue — giữ đúng hành vi
        /// bản C# thay vì để C++ rơi vào hành vi không xác định khi ép kiểu.</summary>
        int DotNetToInt(double x)
        {
            if (std::isnan(x) || x >= 2147483648.0 || x < -2147483648.0) return INT_MIN;
            return (int)x;
        }

        /// <summary>Math.Round(x, MidpointRounding.AwayFromZero) — std::round cũng làm tròn nửa xa số 0.</summary>
        double RoundAway(double x) { return std::round(x); }
    }

    AxisScale::AxisScale(std::shared_ptr<const AxisConfig> cfg) : m_cfg(std::move(cfg))
    {
        if (!m_cfg) throw std::invalid_argument("cfg");
    }

    AxisScale::AxisScale(const AxisConfig& cfg) : m_cfg(std::make_shared<const AxisConfig>(cfg)) {}

    double AxisScale::PulsePerUnit() const
    {
        return m_cfg->PulsePerUnit > 1e-9 ? m_cfg->PulsePerUnit : 1.0;
    }

    double AxisScale::UnitPerRev() const
    {
        return m_cfg->UnitPerRev > 1e-9 ? m_cfg->UnitPerRev : 1.0;
    }

    int AxisScale::PulsePerRev() const
    {
        double p = PulsePerUnit() * 360.0;
        if (p < 1.0) return 1;
        if (p > (double)INT_MAX) return INT_MAX;
        return DotNetToInt(RoundAway(p));
    }

    int AxisScale::ToPulse(double unit) const
    {
        double p = unit * PulsePerUnit();

        // Cắt ở biên int thay vì để tràn âm: tràn biến một lệnh đi xa thành một lệnh đi
        // ngược hết cỡ, đúng kiểu lỗi không ai ngờ tới khi nhìn code.
        if (p > (double)INT_MAX) return INT_MAX;
        if (p < (double)INT_MIN) return INT_MIN;
        // NaN lọt qua hai phép so trên — như bản C#, (int)NaN ra int.MinValue (AxisAngle::Wrap chặn NaN từ trước).
        return DotNetToInt(RoundAway(p));
    }

    double AxisScale::ToUnit(int pulse) const
    {
        return pulse / PulsePerUnit();
    }

    uint16_t AxisScale::ToRpm(double unitPerSec) const
    {
        double rpm = std::fabs(unitPerSec) / UnitPerRev() * 60.0;
        int r = DotNetToInt(RoundAway(rpm));

        int max = m_cfg->MaxRpm > 0 ? m_cfg->MaxRpm : 3000;
        if (r > max) r = max;
        if (r < 1) r = 1;
        return (uint16_t)r;
    }

    uint32_t AxisScale::ToPulsePerSec(double unitPerSec) const
    {
        double pps = std::fabs(unitPerSec) * PulsePerUnit();
        if (pps > (double)UINT_MAX) return UINT_MAX;
        if (std::isnan(pps)) return 1u;
        double rounded = RoundAway(pps);
        if (rounded > (double)UINT_MAX) return UINT_MAX;
        uint32_t v = (uint32_t)rounded;
        return v < 1 ? 1u : v;
    }

    double AxisScale::FromRpm(double rpm) const
    {
        return rpm * UnitPerRev() / 60.0;
    }
}
