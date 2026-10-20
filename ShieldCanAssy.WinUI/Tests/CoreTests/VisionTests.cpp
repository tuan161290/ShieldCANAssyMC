// Kiểm thử tầng vision: SinglePointAlignMath, VmContract, VmOutCheck, GlobalVarSync, FakeVision, VisionConnect.
// Port phần vision của SelfTest.cs (TestSinglePointAlignMath, TestVmFlowOutputCheck, TestGlobalVarCreate, phần thuần
// hợp đồng của TestVmContract) — phần cần VmCycle / McControl nằm ở ControllerTests. Chạy trên thư mục dữ liệu tạm,
// không đụng D:\ShieldCanAssy, KHÔNG nạp SDK VisionMaster (mô phỏng + thiết bị thô giả).
#include "TestKit.h"

#include "Base/Async.h"
#include "Base/Text.h"
#include "Communication/VisionConnect.h"
#include "Controller/SinglePointAlignMath.h"
#include "Infor/AlarmCatalog.h"
#include "Infor/AlarmCode.h"
#include "Infor/AlarmManager.h"
#include "Lang/Loc.h"
#include "SysConfig/AppPaths.h"
#include "Utility/Log/LogWorker.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <filesystem>
#include <set>
#include <thread>

using namespace sca;
namespace fs = std::filesystem;

namespace
{
    const std::string NL = "\r\n";

    bool Near(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }
    bool Has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

    struct ThrowingStore : IGlobalVarStore
    {
        std::map<std::string, std::string> ReadAll() override { throw std::runtime_error("boom"); }
        void Add(const std::string&, const std::string&, const std::string&) override {}
        void SetValue(const std::string&, const std::string&, const std::string&) override {}
        std::string Save() override { return {}; }
    };

    /// Thiết bị "thô" giả cho VisionConnect: đếm lời gọi, treo theo lệnh, ném theo lệnh.
    struct StubDevice : IVisionDevice
    {
        std::atomic<bool> connected{ false }, loaded{ false };
        std::atomic<int> delayMs{ 0 }, calls{ 0 }, calibCalls{ 0 }, clearCalls{ 0 };
        std::atomic<bool> throwOnCall{ false }, loadThrows{ false };
        std::shared_ptr<const ImageData> image = std::make_shared<ImageData>(ImageData{ 2, 1, 1, 2, { 7, 9 } });

        bool IsConnected() const override { return connected; }
        bool IsSolutionLoaded() const override { return loaded; }
        bool Connect() override { connected = true; return true; }
        void Disconnect() override { connected = false; loaded = false; }
        bool LoadSolution(const std::string& path) override
        {
            if (loadThrows) throw VisionDeviceError("SDK refused " + path);
            loaded = true;
            return true;
        }
        std::string ClearCalibPoints(EVisionProcedure) override
        {
            ++clearCalls;
            if (throwOnCall) throw VisionDeviceError("boom");
            return {};
        }
        std::vector<std::string> LastClearedCalibModules() const override { return { "N-point Calibration1" }; }
        VisionPose TriggerCalib(EVisionProcedure, const AxisSnapshot& axes, std::stop_token) override
        {
            ++calibCalls;
            base::Delay(delayMs);
            VisionPose p;
            p.Found = axes.X > 0;
            return p;
        }
        GlobalVarSyncReport SyncGlobalVariables(const std::vector<GlobalVarSpec>&, bool) override { throw VisionDeviceError("sync broken"); }
        VmFlowOutputs ReadFlowOutputs(const std::string& flow) override
        {
            VmFlowOutputs r;
            r.Flow = flow;
            r.AddDeclared("TopX_mm", 14);
            r.AddDeclared("Img", 1);
            return r;
        }
        VmCallResult CallFlow(const VmCallRequest& request, std::stop_token) override
        {
            ++calls;
            base::Delay(delayMs);
            if (throwOnCall) throw VisionDeviceError("boom");
            VmCallResult r;
            r.Ok = true;
            r.Values["Echo"] = request.Globals.count("G") ? request.Globals.at("G") : -1;
            return r;
        }
        std::string SaveSolution() override { return "disk full"; }
        std::shared_ptr<const ImageData> LastTopDownImage() const override { return image; }
        std::shared_ptr<const ImageData> LastBottomUpImageTool1() const override { return nullptr; }
        std::shared_ptr<const ImageData> LastBottomUpImageTool2() const override { return nullptr; }
    };

    VmCallRequest Req(const std::string& flow, std::map<std::string, double> globals, std::vector<std::string> outs, std::vector<std::string> opt = {})
    {
        VmCallRequest r;
        r.Flow = flow;
        r.Camera = EVmCamera::TopDown;
        r.Globals = std::move(globals);
        r.Outputs = std::move(outs);
        r.OptionalOutputs = std::move(opt);
        return r;
    }

    bool Logged(const std::string& message, LogLevel level)
    {
        LogWorker::Instance().Flush();
        for (auto& l : LogWorker::Instance().LogList())
            if (l.Message == message && l.Source == "Vision" && l.Level == level) return true;
        return false;
    }

    // ── SelfTest.TestSinglePointAlignMath ──
    void TestSinglePointAlignMath()
    {
        // Đây là công thức VisionMaster dùng để tính lượng bù (App không tính nữa, Tuấn chốt 2026-09-12). Khoá lại bằng
        // dữ liệu biết trước đáp số: nếu một ngày số của VisionMaster lệch khỏi những khẳng định dưới đây thì lỗi nằm ở
        // CẤU HÌNH solution (đơn vị, dấu góc, gốc toạ độ), không phải ở chỗ khác trong app.
        double dx, dy, dt;
        // Xoay 90° thì lệch tâm 1 mm theo X quay thành 1 mm theo Y, lệnh trừ đi đúng lượng đó.
        SinglePointAlignMath::Solve(0, 0, 90, 1, 0, 0, dx, dy, dt);
        CHECK(Near(dt, 90) && Near(dx, 0) && Near(dy, -1));
        // Sản phẩm đúng tâm xoay thì lệnh = đúng toạ độ đích, không có lượng bù.
        SinglePointAlignMath::Solve(10, 20, 45, 0, 0, 0, dx, dy, dt);
        CHECK(Near(dx, 10) && Near(dy, 20) && Near(dt, 45));
        // Phép thử vòng xoay: xoay +10° rồi quay lại thì Δθ = −10° và lượng dịch = 0.
        const double alpha = 10.0, rad = alpha * 3.14159265358979323846 / 180.0, p0x = 1.5, p0y = -0.8;
        double p1x = p0x * std::cos(rad) - p0y * std::sin(rad), p1y = p0x * std::sin(rad) + p0y * std::cos(rad);
        SinglePointAlignMath::Solve(p0x, p0y, 0, p1x, p1y, alpha, dx, dy, dt);
        CHECK(Near(dt, -alpha) && std::fabs(dx) < 1e-9 && std::fabs(dy) < 1e-9);
        // Hệ toạ độ bị lật (ảnh gương của camera dưới) thì phép thử vòng xoay còn sai số dư khác 0.
        double mx = p0x * std::cos(-rad) - p0y * std::sin(-rad), my = p0x * std::sin(-rad) + p0y * std::cos(-rad);
        SinglePointAlignMath::Solve(p0x, p0y, 0, mx, my, alpha, dx, dy, dt);
        CHECK(std::sqrt(dx * dx + dy * dy) > 0.1);
        // Góc chuẩn hoá: đích 170°, đối tượng −170° → xoay −20° chứ không phải +340°.
        SinglePointAlignMath::Solve(0, 0, 170, 0, 0, -170, dx, dy, dt);
        CHECK(Near(dt, -20));
        CHECK(SinglePointAlignMath::NormalizeDeg(350) == -10 && SinglePointAlignMath::NormalizeDeg(-190) == 170);
    }

    // ── VmContract (phần thuần hợp đồng của SelfTest.TestVmContract) ──
    void TestVmContract()
    {
        VmCallResult vr;
        vr.Values["a"] = 2.5; vr.Values["b"] = 3.5; vr.Values["c"] = -1.4; vr.Values["n"] = std::nan("");
        CHECK(vr.Has("a") && !vr.Has("x") && std::isnan(vr.Get("x")) && vr.Get("c") == -1.4);
        CHECK(vr.GetInt("a") == 2 && vr.GetInt("b") == 4 && vr.GetInt("c") == -1 && vr.GetInt("x") == INT_MIN && vr.GetInt("n") == INT_MIN);
        VmCallResult vf = VmCallResult::Fail("why", true);
        CHECK(!vf.Ok && vf.TimedOut && vf.Error == "why" && !VmCallResult::Fail("x").TimedOut);
        CHECK(VmMath::Near(1.0, 1.0009) && !VmMath::Near(1.0, 1.0011) && !VmMath::Near(std::nan(""), 1) && VmMath::Near(1, 2, 1.0));
        CHECK(VmMath::SameInt(2.49, 2) && !VmMath::SameInt(2.5, 2) && !VmMath::SameInt(INFINITY, 2) && !VmMath::IsFinite(-INFINITY));

        // RunId nằm trong [1, 9 999 999] và tăng dần.
        int a = VmRunIds::Next(), b = VmRunIds::Next();
        CHECK(a >= 1 && a <= VmRunIds::Max && b >= 1 && b <= VmRunIds::Max && (b == a + 1 || (a == VmRunIds::Max && b == 1)));

        CHECK(VmVars::Place().size() == 8 && VmVars::Place()[0] == "RunId" && VmVars::Place()[2] == "CellX_mm" && VmVars::Place()[7] == "AlignSign");
        CHECK(std::string(VmVars::IsTriggerLocate) == "IsTriggerLocate" && std::string(VmVars::HandEyeDy) == "HandEyeDy_mm");
        CHECK(VmOut::LocateEcho() == std::vector<std::string>({ "EchoRunId", "EchoX_mm", "EchoY2_mm" }));
        CHECK(VmOut::AlignEcho()[2] == "EchoA_deg" && VmOut::LocateTopScript()[0] == "TopFound" && VmOut::AlignBottomScript()[4] == "ObjRunId");
        CHECK(VmOut::AlignPlace().size() == 8 && VmOut::AlignPlace()[2] == "CmdY2_mm" && std::string(VmOut::CmdY2) == "CmdY2_mm");

        // Câu mô tả Status đi qua bảng chữ (cùng khoá với bản C#) — thiếu khoá thì Loc trả tên khoá.
        CHECK(VmStatus::Place(1) == Loc::T("Vm.StPlaceOk") && VmStatus::Place(1) != "Vm.StPlaceOk");
        CHECK(VmStatus::Place(0) == Loc::T("Vm.StPlaceNoTarget") && Has(VmStatus::Place(0), "HasTarget") && VmStatus::Place(6) == Loc::T("Vm.StPlaceInvalid"));
        CHECK(VmStatus::Place(9) == Loc::F("Vm.StPlaceOdd", 9) && Has(VmStatus::Place(9), "(9)"));
        CHECK(VmStatus::AlignVerify(8) == Loc::T("Vm.StAlignTurnSmall") && VmStatus::AlignVerify(6) == Loc::T("Vm.StNotFinite")
              && VmStatus::AlignVerify(5) == Loc::F("Vm.StOdd", 5) && VmStatus::AlignVerify(3) == Loc::T("Vm.StAlignAngleOut"));
        CHECK(VmStatus::TopDownVerify(2) == Loc::T("Vm.StTopStepSmall") && VmStatus::TopDownVerify(8) == Loc::T("Vm.StTopWrongSign")
              && VmStatus::TopDownVerify(7) == Loc::T("Vm.StTopAxisMoved") && VmStatus::TopDownVerify(3) == Loc::F("Vm.StOdd", 3));
        for (const char* key : { "Vm.StPlaceOk", "Vm.StPlaceOdd", "Vm.StAlignOk", "Vm.StTopOk", "Vm.StOdd", "Vm.StNotFinite" })
            CHECK_MSG(Loc::Has(key), key);
    }

    // ── SelfTest.TestVmFlowOutputCheck (phần không cần VmCycle) + khoá chữ báo cáo ──
    void TestVmFlowOutputCheck()
    {
        VisionOutputNames names;
        auto scripts = VmScriptContract::Build(names);
        // Hợp đồng Script phủ ba Script (TopAbs + Cmd hai tay).
        CHECK(scripts.size() == 3 && scripts[0].Flow == names.FlowLocateTop && scripts[1].Flow == names.FlowAlignBottomTool1
              && scripts[2].Flow == names.FlowAlignBottomTool2);
        CHECK(scripts[0].ScriptName == "TopAbs" && scripts[0].Inputs.size() == 7 && scripts[0].Outputs.size() == 5);
        CHECK(scripts[1].ScriptName == "Cmd" && scripts[1].Inputs.size() == 12 && scripts[1].Outputs.size() == 9);
        CHECK(scripts[0].Inputs[4] == "EchoY2_mm" && scripts[2].Outputs[2] == "CmdY2_mm" && scripts[2].Outputs[8] == "ObjRunId");

        CHECK(VmOutCheck::TypeName(13) == "int" && VmOutCheck::TypeName(14) == "float" && VmOutCheck::TypeName(25) == "double");
        CHECK(VmOutCheck::TypeName(1) == Loc::T("Vm.TypeImage") && VmOutCheck::TypeName(15) == Loc::T("Vm.TypeString") && VmOutCheck::TypeName(99) == Loc::F("Vm.TypeCode", 99));
        CHECK(VmOutCheck::IsNumeric(13) && VmOutCheck::IsNumeric(14) && !VmOutCheck::IsNumeric(25));
        CHECK(VmOutCheck::Normalize(" HandEyeDx_mm ") == "handeyedx" && VmOutCheck::Normalize("Cell_A_deg") == "cella" && VmOutCheck::EditDistance("detal", "delta") == 2);

        // Bản khai có lỗi: thiếu Found, gõ HandEyeDx thiếu đuôi, Status khai kiểu chuỗi, thừa một tên lạ.
        VmFlowExpect expect;
        expect.Flow = "AlignBottom_Tool1";
        expect.ImageName = "ImageData";
        expect.Names = { "Found", "ObjX_mm", "Status", "HandEyeDx_mm", "TopX_mm", "TopY_mm" };
        VmFlowOutputs actual;
        actual.Flow = "AlignBottom_Tool1";
        actual.InputParamCount = 3;
        actual.AddDeclared("ObjX_mm", VmOutCheck::TypeFloat);
        actual.AddDeclared("Status", 15);                      // chuỗi — App đọc không ra
        actual.AddDeclared("HandEyeDx", VmOutCheck::TypeFloat); // thiếu đuôi _mm
        actual.AddDeclared("TopX_mm", VmOutCheck::TypeFloat);
        actual.AddDeclared("ImageData", 1);                    // ảnh — không phải thừa
        actual.AddDeclared("Score", VmOutCheck::TypeFloat);    // thừa thật, không giống tên nào

        VmOutCheckReport bad;
        VmOutCheck::CompareFlow(expect, &actual, bad);
        // HandEyeDx_mm cũng tính là thiếu: ô đang khai tên HandEyeDx, App đọc theo tên nên không lấy được.
        CHECK(bad.Count(EVmOutState::Missing) == 3);
        auto typo = std::find_if(bad.Items.begin(), bad.Items.end(), [](const VmOutItem& i) { return i.State == EVmOutState::Typo; });
        CHECK(typo != bad.Items.end() && typo->Name == "HandEyeDx" && typo->Suggest == "HandEyeDx_mm");
        CHECK(bad.Count(EVmOutState::WrongType) == 1);
        CHECK(std::none_of(bad.Items.begin(), bad.Items.end(), [](const VmOutItem& i) { return i.Name == "ImageData"; }));
        CHECK(std::any_of(bad.Items.begin(), bad.Items.end(), [](const VmOutItem& i) { return i.Name == "Score" && i.State == EVmOutState::Extra; }));
        // TopX_mm không bị đem gợi ý cho TopY_mm đang thiếu — nó nằm trong hợp đồng, đã khớp.
        CHECK(std::any_of(bad.Items.begin(), bad.Items.end(), [](const VmOutItem& i) { return i.Name == "TopX_mm" && i.State == EVmOutState::Ok; }));
        CHECK(bad.Notes.size() == 1 && bad.Notes[0] == Loc::F("Vm.FlowInputParamsLeft", "AlignBottom_Tool1", 3) && Has(bad.Notes[0], "Input Params"));
        CHECK(!bad.Clean());

        // Chữ báo cáo — đúng thứ tự dòng của bản C#.
        {
            VmFlowExpect e; e.Flow = "F1"; e.Names = { "TopX_mm", "TopY_mm", "TopA_deg", "TopFound" }; e.Note = "remind";
            VmFlowOutputs a; a.Flow = "F1"; a.AddDeclared("TopX", 14); a.AddDeclared("TopY_mm", 15); a.AddDeclared("TopFound", 13); a.AddDeclared("Dbg", 14); a.InputParamCount = 2;
            VmOutCheckReport rep;
            VmOutCheck::CompareFlow(e, &a, rep);
            CHECK(!rep.Clean() && rep.Count(EVmOutState::Missing) == 2 && rep.Count(EVmOutState::Typo) == 1 && rep.Count(EVmOutState::WrongType) == 1 && rep.Count(EVmOutState::Extra) == 1);
            std::string want =
                Loc::F("Vm.OutOkCount", 1) + Loc::F("Vm.CountMissing", 2) + Loc::F("Vm.OutTypoCount", 1) + Loc::F("Vm.CountWrongType", 1) + Loc::F("Vm.OutExtraCount", 1) + NL +
                NL + "Flow F1:" + NL +
                Loc::F("Vm.OutLineMissing", "TopX_mm") + NL +
                Loc::F("Vm.OutLineWrongType", "TopY_mm", Loc::T("Vm.TypeString")) + NL +
                Loc::F("Vm.OutLineMissing", "TopA_deg") + NL +
                Loc::F("Vm.OutLineTypo", "TopX", "TopX_mm") + NL +
                NL + Loc::T("Vm.OutExtraHeader") + NL +
                "  · F1: Dbg" + NL +
                NL + "· " + Loc::F("Vm.FlowInputParamsLeft", "F1", 2) + NL + "· F1: remind";
            CHECK_MSG(rep.ToText() == want, rep.ToText());

            VmFlowExpect clean; clean.Flow = "F0"; clean.Names = { "A_mm", "B" }; clean.ImageName = "ImageData";
            VmFlowOutputs ok; ok.Flow = "F0"; ok.AddDeclared("A_mm", 14); ok.AddDeclared("B", 13); ok.AddDeclared("ImageData", 1); ok.InputParamCount = 0;
            VmOutCheckReport good;
            VmOutCheck::CompareFlow(clean, &ok, good);
            CHECK(good.Clean() && good.Count(EVmOutState::Ok) == 2 && good.Items.size() == 2);
            CHECK_MSG(good.ToText() == Loc::F("Vm.OutOkCount", 2) + Loc::F("Vm.CountMissing", 0) + NL + NL + Loc::T("Vm.OutAllClean"), good.ToText());

            VmOutCheckReport errs;
            VmFlowOutputs gone = VmFlowOutputs::Fail("F1", "no Flow"), empty;
            VmOutCheck::CompareFlow(e, nullptr, errs);
            VmOutCheck::CompareFlow(e, &gone, errs);
            VmOutCheck::CompareFlow(e, &empty, errs);
            // Flow không có trong solution thì báo riêng, không kể là thiếu từng output.
            CHECK(errs.FlowErrors.size() == 3 && errs.Items.empty() && errs.FlowErrors[0] == "F1: " + Loc::T("Vm.FlowUnreadable")
                  && errs.FlowErrors[1] == "F1: no Flow" && errs.FlowErrors[2] == Loc::F("Vm.FlowNoOutputs", "F1"));
            CHECK(!errs.Clean() && errs.ToText().find(Loc::F("Vm.OutOkCount", 0) + Loc::F("Vm.CountMissing", 0) + NL + "✗ F1: " + Loc::T("Vm.FlowUnreadable") + NL) == 0);
            CHECK(VmOutCheckReport::Fail("not loaded").ToText() == Loc::F("Vm.ReportError", "not loaded") && empty.InputParamCount == -1);
        }

        // ── Script hỏng: bỏ trống Subscribe, gõ sai tên, thiếu dòng ──
        const VmScriptExpect& sExpect = scripts[1];
        VmFlowOutputs flowIo;
        flowIo.Flow = names.FlowAlignBottomTool1;
        VmModuleIo mod;
        mod.Name = "Script1";   // tên module khác cẩm nang — vẫn phải tìm ra
        for (auto& nm : VmScriptContract::CmdIn())
        {
            if (nm == "SpaStatus") continue;                                       // thiếu hẳn dòng này
            if (nm == "DeltaTheta_deg") { mod.Inputs.push_back(VmIoInfo{ "DetalTheta_deg", "float", "Single Point Alignment1 · DeltaTheta" }); continue; }
            mod.Inputs.push_back(VmIoInfo{ nm, "float", nm == "AlignSign" ? std::string() : "module In · " + nm });   // AlignSign bỏ trống Subscribe
        }
        for (auto& nm : VmScriptContract::CmdOut()) mod.Outputs.push_back(VmIoInfo{ nm, "float", "" });
        flowIo.Modules.push_back(VmModuleIo{ "Echo0", {}, {} });
        flowIo.Modules.push_back(mod);

        VmOutCheckReport sRep;
        VmOutCheck::CompareScript(sExpect, &flowIo, sRep);
        const VmModuleIo* found = VmOutCheck::FindScript(&flowIo, sExpect);
        CHECK(found != nullptr && found->Name == "Script1");
        CHECK(std::any_of(sRep.Items.begin(), sRep.Items.end(), [](const VmOutItem& i) { return i.Name == "AlignSign" && i.State == EVmOutState::Unbound; }));
        CHECK(std::any_of(sRep.Items.begin(), sRep.Items.end(), [](const VmOutItem& i) { return i.Name == "SpaStatus" && i.State == EVmOutState::Missing; }));
        CHECK(std::any_of(sRep.Items.begin(), sRep.Items.end(), [](const VmOutItem& i) { return i.Name == "DetalTheta_deg" && i.State == EVmOutState::Typo && i.Suggest == "DeltaTheta_deg"; }));
        CHECK(std::none_of(sRep.Items.begin(), sRep.Items.end(), [](const VmOutItem& i) { return Has(i.Flow, "Output Params") && i.State != EVmOutState::Ok; }));
        CHECK(!sRep.Clean() && sRep.ScriptBlocks.size() == 1 && Has(sRep.ScriptBlocks[0], Loc::T("Vm.ScriptNotBound")));
        CHECK(Has(sRep.ToText(), Loc::F("Vm.OutLineUnbound", "AlignSign") + NL) && Has(sRep.ToText(), Loc::F("Vm.OutUnboundCount", 1)));

        // Khối bảng Input: cột canh theo số ký tự, dòng chưa bind in rõ.
        {
            VmScriptExpect e; e.Flow = "LT"; e.ScriptName = "TopAbs"; e.Inputs = { "EchoX_mm", "MatchNum" }; e.Outputs = { "TopFound" };
            VmFlowOutputs f; f.Flow = "LT";
            f.Modules.push_back(VmModuleIo{ "S1", { { "EchoX_mm", "float", "Echo0.X" }, { "MatchNum", "int", "" } }, { { "TopFound", "float", "" } } });
            VmOutCheckReport rep;
            VmOutCheck::CompareScript(e, &f, rep);
            CHECK(rep.Items.size() == 3 && rep.Items[2].Flow == "LT · Script S1 (Input Params)" && rep.Items[2].Name == "MatchNum" && rep.Items[2].State == EVmOutState::Unbound);
            std::string want = Loc::F("Vm.ScriptBlockHead", "S1", "LT", 2, 1) + NL +
                               "    EchoX_mm        float  ← Echo0.X" + NL +
                               "    MatchNum        int    " + Loc::T("Vm.ScriptNotBound") + NL;
            CHECK_MSG(rep.ScriptBlocks.size() == 1 && rep.ScriptBlocks[0] == want, rep.ScriptBlocks.empty() ? "" : rep.ScriptBlocks[0]);
        }

        // Tên VmIO do người dùng khai bị bọc %…% (máy thật 21/09).
        CHECK(VmOutCheck::CleanIoName("%TopX_mm%") == "TopX_mm" && VmOutCheck::CleanIoName("InputImage") == "InputImage");
        CHECK(VmOutCheck::CleanIoName("%") == "%" && VmOutCheck::CleanIoName("50%") == "50%" && VmOutCheck::CleanIoName("%%") == "%%");

        // Không tìm ra Script thì báo cáo in luôn tên output mà module Script đang khai.
        VmFlowOutputs odd;
        odd.Flow = names.FlowAlignBottomTool2;
        odd.Modules.push_back(VmModuleIo{ "Script1", {}, { { "Foo", "float", "" } } });
        VmOutCheckReport oddRep;
        VmOutCheck::CompareScript(scripts[2], &odd, oddRep);
        CHECK_MSG(oddRep.FlowErrors.size() == 1 && Has(oddRep.FlowErrors[0], Loc::F("Vm.ScriptDeclares", "Script1", "Foo"))
                  && oddRep.FlowErrors[0].find(Loc::F("Vm.ScriptNotFound", "AlignBottom_Tool2", "Cmd", "Script1")) == 0,
                  oddRep.FlowErrors.empty() ? "" : oddRep.FlowErrors[0]);

        // Không duyệt được module thì nói thẳng, không quy thành thiếu 21 dòng.
        VmOutCheckReport noMod;
        VmFlowOutputs bare;
        bare.Flow = names.FlowAlignBottomTool1;
        VmOutCheck::CompareScript(sExpect, &bare, noMod);
        CHECK(noMod.Items.empty() && noMod.Notes.size() == 1 && noMod.Notes[0] == Loc::F("Vm.ScriptNoModules", "AlignBottom_Tool1"));
        VmOutCheckReport skip;
        VmFlowOutputs failed = VmFlowOutputs::Fail("X", "no");
        VmOutCheck::CompareScript(sExpect, &failed, skip);
        VmOutCheck::CompareScript(sExpect, nullptr, skip);
        CHECK(skip.Items.empty() && skip.Notes.empty() && skip.FlowErrors.empty());   // lỗi Flow đã báo ở CompareFlow

        // Chữ ký gợi ý tên.
        CHECK(VmOutCheck::SuggestFor("DetalTheta_deg", { "DeltaTheta_deg" }) == std::optional<std::string>("DeltaTheta_deg"));
        CHECK(!VmOutCheck::SuggestFor("CmdX_mm", { "PlacedRunId" }) && !VmOutCheck::SuggestFor("Dbg", { "TopX_mm" }) && !VmOutCheck::SuggestFor("", { "A" }));
        CHECK(VmOutCheck::SuggestFor("HandEyeDx", { "CellX_mm", "HandEyeDx_mm" }) == std::optional<std::string>("HandEyeDx_mm"));
    }

    // ── GlobalVarSync + danh mục (SelfTest.TestGlobalVarCreate, chạy thẳng trên FakeVision thay vì McControl) ──
    void TestGlobalVarSync()
    {
        std::string why;
        CHECK(GlobalVarSync::IsValidName("X_mm", why) && why.empty());
        CHECK(!GlobalVarSync::IsValidName(" ", why) && why == Loc::T("Vm.NameEmpty"));
        CHECK(!GlobalVarSync::IsValidName("a ", why) && why == Loc::T("Vm.NameSpaces"));
        CHECK(!GlobalVarSync::IsValidName(std::string(126, 'a'), why) && why == Loc::T("Vm.NameTooLong") && GlobalVarSync::IsValidName(std::string(125, 'a'), why));
        CHECK(!GlobalVarSync::IsValidName("a(b)", why) && why == Loc::T("Vm.NameForbiddenChars"));
        ThrowingStore throwing;
        CHECK(GlobalVarSync::Run(&throwing, {}, false).ToText() == Loc::F("Vm.ReportError", Loc::F("Vm.GvReadFail", "boom")));
        CHECK(GlobalVarSync::Run(nullptr, {}, true).Error == Loc::T("Vm.GvNoStore"));

        // Danh mục: 13 biến, không trùng tên, đúng luật đặt tên, float trừ cờ IsTriggerLocate kiểu int, giá trị đầu 0.
        SystemData sys;
        auto specs = VisionGlobalVarCatalog::Build(&sys);
        std::vector<std::string> names;
        for (auto& s : specs) names.push_back(s.Name);
        auto has = [&names](const std::string& n) { return std::find(names.begin(), names.end(), n) != names.end(); };
        CHECK_MSG(specs.size() == 13, base::Join(names, ", "));
        CHECK(std::set<std::string>(names.begin(), names.end()).size() == names.size());
        CHECK(std::all_of(specs.begin(), specs.end(), [](const GlobalVarSpec& s) { std::string w; return GlobalVarSync::IsValidName(s.Name, w); }));
        CHECK(std::all_of(specs.begin(), specs.end(), [](const GlobalVarSpec& s)
              { return s.Type == (s.Name == VmVars::IsTriggerLocate ? "int" : "float") && s.InitialValue == "0" && s.Group == EGlobalVarGroup::AppWrites && !s.Remark.empty(); }));
        const VisionOutputNames& vo = sys.VisionOutputs;
        CHECK(has(VmVars::IsTriggerLocate) && has(vo.GvAxisX) && has(vo.GvAxisY2) && has(vo.GvAxisA1) && has(vo.GvAxisA2) && has(VmVars::RunId));
        CHECK(std::all_of(VmVars::Place().begin(), VmVars::Place().end(), has));
        CHECK(names[0] == "X_mm" && names[1] == "Y2_mm" && names[2] == "A1_deg" && names[3] == "A2_deg" && names[4] == "RunId"
              && names[11] == "AlignSign" && names[12] == "IsTriggerLocate");
        CHECK(VisionGlobalVarCatalog::Build(nullptr).size() == 13);
        {
            VisionOutputNames noA2 = vo;
            noA2.GvAxisA2.clear();
            noA2.GvAxisY2 = "X_mm";   // trùng tên thì bỏ, không thêm hai lần
            CHECK(VisionGlobalVarCatalog::Build(noA2).size() == 11);
        }

        // Kho biến của FakeVision: chỉ thêm, không sửa; lưu solution đúng một lần.
        auto fake = std::make_shared<FakeVision>();
        fake->SetProcessTimeMs(5);
        std::vector<GlobalVarSpec> mix{ { "A", "float", "0.5", "", {} }, { "B", "float", "0", "", {} }, { "C", "float", "0.25", "r", {} },
                                        { "bad.name", "float", "0", "", {} }, { "A", "int", "", "", {} } };
        CHECK(fake->SyncGlobalVariables(mix, false).Error == Loc::T("Vm.NoSolution") && fake->SaveSolution() == Loc::T("Vm.NoSolution"));
        CHECK(!fake->LoadSolution("x.solw") && fake->Connect() && fake->LoadSolution("x.solw") && fake->IsConnected() && fake->IsSolutionLoaded());
        fake->PutGlobalVar("%A%", "Float", "9");
        fake->PutGlobalVar("B", "int", "3");
        const std::string tail = Loc::T("Vm.GvListWrongType") + ": B (" + Loc::F("Vm.GvTypeIs", "int", "float") + ")" + NL +
                                 Loc::T("Vm.GvListInvalidName") + ": bad.name (" + Loc::T("Vm.NameForbiddenChars") + ")";
        auto chk = fake->SyncGlobalVariables(mix, false);
        CHECK(!chk.Applied && !chk.Saved && chk.Items.size() == 4 && chk.Items[0].State == EGlobalVarState::Existing && chk.Items[2].State == EGlobalVarState::Missing
              && fake->GlobalVarsSnapshot().size() == 2);
        std::string chkWant = Loc::F("Vm.GvExisting", 1) + Loc::F("Vm.CountMissing", 1) + Loc::F("Vm.CountWrongType", 1) + Loc::F("Vm.GvInvalidNameCount", 1) + NL +
                              tail + NL + Loc::T("Vm.GvListMissing") + ": C";
        CHECK_MSG(chk.ToText() == chkWant, chk.ToText());
        auto app = fake->SyncGlobalVariables(mix, true);
        CHECK(app.Applied && app.Saved && app.Items[2].State == EGlobalVarState::Created && fake->SolutionSaveCount() == 1 && fake->GetGlobal("C") == 0.25);
        CHECK(fake->GlobalVarsSnapshot().at("%A%").Value == "9" && fake->GlobalVarsSnapshot().at("B").Type == "int");   // chỉ thêm, không sửa
        std::string appWant = Loc::F("Vm.GvExisting", 1) + Loc::F("Vm.GvCreated", 1) + Loc::F("Vm.CountMissing", 0) + Loc::F("Vm.CountWrongType", 1) +
                              Loc::F("Vm.GvInvalidNameCount", 1) + NL + Loc::T("Vm.GvSaved") + NL + tail;
        CHECK_MSG(app.ToText() == appWant, app.ToText());
        auto again = fake->SyncGlobalVariables(mix, true);
        CHECK(again.Applied && !again.Saved && again.Count(EGlobalVarState::Existing) == 2 && fake->SolutionSaveCount() == 1);
        CHECK(Has(again.ToText(), Loc::T("Vm.GvNotSaved")));
        CHECK(fake->SaveSolution().empty() && fake->SolutionSaveCount() == 2);

        // SelfTest.TestGlobalVarCreate: dựng sẵn ba biến (một sai kiểu), kiểm tra rồi tạo phần còn thiếu.
        fake->ClearGlobalVars();
        fake->PutGlobalVar(vo.GvAxisX, "float", "12.5");
        fake->PutGlobalVar(vo.GvAxisY2, "float", "0");
        fake->PutGlobalVar(vo.GvAxisA1, "int", "0");   // sai kiểu
        int saveBefore = fake->SolutionSaveCount();
        auto dry = fake->SyncGlobalVariables(specs, false);
        CHECK_MSG(!dry.Applied && fake->GlobalVarsSnapshot().size() == 3 && fake->SolutionSaveCount() == saveBefore, dry.ToText());
        CHECK(std::any_of(dry.Items.begin(), dry.Items.end(), [&](const GlobalVarSyncItem& i) { return i.Spec.Name == vo.GvAxisA1 && i.State == EGlobalVarState::WrongType; }));
        CHECK(dry.Count(EGlobalVarState::Missing) == static_cast<int>(specs.size()) - 3);
        auto made = fake->SyncGlobalVariables(specs, true);
        CHECK_MSG(made.Count(EGlobalVarState::Created) == static_cast<int>(specs.size()) - 3 && made.Count(EGlobalVarState::Failed) == 0 && made.Error.empty(), made.ToText());
        auto snap = fake->GlobalVarsSnapshot();
        CHECK(snap.at(vo.GvAxisX).Value == "12.5" && snap.at(vo.GvAxisA1).Type == "int" && fake->SolutionSaveCount() == saveBefore + 1);
        // Biến mới tạo đúng kiểu danh mục, giá trị đầu 0 — solution không cất hằng số nào.
        CHECK(std::all_of(specs.begin(), specs.end(), [&](const GlobalVarSpec& s)
              { return s.Name == vo.GvAxisX || s.Name == vo.GvAxisY2 || s.Name == vo.GvAxisA1 || (snap.at(s.Name).Type == s.Type && snap.at(s.Name).Value == "0"); }));
        auto twice = fake->SyncGlobalVariables(specs, true);
        CHECK(twice.Count(EGlobalVarState::Created) == 0 && fake->SolutionSaveCount() == saveBefore + 1);
    }

    // ── FakeVision: bản sao Script, cờ rẽ nhánh, Clear Point, huỷ ──
    void TestFakeVision()
    {
        auto fake = std::make_shared<FakeVision>();
        fake->SetProcessTimeMs(5);
        CHECK(fake->ClearCalibPoints(EVisionProcedure::TopDownCalib) == Loc::T("Vm.NoSolution"));
        CHECK(fake->CallFlow(Req("LocateTop", {}, {}), {}).Error == Loc::T("Vm.NotConnected"));
        CHECK(fake->TriggerCalib(EVisionProcedure::TopDownCalib, {}, {}).Error == Loc::T("Vm.NotConnected"));
        CHECK(fake->Connect() && fake->LoadSolution("sim.solw"));
        CHECK(!fake->LastTopDownImage() && !fake->LastBottomUpImageTool1() && !fake->LastBottomUpImageTool2());
        CHECK(fake->ReadFlowOutputs("LocateTop").Error == Loc::T("Vm.SimNoOutputSource"));
        fake->SetFlowOutputSource([](const std::string& f) { VmFlowOutputs r; r.Flow = f; r.AddDeclared("TopX_mm", 14); return r; });
        CHECK(fake->ReadFlowOutputs("LocateTop").Declared.size() == 1 && fake->ReadFlowOutputs("LocateTop").Declared[0].Numeric);

        // Nạp điểm: nhớ vị trí trục gửi đi, móc AfterFlow thấy tên phép (procedure.ToString() của C#).
        std::string seenFlow;
        fake->SetAfterFlow([&](const std::string& f) { seenFlow = f; });
        VisionPose pose = fake->TriggerCalib(EVisionProcedure::AlignCalibTool1, AxisSnapshot{ 1, 2, 3, 4 }, {});
        CHECK(pose.Found && pose.Error.empty() && fake->CalibPointCount() == 1 && seenFlow == "AlignCalibTool1");
        auto sent = fake->LastCalibAxes();
        CHECK(sent && sent->X == 1 && sent->Y2 == 2 && sent->A1 == 3 && sent->A2 == 4);
        fake->SetFoundRate(0);
        CHECK(fake->TriggerCalib(EVisionProcedure::TopDownCalib, {}, {}).Error == Loc::T("Vm.SimObjectNotFound") && fake->CalibPointCount() == 2 && seenFlow == "TopDownCalib");
        fake->SetFoundRate(1);
        CHECK(std::string(ToString(EVisionProcedure::AlignCalibTool2)) == "AlignCalibTool2");

        // Clear Point: đếm lần xoá, nhớ phép, xoá bộ đếm điểm; lỗi thì không xoá gì.
        CHECK(fake->CalibPointsSinceClear() == 2 && !fake->LastCalibClear() && fake->CalibClearCount() == 0);
        CHECK(fake->ClearCalibPoints(EVisionProcedure::AlignCalibTool1).empty() && fake->CalibClearCount() == 1
              && fake->LastCalibClear() == EVisionProcedure::AlignCalibTool1 && fake->CalibPointsSinceClear() == 0);
        fake->TriggerCalib(EVisionProcedure::AlignCalibTool1, {}, {});
        CHECK(fake->CalibPointsSinceClear() == 1 && fake->CalibPointCount() == 3);
        fake->SetFailCalibClear(true);
        CHECK(fake->ClearCalibPoints(EVisionProcedure::TopDownCalib) == Loc::T("Vm.SimClearFail") && fake->CalibClearCount() == 1
              && fake->LastCalibClear() == EVisionProcedure::AlignCalibTool1 && fake->CalibPointsSinceClear() == 1);
        fake->SetFailCalibClear(false);

        // Ghi biến NGHIÊM.
        CHECK(fake->CallFlow(Req("", {}, {}), {}).Error == Loc::T("Vm.FlowNameMissing"));
        CHECK(fake->CallFlow(Req("Nope", {}, {}), {}).Error == Loc::F("Vm.SimFlowNotFound", "Nope"));
        CHECK(fake->CallFlow(Req("LocateTop", { { "RunId", 1 } }, {}), {}).Error == Loc::F("Vm.SimVarNotDeclared", "RunId") && fake->VmCallCount() == 0);
        SystemData sys;
        auto catalog = VisionGlobalVarCatalog::Build(&sys);
        fake->SeedVmSolution(catalog);
        CHECK(fake->GlobalVarsSnapshot().size() == 13 && fake->GlobalVarsSnapshot().at("IsTriggerLocate").Type == "int");
        CHECK(fake->CallFlow(Req("LocateTop", { { "", 1 } }, {}), {}).Error == Loc::T("Vm.SimVarNameEmpty"));
        CHECK(fake->CallFlow(Req("LocateTop", { { "RunId", 5 }, { "X_mm", std::nan("") } }, {}), {}).Error == Loc::F("Vm.SimValueNotFinite", "X_mm")
              && fake->GetGlobal("RunId") == 0);   // không ghi biến nào khi một biến hỏng
        fake->SetGlobal("RunId", 0.1);
        CHECK(fake->GetGlobal("RunId") == 0.1 && std::isnan(fake->GetGlobal("none")) && fake->GlobalVarsSnapshot().at("RunId").Value == "0.1");

        // Cờ rẽ nhánh (Tuấn 2026-10-08): ba Flow, lần soi ghi 1, lần nạp điểm hiệu chuẩn ghi 0 (TestGlobalVarCreate).
        fake->SetGlobal(VmVars::IsTriggerLocate, 7);
        std::map<std::string, double> look{ { "X_mm", 0 }, { "Y2_mm", 0 }, { "A1_deg", 0 }, { "A2_deg", 0 }, { "RunId", 9101 }, { VmVars::IsTriggerLocate, 1 } };
        auto locate = fake->CallFlow(Req("LocateTop", look, { "Found" }), {});
        CHECK_MSG(locate.Ok && fake->GetGlobal(VmVars::IsTriggerLocate) == 1, locate.Error);
        fake->TriggerCalib(EVisionProcedure::TopDownCalib, AxisSnapshot{}, {});
        CHECK(fake->GetGlobal(VmVars::IsTriggerLocate) == 0 && fake->GlobalVarsSnapshot().at(VmVars::IsTriggerLocate).Value == "0");
        look["HasTarget"] = 0;
        auto alignLook = fake->CallFlow(Req("AlignBottom_Tool1", look, { "Found" }), {});
        CHECK_MSG(alignLook.Ok && fake->GetGlobal(VmVars::IsTriggerLocate) == 1, alignLook.Error);
        fake->TriggerCalib(EVisionProcedure::AlignCalibTool1, AxisSnapshot{}, {});
        CHECK(fake->GetGlobal(VmVars::IsTriggerLocate) == 0);
        fake->SetGlobal("HasTarget", 0);
        int locateBefore = fake->LocateTopCount(), alignBefore = fake->AlignBottomCount();

        // LocateTop với quang học giả: dấu chuẩn đứng yên trong hệ máy.
        fake->SetSimulateOptics(true); fake->SetMarkX(10); fake->SetMarkY(20); fake->SetMarkA(1.5);
        auto lt = fake->CallFlow(Req("LocateTop", { { "RunId", 77 }, { "X_mm", 3 }, { "Y2_mm", 4 } }, { "EchoRunId", "EchoX_mm", "EchoY2_mm", "TopFound", "Found" },
                                     { "TopX_mm", "TopY_mm", "TopA_deg", "TopRunId", "Ghost" }), {});
        CHECK(lt.Ok && lt.GetInt("EchoRunId") == 77 && lt.Get("EchoX_mm") == 3 && lt.Get("EchoY2_mm") == 4 && lt.Get("TopFound") == 1 && lt.Get("Found") == 1 && lt.GetInt("TopRunId") == 77);
        CHECK_NEAR(lt.Get("TopX_mm"), 10, 0.0021); CHECK_NEAR(lt.Get("TopY_mm"), 20, 0.0021); CHECK_NEAR(lt.Get("TopA_deg"), 1.5, 0.011);
        CHECK(lt.MissingOptional == std::vector<std::string>{ "Ghost" } && fake->LocateTopCount() == locateBefore + 1 && seenFlow == "LocateTop" && fake->GetGlobal("X_mm") == 3);
        CHECK(fake->CallFlow(Req("LocateTop", {}, { "Ghost" }), {}).Error == Loc::F("Vm.SimFlowMissingOutput", "LocateTop", "Ghost"));
        // Flow MeasurePlaced đã bỏ (ba Flow); LocateTop đã chạy thêm lần "Ghost" (Flow chạy rồi mới hỏng ở bước đọc output).
        CHECK(fake->CallFlow(Req("MeasurePlaced", {}, {}), {}).Error == Loc::F("Vm.SimFlowNotFound", "MeasurePlaced") && fake->LocateTopCount() == locateBefore + 2);

        // AlignBottom_Tool1 chỉ soi: góc của tay #1 là A1_deg; sản phẩm xoay theo trục A quanh tâm xoay.
        const std::vector<std::string> alignOuts{ "EchoRunId", "EchoX_mm", "EchoA_deg", "ObjFound", "ObjX_mm", "ObjY_mm", "ObjA_deg", "ObjRunId", "Status" };
        auto see = fake->CallFlow(Req("AlignBottom_Tool1", { { "RunId", 78 }, { "A1_deg", 10 }, { "A2_deg", 55 }, { "HasTarget", 0 } }, alignOuts, { "CmdX_mm" }), {});
        CHECK(see.Ok && see.Get("Status") == 0 && see.MissingOptional.size() == 1 && see.Get("EchoA_deg") == 10 && fake->PlaceComputeCount() == 0 && !fake->LastPlaceGlobals());
        CHECK_NEAR(see.Get("ObjA_deg"), 10, 0.011);
        const double d10 = 10 * 3.14159265358979323846 / 180;
        CHECK_NEAR(see.Get("ObjX_mm"), 1.5 * std::cos(d10) + 0.75 * std::sin(d10), 0.0021);    // tay #1: lệch tâm (1.5, −0.75)
        auto see2 = fake->CallFlow(Req("AlignBottom_Tool2", { { "RunId", 78 }, { "A1_deg", 10 }, { "A2_deg", 0 } }, alignOuts), {});
        CHECK(see2.Ok && see2.Get("EchoA_deg") == 0 && fake->AlignBottomCount() == alignBefore + 2);
        CHECK_NEAR(see2.Get("ObjX_mm"), -1.5, 0.0021); CHECK_NEAR(see2.Get("ObjY_mm"), 0.75, 0.0021);   // tay #2: (−1.5, 0.75)

        // Kèm ô đích: Group Cmd tính lệnh đặt.
        fake->SetForcedObjPose(std::array<double, 3>{ 1, 0, 0 });
        std::map<std::string, double> place{ { "RunId", 79 }, { "X_mm", 3 }, { "A1_deg", 10 }, { "A2_deg", 0 }, { "HasTarget", 1 }, { "CellX_mm", 0 }, { "CellY_mm", 0 },
                                             { "CellA_deg", 90 }, { "HandEyeDx_mm", 0 }, { "HandEyeDy_mm", 0 }, { "AlignSign", 1 } };
        const std::vector<std::string> cmdOuts{ "Status", "CmdX_mm", "CmdY2_mm", "CmdA_deg", "DiagDA_deg", "DiagEcc_mm", "DiagTarX_mm", "DiagTarY_mm", "EchoRunId", "ObjFound" };
        auto cmd = fake->CallFlow(Req("AlignBottom_Tool1", place, cmdOuts), {});
        CHECK(cmd.Ok && cmd.GetInt("Status") == 1 && cmd.Get("CmdA_deg") == 100 && cmd.Get("DiagDA_deg") == 90 && cmd.Get("DiagEcc_mm") == 1 && cmd.GetInt("EchoRunId") == 79);
        CHECK_NEAR(cmd.Get("CmdX_mm"), 0, 1e-6); CHECK_NEAR(cmd.Get("CmdY2_mm"), -1, 1e-6);
        CHECK(fake->PlaceComputeCount() == 1 && fake->LastPlaceGlobals() && fake->LastPlaceGlobals()->at("HasTarget") == 1 && fake->LastPlaceGlobals()->size() == 8);
        place["AlignSign"] = -1; place["HandEyeDx_mm"] = 2; place["A2_deg"] = 300;
        auto neg = fake->CallFlow(Req("AlignBottom_Tool2", place, cmdOuts), {});     // dấu −1: góc bù đổi dấu, A quay vòng 0…360
        CHECK(neg.GetInt("Status") == 1 && neg.Get("DiagDA_deg") == -90 && neg.Get("CmdA_deg") == 210 && neg.Get("DiagTarX_mm") == 2 && std::fabs(neg.Get("CmdX_mm") - 2) < 1e-6);
        CHECK(fake->LastPlaceGlobals()->at("HandEyeDx_mm") == 2);
        place["AlignSign"] = 0;
        CHECK(fake->CallFlow(Req("AlignBottom_Tool1", place, cmdOuts), {}).GetInt("Status") == 6);
        place["AlignSign"] = 1;
        fake->SetBottomFoundRate(0);
        auto lost = fake->CallFlow(Req("AlignBottom_Tool1", place, cmdOuts), {});
        CHECK(lost.Ok && lost.Get("ObjFound") == 0 && lost.GetInt("Status") == 6);
        fake->SetBottomFoundRate(1);
        fake->SetForcePlaceStatus(4); fake->SetForcePlaceStatusTimes(1); fake->SetCorruptEchoOnce(true); fake->SetTargetShiftMm(0.5); fake->SetTargetShiftTimes(1);
        fake->SetForceDAngleDeg(12); fake->SetForceDAngleTimes(1);
        auto forced = fake->CallFlow(Req("AlignBottom_Tool1", place, cmdOuts), {});
        CHECK(forced.GetInt("Status") == 4 && forced.GetInt("EchoRunId") == 80 && forced.Get("DiagTarX_mm") == 2.5 && forced.Get("DiagDA_deg") == 12 && forced.Get("CmdA_deg") == 22);
        auto after = fake->CallFlow(Req("AlignBottom_Tool1", place, cmdOuts), {});
        CHECK(after.GetInt("Status") == 1 && after.GetInt("EchoRunId") == 79 && after.Get("DiagTarX_mm") == 2 && fake->ForcePlaceStatusTimes() == 0 && !fake->CorruptEchoOnce());
        fake->SetForcedObjPose(std::nullopt);
        CHECK(!fake->ForcedObjPose() && fake->FlowHistorySnapshot().size() == static_cast<size_t>(fake->VmCallCount()) && fake->FlowHistorySnapshot().front() == "LocateTop");
        fake->ClearFlowHistory();
        CHECK(fake->FlowHistorySnapshot().empty());

        fake->SetProcessTimeMs(3000);   // huỷ giữa lúc "chụp": ném OperationCanceled như bản C#, không ghi biến
        {
            std::stop_source src;
            std::jthread killer([&] { base::Delay(60); src.request_stop(); });
            bool cancelled = false;
            base::Stopwatch sw;
            try { fake->CallFlow(Req("LocateTop", { { "RunId", 99 } }, {}), src.get_token()); }
            catch (const base::OperationCanceled&) { cancelled = true; }
            CHECK(cancelled && sw.ElapsedMilliseconds() < 1500 && fake->GetGlobal("RunId") == 79);
            std::stop_source src2; src2.request_stop();
            bool cancelled2 = false;
            try { fake->TriggerCalib(EVisionProcedure::TopDownCalib, {}, src2.get_token()); } catch (const base::OperationCanceled&) { cancelled2 = true; }
            CHECK(cancelled2);
        }
        fake->Disconnect();
        CHECK(!fake->IsConnected() && !fake->IsSolutionLoaded() && fake->CallFlow(Req("LocateTop", {}, {}), {}).Error == Loc::T("Vm.NotConnected"));
    }

    // ── VisionConnect ──
    void TestVisionConnect()
    {
        // Nạp điểm chạy trên chính Flow soi (nhánh hiệu chuẩn, IsTriggerLocate = 0).
        VisionOutputNames names;
        CHECK(VisionConnect::FlowNameOf(names, EVisionProcedure::TopDownCalib) == "LocateTop"
              && VisionConnect::FlowNameOf(names, EVisionProcedure::AlignCalibTool1) == "AlignBottom_Tool1"
              && VisionConnect::FlowNameOf(names, EVisionProcedure::AlignCalibTool2) == "AlignBottom_Tool2");
        auto cv = VisionConnect::CalibVars(names, AxisSnapshot{ 1, 2, 3, 4 });
        CHECK(cv.size() == 5 && cv.at("IsTriggerLocate") == 0 && cv.at("X_mm") == 1 && cv.at("Y2_mm") == 2 && cv.at("A1_deg") == 3 && cv.at("A2_deg") == 4);
        VisionOutputNames noY2;
        noY2.GvAxisY2.clear();
        auto av = VisionConnect::AxisVars(noY2, AxisSnapshot{ 1, 2, 3, 4 });
        CHECK(av.size() == 3 && !av.count("Y2_mm") && !av.count("IsTriggerLocate"));
        CHECK(VisionConnect::IsBottomUp(EVisionProcedure::AlignCalibTool2) && !VisionConnect::IsBottomUp(EVisionProcedure::TopDownCalib));
        CHECK(VisionConnect::BottomUpToolOf(names, "AlignBottom_Tool2") == 2 && VisionConnect::BottomUpToolOf(names, "AlignBottom_Tool1") == 1
              && VisionConnect::BottomUpToolOf(names, "LocateTop") == 1);

        // ── mô phỏng: luôn bản giả, kể cả khi đã đăng ký thiết bị thật ──
        VisionConnect::SetRealDeviceFactory({});
        {
            auto sys = std::make_shared<SystemData>();
            sys->SimulationMode = true;
            sys->VisionOutputs.FlowLocateTop = "LT_custom";
            VisionConnect vc(sys);
            CHECK(vc.IsSimulation() && vc.Fake() && vc.Device() == vc.Fake() && !vc.IsConnected() && vc.CallFlow(Req("LT_custom", {}, {}), {}).Error == Loc::T("Vm.NotConnected"));
            vc.Fake()->SetProcessTimeMs(5);
            CHECK(vc.Connect() && vc.IsConnected() && !vc.IsSolutionLoaded() && vc.LoadSolution("sim.solw") && vc.IsSolutionLoaded());
            auto r = vc.CallFlow(Req("LT_custom", { { "RunId", 5 }, { "X_mm", 1.25 } }, { "EchoRunId", "EchoX_mm", "TopFound" }), {});
            CHECK(r.Ok && r.GetInt("EchoRunId") == 5 && r.Get("EchoX_mm") == 1.25 && vc.Fake()->GlobalVarsSnapshot().size() == 13);
            CHECK(vc.CallFlow(Req("LocateTop", {}, {}), {}).Error == Loc::F("Vm.SimFlowNotFound", "LocateTop"));
            CHECK(vc.TriggerCalib(EVisionProcedure::TopDownCalib, AxisSnapshot{ 9, 0, 0, 0 }, {}).Found && vc.Fake()->LastCalibAxes()->X == 9
                  && vc.Fake()->GetGlobal("IsTriggerLocate") == 0);
            CHECK(vc.ClearCalibPoints(EVisionProcedure::AlignCalibTool2).empty() && vc.Fake()->LastCalibClear() == EVisionProcedure::AlignCalibTool2);
            auto sync = vc.SyncGlobalVariables(VisionGlobalVarCatalog::Build(sys.get()), false);
            CHECK(sync.Error.empty() && sync.Count(EGlobalVarState::Existing) == 13 && vc.SaveSolution().empty());
            CHECK(!vc.LastTopDownImage() && !vc.LastBottomUpImageTool1() && !vc.LastBottomUpImageTool2() && vc.LastClearedCalibModules().empty());
            CHECK(vc.ReadFlowOutputs("LT_custom").Error == Loc::T("Vm.SimNoOutputSource"));
            vc.Disconnect();
            CHECK(!vc.IsConnected() && !vc.IsSolutionLoaded() && AlarmManager::ActiveCount() == 0);
        }

        // ── máy thật mà chưa đăng ký thiết bị → alarm 4001 + log, KHÔNG rơi về bản giả ──
        auto sys = std::make_shared<SystemData>();
        sys->SimulationMode = false;
        sys->VisionTriggerTimeoutMs = 150;
        const std::string noDevice = "no real VisionMaster device registered (VisionConnect::SetRealDeviceFactory)";
        {
            VisionConnect vc(sys);
            CHECK(!VisionConnect::HasRealDeviceFactory() && !vc.IsSimulation() && !vc.Fake() && !vc.Device());
            CHECK(!vc.Connect() && !vc.IsConnected() && AlarmManager::IsActive(AlarmCode::VisionLoadFailed));
            auto al = AlarmManager::Get(AlarmCode::VisionLoadFailed);
            CHECK(al && al->Detail == noDevice && al->Source == "Vision");
            AlarmManager::ForceClearAll();
            CHECK(!vc.LoadSolution("a.solw") && !vc.IsSolutionLoaded() && AlarmManager::IsActive(AlarmCode::VisionLoadFailed));
            CHECK(vc.CallFlow(Req("LocateTop", {}, {}), {}).Error == Loc::T("Vm.NotConnected") && vc.TriggerCalib(EVisionProcedure::TopDownCalib, {}, {}).Error == Loc::T("Vm.NotConnected"));
            CHECK(vc.ClearCalibPoints(EVisionProcedure::TopDownCalib) == Loc::T("Vm.NotConnected") && vc.ReadFlowOutputs("LocateTop").Error == Loc::T("Vm.NotConnected"));
            CHECK(vc.SaveSolution() == Loc::T("Vm.NoSolution") && vc.SyncGlobalVariables({}, false).Error == Loc::T("Vm.NoVisionSolution") && !vc.LastTopDownImage());
            CHECK(Logged("Vision: solution load error — " + noDevice, LogLevel::Error) && Logged("Vision: no real device — " + noDevice, LogLevel::Error));
            AlarmManager::ForceClearAll();
        }

        // ── thiết bị thô qua hàm tạo đăng ký ──
        auto stub = std::make_shared<StubDevice>();
        VisionConnect::SetRealDeviceFactory([stub]() -> std::shared_ptr<IVisionDevice> { return stub; });
        {
            // Mô phỏng: hàm tạo có đăng ký vẫn KHÔNG được dùng — mô phỏng không bao giờ chạm VisionMaster.
            auto simSys = std::make_shared<SystemData>();
            simSys->SimulationMode = true;
            VisionConnect sim(simSys);
            CHECK(sim.IsSimulation() && sim.Device() == sim.Fake() && sim.Device() != stub);
        }
        {
            VisionConnect vc(sys);
            CHECK(vc.Connect() && vc.IsConnected() && vc.Device() == stub && !vc.IsSolutionLoaded() && vc.CallFlow(Req("F", {}, {}), {}).Error == Loc::T("Vm.NoSolution"));
            CHECK(vc.ClearCalibPoints(EVisionProcedure::TopDownCalib) == Loc::T("Vm.NoSolution") && stub->clearCalls == 0);
            stub->loadThrows = true;
            CHECK(!vc.LoadSolution("b.solw") && !AlarmManager::IsActive(AlarmCode::VisionLoadFailed) && Logged("Vision: solution load error — SDK refused b.solw", LogLevel::Error));
            stub->loadThrows = false;
            CHECK(vc.LoadSolution("b.solw") && vc.IsSolutionLoaded());
            auto ok = vc.CallFlow(Req("F", { { "G", 4.5 } }, {}), {});
            CHECK(ok.Ok && ok.Get("Echo") == 4.5 && stub->calls == 1 && vc.LastTopDownImage() == stub->image && !vc.LastBottomUpImageTool1() && !vc.LastBottomUpImageTool2());
            CHECK(vc.CallFlow(Req("", {}, {}), {}).Error == Loc::T("Vm.FlowNameMissing") && vc.ReadFlowOutputs("").Error == Loc::T("Vm.FlowNameNotSet"));
            auto fo = vc.ReadFlowOutputs("F");
            CHECK(fo.Error.empty() && fo.Declared.size() == 2 && fo.Declared[0].TypeText == "float" && fo.Declared[1].TypeText == Loc::T("Vm.TypeImage") && !fo.Declared[1].Numeric);
            CHECK(vc.SyncGlobalVariables({}, true).Error == "sync broken" && vc.SaveSolution() == "disk full");
            CHECK(vc.TriggerCalib(EVisionProcedure::AlignCalibTool1, AxisSnapshot{ 1, 0, 0, 0 }, {}).Found
                  && !vc.TriggerCalib(EVisionProcedure::AlignCalibTool1, AxisSnapshot{ -1, 0, 0, 0 }, {}).Found && stub->calibCalls == 2);

            // Clear Point qua VisionConnect: tới thiết bị, giữ khoá lệnh, ghi log tên module đã xoá.
            CHECK(vc.ClearCalibPoints(EVisionProcedure::TopDownCalib).empty() && stub->clearCalls == 1
                  && vc.LastClearedCalibModules() == std::vector<std::string>{ "N-point Calibration1" });
            CHECK(Logged("Vision: LocateTop — cleared old calib pts of module N-point Calibration1", LogLevel::Info));

            stub->throwOnCall = true;
            CHECK(vc.CallFlow(Req("F", {}, {}), {}).Error == "F: boom" && AlarmManager::ActiveCount() == 0 && Logged("Vision: F error — boom", LogLevel::Error));
            CHECK(vc.ClearCalibPoints(EVisionProcedure::AlignCalibTool2) == "boom" && stub->clearCalls == 2);
            stub->throwOnCall = false;

            // Quá giờ: alarm 4003 dạng sự kiện; lần gọi xếp hàng sau một Run treo bị bỏ thì KHÔNG chạy muộn.
            stub->delayMs = 500;
            int before = stub->calls;
            base::Stopwatch sw;
            auto t1 = vc.CallFlow(Req("Slow", {}, {}), {});
            CHECK(!t1.Ok && t1.TimedOut && t1.Error == Loc::F("Vm.FlowTimeout", "Slow") && sw.ElapsedMilliseconds() < 450);
            auto alarm = AlarmManager::Get(AlarmCode::VisionTriggerTimeout);
            CHECK(alarm && alarm->Detail == "Slow" && alarm->Source == "Vision" && !alarm->ConditionActive);
            auto t2 = vc.CallFlow(Req("Queued", {}, {}), {});
            CHECK(t2.TimedOut && t2.Error == Loc::F("Vm.FlowTimeout", "Queued"));
            base::Delay(600);
            CHECK(stub->calls == before + 1);
            AlarmManager::ForceClearAll();

            // Huỷ giữa lúc Flow chạy: không phải quá giờ, không alarm.
            {
                std::stop_source src;
                std::jthread killer([&] { base::Delay(40); src.request_stop(); });
                auto c = vc.CallFlow(Req("Cut", {}, {}), src.get_token());
                CHECK(!c.Ok && !c.TimedOut && c.Error == Loc::F("Vm.CancelledInFlow", "Cut") && AlarmManager::ActiveCount() == 0);
            }
            base::Delay(600);
            auto tc = vc.TriggerCalib(EVisionProcedure::AlignCalibTool2, AxisSnapshot{ 1, 0, 0, 0 }, {});
            auto tcAlarm = AlarmManager::Get(AlarmCode::VisionTriggerTimeout);
            CHECK(!tc.Found && tc.Error == Loc::T("Vm.Timeout") && tcAlarm && tcAlarm->Detail == "AlignBottom_Tool2");
            std::stop_source dead; dead.request_stop();
            CHECK(vc.TriggerCalib(EVisionProcedure::TopDownCalib, AxisSnapshot{ 1, 0, 0, 0 }, dead.get_token()).Error == Loc::T("Vm.Cancelled"));
            base::Delay(600);
            stub->delayMs = 0;
            CHECK(vc.CallFlow(Req("F", { { "G", 2 } }, {}), {}).Get("Echo") == 2);
            vc.Disconnect();
            CHECK(!vc.IsConnected() && !stub->connected && vc.CallFlow(Req("F", {}, {}), {}).Error == Loc::T("Vm.NotConnected"));
            AlarmManager::ForceClearAll();
        }
        VisionConnect::SetRealDeviceFactory({});
    }
}

int RunVisionTests()
{
    testkit::Group group("Vision");

    fs::path root = fs::temp_directory_path() / ("sca_vision_" + base::NowString("yyyyMMdd_HHmmssfff"));
    AppPaths::Init(base::PathToU8(root));
    LogWorker::Instance().Init(AppPaths::Root());
    fs::path realAlarms = fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "Config" / "alarms.json";
    std::string tplText;
    if (base::ReadAllText(realAlarms, tplText))
    {
        fs::create_directories(root / "exe" / "Config");
        base::WriteAllText(root / "exe" / "Config" / "alarms.json", tplText);
        AppPaths::SetExeDirForTests(root / "exe");
    }
    AlarmCatalog::Load();
    AlarmManager::ForceClearAll();

    TestSinglePointAlignMath();
    TestVmContract();
    TestVmFlowOutputCheck();
    TestGlobalVarSync();
    TestFakeVision();
    TestVisionConnect();

    VisionConnect::SetRealDeviceFactory({});
    base::WaitBackgroundIdle(3000);
    AlarmManager::ForceClearAll();
    AlarmManager::FlushCsv();
    AppPaths::SetExeDirForTests({});
    LogWorker::Instance().Shutdown();
    std::error_code ec;
    fs::remove_all(root, ec);
    return group.Result();
}
