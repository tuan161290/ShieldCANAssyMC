# Controller layer — design rules for the phase-2 implementers (2026-10-18)

Headers in `Core\Controller\` are final contracts (written in phase 1). An implementer may ADD private members to the header of the
class it implements, but must not change public signatures or other classes' headers without telling the lead.

- **Ownership:** McControl::Impl holds McStatus and MotionConnect as statics, plus shared_ptrs to Io, Vision, Safety, Func and the four
  routines (Origin, Auto, Calib, Dry); follow the Slit `State` + `Impl::DoXxx` pattern (see the Slit C++ McControl.cpp in
  `<scratch>\ref_slit\ShieldCanSlitAssy.WinUI\Core\Controller\McControl.cpp`). Routines hold references, so Func must outlive them.
- **Threads:** one control-loop jthread (AboveNormal), one watchdog jthread (1000 ms first delay, then every 200 ms), one routine
  jthread for Origin/Init/Auto/Dry.
- **Cancellation:** Origin/Init get `Safety()->AbortToken()`. Auto/Dry get a per-cycle
  `shared_ptr<base::LinkedStopSource>(Safety()->AbortToken())`; `EndCycle` cancels it. Vision-page sequences use `ManualToken()`, a
  LinkedStopSource replaced when it is cancelled or the safety token changes (compare stop_tokens with `==`).
- **Routine `Run` contract:** blocking, never throws. Catch `base::OperationCanceled` and `std::exception` (raise `RoutineCrashed` as an
  event). McFunction Move* and FakeVision `CallFlow` can throw OperationCanceled when ct is cancelled.
- **Cycle-thread flag:** `Run` calls `McFunction::MarkCycleFlow()` first. Any `base::WhenAll` jobs started from the cycle thread wrap
  their body in `McFunction::CycleFlowScope(inCycle)`.
- **Locking:** Routine_Auto: `m_cacheMutex` (recursive) for `m_assyScan`, `m_assyScannedCells`, `m_scanOneCell`, `m_held`;
  `m_gateMutex` for `m_calib`; `m_placedMutex` for `m_placed`, `m_lastPlaceMove`; `m_waitingMutex` for `m_waiting`.
  Routine_Calib: `m_stateMutex` guards all cross-button state. Never hold these locks while moving axes or calling a Flow.
- **Data shapes:** `Model()->AssyTray`, `InputTray`, `InputTray2` and `RunData()->InputTray`, `AssyTray`, `Nozzle1`, `Nozzle2` are
  shared_ptr. `TrayLayout::Rows()/Cols()` are functions; `TrayState::Rows/Cols` are fields, `CellCount()/HasSelection()` functions.
  `ModelData::InputCell` returns `shared_ptr<TrayLayout>`; `HandEyeJigProblem` returns a string (empty = OK). `AxisConfig::MoveSpeedLow()`
  is a function; `SystemData::GetAxis` non-const; `AlarmData::Message()` a function; VisionCalib dates are TimePoint (`TimePoint{}` = MinValue).
- Routine_Auto statics (TrayMissingAt, Nozzle1TakesFirstCell, Nozzle1TakesFirstPickCell, FirstToScan) live in `Routine_AutoStatics.cpp` — do not
  redefine. `Routine_Auto::RunData()` is the C# `Run` property (renamed: clashes with `Run(ct)`).
- Vision: `VisionConnect::SetRealDeviceFactory(...)` is registered by the App at startup; McControl::Init picks
  `SimulationMode ? FakeVision : VisionConnect` like C#. In simulation the real VisionMaster is never touched.
- Routine_Calib: Align and top-down sequences call `ClearCalibPoints(proc)` first and stop when it returns a non-empty string.
