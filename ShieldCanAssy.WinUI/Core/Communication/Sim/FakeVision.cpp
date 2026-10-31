#include "Communication/Sim/FakeVision.h"

#include "Base/Async.h"
#include "Base/Text.h"
#include "Controller/SinglePointAlignMath.h"
#include "Data/Model/ModelData.h"
#include "Lang/Loc.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace sca
{
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;
        const double NaN = std::numeric_limits<double>::quiet_NaN();

        double F32(double v) { return static_cast<float>(v); }
        bool Fin(double v) { return !std::isnan(v) && !std::isinf(v); }
        double N180(double a) { return a - 360.0 * std::ceil((a - 180.0) / 360.0); }

        double W360(double a)
        {
            double r = a - 360.0 * std::floor(a / 360.0);
            return r >= 360.0 - 1e-9 ? 0 : r;
        }

        /// ToString("R", InvariantCulture): chuỗi ngắn nhất đọc lại ra đúng số đó.
        template <class T>
        std::string RoundTrip(T v)
        {
            if (std::isnan(v)) return "NaN";
            if (std::isinf(v)) return v > 0 ? "Infinity" : "-Infinity";
            char buf[64];
            auto r = std::to_chars(buf, buf + sizeof(buf), v);
            return std::string(buf, r.ptr);
        }

        /// double.TryParse(NumberStyles.Float, InvariantCulture); không đọc được thì NaN.
        double ParseInvariant(const std::string& text)
        {
            std::string t = base::Trim(text);
            if (t.empty()) return NaN;
            if (t == "NaN") return NaN;
            if (t == "Infinity") return std::numeric_limits<double>::infinity();
            if (t == "-Infinity") return -std::numeric_limits<double>::infinity();
            const char* first = t.data();
            const char* last = t.data() + t.size();
            if (*first == '+') ++first;
            double d = 0;
            auto r = std::from_chars(first, last, d);
            return r.ec == std::errc() && r.ptr == last ? d : NaN;
        }

        enum class EFlowKind { None, LocateTop, AlignBottom };

        /// Phân loại Flow theo tên đang cấu hình (FlowKind của bản C#: "LocateTop" / "AlignBottom" / null).
        EFlowKind FlowKind(const VisionOutputNames& n, const std::string& flow, int& tool)
        {
            tool = 0;
            if (flow == n.FlowLocateTop) return EFlowKind::LocateTop;
            for (int t = 1; t <= 2; t++)
            {
                tool = t;
                if (flow == (t == 1 ? n.FlowAlignBottomTool1 : n.FlowAlignBottomTool2)) return EFlowKind::AlignBottom;
            }
            tool = 0;
            return EFlowKind::None;
        }
    }

    // ── System.Random của .NET Framework (bộ sinh trừ của Knuth) — cùng hạt giống thì cùng dãy số với bản C# ──

    FakeVision::DotNetRandom::DotNetRandom(int seed)
    {
        const int MBIG = 2147483647, MSEED = 161803398;
        int subtraction = seed == std::numeric_limits<int>::min() ? MBIG : std::abs(seed);
        int mj = MSEED - subtraction;
        m_seedArray[55] = mj;
        int mk = 1;
        for (int i = 1; i < 55; i++)
        {
            int ii = (21 * i) % 55;
            m_seedArray[ii] = mk;
            mk = mj - mk;
            if (mk < 0) mk += MBIG;
            mj = m_seedArray[ii];
        }
        for (int k = 1; k < 5; k++)
        {
            for (int i = 1; i < 56; i++)
            {
                // Bản .NET trừ kiểu int tràn số (unchecked) — làm qua unsigned để hành vi xác định.
                m_seedArray[i] = static_cast<int>(static_cast<unsigned>(m_seedArray[i]) - static_cast<unsigned>(m_seedArray[1 + (i + 30) % 55]));
                if (m_seedArray[i] < 0) m_seedArray[i] += MBIG;
            }
        }
        m_inext = 0;
        m_inextp = 21;
    }

    double FakeVision::DotNetRandom::NextDouble()
    {
        const int MBIG = 2147483647;
        int locINext = m_inext, locINextp = m_inextp;
        if (++locINext >= 56) locINext = 1;
        if (++locINextp >= 56) locINextp = 1;
        int retVal = m_seedArray[locINext] - m_seedArray[locINextp];
        if (retVal == MBIG) retVal--;
        if (retVal < 0) retVal += MBIG;
        m_seedArray[locINext] = retVal;
        m_inext = locINext;
        m_inextp = locINextp;
        return retVal * (1.0 / MBIG);
    }

    FakeVision::FakeVision() : m_rng(20260731) {}

    std::optional<AxisSnapshot> FakeVision::LastCalibAxes() const { std::lock_guard<std::recursive_mutex> l(m_lock); return m_lastCalibAxes; }
    std::optional<EVisionProcedure> FakeVision::LastCalibClear() const { std::lock_guard<std::recursive_mutex> l(m_lock); return m_lastCalibClear; }
    std::optional<std::map<std::string, double>> FakeVision::LastPlaceGlobals() const { std::lock_guard<std::recursive_mutex> l(m_lock); return m_lastPlaceGlobals; }
    void FakeVision::SetForcedObjPose(std::optional<std::array<double, 3>> pose) { std::lock_guard<std::recursive_mutex> l(m_lock); m_forcedObjPose = pose; }
    std::optional<std::array<double, 3>> FakeVision::ForcedObjPose() const { std::lock_guard<std::recursive_mutex> l(m_lock); return m_forcedObjPose; }
    void FakeVision::SetNames(std::function<VisionOutputNames()> names) { std::lock_guard<std::recursive_mutex> l(m_lock); m_names = std::move(names); }
    void FakeVision::SetFlowOutputSource(std::function<VmFlowOutputs(const std::string&)> source) { std::lock_guard<std::recursive_mutex> l(m_lock); m_flowOutputSource = std::move(source); }
    void FakeVision::SetAfterFlow(std::function<void(const std::string&)> afterFlow) { std::lock_guard<std::recursive_mutex> l(m_lock); m_afterFlow = std::move(afterFlow); }

    bool FakeVision::Connect() { m_connected = true; return true; }

    void FakeVision::Disconnect() { m_connected = false; m_loaded = false; }

    bool FakeVision::LoadSolution(const std::string&)
    {
        base::Delay(200);
        m_loaded = m_connected.load();
        return m_loaded.load();
    }

    VisionPose FakeVision::TriggerCalib(EVisionProcedure procedure, const AxisSnapshot& axes, std::stop_token ct)
    {
        // (axes == null của bản C# không có ở đây: AxisSnapshot là giá trị, không bao giờ rỗng.)
        if (!IsConnected()) return VisionPose::NotFound(Loc::T("Vm.NotConnected"));
        if (!IsSolutionLoaded()) return VisionPose::NotFound(Loc::T("Vm.NoSolution"));

        std::function<void(const std::string&)> afterFlow;
        {
            std::lock_guard<std::recursive_mutex> l(m_lock);
            m_lastCalibAxes = axes;
            afterFlow = m_afterFlow;
            // Như VisionConnect.TriggerCalib: nhánh hiệu chuẩn của Flow (ba Flow, 2026-10-08).
            SetG(VmVars::IsTriggerLocate, 0);
        }
        ++m_calibPointCount;
        ++m_calibPointsSinceClear;
        if (afterFlow) afterFlow(ToString(procedure));   // bài tự kiểm soi trạng thái máy NGAY LÚC chụp (đèn CAM)
        base::Delay(ProcessTimeMs(), ct);

        std::lock_guard<std::recursive_mutex> l(m_lock);
        VisionPose found;
        found.Found = true;
        return m_rng.NextDouble() <= FoundRate() ? found : VisionPose::NotFound(Loc::T("Vm.SimObjectNotFound"));
    }

    std::string FakeVision::ClearCalibPoints(EVisionProcedure procedure)
    {
        if (!IsSolutionLoaded()) return Loc::T("Vm.NoSolution");
        if (FailCalibClear()) return Loc::T("Vm.SimClearFail");
        ++m_calibClearCount;
        {
            std::lock_guard<std::recursive_mutex> l(m_lock);
            m_lastCalibClear = procedure;
        }
        m_calibPointsSinceClear = 0;
        return {};
    }

    std::map<std::string, FakeGlobalVar> FakeVision::GlobalVarsSnapshot() const { std::lock_guard<std::recursive_mutex> l(m_lock); return m_globalVars; }

    void FakeVision::PutGlobalVar(const std::string& name, const std::string& type, const std::string& value)
    {
        std::lock_guard<std::recursive_mutex> l(m_lock);
        m_globalVars[name] = FakeGlobalVar{ type, value };
    }

    void FakeVision::RemoveGlobalVar(const std::string& name) { std::lock_guard<std::recursive_mutex> l(m_lock); m_globalVars.erase(name); }
    void FakeVision::ClearGlobalVars() { std::lock_guard<std::recursive_mutex> l(m_lock); m_globalVars.clear(); }

    GlobalVarSyncReport FakeVision::SyncGlobalVariables(const std::vector<GlobalVarSpec>& specs, bool apply)
    {
        if (!IsSolutionLoaded()) return GlobalVarSyncReport::Fail(Loc::T("Vm.NoSolution"));

        /// Kho biến giả — FakeGlobalVarStore của bản C#. Thông báo lỗi giữ nguyên chữ của SDK VisionMaster.
        struct Store : IGlobalVarStore
        {
            FakeVision& v;
            explicit Store(FakeVision& fv) : v(fv) {}

            std::map<std::string, std::string> ReadAll() override
            {
                std::map<std::string, std::string> d;
                for (auto& kv : v.m_globalVars) d[kv.first] = kv.second.Type;
                return d;
            }

            void Add(const std::string& name, const std::string& type, const std::string&) override
            {
                if (v.m_globalVars.count(name)) throw std::runtime_error("name is already exit");
                v.m_globalVars[name] = FakeGlobalVar{ type, type == "string" ? "" : "0" };
            }

            void SetValue(const std::string& name, const std::string&, const std::string& invariantValue) override
            {
                auto it = v.m_globalVars.find(name);
                if (it == v.m_globalVars.end()) throw std::runtime_error("name is not exit");
                it->second.Value = invariantValue;
            }

            std::string Save() override { ++v.m_solutionSaveCount; return ""; }
        };

        std::lock_guard<std::recursive_mutex> l(m_lock);
        Store store(*this);
        return GlobalVarSync::Run(&store, specs, apply);
    }

    VmFlowOutputs FakeVision::ReadFlowOutputs(const std::string& flow)
    {
        if (!IsSolutionLoaded()) return VmFlowOutputs::Fail(flow, Loc::T("Vm.NoSolution"));
        std::function<VmFlowOutputs(const std::string&)> source;
        { std::lock_guard<std::recursive_mutex> l(m_lock); source = m_flowOutputSource; }
        if (!source) return VmFlowOutputs::Fail(flow, Loc::T("Vm.SimNoOutputSource"));
        return source(flow);
    }

    std::string FakeVision::SaveSolution()
    {
        if (!IsSolutionLoaded()) return Loc::T("Vm.NoSolution");
        ++m_solutionSaveCount;
        return {};
    }

    double FakeVision::Noise(double amplitude) { return (m_rng.NextDouble() - 0.5) * 2 * amplitude; }

    // ═════════════════════════ bản sao Script của solution ═════════════════════════

    void FakeVision::StampCalibrated(VisionCalib* calib)
    {
        if (calib != nullptr) calib->TopDownVerifiedAt = std::chrono::system_clock::now();
    }

    void FakeVision::SeedVmSolution(const std::vector<GlobalVarSpec>& specs)
    {
        std::lock_guard<std::recursive_mutex> l(m_lock);
        m_globalVars.clear();
        // s.InitialValue ?? (...) của bản C#: chuỗi rỗng đứng vai null.
        for (auto& s : specs)
            m_globalVars[s.Name] = FakeGlobalVar{ s.Type, !s.InitialValue.empty() ? s.InitialValue : (s.Type == "string" ? "" : "0") };
    }

    double FakeVision::GetGlobal(const std::string& name) const { std::lock_guard<std::recursive_mutex> l(m_lock); return G(name); }
    void FakeVision::SetGlobal(const std::string& name, double value) { std::lock_guard<std::recursive_mutex> l(m_lock); SetG(name, value); }

    std::vector<std::string> FakeVision::FlowHistorySnapshot() const { std::lock_guard<std::recursive_mutex> l(m_lock); return m_flowHistory; }
    void FakeVision::ClearFlowHistory() { std::lock_guard<std::recursive_mutex> l(m_lock); m_flowHistory.clear(); }

    VmCallResult FakeVision::CallFlow(const VmCallRequest& req, std::stop_token ct)
    {
        if (req.Flow.empty()) return VmCallResult::Fail(Loc::T("Vm.FlowNameMissing"));
        if (!IsConnected()) return VmCallResult::Fail(Loc::T("Vm.NotConnected"));
        if (!IsSolutionLoaded()) return VmCallResult::Fail(Loc::T("Vm.NoSolution"));

        std::function<VisionOutputNames()> names;
        std::function<void(const std::string&)> afterFlow;
        { std::lock_guard<std::recursive_mutex> l(m_lock); names = m_names; afterFlow = m_afterFlow; }
        VisionOutputNames n = names ? names() : VisionOutputNames();
        int tool = 0;
        EFlowKind kind = FlowKind(n, req.Flow, tool);
        if (kind == EFlowKind::None) return VmCallResult::Fail(Loc::F("Vm.SimFlowNotFound", req.Flow));

        base::Delay(ProcessTimeMs(), ct);   // cả ba Flow đều có camera

        std::map<std::string, double> o;
        {
            std::lock_guard<std::recursive_mutex> l(m_lock);
            for (auto& kv : req.Globals)
            {
                if (kv.first.empty()) return VmCallResult::Fail(Loc::T("Vm.SimVarNameEmpty"));
                if (!m_globalVars.count(kv.first))
                    return VmCallResult::Fail(Loc::F("Vm.SimVarNotDeclared", kv.first));
                if (!Fin(kv.second)) return VmCallResult::Fail(Loc::F("Vm.SimValueNotFinite", kv.first));
            }

            for (auto& kv : req.Globals) SetG(kv.first, kv.second);

            ++m_vmCallCount;
            m_flowHistory.push_back(req.Flow);
            if (m_flowHistory.size() > 5000) m_flowHistory.erase(m_flowHistory.begin());

            switch (kind)
            {
            case EFlowKind::LocateTop: RunTopCamera(n, o); break;
            case EFlowKind::AlignBottom: RunAlignBottom(n, o, tool); break;
            default: break;
            }
        }

        if (afterFlow) afterFlow(req.Flow);

        VmCallResult res;
        res.Ok = true;
        for (auto& name : req.Outputs)
        {
            auto it = o.find(name);
            if (it == o.end()) return VmCallResult::Fail(Loc::F("Vm.SimFlowMissingOutput", req.Flow, name));
            res.Values[name] = it->second;
        }
        for (auto& name : req.OptionalOutputs)
        {
            auto it = o.find(name);
            if (it != o.end()) res.Values[name] = it->second;
            else res.MissingOptional.push_back(name);
        }
        return res;
    }

    // ── Flow có camera ──

    void FakeVision::RunTopCamera(const VisionOutputNames& n, std::map<std::string, double>& o)
    {
        ++m_locateTopCount;

        double x = G(n.GvAxisX), y2 = G(n.GvAxisY2), rid = G(VmVars::RunId);
        o[VmOut::EchoRunId] = F32(rid);
        o[VmOut::EchoX] = F32(x);
        o[VmOut::EchoY2] = F32(y2);

        bool found = m_rng.NextDouble() <= FoundRate();
        if (TopMissTimes() > 0) { SetTopMissTimes(TopMissTimes() - 1); found = false; }
        double px, py, pa;
        if (SimulateOptics())
        {
            // Dấu chuẩn (sản phẩm mẫu) đứng yên trong hệ máy: lượng dịch camera báo = vị trí dấu − vị trí trục.
            px = MarkX() - x + Noise(0.002);
            py = MarkY() - y2 + Noise(0.002);
            pa = MarkA() + Noise(0.01);
        }
        else
        {
            px = Noise(NoiseTopMm());
            py = Noise(NoiseTopMm());
            pa = Noise(NoiseAngle());
        }

        o[n.OutFound] = found ? 1 : 0;
        o[VmOut::TopFound] = found ? 1 : 0;
        o[VmOut::TopX] = F32(found ? x + px : 0);
        o[VmOut::TopY] = F32(found ? y2 + py : 0);
        o[VmOut::TopA] = F32(found ? pa : 0);
        o[VmOut::TopRunId] = F32(rid);
    }

    /// AlignBottom_ToolN: soi sản phẩm; nếu biến HasTarget = 1 thì Group Cmd tính luôn lệnh đặt lên ô đích trong
    /// cùng lần chạy — bản sao Script "Cmd" của cẩm nang: đích = pose ô + hand-eye App vừa ghi, Single Point
    /// Alignment, nhân dấu góc, A tuyệt đối = góc lúc chụp + góc bù. Ngưỡng không nằm ở đây — App so.
    void FakeVision::RunAlignBottom(const VisionOutputNames& n, std::map<std::string, double>& o, int tool)
    {
        ++m_alignBottomCount;

        double x = G(n.GvAxisX), a = G(tool == 1 ? n.GvAxisA1 : n.GvAxisA2), rid = G(VmVars::RunId);
        float echoA = static_cast<float>(a);
        o[VmOut::EchoRunId] = F32(rid);
        o[VmOut::EchoX] = F32(x);
        o[VmOut::EchoA] = echoA;

        bool found = m_rng.NextDouble() <= std::min(FoundRate(), BottomFoundRate());
        double ex = tool == 2 ? -SimEccentricityMm() : SimEccentricityMm();
        double ey = tool == 2 ? SimEccentricityMm() * 0.5 : -SimEccentricityMm() * 0.5;
        double ox, oy, oa;
        if (m_forcedObjPose)
        {
            ox = (*m_forcedObjPose)[0];
            oy = (*m_forcedObjPose)[1];
            oa = (*m_forcedObjPose)[2];
        }
        else if (SimulateOptics())
        {
            // Sản phẩm dính trên đầu hút, xoay cùng trục A quanh tâm xoay (gốc toạ độ của file bottom-up).
            double th = BottomAngleSign() * a * Pi / 180.0;
            ox = ex * std::cos(th) - ey * std::sin(th) + Noise(0.002);
            oy = ex * std::sin(th) + ey * std::cos(th) + Noise(0.002);
            oa = N180(BottomAngleSign() * a) + Noise(0.01);
        }
        else
        {
            ox = ex + Noise(NoiseAlignMm());
            oy = ey + Noise(NoiseAlignMm());
            oa = Noise(NoiseAngle());
        }

        o[n.OutFound] = found ? 1 : 0;
        o[VmOut::ObjFound] = found ? 1 : 0;
        o[VmOut::ObjX] = F32(found ? ox : 0);
        o[VmOut::ObjY] = F32(found ? oy : 0);
        o[VmOut::ObjA] = F32(found ? oa : 0);
        o[VmOut::ObjRunId] = F32(rid);

        if (!(G(VmVars::HasTarget) >= 0.5))
        {
            o[VmOut::Status] = 0;   // Group Cmd nghỉ — chỉ soi
            return;
        }

        // ── Group Cmd: module In đọc biến toàn cục, rồi Single Point Alignment, rồi Script Cmd ──
        ++m_placeComputeCount;
        std::map<std::string, double> placeGlobals;
        for (auto& name : VmVars::Place()) placeGlobals[name] = G(name);
        m_lastPlaceGlobals = placeGlobals;

        double heDx = G(VmVars::HandEyeDx), heDy = G(VmVars::HandEyeDy);
        double tarX = G(VmVars::CellX) + heDx, tarY = G(VmVars::CellY) + heDy, tarA = G(VmVars::CellA);
        double px = o[VmOut::ObjX], py = o[VmOut::ObjY], pa = o[VmOut::ObjA], aCap = echoA;

        double dx = 0, dy = 0, dt = 0;
        SinglePointAlignMath::Solve(tarX, tarY, tarA, px, py, pa, dx, dy, dt);
        dt = N180(dt);

        int s = Gi(VmVars::AlignSign);
        int st = 1;
        if (s != 1 && s != -1) st = 6;
        else if (!(found && Fin(dt) && Fin(dx) && Fin(dy) && Fin(aCap) && Fin(px) && Fin(py))) st = 6;

        double dA = Fin(dt) && (s == 1 || s == -1) ? N180(s * dt) : 0;
        if (ForceDAngleTimes() > 0) { SetForceDAngleTimes(ForceDAngleTimes() - 1); dA = ForceDAngleDeg(); }
        double ecc = std::sqrt(px * px + py * py);

        if (ForcePlaceStatusTimes() > 0) { SetForcePlaceStatusTimes(ForcePlaceStatusTimes() - 1); st = ForcePlaceStatus(); }
        double shift = 0;
        if (TargetShiftTimes() > 0) { SetTargetShiftTimes(TargetShiftTimes() - 1); shift = TargetShiftMm(); }

        o[VmOut::Status] = st;
        o[VmOut::CmdX] = F32(dx + shift);
        o[VmOut::CmdY2] = F32(dy);
        o[VmOut::CmdA] = F32(W360(aCap + dA));
        o[VmOut::DiagDA] = F32(dA);
        o[VmOut::DiagEcc] = F32(ecc);
        o[VmOut::DiagTarX] = F32(tarX + shift);
        o[VmOut::DiagTarY] = F32(tarY);

        if (CorruptEchoOnce())
        {
            SetCorruptEchoOnce(false);
            o[VmOut::EchoRunId] = o[VmOut::EchoRunId] + 1;
        }
    }

    // ── kho biến (gọi trong m_lock) ──

    double FakeVision::G(const std::string& name) const
    {
        auto it = m_globalVars.find(name);
        return it == m_globalVars.end() ? NaN : ParseInvariant(it->second.Value);
    }

    int FakeVision::Gi(const std::string& name) const
    {
        double d = G(name);
        return Fin(d) ? static_cast<int>(std::nearbyint(d)) : 0;
    }

    void FakeVision::SetG(const std::string& name, double value)
    {
        auto it = m_globalVars.find(name);
        if (it == m_globalVars.end()) return;
        const std::string& type = it->second.Type;
        it->second.Value = type == "int" ? std::to_string(static_cast<int>(std::nearbyint(value)))
                         : type == "string" ? RoundTrip(value) : RoundTrip(static_cast<float>(value));
    }
}
