# ShieldCanAssy WinUI 3 — UI specification

Date: 2026-10-18. Target: `ShieldCanAssy.WinUI\App\` (C++/WinRT, Windows App SDK 1.8).
Behaviour source: `ShieldCanAssy.Wpf\` (Views, ViewModels, Themes, Services) and `ShieldCanAssy.Core\Core.Lang\StringsEn.resx`.
Infrastructure source: Slit app `ref_slit\ShieldCanSlitAssy.WinUI\App\` (`Ui\Page.h`, `UiUtil`, `Dialogs`, `VmPane`, `JogControl`,
`AlarmTable`, `ManualPageParts`, `SettingPageUi`, `StatusColors`, `Styles.xaml`, `AppBackdrop`).

---

## 0. Conventions

### 0.1 Runtime model (fixed by the Slit infrastructure)

- **No data binding.** Every control that shows Core state has an `x:Name`. `MainWindow` runs a 100 ms `DispatcherQueueTimer` and
  calls `OnTick()` of the **visible** page only (`ui::IPage`). `OnTick` reads cheap Core getters and writes a property only when the
  value changed (`ui::SetText`, `ui::Show`, `ui::SetTint`).
- **Commands.** `McControl.Request*` (flag setters) and pure UI state are called directly on the UI thread. Every blocking Core call
  (C# `…Async` → blocking C++, file save/load, vision SDK) goes through `ui::RunBackground` / `RunBackgroundThenUi` or
  `co_await ui::settingpage::Background{…}`. Short ordered I/O (jog, `Io.SetDo`, `Func.SetVacuum/SetBlow`) goes through
  `ui::PostMotionCommand`.
- **Page contract.** `OnShown(bool)` = WPF `Loaded/Unloaded` (WPF `TabControl` re-loads a view each time it is shown);
  `OnModalChanged(bool)` disables VisionMaster panes; `ui::StopActiveJogs()` on page switch and modal open.
- **Core names.** Cited with the C# names (the C++ port keeps them: `McControl.RequestOrigin` → `McControl::RequestOrigin()`,
  property → getter). "VM:" = the WPF ViewModel member being replaced.
- **Busy.** Where the WPF ViewModel has a `Busy` flag (Teaching, Vision Edit), the page keeps a `bool m_busy` set around the
  background job; controls listed as "off while Busy" get `IsEnabled=false` (WinUI `Border` has no `IsEnabled` — wrap groups in a
  `ContentControl` or disable each control).

### 0.2 Text policy (PORTING.md §3)

- Industrial English, short. **Button labels ≤ 2 words** (a step number shown as a badge does not count).
- Static text is literal in XAML. Formatted text uses `Loc::T/F` with the **same resx key** (shown as `Key`). Tooltips: "tip `Key`"
  means the resx English text verbatim.
- Card headers stay UPPERCASE as in resx (`Main.Operation` = "OPERATION").
- Logs written by the UI (e.g. VM bind diagnostics, dialog records) are English.

### 0.3 Interaction guards (all kept from WPF)

| Mark | Meaning | WinUI implementation |
|---|---|---|
| **[2×]** | Double-click/double-tap required; a single tap does nothing | handler on `DoubleTapped` only, no `Click` (Slit pattern, `OnInitDoubleTapped`). Helper `ui::AttachDoubleTap(btn, fn)` |
| **[Hold]** | Hold to run, release = stop | `ui::AttachJog(btn, axis, dir)` (press → `McFunction.Jog(axis,dir)`, release/exit/capture lost/cancel → `Jog(axis,0)`) |
| **[Hold3s]** | Brake-off: short press = normal, hold 3 s = emergency latch | `ui::AttachHold` with pointer capture (§9.6) |
| **[Confirm]** | Yes/No dialog before acting | `co_await ui::Dialogs::ConfirmAsync(text)` on UI thread, then `RunBackground` |
| **[Busy-off]** | Disabled while page Busy | see 0.1 |

### 0.4 State colours on buttons

`ui::SetTint(button, Tint, bool glow)` — **extend the Slit helper with a `glow` flag**: WPF (2026-10-13) glows only states that need
attention (running, need Init, alarm, SVO on, IO on). Idle "selected" states are solid colour **without** glow
(mode thumb, STOP while stopped, Origin done on Main, Auto-nozzle choice, Dry run). Tables below say "+glow" when glow is on.
Tints: Green `#198754`, Red `#DC3545`, Caution `#D35400`, Blue `#0078D4`, Accent (selection, never glow).

---

## 1. Shell (`MainWindow`)

### 1.1 Decisions

| Topic | Decision | Why |
|---|---|---|
| Navigation | **`SelectorBar`** (WinAppSDK ≥1.5) with 6 `SelectorBarItem`s, RESET to its right | Real Fluent control (animated pill indicator, keyboard/UIA) with the exact look the Slit `NavTabStyle` imitates ("kiểu SelectorBar"). Light: no Frame/back/settings/overflow baggage of `NavigationView`, works with the Visibility-swap page host and `OnShown`. `SelectorBarItem.Child` accepts custom content, so the Alarm item can carry its red fill + glow (same trick as Slit). Items `MinHeight 48`, 15 SemiBold — touch-friendly. `NavigationView` Top is the fallback only if overflow is ever needed. |
| Modal dialogs | **Keep `ui::Dialogs` / `ui::ModalOverlay`** (windowed `Popup`, `ShouldConstrainToRootBounds=false`), restyled to look exactly like `ContentDialog` | VisionMaster ActiveX controls live in owned popup HWNDs **above** the XAML island; a real `ContentDialog` renders inside the island and would be covered on Main and Vision Edit. Also `ContentDialog` allows one open dialog per XamlRoot, but WPF stacks dialogs (`_coveredDialogs`). `ModalChanged` disables VM panes while open. |
| Banner / panel | Alarm banner = real **`InfoBar`** hosted in a windowed `Popup`; alarm detail panel = `ModalOverlay` card | Same reason (must float over VM panes). |
| Backdrop | `AppBackdrop UseAcrylic=True` (DesktopAcrylic Thin, `IsInputActive` always true) | Assembly PC is Windows 10 22H2: Mica unavailable; WPF uses blur + 60 % `#F3F3F3` tint. Fallback: solid `#F3F3F3` (transparency off / RDP). |
| Title bar | `ExtendsContentIntoTitleBar(true)`, `TitleBarHeightOption::Tall`, `SetTitleBar(TitleBarArea)`; system caption buttons | Replaces WPF frameless chrome + FontAwesome min/max/close + WM_GETMINMAXINFO hack; native drag, double-click maximize, snap and minimize animation. Start maximized (`OverlappedPresenter.Maximize()`). Window title "Shield CAN Assy MC — SO1C86-SCAM". |
| Theme | Light only (`Application.RequestedTheme=Light`) | WPF forces light (`DWMWA_USE_IMMERSIVE_DARK_MODE=0`); no dark palette exists. |

### 1.2 Layout

```
┌──────────────────────────────────────────────────────────────────────────────────────────────┐
│[Actro] SHIELD CAN ASSY MC        │ X  │ Y1 │ Y2 │ Z1 │ Z2 │ A1 │ A2 │  [Hoyeon]   ─  □  ✕     │ 64 px, tint = tower lamp
│        Model: XXXX               │0.000│…  │    │    │    │    │    │                         │ 2 px bottom line = tower lamp
├──────────────────────────────────────────────────────────────────────────────────────────────┤
│ Main  Manual  Teaching  Setting  Vision Edit  [⚠ Alarm]                              [RESET] │ 48 px
├──────────────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                              │
│                         page host (one page visible, margin 8)                               │
│                                                                                              │
├──────────────────────────────────────────────────────────────────────────────────────────────┤
│[SIMULATION MODE]  ● Motion  ● I/O  ● Vision                         Data: D:\ShieldCanAssy  │ 28 px
└──────────────────────────────────────────────────────────────────────────────────────────────┘
   floating, non-blocking:  ┌─────────────────────────────────────────────┐
                            │⚠ 3 active ALM                     [Details] ✕│  centre of window
                            │  E1105 Light curtain cut …                   │
                            └─────────────────────────────────────────────┘
```

### 1.3 Elements

| ID | WPF (binding) | WinUI control · x:Name | Label / text | Core | Rules / states |
|---|---|---|---|---|---|
| SH-1 | Header `Border` bg `TowerLampTintBrush`, border `TowerLampBrush` | `Grid` · `TitleBarArea` | — | `Status.TowerLamp` | Bg = `TowerLampTintColor` (tower colour, alpha `0x1C`); bottom border 2 px = `TowerLampColor` (Green/GreenBlink `#389853`, Red* `#C42B1C`, Yellow* `#9D5D00`, else `#606060`; blink ignored). Caption-button bg/inactive bg transparent so the tint shows through. |
| SH-2 | `Image LogoActro` | `Image` | — | — | H 34, left. |
| SH-3 | `MachineName` | `TextBlock` · `MachineNameText` | — | `SysData.MachineName` | 20 Bold. |
| SH-4 | `Status.ModelName` "Model: {0}" | `TextBlock` · `ModelText` | "Model: " + name | `Status.ModelName` (`Status.ModelNotLoaded` when null) | 12 Secondary. |
| SH-5 | `ItemsControl Motion.AxisList` (7 cells, W 58) | `StackPanel` · `AxisPanel` built in code (Slit `BuildAxisCells`) | axis name / "0.000" | `Motion.AxisList[i].CurrentPos` | Order `AxisId.All` (X Y1 Y2 Z1 Z2 A1 A2). Cell W 58 fixed; name 10 Secondary, value 12 SemiBold centred; `Viewbox StretchDirection=DownOnly` so narrow windows shrink instead of clipping. |
| SH-6 | Language button (globe + "EN/VI") → `ToggleLanguageCommand` | **dropped** | — | (`Loc.SetLanguage`, `SysData.Language`) | UI is English-only (PORTING §3). See open question Q1. |
| SH-7 | `Image LogoHoyeon` | `Image` | — | — | H 44, left of caption buttons. |
| SH-8 | min/max/close (FontAwesome) | system caption buttons | — | — | Close → normal app close path (`OnClosed` → `Shutdown`). |
| SH-9 | Nav buttons `ShowMainCommand…` + `Tag=IsMain…` | `SelectorBar` · `NavBar`; items `NavMain`, `NavManual`, `NavTeaching`, `NavSetting`, `NavVisionEdit`, `NavAlarm` | Main · Manual · Teaching · Setting · Vision Edit · Alarm | `ShellSection` 0–5 | `SelectionChanged` → `ShowSection` (StopActiveJogs, old `OnShown(false)`, new `OnShown(true)` + `OnTick()`); ignore programmatic changes. Start on Main. |
| SH-10 | Alarm nav: outer `Border` red + `RedGlowEffect` when `HasActiveAlarms`, text white | `NavAlarm.Child` = `Grid{ AlarmTabGlow, AlarmTabFill(CornerRadius 5), StackPanel{FontIcon E7BA, "Alarm"} }` | ⚠ Alarm | `AlarmManager.ActiveAlarms.Count>0` | Alarm: fill `RedOn`, glow `#FF4D5E`, icon+text white. No alarm: plain item (icon always shown). |
| SH-11 | RESET (`ResetCommand`) right of nav | `Button` · `ShellResetButton` | RESET | `McControl.RequestReset` | Padding 16,8, SemiBold. No alarm: Caution solid + glow. Alarm: Red solid + glow. |
| SH-12 | Page `TabControl SelectedIndex=CurrentSectionIndex` | `Grid` · `PageHost` with `PageMain…PageAlarm` (Visibility swap) | — | — | Margin 8. Only visible page ticks. |
| SH-13 | Sim badge (`Status.Simulation`) | `Border` · `SimBadge` | SIMULATION MODE | `Status.Simulation` | Bg `#FFC800`, text `#5A3D00` Bold 11, radius 4. |
| SH-14 | 3 status ellipses | `Ellipse` · `MotionLamp`, `IoLamp`, `VisionLamp` + labels "Motion" "I/O" "Vision" | — | `Status.MotionConnected/IoConnected/VisionConnected` | 12 px, stroke `#40000000`; on `#0F7B0F`, off `#5C5C5C` (neutral, never red). |
| SH-15 | `Shell.DataPrefix` + `AppPaths.Root` | `TextBlock` · `DataRootText` | "Data: " + path | `AppPaths.Root` | 11 Secondary, right, trimmed + tooltip full path. |
| SH-16 | Alarm banner (`ShowAlarmBanner`) | windowed `Popup` · `m_bannerPopup` containing `InfoBar` · `AlarmBanner` | Title `Loc::F("Shell.ActiveAlarmCount", n)` ("3 active ALM"); Message `BannerAlarm.Display` | `AlarmManager.AlarmRaised` (level ≥ Error only), `ActiveAlarms.Count` | Shown on a **new** Error/Fatal alarm; auto-hide after 10 s (restart timer on each new one); hidden when count = 0. Non-blocking, centred, MinWidth 440, MaxWidth 900. Severity Error but **solid** bg `#F0C42B1C`, white text, 1 px white border, shadow (lightweight-style overrides). Message trimmed one line. |
| SH-17 | Banner "Details" → `ToggleAlarmPanelCommand` | `InfoBar.ActionButton` · `BannerDetailsButton` | Details | — | Opens SH-19. |
| SH-18 | Banner "Hide" → `HideAlarmBannerCommand` | `InfoBar` close button (✕) | tip `Shell.HideBannerTip` | — | Stop timer, hide now; alarm stays; reappears on next new Error/Fatal. |
| SH-19 | Alarm detail overlay (`AlarmPanelOpen`) | `ui::ModalOverlay` · `m_alarmPanel` (ContentDialog look, W 900, MaxH 560) | Title "Active alarms" | `AlarmManager.ActiveAlarms` | Smoke `#60000000`. Body `ui::AlarmTable(Active)` (§9.8). Rebuild rows only when `AlarmManager` version changes. |
| SH-20 | Panel footer `Status.Message` | `TextBlock` · `m_alarmPanelMessage` | — | `Status.Message` | Critical colour, wrap, MaxWidth 480. |
| SH-21 | Panel RESET | `Button` (Primary) | RESET | `McControl.RequestReset` | 120×40. |
| SH-22 | Panel Close | `Button` | Close | — | 100×40; closes overlay. |
| SH-23 | Auto refused (`McControl.AutoModeRefused`) | `ui::Dialogs::Warn(why, "Cannot enter AUTO")` | `Shell.AutoRefusedTitle` | `McControl.AutoModeRefused` event | Posted to UI thread, not blocking the control loop. |

### 1.4 Dialogs (replaces `DialogHost`)

| ID | WPF | WinUI | Rules |
|---|---|---|---|
| DL-1 | Overlay card 440 px, glyph + title, message, buttons | `ui::Dialogs` card in `ModalOverlay`, ContentDialog look: radius 8, title 20 SemiBold, body 14 wrap, footer band `#F3F3F3` with buttons right | W 440, MinH 140, smoke `#60000000`. |
| DL-2 | Kind glyph/colour (`DialogKindGlyph/Brush`) | `FontIcon`: Info `E946` `#0F7B0F`, Warn `E7BA` `#9D5D00`, Error `EA39` `#C42B1C`, Confirm `E9CE` `#0078D4` | Default titles Info / Warning / Error / Confirm (`Dialog.*`). |
| DL-3 | Buttons Yes (Primary) + No / OK (Primary) | Confirm: **Yes** (Primary) **No**; others: **OK** (Primary) | 96×40. |
| DL-4 | Stacking (`_coveredDialogs`) | newest on top; closing reveals the covered one with its buttons intact | Required — a covered caller must not hang (Vision Edit "busy forever" bug). |
| DL-5 | `Record()` → `LogWorker.RecordLog(…, "UI")` | same in English: `Dialog "<title>": line1 | line2` (+ ` → Yes/No` after a confirm) | Error→Error, Warn→Warning, else Info. Confirm logged after answer. |
| DL-6 | Blocking `Confirm` (DispatcherFrame) | `ConfirmAsync` (co_await on UI) or `ConfirmBlocking` from a background thread | Info/Warn/Error are fire-and-forget: WPF kept `Busy` until OK — harmless difference. |

---

## 2. Main (`MainPage`) — VM: `MainViewModel`, `TrayGridViewModel`, `MainView.xaml.cs`

```
┌ 250 ───────────────┐┌ * ──────────────────────────────────────────────────────┐┌ 290 (collapsible) ┐
│ OPERATION          ││ SHIELD CAN TRAY  [Clear(3)] 4×5—cell 7/20 ││ SUB1-HOUSING ASSY 5×8 — cell 12/40   ││ LOG            [›]│
│ ( AUTO | MANUAL )  ││ Tray 1                                    ││ ┌──┬──┬──┬──┬──┬──┬──┬──┐             ││ 10:02:11 Auto     │
│ [ RUN ] [ STOP ]   ││ ┌──┬──┬──┬──┬──┐                          ││ │1-1│1-2│…                │             ││ Cell 3 placed …   │
│ ─────────────────  ││ │1-1│1-2│…     │                          ││ │   │   │                │             ││ 10:02:09 Vision   │
│ [ Origin   ][Init] ││ Tray 2 · not taught (dim)                 ││ └──┴──┴──┴──┴──┴──┴──┴──┘             ││ …                 │
│ [ RESET    ][Mute] ││ ┌──┬──┬──┬──┬──┐                          ││                                       ││                   │
│ ▌Nozzle #1 only…   ││ [Clear Tray]     │ OK 12  NG 1  Total 13  ││ [Clear Tray]     │ OK 40 NG 2 Total 42 ││                   │
│ ▌DRY RUN — NO PROD ││┌BOTTOM CAM — #1 ☰─┐┌BOTTOM CAM — #2 ☰─┐┌TOP CAM — LOCATETOP ☰┐                     ││                   │
│ ▌Trays in: press…  │││(3,4)  X   Y   θ   ││ …                 ││ …                   │                     ││                   │
│ ▌<Status.Message>  │││Ofs   …            ││                   ││                     │                     ││                   │
│ PRODUCTION [Clear] │││Tgt   …            ││                   ││                     │                     ││                   │
│ OK   NG   Total    │││┌────────────────┐││┌────────────────┐ ││┌──────────────────┐ │                     ││                   │
│ 120  3    123      ││││ VM render popup│││| VM render popup| │││ VM render popup  │ │                     ││                   │
│ NG rate  NG streak ││││ / last image   │││|                | │││                  │ │                     ││                   │
│ Cycle Time  01:23.4│││└────────────────┘││└────────────────┘ ││└──────────────────┘ │                     ││                   │
│ Last Cycle  …      │└───────────────────┘└───────────────────┘└─────────────────────┘                     ││                   │
│ Tack Time / Last…  │                                                                                     ││                   │
│ Est. output …      │   (log hidden → 24×56 [‹] tab at right edge, centre column widens)                  │└───────────────────┘
│ Step: S10 · …      │
│ Last comp: …       │
└────────────────────┘
```

Left column: `ScrollViewer` (gutter inside it — WinUI scroll bars overlay). Centre: tray row (Auto height, own `ScrollViewer`) over the
CAM row (fills). Right: log card W 290 collapsible.

### 2.1 OPERATION card

| ID | WPF (binding) | WinUI control · x:Name | Label | Core | Rules / states |
|---|---|---|---|---|---|
| MA-1 | Mode switch `ToggleModeCommand`, thumb `Tag=Status.Mode`, `PlaceModeThumb` | custom `Button` (template: track radius 20 + thumb radius 16 + 2 labels) · `ModeSwitch`, `ModeThumb`, `ModeAutoText`, `ModeManualText` | AUTO · MANUAL | `McControl.RequestSetMode(Status.Mode==Auto ? Manual : Auto)` | H 40. Thumb sits under the **real** `Status.Mode` (Auto `GreenOn`, Manual `CautionOn`), label on thumb white, other label Secondary 14 Bold. Slide 150 ms CubicEase-out on mode change (Storyboard on `TranslateTransform.X`), snap on resize. AUTO refused → thumb stays + SH-23. Not a `ToggleSwitch` (would show optimistic state). |
| MA-2 | RUN `ArmCommand` | `Button` · `RunButton` | RUN | `McControl.RequestArm` (Armed only; real run = hardware Start) | H 52, 18 Bold, text always "RUN". Stopped: `StatusOff #5C5C5C` solid, white. `State==Running`: Green +glow. `State==Armed`: Green, **blink** opacity 1 → 0.3 at 1 Hz (0.5 s each, Storyboard `RunBlink`, discrete keyframes), no glow; stop and reset opacity on exit. Tip `Main.RunTip`. |
| MA-3 | STOP `StopCommand`, `IsStopped` | `Button` · `StopButton` | STOP | `McControl.RequestStop` | H 52, 18 Bold. `IsStopped` = State ∉ {Running, Pausing, WaitOperator} → Red solid white, no glow; else default white. |
| MA-4 | Origin `OriginCommand` | `Button` · `OriginButton` | Origin | `McControl.RequestOrigin` | H 40 SemiBold, col 2*. `Status.OrgOk` → Blue solid, no glow. Single tap (as WPF). |
| MA-5 | Init `InitCommand` + `RequireDoubleClick` | `Button` · `InitButton` | Init | `McControl.RequestInit` | **[2×]**. Col 1*, right of Origin (messages say "press Init (next to Origin)"). `RunData.UnsafePause` → Caution +glow. Tip `Main.InitTip`. |
| MA-6 | RESET `ResetCommand`, `HasActiveAlarms` | `Button` · `ResetButton` | RESET | `McControl.RequestReset` | Active alarms → Red +glow; else default. |
| MA-7 | Mute `MuteCommand` | `Button` · `MuteButton` | Mute | `McControl.RequestMuteBuzzer` | — |
| MA-8 | Only-nozzle strip (`OnlyNozzle≠0`) | `InfoBar` · `OnlyNozzleBar` (not closable, no icon) | `Loc::F("Main.OnlyNozzle", n)` | `SysData.OnlyNozzle` | Visible n≠0. **Solid** `CautionOn #D35400`, white SemiBold 12. |
| MA-9 | Dry-run strip (`Status.DryRun`) | `InfoBar` · `DryRunBar` | Title "DRY RUN — NO PRODUCT"; message `DryRunText` | `Status.DryRun`; `McControl.DryRoutine` (Strokes, Sweeps, Laps, Elapsed) | Solid `Caution #9D5D00`, white. Message = `Main.DryRunIdle` if routine null or (Strokes=0 and Elapsed<1 s) else `Main.DryRunProgress`. |
| MA-10 | Wait-tray strip (`WaitingOperator`) | `InfoBar` · `WaitTrayBar` (Warning) | "Trays in: press any Start button." | `Status.State==WaitOperator` | Reminder only, not clickable. Bold. |
| MA-11 | Message strip (`Status.Message`) | `InfoBar` · `MessageBar` (Warning) | `Status.Message` | `Status.Message` | Visible when non-blank; 12, wrap. |

### 2.2 PRODUCTION card

| ID | WPF | WinUI · x:Name | Label | Core | Rules |
|---|---|---|---|---|---|
| MA-12 | Header Clear `ResetCountersCommand` | `Button` · `ClearCountersButton` | Clear | `McDataManager.RunData.ResetCounters()` | In header row, H 36. No confirm (as WPF). |
| MA-13 | OK / NG / Total | `TextBlock` · `CountOkText`, `CountNgText`, `CountTotalText` | captions OK · NG · Total | `RunData.CountOk / CountNg / CountTotal` | 26 Bold; OK `Success`, NG `Critical`. |
| MA-14 | NG rate | `NgRateText` | caption "NG rate" | `RunData.NgRatePercent` → "0.00 %" | 16 SemiBold. |
| MA-15 | NG streak | `NgStreakText` | caption "NG streak" | `RunData.ConsecutiveNg` | >0 Critical else Primary. Tip `Main.NgStreakTip`. |
| MA-16 | CT table (4 rows, cell borders) | `Grid` · `CycleTimeText`, `LastCycleTimeText`, `TackTimeText`, `LastTackTimeText` | Cycle Time · Last Cycle Time · Tack Time · Last Tack Time | `RunData.AssyTrayStartedAt`, `AssyTrayDone`, `AssyTrayDoneSec/Products/PerProductSec`, `LastAssyTrayTimeSec/Products/PerProductSec`, `AssyTray.Cells` | Every tick (100 ms). Running tray: elapsed = now − start; Tack = elapsed ÷ OK cells (— if 0). Tray done: hold `AssyTrayDone*`. Clock `mm:ss.f` (`h:mm:ss.f` ≥ 1 h, floor 0.1 s); seconds "0.00 s"; "—" when none. Value 15 SemiBold, label 11 Secondary. Tips `Main.CtTrayCurrentTip`, `CtTrayPreviousTip`, `CtAvgCurrentTip`, `CtAvgPreviousTip` on value cells. |
| MA-17 | UPH | `UphText` | `Main.Uph` / `Main.UphNone` | avg = done-tray per-product → last tray → current | 12. Tip `Main.UphTip`. |
| MA-18 | Step | `AutoStepText` | "Step: " + text | `Status.AutoStepText` | 12 Secondary. |
| MA-19 | Last compensation | `LastCompText` | `Loc::F("Main.LastCompensation", …)` | `Status.LastDeltaTheta`, `Status.LastPlaceError` | 11 Secondary. Tip `Main.LastCompensationTip`. |

### 2.3 Tray cards (2× `ui::TrayCard`, §9.1)

| ID | WPF (`TrayGridTemplate`) | WinUI · x:Name (Input / Assy) | Label | Core | Rules |
|---|---|---|---|---|---|
| MA-20 | Title | `InputTitleText` / `AssyTitleText` | SHIELD CAN TRAY / SUB1-HOUSING ASSY | — | Section header. |
| MA-21 | Clear selection `ClearSelectionCommand` | `Button` · `InputClearSelButton` / `AssyClearSelButton` | `Loc::F("Tray.ClearSelection", n)` → "Clear (3)" | `McControl.ClearCellSelection(input)` | Visible when `TrayState.Selected` non-empty. H 28, 12 pt, negative vertical margin — **must not change header height**. Tip `Tray.ClearSelectionTip`. |
| MA-22 | Summary | `InputSummaryText` / `AssySummaryText` | `Tray.SummaryCurrent` / `SummaryDone`; Input card: `Tray.SummaryOfTray` of the active tray | `TrayState.Rows/Cols/RowsPerTray/CellsPerTray/CurrentIndex/CellCount` | 12 Secondary, right. |
| MA-23 | Group caption (Input only, 2 slots) | per group `TextBlock` | "Tray 1" / "Tray 2 · not taught" (`Tray.N`, `Tray.SummaryOfTray`+`Tray.NotTaught`) | group < `TrayState.Trays` | Active tray (holds `CurrentIndex`): SemiBold Primary; else 11 Secondary. Untaught group grid opacity 0.45. Groups stacked, equal height (Assy card one group, same total height). |
| MA-24 | Cells (`UniformGrid Rows×Cols`) | `TrayMap` grid built in code | "r-c" (1-based, 12) | `TrayState.Cells[IndexOf(r0+r,c)]`, `CurrentIndex`, `AutoRoutine.AssyScannedCells`, `Selected` | Geometric layout (not visit order; zigzag drawn as the real tray). Fill by look (§10.2). Current: blue glow `#0078D4` blur 9. Selected: 2 px `AccentPressed #005A9E` overlay. Out of scope (selection exists, cell not in it): opacity 0.35. Min 30×34, gap 2, radius 4, elevation stroke. Tap only when `Status.Mode==Auto` → `McControl.ToggleCellSelection(input, index)`, then re-sync. Rebuild only when rows/cols/scan order change. |
| MA-25 | Reload button `ResetCommand` | `Button` · `InputReloadButton` / `AssyReloadButton` | Clear Tray | Input: `McControl.ReloadInputTray()`; Assy: `McControl.ResetAssyTray()` | H 36, left half of footer. No confirm (as WPF). |
| MA-26 | Card counters | `InputOkText`… / `AssyOkText`… | OK · NG · Total | `RunData.InputCountOk/InputCountNg`, `AssyCountOk/AssyCountNg` (Total = sum) | 16 Bold; right half with left divider. |

### 2.4 CAM cards (3× `ui::CamFrame`, §9.5) — order Tool 1, Tool 2, Top; equal width; readout height 70 so images align

| ID | WPF | WinUI · x:Name (prefix `Tool1`/`Tool2`/`Top`) | Label | Core | Rules |
|---|---|---|---|---|---|
| MA-27 | Toolbar toggle (☰, `ToggleRenderToolbar_Click`) | `Button` (FontIcon `E700`) · `…ToolbarButton` | — | VmRenderControl `SetRenderToolbarVisible` (DISPID `0x1A`) | 32×28. Default hidden toolbar. Visible only when the VM pane exists. Tip `Main.RenderToolbarTip`. |
| MA-28 | Title | `TextBlock` | BOTTOM CAM — #1 · BOTTOM CAM — #2 · TOP CAM — LOCATETOP | — | — |
| MA-29 | Note on header | `…NoteText` | `ReadoutCard.Note` | `VisionReadout.Tool1Card/Tool2Card/TopCard` | Visible when `HasRows`; Caution 11, trimmed, tooltip = full note. |
| MA-30 | Captured time | `…ShotText` | `Loc::F("Main.CapturedAt", HH:mm:ss)` | new image detected on `Vision.LastBottomUpImageTool1/Tool2`, `LastTopDownImage` | 11 Tertiary; time = moment a new image object was first seen. |
| MA-31 | Readout table (`ReadoutTemplate`) | `ui::ReadoutTable` · `…Readout` | header: cell + columns; rows: label + values | `ReadoutCard.Cell/Columns/Rows/Note/HasRows/CanToggle/Breakdown`; text `VisionReadout.Tool1/Tool2/Top` | H 70. No rows → note text (wrap). Tooltip on whole table = full readout text or `Main.NotTriggered`. Tool cards: header row tappable when `CanToggle` → toggle Ofs/Tgt ↔ breakdown for **both** tools (not saved), tip `Main.ReadoutToggleTip`. |
| MA-32 | Image + "(no image)" | `Image` · `…Image`, `TextBlock` · `…NoImageText` | (no image) | last bitmap | Convert only when the image object changes. Holder MinH 140, radius 6, stroke. |
| MA-33 | `VmRenderControl` over image | `Border` · `…Host` + `ui::VmPane(Render)` | — | path `VisionOutputNames.RenderPath(Flow…, Render…)`: (FlowAlignBottomTool1, RenderTool1), (FlowAlignBottomTool2, RenderTool2), (FlowLocateTop, RenderTopDown) | On `OnShown(true)`: retry every 1 s (queued VM work, never inside a XAML event) until all 3 bound, only when `McControl.VisionLoadCount>0` and solution exists; key = load count + path (rebind after reload or Setting change). Enable result callbacks of the module (all modules of a Flow). `SetBackground("#F1F1F1")`, re-apply toolbar state after bind. Log the bind diagnostic once per change (English). Hide/show in `OnShown`, disable in `OnModalChanged`. |

### 2.5 LOG card

| ID | WPF | WinUI · x:Name | Label | Core | Rules |
|---|---|---|---|---|---|
| MA-34 | Header + hide (›) `ToggleLog_Click` | `Button` (FontIcon `E76C`) · `LogHideButton` | — | — | 36×32. Tip "Hide log". Animate host width 290→0 + opacity 250 ms (`EnableDependentAnimation`); centre column widens; VM panes re-sync on layout. |
| MA-35 | Show tab (‹) | `Button` (FontIcon `E76B`) · `LogShowButton` | — | — | 24×56 at right edge, vertically centred, visible only when hidden. Tip "Show log". |
| MA-36 | `ListBox LogBox` (newest first, auto-scroll) | `ListView` · `LogList` (SelectionMode None), items built in code | — | `LogWorker.LogList` (newest at index 0) | Insert new rows at 0 only (no full rebuild), keep in view top. Row: 3 px left bar + time `HH:mm:ss` Consolas 11 Tertiary + source 11 Secondary + message 12 wrap. Level colours §10.2. |

---

## 3. Manual (`ManualPage`) — VM: `ManualViewModel`, `AxisRowVm`, `ManualView.xaml.cs`

```
┌ * (scroll) ────────────────────────────────────────────────────────────────────┐┌ 340 ───────────────────┐
│ ▌Motion blocked: <reason>                                                       ││ INPUTS (DI)            │
│ ▌<Status.Message>                                                               ││ ○ DI 0x00  EMG OK      │
│ ALL AXES [STOP][RESET]  [Servo On][Brake Off][Origin]      CAM lamps [Bottom-up][Top-down]│ ● DI 0x01 …    │
├─────────────────────────────────────────────────────────────────────────────────┤│ …                      │
│ Axis Pos.      Limit │ Servo              │Origin│ Jog (hold)│Inching (step)│Move Abs   │Stop││              │
│ Gantry — X, Z1, Z2                                       ● Z at Z_SAFE — needed …  │├────────────────────────┤
│ X   123.456mm ○POT  │[SVO on][RST]       │[Org] │ [−] [+]   │[0.1][−][+]   │[ 50.0][Move]│[Stop]│ OUTPUTS (DO)│
│ Z1   …        ○NOT  │[SVO on][RST][Brake off]│  …                                     ││ ○ DO 0x00  Vac #1      │
│ Tray — Y1, Y2                                                                      ││ …                      │
│ Nozzle rot. — A1, A2  (no POT/NOT lamps)                                           ││                        │
├─────────────────────────────────────────────────────────────────────────────────┤│                        │
│ SIM PANEL — BUTTONS, SAFETY, DRIVER FAULTS (sim only)                              ││                        │
│ [Start][Stop]  [EMG][Curtain][Air][Tray 1][Tray 2]                                 ││                        │
│ Sim driver faults: X [ALM][Offline]  Y1 [ALM][Offline] …                           ││                        │
├─────────────────────────────────────────────────────────────────────────────────┤│                        │
│ HW DIAGNOSTICS  DI 0x0000A0F1 0000… / DO … / Fastech A1 0x… bits                    ││                        │
└─────────────────────────────────────────────────────────────────────────────────┘└────────────────────────┘
```

### 3.1 Command card

| ID | WPF (binding) | WinUI · x:Name | Label | Core | Rules |
|---|---|---|---|---|---|
| MN-1 | Block banner `BlockReason` | `InfoBar` (Warning) · `BlockBar` | Title "Motion blocked"; message reason | `Safety.SafeToMove`, `Safety.BlockReason` | Visible when not safe. |
| MN-2 | Message banner | `InfoBar` (Warning) · `MessageBar` | `Status.Message` | `Status.Message` | Shows refusals of Origin/Move/SVO right where the operator works. |
| MN-3 | (group caption) | `TextBlock` | ALL AXES | — | Caption before the group so buttons stay ≤ 2 words. |
| MN-4 | STOP all `StopAllCommand` | `Button` · `StopAllButton` | STOP | `McControl.RequestStop` | Same as Main STOP. Tip `Manual.StopAllTip`. |
| MN-5 | RESET `ResetCommand` | `Button` · `ResetAllButton` | RESET | `McControl.RequestReset` | Tip `Manual.ResetTip`. |
| MN-6 | SVO ON all `ServoOnAllCommand`, `Tag=AllServoOn` | `Button` · `ServoOnAllButton` | Servo On | for each axis !ServoOn && !Alarm: `Axis.SetServoOnAsync(true)` in parallel (background); alarmed axes → `Status.Message = Loc::F("Manual.ServoOnSkipped", list)` | `Motion.AllServoOn` → Green +glow. Tip `Manual.ServoOnAllTip`. |
| MN-7 | Brake off all `BrakeReleaseAllCommand`, `Tag=AllBrakesReleased` | `Button` · `BrakeAllButton` | Brake Off | `Func.RequestBrakeRelease(Z1)`, `(Z2)` (normal, not emergency) | `Io.GetDo(DoBrakeReleaseZ1) && GetDo(DoBrakeReleaseZ2)` → Green +glow. Tip `Manual.BrakeReleaseAllTip`. |
| MN-8 | Origin `OriginCommand`, `Tag=Status.OrgOk` | `Button` · `OriginAllButton` | Origin | `McControl.RequestOrigin` | `Status.OrgOk` → **Green** +glow (Manual uses green, Main blue — as WPF). Tip `Manual.OriginTip`. |
| MN-9 | "CAM lamps" label | `TextBlock` | CAM lamps | — | Right side of the row. |
| MN-10 | Bottom-up `ToggleCamBtmLampCommand` | `Button` · `CamBtmLampButton` | Bottom-up | `Io.SetDo(DoLampCam1, !Io.GetDo(DoLampCam1))` | Works on real machine and sim. No tint (state visible in DO list, as WPF). Tip `Manual.CamBtmLampTip`. |
| MN-11 | Top-down `ToggleCamTopLampCommand` | `Button` · `CamTopLampButton` | Top-down | `Io.SetDo(DoLampCam2, toggle)` | Tip `Manual.CamTopLampTip`. |

Row layout: `Grid` (left `WrapPanel`-like `VariableSizedWrapGrid`/wrapping `StackPanel` so buttons wrap rather than clip at 1280 px), buttons W 120 H 40.

### 3.2 Axis table card (`ui::ManualAxisRow` ×7, §9.2)

Header row (widths must equal row cell widths): Axis 30 · Pos. 84 · Limit 40 · Servo 202 · Origin 64 · Jog (hold) 93 · Inching (step) 132 ·
Move Abs 137 · Stop 62 (`Manual.Col*`, "Servo"). Sections: "Gantry — X, Z1, Z2" (with MN-12 on the right), "Tray — Y1, Y2",
"Nozzle rot. — A1, A2" (`Manual.SectionRotate`).

| ID | WPF (`AxisRowTemplate`) | WinUI · x:Name (in row object) | Label | Core | Rules |
|---|---|---|---|---|---|
| MN-12 | Z-safe lamp `ZSafeOk` | `Ellipse` · `ZSafeLamp` + text | Z at Z_SAFE — needed for X, Y1, Y2 to move | `Motion.ZSafeOk` | 12 px, status green / neutral off. |
| MN-13 | Name | `TextBlock` | X, Z1 … | `Axis.Name` | 14 Bold. |
| MN-14 | Position + unit | `TextBlock` with 2 `Run`s · `m_posRun`, `m_unitRun` | "123.456" "mm" | `Axis.CurrentPos` (`0.000`), `Axis.Config.Unit` | Consolas 15; unit 10 Secondary. |
| MN-15 | POT / NOT lamps | `ui::ManualLamp` ×2 + "POT"/"NOT" 9 pt | — | `Axis.FLS`, `Axis.RLS` | Hidden (width kept) for A1/A2. Off LightGray, on Critical + red glow. |
| MN-16 | SVO toggle `ServoToggleCommand` | `Button` · `m_svoButton` W 66 | "SVO off" / "SVO on" | ServoOn → `Axis.SetServoOnAsync(false)`; else Alarm → `Status.Message = Loc::F("Manual.AxisAlarmNoServo", name)`; else `SetServoOnAsync(true)` | Text + colour follow **real** `Axis.ServoOn` (driver-confirmed): on = Green +glow. Background job; row busy-lock. |
| MN-17 | RST `ResetAlarmCommand` | `Button` · `m_rstButton` W 48 | RST | `Axis.ResetAlarmAsync()` | `Axis.Alarm` → Red +glow. Tip `Manual.RstTip`. |
| MN-18 | Brake off (Z1/Z2) | `Button` · `m_brakeButton` W 76 | Brake Off | short press → `Func.RequestBrakeRelease(name)`; hold ≥ 3 s → `Func.SetEmergencyBrakeRelease(name, true)` + `Status.Message=Manual.EmergencyBrakeReleased`; press while latched → `SetEmergencyBrakeRelease(name,false)` at once + `Manual.EmergencyBrakeLocked` | **[Hold3s]** (§9.6). Visible Z1/Z2 only (last in group so columns align). Colours, precedence Red > Caution > Green: latched = Red +glow; holding < 3 s = Caution +glow; `Io.GetDo(DoBrakeReleaseZ1/Z2)` = Green +glow. Tip `Manual.BrakeReleaseTip` (important: emergency behaviour). |
| MN-19 | Origin `HomeCommand` | `Button` · `m_homeButton` W 58 | Origin | `Func.CanMoveAutonomous(name, out reason)` else `Status.Message=Loc::F("Manual.HomeRefused", name, reason)`; then `Motion.HomeAsync(axis, Safety.AbortToken)` | `Axis.OrgOk` → Green +glow. Single tap (as WPF). |
| MN-20 | Jog − / + | `Button` ×2 W 42 | − · + | `Func.Jog(name, −1/+1/0)` | **[Hold]**. Tips `Manual.JogMinusTip`, `JogPlusTip`. Never busy-locked. |
| MN-21 | Step box `StepSize` | `NumberBox` · `m_stepBox` W 48 (spin hidden) | — | row field | Default 1.0 (unit "deg") else 0.1. Commit on LostFocus/Enter (fixes WPF "0." issue by design). Tip `Manual.StepSizeTip`. |
| MN-22 | Inching − / + | `Button` ×2 W 38 | − · + | `Func.MoveIncAsync(name, ±StepSize, Config.MoveSpeed, Safety.AbortToken)` | Background; tips `Manual.IncMinusTip/IncPlusTip`. |
| MN-23 | Target box `TargetPos` | `NumberBox` · `m_targetBox` W 74 | — | row field | Default `Config.StandbyPos`. Tip `Manual.TargetPosTip`. |
| MN-24 | Move `MoveCommand` | `Button` · `m_moveButton` W 58 | Move | `CanMoveAutonomous` else `Manual.MoveRefused`; `Func.MoveAsync(name, target, Config.MoveSpeed, Safety.AbortToken)` | Tip `Manual.MoveTip`. |
| MN-25 | Stop `StopCommand` | `Button` W 56 | Stop | `Axis.Stop()` | Never locked. |

Row busy-lock (Slit `ManualAxisRow::RunBlocking`): while a long command of that row runs, SVO/RST/Origin/Inching/Move are disabled;
Jog/Stop/Brake are not.

### 3.3 SIM PANEL (visible only when `McControl.Io` is `FakeIo`)

| ID | WPF | WinUI · x:Name | Label | Core | Rules |
|---|---|---|---|---|---|
| MN-26 | Note | `TextBlock` | `Manual.SimPanelNote` | — | 12 Secondary. Card header `Manual.SimPanel`. |
| MN-27 | Start `SimPressStartCommand` | `Button` · `SimStartButton` | Start | `FakeIo.SimPress(IoMap.BtnGoB1)` | Tip `Manual.SimStartTip`. |
| MN-28 | Press STOP | `SimStopButton` | Stop | `SimPress(IoMap.BtnStop)` | — |
| MN-29 | EMG | `SimEmgButton` | EMG | `SimToggle(IoMap.EmgOk)` | Gap 16 before (buttons vs toggles). |
| MN-30 | Cut/clear curtain | `SimCurtainButton` | Curtain | `SimToggle(IoMap.LightCurtainOk)` | Tip `Manual.SimLightCurtainTip`. |
| MN-31 | Air pressure | `SimAirButton` | Air | `SimToggle(IoMap.AirOk)` | — |
| MN-32 | In-tray 1 / 2 | `SimTray1Button`, `SimTray2Button` | Tray 1 · Tray 2 | `SimToggle(IoMap.TrayIn1Present / TrayIn2Present)` | — |
| MN-33 | Driver faults list (`Axes`, `IsSimulated`) | wrap panel built in code | header `Manual.SimDriverFaults`; per axis name + ALM + Offline | `FakeAxis.SimDriverAlarm` toggle; `FakeAxis.SimDisconnected` toggle | Only simulated axes. Tips `Manual.SimAlarmTip`, `SimDisconnectTip`. No tint (as WPF). |

### 3.4 HW DIAGNOSTICS card

| ID | WPF | WinUI · x:Name | Text | Core | Rules |
|---|---|---|---|---|---|
| MN-34 | Note | `TextBlock` | `Manual.DiagnosticsNote` | — | 11 Secondary. |
| MN-35 | `RawDiText` / `RawDoText` | `TextBlock` · `RawDiText`, `RawDoText` | "DI  0x%08X   bits" (bit 31 left, space every 8) | `Io.GetDi/GetDo(0…31)` | Consolas 12, selectable. |
| MN-36 | `FastechStatus` lines | `StackPanel` · `FastechStatusPanel` | "{Name}  0x{RawStatus:X8}  {RawStatusText}" | each `FastechAxis` | Consolas 12, wrap; rows added/removed to match. |

### 3.5 I/O lists (right column, 2 equal rows)

| ID | WPF | WinUI · x:Name | Text | Core | Rules |
|---|---|---|---|---|---|
| MN-37 | INPUTS (DI) `DiLamps` | card + `ScrollViewer` · `DiPanel` (32 `ui::IoLampRow`) | "DI 0x00  Name" (Consolas 11) | `Io.GetDi(bit)`, names `IoMap.DiNames` | 11 px lamp: off LightGray, on Critical + red glow (raw bit — many NC bits are ON normally). |
| MN-38 | OUTPUTS (DO) `DoLamps` | `DoPanel` | "DO 0x0B  Name" | `Io.GetDo(bit)`, `IoMap.DoNames` | Same. |

---

## 4. Teaching (`TeachingPage`) — VM: `TeachingViewModel`, `TeachingView.xaml.cs`

```
┌ 440 (scroll) ───────────────────────┐┌ * ─────────────────────────────────────────────────────────────┐
│ MODEL     In use: MODEL_A           ││ CELL COORDS                                          (ring)    │
│ ┌ model list (ListView) ─────────┐  ││ ┌──┬──┬──┬──┬──┐                                               │
│ └────────────────────────────────┘  ││ │ 1│ 2│ 3│ 4│ 5│  ← visit order; green bg = cell ofs; • = trim │
│ [Load][Save][Delete] [New][Copy] (CommandBar)                        ││ │10│ 9│ 8│ 7│ 6│  selected = accent border           │
│ [ new model name            ]       ││ └──┴──┴──┴──┴──┘                                               │
│ SELECT TRAY  ‹Input 1|Input 2|Assy› ││ ─────────────────────────────────────────────────────────────── │
│ Input tray 1   (shared-size note)   ││ (2, 3)  Order #8  X=… Y=… Offset=…/…   [Move][Z1 Pick][Move CAM][Set Offset][Clear] │
│ Select Tool ‹Tool 1|Tool 2›         ││ Top CAM, cell (2,3): X=… Y2=…  (Assy × Tool 2 only)            │
│ Scan order (•)Sequential ( )Zig zag ││ Common ofs: X[ ] Y[ ] Angle[ ] mm/mm/°            [Apply][Clear]│ Assy only
│ [traversal preview 2×4]             ││ Cell ofs:   X[ ] Y[ ] Angle[ ]                    [Apply][Clear]│
│ TOP CAM REF (Assy only) [Teach][Go Ref]                              │├────────────────────────────────────────────────────────────────┤
│ 3-POINT TEACH                       ││ JOG & PNEUMATICS                                         [ Save ]│
│ [P1]→Cols[5]→[P2]→Rows[4]→[P3][Apply]││ X  123.456 [−][+] [Go T1][Go T2]   Y1 … [−][+]                  │
│ Verify (optional) X Y [Teach]       ││ Z1 …       [−][+] [Origin][Go Pick][Go Place]  Y2 … [−][+]      │
│ PICK ANGLE (Input only) A1 [Teach A1]│ Z2 …       [−][+] [Origin][Go Pick][Go Place]  Jog speed ‹Slow|Fast›│
│ PICK/PLACE HEIGHT Z1 [Teach Z1] …   ││ [Vac #1][Vac #2][Blow #1][Blow #2]  [Btm Light][Top Light]      │
│ BOTTOM CAM POS. [Teach #1][Teach #2]││                                                                 │
│ JIG (HAND-EYE) [Teach][Go] ×3       │└────────────────────────────────────────────────────────────────┘
└─────────────────────────────────────┘
```

Common rules: every **Teach** button reads encoders, shows **[Confirm]** `Loc::F("Teach.ConfirmWrite", what, old, new)` (new value read
**before** the dialog), writes only on Yes; not blocked when Origin not done but logs a Warning ("Teaching: <what> written WITHOUT
Origin — operator-verified") when `!Status.OrgOk`. Grid/anchor/align/offset/trim writes call `McControl.InvalidateVisionCache()`
(pick angle and jig points do **not**). Teach writes go to the open model in memory; **Save** persists.

### 4.1 MODEL card

| ID | WPF | WinUI · x:Name | Label | Core | Rules |
|---|---|---|---|---|---|
| TE-1 | "In use: {CurrentModelName}" | `TextBlock` · `CurrentModelText` | "In use: " + name | `McDataManager.Model.Name` | Name Bold. |
| TE-2 | `ListBox ModelNames`, `SelectedModelName` | `ListView` · `ModelList` (Single, H 120) | — | `McDataManager.ListModels()` | Refresh after New/Copy/Delete/Save; keep selection, else first. |
| TE-3 | Load `LoadSelectedModelCommand` | `CommandBar` · `ModelBar` → `AppBarButton` · `LoadModelButton` (icon `E8E5`) | Load | `InvalidateVisionCache`; refuse if State ∈ {Running, Held, Homing} → Warn `Teach.LoadModelBusy`; `McDataManager.LoadModel(name)` else Error `Teach.LoadModelFailed` | On success refresh TE-1 + whole page (tray switched). |
| TE-4 | Save `SaveModelCommand` | `AppBarButton` · `SaveModelButton` (`E74E`) | Save | `McDataManager.SaveModel()` else Error `Teach.SaveModelFailed` | Refresh list, select current. |
| TE-5 | Delete `DeleteModelCommand` | `AppBarButton` · `DeleteModelButton` (`E74D`) | Delete | open model → Warn `Teach.DeleteOpenModel`; **[Confirm]** `Teach.ConfirmDeleteModel`; `McDataManager.DeleteModel(name)` else Error | — |
| TE-6 | New name `TextBox NewModelNameText` | `TextBox` · `NewModelNameBox` | placeholder "New model name" | — | Tip `Teach.NewModelNameTip`. |
| TE-7 | New `NewModelCommand` | `AppBarButton` · `NewModelButton` (`E710`) | New | `InvalidateVisionCache`; validate (`Teach.EnterModelName`, `ModelNameInvalid` for `\/:*?"<>|`); exists → Warn `ModelExists`; `McDataManager.UseModel(new ModelData{Name}, countsAsLoaded:true)`; `SaveModel(name)` | Clear box, refresh, select new. |
| TE-8 | Copy selected `CopyModelCommand` | `AppBarButton` · `CopyModelButton` (`E8C8`) | Copy | no source → Warn `Teach.ChooseSourceModel`; validate target; exists → Warn; `McDataManager.CopyModel(src, target)` else Error `CopyModelFailed` | Tip "Copy selected model to new name". |

`CommandBar`: `DefaultLabelPosition=Right`, `IsDynamicOverflowEnabled=False`, buttons MinHeight 40.

### 4.2 SELECT TRAY card

| ID | WPF | WinUI · x:Name | Label | Core | Rules |
|---|---|---|---|---|---|
| TE-9 | 3 buttons `SelectInputCommand/SelectInput2Command/SelectAssyCommand` (accent = selected) | `SelectorBar` · `TraySelector` (`TrayInput1Item`, `TrayInput2Item`, `TrayAssyItem`) | Input 1 · Input 2 · Assy | `Layout = UseInputTray ? Model.InputLayout(sel) : Model.AssyTray` | Change → reload Rows/Cols boxes, scan order, all cards, recompute cells. |
| TE-10 | `TrayTitle` | `TextBlock` · `TrayTitleText` | Input tray 1 / Input tray 2 / Assy tray | — | 16 Bold. |
| TE-11 | Shared-size note | `TextBlock` · `SharedSizeText` | `Teach.InputSizeShared` | — | Visible for input trays. 11 Secondary. |
| TE-12 | Tool buttons `SelectNozzle1/2Command` | `SelectorBar` · `ToolSelector` (`Tool1Item`, `Tool2Item`) | Tool 1 · Tool 2 | `CurrentZAxis = Tool1 ? Z1 : Z2`; `CurrentGeometry = Layout.GeometryFor(z)` | Header "Select Tool". Change → recompute. |
| TE-13 | Radio `IsRowMajorMode/IsZigZagMode` | `RadioButtons` · `ScanOrderRadios` (MaxColumns 2) | Sequential · Zig zag | `SetScanOrder`: `InvalidateVisionCache`; write on `Model.InputTray` (input) or `Layout`; `McDataManager.SyncRunTrays()` | Header "Scan order". Only RowMajor/Serpentine (ColMajor not exposed). Guard programmatic selection. |
| TE-14 | `Canvas TraversalCanvas` | `Canvas` · `TraversalCanvas` H 80 | — | `TrayState.VisitIndex(r,c,4,order)` | Fixed 2×4 preview: grey cell rects, numbers 1–8, SteelBlue arrows (trimmed ends, head 7 px). Redraw on order change and SizeChanged. |

### 4.3 TOP CAM REF card (visible only Assy tray)

| ID | WPF | WinUI · x:Name | Label | Core | Rules |
|---|---|---|---|---|---|
| TE-15 | Hints | `TextBlock` ×2 | `Teach.CamAnchorHint1`, `CamAnchorHint2` | — | 11 Secondary, wrap (always visible). |
| TE-16 | "X / Y2: x / y" | `TextBlock` · `CamAnchorText` | — | `Model.AssyTray.CamTopDownAnchorX/Y` | Values Bold, `0.000`. |
| TE-17 | Teach `TeachCamTopDownAnchorCommand` | `Button` · `TeachCamAnchorButton` | Teach | reads X + `AssyTray.AxisRow`; [Confirm] (old "(not taught)" if `!CamTopDownAnchorTaught`); set X/Y + `CamTopDownAnchorTaught=true`; InvalidateVisionCache | — |
| TE-18 | Go top CAM ref `MoveCamTopDownAnchorCommand` | `Button` · `GoCamAnchorButton` | Go Ref | `Func.MoveToCellAsync(AssyTray.AxisRow, AnchorY, AnchorX, Safety.AbortToken)` | **[2×]**, [Busy-off]; fail → Warn `Teach.MoveFailed` + `Status.Message`. Tip `Teach.GoCamAnchorTip`. |

### 4.4 3-POINT TEACH card

| ID | WPF | WinUI · x:Name | Label | Core | Rules |
|---|---|---|---|---|---|
| TE-19 | Hint | `TextBlock` | `Teach.ThreePointHint` | — | — |
| TE-20 | P1 `TeachP00Command` | `Button` · `P1Button` (MinH 66) | "P1" 18 Bold + "cell (1,1)" + "X …" + "Y …" (Consolas 10, two lines — digits never clipped) | `TeachPoint(P1)`: X + `Layout.AxisRow` pos; [Confirm] what=`Teach.WhatGridPoint`(name, tray, tool); new `TeachPoint(x,y)` (only way to set `Taught`); input tray → `SyncRunTrays` | Bg `VisionOk #C8EBC8` when `CurrentGeometry.P00.Taught`. Tip `Teach.P1Tip`. |
| TE-21 | Cols box `ColsInput` | `NumberBox` · `ColsBox` (int, min 1, spin Compact) W 64 | caption "Cols" + "→" | — | Between P1 and P2. |
| TE-22 | P2 `TeachP0CCommand` | `P2Button` | "P2", "end of row 1" | same, `P0C` | Tip `Teach.P2Tip`. |
| TE-23 | Rows box `RowsInput` | `NumberBox` · `RowsBox` | "Rows" | — | Between P2 and P3. |
| TE-24 | P3 `TeachPR0Command` | `P3Button` | "P3", "end of col. 1" | same, `PR0` | Tip `Teach.P3Tip`. |
| TE-25 | Apply `ApplySizeCommand` | `Button` · `ApplySizeButton` | Apply | `InvalidateVisionCache`; `(input ? Model.InputTray : Layout).Resize(rows, cols)`; `SyncRunTrays` | Only on click (no resize while typing). Tip `Teach.ApplySizeTip`. |
| TE-26 | Verify point | `TextBlock` ×2 · `VerifyXText`, `VerifyYText` + `Button` · `TeachVerifyButton` | "Verify (optional)" · Teach | `TeachPoint(PRC_Verify)` | — |
| TE-27 | `VerifyText` | `TextBlock` · `VerifyText` | `Teach.VerifyNotTaught` / `VerifyOk` / `VerifyFail` / `VerifyNoPoint` | `Layout.IsTaughtFor(z)`, `Layout.VerifyGrid(z, out err)`, `VerifyThreshold` | 12 Secondary. |

### 4.5 PICK ANGLE card (visible only input trays)

| ID | WPF | WinUI · x:Name | Label | Core | Rules |
|---|---|---|---|---|---|
| TE-28 | Hint | `TextBlock` | `Teach.PickAngleHint` | — | — |
| TE-29 | A1 value + Teach A1 | `PickA1Text`, `TeachPickA1Button` | "A1: 12.34°" · Teach A1 | `AxisAngle.Wrap(A1.CurrentPos)`; [Confirm]; `Model.PickAngleA1` | No cache invalidation. |
| TE-30 | A2 | `PickA2Text`, `TeachPickA2Button` | Teach A2 | `Model.PickAngleA2` | — |

### 4.6 PICK HEIGHT / PLACE HEIGHT card

| ID | WPF | WinUI · x:Name | Label | Core | Rules |
|---|---|---|---|---|---|
| TE-31 | Title/hint by tray | `ZPickTitleText`, `ZPickHintText` | `Teach.ZPickTitle`/`ZPlaceTitle`; `ZPickHint`/`ZPlaceHint` | — | Input = PICK, Assy = PLACE. |
| TE-32 | Z1 / Z2 height | `ZPickZ1Text`, `ZPickZ2Text` + `TeachZPickZ1Button`, `TeachZPickZ2Button` | "Z1: 12.345 mm" · Teach Z1 / Teach Z2 | `Layout.Z_PickZ1/Z2`; [Confirm] what `Teach.WhatZPick/WhatZPlace` | Refresh TE-35. |
| TE-33 | Slow-pt title/hint | `ZSlowTitleText`, `ZSlowHintText` | `Teach.ZSlowPickTitle`/`ZSlowPlaceTitle`; `Loc::F("Teach.ZSlowHint", pick/place word)` | — | Inset divider above. |
| TE-34 | Z1 / Z2 slow pt | `ZSlowZ1Text`, `ZSlowZ2Text` + `TeachZSlowZ1Button`, `TeachZSlowZ2Button` | Teach Z1 / Teach Z2 | `Layout.Z_SlowZ1/Z2`; [Confirm] `Teach.WhatZSlow` | — |
| TE-35 | `ZSlowText` | `TextBlock` · `ZSlowStatusText` | per Z: `Teach.ZSlowNotTaught` / `ZSlowWrongSide` / `ZSlowOk` | `Layout.ZSlowFor/ZPickFor` | 11 Secondary, 2 lines. Informs, never blocks. |

### 4.7 BOTTOM CAM POS. card

| ID | WPF | WinUI · x:Name | Label | Core | Rules |
|---|---|---|---|---|---|
| TE-36 | Hint | `TextBlock` | `Teach.AlignPosHint` | — | — |
| TE-37 | Nozzle #1/#2 X + Teach | `AlignX1Text`, `AlignX2Text`, `TeachAlign1Button`, `TeachAlign2Button` | "Nozzle #1: 123.456 mm" · Teach #1 / Teach #2 | X pos; [Confirm] `Teach.WhatAlignPos`; `Model.AlignPosX_Nozzle1/2`; InvalidateVisionCache | — |

### 4.8 JIG (HAND-EYE) card (always visible — not tray-bound)

| ID | WPF | WinUI · x:Name | Label | Core | Rules |
|---|---|---|---|---|---|
| TE-38 | Hint | `TextBlock` | `Teach.JigHint` | — | — |
| TE-39 | Pick #1 value + Teach + Go | `JigPick1Text`, `TeachJigPick1Button`, `GoJigPick1Button` | "Pick #1: X … · Y2 …" · Teach · Go | Teach: X + `AssyTray.AxisRow`; [Confirm] `Teach.WhatJigPick`; `Model.HandEyePick_Nozzle1 = new TeachPoint`. Go: not taught → Warn `Teach.JigPickNotTaughtWarn`; `Func.MoveToCellAsync(rowAxis, y, x)` | Go **[2×]** [Busy-off], tip `Teach.GoJigPick1Tip`. |
| TE-40 | Pick #2 | `JigPick2Text`, `TeachJigPick2Button`, `GoJigPick2Button` | Teach · Go | `HandEyePick_Nozzle2` | tip `GoJigPick2Tip`. |
| TE-41 | Insp part | `JigCamText`, `TeachJigCamButton`, `GoJigCamButton` | "Insp part: …" · Teach · Go | `Model.HandEyeCamPos`; warn `Teach.JigCamNotTaughtWarn` | tip `GoJigCamTip`. |
| TE-42 | `HandEyeJigText` | `TextBlock` · `JigStatusText` | `Teach.JigNotTaught`(missing list) / `JigAllTaught` + `Teach.JigNote`(AssyTray.Z_PickZ1, Z_PickZ2) | `Model.HandEyePick_Nozzle1/2.Taught`, `HandEyeCamPos.Taught` | 11 Secondary. |

### 4.9 CELL COORDS card (right, fills)

| ID | WPF | WinUI · x:Name | Label | Core | Rules |
|---|---|---|---|---|---|
| TE-43 | Cell grid (`UniformGrid Layout.Rows×Cols`) | `Grid` · `CellGrid` built in code in `ScrollViewer` | visit order (14 Bold) + "●" (8, Accent) when place trim | `Layout.CellPos(z,r,c)`, `GetOffset(z,r,c)`; order `VisitIndex(rowBase+r, c, cols, ScanOrder) − rowBase·cols + 1` (rowBase = Input1 rows when Tray 2) | Button MinW/H 40. Bg `VisionOk` when cell offset ≠ 0. Selected: Accent border 2.5. Tooltip "(r, c)". Tap → select (keep selection across recompute by row/col). [Busy-off]. Optional `ProgressRing` · `BusyRing` in header while Busy. |
| TE-44 | Selected title | `TextBlock` · `SelectedCellText` | "(r, c)" / `Teach.NoCellHint` | — | 13 Bold. |
| TE-45 | Selected details | `TextBlock` · `SelectedCellInfoText` | "Order #n   X=…  Y=…  Offset=dx/dy" | cell row | 11 Secondary. |
| TE-46 | `CamPosText` | `TextBlock` · `CamPosText` | `Teach.CamPosAt` / `CamPosNoAnchor` / `NoCellSelected` | `Layout.CamPosFor(r,c)` | Visible only Assy × Tool 2 (`ShowCamMove`). |
| TE-47 | Move `MoveToCellCommand` | `Button` · `MoveCellButton` | Move | `Func.MoveToCellForPickAsync(nozzle, Model.PickAngle(nozzle), Layout.AxisRow, y, x, Safety.AbortToken)` (Z to Z_SAFE, A to pick angle) | **[2×]** [Busy-off]; fail → Warn `Teach.MoveFailed`. Tip `Teach.MoveCellTip`. |
| TE-48 | Z Pick/Place `MoveZAtCellCommand`, label `ZCellMoveLabel` | `Button` · `ZCellButton` | "Z1 Pick" / "Z2 Place" (by tool × tray) | `MoveZAsync(z, Layout.ZPickFor(z), Layout.ZSlowFor(z))` → `Func.MoveZDownAsync/MoveZUpAsync` (slow pt > 0) else `Func.MoveAsync` | **[2×]** [Busy-off]; fail → Warn `Teach.AxisMoveFailed`. Tip `Teach.ZCellMoveTip`. |
| TE-49 | Move CAM `MoveCamToCellCommand` | `Button` · `MoveCamButton` | Move CAM | anchor not taught → Warn `Teach.CamAnchorNotTaughtWarn`; `Func.MoveToCellAsync(Layout.AxisRow, p.Y, p.X)` with `p = Layout.CamPosFor` | Visible Assy × Tool 2. **[2×]**. Tip `Teach.MoveCamTip`. |
| TE-50 | Set Offset `SetOffsetCommand` | `Button` · `SetOffsetButton` | Set Offset | offset = (X, rowAxis) − `Layout.CellPosRaw(z,r,c)`; [Confirm] `Teach.WhatCellPos` with `Teach.OffsetText`; `Layout.SetOffset(z,r,c,dx,dy)`; InvalidateVisionCache | Tip `Teach.SetOffsetTip`. |
| TE-51 | Clear `ClearOffsetCommand` | `Button` · `ClearOffsetButton` | Clear | `Layout.SetOffset(z,r,c,0,0)`; InvalidateVisionCache | Enabled only when selected cell has offset. Tip `Teach.ClearOffsetTip`. |
| TE-52 | Common trim X/Y/Angle | `NumberBox` ×3 · `TrimAllXBox`, `TrimAllYBox`, `TrimAllABox` + unit "mm / mm / °" | caption "Common ofs:" | `CurrentGeometry.PlaceDxAll/DyAll/DaAll` (loaded on every recompute) | Assy only (`IsAssyTray`). **No Min/Max on the NumberBox** (would clamp silently) — limits checked on Apply. |
| TE-53 | Apply / Clear common | `Button` · `ApplyTrimAllButton`, `ClearTrimAllButton` | Apply · Clear | Apply: input tray → Warn `Teach.TrimAssyOnly`; |v| > 1 mm / 5° → Warn `Teach.TrimTooLarge`; `Layout.SetPlaceTrimAll(z,dx,dy,da)`; InvalidateVisionCache. Clear: `SetPlaceTrimAll(z,0,0,0)` | Clear enabled when any common trim ≠ 0. Tip `Teach.ApplyTrimAllTip`. |
| TE-54 | Cell trim X/Y/Angle | `TrimXBox`, `TrimYBox`, `TrimABox` | caption "Cell ofs:" | selected cell `PlaceDx/Dy/Da` (reloaded on selection change — avoids copying values to another cell) | Assy only. |
| TE-55 | Apply / Clear cell | `ApplyTrimButton`, `ClearTrimButton` | Apply · Clear | no cell → Warn `Teach.NoCellChosen`; limits as TE-53; `Layout.SetPlaceTrim(z,r,c,dx,dy,da)`; Clear `(…,0,0,0)` + reload boxes | Clear enabled when cell has trim. Tip `Teach.ApplyTrimCellTip`. |
| TE-56 | Trim hint | `TextBlock` | `Teach.TrimHint` | — | 11 Secondary. |

### 4.10 JOG & PNEUMATICS card (right, bottom)

| ID | WPF | WinUI · x:Name | Label | Core | Rules |
|---|---|---|---|---|---|
| TE-57 | Jog rows X, Z1, Z2 (col 1), Y1, Y2 (col 2) | `ui::JogRow` ×5 (§9.3) | name, pos (Consolas 14 + unit), − + | `Func.Jog(name, ±1/0)` | **[Hold]**, buttons 36 high (compact by request 2026-09-28: cell grid has priority). No Stop/Origin/SVO/RST/Move here (Manual only). |
| TE-58 | Go T1 / Go T2 (X row) `MoveXTrigger1/2Command` | `Button` · `GoT1Button`, `GoT2Button` | Go T1 · Go T2 | Z(n) → `Motion.ZSafe` via `Func.MoveAsync`, then X → `Model.AlignPosX(n)`; fail → Warn `Teach.ZCaptureFailed` / `AxisMoveFailed` | **[2×]** [Busy-off]. Tips `Teach.GoT1TriggerTip`, `GoT2TriggerTip`. |
| TE-59 | Origin (Z rows) `MoveZHomeCommand` | `Button` · `ZOriginButton` (per row) | Origin | `MoveZAsync(z, Config.HomeOffset, Layout.ZSlowFor(z))` (up through slow pt) | **[2×]** [Busy-off]. Tip `Teach.ZHomeTip`. |
| TE-60 | Go Pick `MoveZPickCommand` | `Button` · `ZGoPickButton` | Go Pick | input tray selected → its `ZPickFor/ZSlowFor`; Assy selected → `Model.InputTray` | **[2×]**. Tip `Teach.GoPickTip`. |
| TE-61 | Go Place `MoveZPlaceCommand` | `Button` · `ZGoPlaceButton` | Go Place | `Model.AssyTray.ZPickFor/ZSlowFor` | **[2×]**. Tip `Teach.GoPlaceTip`. |
| TE-62 | Jog speed Slow/Fast | `SelectorBar` · `JogSpeedSelector` | Slow · Fast | `McFunction.JogFast` (shared with Vision Edit) | Header "Jog speed". Sync from `Func.JogFast` each tick (may change on Vision Edit); guard programmatic. |
| TE-63 | Vac #1 / #2 | `Button` · `Vac1Button`, `Vac2Button` | Vac #1 · Vac #2 | `Func.SetVacuum(n, !on)` | Real DO `DoVacuum1/2` → Green +glow. |
| TE-64 | Blow #1 / #2 | `Blow1Button`, `Blow2Button` | Blow #1 · Blow #2 | `Func.SetBlow(n, !on)` | `DoBlow1/2` → Green +glow. |
| TE-65 | CAM BTM-UP / TOP-DOWN LIGHT | `CamBtmLightButton`, `CamTopLightButton` | Btm Light · Top Light | `Io.SetDo(DoLampCam1/2, toggle)` | DO → Green +glow. Tips `Teach.Cam1LightTip`, `Cam2LightTip`. |
| TE-66 | Save (big) | `Button` (Primary) · `SaveBigButton` 130×48, 16 Bold | Save | `McDataManager.SaveModel()` (= TE-4) | Bottom-right inside the card, shadow. Tip `Teach.SaveTip`. |

---

## 5. Setting (`SettingPage`) — VM: `SettingViewModel`

Navigation inside the page: **`NavigationView`** `PaneDisplayMode=Left`, `IsPaneToggleButtonVisible=False`, `IsBackButtonVisible=Collapsed`,
`IsSettingsVisible=False`, `OpenPaneLength=200` (Windows-Settings look, same as Slit `SideNavItemStyle` but the real control). Items:
System (`E713`) · Vision (`E722`) · Servo (`E90F`) · Delay (`E916`) · Log (`E8FD`) (`Setting.Tab*`). Content = 5 sections, Visibility swap.
`PaneFooter` = save block (always visible, like WPF's row outside tabs).

Field model (Slit `ui::settingpage::FieldGroup`): fields write into **live** `SysData` / `AxisConfig` on LostFocus/Enter/Toggled;
**Save** calls `CommitAll()` then saves. Add `BindNumber(NumberBox, label, get, set, optional=false)`. Row layout = Slit
`SettingRowStyle`: title left (WPF label without unit), control + unit right, 1 px dividers; long WPF tooltips stay tooltips.

```
┌ 200 ─────────────┐┌ * ─────────────────────────────────────────────────────────────────┐
│ ⚙ System         ││ AUTO MODE                                                           │
│ 📷 Vision        ││  Auto nozzles      [ Both ][Nozzle #1][Nozzle #2]                   │
│ 🔧 Servo         ││  Dry run           [Dry Run]  OFF                                   │
│ ⏱ Delay          ││  Applies at once, no Save. Locked while running — stop first.       │
│ ☰ Log            ││  ▌<Status.Message>                                                  │
│                  ││ SYSTEM PARAMS                                                       │
│                  ││  Z_SAFE                                  [  5.000 ] mm              │
│                  ││  Pick retries                            [ 2 ]                      │
│ ──────────────── ││  …                                                                  │
│ [ Save System ]  ││  Simulation (needs restart)              ( ●) On                    │
│ [ Open Folder ]  ││  Re-Origin after EMG                     (● ) Off                   │
│ ▌system.json saved│ …                                                                  │
│ Data: D:\…       ││                                                                     │
│ alarms.json NOT… │└─────────────────────────────────────────────────────────────────────┘
└──────────────────┘
```

### 5.1 Pane footer (save block)

| ID | WPF | WinUI · x:Name | Label | Core | Rules |
|---|---|---|---|---|---|
| SE-1 | Save system.json `SaveCommand` | `Button` (Primary) · `SaveSystemButton` | Save System | `CommitAll()` (errors → Warn list, abort); `SysData.ZSafe = Motion.ZSafe = ZSafe box`; `McDataManager.SaveSystemData()` | Result → SE-3. |
| SE-2 | Open data folder | `Button` · `OpenFolderButton` | Open Folder | `explorer.exe AppPaths.Root` | Errors ignored (as WPF). |
| SE-3 | `SaveResult` | `InfoBar` · `SaveResultBar` | `Setting.Saved` (Success) / `Setting.SaveFailed` (Error) | — | Hidden until first save. |
| SE-4 | Data folder | `TextBlock` · `DataRootText` | "Data folder: " + path | `AppPaths.Root` | 11 Secondary, wrap/trim + tooltip. |
| SE-5 | Fallback root | `TextBlock` · `FallbackRootText` | `Setting.UsingFallbackRoot` | `AppPaths.UsingFallbackRoot` | Caution colour; visible when true. |
| SE-6 | Alarm catalog | `TextBlock` · `AlarmCatalogText` | `Setting.AlarmCatalogNotLoaded` | `AlarmCatalog.Loaded` | Critical; visible when false. |

### 5.2 System section

| ID | WPF | WinUI · x:Name | Label | Core | Rules |
|---|---|---|---|---|---|
| SE-7 | Both / Nozzle #1 / Nozzle #2 (`SetOnlyNozzleCommand` 0/1/2) | `Button` ×3 · `NozzleBothButton`, `Nozzle1Button`, `Nozzle2Button` (110×40) | Both · Nozzle #1 · Nozzle #2 | `McControl.SetOnlyNozzle(n)` (immediate, no Save; refused while running → `Status.Message`) | Follows real `SysData.OnlyNozzle`: Both = Green, single = Caution; solid, no glow. Row title "Auto nozzles". |
| SE-8 | Dry run button (`ToggleDryRunCommand`, content OFF/ON) | `Button` · `DryRunButton` (160×40) + `TextBlock` · `DryRunStateText` | Dry Run · state "ON — no product" / "OFF" | `McControl.SetDryRun(!Status.DryRun)` | `Status.DryRun` → Caution `#9D5D00` solid, no glow. Tip `Setting.DryRunTip`. Not a ToggleSwitch (refusal must not flicker). |
| SE-9 | Note | `TextBlock` | `Setting.AutoModeNote` | — | 11 Secondary. |
| SE-10 | Message banner | `InfoBar` (Warning) · `AutoMessageBar` | `Status.Message` | — | Visible when non-blank (refusal reason). |
| SE-11 | Z_SAFE (mm) `ZSafe` (VM copy, applied on Save) | `NumberBox` · `ZSafeBox` + "mm" | Z_SAFE | `SysData.ZSafe`, `Motion.ZSafe` | Loaded on page create; written only by SE-1. |
| SE-12 | Pick retries | `NumberBox` (int) · `PickRetryBox` | Pick retries | `SysData.PickRetryCount` | — |
| SE-13 | Max NG in a row | `MaxNgBox` | Max NG in a row | `SysData.MaxConsecutiveNg` | — |
| SE-14 | Ctrl loop (ms) | `ControlTickBox` + "ms" | Ctrl loop | `SysData.ControlTickMs` | — |
| SE-15 | Watchdog (ms) | `WatchdogBox` + "ms" | Watchdog | `SysData.WatchdogTimeoutMs` | — |
| SE-16 | Simulation (needs restart) | `ToggleSwitch` · `SimModeSwitch` | Simulation (needs restart) | `SysData.SimulationMode` | — |
| SE-17 | Re-Origin after EMG | `RequireHomeSwitch` | Re-Origin after EMG | `SysData.RequireHomeAfterEmg` | — |
| SE-18 | Origin A1/A2 in sequence | `HomeSeqSwitch` | Origin A1/A2 in sequence | `SysData.HomeRotateSequential` | — |
| SE-19 | Curtain fitted (DI13) | `CurtainSwitch` | Curtain fitted (DI13) | `SysData.HasLightCurtain` | Tip `Setting.HasLightCurtainTip` (important). |
| SE-20 | Scan Assy tray by row | `ScanByRowSwitch` | Scan Assy tray by row | `SysData.ScanAssyByRow` | Tip `Setting.ScanAssyByRowTip`. |

### 5.3 Vision section (3 cards as WPF)

| ID | WPF | WinUI · x:Name | Label | Core | Rules |
|---|---|---|---|---|---|
| SE-21 | Solution path `VisionSolutionPath` | `TextBox` · `SolutionPathBox` W 360 | title `Setting.SolutionPath`; note `SolutionPathNote` (tooltip `SolutionPathTip`) | `SysData.VisionSolutionPath` | Card `VISION — SOLUTION SETUP`. |
| SE-22 | Browse... | `Button` · `BrowseSolutionButton` | Browse | `PickSolutionFileAsync` (FileOpenPicker, Win32 fallback; filter `*.solw`) | Sets the box + SysData. |
| SE-23 | Reload solution | `Button` (Primary) · `ReloadSolutionButton` | Reload | `CommitAll`; `McDataManager.SaveSystemData()`; `McControl.ReloadVisionSolution()` → refusal text or `Setting.ReloadRequested` | Result → `ReloadResultText` (11 Secondary). Immediate, no Save needed. |
| SE-24 | Tools note | `TextBlock` | `Setting.SolutionTools` / `SolutionToolsNote` | — | — |
| SE-25 | Check vars | `Button` · `CheckVarsButton` | Check Vars | `McControl.SyncVisionGlobalVariablesAsync(false)` → `report.ToText()` | Background; "Checking…" first. Tip `Setting.CheckVarsTip`. |
| SE-26 | Create missing vars | `Button` (Primary) · `CreateVarsButton` | Create Vars | dry run first; error → report; 0 missing → `Setting.NoMissingVars` + report; else **[Confirm]** `Loc::F("Setting.CreateVarsConfirm", n)` + path + `CreateVarsConfirmTail`, title `Setting.CreateVarsTitle` (WPF used a `MessageBox` — now `ui::Dialogs`); No → `CreateVarsCancelled`; Yes → `CreatingVars` → `SyncVisionGlobalVariablesAsync(true)` | Exceptions → `Setting.Error`. Tip `Setting.CreateVarsTip`. |
| SE-27 | Check outputs | `Button` · `CheckOutputsButton` | Check Outputs | `McControl.CheckVisionFlowOutputsAsync()` | "Reading Output Params…" first. Tip `Setting.CheckFlowScriptTip`. |
| SE-28 | `GlobalVarReport` | `TextBlock` (selectable) in `ScrollViewer` MaxH 160 · `VarReportText` | — | — | 11. Optional `ProgressRing` while running. |
| SE-29 | `FlowOutputReport` | `TextBlock` Consolas 11, MaxH 320 · `FlowReportText` | — | — | Selectable. |
| SE-30 | Limits (card `VISION — LIMITS & TOLERANCES`, note) | `NumberBox` rows: `MaxCompAngleBox` °, `MaxCompDistBox` mm, `PlaceGuardBox` mm, `GuardSkipBox`, `AlignXTolBox` mm, `ACapTolBox` °, `FlowTimeoutBox` ms | Max comp. angle · Max center ofs · Max grid dev. · Off-grid cells in a row · Insp pos. X tol. · A dev. at capture · Timeout Flow | `SysData.MaxCompensationAngleDeg`, `MaxCompensationDistance`, `PlaceTargetGuardMm`, `MaxGuardSkipStreak`, `AlignXTolMm`, `ACapTolDeg`, `VisionTriggerTimeoutMs` | Sub-header "Auto place limits". |
| SE-31 | Check & rot. test | `CalibResidualBox` mm, `VerifyAngleTolBox` °, `VerifyMinEccBox` mm, `VerifyMinTurnBox` ° | Check tol. · Rot. test angle tol. · Rot. test min ecc. · Min test turn | `SysData.CalibMaxResidualThresholdMm`, `AlignVerifyAngleTolDeg`, `AlignVerifyMinEccMm`, `AlignVerifyMinTurnDeg` | Sub-header "Top-down check & rot. test". |
| SE-32 | Flow names (card `VISION — FLOW & VAR NAMES`, notes) | `TextBox` · `FlowLocateTopBox`, `FlowTool1Box`, `FlowTool2Box`, `OutFoundBox` | Flow LocateTop · Flow AlignBottom Tool1 · Flow AlignBottom Tool2 · Output Found (shared) | `SysData.VisionOutputs.FlowLocateTop / FlowAlignBottomTool1 / FlowAlignBottomTool2 / OutFound` | W 150. |
| SE-33 | Main image views | `TextBox` · `RenderTool1Box`, `RenderTool2Box`, `RenderTopBox` | Bottom CAM — Nozzle #1 · #2 · Top CAM | `SysData.VisionOutputs.RenderTool1 / RenderTool2 / RenderTopDown` | W 200; note `Setting.MainViewsNote`. |

### 5.4 Servo section

| ID | WPF | WinUI · x:Name | Label | Core | Rules |
|---|---|---|---|---|---|
| SE-34 | Ezi-IO IPs | `TextBox` · `IoDiIpBox`, `IoDoIpBox` | IP module 32Di · IP module 32Do | `SysData.IoDiIpAddress`, `IoDoIpAddress` | Card `7-AXIS CFG`, sub-header "Card I/O Ezi-IO Ethernet". Restart needed (Saved text says so). |
| SE-35 | Z slow-down pt flags | `ToggleSwitch` ×4 · `InputZDownSwitch`, `InputZUpSwitch`, `AssyZDownSwitch`, `AssyZUpSwitch` | Input tray — down/up · Assy tray — down/up | `SysData.InputZSlowDown/InputZSlowUp/AssyZSlowDown/AssyZSlowUp` | Sub-header tip `Setting.ZSlowTip`. |
| SE-36 | Axis table (`DataGrid AxisConfigs`) | code-built table (`SimpleTable` + `CellBox`) · `AxisTableHost`, horizontal scroll, frozen header | Axis · Driver · COM (A6SF) · IP (Fastech) · Jog slow · Jog fast · Auto high · Auto mid · Auto low · Origin ofs · Standby · Soft Limit − · Soft Limit + · Acc (ms) · Dec (ms) | `SysData.Axes[i]`: `Name`, `Kind` (read-only), `ComPort`, `IpAddress`, `JogSpeedSlow/Fast`, `MoveSpeed`, `MoveSpeedMid/Low` (blank = 50 % / 20 % → `BindOptionalDouble`), `HomeOffset`, `StandbyPos`, `SoftLimitNeg/Pos`, `AccelTimeMs`, `DecelTimeMs` | Live objects (soft limits, standby, acc/dec effective after commit, no restart). Header tips on Acc/Dec: `Setting.AccTip`, `DecTip`. Invalid cell → red border + tooltip, value kept. |

### 5.5 Delay section (card `DELAY`)

| ID | WPF | WinUI · x:Name | Label | Core | Rules |
|---|---|---|---|---|---|
| SE-37 | Capture table (rows CAM, cols settle / lamp) | `Grid`: `BottomSettleBox`, `BottomLampBox`, `TopSettleBox`, `TopLampBox` (NumberBox, "ms") | rows "CAM bottom-up", "CAM top-down"; cols "Capture settle", "Light warm-up" | `SysData.BottomSettleMs/BottomLampMs/TopSettleMs/TopLampMs` | Tips `Setting.SettleTip`, `LampTip` on headers and boxes. |
| SE-38 | Vac | `VacTimeoutBox`, `VacSettleBox`, `BlowTimeBox` ("ms") | Sensor timeout · Vac build-up · Release blow | `SysData.VacuumTimeoutMs/VacuumSettleMs/BlowTimeMs` | Tips `Setting.VacuumTimeoutTip`, `VacuumSettleTip`, `BlowTimeTip`. |

### 5.6 Log section

| ID | WPF | WinUI · x:Name | Label | Core | Rules |
|---|---|---|---|---|---|
| SE-39 | `DataGrid LogList` | `ui::settingpage::LogTable` · `LogTableHost` | Time · Level · Source · Msg | `LogWorker.LogList` | Card `LOG (MEMORY)`. Prepend new rows only (version check). |

---

## 6. Vision Edit (`VisionEditPage`) — VM: `VisionCalibViewModel`, `VisionEditView.xaml.cs`

```
┌───────────────────────────────────────────────────────────────────────────────────────────────────┐
│ VISION EDIT │ X [−][+]  Z1 [−][+]  Z2 [−][+] │ [Z1 Safe][Z2 Safe] │ ‹Slow|Fast› │ [  STOP  ]     │
│             │ Y2[−][+]  A1 [−][+]  A2 [−][+] │ [Vac #1 ][Vac #2 ] │             │ [ 96×76  ]     │
├──────────────────────────────────────────────────────────────┬────────────────────────────────────┤
│ [VmGlobalToolControl popup — camera, comm, vars, script…]    │ ▸ VISION CALIB (note)        320  │
│ ──────────────────────────────────────────────────────────── │ <ReadyStatusText>                 │
│                                                              │ [Check Calib]                     │
│        VmMainViewConfigControl popup (Flow editor)           │ <VmStatusText>                    │
│                                                              │ (ring) <SequenceStatusText>       │
│                                                              │ Tool (nozzle) ‹Tool 1|Tool 2›     │
│                                                              │ Calib steps (in order) [▼ combo]  │
│                                                              │ ── selected step panel ──         │
│                                                              │ e.g. Top CAM: center, steps,      │
│                                                              │  [7][8][9] / [4][5][6] / [1][2][3]│
│                                                              │  [Auto Calib] [Load Point]        │
│                                                              │  Test travel [ ] [Verify Top]     │
│                                                              │  <VerifyResultText>               │
└──────────────────────────────────────────────────────────────┴────────────────────────────────────┘
```

Right column W 320 (WPF 280; WinUI scroll bars overlay so text gets more room). Whole calibration column is **[Busy-off]**
(`ContentControl` · `CalibPanel`), STOP never disabled. Every step button below uses `StepRunAsync` semantics: ignore if Busy;
`!Status.OrgOk` → Warn `Vision.NoOrigin`; run on background with `McControl.ManualToken`; result line to its result text;
failure → Warn `Loc::F("Vision.StepStopped", title, reason)`.

### 6.1 Header jog bar (full width)

| ID | WPF | WinUI · x:Name | Label | Core | Rules |
|---|---|---|---|---|---|
| VE-1 | Title | `TextBlock` | VISION EDIT | — | 20 Bold. |
| VE-2 | Jog grid 3×2 (`JogAxes` X Z1 Z2 / Y2 A1 A2) | `Grid` · `JogGrid` built in code: name + − + per axis | − · + | `Func.Jog(name, ±1/0)` | **[Hold]** [Busy-off]. Buttons 48×40, 18 pt. Tips `Vision.JogBackTip`, `JogFwdTip`. No positions (title bar shows them). |
| VE-3 | Z1 → Z_SAFE / Z2 → Z_SAFE `MoveZSafeCommand` | `Button` · `Z1SafeButton`, `Z2SafeButton` (112×40) | Z1 Safe · Z2 Safe | `Calib.StepMoveZSafeAsync(n, ManualToken)` (a move, not homing) | **[2×]** [Busy-off]. Tips `Vision.Z1SafeTip`, `Z2SafeTip`. |
| VE-4 | Vac #1 / #2 | `Button` · `Vac1Button`, `Vac2Button` | Vac #1 · Vac #2 | `Func.SetVacuum(n, !on)` | Green +glow from `Io.GetDo(DoVacuum1/2)`. [Busy-off]. Tips `Vision.Vacuum1Tip`, `Vacuum2Tip`. |
| VE-5 | Slow / Fast | `SelectorBar` · `JogSpeedSelector` | Slow · Fast | `McFunction.JogFast` | Re-sync on `OnShown(true)` and each tick (shared with Teaching). |
| VE-6 | STOP | `Button` · `StopAllButton` 96×76, 16 Bold, Red solid | STOP | `McControl.RequestStop` (also cancels calib / hand-eye / step run on `ManualToken`) | Never disabled. Tip `Vision.StopTip`. |

### 6.2 VisionMaster pane (left)

| ID | WPF | WinUI · x:Name | Label | Core | Rules |
|---|---|---|---|---|---|
| VE-7 | `VmGlobalToolControl` | `Border` · `GlobalToolHost` H 40 + `ui::VmPane(GlobalTool)` | — | VisionMaster SDK | Created via queued VM work on first `OnShown(true)`. |
| VE-8 | `VmMainViewConfigControl ConfigControl` + `BindMultiProcedure()` | `Border` · `ConfigHost` + `ui::VmPane(MainView)` | — | `ProcBindMultiProcedure` (DISPID `0x3`) when solution loaded | Bind on each `OnShown(true)`; failure → log Warning "Vision Edit: solution not bound — …", page stays usable. Placeholder `TextBlock` · `VmPlaceholderText` shows the reason when ActiveX is unavailable. Hide on `OnShown(false)`, disable on `OnModalChanged(true)`. |

### 6.3 Calibration column — always visible part

| ID | WPF | WinUI · x:Name | Label | Core | Rules |
|---|---|---|---|---|---|
| VE-9 | Header Expander | `Expander` (lightweight style: transparent, no border) · `HeaderExpander` | VISION CALIB / body `Vision.HeaderNote` | — | Collapsed by default. |
| VE-10 | `ReadyStatusText` | `TextBlock` · `ReadyText` | `Calib.LastCalibGate.Describe()` or `Vision.ReadyHint` | `McControl.Calib.LastCalibGate` | 11 Secondary. |
| VE-11 | Check calib `CheckCalibCommand` | `Button` · `CheckCalibButton` | Check Calib | `Calib.CheckCalib().Describe()` → VE-12 | Single tap (read-only). |
| VE-12 | `VmStatusText` | `TextBlock` · `GateText` | default `Vision.NotCalibChecked` | — | 11. |
| VE-13 | `SequenceStatusText` | `TextBlock` · `SequenceText` + `ProgressRing` · `BusyRing` | — | progress callbacks | Ring active while Busy (visual only). |
| VE-14 | Tool radio `IsTool1/IsTool2` | `SelectorBar` · `ToolSelector` | Tool 1 · Tool 2 | `SelectedTool` (1/2) | Header "Tool (nozzle)". Change → refresh align/hand-eye status texts. |
| VE-15 | Steps `ComboBox CalibSteps`, `StepIndex` | `ComboBox` · `StepCombo` (items wrap) | Top CAM calib + check (shared) · Bottom CAM calib (per Tool) · Rotation test (per Tool) · Hand-eye (per Tool) · Acceptance (dry run) · Step run (full cycle) | — | Header `Vision.StepsLabel`, tip `Vision.StepsTip`. Shows one step panel (VE-16…VE-61). No step numbers in names (rule 15/09). Items keep index when tool changes. |

### 6.4 Step: Top CAM calib + check (shared) — `StepPanelTop`

| ID | WPF | WinUI · x:Name | Label | Core | Rules |
|---|---|---|---|---|---|
| VE-16 | Note Expander | `Expander` | `Vision.TopDownNoteHeader` / `TopDownNote` | — | Collapsed. |
| VE-17 | Center point text | `TextBlock` · `CenterText` | "Calibration Center Point: X=… Y=… (saved)/(not saved)" | `SysData.TopDownCalibCenterX/Y/Set` | 11. |
| VE-18 | Save (system.json) `SaveCalibrationCenterCommand` | `Button` · `SaveCenterButton` | Save Center | `!OrgOk` → Warn `Vision.NoOriginCoords`; center = X, Y2 pos; `Set=true`; `McDataManager.SaveSystemData()` → Info `CenterSavedOk` / `CenterSavedDiskFail` | Writes disk immediately (page has no Save). Tip `Vision.SaveCenterTip`. |
| VE-19 | Step X / Step Y (mm) | `NumberBox` · `GridStepXBox`, `GridStepYBox` + "mm" | Step X · Step Y | `SysData.TopDownCalibStepXMm/YMm` | Written on commit. |
| VE-20 | Numpad 7 8 9 / 4 5 6 / 1 2 3 | `Grid` 3×3 · `Numpad` (buttons H 48) | 1 … 9 | point n → offset (row = (n−1)/3 − 1, col = (n−1)%3 − 1) × step from center; `Calib.MoveAndTriggerTopDownCalibAsync(x, y, ManualToken)`; fail → Warn `Vision.GridPointFail` | **[2×]**. Pre-check: `!OrgOk` → Warn `NoOriginCalib`; center not set → Warn `NoCenter`. Tips `Vision.GridPointTip`; 5: `GridPoint5Tip`. |
| VE-21 | Auto Calib (S order) | `Button` (Primary) · `AutoCalibButton` | Auto Calib | pre-check as VE-20; **[Confirm]** `Vision.AutoCalibConfirm`; `Calib.ClearCalibPointsAsync(TopDownCalib)` (error → Warn `ClearFail`, status `ClearFailStatus`); for `Routine_Calib.TopDownGridSOrder`: status `AutoCalibPoint`, move+trigger, stop at first failure (Warn `AutoCalibStop`); done → `AutoCalibLoaded`, InvalidateVisionCache, Info `AutoCalibDone` | **[2×]**. Tip `Vision.AutoCalibTip`. |
| VE-22 | Trigger LocateTop (calib) | `Button` · `TopLoadHereButton` | Load Point | `Calib.TriggerTopDownCalibFlowAsync(ManualToken)` → Info `TriggerTopOk` / `TriggerTopNotFound` | Single tap. Tip `Vision.TriggerTopCalibTip` (warns: adds a calib point). |
| VE-23 | Test travel (mm) | `NumberBox` · `VerifyTravelBox` + "mm" | Test travel | `SysData.TopDownVerifyStepMm` | — |
| VE-24 | Top-down check | `Button` · `VerifyTopButton` | Verify Top | `!OrgOk` → Warn; status `VerifyingTopDown`; `Calib.VerifyTopDownAsync(step, ManualToken)`; InvalidateVisionCache; refresh gate; result `TopDownPass`/`TopDownFail` (+ note) → VE-25 + Info/Warn | **[2×]**. Tip `Vision.VerifyTopDownTip`. |
| VE-25 | `VerifyResultText` | `TextBlock` · `VerifyResultTopText` | default `Vision.NotChecked` | — | Shared value with VE-34 (top-down check and rotation test both write it). |

### 6.5 Step: Bottom CAM calib (per Tool) — `StepPanelBottom`

| ID | WPF | WinUI · x:Name | Label | Core | Rules |
|---|---|---|---|---|---|
| VE-26 | Note Expander | `Expander` | `Vision.BottomUpNoteHeader` / `BottomUpNote` | — | — |
| VE-27 | X Ofs (mm) list / A angles (°) list | `TextBox` · `AlignXOffsetsBox`, `AlignAnglesBox` | X Ofs (mm) · A angles (°) | `SysData.AlignCalibXOffsetsMm`, `AlignCalibAnglesDeg` (strings) | Free lists (not NumberBox). |
| VE-28 | Run Align calib | `Button` (Primary) · `RunAlignCalibButton` | Run Calib | `!OrgOk` → Warn `NoOriginCalib`; `Model.AlignPosX(tool)≈0` → Warn `NoAlignPosX`; angles < 3 → Warn `NeedThreeAngles`; **[Confirm]** `AlignSeqConfirm`; `Calib.RunAlignCalibSequenceAsync(tool, alignX, SysData.AlignCalibXOffsets(), AlignCalibAngles(), progress→VE-13, ManualToken)`; ok → `AlignSeqLoaded`, InvalidateVisionCache, Info `AlignSeqDone`; else `StoppedMidway` + Warn `AlignSeqStop` | **[2×]**. Tip `Vision.RunAlignSeqTip`. |
| VE-29 | Trigger AlignBottom (calib, no rot.) | `Button` · `AlignLoadHereButton` | Load Point | `Calib.TriggerAlignCalibFlowAsync(tool, ManualToken)` → Info `TriggerAlignOk` / `TriggerAlignNotFound` | **[2×]** (moves Z to capture height). Tip `Vision.TriggerAlignCalibTip`. |
| VE-30 | A to 0° | `Button` · `AxisAZeroButton` | A → 0° | `!OrgOk` → Warn; `Calib.MoveAlignAxisToZeroAsync(tool)`; fail → Warn `AxisAZeroFail` | **[2×]**. Tip `Vision.AxisAToZeroTip`. |

### 6.6 Step: Rotation test (per Tool) — `StepPanelRotation`

| ID | WPF | WinUI · x:Name | Label | Core | Rules |
|---|---|---|---|---|---|
| VE-31 | Test angle (°) | `NumberBox` · `TestAngleBox` + "°" | Test angle | `SysData.AlignVerifyAngleDeg` | — |
| VE-32 | Rotation test | `Button` (Primary) · `RotationTestButton` | Run Test | `!OrgOk` → Warn; if \|X − AlignPosX\| > 0.05 → **[Confirm]** `RotationMoveXConfirm` then `Func.MoveAsync(X, alignX, SysData.GetAxis(X).MoveSpeed, ManualToken)` (fail → Warn `MoveXToAlignFail`); status `RotationRunning`; `Calib.VerifyAlignRotationAsync(tool, angle)`; InvalidateVisionCache; refresh gate; NG → `RotationFail` + Warn `NothingWritten`; OK → `RotationPass` + Info (+ `Vision.Note`) | **[2×]**. Tip `Vision.RotationTestTip`. |
| VE-33 | `AlignStatusText` | `TextBlock` · `AlignStatusText` | `Vision.AlignNotVerified` / `AlignStatus` (± `Expired`) | `Model.Calib.IsAlignVerified/AlignAngleSign/AlignVerifyResidual/AlignVerifiedPosX/AlignVerifiedAt`, `IsExpired(SysData.CalibValidDays)` | 11. |
| VE-34 | `VerifyResultText` | `TextBlock` · `VerifyResultRotText` | (same value as VE-25) | — | Needed here because VE-25 is hidden. |

### 6.7 Step: Hand-eye (per Tool) — `StepPanelHandEye`

Buttons show a number **badge** (circle 26 px) + ≤ 2-word label; all **[2×]**; result → `HandEyeStepText` (own text, not shared
with step run).

| ID | WPF | WinUI · x:Name | Label | Core | Rules |
|---|---|---|---|---|---|
| VE-35 | Note Expander | `Expander` | `Vision.HandEyeNoteHeader` / `HandEyeNote` | — | — |
| VE-36 | `HandEyeStatusText` | `TextBlock` · `HandEyeStatusText` | `HandEyeOutdated` / `HandEyeNotMeasured` / `HandEyeStatus` | `Model.Calib.IsHandEyeOutdated/IsHandEyeMeasured/HandEyeDx/Dy/MeasuredAt/AngleRef(n)` | 11. |
| VE-37 | 1 · To jig pick pos. | `Button` · `He1Button` | ① Go Jig | `Calib.HandEyeGoToPickAsync(n, ManualToken)` | Tip `Vision.HeGoToPickTip`. |
| VE-38 | 2 · Pick sample can | `He2Button` | ② Pick Can | **[Confirm]** `HePickConfirm`; `Calib.HandEyePickAsync(n)` | Tip `HePickTip`. |
| VE-39 | 3 · Inspect target (LocateTop) | `He3Button` | ③ Locate Target | `Calib.HandEyeLocateAsync(n)` | Tip `HeLocateTip`. |
| VE-40 | 4 · To bottom CAM | `He4Button` | ④ Go Bottom | `Calib.HandEyeGoToAlignAsync(n)` | Tip `HeGoToAlignTip`. |
| VE-41 | 5 · Save hand-eye | `He5Button` | ⑤ Save Hand-eye | `!OrgOk` → Warn; `Calib.HandEyeCommitAsync(n)`; refresh gate + status; fail → `StepStopped` + Warn `HeCommitFail`; ok → `HeCommitLine` + Info `HeCommitDone` | Tip `HeCommitTip`. |
| VE-42 | Re-test Expander | `Expander` | `Vision.HeRetestHeader` / `HeRetestNote` | — | — |
| VE-43 | 6 · Inspect part (label by cell) | `He6Button` | ⑥ Locate Part + caption "(in place)" / "cell (r,c)" (`HeLocateHere` / `HeLocateCell`) | `Calib.HandEyeLocateHereAsync(n, row, col, ManualToken)` (cell latched at press; −1,−1 = in place) | Tip `HeLocateHereTip`. |
| VE-44 | Cell picker button + `Popup` | `Button` · `HeCellButton` + `Flyout` · `HeCellFlyout` (`ShouldConstrainToRootBounds=False`, BottomEdgeAlignedRight) | "Cell…" / "(r,c)" | rebuild on open from `Model.AssyTray.Rows/Cols` (no model → Warn `Vision.NoModel`); keep selection if still inside | Single tap. Flyout title `Vision.HeCellPickerTitle`; cell buttons 48×40 "(r,c)", selected = Accent border 2.5; select closes flyout. Tip `HeCellPickerTip`. |
| VE-45 | In place (no move) | `Button` in flyout · `HeCellInPlaceButton` | In Place | clear cell | Closes flyout. |
| VE-46 | 7 · To bottom CAM | `He7Button` | ⑦ Go Bottom | `Calib.HandEyeGoToAlignForTestAsync(n)` | Tip `HeGoToAlign7Tip`. |
| VE-47 | 8 · Trigger AlignBottom → target | `He8Button` | ⑧ Calc Target | `Calib.HandEyeCheckTargetAsync(n)` | Tip `HeCheckTargetTip`. |
| VE-48 | 9 · To target | `He9Button` | ⑨ Go Target | `Calib.HandEyeGoToTargetAsync(n)` | No dialog (double-tap is the guard). Tip `HeGoToTargetTip`. |
| VE-49 | Return can to jig | `Button` · `HeReturnButton` | Return Can | **[Confirm]** `HeReturnConfirm`; `Calib.HandEyeReturnAsync(n)` | Tip `HeReturnTip`. |
| VE-50 | `HandEyeStepText` | `TextBlock` · `HandEyeStepText` | default `Vision.NotRunYet` | — | 11, selectable. |

### 6.8 Step: Acceptance (dry run) — `StepPanelAccept`

| ID | WPF | WinUI · x:Name | Label | Core | Rules |
|---|---|---|---|---|---|
| VE-51 | Note Expander | `Expander` | `Vision.AcceptHeader` / `AcceptNote` | — | — |
| VE-52 | Dry-run place on mark | `Button` (Primary) · `DryPlaceButton` | Dry Place | `!OrgOk` → Warn; **[Confirm]** `DryRunConfirm`; `Calib.DryRunPlaceAlignAsync(tool)`; VE-12 = `LastCalibGate.Describe()`; Info `DryRunDone` (+ `CmdCheck`) / Warn `DryRunStop` | **[2×]**. Tip `Vision.DryRunPlaceTip`. |

### 6.9 Step: Step run (full cycle) — `StepPanelSequence`

| ID | WPF | WinUI · x:Name | Label | Core | Rules |
|---|---|---|---|---|---|
| VE-53 | Note Expander | `Expander` | `Vision.SeqHeader` / `SeqNote` | — | — |
| VE-54 | Assy row/col, Input row/col (1-based) | `NumberBox` (int, min 1) · `SeqAssyRowBox`, `SeqAssyColBox`, `SeqInputRowBox`, `SeqInputColBox` | Assy cell — row · col · Input cell — row · col | page fields (default 1) | — |
| VE-55 | 1 · CAM to Assy cell | `Button` · `Seq1Button` | ① Go CAM | `Calib.StepMoveCamToCellAsync(r−1, c−1, ManualToken)` | **[2×]**. Tip `Vision.SeqMoveCamTip`. |
| VE-56 | 2 · Trigger LocateTop | `Seq2Button` | ② Locate Top | `Calib.StepLocateTopAsync(r−1, c−1)` | Single tap (no axis move). Tip `SeqLocateTopTip`. |
| VE-57 | 3 · Pick from input cell | `Seq3Button` | ③ Pick | **[Confirm]** `SeqPickConfirm`; `Calib.StepPickAsync(tool, r−1, c−1)` | **[2×]**. Tip `SeqPickTip`. |
| VE-58 | 4 · To bottom CAM | `Seq4Button` | ④ Go Bottom | `Calib.StepMoveToAlignAsync(tool)` | **[2×]**. Tip `SeqMoveToAlignTip`. |
| VE-59 | 5 · Trigger AlignBottom (get Ofs) | `Seq5Button` | ⑤ Align Bottom | `Calib.StepAlignBottomAsync(tool)`; ok with `GuardWarn` → Warn `Vision.GuardWarn` | **[2×]**. Tip `SeqAlignBottomTip`. |
| VE-60 | 6 · To place pt | `Seq6Button` | ⑥ Go Place | `Calib.StepGuardWarn` non-empty → **[Confirm]** `GuardConfirm`; `Calib.StepMoveToCommandAsync(tool)` (Z stays safe, Vac stays on) | **[2×]**. Tip `SeqMoveToCmdTip`. |
| VE-61 | `StepResultText` | `TextBlock` · `StepResultText` | default `Vision.NotRunYet` | — | 11, selectable. |

---

## 7. Alarm (`AlarmPage`) — VM: `ShellViewModel` (inherited DataContext)

```
┌ ACTIVE ALARMS ───────────────────────────────────────────────────────────────────────────────┐
│ Time     Code  Level    Source  Root  Cause present  Message                       Detail     │
│ 10:02:11 1105 [Error]   Safety  Yes   Yes            Light curtain cut …           …          │
│   Cause: …   Remedy: …          (row tapped → expanded)                                      │
│                                                    <Status.Message (red)>   [Mute] [RESET]   │
├ ALARM HISTORY (LAST 200) ────────────────────────────────────────────────────────────────────┤
│ Raised         Cleared        Code  Level  Source  Root  Message                  Detail     │
└───────────────────────────────────────────────────────────────────────────────────────────────┘
```

| ID | WPF | WinUI · x:Name | Label | Core | Rules |
|---|---|---|---|---|---|
| AL-1 | Active `DataGrid ActiveAlarms` + RowDetails | `ui::AlarmTable(Active)` · `ActiveTableHost` | cols Time (HH:mm:ss) 70 · Code 55 · Level 70 (chip) · Source 70 · Root 52 · Cause present 115 · Message * · Detail 180 | `AlarmManager.ActiveAlarms`: `RaisedAt, Code, Level, Source, IsRootCause, ConditionActive, Message, Detail, CauseText, RemedyText` | Card `Alarm.ActiveHeader`, half height. Level chip colour §10.2, white 11. Bools "Yes"/"—". Tap row → expand "Cause: …" (Secondary) / "Remedy: …". Rebuild only on version change; keep expanded rows (key code + raised time). |
| AL-2 | Footer `Status.Message` | `TextBlock` · `AlarmMessageText` | — | `Status.Message` | Critical, wrap, MaxWidth 480. |
| AL-3 | Mute | `Button` · `AlarmMuteButton` 100×40 | Mute | `McControl.RequestMuteBuzzer` | — |
| AL-4 | RESET | `Button` (Primary) · `AlarmResetButton` 120×40 | RESET | `McControl.RequestReset` | — |
| AL-5 | History `DataGrid AlarmHistory` | `ui::AlarmTable(History)` · `HistoryTableHost` | Raised (HH:mm:ss dd/MM) 110 · Cleared 110 · Code · Level · Source · Root · Message * · Detail 180 | `AlarmManager.History` (≤ 200, incl. cleared) | Card `Alarm.HistoryHeader`; same row expansion. |

---

## 8. Button labels — old WPF text → new label (≤ 2 words)

| Page | WPF text | New label | Note |
|---|---|---|---|
| Shell | ─ □ ✕ (FontAwesome) | system caption buttons | — |
| Shell | 🌐 EN / VI | — (removed) | Q1 |
| Shell | Main / Manual / Teaching / Setting / Vision Edit | same | SelectorBar items |
| Shell | ⚠ Alarm | ⚠ Alarm | icon + 1 word |
| Shell | RESET (nav row, panel) | RESET | — |
| Shell | Details / Hide (banner) | Details / ✕ | close = Hide, tooltip kept |
| Shell | Close (alarm panel) | Close | — |
| Dialog | Yes / No / OK | Yes / No / OK | — |
| Main | AUTO / MANUAL (switch) | AUTO / MANUAL | — |
| Main | RUN / STOP | RUN / STOP | — |
| Main | Origin | Origin | — |
| Main | Init | Init | — |
| Main | RESET / Mute | RESET / Mute | — |
| Main | Clear (production) | Clear | — |
| Main | CLEAR TRAY (×2) | Clear Tray | — |
| Main | Clear ({n}) (tray header) | Clear (n) | — |
| Main | ☰ (toolbar) ×3, › / ‹ (log) | icons | tooltips kept |
| Manual | STOP all | STOP | under caption "ALL AXES" |
| Manual | RESET | RESET | — |
| Manual | SVO ON all | Servo On | under "ALL AXES" |
| Manual | Brake off all | Brake Off | under "ALL AXES" |
| Manual | Origin (all) | Origin | — |
| Manual | Bottom-up / Top-down (CAM lamps) | Bottom-up / Top-down | caption "CAM lamps" |
| Manual | SVO off / SVO on (row) | SVO off / SVO on | state text |
| Manual | RST | RST | — |
| Manual | Brake off (row) | Brake Off | — |
| Manual | Origin (row) | Origin | — |
| Manual | − / + (jog, inching) | − / + | — |
| Manual | Move / Stop (row) | Move / Stop | — |
| Manual | Start (sim) | Start | — |
| Manual | Press STOP (sim) | Stop | — |
| Manual | EMG | EMG | — |
| Manual | Cut/clear curtain | Curtain | — |
| Manual | Air pressure | Air | — |
| Manual | In-tray 1 / In-tray 2 | Tray 1 / Tray 2 | — |
| Manual | ALM / Offline (sim faults) | ALM / Offline | — |
| Teaching | Load / Save / Delete | Load / Save / Delete | CommandBar |
| Teaching | New / Copy selected | New / Copy | — |
| Teaching | Input tray 1 / Input tray 2 / Assy tray | Input 1 / Input 2 / Assy | SelectorBar |
| Teaching | Tool 1 / Tool 2 | Tool 1 / Tool 2 | SelectorBar |
| Teaching | Sequential / Zig zag (radio) | Sequential / Zig zag | RadioButtons |
| Teaching | Teach (CAM ref, verify, jig ×3) | Teach | — |
| Teaching | Go top CAM ref | Go Ref | — |
| Teaching | P1 / P2 / P3 | P1 / P2 / P3 | multi-line content |
| Teaching | Apply (size) | Apply | — |
| Teaching | Teach A1 / Teach A2 | Teach A1 / Teach A2 | — |
| Teaching | Teach Z1 / Teach Z2 (×2 sets) | Teach Z1 / Teach Z2 | — |
| Teaching | Teach #1 / Teach #2 | Teach #1 / Teach #2 | — |
| Teaching | Go (jig ×3) | Go | — |
| Teaching | Move (cell) | Move | — |
| Teaching | Z1 Pick / Z2 Place (dynamic) | Z1 Pick / Z2 Place | — |
| Teaching | Move CAM | Move CAM | — |
| Teaching | Set Offset / Clear | Set Offset / Clear | — |
| Teaching | Apply / Clear (common, cell trim) | Apply / Clear | — |
| Teaching | Go T1 Trigger / Go T2 Trigger | Go T1 / Go T2 | — |
| Teaching | Origin (Z rows) | Origin | moves Z to `HomeOffset` |
| Teaching | Go Pick / Go Place | Go Pick / Go Place | — |
| Teaching | Slow / Fast | Slow / Fast | SelectorBar |
| Teaching | Vac #1 / Vac #2 / Blow #1 / Blow #2 | same | — |
| Teaching | CAM BTM-UP LIGHT / CAM TOP-DOWN LIGHT | Btm Light / Top Light | — |
| Teaching | Save (big) | Save | — |
| Setting | Both nozzles / Nozzle #1 / Nozzle #2 | Both / Nozzle #1 / Nozzle #2 | — |
| Setting | Dry run: OFF / DRY RUN: ON | Dry Run | state by colour + caption |
| Setting | Browse... | Browse | — |
| Setting | Reload solution | Reload | — |
| Setting | Check vars | Check Vars | — |
| Setting | Create missing vars | Create Vars | — |
| Setting | Check outputs | Check Outputs | — |
| Setting | Save system.json | Save System | — |
| Setting | Open data folder | Open Folder | — |
| Vision | − / + (jog ×6) | − / + | — |
| Vision | Z1 → Z_SAFE / Z2 → Z_SAFE | Z1 Safe / Z2 Safe | — |
| Vision | Vac #1 / Vac #2 | same | — |
| Vision | Slow / Fast | Slow / Fast | — |
| Vision | STOP | STOP | — |
| Vision | Check calib | Check Calib | — |
| Vision | Tool 1 / Tool 2 (radio) | Tool 1 / Tool 2 | SelectorBar |
| Vision | Save (system.json) | Save Center | — |
| Vision | 1 … 9 (numpad) | 1 … 9 | — |
| Vision | Auto Calib (S order) | Auto Calib | — |
| Vision | Trigger LocateTop (calib) | Load Point | tooltip keeps warning |
| Vision | Top-down check | Verify Top | — |
| Vision | Run Align calib | Run Calib | — |
| Vision | Trigger AlignBottom (calib, no rot.) | Load Point | — |
| Vision | A to 0° | A → 0° | — |
| Vision | Rotation test | Run Test | — |
| Vision | 1 · To jig pick pos. | ① Go Jig | badge |
| Vision | 2 · Pick sample can | ② Pick Can | — |
| Vision | 3 · Inspect target (LocateTop) | ③ Locate Target | — |
| Vision | 4 · To bottom CAM | ④ Go Bottom | — |
| Vision | 5 · Save hand-eye | ⑤ Save Hand-eye | — |
| Vision | 6 · Inspect part (in place / cell r,c) | ⑥ Locate Part | cell shown as caption |
| Vision | Cell… / (r,c) | Cell… / (r,c) | — |
| Vision | In place (no move) | In Place | — |
| Vision | 7 · To bottom CAM | ⑦ Go Bottom | — |
| Vision | 8 · Trigger AlignBottom → target | ⑧ Calc Target | — |
| Vision | 9 · To target | ⑨ Go Target | — |
| Vision | Return can to jig | Return Can | — |
| Vision | Dry-run place on mark | Dry Place | — |
| Vision | 1 · CAM to Assy cell | ① Go CAM | — |
| Vision | 2 · Trigger LocateTop | ② Locate Top | — |
| Vision | 3 · Pick from input cell | ③ Pick | — |
| Vision | 4 · To bottom CAM | ④ Go Bottom | — |
| Vision | 5 · Trigger AlignBottom (get Ofs) | ⑤ Align Bottom | — |
| Vision | 6 · To place pt | ⑥ Go Place | — |
| Alarm | Mute / RESET | Mute / RESET | — |

---

## 9. Shared components (API sketch, namespace `ui`)

### 9.1 `TrayCard` / `TrayMap` (Main)

```cpp
enum class TrayCellLook { Empty, Scanned, Loaded, Processing, Ok, Ng, Skipped };   // = WPF ETrayCellLook
class TrayCard {
public:
    // slots: 2 = Input (Tray 1 above Tray 2), 1 = Assy. input: which McControl selection API to call.
    FrameworkElement Build(std::wstring title, int slots, bool input);
    // Rebuilds cells only when RowsPerTray/Cols/ScanOrder/slot count change; else updates state per cell.
    void Sync(const sca::TrayState& tray, const std::vector<int>* scanned, bool canSelect);
    void SetCounts(int ok, int ng);                 // Total = ok + ng
    std::function<void(int index)> OnCellTapped;    // → McControl::ToggleCellSelection(input, index)
    std::function<void()> OnClearSelection;         // → McControl::ClearCellSelection(input)
    std::function<void()> OnReload;                 // → ReloadInputTray / ResetAssyTray
};
```
Cell = `Grid{ Border(fill, elevation stroke, radius 4), TextBlock "r-c", Border(selection overlay 2 px) }`; glow via `ui::SetGlow`.
Cell index = `tray.IndexOf(slot*rows + r, c)` (geometry ≠ visit order).

### 9.2 `ManualAxisRow` (Manual, from Slit `ManualPageParts`)

```cpp
class ManualAxisRow : public std::enable_shared_from_this<ManualAxisRow> {
public:
    static std::shared_ptr<ManualAxisRow> Create(std::shared_ptr<sca::IAxis> axis, ManualLampBrushes const&);
    FrameworkElement Root() const;
    void Update();                      // OnTick: pos, POT/NOT, ServoOn, Alarm, OrgOk, brake state; writes on change
    void SetBrakeReleased(bool doOn);   // from Io.GetDo(DoBrakeReleaseZ1/Z2) (page reads IO once per tick)
private:
    void RunBlocking(std::function<void()> work);   // row busy-lock
    // + brake hold state: m_holding, m_holdStart, m_emergencyArming, m_emergencyArmed, DispatcherQueueTimer 100 ms
};
```
Column widths fixed (§3.2) and shared with the header row.

### 9.3 `JogRow` (Teaching, Vision Edit)

```cpp
struct JogRowOptions { bool showPosition = true; double buttonHeight = 36; double buttonWidth = 36; };
FrameworkElement BuildJogRow(std::string axisName, JogRowOptions, /*out*/ TextBlock& posText);
// − / + use ui::AttachJog(btn, axis, ∓1). Extra buttons (Go T1, Origin, Go Pick…) are appended by the page.
```

### 9.4 `IoLampList` + `ManualLamp`

```cpp
class IoLampList {
public:
    FrameworkElement Build(bool inputs, int count, std::function<std::string(int)> name);  // "DI 0x%02X  name"
    void Update(std::function<bool(int bit)> read);   // ManualLamp::Set per bit, only on change
};
```
`ManualLamp` (existing): 10–11 px dot, off LightGray, on Critical + Composition glow `#FF5C5C` blur 6.

### 9.5 `CamFrame` + `ReadoutTable` (Main)

```cpp
class CamFrame {
public:
    FrameworkElement Build(std::wstring title, bool toggleableReadout);
    void SetReadout(const sca::ReadoutCard* card, std::string fullText);   // null → "Not triggered"
    void SetImage(std::shared_ptr<const sca::ImageData> img);               // convert only when pointer changes; sets "Shot hh:mm:ss"
    // VisionMaster (called from queued VM work only):
    bool BindModule(std::string const& path, int loadCount);               // key = loadCount|path; enable result callbacks
    void SetToolbarVisible(bool);  void SetVisible(bool);  void SetEnabled(bool);  void Dispose();
    std::function<void()> OnReadoutHeaderTapped;                            // toggle breakdown (Tool cards)
private:
    VmPane m_render;   // vmkind::Render, SetBackground "#F1F1F1"
};
```

### 9.6 Guards

```cpp
void AttachDoubleTap(Button const&, std::function<void()> fn);          // DoubleTapped only; no Click
void AttachJog(Button const&, std::string axis, int dir);                // existing
// Hold with pointer capture: start on PointerPressed (CapturePointer), end on PointerReleased / PointerCaptureLost / PointerCanceled.
// "end" fires only if a press actually started (hover never releases the brake).
void AttachHold(Button const&, std::function<void()> onStart, std::function<void()> onEnd);
```

### 9.7 Numeric input with unit

```cpp
// NumberBox (SpinButtonPlacementMode Hidden, AcceptsExpression false, ValidationMode InvalidInputOverwritten,
// NumberFormatter: IntegerDigits 1, FractionDigits 0..3 as needed) + unit TextBlock right ("mm", "ms", "°").
StackPanel UnitNumber(NumberBox const&, wchar_t const* unit);
void FieldGroup::BindNumber(NumberBox const&, std::string label, std::function<double()> get,
                            std::function<void(double)> set, bool optional = false);   // NaN ↔ nullopt when optional
```
Do not set Minimum/Maximum where WPF validates with a message (trims) — NumberBox would clamp silently.

### 9.8 Tables and lists

- `AlarmTable` (existing): `Build(Active|History)`, `Update(alarms)`; rows code-built, tap toggles Cause/Remedy, expanded keys kept.
- `settingpage::SimpleTable`, `CellBox`, `LogTable` (existing) for Setting servo table and log tab.
- `LogListView` (Main): `Build()` → `ListView` (SelectionMode None); `Update()` prepends new `LogWorker.LogList` entries (version
  check), caps at the LogWorker capacity, keeps top in view.

### 9.9 Shell helpers (existing, keep)

`ui::Dialogs` / `ModalOverlay` (windowed popup, ContentDialog look, stacking), `VmPane` (+ `QueueVmWork`), `SetTint(+glow flag)`,
`SetGlow`, `SetText`, `Show`, `RunBackground(ThenUi)`, `PostUi`, `PostMotionCommand`, `StopActiveJogs`, `StatusColors.h`
(`AlarmLevelColor`, `TowerLampColor`, `TowerLampTintColor`).

---

## 10. Theme

### 10.1 Rules

- Three layers as WPF/Slit: **background** (acrylic + `#99F3F3F3` tint) → **card** (translucent white, 1 px stroke, radius 8,
  padding 12, no card-in-card; groups inside a card separated by 1 px dividers) → **control** (solid white, elevation stroke,
  radius 4).
- Keys = Slit `Styles.xaml` names (already in the tree); **values = WPF `Palette.xaml`/`Controls.xaml`** where they differ (this app
  must look like today's Assy HMI). Deltas listed; see Q3.
- Light theme only. No dark tokens.

### 10.2 Colour tokens

| WPF key | WPF value | WinUI key | Use value | Slit value (delta) |
|---|---|---|---|---|
| SolidBackgroundFillColorBase / BackgroundBrush | `#F3F3F3` | `BackgroundBrush` | `#FFF3F3F3` | same |
| BackgroundSecondaryBrush | `#EEEEEE` | `BackgroundSecondaryBrush` | `#FFEEEEEE` | same |
| SurfaceTranslucentBrush (card) | `#B3FFFFFF` | `CardBrush` | `#B3FFFFFF` | `#80FFFFFF` |
| SurfaceBrush (dialogs) | `#FFFFFF` | `SurfaceBrush` | `#FFFFFFFF` | same |
| CardStrokeBrush | `#33000000` | `CardStrokeBrush` | `#33000000` | `#1A000000` |
| DividerBrush | `#1A000000` | `DividerBrush` | `#1A000000` | same |
| ButtonFillBrush | `#FFFFFF` | `ControlFillBrush` | `#FFFFFFFF` | same |
| ButtonElevationStrokeBrush | `#E0E0E0`→`#C4C4C4` (0.92–1) | `ControlElevationStrokeBrush` | same gradient | same |
| ControlStrokeBrush / Secondary | `#0F000000` / `#29000000` | `ControlStrokeBrush` / `ControlStrokeSecondaryBrush` | WPF values | `#E0E0E0` / `#C4C4C4` |
| SubtleHover / Pressed | `#09000000` / `#06000000` | `SubtleHoverBrush` / `SubtlePressedBrush` | WPF values | `#0A` / `#06` |
| AccentFillBrush / AccentBrush | `#0078D4` | `AccentFillBrush`, `AccentBrush` | `#FF0078D4` | `#005FB8` |
| AccentPressedBrush | `#005A9E` | `AccentPressedBrush` (add) | `#FF005A9E` | — |
| TextPrimary / Secondary / Tertiary / Disabled | `#E4000000` / `#9E000000` / `#72000000` / `#5C000000` | same keys | same | same |
| SuccessBrush / CautionBrush / CriticalBrush | `#0F7B0F` / `#9D5D00` / `#C42B1C` | same | same | same |
| StatusOffBrush | `#5C5C5C` | same | same | same |
| GreenOn / RedOn / CautionOn / BlueOn / LimeOn | `#198754` / `#DC3545` / `#D35400` / `#0078D4` / `#5E9E10` | same keys | WPF values | RedOn `#C42B1C`, BlueOn `#005FB8` |
| Glow (button) | Green `#22C55E`, Red `#FF4D5E`, Caution `#FF7A1A`, Blue `#3AA0FF`, Lime `#84CC16`; blur 16, opacity 0.75 | `ui::SetTint` glow table | same | — |
| Lamp glow | `#FF5C5C`, blur 6 | `ManualLamp` | same | same |
| CustomLightGreen / DarkGreen / LightRed / LightBlue | `#86C691` / `#389853` / `#E94949` / `#4E6D9C` | same | same | same |
| CustomLightYellowBrush (notice) | `#FFFFC0` | (replaced by `InfoBar` Warning `#FFF4CE`) | system | `#FFF4CE` |
| NotUse / Bypass / VisionOk | `#464646` / `#A5A5A5` / `#C8EBC8` | same | same | same |
| LaserOk / LaserNg | `#78E600` / `#B61616` | — (unused) | — | — |
| TrayCellEmpty / Scanned / Loaded / Processing / Ok / Ng / Skipped | `#FFFFFF` / `#CFE3F7` / `#CFE3F7` / `#FFE080` / `#86C691` / `#E94949` / `#A5A5A5` | `TrayCell*Brush` (add `TrayCellScannedBrush`) | same | Scanned missing |
| Tray cell stroke | `#0F000000`→`#29000000` bottom | code | same | — |
| Tray current glow | `#0078D4`, blur 9, opacity 0.9 | `SetGlow` | same | — |
| Mode track | `#0F000000` / hover `#17000000` / pressed `#1F000000` | `ModeTrack*Brush` (page) | same | — |
| Tower lamp | Green `#389853`, Red `#C42B1C`, Yellow `#9D5D00`, off `#606060`; tint alpha `0x1C` | `StatusColors.h` | same | same |
| Tower bulb (unused in WPF view) | R `#E94949` Y `#FFC800` G `#389853` off `#3A3A3A` | `TowerBulbColor` | — | — |
| Alarm level | Fatal `#C42B1C`, Error `#E94949`, Warning `#9D5D00`, other `#606060` | `AlarmLevelColor` | same | same |
| Log level | Info bar `#0078D4`; Warning bar `#E8A317` text `#9D5D00`; Error bar `#E94949` text `#C42B1C` SemiBold; Debug bar `#A0A0A0` text `#707070` | code | same | same |
| Dialog kind | Info `#0F7B0F`, Warn `#9D5D00`, Error `#C42B1C`, Confirm `#0078D4` | `Dialogs` | same | — |
| Sim badge | bg `#FFC800`, text `#5A3D00` | `SimBadge` | same | Caution bg |
| Alarm banner | `#F0C42B1C`, 1 px white, shadow 28 / 0.5 | `AlarmBanner` | same | `CriticalBrush` |
| Modal smoke | `#60000000` | `SmokeBrush` | `#60000000` | `#4D000000` |
| VM render background | `#F1F1F1` (SDK solid only) | `VmPane` | same | `#F9F9F9` holder |

### 10.3 Fonts

| Use | Font | Size / weight |
|---|---|---|
| All UI | **Segoe UI** (not "Segoe UI Variable"; set `ContentControlThemeFontFamily`) | — |
| Icons | `Segoe Fluent Icons, Segoe MDL2 Assets` (Win10 has only MDL2; use glyphs present in both) | 14–16 |
| Numbers (positions, raw IO, diagnostics, log time, P1–P3 coords) | `Consolas` (Cascadia not on Win10 by default) | 10–15 |
| Machine name | Segoe UI | 20 Bold |
| Nav items | Segoe UI | 15 SemiBold |
| Card header (uppercase) | Segoe UI | 13–14 SemiBold |
| Buttons | Segoe UI | 14 (primary ops 16–18 Bold) |
| Inputs | Segoe UI | 13 |
| Captions / hints | Segoe UI | 11–12 Secondary |
| Big counters | Segoe UI | 26 Bold (card counters 16 Bold) |

Glyph map (FontAwesome → Segoe): globe `E774` (unused), log hide `E76C`, log show `E76B`, toolbar `E700`, warning `E7BA`,
info `E946`, error `EA39`, question `E9CE`.

### 10.4 Sizes (touch)

| Item | Size |
|---|---|
| Default button | MinHeight **40** (WPF 36), padding 11,5, radius 4 |
| Primary operation buttons (Main RUN/STOP) | H 52; Origin/Init/RESET/Mute H 40 |
| Compact exceptions (keep WPF density) | Teaching jog row 36; tray header "Clear (n)" 28 (header height must not change); CAM toolbar toggle 32×28; log tab 24×56 |
| SelectorBar item (nav) | MinHeight 48 |
| SelectorBar item (in-page) | MinHeight 40 |
| NumberBox / TextBox | MinHeight 36 |
| ToggleSwitch row | MinHeight 40 |
| Cells | Main tray min 30×34 (8-column assy tray); Teaching cell min 40×40; Vision cell picker 48×40; numpad H 48 |
| Lamps | status 12, IO 11, POT/NOT 10 |
| Gutters / card padding / radius | 8 / 12 / 8 |
| Title bar / nav / status bar | 64 / 48 / 28 |

---

## 11. Open questions (need the user's choice)

| # | Question | Default in this spec |
|---|---|---|
| Q1 | WPF has a VI/EN toggle in the title bar; PORTING §3 says English UI. Drop the toggle? | Dropped. |
| Q2 | Title bar: WPF tints the **whole header** with the tower-lamp colour; Slit uses a small state pill with `Status.StateText` (text the Assy WPF never shows). | WPF whole-header tint, no state text. |
| Q3 | Chrome colours: Assy WPF (accent `#0078D4`, card 70 %, stroke 20 %, RedOn `#DC3545`) vs Slit (accent `#005FB8`, card 50 %, stroke 10 %, RedOn `#C42B1C`). | Assy WPF values. |
| Q4 | Touch minimum 40 px vs WPF/Slit 36 px — accept slightly taller rows (Manual axis table, Setting rows)? | 40, with listed compact exceptions. |
| Q5 | Vision Edit step selector: keep `ComboBox` (WPF) or a 6-chip step strip like Slit (needs very short step names, wider column)? | ComboBox, column 320 px. |
| Q6 | Destructive actions without confirmation in WPF (Clear counters, Clear Tray ×2) — keep as is? | Kept without confirmation (no new behaviour). |
| Q7 | ToolTips near VisionMaster panes (Main CAM headers, Vision Edit) may render under the VM popups if WinUI tooltips are not windowed — verify on the machine; if hidden, move that info into visible text. | Keep tooltips; verify. |
