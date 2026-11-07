#include "pch.h"
#include "MainPage.xaml.h"
#if __has_include("MainPage.g.cpp")
#include "MainPage.g.cpp"
#endif

#include <algorithm>
#include <cmath>
#include <format>

#include "Base/Text.h"
#include "Communication/VisionConnect.h"
#include "Controller/McControl.h"
#include "Controller/Routine_Auto.h"
#include "Controller/Routine_DryCycle.h"
#include "Controller/VisionReadout.h"
#include "Data/McDataManager.h"
#include "Data/Model/ModelData.h"
#include "Data/RunningData.h"
#include "Data/SystemData.h"
#include "Infor/AlarmManager.h"
#include "Lang/Loc.h"
#include "Ui/AppHost.h"
#include "Ui/Dialogs.h"
#include "Ui/JogControl.h"
#include "Ui/UiUtil.h"
#include "Utility/Log/LogWorker.h"
#include "Vision/VisionMasterDevice.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Media;
using namespace winrt::Microsoft::UI::Xaml::Media::Animation;
namespace WF = winrt::Windows::Foundation;

namespace
{
    constexpr const char* Dash = "\xE2\x80\x94";   // "—" = chưa có số

    /// <summary>Sec của MainViewModel: "0.00 s".</summary>
    std::string Sec(double sec) { return std::format("{:.2f} s", sec); }

    /// <summary>Clock của MainViewModel: làm tròn XUỐNG 0.1 s, "mm:ss.f" (dưới 1 giờ) hoặc "h:mm:ss.f".</summary>
    std::string Clock(double sec)
    {
        const long long tenths = (long long)std::floor(std::max(0.0, sec) * 10.0);
        const long long h = tenths / 36000, m = tenths / 600 % 60, s = tenths / 10 % 60, f = tenths % 10;
        return h >= 1 ? std::format("{}:{:02}:{:02}.{}", h, m, s, f) : std::format("{:02}:{:02}.{}", m, s, f);
    }
}

namespace winrt::ShieldCanAssy::implementation
{
    // ══════════ Dựng ══════════

    void MainPage::OnRootLoaded(WF::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_loaded) return;
        m_loaded = true;

        // Tooltip lấy chữ resx (UI-SPEC §0.2).
        ToolTipService::SetToolTip(RunButton(), box_value(ui::T("Main.RunTip")));
        ToolTipService::SetToolTip(InitButton(), box_value(ui::T("Main.InitTip")));
        ToolTipService::SetToolTip(NgStreakText(), box_value(ui::T("Main.NgStreakTip")));
        ToolTipService::SetToolTip(CycleTimeCell(), box_value(ui::T("Main.CtTrayCurrentTip")));
        ToolTipService::SetToolTip(LastCycleTimeCell(), box_value(ui::T("Main.CtTrayPreviousTip")));
        ToolTipService::SetToolTip(TackTimeCell(), box_value(ui::T("Main.CtAvgCurrentTip")));
        ToolTipService::SetToolTip(LastTackTimeCell(), box_value(ui::T("Main.CtAvgPreviousTip")));
        ToolTipService::SetToolTip(UphText(), box_value(ui::T("Main.UphTip")));
        ToolTipService::SetToolTip(LastCompText(), box_value(ui::T("Main.LastCompensationTip")));
        ToolTipService::SetToolTip(LogHideButton(), box_value(ui::T("Main.HideLog")));
        ToolTipService::SetToolTip(LogShowButton(), box_value(ui::T("Main.ShowLog")));

        // MA-5 [2×]: nút làm trục chạy phải bấm đúp (quy ước 2026-09-26) — không có Click.
        ui::AttachDoubleTap(InitButton(), [] { sca::McControl::RequestInit(); });

        // RUN lúc Armed: xanh NHẤP NHÁY 1 Hz — Opacity rời rạc 1 (0,5 s đầu) / 0,3 (0,5 s sau), như ArmedBlink của WPF.
        {
            DoubleAnimationUsingKeyFrames blink;
            blink.Duration(DurationHelper::FromTimeSpan(std::chrono::seconds(1)));
            DiscreteDoubleKeyFrame on;
            on.KeyTime(KeyTimeHelper::FromTimeSpan(std::chrono::milliseconds(0)));
            on.Value(1.0);
            DiscreteDoubleKeyFrame dim;
            dim.KeyTime(KeyTimeHelper::FromTimeSpan(std::chrono::milliseconds(500)));
            dim.Value(0.3);
            blink.KeyFrames().Append(on);
            blink.KeyFrames().Append(dim);
            Storyboard::SetTarget(blink, RunButton());
            Storyboard::SetTargetProperty(blink, L"Opacity");
            m_armedBlink = Storyboard();
            m_armedBlink.Children().Append(blink);
            m_armedBlink.RepeatBehavior(RepeatBehaviorHelper::Forever());
        }
        // Con trượt AUTO/MANUAL: 150 ms CubicEase-out.
        {
            m_modeSlideX = DoubleAnimation();
            m_modeSlideX.Duration(DurationHelper::FromTimeSpan(std::chrono::milliseconds(150)));
            CubicEase ease;
            ease.EasingMode(EasingMode::EaseOut);
            m_modeSlideX.EasingFunction(ease);
            Storyboard::SetTarget(m_modeSlideX, ModeThumbTransform());
            Storyboard::SetTargetProperty(m_modeSlideX, L"X");
            m_modeSlide = Storyboard();
            m_modeSlide.Children().Append(m_modeSlideX);
        }

        // ── Hai card tray (TrayGridViewModel). Bấm ô lúc AUTO = chọn/bỏ chọn, rồi đồng bộ lại ngay như WPF. ──
        InputTrayHost().Child(m_inputCard.Build(std::wstring(ui::T("Main.InputTray")), sca::ModelData::InputTraySlots));
        AssyTrayHost().Child(m_assyCard.Build(std::wstring(ui::T("Main.AssyTray")), 1));
        m_inputCard.OnCellTapped = [this](int i) { sca::McControl::ToggleCellSelection(true, i); SyncTray(true); };
        m_assyCard.OnCellTapped = [this](int i) { sca::McControl::ToggleCellSelection(false, i); SyncTray(false); };
        // Nhấn giữ một ô rồi kéo = chọn / bỏ chọn cả vùng (Tuấn 2026-10-30).
        m_inputCard.OnCellsDragged = [this](auto const& cells, bool on) { sca::McControl::SetCellsSelected(true, cells, on); SyncTray(true); };
        m_assyCard.OnCellsDragged = [this](auto const& cells, bool on) { sca::McControl::SetCellsSelected(false, cells, on); SyncTray(false); };
        m_inputCard.CanSelectCell = [](int i) { return sca::McControl::CanSelectCell(true, i); };
        m_assyCard.CanSelectCell = [](int i) { return sca::McControl::CanSelectCell(false, i); };
        m_inputCard.OnClearSelection = [this] { sca::McControl::ClearCellSelection(true); SyncTray(true); };
        m_assyCard.OnClearSelection = [this] { sca::McControl::ClearCellSelection(false); SyncTray(false); };
        // Tray 1, và Tray 2 nếu đã teach, thành có hàng — McControl::ReloadInputTray.
        m_inputCard.OnReload = [this] { sca::McControl::ReloadInputTray(); if (m_coreReady) OnTick(); };
        // Mọi ô thành trống, bỏ số đo quét trước, chu trình làm lại từ S00 — McControl::ResetAssyTray.
        m_assyCard.OnReload = [this] { sca::McControl::ResetAssyTray(); if (m_coreReady) OnTick(); };

        // ── Ba khung CAM ──
        m_cams.Build(Tool1CamHost(), Tool2CamHost(), TopCamHost());

        // ── Log ──
        LogListHost().Child(m_log.Build());
        m_log.Update();

        if (m_coreReady) OnTick();
    }

    void MainPage::OnCoreReady()
    {
        m_coreReady = true;
        m_cams.SetCoreReady();
        if (m_loaded) OnTick();
    }

    // ══════════ Cập nhật theo nhịp (100 ms, chỉ khi trang đang hiện) ══════════

    void MainPage::OnTick()
    {
        if (!m_loaded || m_closing) return;
        if (ui::AppHost::Options().ShotMode()) m_cams.InjectShotSample();

        UpdateOperation();
        UpdateCounters();
        RefreshAssyTrayClock();
        UpdateTrays();
        m_cams.Update();
        m_log.Update();
    }

    void MainPage::UpdateOperation()
    {
        sca::McStatus& status = sca::McControl::Status();
        const sca::EMcState state = status.State();

        // MA-1: con trượt + chữ theo chế độ THẬT.
        const int mode = (int)status.Mode();
        if (mode != m_modeShown)
        {
            const bool animate = m_modeShown != -1;
            m_modeShown = mode;
            const bool isAuto = mode == (int)sca::EMcMode::Auto;
            ModeThumb().Background(ui::Res(isAuto ? L"GreenOnBrush" : L"CautionOnBrush"));
            ModeAutoText().Foreground(ui::Res(isAuto ? L"TextOnAccentBrush" : L"TextSecondaryBrush"));
            ModeManualText().Foreground(ui::Res(isAuto ? L"TextSecondaryBrush" : L"TextOnAccentBrush"));
            PlaceModeThumb(animate);
        }

        UpdateRunButton((int)state);

        // MA-3: STOP đỏ khi máy không chạy — chờ nạp tray (WaitOperator) vẫn là đang chạy Auto (Tuấn 01/10).
        const bool isStopped = state != sca::EMcState::Running && state != sca::EMcState::Pausing &&
                               state != sca::EMcState::WaitOperator;
        ui::SetTint(StopButton(), isStopped ? ui::Tint::Red : ui::Tint::None, false);
        // MA-4: xanh dương khi cả 7 trục đã về gốc (trạng thái nghỉ — không quầng).
        ui::SetTint(OriginButton(), status.OrgOk() ? ui::Tint::Blue : ui::Tint::None, false);
        // MA-5: cam + quầng khi bắt buộc Init trước khi chạy tiếp (dừng giữa hành trình).
        auto run = sca::McDataManager::RunData();
        ui::SetTint(InitButton(), run && run->UnsafePause ? ui::Tint::Caution : ui::Tint::None, true);
        // MA-6: đỏ + quầng khi còn alarm.
        ui::SetTint(ResetButton(), sca::AlarmManager::ActiveCount() > 0 ? ui::Tint::Red : ui::Tint::None, true);

        // MA-8
        auto sys = sca::McDataManager::SysData();
        const int onlyNozzle = sys ? sys->OnlyNozzle : 0;
        ui::Show(OnlyNozzleBar(), onlyNozzle != 0);
        ui::SetText(OnlyNozzleText(), sca::Loc::F("Main.OnlyNozzle", onlyNozzle));

        // MA-9: chạy rà — tiến độ của Routine_DryCycle.
        ui::Show(DryRunBar(), status.DryRun());
        ui::Show(NgStopBar(), status.StopOnVisionNg());
        if (status.DryRun())
        {
            auto dry = sca::McControl::DryRoutine();
            std::string text;
            if (!dry || (dry->Strokes() == 0 && dry->Elapsed() < std::chrono::seconds(1)))
                text = sca::Loc::T("Main.DryRunIdle");
            else
                text = sca::Loc::F("Main.DryRunProgress", dry->Strokes(), dry->Sweeps(), dry->Laps(),
                                   std::chrono::duration<double>(dry->Elapsed()).count() / 60.0);
            ui::SetText(DryRunText(), text);
        }

        // MA-10 / MA-11 — một ô cao cố định (NoticeSlot): ẩn/hiện dòng chờ tray BÊN TRONG ô không đổi chiều cao ô.
        const bool waitTray = state == sca::EMcState::WaitOperator;
        const std::string message = status.Message();
        ui::Show(WaitTrayText(), waitTray);
        const int messageLines = waitTray ? 2 : 4;
        if (MessageText().MaxLines() != messageLines) MessageText().MaxLines(messageLines);
        ui::SetClampedText(MessageText(), message, NoticeSlot());
        ui::ShowInPlace(NoticeSlot(), waitTray || !::base::IsBlank(message));

        // MA-18 / MA-19
        ui::SetText(AutoStepText(), sca::Loc::T("Main.StepPrefix") + status.AutoStepText());
        ui::SetText(LastCompText(), sca::Loc::F("Main.LastCompensation", status.LastDeltaTheta(), status.LastPlaceError()));
    }

    // MA-2: chữ luôn RUN, MÀU báo trạng thái — xám khi đứng, xanh nhấp nháy khi Armed (không quầng), xanh + quầng khi
    // Running. Bấm = nút Start cứng (OnRunClick).
    void MainPage::UpdateRunButton(int stateValue)
    {
        const auto state = (sca::EMcState)stateValue;
        const int visual = state == sca::EMcState::Running ? 2 : state == sca::EMcState::Armed ? 1 : 0;
        if (visual == m_runVisual) return;
        m_runVisual = visual;

        auto button = RunButton();
        if (visual == 2)
        {
            ui::SetTint(button, ui::Tint::Green, true);
        }
        else
        {
            ui::SetTint(button, ui::Tint::None);   // gỡ quầng nếu vừa ở trạng thái chạy
            Brush fill = ui::Res(visual == 1 ? L"GreenOnBrush" : L"StatusOffBrush");
            button.Background(fill);
            button.BorderBrush(fill);
            button.Foreground(ui::Res(L"TextOnAccentBrush"));
        }

        if (!m_armedBlink) return;
        if (visual == 1) m_armedBlink.Begin();
        else
        {
            m_armedBlink.Stop();
            button.Opacity(1.0);
        }
    }

    void MainPage::PlaceModeThumb(bool animate)
    {
        if (m_modeShown < 0) return;
        const double x = m_modeShown == (int)sca::EMcMode::Manual ? ModeSwitchTrack().ActualWidth() / 2 : 0.0;
        if (!animate || !m_modeSlide)
        {
            if (m_modeSlide) m_modeSlide.Stop();
            ModeThumbTransform().X(x);
            return;
        }
        m_modeSlideX.To(x);
        m_modeSlide.Begin();
    }

    void MainPage::OnModeTrackSizeChanged(WF::IInspectable const&, SizeChangedEventArgs const&)
    {
        PlaceModeThumb(false);
    }

    // RunningData là POCO ghi từ thread điều khiển — đọc theo nhịp hiển thị như MainViewModel.Refresh.
    void MainPage::UpdateCounters()
    {
        auto run = sca::McDataManager::RunData();
        if (!run) return;

        ui::SetText(CountOkText(), std::to_string(run->CountOk));
        ui::SetText(CountNgText(), std::to_string(run->CountNg));
        ui::SetText(CountTotalText(), std::to_string(run->CountTotal));
        ui::SetText(NgRateText(), std::format("{:.2f} %", run->NgRatePercent()));

        const int consecutive = run->ConsecutiveNg;
        ui::SetText(NgStreakText(), std::to_string(consecutive));
        const int streak = consecutive > 0 ? 1 : 0;
        if (streak != m_ngStreakShown)
        {
            m_ngStreakShown = streak;
            NgStreakText().Foreground(ui::Res(streak ? L"CriticalBrush" : L"TextPrimaryBrush"));
        }
    }

    // MainViewModel.RefreshAssyTrayClock (nhịp 100 ms riêng ở WPF — ở đây là OnTick). Tray đang làm: đồng hồ = bây giờ −
    // mốc bắt đầu, Tack = đồng hồ ÷ số ô OK. Tray xong (AssyTrayDone): Cycle/Tack Time giữ số chốt tới khi bấm Start với
    // tray mới thì số đó chuyển xuống hàng Last (RunningData::RestartAssyTrayClock). Đang chạy rà: cùng bảng, số lấy từ đồng
    // hồ lượt tray của chạy rà (Routine_DryCycle::Lap, Tuấn 2026-11-07) — mỗi lần đặt là một sản phẩm, hết lượt thì lượt
    // sau bắt đầu ngay; số của chu trình thật giữ nguyên, tắt chạy rà là hiện lại.
    void MainPage::RefreshAssyTrayClock()
    {
        auto run = sca::McDataManager::RunData();
        if (!run) return;

        std::optional<sca::TimePoint> start;   // tray (lượt) đang làm
        int doneNow = 0;                        // số ô đặt OK (lần đặt) của tray đang làm
        bool held = false;                      // tray xong, giữ số tới khi Start với tray mới
        double heldSec = 0, lastSec = 0;
        int heldProducts = 0, lastProducts = 0;
        auto dry = m_coreReady && sca::McControl::Status().DryRun() ? sca::McControl::DryRoutine() : nullptr;
        if (dry)
        {
            const sca::Routine_DryCycle::LapClock lap = dry->Lap();
            start = lap.StartedAt;
            doneNow = lap.Places;
            lastSec = lap.LastSec;
            lastProducts = lap.LastPlaces;
        }
        else
        {
            start = run->AssyTrayStartedAt;
            if (auto tray = run->AssyTray; start && tray)
            {
                const std::vector<sca::ECellState> cells = tray->Cells;   // bản chụp (ToArray của WPF)
                doneNow = (int)std::count(cells.begin(), cells.end(), sca::ECellState::Ok);
            }
            held = run->AssyTrayDone;
            heldSec = run->AssyTrayDoneSec;
            heldProducts = run->AssyTrayDoneProducts;
            lastSec = run->LastAssyTrayTimeSec;
            lastProducts = run->LastAssyTrayProducts;
        }

        ui::SetText(LastCycleTimeText(), lastSec > 0 ? Clock(lastSec) : std::string(Dash));
        ui::SetText(LastTackTimeText(), lastProducts > 0 ? Sec(lastSec / lastProducts) : std::string(Dash));

        double currentAvg = 0;
        if (start)
        {
            const double sec = std::chrono::duration<double>(std::chrono::system_clock::now() - *start).count();
            if (doneNow > 0) currentAvg = sec / doneNow;
            ui::SetText(CycleTimeText(), Clock(sec));
            ui::SetText(TackTimeText(), doneNow > 0 ? Sec(currentAvg) : std::string(Dash));
        }
        else if (held)
        {
            ui::SetText(CycleTimeText(), Clock(heldSec));
            ui::SetText(TackTimeText(), heldProducts > 0 ? Sec(heldSec / heldProducts) : std::string(Dash));
        }
        else
        {
            ui::SetText(CycleTimeText(), Dash);
            ui::SetText(TackTimeText(), Dash);
        }

        // Năng suất theo Tack Time của tray xong gần nhất — số đã trọn một tray; chưa có tray nào xong thì theo tray đang làm.
        const double avg = held && heldProducts > 0 ? heldSec / heldProducts
                         : lastProducts > 0 ? lastSec / lastProducts : currentAvg;
        ui::SetText(UphText(), avg > 0 ? sca::Loc::F("Main.Uph", 3600.0 / avg) : sca::Loc::T("Main.UphNone"));
    }

    // ══════════ Tray ══════════

    void MainPage::UpdateTrays()
    {
        auto run = sca::McDataManager::RunData();
        if (!run) return;
        SyncTray(true);
        SyncTray(false);
        m_inputCard.SetCounts(run->InputCountOk, run->InputCountNg);
        m_assyCard.SetCounts(run->AssyCountOk, run->AssyCountNg);
    }

    void MainPage::SyncTray(bool input)
    {
        auto run = sca::McDataManager::RunData();
        if (!run) return;
        // Chọn ô chỉ ở AUTO (Tuấn 2026-10-16).
        const bool canSelect = sca::McControl::Status().Mode() == sca::EMcMode::Auto;
        // Quầng ô hiện tại chỉ khi chu trình đang dở (Tuấn 2026-10-30) — đứng yên / chờ đổi tray thì không.
        const auto state = sca::McControl::Status().State();
        const bool showCurrent = state == sca::EMcState::Running || state == sca::EMcState::Pausing ||
                                 state == sca::EMcState::Paused || state == sca::EMcState::Held;
        if (input)
        {
            if (auto tray = run->InputTray) m_inputCard.Sync(*tray, nullptr, canSelect, showCurrent);
            return;
        }
        auto tray = run->AssyTray;
        if (!tray) return;
        // Ô trống đã quét trước OK tô xanh nhạt (Routine_Auto::AssyScannedCells).
        std::vector<int> scanned;
        auto routine = m_coreReady ? sca::McControl::AutoRoutine() : nullptr;
        if (routine) scanned = routine->AssyScannedCells();
        m_assyCard.Sync(*tray, routine ? &scanned : nullptr, canSelect, showCurrent);
    }

    // ══════════ Log ══════════

    // Ẩn/hiện card log (Tuấn 06/10): LogHost co về 0 / giãn lại 290, card mờ dần theo, thẻ dọc hiện ra khi ẩn. Bấm lại giữa
    // chừng thì chạy ngược từ chỗ đang dừng (animation mới không có From — bắt đầu từ giá trị đang hiện).
    void MainPage::OnToggleLogClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        m_logShown = !m_logShown;
        const auto duration = DurationHelper::FromTimeSpan(std::chrono::milliseconds(250));
        CubicEase ease;
        ease.EasingMode(EasingMode::EaseInOut);

        Storyboard sb;
        DoubleAnimation width;
        width.To(m_logShown ? LogCard().Width() : 0.0);
        width.Duration(duration);
        width.EasingFunction(ease);
        width.EnableDependentAnimation(true);
        Storyboard::SetTarget(width, LogHost());
        Storyboard::SetTargetProperty(width, L"Width");
        sb.Children().Append(width);

        DoubleAnimation card;
        card.To(m_logShown ? 1.0 : 0.0);
        card.Duration(duration);
        Storyboard::SetTarget(card, LogCard());
        Storyboard::SetTargetProperty(card, L"Opacity");
        sb.Children().Append(card);

        DoubleAnimation tab;
        tab.To(m_logShown ? 0.0 : 1.0);
        tab.Duration(duration);
        Storyboard::SetTarget(tab, LogShowButton());
        Storyboard::SetTargetProperty(tab, L"Opacity");
        sb.Children().Append(tab);

        if (!m_logShown) ui::Show(LogShowButton(), true);
        sb.Completed([weak = get_weak()](auto&&, auto&&) {
            auto self = weak.get();
            if (self && self->m_logShown) ui::Show(self->LogShowButton(), false);
        });
        m_logAnim = sb;
        sb.Begin();
    }

    // ══════════ Vòng đời ══════════

    void MainPage::OnShown(bool visible)
    {
        m_visible = visible;
        m_cams.SetVisible(visible);   // mỗi lần mở lại trang Main thì xét lại — đổi tên module ở Setting là ăn ngay
    }

    void MainPage::OnWindowMoved()
    {
        m_cams.OnWindowMoved();
    }

    void MainPage::OnModalChanged(bool modalOpen)
    {
        m_cams.OnModalChanged(modalOpen);
    }

    void MainPage::OnClosing()
    {
        m_closing = true;
        if (m_armedBlink) m_armedBlink.Stop();
        m_cams.Dispose();
    }

    // ══════════ Lệnh — Request* chỉ đặt cờ; vòng quét McControl quyết định (interlock ở đó) ══════════

    // MA-1: gạt sang chế độ kia của chế độ ĐANG áp dụng. McControl tự từ chối AUTO khi chưa đủ điều kiện (shell bật hộp
    // thoại AutoModeRefused) — công tắc khi đó đứng yên.
    void MainPage::OnModeClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        sca::McControl::RequestSetMode(sca::McControl::Status().Mode() == sca::EMcMode::Auto ? sca::EMcMode::Manual
                                                                                            : sca::EMcMode::Auto);
    }

    // RUN = bấm một trong bốn nút Start cứng (Tuấn 2026-10-31; trước đó cố ý chỉ vào Armed, 2026-08-20): máy đứng ở AUTO
    // thì vào sẵn sàng rồi chạy, tạm dừng thì chạy tiếp, đang chờ tray thì nạp tray — McControl DoGo, cùng mọi điều kiện an
    // toàn (rèm, alarm chưa RESET, Init sau dừng giữa hành trình…).
    void MainPage::OnRunClick(WF::IInspectable const&, RoutedEventArgs const&) { sca::McControl::RequestGo(); }
    void MainPage::OnStopClick(WF::IInspectable const&, RoutedEventArgs const&) { sca::McControl::RequestStop(); }
    void MainPage::OnResetClick(WF::IInspectable const&, RoutedEventArgs const&) { sca::McControl::RequestReset(); }
    void MainPage::OnMuteClick(WF::IInspectable const&, RoutedEventArgs const&) { sca::McControl::RequestMuteBuzzer(); }

    // MA-4. Z1/Z2 chưa nhả phanh thì hỏi một lần rồi làm hộ nghi thức của trang Manual — Servo On mọi trục, "Mở phanh"
    // Z1/Z2 — rồi mới về gốc (Tuấn 2026-10-21). Còn lý do chặn khác (EMG, ALM, đang chạy…) thì gửi thẳng như cũ: Core báo
    // lý do ở dòng thông báo, không hỏi một câu vô ích. Sau co_await không đụng tới this.
    winrt::fire_and_forget MainPage::OnOriginClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        auto busy = m_originPrep;
        if (busy->load()) co_return;
        if (!m_coreReady || sca::McControl::OriginBrakesReady() || !sca::McControl::OriginBlockReason().empty())
        {
            sca::McControl::RequestOrigin();
            co_return;
        }

        std::vector<std::string> servoOff;
        for (const auto& axis : sca::McControl::Motion().AxisList())
            if (axis && !axis->ServoOn()) servoOff.push_back(axis->Name());
        std::string message = sca::Loc::T("Main.OriginPrepConfirm");
        if (!servoOff.empty()) message += "\n" + sca::Loc::F("Main.OriginPrepServoOff", ::base::Join(servoOff, ", "));

        busy->store(true);
        bool yes = co_await ui::Dialogs::ConfirmAsync(std::move(message), sca::Loc::T("Main.OriginPrepTitle"));
        if (!yes)
        {
            busy->store(false);
            co_return;
        }

        sca::McControl::Status().SetMessage(sca::Loc::T("Main.OriginPrepRunning"));
        ui::RunBackgroundThenUi(
            [] {
                // Token chạy tay: STOP (mềm hoặc cứng), EMG, alarm huỷ được lúc đang chờ servo/phanh.
                std::string why = sca::McControl::ServoOnAndReleaseBrakes(sca::McControl::ManualToken());
                if (why.empty()) sca::McControl::RequestOrigin();
                else sca::McControl::Status().SetMessage(sca::Loc::F("Mc.CannotHome", why));
            },
            [busy] { busy->store(false); });
    }

    // MA-12: hỏi xác nhận trước khi xoá (Tuấn 2026-11-07; WPF không hỏi). Đang chạy rà thì xoá luôn lượt trước của chạy rà.
    // Sau co_await chỉ đụng this qua weak.
    winrt::fire_and_forget MainPage::OnClearCountersClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        auto weak = get_weak();
        if (!co_await ui::Dialogs::ConfirmAsync(sca::Loc::T("Main.ClearCountersConfirm"), sca::Loc::T("Main.ClearCountersTitle")))
            co_return;
        if (auto run = sca::McDataManager::RunData()) run->ResetCounters();
        if (auto dry = sca::McControl::DryRoutine()) dry->ClearLastLap();
        sca::LogWorker::Instance().RecordLog("Production counters cleared", sca::LogLevel::Info, "UI");
        if (auto self = weak.get(); self && self->m_coreReady) self->OnTick();
    }
}
