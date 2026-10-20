// Kiểm phần nền của lớp điều khiển — logic THUẦN, không cần McControl/thiết bị: VmCycle (dựng biến, đọc kết quả, hàm chặn
// lệnh đặt, hai phép App tự phán, cổng hiệu chuẩn, vết AlignBottom), SinglePointAlignMath, TwoHandGate, các hàm static
// của McFunction / Routine_Auto, và VisionReadout (chữ + bảng). Port các phép kiểm tương ứng của ShieldCanAssy.Wpf\SelfTest.cs
// (TestVmContract, TestVmFlowOutputCheck, TestSinglePointAlignMath, TestTwoHandGate, TestZDecelPoint, TestSettleBeforeCapture,
// TestTrayCellOrder…). Chữ hiển thị so qua Loc::T/Loc::F nên không phụ thuộc câu chữ cụ thể.
#include "TestKit.h"

#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "Communication/AxisAngle.h"
#include "Communication/VmContract.h"
#include "Controller/McFunction.h"
#include "Controller/Routine_Auto.h"
#include "Controller/SinglePointAlignMath.h"
#include "Controller/TwoHandGate.h"
#include "Controller/VisionReadout.h"
#include "Controller/VmCycle.h"
#include "Data/McDataManager.h"
#include "Data/Model/ModelData.h"
#include "Data/SystemData.h"
#include "Infor/AxisId.h"
#include "Lang/Loc.h"

using namespace sca;

namespace
{
    constexpr double Pi = 3.14159265358979323846;
    constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

    bool Near(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }
    bool Contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
    using Globals = std::map<std::string, double>;

    // ModelData::CreateSimulation có thể trả shared_ptr hoặc giá trị — đọc được cả hai.
    template <class T> T& Deref(T& v) { return v; }
    template <class T> T& Deref(std::shared_ptr<T>& p) { return *p; }
    template <class T> std::shared_ptr<T> ToShared(std::shared_ptr<T> p) { return p; }
    template <class T> std::shared_ptr<T> ToShared(T v) { return std::make_shared<T>(std::move(v)); }

    // Ngày trong VisionCalib: TimePoint (TimePoint{} = DateTime.MinValue) hoặc optional<TimePoint>.
    void ClearDate(TimePoint& t) { t = TimePoint{}; }
    void ClearDate(std::optional<TimePoint>& t) { t.reset(); }

    TimePoint Now() { return std::chrono::system_clock::now(); }

    // ───────────────────────── SinglePointAlignMath ─────────────────────────

    void TestSinglePointAlignMath()
    {
        double dx = 0, dy = 0, dTheta = 0;

        // Sản phẩm lệch 1 mm theo X so với tâm xoay, phải xoay 90°: xoay làm nó dịch sang (0, 1), nên lệnh phải lùi lại
        // đúng lượng đó.
        SinglePointAlignMath::Solve(0, 0, 90, 1, 0, 0, dx, dy, dTheta);
        CHECK(Near(dTheta, 90) && Near(dx, 0) && Near(dy, -1));

        // Sản phẩm nằm đúng tâm xoay: xoay bao nhiêu cũng không phải dịch.
        SinglePointAlignMath::Solve(10, 20, 45, 0, 0, 0, dx, dy, dTheta);
        CHECK(Near(dx, 10) && Near(dy, 20) && Near(dTheta, 45));

        // Vòng xoay khép kín (phép thử vòng xoay §6.4b): xoay +10° rồi quay lại → Δθ = −10°, lượng dịch = 0.
        const double alpha = 10.0;
        double rad = alpha * Pi / 180.0;
        double p0x = 1.5, p0y = -0.8;
        double p1x = p0x * std::cos(rad) - p0y * std::sin(rad);
        double p1y = p0x * std::sin(rad) + p0y * std::cos(rad);
        SinglePointAlignMath::Solve(p0x, p0y, 0, p1x, p1y, alpha, dx, dy, dTheta);
        CHECK(Near(dTheta, -alpha) && std::fabs(dx) < 1e-9 && std::fabs(dy) < 1e-9);

        // Handedness sai (ảnh gương) → sai số dư khác 0, bắt được lỗi.
        double mx = p0x * std::cos(-rad) - p0y * std::sin(-rad);
        double my = p0x * std::sin(-rad) + p0y * std::cos(-rad);
        SinglePointAlignMath::Solve(p0x, p0y, 0, mx, my, alpha, dx, dy, dTheta);
        CHECK(std::sqrt(dx * dx + dy * dy) > 0.1);

        // Góc chuẩn hoá về (−180, 180]: đích 170°, đối tượng −170° → xoay −20°.
        SinglePointAlignMath::Solve(0, 0, 170, 0, 0, -170, dx, dy, dTheta);
        CHECK(Near(dTheta, -20));
        CHECK(Near(SinglePointAlignMath::NormalizeDeg(180), 180) && Near(SinglePointAlignMath::NormalizeDeg(-180), 180) &&
              Near(SinglePointAlignMath::NormalizeDeg(350), -10));
    }

    // ───────────────────────── TwoHandGate ─────────────────────────

    void TestTwoHandGate()
    {
        TwoHandGate g1(500);
        CHECK_MSG(g1.Update(true, true), "2 nút lên cùng lúc thì trigger ngay");

        TwoHandGate g2(500);
        g2.Update(true, false);
        CHECK_MSG(g2.Update(true, true), "bấm lệch trong cửa sổ vẫn trigger");

        TwoHandGate g3(500);
        g3.Update(true, false);
        bool r3a = g3.Update(false, false);
        bool r3b = g3.Update(true, true);
        CHECK_MSG(!r3a && r3b, "bấm hụt 1 nút rồi nhả thì không trigger, thử lại vẫn được");

        TwoHandGate g4(20);   // cửa sổ ngắn để test không phải chờ lâu
        g4.Update(true, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));   // vượt hẳn cửa sổ 20 ms
        bool r4a = g4.Update(true, true);
        bool r4b = g4.Update(true, true);
        CHECK_MSG(!r4a && !r4b, "lệch quá cửa sổ thì không trigger, giữ nguyên cũng không tự trigger");
        g4.Update(false, false);
        CHECK_MSG(g4.Update(true, true), "nhả hết rồi bấm lại đồng thời thì trigger lại được");

        // Chống dán băng dính: giữ chặt 1 nút, tap nút kia — chỉ trigger đúng 1 lần đầu.
        TwoHandGate g5(500);
        bool r5a = g5.Update(true, true);
        bool r5b = g5.Update(true, false);
        bool r5c = g5.Update(true, true);
        bool r5d = g5.Update(true, false);
        bool r5e = g5.Update(true, true);
        CHECK(r5a && !r5b && !r5c && !r5d && !r5e);
    }

    // ───────────────────────── McFunction / Routine_Auto static ─────────────────────────

    void TestStaticRules()
    {
        // Điểm giảm tốc khi hạ.
        CHECK_MSG(!McFunction::UseTwoStageZ(49, 0, 90), "chưa teach điểm giảm tốc thì hạ một mạch");
        CHECK_MSG(McFunction::UseTwoStageZ(49, 85, 90), "điểm giảm tốc giữa chỗ đứng và đích thì hai chặng");
        CHECK_MSG(!McFunction::UseTwoStageZ(49, 95, 90), "teach nhầm dưới cao độ hút thì bỏ qua");
        CHECK_MSG(!McFunction::UseTwoStageZ(86, 85, 90), "Z đã dưới điểm giảm tốc thì hạ nốt một mạch");
        // Khi nâng — gương của chiều xuống.
        CHECK(!McFunction::UseTwoStageZUp(90, 0, 49));
        CHECK(McFunction::UseTwoStageZUp(90, 85, 49));
        CHECK(!McFunction::UseTwoStageZUp(90, 95, 49));
        CHECK(!McFunction::UseTwoStageZUp(84, 85, 49));
        CHECK(McFunction::UseTwoStageZUp(90, 85, 49) == McFunction::UseTwoStageZ(49, 85, 90) &&
              McFunction::UseTwoStageZUp(84, 85, 49) == McFunction::UseTwoStageZ(49, 85, 84));

        // Chờ ổn định trước khi chụp.
        CHECK(McFunction::SettleRemainingMs(McFunction::NeverMoved, 10000, 200) == 0);
        CHECK(McFunction::SettleRemainingMs(1000, 1000, 0) == 0);
        CHECK(McFunction::SettleRemainingMs(1000, 1000, 200) == 200);
        CHECK(McFunction::SettleRemainingMs(1000, 1150, 200) == 50);
        CHECK(McFunction::SettleRemainingMs(1000, 1500, 200) == 0);
        CHECK(McFunction::SettleRemainingMs(1000, 9999999, 200) == 0);

        // Cảm biến "có tray" chỉ có nghĩa ở vị trí chờ.
        CHECK(!Routine_Auto::TrayMissingAt(20.0, 20.0, true));
        CHECK(Routine_Auto::TrayMissingAt(20.0, 20.0, false));
        CHECK(Routine_Auto::TrayMissingAt(20.5, 20.0, false));
        CHECK(!Routine_Auto::TrayMissingAt(60.0, 20.0, false));
        CHECK(!Routine_Auto::TrayMissingAt(60.0, 20.0, true));

        // Giao ô theo hình học.
        CHECK(Routine_Auto::Nozzle1TakesFirstCell(483.585, 508.572));
        CHECK(!Routine_Auto::Nozzle1TakesFirstCell(508.572, 483.585));
        CHECK(Routine_Auto::Nozzle1TakesFirstCell(483.585, 483.585));
        CHECK(Routine_Auto::Nozzle1TakesFirstPickCell(90.715, 27.475, 114.724, 51.458));
        CHECK(!Routine_Auto::Nozzle1TakesFirstPickCell(186.751, 123.407, 90.587, 27.457));
        CHECK(Routine_Auto::Nozzle1TakesFirstPickCell(90.715, 27.475, 90.715, 27.475));
        // Cặp cùng cột (lệch X dưới nửa bước cột) giữ thứ tự ghé; khác cột vẫn theo X (bước cột 22 mm → nửa 11 mm).
        CHECK(Routine_Auto::Nozzle1TakesFirstCell(400.03, 400.0, 11.0) &&
              !Routine_Auto::Nozzle1TakesFirstCell(400.03, 400.0) &&
              !Routine_Auto::Nozzle1TakesFirstCell(422.0, 400.0, 11.0));
        CHECK(Routine_Auto::Nozzle1TakesFirstPickCell(209.64, 147.92, 210.0, 148.0, 7.5));
    }

    // ───────────────────────── VmCycle ─────────────────────────

    void TestVmCycleGlobals(const VisionOutputNames& names)
    {
        VisionCalib calib;
        calib.SetHandEye(1, -40, 5, 0, Now());
        calib.SetAlignVerified(1, 1, 300, 0, Now());

        Globals g = { { names.GvAxisX, 300 }, { names.GvAxisY2, 80 }, { names.GvAxisA1, 100 }, { names.GvAxisA2, 0 }, { VmVars::RunId, 4242 } };
        PlacedTopRecord top{ 390, 60, 1.5, 0 };
        VmCycle::AddPlaceGlobals(g, &top, &calib, 1);
        auto req = VmCycle::AlignBottomRequest(names, 1, g, true);
        bool hasCmdX = false;
        for (const auto& o : req.OptionalOutputs) if (o == std::string(VmOut::CmdX)) hasCmdX = true;
        CHECK_MSG(g.size() == 13 && g[VmVars::IsTriggerLocate] == 1 && g[VmVars::HasTarget] == 1 && g[VmVars::CellX] == 390 &&
                  g[VmVars::HandEyeDx] == -40 && g[VmVars::AlignSign] == 1 && hasCmdX,
                  "pose ô đích và hằng số của tay đi qua biến toàn cục");
        CHECK(req.Globals.size() == g.size() && req.Camera == EVmCamera::BottomUp && req.Flow == names.FlowAlignBottom(1));

        Globals gNo = g;
        VmCycle::AddPlaceGlobals(gNo, nullptr, &calib, 1);
        CHECK_MSG(gNo[VmVars::HasTarget] == 0 && gNo[VmVars::CellX] == 0 && gNo[VmVars::CellA] == 0, "chỉ soi: HasTarget = 0, pose ô về 0");

        // Bù đặt cộng thẳng vào pose đích; Y gửi đi đổi dấu (hệ camera dưới).
        Globals gTrim;
        CellOffset trim;
        trim.PlaceDx = 0.05; trim.PlaceDy = -0.07; trim.PlaceDa = 1.0;
        PlacedTopRecord topTrim{ 390, 60, 359.5, 0 };
        VmCycle::AddPlaceGlobals(gTrim, &topTrim, &calib, 1, &trim);
        CHECK(Near(gTrim[VmVars::CellX], 390.05) && Near(gTrim[VmVars::CellY], -59.93));
        CHECK(Near(gTrim[VmVars::HandEyeDy], -5) && Near(gTrim[VmVars::HandEyeDx], -40));
        CHECK(Near(gTrim[VmVars::CellA], 1.5));

        Globals gNoTrim;
        PlacedTopRecord top12{ 390, 60, 12, 0 };
        VmCycle::AddPlaceGlobals(gNoTrim, &top12, &calib, 1, nullptr);
        CHECK(Near(gNoTrim[VmVars::CellX], 390) && Near(gNoTrim[VmVars::CellY], -60) && Near(gNoTrim[VmVars::CellA], 348));

        // Mốc góc: CellA = mốc góc của tay − góc LocateTop + bù góc của ô, quấn về [0, 360).
        VisionCalib calibRef;
        calibRef.SetHandEye(1, -40, 5, 5.0, Now());
        calibRef.SetHandEye(2, -40, 5, -3.0, Now());
        Globals gRef1, gRef2;
        CellOffset da1;
        da1.PlaceDa = 1.0;
        PlacedTopRecord t358{ 390, 60, 358.0, 0 }, t1{ 390, 60, 1.0, 0 };
        VmCycle::AddPlaceGlobals(gRef1, &t358, &calibRef, 1, &da1);
        VmCycle::AddPlaceGlobals(gRef2, &t1, &calibRef, 2);
        CHECK(Near(gRef1[VmVars::CellA], 8.0) && Near(gRef2[VmVars::CellA], 356.0));

        // Chi tiết đích xoay +10° thì góc đích gửi đi đổi −10° (hai camera ngược chiều).
        Globals gDir0, gDir10;
        PlacedTopRecord t37{ 390, 60, 37.0, 0 }, t47{ 390, 60, 47.0, 0 };
        VmCycle::AddPlaceGlobals(gDir0, &t37, &calibRef, 1);
        VmCycle::AddPlaceGlobals(gDir10, &t47, &calibRef, 1);
        CHECK(Near(AxisAngle::ShortestDelta(gDir0[VmVars::CellA], gDir10[VmVars::CellA]), -10.0));

        // Đặt lại đúng tư thế lúc đo mốc góc → góc bù 0.
        VisionCalib calibHe;
        const double topAtCommit = 37.07, objAtCommit = -179.67;
        calibHe.SetHandEye(1, -40, 5, AxisAngle::Wrap(objAtCommit + topAtCommit), Now());
        Globals gHe;
        PlacedTopRecord tHe{ 390, 60, topAtCommit, 0 };
        VmCycle::AddPlaceGlobals(gHe, &tHe, &calibHe, 1);
        CHECK(Near(AxisAngle::ShortestDelta(gHe[VmVars::CellA], objAtCommit), 0.0));

        Globals gRefNo;
        VmCycle::AddPlaceGlobals(gRefNo, nullptr, &calibRef, 1);
        CHECK(gRefNo[VmVars::CellA] == 0);

        Globals gTrimOff;
        CellOffset dx05;
        dx05.PlaceDx = 0.05;
        VmCycle::AddPlaceGlobals(gTrimOff, nullptr, &calib, 1, &dx05);
        CHECK(gTrimOff[VmVars::HasTarget] == 0 && gTrimOff[VmVars::CellX] == 0);

        // LocateRequest ghi cờ nhánh soi vào chính bộ biến, đòi Found + 3 echo, Script tuỳ chọn.
        Globals gl = VmCycle::AxisGlobals(nullptr, names, 9101);
        CHECK(gl.size() == 5 && gl[VmVars::RunId] == 9101 && gl[names.GvAxisX] == 0);
        auto lreq = VmCycle::LocateRequest(names, gl);
        CHECK(gl[VmVars::IsTriggerLocate] == 1 && lreq.Outputs.size() == 4 && lreq.OptionalOutputs.size() == 5 &&
              lreq.Camera == EVmCamera::TopDown && lreq.Flow == names.FlowLocateTop);

        // AxisVarProblem.
        CHECK(VmCycle::AxisVarProblem(&names).empty());
        VisionOutputNames blank = names;
        blank.GvAxisA2.clear();
        CHECK(VmCycle::AxisVarProblem(&blank) == Loc::T("VmCycle.AxisVarsEmpty"));
        CHECK(VmCycle::AxisVarProblem(nullptr) == Loc::T("VmCycle.NoVisionNames"));
    }

    void TestVmCycleTrace(const VisionOutputNames& names)
    {
        VisionCalib calib;
        calib.SetHandEye(1, -40, 5, 0, Now());
        calib.SetAlignVerified(1, 1, 300, 0, Now());

        Globals gT = { { names.GvAxisX, 291.018 }, { names.GvAxisY2, 58.629 }, { names.GvAxisA1, 3.0 }, { names.GvAxisA2, 0 }, { VmVars::RunId, 77 } };
        PlacedTopRecord top{ 420.0, 0.03, 37.2, 0 };
        VmCycle::AddPlaceGlobals(gT, &top, &calib, 1);
        auto reqT = VmCycle::AlignBottomRequest(names, 1, gT, true);
        double tdx = 0, tdy = 0, tdt = 0;
        SinglePointAlignMath::Solve(gT[VmVars::CellX] + gT[VmVars::HandEyeDx], gT[VmVars::CellY] + gT[VmVars::HandEyeDy], gT[VmVars::CellA],
                                    -0.1, 0.45, 40.0, tdx, tdy, tdt);
        auto traceOf = [&](const std::function<void(VmCallResult&)>& change)
        {
            VmCallResult r;
            r.Ok = true;
            r.Values[VmOut::EchoRunId] = 77; r.Values[VmOut::EchoX] = 291.018; r.Values[VmOut::EchoA] = 3.0;
            r.Values[names.OutFound] = 1; r.Values[VmOut::ObjFound] = 1; r.Values[VmOut::ObjRunId] = 77;
            r.Values[VmOut::ObjX] = -0.1; r.Values[VmOut::ObjY] = 0.45; r.Values[VmOut::ObjA] = 40.0;
            r.Values[VmOut::Status] = 1;
            r.Values[VmOut::CmdX] = tdx; r.Values[VmOut::CmdY2] = tdy;
            r.Values[VmOut::CmdA] = AxisAngle::Wrap(3.0 + tdt); r.Values[VmOut::DiagDA] = tdt;
            r.Values[VmOut::DiagEcc] = std::sqrt(0.01 + 0.2025);
            r.Values[VmOut::DiagTarX] = gT[VmVars::CellX] + gT[VmVars::HandEyeDx];
            r.Values[VmOut::DiagTarY] = gT[VmVars::CellY] + gT[VmVars::HandEyeDy];
            if (change) change(r);
            return VmCycle::TraceAlignBottom(r, reqT, names, 1, 0.0);
        };

        auto trOk = traceOf(nullptr);
        CHECK_MSG(trOk.Checked && trOk.Match && Contains(trOk.Line, AxisId::Z1) && Contains(trOk.Line, VmVars::CellA) &&
                  Contains(trOk.Line, VmVars::HandEyeDx) && Contains(trOk.Line, VmOut::DiagTarX) && Contains(trOk.Line, trOk.Check) &&
                  !trOk.Check.empty(), trOk.Line);
        auto trX = traceOf([](VmCallResult& r) { r.Values[VmOut::CmdX] += 0.1; });
        CHECK_MSG(trX.Checked && !trX.Match, trX.Check);
        auto trA = traceOf([tdt](VmCallResult& r) { r.Values[VmOut::DiagDA] = -tdt; r.Values[VmOut::CmdA] = AxisAngle::Wrap(3.0 - tdt); });
        CHECK_MSG(trA.Checked && !trA.Match, trA.Check);

        Globals gT0 = gT;
        VmCycle::AddPlaceGlobals(gT0, nullptr, &calib, 1);
        VmCallResult okEmpty;
        okEmpty.Ok = true;
        auto req0 = VmCycle::AlignBottomRequest(names, 1, gT0, false);
        auto tr0 = VmCycle::TraceAlignBottom(okEmpty, req0, names, 1, 0.0);
        CHECK_MSG(!tr0.Checked && Contains(tr0.Line, "HasTarget = 0"), tr0.Line);

        auto trFail = VmCycle::TraceAlignBottom(VmCallResult::Fail("boom"), reqT, names, 1, 0.0);
        CHECK(!trFail.Checked && Contains(trFail.Line, "boom"));
    }

    void TestVmCycleCheckPlace(const VisionOutputNames& names)
    {
        VisionCalib calib;
        calib.SetHandEye(1, -40, 5, 0, Now());
        calib.SetAlignVerified(1, 1, 300, 0, Now());
        Globals g = { { names.GvAxisX, 300 }, { names.GvAxisY2, 80 }, { names.GvAxisA1, 100 }, { names.GvAxisA2, 0 }, { VmVars::RunId, 4242 } };
        PlacedTopRecord top{ 390, 60, 1.5, 0 };
        VmCycle::AddPlaceGlobals(g, &top, &calib, 1);
        auto req = VmCycle::AlignBottomRequest(names, 1, g, true);

        auto guard = [&](const std::function<void(Globals&)>& change, bool grid)
        {
            Globals v = {
                { VmOut::Status, 1 },
                // Y của Flow nằm trong hệ camera dưới (ngược chiều máy) — App đọc về rồi đổi dấu.
                { VmOut::CmdX, 348.8 }, { VmOut::CmdY2, -65.6 }, { VmOut::CmdA, 100.7 }, { VmOut::DiagDA, 0.7 }, { VmOut::DiagEcc, 1.34 },
                { VmOut::DiagTarX, 350 }, { VmOut::DiagTarY, -65 },
                { VmOut::EchoRunId, 4242 }, { VmOut::ObjRunId, 4242 }
            };
            if (change) change(v);
            VmCallResult r;
            r.Ok = true;
            for (const auto& kv : v) r.Values[kv.first] = kv.second;
            return VmCycle::CheckPlace(r, req, 1, 100, 350, 65, 1.5, 15, 3,
                                       [](double x) { return x >= -1 && x <= 600; }, [](double y) { return y >= -1 && y <= 400; }, grid);
        };
        auto show = [](const VmPlaceCommand& c) { return std::string(ToString(c.Route)) + ": " + c.Why; };

        auto ok = guard(nullptr, true);
        CHECK_MSG(ok.Route == EVmRoute::Ok && ok.CmdX == 348.8 && Near(ok.CmdY2, 65.6) && Near(ok.DiagTarY, 65) && ok.CmdA == 100.7, show(ok));
        auto cY = guard([](Globals& v) { v[VmOut::DiagTarY] = -67; }, true);
        CHECK_MSG(cY.Route == EVmRoute::Skip, show(cY));
        auto c1 = guard([](Globals& v) { v[VmOut::EchoRunId] = 4243; }, true);
        CHECK_MSG(c1.Route == EVmRoute::Error, show(c1));
        auto c2 = guard([](Globals& v) { v[VmOut::ObjRunId] = 4241; }, true);
        CHECK_MSG(c2.Route == EVmRoute::Error, show(c2));
        auto c4 = guard([](Globals& v) { v.erase(VmOut::Status); }, true);
        CHECK_MSG(c4.Route == EVmRoute::Error, show(c4));
        auto c5 = guard([](Globals& v) { v[VmOut::DiagDA] = -15.5; }, true);
        auto c6 = guard([](Globals& v) { v[VmOut::DiagEcc] = 3.2; }, true);
        CHECK_MSG(c5.Route == EVmRoute::Ng && c6.Route == EVmRoute::Ng, show(c5) + " | " + show(c6));
        auto c10 = guard([](Globals& v) { v[VmOut::Status] = 6; }, true);
        auto c11 = guard([](Globals& v) { v[VmOut::Status] = 0; }, true);
        CHECK_MSG(c10.Route == EVmRoute::Error && c11.Route == EVmRoute::Error, show(c10) + " | " + show(c11));
        auto c12 = guard([](Globals& v) { v[VmOut::CmdA] = NaN; }, true);
        auto c13 = guard([](Globals& v) { v[VmOut::CmdA] = 361; }, true);
        CHECK_MSG(c12.Route == EVmRoute::Error && c13.Route == EVmRoute::Error, show(c12) + " | " + show(c13));
        auto c16 = guard([](Globals& v) { v[VmOut::DiagTarX] = 352; }, true);
        CHECK_MSG(c16.Route == EVmRoute::Skip, show(c16));
        auto c17 = guard([](Globals& v) { v[VmOut::CmdX] = 355; }, true);
        CHECK_MSG(c17.Route == EVmRoute::Skip, show(c17));
        auto c18 = guard([](Globals& v) { v[VmOut::CmdX] = 700; }, false);
        CHECK_MSG(c18.Route == EVmRoute::Error, show(c18));
        auto c19 = guard([](Globals& v) { v[VmOut::CmdA] = 120; }, true);
        CHECK_MSG(c19.Route == EVmRoute::Error, show(c19));
        auto failed = VmCycle::CheckPlace(VmCallResult::Fail("timeout", true), req, 1, 100, 350, 65, 1.5, 15, 3, nullptr, nullptr, true);
        CHECK_MSG(failed.Route == EVmRoute::Error && Contains(failed.Why, "timeout"), show(failed));
    }

    void TestVmCycleRead(const VisionOutputNames& names)
    {
        // ── LocateTop ──
        Globals gl = { { names.GvAxisX, 300 }, { names.GvAxisY2, 80 }, { VmVars::RunId, 55 } };
        auto locate = [&](const std::function<void(VmCallResult&)>& change)
        {
            VmCallResult r;
            r.Ok = true;
            r.Values[names.OutFound] = 1;
            r.Values[VmOut::EchoRunId] = 55; r.Values[VmOut::EchoX] = 300; r.Values[VmOut::EchoY2] = 80;
            r.Values[VmOut::TopFound] = 1; r.Values[VmOut::TopX] = 300.2; r.Values[VmOut::TopY] = 79.9; r.Values[VmOut::TopA] = 3;
            r.Values[VmOut::TopRunId] = 55;
            if (change) change(r);
            return VmCycle::ReadLocate(r, gl, names);
        };
        auto l0 = locate(nullptr);
        CHECK_MSG(l0.Found && l0.VmValid && l0.X == 300.2 && l0.A == 3 && l0.AxX == 300 && l0.AxY == 80, l0.VmIssue);
        auto l1 = locate([](VmCallResult& r) { r.Values[VmOut::EchoY2] = 82; });
        CHECK_MSG(!l1.EchoOk && !l1.VmValid, l1.EchoIssue);
        auto l3 = locate([&](VmCallResult& r) { r.Values[names.OutFound] = 0; r.Values.erase(VmOut::TopX); r.Values.erase(VmOut::TopRunId); });
        CHECK_MSG(!l3.Found && l3.VmValid, l3.VmIssue);
        auto l4 = locate([](VmCallResult& r) { r.Values[VmOut::TopRunId] = 54; });
        CHECK_MSG(!l4.VmValid, l4.VmIssue);
        auto l5 = locate([](VmCallResult& r) { r.Values.erase(VmOut::EchoX); });
        CHECK_MSG(!l5.EchoOk && Contains(l5.EchoIssue, Loc::F("VmCycle.MissingOutput", VmOut::EchoX)), l5.EchoIssue);
        auto lFail = VmCycle::ReadLocate(VmCallResult::Fail(""), gl, names);
        CHECK(!lFail.CallOk && lFail.CallError == Loc::T("VmCycle.NoResult"));

        // ── AlignBottom ──
        Globals gNo = { { names.GvAxisX, 300 }, { names.GvAxisY2, 80 }, { names.GvAxisA1, 100 }, { names.GvAxisA2, 0 }, { VmVars::RunId, 4242 } };
        VmCycle::AddPlaceGlobals(gNo, nullptr, nullptr, 1);
        auto reqNoTarget = VmCycle::AlignBottomRequest(names, 1, gNo, false);
        CHECK(reqNoTarget.OptionalOutputs.size() == 5);
        auto align = [&](const std::function<void(VmCallResult&)>& change)
        {
            VmCallResult r;
            r.Ok = true;
            r.Values[names.OutFound] = 1;
            r.Values[VmOut::EchoRunId] = 4242; r.Values[VmOut::EchoX] = 300; r.Values[VmOut::EchoA] = 100;
            r.Values[VmOut::ObjFound] = 1; r.Values[VmOut::ObjX] = 1.2; r.Values[VmOut::ObjY] = -0.6; r.Values[VmOut::ObjA] = 0.8;
            r.Values[VmOut::ObjRunId] = 4242;
            if (change) change(r);
            return VmCycle::ReadAlignBottom(r, reqNoTarget, names, 1);
        };
        auto b0 = align(nullptr);
        CHECK_MSG(b0.Found && b0.VmValid && b0.X == 1.2, b0.VmIssue);
        auto b1 = align([](VmCallResult& r) { r.Values[VmOut::EchoA] = 0; });
        CHECK_MSG(!b1.EchoOk && !b1.VmValid, b1.EchoIssue);
        auto b2 = align([](VmCallResult& r) { r.Values[VmOut::ObjRunId] = 4241; });
        CHECK_MSG(b2.EchoOk && !b2.VmValid, b2.VmIssue);
    }

    void TestVmCycleJudges()
    {
        // ── Bước 2 ──
        auto cap = [](double cx, double cy, double ax, double ay)
        {
            VmMeanCapture c;
            c.Ok = true; c.X = cx; c.Y = cy; c.AxX = ax; c.AxY = ay;
            return c;
        };
        auto j0 = VmCycle::JudgeTopDown(cap(300.3, 79.8, 300, 80), cap(300.31, 79.81, 305, 80), cap(300.29, 79.79, 300, 85), 0.05);
        CHECK(j0.Status == VmStatus::Ok);
        auto j1 = VmCycle::JudgeTopDown(cap(300.3, 79.8, 300, 80), cap(305.3, 79.8, 305, 80), cap(300.3, 79.8, 300, 85), 0.05);
        CHECK(j1.Status == VmStatus::Code8);
        auto j2 = VmCycle::JudgeTopDown(cap(300.3, 79.8, 300, 80), cap(300.38, 79.8, 305, 80), cap(300.3, 79.8, 300, 85), 0.05);
        CHECK(j2.Status == VmStatus::EccOut);
        auto j3 = VmCycle::JudgeTopDown(cap(300.3, 79.8, 300, 80), cap(300.3, 79.8, 305, 80.1), cap(300.3, 79.8, 300, 85), 0.05);
        CHECK(j3.Status == VmStatus::AxisMoved);
        auto j4 = VmCycle::JudgeTopDown(cap(300.3, 79.8, 300, 80), cap(300.3, 79.8, 300.2, 80), cap(300.3, 79.8, 300, 85), 0.05);
        CHECK(j4.Status == VmStatus::NoConclusion);
        auto j5 = VmCycle::JudgeTopDown(cap(NaN, 79.8, 300, 80), cap(300.3, 79.8, 305, 80), cap(300.3, 79.8, 300, 85), 0.05);
        CHECK(j5.Status == VmStatus::Invalid);

        // ── Bước 4 ──
        auto bot = [](double px, double py, double pa, double axA)
        {
            VmMeanCapture c;
            c.Ok = true; c.X = px; c.Y = py; c.A = pa; c.AxX = 300; c.AxA = axA;
            return c;
        };
        auto rotX = [](double r, double deg) { return r * std::cos(deg * Pi / 180.0); };
        auto rotY = [](double r, double deg) { return r * std::sin(deg * Pi / 180.0); };
        auto p0 = bot(2.0, 0, 0, 0);
        auto p1Plus = bot(rotX(2.0, 10), rotY(2.0, 10), 10, 10);
        auto p1Minus = bot(rotX(2.0, -10), rotY(2.0, -10), -10, 10);

        auto a0 = VmCycle::JudgeAlignSign(p0, p1Plus, 0.05, 1.5, 0.5, 1.0);
        CHECK(a0.Status == VmStatus::Ok && a0.Sign == 1 && std::fabs(a0.Residual) < 0.01);
        auto a1 = VmCycle::JudgeAlignSign(p0, p1Minus, 0.05, 1.5, 0.5, 1.0);
        CHECK(a1.Status == VmStatus::Ok && a1.Sign == -1);
        auto a2 = VmCycle::JudgeAlignSign(bot(0.1, 0, 0, 0), bot(rotX(0.1, 10), rotY(0.1, 10), 10, 10), 0.05, 1.5, 0.5, 1.0);
        CHECK(a2.Status == VmStatus::NoConclusion && a2.Sign != 0);
        auto a3 = VmCycle::JudgeAlignSign(p0, bot(2.0, 0, 3, 10), 0.05, 1.5, 0.5, 1.0);
        CHECK(a3.Status == VmStatus::AngleOut && a3.Sign == 0);
        auto a4 = VmCycle::JudgeAlignSign(p0, bot(rotX(2.0, 10) + 0.5, rotY(2.0, 10), 10, 10), 0.05, 1.5, 0.5, 1.0);
        CHECK(a4.Status == VmStatus::EccOut);
        auto a5 = VmCycle::JudgeAlignSign(p0, bot(rotX(2.0, 0.2), rotY(2.0, 0.2), 0.2, 0.2), 0.05, 1.5, 0.5, 1.0);
        CHECK(a5.Status == VmStatus::Code8);
        VmMeanCapture moved = bot(rotX(2.0, 10), rotY(2.0, 10), 10, 10);
        moved.AxX = 305;
        auto a6 = VmCycle::JudgeAlignSign(p0, moved, 0.05, 1.5, 0.5, 1.0);
        CHECK(a6.Status == VmStatus::AxisMoved);

        // ── tiện ích ──
        double s = std::sin(179 * Pi / 180) + std::sin(-179 * Pi / 180), c = std::cos(179 * Pi / 180) + std::cos(-179 * Pi / 180);
        CHECK_MSG(Near(std::fabs(VmCycle::MeanAngleDeg(s, c)), 180, 1e-9), "trung bình góc không gãy ở ±180°");
        double s2 = std::sin(10 * Pi / 180) + std::sin(20 * Pi / 180), c2 = std::cos(10 * Pi / 180) + std::cos(20 * Pi / 180);
        CHECK(Near(VmCycle::MeanAngleDeg(s2, c2), 15, 1e-9));
        CHECK(VmCycle::Fmt(1.23456) == "1.235" && VmCycle::Fmt(2.0) == "2" && VmCycle::Fmt(-0.5) == "-0.5");
        CHECK(VmCycle::Fmt(NaN) == Loc::T("VmCycle.NaNMissing") && VmCycle::Fmt(std::numeric_limits<double>::infinity()) == "Infinity");
        Globals d = { { "a", 1.5 } };
        CHECK(VmCycle::Val(d, "a") == 1.5 && std::isnan(VmCycle::Val(d, "b")));
        CHECK(std::string(ToString(EVmRoute::Skip)) == "Skip" && std::string(ToString(EVmRoute::Ng)) == "Ng");
    }

    void TestVmCycleGate()
    {
        SystemData sys;
        auto holder = ModelData::CreateSimulation();
        ModelData& model = Deref(holder);
        model.Calib.TopDownVerifiedAt = Now();

        auto g0 = VmCycle::EvaluateCalib(&model, &sys);
        CHECK_MSG(g0.Block.empty(), g0.Block);
        CHECK_MSG(g0.Ready() && Near(g0.HandEyeDx1, model.Calib.HandEyeDx(1)) && g0.AlignSign1 == 1 && !std::isnan(g0.DaysSinceOldest),
                  g0.Describe());

        auto alignAt1 = model.Calib.AlignVerifiedAt1;
        ClearDate(model.Calib.AlignVerifiedAt1);
        auto g1 = VmCycle::EvaluateCalib(&model, &sys);
        auto g1Tool2 = VmCycle::EvaluateCalib(&model, &sys, 2);
        auto g1Tool1 = VmCycle::EvaluateCalib(&model, &sys, 1);
        model.Calib.AlignVerifiedAt1 = alignAt1;
        CHECK_MSG(Contains(g1.Block, Loc::F("VmCycle.GateNoSign", 1)), g1.Block);
        CHECK_MSG(g1Tool2.Block.empty(), g1Tool2.Block);
        CHECK_MSG(Contains(g1Tool1.Block, Loc::F("VmCycle.GateNoSign", 1)), g1Tool1.Block);

        model.AlignPosX_Nozzle1 += 0.2;
        auto g3 = VmCycle::EvaluateCalib(&model, &sys);
        std::string moved = Loc::F("VmCycle.GateAlignPosMoved", 1, model.AlignPosX(1), model.Calib.AlignVerifiedPosX(1), sys.AlignXTolMm);
        model.AlignPosX_Nozzle1 -= 0.2;
        CHECK_MSG(!g3.Ready() && Contains(g3.Block, moved), g3.Block + " ‖ " + moved);

        auto topAt = model.Calib.TopDownVerifiedAt;
        ClearDate(model.Calib.TopDownVerifiedAt);
        auto g4 = VmCycle::EvaluateCalib(&model, &sys);
        model.Calib.TopDownVerifiedAt = topAt;
        CHECK_MSG(Contains(g4.Block, Loc::T("VmCycle.GateTopDown")), g4.Block);

        SystemData badSys;
        badSys.MaxCompensationAngleDeg = 60;
        auto g5 = VmCycle::EvaluateCalib(&model, &badSys);
        CHECK_MSG(Contains(g5.Block, Loc::T("VmCycle.GateThresholds")), g5.Block);

        auto gNull = VmCycle::EvaluateCalib(nullptr, &sys);
        CHECK(gNull.Block == Loc::T("VmCycle.GateNoModel") && !gNull.Ready() &&
              gNull.Describe() == Loc::F("VmCycle.GateNotReady", gNull.Block));
    }

    void TestVmCycleContract(const VisionOutputNames& names)
    {
        auto expects = VmCycle::ExpectedFlowOutputs(names);
        CHECK(expects.size() == 3);
        const VmFlowExpect* locate = nullptr;
        const VmFlowExpect* align1 = nullptr;
        for (const auto& e : expects)
        {
            if (e.Flow == names.FlowLocateTop) locate = &e;
            if (e.Flow == names.FlowAlignBottom(1)) align1 = &e;
        }
        CHECK_MSG(locate != nullptr && locate->Names.size() == 9, "LocateTop đòi 9 output (Found + 3 echo + 5 của TopAbs)");
        CHECK_MSG(align1 != nullptr && align1->Names.size() == 17, "AlignBottom_Tool1 đòi 17 output");
        bool allFound = true;
        for (const auto& e : expects)
        {
            bool has = false;
            for (const auto& n : e.Names) if (n == names.OutFound) has = true;
            allFound = allFound && has && e.ImageName == names.OutImage;
        }
        CHECK(allFound);

        for (const auto& e : expects)
        {
            auto fake = VmCycle::FakeFlowOutputs(e.Flow, names);
            CHECK(fake.Error.empty() && fake.Declared.size() == e.Names.size() && fake.InputParamCount == 0 && fake.Modules.size() == 1);
        }
        CHECK(!VmCycle::FakeFlowOutputs("NoSuchFlow", names).Error.empty());
    }

    // ───────────────────────── VisionReadout ─────────────────────────

    void TestVisionReadout()
    {
        auto prevModel = McDataManager::Model();
        auto simHolder = ModelData::CreateSimulation();
        auto sim = ToShared(std::move(simHolder));
        McDataManager::UseModel(sim);

        VmCameraRead read;
        read.CallOk = true; read.EchoOk = true; read.VmValid = true; read.Found = true;
        read.X = 0.1; read.Y = -0.2; read.A = 0.5;
        VmPlaceCommand cmd;
        cmd.Route = EVmRoute::Ok; cmd.Status = 1;
        cmd.CmdX = 351.2; cmd.CmdY2 = 60.5; cmd.CmdA = 101.0; cmd.DiagDA = 1.0; cmd.DiagEcc = 0.3;

        // Không nhắm ô: chữ có độ lệch và đích; bảng hai hàng đủ cột; không có bảng tách.
        VisionReadout::Tool(1, read, cmd);
        std::string t1 = VisionReadout::Tool1();
        auto card1 = VisionReadout::Tool1Card();
        CHECK_MSG(Contains(t1, Loc::F("Readout.OffsetText", 0.1, -0.2, 0.5)) &&
                  Contains(t1, Loc::F("Readout.TargetText", 351.2, sim->AssyTray->AxisRow, 60.5, 1, 101.0, 1.0)), t1);
        CHECK(card1 != nullptr && card1->Rows.size() == 2 && card1->Rows[0].Label == Loc::T("Readout.RowOffset") &&
              card1->Rows[1].Label == Loc::T("Readout.RowTarget") && card1->Columns.size() == 4 &&
              card1->Rows[0].Values.size() == 4 && card1->Rows[1].Values.size() == 4 && card1->Cell.empty() &&
              card1->Breakdown == nullptr && !card1->CanToggle() && card1->Note.empty());

        // Nhắm ô (hàng 1, cột 2): ô in ở góc bảng, bảng tách Đích cộng lại đúng bằng Đích (X, Y, θ).
        VisionReadout::Tool(2, read, cmd, 0, 1);
        auto card2 = VisionReadout::Tool2Card();
        CHECK(card2 != nullptr && card2->Cell == Loc::F("Readout.Cell", 1, 2) && card2->CanToggle());
        auto bd = card2 ? card2->Breakdown : nullptr;
        bool sums = bd != nullptr && bd->IsBreakdown && bd->Rows.size() == 3 && bd->Columns.size() == 5;
        if (sums)
        {
            for (size_t i = 0; i < 3; ++i)
            {
                const auto& v = bd->Rows[i].Values;
                if (v.size() != 5) { sums = false; break; }
                double total = std::stod(v[0]) + std::stod(v[1]) + std::stod(v[2]) + std::stod(v[3]);
                double target = std::stod(v[4]);
                bool okRow = i < 2 ? std::fabs(total - target) <= 0.0025 : AxisAngle::Distance(total, target) <= 0.025;
                sums = sums && okRow;
            }
        }
        CHECK_MSG(sums, VisionReadout::Tool2());

        // Bỏ ô (G): ghi chú "bỏ ô" và vẫn in đích.
        VmPlaceCommand skip = cmd;
        skip.Route = EVmRoute::Skip;
        skip.Why = "off grid\nline2";
        VisionReadout::Tool(1, read, skip, 0, 0);
        auto cardSkip = VisionReadout::Tool1Card();
        CHECK(cardSkip != nullptr && cardSkip->Note == Loc::F("Readout.SkipCell", "off grid line2") && cardSkip->Rows.size() == 2);

        // Không đặt (N/E): ghi chú "không đặt", không có hàng Đích.
        VmPlaceCommand ng = cmd;
        ng.Route = EVmRoute::Ng;
        ng.Why = "too far";
        VisionReadout::Tool(1, read, ng);
        auto cardNg = VisionReadout::Tool1Card();
        CHECK(cardNg != nullptr && cardNg->Note == Loc::F("Readout.NotPlaced", "too far") && cardNg->Rows.size() == 1);

        // Lỗi gọi Flow: một dòng, không có hàng số.
        VmCameraRead bad;
        bad.CallOk = false;
        bad.CallError = "boom\r\nline2";
        VisionReadout::Tool(1, bad, nullptr);
        auto cardBad = VisionReadout::Tool1Card();
        CHECK(cardBad != nullptr && cardBad->Note == Loc::F("Readout.CallError", "boom line2") && !cardBad->HasRows());

        // Không thấy sản phẩm.
        VmCameraRead none = read;
        none.Found = false;
        VisionReadout::Tool(1, none, nullptr);
        CHECK(VisionReadout::Tool1Card()->Note == Loc::T("Readout.NoProduct"));

        // Top-down, model mô phỏng chưa có ảnh master hand-eye: hàng Chi tiết + Δ tay #1, #2 để "—", ghi chú nhắc chạy lại.
        VmCameraRead topRead = read;
        topRead.X = 300.2; topRead.Y = 79.9; topRead.A = 3; topRead.AxX = 300; topRead.AxY = 80;
        VisionReadout::TopDown(topRead, 1, 0);
        auto cardTop = VisionReadout::TopCard();
        std::string textTop = VisionReadout::Top();
        CHECK_MSG(cardTop != nullptr && cardTop->Rows.size() == 3 && cardTop->Columns.size() == 3 &&
                  cardTop->Rows[0].Label == Loc::T("Readout.RowPart") && cardTop->Rows[1].Label == Loc::F("Readout.DeltaNozzle", 1) &&
                  cardTop->Rows[2].Label == Loc::F("Readout.DeltaNozzle", 2) && cardTop->Rows[1].Values.size() == 3 &&
                  cardTop->Rows[1].Values[0] == "—" && cardTop->Note == Loc::F("Readout.NoMasterNote", "#1, #2") &&
                  cardTop->Cell == Loc::F("Readout.Cell", 2, 1), textTop);
        CHECK(Contains(textTop, Loc::F("Readout.PartText", 300.2, 79.9, 3.0)) && Contains(textTop, Loc::F("Readout.NoMasterLine", 1)));

        // Có ảnh master hand-eye của tay #1: Δ = (vị trí trong ảnh) − (master trong ảnh).
        sim->Calib.SetHandEyeTarget(1, 300.0, 80.0, 2.0, 299.5, 80.5);
        VisionReadout::TopDown(topRead);
        auto cardTop2 = VisionReadout::TopCard();
        // Δx = (300.2 − 300) − (300 − 299.5) = −0.3; Δy = (79.9 − 80) − (80 − 80.5) = 0.4; Δθ = 1.
        CHECK_MSG(cardTop2 != nullptr && cardTop2->Rows.size() == 3 && cardTop2->Rows[1].Values[0] == "-0.300" &&
                  cardTop2->Rows[1].Values[1] == "+0.400" && cardTop2->Rows[1].Values[2] == "+1.00" &&
                  cardTop2->Rows[2].Values[0] == "—" && cardTop2->Note == Loc::F("Readout.NoMasterNote", "#2"), VisionReadout::Top());

        McDataManager::UseModel(prevModel);
    }
}

int RunControllerFoundationTests()
{
    testkit::Group group("ControllerFoundation");

    VisionOutputNames names;

    TestSinglePointAlignMath();
    TestTwoHandGate();
    TestStaticRules();
    TestVmCycleGlobals(names);
    TestVmCycleTrace(names);
    TestVmCycleCheckPlace(names);
    TestVmCycleRead(names);
    TestVmCycleJudges();
    TestVmCycleGate();
    TestVmCycleContract(names);
    TestVisionReadout();

    return group.Result();
}
