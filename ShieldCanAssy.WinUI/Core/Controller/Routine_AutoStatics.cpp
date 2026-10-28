// Các hàm THUẦN (static, không trạng thái) của Routine_Auto — tách khỏi Routine_Auto.cpp để bài tự kiểm nền
// (Tests\CoreTests\ControllerFoundationTests.cpp) khoá được luật chia ô / cảm biến tray mà không cần cả chu trình.
// Routine_Auto.cpp KHÔNG định nghĩa lại các hàm này.
#include "Controller/Routine_Auto.h"

#include <algorithm>
#include <cmath>

namespace sca
{
    bool Routine_Auto::TrayMissingAt(double axisPos, double standbyPos, bool sensorOn)
    {
        return !sensorOn && std::fabs(axisPos - standbyPos) <= TraySensorWindowMm;
    }

    bool Routine_Auto::Nozzle1TakesFirstCell(double xFirst, double xSecond, double sameColumnMm)
    {
        if (std::fabs(xFirst - xSecond) < sameColumnMm) return true;
        return xFirst <= xSecond;
    }

    bool Routine_Auto::Nozzle1TakesFirstPickCell(double firstAtNozzle1, double firstAtNozzle2,
                                                 double secondAtNozzle1, double secondAtNozzle2,
                                                 double sameColumnMm)
    {
        if (std::fabs(firstAtNozzle1 - secondAtNozzle1) < sameColumnMm) return true;
        return std::min(firstAtNozzle1, secondAtNozzle2) >= std::min(secondAtNozzle1, firstAtNozzle2);
    }

    int Routine_Auto::FirstToScan(const std::vector<double>& camX, double sameColumnMm)
    {
        int best = -1;
        for (int i = 0; i < (int)camX.size(); i++)
        {
            if (best < 0) { best = i; continue; }
            double d = camX[(size_t)i] - camX[(size_t)best];
            if (d > 0 && d >= sameColumnMm) best = i;
        }
        return best;
    }
}
