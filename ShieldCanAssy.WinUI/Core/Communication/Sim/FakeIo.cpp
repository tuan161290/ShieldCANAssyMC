#include "Communication/Sim/FakeIo.h"

namespace sca
{
    namespace
    {
        using Lock = std::lock_guard<std::mutex>;
    }

    FakeIo::FakeIo()
    {
        // Tín hiệu an toàn đấu NC: 1 = bình thường.
        m_di[IoMap::EmgOk] = true;
        m_di[IoMap::LightCurtainOk] = true;
        m_di[IoMap::AirOk] = true;
        // Tray 2 đầu vào mặc định VẮNG: model SIM không teach Tray 2, máy mô phỏng chạy một tray như trước nên thời gian
        // self-test không đổi — bài kiểm hai tray tự bật cảm biến này.
        m_di[IoMap::TrayIn1Present] = true;
        m_di[IoMap::TrayAssyPresent] = true;
    }

    bool FakeIo::GetDi(int bit)
    {
        Lock l(m_lock);
        return bit >= 0 && bit < IoMap::DiCount && m_di[bit];
    }

    bool FakeIo::GetDo(int bit)
    {
        Lock l(m_lock);
        return bit >= 0 && bit < IoMap::DoCount && m_do[bit];
    }

    void FakeIo::SetDo(int bit, bool value)
    {
        if (bit < 0 || bit >= IoMap::DoCount) return;
        {
            Lock l(m_lock);
            if (m_do[bit] == value) return;
            m_do[bit] = value;
        }

        // Cảm biến chân không bám theo van hút — có xác suất hút hụt để kiểm thử retry.
        if (bit == IoMap::DoVacuum1) SetVacuumSensor(IoMap::Vacuum1, value);
        if (bit == IoMap::DoVacuum2) SetVacuumSensor(IoMap::Vacuum2, value);

        auto after = AfterSetDo.Get();
        if (after) after(bit, value);
    }

    void FakeIo::SetVacuumSensor(int sensorBit, bool valveOn)
    {
        Lock l(m_lock);
        bool sensed = valveOn && std::uniform_real_distribution<double>(0.0, 1.0)(m_rng) < m_pickSuccessRate.load();
        m_di[sensorBit] = sensed;
    }

    void FakeIo::Refresh()
    {
        Lock l(m_lock);
        m_diPrev = m_di;

        // Nút bấm giả lập: giữ đúng MỘT vòng quét rồi tự nhả, để bên tiêu thụ buộc phải
        // xử lý bằng sườn lên chứ không phải bằng mức (PLAN.md §3.2).
        for (int bit : m_pendingPress)
            if (bit >= 0 && bit < IoMap::DiCount) m_di[bit] = true;

        for (int bit : m_pendingPress)
            if (bit >= 0 && bit < IoMap::DiCount) m_diPrev[bit] = false;

        m_pendingPress.clear();

        for (int i = 0; i < IoMap::DiCount; i++)
        {
            if (i == IoMap::BtnStop ||
                i == IoMap::BtnArmA1 || i == IoMap::BtnArmA2 ||
                i == IoMap::BtnGoB1 || i == IoMap::BtnGoB2)
            {
                // Nút không được giữ: nếu vòng trước đã ở mức 1 thì vòng này nhả.
                if (m_diPrev[i]) m_di[i] = false;
            }
        }
    }

    bool FakeIo::RisingEdge(int bit)
    {
        Lock l(m_lock);
        return bit >= 0 && bit < IoMap::DiCount && m_di[bit] && !m_diPrev[bit];
    }

    bool FakeIo::FallingEdge(int bit)
    {
        Lock l(m_lock);
        return bit >= 0 && bit < IoMap::DiCount && !m_di[bit] && m_diPrev[bit];
    }

    void FakeIo::SimPress(int diBit)
    {
        Lock l(m_lock);
        m_pendingPress.insert(diBit);
    }

    void FakeIo::SimToggle(int diBit)
    {
        Lock l(m_lock);
        if (diBit < 0 || diBit >= IoMap::DiCount) return;
        m_di[diBit] = !m_di[diBit];
    }

    void FakeIo::SimSet(int diBit, bool value)
    {
        Lock l(m_lock);
        if (diBit < 0 || diBit >= IoMap::DiCount) return;
        m_di[diBit] = value;
    }
}
