#include "Controller/SafetyMonitor.h"

#include "Infor/AlarmManager.h"
#include "Infor/IoMap.h"
#include "Lang/Loc.h"
#include "Utility/Log/LogWorker.h"

namespace sca
{
    SafetyMonitor::SafetyMonitor(MotionConnect& motion, McStatus& status, std::function<std::shared_ptr<SystemData>()> sys)
        : m_motion(motion), m_status(status), m_sys(std::move(sys))
    {
    }

    std::stop_token SafetyMonitor::AbortToken() const
    {
        std::lock_guard<std::mutex> lock(m_ctsLock);
        return m_cts.get_token();
    }

    std::string SafetyMonitor::BlockReason() const
    {
        const char* key = m_blockKey.load();
        if (key == nullptr || *key == '\0') return {};
        return Loc::T(key);
    }

    void SafetyMonitor::RenewToken()
    {
        std::lock_guard<std::mutex> lock(m_ctsLock);
        if (!m_cts.stop_requested()) return;
        m_cts = std::stop_source();
    }

    void SafetyMonitor::Abort(const std::string& reason)
    {
        std::stop_source cts;
        {
            std::lock_guard<std::mutex> lock(m_ctsLock);
            if (m_cts.stop_requested()) return;
            cts = m_cts;
        }
        // Huỷ NGOÀI khoá: các stop_callback của token (lệnh trục đang chờ) chạy ngay trên luồng này. request_stop trả
        // false nếu luồng khác vừa huỷ trước — khi đó chỉ một bên ghi log và dừng trục, như lock + IsCancellationRequested.
        if (!cts.request_stop()) return;
        LogWorker::Instance().RecordLog("Abort all motion: " + reason, LogLevel::Error, "Safety");
        m_motion.StopAll();
    }

    void SafetyMonitor::Evaluate()
    {
        auto sysPtr = m_sys();
        const SystemData& sys = *sysPtr;
        IIoDevice* io = m_io.get();

        bool emgOk = io != nullptr && io->IsConnected() && io->GetDi(IoMap::EmgOk);
        bool airOk = !sys.HasAirSensor || (io != nullptr && io->GetDi(IoMap::AirOk));
        bool ioOk = io != nullptr && io->IsConnected();
        bool curtainOk = !sys.HasLightCurtain || (io != nullptr && io->GetDi(IoMap::LightCurtainOk));

        m_status.SetEmgOk(emgOk);
        m_status.SetLightCurtainOk(curtainOk);
        m_status.SetAirOk(airOk);
        m_status.SetIoConnected(ioOk);
        m_status.SetMotionConnected(m_motion.AllConnected());

        // ── Card I/O ──
        //
        // Trên máy này EMG và rèm sáng đều đọc qua Ezi-IO (quyết định của chủ đầu tư, xem
        // PLAN.md §3.1). Hệ quả trực tiếp: MẤT KẾT NỐI EZI-IO LÀ MÙ HOÀN TOÀN — không còn
        // đường nào biết EMG có đang bị nhấn hay rèm có bị cắt hay không. Nên mất kết nối
        // phải được xử lý như chính sự cố tệ nhất mà nó che mất: dừng ngay mọi chuyển động,
        // không chỉ chặn lệnh mới.
        if (!ioOk)
        {
            AlarmManager::Raise(AlarmCode::IoDisconnected);
            Abort("I/O card disconnected — EMG and light curtain unreadable");
        }
        else
        {
            AlarmManager::ClearCondition(AlarmCode::IoDisconnected);
        }

        // ── EMG (NC: 0 = đã nhấn hoặc đứt dây) ──
        if (ioOk && !emgOk)
        {
            AlarmManager::Raise(AlarmCode::EmergencyStop);
            Abort("EMG");
        }
        else
        {
            AlarmManager::ClearCondition(AlarmCode::EmergencyStop);
        }

        // ── Rèm sáng ──
        // Rèm bị cắt thì DỪNG NGAY mọi trục dù đang ở chế độ nào, vì người có thể thò tay qua rèm bất cứ lúc nào — kể
        // cả lúc thợ đang jog tay ở Manual — và chặn lệnh mới tới khi rèm thông. Chu trình Auto thì tạm dừng như STOP,
        // rèm thông rồi bấm Start chạy tiếp đúng chỗ dở (Tuấn 2026-10-01) — không alarm, không RESET. Rèm
        // thông trở lại cũng không tự chạy: phải có người bấm Start.
        // Chỉ ghi Nhật ký khi có trục đang chạy thật sự bị dừng — máy đứng yên mà đi qua rèm thì không ghi (Tuấn 2026-10-07).
        if (ioOk && !curtainOk)
        {
            if (m_curtainWasOk)
            {
                m_curtainWasOk = false;
                if (m_motion.AnyBusy())
                    LogWorker::Instance().RecordLog("Light curtain broken — all motion stopped", LogLevel::Warning, "Safety");
                if (m_curtainBroken) m_curtainBroken();
                m_motion.StopAll();
            }
        }
        else if (ioOk)
        {
            m_curtainWasOk = true;
        }

        // ── Áp suất khí ──
        if (ioOk && !airOk) AlarmManager::Raise(AlarmCode::AirPressureLow);
        else AlarmManager::ClearCondition(AlarmCode::AirPressureLow);

        // ── Trục có alarm thì dừng luôn, không chờ routine phát hiện ──
        if (m_motion.AnyAlarm() || !m_motion.AllConnected())
        {
            EMcState st = m_status.State();
            if (st == EMcState::Running || st == EMcState::Homing)
                Abort("axis alarm or axis disconnected");
        }

        // ── Kết luận ──
        m_status.SetSafetyChainOk(emgOk && curtainOk && airOk);
        m_safeToMove = ComputeSafeToMove(sys, ioOk, emgOk, curtainOk, airOk);
    }

    bool SafetyMonitor::ComputeSafeToMove(const SystemData& /*sys*/, bool ioOk, bool emgOk, bool curtainOk, bool airOk)
    {
        if (!ioOk)                     { m_blockKey = "Mc.BlockIoLost"; return false; }
        if (!emgOk)                    { m_blockKey = "Mc.BlockEmg"; return false; }
        if (!curtainOk)                { m_blockKey = "Mc.BlockCurtain"; return false; }
        if (AlarmManager::HasError())  { m_blockKey = "Mc.BlockAlarm"; return false; }
        if (!m_motion.AllConnected())  { m_blockKey = "Mc.BlockAxisLost"; return false; }
        if (m_motion.AnyAlarm())       { m_blockKey = "Mc.BlockAxisAlarm"; return false; }
        if (!airOk)                    { m_blockKey = "Mc.BlockAir"; return false; }

        m_blockKey = "";
        return true;
    }
}
