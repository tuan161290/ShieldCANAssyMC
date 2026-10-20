# Brief for page agents (2026-10-18)

You port ONE page of the WPF HMI to the WinUI 3 C++ app. Read first: `PORTING.md` (§3 English, buttons ≤ 2 words; §6 safety),
`UI-SPEC.md` (your page's section + shell + shared components + theme), `CONTROLLER-NOTES.md`.

## State of the tree
- Core is complete and tested (CoreTests 2056/2056). Read the headers in `Core\` for the real API — never guess.
- The shell is done: `App\MainWindow.*`, `App\Ui\*` (Page.h contract, UiUtil, JogControl, Controls, TrayCard, Dialogs, VmPane,
  AlarmTable, SettingPageUi), `Styles.xaml`. (ManualPageParts của Slit đã xoá 2026-10-18 — trang Manual dùng `Ui\ManualPageAxisRow`.) Your page currently is a placeholder implementing `ui::IPage`.
- Page lifecycle (`Ui\Page.h`): `Loaded` may run before Core Init → build controls only. `OnCoreReady()` once → from then
  McControl::Io/Vision/Func/Safety/AutoRoutine/DryRoutine/Calib may be used (null-check: Init can fail). `OnShown`, `OnTick` (100 ms,
  only for the visible page; set control properties only when the value changed — ui::SetText/SetEnabled/Show), `OnModalChanged`,
  `OnWindowMoved`, `OnClosing`.
- Blocking Core calls: `ui::RunBackground` / `ui::RunBackgroundThenUi`. Jog/IO: `ui::PostMotionCommand`. Dialogs: `ui::Dialogs::*`.
  VisionMaster panes: `ui::VmPane` + `ui::QueueVmWork`.

## Rules
- Edit ONLY your page files (`App\<Page>.xaml`, `.xaml.h`, `.xaml.cpp`, `.idl`) and new files you create named `App\Ui\<Page>*.h/.cpp`.
  Do NOT edit MainWindow, Styles.xaml, other pages, shared Ui files or Core. Need a shared change or a new style/brush? Put local
  resources in your page's `<Page.Resources>` and list the request in your report.
- Port every function of the WPF view + view model (UI-SPEC lists them). Keep guards (double-tap, hold, confirm, busy locks).
- Build a PRIVATE copy (other agents build at the same time):
  ```
  $w = "<scratch>\pages\<page>"   # <scratch> = C:\Users\Hoyeon\AppData\Local\Temp\claude\c--PGM-ShieldCANAssyMC\b452042b-fdc6-4106-b769-ed6a901f786c\scratchpad
  robocopy C:\PGM\ShieldCANAssyMC\ShieldCanAssy.WinUI\App $w\App /E /XD obj "Generated Files"   # first time / to refresh
  & "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" $w\App\ShieldCanAssy.vcxproj -restore `
     -p:Configuration=Release -p:Platform=x64 -p:ScaRoot=C:\PGM\ShieldCANAssyMC\ShieldCanAssy.WinUI -p:ScaBuildDir=$w\build `
     -p:ScaCoreLib=C:\PGM\ShieldCANAssyMC\ShieldCanAssy.WinUI\build\x64\Release\lib\Core.lib -p:BuildProjectReferences=false -m -nologo -v:m
  ```
  Edit in your copy, then copy ONLY your page files back into the main tree's `App\` when done.
- Screenshot check (optional but recommended): only if `Get-Process ShieldCanAssy.Wpf,vServerApp,ShieldCanAssy -ErrorAction SilentlyContinue`
  returns nothing, run `$w\build\x64\Release\App\ShieldCanAssy.exe --root $w\data --shot $w\png` (simulation forced, no VisionMaster),
  wait for exit (kill after 120 s), and LOOK at your page's PNG. Never run the exe without `--shot`.
- Safety: production assembly machine. Never touch the WPF app/bin/_RunData, hardware, COM ports, VisionMaster. No git.

## Report
Concise: what's ported (map to UI-SPEC rows), anything not ported and why, shared-file change requests, build status, screenshot notes.
