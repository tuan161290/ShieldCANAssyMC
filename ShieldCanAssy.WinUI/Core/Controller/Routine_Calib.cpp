#include "Controller/Routine_Calib.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>

#include "Base/Async.h"
#include "Communication/AxisAngle.h"
#include "Controller/VisionReadout.h"
#include "Data/McDataManager.h"
#include "Infor/AlarmCode.h"
#include "Infor/AlarmManager.h"
#include "Infor/AxisId.h"
#include "Infor/IoMap.h"
#include "Lang/Loc.h"
#include "Utility/Log/LogWorker.h"

// Port của ShieldCanAssy.Core\Core.Controller\Routine_Calib.cs — xem PORTING.md, CONTROLLER-NOTES.md.
// Mọi hàm Task của bản C# là hàm CHẶN ở đây; trạng thái giữa các nút nằm dưới m_stateMutex, KHÔNG giữ khoá trong lúc
// chạy trục hay gọi Flow.

namespace sca
{
    namespace
    {
        constexpr double DoubleNaN = std::numeric_limits<double>::quiet_NaN();

        void Log(const std::string& msg, LogLevel level = LogLevel::Info) { LogWorker::Instance().RecordLog(msg, level, "Calib"); }

        TimePoint Now() { return std::chrono::system_clock::now(); }

        std::string RouteText(EVmRoute r) { return ToString(r); }

        std::function<bool(double)> SoftLimitOf(const std::shared_ptr<AxisConfig>& cfg)
        {
            return [cfg](double p) { return cfg->InSoftLimit(p); };
        }

        /// <summary>
        /// Vỏ của các hàm bool công khai (nạp một điểm lẻ, đưa A về 0°). Bản C# để exception lọt ra ViewModel; header
        /// C++ hứa "không ném" (trang gọi trên luồng nền), nên huỷ → false, lỗi → alarm sự kiện RoutineCrashed + false,
        /// cùng cách các chuỗi khác của lớp này xử lý.
        /// </summary>
        template <class F>
        bool NoThrowBool(F&& f)
        {
            try
            {
                return f();
            }
            catch (const base::OperationCanceled&)
            {
                return false;
            }
            catch (const std::exception& ex)
            {
                AlarmManager::RaiseEvent(AlarmCode::RoutineCrashed, "Calib", ex.what());
                return false;
            }
        }
    }

    Routine_Calib::Routine_Calib(MotionConnect& motion, McFunction& func, std::function<std::shared_ptr<SystemData>()> sys,
                                 std::function<std::shared_ptr<IVisionDevice>()> vision)
        : m_motion(motion), m_func(func), m_sys(std::move(sys)), m_vision(std::move(vision))
    {
    }

    // ───────────────────────── đèn CAM / Flow ─────────────────────────

    /// <summary>
    /// Bật đèn CAM tương ứng, <b>chờ đèn sáng đủ</b> (SystemData::LampMsFor), chạy việc cần camera, rồi TẮT đèn — kể cả
    /// khi Flow lỗi, quá giờ hay bị huỷ giữa lúc chờ đèn (RAII thay try/finally). Bottom-up là đèn CAM1, top-down là đèn
    /// CAM2; Flow chỉ tính thì không bật đèn nào và cũng không chờ. Trang hiệu chuẩn trước 2026-09-23 gọi thẳng vision
    /// nên các nút Trigger chụp trong lúc đèn tắt (Tuấn yêu cầu sửa); chuỗi Auto đã làm thế này từ 2026-09-04.
    /// </summary>
    template <class T>
    T Routine_Calib::WithCamLamp(EVmCamera cam, std::stop_token ct, const std::function<T()>& run)
    {
        int lampBit = cam == EVmCamera::BottomUp ? IoMap::DoLampCam1
                    : cam == EVmCamera::TopDown ? IoMap::DoLampCam2 : -1;
        if (lampBit >= 0)
            if (auto io = m_func.Io()) io->SetDo(lampBit, true);

        struct LampOff
        {
            McFunction& func;
            int bit;
            ~LampOff()
            {
                if (bit >= 0)
                    if (auto io = func.Io()) io->SetDo(bit, false);
            }
        } off{ m_func, lampBit };

        int ms = m_sys()->LampMsFor(cam);
        if (lampBit >= 0 && ms > 0) base::Delay(ms, ct);
        return run();
    }

    VmCallResult Routine_Calib::CallVm(const VmCallRequest& req, std::stop_token ct)
    {
        return WithCamLamp<VmCallResult>(req.Camera, ct, [&]() { return m_vision()->CallFlow(req, ct); });
    }

    /// <summary>
    /// Đọc <c>CurrentPos</c> chứ không dùng vị trí LỆNH: sai lệch bám của servo, khe hở cơ khí và bù nhiệt đều nằm ở
    /// hiệu số giữa hai thứ đó, và chính sai lệch ấy là cái phép hiệu chuẩn phải học (PLAN.md §6.1 quy tắc 3).
    /// </summary>
    AxisSnapshot Routine_Calib::Snapshot()
    {
        AxisSnapshot s;
        auto x = m_motion[AxisId::X];
        auto y2 = m_motion[AxisId::Y2];
        auto a1 = m_motion[AxisId::A1];
        auto a2 = m_motion[AxisId::A2];
        s.X = x ? x->CurrentPos() : 0;
        s.Y2 = y2 ? y2->CurrentPos() : 0;
        s.A1 = a1 ? a1->CurrentPos() : 0;
        s.A2 = a2 ? a2->CurrentPos() : 0;
        return s;
    }

    // ───────────────────────── §6.2 CAM top-down: lưới 9 điểm ─────────────────────────

    std::string Routine_Calib::ClearCalibPoints(EVisionProcedure procedure)
    {
        std::string err = m_vision()->ClearCalibPoints(procedure);
        if (!err.empty())
            Log(std::string("Calib: cannot clear old points (") + ToString(procedure) + ") — " + err, LogLevel::Warning);
        return err;
    }

    bool Routine_Calib::MoveAndTriggerTopDownCalib(double x, double y2, std::stop_token ct)
    {
        return NoThrowBool([&]()
        {
            if (!m_func.MoveToCell(AxisId::Y2, y2, x, ct))
                return false;
            return TriggerTopDownCalibFlow(ct);
        });
    }

    bool Routine_Calib::TriggerTopDownCalibFlow(std::stop_token ct)
    {
        return NoThrowBool([&]()
        {
            m_func.WaitSettle(EVmCamera::TopDown, ct);
            AxisSnapshot snap = Snapshot();
            VisionPose pose = WithCamLamp<VisionPose>(EVmCamera::TopDown, ct,
                [&]() { return m_vision()->TriggerCalib(EVisionProcedure::TopDownCalib, snap, ct); });
            LogCalibPoint(m_sys()->VisionOutputs.FlowLocateTop + " (calib)", snap, 0, pose.Found);
            return pose.Found;
        });
    }

    // ───────────────────────── §6.4 CAM bottom-up: nạp điểm hiệu chuẩn ─────────────────────────

    /// <summary>
    /// Mốc góc giữa hai camera KHÔNG lấy ở đây, và cũng không cần "A = 0°": camera dưới đo hướng thật của sản phẩm chứ
    /// không đo giá trị tuyệt đối của A, và Auto cũng không bao giờ đưa A về 0° (nó chạy tới CmdA = góc lúc chụp + góc
    /// bù). Mốc góc đo ở bước xác định hand-eye (HandEyeCommit), điều kiện duy nhất là <b>không xoay A giữa lúc hút con
    /// can lắp đúng và lúc chụp</b>.
    /// </summary>
    AlignCalibSequenceResult Routine_Calib::RunAlignCalibSequence(int nozzle, double alignPosX, const std::vector<double>& xOffsets,
                                                                  const std::vector<double>& anglesDeg,
                                                                  const std::function<void(const std::string&)>& progress,
                                                                  std::stop_token ct)
    {
        AlignCalibSequenceResult result;
        try
        {
            std::string axisName = nozzle == 1 ? AxisId::A1 : AxisId::A2;
            EVisionProcedure proc = nozzle == 1 ? EVisionProcedure::AlignCalibTool1 : EVisionProcedure::AlignCalibTool2;

            if (anglesDeg.size() < 3)
            {
                result.Reason = Loc::T("Calib.NeedThreeAngles");
                return result;
            }

            // Xoá điểm của lần trước trước khi chạy trục nào — chạy chồng lên danh sách cũ là ra file hiệu chuẩn trộn
            // hai lần nạp, không có thông báo lỗi nào.
            std::string clearErr = ClearCalibPoints(proc);
            if (!clearErr.empty())
            { result.Reason = Loc::F("Calib.ClearPointsFailed", clearErr); return result; }

            if (!MoveZToBottomCapture(nozzle, ct))
            { result.Reason = ZCaptureFail(nozzle); return result; }

            std::string flow = m_sys()->VisionOutputs.FlowAlignBottom(nozzle) + " (calib)";
            int total = (int)xOffsets.size() + (int)anglesDeg.size();
            Log(Loc::Format("Align calib nozzle #{0}: start — {1} {2:0.000}, align X {3:0.000}, X offsets [{4}] mm, {5} angles [{6}]°",
                { nozzle, ZName(nozzle), Pos(ZName(nozzle)), alignPosX, Join(xOffsets), axisName, Join(anglesDeg) }));

            // Phần tịnh tiến chụp ở MỘT góc cố định — góc đầu của dãy. Vừa xoay vừa tịnh tiến
            // thì module hiệu chuẩn không tách được hai thành phần.
            double firstAngle = anglesDeg[0];
            if (!Move(axisName, firstAngle, ct))
            { result.Reason = Loc::F("Calib.RotateToFirstAngleFailed", axisName); return result; }

            for (size_t i = 0; i < xOffsets.size(); i++)
            {
                double x = alignPosX + xOffsets[i];
                if (progress) progress(Loc::F("Calib.ProgressShift", (int)i + 1, (int)xOffsets.size(), x));

                if (!Move(AxisId::X, x, ct))
                { result.Reason = Loc::F("Calib.MoveXToFailed", x); return result; }

                m_func.WaitSettle(EVmCamera::BottomUp, ct);
                AxisSnapshot snap = Snapshot();
                VisionPose pose = WithCamLamp<VisionPose>(EVmCamera::BottomUp, ct,
                    [&]() { return m_vision()->TriggerCalib(proc, snap, ct); });
                LogCalibPoint(Loc::Format("{0} point {1}/{2}", { flow, result.PointsCaptured + 1, total }), snap, nozzle, pose.Found);
                if (!pose.Found)
                { result.Reason = Loc::F("Calib.ShiftPointNotFound", x); return result; }
                result.PointsCaptured++;
            }

            // Phần xoay: luôn tại ĐÚNG vị trí soi của tay này — chạy thật cũng dừng ở đó
            // (PDF Hikrobot tr.45: vị trí chụp lúc chạy phải trùng vị trí chụp lúc hiệu chuẩn).
            if (!Move(AxisId::X, alignPosX, ct))
            { result.Reason = Loc::T("Calib.MoveXToAlignPosFailed"); return result; }

            for (size_t i = 0; i < anglesDeg.size(); i++)
            {
                double angle = anglesDeg[i];
                if (progress) progress(Loc::F("Calib.ProgressAngle", (int)i + 1, (int)anglesDeg.size(), angle));

                if (!Move(axisName, angle, ct))
                { result.Reason = Loc::F("Calib.RotateToFailed", angle); return result; }

                m_func.WaitSettle(EVmCamera::BottomUp, ct);
                AxisSnapshot snap = Snapshot();
                VisionPose pose = WithCamLamp<VisionPose>(EVmCamera::BottomUp, ct,
                    [&]() { return m_vision()->TriggerCalib(proc, snap, ct); });
                LogCalibPoint(Loc::Format("{0} point {1}/{2}", { flow, result.PointsCaptured + 1, total }), snap, nozzle, pose.Found);
                if (!pose.Found)
                { result.Reason = Loc::F("Calib.AngleNotFound", angle); return result; }
                result.PointsCaptured++;
            }

            if (!Move(axisName, 0, ct))
            { result.Reason = Loc::F("Calib.AxisToZeroFailed", axisName); return result; }

            result.Ok = true;
            Log(Loc::Format("Align calib nozzle #{0}: {1} points captured at {2} {3:0.000}, A axis back to 0°",
                { nozzle, result.PointsCaptured, ZName(nozzle), Pos(ZName(nozzle)) }));
            return result;
        }
        catch (const base::OperationCanceled&)
        {
            result.Reason = Loc::T("Calib.Cancelled");
            return result;
        }
        catch (const std::exception& ex)
        {
            AlarmManager::RaiseEvent(AlarmCode::RoutineCrashed, "Calib", ex.what());
            result.Reason = Loc::F("Calib.UnexpectedError", std::string(ex.what()));
            return result;
        }
    }

    bool Routine_Calib::MoveAndTriggerAlignCalib(int nozzle, double angleDeg, std::stop_token ct)
    {
        return NoThrowBool([&]()
        {
            std::string axisName = nozzle == 1 ? AxisId::A1 : AxisId::A2;
            if (m_motion[axisName] == nullptr) return false;

            if (!Move(axisName, angleDeg, ct))
                return false;

            return TriggerAlignCalibFlow(nozzle, ct);
        });
    }

    bool Routine_Calib::TriggerAlignCalibFlow(int nozzle, std::stop_token ct)
    {
        return NoThrowBool([&]()
        {
            EVisionProcedure proc = nozzle == 1 ? EVisionProcedure::AlignCalibTool1 : EVisionProcedure::AlignCalibTool2;
            if (!MoveZToBottomCapture(nozzle, ct)) return false;
            m_func.WaitSettle(EVmCamera::BottomUp, ct);
            AxisSnapshot snap = Snapshot();
            VisionPose pose = WithCamLamp<VisionPose>(EVmCamera::BottomUp, ct,
                [&]() { return m_vision()->TriggerCalib(proc, snap, ct); });
            LogCalibPoint(m_sys()->VisionOutputs.FlowAlignBottom(nozzle) + " (calib) one point in place", snap, nozzle, pose.Found);
            return pose.Found;
        });
    }

    bool Routine_Calib::MoveAlignAxisToZero(int nozzle, std::stop_token ct)
    {
        return NoThrowBool([&]()
        {
            std::string axisName = nozzle == 1 ? AxisId::A1 : AxisId::A2;
            if (m_motion[axisName] == nullptr) return false;
            return Move(axisName, 0, ct);
        });
    }

    // ───────────────────────── §6.2b, §6.4b, §6.5 — đo hằng số ─────────────────────────

    std::optional<VmCalibGate> Routine_Calib::LastCalibGate() const
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        return m_lastCalibGate;
    }

    VmCalibGate Routine_Calib::CheckCalib()
    {
        auto model = McDataManager::Model();
        auto sys = m_sys();
        VmCalibGate g = VmCycle::EvaluateCalib(model.get(), sys.get());
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastCalibGate = g;
        return g;
    }

    /// <summary>
    /// Chụp N ảnh rồi App lấy trung bình: X, Y cộng thẳng, góc theo sin/cos để không gãy ở ±180°, vị trí trục lúc chụp từ
    /// encoder. Mỗi ảnh phải thấy vật và echo khớp — lệch là huỷ cả chuỗi.
    /// </summary>
    VmMeanCapture Routine_Calib::CaptureMean(int tool, int shots, std::stop_token ct)
    {
        VmMeanCapture res;
        res.Shots = shots;
        auto sys = m_sys();
        const VisionOutputNames& names = sys->VisionOutputs;
        res.Reason = VmCycle::AxisVarProblem(&names);
        if (!res.Reason.empty()) return res;
        if (tool != 0 && !MoveZToBottomCapture(tool, ct))
        { res.Reason = ZCaptureFail(tool); return res; }

        double sx = 0, sy = 0, ss = 0, sc = 0, ax = 0, ay = 0, as_ = 0, ac = 0;
        // Dao động giữa các ảnh (lớn nhất − nhỏ nhất) — để log phân biệt được số đo rung với số đo lệch đều.
        double minX = std::numeric_limits<double>::max(), maxX = std::numeric_limits<double>::lowest();
        double minY = std::numeric_limits<double>::max(), maxY = std::numeric_limits<double>::lowest();
        double firstA = 0, minDA = 0, maxDA = 0;
        std::string flow = tool == 0 ? std::string("LocateTop") : "AlignBottom_Tool" + std::to_string(tool);
        for (int i = 1; i <= shots; i++)
        {
            if (ct.stop_requested()) { res.Reason = Loc::T("Calib.Cancelled"); return res; }

            m_func.WaitSettle(tool == 0 ? EVmCamera::TopDown : EVmCamera::BottomUp, ct);
            int rid = VmRunIds::Next();
            auto g = VmCycle::AxisGlobals(m_motion, names, rid);
            VmCallRequest req;
            if (tool == 0) req = VmCycle::LocateRequest(names, g);
            else
            {
                auto model = McDataManager::Model();
                VmCycle::AddPlaceGlobals(g, nullptr, model ? &model->Calib : nullptr, tool);
                req = VmCycle::AlignBottomRequest(names, tool, g, false);
            }
            VmCallResult r = CallVm(req, ct);
            VmCameraRead read = tool == 0 ? VmCycle::ReadLocate(r, g, names) : VmCycle::ReadAlignBottom(r, req, names, tool);
            if (tool == 0) VisionReadout::TopDown(read);
            else VisionReadout::Tool(tool, read, static_cast<const VmPlaceCommand*>(nullptr));
            if (tool != 0)
                Log(VmCycle::TraceAlignBottom(r, req, names, tool, Pos(ZName(tool))).Line);

            std::string what = Loc::F("Calib.ShotPrefix", flow, i, shots);
            if (!read.CallOk) { res.Reason = what + read.CallError; return res; }
            if (!read.EchoOk) { res.Reason = what + read.EchoIssue; return res; }
            if (!read.Found) { res.Reason = what + Loc::T(tool == 0 ? "Calib.NoObjectSeen" : "Calib.NoProductSeen"); return res; }
            if (!read.VmValid) { res.Reason = what + read.VmIssue; return res; }

            sx += read.X;
            sy += read.Y;
            minX = std::min(minX, read.X); maxX = std::max(maxX, read.X);
            minY = std::min(minY, read.Y); maxY = std::max(maxY, read.Y);
            if (i == 1) firstA = read.A;
            double dA = AxisAngle::ShortestDelta(firstA, read.A);
            minDA = std::min(minDA, dA); maxDA = std::max(maxDA, dA);
            double rad = read.A * 3.14159265358979323846 / 180.0;
            ss += std::sin(rad);
            sc += std::cos(rad);
            ax += VmCycle::Val(g, names.GvAxisX);
            ay += VmCycle::Val(g, names.GvAxisY2);
            double axisA = VmCycle::Val(g, tool == 2 ? names.GvAxisA2 : names.GvAxisA1) * 3.14159265358979323846 / 180.0;
            as_ += std::sin(axisA);
            ac += std::cos(axisA);
        }

        res.X = sx / shots;
        res.Y = sy / shots;
        res.A = VmCycle::MeanAngleDeg(ss, sc);
        res.AxX = ax / shots;
        res.AxY = ay / shots;
        res.AxA = VmCycle::MeanAngleDeg(as_, ac);
        res.Ok = true;

        std::string spread = Loc::Format("spread X {0:0.000}, Y {1:0.000} mm, angle {2:0.00}°", { maxX - minX, maxY - minY, maxDA - minDA });
        Log(tool == 0
            ? Loc::Format("{0}, mean of {1} shots: object at X {2:0.000}, Y {3:0.000} mm, angle {4:0.00}° ({5}); axis X {6:0.000}, Y2 {7:0.000}",
                { flow, shots, res.X, res.Y, res.A, spread, res.AxX, res.AxY })
            : Loc::Format("{0}, mean of {1} shots: product off rotation center X {2:0.000}, Y {3:0.000} mm, angle {4:0.00}° ({5}); axis X {6:0.000}, {7} {8:0.000}°, {9} {10:0.000}",
                { flow, shots, res.X, res.Y, res.A, spread, res.AxX, AName(tool), res.AxA, ZName(tool), Pos(ZName(tool)) }));
        return res;
    }

    std::string Routine_Calib::SaveModelNote()
    {
        if (!McDataManager::ModelLoaded()) return Loc::T("Calib.ModelNotLoadedNote");
        return McDataManager::SaveModel()
            ? Loc::F("Calib.SavedToModel", McDataManager::Model()->Name)
            : Loc::T("Calib.SaveModelFailed");
    }

    /// <summary>
    /// Bước 2 (§6.2b): soi dấu chuẩn (sản phẩm mẫu đặt cố định trong ô tray) ở ba chỗ bằng LocateTop — gốc, sau khi chạy
    /// X, sau khi chạy Y2 — rồi App so vị trí (VmCycle::JudgeTopDown). Dấu chuẩn đứng yên trong hệ máy nên TopX/TopY phải
    /// không đổi khi trục chạy — bắt được sai thang đo, <b>sai dấu</b> và lẫn trục X ↔ Y.
    /// </summary>
    TopDownVerifyResult Routine_Calib::VerifyTopDown(double stepMm, std::stop_token ct)
    {
        TopDownVerifyResult result;
        result.StepMm = stepMm;
        try
        {
            if (std::fabs(stepMm) < 0.5)
            {
                result.Reason = Loc::T("Calib.StepTooSmall");
                return result;
            }

            auto sys = m_sys();
            int shots = std::max(1, sys->CalibShotsPerPoint);
            auto xAxis = m_motion[AxisId::X];
            auto yAxis = m_motion[AxisId::Y2];
            double x0 = xAxis ? xAxis->CurrentPos() : 0;
            double y0 = yAxis ? yAxis->CurrentPos() : 0;

            VmMeanCapture s0 = CaptureMean(0, shots, ct);
            if (!s0.Ok) { result.Reason = s0.Reason; return result; }

            if (!Move(AxisId::X, x0 + stepMm, ct))
            { result.Reason = Loc::F("Calib.AxisMoveFailed", AxisId::X); return result; }
            VmMeanCapture s1 = CaptureMean(0, shots, ct);
            bool backX = Move(AxisId::X, x0, ct);
            if (!s1.Ok) { result.Reason = s1.Reason; return result; }
            if (!backX) { result.Reason = Loc::F("Calib.AxisBackToStartFailed", AxisId::X); return result; }

            if (!Move(AxisId::Y2, y0 + stepMm, ct))
            { result.Reason = Loc::F("Calib.AxisMoveFailed", AxisId::Y2); return result; }
            VmMeanCapture s2 = CaptureMean(0, shots, ct);
            bool backY = Move(AxisId::Y2, y0, ct);
            if (!s2.Ok) { result.Reason = s2.Reason; return result; }
            if (!backY) { result.Reason = Loc::F("Calib.AxisBackToStartFailed", AxisId::Y2); return result; }

            VmTopDownJudge j = VmCycle::JudgeTopDown(s0, s1, s2, sys->CalibMaxResidualThresholdMm);
            result.Status = j.Status;
            result.ErrorX = j.ErrX;
            result.ErrorY = j.ErrY;
            result.CrossX = j.CrossX;
            result.CrossY = j.CrossY;
            result.SignXOk = j.Status != VmStatus::Code8;
            result.SignYOk = result.SignXOk;
            result.Ok = j.Status == VmStatus::Ok;
            if (!result.Ok) result.Reason = Loc::F("Calib.TopDownVerifyFail", VmStatus::TopDownVerify(j.Status));

            if (result.Ok)
            {
                if (auto model = McDataManager::Model()) model->Calib.TopDownVerifiedAt = Now();
                result.Note = SaveModelNote();
            }

            Log(Loc::Format("Top-down verify (step {0:0.0} mm): Status {1}, error X {2:0.000}, Y {3:0.000}, cross {4:0.000}/{5:0.000} mm — {6}",
                { stepMm, result.Status, result.ErrorX, result.ErrorY, result.CrossX, result.CrossY, result.Ok ? "PASS" : "FAIL" }),
                result.Ok ? LogLevel::Info : LogLevel::Warning);
            return result;
        }
        catch (const base::OperationCanceled&)
        {
            result.Reason = Loc::T("Calib.Cancelled");
            return result;
        }
        catch (const std::exception& ex)
        {
            AlarmManager::RaiseEvent(AlarmCode::RoutineCrashed, "Calib", ex.what());
            result.Reason = Loc::F("Calib.UnexpectedError", std::string(ex.what()));
            return result;
        }
    }

    /// <summary>
    /// Bước 4 (§6.4b), sản phẩm đang trên tay tại vị trí soi: App lấy trung bình N ảnh AlignBottom_ToolN trước và sau khi
    /// xoay thử (HasTarget = 0 nên Group Cmd nghỉ), rồi <b>tự phán</b> bằng VmCycle::JudgeAlignSign — không Flow nào tham
    /// gia. Sai số dư khác 0 nghĩa là gốc toạ độ của file bottom-up không nằm đúng tâm xoay, hoặc chiều hệ toạ độ bị lật.
    /// </summary>
    AlignVerifyResult Routine_Calib::VerifyAlignRotation(int nozzle, double testAngleDeg, std::stop_token ct)
    {
        AlignVerifyResult result;
        result.TestAngleDeg = testAngleDeg;
        try
        {
            auto sys = m_sys();
            std::string axisName = nozzle == 1 ? AxisId::A1 : AxisId::A2;
            if (std::fabs(testAngleDeg) < 1.0)
            { result.Reason = Loc::T("Calib.TestAngleTooSmall"); return result; }
            auto aAxis = m_motion[axisName];
            if (aAxis == nullptr)
            { result.Reason = Loc::F("Calib.NoAxis", axisName); return result; }

            double a0 = aAxis->CurrentPos();
            int shots = std::max(1, sys->CalibShotsPerPoint);

            VmMeanCapture s0 = CaptureMean(nozzle, shots, ct);
            if (!s0.Ok) { result.Reason = s0.Reason; return result; }
            result.PosX = s0.AxX;

            if (!Move(axisName, a0 + testAngleDeg, ct))
            { result.Reason = Loc::F("Calib.RotateAxisFailed", axisName); return result; }
            VmMeanCapture s1 = CaptureMean(nozzle, shots, ct);
            bool back = Move(axisName, a0, ct);
            if (!s1.Ok) { result.Reason = s1.Reason; return result; }
            if (!back) { result.Reason = Loc::F("Calib.AxisBackToStartAngleFailed", axisName); return result; }
            VmAlignJudge j = VmCycle::JudgeAlignSign(s0, s1, sys->CalibMaxResidualThresholdMm, sys->AlignVerifyAngleTolDeg,
                                                     sys->AlignVerifyMinEccMm, sys->AlignVerifyMinTurnDeg);
            result.Status = j.Status;
            result.Sign = j.Sign;
            result.MeasuredDeltaThetaDeg = j.RawDTheta;
            result.ResidualMm = j.Residual;
            result.EccentricityMm = j.Ecc;
            result.Ok = j.Status == VmStatus::Ok && j.Sign != 0;

            // Số đo đi kèm kết luận, cả trên bảng báo lẫn trong log: chỉ đọc "KHÔNG ĐẠT" thì không biết lệch bao nhiêu
            // và lệch ở đâu (Tuấn yêu cầu 2026-09-22).
            std::string numbers = Loc::F("Calib.AlignVerifyNumbers",
                axisName, j.EncDA, AxisAngle::ShortestDelta(s0.A, s1.A), std::fabs(std::fabs(j.RawDTheta) - std::fabs(j.EncDA)),
                sys->AlignVerifyAngleTolDeg, j.Residual, sys->CalibMaxResidualThresholdMm, j.Ecc, sys->AlignVerifyMinEccMm,
                s0.AxX, ZName(nozzle), Pos(ZName(nozzle)));
            if (!result.Ok) result.Reason = Loc::F("Calib.AlignVerifyFail", nozzle, VmStatus::AlignVerify(j.Status), numbers);

            if (result.Ok)
            {
                if (auto model = McDataManager::Model())
                    model->Calib.SetAlignVerified(nozzle, result.Sign, result.PosX, result.ResidualMm, Now());
                result.Note = SaveModelNote();
            }

            Log(Loc::Format("Rotation test nozzle #{0}: Status {1} ({2}), sign {3}; {4}; product before ({5:0.000}, {6:0.000}, {7:0.00}°) → after ({8:0.000}, {9:0.000}, {10:0.00}°)",
                { nozzle, result.Status, VmStatus::AlignVerify(j.Status), result.Sign, numbers, s0.X, s0.Y, s0.A, s1.X, s1.Y, s1.A }),
                result.Ok ? LogLevel::Info : LogLevel::Warning);
            return result;
        }
        catch (const base::OperationCanceled&)
        {
            result.Reason = Loc::T("Calib.Cancelled");
            return result;
        }
        catch (const std::exception& ex)
        {
            AlarmManager::RaiseEvent(AlarmCode::RoutineCrashed, "Calib", ex.what());
            result.Reason = Loc::F("Calib.UnexpectedError", std::string(ex.what()));
            return result;
        }
    }

    // ───────────────────────── xác định hand-eye (§6.5) ─────────────────────────
    //
    // Bảy nút, mỗi nút một việc (Tuấn yêu cầu 2026-09-25). 26/09: nút tính điểm đích tách thành 7 · tới vị trí soi
    // CAM dưới và 8 · Trigger AlignBottom, thêm 9 · tới điểm đích vừa tính. Gắp trên CƠ CẤU KẸP giữ chi tiết đích đứng
    // yên, ở vị trí Jog-dạy riêng — xem ModelData::HandEyePick_Nozzle1 để biết vì sao không dùng ô tray.
    //
    // Trạng thái giữa các nút nằm ở ĐÂY, không ở ViewModel — bài tự kiểm gọi thẳng lớp này, nên mọi cổng chặn đều được
    // kiểm chứ không nằm ở chỗ test không tới được.

    /// <summary>
    /// Đường chạy thật tự dọn ở hai chỗ — nút 1 xoá sạch khi bắt đầu lại, và nút trả xoá khi đã nhả xong — nên KHÔNG có
    /// hook nào gọi hàm này. Cố ý không gọi khi đổi model hay rời trang: xoá sổ lúc tay còn cầm con can là mở đường cho
    /// lần hạ sau đưa một tay "trống" xuống chỗ đã có hàng.
    /// </summary>
    void Routine_Calib::HandEyeClear()
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_he.reset();
        m_heTest.reset();
    }

    /// <summary>Cổng chặn của phép thử tổng quát — KHÔNG đòi nút 1 → 5 trong phiên này, chỉ đòi hand-eye của tay này đã
    /// ghi trong model: 0 = nút 6, 7 · 1 = đã soi (nút 8) · 2 = đã tính điểm đích (nút 9).</summary>
    std::string Routine_Calib::HandEyeTestNeeds(int nozzle, int stage)
    {
        auto model = McDataManager::Model();
        if (model == nullptr) return Loc::T("Calib.NoModel");
        if (!model->Calib.IsHandEyeMeasured(nozzle))
            return Loc::F("Calib.HandEyeNotRecorded", nozzle);
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        if (stage >= 1 && (!m_heTest || m_heTest->Nozzle != nozzle))
            return Loc::F("Calib.NeedLocateHere", nozzle);
        if (stage >= 2 && !m_heTest->HasTarget)
            return Loc::T("Calib.NeedTarget");
        return {};
    }

    /// <summary>Lệch của điểm đích so với chỗ đã gắp — chỉ có nghĩa khi con can trên tay được gắp bằng nút 2 trong phiên
    /// này; hút lên cách khác thì không có chỗ gắp nào để so, trả chuỗi rỗng.</summary>
    std::string Routine_Calib::HandEyeVsPick(int nozzle, double x, double y, const std::string& rowAxis)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        if (!m_he || m_he->Nozzle != nozzle || !m_he->Picked) return "";
        return Loc::F("Calib.VsPick", x - m_he->PickX, rowAxis, y - m_he->PickY);
    }

    /// <summary>
    /// Cổng chặn dùng chung: tiến trình đang dở phải là của tay này và đã qua ít nhất <paramref name="stage"/> mốc — 1 đã
    /// tới vị trí gắp · 2 đã gắp · 3 đã soi chi tiết. Trả CÂU nói thiếu bước nào / tay nào, không trả bool: nút xám không
    /// nói gì, mà câu "tay hút #1 chưa gắp con can trên cơ cấu kẹp — bấm 2 trước" mới là thứ người vận hành cần.
    /// </summary>
    std::string Routine_Calib::HandEyeNeeds(int nozzle, int stage)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        if (!m_he)
            return Loc::T("Calib.HeNotStarted");
        if (m_he->Nozzle != nozzle)
            return Loc::F("Calib.HeOtherNozzle", m_he->Nozzle, nozzle);
        if (stage >= 2 && !m_he->Picked)
            return Loc::F("Calib.HeNotPicked", nozzle);
        if (stage >= 3 && m_he->Shots <= 0)
            return Loc::T("Calib.HeNotLocated");
        return {};
    }

    StepRunResult Routine_Calib::HandEyeGoToPick(int nozzle, std::stop_token ct)
    {
        return StepGuard("Go to pick", [&]() { return HandEyeGoToPickCore(nozzle, ct); }, "Hand-eye");
    }

    /// <summary>Bước 1 — tay đang chọn tới vị trí gắp đã dạy trên cơ cấu kẹp, trục A về góc hút. KHÔNG chạm Z. Bấm lại là
    /// làm lại từ đầu: mọi số đo của lần trước bị bỏ.</summary>
    StepRunResult Routine_Calib::HandEyeGoToPickCore(int nozzle, std::stop_token ct)
    {
        StepRunResult res;
        auto model = McDataManager::Model();
        if (model == nullptr) { res.Reason = Loc::T("Calib.NoModel"); return res; }
        res.Reason = model->HandEyeJigProblem(nozzle);
        if (!res.Reason.empty()) return res;
        if (m_func.HasProduct(nozzle))
        { res.Reason = Loc::F("Calib.NozzleBusyRelease", nozzle); return res; }

        {
            std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
            m_he.reset();   // bấm nút 1 là bỏ hết số đo cũ, kể cả của tay kia
        }
        auto tray = model->AssyTray;
        TeachPoint pick = model->HandEyePick(nozzle);
        if (!m_func.EnsureZSafe(ct))
        { res.Reason = Loc::T("Calib.ZSafeFailed"); return res; }
        if (!m_func.MoveToCellForPick(nozzle, model->PickAngle(nozzle), tray->AxisRow, pick.Y, pick.X, ct))
        { res.Reason = Loc::T("Calib.GoToJigPickFailed"); return res; }

        {
            std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
            m_he = HandEyeProgress{};
            m_he->Nozzle = nozzle;
        }
        res.Ok = true;
        res.Text = Loc::F("Calib.HeAtPick", nozzle, pick.X, tray->AxisRow, pick.Y, AName(nozzle), model->PickAngle(nozzle));
        return res;
    }

    StepRunResult Routine_Calib::HandEyePick(int nozzle, std::stop_token ct)
    {
        return StepGuard("Pick sample can", [&]() { return HandEyePickCore(nozzle, ct); }, "Hand-eye");
    }

    /// <summary>
    /// Bước 2 — hạ Z xuống gắp con can mẫu trên cơ cấu kẹp, rồi nâng lên.
    ///
    /// <para><b>Cao độ hạ và điểm giảm tốc lấy của tray LẮP RÁP</b> (Tuấn chốt 2026-09-25), nên cơ cấu kẹp phải giữ con
    /// can ở đúng cao độ như một ô tray — cao hơn thì đầu hút ấn xuống con can, thấp hơn thì gắp hụt.</para>
    ///
    /// <para>Ra lệnh lại tới đúng vị trí gắp trước khi hạ, chứ không tin là tay còn đứng đó: người bấm có thể jog đi đâu
    /// đó sau khi bấm nút 1, và hạ Z ở chỗ khác là hạ vào chỗ không có gì đỡ.</para>
    ///
    /// <para>Encoder đọc khi tay <b>còn đứng trên</b> cơ cấu kẹp — hai con số đó vào thẳng phép trừ của hand-eye, đọc sau
    /// khi chạy đi là mất nghĩa.</para>
    /// </summary>
    StepRunResult Routine_Calib::HandEyePickCore(int nozzle, std::stop_token ct)
    {
        StepRunResult res;
        auto model = McDataManager::Model();
        if (model == nullptr) { res.Reason = Loc::T("Calib.NoModel"); return res; }
        res.Reason = HandEyeNeeds(nozzle, 1);
        if (!res.Reason.empty()) return res;
        res.Reason = model->HandEyeJigProblem(nozzle);
        if (!res.Reason.empty()) return res;
        if (m_func.HasProduct(nozzle))
        { res.Reason = Loc::F("Calib.NozzleBusyRelease", nozzle); return res; }
        std::string zName = ZName(nozzle), aName = AName(nozzle);
        auto xAxis = m_motion[AxisId::X];
        auto yAxis = m_motion[AxisId::Y2];
        auto aAxis = m_motion[aName];
        if (xAxis == nullptr || yAxis == nullptr || aAxis == nullptr)
        { res.Reason = Loc::F("Calib.CannotReadAxes", aName); return res; }

        auto tray = model->AssyTray;
        TeachPoint pick = model->HandEyePick(nozzle);

        if (!m_func.EnsureZSafe(ct))
        { res.Reason = Loc::T("Calib.ZSafeFailed"); return res; }
        if (!m_func.MoveToCellForPick(nozzle, model->PickAngle(nozzle), tray->AxisRow, pick.Y, pick.X, ct))
        { res.Reason = Loc::T("Calib.GoToJigPickFailed"); return res; }

        double pickX = xAxis->CurrentPos(), pickY = yAxis->CurrentPos();
        double pickA = aAxis->CurrentPos();

        if (!m_func.MoveZDown(zName, tray->ZPickFor(zName), tray->ZSlowFor(zName), ct))
        { res.Reason = Loc::F("Calib.ZDownAssyPlaceFailed", zName); return res; }

        m_func.SetVacuum(nozzle, true);
        bool gripped = WaitVacuum(nozzle, true, ct);
        SettleVacuum(gripped, ct);

        if (!m_func.MoveZUp(zName, m_motion.ZSafe(), tray->ZSlowFor(zName), ct))
        { res.Reason = Loc::F("Calib.ZUpFailed", zName); return res; }
        if (!gripped)
        {
            m_func.SetVacuum(nozzle, false);
            res.Reason = Loc::T("Calib.HePickMissed");
            return res;
        }

        // Ghi sổ: cảm biến đang báo có hàng thì phần mềm phải biết, không thì lần START sau ResumeIntegrityOk thấy hai
        // bên lệch nhau, phát alarm và bắt RESET. Ô nguồn để −1 vì con can này KHÔNG đến từ ô tray nào — đúng nghĩa mà
        // Routine_Auto::ReturnToSource gán cho −1: không có chỗ đúng để trả, phải người lấy ra.
        auto run = McDataManager::RunData();
        auto nz = nozzle == 1 ? run->Nozzle1 : run->Nozzle2;
        nz->HasProduct = true;
        nz->Aligned = false;
        nz->SourceCellIndex = -1;
        McDataManager::SaveRunningData();

        {
            std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
            // TODO(port): bản C# ghi thẳng _he.Picked (giả định _he còn — mỗi lúc một chuỗi); ở đây thiếu thì bỏ qua thay
            // vì truy cập null.
            if (m_he)
            {
                m_he->Picked = true;
                m_he->PickX = pickX;
                m_he->PickY = pickY;
                m_he->PickA = pickA;
            }
        }
        res.Ok = true;
        res.Text = Loc::F("Calib.HePicked", nozzle, pickX, tray->AxisRow, pickY, aName, pickA, tray->ZPickFor(zName));
        return res;
    }

    StepRunResult Routine_Calib::HandEyeLocate(int nozzle, std::stop_token ct)
    {
        return StepGuard("Locate target part", [&]() { return HandEyeLocateCore(nozzle, false, ct); }, "Hand-eye");
    }

    StepRunResult Routine_Calib::HandEyeLocateHere(int nozzle, std::stop_token ct)
    {
        return HandEyeLocateHere(nozzle, -1, -1, ct);
    }

    StepRunResult Routine_Calib::HandEyeLocateHere(int nozzle, int row, int col, std::stop_token ct)
    {
        return StepGuard(row < 0 ? "Locate in place" : "Locate at assy cell",
                         [&]() { return HandEyeLocateCore(nozzle, true, ct, row, col); }, "Hand-eye");
    }

    /// <summary>
    /// Bước 3 (<paramref name="here"/> = false) — CAM top-down tới vị trí soi đã dạy rồi soi <b>chi tiết đích vừa lộ
    /// ra</b> N ảnh. <b>Gắp trước, soi sau</b>: sản phẩm gồm hai mảnh nên chưa nhấc con can ra thì camera trên chỉ thấy con
    /// can, mà số cần trừ là vị trí <b>chi tiết</b>. <b>Không xoay A</b> từ đây tới hết bước.
    ///
    /// <para>Bước 6 (<paramref name="here"/> = true) — <b>phép thử tổng quát</b>: kích LocateTop <b>tại chỗ</b> (hoặc ở
    /// ô tray lắp ráp đã chọn), một ảnh. Nó chứng minh pose top-down bất biến khắp khung hình, camera dưới lặp lại được,
    /// sổ góc A đúng, Group Cmd nối đúng; nó <b>KHÔNG chứng minh</b> giá trị tuyệt đối của hand-eye — mọi sai chung cho
    /// cả lần đo và lần thử đều triệt tiêu.</para>
    /// </summary>
    StepRunResult Routine_Calib::HandEyeLocateCore(int nozzle, bool here, std::stop_token ct, int row, int col)
    {
        StepRunResult res;
        auto sys = m_sys();
        res.Reason = VmCycle::AxisVarProblem(&sys->VisionOutputs);
        if (!res.Reason.empty()) return res;
        auto model = McDataManager::Model();
        if (model == nullptr) { res.Reason = Loc::T("Calib.NoModel"); return res; }
        // Nút 6 chỉ đòi hand-eye đã ghi trong model, không đòi nút 1 → 5 trong phiên này (Tuấn 26/09), và không
        // đòi tay cầm hàng — nó chỉ soi bằng CAM trên; nút 8 mới cần con can.
        res.Reason = here ? HandEyeTestNeeds(nozzle, 0) : HandEyeNeeds(nozzle, 2);
        if (!res.Reason.empty()) return res;
        if (!here && !m_func.HasProduct(nozzle))
        { res.Reason = Loc::F("Calib.NozzleLostCan", nozzle); return res; }

        if (!here)
        {
            res.Reason = model->HandEyeJigProblem(nozzle);
            if (!res.Reason.empty()) return res;
            TeachPoint cam = model->HandEyeCamPos;
            if (!m_func.MoveToCell(model->AssyTray->AxisRow, cam.Y, cam.X, ct))
            { res.Reason = Loc::T("Calib.GoToTargetCamFailed"); return res; }
        }
        else if (row >= 0)
        {
            auto tray = model->AssyTray;
            if (row >= tray->Rows() || col < 0 || col >= tray->Cols())
            { res.Reason = Loc::F("Calib.CellOutsideAssyGrid", tray->Rows(), tray->Cols()); return res; }
            if (!tray->CamTopDownAnchorTaught)
            { res.Reason = Loc::T("Calib.AssyAnchorNotTaught"); return res; }
            TeachPoint cam = tray->CamPosFor(row, col);
            if (!m_func.MoveToCell(tray->AxisRow, cam.Y, cam.X, ct))
            { res.Reason = Loc::F("Calib.GoToCellCamFailedAt", row + 1, col + 1); return res; }
        }

        int shots = here ? 1 : std::max(1, sys->CalibShotsPerPoint);
        VmMeanCapture top = CaptureMean(0, shots, ct);
        if (!top.Ok) { res.Reason = top.Reason; return res; }

        res.Ok = true;
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        if (!here)
        {
            if (m_he)   // TODO(port): C# giả định _he còn (đã qua HandEyeNeeds); thiếu thì chỉ không ghi.
            {
                m_he->Shots = shots;
                m_he->TopX = top.X; m_he->TopY = top.Y; m_he->TopA = top.A;
                m_he->TopAxX = top.AxX; m_he->TopAxY = top.AxY;
            }
            res.Text = Loc::F("Calib.HeLocated", shots, top.X, top.Y, top.A);
        }
        else
        {
            // Soi mới là bỏ điểm đích cũ — nó tính từ pose của lần soi trước.
            HandEyeTest t;
            t.Nozzle = nozzle;
            t.HereTopX = top.X; t.HereTopY = top.Y; t.HereTopA = top.A;
            t.HereAxX = top.AxX; t.HereAxY = top.AxY;
            m_heTest = t;
            res.Text = Loc::F("Calib.HeLocatedHere",
                row >= 0 ? Loc::F("Calib.AtCell", row + 1, col + 1) : Loc::T("Calib.InPlace"),
                top.AxX, model->AssyTray->AxisRow, top.AxY, top.X, top.Y, top.A);
            // ΔX/ΔY/Δθ so với điểm teach hand-eye đã lưu (Tuấn 2026-09-30) — cả khi soi ô tray lắp ráp: khi đó Δ gồm
            // cả khoảng cách từ cơ cấu kẹp tới ô. Bản ghi cũ chưa lưu điểm teach thì so với bước 3 của phiên này.
            double tx = 0, ty = 0, ta = 0;
            if (model->Calib.TryGetHandEyeTarget(nozzle, tx, ty, ta))
                res.Text += Loc::F("Calib.VsTeachTarget", nozzle, top.X - tx, top.Y - ty, AxisAngle::ShortestDelta(ta, top.A));
            else if (row < 0 && m_he && m_he->Nozzle == nozzle && m_he->Shots > 0)
                res.Text += Loc::F("Calib.VsStep3", top.X - m_he->TopX, top.Y - m_he->TopY, AxisAngle::ShortestDelta(m_he->TopA, top.A));
            else
                res.Text += Loc::F("Calib.NoTeachTarget", nozzle);
        }
        return res;
    }

    StepRunResult Routine_Calib::HandEyeGoToAlign(int nozzle, std::stop_token ct)
    {
        return StepGuard("Go to bottom cam", [&]() { return HandEyeGoToAlignCore(nozzle, "4", ct); }, "Hand-eye");
    }

    /// <summary>Bước 7 của phép thử tổng quát — y như nút 4, đặt lại dưới nút 6 cho đúng thứ tự bấm: soi chi tiết → tới
    /// vị trí soi CAM dưới → Trigger AlignBottom → tới điểm đích (Tuấn tách 26/09).</summary>
    StepRunResult Routine_Calib::HandEyeGoToAlignForTest(int nozzle, std::stop_token ct)
    {
        return StepGuard("Go to bottom cam", [&]() { return HandEyeGoToAlignCore(nozzle, "7", ct); }, "Hand-eye");
    }

    /// <summary>Dùng chung lõi với nút 4 của khu chạy kiểm từng bước, để không có bản sao nào lệch đi. <b>Không xoay
    /// A.</b></summary>
    StepRunResult Routine_Calib::HandEyeGoToAlignCore(int nozzle, const std::string& step, std::stop_token ct)
    {
        StepRunResult res;
        res.Reason = step == "7" ? HandEyeTestNeeds(nozzle, 0) : HandEyeNeeds(nozzle, 3);
        if (!res.Reason.empty()) return res;
        return StepMoveToAlignCore(nozzle, ct, step);
    }

    /// <summary>
    /// Bước 5 — tay đang cầm chính con can vừa gắp và đứng ở vị trí soi bottom-up. App chụp AlignBottom_ToolN N ảnh với
    /// HasTarget = 0 để đọc ObjX_mm / ObjY_mm — lượng can lệch khỏi gốc hệ bottom-up — rồi ghi
    /// <c>HandEyeDx_mm = X_gắp + ObjX_mm − TopX_mm</c>.
    ///
    /// <para>Gắp lệch tâm bao nhiêu không sao: camera dưới đọc đúng lượng lệch đó. Chỉ chặn khi lệch quá ngưỡng lệch tâm
    /// — lúc ấy lần gắp không đại diện cho lần gắp thật. Thứ KHÔNG được xê dịch là <b>chi tiết đích</b>, việc của cơ cấu
    /// kẹp.</para>
    ///
    /// <para><b>Mốc góc</b> = ObjA_deg + TopA_deg — <b>CỘNG hai góc, không trừ</b> (sửa 2026-09-23): hai camera nhìn
    /// ngược chiều nhau nên đo góc ngược dấu. Công thức trừ cũ chỉ đúng ở đúng cái góc đem ra đo, nên đo một điểm không
    /// phát hiện được.</para>
    ///
    /// <para><b>Xoay A về đúng encoder lúc gắp trước khi chụp</b> (2026-09-25). Jog A đi δ độ giữa nút 2 và nút 5 thì mốc
    /// góc lệch đúng δ, và mọi lần đặt của Auto xoay lệch đều δ — lệch mà các phép kiểm khác đều không thấy.</para>
    /// </summary>
    HandEyeCommitResult Routine_Calib::HandEyeCommit(int nozzle, std::stop_token ct)
    {
        HandEyeCommitResult res;
        try
        {
            res.Reason = HandEyeNeeds(nozzle, 3);
            if (!res.Reason.empty()) return res;
            HandEyeProgress he;
            {
                std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
                if (!m_he) { res.Reason = Loc::T("Calib.HeNotStarted"); return res; }
                he = *m_he;
            }
            if (!m_func.HasProduct(nozzle))
            { res.Reason = Loc::F("Calib.NozzleLostCan", nozzle); return res; }

            auto sys = m_sys();
            auto model = McDataManager::Model();
            auto xAxis = m_motion[AxisId::X];
            if (xAxis == nullptr) { res.Reason = Loc::T("Calib.CannotReadX"); return res; }

            // X phải ĐANG ở vị trí soi bottom-up, không chỉ "đã bấm nút 4": người bấm có thể jog đi sau đó, và
            // lượng lệch tâm đọc ở chỗ khác là số của chỗ khác.
            double alignX = model->AlignPosX(nozzle), nowX = xAxis->CurrentPos();
            if (std::fabs(nowX - alignX) > std::max(0.001, sys->AlignXTolMm))
            {
                res.Reason = Loc::F("Calib.XNotAtAlignPos4", nowX, nozzle, alignX);
                return res;
            }

            // A phải ĐANG ở góc lúc gắp. Khác X, ở đây ra lệnh lại chứ không từ chối: xoay A tại chỗ soi là việc
            // phép thử vòng xoay vẫn làm, và không có ngưỡng nào để cấu hình.
            if (!MoveZToBottomCapture(nozzle, ct))
            { res.Reason = ZCaptureFail(nozzle); return res; }
            if (!Move(AName(nozzle), AxisAngle::Wrap(he.PickA), ct))
            {
                res.Reason = Loc::F("Calib.RotateToPickAngleFailed", AName(nozzle), he.PickA);
                return res;
            }

            int shots = std::max(1, sys->CalibShotsPerPoint);
            VmMeanCapture m = CaptureMean(nozzle, shots, ct);
            if (!m.Ok) { res.Reason = m.Reason; return res; }

            double ecc = std::sqrt(m.X * m.X + m.Y * m.Y);
            if (ecc > std::max(0.1, sys->MaxCompensationDistance))
            {
                res.Status = VmStatus::NoConclusion;
                res.Reason = Loc::F("Calib.EccTooLarge", ecc, sys->MaxCompensationDistance);
                return res;
            }

            // Y camera dưới ngược chiều Y2 của máy — lệch tâm Y quy về chiều máy bằng cách đổi dấu (VmCycle::BottomYSign,
            // Tuấn 2026-09-30). X đã cùng chiều.
            double eccYMachine = VmCycle::BottomYSign * m.Y;
            res.Dx = he.PickX + m.X - he.TopX;
            res.Dy = he.PickY + eccYMachine - he.TopY;
            res.AngleRef = AxisAngle::Wrap(m.A + he.TopA);    // ObjA + TopA, về [0, 360)
            if (!VmMath::IsFinite(res.Dx) || !VmMath::IsFinite(res.Dy) || !VmMath::IsFinite(m.A) || !VmMath::IsFinite(he.TopA))
            {
                res.Status = VmStatus::Invalid;
                res.Reason = Loc::F("Calib.HandEyeFail", VmStatus::AlignVerify(VmStatus::Invalid));
                return res;
            }

            res.Status = VmStatus::Ok;
            res.Ok = true;
            VisionCalib& calib = model->Calib;
            calib.SetHandEye(nozzle, res.Dx, res.Dy, res.AngleRef, Now());
            calib.SetHandEyeTarget(nozzle, he.TopX, he.TopY, he.TopA, he.TopAxX, he.TopAxY);
            res.Note = SaveModelNote();
            {
                std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
                if (m_heTest && m_heTest->Nozzle == nozzle) m_heTest.reset();   // điểm đích cũ tính bằng hand-eye cũ
            }

            Log(Loc::Format("Hand-eye nozzle #{0}: dx {1:0.000}, dy {2:0.000} mm = pick ({3:0.000}, {4:0.000}) + ecc in machine dir ({5:0.000}, {6:0.000}) − part ({7:0.000}, {8:0.000}) [bottom cam ObjY {12:0.000}, Y opposite to machine]; angle ref {9:0.00}° = can {10:0.00}° + part {11:0.00}°",
                { nozzle, res.Dx, res.Dy, he.PickX, he.PickY, m.X, eccYMachine, he.TopX, he.TopY, res.AngleRef, m.A, he.TopA, m.Y }));
            return res;
        }
        catch (const base::OperationCanceled&)
        {
            res.Reason = Loc::T("Calib.Cancelled");
            return res;
        }
        catch (const std::exception& ex)
        {
            AlarmManager::RaiseEvent(AlarmCode::RoutineCrashed, "Calib", ex.what());
            res.Reason = Loc::F("Calib.UnexpectedError", std::string(ex.what()));
            return res;
        }
    }

    StepRunResult Routine_Calib::HandEyeCheckTarget(int nozzle, std::stop_token ct)
    {
        return StepGuard("Trigger AlignBottom, compute target", [&]() { return HandEyeCheckTargetCore(nozzle, ct); }, "Hand-eye");
    }

    /// <summary>
    /// Bước 8 của <b>phép thử tổng quát</b> — tay đã đứng ở vị trí soi CAM dưới (nút 7), AlignBottom_ToolN soi con can đang
    /// cầm, Group Cmd tính điểm đặt lên pose mà nút 6 vừa đo. Không chạy X. Soi lại chính chi tiết đã gắp thì phải ra
    /// đúng encoder lúc gắp, vì <c>Cmd = TopX_mới + Dx − ObjX_mới</c> và <c>Dx = X_gắp + ObjX − TopX</c>.
    ///
    /// <para><b>KHÔNG chạy tới lệnh</b>: chỉ tính và ghi lại — nút 9 chạy tới. <b>Tắt hàng rào lưới teach</b>: phép thử
    /// không nhắm vào ô nào — cùng cách chạy khô làm.</para>
    /// </summary>
    StepRunResult Routine_Calib::HandEyeCheckTargetCore(int nozzle, std::stop_token ct)
    {
        StepRunResult res;
        auto sys = m_sys();
        const VisionOutputNames& names = sys->VisionOutputs;
        res.Reason = VmCycle::AxisVarProblem(&names);
        if (!res.Reason.empty()) return res;
        auto model = McDataManager::Model();
        if (model == nullptr) { res.Reason = Loc::T("Calib.NoModel"); return res; }
        res.Reason = HandEyeTestNeeds(nozzle, 1);
        if (!res.Reason.empty()) return res;
        if (!m_func.HasProduct(nozzle))
        { res.Reason = Loc::F("Calib.NozzleEmptyPickFirst", nozzle); return res; }

        // Không chạy X — nút 7 đưa tay tới vị trí soi (Tuấn tách 26/09). Nhưng X phải ĐANG ở đó: người bấm có thể
        // jog đi sau nút 7, và AlignBottom chụp ở chỗ khác là số của chỗ khác. Cùng phép xét với nút 5.
        auto xAxis = m_motion[AxisId::X];
        if (xAxis == nullptr) { res.Reason = Loc::T("Calib.CannotReadX"); return res; }
        double alignX = model->AlignPosX(nozzle), nowX = xAxis->CurrentPos();
        if (std::fabs(nowX - alignX) > std::max(0.001, sys->AlignXTolMm))
        {
            res.Reason = Loc::F("Calib.XNotAtAlignPos7", nowX, nozzle, alignX);
            return res;
        }
        if (!MoveZToBottomCapture(nozzle, ct))
        { res.Reason = ZCaptureFail(nozzle); return res; }

        PlacedTopRecord target;
        {
            std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
            if (!m_heTest) { res.Reason = Loc::F("Calib.NeedLocateHere", nozzle); return res; }
            target.X = m_heTest->HereTopX;
            target.Y = m_heTest->HereTopY;
            target.A = m_heTest->HereTopA;
        }

        m_func.WaitSettle(EVmCamera::BottomUp, ct);
        int rid = VmRunIds::Next();
        auto g = VmCycle::AxisGlobals(m_motion, names, rid);
        VmCycle::AddPlaceGlobals(g, &target, &model->Calib, nozzle);
        double aSent = VmCycle::Val(g, nozzle == 1 ? names.GvAxisA1 : names.GvAxisA2);
        VmCallRequest req = VmCycle::AlignBottomRequest(names, nozzle, g, true);
        VmCallResult r = CallVm(req, ct);
        VmCameraRead read = VmCycle::ReadAlignBottom(r, req, names, nozzle);
        VisionReadout::Tool(nozzle, read, static_cast<const VmPlaceCommand*>(nullptr));
        VmAlignTrace trace = VmCycle::TraceAlignBottom(r, req, names, nozzle, Pos(ZName(nozzle)));
        Log("General test nozzle #" + std::to_string(nozzle) + " — " + trace.Line,
            trace.Checked && !trace.Match ? LogLevel::Warning : LogLevel::Info);
        if (!read.CallOk) { res.Reason = "AlignBottom: " + read.CallError; return res; }
        if (!read.EchoOk) { res.Reason = "AlignBottom: " + read.EchoIssue; return res; }
        if (!read.Found) { res.Reason = Loc::F("Calib.AlignNoCan", nozzle); return res; }
        if (!read.VmValid) { res.Reason = "AlignBottom: " + read.VmIssue; return res; }

        VmPlaceCommand cmd = VmCycle::CheckPlace(r, req, nozzle, aSent, 0, 0, 0, sys->MaxCompensationAngleDeg,
            sys->MaxCompensationDistance, SoftLimitOf(sys->GetAxis(AxisId::X)), SoftLimitOf(sys->GetAxis(AxisId::Y2)), false);
        VisionReadout::Tool(nozzle, read, cmd);
        if (cmd.Route != EVmRoute::Ok) { res.Reason = RouteText(cmd.Route) + ": " + cmd.Why; return res; }

        res.Ok = true;
        res.TargetX = cmd.CmdX;
        res.TargetY2 = cmd.CmdY2;
        res.TargetA = cmd.CmdA;
        {
            std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
            if (m_heTest)
            {
                m_heTest->HasTarget = true;
                m_heTest->TargetX = cmd.CmdX;
                m_heTest->TargetY = cmd.CmdY2;
                m_heTest->TargetA = cmd.CmdA;
            }
        }
        res.Text = Loc::F("Calib.HeTarget", cmd.CmdX, model->AssyTray->AxisRow, cmd.CmdY2, AName(nozzle), cmd.CmdA, cmd.DiagDA) +
                   HandEyeVsPick(nozzle, cmd.CmdX, cmd.CmdY2, model->AssyTray->AxisRow) +
                   Loc::T("Calib.HeTargetHint");
        return res;
    }

    StepRunResult Routine_Calib::HandEyeGoToTarget(int nozzle, std::stop_token ct)
    {
        return StepGuard("Go to computed target", [&]() { return HandEyeGoToTargetCore(nozzle, ct); }, "Hand-eye");
    }

    /// <summary>
    /// Bước 9 của phép thử tổng quát (Tuấn yêu cầu 2026-09-26) — chạy X, Y2, A tới đúng điểm đích nút 8 vừa tính, rồi hạ
    /// Z tới <b>điểm giảm tốc</b> của tray lắp ráp và dừng ở đó: con can lơ lửng ngay trên chi tiết, không chạm. Không nhả
    /// hút.
    ///
    /// <para><b>Chỉ tới điểm giảm tốc, không xuống cao độ đặt</b> (Tuấn chọn): điểm tính sai thì hạ hết là ấn con can vào
    /// chi tiết. Chưa teach điểm giảm tốc thì từ chối, không tự bịa một cao độ nào.</para>
    ///
    /// <para><b>Không chặn theo khoảng cách tới chỗ đã gắp</b>: cơ cấu kẹp gá nhiều sản phẩm, điểm đích cách chỗ gắp vài
    /// chục mm là đúng.</para>
    /// </summary>
    StepRunResult Routine_Calib::HandEyeGoToTargetCore(int nozzle, std::stop_token ct)
    {
        StepRunResult res;
        auto model = McDataManager::Model();
        if (model == nullptr) { res.Reason = Loc::T("Calib.NoModel"); return res; }
        res.Reason = HandEyeTestNeeds(nozzle, 2);
        if (!res.Reason.empty()) return res;
        if (!m_func.HasProduct(nozzle))
        { res.Reason = Loc::F("Calib.NozzleEmptyPickFirst", nozzle); return res; }
        HandEyeTest t;
        {
            std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
            if (!m_heTest) { res.Reason = Loc::F("Calib.NeedLocateHere", nozzle); return res; }
            t = *m_heTest;
        }

        auto tray = model->AssyTray;
        std::string zName = ZName(nozzle);
        if (!tray->ZSlowValid(zName))
        {
            res.Reason = Loc::F("Calib.NoZSlowTaught", zName);
            return res;
        }
        double zHover = tray->ZSlowFor(zName);

        // Y2 tới điểm đích ở mức Auto thấp như chặng đặt của Auto (Tuấn 01/10).
        if (!m_func.MoveToCellForPick(nozzle, t.TargetA, tray->AxisRow, t.TargetY, t.TargetX, ct, true))
        { res.Reason = Loc::F("Calib.MoveToTargetFailed", tray->AxisRow, AName(nozzle)); return res; }
        if (!Move(zName, zHover, ct))
        { res.Reason = Loc::F("Calib.ZToSlowFailed", zName); return res; }

        res.Ok = true;
        res.TargetX = t.TargetX;
        res.TargetY2 = t.TargetY;
        res.TargetA = t.TargetA;
        res.Text = Loc::F("Calib.HeAtTarget", nozzle, t.TargetX, tray->AxisRow, t.TargetY, AName(nozzle), t.TargetA, zName, zHover) +
                   HandEyeVsPick(nozzle, t.TargetX, t.TargetY, tray->AxisRow) +
                   Loc::T("Calib.HeAtTargetHint");
        return res;
    }

    StepRunResult Routine_Calib::HandEyeReturn(int nozzle, std::stop_token ct)
    {
        return StepGuard("Return can to jig", [&]() { return HandEyeReturnCore(nozzle, ct); }, "Hand-eye");
    }

    /// <summary>
    /// Trả con can mẫu về cơ cấu kẹp. <b>Xoay A về góc hút</b> cùng lúc chạy X/Y2 — con can được nhấc lên ở góc đó; hạ
    /// xuống ở góc khác thì con can không ngồi vào chi tiết. <b>Không đòi bước nào trước đó</b>, để bỏ dở ở đâu cũng gọi
    /// được. Xoá sổ <b>chỉ khi</b> cảm biến thật sự nhả; còn báo có hàng thì nói cho người vận hành, KHÔNG phát alarm:
    /// đây là trang hiệu chuẩn, người đang đứng ngay đó; Auto mới phải dừng máy.
    /// </summary>
    StepRunResult Routine_Calib::HandEyeReturnCore(int nozzle, std::stop_token ct)
    {
        StepRunResult res;
        auto model = McDataManager::Model();
        if (model == nullptr) { res.Reason = Loc::T("Calib.NoModel"); return res; }
        if (!m_func.HasProduct(nozzle))
        { res.Reason = Loc::F("Calib.NozzleHoldsNothing", nozzle); return res; }
        res.Reason = model->HandEyeJigProblem(nozzle);
        if (!res.Reason.empty()) return res;

        auto tray = model->AssyTray;
        TeachPoint pick = model->HandEyePick(nozzle);
        std::string zName = ZName(nozzle);

        if (!m_func.EnsureZSafe(ct))
        { res.Reason = Loc::T("Calib.ZSafeFailed"); return res; }
        if (!m_func.MoveToCellForPick(nozzle, model->PickAngle(nozzle), tray->AxisRow, pick.Y, pick.X, ct))
        { res.Reason = Loc::T("Calib.GoToJigPickFailed"); return res; }
        if (!m_func.MoveZDown(zName, tray->ZPickFor(zName), tray->ZSlowFor(zName), ct))
        { res.Reason = Loc::F("Calib.ZDownPlaceFailed", zName); return res; }

        m_func.SetVacuum(nozzle, false);
        m_func.SetBlow(nozzle, true);
        bool released = WaitVacuum(nozzle, false, ct);
        base::Delay(m_sys()->BlowTimeMs, ct);
        m_func.SetBlow(nozzle, false);

        if (!m_func.MoveZUp(zName, m_motion.ZSafe(), tray->ZSlowFor(zName), ct))
        { res.Reason = Loc::F("Calib.ZUpAfterReturnFailed", zName); return res; }

        if (!released)
        {
            // Dừng TRƯỚC khi ghi sổ: ghi tay hút thành trống trong lúc con can còn dính trên đầu hút là để lần
            // hạ sau đưa một tay "trống" đang cầm hàng xuống chỗ đã có hàng.
            res.Reason = Loc::T("Calib.StillHolding");
            return res;
        }

        auto run = McDataManager::RunData();
        auto nz = nozzle == 1 ? run->Nozzle1 : run->Nozzle2;
        nz->HasProduct = false;
        nz->SourceCellIndex = -1;
        nz->Aligned = false;
        McDataManager::SaveRunningData();
        {
            std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
            if (m_he && m_he->Nozzle == nozzle) m_he.reset();
            if (m_heTest && m_heTest->Nozzle == nozzle) m_heTest.reset();
        }

        res.Ok = true;
        res.Text = Loc::F("Calib.Returned", nozzle, pick.X, tray->AxisRow, pick.Y, AName(nozzle), model->PickAngle(nozzle),
                          tray->ZPickFor(zName));
        return res;
    }

    /// <summary>
    /// Bước 5b — chạy khô với sản phẩm mẫu đang trên tay: LocateTop soi dấu chuẩn và coi dấu là ô đích; tay tới vị trí
    /// soi, AlignBottom_ToolN soi sản phẩm rồi Group Cmd tính lệnh đặt lên dấu trong cùng lần chạy; App đưa tay tới lệnh ở
    /// Z an toàn. Tâm sản phẩm phải nằm trên dấu chuẩn. Không hạ Z. Đây là phép duy nhất bắt được Group Cmd nối sai hoặc
    /// hand-eye đo sai ở bước xác định hand-eye.
    /// </summary>
    DryRunPlaceResult Routine_Calib::DryRunPlaceAlign(int nozzle, std::stop_token ct)
    {
        DryRunPlaceResult res;
        try
        {
            auto sys = m_sys();
            const VisionOutputNames& names = sys->VisionOutputs;
            auto model = McDataManager::Model();
            res.Reason = VmCycle::AxisVarProblem(&names);
            if (!res.Reason.empty()) return res;

            CheckCalib();   // cổng Auto — cập nhật dòng trạng thái của trang
            VmCalibGate gate = VmCycle::EvaluateCalib(model.get(), sys.get(), nozzle);
            if (!gate.Block.empty()) { res.Reason = Loc::F("Calib.CalibNotReady", nozzle, gate.Block); return res; }

            m_func.WaitSettle(EVmCamera::TopDown, ct);
            int rid = VmRunIds::Next();
            auto g = VmCycle::AxisGlobals(m_motion, names, rid);
            VmCallResult markCall = CallVm(VmCycle::LocateRequest(names, g), ct);
            VmCameraRead mark = VmCycle::ReadLocate(markCall, g, names);
            VisionReadout::TopDown(mark);
            if (!mark.CallOk) { res.Reason = "LocateTop: " + mark.CallError; return res; }
            if (!mark.EchoOk) { res.Reason = "LocateTop: " + mark.EchoIssue; return res; }
            if (!mark.Found) { res.Reason = Loc::T("Calib.LocateNoMark"); return res; }
            if (!mark.VmValid) { res.Reason = "LocateTop: " + mark.VmIssue; return res; }
            res.CamX = mark.X;
            res.CamY = mark.Y;

            if (!m_func.EnsureZSafe(ct))
            { res.Reason = Loc::T("Calib.ZSafeFailed"); return res; }
            if (!MoveZToBottomCapture(nozzle, ct))
            { res.Reason = ZCaptureFail(nozzle); return res; }
            if (!Move(AxisId::X, model->AlignPosX(nozzle), ct))
            { res.Reason = Loc::F("Calib.GoToAlignPosFailed", nozzle); return res; }

            m_func.WaitSettle(EVmCamera::BottomUp, ct);
            int rid2 = VmRunIds::Next();
            auto g2 = VmCycle::AxisGlobals(m_motion, names, rid2);
            PlacedTopRecord markRec;
            markRec.X = mark.X;
            markRec.Y = mark.Y;
            markRec.A = mark.A;
            VmCycle::AddPlaceGlobals(g2, &markRec, &model->Calib, nozzle);
            double aSent = VmCycle::Val(g2, nozzle == 1 ? names.GvAxisA1 : names.GvAxisA2);
            VmCallRequest req = VmCycle::AlignBottomRequest(names, nozzle, g2, true);
            VmCallResult r = CallVm(req, ct);
            VmCameraRead read = VmCycle::ReadAlignBottom(r, req, names, nozzle);
            VisionReadout::Tool(nozzle, read, static_cast<const VmPlaceCommand*>(nullptr));
            VmAlignTrace trace = VmCycle::TraceAlignBottom(r, req, names, nozzle, Pos(ZName(nozzle)));
            Log("Dry run nozzle #" + std::to_string(nozzle) + " — " + trace.Line,
                trace.Checked && !trace.Match ? LogLevel::Warning : LogLevel::Info);
            res.CmdCheck = trace.Check;
            if (!read.CallOk) { res.Reason = "AlignBottom: " + read.CallError; return res; }
            if (!read.EchoOk) { res.Reason = "AlignBottom: " + read.EchoIssue; return res; }
            if (!read.Found) { res.Reason = Loc::F("Calib.AlignNoProduct", nozzle); return res; }
            if (!read.VmValid) { res.Reason = "AlignBottom: " + read.VmIssue; return res; }

            VmPlaceCommand cmd = VmCycle::CheckPlace(r, req, nozzle, aSent, 0, 0, 0, sys->MaxCompensationAngleDeg, sys->MaxCompensationDistance,
                SoftLimitOf(sys->GetAxis(AxisId::X)), SoftLimitOf(sys->GetAxis(AxisId::Y2)), false);
            VisionReadout::Tool(nozzle, read, cmd);

            // Ba góc làm nên góc bù — đi kèm mọi kết cục, cả trên bảng báo lẫn trong log (Tuấn gặp 2026-09-22: góc bù
            // 179.81° mà bảng chỉ có một con số, không biết góc nào lệch).
            std::string angles = Loc::F("Calib.DryRunAngles",
                VmCycle::Val(g2, VmVars::CellA), mark.A, model->Calib.HandEyeAngleRef(nozzle), read.A, AName(nozzle), aSent);
            Log(Loc::Format("Dry run nozzle #{0}: mark ({1:0.000}, {2:0.000}); product off rotation center ({3:0.000}, {4:0.000}); {5}; Group Cmd Status {6}, comp angle {7:+0.00;-0.00}°, cmd X {8:0.000}, Y2 {9:0.000}, A {10:0.00} — {11}",
                { nozzle, mark.X, mark.Y, read.X, read.Y, angles, cmd.Status, cmd.DiagDA, cmd.CmdX, cmd.CmdY2, cmd.CmdA,
                  cmd.Route == EVmRoute::Ok ? std::string("moving to command") : RouteText(cmd.Route) + ": " + cmd.Why }),
                cmd.Route == EVmRoute::Ok ? LogLevel::Info : LogLevel::Warning);
            if (cmd.Route != EVmRoute::Ok)
            {
                res.Reason = RouteText(cmd.Route) + ": " + cmd.Why + "\n" + angles + (trace.Check.empty() ? "" : "\n" + trace.Check);
                return res;
            }

            res.CmdX = cmd.CmdX;
            res.CmdY2 = cmd.CmdY2;
            res.CmdA = cmd.CmdA;

            std::string aName = nozzle == 1 ? AxisId::A1 : AxisId::A2;
            if (!Move(aName, AxisAngle::Wrap(cmd.CmdA), ct))
            { res.Reason = Loc::F("Calib.RotateAxisToCmdFailed", aName); return res; }
            if (!m_func.MoveToCell(AxisId::Y2, cmd.CmdY2, cmd.CmdX, ct, true))
            { res.Reason = Loc::F("Calib.MoveXYToCmdFailed", AxisId::Y2); return res; }

            res.Ok = true;
            Log(Loc::Format("Dry run place nozzle #{0}: mark ({1:0.000}, {2:0.000}, {6:0.00}°); product off rotation center ({7:0.000}, {8:0.000}), angle {9:0.00}°, captured at X {10:0.000}, {11} {12:0.000}°, {13} {14:0.000}; comp angle {15:+0.00;-0.00}° → cmd X {3:0.000}, Y2 {4:0.000}, A {5:0.00}",
                { nozzle, res.CamX, res.CamY, res.CmdX, res.CmdY2, res.CmdA, mark.A, read.X, read.Y, read.A,
                  VmCycle::Val(g2, names.GvAxisX), AName(nozzle), aSent, ZName(nozzle), Pos(ZName(nozzle)), cmd.DiagDA }));
            return res;
        }
        catch (const base::OperationCanceled&)
        {
            res.Reason = Loc::T("Calib.Cancelled");
            return res;
        }
        catch (const std::exception& ex)
        {
            AlarmManager::RaiseEvent(AlarmCode::RoutineCrashed, "Calib", ex.what());
            res.Reason = Loc::F("Calib.UnexpectedError", std::string(ex.what()));
            return res;
        }
    }

    // ───────────────────────── Chạy kiểm từng bước (trang Vision Edit) ─────────────────────────
    //
    // Sáu nút chạy đúng trình tự của một vòng Auto — S05 (soi ô) → S1x (hút) → S20 (soi sản phẩm, tính lệnh)
    // → S3x (tới điểm đặt) — nhưng mỗi bước một nút, dừng lại giữa các bước để đọc số. KHÔNG hạ Z và KHÔNG
    // nhả hút ở bước cuối: chỗ nguy hiểm nhất của chu trình là lần hạ xuống đặt, và mọi sai lệch cần nhìn đều
    // đọc được ở trên cao rồi (Tuấn yêu cầu 2026-09-14).
    //
    // Phép tính đi đúng đường của Auto, không có đường tắt nào: cùng AddPlaceGlobals (kèm bù chung + bù riêng của ô),
    // cùng AlignBottomRequest, cùng CheckPlace kể cả hàng rào lưới teach — chạy kiểm mà đi đường khác thì
    // không kết luận được gì về đường chạy thật.

    bool Routine_Calib::StepHasTop() const
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        return m_stepTop.has_value();
    }

    bool Routine_Calib::StepHasCommand() const
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        return m_stepCmd.has_value();
    }

    std::string Routine_Calib::StepGuardWarn() const
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        return m_stepGuardWarn;
    }

    void Routine_Calib::StepClear()
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_stepTop.reset();
        m_stepRow = -1;
        m_stepCol = -1;
        m_stepCmd.reset();
        m_stepCmdNozzle = 0;
        m_stepGuardWarn.clear();
    }

    StepRunResult Routine_Calib::StepMoveCamToCell(int row, int col, std::stop_token ct)
    {
        return StepGuard("CAM to assy cell", [&]() { return StepMoveCamCore(row, col, ct); });
    }

    StepRunResult Routine_Calib::StepLocateTop(int row, int col, std::stop_token ct)
    {
        return StepGuard("Trigger LocateTop", [&]() { return StepLocateCore(row, col, ct); });
    }

    StepRunResult Routine_Calib::StepPick(int nozzle, int row, int col, std::stop_token ct)
    {
        return StepGuard("Pick product", [&]() { return StepPickCore(nozzle, row, col, ct); });
    }

    StepRunResult Routine_Calib::StepMoveToAlign(int nozzle, std::stop_token ct)
    {
        return StepGuard("Go to bottom cam", [&]() { return StepMoveToAlignCore(nozzle, ct); });
    }

    StepRunResult Routine_Calib::StepAlignBottom(int nozzle, std::stop_token ct)
    {
        return StepGuard("Trigger AlignBottom", [&]() { return StepAlignCore(nozzle, ct); });
    }

    /// <summary>
    /// Nút "Z1 → Z_SAFE" / "Z2 → Z_SAFE" ở thanh Jog. Trước 2026-09-23 hai nút này chạy VỀ GỐC trục Z; Tuấn đổi sang lệnh
    /// chạy tới Z_SAFE — một lệnh Move tới toạ độ chứ không phải chu trình về gốc. Origin đã về gốc Z rồi, nên nút về gốc
    /// lẻ ở trang hiệu chuẩn chỉ tổ bấm nhầm giữa chừng. Đổi lại: nút này đòi đã về gốc (cổng OrgOk của trang).
    /// </summary>
    StepRunResult Routine_Calib::StepMoveZSafe(int nozzle, std::stop_token ct)
    {
        return StepGuard(ZName(nozzle) + " to Z safe", [&]() { return StepMoveZSafeCore(nozzle, ct); });
    }

    StepRunResult Routine_Calib::StepMoveZSafeCore(int nozzle, std::stop_token ct)
    {
        StepRunResult res;
        if (!MoveZToBottomCapture(nozzle, ct))
        { res.Reason = Loc::F("Calib.ZSafeNotReached", ZName(nozzle)); return res; }

        res.Ok = true;
        res.Text = Loc::F("Calib.ZAtSafe", ZName(nozzle), Pos(ZName(nozzle)));
        return res;
    }

    StepRunResult Routine_Calib::StepMoveToCommand(int nozzle, std::stop_token ct)
    {
        return StepGuard("Go to place point", [&]() { return StepMoveToCommandCore(nozzle, ct); });
    }

    StepRunResult Routine_Calib::StepMoveCamCore(int row, int col, std::stop_token ct)
    {
        StepRunResult res;
        auto model = McDataManager::Model();
        if (model == nullptr) { res.Reason = Loc::T("Calib.NoModel"); return res; }
        auto tray = model->AssyTray;
        if (row < 0 || row >= tray->Rows() || col < 0 || col >= tray->Cols())
        { res.Reason = Loc::F("Calib.CellOutsideAssyGrid", tray->Rows(), tray->Cols()); return res; }
        if (!tray->CamTopDownAnchorTaught)
        { res.Reason = Loc::T("Calib.AssyAnchorNotTaught"); return res; }

        // Cùng phép tính với S05 — gọi chung TrayLayout::CamPosFor để không có bản sao nào lệch đi.
        TeachPoint cam = tray->CamPosFor(row, col);
        double camX = cam.X, camY = cam.Y;

        if (!m_func.MoveToCell(tray->AxisRow, camY, camX, ct))
        { res.Reason = Loc::T("Calib.GoToCellCamFailed"); return res; }

        // Camera đổi chỗ thì số LocateTop cũ là của ô khác — bỏ luôn, kèm lệnh đã tính từ nó.
        StepClear();
        res.Ok = true;
        res.Text = Loc::F("Calib.StepCamAtCell", row + 1, col + 1, camX, tray->AxisRow, camY);
        return res;
    }

    StepRunResult Routine_Calib::StepLocateCore(int row, int col, std::stop_token ct)
    {
        StepRunResult res;
        auto sys = m_sys();
        const VisionOutputNames& names = sys->VisionOutputs;
        res.Reason = VmCycle::AxisVarProblem(&names);
        if (!res.Reason.empty()) return res;
        auto model = McDataManager::Model();
        if (model == nullptr) { res.Reason = Loc::T("Calib.NoModel"); return res; }
        auto tray = model->AssyTray;
        if (row < 0 || row >= tray->Rows() || col < 0 || col >= tray->Cols())
        { res.Reason = Loc::F("Calib.CellOutsideAssyGrid", tray->Rows(), tray->Cols()); return res; }

        m_func.WaitSettle(EVmCamera::TopDown, ct);
        int rid = VmRunIds::Next();
        auto g = VmCycle::AxisGlobals(m_motion, names, rid);
        VmCallResult call = CallVm(VmCycle::LocateRequest(names, g), ct);
        VmCameraRead read = VmCycle::ReadLocate(call, g, names);
        VisionReadout::TopDown(read, row, col);
        if (!read.CallOk) { res.Reason = "LocateTop: " + read.CallError; return res; }
        if (!read.EchoOk) { res.Reason = "LocateTop: " + read.EchoIssue; return res; }
        if (!read.Found) { res.Reason = Loc::T("Calib.LocateNoTargetInCell"); return res; }
        if (!read.VmValid) { res.Reason = "LocateTop: " + read.VmIssue; return res; }

        {
            std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
            PlacedTopRecord top;
            top.X = read.X;
            top.Y = read.Y;
            top.A = read.A;
            m_stepTop = top;
            m_stepRow = row;
            m_stepCol = col;
            m_stepCmd.reset();
        }
        res.Ok = true;
        res.Text = Loc::F("Calib.StepLocated", row + 1, col + 1, read.X, read.Y, read.A);
        return res;
    }

    StepRunResult Routine_Calib::StepPickCore(int nozzle, int row, int col, std::stop_token ct)
    {
        StepRunResult res;
        auto model = McDataManager::Model();
        if (model == nullptr) { res.Reason = Loc::T("Calib.NoModel"); return res; }
        auto tray = model->InputTray;
        if (row < 0 || row >= tray->Rows() || col < 0 || col >= tray->Cols())
        { res.Reason = Loc::F("Calib.CellOutsideInputGrid", tray->Rows(), tray->Cols()); return res; }
        if (m_func.HasProduct(nozzle))
        { res.Reason = Loc::F("Calib.NozzleBusyPick", nozzle); return res; }

        std::string zName = nozzle == 1 ? AxisId::Z1 : AxisId::Z2;
        TeachPoint cell = tray->CellPos(zName, row, col);
        if (!m_func.MoveToCellForPick(nozzle, model->PickAngle(nozzle), tray->AxisRow, cell.Y, cell.X, ct))
        { res.Reason = Loc::T("Calib.GoToPickCellFailed"); return res; }
        if (!m_func.MoveZDown(zName, tray->ZPickFor(zName), tray->ZSlowFor(zName), ct))
        { res.Reason = Loc::F("Calib.ZDownPickFailed", zName); return res; }

        m_func.SetVacuum(nozzle, true);
        bool gripped = WaitVacuum(nozzle, true, ct);
        SettleVacuum(gripped, ct);

        if (!m_func.MoveZUp(zName, m_motion.ZSafe(), tray->ZSlowFor(zName), ct))
        { res.Reason = Loc::F("Calib.ZUpFailed", zName); return res; }
        if (!gripped)
        {
            m_func.SetVacuum(nozzle, false);
            res.Reason = Loc::T("Calib.PickMissed");
            return res;
        }

        // Ghi sổ đúng như S1x. Không ghi thì lần START sau ResumeIntegrityOk thấy cảm biến khác phần mềm,
        // phát alarm và bắt RESET — sản phẩm do chạy kiểm để lại trên tay là trạng thái thật của máy.
        auto run = McDataManager::RunData();
        auto nz = nozzle == 1 ? run->Nozzle1 : run->Nozzle2;
        int index = run->InputTray ? CellIndexOf(*run->InputTray, row, col) : -1;
        nz->HasProduct = true;
        nz->Aligned = false;
        nz->SourceCellIndex = index;
        if (index >= 0) run->InputTray->Set(index, ECellState::Processing);
        McDataManager::SaveRunningData();

        res.Ok = true;
        res.Text = Loc::F("Calib.StepPicked", nozzle, row + 1, col + 1, cell.X, tray->AxisRow, cell.Y, tray->ZPickFor(zName));
        return res;
    }

    StepRunResult Routine_Calib::StepMoveToAlignCore(int nozzle, std::stop_token ct, const std::string& step)
    {
        StepRunResult res;
        auto model = McDataManager::Model();
        if (model == nullptr) { res.Reason = Loc::T("Calib.NoModel"); return res; }

        double alignX = model->AlignPosX(nozzle);
        if (!m_func.EnsureZSafe(ct))
        { res.Reason = Loc::T("Calib.ZSafeFailed"); return res; }
        if (!MoveZToBottomCapture(nozzle, ct))
        { res.Reason = ZCaptureFail(nozzle); return res; }
        if (!Move(AxisId::X, alignX, ct))
        { res.Reason = Loc::F("Calib.GoToAlignPosFailed", nozzle); return res; }

        res.Ok = true;
        res.Text = Loc::F("Calib.StepAtAlign", nozzle, alignX, nozzle == 1 ? AxisId::Z1 : AxisId::Z2, m_motion.ZSafe(), step);
        return res;
    }

    StepRunResult Routine_Calib::StepAlignCore(int nozzle, std::stop_token ct)
    {
        StepRunResult res;
        auto sys = m_sys();
        const VisionOutputNames& names = sys->VisionOutputs;
        res.Reason = VmCycle::AxisVarProblem(&names);
        if (!res.Reason.empty()) return res;
        auto model = McDataManager::Model();
        if (model == nullptr) { res.Reason = Loc::T("Calib.NoModel"); return res; }
        PlacedTopRecord stepTop;
        int stepRow, stepCol;
        {
            std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
            if (!m_stepTop) { res.Reason = Loc::T("Calib.NeedTopPose"); return res; }
            stepTop = *m_stepTop;
            stepRow = m_stepRow;
            stepCol = m_stepCol;
        }

        std::string zAxis = nozzle == 1 ? AxisId::Z1 : AxisId::Z2;
        CellOffset trim = model->AssyTray->PlaceTrim(zAxis, stepRow, stepCol);

        if (!MoveZToBottomCapture(nozzle, ct))
        { res.Reason = ZCaptureFail(nozzle); return res; }
        m_func.WaitSettle(EVmCamera::BottomUp, ct);
        int rid = VmRunIds::Next();
        auto g = VmCycle::AxisGlobals(m_motion, names, rid);
        VmCycle::AddPlaceGlobals(g, &stepTop, &model->Calib, nozzle, &trim);
        double aSent = VmCycle::Val(g, nozzle == 1 ? names.GvAxisA1 : names.GvAxisA2);
        VmCallRequest req = VmCycle::AlignBottomRequest(names, nozzle, g, true);
        VmCallResult r = CallVm(req, ct);
        VmCameraRead read = VmCycle::ReadAlignBottom(r, req, names, nozzle);
        VisionReadout::Tool(nozzle, read, static_cast<const VmPlaceCommand*>(nullptr), stepRow, stepCol);
        VmAlignTrace trace = VmCycle::TraceAlignBottom(r, req, names, nozzle, Pos(zAxis));
        Log("Step check — 5. " + trace.Line, trace.Checked && !trace.Match ? LogLevel::Warning : LogLevel::Info);
        if (!read.CallOk) { res.Reason = "AlignBottom: " + read.CallError; return res; }
        if (!read.EchoOk) { res.Reason = "AlignBottom: " + read.EchoIssue; return res; }
        if (!read.Found) { res.Reason = Loc::F("Calib.AlignNoProduct", nozzle); return res; }
        if (!read.VmValid) { res.Reason = "AlignBottom: " + read.VmIssue; return res; }

        TeachPoint cellPos = model->AssyTray->CellPos(zAxis, stepRow, stepCol);
        double guardX = cellPos.X + trim.PlaceDx;
        double guardY = cellPos.Y + trim.PlaceDy;
        std::string yName = model->AssyTray->AxisRow;
        auto xCfg = sys->GetAxis(AxisId::X);
        auto yCfg = sys->GetAxis(yName);
        VmPlaceCommand cmd = VmCycle::CheckPlace(r, req, nozzle, aSent, guardX, guardY, sys->PlaceTargetGuardMm,
            sys->MaxCompensationAngleDeg, sys->MaxCompensationDistance, SoftLimitOf(xCfg), SoftLimitOf(yCfg), true);
        VisionReadout::Tool(nozzle, read, cmd, stepRow, stepCol);

        {
            std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
            m_stepCmd.reset();
            m_stepGuardWarn.clear();
        }
        if (cmd.Route != EVmRoute::Ok && cmd.Route != EVmRoute::Skip)
        { res.Reason = cmd.Why + (trace.Check.empty() ? "" : "\n" + trace.Check); return res; }

        // Nhánh lệch lưới (G) dừng CheckPlace ngay ở phép chặn (f), chưa tới phép soft limit (g) — mà bước 6
        // vẫn cho chạy tới lệnh đó nếu người bấm xác nhận, nên phải tự kiểm ở đây. Không có chỗ nào khác.
        std::string guardWarn;
        if (cmd.Route == EVmRoute::Skip)
        {
            if (!xCfg->InSoftLimit(cmd.CmdX) || !yCfg->InSoftLimit(cmd.CmdY2))
            { res.Reason = Loc::F("Calib.GuardOutsideSoftLimit", cmd.Why); return res; }
            res.GuardWarn = cmd.Why;
            guardWarn = cmd.Why;
        }

        {
            std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
            m_stepGuardWarn = guardWarn;
            m_stepCmd = cmd;
            m_stepCmdNozzle = nozzle;
            m_stepCmdACap = aSent;
        }
        res.Ok = true;
        res.HasCommand = true;
        res.Text = Loc::F("Calib.StepAligned", nozzle, read.X, read.Y, read.A, cmd.CmdX, yName, cmd.CmdY2, cmd.CmdA, cmd.DiagDA,
                          cmd.DiagEcc, cmd.DiagTarX, cmd.DiagTarY) + (trace.Check.empty() ? "" : "\n" + trace.Check);
        return res;
    }

    StepRunResult Routine_Calib::StepMoveToCommandCore(int nozzle, std::stop_token ct)
    {
        StepRunResult res;
        auto model = McDataManager::Model();
        if (model == nullptr) { res.Reason = Loc::T("Calib.NoModel"); return res; }
        VmPlaceCommand stepCmd;
        double aCap = 0;
        {
            std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
            if (!m_stepCmd || m_stepCmdNozzle != nozzle)
            { res.Reason = Loc::F("Calib.NeedCommand", nozzle); return res; }
            stepCmd = *m_stepCmd;
            aCap = m_stepCmdACap;
        }

        // CmdA là góc TUYỆT ĐỐI tính từ góc lúc chụp: trục A xoay sau lúc chụp là lệnh hết đúng (nhánh R của S3x).
        std::string aName = nozzle == 1 ? AxisId::A1 : AxisId::A2;
        auto aAxis = m_motion[aName];
        double aNow = aAxis == nullptr ? DoubleNaN : aAxis->CurrentPos();
        if (!VmMath::IsFinite(aNow) || AxisAngle::Distance(aNow, aCap) > m_sys()->ACapTolDeg)
        {
            res.Reason = Loc::F("Calib.ARotatedSinceCapture", aName, aCap, aNow);
            return res;
        }

        std::string yName = model->AssyTray->AxisRow;
        if (!m_func.EnsureZSafe(ct))
        { res.Reason = Loc::T("Calib.ZSafeFailed"); return res; }
        if (!Move(aName, AxisAngle::Wrap(stepCmd.CmdA), ct))
        { res.Reason = Loc::F("Calib.RotateAxisToCmdFailed", aName); return res; }
        if (!m_func.MoveToCell(yName, stepCmd.CmdY2, stepCmd.CmdX, ct, true))
        { res.Reason = Loc::F("Calib.MoveXYToCmdFailed", yName); return res; }

        // Trục A vừa bị CHÍNH bước này xoay tới lệnh; không dời mốc thì bấm lại bước 6 lần nữa là tự mình
        // chặn mình. Lệnh góc là góc tuyệt đối nên chạy lại đúng lệnh đó không đổi kết quả; jog tay sau đó
        // vẫn bị bắt như cũ.
        {
            std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
            m_stepCmdACap = AxisAngle::Wrap(stepCmd.CmdA);
        }

        res.Ok = true;
        res.Text = Loc::F("Calib.StepAtCommand", nozzle, Pos(AxisId::X), yName, Pos(yName), aName, Pos(aName));
        return res;
    }

    // ───────────────────────── tiện ích ─────────────────────────

    /// <summary>Vỏ chung của các nút chạy từng bước: bắt lỗi như mọi routine khác, và ghi log MỘT dòng cho mỗi lần bấm —
    /// chuỗi log đó chính là biên bản của lần chạy.</summary>
    StepRunResult Routine_Calib::StepGuard(const std::string& what, const std::function<StepRunResult()>& body,
                                           const std::string& logTag)
    {
        StepRunResult res;
        try
        {
            res = body();
        }
        catch (const base::OperationCanceled&)
        {
            res = StepRunResult{};
            res.Reason = Loc::T("Calib.Cancelled");
        }
        catch (const std::exception& ex)
        {
            AlarmManager::RaiseEvent(AlarmCode::RoutineCrashed, "Calib", ex.what());
            res = StepRunResult{};
            res.Reason = Loc::F("Calib.UnexpectedError", std::string(ex.what()));
        }

        Log(logTag + " — " + (res.Ok ? res.Text : what + " stopped: " + res.Reason),
            res.Ok ? LogLevel::Info : LogLevel::Warning);
        return res;
    }

    /// <summary>Chờ lực hút lên đủ sau khi cảm biến đã báo hút được, rồi mới cho nâng Z (SystemData::VacuumSettleMs). Hút
    /// hụt thì không chờ — đằng nào cũng đi thử lại.</summary>
    void Routine_Calib::SettleVacuum(bool gripped, std::stop_token ct)
    {
        int ms = m_sys()->VacuumSettleMs;
        if (gripped && ms > 0) base::Delay(ms, ct);
    }

    /// <summary>Chờ cảm biến chân không đạt trạng thái mong muốn — bản dùng cho bước hút của khu chạy kiểm.</summary>
    bool Routine_Calib::WaitVacuum(int nozzle, bool want, std::stop_token ct)
    {
        int limit = m_sys()->VacuumTimeoutMs, waited = 0;
        while (waited < limit)
        {
            if (m_func.HasProduct(nozzle) == want) return true;
            base::Delay(10, ct);
            waited += 10;
        }
        return m_func.HasProduct(nozzle) == want;
    }

    int Routine_Calib::CellIndexOf(const TrayState& tray, int row, int col)
    {
        for (int i = 0; i < tray.CellCount(); i++)
        {
            int r = 0, c = 0;
            tray.RowCol(i, r, c);
            if (r == row && c == col) return i;
        }
        return -1;
    }

    bool Routine_Calib::Move(const std::string& axisName, double pos, std::stop_token ct)
    {
        auto cfg = m_sys()->GetAxis(axisName);
        return m_func.Move(axisName, pos, cfg->MoveSpeed, ct);
    }

    /// <summary>
    /// Cao độ soi CAM bottom-up: Z của tay đang soi về đúng Z_SAFE — đúng chỗ chu trình Auto nâng Z lên sau khi hút rồi
    /// soi. Mọi ảnh bottom-up phải chụp ở cùng một cao độ: đổi khoảng cách tới camera là đổi thang mm/pixel và tâm xoay
    /// VisionMaster đã học (Tuấn chốt 2026-09-22 dùng Z_SAFE, không thêm ô cài riêng).
    /// </summary>
    bool Routine_Calib::MoveZToBottomCapture(int nozzle, std::stop_token ct)
    {
        return Move(nozzle == 1 ? AxisId::Z1 : AxisId::Z2, m_motion.ZSafe(), ct);
    }

    std::string Routine_Calib::ZCaptureFail(int nozzle)
    {
        return Loc::F("Calib.ZCaptureFail", ZName(nozzle));
    }

    std::string Routine_Calib::ZName(int nozzle) { return nozzle == 1 ? AxisId::Z1 : AxisId::Z2; }
    std::string Routine_Calib::AName(int nozzle) { return nozzle == 1 ? AxisId::A1 : AxisId::A2; }

    double Routine_Calib::Pos(const std::string& axis)
    {
        auto a = m_motion[axis];
        return a == nullptr ? DoubleNaN : a->CurrentPos();
    }

    std::string Routine_Calib::Join(const std::vector<double>& values)
    {
        if (values.empty()) return "—";
        std::string s;
        for (size_t i = 0; i < values.size(); i++)
        {
            if (i > 0) s += ", ";
            s += Loc::FormatValue(FmtArg(values[i]), "0.###");
        }
        return s;
    }

    /// <summary>Một dòng log cho mỗi lần kích Flow nạp điểm hiệu chuẩn: vị trí trục lúc chụp (đúng bộ số App ghi vào biến
    /// toàn cục) và Flow có thấy vật không. <paramref name="nozzle"/> = 0 là CAM top-down.</summary>
    void Routine_Calib::LogCalibPoint(const std::string& what, const AxisSnapshot& s, int nozzle, bool found)
    {
        std::string text = nozzle == 0
            ? Loc::Format("{0}: X {1:0.000}, Y2 {2:0.000}", { what, s.X, s.Y2 })
            : Loc::Format("{0}: X {1:0.000}, {2} {3:0.000}°, {4} {5:0.000}",
                { what, s.X, AName(nozzle), nozzle == 1 ? s.A1 : s.A2, ZName(nozzle), Pos(ZName(nozzle)) });
        Log(text + (found ? " — object found" : " — Flow reports NO object"), found ? LogLevel::Info : LogLevel::Warning);
    }
}
