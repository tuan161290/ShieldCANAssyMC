// Kiểm Routine_Calib (wizard hiệu chuẩn vision, xác định hand-eye, chạy kiểm từng bước) trên máy giả dựng tay — KHÔNG qua
// McControl: MotionConnect (FakeAxis) + FakeIo + SafetyMonitor + McFunction + FakeVision, một vòng quét giả trên luồng
// riêng. Port các phép kiểm của ShieldCanAssy.Wpf\SelfTest.cs: TestRoutineCalib, TestVmCalib, TestVmStepCheck (bỏ phần cần
// McControl — xem cuối file). Chữ lý do so qua Loc::T/Loc::F (bản C# so chuỗi tiếng Việt).
//
// AN TOÀN: máy chạy test là máy lắp ráp thật. SimulationMode ép = true TRƯỚC MotionConnect::Init (chỉ dựng FakeAxis), I/O
// là FakeIo, vision là FakeVision — không cổng COM, không EziMOTIONPlusE.dll, không VisionMaster. Dữ liệu ghi vào thư mục
// tạm mới.
#include "TestKit.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "Base/Async.h"
#include "Base/Text.h"
#include "Communication/AxisAngle.h"
#include "Communication/GlobalVarSync.h"
#include "Communication/MotionConnect.h"
#include "Communication/Sim/FakeAxis.h"
#include "Communication/Sim/FakeIo.h"
#include "Communication/Sim/FakeVision.h"
#include "Communication/VmContract.h"
#include "Controller/McFunction.h"
#include "Controller/McStatus.h"
#include "Controller/Routine_Calib.h"
#include "Controller/SafetyMonitor.h"
#include "Controller/SinglePointAlignMath.h"
#include "Controller/VisionReadout.h"
#include "Controller/VmCycle.h"
#include "Data/McDataManager.h"
#include "Data/Model/ModelData.h"
#include "Data/SystemData.h"
#include "Infor/AlarmCatalog.h"
#include "Infor/AlarmManager.h"
#include "Infor/AxisId.h"
#include "Infor/IoMap.h"
#include "Lang/Loc.h"
#include "SysConfig/AppPaths.h"
#include "Utility/Log/LogWorker.h"

using namespace sca;
namespace fs = std::filesystem;

namespace
{
    /// <summary>Near của SelfTest.cs: trục giả dừng lệch vài µm.</summary>
    bool Near(double a, double b) { return std::fabs(a - b) < 0.02; }
    bool Contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
    std::string Num(double v) { return Loc::Format("{0:0.###}", { v }); }

    bool WaitUntil(const std::function<bool()>& cond, int timeoutMs)
    {
        base::Stopwatch sw;
        while (sw.ElapsedMilliseconds() < timeoutMs)
        {
            if (cond()) return true;
            base::Delay(10);
        }
        return cond();
    }

    /// <summary>Phần chữ của khoá Loc đứng trước "ΔX " khi điền <paramref name="args"/> — mốc để đọc ΔX/ΔY trong một
    /// dòng kết quả mà không ghi cứng câu chữ.</summary>
    template <class... A>
    std::string DeltaPrefix(const char* key, const A&... args)
    {
        std::string s = Loc::F(key, args...);
        size_t p = s.find("ΔX ");
        return p == std::string::npos ? s : s.substr(0, p);
    }

    /// <summary>DeltaNear của SelfTest.cs: chữ có "prefix ΔX …, ΔY …" với hai số gần (dx, dy).</summary>
    bool DeltaNear(const std::string& text, const std::string& prefix, double dx, double dy)
    {
        const std::string head = prefix + "ΔX ";
        size_t p = text.find(head);
        if (p == std::string::npos) return false;
        const char* s = text.c_str() + p + head.size();
        char* end = nullptr;
        double vx = std::strtod(s, &end);
        if (end == s) return false;
        const std::string sep = ", ΔY ";
        if (std::strncmp(end, sep.c_str(), sep.size()) != 0) return false;
        const char* s2 = end + sep.size();
        char* end2 = nullptr;
        double vy = std::strtod(s2, &end2);
        if (end2 == s2) return false;
        return Near(vx, dx) && Near(vy, dy);
    }

    /// <summary>Máy giả: thứ tự khai báo = thứ tự dựng; vòng quét khai báo CUỐI nên dừng TRƯỚC khi các thiết bị bị huỷ.</summary>
    struct Rig
    {
        std::shared_ptr<SystemData> sys;
        std::function<std::shared_ptr<SystemData>()> sysFn;
        MotionConnect motion;
        McStatus status;
        std::shared_ptr<FakeIo> io;
        std::unique_ptr<SafetyMonitor> safety;
        std::unique_ptr<McFunction> func;
        std::shared_ptr<FakeVision> fv;
        std::unique_ptr<Routine_Calib> calib;
        std::jthread scan;

        std::shared_ptr<IAxis> Ax(const char* name) const { return motion[name]; }
        double P(const std::string& name) const { auto a = motion[name]; return a ? a->CurrentPos() : std::nan(""); }
        bool MoveAxis(const std::string& name, double pos)
        {
            return func->Move(name, pos, sys->GetAxis(name)->MoveSpeed, std::stop_token{});
        }
    };

    void ClearFaults(FakeVision& fv)
    {
        fv.SetSimulateOptics(false);
        fv.SetBottomAngleSign(1);
        fv.SetMarkA(0);
        fv.SetForcedObjPose(std::nullopt);
        fv.SetForcePlaceStatusTimes(0);
        fv.SetForceDAngleTimes(0);
        fv.SetTargetShiftTimes(0);
        fv.SetCorruptEchoOnce(false);
        fv.SetAfterFlow(nullptr);
        fv.SetFoundRate(1.0);
        fv.SetBottomFoundRate(1.0);
    }

    /// <summary>VmSetup của SelfTest.cs: model mô phỏng mới tinh, kho biến giả đủ 13 biến, Bước 2 đã ghi ngày.</summary>
    std::shared_ptr<ModelData> VmSetup(Rig& r)
    {
        McDataManager::UseModel(ModelData::CreateSimulation());
        ClearFaults(*r.fv);
        r.fv->SeedVmSolution(VisionGlobalVarCatalog::Build(r.sys.get()));
        auto model = McDataManager::Model();
        r.fv->StampCalibrated(&model->Calib);
        return model;
    }

    bool BuildRig(Rig& r)
    {
        r.sys = McDataManager::SysData();
        // Ép mô phỏng TRƯỚC Init: Init chọn lớp trục theo cờ này — false là dựng trục thật.
        r.sys->SimulationMode = true;
        r.sys->BottomSettleMs = 0;
        r.sys->TopSettleMs = 0;
        r.sys->BottomLampMs = 0;
        r.sys->TopLampMs = 0;
        auto sys = r.sys;
        r.sysFn = [sys]() { return sys; };

        r.motion.Init(*r.sys);
        if (!r.motion.Simulation()) return false;
        for (const auto& a : r.motion.AxisList())
            if (std::dynamic_pointer_cast<FakeAxis>(a) == nullptr) return false;

        r.io = std::make_shared<FakeIo>();
        r.io->Connect();
        r.safety = std::make_unique<SafetyMonitor>(r.motion, r.status, r.sysFn);
        r.safety->AttachIo(r.io);
        r.func = std::make_unique<McFunction>(r.motion, *r.safety, r.status, r.sysFn);
        r.func->SetIo(r.io);

        r.fv = std::make_shared<FakeVision>();
        r.fv->SetProcessTimeMs(0);
        r.fv->Connect();
        r.fv->LoadSolution("sim.solw");
        r.fv->SetNames([sys]() { return sys->VisionOutputs; });
        auto fv = r.fv;
        r.calib = std::make_unique<Routine_Calib>(r.motion, *r.func, r.sysFn, [fv]() -> std::shared_ptr<IVisionDevice> { return fv; });

        // Vòng quét giả — vai trò vòng điều khiển của McControl: trục, I/O, an toàn, phanh.
        Rig* rp = &r;
        r.scan = std::jthread([rp](std::stop_token st)
        {
            while (!st.stop_requested())
            {
                rp->motion.Refresh();
                rp->io->Refresh();
                rp->safety->Evaluate();
                rp->func->UpdateBrakeOutputs();
                base::Delay(2);
            }
        });

        if (!r.motion.ConnectAll()) return false;
        for (const auto& a : r.motion.AxisList()) a->SetServoOn(true);
        r.func->RequestBrakeRelease(AxisId::Z1);
        r.func->RequestBrakeRelease(AxisId::Z2);

        // Về gốc (bản tay của Routine_Origin): Z trước, rồi các trục còn lại.
        std::stop_source never;
        bool homed = true;
        for (const char* n : { AxisId::Z1, AxisId::Z2, AxisId::X, AxisId::Y1, AxisId::Y2, AxisId::A1, AxisId::A2 })
            homed = r.motion.Home(r.motion[n], never.get_token()) && homed;
        // Trục giả khởi đầu lệch khỏi gốc có thể đã chạm cữ cứng trước khi về gốc — alarm đó đã hết nguyên nhân.
        base::Delay(20);
        AlarmManager::ForceClearAll();
        bool ready = WaitUntil([&] { return r.safety->SafeToMove() && r.func->BrakeReady(AxisId::Z1) && r.func->BrakeReady(AxisId::Z2); }, 3000);
        CHECK_MSG(homed && r.motion.AllHomed(), "home");
        CHECK_MSG(ready, r.safety->BlockReason());
        return homed && ready;
    }

    // ───────────────────────── SelfTest.TestRoutineCalib (FakeVision tắt quang học giả) ─────────────────────────

    void TestRoutineCalib(Rig& r)
    {
        auto model = VmSetup(r);
        auto& calib = *r.calib;
        auto& fv = *r.fv;
        std::stop_token ct;
        auto x = r.Ax(AxisId::X), y2 = r.Ax(AxisId::Y2), a1 = r.Ax(AxisId::A1), z1 = r.Ax(AxisId::Z1);

        // ── §6.2b: lượng dịch khớp mẫu là nhiễu quanh 0, KHÔNG gắn với trục → phép kiểm tra phải KHÔNG ĐẠT ──
        double x0 = x->CurrentPos(), y0 = y2->CurrentPos();
        auto td = calib.VerifyTopDown(5.0, ct);
        CHECK_MSG(!td.Ok, td.Reason);
        CHECK_MSG(Near(x->CurrentPos(), x0) && Near(y2->CurrentPos(), y0),
                  "X=" + Num(x->CurrentPos()) + "/" + Num(x0) + " Y2=" + Num(y2->CurrentPos()) + "/" + Num(y0));
        auto tdSmall = calib.VerifyTopDown(0.1, ct);
        CHECK_MSG(!tdSmall.Ok && tdSmall.Reason == Loc::T("Calib.StepTooSmall"), tdSmall.Reason);

        // ── §6.4 chuỗi nạp điểm Align: tịnh tiến X rồi xoay A, kết thúc ở 0°, Z ở cao độ soi ──
        double alignPosX = model->AlignPosX(1);
        double zCapture = r.motion.ZSafe();
        bool z1Away = r.MoveAxis(AxisId::Z1, 0) && !Near(z1->CurrentPos(), zCapture);
        auto seq = calib.RunAlignCalibSequence(1, alignPosX, { -1, 0, 1 }, { 0, 60, 120 }, nullptr, ct);
        CHECK_MSG(seq.Ok, seq.Reason);
        CHECK_MSG(seq.PointsCaptured == 6, std::to_string(seq.PointsCaptured));
        CHECK_MSG(AxisAngle::Distance(a1->CurrentPos(), 0) < 0.05, "A1=" + Num(a1->CurrentPos()));
        CHECK_MSG(Near(x->CurrentPos(), alignPosX), "X=" + Num(x->CurrentPos()));
        CHECK_MSG(z1Away && Near(z1->CurrentPos(), zCapture), "Z1=" + Num(z1->CurrentPos()));

        auto seqFew = calib.RunAlignCalibSequence(1, alignPosX, { 0 }, { 0, 90 }, nullptr, ct);
        CHECK_MSG(!seqFew.Ok && seqFew.Reason == Loc::T("Calib.NeedThreeAngles"), seqFew.Reason);

        // ── Chuỗi nạp điểm tự bấm "Clear Point" trước (Tuấn 2026-10-09) ──
        CHECK(fv.LastCalibClear() == EVisionProcedure::AlignCalibTool1 && fv.CalibPointsSinceClear() == seq.PointsCaptured);
        {
            int pointsBefore = fv.CalibPointCount();
            double xBefore = x->CurrentPos();
            fv.SetFailCalibClear(true);
            auto seqNoClear = calib.RunAlignCalibSequence(1, alignPosX, { -1, 0, 1 }, { 0, 60, 120 }, nullptr, ct);
            fv.SetFailCalibClear(false);
            CHECK_MSG(!seqNoClear.Ok && fv.CalibPointCount() == pointsBefore && Near(x->CurrentPos(), xBefore) &&
                      seqNoClear.Reason == Loc::F("Calib.ClearPointsFailed", Loc::T("Vm.SimClearFail")), seqNoClear.Reason);
        }

        // progress được gọi đúng số bước (bản C++ hiện tiến độ qua callback).
        {
            int calls = 0;
            auto seqProg = calib.RunAlignCalibSequence(1, alignPosX, { -1, 1 }, { 0, 120, 240 }, [&](const std::string&) { ++calls; }, ct);
            CHECK_MSG(seqProg.Ok && calls == 5 && seqProg.PointsCaptured == 5, seqProg.Reason);
        }

        bool alignCalibOk = calib.MoveAndTriggerAlignCalib(1, 60, ct);
        CHECK_MSG(alignCalibOk && Near(a1->CurrentPos(), 60), "A1=" + Num(a1->CurrentPos()));

        // ── Mỗi điểm hiệu chuẩn mang theo VỊ TRÍ TRỤC THẬT (encoder), đủ bốn trục ──
        auto sent = fv.LastCalibAxes();
        CHECK(sent.has_value());
        if (sent)
        {
            CHECK(Near(sent->A1, a1->CurrentPos()));
            CHECK(Near(sent->X, x->CurrentPos()));
            CHECK(Near(sent->Y2, y2->CurrentPos()) && Near(sent->A2, r.P(AxisId::A2)));
        }

        int calibPointsBefore = fv.CalibPointCount();
        CHECK(calib.TriggerTopDownCalibFlow(ct));
        CHECK(fv.CalibPointCount() == calibPointsBefore + 1);

        z1Away = r.MoveAxis(AxisId::Z1, 0) && !Near(z1->CurrentPos(), zCapture);
        bool alignFlowOk = calib.TriggerAlignCalibFlow(1, ct);
        CHECK_MSG(alignFlowOk && Near(a1->CurrentPos(), 60), "A1=" + Num(a1->CurrentPos()));
        CHECK_MSG(z1Away && Near(z1->CurrentPos(), zCapture), "Z1=" + Num(z1->CurrentPos()));

        bool toZeroOk = calib.MoveAlignAxisToZero(1, ct);
        CHECK_MSG(toZeroOk && AxisAngle::Distance(a1->CurrentPos(), 0) < 0.05, "A1=" + Num(a1->CurrentPos()));

        // Nạp điểm top-down qua MoveAndTrigger: tới đúng (x, y2) rồi nạp một điểm.
        {
            int before = fv.CalibPointCount();
            bool ok = calib.MoveAndTriggerTopDownCalib(model->HandEyeCamPos.X, model->HandEyeCamPos.Y, ct);
            CHECK(ok && fv.CalibPointCount() == before + 1 && Near(x->CurrentPos(), model->HandEyeCamPos.X) &&
                  Near(y2->CurrentPos(), model->HandEyeCamPos.Y));
        }

        // ── §6.4b: góc mô phỏng không gắn với trục → TỪ CHỐI, và trả A về góc ban đầu ──
        double a1Before = a1->CurrentPos();
        auto av = calib.VerifyAlignRotation(1, 10.0, ct);
        CHECK_MSG(!av.Ok, av.Reason);
        CHECK_MSG(AxisAngle::Distance(a1->CurrentPos(), a1Before) < 0.05, "A1=" + Num(a1->CurrentPos()) + "/" + Num(a1Before));
        auto avSmall = calib.VerifyAlignRotation(1, 0.2, ct);
        CHECK_MSG(!avSmall.Ok && avSmall.Reason == Loc::T("Calib.TestAngleTooSmall"), avSmall.Reason);

        // ── §6.5 hand-eye — từng biên một lý do, trục KHÔNG nhúc nhích; rồi đường thuận ──
        TeachPoint keepPick1 = model->HandEyePick_Nozzle1;
        TeachPoint keepCam = model->HandEyeCamPos;
        double heX0 = x->CurrentPos(), heY0 = y2->CurrentPos();

        model->HandEyePick_Nozzle1 = TeachPoint();
        auto heNoPick = calib.HandEyeGoToPick(1, ct);
        CHECK_MSG(!heNoPick.Ok && !heNoPick.Reason.empty() && heNoPick.Reason == model->HandEyeJigProblem(1), heNoPick.Reason);
        model->HandEyePick_Nozzle1 = keepPick1;
        model->HandEyeCamPos = TeachPoint();
        auto heNoCam = calib.HandEyeGoToPick(1, ct);
        CHECK_MSG(!heNoCam.Ok && !heNoCam.Reason.empty() && heNoCam.Reason == model->HandEyeJigProblem(1), heNoCam.Reason);
        CHECK(Near(x->CurrentPos(), heX0) && Near(y2->CurrentPos(), heY0));
        model->HandEyeCamPos = keepCam;

        calib.HandEyeClear();
        auto heNoStart = calib.HandEyePick(1, ct);
        CHECK_MSG(!heNoStart.Ok && heNoStart.Reason == Loc::T("Calib.HeNotStarted"), heNoStart.Reason);
        auto heNoLoc = calib.HandEyeLocate(1, ct);
        CHECK_MSG(!heNoLoc.Ok && heNoLoc.Reason == Loc::T("Calib.HeNotStarted"), heNoLoc.Reason);
        auto heNoCommit = calib.HandEyeCommit(1, ct);
        CHECK_MSG(!heNoCommit.Ok, heNoCommit.Reason);
        auto heNoTarget = calib.HandEyeCheckTarget(1, ct);
        CHECK_MSG(!heNoTarget.Ok, heNoTarget.Reason);
        auto heNoHold = calib.HandEyeReturn(1, ct);
        CHECK_MSG(!heNoHold.Ok && heNoHold.Reason == Loc::F("Calib.NozzleHoldsNothing", 1), heNoHold.Reason);

        auto heGo = calib.HandEyeGoToPick(1, ct);
        TeachPoint heAssyCell = model->AssyTray->CellPos(AxisId::Z1, 0, 0);
        TeachPoint heInputCell = model->InputTray->CellPos(AxisId::Z1, 0, 0);
        CHECK_MSG(heGo.Ok && Near(x->CurrentPos(), keepPick1.X) && Near(y2->CurrentPos(), keepPick1.Y),
                  heGo.Reason + " X=" + Num(x->CurrentPos()) + " Y2=" + Num(y2->CurrentPos()));
        CHECK(!Near(keepPick1.X, heAssyCell.X) && !Near(keepPick1.X, heInputCell.X) &&
              !Near(keepPick1.X, model->HandEyePick_Nozzle2.X) && !Near(keepPick1.Y, model->HandEyePick_Nozzle2.Y));

        auto heWrongTool = calib.HandEyePick(2, ct);
        CHECK_MSG(!heWrongTool.Ok && heWrongTool.Reason == Loc::F("Calib.HeOtherNozzle", 1, 2), heWrongTool.Reason);
        calib.HandEyeClear();
    }

    // ───────────────────────── SelfTest.TestVmCalib (quang học giả bật) ─────────────────────────

    void TestVmCalib(Rig& r)
    {
        AlarmManager::ForceClearAll();
        auto model = VmSetup(r);
        auto& calib = *r.calib;
        auto& fv = *r.fv;
        auto sys = r.sys;
        const VisionOutputNames& names = sys->VisionOutputs;
        VisionCalib& cal = model->Calib;
        std::stop_token ct;
        auto x = r.Ax(AxisId::X), y2 = r.Ax(AxisId::Y2), a1 = r.Ax(AxisId::A1), z1 = r.Ax(AxisId::Z1);
        r.func->SetVacuum(1, false);
        r.func->SetVacuum(2, false);

        fv.SetSimulateOptics(true);

        // ── Bước 2 ──
        cal.TopDownVerifiedAt = TimePoint{};
        fv.SetMarkX(x->CurrentPos() + 0.3);
        fv.SetMarkY(y2->CurrentPos() - 0.2);
        double x0 = x->CurrentPos(), y0 = y2->CurrentPos();
        auto td = calib.VerifyTopDown(5.0, ct);
        CHECK_MSG(td.Ok && td.Status == VmStatus::Ok, td.Reason + " | " + td.Note);
        CHECK(cal.IsTopDownVerified());
        CHECK(td.Note == Loc::T("Calib.ModelNotLoadedNote"));   // model mô phỏng chưa nạp từ đĩa: chỉ ghi trong bộ nhớ
        CHECK(Near(x->CurrentPos(), x0) && Near(y2->CurrentPos(), y0));

        // ── Đèn CAM quanh mỗi lần chụp — đọc đúng lúc chụp qua AfterFlow ──
        auto io = r.io;
        std::atomic<bool> camTopOn{ false }, camBottomOn{ false };
        fv.SetAfterFlow([&](const std::string&) { camTopOn = io->GetDo(IoMap::DoLampCam2); camBottomOn = io->GetDo(IoMap::DoLampCam1); });
        sys->TopLampMs = 300;
        sys->BottomLampMs = 0;
        base::Stopwatch lampSw;
        calib.TriggerTopDownCalibFlow(ct);
        long long topMs = lampSw.ElapsedMilliseconds();
        CHECK_MSG(camTopOn && !camBottomOn, std::string("CAM2 ") + (camTopOn ? "on" : "off") + ", CAM1 " + (camBottomOn ? "on" : "off"));
        CHECK_MSG(topMs >= 250, std::to_string(topMs) + " ms");
        CHECK(!io->GetDo(IoMap::DoLampCam1) && !io->GetDo(IoMap::DoLampCam2));

        camTopOn = false; camBottomOn = false;
        sys->TopLampMs = 0;
        sys->BottomLampMs = 300;
        lampSw.Restart();
        calib.TriggerAlignCalibFlow(1, ct);
        long long bottomMs = lampSw.ElapsedMilliseconds();
        sys->BottomLampMs = 0;
        sys->TopLampMs = 0;
        fv.SetAfterFlow(nullptr);
        CHECK_MSG(bottomMs >= 250, std::to_string(bottomMs) + " ms");
        CHECK(camBottomOn && !camTopOn);
        CHECK(!io->GetDo(IoMap::DoLampCam1) && !io->GetDo(IoMap::DoLampCam2));

        // ── Nút "Z1 → Z_SAFE" ──
        r.MoveAxis(AxisId::Z1, sys->ZSafe + 5);
        auto zsafe = calib.StepMoveZSafe(1, ct);
        CHECK_MSG(zsafe.Ok && Near(z1->CurrentPos(), sys->ZSafe), zsafe.Reason + " Z1=" + Num(z1->CurrentPos()));

        // ── Bước 4 — App tự phán ──
        double alignX = model->AlignPosX(1);
        r.MoveAxis(AxisId::X, alignX);
        cal.SetAlignVerified(1, 0, 0, 0, TimePoint{});
        double aStart = a1->CurrentPos();
        int saves0 = fv.SolutionSaveCount();
        auto av = calib.VerifyAlignRotation(1, 10.0, ct);
        CHECK_MSG(av.Ok && av.ResidualMm < 0.02, av.Reason + " | residual " + Num(av.ResidualMm));
        CHECK_MSG(cal.AlignAngleSign1 == 1 && Near(cal.AlignVerifiedPosX1, alignX), std::to_string(cal.AlignAngleSign1));
        CHECK(fv.SolutionSaveCount() == saves0);
        CHECK(AxisAngle::Distance(a1->CurrentPos(), aStart) < 0.05);

        auto avNeg = calib.VerifyAlignRotation(1, -10.0, ct);
        CHECK_MSG(avNeg.Ok && avNeg.Sign == av.Sign, avNeg.Reason);

        fv.SetBottomAngleSign(-1);
        auto avRev = calib.VerifyAlignRotation(1, 10.0, ct);
        CHECK_MSG(avRev.Ok && avRev.Sign == -1 && cal.AlignAngleSign1 == -1, avRev.Reason);
        fv.SetBottomAngleSign(1);

        // Góc sản phẩm đứng yên khi A xoay → không đạt; bảng báo kèm số đo (góc encoder, góc đo, sai số dư, cao độ Z).
        fv.SetForcedObjPose(std::array<double, 3>{ 0.8, -0.6, 7.0 });
        auto avStuck = calib.VerifyAlignRotation(1, 10.0, ct);
        std::string stuckHead = Loc::F("Calib.AlignVerifyFail", 1, "", "");
        stuckHead = stuckHead.substr(0, stuckHead.find(':'));
        CHECK_MSG(!avStuck.Ok && Contains(avStuck.Reason, stuckHead) && Contains(avStuck.Reason, "+0.00°") &&
                  Contains(avStuck.Reason, AxisId::Z1) && Contains(avStuck.Reason, AxisId::A1), avStuck.Reason);
        fv.SetForcedObjPose(std::nullopt);
        cal.SetAlignVerified(1, 1, alignX, 0, std::chrono::system_clock::now());

        // ── Xác định hand-eye: gắp trên CƠ CẤU KẸP ──
        TeachPoint jigPick = model->HandEyePick(1);
        TeachPoint jigCam = model->HandEyeCamPos;
        fv.SetMarkX(jigCam.X + 0.4);
        fv.SetMarkY(jigCam.Y - 0.3);
        fv.SetMarkA(2.0);
        fv.SetForcedObjPose(std::array<double, 3>{ 0.8, -0.6, 7.0 });
        int heShots = std::max(1, sys->CalibShotsPerPoint);

        calib.HandEyeClear();
        auto he1 = calib.HandEyeGoToPick(1, ct);
        auto he2 = calib.HandEyePick(1, ct);
        CHECK_MSG(he1.Ok && he2.Ok, he1.Reason + " | " + he2.Reason);
        CHECK(Near(x->CurrentPos(), jigPick.X) && Near(y2->CurrentPos(), jigPick.Y));
        CHECK(r.func->HasProduct(1) && McDataManager::RunData()->Nozzle1->HasProduct && McDataManager::RunData()->Nozzle1->SourceCellIndex == -1);
        auto heBusy = calib.HandEyeGoToPick(1, ct);
        CHECK_MSG(!heBusy.Ok && heBusy.Reason == Loc::F("Calib.NozzleBusyRelease", 1), heBusy.Reason);

        // Nút 3 kích LocateTop khi gantry ĐANG ở điểm soi đã dạy (chụp trong AfterFlow).
        std::atomic<double> atFlowX{ std::nan("") }, atFlowY{ std::nan("") };
        int locBefore = fv.LocateTopCount();
        fv.SetAfterFlow([&](const std::string& flow) { if (flow == names.FlowLocateTop) { atFlowX = x->CurrentPos(); atFlowY = y2->CurrentPos(); } });
        auto he3 = calib.HandEyeLocate(1, ct);
        fv.SetAfterFlow(nullptr);
        CHECK_MSG(he3.Ok, he3.Reason);
        CHECK(Near(atFlowX, jigCam.X) && Near(atFlowY, jigCam.Y) && !Near(jigCam.X, model->AssyTray->CamPosFor(0, 0).X));
        CHECK_MSG(fv.LocateTopCount() - locBefore == heShots, std::to_string(fv.LocateTopCount() - locBefore));

        // Nút 5 trước nút 4: X chưa ở vị trí soi CAM dưới.
        auto heEarlyX = calib.HandEyeCommit(1, ct);
        CHECK_MSG(!heEarlyX.Ok && heEarlyX.Reason == Loc::F("Calib.XNotAtAlignPos4", x->CurrentPos(), 1, model->AlignPosX(1)), heEarlyX.Reason);

        auto he4 = calib.HandEyeGoToAlign(1, ct);
        CHECK_MSG(he4.Ok && Near(x->CurrentPos(), model->AlignPosX(1)), he4.Reason);

        // Jog A1 đi 4° giữa nút 4 và nút 5 → nút 5 xoay về góc lúc gắp rồi mới kích AlignBottom.
        double heAPick = model->PickAngle(1);
        bool heJogA = r.MoveAxis(AxisId::A1, AxisAngle::Wrap(heAPick + 4.0)) && AxisAngle::Distance(a1->CurrentPos(), heAPick) > 3.0;
        std::atomic<int> heAlignShots{ 0 };
        std::atomic<double> heWorstA{ 0 };
        fv.SetAfterFlow([&](const std::string& flow)
        {
            if (flow != names.FlowAlignBottom(1)) return;
            ++heAlignShots;
            heWorstA = std::max(heWorstA.load(), AxisAngle::Distance(a1->CurrentPos(), heAPick));
        });
        double expectDx = jigPick.X + 0.8 - fv.MarkX(), expectDy = jigPick.Y + VmCycle::BottomYSign * -0.6 - fv.MarkY();
        auto hePick = calib.HandEyeCommit(1, ct);
        fv.SetAfterFlow(nullptr);
        CHECK_MSG(heJogA && heAlignShots == heShots && heWorstA < 0.05, std::to_string(heAlignShots.load()) + " shots, worst " + Num(heWorstA));
        CHECK_MSG(hePick.Ok && Near(hePick.Dx, expectDx) && Near(hePick.Dy, expectDy) && Near(cal.HandEye1Dx, expectDx),
                  hePick.Reason + " dx " + Num(hePick.Dx) + "/" + Num(expectDx) + " dy " + Num(hePick.Dy) + "/" + Num(expectDy));
        CHECK_MSG(hePick.Ok && std::fabs(hePick.AngleRef - 9.0) < 0.05 && cal.HandEye1AngleRef && std::fabs(*cal.HandEye1AngleRef - 9.0) < 0.05,
                  Num(hePick.AngleRef));
        CHECK(r.func->HasProduct(1));

        // Đặt lại chính con can đó vào chỗ vừa gắp: góc bù 0, tâm xoay về đúng chỗ gắp.
        {
            std::map<std::string, double> gBack;
            PlacedTopRecord rec;
            rec.X = fv.MarkX(); rec.Y = fv.MarkY(); rec.A = fv.MarkA();
            VmCycle::AddPlaceGlobals(gBack, &rec, &cal, 1);
            double sdx = 0, sdy = 0, sdt = 0;
            SinglePointAlignMath::Solve(gBack[VmVars::CellX] + gBack[VmVars::HandEyeDx], gBack[VmVars::CellY] + gBack[VmVars::HandEyeDy],
                                        gBack[VmVars::CellA], 0.8, -0.6, 7.0, sdx, sdy, sdt);
            sdy *= VmCycle::BottomYSign;
            CHECK_MSG(std::fabs(AxisAngle::ShortestDelta(0, sdt)) < 0.05 && std::fabs(sdx - jigPick.X) < 0.05 && std::fabs(sdy - jigPick.Y) < 0.05,
                      Num(sdt) + " " + Num(sdx) + " " + Num(sdy));
        }

        // ── Phép thử tổng quát (nút 6 → 7 → 8) ở HAI chỗ Jog khác nhau ──
        auto heTargetEarly = calib.HandEyeCheckTarget(1, ct);
        CHECK_MSG(!heTargetEarly.Ok && heTargetEarly.Reason == Loc::F("Calib.NeedLocateHere", 1), heTargetEarly.Reason);

        const double jogSpots[2][2] = { { jigCam.X + 7.0, jigCam.Y + 5.0 }, { jigCam.X - 9.0, jigCam.Y - 4.0 } };
        std::vector<double> cmdX, cmdY;
        bool jogRan = true, sixMovedNothing = true, eightStayedAtAlign = true, eightNeedsAlign = false;
        std::string eightNeedsAlignWhy;
        for (int k = 0; k < 2; k++)
        {
            jogRan = r.func->MoveToCell(AxisId::Y2, jogSpots[k][1], jogSpots[k][0], ct) && jogRan;
            double bx = x->CurrentPos(), by = y2->CurrentPos();
            int vmBefore6 = fv.VmCallCount();
            auto h6 = calib.HandEyeLocateHere(1, ct);
            sixMovedNothing = sixMovedNothing && h6.Ok && Near(x->CurrentPos(), bx) && Near(y2->CurrentPos(), by) && fv.VmCallCount() - vmBefore6 == 1;
            if (k == 0)
            {
                auto h8Here = calib.HandEyeCheckTarget(1, ct);
                eightNeedsAlign = !h8Here.Ok && h8Here.Reason == Loc::F("Calib.XNotAtAlignPos7", x->CurrentPos(), 1, model->AlignPosX(1)) &&
                                  Near(x->CurrentPos(), bx);
                eightNeedsAlignWhy = h8Here.Reason;
            }
            auto h7 = calib.HandEyeGoToAlignForTest(1, ct);
            auto h8 = calib.HandEyeCheckTarget(1, ct);
            if (!h7.Ok || !h8.Ok) { CHECK_MSG(false, h7.Reason + " | " + h8.Reason); break; }
            eightStayedAtAlign = eightStayedAtAlign && Near(x->CurrentPos(), model->AlignPosX(1)) && !Near(x->CurrentPos(), h8.TargetX);
            cmdX.push_back(h8.TargetX);
            cmdY.push_back(h8.TargetY2);
        }
        CHECK(jogRan && sixMovedNothing);
        CHECK_MSG(eightNeedsAlign, eightNeedsAlignWhy);
        CHECK(eightStayedAtAlign);
        CHECK_MSG(cmdX.size() == 2 && Near(cmdX[0], jigPick.X) && Near(cmdX[1], jigPick.X) && Near(cmdY[0], jigPick.Y) && Near(cmdY[1], jigPick.Y),
                  cmdX.size() == 2 ? Num(cmdX[0]) + " " + Num(cmdX[1]) + " / " + Num(jigPick.X) + "; " + Num(cmdY[0]) + " " + Num(cmdY[1]) + " / " + Num(jigPick.Y)
                                   : std::string("ran ") + std::to_string(cmdX.size()));

        // Nút 6 chọn ô tray lắp ráp: CAM tới vị trí soi ô — cùng CamPosFor với S05 — rồi kích MỘT lần.
        TeachPoint cellCam = model->AssyTray->CamPosFor(1, 2);
        auto rowAxis = r.Ax(model->AssyTray->AxisRow.c_str());
        int vmBeforeCell = fv.VmCallCount();
        auto h6Cell = calib.HandEyeLocateHere(1, 1, 2, ct);
        CHECK_MSG(h6Cell.Ok && Near(x->CurrentPos(), cellCam.X) && Near(rowAxis->CurrentPos(), cellCam.Y) && fv.VmCallCount() - vmBeforeCell == 1,
                  h6Cell.Reason);
        auto h6Out = calib.HandEyeLocateHere(1, model->AssyTray->Rows(), 0, ct);
        CHECK_MSG(!h6Out.Ok && h6Out.Reason == Loc::F("Calib.CellOutsideAssyGrid", model->AssyTray->Rows(), model->AssyTray->Cols()), h6Out.Reason);
        calib.HandEyeGoToAlignForTest(1, ct);

        // Bơm 0.2 mm vào pose top-down: điểm đích lệch ĐÚNG 0.2.
        fv.SetMarkX(fv.MarkX() + 0.2);
        auto h6Shift = calib.HandEyeLocateHere(1, ct);
        auto h7Shift = calib.HandEyeCheckTarget(1, ct);
        CHECK_MSG(h6Shift.Ok && h7Shift.Ok && std::fabs((h7Shift.TargetX - jigPick.X) - 0.2) < 0.01,
                  h7Shift.Reason + " ofs " + Num(h7Shift.TargetX - jigPick.X));
        double teachX = 0, teachY = 0, teachA = 0;
        CHECK_MSG(cal.TryGetHandEyeTarget(1, teachX, teachY, teachA) && Near(teachX, fv.MarkX() - 0.2) &&
                  DeltaNear(h6Shift.Text, DeltaPrefix("Calib.VsTeachTarget", 1, 0.0, 0.0, 0.0), 0.2, 0), h6Shift.Text);
        // Khung top-down trang Main: Δ TRONG ẢNH so với ảnh master — trừ quãng trục đã chạy từ chỗ chụp master.
        double mAxX = 0, mAxY = 0;
        bool hasMasterAxis = cal.TryGetHandEyeMasterAxis(1, mAxX, mAxY);
        double expDx = 0.2 - (x->CurrentPos() - mAxX), expDy = -(rowAxis->CurrentPos() - mAxY);
        CHECK_MSG(hasMasterAxis && Near(mAxX, model->HandEyeCamPos.X) && Near(mAxY, model->HandEyeCamPos.Y) && std::fabs(expDx - 0.2) > 1 &&
                  DeltaNear(VisionReadout::Top(), DeltaPrefix("Readout.VsMaster", 1, 0.0, 0.0, 0.0), expDx, expDy),
                  "expect " + Num(expDx) + "/" + Num(expDy) + " — " + VisionReadout::Top());
        fv.SetMarkX(fv.MarkX() - 0.2);

        // Con can xê dịch +0.3 mm trên đầu hút (camera dưới đo ObjY −0.3) → điểm đích lùi 0.3 cả X lẫn Y2.
        auto poseKeep = fv.ForcedObjPose();
        fv.SetForcedObjPose(std::array<double, 3>{ (*poseKeep)[0] + 0.3, (*poseKeep)[1] - 0.3, (*poseKeep)[2] });
        auto h6Slip = calib.HandEyeLocateHere(1, ct);
        auto h8Slip = calib.HandEyeCheckTarget(1, ct);
        fv.SetForcedObjPose(poseKeep);
        CHECK_MSG(h6Slip.Ok && h8Slip.Ok && std::fabs((h8Slip.TargetX - jigPick.X) + 0.3) < 0.01 && std::fabs((h8Slip.TargetY2 - jigPick.Y) + 0.3) < 0.01,
                  h8Slip.Reason + " X " + Num(h8Slip.TargetX - jigPick.X) + " Y2 " + Num(h8Slip.TargetY2 - jigPick.Y));

        // Hàng rào lưới teach TẮT ở phép thử.
        double markXKeep = fv.MarkX(), markYKeep = fv.MarkY();
        fv.SetMarkX(jigCam.X + 25.0);
        fv.SetMarkY(jigCam.Y + 18.0);
        auto h6Far = calib.HandEyeLocateHere(1, ct);
        auto h7Far = calib.HandEyeCheckTarget(1, ct);
        CHECK_MSG(h6Far.Ok && h7Far.Ok, h6Far.Reason + " | " + h7Far.Reason);
        fv.SetMarkX(markXKeep);
        fv.SetMarkY(markYKeep);

        // Gắp lệch quá ngưỡng lệch tâm: không ghi.
        fv.SetForcedObjPose(std::array<double, 3>{ 9.0, 0.0, 0.0 });
        double dxBefore = cal.HandEye1Dx;
        auto heFar = calib.HandEyeCommit(1, ct);
        CHECK_MSG(!heFar.Ok && heFar.Status == VmStatus::NoConclusion && Near(cal.HandEye1Dx, dxBefore), heFar.Reason);
        fv.SetForcedObjPose(std::array<double, 3>{ 0.8, -0.6, 7.0 });

        // ── Nút 9 ──
        auto h6For9 = calib.HandEyeLocateHere(1, ct);
        auto h9Stale = calib.HandEyeGoToTarget(1, ct);
        CHECK_MSG(h6For9.Ok && !h9Stale.Ok && h9Stale.Reason == Loc::T("Calib.NeedTarget"), h9Stale.Reason);

        auto h8For9 = calib.HandEyeCheckTarget(1, ct);
        auto assy = model->AssyTray;
        double slowKeep = assy->Z_SlowZ1, xBeforeNoSlow = x->CurrentPos(), zBeforeNoSlow = z1->CurrentPos();
        assy->Z_SlowZ1 = 0;
        auto h9NoSlow = calib.HandEyeGoToTarget(1, ct);
        assy->Z_SlowZ1 = slowKeep;
        CHECK_MSG(h8For9.Ok && !h9NoSlow.Ok && h9NoSlow.Reason == Loc::F("Calib.NoZSlowTaught", AxisId::Z1) &&
                  Near(x->CurrentPos(), xBeforeNoSlow) && Near(z1->CurrentPos(), zBeforeNoSlow), h9NoSlow.Reason + h8For9.Reason);

        fv.SetMarkX(fv.MarkX() + 50.0);
        auto h6Other = calib.HandEyeLocateHere(1, ct);
        auto h8Other = calib.HandEyeCheckTarget(1, ct);
        auto y2Fake = std::dynamic_pointer_cast<FakeAxis>(y2);
        int gentle9 = y2Fake->GentleMoves();
        auto h9 = calib.HandEyeGoToTarget(1, ct);
        fv.SetMarkX(fv.MarkX() - 50.0);
        double yLow9 = sys->GetAxis(AxisId::Y2)->MoveSpeedLow();
        CHECK_MSG(y2Fake->GentleMoves() - gentle9 == 1 && Near(y2Fake->LastGentleSpeed(), yLow9),
                  std::to_string(y2Fake->GentleMoves() - gentle9) + " gentle, " + Num(y2Fake->LastGentleSpeed()) + "/" + Num(yLow9));
        double zSlow1 = assy->ZSlowFor(AxisId::Z1);
        CHECK_MSG(h6Other.Ok && h8Other.Ok && h9.Ok && std::fabs((h8Other.TargetX - jigPick.X) - 50.0) < 0.01 &&
                  Near(x->CurrentPos(), h8Other.TargetX) && Near(y2->CurrentPos(), h8Other.TargetY2) &&
                  AxisAngle::Distance(a1->CurrentPos(), AxisAngle::Wrap(h8Other.TargetA)) < 0.05 &&
                  Near(z1->CurrentPos(), zSlow1) && zSlow1 < assy->ZPickFor(AxisId::Z1) && r.func->HasProduct(1) &&
                  Near(h9.TargetX, h8Other.TargetX) && Near(h9.TargetY2, h8Other.TargetY2),
                  h9.Reason + h8Other.Reason + " Z1 " + Num(z1->CurrentPos()) + "/" + Num(zSlow1));

        // ── Đường trả hàng: HẠ Z rồi mới nhả; Jog A1 đi 6° trước → nút trả xoay về góc hút rồi mới nhả ──
        bool heJogARet = r.MoveAxis(AxisId::A1, AxisAngle::Wrap(heAPick - 6.0)) && AxisAngle::Distance(a1->CurrentPos(), heAPick) > 5.0;
        std::atomic<double> zAtRelease{ std::nan("") }, aAtRelease{ std::nan("") };
        io->AfterSetDo = FakeIo::SetDoHook::Fn([&](int bit, bool on)
        {
            if (bit == IoMap::DoVacuum1 && !on) { zAtRelease = z1->CurrentPos(); aAtRelease = a1->CurrentPos(); }
        });
        auto heRet = calib.HandEyeReturn(1, ct);
        io->AfterSetDo = nullptr;
        CHECK_MSG(heRet.Ok && Near(zAtRelease, model->AssyTray->ZPickFor(AxisId::Z1)), heRet.Reason + " Z@release " + Num(zAtRelease));
        CHECK_MSG(heJogARet && heRet.Ok && AxisAngle::Distance(aAtRelease, heAPick) < 0.05, "A@release " + Num(aAtRelease));
        CHECK(!r.func->HasProduct(1) && !McDataManager::RunData()->Nozzle1->HasProduct && Near(z1->CurrentPos(), r.motion.ZSafe()) &&
              Near(x->CurrentPos(), jigPick.X) && Near(y2->CurrentPos(), jigPick.Y));

        // ── Hand-eye đã ghi thì nút 6 → 9 dùng được ngay (mô phỏng mở lại App, hút bằng trang Manual) ──
        calib.HandEyeClear();
        r.func->SetVacuum(1, true);
        auto r6 = calib.HandEyeLocateHere(1, ct);
        auto r7 = calib.HandEyeGoToAlignForTest(1, ct);
        auto r8 = calib.HandEyeCheckTarget(1, ct);
        auto r9 = calib.HandEyeGoToTarget(1, ct);
        CHECK_MSG(r6.Ok && r7.Ok && r8.Ok && r9.Ok && Near(x->CurrentPos(), r8.TargetX) && Near(y2->CurrentPos(), r8.TargetY2),
                  r6.Reason + r7.Reason + r8.Reason + r9.Reason);
        std::string vsPickHead = Loc::T("Calib.VsPick");
        vsPickHead = vsPickHead.substr(0, vsPickHead.find('{'));
        CHECK_MSG(r8.Ok && !r8.Text.empty() && !Contains(r8.Text, vsPickHead), r8.Text);
        r.func->EnsureZSafe(ct);

        fv.SetForcedObjPose(std::nullopt);
        fv.SetMarkA(0);
        calib.HandEyeClear();
        r.func->SetVacuum(1, false);
        McDataManager::RunData()->Nozzle1->HasProduct = false;
        McDataManager::RunData()->Nozzle1->SourceCellIndex = -1;

        // ── Cổng hiệu chuẩn ──
        model = VmSetup(r);
        auto gate = calib.CheckCalib();
        CHECK_MSG(gate.Ready() && calib.LastCalibGate() && calib.LastCalibGate()->Ready(), gate.Describe());
        VisionCalib& mcal = model->Calib;
        auto savedRef2 = mcal.HandEye2AngleRef;
        mcal.HandEye2AngleRef.reset();
        auto gateOld = calib.CheckCalib();
        mcal.HandEye2AngleRef = savedRef2;
        CHECK_MSG(!gateOld.Ready() && Contains(gateOld.Block, Loc::F("VmCycle.GateHandEyeOld", 2)), gateOld.Describe());
        mcal.HandEye2YFixed = false;
        auto gateOldY = calib.CheckCalib();
        mcal.HandEye2YFixed = true;
        CHECK_MSG(!gateOldY.Ready() && Contains(gateOldY.Block, Loc::F("VmCycle.GateHandEyeOld", 2)), gateOldY.Describe());
        int savedSign2 = mcal.AlignAngleSign2;
        mcal.SetAlignVerified(2, 0, 0, 0, TimePoint{});
        auto gate3 = calib.CheckCalib();
        mcal.SetAlignVerified(2, savedSign2, model->AlignPosX(2), 0, std::chrono::system_clock::now());
        CHECK_MSG(!gate3.Ready() && Contains(gate3.Block, Loc::F("VmCycle.GateNoSign", 2)), gate3.Describe());
    }

    // ───────────────────────── Bước 5b — chạy khô (không có trong SelfTest.cs; thêm cho bản C++) ─────────────────────────

    void TestDryRun(Rig& r)
    {
        AlarmManager::ForceClearAll();
        auto model = VmSetup(r);
        auto& calib = *r.calib;
        auto& fv = *r.fv;
        std::stop_token ct;
        auto x = r.Ax(AxisId::X), y2 = r.Ax(AxisId::Y2), a1 = r.Ax(AxisId::A1);
        r.func->EnsureZSafe(ct);
        // A1 về 0: quang học giả cho góc sản phẩm theo A, mốc góc mô phỏng 0 → góc bù 0, không vướng ngưỡng góc.
        r.MoveAxis(AxisId::A1, 0);

        // Cổng chặn (thiếu Bước 2): từ chối, không chạy trục nào.
        model->Calib.TopDownVerifiedAt = TimePoint{};
        double xb = x->CurrentPos(), yb = y2->CurrentPos();
        auto blocked = calib.DryRunPlaceAlign(1, ct);
        CHECK_MSG(!blocked.Ok && Contains(blocked.Reason, Loc::T("VmCycle.GateTopDown")) && Near(x->CurrentPos(), xb) && Near(y2->CurrentPos(), yb),
                  blocked.Reason);
        r.fv->StampCalibrated(&model->Calib);

        // Dấu chuẩn quanh ô (1,1) của tray lắp ráp — lệnh rơi trong hành trình; tay tới đúng lệnh ở Z an toàn.
        fv.SetSimulateOptics(true);
        TeachPoint cell = model->AssyTray->CellPos(AxisId::Z1, 0, 0);
        fv.SetMarkX(cell.X - model->Calib.HandEyeDx(1));
        fv.SetMarkY(cell.Y - model->Calib.HandEyeDy(1));
        CHECK(r.func->MoveToCell(AxisId::Y2, fv.MarkY(), fv.MarkX(), ct));
        auto dry = calib.DryRunPlaceAlign(1, ct);
        CHECK_MSG(dry.Ok && Near(dry.CamX, fv.MarkX()) && Near(dry.CamY, fv.MarkY()), dry.Reason);
        CHECK_MSG(dry.Ok && Near(x->CurrentPos(), dry.CmdX) && Near(y2->CurrentPos(), dry.CmdY2) &&
                  AxisAngle::Distance(a1->CurrentPos(), AxisAngle::Wrap(dry.CmdA)) < 0.05 && r.motion.ZSafeOk(),
                  "X " + Num(x->CurrentPos()) + "/" + Num(dry.CmdX) + " Y2 " + Num(y2->CurrentPos()) + "/" + Num(dry.CmdY2));
        CHECK(calib.LastCalibGate().has_value());
        fv.SetSimulateOptics(false);
    }

    // ───────────────────────── SelfTest.TestVmStepCheck ─────────────────────────

    void TestStepCheck(Rig& r)
    {
        AlarmManager::ForceClearAll();
        auto model = VmSetup(r);
        auto& calib = *r.calib;
        auto& fv = *r.fv;
        auto sys = r.sys;
        auto run = McDataManager::RunData();
        auto assy = model->AssyTray;
        std::stop_token ct;
        auto x = r.Ax(AxisId::X);
        std::string yName = assy->AxisRow;
        auto yAxis = r.Ax(yName.c_str());

        // VmPrepareRun (phần không cần McControl): tray đầu vào đầy, tray lắp ráp trống, tay hút trống.
        run->InputTray->ResetAll(ECellState::Loaded);
        run->AssyTray->ResetAll(ECellState::Empty);
        r.func->SetVacuum(1, false);
        r.func->SetVacuum(2, false);
        run->Nozzle1->HasProduct = false;
        run->Nozzle2->HasProduct = false;
        run->Nozzle1->SourceCellIndex = -1;
        run->Nozzle2->SourceCellIndex = -1;
        r.func->EnsureZSafe(ct);
        calib.StepClear();

        // ── Bấm sai thứ tự ──
        double x0 = x->CurrentPos(), y0 = yAxis->CurrentPos();
        auto early5 = calib.StepAlignBottom(1, ct);
        CHECK_MSG(!early5.Ok && early5.Reason == Loc::T("Calib.NeedTopPose") && Near(x->CurrentPos(), x0) && Near(yAxis->CurrentPos(), y0), early5.Reason);
        auto early6 = calib.StepMoveToCommand(1, ct);
        CHECK_MSG(!early6.Ok && early6.Reason == Loc::F("Calib.NeedCommand", 1), early6.Reason);
        auto outside = calib.StepMoveCamToCell(assy->Rows(), 0, ct);
        CHECK_MSG(!outside.Ok && outside.Reason == Loc::F("Calib.CellOutsideAssyGrid", assy->Rows(), assy->Cols()), outside.Reason);

        // ── Bước 1 ──
        auto s1 = calib.StepMoveCamToCell(0, 0, ct);
        CHECK_MSG(s1.Ok && Near(x->CurrentPos(), assy->CamTopDownAnchorX) && Near(yAxis->CurrentPos(), assy->CamTopDownAnchorY), s1.Reason);

        // ── Bước 2 ──
        int locate0 = fv.LocateTopCount();
        auto s2 = calib.StepLocateTop(0, 0, ct);
        CHECK_MSG(s2.Ok && fv.LocateTopCount() == locate0 + 1 && calib.StepHasTop(), s2.Reason);

        // ── Bước 3: A lệch 40° → nút hút tự đưa A về GÓC HÚT trước khi hạ Z ──
        auto a1ax = r.Ax(AxisId::A1);
        r.MoveAxis(AxisId::A1, 40);
        auto s3 = calib.StepPick(1, 0, 0, ct);
        CHECK_MSG(AxisAngle::Distance(a1ax->CurrentPos(), model->PickAngle(1)) < 0.05, "A1 " + Num(a1ax->CurrentPos()));
        CHECK(std::fabs(model->PickAngle(1)) > 0.5);
        CHECK_MSG(s3.Ok && r.func->HasProduct(1) && run->Nozzle1->HasProduct && run->Nozzle1->SourceCellIndex >= 0, s3.Reason);
        CHECK(run->Nozzle1->SourceCellIndex < 0 || run->InputTray->Get(run->Nozzle1->SourceCellIndex) == ECellState::Processing);
        CHECK(r.motion.ZSafeOk());
        auto s3b = calib.StepPick(1, 0, 1, ct);
        CHECK_MSG(!s3b.Ok && s3b.Reason == Loc::F("Calib.NozzleBusyPick", 1), s3b.Reason);

        // ── Bước 4 ──
        auto s4 = calib.StepMoveToAlign(1, ct);
        CHECK_MSG(s4.Ok && Near(x->CurrentPos(), model->AlignPosX(1)), s4.Reason);

        // ── Bước 5 ──
        int comp0 = fv.PlaceComputeCount();
        auto s5 = calib.StepAlignBottom(1, ct);
        CHECK_MSG(s5.Ok && s5.HasCommand && calib.StepHasCommand() && fv.PlaceComputeCount() == comp0 + 1, s5.Reason);
        auto sent = fv.LastPlaceGlobals();
        CHECK(sent.has_value() && sent->at(VmVars::HasTarget) == 1 && Near(sent->at(VmVars::HandEyeDx), model->Calib.HandEyeDx(1)));
        CHECK_MSG(calib.StepGuardWarn().empty(), calib.StepGuardWarn());

        // ── Bước 6 ──
        auto yFake = std::dynamic_pointer_cast<FakeAxis>(yAxis);
        int gentle6 = yFake->GentleMoves();
        auto s6 = calib.StepMoveToCommand(1, ct);
        double yLow6 = sys->GetAxis(yName)->MoveSpeedLow();
        CHECK_MSG(yFake->GentleMoves() - gentle6 == 1 && Near(yFake->LastGentleSpeed(), yLow6), std::to_string(yFake->GentleMoves() - gentle6));
        TeachPoint cell = assy->CellPos(AxisId::Z1, 0, 0);
        double lim = sys->PlaceTargetGuardMm + sys->MaxCompensationDistance;
        CHECK_MSG(s6.Ok && std::fabs(x->CurrentPos() - cell.X) <= lim && std::fabs(yAxis->CurrentPos() - cell.Y) <= lim, s6.Reason);
        CHECK(r.motion.ZSafeOk() && r.func->HasProduct(1));

        auto s6again = calib.StepMoveToCommand(1, ct);
        CHECK_MSG(s6again.Ok, s6again.Reason);

        // Jog trục A sau lúc chụp: lệnh góc hết đúng.
        r.func->MoveInc(AxisId::A1, 5.0, sys->GetAxis(AxisId::A1)->MoveSpeed, ct);
        auto s6moved = calib.StepMoveToCommand(1, ct);
        std::string rotHead = Loc::F("Calib.ARotatedSinceCapture", AxisId::A1, 0.0, 0.0);
        rotHead = rotHead.substr(0, rotHead.find('('));
        CHECK_MSG(!s6moved.Ok && Contains(s6moved.Reason, rotHead), s6moved.Reason);

        // Bấm lại bước 1: bỏ hết số cũ.
        calib.StepMoveCamToCell(0, 1, ct);
        CHECK(!calib.StepHasTop() && !calib.StepHasCommand());

        // Nút đặt Z an toàn của tay #2.
        auto z2safe = calib.StepMoveZSafe(2, ct);
        CHECK_MSG(z2safe.Ok && Near(r.P(AxisId::Z2), r.motion.ZSafe()), z2safe.Reason);

        // Trả máy về trạng thái sạch (McControl.SyncNozzleStateFromSensor: ghi sổ theo cảm biến).
        r.func->SetVacuum(1, false);
        WaitUntil([&] { return !r.func->HasProduct(1); }, 2000);
        run->Nozzle1->HasProduct = false;
        run->Nozzle1->SourceCellIndex = -1;
        run->InputTray->ResetAll(ECellState::Loaded);
    }

    // ───────────────────────── huỷ / lỗi: không ném, trả lý do ─────────────────────────

    void TestCancel(Rig& r)
    {
        VmSetup(r);
        std::stop_source cancelled;
        cancelled.request_stop();
        auto td = r.calib->VerifyTopDown(5.0, cancelled.get_token());
        CHECK_MSG(!td.Ok && td.Reason == Loc::T("Calib.Cancelled"), td.Reason);
        auto st = r.calib->StepMoveZSafe(1, cancelled.get_token());
        CHECK_MSG(!st.Ok && !st.Reason.empty(), st.Reason);
        // Hàm bool công khai: huỷ → false, không ném ra trang gọi.
        CHECK(!r.calib->TriggerTopDownCalibFlow(cancelled.get_token()));
        CHECK(!r.calib->MoveAlignAxisToZero(1, cancelled.get_token()));
    }
}

int RunRoutineCalibTests()
{
    testkit::Group group("RoutineCalib");

    fs::path root = fs::temp_directory_path() / ("sca_calib_" + base::NowString("yyyyMMdd_HHmmssfff"));
    AppPaths::Init(base::PathToU8(root));
    LogWorker::Instance().Init(AppPaths::Root());
    fs::path realAlarms = fs::path(__FILE__).parent_path().parent_path().parent_path() / "Config" / "alarms.json";
    if (!fs::exists(realAlarms)) realAlarms = fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "Config" / "alarms.json";
    fs::path exeDir = root / "exe";
    fs::create_directories(exeDir / "Config");
    std::string tplText;
    if (base::ReadAllText(realAlarms, tplText)) base::WriteAllText(exeDir / "Config" / "alarms.json", tplText);
    AppPaths::SetExeDirForTests(exeDir);
    AlarmCatalog::Load();
    McDataManager::LoadSystemData();
    // Thư mục tạm mới: system.json mặc định là mô phỏng. Không bài nào được chạy khi khác đi.
    CHECK(McDataManager::SysData()->SimulationMode);
    if (!McDataManager::SysData()->SimulationMode) return group.Result();

    AlarmManager::ForceClearAll();
    auto prevModel = McDataManager::Model();
    double prevSpeed = FakeAxis::SpeedFactor();
    FakeAxis::SetSpeedFactor(1000);
    {
        Rig rig;
        if (BuildRig(rig))
        {
            TestRoutineCalib(rig);
            TestVmCalib(rig);
            TestDryRun(rig);
            TestStepCheck(rig);
            TestCancel(rig);
        }
        rig.scan.request_stop();
        if (rig.scan.joinable()) rig.scan.join();
        rig.motion.StopAll();
    }
    FakeAxis::SetSpeedFactor(prevSpeed);
    McDataManager::UseModel(prevModel);
    AlarmManager::ForceClearAll();
    AppPaths::SetExeDirForTests({});
    LogWorker::Instance().Flush();
    return group.Result();
}

// SelfTest.cs — phần KHÔNG port vì cần McControl:
//  - TestRoutineCalib/TestVmCalib: về gốc qua McControl.RequestOrigin (thay bằng về gốc tay trên MotionConnect),
//    McControl.InvalidateVisionCache / RequestReset / VmStopRoutine.
//  - TestVmStepCheck: VmPrepareRun (RequestSetMode Auto, InvalidateAssyScan, RequestReset) và
//    McControl.SyncNozzleStateFromSensor ở bước dọn — thay bằng ghi sổ tay hút trực tiếp.
