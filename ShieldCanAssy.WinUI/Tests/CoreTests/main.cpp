// CoreTests.exe            chạy mọi nhóm kiểm thử (mã thoát 0 = đạt hết) — chỉ mô phỏng, không chạm phần cứng
// CoreTests.exe vmprobe    thử thiết bị VisionMaster thật (SDK C++) — cần máy đã cài VisionMaster 4.4; KHÔNG chạy khi app
//                          sản xuất đang mở VisionMaster
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "TestKit.h"

int RunInfraTests();
int RunFastechBindingTests();
int RunMotionTests();
int RunVisionTests();
int RunControllerFoundationTests();
int RunRoutineCalibTests();
int RunControllerTests();
int RunVisionMasterProbe();

namespace
{
    int RunGroup(const char* name, int (*fn)())
    {
        try { return fn(); }
        catch (const std::exception& ex) { std::printf("[%s] EXCEPTION: %s\n", name, ex.what()); return 1; }
        catch (...) { std::printf("[%s] EXCEPTION (unknown)\n", name); return 1; }
    }
}

int main(int argc, char** argv)
{
    SetConsoleOutputCP(CP_UTF8);

    if (argc > 1 && std::strcmp(argv[1], "vmprobe") == 0)
    {
        int failedChecks = RunVisionMasterProbe();
        std::printf("\nvmprobe: %d failed checks\n", failedChecks);
        return failedChecks == 0 ? 0 : 1;
    }

    int failed = 0;
    failed += RunGroup("Infra", RunInfraTests);
    failed += RunGroup("FastechBinding", RunFastechBindingTests);
    failed += RunGroup("Motion", RunMotionTests);
    failed += RunGroup("Vision", RunVisionTests);
    failed += RunGroup("ControllerFoundation", RunControllerFoundationTests);
    failed += RunGroup("RoutineCalib", RunRoutineCalibTests);
    failed += RunGroup("Controller", RunControllerTests);

    std::printf("\n%d checks, %d failed\n", testkit::Checks(), testkit::Failures());
    return (failed == 0 && testkit::Failures() == 0) ? 0 : 1;
}
