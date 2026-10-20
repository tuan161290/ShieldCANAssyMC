#include "pch.h"
#include "VisionEditPage.xaml.h"
#if __has_include("VisionEditPage.g.cpp")
#include "VisionEditPage.g.cpp"
#endif

#include <cmath>
#include <format>

#include "Base/Async.h"
#include "Base/Text.h"
#include "Controller/McControl.h"
#include "Controller/VmCycle.h"
#include "Data/McDataManager.h"
#include "Infor/AxisId.h"
#include "Infor/IoMap.h"
#include "Lang/Loc.h"
#include "Ui/AppHost.h"
#include "Ui/Controls.h"
#include "Ui/Dialogs.h"
#include "Ui/JogControl.h"
#include "Ui/UiUtil.h"
#include "Ui/VisionEditPageParts.h"
#include "Utility/Log/LogWorker.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
namespace WF = winrt::Windows::Foundation;

namespace
{
    using sca::Loc;
    using sca::McControl;
    using sca::McDataManager;

    /// Token mới cho mỗi lời gọi — McControl thay token khi STOP huỷ nó (C#: McControl.ManualToken đọc mỗi lần gọi).
    std::stop_token MT() { return McControl::ManualToken(); }

    std::string NoteSuffix(std::string const& note, const char* sep = "\n")
    {
        return note.empty() ? std::string() : sep + note;
    }

    std::string CellLabel(int row, int col)
    {
        return "(" + std::to_string(row + 1) + "," + std::to_string(col + 1) + ")";
    }

    /// Đặt tooltip từ khoá Loc (chữ resx nguyên văn).
    void Tip(DependencyObject const& e, const char* key)
    {
        ToolTipService::SetToolTip(e, box_value(ui::T(key)));
    }
}

namespace winrt::ShieldCanAssy::implementation
{
    // ═════════════════════════ dựng ═════════════════════════

    void VisionEditPage::OnRootLoaded(WF::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_loaded) return;
        m_loaded = true;

        BuildJogGrid();
        BuildNumpad();
        BuildNumberBoxes();
        BuildStepCombo();
        ApplyStaticTexts();
        AttachHandlers();
        LoadInputs();

        m_syncing = true;
        ToolSelector().SelectedItem(Tool1Item());
        m_syncing = false;
        SelectTool(1);
        SelectStep(0);

        SetGate({});
        SetVerifyResult({});
        SetHandEyeStep({});
        SetStepResult({});
        SetSequenceStatus({});
        ApplyBusy();

        if (m_coreReady) OnTick();
        if (m_visible) QueueVmWork();   // OnShown(true) tới trước Loaded thì EnsureVmPanes đã bỏ qua
    }

    // Lưới 3 cột × 2 hàng: X, Z1, Z2 / Y2, A1, A2 — cột 1 gantry, cột 2 tay #1, cột 3 tay #2 (Tuấn sắp 2026-09-26).
    void VisionEditPage::BuildJogGrid()
    {
        const char* axes[] = { sca::AxisId::X, sca::AxisId::Z1, sca::AxisId::Z2, sca::AxisId::Y2, sca::AxisId::A1, sca::AxisId::A2 };
        for (int i = 0; i < 6; ++i)
        {
            auto cell = ui::visionedit::BuildJogCell(axes[i], m_jogButtons);
            Grid::SetRow(cell, i / 3);
            Grid::SetColumn(cell, i % 3);
            JogGrid().Children().Append(cell);
        }
    }

    // Numpad 7 8 9 / 4 5 6 / 1 2 3 khớp hình minh hoạ người dùng gửi; 5 = Calibration Center Point. Bấm đúp.
    void VisionEditPage::BuildNumpad()
    {
        for (int n = 1; n <= 9; ++n)
        {
            Button b;
            b.Content(box_value(winrt::to_hstring(n)));
            b.Height(48);
            b.HorizontalAlignment(HorizontalAlignment::Stretch);
            Tip(b, n == 5 ? "Vision.GridPoint5Tip" : "Vision.GridPointTip");
            // Một closure mỗi số (như 9 command riêng của view-model): không có đường nào ép kiểu nhầm số điểm.
            ui::AttachDoubleTap(b, [weak = get_weak(), n] {
                if (auto self = weak.get()) self->RunTopDownGridPoint(n);
            });
            int idx = n - 1;
            Grid::SetRow(b, 2 - idx / 3);
            Grid::SetColumn(b, idx % 3);
            Numpad().Children().Append(b);
        }
    }

    void VisionEditPage::BuildNumberBoxes()
    {
        auto make = [this](StackPanel const& host, int decimals, wchar_t const* unit, double sca::SystemData::* field) {
            NumberBox box;
            ui::ConfigureNumberBox(box, decimals);
            box.Width(120);
            // Ghi khi xác nhận (Enter / rời ô) — như binding TextBox của WPF. Ô trống: trả lại số cũ.
            box.ValueChanged([weak = get_weak(), field](NumberBox const& s, NumberBoxValueChangedEventArgs const&) {
                auto self = weak.get();
                if (!self || self->m_syncing) return;
                auto sys = McDataManager::SysData();
                if (!sys) return;
                double v = s.Value();
                if (std::isnan(v))
                {
                    self->m_syncing = true;
                    s.Value((*sys).*field);
                    self->m_syncing = false;
                    return;
                }
                (*sys).*field = v;
            });
            host.Children().Append(ui::UnitNumber(box, unit));
            return box;
        };
        m_gridStepXBox = make(GridStepXHost(), 3, L"mm", &sca::SystemData::TopDownCalibStepXMm);
        m_gridStepYBox = make(GridStepYHost(), 3, L"mm", &sca::SystemData::TopDownCalibStepYMm);
        m_verifyTravelBox = make(VerifyTravelHost(), 3, L"mm", &sca::SystemData::TopDownVerifyStepMm);
        m_testAngleBox = make(TestAngleHost(), 2, L"°", &sca::SystemData::AlignVerifyAngleDeg);

        // Ô của khu chạy kiểm từng bước: số nguyên ≥ 1, giữ trong trang (mặc định 1, như view-model).
        for (auto box : { SeqAssyRowBox(), SeqAssyColBox(), SeqInputRowBox(), SeqInputColBox() })
        {
            ui::ConfigureNumberBox(box, 0);
            box.Minimum(1);
            box.HorizontalAlignment(HorizontalAlignment::Stretch);
        }
    }

    // Tên bước KHÔNG mang số thứ tự (luật Tuấn 15/09); thứ tự nằm ở thứ tự danh sách. Không ghép số Tool vào tên: đổi Tool
    // không được làm Combobox nhảy về bước đầu.
    void VisionEditPage::BuildStepCombo()
    {
        const char* keys[] = { "Vision.StepTopDown", "Vision.StepBottomUp", "Vision.StepRotation",
                               "Vision.StepHandEye", "Vision.StepVerify", "Vision.StepSequence" };
        for (const char* key : keys)
        {
            ComboBoxItem item;
            TextBlock t;
            t.Text(ui::T(key));
            t.TextWrapping(TextWrapping::Wrap);
            item.Content(t);
            StepCombo().Items().Append(item);
        }
    }

    void VisionEditPage::ApplyStaticTexts()
    {
        // Chú thích dài và nhãn có trong resx: lấy nguyên văn theo khoá.
        std::pair<TextBlock, const char*> texts[] = {
            { HeaderNoteText(), "Vision.HeaderNote" },
            { StepsLabelText(), "Vision.StepsLabel" },
            { TopDownNoteHeader(), "Vision.TopDownNoteHeader" },
            { TopDownNoteText(), "Vision.TopDownNote" },
            { BottomUpNoteHeader(), "Vision.BottomUpNoteHeader" },
            { BottomUpNoteText(), "Vision.BottomUpNote" },
            { HandEyeNoteHeader(), "Vision.HandEyeNoteHeader" },
            { HandEyeNoteText(), "Vision.HandEyeNote" },
            { HeRetestHeader(), "Vision.HeRetestHeader" },
            { HeRetestNoteText(), "Vision.HeRetestNote" },
            { AcceptHeaderText(), "Vision.AcceptHeader" },
            { AcceptNoteText(), "Vision.AcceptNote" },
            { SeqHeaderText(), "Vision.SeqHeader" },
            { SeqNoteText(), "Vision.SeqNote" },
        };
        for (auto& [block, key] : texts) block.Text(ui::T(key));

        std::pair<DependencyObject, const char*> tips[] = {
            { Z1SafeButton(), "Vision.Z1SafeTip" },
            { Z2SafeButton(), "Vision.Z2SafeTip" },
            { Vac1Button(), "Vision.Vacuum1Tip" },
            { Vac2Button(), "Vision.Vacuum2Tip" },
            { StopAllButton(), "Vision.StopTip" },
            { StepCombo(), "Vision.StepsTip" },
            { SaveCenterButton(), "Vision.SaveCenterTip" },
            { AutoCalibButton(), "Vision.AutoCalibTip" },
            { TopLoadHereButton(), "Vision.TriggerTopCalibTip" },
            { VerifyTopButton(), "Vision.VerifyTopDownTip" },
            { RunAlignCalibButton(), "Vision.RunAlignSeqTip" },
            { AlignLoadHereButton(), "Vision.TriggerAlignCalibTip" },
            { AxisAZeroButton(), "Vision.AxisAToZeroTip" },
            { RotationTestButton(), "Vision.RotationTestTip" },
            { He1Button(), "Vision.HeGoToPickTip" },
            { He2Button(), "Vision.HePickTip" },
            { He3Button(), "Vision.HeLocateTip" },
            { He4Button(), "Vision.HeGoToAlignTip" },
            { He5Button(), "Vision.HeCommitTip" },
            { He6Button(), "Vision.HeLocateHereTip" },
            { HeCellButton(), "Vision.HeCellPickerTip" },
            { He7Button(), "Vision.HeGoToAlign7Tip" },
            { He8Button(), "Vision.HeCheckTargetTip" },
            { He9Button(), "Vision.HeGoToTargetTip" },
            { HeReturnButton(), "Vision.HeReturnTip" },
            { DryPlaceButton(), "Vision.DryRunPlaceTip" },
            { Seq1Button(), "Vision.SeqMoveCamTip" },
            { Seq2Button(), "Vision.SeqLocateTopTip" },
            { Seq3Button(), "Vision.SeqPickTip" },
            { Seq4Button(), "Vision.SeqMoveToAlignTip" },
            { Seq5Button(), "Vision.SeqAlignBottomTip" },
            { Seq6Button(), "Vision.SeqMoveToCmdTip" },
        };
        for (auto& [e, key] : tips) Tip(e, key);

        // Nút có huy hiệu số bước (UI-SPEC §6.7, §6.9): số ở huy hiệu, nhãn ≤ 2 từ.
        using ui::visionedit::SetBadge;
        SetBadge(He1Button(), 1, L"Go Jig");
        SetBadge(He2Button(), 2, L"Pick Can");
        SetBadge(He3Button(), 3, L"Locate Target");
        SetBadge(He4Button(), 4, L"Go Bottom");
        SetBadge(He5Button(), 5, L"Save Hand-eye");
        SetBadge(He6Button(), 6, L"Locate Part", &m_he6Caption);
        SetBadge(He7Button(), 7, L"Go Bottom");
        SetBadge(He8Button(), 8, L"Calc Target");
        SetBadge(He9Button(), 9, L"Go Target");
        SetBadge(Seq1Button(), 1, L"Go CAM");
        SetBadge(Seq2Button(), 2, L"Locate Top");
        SetBadge(Seq3Button(), 3, L"Pick");
        SetBadge(Seq4Button(), 4, L"Go Bottom");
        SetBadge(Seq5Button(), 5, L"Align Bottom");
        SetBadge(Seq6Button(), 6, L"Go Place");
        ui::SetText(m_he6Caption, std::string("(in place)"));
        HeCellButton().Content(box_value(ui::T("Vision.HeCellButton")));
    }

    // Mọi nút làm trục chạy trên trang đều BẤM ĐÚP (Tuấn đồng bộ 2026-09-26). Nút chỉ kích Flow tại chỗ hay chỉ đọc thì
    // bấm một lần (Click trong XAML).
    void VisionEditPage::AttachHandlers()
    {
        auto dbl = [this](Button const& b, std::function<void(VisionEditPage&)> fn) {
            ui::AttachDoubleTap(b, [weak = get_weak(), fn = std::move(fn)] {
                if (auto self = weak.get()) fn(*self);
            });
        };
        using CR = sca::Routine_Calib;

        dbl(Z1SafeButton(), [](VisionEditPage& p) { p.MoveZSafe(1); });
        dbl(Z2SafeButton(), [](VisionEditPage& p) { p.MoveZSafe(2); });

        dbl(AutoCalibButton(), [](VisionEditPage& p) { p.RunAutoCalib(); });
        dbl(VerifyTopButton(), [](VisionEditPage& p) { p.VerifyTopDown(); });

        dbl(RunAlignCalibButton(), [](VisionEditPage& p) { p.RunAlignCalibSequence(); });
        dbl(AlignLoadHereButton(), [](VisionEditPage& p) { p.TriggerAlignCalibFlow(); });
        dbl(AxisAZeroButton(), [](VisionEditPage& p) { p.MoveAlignAxisToZero(); });

        dbl(RotationTestButton(), [](VisionEditPage& p) { p.VerifyAlignRotation(); });

        dbl(He1Button(), [](VisionEditPage& p) {
            p.HandEyeStep(Loc::T("Vision.HeTitleGoToPick"), [](CR& c, int n, std::stop_token ct) { return c.HandEyeGoToPick(n, ct); });
        });
        dbl(He2Button(), [](VisionEditPage& p) { p.HandEyePickWithConfirm(); });
        dbl(He3Button(), [](VisionEditPage& p) {
            p.HandEyeStep(Loc::T("Vision.HeTitleLocate"), [](CR& c, int n, std::stop_token ct) { return c.HandEyeLocate(n, ct); });
        });
        dbl(He4Button(), [](VisionEditPage& p) {
            p.HandEyeStep(Loc::T("Vision.GoToAlign4"), [](CR& c, int n, std::stop_token ct) { return c.HandEyeGoToAlign(n, ct); });
        });
        dbl(He5Button(), [](VisionEditPage& p) { p.HandEyeCommit(); });
        dbl(He6Button(), [](VisionEditPage& p) { p.HandEyeLocateHere(); });
        dbl(He7Button(), [](VisionEditPage& p) {
            p.HandEyeStep(Loc::T("Vision.GoToAlign7"), [](CR& c, int n, std::stop_token ct) { return c.HandEyeGoToAlignForTest(n, ct); });
        });
        dbl(He8Button(), [](VisionEditPage& p) {
            p.HandEyeStep(Loc::T("Vision.HeCheckTarget"), [](CR& c, int n, std::stop_token ct) { return c.HandEyeCheckTarget(n, ct); });
        });
        // Nút 9 hạ Z tới điểm giảm tốc (không chạm): chặn bấm nhầm bằng bấm đúp, không hỏi lại (Tuấn đổi 2026-09-26).
        dbl(He9Button(), [](VisionEditPage& p) {
            p.HandEyeStep(Loc::T("Vision.HeGoToTarget"), [](CR& c, int n, std::stop_token ct) { return c.HandEyeGoToTarget(n, ct); });
        });
        dbl(HeReturnButton(), [](VisionEditPage& p) { p.HandEyeReturnWithConfirm(); });

        dbl(DryPlaceButton(), [](VisionEditPage& p) { p.DryRunPlaceAlign(); });

        dbl(Seq1Button(), [](VisionEditPage& p) {
            int r = p.SeqValue(p.SeqAssyRowBox(), p.m_stepAssyRow) - 1;
            int c = p.SeqValue(p.SeqAssyColBox(), p.m_stepAssyCol) - 1;
            p.StepRun(Loc::T("Vision.SeqMoveCamTitle"),
                      [r, c](CR& cal, int, std::stop_token ct) { return cal.StepMoveCamToCell(r, c, ct); }, Report::Step, p.m_tool);
        });
        dbl(Seq3Button(), [](VisionEditPage& p) { p.StepPick(); });
        dbl(Seq4Button(), [](VisionEditPage& p) {
            p.StepRun(Loc::T("Vision.SeqMoveToAlignTitle"),
                      [](CR& cal, int n, std::stop_token ct) { return cal.StepMoveToAlign(n, ct); }, Report::Step, p.m_tool);
        });
        dbl(Seq5Button(), [](VisionEditPage& p) { p.StepAlignBottom(); });
        dbl(Seq6Button(), [](VisionEditPage& p) { p.StepMoveToCommand(); });
    }

    void VisionEditPage::LoadInputs()
    {
        if (!m_loaded) return;
        auto sys = McDataManager::SysData();
        if (!sys) return;
        m_syncing = true;
        m_gridStepXBox.Value(sys->TopDownCalibStepXMm);
        m_gridStepYBox.Value(sys->TopDownCalibStepYMm);
        m_verifyTravelBox.Value(sys->TopDownVerifyStepMm);
        m_testAngleBox.Value(sys->AlignVerifyAngleDeg);
        AlignXOffsetsBox().Text(ui::H(sys->AlignCalibXOffsetsMm));
        AlignAnglesBox().Text(ui::H(sys->AlignCalibAnglesDeg));
        SeqAssyRowBox().Value(m_stepAssyRow);
        SeqAssyColBox().Value(m_stepAssyCol);
        SeqInputRowBox().Value(m_stepInputRow);
        SeqInputColBox().Value(m_stepInputCol);
        m_syncing = false;
    }

    void VisionEditPage::CommitAlignLists()
    {
        if (!m_loaded) return;
        auto sys = McDataManager::SysData();
        if (!sys) return;
        std::string x = ui::U8(AlignXOffsetsBox().Text());
        std::string a = ui::U8(AlignAnglesBox().Text());
        if (x != sys->AlignCalibXOffsetsMm) sys->AlignCalibXOffsetsMm = x;
        if (a != sys->AlignCalibAnglesDeg) sys->AlignCalibAnglesDeg = a;
    }

    void VisionEditPage::OnAlignListLostFocus(WF::IInspectable const&, RoutedEventArgs const&)
    {
        CommitAlignLists();
    }

    int VisionEditPage::SeqValue(NumberBox const& box, int& field)
    {
        double v = box.Value();
        if (std::isnan(v) || v < 1)
        {
            m_syncing = true;
            box.Value(field);
            m_syncing = false;
        }
        else field = static_cast<int>(std::lround(v));
        return field;
    }

    // ═════════════════════════ trạng thái ═════════════════════════

    void VisionEditPage::OnCoreReady()
    {
        m_coreReady = true;
        LoadInputs();
    }

    void VisionEditPage::OnTick()
    {
        if (!m_loaded || !m_coreReady) return;
        ApplyBusy();

        // Mức Chậm/Nhanh dùng chung với trang Teaching — có thể vừa đổi ở đó.
        auto func = McControl::Func();
        ui::SetJogSpeedButtons(JogSlowButton(), JogFastButton(), func && func->JogFast());   // SetTint bỏ qua khi không đổi

        // Van hút: màu theo DO thật (routine hiệu chuẩn cũng tự bật/tắt hút).
        if (auto io = McControl::Io())
        {
            m_vac1On = io->GetDo(sca::IoMap::DoVacuum1);
            m_vac2On = io->GetDo(sca::IoMap::DoVacuum2);
        }
        ui::SetTint(Vac1Button(), m_vac1On ? ui::Tint::Green : ui::Tint::None, true);
        ui::SetTint(Vac2Button(), m_vac2On ? ui::Tint::Green : ui::Tint::None, true);

        UpdateCalibTexts();

        // Solution vừa được nạp lại (trang Setting): gắn lại trình soạn Flow.
        if (m_visible && m_config.Created() && McControl::VisionLoadCount() != m_boundLoadCount)
        {
            auto vision = McControl::Vision();
            if (vision && vision->IsSolutionLoaded()) QueueVmWork();
        }
    }

    void VisionEditPage::UpdateCalibTexts()
    {
        auto sys = McDataManager::SysData();
        auto model = McDataManager::Model();
        auto cal = McControl::Calib();

        // Cổng Auto, gom một dòng: lần kiểm tra hiệu chuẩn gần nhất.
        std::optional<sca::VmCalibGate> last = cal ? cal->LastCalibGate() : std::nullopt;
        ui::SetClampedText(ReadyText(), last ? last->Describe() : Loc::T("Vision.ReadyHint"));

        if (sys)
        {
            ui::SetClampedText(CenterText(), std::format("Calibration Center Point: X={} Y={} {}",
                Loc::FormatValue(sys->TopDownCalibCenterX, "0.000"), Loc::FormatValue(sys->TopDownCalibCenterY, "0.000"),
                Loc::T(sys->TopDownCalibCenterSet ? "Vision.CenterSaved" : "Vision.CenterNotSaved")));
        }

        if (model && sys)
        {
            const auto& c = model->Calib;
            int n = m_tool;

            // Không hiện scale mm/px hay RMS — App không giữ ma trận nào; chất lượng fit đọc trong VisionMaster.
            std::string align;
            if (!c.IsAlignVerified(n)) align = Loc::T("Vision.AlignNotVerified");
            else
            {
                std::string expired = c.IsExpired(sys->CalibValidDays) ? Loc::T("Vision.Expired") + " — " : std::string();
                align = Loc::F("Vision.AlignStatus", expired, c.AlignAngleSign(n) > 0 ? "+1" : "−1",
                               n == 1 ? c.AlignVerifyResidual1 : c.AlignVerifyResidual2, c.AlignVerifiedPosX(n), c.AlignVerifiedAt(n));
            }
            ui::SetText(AlignStatusText(), align);

            std::string he;
            if (c.IsHandEyeOutdated(n)) he = Loc::T("Vision.HandEyeOutdated");
            else if (!c.IsHandEyeMeasured(n)) he = Loc::T("Vision.HandEyeNotMeasured");
            else he = Loc::F("Vision.HandEyeStatus", c.HandEyeDx(n), c.HandEyeDy(n), c.HandEyeMeasuredAt(n), c.HandEyeAngleRef(n));
            ui::SetClampedText(HandEyeStatusText(), he);
        }
    }

    void VisionEditPage::SelectTool(int tool)
    {
        if (tool != 1 && tool != 2) return;
        m_tool = tool;
        if (m_loaded && m_coreReady) UpdateCalibTexts();
    }

    void VisionEditPage::SelectStep(int step)
    {
        if (step < 0 || step > 5) return;
        m_step = step;
        if (!m_loaded) return;
        FrameworkElement panels[] = { StepPanelTop(), StepPanelBottom(), StepPanelRotation(),
                                      StepPanelHandEye(), StepPanelAccept(), StepPanelSequence() };
        for (int i = 0; i < 6; ++i) ui::Show(panels[i], i == step);
        if (StepCombo().SelectedIndex() != step)
        {
            m_syncing = true;
            StepCombo().SelectedIndex(step);
            m_syncing = false;
        }
    }

    void VisionEditPage::ShowShotVariant(int index)
    {
        SelectStep(index);
    }

    void VisionEditPage::OnToolChanged(SelectorBar const& sender, SelectorBarSelectionChangedEventArgs const&)
    {
        if (m_syncing) return;
        auto sel = sender.SelectedItem();
        if (!sel) return;
        SelectTool(sel == Tool2Item() ? 2 : 1);
    }

    void VisionEditPage::OnStepChanged(WF::IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (m_syncing || !m_loaded) return;
        int idx = StepCombo().SelectedIndex();
        if (idx >= 0) SelectStep(idx);
    }

    void VisionEditPage::SetBusy(bool busy)
    {
        m_busy = busy;
        ApplyBusy();
    }

    // Busy khoá cả cột hiệu chuẩn, jog, Z Safe và van hút — tay không giành trục/van với routine. STOP không bao giờ.
    void VisionEditPage::ApplyBusy()
    {
        if (!m_loaded || m_appliedBusy == m_busy) return;
        m_appliedBusy = m_busy;
        bool en = !m_busy;
        CalibPanel().IsEnabled(en);
        for (auto& b : m_jogButtons) b.IsEnabled(en);
        for (auto b : { Z1SafeButton(), Z2SafeButton(), Vac1Button(), Vac2Button() }) b.IsEnabled(en);
        BusyRing().IsActive(m_busy);
        // Giữ chỗ (ShowInPlace), không Collapsed: dòng này nằm trên cả cột nút — xem SequenceRow trong XAML.
        ui::ShowInPlace(BusyRing(), m_busy);
        ui::ShowInPlace(SequenceRow(), m_busy || !m_sequenceStatus.empty());
    }

    void VisionEditPage::SetSequenceStatus(std::string text)
    {
        m_sequenceStatus = std::move(text);
        if (!m_loaded) return;
        ui::SetClampedText(SequenceText(), m_sequenceStatus, SequenceRow());
        ui::ShowInPlace(SequenceRow(), m_busy || !m_sequenceStatus.empty());
    }

    // Kết quả phép kiểm tra gần nhất — top-down và vòng xoay ghi CHUNG một ô; hiện ở cả hai bước (một trong hai đang ẩn).
    void VisionEditPage::SetVerifyResult(std::string text)
    {
        if (!text.empty()) m_verifyResult = std::move(text);
        if (!m_loaded) return;
        std::string shown = m_verifyResult ? *m_verifyResult : Loc::T("Vision.NotChecked");
        ui::SetText(VerifyResultTopText(), shown);
        ui::SetText(VerifyResultRotText(), shown);
    }

    // Ô RIÊNG của khu hand-eye — hai khu nút ghi chung một ô thì bấm khu này xoá số của khu kia.
    void VisionEditPage::SetHandEyeStep(std::string text)
    {
        if (!text.empty()) m_handEyeStep = std::move(text);
        if (m_loaded) ui::SetText(HandEyeStepText(), m_handEyeStep ? *m_handEyeStep : Loc::T("Vision.NotRunYet"));
    }

    void VisionEditPage::SetStepResult(std::string text)
    {
        if (!text.empty()) m_stepResult = std::move(text);
        if (m_loaded) ui::SetText(StepResultText(), m_stepResult ? *m_stepResult : Loc::T("Vision.NotRunYet"));
    }

    void VisionEditPage::SetGate(std::string text)
    {
        if (!text.empty()) m_gate = std::move(text);
        if (m_loaded) ui::SetClampedText(GateText(), m_gate ? *m_gate : Loc::T("Vision.NotCalibChecked"));
    }

    void VisionEditPage::RefreshGate()
    {
        if (auto cal = McControl::Calib()) SetGate(cal->CheckCalib().Describe());
        if (m_coreReady) UpdateCalibTexts();
    }

    std::string VisionEditPage::HandEyeLocateLabel() const
    {
        return m_heCellRow < 0 ? Loc::T("Vision.HeLocateHere") : Loc::F("Vision.HeLocateCell", CellLabel(m_heCellRow, m_heCellCol));
    }

    bool VisionEditPage::OrgOk() const
    {
        return McControl::Status().OrgOk();
    }

    // ═════════════════════════ chạy ═════════════════════════

    void VisionEditPage::PostToPage(std::function<void(VisionEditPage&)> fn)
    {
        ui::PostUi([weak = get_weak(), fn = std::move(fn)] {
            if (auto self = weak.get(); self && !self->m_closing) fn(*self);
        });
    }

    void VisionEditPage::RunJob(std::function<void(sca::Routine_Calib&)> work,
                                std::function<void(VisionEditPage&, std::string const&)> done)
    {
        SetBusy(true);
        auto err = std::make_shared<std::string>();
        ui::RunBackgroundThenUi(
            [work = std::move(work), err] {
                auto cal = McControl::Calib();
                if (!cal) { *err = "Machine not initialized."; return; }
                try { work(*cal); }
                catch (const ::base::OperationCanceled&) { *err = Loc::T("Calib.Cancelled"); }
                catch (const std::exception& ex) { *err = ex.what(); }
            },
            [weak = get_weak(), done = std::move(done), err] {
                auto self = weak.get();
                if (!self || self->m_closing) return;
                self->SetBusy(false);
                // Routine_Calib không ném; tới đây chỉ khi máy chưa khởi tạo hoặc một lệnh trục bị huỷ giữa chừng.
                if (!err->empty())
                {
                    self->SetSequenceStatus({});
                    ui::Dialogs::Warn(*err);
                    return;
                }
                if (done) done(*self, *err);
            });
    }

    void VisionEditPage::StepRun(std::string title, StepBody body, Report report, int nozzle,
                                 std::function<void(VisionEditPage&, sca::StepRunResult const&)> after)
    {
        if (m_busy) return;
        if (!OrgOk()) { ui::Dialogs::Warn(Loc::T("Vision.NoOrigin")); return; }

        auto r = std::make_shared<sca::StepRunResult>();
        RunJob(
            [body = std::move(body), nozzle, r](sca::Routine_Calib& cal) { *r = body(cal, nozzle, MT()); },
            [title = std::move(title), report, r, after = std::move(after)](VisionEditPage& p, std::string const&) {
                std::string line = r->Ok ? r->Text : Loc::F("Vision.StepStopped", title, r->Reason);
                if (report == Report::HandEye) p.SetHandEyeStep(line);
                else p.SetStepResult(line);
                if (!r->Ok) ui::Dialogs::Warn(Loc::F("Vision.StepStopped", title, r->Reason));
                if (after) after(p, *r);
            });
    }

    void VisionEditPage::HandEyeStep(std::string title, StepBody body)
    {
        StepRun(std::move(title), std::move(body), Report::HandEye, m_tool);
    }

    // ═════════════════════════ 6.1 thanh Jog ═════════════════════════

    // "Z1 Safe" / "Z2 Safe" — một lệnh Move tới Z_SAFE, không phải chu trình về gốc.
    void VisionEditPage::MoveZSafe(int nozzle)
    {
        StepRun(Loc::F("Vision.ZSafeTitle", nozzle == 2 ? sca::AxisId::Z2 : sca::AxisId::Z1),
                [](sca::Routine_Calib& cal, int n, std::stop_token ct) { return cal.StepMoveZSafe(n, ct); }, Report::Step, nozzle);
    }

    void VisionEditPage::OnVac1Click(WF::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_busy) return;
        bool want = !m_vac1On;
        ui::PostMotionCommand([want] { if (auto f = McControl::Func()) f->SetVacuum(1, want); });
    }

    void VisionEditPage::OnVac2Click(WF::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_busy) return;
        bool want = !m_vac2On;
        ui::PostMotionCommand([want] { if (auto f = McControl::Func()) f->SetVacuum(2, want); });
    }

    void VisionEditPage::OnJogSlowClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        if (auto f = McControl::Func()) f->SetJogFast(false);
        ui::SetJogSpeedButtons(JogSlowButton(), JogFastButton(), false);
    }

    void VisionEditPage::OnJogFastClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        if (auto f = McControl::Func()) f->SetJogFast(true);
        ui::SetJogSpeedButtons(JogSlowButton(), JogFastButton(), true);
    }

    // STOP như nút STOP trang Main, cộng huỷ chuỗi hiệu chuẩn/hand-eye/chạy kiểm đang chạy (Tuấn 2026-10-08): mọi chuỗi của
    // trang chạy trên McControl::ManualToken.
    void VisionEditPage::OnStopAllClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        McControl::RequestStop();
    }

    // ═════════════════════════ 6.3 luôn hiện ═════════════════════════

    void VisionEditPage::OnCheckCalibClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        RefreshGate();
    }

    // ═════════════════════════ 6.4 top-down ═════════════════════════

    // Đọc (X, Y2) hiện tại làm Calibration Center Point và ghi thẳng system.json: trang này không có nút Lưu riêng —
    // trước đây tắt app là mất.
    void VisionEditPage::OnSaveCenterClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        if (!OrgOk()) { ui::Dialogs::Warn(Loc::T("Vision.NoOriginCoords")); return; }
        auto sys = McDataManager::SysData();
        if (!sys) return;
        auto ax = McControl::Motion()[sca::AxisId::X];
        auto ay = McControl::Motion()[sca::AxisId::Y2];
        double x = ax ? ax->CurrentPos() : 0, y = ay ? ay->CurrentPos() : 0;
        sys->TopDownCalibCenterX = x;
        sys->TopDownCalibCenterY = y;
        sys->TopDownCalibCenterSet = true;
        UpdateCalibTexts();
        ui::RunBackground([x, y] {
            bool saved = McDataManager::SaveSystemData();
            ui::Dialogs::Info(Loc::F(saved ? "Vision.CenterSavedOk" : "Vision.CenterSavedDiskFail", x, y));
        });
    }

    bool VisionEditPage::CheckReadyForGrid()
    {
        if (!OrgOk()) { ui::Dialogs::Warn(Loc::T("Vision.NoOriginCalib")); return false; }
        auto sys = McDataManager::SysData();
        if (!sys || !sys->TopDownCalibCenterSet) { ui::Dialogs::Warn(Loc::T("Vision.NoCenter")); return false; }
        return true;
    }

    namespace
    {
        /// Số numpad 1-9 → toạ độ (X, Y2): hệ số hàng/cột quanh tâm (1,2,3 hàng dưới; 7,8,9 hàng trên) × bước lưới.
        struct GridFrame { double CenterX{ 0 }, CenterY{ 0 }, StepX{ 0 }, StepY{ 0 }; };

        GridFrame GridFrameOf(sca::SystemData const& s)
        {
            return { s.TopDownCalibCenterX, s.TopDownCalibCenterY, s.TopDownCalibStepXMm, s.TopDownCalibStepYMm };
        }

        void GridPointXY(GridFrame const& g, int number, double& x, double& y)
        {
            int idx = number - 1;
            int rowMul = idx / 3 - 1;
            int colMul = idx % 3 - 1;
            x = g.CenterX + colMul * g.StepX;
            y = g.CenterY + rowMul * g.StepY;
        }
    }

    void VisionEditPage::RunTopDownGridPoint(int number)
    {
        if (m_busy || !CheckReadyForGrid()) return;
        double x = 0, y = 0;
        GridPointXY(GridFrameOf(*McDataManager::SysData()), number, x, y);
        auto ok = std::make_shared<bool>(false);
        RunJob([x, y, ok](sca::Routine_Calib& cal) { *ok = cal.MoveAndTriggerTopDownCalib(x, y, MT()); },
               [number, ok](VisionEditPage&, std::string const&) {
                   if (!*ok) ui::Dialogs::Warn(Loc::F("Vision.GridPointFail", number));
               });
    }

    // Auto Calib — 9 điểm theo hình S của VisionMaster (Routine_Calib::TopDownGridSOrder), dừng ngay ở điểm thất bại đầu
    // tiên thay vì âm thầm bỏ qua rồi báo "xong" sai sự thật.
    winrt::fire_and_forget VisionEditPage::RunAutoCalib()
    {
        auto strong = get_strong();
        if (m_busy || !CheckReadyForGrid()) co_return;

        // Mỗi lần chạy Flow LocateTop (hiệu chuẩn) module N-point nạp thêm một điểm — chạy đè danh sách cũ là ra ma trận trộn
        // hai lần hiệu chuẩn, không cảnh báo nào; nên App xoá trước.
        if (!co_await ui::Dialogs::ConfirmAsync(Loc::T("Vision.AutoCalibConfirm"))) co_return;
        if (m_busy || m_closing) co_return;

        GridFrame sys = GridFrameOf(*McDataManager::SysData());   // bản chụp tâm + bước lưới lúc bấm
        auto weak = get_weak();
        auto status = [weak](std::string s) {
            ui::PostUi([weak, s = std::move(s)] { if (auto p = weak.get()) p->SetSequenceStatus(s); });
        };
        RunJob([sys, status](sca::Routine_Calib& cal) {
            status(Loc::T("Vision.ClearingPoints"));
            std::string clearErr = cal.ClearCalibPoints(sca::EVisionProcedure::TopDownCalib);
            if (!clearErr.empty())
            {
                status(Loc::T("Vision.ClearFailStatus"));
                ui::Dialogs::Warn(Loc::F("Vision.ClearFail", clearErr));
                return;
            }

            const auto& order = sca::Routine_Calib::TopDownGridSOrder;
            int count = static_cast<int>(order.size());
            for (int i = 0; i < count; ++i)
            {
                int number = order[i];
                status(Loc::F("Vision.AutoCalibPoint", i + 1, count, number));
                double x = 0, y = 0;
                GridPointXY(sys, number, x, y);
                if (!cal.MoveAndTriggerTopDownCalib(x, y, MT()))
                {
                    status(Loc::F("Vision.AutoCalibStopStatus", i + 1, number));
                    ui::Dialogs::Warn(Loc::F("Vision.AutoCalibStop", i + 1, number));
                    return;
                }
            }

            status(Loc::T("Vision.AutoCalibLoaded"));
            McControl::InvalidateVisionCache();
            ui::Dialogs::Info(Loc::T("Vision.AutoCalibDone"));
        }, {});
    }

    // Kích Flow LocateTop (hiệu chuẩn) tại chỗ — chụp lại một điểm lẻ (CŨNG nạp một điểm).
    void VisionEditPage::OnTopLoadHereClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_busy) return;
        auto ok = std::make_shared<bool>(false);
        RunJob([ok](sca::Routine_Calib& cal) { *ok = cal.TriggerTopDownCalibFlow(MT()); },
               [ok](VisionEditPage&, std::string const&) {
                   ui::Dialogs::Info(Loc::T(*ok ? "Vision.TriggerTopOk" : "Vision.TriggerTopNotFound"));
               });
    }

    // Kiểm tra hiệu chuẩn top-down bằng chuyển động trục thật (§6.2b).
    void VisionEditPage::VerifyTopDown()
    {
        if (m_busy) return;
        if (!OrgOk()) { ui::Dialogs::Warn(Loc::T("Vision.NoOrigin")); return; }

        double step = McDataManager::SysData()->TopDownVerifyStepMm;
        auto r = std::make_shared<sca::TopDownVerifyResult>();
        SetSequenceStatus(Loc::T("Vision.VerifyingTopDown"));
        RunJob([step, r](sca::Routine_Calib& cal) { *r = cal.VerifyTopDown(step, MT()); },
               [r](VisionEditPage& p, std::string const&) {
                   McControl::InvalidateVisionCache();
                   p.RefreshGate();
                   p.SetSequenceStatus({});
                   std::string text = (r->Ok ? Loc::F("Vision.TopDownPass", r->StepMm, r->ErrorX, r->ErrorY, r->CrossX, r->CrossY)
                                             : Loc::F("Vision.TopDownFail", r->Reason)) + NoteSuffix(r->Note);
                   p.SetVerifyResult(text);
                   if (r->Ok) ui::Dialogs::Info(text);
                   else ui::Dialogs::Warn(text);
               });
    }

    // ═════════════════════════ 6.5 bottom-up ═════════════════════════

    // Cả chuỗi nạp điểm: các vị trí X rồi các góc, cuối cùng A về 0°.
    winrt::fire_and_forget VisionEditPage::RunAlignCalibSequence()
    {
        auto strong = get_strong();
        if (m_busy) co_return;
        if (!OrgOk()) { ui::Dialogs::Warn(Loc::T("Vision.NoOriginCalib")); co_return; }

        CommitAlignLists();
        int tool = m_tool;
        auto model = McDataManager::Model();
        double alignX = model ? model->AlignPosX(tool) : 0;
        if (std::abs(alignX) < 1e-9) { ui::Dialogs::Warn(Loc::F("Vision.NoAlignPosX", tool)); co_return; }

        auto sys = McDataManager::SysData();
        std::vector<double> xOffsets = sys->AlignCalibXOffsets();
        std::vector<double> angles = sys->AlignCalibAngles();
        if (angles.size() < 3) { ui::Dialogs::Warn(Loc::T("Vision.NeedThreeAngles")); co_return; }

        if (!co_await ui::Dialogs::ConfirmAsync(Loc::F("Vision.AlignSeqConfirm", tool, static_cast<int>(xOffsets.size()),
                                                       static_cast<int>(angles.size()))))
            co_return;
        if (m_busy || m_closing) co_return;

        auto weak = get_weak();
        auto r = std::make_shared<sca::AlignCalibSequenceResult>();
        RunJob(
            [tool, alignX, xOffsets, angles, r, weak](sca::Routine_Calib& cal) {
                *r = cal.RunAlignCalibSequence(tool, alignX, xOffsets, angles, [weak](const std::string& msg) {
                    ui::PostUi([weak, msg] { if (auto p = weak.get()) p->SetSequenceStatus(msg); });
                }, MT());
            },
            [tool, r](VisionEditPage& p, std::string const&) {
                p.SetSequenceStatus(r->Ok ? Loc::F("Vision.AlignSeqLoaded", r->PointsCaptured) : Loc::T("Vision.StoppedMidway"));
                if (r->Ok)
                {
                    McControl::InvalidateVisionCache();
                    ui::Dialogs::Info(Loc::F("Vision.AlignSeqDone", r->PointsCaptured, tool));
                }
                else ui::Dialogs::Warn(Loc::F("Vision.AlignSeqStop", tool, r->Reason, r->PointsCaptured));
            });
    }

    // Có chạy trục: hạ Z tới cao độ chụp bottom-up trước khi kích (nên bấm đúp).
    void VisionEditPage::TriggerAlignCalibFlow()
    {
        if (m_busy) return;
        int tool = m_tool;
        auto ok = std::make_shared<bool>(false);
        RunJob([tool, ok](sca::Routine_Calib& cal) { *ok = cal.TriggerAlignCalibFlow(tool, MT()); },
               [tool, ok](VisionEditPage&, std::string const&) {
                   ui::Dialogs::Info(Loc::F(*ok ? "Vision.TriggerAlignOk" : "Vision.TriggerAlignNotFound", tool));
               });
    }

    void VisionEditPage::MoveAlignAxisToZero()
    {
        if (m_busy) return;
        if (!OrgOk()) { ui::Dialogs::Warn(Loc::T("Vision.NoOrigin")); return; }
        int tool = m_tool;
        auto ok = std::make_shared<bool>(false);
        RunJob([tool, ok](sca::Routine_Calib& cal) { *ok = cal.MoveAlignAxisToZero(tool, MT()); },
               [tool, ok](VisionEditPage&, std::string const&) {
                   if (!*ok) ui::Dialogs::Warn(Loc::F("Vision.AxisAZeroFail", tool));
               });
    }

    // ═════════════════════════ 6.6 phép thử vòng xoay ═════════════════════════

    // Đo dấu góc và kiểm tâm xoay/chiều hệ toạ độ của file hiệu chuẩn bottom-up (§6.4b). ĐẠT thì Routine_Calib ghi dấu góc
    // và vị trí soi vào model; KHÔNG ĐẠT thì không ghi gì.
    winrt::fire_and_forget VisionEditPage::VerifyAlignRotation()
    {
        auto strong = get_strong();
        if (m_busy) co_return;
        if (!OrgOk()) { ui::Dialogs::Warn(Loc::T("Vision.NoOrigin")); co_return; }

        int tool = m_tool;
        auto model = McDataManager::Model();
        double alignX = model ? model->AlignPosX(tool) : 0;
        auto ax = McControl::Motion()[sca::AxisId::X];
        double currentX = ax ? ax->CurrentPos() : 0;
        bool moveX = false;
        if (std::abs(currentX - alignX) > 0.05)
        {
            // Vị trí chụp lúc thử phải trùng vị trí chụp lúc chạy Auto, nếu không dấu đo đúng nhưng tâm xoay lại đo ở chỗ
            // khác (PDF Hikrobot tr.45).
            if (!co_await ui::Dialogs::ConfirmAsync(Loc::F("Vision.RotationMoveXConfirm", currentX, tool, alignX))) co_return;
            if (m_busy || m_closing) co_return;
            moveX = true;
        }
        RunRotationTest(tool, moveX, alignX);
    }

    void VisionEditPage::RunRotationTest(int tool, bool moveX, double alignX)
    {
        auto sys = McDataManager::SysData();
        double angle = sys->AlignVerifyAngleDeg;
        auto xcfg = sys->GetAxis(sca::AxisId::X);
        double speed = xcfg ? xcfg->MoveSpeed : 0;

        struct Res { bool moved{ true }; sca::AlignVerifyResult r; };
        auto res = std::make_shared<Res>();
        auto weak = get_weak();
        if (!moveX) SetSequenceStatus(Loc::T("Vision.RotationRunning"));
        RunJob(
            [tool, moveX, alignX, angle, speed, res, weak](sca::Routine_Calib& cal) {
                if (moveX)
                {
                    bool ok = false;
                    try
                    {
                        auto func = McControl::Func();
                        ok = func && func->Move(sca::AxisId::X, alignX, speed, MT());
                    }
                    catch (const ::base::OperationCanceled&) { ok = false; }
                    if (!ok) { res->moved = false; return; }
                    ui::PostUi([weak] { if (auto p = weak.get()) p->SetSequenceStatus(Loc::T("Vision.RotationRunning")); });
                }
                res->r = cal.VerifyAlignRotation(tool, angle, MT());
            },
            [tool, res](VisionEditPage& p, std::string const&) {
                if (!res->moved) { ui::Dialogs::Warn(Loc::T("Vision.MoveXToAlignFail")); return; }
                const auto& r = res->r;
                p.SetSequenceStatus({});
                McControl::InvalidateVisionCache();
                p.RefreshGate();

                if (!r.Ok)
                {
                    std::string text = Loc::F("Vision.RotationFail", tool, r.Reason) + NoteSuffix(r.Note);
                    p.SetVerifyResult(text);
                    ui::Dialogs::Warn(Loc::F("Vision.NothingWritten", text));
                    return;
                }

                std::string text = Loc::F("Vision.RotationPass", tool, r.TestAngleDeg, r.MeasuredDeltaThetaDeg,
                                          r.Sign > 0 ? "+1" : "−1", r.ResidualMm, r.EccentricityMm);
                p.SetVerifyResult(text);
                ui::Dialogs::Info(text + (r.Reason.empty() ? std::string() : "\n\n" + Loc::F("Vision.Note", r.Reason)) +
                                  NoteSuffix(r.Note, "\n\n"));
            });
    }

    // ═════════════════════════ 6.7 hand-eye ═════════════════════════
    //
    // Tiến trình giữa các nút KHÔNG ở đây mà ở Routine_Calib (bài tự kiểm gọi thẳng lớp routine, nên mọi cổng chặn "chưa
    // gắp", "sai tay", "X không ở vị trí soi" đều được kiểm). Chặn lúc chạy, có lý do — không làm nút xám.

    // Nút 2 hạ Z xuống gắp nên hỏi lại: cơ cấu kẹp phải đang kẹp chi tiết đích, cao độ hạ lấy của tray lắp ráp.
    winrt::fire_and_forget VisionEditPage::HandEyePickWithConfirm()
    {
        auto strong = get_strong();
        if (m_busy) co_return;
        int nozzle = m_tool;
        if (!co_await ui::Dialogs::ConfirmAsync(Loc::F("Vision.HePickConfirm", nozzle))) co_return;
        if (m_closing) co_return;
        StepRun(Loc::T("Vision.HePick"), [](sca::Routine_Calib& c, int n, std::stop_token ct) { return c.HandEyePick(n, ct); },
                Report::HandEye, nozzle);
    }

    winrt::fire_and_forget VisionEditPage::HandEyeReturnWithConfirm()
    {
        auto strong = get_strong();
        if (m_busy) co_return;
        int nozzle = m_tool;
        if (!co_await ui::Dialogs::ConfirmAsync(Loc::F("Vision.HeReturnConfirm", nozzle))) co_return;
        if (m_closing) co_return;
        StepRun(Loc::T("Vision.HeReturn"), [](sca::Routine_Calib& c, int n, std::stop_token ct) { return c.HandEyeReturn(n, ct); },
                Report::HandEye, nozzle);
    }

    // Nút 6: chốt ô LÚC BẤM — chọn ô khác trong lúc trục đang chạy không được đổi đích giữa chừng.
    void VisionEditPage::HandEyeLocateHere()
    {
        int row = m_heCellRow, col = m_heCellCol;
        StepRun(HandEyeLocateLabel(),
                [row, col](sca::Routine_Calib& c, int n, std::stop_token ct) { return c.HandEyeLocateHere(n, row, col, ct); },
                Report::HandEye, m_tool);
    }

    // Nút 5 — ghi hand-eye + mốc góc vào model. Khuôn riêng (trả HandEyeCommitResult và còn phải chạy lại cổng).
    void VisionEditPage::HandEyeCommit()
    {
        if (m_busy) return;
        if (!OrgOk()) { ui::Dialogs::Warn(Loc::T("Vision.NoOrigin")); return; }
        int nozzle = m_tool;
        auto r = std::make_shared<sca::HandEyeCommitResult>();
        RunJob([nozzle, r](sca::Routine_Calib& cal) { *r = cal.HandEyeCommit(nozzle, MT()); },
               [nozzle, r](VisionEditPage& p, std::string const&) {
                   p.RefreshGate();
                   if (!r->Ok)
                   {
                       p.SetHandEyeStep(Loc::F("Vision.StepStopped", Loc::T("Vision.HeCommit"), r->Reason));
                       ui::Dialogs::Warn(Loc::F("Vision.HeCommitFail", nozzle, r->Reason) + NoteSuffix(r->Note, "\n\n"));
                       return;
                   }
                   p.SetHandEyeStep(Loc::F("Vision.HeCommitLine", nozzle, r->Dx, r->Dy, r->AngleRef));
                   ui::Dialogs::Info(Loc::F("Vision.HeCommitDone", nozzle, r->Dx, r->Dy, r->AngleRef) + NoteSuffix(r->Note, "\n\n"));
               });
    }

    // Nút nhỏ cạnh nút 6: lưới ô của tray lắp ráp, dựng lại mỗi lần mở (đổi model thì kích thước đổi theo). Giữ ô đang chọn
    // nếu còn nằm trong lưới mới.
    void VisionEditPage::OnHeCellClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        auto model = McDataManager::Model();
        auto tray = model ? model->AssyTray : nullptr;
        if (!tray) { ui::Dialogs::Warn(Loc::T("Vision.NoModel")); return; }

        int rows = (std::max)(1, tray->Rows()), cols = (std::max)(1, tray->Cols());
        if (m_heCellRow >= tray->Rows() || m_heCellCol >= tray->Cols()) { m_heCellRow = -1; m_heCellCol = -1; }

        if (!m_cellFlyout)
        {
            m_cellFlyout = Flyout();
            m_cellFlyout.ShouldConstrainToRootBounds(false);   // cửa sổ riêng — nổi trên popup VisionMaster
            m_cellFlyout.Placement(Primitives::FlyoutPlacementMode::BottomEdgeAlignedRight);
        }
        m_cellFlyout.Content(ui::visionedit::BuildCellPicker(rows, cols, m_heCellRow, m_heCellCol,
            [weak = get_weak()](int r, int c) {
                auto self = weak.get();
                if (!self) return;
                self->m_heCellRow = r;
                self->m_heCellCol = c;
                ui::SetText(self->m_he6Caption, r < 0 ? std::string("(in place)") : "cell " + CellLabel(r, c));
                self->HeCellButton().Content(box_value(r < 0 ? ui::T("Vision.HeCellButton") : ui::H(CellLabel(r, c))));
                if (self->m_cellFlyout) self->m_cellFlyout.Hide();
            }));
        // Cập nhật nhãn ngay cả khi ô cũ vừa bị bỏ vì lưới nhỏ lại.
        ui::SetText(m_he6Caption, m_heCellRow < 0 ? std::string("(in place)") : "cell " + CellLabel(m_heCellRow, m_heCellCol));
        HeCellButton().Content(box_value(m_heCellRow < 0 ? ui::T("Vision.HeCellButton") : ui::H(CellLabel(m_heCellRow, m_heCellCol))));
        m_cellFlyout.ShowAt(HeCellButton());
    }

    // ═════════════════════════ 6.8 nghiệm thu ═════════════════════════

    // Bước 5b: tay cầm sản phẩm mẫu; soi dấu chuẩn, AlignBottom_ToolN tính lệnh đặt lên dấu, tay tới lệnh ở Z an toàn.
    winrt::fire_and_forget VisionEditPage::DryRunPlaceAlign()
    {
        auto strong = get_strong();
        if (m_busy) co_return;
        if (!OrgOk()) { ui::Dialogs::Warn(Loc::T("Vision.NoOrigin")); co_return; }
        int tool = m_tool;
        if (!co_await ui::Dialogs::ConfirmAsync(Loc::F("Vision.DryRunConfirm", tool))) co_return;
        if (m_busy || m_closing) co_return;

        auto r = std::make_shared<sca::DryRunPlaceResult>();
        RunJob([tool, r](sca::Routine_Calib& cal) { *r = cal.DryRunPlaceAlign(tool, MT()); },
               [tool, r](VisionEditPage& p, std::string const&) {
                   if (auto cal = McControl::Calib())
                       if (auto gate = cal->LastCalibGate()) p.SetGate(gate->Describe());
                   if (p.m_coreReady) p.UpdateCalibTexts();
                   if (r->Ok)
                       ui::Dialogs::Info(Loc::F("Vision.DryRunDone", tool, r->CamX, r->CamY, r->CmdX, r->CmdY2) +
                                         NoteSuffix(r->CmdCheck, "\n\n"));
                   else
                       ui::Dialogs::Warn(Loc::F("Vision.DryRunStop", tool, r->Reason));
               });
    }

    // ═════════════════════════ 6.9 chạy kiểm từng bước ═════════════════════════
    //
    // Sáu nút đúng trình tự Auto, dừng giữa từng bước để đọc số (Tuấn 2026-09-14). Tay hút = Tool đang chọn ở trên — không
    // có ô chọn tay riêng ("bấm nút của tay này, xem số của tay kia").

    void VisionEditPage::OnSeq2Click(WF::IInspectable const&, RoutedEventArgs const&)
    {
        int r = SeqValue(SeqAssyRowBox(), m_stepAssyRow) - 1;
        int c = SeqValue(SeqAssyColBox(), m_stepAssyCol) - 1;
        StepRun("Trigger LocateTop",
                [r, c](sca::Routine_Calib& cal, int, std::stop_token ct) { return cal.StepLocateTop(r, c, ct); }, Report::Step, m_tool);
    }

    winrt::fire_and_forget VisionEditPage::StepPick()
    {
        auto strong = get_strong();
        if (m_busy) co_return;
        int tool = m_tool;
        int row = SeqValue(SeqInputRowBox(), m_stepInputRow);
        int col = SeqValue(SeqInputColBox(), m_stepInputCol);
        if (!co_await ui::Dialogs::ConfirmAsync(Loc::F("Vision.SeqPickConfirm", tool, row, col))) co_return;
        if (m_closing) co_return;
        StepRun(Loc::T("Vision.SeqPickTitle"),
                [row, col](sca::Routine_Calib& cal, int n, std::stop_token ct) { return cal.StepPick(n, row - 1, col - 1, ct); },
                Report::Step, tool);
    }

    void VisionEditPage::StepAlignBottom()
    {
        StepRun("Trigger AlignBottom",
                [](sca::Routine_Calib& cal, int n, std::stop_token ct) { return cal.StepAlignBottom(n, ct); }, Report::Step, m_tool,
                [](VisionEditPage&, sca::StepRunResult const& r) {
                    if (r.Ok && !r.GuardWarn.empty()) ui::Dialogs::Warn(Loc::F("Vision.GuardWarn", r.GuardWarn));
                });
    }

    winrt::fire_and_forget VisionEditPage::StepMoveToCommand()
    {
        auto strong = get_strong();
        if (m_busy) co_return;
        auto cal = McControl::Calib();
        std::string warn = cal ? cal->StepGuardWarn() : std::string();
        if (!warn.empty())
        {
            bool go = co_await ui::Dialogs::ConfirmAsync(Loc::F("Vision.GuardConfirm", warn));
            if (!go) co_return;
        }
        if (m_closing) co_return;
        StepRun(Loc::T("Vision.SeqMoveToCmdTitle"),
                [](sca::Routine_Calib& c, int n, std::stop_token ct) { return c.StepMoveToCommand(n, ct); }, Report::Step, m_tool);
    }

    // ═════════════════════════ VisionMaster ═════════════════════════

    void VisionEditPage::OnShown(bool visible)
    {
        m_visible = visible;
        if (visible)
        {
            LoadInputs();   // tham số có thể vừa đổi ở trang Setting
            m_bindOnShow = true;   // WPF: BindMultiProcedure mỗi lần trang Loaded
            QueueVmWork();
        }
        else CommitAlignLists();
        m_globalTool.SetVisible(visible);
        m_config.SetVisible(visible);
    }

    // Control VM là .NET và gọi sang vServerApp; trong lúc chờ, COM bơm thông điệp. Gọi nó ngay trong sự kiện XAML là app tự
    // kết thúc (0xC000027B). Nên chỉ xếp hàng; việc chạy ở lượt sau, ngoài mọi sự kiện XAML.
    void VisionEditPage::QueueVmWork()
    {
        if (m_vmQueued || m_closing) return;
        m_vmQueued = true;
        ui::PostUi([weak = get_weak()] {
            auto self = weak.get();
            if (!self) return;
            self->m_vmQueued = false;
            if (self->m_vmBusy || self->m_closing || !self->m_visible || !self->m_loaded) return;
            self->m_vmBusy = true;
            try
            {
                if (!self->m_vmAttempted) self->EnsureVmPanes();   // gắn solution luôn ở cuối
                else if (self->m_config.Created() &&
                         (self->m_bindOnShow || McControl::VisionLoadCount() != self->m_boundLoadCount))
                    self->BindProcedures();
                self->m_bindOnShow = false;
            }
            catch (...) { self->m_vmBusy = false; throw; }
            self->m_vmBusy = false;
        });
    }

    void VisionEditPage::EnsureVmPanes()
    {
        if (m_vmAttempted || !m_loaded) return;
        m_vmAttempted = true;

        if (ui::AppHost::Options().ShotMode())
        {
            ui::SetText(VmPlaceholderText(), std::string("Screenshot mode: VisionMaster Flow editor not created."));
            return;
        }

        std::string error;
        if (!m_globalTool.Create(GlobalToolHost(), ui::vmkind::GlobalTool, &error))
            ui::SetText(VmPlaceholderText(), "VisionMaster toolbar unavailable: " + error);
        if (!m_config.Create(ConfigHost(), ui::vmkind::MainView, &error))
        {
            ui::SetText(VmPlaceholderText(), "VisionMaster Flow editor unavailable: " + error);
            return;
        }
        ui::SetText(VmPlaceholderText(), std::string());
        BindProcedures();
    }

    // Gắn trình soạn vào solution đang nạp. Có thể chưa có solution (VisionConnect còn nạp nền lúc khởi động, hoặc mô phỏng
    // không có VisionMaster thật) — chỉ ghi log, trang vẫn dùng được: đây là trang tiện ích, không nằm trên đường Auto.
    void VisionEditPage::BindProcedures()
    {
        auto sys = McDataManager::SysData();
        auto vision = McControl::Vision();
        if (!m_config.Created() || !vision || (sys && sys->SimulationMode) || !vision->IsSolutionLoaded()) return;

        m_boundLoadCount = McControl::VisionLoadCount();
        HRESULT hr = m_config.Host().CallVoid(ui::vmdisp::ProcBindMultiProcedure);
        if (FAILED(hr))
            sca::LogWorker::Instance().RecordLog("Vision Edit: solution not bound — " + ::base::Narrow(m_config.Host().LastError()),
                                                 sca::LogLevel::Warning, "VisionEdit");
    }

    void VisionEditPage::OnWindowMoved()
    {
        m_globalTool.Sync();
        m_config.Sync();
    }

    void VisionEditPage::OnModalChanged(bool modalOpen)
    {
        m_globalTool.SetEnabled(!modalOpen);
        m_config.SetEnabled(!modalOpen);
    }

    void VisionEditPage::OnClosing()
    {
        m_closing = true;
        if (m_cellFlyout) m_cellFlyout.Hide();
        m_config.Dispose();
        m_globalTool.Dispose();
    }
}
