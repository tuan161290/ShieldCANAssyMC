#include "Controller/McStatus.h"

#include "Lang/Loc.h"

namespace sca
{
    std::string McStatus::ModelName() const
    {
        {
            std::lock_guard<std::mutex> l(m_textMutex);
            if (m_modelName) return *m_modelName;
        }
        // null = chưa nạp model — hiện chữ "(not loaded)".
        return Loc::T("Status.ModelNotLoaded");
    }

    std::string McStatus::AutoStepText() const
    {
        EAutoState s = AutoStep();
        switch (s)
        {
            case EAutoState::S00_Ready:     return Loc::T("Status.StepS00");
            case EAutoState::S05_ScanAssy:  return Loc::T("Status.StepS05");
            case EAutoState::S10_Pick1:     return Loc::T("Status.StepS10");
            case EAutoState::S11_Pick2:     return Loc::T("Status.StepS11");
            // Tay #2 gần CAM1 hơn nên soi trước (Tuấn xác nhận 2026-09-05) — S20 vẫn là
            // BƯỚC đầu của cặp align, không phải "tay #1", xem Routine_Auto.
            case EAutoState::S20_Align1:    return Loc::T("Status.StepS20");
            case EAutoState::S21_Align2:    return Loc::T("Status.StepS21");
            case EAutoState::S30_Locate1:   return Loc::T("Status.StepS30");
            case EAutoState::S31_Place1:    return Loc::T("Status.StepS31");
            case EAutoState::S32_Locate2:   return Loc::T("Status.StepS32");
            case EAutoState::S33_Place2:    return Loc::T("Status.StepS33");
            case EAutoState::S40_Index:     return Loc::T("Status.StepS40");
            case EAutoState::S50_TrayCheck: return Loc::T("Status.StepS50");
            default:                        return ToString(s);
        }
    }

    std::string McStatus::StateText() const
    {
        EMcState s = State();
        switch (s)
        {
            case EMcState::Init:         return Loc::T("Status.StateInit");
            case EMcState::NotHomed:     return Loc::T("Status.StateNotHomed");
            case EMcState::Homing:       return Loc::T("Status.StateHoming");
            case EMcState::Idle:         return Loc::T("Status.StateIdle");
            case EMcState::Armed:        return Loc::T("Status.StateArmed");
            case EMcState::Running:      return Loc::T("Status.StateRunning");
            case EMcState::Pausing:      return Loc::T("Status.StatePausing");
            case EMcState::Paused:       return Loc::T("Status.StatePaused");
            case EMcState::Held:         return Loc::T("Status.StateHeld");
            case EMcState::WaitOperator: return Loc::T("Status.StateWaitOperator");
            case EMcState::Alarm:        return Loc::T("Status.StateAlarm");
            case EMcState::Emg:          return Loc::T("Status.StateEmg");
            default:                     return ToString(s);
        }
    }
}
