#include "pch.h"
#include "SettingPage.xaml.h"
#if __has_include("SettingPage.g.cpp")
#include "SettingPage.g.cpp"
#endif

#include <array>
#include <shellapi.h>

#include "Base/Text.h"
#include "Communication/GlobalVarSync.h"
#include "Communication/VmOutCheck.h"
#include "Controller/McControl.h"
#include "Data/McDataManager.h"
#include "Infor/AlarmCatalog.h"
#include "Infor/Enums.h"
#include "Lang/Loc.h"
#include "SysConfig/AppPaths.h"
#include "SysConfig/UserAccess.h"
#include "Ui/Dialogs.h"
#include "Ui/LoginDialog.h"
#include "Ui/UiUtil.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
namespace WF = winrt::Windows::Foundation;
namespace sp = ui::settingpage;

namespace
{
    std::shared_ptr<sca::SystemData> Sys() { return sca::McDataManager::SysData(); }

    /// <summary>Báo cáo của Core xuống dòng bằng "\r\n" (như bản C#); TextBlock chỉ cần "\n".</summary>
    std::string NormalizeNewLines(const std::string& text) { return ::base::Replace(text, "\r\n", "\n"); }

    int TagIndex(WF::IInspectable const& tag)
    {
        int value = -1;
        hstring text = winrt::unbox_value_or<hstring>(tag, hstring{});
        if (text.empty() || !ui::ParseInt(text, value)) return -1;
        return value;
    }
}

namespace winrt::ShieldCanAssy::implementation
{
    // ══════════ Dựng trang ══════════

    void SettingPage::OnRootLoaded(WF::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_loaded) return;
        m_loaded = true;

        SetupNumberBoxes();
        SetupIdleLogoutBox();
        BindFields();
        BuildAxisTable();
        LogTableHost().Child(m_log.Build());

        LoadAll(true);
        LoadZSafe();
        UpdateStaticInfo();
        UpdateCommandStates();
        UpdateUserAccess(true);
        SelectSection(m_section);
    }

    void SettingPage::OnCoreReady()
    {
        m_coreReady = true;
        if (!m_loaded) return;
        UpdateCommandStates();
        UpdateAutoMode();
    }

    // ══════════ Mục ở NavigationView ══════════

    void SettingPage::SelectSection(int section)
    {
        if (section < 0 || section >= SectionCount) return;
        m_section = section;

        std::array<NavigationViewItem, SectionCount> items{ NavSystem(), NavVision(), NavServo(), NavDelay(), NavLog(), NavUsers() };
        auto selected = SectionNav().SelectedItem();
        if (!selected || selected != items[(size_t)section]) SectionNav().SelectedItem(items[(size_t)section]);

        ui::Show(SystemSection(), section == SectionSystem);
        ui::Show(VisionSection(), section == SectionVision);
        ui::Show(ServoSection(), section == SectionServo);
        ui::Show(DelaySection(), section == SectionDelay);
        ui::Show(LogSection(), section == SectionLog);
        ui::Show(UsersSection(), section == SectionUsers);
        if (m_coreReady) OnTick();
    }

    void SettingPage::OnSectionChanged(NavigationView const&, NavigationViewSelectionChangedEventArgs const& e)
    {
        auto item = e.SelectedItem().try_as<NavigationViewItem>();
        if (!item) return;
        int section = TagIndex(item.Tag());
        if (section >= 0 && section != m_section) SelectSection(section);
    }

    // ══════════ Gắn ô nhập với dữ liệu sống ══════════

    void SettingPage::SetupNumberBoxes()
    {
        for (NumberBox const& box : { ZSafeBox(), PickRetryBox(), MaxNgBox(), ControlTickBox(), WatchdogBox(),
                                      MaxCompAngleBox(), MaxCompDistBox(), PlaceGuardBox(), GuardSkipBox(), AlignXTolBox(),
                                      ACapTolBox(), FlowTimeoutBox(), CalibResidualBox(), VerifyAngleTolBox(), VerifyMinEccBox(),
                                      VerifyMinTurnBox(), BottomSettleBox(), BottomLampBox(), TopSettleBox(), TopLampBox(),
                                      VacTimeoutBox(), VacSettleBox(), BlowTimeBox() })
            sp::NumberToggleGroup::SetupNumberBox(box);

        // Chú thích của bảng chờ chụp: tiêu đề cột và từng ô (WPF: ToolTip trên cả hai).
        hstring settleTip = ui::T("Setting.SettleTip");
        hstring lampTip = ui::T("Setting.LampTip");
        for (FrameworkElement const& e : { SettleHeaderText().as<FrameworkElement>(), BottomSettleBox().as<FrameworkElement>(),
                                           TopSettleBox().as<FrameworkElement>() })
            ToolTipService::SetToolTip(e, box_value(settleTip));
        for (FrameworkElement const& e : { LampHeaderText().as<FrameworkElement>(), BottomLampBox().as<FrameworkElement>(),
                                           TopLampBox().as<FrameworkElement>() })
            ToolTipService::SetToolTip(e, box_value(lampTip));
    }

    void SettingPage::BindFields()
    {
        auto& n = m_numbers;
        auto bindInt = [&n](NumberBox const& box, const char* label, int sca::SystemData::*member) {
            n.BindInt(box, label, [member] { return Sys().get()->*member; }, [member](int v) { Sys().get()->*member = v; });
        };
        auto bindDouble = [&n](NumberBox const& box, const char* label, double sca::SystemData::*member) {
            n.BindNumber(box, label, [member] { return Sys().get()->*member; }, [member](double v) { Sys().get()->*member = v; });
        };
        auto bindToggle = [&n](ToggleSwitch const& sw, bool sca::SystemData::*member) {
            n.BindToggle(sw, [member] { return Sys().get()->*member; }, [member](bool v) { Sys().get()->*member = v; });
        };

        // ── SYSTEM PARAMS (SE-12…SE-20). Z_SAFE riêng — xem LoadZSafe/OnSaveClick. ──
        bindInt(PickRetryBox(), "Pick retries", &sca::SystemData::PickRetryCount);
        bindInt(MaxNgBox(), "Max NG in a row", &sca::SystemData::MaxConsecutiveNg);
        bindInt(ControlTickBox(), "Ctrl loop (ms)", &sca::SystemData::ControlTickMs);
        bindInt(WatchdogBox(), "Watchdog (ms)", &sca::SystemData::WatchdogTimeoutMs);
        bindToggle(SimModeSwitch(), &sca::SystemData::SimulationMode);
        n.BindToggle(RequireHomeSwitch(), [] { return Sys()->RequireHomeAfterEmg(); },
                     [](bool v) { Sys()->SetRequireHomeAfterEmg(v); });
        bindToggle(HomeSeqSwitch(), &sca::SystemData::HomeRotateSequential);
        bindToggle(CurtainSwitch(), &sca::SystemData::HasLightCurtain);
        // Cả tray / từng hàng / từng cặp — hai cờ ScanAssyByRow + ScanAssyByPair gộp ở SystemData::ScanAssy.
        using Scan = sca::SystemData::EScanAssy;
        m_texts.BindChoice(ScanAssyCombo(), { "Whole tray", "By row", "By pair" },
            []() -> std::string {
                switch (Sys()->ScanAssy())
                {
                case Scan::Row: return "By row";
                case Scan::Pair: return "By pair";
                default: return "Whole tray";
                }
            },
            [](std::string v) { Sys()->SetScanAssy(v == "By row" ? Scan::Row : v == "By pair" ? Scan::Pair : Scan::Tray); });

        // ── VISION — LIMITS & TOLERANCES (SE-30, SE-31) ──
        bindDouble(MaxCompAngleBox(), "Max comp. angle (°)", &sca::SystemData::MaxCompensationAngleDeg);
        bindDouble(MaxCompDistBox(), "Max center ofs (mm)", &sca::SystemData::MaxCompensationDistance);
        bindDouble(PlaceGuardBox(), "Max grid dev. (mm)", &sca::SystemData::PlaceTargetGuardMm);
        bindInt(GuardSkipBox(), "Off-grid cells in a row", &sca::SystemData::MaxGuardSkipStreak);
        bindDouble(AlignXTolBox(), "Insp pos. X tol. (mm)", &sca::SystemData::AlignXTolMm);
        bindDouble(ACapTolBox(), "A dev. at capture (°)", &sca::SystemData::ACapTolDeg);
        bindInt(FlowTimeoutBox(), "Timeout Flow (ms)", &sca::SystemData::VisionTriggerTimeoutMs);
        bindDouble(CalibResidualBox(), "Check tol. (mm)", &sca::SystemData::CalibMaxResidualThresholdMm);
        bindDouble(VerifyAngleTolBox(), "Rot. test angle tol. (°)", &sca::SystemData::AlignVerifyAngleTolDeg);
        bindDouble(VerifyMinEccBox(), "Rot. test min ecc. (mm)", &sca::SystemData::AlignVerifyMinEccMm);
        bindDouble(VerifyMinTurnBox(), "Min test turn (°)", &sca::SystemData::AlignVerifyMinTurnDeg);

        // ── VISION — FLOW & VAR NAMES (SE-32, SE-33) ──
        auto& t = m_texts;
        auto bindName = [&t](TextBox const& box, const char* label, std::string sca::VisionOutputNames::*member) {
            t.BindString(box, label, [member] { return Sys()->VisionOutputs.*member; },
                         [member](std::string v) { Sys()->VisionOutputs.*member = std::move(v); });
        };
        bindName(FlowLocateTopBox(), "Flow LocateTop", &sca::VisionOutputNames::FlowLocateTop);
        bindName(FlowTool1Box(), "Flow AlignBottom Tool1", &sca::VisionOutputNames::FlowAlignBottomTool1);
        bindName(FlowTool2Box(), "Flow AlignBottom Tool2", &sca::VisionOutputNames::FlowAlignBottomTool2);
        bindName(OutFoundBox(), "Output Found (shared)", &sca::VisionOutputNames::OutFound);
        bindName(RenderTool1Box(), "Bottom CAM — Nozzle #1", &sca::VisionOutputNames::RenderTool1);
        bindName(RenderTool2Box(), "Bottom CAM — Nozzle #2", &sca::VisionOutputNames::RenderTool2);
        bindName(RenderTopBox(), "Top CAM", &sca::VisionOutputNames::RenderTopDown);

        // ── SERVO: IP Ezi-IO (SE-34), Z qua điểm giảm tốc (SE-35) ──
        t.BindString(IoDiIpBox(), "IP module 32Di", [] { return Sys()->IoDiIpAddress; },
                     [](std::string v) { Sys()->IoDiIpAddress = std::move(v); });
        t.BindString(IoDoIpBox(), "IP module 32Do", [] { return Sys()->IoDoIpAddress; },
                     [](std::string v) { Sys()->IoDoIpAddress = std::move(v); });
        bindToggle(InputZDownSwitch(), &sca::SystemData::InputZSlowDown);
        bindToggle(InputZUpSwitch(), &sca::SystemData::InputZSlowUp);
        bindToggle(AssyZDownSwitch(), &sca::SystemData::AssyZSlowDown);
        bindToggle(AssyZUpSwitch(), &sca::SystemData::AssyZSlowUp);

        // ── DELAY (SE-37, SE-38) ──
        bindInt(BottomSettleBox(), "CAM bottom-up — Capture settle (ms)", &sca::SystemData::BottomSettleMs);
        bindInt(BottomLampBox(), "CAM bottom-up — Light warm-up (ms)", &sca::SystemData::BottomLampMs);
        bindInt(TopSettleBox(), "CAM top-down — Capture settle (ms)", &sca::SystemData::TopSettleMs);
        bindInt(TopLampBox(), "CAM top-down — Light warm-up (ms)", &sca::SystemData::TopLampMs);
        bindInt(VacTimeoutBox(), "Sensor timeout (ms)", &sca::SystemData::VacuumTimeoutMs);
        bindInt(VacSettleBox(), "Vac build-up (ms)", &sca::SystemData::VacuumSettleMs);
        bindInt(BlowTimeBox(), "Release blow (ms)", &sca::SystemData::BlowTimeMs);
    }

    // Bảng 7 trục (DataGrid AxisConfigs của bản WPF, SE-36). Ô ghi thẳng vào AxisConfig sống mà MotionConnect đang dùng
    // (không phải bản sao): soft limit, vị trí chờ, offset gốc, acc/dec ăn ngay từ lệnh kế tiếp; COM/IP cần khởi động lại.
    void SettingPage::BuildAxisTable()
    {
        m_axisTable.Build({ { L"Axis", 60 }, { L"Driver", 90 }, { L"COM (A6SF)", 110 }, { L"IP (Fastech)", 130 },
                            { L"Jog slow", 80 }, { L"Jog fast", 80 }, { L"Auto high", 80 }, { L"Auto mid", 80 },
                            { L"Auto low", 80 }, { L"Origin ofs", 84 }, { L"Standby", 84 }, { L"Soft Limit −", 96 },
                            { L"Soft Limit +", 96 }, { L"Acc (ms)", 80 }, { L"Dec (ms)", 80 } }, 36);
        double width = 0;
        for (const auto& c : m_axisTable.Columns) width += c.Width;
        m_axisTable.Root.Width(width);
        AxisTableHost().Child(m_axisTable.Root);

        // Chú thích tiêu đề Acc / Dec (WPF: HeaderStyle ToolTip).
        if (auto header = m_axisTable.Root.Children().GetAt(0).try_as<Grid>())
        {
            auto cells = header.Children();
            if (cells.Size() >= 15)
            {
                ToolTipService::SetToolTip(cells.GetAt(13), box_value(ui::T("Setting.AccTip")));
                ToolTipService::SetToolTip(cells.GetAt(14), box_value(ui::T("Setting.DecTip")));
            }
        }

        auto sys = Sys();
        if (!sys) return;
        auto& f = m_axisFields;
        for (const auto& cfg : sys->Axes)
        {
            if (!cfg) continue;
            auto a = cfg;
            std::string name = a->Name;
            auto label = [name](const char* column) { return "Axis " + name + " — " + column; };

            Grid row = m_axisTable.NewRow(44);
            auto box = [&row](int column) {
                TextBox b = sp::CellBox();
                sp::PlaceInColumn(row, b, column);
                return b;
            };

            TextBlock nameText = sp::TextCell(name, 13);
            nameText.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
            sp::PlaceInColumn(row, nameText, 0);
            TextBlock kind = sp::TextCell(sca::ToString(a->Kind));   // chỉ đọc như bản WPF
            kind.Foreground(ui::Res(L"TextSecondaryBrush"));
            sp::PlaceInColumn(row, kind, 1);

            f.BindString(box(2), label("COM (A6SF)"), [a] { return a->ComPort; }, [a](std::string v) { a->ComPort = std::move(v); });
            f.BindString(box(3), label("IP (Fastech)"), [a] { return a->IpAddress; }, [a](std::string v) { a->IpAddress = std::move(v); });
            f.BindDouble(box(4), label("Jog slow"), [a] { return a->JogSpeedSlow; }, [a](double v) { a->JogSpeedSlow = v; });
            f.BindDouble(box(5), label("Jog fast"), [a] { return a->JogSpeedFast; }, [a](double v) { a->JogSpeedFast = v; });
            f.BindDouble(box(6), label("Auto high"), [a] { return a->MoveSpeed; }, [a](double v) { a->MoveSpeed = v; });
            // Như bản WPF: ô hiện giá trị HIỆU LỰC (chưa đặt = 50 % / 20 % mức cao); gõ 0 = quay về mặc định theo tỉ lệ.
            f.BindDouble(box(7), label("Auto mid"), [a] { return a->MoveSpeedMid(); }, [a](double v) { a->SetMoveSpeedMid(v); });
            f.BindDouble(box(8), label("Auto low"), [a] { return a->MoveSpeedLow(); }, [a](double v) { a->SetMoveSpeedLow(v); });
            f.BindDouble(box(9), label("Origin ofs"), [a] { return a->HomeOffset; }, [a](double v) { a->HomeOffset = v; });
            f.BindDouble(box(10), label("Standby"), [a] { return a->StandbyPos; }, [a](double v) { a->StandbyPos = v; });
            f.BindDouble(box(11), label("Soft Limit −"), [a] { return a->SoftLimitNeg; }, [a](double v) { a->SoftLimitNeg = v; });
            f.BindDouble(box(12), label("Soft Limit +"), [a] { return a->SoftLimitPos; }, [a](double v) { a->SoftLimitPos = v; });
            f.BindInt(box(13), label("Acc (ms)"), [a] { return a->AccelTimeMs; }, [a](int v) { a->AccelTimeMs = v; });
            f.BindInt(box(14), label("Dec (ms)"), [a] { return a->DecelTimeMs; }, [a](int v) { a->DecelTimeMs = v; });

            m_axisTable.Rows.Children().Append(row);
        }
    }

    // ══════════ Nạp ô nhập từ dữ liệu ══════════

    void SettingPage::LoadAll(bool force)
    {
        if (!Sys()) return;
        m_texts.LoadAll(force);
        m_numbers.LoadAll();
        m_axisFields.LoadAll(force);

        hstring path = ui::H(Sys()->VisionSolutionPath);
        if (SolutionPathBox().Text() != path) SolutionPathBox().Text(path);
    }

    /// <summary>Z_SAFE là giá trị riêng của trang (ZSafe của ViewModel cũ): nạp lúc dựng trang, sau Lưu và khi model đã đổi,
    /// chỉ áp vào model + SystemData + MotionConnect khi bấm Save System.</summary>
    void SettingPage::LoadZSafe()
    {
        m_zSafeModel = sca::McDataManager::Model();
        if (auto sys = Sys()) ZSafeBox().Value(sys->ZSafe);
    }

    void SettingPage::UpdateStaticInfo()
    {
        std::string root = ::base::PathToU8(sca::AppPaths::Root());
        ui::SetText(DataRootText(), sca::Loc::T("Setting.DataFolderPrefix") + root);
        ToolTipService::SetToolTip(DataRootText(), box_value(ui::H(root)));
        ui::Show(FallbackRootText(), sca::AppPaths::UsingFallbackRoot());
        ui::Show(AlarmCatalogText(), !sca::AlarmCatalog::Loaded());
    }

    bool SettingPage::CommitEdits(bool includeZSafe, const std::string& action)
    {
        std::vector<std::string> errors;
        if (includeZSafe && std::isnan(ZSafeBox().Value())) errors.push_back("Z_SAFE (mm): not a number");
        for (auto& e : m_texts.CommitAll()) errors.push_back(std::move(e));
        for (auto& e : m_axisFields.CommitAll()) errors.push_back(std::move(e));
        if (errors.empty()) return true;

        ui::Dialogs::Warn(action + " — invalid fields (red border):\n• " + ::base::Join(errors, "\n• ") +
                              "\n\nFix (or retype the old value), then retry.",
                          "Invalid value");
        return false;
    }

    // ══════════ Lưu (SE-1…SE-6) ══════════

    void SettingPage::ShowSaveResult(bool ok, const std::string& text)
    {
        // Màu như InfoBar Success / Error: nền nhạt, biểu tượng đậm cùng tông.
        auto background = ui::Res(ok ? L"SuccessBackgroundBrush" : L"CriticalBackgroundBrush");
        SaveResultSlot().Background(background);
        SaveResultSlot().BorderBrush(background);
        SaveResultIcon().Glyph(ok ? L"" : L"");
        SaveResultIcon().Foreground(ui::Res(ok ? L"SuccessBrush" : L"CriticalBrush"));
        ui::SetNotice(SaveResultSlot(), SaveResultText(), text);
    }

    void SettingPage::OnSaveClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        if (!CommitEdits(true, "system.json not saved")) return;
        bool ok = false;
        std::string text;
        try
        {
            double z = ZSafeBox().Value();
            auto sys = Sys();
            // Z_SAFE và solution là của model đang dùng (Tuấn 2026-11-07); system.json giữ bản sao cho model chưa có.
            bool modelOk = sca::McDataManager::SaveModelMachineValues(z, sys->VisionSolutionPath);
            sca::McControl::Motion().SetZSafe(z);
            ok = sca::McDataManager::SaveSystemData() && modelOk;
            text = ok ? sca::Loc::F("Setting.SavedWithModel", sca::McDataManager::Model()->Name) : sca::Loc::T("Setting.SaveFailed");
        }
        catch (const std::exception& e)
        {
            ok = false;
            text = sca::Loc::T("Setting.SaveFailed") + " " + e.what();
        }
        ShowSaveResult(ok, text);
        LoadAll(true);
        LoadZSafe();
        UpdateStaticInfo();
    }

    void SettingPage::OnOpenFolderClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        try
        {
            std::wstring root = sca::AppPaths::Root().wstring();
            ShellExecuteW(nullptr, L"open", L"explorer.exe", root.c_str(), nullptr, SW_SHOWNORMAL);
        }
        catch (...) {}   // như bản WPF: lỗi bỏ qua
    }

    // ══════════ Chế độ Auto (SE-7…SE-10) ══════════
    // Chỉ CHỌN chế độ, không chạy máy (vẫn phải qua nút Start cứng); có hiệu lực ngay, không cần Lưu; máy đang chạy thì
    // McControl từ chối — lý do hiện ở dải Status.Message.

    void SettingPage::OnNozzleClick(WF::IInspectable const& sender, RoutedEventArgs const&)
    {
        if (!m_coreReady) return;
        int nozzle = TagIndex(sender.as<FrameworkElement>().Tag());
        if (nozzle < 0 || nozzle > 2) return;
        sca::McControl::SetOnlyNozzle(nozzle);
        UpdateAutoMode();
    }

    void SettingPage::OnDryRunClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_coreReady) return;
        sca::McControl::SetDryRun(!sca::McControl::Status().DryRun());
        UpdateAutoMode();
    }

    void SettingPage::OnNgStopClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_coreReady) return;
        sca::McControl::SetStopOnVisionNg(!sca::McControl::Status().StopOnVisionNg());
        UpdateAutoMode();
    }

    void SettingPage::UpdateAutoMode()
    {
        auto sys = Sys();
        int nozzle = sys ? sys->OnlyNozzle : 0;
        if (nozzle != m_shownNozzle)
        {
            m_shownNozzle = nozzle;
            // Cả hai tay = xanh, một tay = cam; tô đặc, không quầng (UI-SPEC §0.4).
            ui::SetTint(NozzleBothButton(), nozzle == 0 ? ui::Tint::Green : ui::Tint::None, false);
            ui::SetTint(Nozzle1Button(), nozzle == 1 ? ui::Tint::Caution : ui::Tint::None, false);
            ui::SetTint(Nozzle2Button(), nozzle == 2 ? ui::Tint::Caution : ui::Tint::None, false);
        }

        int dry = sca::McControl::Status().DryRun() ? 1 : 0;
        if (dry != m_shownDryRun)
        {
            m_shownDryRun = dry;
            Button b = DryRunButton();
            if (dry)
            {
                // CautionBrush #9D5D00 như bản WPF (không phải CautionOn), đặc, không quầng.
                b.Background(ui::Res(L"CautionBrush"));
                b.BorderBrush(ui::Res(L"CautionBrush"));
                b.Foreground(ui::Res(L"TextOnAccentBrush"));
            }
            else
            {
                b.ClearValue(Control::BackgroundProperty());
                b.ClearValue(Control::BorderBrushProperty());
                b.ClearValue(Control::ForegroundProperty());
            }
            ui::SetText(DryRunStateText(), dry ? L"ON — no product" : L"OFF");
        }

        int ngStop = sca::McControl::Status().StopOnVisionNg() ? 1 : 0;
        if (ngStop != m_shownNgStop)
        {
            m_shownNgStop = ngStop;
            // Bật = vàng cảnh báo có quầng (máy sẽ tự dừng giữa chu trình), như nút jog Fast.
            ui::SetTint(NgStopButton(), ngStop ? ui::Tint::Yellow : ui::Tint::None, true);
            ui::SetText(NgStopStateText(), ngStop ? L"ON — halt at NG" : L"OFF — NG handled");
        }

        ui::SetNotice(AutoNoticeSlot(), AutoMessageText(), sca::McControl::Status().Message());
    }

    void SettingPage::UpdateCommandStates()
    {
        bool vision = m_coreReady && !m_visionBusy;
        ui::SetEnabled(CheckVarsButton(), vision);
        ui::SetEnabled(CreateVarsButton(), vision);
        ui::SetEnabled(CheckOutputsButton(), vision);
        ui::SetEnabled(ReloadSolutionButton(), m_coreReady);
        ui::SetEnabled(NozzleBothButton(), m_coreReady);
        ui::SetEnabled(Nozzle1Button(), m_coreReady);
        ui::SetEnabled(Nozzle2Button(), m_coreReady);
        ui::SetEnabled(DryRunButton(), m_coreReady);
        ui::SetEnabled(NgStopButton(), m_coreReady);
        if (VisionBusyRing().IsActive() != m_visionBusy) VisionBusyRing().IsActive(m_visionBusy);
    }

    // ══════════ Phân quyền (Tuấn 2026-10-21) ══════════

    void SettingPage::SetupIdleLogoutBox()
    {
        NumberBox box = IdleLogoutBox();
        sp::NumberToggleGroup::SetupNumberBox(box);
        box.Minimum(sca::UserAccess::MinIdleLogoutMin);
        box.Maximum(sca::UserAccess::MaxIdleLogoutMin);
        box.ValueChanged([this](NumberBox const& sender, NumberBoxValueChangedEventArgs const& e) {
            if (m_syncingIdle) return;
            double v = e.NewValue();
            std::string error = std::isnan(v) ? sca::Loc::F("User.IdleRange", sca::UserAccess::MinIdleLogoutMin, sca::UserAccess::MaxIdleLogoutMin)
                                              : sca::UserAccess::SetIdleLogoutMin((int)std::lround(v));
            if (!error.empty()) ui::Dialogs::Warn(error);
            // Từ chối hay làm tròn thì ô về đúng số đang dùng.
            if ((int)std::lround(std::isnan(v) ? -1 : v) != sca::UserAccess::IdleLogoutMin())
            {
                m_syncingIdle = true;
                sender.Value(sca::UserAccess::IdleLogoutMin());
                m_syncingIdle = false;
            }
        });
    }

    void SettingPage::UpdateUserAccess(bool force)
    {
        uint64_t version = sca::UserAccess::Version();
        if (!force && version == m_shownUserVersion) return;
        m_shownUserVersion = version;

        bool engineer = sca::UserAccess::IsAtLeast(sca::EUserLevel::Engineer);
        ui::SetEnabled(ChangePasswordButton(), engineer);
        ui::SetEnabled(IdleLogoutBox(), engineer);

        // Ẩn thì giá trị trong hai card vẫn dùng và vẫn lưu như cũ, chỉ không xem / sửa được (Tuấn 2026-11-02).
        bool master = sca::UserAccess::IsAtLeast(sca::EUserLevel::Master);
        ui::Show(VisionLimitsCard(), master);
        ui::Show(VisionNamesCard(), master);

        bool isDefault = sca::UserAccess::IsDefaultPassword(sca::EUserLevel::Engineer);
        std::string changed = sca::UserAccess::PasswordChangedAt(sca::EUserLevel::Engineer);
        ui::SetText(PasswordStateText(), isDefault ? sca::Loc::T("User.DefaultPassword")
                                                   : changed.empty() ? std::string() : sca::Loc::F("User.PasswordSetAt", changed));
        PasswordStateText().Foreground(ui::Res(isDefault ? L"CautionBrush" : L"TextSecondaryBrush"));

        // Không ghi đè lúc người dùng đang gõ dở trong ô.
        if (IdleLogoutBox().FocusState() == FocusState::Unfocused)
        {
            m_syncingIdle = true;
            IdleLogoutBox().Value(sca::UserAccess::IdleLogoutMin());
            m_syncingIdle = false;
        }
    }

    void SettingPage::OnChangePasswordClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        ui::LoginDialog::ShowChangePassword([weak = get_weak()](bool ok) {
            if (!ok) return;
            ui::Dialogs::Info(sca::Loc::T("User.PasswordChanged"));
            if (auto self = weak.get()) self->UpdateUserAccess(true);
        });
    }

    // ══════════ Vision (SE-21…SE-29) ══════════

    void SettingPage::SetReport(TextBlock const& block, const std::string& text)
    {
        ui::SetText(block, NormalizeNewLines(text));
        ui::Show(block == VarReportText() ? VarReportScroll() : FlowReportScroll(), !text.empty());
    }

    // UpdateSourceTrigger=PropertyChanged của bản WPF: ghi ngay vào SystemData (Reload đọc từ đó).
    void SettingPage::OnSolutionPathChanged(WF::IInspectable const&, TextChangedEventArgs const&)
    {
        if (!m_loaded) return;
        auto sys = Sys();
        if (!sys) return;
        std::string path = ui::U8(SolutionPathBox().Text());
        if (sys->VisionSolutionPath != path) sys->VisionSolutionPath = path;
    }

    winrt::fire_and_forget SettingPage::OnBrowseSolutionClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        auto self = get_strong();
        hstring path;
        try
        {
            path = co_await sp::PickSolutionFileAsync(SolutionPathBox().Text());
        }
        catch (...)
        {
            path = {};
        }
        if (!path.empty()) SolutionPathBox().Text(path);   // TextChanged ghi vào SystemData
    }

    // Nạp lại ngay để có hiệu lực không cần khởi động lại app — đường dẫn solution chỉ được VmSolution đọc MỘT LẦN lúc
    // Init nên phải chủ động gọi lại (khác tên Flow/Output: VisionConnect đọc lại mỗi lần trigger).
    void SettingPage::OnReloadSolutionClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_coreReady) return;
        if (!CommitEdits(false, "Solution not reloaded")) return;
        std::string text;
        try
        {
            // Solution riêng của model đang dùng (Tuấn 2026-11-07): ghi vào file model; Z_SAFE giữ giá trị đang áp dụng.
            sca::McDataManager::SaveModelMachineValues(Sys()->ZSafe, ui::U8(SolutionPathBox().Text()));
            sca::McDataManager::SaveSystemData();
            std::string refused = sca::McControl::ReloadVisionSolution();   // không chặn (nạp trên luồng nền)
            text = refused.empty() ? sca::Loc::T("Setting.ReloadRequested") : refused;
        }
        catch (const std::exception& e)
        {
            text = sca::Loc::F("Setting.Error", std::string(e.what()));
        }
        ui::SetClampedText(ReloadResultText(), text);
    }

    void SettingPage::OnCheckVarsClick(WF::IInspectable const&, RoutedEventArgs const&) { RunGlobalVarSync(false); }

    void SettingPage::OnCreateVarsClick(WF::IInspectable const&, RoutedEventArgs const&) { RunGlobalVarSync(true); }

    /// <summary>Kiểm tra hoặc tạo biến toàn cục cho solution đang nạp. Tạo thì kiểm tra trước để hỏi xác nhận kèm số
    /// biến sẽ tạo và file .solw sẽ bị lưu.</summary>
    winrt::fire_and_forget SettingPage::RunGlobalVarSync(bool apply)
    {
        auto self = get_strong();
        if (m_visionBusy || !m_coreReady) co_return;
        m_visionBusy = true;
        UpdateCommandStates();
        SetReport(VarReportText(), sca::Loc::T(apply ? "Setting.CheckingBeforeCreate" : "Setting.Checking"));

        std::string text;
        bool done = false;
        if (apply)
        {
            sca::GlobalVarSyncReport dry;
            std::string error;
            co_await sp::Background([&dry, &error] {
                try { dry = sca::McControl::SyncVisionGlobalVariables(false); }
                catch (const std::exception& e) { error = e.what(); }
            });

            if (!error.empty())
            {
                text = sca::Loc::F("Setting.Error", error);
                done = true;
            }
            else if (!dry.Error.empty())
            {
                text = dry.ToText();
                done = true;
            }
            else
            {
                int missing = dry.Count(sca::EGlobalVarState::Missing);
                if (missing == 0)
                {
                    text = sca::Loc::T("Setting.NoMissingVars") + "\n" + dry.ToText();
                    done = true;
                }
                else
                {
                    bool yes = false;
                    try
                    {
                        yes = co_await ui::Dialogs::ConfirmAsync(
                            sca::Loc::F("Setting.CreateVarsConfirm", missing) + "\n" + Sys()->VisionSolutionPath + "\n\n" +
                                sca::Loc::T("Setting.CreateVarsConfirmTail"),
                            sca::Loc::T("Setting.CreateVarsTitle"));
                    }
                    catch (...)
                    {
                        yes = false;
                    }
                    if (!yes)
                    {
                        text = sca::Loc::T("Setting.CreateVarsCancelled");
                        done = true;
                    }
                    else
                    {
                        SetReport(VarReportText(), sca::Loc::F("Setting.CreatingVars", missing));
                    }
                }
            }
        }

        if (!done)
        {
            sca::GlobalVarSyncReport report;
            std::string error;
            co_await sp::Background([&report, &error, apply] {
                try { report = sca::McControl::SyncVisionGlobalVariables(apply); }
                catch (const std::exception& e) { error = e.what(); }
            });
            text = error.empty() ? report.ToText() : sca::Loc::F("Setting.Error", error);
        }

        SetReport(VarReportText(), text);
        m_visionBusy = false;
        UpdateCommandStates();
    }

    /// <summary>Đối chiếu Output Params của ba Flow với hợp đồng. Chỉ đọc khai báo — không chạy Flow nào, nên không cần
    /// hỏi xác nhận như nút tạo biến (nút đó lưu đè file .solw).</summary>
    winrt::fire_and_forget SettingPage::OnCheckOutputsClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        auto self = get_strong();
        if (m_visionBusy || !m_coreReady) co_return;
        m_visionBusy = true;
        UpdateCommandStates();
        SetReport(FlowReportText(), sca::Loc::T("Setting.ReadingFlowOutputs"));

        sca::VmOutCheckReport report;
        std::string error;
        co_await sp::Background([&report, &error] {
            try { report = sca::McControl::CheckVisionFlowOutputs(); }
            catch (const std::exception& e) { error = e.what(); }
        });
        SetReport(FlowReportText(), error.empty() ? report.ToText() : sca::Loc::F("Setting.Error", error));

        m_visionBusy = false;
        UpdateCommandStates();
    }

    // ══════════ Vòng cập nhật ══════════

    void SettingPage::OnTick()
    {
        if (!m_loaded) return;
        UpdateUserAccess();
        if (!m_coreReady) return;

        UpdateAutoMode();
        UpdateCommandStates();
        if (m_section == SectionLog) m_log.Update();
    }

    void SettingPage::OnShown(bool visible)
    {
        if (!visible || !m_loaded) return;
        // Ô người vận hành đang sửa dở (chữ sai chưa ghi được) giữ nguyên; Z_SAFE là giá trị riêng của trang, chỉ nạp lại
        // khi đã đổi model (Z_SAFE theo model, Tuấn 2026-11-07).
        LoadAll(false);
        if (sca::McDataManager::Model() != m_zSafeModel) LoadZSafe();
        UpdateStaticInfo();
    }
}
