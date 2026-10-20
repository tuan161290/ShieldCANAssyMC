#include "Controller/McFunction.h"

#include <cmath>
#include <limits>
#include <vector>

#include "Communication/AxisAngle.h"
#include "Infor/AxisId.h"
#include "Infor/IoMap.h"
#include "Lang/Loc.h"
#include "Utility/Log/LogWorker.h"

namespace sca
{
    namespace
    {
        /// Thay AsyncLocal&lt;bool&gt; _inCycle của bản C# — xem McFunction::MarkCycleFlow.
        thread_local bool t_inCycle = false;

        /// AxisId::PlanarAxes / ZAxes có thể là mảng const char* hoặc std::string — so bằng string_view.
        template <class Arr>
        bool Contains(const Arr& names, std::string_view name)
        {
            for (const auto& n : names)
                if (name == std::string_view(n)) return true;
            return false;
        }

        bool Is(std::string_view a, std::string_view b) { return a == b; }

        void Log(const std::string& msg, LogLevel level, std::string_view source) { LogWorker::Instance().RecordLog(msg, level, source); }
    }

    McFunction::McFunction(MotionConnect& motion, SafetyMonitor& safety, McStatus& status, std::function<std::shared_ptr<SystemData>()> sys)
        : m_motion(motion), m_safety(safety), m_status(status), m_sys(std::move(sys))
    {
    }

    std::string McFunction::HeldReason() { return Loc::T("Mc.HeldReason"); }

    // ───────────────────────── phanh Z1/Z2 ─────────────────────────

    const McFunction::BrakeState* McFunction::BrakeOf(std::string_view axisName) const
    {
        if (Is(axisName, AxisId::Z1)) return &m_brakeZ1;
        if (Is(axisName, AxisId::Z2)) return &m_brakeZ2;
        return nullptr;
    }

    McFunction::BrakeState* McFunction::BrakeOf(std::string_view axisName)
    {
        if (Is(axisName, AxisId::Z1)) return &m_brakeZ1;
        if (Is(axisName, AxisId::Z2)) return &m_brakeZ2;
        return nullptr;
    }

    void McFunction::RequestBrakeRelease(std::string_view axisName)
    {
        if (auto* s = BrakeOf(axisName)) s->Requested = true;
    }

    void McFunction::SetEmergencyBrakeRelease(std::string_view axisName, bool armed)
    {
        if (auto* s = BrakeOf(axisName)) s->EmergencyReleaseArmed = armed;
    }

    bool McFunction::IsEmergencyBrakeReleaseArmed(std::string_view axisName) const
    {
        const auto* s = BrakeOf(axisName);
        return s != nullptr && s->EmergencyReleaseArmed.load();
    }

    void McFunction::UpdateBrakeOutputs()
    {
        UpdateBrakeOutput(AxisId::Z1, IoMap::DoBrakeReleaseZ1, m_brakeZ1);
        UpdateBrakeOutput(AxisId::Z2, IoMap::DoBrakeReleaseZ2, m_brakeZ2);
    }

    void McFunction::UpdateBrakeOutput(const std::string& axisName, int doBit, BrakeState& state)
    {
        auto io = m_io;
        if (!io) return;

        auto axis = m_motion.Axis(axisName);
        bool ready = axis && axis->IsConnected() && axis->ServoOn() && !axis->Alarm();

        // Mất điều kiện servo là xoá luôn yêu cầu — không được để yêu cầu cũ "sống sót" qua
        // một lần Alarm/ServoOff rồi tự nhả lại khi ServoOn quay về, phải bấm nút lại từ đầu.
        // Đây cũng chính là "đảm bảo xử lý xong lỗi mới cho phép nhả phanh" — Alarm xoá cờ
        // ngay, và cờ chỉ đặt lại được bằng nút bấm tay sau khi operator đã RST xong.
        if (!ready) state.Requested = false;

        bool normalRelease = ready && state.Requested;
        bool emergencyRelease = state.EmergencyReleaseArmed;

        bool release = normalRelease || emergencyRelease;

        if (release) { if (state.ReleaseStartMs.load() < 0) state.ReleaseStartMs = m_clock.ElapsedMilliseconds(); }
        else state.ReleaseStartMs = -1;

        io->SetDo(doBit, release);
    }

    bool McFunction::BrakeReady(std::string_view axisName) const
    {
        const auto* state = BrakeOf(axisName);
        if (state == nullptr) return true;
        long long start = state->ReleaseStartMs.load();
        return start >= 0 && m_clock.ElapsedMilliseconds() - start >= BrakeReleaseDelayMs;
    }

    // ───────────────────────── chốt chặn ─────────────────────────

    bool McFunction::CanMove(std::string_view axisName, std::string& reason) const
    {
        reason.clear();
        std::string name(axisName);

        auto axis = m_motion.Axis(name);
        if (!axis) { reason = Loc::F("Mc.NoAxis", name); return false; }

        if (!m_safety.SafeToMove())
        {
            reason = m_safety.BlockReason();
            return false;
        }
        if (!axis->IsConnected()) { reason = Loc::F("Mc.AxisDisconnected", name); return false; }
        if (axis->Alarm())        { reason = Loc::F("Mc.AxisAlarm", name); return false; }
        if (!axis->ServoOn())     { reason = Loc::F("Mc.AxisServoOff", name); return false; }

        // Cảnh báo mở phanh trước khi thao tác (Tuấn yêu cầu, 2026-09-04) — chỉ Z1/Z2 có
        // phanh; DO nhả phanh do UpdateBrakeOutputs tự bật theo ServoOn/Alarm, ở đây chỉ chờ
        // đủ BrakeReleaseDelayMs rồi mới cho di chuyển thật, không suy diễn phanh đã nhả
        // ngay khi ServoOn vừa lên.
        if (!BrakeReady(name)) { reason = Loc::F("Mc.AxisBrakeLocked", name); return false; }

        // ── Chốt chặn chống va (PLAN.md §5.1) ──
        // X, Y1, Y2 chỉ được chạy khi CẢ HAI trục Z ≤ Z_SAFE + 1 mm. Kiểm tra bằng vị trí
        // thực đọc từ encoder, không phải bằng việc lệnh nâng Z trước đó đã trả về thành công.
        if (Contains(AxisId::PlanarAxes, name) && !m_motion.ZSafeOk())
        {
            auto z1 = m_motion.Axis(AxisId::Z1);
            auto z2 = m_motion.Axis(AxisId::Z2);
            // C# truyền z?.CurrentPos (nullable) — trục không có thì in rỗng.
            FmtArg p1 = z1 ? FmtArg(z1->CurrentPos()) : FmtArg(std::string());
            FmtArg p2 = z2 ? FmtArg(z2->CurrentPos()) : FmtArg(std::string());
            reason = Loc::F("Mc.ZNotSafe", p1, p2, m_motion.ZSafeLimit());
            return false;
        }

        return true;
    }

    bool McFunction::CanMoveAutonomous(std::string_view axisName, std::string& reason) const
    {
        if (!CanMove(axisName, reason)) return false;
        if (!m_runBit) { reason = HeldReason(); return false; }

        if (Contains(AxisId::PlanarAxes, axisName))
        {
            auto z1 = m_motion.Axis(AxisId::Z1);
            auto z2 = m_motion.Axis(AxisId::Z2);
            if (!z1 || !z1->OrgOk() || !z2 || !z2->OrgOk())
            {
                reason = Loc::F("Mc.ZMustHomeFirst", std::string(axisName));
                return false;
            }
        }

        return true;
    }

    // ───────────────────────── lệnh chạy ─────────────────────────

    bool McFunction::Move(const std::string& axisName, double pos, double speed, std::stop_token ct)
    {
        return MoveHoldable(axisName, pos, speed, ct, true);
    }

    bool McFunction::MoveGentle(const std::string& axisName, double pos, double speed, std::stop_token ct)
    {
        return MoveHoldable(axisName, pos, speed, ct, true, true);
    }

    void McFunction::MarkCycleFlow() { t_inCycle = true; }
    bool McFunction::InCycleFlow() { return t_inCycle; }
    McFunction::CycleFlowScope::CycleFlowScope(bool inCycle) : m_before(t_inCycle) { t_inCycle = inCycle; }
    McFunction::CycleFlowScope::~CycleFlowScope() { t_inCycle = m_before; }

    void McFunction::SetRunBit(bool on)
    {
        std::stop_source cancel;
        bool doCancel = false;
        {
            std::lock_guard<std::mutex> lock(m_runLock);
            if (m_runBit == on) return;
            m_runBit = on;
            if (on) m_runCts = std::stop_source();
            else { cancel = m_runCts; doCancel = true; }
        }
        // Huỷ ngoài khoá — stop_callback của lệnh trục đang chờ chạy ngay trên luồng này.
        if (doCancel) cancel.request_stop();
        if (!on) m_motion.StopAll();
    }

    void McFunction::WaitRunBit(std::stop_token ct)
    {
        while (!m_runBit) base::Delay(20, ct);
    }

    bool McFunction::MoveHoldable(const std::string& axisName, double pos, double speed, std::stop_token ct, bool interlock, bool gentle)
    {
        if (!m_runBit && !t_inCycle)
        {
            m_status.SetMessage(Loc::F("Mc.CannotMoveAxis", axisName, HeldReason()));
            return false;
        }

        auto axis = m_motion.Axis(axisName);
        while (true)
        {
            WaitRunBit(ct);

            std::string reason;
            if (interlock && !CanMove(axisName, reason))
            {
                // Rèm sáng vừa bị cắt giữa lúc qua chờ bit và lúc xét chốt: bit RUN đã tắt trước khi SafeToMove tắt
                // (SafetyMonitor.CurtainBroken), nên quay lại chờ Start như STOP thay vì báo lỗi dừng chu trình.
                if (!m_runBit && t_inCycle) continue;
                std::string msg = Loc::F("Mc.CannotMoveAxis", axisName, reason);
                m_status.SetMessage(msg);
                Log(msg, LogLevel::Warning, axisName);
                return false;
            }
            if (!axis)
            {
                // Chỉ tới được đây khi interlock = false (CanMove đã chặn trục không tồn tại). C# sẽ NullReference —
                // ở đây báo như CanMove thay vì đổ.
                m_status.SetMessage(Loc::F("Mc.CannotMoveAxis", axisName, Loc::F("Mc.NoAxis", axisName)));
                return false;
            }

            std::stop_source run;
            {
                std::lock_guard<std::mutex> lock(m_runLock);
                run = m_runCts;
            }
            bool ok;
            try
            {
                base::LinkedStopSource linked(ct, run.get_token());
                ok = axis->MoveAbs(pos, speed, linked.Token(), gentle);
            }
            catch (const base::OperationCanceled&)
            {
                // C#: catch (OperationCanceledException) when (!ct.IsCancellationRequested && run.IsCancellationRequested)
                // — huỷ do ct thì exception đi tiếp ra routine (không qua MarkMoveEnded, như bản C#).
                if (!(!ct.stop_requested() && run.stop_requested())) throw;
                ok = false;
            }
            MarkMoveEnded();

            if (ok) return true;
            if (ct.stop_requested() || !run.stop_requested()) return false;
            // Bit RUN tắt giữa hành trình: quay lại chờ bật rồi phát lại lệnh.
        }
    }

    int McFunction::SettleRemainingMs(long long lastMoveEndedMs, long long nowMs, int settleMs)
    {
        if (settleMs <= 0 || lastMoveEndedMs == NeverMoved) return 0;
        long long left = settleMs - (nowMs - lastMoveEndedMs);
        if (left <= 0) return 0;
        return left > settleMs ? settleMs : (int)left;
    }

    void McFunction::WaitSettle(EVmCamera cam, std::stop_token ct)
    {
        int left = SettleRemainingMs(m_lastMoveEndedMs.load(), m_clock.ElapsedMilliseconds(), m_sys()->SettleMsFor(cam));
        if (left > 0) base::Delay(left, ct);
    }

    bool McFunction::UseTwoStageZ(double now, double slowAt, double target)
    {
        return slowAt > 0 && now < slowAt && slowAt < target;
    }

    bool McFunction::UseTwoStageZUp(double now, double slowAt, double target)
    {
        return UseTwoStageZ(target, slowAt, now);
    }

    bool McFunction::MoveZDown(const std::string& zAxis, double target, double slowAt, std::stop_token ct)
    {
        auto cfg = m_sys()->GetAxis(zAxis);
        auto axis = m_motion.Axis(zAxis);
        double now = axis ? axis->CurrentPos() : std::numeric_limits<double>::quiet_NaN();

        if (!UseTwoStageZ(now, slowAt, target))
            return Move(zAxis, target, cfg->MoveSpeed, ct);

        if (!Move(zAxis, slowAt, cfg->MoveSpeed, ct)) return false;
        return Move(zAxis, target, cfg->MoveSpeedLow(), ct);
    }

    bool McFunction::MoveZUp(const std::string& zAxis, double target, double slowAt, std::stop_token ct)
    {
        auto cfg = m_sys()->GetAxis(zAxis);
        auto axis = m_motion.Axis(zAxis);
        double now = axis ? axis->CurrentPos() : std::numeric_limits<double>::quiet_NaN();

        if (!UseTwoStageZUp(now, slowAt, target))
            return Move(zAxis, target, cfg->MoveSpeed, ct);

        if (!Move(zAxis, slowAt, cfg->MoveSpeedLow(), ct)) return false;
        return Move(zAxis, target, cfg->MoveSpeed, ct);
    }

    bool McFunction::MoveInc(const std::string& axisName, double delta, double speed, std::stop_token ct)
    {
        std::string reason = m_runBit ? std::string() : HeldReason();
        if (!reason.empty() || !CanMove(axisName, reason))
        {
            std::string msg = Loc::F("Mc.CannotMoveAxis", axisName, reason);
            m_status.SetMessage(msg);
            Log(msg, LogLevel::Warning, axisName);
            return false;
        }

        auto axis = m_motion.Axis(axisName);
        bool ok = axis->MoveInc(delta, speed, ct);
        MarkMoveEnded();
        return ok;
    }

    void McFunction::Jog(const std::string& axisName, int dir, double speed)
    {
        std::string reason;
        if (dir != 0 && !m_runBit)
        {
            m_status.SetMessage(Loc::F("Mc.CannotJogAxis", axisName, HeldReason()));
            return;
        }
        if (dir != 0 && !CanMove(axisName, reason))
        {
            m_status.SetMessage(Loc::F("Mc.CannotJogAxis", axisName, reason));
            return;
        }

        auto axis = m_motion.Axis(axisName);
        if (!axis) return;

        auto cfg = axis->Config();
        double v = speed > 0 ? speed : (m_jogFast ? cfg->JogSpeedFast : cfg->JogSpeedSlow);
        axis->Jog(dir, v);
    }

    bool McFunction::EnsureZSafe(std::stop_token ct)
    {
        if (m_motion.ZSafeOk()) return true;

        // "An toàn" là vị trí NHỎ (đã rút lên) — xem MotionConnect.ZSafeOk. Dừng đúng Z_SAFE: chốt chặn
        // đã nới MotionConnect.ZSafeMargin nên không đứng ngay biên.
        double target = m_motion.ZSafe();
        bool inCycle = t_inCycle;
        std::vector<std::function<bool()>> jobs;
        for (const auto& n : AxisId::ZAxes)
        {
            auto a = m_motion.Axis(std::string(n));
            if (!a || !(a->CurrentPos() > target)) continue;
            jobs.push_back([this, a, target, ct, inCycle]
            {
                CycleFlowScope scope(inCycle);
                return MoveHoldable(a->Name(), target, a->Config()->MoveSpeed, ct, false);
            });
        }

        if (jobs.empty()) return m_motion.ZSafeOk();

        bool all = base::WhenAll(std::move(jobs));
        return all && m_motion.ZSafeOk();
    }

    bool McFunction::MoveRow(const std::string& rowAxis, double pos, bool slow, std::stop_token ct)
    {
        auto cfg = m_sys()->GetAxis(rowAxis);
        return slow ? MoveGentle(rowAxis, pos, cfg->MoveSpeedLow(), ct) : Move(rowAxis, pos, cfg->MoveSpeed, ct);
    }

    bool McFunction::MoveToCell(const std::string& rowAxis, double rowPos, double colPos, std::stop_token ct, bool slowRow)
    {
        if (!EnsureZSafe(ct)) return false;

        bool inCycle = t_inCycle;
        double xSpeed = m_sys()->GetAxis(AxisId::X)->MoveSpeed;
        std::vector<std::function<bool()>> jobs;
        jobs.push_back([this, rowAxis, rowPos, slowRow, ct, inCycle]
        {
            CycleFlowScope scope(inCycle);
            return MoveRow(rowAxis, rowPos, slowRow, ct);
        });
        jobs.push_back([this, colPos, xSpeed, ct, inCycle]
        {
            CycleFlowScope scope(inCycle);
            return Move(AxisId::X, colPos, xSpeed, ct);
        });
        return base::WhenAll(std::move(jobs));
    }

    bool McFunction::MoveToCellForPick(int pickNozzle, double pickAngleDeg, const std::string& rowAxis, double rowPos, double colPos,
                                       std::stop_token ct, bool slowRow)
    {
        if (!EnsureZSafe(ct)) return false;

        std::string aName = pickNozzle == 1 ? std::string(AxisId::A1) : std::string(AxisId::A2);
        auto sys = m_sys();
        double xSpeed = sys->GetAxis(AxisId::X)->MoveSpeed;
        double aSpeed = sys->GetAxis(aName)->MoveSpeed;
        double aTarget = AxisAngle::Wrap(pickAngleDeg);
        bool inCycle = t_inCycle;
        std::vector<std::function<bool()>> jobs;
        jobs.push_back([this, rowAxis, rowPos, slowRow, ct, inCycle]
        {
            CycleFlowScope scope(inCycle);
            return MoveRow(rowAxis, rowPos, slowRow, ct);
        });
        jobs.push_back([this, colPos, xSpeed, ct, inCycle]
        {
            CycleFlowScope scope(inCycle);
            return Move(AxisId::X, colPos, xSpeed, ct);
        });
        jobs.push_back([this, aName, aTarget, aSpeed, ct, inCycle]
        {
            CycleFlowScope scope(inCycle);
            return Move(aName, aTarget, aSpeed, ct);
        });
        return base::WhenAll(std::move(jobs));
    }

    // ───────────────────────── I/O nghiệp vụ ─────────────────────────

    void McFunction::SetVacuum(int nozzle, bool on)
    {
        auto io = m_io;
        if (!io) return;
        io->SetDo(nozzle == 1 ? IoMap::DoVacuum1 : IoMap::DoVacuum2, on);
        if (on) io->SetDo(nozzle == 1 ? IoMap::DoBlow1 : IoMap::DoBlow2, false);
    }

    void McFunction::SetBlow(int nozzle, bool on)
    {
        auto io = m_io;
        if (!io) return;
        if (on) io->SetDo(nozzle == 1 ? IoMap::DoVacuum1 : IoMap::DoVacuum2, false);
        io->SetDo(nozzle == 1 ? IoMap::DoBlow1 : IoMap::DoBlow2, on);
    }

    bool McFunction::HasProduct(int nozzle) const
    {
        auto io = m_io;
        return io && io->GetDi(nozzle == 1 ? IoMap::Vacuum1 : IoMap::Vacuum2);
    }

    bool McFunction::HasTrayIn(int tray) const
    {
        auto io = m_io;
        return io && io->GetDi(tray == 1 ? IoMap::TrayIn2Present : IoMap::TrayIn1Present);
    }

    bool McFunction::HasTrayAssy() const
    {
        auto io = m_io;
        return io && io->GetDi(IoMap::TrayAssyPresent);
    }
}
