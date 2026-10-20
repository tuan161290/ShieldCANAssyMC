#include "Communication/FastechAxis.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>

#include "Communication/AxisAngle.h"
#include "Fastech/EziAxisStatus.h"
#include "Fastech/EziMOTIONPlusELib.h"
#include "Infor/AlarmManager.h"
#include "Lang/Loc.h"
#include "Utility/Log/LogWorker.h"

namespace sca
{
    namespace
    {
        using CmdLock = std::lock_guard<std::mutex>;
    }

    FastechAxis::FastechAxis(std::shared_ptr<AxisConfig> cfg)
        : m_name(cfg->Name), m_cfg(cfg), m_scale(cfg), m_bdId(cfg->BoardId)
    {
    }

    std::string FastechAxis::RawStatusText() const
    {
        return EziAxisStatus::Describe(m_rawStatus.load());
    }

    std::shared_ptr<FastechAxis> FastechAxis::KeepAlive()
    {
        return weak_from_this().lock();
    }

    // ───────────────────────── kết nối ─────────────────────────

    bool FastechAxis::Connect()
    {
        m_opened = false;
        m_failStreak = 0;
        try
        {
            const std::string ipText = m_cfg->IpAddress;
            IPAddress ip;
            if (!IPAddress::TryParse(ipText, ip))
            {
                LogWorker::Instance().RecordLog(std::format("{}: invalid IP — {}", m_name, ipText),
                    LogLevel::Error, m_name);
                m_connected = false;
                return false;
            }

            // Chạy trên máy chưa có EziMOTIONPlusE.dll: báo rõ chứ không để lỗi trần làm sập vòng khởi tạo
            // (C# catch (DllNotFoundException) — P/Invoke nạp DLL ở lời gọi FAS_* đầu tiên; ở đây nạp tường minh).
            std::string dllError;
            if (!EziMOTIONPlusELib::IsLoaded() && !EziMOTIONPlusELib::Load(dllError))
            {
                LogWorker::Instance().RecordLog(
                    std::format("{}: EziMOTIONPlusE.dll not found next to exe{}", m_name,
                                dllError.empty() ? std::string() : " (" + dllError + ")"),
                    LogLevel::Error, m_name);
                m_connected = false;
                return false;
            }

            // FAS_Connect lần đầu trong phiên hay hỏng (~230 ms rồi trả false) dù mở lại app vài
            // giây sau là được — log 23–26/09 chỉ thấy ở A1, thiết bị Fastech nối đầu tiên. Thử
            // lại vài lần thay vì bắt mở lại app; Refresh không tự nối lại khi Connect hỏng.
            constexpr int ConnectRetries = 5;
            constexpr int ConnectRetryDelayMs = 200;
            bool connected = false;
            int connectAttempts = 0;
            for (int attempt = 0; attempt < ConnectRetries; attempt++)
            {
                connectAttempts++;
                EziMOTIONPlusELib::FAS_Close(m_bdId);
                if (EziMOTIONPlusELib::FAS_Connect(ip, m_bdId)) { connected = true; break; }
                if (attempt < ConnectRetries - 1) base::Delay(ConnectRetryDelayMs);
            }

            if (!connected)
            {
                LogWorker::Instance().RecordLog(
                    std::format("{}: cannot connect {} (bdID {}, tried {} times)", m_name, ipText, m_bdId, ConnectRetries),
                    LogLevel::Error, m_name);
                m_connected = false;
                return false;
            }

            // FAS_Connect chỉ mở socket UDP; nó thành công cả khi đầu kia không có thiết bị
            // nào. FAS_IsSlaveExist mới là câu hỏi "có ai trả lời không" — nhưng hỏi ngay lúc
            // app vừa khởi động thì driver Ezi thường CHƯA kịp sẵn sàng trả lời (đang tự khởi
            // động Ethernet stack của chính nó), nên hay báo "mất kết nối" oan dù cáp/nguồn
            // đều ổn (Tuấn báo lỗi, 2026-09-08). Thử lại vài lần thay vì hỏi đúng một lần rồi
            // kết luận ngay — driver trả lời được là dừng thử ngay, không phải lúc nào cũng
            // chờ đủ 1.2s này.
            constexpr int SlaveExistRetries = 6;
            constexpr int SlaveExistRetryDelayMs = 200;
            bool slaveExists = false;
            for (int attempt = 0; attempt < SlaveExistRetries; attempt++)
            {
                if (EziMOTIONPlusELib::FAS_IsSlaveExist(m_bdId) != 0) { slaveExists = true; break; }
                if (attempt < SlaveExistRetries - 1) base::Delay(SlaveExistRetryDelayMs);
            }

            if (!slaveExists)
            {
                LogWorker::Instance().RecordLog(
                    std::format("{}: socket open but no device answers at {} (tried {} times)", m_name, ipText, SlaveExistRetries),
                    LogLevel::Error, m_name);
                EziMOTIONPlusELib::FAS_Close(m_bdId);
                m_connected = false;
                return false;
            }

            m_opened = true;
            {
                std::lock_guard<std::mutex> lock(m_sinceReadMutex);
                m_sinceRead.Restart();
                m_sinceReadRunning = true;
            }
            m_connected = true;
            LogWorker::Instance().RecordLog(
                std::format("{}: connected {} (bdID {}){}", m_name, ipText, m_bdId,
                            connectAttempts > 1 ? std::format(" after {} attempts", connectAttempts) : std::string()),
                LogLevel::Info, m_name);
            return true;
        }
        catch (const std::exception& ex)
        {
            LogWorker::Instance().RecordLog(std::format("{}: connect error — {}", m_name, ex.what()),
                LogLevel::Error, m_name);
            m_connected = false;
            return false;
        }
    }

    void FastechAxis::Disconnect()
    {
        // Qua chung m_cmdLock với mọi lệnh khác — đóng board giữa lúc một lệnh khác đang gửi
        // tới CÙNG bdId dễ để lại trạng thái lỡ dở (Tuấn lo ngại lúc đóng app, 2026-09-10).
        // Khác PanasonicAxis: khoá đồng bộ, không cần timeout vì mọi lệnh ở đây đều là gọi thư viện
        // nhanh, không có bước chờ Modbus nối tiếp kiểu servo Panasonic.
        {
            CmdLock lock(m_cmdLock);
            try { EziMOTIONPlusELib::FAS_Close(m_bdId); } catch (...) {}
        }
        m_opened = false;
        m_failStreak = 0;
        m_connected = false;
        m_softHomed = false;
        m_orgOk = false;
    }

    // ───────────────────────── đọc trạng thái ─────────────────────────

    void FastechAxis::Refresh()
    {
        if (!m_opened) return;
        {
            std::lock_guard<std::mutex> lock(m_sinceReadMutex);
            if (!m_connected && m_sinceReadRunning && m_sinceRead.ElapsedMilliseconds() < RetryMs) return;
            m_sinceRead.Restart();
            m_sinceReadRunning = true;
        }

        uint32_t inStatus = 0, outStatus = 0, axisStatus = 0;
        int32_t cmdPos = 0, actPos = 0, posErr = 0, actVel = 0;
        uint16_t itemNo = 0;

        int rc;
        try
        {
            rc = EziMOTIONPlusELib::FAS_GetAllStatus(m_bdId, inStatus, outStatus,
                axisStatus, cmdPos, actPos, posErr, actVel, itemNo);
        }
        catch (const std::exception& ex) { ReadFailed(ex.what()); return; }

        if (rc != 0) { ReadFailed(std::format("FAS_GetAllStatus returned {}", rc)); return; }

        ReadOk();
        m_rawStatus = axisStatus;

        // Vị trí là trường số nguyên riêng — đúng kể cả khi bảng bit chưa xác minh.
        m_rawPulse = actPos;
        m_pos = ToMachine(actPos);

        bool alarm = (axisStatus & EziAxisStatus::AnyErrorMask) != 0;
        m_servoOn = EziAxisStatus::Has(axisStatus, EziAxisStatus::ServoOn);
        m_alarm = alarm;
        m_busy = (axisStatus & EziAxisStatus::BusyMask) != 0;
        m_fls = EziAxisStatus::Has(axisStatus, EziAxisStatus::HwPosLimit);
        m_rls = EziAxisStatus::Has(axisStatus, EziAxisStatus::HwNegLimit);
        m_errorCode = (int)(axisStatus & EziAxisStatus::AnyErrorMask);

        // Servo OFF không làm mất gốc (Tuấn bỏ 2026-09-26) — driver vẫn đếm encoder. Alarm thì vẫn mất.
        if (alarm) m_softHomed = false;

        m_orgOk = m_softHomed.load() && EziAxisStatus::Has(axisStatus, EziAxisStatus::OriginReturnOk);
    }

    void FastechAxis::ReadFailed(const std::string& why)
    {
        int streak = ++m_failStreak;
        if (streak < FailLimit) return;
        if (m_connected)
            LogWorker::Instance().RecordLog(
                std::format("{}: connection lost after {} consecutive failed reads ({}) — still retrying every {} ms, "
                            "no app restart needed", m_name, streak, why, RetryMs),
                LogLevel::Warning, m_name);
        m_connected = false;
    }

    void FastechAxis::ReadOk()
    {
        if (!m_connected && m_opened)
            LogWorker::Instance().RecordLog(m_name + ": reads OK again, connection back to normal",
                LogLevel::Info, m_name);
        m_failStreak = 0;
        m_connected = true;
    }

    // ───────────────────────── quy đổi toạ độ ─────────────────────────

    double FastechAxis::ToMachine(int pulse) const
    {
        double deg = m_scale.ToUnit(pulse);
        if (!m_cfg->WrapDegrees) return deg;
        return AxisAngle::Wrap(deg + m_cfg->HomeOffset);
    }

    int FastechAxis::TargetPulse(double pos) const
    {
        if (!m_cfg->WrapDegrees) return m_scale.ToPulse(pos);

        int perRev = m_scale.PulsePerRev();
        int basePulse = m_scale.ToPulse(AxisAngle::Wrap(pos - m_cfg->HomeOffset));

        // k = số vòng phải cộng thêm để basePulse về sát vị trí hiện tại. Làm tròn nửa lên,
        // nên đúng nửa vòng thì đi chiều dương — cùng quy ước hoà với AxisAngle::ShortestDelta.
        int k = (int)std::floor((double)((long long)m_rawPulse.load() - basePulse) / (double)perRev + 0.5);
        return basePulse + k * perRev;
    }

    // ───────────────────────── lệnh ─────────────────────────

    bool FastechAxis::SetServoOn(bool on)
    {
        if (!IsConnected()) return false;
        try
        {
            int rc;
            {
                CmdLock lock(m_cmdLock);
                rc = EziMOTIONPlusELib::FAS_ServoEnable(m_bdId, on ? 1 : 0);
            }
            if (rc != 0) return false;

            if (!on) return true;   // không làm mất gốc — xem Refresh

            base::Stopwatch sw;
            while (sw.ElapsedMilliseconds() < 5000)
            {
                uint32_t st = 0;
                {
                    CmdLock lock(m_cmdLock);
                    EziMOTIONPlusELib::FAS_GetAxisStatus(m_bdId, st);
                }
                if (EziAxisStatus::Has(st, EziAxisStatus::ServoOn)) return true;
                if ((st & EziAxisStatus::AnyErrorMask) != 0) return false;
                base::Delay(20);
            }
            return false;
        }
        catch (const std::exception& ex)
        {
            LogWorker::Instance().RecordLog(std::format("{}: SetServoOn error — {}", m_name, ex.what()),
                LogLevel::Error, m_name);
            return false;
        }
    }

    bool FastechAxis::Home(std::stop_token ct)
    {
        if (!IsConnected()) return false;

        m_softHomed = false;
        m_orgOk = false;

        int rc;
        {
            CmdLock lock(m_cmdLock);
            rc = EziMOTIONPlusELib::FAS_MoveOriginSingleAxis(m_bdId);
        }
        if (rc != 0)
        {
            AlarmManager::RaiseAxisEvent(m_name, AxisFault::HomeTimeout, Loc::F("Comm.FasReturned", "FAS_MoveOriginSingleAxis", rc));
            return false;
        }

        base::Stopwatch sw;
        while (sw.ElapsedMilliseconds() < m_cfg->HomeTimeoutMs)
        {
            if (ct.stop_requested()) { Stop(); return false; }
            base::Delay(20, ct);   // C#: await Task.Delay(20, ct) — huỷ giữa lúc chờ thì ném OperationCanceled

            uint32_t st = 0;
            {
                CmdLock lock(m_cmdLock);
                EziMOTIONPlusELib::FAS_GetAxisStatus(m_bdId, st);
            }

            if ((st & EziAxisStatus::AnyErrorMask) != 0)
            {
                AlarmManager::RaiseAxisEvent(m_name, AxisFault::HomeTimeout,
                    Loc::F("Axis.HomeDriverError", EziAxisStatus::Describe(st)));
                return false;
            }

            if (EziAxisStatus::Has(st, EziAxisStatus::OriginReturnOk) &&
                !EziAxisStatus::Has(st, EziAxisStatus::OriginReturning))
            {
                m_softHomed = true;
                m_orgOk = true;
                LogWorker::Instance().RecordLog(
                    std::format("{}: origin done in {:.1f} s", m_name, sw.ElapsedSeconds()),
                    LogLevel::Info, m_name);
                return true;
            }
        }

        Stop();
        AlarmManager::RaiseAxisEvent(m_name, AxisFault::HomeTimeout, Loc::F("Axis.TimeoutMs", m_cfg->HomeTimeoutMs));
        return false;
    }

    bool FastechAxis::MoveAbs(double pos, double speed, std::stop_token ct, bool /*gentle*/)
    {
        if (!IsConnected()) return false;

        if (!m_cfg->InSoftLimit(pos))
        {
            AlarmManager::RaiseAxisEvent(m_name, AxisFault::SoftLimit,
                Loc::F("Axis.SoftLimitOut", pos, m_cfg->Unit, m_cfg->SoftLimitNeg, m_cfg->SoftLimitPos));
            return false;
        }

        int target = TargetPulse(pos);
        uint32_t vel = m_scale.ToPulsePerSec(speed);

        int rc;
        {
            CmdLock lock(m_cmdLock);
            rc = EziMOTIONPlusELib::FAS_MoveSingleAxisAbsPos(m_bdId, target, vel);
        }
        if (rc != 0)
        {
            AlarmManager::RaiseAxisEvent(m_name, AxisFault::MoveTimeout, Loc::F("Comm.FasReturned", "FAS_MoveSingleAxisAbsPos", rc));
            return false;
        }

        return WaitReached(target, ct);
    }

    bool FastechAxis::MoveInc(double delta, double speed, std::stop_token ct)
    {
        if (!IsConnected()) return false;

        double pos = CurrentPos() + delta;
        if (!m_cfg->InSoftLimit(pos))
        {
            AlarmManager::RaiseAxisEvent(m_name, AxisFault::SoftLimit,
                Loc::F("Axis.SoftLimitOut", pos, m_cfg->Unit, m_cfg->SoftLimitNeg, m_cfg->SoftLimitPos));
            return false;
        }

        int incPulse = m_scale.ToPulse(delta);
        uint32_t vel = m_scale.ToPulsePerSec(speed);
        int target = m_rawPulse.load() + incPulse;

        int rc;
        {
            CmdLock lock(m_cmdLock);
            rc = EziMOTIONPlusELib::FAS_MoveSingleAxisIncPos(m_bdId, incPulse, vel);
        }
        if (rc != 0)
        {
            AlarmManager::RaiseAxisEvent(m_name, AxisFault::MoveTimeout, Loc::F("Comm.FasReturned", "FAS_MoveSingleAxisIncPos", rc));
            return false;
        }

        return WaitReached(target, ct);
    }

    bool FastechAxis::WaitReached(int targetPulse, std::stop_token ct)
    {
        base::Stopwatch sw;
        int tol = std::max(1, m_cfg->PositionTolerancePulse);

        while (sw.ElapsedMilliseconds() < m_cfg->MoveTimeoutMs)
        {
            if (ct.stop_requested()) { Stop(); return false; }
            base::Delay(10, ct);   // C#: await Task.Delay(10, ct)

            uint32_t st = 0;
            int32_t actPos = 0;
            {
                CmdLock lock(m_cmdLock);
                EziMOTIONPlusELib::FAS_GetAxisStatus(m_bdId, st);
                EziMOTIONPlusELib::FAS_GetActualPos(m_bdId, actPos);
            }

            if ((st & EziAxisStatus::AnyErrorMask) != 0)
            {
                AlarmManager::RaiseAxis(m_name, AxisFault::DriverAlarm, EziAxisStatus::Describe(st));
                return false;
            }
            if (EziAxisStatus::Has(st, EziAxisStatus::HwPosLimit) ||
                EziAxisStatus::Has(st, EziAxisStatus::HwNegLimit))
            {
                AlarmManager::RaiseAxis(m_name, AxisFault::HardLimit,
                    EziAxisStatus::Has(st, EziAxisStatus::HwPosLimit) ? Loc::T("Axis.HwLimitPos") : Loc::T("Axis.HwLimitNeg"));
                return false;
            }

            if (!EziAxisStatus::Has(st, EziAxisStatus::Motioning) &&
                std::llabs((long long)actPos - targetPulse) <= tol)
            {
                // Ghi luôn con số vừa đọc vào CurrentPos — xem ghi chú dài ở
                // PanasonicAxis::WaitMotion (Tuấn phát hiện 2026-09-19).
                m_pos = ToMachine(actPos);
                return true;
            }
        }

        Stop();
        AlarmManager::RaiseAxisEvent(m_name, AxisFault::MoveTimeout,
            Loc::F("Axis.MoveTimeout",
                m_cfg->MoveTimeoutMs, ToMachine(targetPulse), m_cfg->Unit, CurrentPos()));
        return false;
    }

    void FastechAxis::Jog(int dir, double speed)
    {
        if (!IsConnected()) return;
        uint32_t pulsePerSec = m_scale.ToPulsePerSec(speed);
        auto self = KeepAlive();
        base::RunDetached([this, self, dir, pulsePerSec]
        {
            try
            {
                CmdLock lock(m_cmdLock);
                if (dir == 0) EziMOTIONPlusELib::FAS_MoveStop(m_bdId);
                else EziMOTIONPlusELib::FAS_MoveVelocity(m_bdId, pulsePerSec, dir > 0 ? 1 : 0);
            }
            catch (const std::exception& ex)
            {
                LogWorker::Instance().RecordLog(std::format("{}: Jog error — {}", m_name, ex.what()),
                    LogLevel::Error, m_name);
            }
        });
    }

    void FastechAxis::Stop()
    {
        if (!IsConnected()) return;
        auto self = KeepAlive();
        base::RunDetached([this, self]
        {
            try { CmdLock lock(m_cmdLock); EziMOTIONPlusELib::FAS_MoveStop(m_bdId); } catch (...) {}
        });
    }

    bool FastechAxis::ResetAlarm()
    {
        if (!IsConnected()) return false;
        try
        {
            int rc;
            {
                CmdLock lock(m_cmdLock);
                rc = EziMOTIONPlusELib::FAS_ServoAlarmReset(m_bdId);
            }

            m_softHomed = false;
            m_orgOk = false;

            base::Delay(200);
            return rc == 0;
        }
        catch (...) { return false; }
    }
}
