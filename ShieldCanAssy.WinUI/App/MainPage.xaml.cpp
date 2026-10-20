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

    std::string OrWaiting(std::string s) { return s.empty() ? sca::Loc::T("Main.NotTriggered") : s; }

    std::string BindKey(int loadCount, const std::string& path) { return std::to_string(loadCount) + "|" + path; }

    /// <summary>Đường dẫn Flow/module của ba khung (Tool 1, Tool 2, Top) — ô ở Setting: "T1" = cả Flow T1, "T1.Module" =
    /// module trong Flow T1, trống = cả Flow (VisionOutputNames::RenderPath).</summary>
    std::array<std::string, 3> RenderPaths()
    {
        auto sys = sca::McDataManager::SysData();
        sca::VisionOutputNames n = sys ? sys->VisionOutputs : sca::VisionOutputNames{};
        return { sca::VisionOutputNames::RenderPath(n.FlowAlignBottomTool1, n.RenderTool1),
                 sca::VisionOutputNames::RenderPath(n.FlowAlignBottomTool2, n.RenderTool2),
                 sca::VisionOutputNames::RenderPath(n.FlowLocateTop, n.RenderTopDown) };
    }

    /// <summary>Nền khung ảnh CAM = màu card nhìn thấy trên máy (SetBackground của SDK chỉ nhận màu đặc).</summary>
    constexpr const wchar_t* RenderBackground = L"#F1F1F1";
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
        m_inputCard.OnClearSelection = [this] { sca::McControl::ClearCellSelection(true); SyncTray(true); };
        m_assyCard.OnClearSelection = [this] { sca::McControl::ClearCellSelection(false); SyncTray(false); };
        // Tray 1, và Tray 2 nếu đã teach, thành có hàng — McControl::ReloadInputTray.
        m_inputCard.OnReload = [this] { sca::McControl::ReloadInputTray(); if (m_coreReady) OnTick(); };
        // Mọi ô thành trống, bỏ số đo quét trước, chu trình làm lại từ S00 — McControl::ResetAssyTray.
        m_assyCard.OnReload = [this] { sca::McControl::ResetAssyTray(); if (m_coreReady) OnTick(); };

        // ── Ba khung CAM ──
        Tool1CamHost().Child(m_cams[CamTool1].Build(std::wstring(ui::T("Main.CamTool1"))));
        Tool2CamHost().Child(m_cams[CamTool2].Build(std::wstring(ui::T("Main.CamTool2"))));
        TopCamHost().Child(m_cams[CamTop].Build(std::wstring(ui::T("Main.CamTop"))));
        {
            auto waiting = std::make_shared<sca::ReadoutCard>();
            waiting->Note = sca::Loc::T("Main.NotTriggered");
            m_waitingCard = waiting;
        }
        for (int i = 0; i < (int)m_cams.size(); ++i)
        {
            m_cams[i].SetReadout(m_waitingCard, sca::Loc::T("Main.NotTriggered"));
            // Bấm tiêu đề bảng: đổi Ofs/Tgt ↔ bảng tách Đích cho CẢ HAI tay (ReadoutHeader_Click → ToggleBreakdown).
            m_cams[i].OnReadoutHeaderTapped = [this] {
                m_showBreakdown = !m_showBreakdown;
                SyncToolCards();
            };
            m_cams[i].OnToolbarClicked = [this, i] { ToggleToolbar(i); };
        }

        // ── Log ──
        LogListHost().Child(m_log.Build());
        m_log.Update();

        if (m_coreReady) { QueueVm(true); OnTick(); }
    }

    void MainPage::OnCoreReady()
    {
        m_coreReady = true;
        if (m_loaded) OnTick();
    }

    // ══════════ Cập nhật theo nhịp (100 ms, chỉ khi trang đang hiện) ══════════

    void MainPage::OnTick()
    {
        if (!m_loaded || m_closing) return;
        if (ui::AppHost::Options().ShotMode()) InjectShotSample();

        UpdateOperation();
        UpdateCounters();
        RefreshAssyTrayClock();
        UpdateTrays();
        UpdateCams();
        m_log.Update();
        QueueVm(false);
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
    // Running. Bấm vào chỉ vào Armed.
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
    // tray mới thì số đó chuyển xuống hàng Last (RunningData::RestartAssyTrayClock).
    void MainPage::RefreshAssyTrayClock()
    {
        auto run = sca::McDataManager::RunData();
        if (!run) return;
        const std::optional<sca::TimePoint> start = run->AssyTrayStartedAt;

        const bool hasLast = run->LastAssyTrayTimeSec > 0;
        ui::SetText(LastCycleTimeText(), hasLast ? Clock(run->LastAssyTrayTimeSec) : std::string(Dash));
        ui::SetText(LastTackTimeText(), run->LastAssyTrayProducts > 0 ? Sec(run->LastAssyTrayPerProductSec()) : std::string(Dash));

        double currentAvg = 0;
        if (start)
        {
            const double sec = std::chrono::duration<double>(std::chrono::system_clock::now() - *start).count();
            int done = 0;
            auto tray = run->AssyTray;
            if (tray)
            {
                const std::vector<sca::ECellState> cells = tray->Cells;   // bản chụp (ToArray của WPF)
                done = (int)std::count(cells.begin(), cells.end(), sca::ECellState::Ok);
            }
            if (done > 0) currentAvg = sec / done;
            ui::SetText(CycleTimeText(), Clock(sec));
            ui::SetText(TackTimeText(), done > 0 ? Sec(currentAvg) : std::string(Dash));
        }
        else if (run->AssyTrayDone)
        {
            ui::SetText(CycleTimeText(), Clock(run->AssyTrayDoneSec));
            ui::SetText(TackTimeText(), run->AssyTrayDoneProducts > 0 ? Sec(run->AssyTrayDonePerProductSec()) : std::string(Dash));
        }
        else
        {
            ui::SetText(CycleTimeText(), Dash);
            ui::SetText(TackTimeText(), Dash);
        }

        // Năng suất theo Tack Time của tray xong gần nhất — số đã trọn một tray; chưa có tray nào xong thì theo tray đang làm.
        const double avg = run->AssyTrayDone && run->AssyTrayDoneProducts > 0 ? run->AssyTrayDonePerProductSec()
                         : run->LastAssyTrayProducts > 0 ? run->LastAssyTrayPerProductSec() : currentAvg;
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
        if (input)
        {
            if (auto tray = run->InputTray) m_inputCard.Sync(*tray, nullptr, canSelect);
            return;
        }
        auto tray = run->AssyTray;
        if (!tray) return;
        // Ô trống đã quét trước OK tô xanh nhạt (Routine_Auto::AssyScannedCells).
        std::vector<int> scanned;
        auto routine = m_coreReady ? sca::McControl::AutoRoutine() : nullptr;
        if (routine) scanned = routine->AssyScannedCells();
        m_assyCard.Sync(*tray, routine ? &scanned : nullptr, canSelect);
    }

    // ══════════ Khung CAM: bảng số + ảnh gần nhất ══════════

    std::shared_ptr<const sca::ReadoutCard> MainPage::Shown(std::shared_ptr<const sca::ReadoutCard> const& c) const
    {
        if (!c) return m_waitingCard;
        return m_showBreakdown && c->Breakdown ? c->Breakdown : c;
    }

    void MainPage::SyncToolCards()
    {
        if (m_shotSample && !sca::VisionReadout::Tool1Card())
            m_cams[CamTool1].SetReadout(Shown(m_shotSample), "Self-test sample readout");
        else
            m_cams[CamTool1].SetReadout(Shown(sca::VisionReadout::Tool1Card()), OrWaiting(sca::VisionReadout::Tool1()));
        m_cams[CamTool2].SetReadout(Shown(sca::VisionReadout::Tool2Card()), OrWaiting(sca::VisionReadout::Tool2()));
    }

    void MainPage::UpdateCams()
    {
        SyncToolCards();
        auto top = sca::VisionReadout::TopCard();
        m_cams[CamTop].SetReadout(top ? top : m_waitingCard, OrWaiting(sca::VisionReadout::Top()));

        if (m_shotSample) return;   // --shot: ảnh mẫu đã đặt, không có Vision thật
        if (auto vision = sca::McControl::Vision())
        {
            m_cams[CamTool1].SetImage(vision->LastBottomUpImageTool1());
            m_cams[CamTool2].SetImage(vision->LastBottomUpImageTool2());
            m_cams[CamTop].SetImage(vision->LastTopDownImage());
        }
    }

    // --shot (luôn mô phỏng, dữ liệu tạm): một ảnh và một bảng số mẫu ở khung Tool 1 để ảnh chụp kiểm được bố cục. Chỉ ở
    // phía UI — không ghi gì vào Core.
    void MainPage::InjectShotSample()
    {
        if (m_shotSample) return;
        auto card = std::make_shared<sca::ReadoutCard>();
        card->Cell = "Cell (2,3)";
        card->Columns = { "X", "Y", "\xCE\xB8" };
        card->Rows = { { "Ofs", { "0.012", "-0.034", "0.15" } }, { "Tgt", { "182.410", "96.205", "90.15" } } };
        card->Note = "Sample";
        m_shotSample = card;

        auto img = std::make_shared<sca::ImageData>();
        img->Width = 320; img->Height = 240; img->Channels = 1; img->Stride = 320;
        img->Pixels.resize((size_t)img->Stride * img->Height);
        for (int y = 0; y < img->Height; ++y)
            for (int x = 0; x < img->Width; ++x)
                img->Pixels[(size_t)y * img->Stride + x] = (uint8_t)(std::abs(x - y) < 3 ? 255 : 60 + x * 150 / 319);
        m_cams[CamTool1].SetImage(img);
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

    // ══════════ VmRenderControl của VisionMaster (khi vision là VisionMaster thật) ══════════

    // Control VM gọi sang vServerApp và COM bơm thông điệp trong lúc chờ — gọi trong Loaded/Tick/Click là XAML tự kết thúc
    // app (0xC000027B). Nên chỉ xếp hàng (ui::QueueVmWork); gộp các lần gọi dồn dập thành một.
    void MainPage::QueueVm(bool force)
    {
        m_vmForce = m_vmForce || force;
        if (m_vmQueued || m_closing || !m_coreReady) return;
        m_vmQueued = true;
        ui::QueueVmWork([weak = get_weak()] {
            auto self = weak.get();
            if (!self) return;
            self->m_vmQueued = false;
            // Đang ở giữa một lời gọi VM (nó bơm thông điệp nên việc này chạy lồng vào): bỏ, nhịp sau xếp lại.
            if (self->m_vmBusy || self->m_closing) return;
            const bool force = std::exchange(self->m_vmForce, false);
            self->m_vmBusy = true;
            try
            {
                self->EnsureVmPanes();
                self->UpdateRenderBindings(force);
            }
            catch (...) { self->m_vmBusy = false; throw; }
            self->m_vmBusy = false;
        });
    }

    // Tạo ba control lần đầu trang hiện. Mô phỏng (FakeVision) / --shot: không có VisionMaster — khung chỉ có ảnh gần nhất
    // và chữ "(no image)". Bản C++ không chạm VisionMaster khi mô phỏng (CONTROLLER-NOTES).
    void MainPage::EnsureVmPanes()
    {
        if (m_vmAttempted || !m_loaded || !m_visible || !m_coreReady) return;
        m_vmAttempted = true;

        if (ui::AppHost::Options().ShotMode()) return;
        auto vc = std::dynamic_pointer_cast<sca::VisionConnect>(sca::McControl::Vision());
        if (!vc || vc->IsSimulation()) return;

        std::string error;
        for (int i = 0; i < (int)m_cams.size(); ++i)
        {
            ui::MainCamFrame& f = m_cams[i];
            if (f.Pane.Create(f.RenderHost(), ui::vmkind::Render, &error))
            {
                ApplyRenderLook(i);
                ui::Show(f.ToolbarButton(), true);
                continue;
            }
            // Thường là chưa đăng ký ActiveX của VisionMaster — ba khung cùng lý do, không thử tiếp.
            for (ui::MainCamFrame& g : m_cams)
                if (!g.Pane.Created()) g.ShowVmError("Cannot create VisionMaster view: " + error);
            sca::LogWorker::Instance().RecordLog("Main: cannot create CAM view - " + error, sca::LogLevel::Warning, "Vision");
            break;
        }
        for (const ui::MainCamFrame& f : m_cams) m_vmActive = m_vmActive || f.Pane.Created();

        ApplyPaneVisibility();
        UpdateRenderBindings(true);
    }

    void MainPage::ApplyPaneVisibility()
    {
        if (!m_vmActive) return;
        const int show = m_visible && !m_closing ? 1 : 0;
        for (ui::MainCamFrame& f : m_cams)
        {
            if (!f.Pane.Created() || show == f.PaneShown) continue;
            f.PaneShown = show;
            f.Pane.SetVisible(show == 1);
        }
    }

    // Gắn ba control vào module đặt ở trang Setting (mặc định cả Flow AlignBottom_Tool1, AlignBottom_Tool2, LocateTop). Chỉ
    // gắn SAU khi McControl nạp xong solution (nạp xong thì callback kết quả mọi module bị tắt — gắn trước là bị tắt theo);
    // nạp lại (VisionLoadCount đổi) hay đổi tên ở Setting thì gắn lại. Chưa gắn đủ thì thử lại mỗi giây (timer 1 s của WPF).
    void MainPage::UpdateRenderBindings(bool force)
    {
        if (!m_vmActive || !m_visible || m_closing) return;

        const int load = sca::McControl::VisionLoadCount();
        const auto paths = RenderPaths();

        bool allBound = true;
        for (int i = 0; i < (int)m_cams.size(); ++i)
            if (m_cams[i].Pane.Created() && m_cams[i].BoundKey != BindKey(load, paths[i])) allBound = false;
        if (allBound) return;

        const auto now = std::chrono::steady_clock::now();
        if (!force && now - m_lastBindTry < std::chrono::seconds(1)) return;
        m_lastBindTry = now;

        auto vc = std::dynamic_pointer_cast<sca::VisionConnect>(sca::McControl::Vision());
        std::shared_ptr<sca::VisionMasterDevice> device =
            vc ? std::dynamic_pointer_cast<sca::VisionMasterDevice>(vc->Device()) : nullptr;
        const bool ready = load > 0 && device != nullptr && vc->IsSolutionLoaded();

        std::string diag = ready ? "" : ", waiting for solution load";
        if (ready)
        {
            for (int i = 0; i < (int)m_cams.size(); ++i)
            {
                if (!m_cams[i].Pane.Created()) continue;
                const bool ok = BindOne(i, *device, paths[i], load);
                diag += std::format(", '{}'={}", paths[i], ok ? "bound" : "NOT found");
            }
        }

        // Log một lần cho mỗi bộ tên — đủ biết kẹt ở đâu (chưa nạp solution, hay sai tên module), không rác log mỗi giây.
        if (diag != m_loggedVisionDiagnostic)
        {
            m_loggedVisionDiagnostic = diag;
            sca::LogWorker::Instance().RecordLog("Main: CAM views" + diag, sca::LogLevel::Info, "Vision");
        }
    }

    // Gán Flow/module <path> cho khung; đã gán đúng đường dẫn đó trong lần nạp đó thì thôi. False khi solution chưa có
    // Flow/module đó, hoặc SDK đang bận chạy Flow (RenderSource không chờ) — lần sau thử lại.
    bool MainPage::BindOne(int index, sca::VisionMasterDevice& device, std::string const& path, int loadCount)
    {
        ui::MainCamFrame& f = m_cams[index];
        const std::string key = BindKey(loadCount, path);
        if (f.BoundKey == key) return true;
        if (!f.Pane.Created() || path.empty()) return false;

        // Khung chỉ vẽ được module ĐANG BẬT callback kết quả (bản WPF EnableResultCallbacks, sửa lỗi 30/09): bật trước
        // khi gán; cả Flow thì bật mọi module con. SDK bận/chưa nạp → thử lại lần sau; lỗi từng module chỉ log như C#.
        std::string enableError;
        if (!device.EnableResultCallbacks(path, enableError)) return false;
        if (!enableError.empty() && f.CallbackFailLoggedKey != key)
        {
            f.CallbackFailLoggedKey = key;
            sca::LogWorker::Instance().RecordLog(
                std::format("Main: CAM view '{}' - can't enable result callbacks: {}", path, enableError),
                sca::LogLevel::Warning, "Vision");
        }

        // Con trỏ thuộc về SDK: đưa cho control NGAY, không cất giữ.
        void* source = device.RenderSource(path);
        if (source == nullptr) return false;

        HRESULT hr = f.Pane.Host().CallPtr(ui::vmdisp::RenderSetParamsInfo, source);
        if (FAILED(hr))
        {
            if (f.FailLoggedKey != key)
            {
                f.FailLoggedKey = key;
                sca::LogWorker::Instance().RecordLog(
                    std::format("Main: CAM view '{}' - SetParamsInfo hr=0x{:08X} {}", path, (unsigned)hr,
                                ::base::Narrow(f.Pane.Host().LastError())),
                    sca::LogLevel::Warning, "Vision");
            }
            return false;
        }
        f.BoundKey = key;
        ApplyRenderLook(index);   // gọi lại sau mỗi lần gắn — SDK có thể trả nền/thanh công cụ về mặc định
        return true;
    }

    void MainPage::ApplyRenderLook(int index)
    {
        ui::MainCamFrame& f = m_cams[index];
        if (!f.Pane.Created()) return;
        HRESULT hr = f.Pane.Host().CallStr(ui::vmdisp::RenderSetBackground, RenderBackground);
        if (SUCCEEDED(hr)) hr = f.Pane.Host().CallBool(ui::vmdisp::RenderSetToolbarVisible, f.ToolbarShown);
        if (FAILED(hr))
        {
            std::string why = ::base::Narrow(f.Pane.Host().LastError());
            if (why == m_loggedRenderLookError) return;
            m_loggedRenderLookError = why;
            sca::LogWorker::Instance().RecordLog("Main: cannot set CAM view background/toolbar - " + why,
                                                 sca::LogLevel::Warning, "Vision");
        }
    }

    // ☰: ẩn/hiện thanh công cụ của khung (Tuấn 2026-10-07). Lời gọi vào control qua hàng đợi, không gọi trong Click.
    void MainPage::ToggleToolbar(int index)
    {
        ui::MainCamFrame& f = m_cams[index];
        f.ToolbarShown = !f.ToolbarShown;
        ui::SetTint(f.ToolbarButton(), f.ToolbarShown ? ui::Tint::Accent : ui::Tint::None);
        ui::QueueVmWork([weak = get_weak(), index] {
            auto self = weak.get();
            if (!self || self->m_closing || self->m_vmBusy) return;
            self->ApplyRenderLook(index);
        });
    }

    // ══════════ Vòng đời ══════════

    void MainPage::OnShown(bool visible)
    {
        m_visible = visible;
        ApplyPaneVisibility();
        // Mỗi lần mở lại trang Main thì xét lại — đổi tên module ở Setting là ăn ngay.
        if (visible) QueueVm(true);
    }

    void MainPage::OnWindowMoved()
    {
        for (ui::MainCamFrame& f : m_cams) f.Pane.Sync();
    }

    void MainPage::OnModalChanged(bool modalOpen)
    {
        for (ui::MainCamFrame& f : m_cams) f.Pane.SetEnabled(!modalOpen);
    }

    void MainPage::OnClosing()
    {
        m_closing = true;
        if (m_armedBlink) m_armedBlink.Stop();
        for (ui::MainCamFrame& f : m_cams) f.Pane.Dispose();
    }

    // ══════════ Lệnh — Request* chỉ đặt cờ; vòng quét McControl quyết định (interlock ở đó) ══════════

    // MA-1: gạt sang chế độ kia của chế độ ĐANG áp dụng. McControl tự từ chối AUTO khi chưa đủ điều kiện (shell bật hộp
    // thoại AutoModeRefused) — công tắc khi đó đứng yên.
    void MainPage::OnModeClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        sca::McControl::RequestSetMode(sca::McControl::Status().Mode() == sca::EMcMode::Auto ? sca::EMcMode::Manual
                                                                                            : sca::EMcMode::Auto);
    }

    // Nút mềm CỐ Ý chỉ đưa máy vào Armed — máy chạy thật chỉ nhận từ nút Start cứng (2026-08-20).
    void MainPage::OnRunClick(WF::IInspectable const&, RoutedEventArgs const&) { sca::McControl::RequestArm(); }
    void MainPage::OnStopClick(WF::IInspectable const&, RoutedEventArgs const&) { sca::McControl::RequestStop(); }
    void MainPage::OnOriginClick(WF::IInspectable const&, RoutedEventArgs const&) { sca::McControl::RequestOrigin(); }
    void MainPage::OnResetClick(WF::IInspectable const&, RoutedEventArgs const&) { sca::McControl::RequestReset(); }
    void MainPage::OnMuteClick(WF::IInspectable const&, RoutedEventArgs const&) { sca::McControl::RequestMuteBuzzer(); }

    // MA-12: không hỏi xác nhận (như WPF).
    void MainPage::OnClearCountersClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        if (auto run = sca::McDataManager::RunData()) run->ResetCounters();
        if (m_coreReady) OnTick();
    }
}
