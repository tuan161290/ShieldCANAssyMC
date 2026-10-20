#include "Communication/MotionConnect.h"

#include <algorithm>
#include <format>

#include "Communication/FastechAxis.h"
#include "Communication/PanasonicAxis.h"
#include "Communication/Sim/FakeAxis.h"
#include "Infor/AlarmManager.h"
#include "Infor/AxisId.h"
#include "Infor/Enums.h"
#include "Utility/Log/LogWorker.h"

namespace sca
{
    std::shared_ptr<IAxis> MotionConnect::Axis(std::string_view name) const
    {
        auto it = m_axes.find(name);
        return it != m_axes.end() ? it->second : nullptr;
    }

    void MotionConnect::Init(SystemData& sys)
    {
        m_axes.clear();
        m_simulation = sys.SimulationMode;
        m_zSafe = sys.ZSafe;
        bool simulation = sys.SimulationMode;

        std::vector<std::shared_ptr<IAxis>> list;
        for (const auto& id : AxisId::All)
        {
            std::string name(id);
            auto cfg = sys.GetAxis(name);

            auto axis = CreateAxis(cfg, simulation);
            m_axes[name] = axis;
            list.push_back(axis);
        }

        m_axisList = std::move(list);
    }

    std::shared_ptr<IAxis> MotionConnect::CreateAxis(const std::shared_ptr<AxisConfig>& cfg, bool simulation)
    {
        if (simulation) return std::make_shared<FakeAxis>(cfg);

        switch (cfg->Kind)
        {
            case EAxisKind::Fastech:
                return std::make_shared<FastechAxis>(cfg);
            case EAxisKind::Panasonic:
            default:
                return std::make_shared<PanasonicAxis>(cfg);
        }
    }

    bool MotionConnect::ConnectAll()
    {
        bool all = true;
        for (const auto& axis : m_axisList)
        {
            bool ok = false;
            try { ok = axis->Connect(); }
            catch (const std::exception& ex)
            {
                LogWorker::Instance().RecordLog(std::format("Axis {} connect error: {}", axis->Name(), ex.what()),
                    LogLevel::Error, axis->Name());
            }
            catch (...)
            {
                LogWorker::Instance().RecordLog(std::format("Axis {} connect error: unknown exception", axis->Name()),
                    LogLevel::Error, axis->Name());
            }

            if (!ok)
            {
                AlarmManager::RaiseAxis(axis->Name(), AxisFault::Disconnected);
                all = false;
            }
        }
        return all;
    }

    void MotionConnect::DisconnectAll()
    {
        for (const auto& axis : m_axisList)
        {
            try { axis->Disconnect(); } catch (...) {}
        }
    }

    void MotionConnect::Refresh()
    {
        for (const auto& axis : m_axisList)
        {
            try
            {
                axis->Refresh();

                if (!axis->IsConnected()) AlarmManager::RaiseAxis(axis->Name(), AxisFault::Disconnected);
                else AlarmManager::ClearConditionAxis(axis->Name(), AxisFault::Disconnected);

                if (axis->Alarm()) AlarmManager::RaiseAxis(axis->Name(), AxisFault::DriverAlarm, std::format("0x{:04X}", axis->ErrorCode()));
                else AlarmManager::ClearConditionAxis(axis->Name(), AxisFault::DriverAlarm);

                bool fls = axis->FLS();
                bool rls = axis->RLS() && !IgnoreNegLimitWhileHoming(axis->Name());
                if (fls || rls)
                    AlarmManager::RaiseAxis(axis->Name(), AxisFault::HardLimit, fls ? "POT" : "NOT");
                else
                    AlarmManager::ClearConditionAxis(axis->Name(), AxisFault::HardLimit);
            }
            catch (const std::exception& ex)
            {
                LogWorker::Instance().RecordLog(std::format("Axis {} scan error: {}", axis->Name(), ex.what()),
                    LogLevel::Error, axis->Name());
            }
            catch (...)
            {
                LogWorker::Instance().RecordLog(std::format("Axis {} scan error: unknown exception", axis->Name()),
                    LogLevel::Error, axis->Name());
            }
        }
    }

    bool MotionConnect::Home(const std::shared_ptr<IAxis>& axis, std::stop_token ct)
    {
        const std::string name = axis->Name();
        {
            std::lock_guard<std::mutex> lock(m_homingMutex);
            m_homing.insert(name);
        }
        // finally { _homing.TryRemove(axis.Name, out _); }
        struct Remove
        {
            MotionConnect& self;
            const std::string& name;
            ~Remove()
            {
                std::lock_guard<std::mutex> lock(self.m_homingMutex);
                self.m_homing.erase(name);
            }
        } remove{ *this, name };

        return axis->Home(ct);
    }

    bool MotionConnect::IsHoming(std::string_view name) const
    {
        std::lock_guard<std::mutex> lock(m_homingMutex);
        return m_homing.find(name) != m_homing.end();
    }

    bool MotionConnect::IgnoreNegLimitWhileHoming(std::string_view name) const
    {
        return (name == AxisId::Y1 || name == AxisId::Y2) && IsHoming(name);
    }

    bool MotionConnect::AllConnected() const
    {
        return !m_axisList.empty() && std::all_of(m_axisList.begin(), m_axisList.end(), [](const auto& a) { return a->IsConnected(); });
    }

    bool MotionConnect::AllServoOn() const
    {
        return !m_axisList.empty() && std::all_of(m_axisList.begin(), m_axisList.end(), [](const auto& a) { return a->ServoOn(); });
    }

    bool MotionConnect::AllHomed() const
    {
        return !m_axisList.empty() && std::all_of(m_axisList.begin(), m_axisList.end(), [](const auto& a) { return a->OrgOk(); });
    }

    bool MotionConnect::AnyAlarm() const
    {
        return std::any_of(m_axisList.begin(), m_axisList.end(), [](const auto& a) { return a->Alarm(); });
    }

    bool MotionConnect::AnyBusy() const
    {
        return std::any_of(m_axisList.begin(), m_axisList.end(), [](const auto& a) { return a->Busy(); });
    }

    bool MotionConnect::ZSafeOk() const
    {
        auto z1 = Axis(AxisId::Z1);
        auto z2 = Axis(AxisId::Z2);
        if (z1 == nullptr || z2 == nullptr) return false;
        return z1->CurrentPos() <= ZSafeLimit() && z2->CurrentPos() <= ZSafeLimit();
    }

    void MotionConnect::StopAll()
    {
        for (const auto& axis : m_axisList)
        {
            try { axis->Stop(); } catch (...) {}
        }
    }
}
