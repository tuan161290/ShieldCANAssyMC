#include "Communication/VmContract.h"

#include "Lang/Loc.h"

#include <atomic>
#include <cmath>
#include <limits>
#include <random>

namespace sca
{
    double VmCallResult::Get(const std::string& name) const
    {
        auto it = Values.find(name);
        return it != Values.end() ? it->second : std::numeric_limits<double>::quiet_NaN();
    }

    int VmCallResult::GetInt(const std::string& name) const
    {
        double v = Get(name);
        if (!VmMath::IsFinite(v)) return std::numeric_limits<int>::min();
        double r = std::nearbyint(v);   // Math.Round: làm tròn nửa về số chẵn (chế độ làm tròn mặc định)
        // (int) của C# với số ngoài khoảng Int32 không xác định; giữ an toàn bằng mã "không có".
        if (r < -2147483648.0 || r > 2147483647.0) return std::numeric_limits<int>::min();
        return static_cast<int>(r);
    }

    VmCallResult VmCallResult::Fail(const std::string& why, bool timedOut)
    {
        VmCallResult r;
        r.Ok = false;
        r.Error = why;
        r.TimedOut = timedOut;
        return r;
    }

    bool VmMath::IsFinite(double v) { return !std::isnan(v) && !std::isinf(v); }

    bool VmMath::Near(double a, double b, double tol)
    {
        return IsFinite(a) && IsFinite(b) && std::fabs(a - b) <= tol;
    }

    bool VmMath::SameInt(double a, int b) { return IsFinite(a) && std::fabs(a - b) < 0.5; }

    int VmRunIds::Next()
    {
        static std::atomic<int> last = []
        {
            std::random_device rd;
            std::uniform_int_distribution<int> dist(1, Max - 1);   // Random.Next(1, Max): cận trên không lấy
            return dist(rd);
        }();

        while (true)
        {
            int cur = last.load();
            int next = cur >= Max ? 1 : cur + 1;
            if (last.compare_exchange_weak(cur, next)) return next;
        }
    }

    const std::vector<std::string>& VmVars::Place()
    {
        static const std::vector<std::string> v{ RunId, HasTarget, CellX, CellY, CellA, HandEyeDx, HandEyeDy, AlignSign };
        return v;
    }

    const std::vector<std::string>& VmOut::LocateEcho()
    {
        static const std::vector<std::string> v{ EchoRunId, EchoX, EchoY2 };
        return v;
    }

    const std::vector<std::string>& VmOut::LocateTopScript()
    {
        static const std::vector<std::string> v{ TopFound, TopX, TopY, TopA, TopRunId };
        return v;
    }

    const std::vector<std::string>& VmOut::AlignEcho()
    {
        static const std::vector<std::string> v{ EchoRunId, EchoX, EchoA };
        return v;
    }

    const std::vector<std::string>& VmOut::AlignBottomScript()
    {
        static const std::vector<std::string> v{ ObjFound, ObjX, ObjY, ObjA, ObjRunId };
        return v;
    }

    const std::vector<std::string>& VmOut::AlignPlace()
    {
        static const std::vector<std::string> v{ Status, CmdX, CmdY2, CmdA, DiagDA, DiagEcc, DiagTarX, DiagTarY };
        return v;
    }

    std::string VmStatus::Place(int st)
    {
        switch (st)
        {
        case NoTarget: return Loc::T("Vm.StPlaceNoTarget");
        case Ok: return Loc::T("Vm.StPlaceOk");
        case Invalid: return Loc::T("Vm.StPlaceInvalid");
        default: return Loc::F("Vm.StPlaceOdd", st);
        }
    }

    std::string VmStatus::AlignVerify(int st)
    {
        switch (st)
        {
        case Ok: return Loc::T("Vm.StAlignOk");
        case NoConclusion: return Loc::T("Vm.StAlignNoConclusion");
        case AngleOut: return Loc::T("Vm.StAlignAngleOut");
        case EccOut: return Loc::T("Vm.StAlignEccOut");
        case Invalid: return Loc::T("Vm.StNotFinite");
        case AxisMoved: return Loc::T("Vm.StAlignAxisMoved");
        case Code8: return Loc::T("Vm.StAlignTurnSmall");
        default: return Loc::F("Vm.StOdd", st);
        }
    }

    std::string VmStatus::TopDownVerify(int st)
    {
        switch (st)
        {
        case Ok: return Loc::T("Vm.StTopOk");
        case NoConclusion: return Loc::T("Vm.StTopStepSmall");
        case EccOut: return Loc::T("Vm.StTopEccOut");
        case Invalid: return Loc::T("Vm.StNotFinite");
        case AxisMoved: return Loc::T("Vm.StTopAxisMoved");
        case Code8: return Loc::T("Vm.StTopWrongSign");
        default: return Loc::F("Vm.StOdd", st);
        }
    }
}
