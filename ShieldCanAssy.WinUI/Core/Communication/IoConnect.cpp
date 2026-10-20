#include "Communication/IoConnect.h"

#include <format>

#include "Fastech/EziMOTIONPlusELib.h"
#include "Fastech/FastechBoardId.h"
#include "Infor/AlarmCode.h"
#include "Infor/AlarmManager.h"
#include "Lang/Loc.h"
#include "Utility/Log/LogWorker.h"

namespace sca
{
    namespace
    {
        using Lock = std::lock_guard<std::mutex>;
    }

    IoConnect::IoConnect(std::shared_ptr<SystemData> sys)
        : m_sys(std::move(sys))
    {
    }

    // ───────────────────────── kết nối ─────────────────────────

    bool IoConnect::Connect()
    {
        m_diOk = Open(m_sys->IoDiIpAddress, FastechBoardId::Io32Di, "32Di");
        m_doOk = Open(m_sys->IoDoIpAddress, FastechBoardId::Io32Do, "32Do");

        // Đọc lại ảnh DO module đang giữ TRƯỚC khi ghi bất cứ gì xuống (Tuấn phát hiện,
        // 2026-09-13: khởi động lại app thì mọi DO về 0). Bản cũ xoá sạch ở đây với lý do
        // "lúc khởi tạo chưa có sản phẩm trên tay hút" — sai từ khi Auto chạy tiếp được sau
        // khi mở lại app: tắt van hút lúc này là thả rơi sản phẩm vào trong máy, đúng điều
        // §3.1 quy tắc 3 cấm. DO nào được tính lại mỗi vòng quét (đèn tháp, còi, đèn nút,
        // nhả phanh) thì vòng quét đầu tự ghi đè; DO nghiệp vụ (hút, thổi, đèn camera) giữ
        // nguyên tới khi phần mềm chủ động đổi.
        {
            Lock l(m_lock);
            m_doSynced = false;
        }
        if (m_doOk) m_doOk = SyncOutputsFromModule(true);

        m_connected = m_diOk && m_doOk;
        m_failStreak = 0;
        return m_connected.load();
    }

    bool IoConnect::SyncOutputsFromModule(bool logFailure)
    {
        uint32_t output = 0, status = 0;
        int rc;
        try { rc = EziMOTIONPlusELib::FAS_GetOutput(FastechBoardId::Io32Do, output, status); }
        catch (...) { rc = -1; }

        if (rc != 0)
        {
            if (logFailure)
                LogWorker::Instance().RecordLog(
                    std::format("Ezi-IO 32Do: cannot read current outputs (code {}) — no DO written yet, retrying in scan", rc),
                    LogLevel::Error, "IO");
            return false;
        }

        {
            Lock l(m_lock);
            m_doWanted = output;
            m_doWritten = output;
            m_doDirty = false;
            m_doSynced = true;
            for (int i = 0; i < IoMap::DoCount; i++)
                m_do[i] = (output & (1u << i)) != 0;
        }

        LogWorker::Instance().RecordLog(
            std::format("Ezi-IO 32Do: keeping outputs held by module — 0x{:08X}", output),
            LogLevel::Info, "IO");
        return true;
    }

    bool IoConnect::Open(const std::string& ipText, int bdId, const char* what)
    {
        IPAddress ip;
        if (!IPAddress::TryParse(ipText, ip))
        {
            LogWorker::Instance().RecordLog(std::format("Ezi-IO {}: invalid IP — {}", what, ipText),
                LogLevel::Error, "IO");
            return false;
        }

        try
        {
            // C# catch (DllNotFoundException): P/Invoke nạp DLL ở lời gọi FAS_* đầu tiên — ở đây nạp tường minh.
            std::string dllError;
            if (!EziMOTIONPlusELib::IsLoaded() && !EziMOTIONPlusELib::Load(dllError))
            {
                LogWorker::Instance().RecordLog(
                    std::format("Ezi-IO {}: EziMOTIONPlusE.dll not found next to exe{}", what,
                                dllError.empty() ? std::string() : " (" + dllError + ")"),
                    LogLevel::Error, "IO");
                return false;
            }

            EziMOTIONPlusELib::FAS_Close(bdId);
            if (!EziMOTIONPlusELib::FAS_Connect(ip, bdId))
            {
                LogWorker::Instance().RecordLog(std::format("Ezi-IO {}: cannot connect {}", what, ipText),
                    LogLevel::Error, "IO");
                return false;
            }

            LogWorker::Instance().RecordLog(
                std::format("Ezi-IO {}: connected {} (bdID {})", what, ipText, bdId),
                LogLevel::Info, "IO");
            return true;
        }
        catch (const std::exception& ex)
        {
            LogWorker::Instance().RecordLog(std::format("Ezi-IO {}: connect error — {}", what, ex.what()),
                LogLevel::Error, "IO");
            return false;
        }
    }

    void IoConnect::Disconnect()
    {
        try { EziMOTIONPlusELib::FAS_Close(FastechBoardId::Io32Di); } catch (...) {}
        try { EziMOTIONPlusELib::FAS_Close(FastechBoardId::Io32Do); } catch (...) {}
        m_diOk = false;
        m_doOk = false;
        {
            Lock l(m_lock);
            m_doSynced = false;
        }
        m_connected = false;
    }

    // ───────────────────────── vòng quét ─────────────────────────

    void IoConnect::Refresh()
    {
        bool ok = ReadInputs();
        bool synced;
        {
            Lock l(m_lock);
            synced = m_doSynced;
        }
        if (!synced) SyncOutputsFromModule(false);
        ok &= Flush();

        if (ok) m_failStreak = 0;
        else ++m_failStreak;

        m_connected = m_failStreak.load() < FailLimit;
    }

    bool IoConnect::ReadInputs()
    {
        uint32_t input = 0, latch = 0;
        int rc;
        try { rc = EziMOTIONPlusELib::FAS_GetInput(FastechBoardId::Io32Di, input, latch); }
        catch (...) { return false; }

        if (rc != 0) return false;

        Lock l(m_lock);
        m_diPrev = m_di;
        for (int i = 0; i < IoMap::DiCount; i++)
            m_di[i] = (input & (1u << i)) != 0;
        return true;
    }

    bool IoConnect::Flush()
    {
        uint32_t want;
        {
            Lock l(m_lock);
            if (!m_doSynced) return false;
            if (!m_doDirty && m_doWanted == m_doWritten) return true;
            want = m_doWanted;
        }

        uint32_t setMask = want;
        uint32_t clrMask = ~want;

        int rc;
        try { rc = EziMOTIONPlusELib::FAS_SetOutput(FastechBoardId::Io32Do, setMask, clrMask); }
        catch (...) { return false; }

        if (rc != 0)
        {
            AlarmManager::Raise(AlarmCode::IoWriteFailed, "IO", Loc::F("Comm.FasReturned", "FAS_SetOutput", rc));
            return false;
        }

        AlarmManager::ClearCondition(AlarmCode::IoWriteFailed);
        Lock l(m_lock);
        m_doWritten = want;
        m_doDirty = false;
        for (int i = 0; i < IoMap::DoCount; i++)
            m_do[i] = (want & (1u << i)) != 0;
        return true;
    }

    // ───────────────────────── đọc / ghi bit ─────────────────────────

    bool IoConnect::GetDi(int bit)
    {
        Lock l(m_lock);
        return bit >= 0 && bit < IoMap::DiCount && m_di[bit];
    }

    bool IoConnect::GetDo(int bit)
    {
        Lock l(m_lock);
        return bit >= 0 && bit < IoMap::DoCount && m_do[bit];
    }

    void IoConnect::SetDo(int bit, bool value)
    {
        if (bit < 0 || bit >= IoMap::DoCount) return;
        Lock l(m_lock);
        uint32_t mask = 1u << bit;
        uint32_t next = value ? (m_doWanted | mask) : (m_doWanted & ~mask);
        if (next == m_doWanted) return;
        m_doWanted = next;
        m_doDirty = true;
    }

    bool IoConnect::RisingEdge(int bit)
    {
        Lock l(m_lock);
        return bit >= 0 && bit < IoMap::DiCount && m_di[bit] && !m_diPrev[bit];
    }

    bool IoConnect::FallingEdge(int bit)
    {
        Lock l(m_lock);
        return bit >= 0 && bit < IoMap::DiCount && !m_di[bit] && m_diPrev[bit];
    }

    uint32_t IoConnect::RawDi() const
    {
        uint32_t v = 0;
        Lock l(m_lock);
        for (int i = 0; i < IoMap::DiCount; i++)
            if (m_di[i]) v |= 1u << i;
        return v;
    }

    uint32_t IoConnect::RawDo() const
    {
        Lock l(m_lock);
        return m_doWanted;
    }
}
