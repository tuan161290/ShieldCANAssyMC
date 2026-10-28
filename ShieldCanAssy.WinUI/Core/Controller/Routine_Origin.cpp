#include "Controller/Routine_Origin.h"

#include <cmath>
#include <exception>

#include "Base/Async.h"
#include "Infor/AlarmManager.h"
#include "Infor/AxisId.h"
#include "Lang/Loc.h"
#include "Utility/Log/LogWorker.h"

namespace sca
{
    namespace
    {
        template <class Arr>
        std::vector<std::string> Names(const Arr& a)
        {
            std::vector<std::string> v;
            for (const auto& n : a) v.emplace_back(n);
            return v;
        }

        void Log(const std::string& msg, LogLevel level) { LogWorker::Instance().RecordLog(msg, level, "Origin"); }
    }

    Routine_Origin::Routine_Origin(MotionConnect& motion, McFunction& func, McStatus& status, std::function<std::shared_ptr<SystemData>()> sys)
        : m_motion(motion), m_func(func), m_status(status), m_sys(std::move(sys))
    {
    }

    bool Routine_Origin::Run(std::stop_token ct)
    {
        auto sys = m_sys();
        m_status.SetOrgOk(false);

        try
        {
            // KHÔNG gọi Abort() ở đây — Abort() tự đặt Message bằng một câu chung chung, đè
            // mất lý do cụ thể mà Fail() đã ghi bên trong PreCheck (ví dụ lý do phanh
            // Z1/Z2). Tự làm phần còn lại của Abort() mà không đụng vào Message.
            if (!PreCheck())
            {
                m_motion.StopAll();
                m_status.SetOrgOk(false);
                m_status.SetOriginStep(EOriginState::Aborted);
                return false;
            }

            Step(EOriginState::O1_ServoOn);
            if (!ServoOnAll(ct)) return Abort(Loc::T("Origin.ServoOnFailed"));

            Step(EOriginState::O2_HomeZ);
            if (!HomeParallel(Names(AxisId::ZAxes), ct)) return Abort(Loc::T("Origin.HomeZFailed"));

            Step(EOriginState::O3_GateZSafe);
            if (!GateZSafe()) return Abort(Loc::T("Origin.ZNotSafe"));

            // A1/A2 đã xác nhận có cảm biến gốc và xoay tự do (§11 #10) nên mặc định về gốc
            // song song — và CÙNG LÚC với X/Y1/Y2 (Tuấn 2026-09-30, không đợi A xong mới chạy X/Y): Z đã qua
            // chốt O3 nên đầu hút ở cao độ an toàn, xoay trong lúc gantry chạy ngang không va gì — Auto cũng
            // xoay A song song với X/Y. Cờ HomeRotateSequential vẫn còn để đổi được nếu sau này gá đầu hút
            // to ra tới mức xoay đồng thời là va nhau — bật cờ thì quay về A tuần tự rồi mới X/Y như cũ.
            if (sys->HomeRotateSequential)
            {
                Step(EOriginState::O4_HomeA);
                if (!HomeSequential(Names(AxisId::RotateAxes), ct)) return Abort(Loc::T("Origin.HomeAFailed"));
                Step(EOriginState::O5_HomeXY);
                if (!HomeParallel(Names(AxisId::PlanarAxes), ct)) return Abort(Loc::T("Origin.HomeXYFailed"));
            }
            else
            {
                Step(EOriginState::O5_HomeXY);
                auto names = Names(AxisId::RotateAxes);
                for (const auto& n : AxisId::PlanarAxes) names.emplace_back(n);
                if (!HomeParallel(names, ct)) return Abort(Loc::T("Origin.HomeAXYFailed"));
            }

            Step(EOriginState::O6_Standby);
            if (!MoveStandby(ct)) return Abort(Loc::T("Origin.StandbyFailed"));

            Step(EOriginState::O7_Finish);
            bool homed = m_motion.AllHomed();
            m_status.SetOrgOk(homed);
            Log(homed ? "Homing complete" : "Homing ended but some axes are not homed", homed ? LogLevel::Info : LogLevel::Warning);
            return homed;
        }
        catch (const base::OperationCanceled&)
        {
            return Abort(Loc::T("Mc.Cancelled"));
        }
        catch (const std::exception& ex)
        {
            AlarmManager::RaiseEvent(AlarmCode::RoutineCrashed, "Origin", ex.what());   // sự kiện: RESET gỡ được (30/09)
            return Abort(Loc::F("Mc.Unexpected", ex.what()));
        }
        catch (...)
        {
            AlarmManager::RaiseEvent(AlarmCode::RoutineCrashed, "Origin", "unknown exception");
            return Abort(Loc::F("Mc.Unexpected", "unknown exception"));
        }
    }

    bool Routine_Origin::Init(std::stop_token ct)
    {
        try
        {
            if (!m_motion.AllHomed()) { m_status.SetMessage(Loc::T("Mc.InitNotHomed")); return false; }
            if (!m_func.EnsureZSafe(ct))
            {
                m_status.SetMessage(Loc::T("Origin.InitZFailed"));
                return false;
            }
            if (!MoveStandby(ct))
            {
                m_status.SetMessage(Loc::T("Origin.InitStandbyFailed"));
                return false;
            }
            Log("Init done — all axes at standby", LogLevel::Info);
            return true;
        }
        catch (const base::OperationCanceled&)
        {
            m_status.SetMessage(Loc::T("Origin.InitCancelled"));
            return false;
        }
        catch (const std::exception& ex)
        {
            // Không bắt thì luồng lỗi, đoạn chạy sau Init (McControl.DoInit) không chạy và máy kẹt ở trạng thái về gốc.
            AlarmManager::RaiseEvent(AlarmCode::RoutineCrashed, "Origin", ex.what());
            m_status.SetMessage(Loc::F("Origin.InitUnexpected", ex.what()));
            return false;
        }
        catch (...)
        {
            AlarmManager::RaiseEvent(AlarmCode::RoutineCrashed, "Origin", "unknown exception");
            m_status.SetMessage(Loc::F("Origin.InitUnexpected", "unknown exception"));
            return false;
        }
    }

    // ─────────────── O0 ───────────────

    /// Toàn bộ danh sách phải PASS mới đi tiếp — PLAN.md §4.2.
    bool Routine_Origin::PreCheck()
    {
        Step(EOriginState::O0_PreCheck);
        std::string reason = BlockReason(true);
        return reason.empty() ? true : Fail(reason);
    }

    std::string Routine_Origin::BlockReason(bool checkBrakes) const
    {
        if (!m_motion.AllConnected()) return Loc::T("Origin.AxisNotConnected");
        if (m_motion.AnyAlarm()) return Loc::T("Origin.AxisAlarm");
        if (!m_status.EmgOk()) return Loc::T("Mc.BlockEmg");
        if (!m_status.LightCurtainOk()) return Loc::T("Mc.BlockCurtain");
        if (!m_status.AirOk()) return Loc::T("Mc.BlockAir");

        // Z1/Z2 phải đã qua nghi thức thao tác tay TRƯỚC khi được phép về gốc: bật servo rồi
        // bấm "Mở phanh" (trang Manual, hoặc xác nhận trên popup Origin của trang Main — Tuấn
        // 2026-10-21) — chuỗi Về gốc KHÔNG được tự bật servo/nhả phanh hộ operator (Tuấn yêu
        // cầu, 2026-09-04). O1 (ServoOnAll) vẫn bật servo cho các trục còn lại như cũ; với
        // Z1/Z2 thì tới đây phải đã bật sẵn rồi.
        if (checkBrakes && (!m_func.BrakeReady(AxisId::Z1) || !m_func.BrakeReady(AxisId::Z2)))
            return Loc::T("Origin.BrakeNotReleased");

        // KHÔNG chặn khi tay hút còn sản phẩm (Tuấn bỏ 2026-09-26): về gốc nâng Z lên trước rồi mới chạy
        // ngang, sản phẩm đi theo tay hút như lúc chạy Auto. Hàng còn trên tay vẫn bị chặn ở START/Arm
        // (ResumeIntegrityOk, NozzleClearPending) cho tới khi xử lý xong.

        if (m_status.State() == EMcState::Running) return Loc::T("Origin.AutoRunning");

        return {};
    }

    bool Routine_Origin::Fail(const std::string& reason)
    {
        std::string msg = Loc::F("Mc.CannotHome", reason);
        m_status.SetMessage(msg);
        Log(msg, LogLevel::Warning);
        return false;
    }

    // ─────────────── O1 ───────────────

    bool Routine_Origin::ServoOnAll(std::stop_token ct)
    {
        for (const auto& axis : m_motion.AxisList())
        {
            if (ct.stop_requested()) return false;
            if (!axis->SetServoOn(true))
            {
                AlarmManager::RaiseAxisEvent(axis->Name(), AxisFault::ServoOffUnexpected, Loc::T("Origin.ServoOnDetail"));
                return false;
            }
        }

        // Chờ driver báo sẵn sàng thay vì giả định servo on là dùng được ngay.
        base::Stopwatch sw;
        while (!m_motion.AllServoOn())
        {
            if (ct.stop_requested()) return false;
            if (sw.ElapsedMilliseconds() > 5000) return false;
            base::Delay(20);
        }
        return true;
    }

    // ─────────────── O2 / O4 / O5 ───────────────

    bool Routine_Origin::HomeParallel(const std::vector<std::string>& names, std::stop_token ct)
    {
        std::vector<std::shared_ptr<IAxis>> axes;
        std::string joined;
        for (const auto& n : names)
        {
            if (!joined.empty()) joined += ", ";
            joined += n;
            if (auto a = m_motion.Axis(n)) axes.push_back(a);
        }
        if (axes.empty()) return true;

        Log("Homing in parallel: " + joined, LogLevel::Info);

        // Một nguồn huỷ liên kết: trục nào hỏng thì huỷ luôn các trục còn lại,
        // thay vì để chúng chạy tiếp trong khi máy đã ở trạng thái không xác định.
        base::LinkedStopSource linked(ct);
        std::vector<std::function<bool()>> jobs;
        for (const auto& a : axes)
            jobs.push_back([this, a, &linked]
            {
                bool ok = m_motion.Home(a, linked.Token());
                if (!ok) linked.Cancel();
                return ok;
            });
        return base::WhenAll(std::move(jobs));
    }

    bool Routine_Origin::HomeSequential(const std::vector<std::string>& names, std::stop_token ct)
    {
        for (const auto& name : names)
        {
            auto axis = m_motion.Axis(name);
            if (!axis) continue;
            if (!m_motion.Home(axis, ct)) return false;
        }
        return true;
    }

    // ─────────────── O3 ───────────────

    /// Chốt chặn: đọc LẠI vị trí thực của cả hai trục Z và cờ về gốc. Không đạt thì huỷ toàn
    /// bộ, không đi tiếp. Đây là chỗ duy nhất ngăn X/Y chạy ngang khi đầu hút còn thấp.
    bool Routine_Origin::GateZSafe()
    {
        auto z1 = m_motion.Axis(AxisId::Z1);
        auto z2 = m_motion.Axis(AxisId::Z2);

        if (!z1 || !z2) return false;
        if (!z1->OrgOk() || !z2->OrgOk())
        {
            m_status.SetMessage(Loc::T("Origin.GateNotHomed"));
            return false;
        }
        if (!m_motion.ZSafeOk())
        {
            m_status.SetMessage(Loc::F("Origin.GateZPos", z1->CurrentPos(), z2->CurrentPos(), m_motion.ZSafeLimit()));
            return false;
        }

        Log(Loc::Format("Z interlock OK: Z1={0:0.00}, Z2={1:0.00} ≤ {2:0.00}", { z1->CurrentPos(), z2->CurrentPos(), m_motion.ZSafeLimit() }),
            LogLevel::Info);
        return true;
    }

    // ─────────────── O6 ───────────────

    bool Routine_Origin::MoveStandby(std::stop_token ct)
    {
        for (const auto& axis : m_motion.AxisList())
        {
            if (ct.stop_requested()) return false;
            auto cfg = axis->Config();
            if (std::fabs(axis->CurrentPos() - cfg->StandbyPos) < 0.01) continue;
            if (!m_func.Move(axis->Name(), cfg->StandbyPos, cfg->MoveSpeed, ct))
                return false;
        }
        return true;
    }

    // ─────────────── tiện ích ───────────────

    void Routine_Origin::Step(EOriginState step)
    {
        m_status.SetOriginStep(step);
        Log(std::string("Origin ") + ToString(step), LogLevel::Info);
    }

    /// Huỷ giữa chừng — PLAN.md §4.4. Không bao giờ để trạng thái "về gốc dở" được đánh dấu
    /// là đã về gốc: OrgOk phải về false và ghi rõ trục nào ở bước nào gây huỷ.
    bool Routine_Origin::Abort(const std::string& reason)
    {
        m_motion.StopAll();
        m_status.SetOrgOk(false);
        std::string msg = Loc::F("Origin.AbortedAtStep", ToString(m_status.OriginStep()), reason);
        m_status.SetMessage(msg);
        m_status.SetOriginStep(EOriginState::Aborted);
        Log(msg, LogLevel::Error);
        return false;
    }
}
