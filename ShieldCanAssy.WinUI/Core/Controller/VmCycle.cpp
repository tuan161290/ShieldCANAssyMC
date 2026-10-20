#include "Controller/VmCycle.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <type_traits>

#include "Communication/AxisAngle.h"
#include "Controller/Routine_Auto.h"
#include "Controller/SinglePointAlignMath.h"
#include "Infor/AxisId.h"
#include "Lang/Loc.h"

namespace sca
{
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;
        constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

        /// VmOut::LocateEcho… là mảng tên trong bản C#; VmContract.h bản C++ có thể khai chúng là hàm trả
        /// <c>const std::vector&lt;std::string&gt;&amp;</c> hoặc là mảng hằng — đọc được cả hai.
        template <class T>
        decltype(auto) Items(T&& v)
        {
            if constexpr (std::is_invocable_v<T>) return v();
            else return (v);
        }

        template <class List>
        void AddAll(std::vector<std::string>& to, const List& names)
        {
            for (const auto& o : names) to.emplace_back(o);
        }

        bool ContainsName(const std::vector<std::string>& names, const std::string& o)
        {
            return std::find(names.begin(), names.end(), o) != names.end();
        }

        double Pos(const MotionConnect* motion, const std::string& axis)
        {
            auto a = motion == nullptr ? nullptr : motion->Axis(axis);
            return a == nullptr ? 0 : a->CurrentPos();
        }

        void AddExpect(std::vector<VmFlowExpect>& list, const VmCallRequest& req, const std::string& image, const std::string& note)
        {
            VmFlowExpect e;
            e.Flow = req.Flow;
            e.ImageName = image;
            e.Note = note;
            for (const auto& o : req.Outputs) if (!ContainsName(e.Names, o)) e.Names.push_back(o);
            for (const auto& o : req.OptionalOutputs) if (!ContainsName(e.Names, o)) e.Names.push_back(o);
            list.push_back(std::move(e));
        }

        /// C# Join: rỗng → null (ở đây: chuỗi rỗng), không thì nối bằng "; ".
        std::string Join(const std::vector<std::string>& parts)
        {
            std::string s;
            for (size_t i = 0; i < parts.size(); ++i)
            {
                if (i > 0) s += "; ";
                s += parts[i];
            }
            return s;
        }

        void CheckEcho(const VmCallResult& r, const std::string& name, double expect, double tol, std::vector<std::string>& issues)
        {
            if (!r.Has(name)) { issues.push_back(Loc::F("VmCycle.MissingOutput", name)); return; }
            double got = r.Get(name);
            if (!VmMath::Near(got, expect, tol)) issues.push_back(name + " = " + VmCycle::Fmt(got) + " ≠ " + VmCycle::Fmt(expect));
        }

        VmPlaceCommand Fail(VmPlaceCommand& c, std::string why) { c.Route = EVmRoute::Error; c.Why = std::move(why); return c; }
        VmPlaceCommand Skip(VmPlaceCommand& c, std::string why) { c.Route = EVmRoute::Skip; c.Why = std::move(why); return c; }
        VmPlaceCommand Ng(VmPlaceCommand& c, std::string why) { c.Route = EVmRoute::Ng; c.Why = std::move(why); return c; }

        /// "0.####" bất biến; không hữu hạn → "NaN" (như bản C#).
        std::string Fmt4(double v)
        {
            return VmMath::IsFinite(v) ? Loc::FormatValue(FmtArg(v), "0.####") : std::string("NaN");
        }

        /// C# Math.Sign cho số hữu hạn.
        int Sign(double v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }

        /// DateTime trong bản ghi hiệu chuẩn: <c>DateTime.MinValue</c> (TimePoint{}) là "chưa đo". VisionCalib bản C++ có
        /// thể giữ <c>TimePoint</c> hoặc <c>std::optional&lt;TimePoint&gt;</c> — đọc được cả hai.
        double DaysSince(const TimePoint& at)
        {
            if (at <= TimePoint{}) return NaN;
            return std::chrono::duration<double>(std::chrono::system_clock::now() - at).count() / 86400.0;
        }
        double DaysSince(const std::optional<TimePoint>& at) { return at ? DaysSince(*at) : NaN; }
    }

    const char* ToString(EVmRoute r)
    {
        switch (r)
        {
            case EVmRoute::Ok: return "Ok";
            case EVmRoute::Ng: return "Ng";
            case EVmRoute::Skip: return "Skip";
            case EVmRoute::Error: return "Error";
        }
        return "?";
    }

    std::string VmCalibGate::Describe() const
    {
        if (!Block.empty()) return Loc::F("VmCycle.GateNotReady", Block);
        return Loc::F("VmCycle.GateReady", HandEyeDx1, HandEyeDy1, HandEyeDx2, HandEyeDy2, AlignSign1, AlignSign2, AngleRef1, AngleRef2);
    }

    // ───────────────────────── biến App ghi ─────────────────────────

    std::string VmCycle::AxisVarProblem(const VisionOutputNames* n)
    {
        if (n == nullptr) return Loc::T("VmCycle.NoVisionNames");
        if (n->GvAxisX.empty() || n->GvAxisY2.empty() || n->GvAxisA1.empty() || n->GvAxisA2.empty())
            return Loc::T("VmCycle.AxisVarsEmpty");
        return {};
    }

    std::map<std::string, double> VmCycle::AxisGlobals(const MotionConnect* motion, const VisionOutputNames& n, int runId)
    {
        std::map<std::string, double> g;
        g[n.GvAxisX] = Pos(motion, AxisId::X);
        g[n.GvAxisY2] = Pos(motion, AxisId::Y2);
        g[n.GvAxisA1] = Pos(motion, AxisId::A1);
        g[n.GvAxisA2] = Pos(motion, AxisId::A2);
        g[VmVars::RunId] = runId;
        return g;
    }

    // ───────────────────────── yêu cầu gọi Flow ─────────────────────────

    VmCallRequest VmCycle::LocateRequest(const VisionOutputNames& n, std::map<std::string, double>& globals)
    {
        // Nhánh soi của Flow (Tuấn 2026-10-08) — nạp điểm hiệu chuẩn top-down ghi 0 ở VisionConnect.
        globals[VmVars::IsTriggerLocate] = 1;
        VmCallRequest req;
        req.Flow = n.FlowLocateTop;
        req.Camera = EVmCamera::TopDown;
        req.Globals = globals;
        req.Outputs.push_back(n.OutFound);
        AddAll(req.Outputs, Items(VmOut::LocateEcho));
        AddAll(req.OptionalOutputs, Items(VmOut::LocateTopScript));
        return req;
    }

    void VmCycle::AddPlaceGlobals(std::map<std::string, double>& g, const PlacedTopRecord* target, const VisionCalib* calib, int tool,
                                  const CellOffset* trim)
    {
        double tdx = trim == nullptr ? 0 : trim->PlaceDx;
        double tdy = trim == nullptr ? 0 : trim->PlaceDy;
        double tda = trim == nullptr ? 0 : trim->PlaceDa;
        g[VmVars::HasTarget] = target == nullptr ? 0 : 1;
        g[VmVars::CellX] = target == nullptr ? 0 : target->X + tdx;
        g[VmVars::CellY] = target == nullptr ? 0 : BottomYSign * (target->Y + tdy);
        double refA = calib == nullptr ? 0 : calib->HandEyeAngleRef(tool);
        g[VmVars::CellA] = target == nullptr ? 0 : AxisAngle::Wrap(refA - target->A + tda);
        g[VmVars::HandEyeDx] = calib == nullptr ? 0 : calib->HandEyeDx(tool);
        g[VmVars::HandEyeDy] = calib == nullptr ? 0 : BottomYSign * calib->HandEyeDy(tool);
        g[VmVars::AlignSign] = calib == nullptr ? 0 : calib->AlignAngleSign(tool);
    }

    VmCallRequest VmCycle::AlignBottomRequest(const VisionOutputNames& n, int tool, std::map<std::string, double>& globals, bool hasTarget)
    {
        // Nhánh soi của Flow (Tuấn 2026-10-08) — nạp điểm hiệu chuẩn tâm xoay ghi 0 ở VisionConnect.
        globals[VmVars::IsTriggerLocate] = 1;
        VmCallRequest req;
        req.Flow = n.FlowAlignBottom(tool);
        req.Camera = EVmCamera::BottomUp;
        req.Globals = globals;
        req.Outputs.push_back(n.OutFound);
        AddAll(req.Outputs, Items(VmOut::AlignEcho));
        AddAll(req.OptionalOutputs, Items(VmOut::AlignBottomScript));
        if (hasTarget)
            AddAll(req.OptionalOutputs, Items(VmOut::AlignPlace));
        return req;
    }

    // ───────────────────────── hợp đồng Output Params ─────────────────────────

    std::vector<VmFlowExpect> VmCycle::ExpectedFlowOutputs(const VisionOutputNames& n)
    {
        std::vector<VmFlowExpect> list;
        std::map<std::string, double> empty;

        AddExpect(list, LocateRequest(n, empty), n.OutImage, std::string());
        AddExpect(list, AlignBottomRequest(n, 1, empty, true), n.OutImage, Loc::T("VmCycle.NoteGroupCmdOutputs"));
        AddExpect(list, AlignBottomRequest(n, 2, empty, true), n.OutImage, Loc::T("VmCycle.NoteTool2Echo"));
        return list;
    }

    VmFlowOutputs VmCycle::FakeFlowOutputs(const std::string& flow, const VisionOutputNames& n)
    {
        auto expects = ExpectedFlowOutputs(n);
        auto expect = std::find_if(expects.begin(), expects.end(), [&](const VmFlowExpect& e) { return e.Flow == flow; });
        if (expect == expects.end()) return VmFlowOutputs::Fail(flow, Loc::T("VmCycle.NoSuchFlow"));

        VmFlowOutputs r;
        r.Flow = flow;
        r.InputParamCount = 0;
        for (const auto& name : expect->Names)
        {
            VmDeclaredOut d;
            d.Name = name;
            d.TypeCode = VmOutCheck::TypeFloat;
            d.TypeText = VmOutCheck::TypeName(VmOutCheck::TypeFloat);
            d.Numeric = true;
            r.Declared.push_back(std::move(d));
        }

        // Module Script giả — bind đủ, để bản mô phỏng ra báo cáo sạch.
        auto scripts = VmScriptContract::Build(n);
        auto script = std::find_if(scripts.begin(), scripts.end(), [&](const VmScriptExpect& e) { return e.Flow == flow; });
        if (script != scripts.end())
        {
            VmModuleIo m;
            m.Name = script->ScriptName;
            for (const auto& i : script->Inputs)
            {
                VmIoInfo io;
                io.Name = i;
                io.TypeText = "float";
                io.Source = Loc::T("VmCycle.SimSource");
                m.Inputs.push_back(std::move(io));
            }
            for (const auto& o : script->Outputs)
            {
                VmIoInfo io;
                io.Name = o;
                io.TypeText = "float";
                m.Outputs.push_back(std::move(io));
            }
            r.Modules.push_back(std::move(m));
        }
        return r;
    }

    // ───────────────────────── đọc kết quả Flow có camera ─────────────────────────

    VmCameraRead VmCycle::ReadLocate(const VmCallResult& r, const std::map<std::string, double>& sent, const VisionOutputNames& n)
    {
        VmCameraRead c;
        c.AxX = Val(sent, n.GvAxisX);
        c.AxY = Val(sent, n.GvAxisY2);
        // C# r == null → "không có kết quả"; bản C++ nhận kết quả theo giá trị nên lỗi rỗng đứng vai đó.
        if (!r.Ok) { c.CallError = r.Error.empty() ? Loc::T("VmCycle.NoResult") : r.Error; return c; }
        c.CallOk = true;

        double rid = Val(sent, VmVars::RunId);
        std::vector<std::string> echo;
        CheckEcho(r, VmOut::EchoRunId, rid, 0.5, echo);
        CheckEcho(r, VmOut::EchoX, Val(sent, n.GvAxisX), VmMath::EchoTol, echo);
        CheckEcho(r, VmOut::EchoY2, Val(sent, n.GvAxisY2), VmMath::EchoTol, echo);
        c.EchoOk = echo.empty();
        c.EchoIssue = Join(echo);

        c.Found = r.Get(n.OutFound) > 0.5;

        std::vector<std::string> vm = echo;
        if (c.Found)
        {
            if (!VmMath::SameInt(r.Get(VmOut::TopFound), 1)) vm.push_back("TopFound ≠ 1");
            if (!VmMath::Near(r.Get(VmOut::TopRunId), rid, 0.5)) vm.push_back(Loc::F("VmCycle.RunIdNotReached", "TopRunId"));

            c.X = r.Get(VmOut::TopX);
            c.Y = r.Get(VmOut::TopY);
            c.A = r.Get(VmOut::TopA);
            if (!VmMath::IsFinite(c.X) || !VmMath::IsFinite(c.Y) || !VmMath::IsFinite(c.A))
                vm.push_back(Loc::F("VmCycle.PoseNotFinite", VmOut::TopX, VmOut::TopY, VmOut::TopA));
        }
        c.VmValid = vm.empty();
        c.VmIssue = Join(vm);
        return c;
    }

    VmCameraRead VmCycle::ReadAlignBottom(const VmCallResult& r, const VmCallRequest& req, const VisionOutputNames& n, int tool)
    {
        VmCameraRead c;
        if (!r.Ok) { c.CallError = r.Error.empty() ? Loc::T("VmCycle.NoResult") : r.Error; return c; }
        c.CallOk = true;

        const auto& sent = req.Globals;
        double rid = Val(sent, VmVars::RunId);
        double aSent = Val(sent, tool == 1 ? n.GvAxisA1 : n.GvAxisA2);
        std::vector<std::string> echo;
        CheckEcho(r, VmOut::EchoRunId, rid, 0.5, echo);
        CheckEcho(r, VmOut::EchoX, Val(sent, n.GvAxisX), VmMath::EchoTol, echo);
        CheckEcho(r, VmOut::EchoA, aSent, VmMath::EchoTol, echo);
        c.EchoOk = echo.empty();
        c.EchoIssue = Join(echo);

        c.Found = r.Get(n.OutFound) > 0.5;

        std::vector<std::string> vm = echo;
        if (c.Found)
        {
            if (!VmMath::SameInt(r.Get(VmOut::ObjFound), 1)) vm.push_back("ObjFound ≠ 1");
            if (!VmMath::Near(r.Get(VmOut::ObjRunId), rid, 0.5)) vm.push_back(Loc::F("VmCycle.RunIdNotReached", "ObjRunId"));
            c.X = r.Get(VmOut::ObjX);
            c.Y = r.Get(VmOut::ObjY);
            c.A = r.Get(VmOut::ObjA);
            if (!VmMath::IsFinite(c.X) || !VmMath::IsFinite(c.Y) || !VmMath::IsFinite(c.A))
                vm.push_back(Loc::F("VmCycle.PoseNotFinite", VmOut::ObjX, VmOut::ObjY, VmOut::ObjA));
        }
        c.VmValid = vm.empty();
        c.VmIssue = Join(vm);
        return c;
    }

    // ───────────────────────── hàm chặn lệnh đặt (mục 3.4) ─────────────────────────

    VmPlaceCommand VmCycle::CheckPlace(const VmCallResult& r, const VmCallRequest& req, int tool, double aSent,
                                       double cellX, double cellY, double guardMm, double maxAngleDeg, double maxEccMm,
                                       const std::function<bool(double)>& xInLimit, const std::function<bool(double)>& yInLimit,
                                       bool checkGrid)
    {
        VmPlaceCommand c;
        c.Route = EVmRoute::Error;
        const std::string flow = "AlignBottom_Tool" + std::to_string(tool) + ": ";
        const auto& sent = req.Globals;

        // a — đọc được, đúng lần chạy, đúng số vừa gửi
        if (!r.Ok) return Fail(c, flow + (r.Error.empty() ? Loc::T("VmCycle.NoResult") : r.Error));
        if (!VmMath::Near(r.Get(VmOut::EchoRunId), Val(sent, VmVars::RunId), 0.5))
            return Fail(c, flow + Loc::T("VmCycle.EchoRunIdMismatch"));
        if (!VmMath::Near(r.Get(VmOut::ObjRunId), Val(sent, VmVars::RunId), 0.5))
            return Fail(c, flow + Loc::T("VmCycle.ObjRunIdMismatch"));

        // b — Status
        c.Status = r.GetInt(VmOut::Status);
        c.DiagDA = r.Get(VmOut::DiagDA);
        c.DiagEcc = r.Get(VmOut::DiagEcc);
        if (c.Status != VmStatus::Ok) return Fail(c, flow + VmStatus::Place(c.Status));

        // c — số hữu hạn, góc trong dải
        c.CmdX = r.Get(VmOut::CmdX);
        c.CmdY2 = BottomYSign * r.Get(VmOut::CmdY2);        // Group Cmd tính trong hệ camera dưới — xem BottomYSign
        c.CmdA = r.Get(VmOut::CmdA);
        c.DiagTarX = r.Get(VmOut::DiagTarX);
        c.DiagTarY = BottomYSign * r.Get(VmOut::DiagTarY);
        if (!VmMath::IsFinite(c.CmdX) || !VmMath::IsFinite(c.CmdY2) || !VmMath::IsFinite(c.CmdA) || !VmMath::IsFinite(c.DiagTarX) ||
            !VmMath::IsFinite(c.DiagTarY) || !VmMath::IsFinite(c.DiagDA) || !VmMath::IsFinite(c.DiagEcc))
            return Fail(c, flow + Loc::T("VmCycle.CmdNotFinite"));
        if (c.CmdA < 0 || c.CmdA > 360) return Fail(c, flow + Loc::F("VmCycle.CmdAOutOfRange", VmOut::CmdA, Fmt(c.CmdA)));

        // e — ngưỡng góc bù và lệch tâm (N)
        if (!VmMath::IsFinite(maxAngleDeg) || std::fabs(c.DiagDA) > maxAngleDeg)
            return Ng(c, Loc::F("VmCycle.NgAngle", c.DiagDA, maxAngleDeg, c.DiagEcc));
        if (!VmMath::IsFinite(maxEccMm) || c.DiagEcc > maxEccMm)
            return Ng(c, Loc::F("VmCycle.NgEcc", c.DiagEcc, maxEccMm, c.DiagDA));

        // f — chặn theo lưới teach của đúng tay (G)
        if (checkGrid)
        {
            double ex = c.DiagTarX - cellX, ey = c.DiagTarY - cellY;
            if (std::fabs(ex) > guardMm || std::fabs(ey) > guardMm)
                return Skip(c, Loc::F("VmCycle.SkipTargetOffGrid", ex, ey, guardMm));
            double lim = guardMm + maxEccMm;
            double cx = c.CmdX - cellX, cy = c.CmdY2 - cellY;
            if (std::fabs(cx) > lim || std::fabs(cy) > lim)
                return Skip(c, Loc::F("VmCycle.SkipCmdOffGrid", cx, cy, lim));
        }

        // g — soft limit trên đúng tham số sẽ truyền cho trục
        if (xInLimit && !xInLimit(c.CmdX)) return Fail(c, flow + Loc::F("VmCycle.CmdXSoftLimit", Fmt(c.CmdX)));
        if (yInLimit && !yInLimit(c.CmdY2)) return Fail(c, flow + Loc::F("VmCycle.CmdY2SoftLimit", Fmt(c.CmdY2)));

        // h — lưới an toàn góc (không phải ngưỡng thứ hai). Góc lúc chụp là số App vừa ghi vào biến trục.
        if (!VmMath::IsFinite(aSent) || AxisAngle::Distance(aSent, c.CmdA) > maxAngleDeg + 1)
            return Fail(c, flow + Loc::F("VmCycle.CmdAFarFromCapture", c.CmdA, Fmt(aSent)));

        c.Route = EVmRoute::Ok;
        return c;
    }

    // ───────────────────────── Bước 4 — App tự phán ─────────────────────────

    VmAlignJudge VmCycle::JudgeAlignSign(const VmMeanCapture& s0, const VmMeanCapture& s1,
                                         double tolMm, double angleTolDeg, double minEccMm, double minTurnDeg)
    {
        VmAlignJudge j;

        // Single Point Alignment: Demonstrate = tư thế trước khi xoay, Run = tư thế sau khi xoay
        double dx = 0, dy = 0, raw = 0;
        SinglePointAlignMath::Solve(s0.X, s0.Y, s0.A, s1.X, s1.Y, s1.A, dx, dy, raw);
        j.RawDTheta = SinglePointAlignMath::NormalizeDeg(raw);
        j.EncDA = SinglePointAlignMath::NormalizeDeg(s1.AxA - s0.AxA);
        j.Residual = std::sqrt(dx * dx + dy * dy);
        j.Ecc = std::sqrt(s0.X * s0.X + s0.Y * s0.Y);

        if (!(VmMath::IsFinite(j.RawDTheta) && VmMath::IsFinite(j.Residual) && VmMath::IsFinite(j.Ecc) && VmMath::IsFinite(j.EncDA)))
            j.Status = VmStatus::Invalid;
        else if (std::fabs(s1.AxX - s0.AxX) > 0.01) j.Status = VmStatus::AxisMoved;
        else if (std::fabs(j.EncDA) < minTurnDeg) j.Status = VmStatus::Code8;
        else if (std::fabs(std::fabs(j.RawDTheta) - std::fabs(j.EncDA)) > angleTolDeg) j.Status = VmStatus::AngleOut;
        else if (j.Residual > tolMm) j.Status = VmStatus::EccOut;
        else if (j.Ecc < minEccMm) j.Status = VmStatus::NoConclusion;
        else j.Status = VmStatus::Ok;

        // Dấu tính theo encoder nên góc thử âm vẫn ra đúng dấu.
        j.Sign = (j.Status == VmStatus::Ok || j.Status == VmStatus::NoConclusion)
            ? -Sign(j.EncDA) * Sign(j.RawDTheta) : 0;
        return j;
    }

    // ───────────────────────── Bước 2 — App tự phán ─────────────────────────

    VmTopDownJudge VmCycle::JudgeTopDown(const VmMeanCapture& s0, const VmMeanCapture& s1, const VmMeanCapture& s2, double tolMm)
    {
        VmTopDownJudge j;
        j.StepX = s1.AxX - s0.AxX;
        j.StepY = s2.AxY - s0.AxY;
        j.ErrX = s1.X - s0.X;
        j.CrossX = s1.Y - s0.Y;
        j.ErrY = s2.Y - s0.Y;
        j.CrossY = s2.X - s0.X;

        bool finite = VmMath::IsFinite(j.ErrX) && VmMath::IsFinite(j.ErrY) && VmMath::IsFinite(j.CrossX) &&
                      VmMath::IsFinite(j.CrossY) && VmMath::IsFinite(j.StepX) && VmMath::IsFinite(j.StepY) && VmMath::IsFinite(tolMm);
        if (!finite) j.Status = VmStatus::Invalid;
        else if (std::fabs(s1.AxY - s0.AxY) > 0.01 || std::fabs(s2.AxX - s0.AxX) > 0.01) j.Status = VmStatus::AxisMoved;
        else if (std::fabs(j.StepX) < 0.5 || std::fabs(j.StepY) < 0.5) j.Status = VmStatus::NoConclusion;
        else if (std::fabs(j.ErrX) >= std::fabs(j.StepX) || std::fabs(j.ErrY) >= std::fabs(j.StepY)) j.Status = VmStatus::Code8;
        else if (std::fabs(j.ErrX) > tolMm || std::fabs(j.ErrY) > tolMm || std::fabs(j.CrossX) > tolMm || std::fabs(j.CrossY) > tolMm)
            j.Status = VmStatus::EccOut;
        else j.Status = VmStatus::Ok;
        return j;
    }

    // ───────────────────────── cổng hiệu chuẩn phía App ─────────────────────────

    VmCalibGate VmCycle::EvaluateCalib(const ModelData* model, const SystemData* sys, int nozzle)
    {
        VmCalibGate g;
        if (model == nullptr || sys == nullptr) { g.Block = Loc::T("VmCycle.GateNoModel"); return g; }

        const VisionCalib& c = model->Calib;
        std::vector<std::string> why;
        std::vector<int> tools = nozzle == 1 || nozzle == 2 ? std::vector<int>{ nozzle } : std::vector<int>{ 1, 2 };
        if (!c.IsTopDownVerified()) why.push_back(Loc::T("VmCycle.GateTopDown"));
        for (int n : tools)
            if (!c.IsAlignVerified(n)) why.push_back(Loc::F("VmCycle.GateNoSign", n));
        for (int n : tools)
        {
            if (c.IsHandEyeOutdated(n))
                why.push_back(Loc::F("VmCycle.GateHandEyeOld", n));
            else if (!c.IsHandEyeMeasured(n)) why.push_back(Loc::F("VmCycle.GateHandEyeMissing", n));
        }

        bool thresholdsOk = sys->MaxCompensationAngleDeg > 0 && sys->MaxCompensationAngleDeg <= 45 &&
                            sys->MaxCompensationDistance > 0 && sys->MaxCompensationDistance <= 10 &&
                            sys->AlignXTolMm > 0 && sys->AlignXTolMm <= 1;
        if (!thresholdsOk) why.push_back(Loc::T("VmCycle.GateThresholds"));

        for (int n : tools)
        {
            if (!c.IsAlignVerified(n) || !thresholdsOk) continue;
            double taught = model->AlignPosX(n), verified = c.AlignVerifiedPosX(n);
            if (std::fabs(taught - verified) > sys->AlignXTolMm)
                why.push_back(Loc::F("VmCycle.GateAlignPosMoved", n, taught, verified, sys->AlignXTolMm));
        }

        // Ngày cũ nhất trong bản ghi: Bước 2 dùng chung, Bước 4 và 5 của từng tay được xét.
        double oldest = NaN;
        auto consider = [&oldest](double days)
        {
            if (std::isnan(days)) return;
            oldest = std::isnan(oldest) ? days : std::max(oldest, days);
        };
        consider(DaysSince(c.TopDownVerifiedAt));
        for (int n : tools)
        {
            consider(n == 1 ? DaysSince(c.AlignVerifiedAt1) : DaysSince(c.AlignVerifiedAt2));
            consider(n == 1 ? DaysSince(c.HandEye1MeasuredAt) : DaysSince(c.HandEye2MeasuredAt));
        }
        g.DaysSinceOldest = oldest;

        g.HandEyeDx1 = c.HandEyeDx(1); g.HandEyeDy1 = c.HandEyeDy(1); g.AngleRef1 = c.HandEyeAngleRef(1);
        g.HandEyeDx2 = c.HandEyeDx(2); g.HandEyeDy2 = c.HandEyeDy(2); g.AngleRef2 = c.HandEyeAngleRef(2);
        g.AlignSign1 = c.AlignAngleSign(1); g.AlignSign2 = c.AlignAngleSign(2);

        g.Block = Join(why);
        return g;
    }

    // ───────────────────────── vết một lần kích AlignBottom ─────────────────────────

    VmAlignTrace VmCycle::TraceAlignBottom(const VmCallResult& r, const VmCallRequest& req, const VisionOutputNames& n, int tool, double zPos)
    {
        VmAlignTrace t;
        const auto& g = req.Globals;
        const std::string& aName = tool == 2 ? n.GvAxisA2 : n.GvAxisA1;
        std::string sb = Loc::Format("{0} RunId {1}: axis X {2}, Y2 {3}, {4} {5}°, {6} {7}",
            { req.Flow.empty() ? std::string("AlignBottom") : req.Flow, Fmt4(Val(g, VmVars::RunId)), Fmt4(Val(g, n.GvAxisX)),
              Fmt4(Val(g, n.GvAxisY2)), aName, Fmt4(Val(g, aName)),
              std::string(tool == 2 ? AxisId::Z2 : AxisId::Z1), Fmt4(zPos) });

        sb += " | App wrote:";
        for (const auto& kv : g)
            if (kv.first != n.GvAxisX && kv.first != n.GvAxisY2 && kv.first != n.GvAxisA1 && kv.first != n.GvAxisA2 &&
                kv.first != VmVars::RunId)
                sb += " " + kv.first + " " + Fmt4(kv.second) + ",";
        if (!sb.empty() && sb.back() == ',') sb.pop_back();

        if (!r.Ok)
        {
            sb += " | Flow call error: " + (r.Error.empty() ? std::string("no result") : r.Error);
            t.Line = sb;
            return t;
        }
        sb += " | Flow returned:";
        std::vector<std::string> names;
        for (const auto& o : req.Outputs) if (!ContainsName(names, o)) names.push_back(o);
        for (const auto& o : req.OptionalOutputs) if (!ContainsName(names, o)) names.push_back(o);
        for (const auto& o : names)
            sb += " " + o + " " + (r.Has(o) ? Fmt4(r.Get(o)) : std::string("(missing)")) + ",";
        if (!sb.empty() && sb.back() == ',') sb.pop_back();

        // ── App tính lại Group Cmd ──
        double hasTarget = Val(g, VmVars::HasTarget);
        if (!(hasTarget >= 0.5)) { sb += " | Group Cmd idle (HasTarget = 0)"; t.Line = sb; return t; }
        if (!VmMath::SameInt(r.Get(VmOut::ObjFound), 1)) { sb += " | not checked: no product found"; t.Line = sb; return t; }
        if (!VmMath::SameInt(r.Get(VmOut::Status), VmStatus::Ok))
        {
            sb += " | not checked: Group Cmd Status " + Fmt4(r.Get(VmOut::Status));
            t.Line = sb;
            return t;
        }

        double tarX = Val(g, VmVars::CellX) + Val(g, VmVars::HandEyeDx);
        double tarY = Val(g, VmVars::CellY) + Val(g, VmVars::HandEyeDy);
        double tarA = Val(g, VmVars::CellA);
        double ox = r.Get(VmOut::ObjX), oy = r.Get(VmOut::ObjY), oa = r.Get(VmOut::ObjA);
        double dx = 0, dy = 0, dt = 0;
        SinglePointAlignMath::Solve(tarX, tarY, tarA, ox, oy, oa, dx, dy, dt);
        // C# (int)Math.Round — làm tròn về số chẵn khi đúng .5; dấu góc chỉ là ±1/0 nên không gặp.
        int sign = (int)std::nearbyint(Val(g, VmVars::AlignSign));
        double dA = SinglePointAlignMath::NormalizeDeg(sign * dt);
        double cmdA = AxisAngle::Wrap(Val(g, aName) + dA);
        double ecc = std::sqrt(ox * ox + oy * oy);

        t.Checked = true;
        t.ErrMm = std::max({ std::fabs(r.Get(VmOut::CmdX) - dx), std::fabs(r.Get(VmOut::CmdY2) - dy),
                             std::fabs(r.Get(VmOut::DiagTarX) - tarX), std::fabs(r.Get(VmOut::DiagTarY) - tarY),
                             std::fabs(r.Get(VmOut::DiagEcc) - ecc) });
        t.ErrDeg = std::max(AxisAngle::Distance(r.Get(VmOut::CmdA), cmdA),
                            std::fabs(SinglePointAlignMath::NormalizeDeg(r.Get(VmOut::DiagDA) - dA)));
        if (!VmMath::IsFinite(t.ErrMm)) t.ErrMm = std::numeric_limits<double>::infinity();
        if (!VmMath::IsFinite(t.ErrDeg)) t.ErrDeg = std::numeric_limits<double>::infinity();
        t.Match = t.ErrMm <= CmdCheckTolMm && t.ErrDeg <= CmdCheckTolDeg;

        // So trong hệ camera dưới (đúng số Flow thấy), nhưng in đích và lệnh theo chiều máy — xem BottomYSign.
        std::string verdict = t.Match
            ? Loc::F("VmCycle.CheckMatch", t.ErrMm, t.ErrDeg)
            : Loc::F("VmCycle.CheckMismatch", Fmt4(r.Get(VmOut::CmdX)), Fmt4(r.Get(VmOut::CmdY2)), Fmt4(r.Get(VmOut::CmdA)),
                     Fmt4(r.Get(VmOut::DiagDA)), t.ErrMm, t.ErrDeg);
        t.Check = Loc::F("VmCycle.CheckRecalc", tarX, BottomYSign * tarY, tarA, dt, sign, dA, dx, BottomYSign * dy, cmdA, verdict);
        sb += " | " + t.Check;
        t.Line = sb;
        return t;
    }

    // ───────────────────────── tiện ích ─────────────────────────

    double VmCycle::Val(const std::map<std::string, double>& d, const std::string& key)
    {
        auto it = d.find(key);
        return it != d.end() ? it->second : NaN;
    }

    double VmCycle::MeanAngleDeg(double sumSin, double sumCos)
    {
        return std::atan2(sumSin, sumCos) * 180.0 / Pi;
    }

    std::string VmCycle::Fmt(double v)
    {
        if (!VmMath::IsFinite(v))
            return std::isnan(v) ? Loc::T("VmCycle.NaNMissing") : std::string(v > 0 ? "Infinity" : "-Infinity");
        return Loc::FormatValue(FmtArg(v), "0.###");
    }
}
