#include "Controller/SinglePointAlignMath.h"

#include <cmath>

namespace sca
{
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;
    }

    void SinglePointAlignMath::Solve(double targetX, double targetY, double targetAngleDeg,
                                     double objectX, double objectY, double objectAngleDeg,
                                     double& deltaX, double& deltaY, double& deltaThetaDeg)
    {
        deltaThetaDeg = NormalizeDeg(targetAngleDeg - objectAngleDeg);

        double rad = deltaThetaDeg * Pi / 180.0;
        double cos = std::cos(rad), sin = std::sin(rad);

        // R(Δθ)·đối_tượng — đối tượng đo trong hệ có GỐC ĐẶT TẠI TÂM XOAY nên phép quay này
        // chính là "tâm sản phẩm dịch đi đâu sau khi xoay Δθ".
        double rx = objectX * cos - objectY * sin;
        double ry = objectX * sin + objectY * cos;

        deltaX = targetX - rx;
        deltaY = targetY - ry;
    }

    double SinglePointAlignMath::NormalizeDeg(double deg)
    {
        while (deg > 180.0) deg -= 360.0;
        while (deg <= -180.0) deg += 360.0;
        return deg;
    }
}
