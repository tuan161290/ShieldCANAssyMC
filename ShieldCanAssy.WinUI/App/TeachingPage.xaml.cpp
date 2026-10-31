#include "pch.h"
#include "TeachingPage.xaml.h"
#if __has_include("TeachingPage.g.cpp")
#include "TeachingPage.g.cpp"
#endif

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Base/Async.h"
#include "Base/Text.h"
#include "Communication/AxisAngle.h"
#include "Controller/McControl.h"
#include "Data/McDataManager.h"
#include "Infor/AxisId.h"
#include "Infor/IoMap.h"
#include "Lang/Loc.h"
#include "Ui/Controls.h"
#include "Ui/Dialogs.h"
#include "Ui/JogControl.h"
#include "Ui/UiUtil.h"
#include "Utility/Log/LogWorker.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
namespace WF = winrt::Windows::Foundation;

using sca::AxisId;
using sca::Loc;
using sca::McControl;
using sca::McDataManager;

namespace
{
    /// <summary>Một giá trị theo chuỗi định dạng .NET ("0.000", "0.00", "0.#") — cùng cách in với Loc::F.</summary>
    std::string Fv(double v, std::string_view spec) { return Loc::FormatValue(sca::FmtArg(v), spec); }

    std::string XyText(double x, std::string const& rowAxis, double y)
    {
        return Loc::Format("X {0:0.000}, {1} {2:0.000}", { x, rowAxis, y });
    }

    std::string XyText(sca::TeachPoint const& p, std::string const& rowAxis)
    {
        return p.Taught ? XyText(p.X, rowAxis, p.Y) : Loc::T("Teach.NotTaught");
    }

    std::string MmText(double v) { return Fv(v, "0.000") + " mm"; }

    /// <summary>
    /// Trang Teaching KHÔNG chặn lúc chưa về gốc nữa — Tuấn chốt 2026-09-17: "bỏ qua việc kiểm tra về gốc, tôi sẽ chịu
    /// trách nhiệm kiểm tra xác nhận toạ độ có đúng hay không". Đổi lại chỉ giữ một dòng log mức Cảnh báo, để sau này
    /// còn truy được điểm nào được ghi trong tình trạng đó. Chỉ nới cho các nút GHI TOẠ ĐỘ; các nút CHẠY vẫn qua đủ
    /// chốt chặn của McFunction.
    /// </summary>
    void NoteTeachWithoutOrigin(std::string const& what)
    {
        if (McControl::Status().OrgOk()) return;
        sca::LogWorker::Instance().RecordLog("Teaching: wrote " + what + " WITHOUT Origin — operator-verified",
                                             sca::LogLevel::Warning, "Teaching");
    }

    /// <summary>Hỏi lại trước khi ghi đè một điểm teach (Tuấn yêu cầu 2026-09-26, kẻo ấn nhầm): câu hỏi nêu điểm nào,
    /// số CŨ và số MỚI. Số mới đọc từ encoder TRƯỚC khi hỏi, và chính số đó được ghi: hộp thoại là modal (lớp phủ che
    /// trang, StopActiveJogs) nên trục không nhận lệnh Jog nào trong lúc chờ.</summary>
    WF::IAsyncOperation<bool> ConfirmTeach(std::string what, std::string oldValue, std::string newValue)
    {
        return ui::Dialogs::ConfirmAsync(Loc::F("Teach.ConfirmWrite", what, oldValue, newValue));
    }

    void LogUiError(std::string const& what, std::string const& detail)
    {
        sca::LogWorker::Instance().RecordLog("Teaching: " + what + " failed: " + detail, sca::LogLevel::Error, "UI");
    }

    /// <summary>Lệnh trên luồng UI gọi vào Core: lỗi lọt ra thì ghi log + báo, không làm sập app.</summary>
    void Guarded(const char* what, std::function<void()> const& fn)
    {
        try { fn(); }
        catch (const std::exception& ex)
        {
            LogUiError(what, ex.what());
            ui::Dialogs::Error(std::string(what) + " failed: " + ex.what());
        }
        catch (winrt::hresult_error const& ex)
        {
            LogUiError(what, winrt::to_string(ex.message()));
            ui::Dialogs::Error(std::string(what) + " failed: " + winrt::to_string(ex.message()));
        }
    }

    /// <summary>Path.GetInvalidFileNameChars() trên Windows: ký tự điều khiển và " &lt; &gt; | : * ? \ /.</summary>
    bool ValidModelName(std::string const& name)
    {
        if (::base::IsBlank(name))
        {
            ui::Dialogs::Warn(Loc::T("Teach.EnterModelName"));
            return false;
        }
        for (unsigned char c : name)
        {
            if (c < 32 || std::strchr("\"<>|:*?\\/", c) != nullptr)
            {
                ui::Dialogs::Warn(Loc::T("Teach.ModelNameInvalid"));
                return false;
            }
        }
        return true;
    }

    /// <summary>ObservableCollection&lt;string&gt;.Contains của bản WPF — so khớp phân biệt hoa thường.</summary>
    bool ContainsName(std::vector<std::string> const& names, std::string const& name)
    {
        return std::find(names.begin(), names.end(), name) != names.end();
    }

    std::shared_ptr<sca::IAxis> AxisOf(std::string_view name) { return McControl::Motion().Axis(name); }
}

namespace winrt::ShieldCanAssy::implementation
{
    // ───────────────────────── dựng trang ─────────────────────────

    void TeachingPage::OnRootLoaded(WF::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_loaded) return;
        m_loaded = true;

        // Chữ dài lấy từ bảng Loc (cùng khoá resx với bản WPF).
        ui::SetText(SharedSizeText(), Loc::T("Teach.InputSizeShared"));
        ui::SetText(CamAnchorHint1Text(), Loc::T("Teach.CamAnchorHint1"));
        ui::SetText(CamAnchorHint2Text(), Loc::T("Teach.CamAnchorHint2"));
        ui::SetText(ThreePointHintText(), Loc::T("Teach.ThreePointHint"));
        ui::SetText(PickAngleHintText(), Loc::T("Teach.PickAngleHint"));
        ui::SetText(AlignPosHintText(), Loc::T("Teach.AlignPosHint"));
        ui::SetText(JigHintText(), Loc::T("Teach.JigHint"));
        ui::SetText(TrimHintText(), Loc::T("Teach.TrimHint"));
        SetTips();

        // Rows/Cols: số nguyên, có nút tăng/giảm gọn. Ô bù: KHÔNG Minimum/Maximum — ngưỡng kiểm khi Apply (NumberBox sẽ
        // âm thầm kẹp giá trị, mất lời báo "vượt ±1 mm").
        for (auto box : { ColsBox(), RowsBox() })
        {
            ui::ConfigureNumberBox(box, 0);
            box.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Compact);
            box.Minimum(sca::TrayLayout::MinDim);
            box.Maximum(sca::TrayLayout::MaxDim);
        }
        for (auto box : { TrimAllXBox(), TrimAllYBox(), TrimAllABox(), TrimXBox(), TrimYBox(), TrimABox() })
            ui::ConfigureNumberBox(box, 3);
        BuildZBoxes();

        m_cellView.Attach(CellGrid());
        m_cellView.OnCellTapped = [this](int row, int col) { SelectCell(FirstTray(), row, col); };
        m_cellView2.Attach(CellGrid2());
        m_cellView2.OnCellTapped = [this](int row, int col) { SelectCell(1, row, col); };

        BuildJog();
        WireEvents();

        Guarded("Teaching page load", [&] {
            RefreshModelList();
            m_model = McDataManager::Model();
            OnTraySwitched();
        });
    }

    void TeachingPage::SetTips()
    {
        auto tip = [](DependencyObject const& target, char const* key) {
            ToolTipService::SetToolTip(target, box_value(ui::T(key)));
        };
        tip(NewModelNameBox(), "Teach.NewModelNameTip");
        tip(GoCamAnchorButton(), "Teach.GoCamAnchorTip");
        tip(P1Button(), "Teach.P1Tip");
        tip(P2Button(), "Teach.P2Tip");
        tip(P3Button(), "Teach.P3Tip");
        tip(T2P1Button(), "Teach.P1Tip");
        tip(T2P2Button(), "Teach.P2Tip");
        tip(T2P3Button(), "Teach.P3Tip");
        tip(ApplySizeButton(), "Teach.ApplySizeTip");
        tip(GoJigPick1Button(), "Teach.GoJigPick1Tip");
        tip(GoJigPick2Button(), "Teach.GoJigPick2Tip");
        tip(GoJigCamButton(), "Teach.GoJigCamTip");
        tip(MoveCellButton(), "Teach.MoveCellTip");
        tip(ZCellButton(), "Teach.ZCellMoveTip");
        tip(MoveCamButton(), "Teach.MoveCamTip");
        tip(SetOffsetButton(), "Teach.SetOffsetTip");
        tip(ClearOffsetButton(), "Teach.ClearOffsetTip");
        tip(ApplyTrimAllButton(), "Teach.ApplyTrimAllTip");
        tip(ApplyTrimButton(), "Teach.ApplyTrimCellTip");
        tip(CamBtmLightButton(), "Teach.Cam1LightTip");
        tip(CamTopLightButton(), "Teach.Cam2LightTip");
        tip(SaveBigButton(), "Teach.SaveTip");
    }

    Button TeachingPage::JogExtraButton(wchar_t const* text, double width, char const* tipKey)
    {
        Button b;
        b.Content(box_value(text));
        b.Width(width);
        auto key = box_value(L"JogExtraButtonStyle");
        if (Resources().HasKey(key)) b.Style(Resources().Lookup(key).as<winrt::Microsoft::UI::Xaml::Style>());
        b.VerticalAlignment(VerticalAlignment::Center);
        if (tipKey) ToolTipService::SetToolTip(b, box_value(ui::T(tipKey)));
        return b;
    }

    // Panel jog RÚT GỌN — chỉ Jog −/+ và vị trí (không Stop/Home/SVO/RST/Move: những cái đó ở trang Manual). Chỉ 2 nhóm
    // trục cần khi teach: X/Z (toạ độ + Z_Pick) bên trái kèm nút chạy tới vị trí, Y1/Y2 (trục hàng) bên phải. KHÔNG kéo
    // A1/A2 sang (Tuấn 2026-08-19).
    void TeachingPage::BuildJog()
    {
        ui::JogRowOptions options;
        options.buttonHeight = 36;
        options.buttonWidth = 36;
        options.nameWidth = 28;
        options.positionWidth = 76;

        auto weak = get_weak();
        auto addRow = [&](std::string const& name, StackPanel const& host) -> StackPanel {
            JogAxisRow row;
            row.Name = name;
            StackPanel line = ui::BuildJogRow(name, options, row.Pos);
            row.Unit = TextBlock();
            row.Unit.FontSize(10);
            row.Unit.Width(22);
            row.Unit.VerticalAlignment(VerticalAlignment::Center);
            row.Unit.Foreground(ui::Res(L"TextSecondaryBrush"));
            line.Children().InsertAt(2, row.Unit);   // tên, vị trí, ĐƠN VỊ, −, +
            host.Children().Append(line);
            m_jogRows.push_back(row);
            return line;
        };

        // Hàng X: thêm 2 nút chạy thử tới vị trí soi CAM1 của từng tay hút (Tuấn 2026-09-05). [2×] [Busy-off].
        StackPanel xLine = addRow(AxisId::X, JogGantryHost());
        for (int nozzle : { 1, 2 })
        {
            Button b = JogExtraButton(nozzle == 1 ? L"Go T1" : L"Go T2", 72, nozzle == 1 ? "Teach.GoT1TriggerTip" : "Teach.GoT2TriggerTip");
            if (nozzle == 1) b.Margin(ThicknessHelper::FromLengths(6, 0, 0, 0));
            ui::AttachDoubleTap(b, [weak, nozzle] { if (auto s = weak.get()) s->MoveToTrigger(nozzle); });
            xLine.Children().Append(b);
            m_jogMoveButtons.push_back(b);
        }

        // Hàng Z1/Z2: thêm 3 nút chạy thử tới cao độ tham chiếu (Tuấn 2026-09-05). [2×] [Busy-off].
        for (const char* z : AxisId::ZAxes)
        {
            std::string zName = z;
            StackPanel zLine = addRow(zName, JogGantryHost());

            Button home = JogExtraButton(L"Origin", 70, "Teach.ZHomeTip");
            home.Margin(ThicknessHelper::FromLengths(6, 0, 0, 0));
            ui::AttachDoubleTap(home, [weak, zName] { if (auto s = weak.get()) s->MoveZHome(zName); });
            zLine.Children().Append(home);
            m_jogMoveButtons.push_back(home);

            Button pick = JogExtraButton(L"Go Pick", 78, "Teach.GoPickTip");
            ui::AttachDoubleTap(pick, [weak, zName] { if (auto s = weak.get()) s->MoveZPick(zName); });
            zLine.Children().Append(pick);
            m_jogMoveButtons.push_back(pick);

            Button place = JogExtraButton(L"Go Place", 84, "Teach.GoPlaceTip");
            ui::AttachDoubleTap(place, [weak, zName] { if (auto s = weak.get()) s->MoveZPlace(zName); });
            zLine.Children().Append(place);
            m_jogMoveButtons.push_back(place);
        }

        addRow(AxisId::Y1, JogTrayHost());
        addRow(AxisId::Y2, JogTrayHost());
    }

    void TeachingPage::WireEvents()
    {
        auto weak = get_weak();
        auto click = [weak](Primitives::ButtonBase const& b, std::function<void(TeachingPage&)> fn) {
            b.Click([weak, fn](auto&&, auto&&) {
                if (auto s = weak.get()) fn(*s);
            });
        };
        auto dbl = [weak](Button const& b, std::function<void(TeachingPage&)> fn) {
            ui::AttachDoubleTap(b, [weak, fn] {
                if (auto s = weak.get()) fn(*s);
            });
        };

        // ── model ──
        ModelList().SelectionChanged([weak](auto&&, auto&&) {
            auto s = weak.get();
            if (!s || s->m_suppressModelSelection) return;
            int index = s->ModelList().SelectedIndex();
            s->m_selectedModelName = (index >= 0 && index < (int)s->m_modelNames.size()) ? s->m_modelNames[(size_t)index] : std::string();
        });
        click(LoadModelButton(), [](TeachingPage& p) { p.LoadSelectedModel(); });
        click(SaveModelButton(), [](TeachingPage& p) { p.SaveModel(); });
        click(DeleteModelButton(), [](TeachingPage& p) { p.DeleteModel(); });
        click(NewModelButton(), [](TeachingPage& p) { p.NewModel(); });
        click(CopyModelButton(), [](TeachingPage& p) { p.CopyModel(); });
        click(SaveBigButton(), [](TeachingPage& p) { p.SaveModel(); });

        // ── tray / Tool / thứ tự quét ──
        // Nút kiểu Radio: Checked chỉ bắn ở nút vừa được chọn (bấm lại nút đang chọn không bắn).
        auto checked = [weak](RadioButton const& radio, std::function<void(TeachingPage&)> fn) {
            radio.Checked([weak, fn](auto&&, auto&&) {
                auto s = weak.get();
                if (s && !s->m_suppressSelectors) fn(*s);
            });
        };
        checked(TrayInputRadio(), [](TeachingPage& p) { p.SelectTray(p.m_lastInputTray); });
        checked(TrayAssyRadio(), [](TeachingPage& p) { p.SelectTray(AssyTraySel); });
        // OnToolSwitched
        checked(Tool1Radio(), [](TeachingPage& p) { p.m_useNozzle1 = true; p.Compute(); });
        checked(Tool2Radio(), [](TeachingPage& p) { p.m_useNozzle1 = false; p.Compute(); });
        ScanOrderRadios().SelectionChanged([weak](auto&&, auto&&) {
            auto s = weak.get();
            if (!s || s->m_suppressSelectors) return;
            int index = s->ScanOrderRadios().SelectedIndex();
            // Chỉ 2 chế độ chọn được qua UI — ColMajor tồn tại trong EScanOrder nhưng TrayState.RowCol chưa cài.
            if (index == 1) s->SetScanOrder(sca::EScanOrder::Serpentine);
            else if (index == 0) s->SetScanOrder(sca::EScanOrder::RowMajor);
        });
        TraversalCanvas().SizeChanged([weak](auto&&, auto&&) {
            if (auto s = weak.get()) s->SyncScanRadios();
        });

        // ── mốc CAM top-down ──
        click(TeachCamAnchorButton(), [](TeachingPage& p) { p.TeachCamTopDownAnchor(); });
        dbl(GoCamAnchorButton(), [](TeachingPage& p) { p.MoveCamTopDownAnchor(); });

        // ── teach 3 điểm ──
        // Hàng thứ nhất = Tray 1 (Input) hoặc Assy; hàng T2* = Tray 2, chỉ hiện ở chế độ Input.
        click(P1Button(), [](TeachingPage& p) { p.TeachGridPoint(GridPoint::P00, p.FirstTray()); });
        click(P2Button(), [](TeachingPage& p) { p.TeachGridPoint(GridPoint::P0C, p.FirstTray()); });
        click(P3Button(), [](TeachingPage& p) { p.TeachGridPoint(GridPoint::PR0, p.FirstTray()); });
        click(TeachVerifyButton(), [](TeachingPage& p) { p.TeachGridPoint(GridPoint::Verify, p.FirstTray()); });
        click(T2P1Button(), [](TeachingPage& p) { p.TeachGridPoint(GridPoint::P00, 1); });
        click(T2P2Button(), [](TeachingPage& p) { p.TeachGridPoint(GridPoint::P0C, 1); });
        click(T2P3Button(), [](TeachingPage& p) { p.TeachGridPoint(GridPoint::PR0, 1); });
        click(T2TeachVerifyButton(), [](TeachingPage& p) { p.TeachGridPoint(GridPoint::Verify, 1); });
        click(ApplySizeButton(), [](TeachingPage& p) { p.ApplySize(); });

        // ── góc hút, cao độ, vị trí soi, cơ cấu kẹp ──
        click(TeachPickA1Button(), [](TeachingPage& p) { p.TeachPickAngle(1); });
        click(TeachPickA2Button(), [](TeachingPage& p) { p.TeachPickAngle(2); });
        click(TeachZPickZ1Button(), [](TeachingPage& p) { p.TeachZPick(AxisId::Z1, p.FirstTray()); });
        click(TeachZPickZ2Button(), [](TeachingPage& p) { p.TeachZPick(AxisId::Z2, p.FirstTray()); });
        click(TeachZSlowZ1Button(), [](TeachingPage& p) { p.TeachZSlow(AxisId::Z1, p.FirstTray()); });
        click(TeachZSlowZ2Button(), [](TeachingPage& p) { p.TeachZSlow(AxisId::Z2, p.FirstTray()); });
        click(T2TeachZPickZ1Button(), [](TeachingPage& p) { p.TeachZPick(AxisId::Z1, 1); });
        click(T2TeachZPickZ2Button(), [](TeachingPage& p) { p.TeachZPick(AxisId::Z2, 1); });
        click(T2TeachZSlowZ1Button(), [](TeachingPage& p) { p.TeachZSlow(AxisId::Z1, 1); });
        click(T2TeachZSlowZ2Button(), [](TeachingPage& p) { p.TeachZSlow(AxisId::Z2, 1); });
        click(TeachAlign1Button(), [](TeachingPage& p) { p.TeachAlignPos(1); });
        click(TeachAlign2Button(), [](TeachingPage& p) { p.TeachAlignPos(2); });
        click(TeachJigPick1Button(), [](TeachingPage& p) { p.TeachHandEyePick(1); });
        click(TeachJigPick2Button(), [](TeachingPage& p) { p.TeachHandEyePick(2); });
        click(TeachJigCamButton(), [](TeachingPage& p) { p.TeachHandEyeCamPos(); });
        dbl(GoJigPick1Button(), [](TeachingPage& p) { p.MoveToHandEyePick(1); });
        dbl(GoJigPick2Button(), [](TeachingPage& p) { p.MoveToHandEyePick(2); });
        dbl(GoJigCamButton(), [](TeachingPage& p) { p.MoveToHandEyeCamPos(); });

        // ── ô đang chọn ──
        dbl(MoveCellButton(), [](TeachingPage& p) { p.MoveToCell(); });
        dbl(ZCellButton(), [](TeachingPage& p) { p.MoveZAtCell(); });
        dbl(MoveCamButton(), [](TeachingPage& p) { p.MoveCamToCell(); });
        click(SetOffsetButton(), [](TeachingPage& p) { p.SetOffset(); });
        click(ClearOffsetButton(), [](TeachingPage& p) { p.ClearOffset(); });
        click(ApplyTrimAllButton(), [](TeachingPage& p) { p.ApplyPlaceTrimAll(); });
        click(ClearTrimAllButton(), [](TeachingPage& p) { p.ClearPlaceTrimAll(); });
        click(ApplyTrimButton(), [](TeachingPage& p) { p.ApplyPlaceTrim(); });
        click(ClearTrimButton(), [](TeachingPage& p) { p.ClearPlaceTrim(); });
        CellScroll().SizeChanged([weak](auto&&, SizeChangedEventArgs const& e) {
            if (auto s = weak.get())
            {
                s->m_cellView.FitWidth(e.NewSize().Width);
                s->m_cellView2.FitWidth(e.NewSize().Width);
            }
        });

        // ── tốc độ jog — CHUNG cho mọi trục và mọi trang (McFunction::JogFast) ──
        auto setJogFast = [](TeachingPage& p, bool fast) {
            if (auto func = McControl::Func()) func->SetJogFast(fast);
            p.m_jogFastShown = -1;   // OnTick đọc lại từ Func
        };
        click(JogSlowButton(), [setJogFast](TeachingPage& p) { setJogFast(p, false); });
        click(JogFastButton(), [setJogFast](TeachingPage& p) { setJogFast(p, true); });

        // ── hút / thổi / đèn: 1 nút toggle mỗi van (Tuấn 2026-09-10) — bật hay tắt theo DO thật đọc ở nhịp trước ──
        click(Vac1Button(), [](TeachingPage& p) {
            if (auto func = McControl::Func()) { bool on = p.m_vac1On; ui::PostMotionCommand([func, on] { func->SetVacuum(1, !on); }); }
        });
        click(Vac2Button(), [](TeachingPage& p) {
            if (auto func = McControl::Func()) { bool on = p.m_vac2On; ui::PostMotionCommand([func, on] { func->SetVacuum(2, !on); }); }
        });
        click(Blow1Button(), [](TeachingPage& p) {
            if (auto func = McControl::Func()) { bool on = p.m_blow1On; ui::PostMotionCommand([func, on] { func->SetBlow(1, !on); }); }
        });
        click(Blow2Button(), [](TeachingPage& p) {
            if (auto func = McControl::Func()) { bool on = p.m_blow2On; ui::PostMotionCommand([func, on] { func->SetBlow(2, !on); }); }
        });
        click(CamBtmLightButton(), [](TeachingPage& p) { p.ToggleCamLamp(sca::IoMap::DoLampCam1); });
        click(CamTopLightButton(), [](TeachingPage& p) { p.ToggleCamLamp(sca::IoMap::DoLampCam2); });
    }

    // ───────────────────────── vòng đời ─────────────────────────

    void TeachingPage::OnCoreReady()
    {
        m_coreReady = true;
        for (JogAxisRow& row : m_jogRows) row.Axis = AxisOf(row.Name);
    }

    void TeachingPage::OnShown(bool visible)
    {
        if (!m_loaded || !visible) return;
        Guarded("Teaching page open", [&] {
            RefreshModelList();
            if (McDataManager::Model() != m_model)
            {
                m_model = McDataManager::Model();
                OnTraySwitched();
            }
            SyncScanRadios();
        });
    }

    void TeachingPage::ShowShotVariant(int index)
    {
        if (!m_loaded || index < 1) return;
        if (index <= 2)
        {
            m_useNozzle1 = false;
            SelectTray(AssyTraySel);
            SelectCell(AssyTraySel, 0, 0);
            LeftScroll().ChangeView(nullptr, index == 1 ? 0.0 : 760.0, nullptr, true);
        }
        else
        {
            m_useNozzle1 = true;
            SelectTray(0);
            SelectCell(1, 0, 0);
            double offset = LeftScroll().ScrollableHeight() + 2000.0;
            if (index == 4)
            {
                // Card 3 điểm với hai hàng Tray 1 / Tray 2: đo chỗ chữ nhắc đầu card sau khi phần Tray 2 vừa hiện.
                LeftScroll().UpdateLayout();
                auto content = LeftScroll().Content().as<UIElement>();
                offset = ThreePointHintText().TransformToVisual(content).TransformPoint({ 0, 0 }).Y - 70;
            }
            LeftScroll().ChangeView(nullptr, offset, nullptr, true);
        }
        OnTick();
    }

    void TeachingPage::OnTick()
    {
        if (!m_loaded) return;

        // Model được thay ở nơi khác (model mô phỏng lúc Init, nạp ở trang khác…): lưới ô đang giữ là của đối tượng cũ.
        if (McDataManager::Model() != m_model)
        {
            Guarded("Teaching model reload", [&] {
                m_model = McDataManager::Model();
                OnTraySwitched();
            });
        }

        for (JogAxisRow& row : m_jogRows)
        {
            if (!row.Axis) continue;
            ui::SetText(row.Pos, ui::AxisPos(*row.Axis, 3));
            auto cfg = row.Axis->Config();
            ui::SetText(row.Unit, cfg ? cfg->Unit : std::string());
        }

        // Đọc lại DO đèn CAM + van hút/thổi để tô màu nút — không có nguồn nào khác báo khi DO đổi (kể cả đổi bởi
        // Routine_Auto lúc chạy Auto).
        if (auto io = McControl::Io())
        {
            m_cam1LampOn = io->GetDo(sca::IoMap::DoLampCam1);
            m_cam2LampOn = io->GetDo(sca::IoMap::DoLampCam2);
            m_vac1On = io->GetDo(sca::IoMap::DoVacuum1);
            m_vac2On = io->GetDo(sca::IoMap::DoVacuum2);
            m_blow1On = io->GetDo(sca::IoMap::DoBlow1);
            m_blow2On = io->GetDo(sca::IoMap::DoBlow2);
        }
        ui::SetTint(Vac1Button(), m_vac1On ? ui::Tint::Green : ui::Tint::None, true);
        ui::SetTint(Vac2Button(), m_vac2On ? ui::Tint::Green : ui::Tint::None, true);
        ui::SetTint(Blow1Button(), m_blow1On ? ui::Tint::Green : ui::Tint::None, true);
        ui::SetTint(Blow2Button(), m_blow2On ? ui::Tint::Green : ui::Tint::None, true);
        ui::SetTint(CamBtmLightButton(), m_cam1LampOn ? ui::Tint::Green : ui::Tint::None, true);
        ui::SetTint(CamTopLightButton(), m_cam2LampOn ? ui::Tint::Green : ui::Tint::None, true);

        // Mức jog dùng chung với trang Manual / Vision Edit — có thể vừa đổi ở đó.
        if (auto func = McControl::Func())
        {
            int fast = func->JogFast() ? 1 : 0;
            if (fast != m_jogFastShown)
            {
                m_jogFastShown = fast;
                ui::SetJogSpeedButtons(JogSlowButton(), JogFastButton(), fast != 0);
            }
        }

        Guarded("Teaching refresh", [&] { RefreshTexts(); });
        RefreshBusy();
    }

    void TeachingPage::RefreshBusy()
    {
        int busy = m_busy ? 1 : 0;
        if (busy == m_busyShown) return;
        m_busyShown = busy;
        bool enabled = !m_busy;
        ui::SetEnabled(CellScroll(), enabled);
        ui::SetEnabled(CellActionsHost(), enabled);
        ui::SetEnabled(GoCamAnchorButton(), enabled);
        ui::SetEnabled(GoJigPick1Button(), enabled);
        ui::SetEnabled(GoJigPick2Button(), enabled);
        ui::SetEnabled(GoJigCamButton(), enabled);
        for (auto const& b : m_jogMoveButtons) ui::SetEnabled(b, enabled);
        BusyRing().IsActive(m_busy);
        ui::ShowInPlace(BusyRing(), m_busy);
    }

    // ───────────────────────── tray / Tool ─────────────────────────

    std::shared_ptr<sca::TrayLayout> TeachingPage::LayoutOf(int tray) const
    {
        auto model = McDataManager::Model();
        return tray == AssyTraySel ? model->AssyTray : model->InputLayout(tray);
    }

    std::string TeachingPage::CurrentZAxis() const { return m_useNozzle1 ? AxisId::Z1 : AxisId::Z2; }

    std::string TeachingPage::TrayTextOf(int tray) const
    {
        return Loc::T(tray == 0 ? "Teach.TrayInput1Lower" : tray == 1 ? "Teach.TrayInput2Lower" : "Teach.TrayAssyLower");
    }

    /// <summary>"pick" / "place" theo tray đang chọn — tray đầu vào là nơi HÚT, tray lắp ráp là nơi ĐẶT.</summary>
    std::string TeachingPage::PickPlaceWord() const { return Loc::T(UseInputTray() ? "Teach.WordPick" : "Teach.WordPlace"); }

    /// <summary>Một dòng cho mỗi tay: đã teach điểm giảm tốc chưa, có đúng phía không. Không chặn — chỉ nói máy sẽ làm gì.</summary>
    std::string TeachingPage::ZSlowLine(sca::TrayLayout const& layout, std::string const& zAxis) const
    {
        double slow = layout.ZSlowFor(zAxis);
        double pick = layout.ZPickFor(zAxis);
        std::string what = PickPlaceWord();

        if (slow <= 0) return Loc::F("Teach.ZSlowNotTaught", zAxis, what);
        if (slow >= pick) return Loc::F("Teach.ZSlowWrongSide", zAxis, slow, what, pick);
        return Loc::F("Teach.ZSlowOk", zAxis, pick - slow);
    }

    std::string TeachingPage::VerifyLine(sca::TrayLayout const& layout) const
    {
        std::string zAxis = CurrentZAxis();
        if (!layout.IsTaughtFor(zAxis)) return Loc::T("Teach.VerifyNotTaught");
        double err = 0;
        bool ok = layout.VerifyGrid(zAxis, err);
        if (!layout.GeometryFor(zAxis).PRC_Verify.Taught) return Loc::T("Teach.VerifyNoPoint");
        return ok ? Loc::F("Teach.VerifyOk", err, layout.VerifyThreshold) : Loc::F("Teach.VerifyFail", err, layout.VerifyThreshold);
    }

    /// <summary>Còn thiếu điểm nào của cơ cấu kẹp, và hai điều kiện CƠ KHÍ mà bước hand-eye dựa vào. Báo chứ không chặn.</summary>
    std::string TeachingPage::HandEyeJigText() const
    {
        auto m = McDataManager::Model();
        std::vector<std::string> missing;
        if (!m->HandEyePick_Nozzle1.Taught) missing.push_back(Loc::F("Teach.JigMissingPick", 1));
        if (!m->HandEyePick_Nozzle2.Taught) missing.push_back(Loc::F("Teach.JigMissingPick", 2));
        if (!m->HandEyeCamPos.Taught) missing.push_back(Loc::T("Teach.JigMissingCam"));

        std::string head = !missing.empty() ? Loc::F("Teach.JigNotTaught", ::base::Join(missing, ", ")) : Loc::T("Teach.JigAllTaught");
        return head + "\n" + Loc::F("Teach.JigNote", m->AssyTray->Z_PickZ1, m->AssyTray->Z_PickZ2);
    }

    void TeachingPage::SelectTray(int tray)
    {
        m_selectedTray = tray;
        if (tray != AssyTraySel) m_lastInputTray = tray;
        OnTraySwitched();
    }

    void TeachingPage::OnTraySwitched()
    {
        auto layout = Layout();
        m_rowsInput = layout->Rows();
        m_colsInput = layout->Cols();
        RowsBox().Value(m_rowsInput);
        ColsBox().Value(m_colsInput);
        SyncSelectors();
        SyncScanRadios();
        Compute();
    }

    void TeachingPage::SyncSelectors()
    {
        // Đặt true cho nút đúng là đủ — GroupName tự bỏ chọn nút còn lại.
        m_suppressSelectors = true;
        (IsAssyTray() ? TrayAssyRadio() : TrayInputRadio()).IsChecked(true);
        (m_useNozzle1 ? Tool1Radio() : Tool2Radio()).IsChecked(true);
        m_suppressSelectors = false;
    }

    void TeachingPage::SyncScanRadios()
    {
        bool zigzag = Layout()->ScanOrder == sca::EScanOrder::Serpentine;
        int index = zigzag ? 1 : 0;
        if (ScanOrderRadios().SelectedIndex() != index)
        {
            m_suppressSelectors = true;
            ScanOrderRadios().SelectedIndex(index);
            m_suppressSelectors = false;
        }
        ui::teaching::DrawTraversal(TraversalCanvas(), zigzag);
    }

    void TeachingPage::SetScanOrder(sca::EScanOrder order)
    {
        Guarded("Set scan order", [&] {
            McControl::InvalidateVisionCache();   // số quét ô / tư thế trên tay đã cất không còn khớp lưới, mốc hay model
            auto layout = Layout();
            if (layout->ScanOrder == order) return;
            // Tray 2 dùng chung thứ tự quét với Tray 1 — đổi ở tray đầu vào nào cũng là đổi của Tray 1.
            (UseInputTray() ? McDataManager::Model()->InputTray : layout)->ScanOrder = order;
            // TrayState (RunningData, đọc bởi Routine_Auto) là bản sao riêng — SyncRunTrays chép lại (và đưa Tray 2 theo
            // Tray 1).
            McDataManager::SyncRunTrays();
            SyncScanRadios();
            Compute();
        });
    }

    /// <summary>Đổi kích thước lưới KHÔNG tự làm mỗi lần gõ số — bấm Apply mới áp dụng.</summary>
    void TeachingPage::ApplySize()
    {
        Guarded("Apply size", [&] {
            double rows = RowsBox().Value(), cols = ColsBox().Value();
            if (!std::isnan(rows)) m_rowsInput = (int)std::lround(rows);
            if (!std::isnan(cols)) m_colsInput = (int)std::lround(cols);

            McControl::InvalidateVisionCache();
            // Tray 2 dùng chung số hàng/cột với Tray 1 — SyncRunTrays đưa Tray 2 theo và dựng lại sổ tiến độ.
            (UseInputTray() ? McDataManager::Model()->InputTray : Layout())->Resize(m_rowsInput, m_colsInput);
            McDataManager::SyncRunTrays();
            auto layout = Layout();
            m_rowsInput = layout->Rows();
            m_colsInput = layout->Cols();
            RowsBox().Value(m_rowsInput);
            ColsBox().Value(m_colsInput);
            Compute();
        });
    }

    // ───────────────────────── lưới ô ─────────────────────────

    void TeachingPage::Compute()
    {
        auto model = McDataManager::Model();
        std::string zAxis = CurrentZAxis();

        m_cells.clear();
        // Sổ tray đầu vào nối Tray 2 sau Tray 1 theo hàng (ModelData::InputCell): đánh số theo chỉ số sổ — Tray 1 từ 1,
        // Tray 2 từ R×C + 1 — để số trên bảng đúng đường máy đi, kể cả chiều zigzag ở hàng đầu Tray 2.
        auto addTray = [&](int tray, sca::TrayLayout const& layout, int rowBase) {
            int rows = layout.Rows(), cols = layout.Cols();
            for (int r = 0; r < rows; ++r)
            {
                for (int c = 0; c < cols; ++c)
                {
                    auto p = layout.CellPos(zAxis, r, c);
                    const sca::CellOffset* off = layout.GetOffset(zAxis, r, c);
                    ui::teaching::CellInfo cell;
                    cell.Tray = tray;
                    cell.Row = r;
                    cell.Col = c;
                    cell.X = p.X;
                    cell.Y = p.Y;
                    cell.Dx = off ? off->Dx : 0;
                    cell.Dy = off ? off->Dy : 0;
                    cell.PlaceDx = off ? off->PlaceDx : 0;
                    cell.PlaceDy = off ? off->PlaceDy : 0;
                    cell.PlaceDa = off ? off->PlaceDa : 0;
                    cell.VisitOrder = sca::TrayState::VisitIndex(rowBase + r, c, cols, layout.ScanOrder) + 1;
                    m_cells.push_back(cell);
                }
            }
        };
        if (IsAssyTray())
        {
            addTray(AssyTraySel, *model->AssyTray, 0);
        }
        else
        {
            addTray(0, *model->InputTray, 0);
            addTray(1, *model->InputTray2, model->InputTray->Rows());
        }

        // Ô đang chọn giữ qua (Row, Col) — ô không còn tồn tại (vừa thu nhỏ lưới) thì bỏ chọn, và chế độ Input quay về
        // Tray 1 (không còn ô nào nói đang làm Tray 2).
        auto layout = Layout();
        if (m_selRow >= layout->Rows() || m_selCol >= layout->Cols())
        {
            m_selRow = -1;
            m_selCol = -1;
            if (UseInputTray()) m_selectedTray = m_lastInputTray = 0;
        }
        // Bản WPF dựng lại TeachCellRow mới mỗi lần nên setter SelectedCell luôn chạy → nạp lại 3 ô bù của ô chọn.
        LoadTrimBoxes();
        LoadTrimAllBoxes();

        UpdateCellViews();
        RefreshTexts();
    }

    void TeachingPage::UpdateCellViews()
    {
        auto first = LayoutOf(FirstTray());
        bool inFirst = m_selectedTray == FirstTray();
        m_cellView.Update(m_cells, FirstTray(), first->Rows(), first->Cols(), inFirst ? m_selRow : -1, inFirst ? m_selCol : -1);
        if (UseInputTray())
        {
            auto second = LayoutOf(1);
            bool inSecond = m_selectedTray == 1;
            m_cellView2.Update(m_cells, 1, second->Rows(), second->Cols(), inSecond ? m_selRow : -1, inSecond ? m_selCol : -1);
        }
    }

    const ui::teaching::CellInfo* TeachingPage::SelectedCell() const
    {
        if (m_selRow < 0 || m_selCol < 0) return nullptr;
        for (auto const& c : m_cells)
            if (c.Tray == m_selectedTray && c.Row == m_selRow && c.Col == m_selCol) return &c;
        return nullptr;
    }

    void TeachingPage::SelectCell(int tray, int row, int col)
    {
        if (tray == m_selectedTray && row == m_selRow && col == m_selCol) return;
        // Chế độ Input: ô ở lưới nào thì tray đó thành tray đang làm — Move, hạ Z, Set Offset, Go Pick theo nó.
        m_selectedTray = tray;
        if (tray != AssyTraySel) m_lastInputTray = tray;
        m_selRow = row;
        m_selCol = col;
        // Nạp 3 ô nhập theo ô vừa chọn — không nạp lại thì số của ô trước còn nằm đó và một cái bấm Apply là chép nhầm
        // lượng bù sang ô khác.
        LoadTrimBoxes();
        UpdateCellViews();
        UpdateCellUi();
    }

    void TeachingPage::LoadTrimBoxes()
    {
        auto cell = SelectedCell();
        TrimXBox().Value(cell ? cell->PlaceDx : 0);
        TrimYBox().Value(cell ? cell->PlaceDy : 0);
        TrimABox().Value(cell ? cell->PlaceDa : 0);
    }

    /// <summary>Nạp 3 ô bù chung theo tray + Tool đang chọn — gọi trong Compute, nên đổi Tool, đổi tray hay nạp model đều
    /// hiện đúng lượng bù của lưới đó.</summary>
    void TeachingPage::LoadTrimAllBoxes()
    {
        auto layout = Layout();
        const auto& geo = layout->GeometryFor(CurrentZAxis());
        TrimAllXBox().Value(geo.PlaceDxAll);
        TrimAllYBox().Value(geo.PlaceDyAll);
        TrimAllABox().Value(geo.PlaceDaAll);
    }

    bool TeachingPage::HasPlaceTrimAll() const
    {
        auto layout = Layout();
        const auto& geo = layout->GeometryFor(CurrentZAxis());
        return std::fabs(geo.PlaceDxAll) > 1e-9 || std::fabs(geo.PlaceDyAll) > 1e-9 || std::fabs(geo.PlaceDaAll) > 1e-9;
    }

    void TeachingPage::UpdateCellUi()
    {
        auto layout = Layout();
        auto cell = SelectedCell();

        // Chế độ Input có hai lưới — ghi rõ ô thuộc tray nào ("Tray 2 · cell (1, 3)").
        std::string cellText = !cell ? Loc::T("Teach.NoCellHint")
                             : UseInputTray() ? Loc::F("Tray.SummaryOfTray", cell->Tray + 1, Loc::F("Teach.CellLabel", cell->Cell()))
                                              : cell->Cell();
        ui::SetText(SelectedCellText(), cellText);
        if (cell)
        {
            ui::SetText(SelectedCellInfoText(),
                        Loc::Format("Order #{0}   X={1:0.000}  Y={2:0.000}  Offset={3:0.000}/{4:0.000}",
                                    { cell->VisitOrder, cell->X, cell->Y, cell->Dx, cell->Dy }));
        }
        ui::Show(SelectedCellInfoText(), cell != nullptr);

        // Chỗ đứng soi của CAM top-down — chỉ ở Tray lắp ráp × Tool 2: chỗ đứng soi tính theo lưới Tool 2
        // (TrayLayout::CamGridZAxis), hiện lúc chọn Tool 1 là mời hiểu nhầm rằng nó đi theo lưới đang xem.
        bool camMove = ShowCamMove();
        ui::Show(CamPosText(), camMove);
        ui::Show(MoveCamButton(), camMove);
        if (camMove)
        {
            std::string text;
            if (!cell) text = Loc::T("Teach.NoCellSelected");
            else if (!layout->CamTopDownAnchorTaught) text = Loc::T("Teach.CamPosNoAnchor");
            else
            {
                auto p = layout->CamPosFor(cell->Row, cell->Col);
                text = Loc::F("Teach.CamPosAt", cell->Cell(), p.X, layout->AxisRow, p.Y);
            }
            ui::SetText(CamPosText(), text);
        }

        // Nhãn nút hạ Z — trục theo Tool đang chọn, Pick/Place theo tray đang teach.
        ui::SetContent(ZCellButton(), ui::H(CurrentZAxis() + (UseInputTray() ? " Pick" : " Place")));

        ui::SetEnabled(ClearOffsetButton(), cell && cell->HasOffset());
        ui::SetEnabled(ClearTrimButton(), cell && cell->HasPlaceTrim());
        ui::SetEnabled(ClearTrimAllButton(), HasPlaceTrimAll());
        // Chỉ Tray lắp ráp mới có bù đặt — hút ở tray đầu vào không qua camera.
        ui::Show(TrimBlock(), IsAssyTray());
    }

    void TeachingPage::RefreshTexts()
    {
        auto model = McDataManager::Model();
        auto first = LayoutOf(FirstTray());   // hàng / cột thứ nhất: Tray 1 (Input) hoặc Assy
        auto second = LayoutOf(1);            // hàng / cột Tray 2 — chỉ hiện ở chế độ Input
        std::string zAxis = CurrentZAxis();
        bool input = UseInputTray();

        ui::SetText(CurrentModelText(), model->Name);

        // ── chọn tray ──
        // Bộ chọn nằm trên card CELL COORDS — ở đầu cột trái vẫn nhắc đang làm tray nào, Tool nào.
        ui::SetText(TrayTitleText(), Loc::F("Teach.TrayToolTitle", Loc::T(input ? "Teach.TrayInputBoth" : "Teach.TrayAssy"), m_useNozzle1 ? 1 : 2));
        ui::Show(SharedSizeText(), input);

        // Phần của Tray 2 chỉ ẩn/hiện khi đổi chế độ tray (người bấm), không theo trạng thái máy.
        int tray2 = input ? 1 : 0;
        if (tray2 != m_tray2Shown)
        {
            m_tray2Shown = tray2;
            UIElement const tray2Parts[] = {
                Tray1PointsCaption(), Tray2PointsBlock(), CellTray1Text(), CellTray2Block(),
                ZPickHead1Text(), ZPickHead2Text(), T2ZPickZ1Box(), T2ZPickZ2Box(), T2TeachZPickZ1Button(), T2TeachZPickZ2Button(),
                ZSlowHead1Text(), ZSlowHead2Text(), T2ZSlowZ1Box(), T2ZSlowZ2Box(), T2TeachZSlowZ1Button(), T2TeachZSlowZ2Button(),
            };
            for (UIElement const& part : tray2Parts) ui::Show(part, input);
            // Cột giá trị Tray 2 của hai bảng cao độ: chế độ Assy co về 0 để cột thứ nhất giãn hết bề ngang.
            GridLength width = input ? GridLengthHelper::FromValueAndType(1, GridUnitType::Star) : GridLengthHelper::FromPixels(0);
            ZPickGrid().ColumnDefinitions().GetAt(3).Width(width);
            ZSlowGrid().ColumnDefinitions().GetAt(3).Width(width);
        }
        // "Tray 2 · outer" (+ "· not taught" khi chưa teach đủ cả hai Tool — máy chỉ dùng tray đã teach đủ).
        auto trayCaption = [](int tray, sca::TrayLayout const& layout) {
            std::string text = Loc::F("Tray.SummaryOfTray", tray + 1, Loc::T(tray == 0 ? "Teach.TrayInner" : "Teach.TrayOuter"));
            return layout.IsTaught() ? text : text + " · " + Loc::T("Tray.NotTaught");
        };
        if (input)
        {
            std::string caption1 = trayCaption(0, *first), caption2 = trayCaption(1, *second);
            ui::SetText(Tray1PointsCaption(), caption1);
            ui::SetText(Tray2PointsCaption(), caption2);
            ui::SetText(CellTray1Text(), caption1);
            ui::SetText(CellTray2Text(), caption2);
        }

        // ── mốc CAM top-down (chỉ Tray lắp ráp) ──
        ui::Show(CamAnchorCard(), !input);
        ui::SetText(CamAnchorText(), Fv(model->AssyTray->CamTopDownAnchorX, "0.000") + " / " + Fv(model->AssyTray->CamTopDownAnchorY, "0.000"));

        // ── teach 3 điểm: hàng thứ nhất (slot 0) và hàng Tray 2 (slot 1) ──
        struct PointRow
        {
            Button Buttons[3];
            TextBlock Xs[3];
            TextBlock Ys[3];
            TextBlock VerifyX, VerifyY, Verify;
        };
        auto drawPoints = [&](int slot, sca::TrayLayout const& layout, PointRow const& row) {
            const auto& geo = layout.GeometryFor(zAxis);
            const sca::TeachPoint* steps[3] = { &geo.P00, &geo.P0C, &geo.PR0 };
            for (int i = 0; i < 3; ++i)
            {
                ui::SetText(row.Xs[i], "X " + Fv(steps[i]->X, "0.000"));
                ui::SetText(row.Ys[i], "Y " + Fv(steps[i]->Y, "0.000"));
                int taught = steps[i]->Taught ? 1 : 0;
                int& shown = m_stepTaughtShown[slot * 3 + i];
                if (taught != shown)
                {
                    shown = taught;
                    if (taught) row.Buttons[i].Background(ui::Res(L"VisionOkBrush"));
                    else row.Buttons[i].ClearValue(Control::BackgroundProperty());
                }
            }
            ui::SetText(row.VerifyX, Fv(geo.PRC_Verify.X, "0.000"));
            ui::SetText(row.VerifyY, Fv(geo.PRC_Verify.Y, "0.000"));
            ui::SetText(row.Verify, VerifyLine(layout));
        };
        drawPoints(0, *first, PointRow{ { P1Button(), P2Button(), P3Button() }, { P1XText(), P2XText(), P3XText() },
                                        { P1YText(), P2YText(), P3YText() }, VerifyXText(), VerifyYText(), VerifyText() });
        if (input)
        {
            drawPoints(1, *second, PointRow{ { T2P1Button(), T2P2Button(), T2P3Button() }, { T2P1XText(), T2P2XText(), T2P3XText() },
                                             { T2P1YText(), T2P2YText(), T2P3YText() }, T2VerifyXText(), T2VerifyYText(), T2VerifyText() });
            ui::SetText(T2ColsText(), std::to_string(second->Cols()));
            ui::SetText(T2RowsText(), std::to_string(second->Rows()));
        }

        // ── góc hút (chỉ tray đầu vào — góc ĐẶT do vision tính) ──
        ui::Show(PickAngleCard(), input);
        // Góc hút lưu [0, 360), hiển thị có dấu như toạ độ trục A ở đầu trang (Tuấn 2026-10-30).
        ui::SetText(PickA1Text(), sca::AxisAngle::SignedText(model->PickAngleA1, 2) + "°");
        ui::SetText(PickA2Text(), sca::AxisAngle::SignedText(model->PickAngleA2, 2) + "°");

        // ── cao độ hút/đặt + điểm giảm tốc: cùng cặp Z1/Z2 trên mỗi lưới, nghĩa vật lý theo tray; Tray 2 teach Z riêng ──
        ui::SetText(ZPickTitleText(), Loc::T(input ? "Teach.ZPickTitle" : "Teach.ZPlaceTitle"));
        ui::SetText(ZPickHintText(), Loc::T(input ? "Teach.ZPickHint" : "Teach.ZPlaceHint"));
        ui::SetText(ZSlowTitleText(), Loc::T(input ? "Teach.ZSlowPickTitle" : "Teach.ZSlowPlaceTitle"));
        ui::SetText(ZSlowHintText(), Loc::F("Teach.ZSlowHint", PickPlaceWord()));
        // Ô số: chỉ đặt khi model đổi và ô không có focus — OnTick không ghi đè số đang gõ.
        for (ZBox& z : m_zBoxes)
            if (!z.SecondCol || input) ShowZBox(z, ZFieldOf(z.SecondCol ? *second : *first, z));
        std::string slowStatus = ZSlowLine(*first, AxisId::Z1) + "\n" + ZSlowLine(*first, AxisId::Z2);
        if (input)
        {
            auto ofTray = [](int tray, std::string const& line) { return Loc::F("Tray.SummaryOfTray", tray, line); };
            slowStatus = ofTray(1, ZSlowLine(*first, AxisId::Z1)) + "\n" + ofTray(1, ZSlowLine(*first, AxisId::Z2)) + "\n" +
                         ofTray(2, ZSlowLine(*second, AxisId::Z1)) + "\n" + ofTray(2, ZSlowLine(*second, AxisId::Z2));
        }
        ui::SetText(ZSlowStatusText(), slowStatus);

        // ── vị trí soi CAM bottom-up ──
        ui::SetText(AlignX1Text(), MmText(model->AlignPosX_Nozzle1));
        ui::SetText(AlignX2Text(), MmText(model->AlignPosX_Nozzle2));

        // ── cơ cấu kẹp ──
        auto jig = [](sca::TeachPoint const& p) { return Loc::Format("X {0:0.000} · Y2 {1:0.000}", { p.X, p.Y }); };
        ui::SetText(JigPick1Text(), jig(model->HandEyePick_Nozzle1));
        ui::SetText(JigPick2Text(), jig(model->HandEyePick_Nozzle2));
        ui::SetText(JigCamText(), jig(model->HandEyeCamPos));
        ui::SetText(JigStatusText(), HandEyeJigText());

        UpdateCellUi();
    }

    // ───────────────────────── teach ─────────────────────────

    /// <summary>
    /// Đọc vị trí THỰC của trục X và trục hàng (Y1/Y2 tuỳ tray), hỏi lại, rồi ghi vào đúng điểm teach của lưới tray ×
    /// Tool đang chọn. <c>TeachPoint(x, y)</c> là đường DUY NHẤT bật cờ Taught. Không chặn khi chưa về gốc.
    /// </summary>
    winrt::fire_and_forget TeachingPage::TeachGridPoint(GridPoint which, int tray)
    {
        auto self = get_strong();
        try
        {
            auto layout = LayoutOf(tray);
            std::string rowAxis = layout->AxisRow;
            auto axisX = AxisOf(AxisId::X), axisRow = AxisOf(rowAxis);
            if (!axisX || !axisRow) co_return;
            double x = axisX->CurrentPos();
            double y = axisRow->CurrentPos();
            std::string zAxis = CurrentZAxis();
            bool input = tray != AssyTraySel;
            int nozzle = m_useNozzle1 ? 1 : 2;

            const char* nameKey = which == GridPoint::P00 ? "Teach.PointP1" : which == GridPoint::P0C ? "Teach.PointP2"
                                : which == GridPoint::PR0 ? "Teach.PointP3" : "Teach.PointVerify";
            auto& geo = layout->GeometryFor(zAxis);
            sca::TeachPoint old = which == GridPoint::P00 ? geo.P00 : which == GridPoint::P0C ? geo.P0C
                                : which == GridPoint::PR0 ? geo.PR0 : geo.PRC_Verify;

            // Lưới teach riêng theo tray × Tool — câu hỏi phải nói đang ghi vào bản nào.
            if (!co_await ConfirmTeach(Loc::F("Teach.WhatGridPoint", Loc::T(nameKey), TrayTextOf(tray), nozzle),
                                       XyText(old, rowAxis), XyText(x, rowAxis, y)))
                co_return;

            McControl::InvalidateVisionCache();   // số quét ô / tư thế trên tay đã cất không còn khớp lưới, mốc hay model
            NoteTeachWithoutOrigin("grid teach point");
            auto& target = layout->GeometryFor(zAxis);
            sca::TeachPoint point(x, y);
            switch (which)
            {
            case GridPoint::P00: target.P00 = point; break;
            case GridPoint::P0C: target.P0C = point; break;
            case GridPoint::PR0: target.PR0 = point; break;
            case GridPoint::Verify: target.PRC_Verify = point; break;
            }
            // Teach xong Tray 2 là sổ tray đầu vào nối thêm Tray 2 (ModelData::InputTraysInUse).
            if (input) McDataManager::SyncRunTrays();
            Compute();
        }
        catch (const std::exception& ex) { LogUiError("Teach grid point", ex.what()); ui::Dialogs::Error(ex.what()); }
        catch (...) { LogUiError("Teach grid point", "unknown error"); }
    }

    winrt::fire_and_forget TeachingPage::TeachZPick(std::string zAxis, int tray)
    {
        auto self = get_strong();
        try
        {
            auto axis = AxisOf(zAxis);
            if (!axis) co_return;
            double z = axis->CurrentPos();
            auto layout = LayoutOf(tray);
            double old = zAxis == AxisId::Z1 ? layout->Z_PickZ1 : layout->Z_PickZ2;
            if (!co_await ConfirmTeach(Loc::F(tray != AssyTraySel ? "Teach.WhatZPick" : "Teach.WhatZPlace", zAxis, TrayTextOf(tray)),
                                       MmText(old), MmText(z)))
                co_return;
            NoteTeachWithoutOrigin("pick height " + zAxis);
            if (zAxis == AxisId::Z1) layout->Z_PickZ1 = z; else layout->Z_PickZ2 = z;
            RefreshTexts();   // quãng chạy chậm đo từ cao độ hút/đặt
        }
        catch (const std::exception& ex) { LogUiError("Teach Z pick", ex.what()); ui::Dialogs::Error(ex.what()); }
        catch (...) { LogUiError("Teach Z pick", "unknown error"); }
    }

    /// <summary>Ghi cao độ BẮT ĐẦU GIẢM TỐC — cùng cách teach với cao độ hút/đặt. Không chặn khi số nằm sai phía: dòng
    /// trạng thái nói rõ máy sẽ bỏ qua.</summary>
    winrt::fire_and_forget TeachingPage::TeachZSlow(std::string zAxis, int tray)
    {
        auto self = get_strong();
        try
        {
            auto axis = AxisOf(zAxis);
            if (!axis) co_return;
            double z = axis->CurrentPos();
            auto layout = LayoutOf(tray);
            double old = zAxis == AxisId::Z1 ? layout->Z_SlowZ1 : layout->Z_SlowZ2;
            if (!co_await ConfirmTeach(Loc::F("Teach.WhatZSlow", zAxis, TrayTextOf(tray)), MmText(old), MmText(z))) co_return;
            NoteTeachWithoutOrigin("slow pt " + zAxis);
            if (zAxis == AxisId::Z1) layout->Z_SlowZ1 = z; else layout->Z_SlowZ2 = z;
            RefreshTexts();
        }
        catch (const std::exception& ex) { LogUiError("Teach Z slow", ex.what()); ui::Dialogs::Error(ex.what()); }
        catch (...) { LogUiError("Teach Z slow", "unknown error"); }
    }

    // ───────────────────────── cao độ gõ tay ─────────────────────────
    // Cao độ hút/đặt và điểm giảm tốc là số thường (không có cờ Taught như TeachPoint), nên gõ tay không chặn Auto. Gõ tay
    // vẫn đi qua cùng hộp hỏi Old/New với nút Teach, và bị chặn ngoài soft limit của trục Z.

    void TeachingPage::BuildZBoxes()
    {
        struct Def { NumberBox Box; const char* Axis; bool SecondCol; bool Slow; };
        const Def defs[] = {
            { ZPickZ1Box(), AxisId::Z1, false, false },   { ZPickZ2Box(), AxisId::Z2, false, false },
            { ZSlowZ1Box(), AxisId::Z1, false, true },    { ZSlowZ2Box(), AxisId::Z2, false, true },
            { T2ZPickZ1Box(), AxisId::Z1, true, false },  { T2ZPickZ2Box(), AxisId::Z2, true, false },
            { T2ZSlowZ1Box(), AxisId::Z1, true, true },   { T2ZSlowZ2Box(), AxisId::Z2, true, true },
        };
        m_zBoxes.clear();
        for (Def const& d : defs)
        {
            ui::ConfigureNumberBox(d.Box, 3);
            ToolTipService::SetToolTip(d.Box, box_value(ui::T("Teach.ZTypeTip")));
            ZBox z;
            z.Box = d.Box;
            z.ZAxis = d.Axis;
            z.SecondCol = d.SecondCol;
            z.Slow = d.Slow;
            m_zBoxes.push_back(std::move(z));
        }
        // Sau vòng trên m_zBoxes không đổi kích thước nữa — chỉ số trong lambda luôn đúng.
        for (size_t i = 0; i < m_zBoxes.size(); ++i)
        {
            m_zBoxes[i].Box.ValueChanged([weak = get_weak(), i](NumberBox const&, NumberBoxValueChangedEventArgs const&) {
                if (auto self = weak.get()) self->CommitZTyped(i);
            });
        }
    }

    double& TeachingPage::ZFieldOf(sca::TrayLayout& layout, ZBox const& z) const
    {
        const bool z1 = z.ZAxis == AxisId::Z1;
        if (z.Slow) return z1 ? layout.Z_SlowZ1 : layout.Z_SlowZ2;
        return z1 ? layout.Z_PickZ1 : layout.Z_PickZ2;
    }

    void TeachingPage::ShowZBox(ZBox& z, double value, bool force)
    {
        if (!z.Box) return;
        if (!force)
        {
            if (z.Shown == value) return;                                      // NaN ≠ NaN: lần đầu luôn đặt
            if (z.Box.FocusState() != FocusState::Unfocused) return;           // người đang gõ
        }
        z.Shown = value;
        z.Box.Value(value);   // bắn ValueChanged → CommitZTyped thấy số = model, bỏ qua
    }

    winrt::fire_and_forget TeachingPage::CommitZTyped(size_t index)
    {
        auto self = get_strong();
        if (index >= m_zBoxes.size()) co_return;
        try
        {
            ZBox& z = m_zBoxes[index];
            const int tray = z.SecondCol ? 1 : FirstTray();
            auto layout = LayoutOf(tray);
            if (!layout) co_return;
            const double old = ZFieldOf(*layout, z);
            const double v = z.Box.Value();
            if (std::isnan(v)) { ShowZBox(z, old, true); co_return; }   // ô trống: trả lại số cũ
            if (std::abs(v - old) < 0.0005) co_return;                   // số do code đặt, hoặc chỉ định dạng lại
            if (m_zCommitBusy) { ShowZBox(z, old, true); co_return; }    // đang hỏi cho ô khác

            const std::string what = Loc::F(z.Slow ? "Teach.WhatZSlow" : tray != AssyTraySel ? "Teach.WhatZPick" : "Teach.WhatZPlace",
                                            z.ZAxis, TrayTextOf(tray));
            auto axis = AxisOf(z.ZAxis);
            auto cfg = axis ? axis->Config() : nullptr;
            const double lo = std::max(0.0, cfg ? cfg->SoftLimitNeg : 0.0);
            const double hi = cfg ? cfg->SoftLimitPos : std::numeric_limits<double>::infinity();
            std::string why;
            if (!z.Slow && v <= 0) why = Loc::F("Teach.ZTypeNotPositive", what);
            else if (v < lo || v > hi) why = Loc::F("Teach.ZTypeOutOfRange", what, v, z.ZAxis, lo, hi);
            if (!why.empty())
            {
                ShowZBox(z, old, true);
                ui::Dialogs::Warn(why);
                co_return;
            }

            bool ok = false;
            {
                m_zCommitBusy = true;
                struct Reset { bool& Flag; ~Reset() { Flag = false; } } reset{ m_zCommitBusy };
                ok = co_await ConfirmTeach(what, MmText(old), MmText(v));
            }
            // Trong lúc hỏi có thể đã nạp model khác: không ghi vào lưới cũ.
            auto now = LayoutOf(tray);
            if (!ok || now != layout)
            {
                if (now) ShowZBox(z, ZFieldOf(*now, z), true);
                co_return;
            }
            ZFieldOf(*layout, z) = v;
            sca::LogWorker::Instance().RecordLog(
                Loc::Format("Teaching: typed {0} = {1:0.000} mm (was {2:0.000})", { what, v, old }), sca::LogLevel::Info, "Teaching");
            ShowZBox(z, v, true);
            RefreshTexts();   // quãng chạy chậm đo từ cao độ hút/đặt
        }
        catch (const std::exception& ex) { LogUiError("Type Z height", ex.what()); ui::Dialogs::Error(ex.what()); }
        catch (winrt::hresult_error const& ex) { LogUiError("Type Z height", winrt::to_string(ex.message())); }
        catch (...) { LogUiError("Type Z height", "unknown error"); }
    }

    /// <summary>Ghi GÓC HÚT của tay <paramref name="nozzle"/> — toạ độ trục A quấn về [0, 360). Mọi lần hạ Z xuống hút
    /// đều xoay A về đây trước (Tuấn 2026-09-23). Không gọi InvalidateVisionCache.</summary>
    winrt::fire_and_forget TeachingPage::TeachPickAngle(int nozzle)
    {
        auto self = get_strong();
        try
        {
            std::string aAxis = nozzle == 1 ? AxisId::A1 : AxisId::A2;
            auto axis = AxisOf(aAxis);
            if (!axis) co_return;
            double a = sca::AxisAngle::Wrap(axis->CurrentPos());
            auto model = McDataManager::Model();
            double old = nozzle == 1 ? model->PickAngleA1 : model->PickAngleA2;
            if (!co_await ConfirmTeach(Loc::F("Teach.WhatPickAngle", aAxis), sca::AxisAngle::SignedText(old, 2) + "°",
                                       sca::AxisAngle::SignedText(a, 2) + "°"))
                co_return;
            NoteTeachWithoutOrigin("pick angle " + aAxis);
            if (nozzle == 1) model->PickAngleA1 = a; else model->PickAngleA2 = a;
            RefreshTexts();
        }
        catch (const std::exception& ex) { LogUiError("Teach pick angle", ex.what()); ui::Dialogs::Error(ex.what()); }
        catch (...) { LogUiError("Teach pick angle", "unknown error"); }
    }

    /// <summary>X của gantry đưa tay hút vào giữa khung hình CAM bottom-up — teach RIÊNG từng tay (lệch cơ khí).</summary>
    winrt::fire_and_forget TeachingPage::TeachAlignPos(int nozzle)
    {
        auto self = get_strong();
        try
        {
            auto axis = AxisOf(AxisId::X);
            if (!axis) co_return;
            double x = axis->CurrentPos();
            auto model = McDataManager::Model();
            double old = nozzle == 1 ? model->AlignPosX_Nozzle1 : model->AlignPosX_Nozzle2;
            if (!co_await ConfirmTeach(Loc::F("Teach.WhatAlignPos", nozzle), "X " + MmText(old), "X " + MmText(x))) co_return;
            McControl::InvalidateVisionCache();
            NoteTeachWithoutOrigin("nozzle #" + std::to_string(nozzle) + " bottom CAM pos.");
            if (nozzle == 1) model->AlignPosX_Nozzle1 = x; else model->AlignPosX_Nozzle2 = x;
            RefreshTexts();
        }
        catch (const std::exception& ex) { LogUiError("Teach align pos", ex.what()); ui::Dialogs::Error(ex.what()); }
        catch (...) { LogUiError("Teach align pos", "unknown error"); }
    }

    /// <summary>
    /// Vị trí gantry (X, Y2) để đầu hút của tay <paramref name="nozzle"/> nằm trên con can mẫu trên cơ cấu kẹp (Tuấn chốt
    /// 2026-09-25). Trục hàng lấy của tray LẮP RÁP (cơ cấu kẹp gá trên bàn đó). <b>Không gọi InvalidateVisionCache</b>: ba
    /// điểm này chỉ dùng trong bước xác định hand-eye.
    /// </summary>
    winrt::fire_and_forget TeachingPage::TeachHandEyePick(int nozzle)
    {
        auto self = get_strong();
        try
        {
            auto model = McDataManager::Model();
            std::string rowAxis = model->AssyTray->AxisRow;
            auto axisX = AxisOf(AxisId::X), axisRow = AxisOf(rowAxis);
            if (!axisX || !axisRow) co_return;
            std::string what = Loc::F("Teach.WhatJigPick", nozzle);
            sca::TeachPoint point(axisX->CurrentPos(), axisRow->CurrentPos());
            if (!co_await ConfirmTeach(what, XyText(model->HandEyePick(nozzle), rowAxis), XyText(point, rowAxis))) co_return;
            NoteTeachWithoutOrigin(what);
            if (nozzle == 1) model->HandEyePick_Nozzle1 = point; else model->HandEyePick_Nozzle2 = point;
            RefreshTexts();
        }
        catch (const std::exception& ex) { LogUiError("Teach jig pick", ex.what()); ui::Dialogs::Error(ex.what()); }
        catch (...) { LogUiError("Teach jig pick", "unknown error"); }
    }

    /// <summary>Vị trí gantry (X, Y2) để CAM top-down ngắm dấu định vị của chi tiết đích trên cơ cấu kẹp.</summary>
    winrt::fire_and_forget TeachingPage::TeachHandEyeCamPos()
    {
        auto self = get_strong();
        try
        {
            auto model = McDataManager::Model();
            std::string rowAxis = model->AssyTray->AxisRow;
            auto axisX = AxisOf(AxisId::X), axisRow = AxisOf(rowAxis);
            if (!axisX || !axisRow) co_return;
            std::string what = Loc::T("Teach.WhatJigCam");
            sca::TeachPoint point(axisX->CurrentPos(), axisRow->CurrentPos());
            if (!co_await ConfirmTeach(what, XyText(model->HandEyeCamPos, rowAxis), XyText(point, rowAxis))) co_return;
            NoteTeachWithoutOrigin(what);
            model->HandEyeCamPos = point;
            RefreshTexts();
        }
        catch (const std::exception& ex) { LogUiError("Teach jig cam", ex.what()); ui::Dialogs::Error(ex.what()); }
        catch (...) { LogUiError("Teach jig cam", "unknown error"); }
    }

    /// <summary>
    /// Teach vị trí gantry khi CAM top-down nằm đúng tâm Ô (1,1) của Assy Tray — jog tay theo ảnh trực tiếp. MỘT mốc dùng
    /// chung cho cả 2 Tool (camera gắn cố định trên gantry, Tuấn xác nhận 2026-09-05).
    /// </summary>
    winrt::fire_and_forget TeachingPage::TeachCamTopDownAnchor()
    {
        auto self = get_strong();
        try
        {
            auto assy = McDataManager::Model()->AssyTray;
            std::string rowAxis = assy->AxisRow;
            auto axisX = AxisOf(AxisId::X), axisRow = AxisOf(rowAxis);
            if (!axisX || !axisRow) co_return;
            double x = axisX->CurrentPos();
            double y = axisRow->CurrentPos();
            std::string old = assy->CamTopDownAnchorTaught ? XyText(assy->CamTopDownAnchorX, rowAxis, assy->CamTopDownAnchorY)
                                                           : Loc::T("Teach.NotTaught");
            if (!co_await ConfirmTeach(Loc::T("Teach.WhatCamAnchor"), old, XyText(x, rowAxis, y))) co_return;
            McControl::InvalidateVisionCache();
            NoteTeachWithoutOrigin("top CAM ref");
            assy->CamTopDownAnchorX = x;
            assy->CamTopDownAnchorY = y;
            assy->CamTopDownAnchorTaught = true;
            RefreshTexts();
        }
        catch (const std::exception& ex) { LogUiError("Teach top CAM ref", ex.what()); ui::Dialogs::Error(ex.what()); }
        catch (...) { LogUiError("Teach top CAM ref", "unknown error"); }
    }

    /// <summary>
    /// Ghi độ lệch giữa vị trí VỪA JOG TỚI TAY và giá trị nội suy thuần (không offset). Trừ <c>CellPosRaw</c> chứ không
    /// phải <c>CellPos</c> — trừ nhầm sẽ trừ mất offset cũ hai lần.
    /// </summary>
    winrt::fire_and_forget TeachingPage::SetOffset()
    {
        auto self = get_strong();
        try
        {
            auto cell = SelectedCell();
            if (!cell) co_return;
            int r = cell->Row, c = cell->Col;
            auto layout = Layout();
            std::string rowAxis = layout->AxisRow;
            auto axisX = AxisOf(AxisId::X), axisRow = AxisOf(rowAxis);
            if (!axisX || !axisRow) co_return;
            double x = axisX->CurrentPos();
            double y = axisRow->CurrentPos();
            std::string zAxis = CurrentZAxis();
            auto raw = layout->CellPosRaw(zAxis, r, c);
            const sca::CellOffset* oldOff = layout->GetOffset(zAxis, r, c);
            double oldDx = oldOff ? oldOff->Dx : 0, oldDy = oldOff ? oldOff->Dy : 0;
            auto offText = [&](double dx, double dy) { return Loc::Format(Loc::T("Teach.OffsetText"), { dx, rowAxis, dy }); };
            if (!co_await ConfirmTeach(Loc::F("Teach.WhatCellPos", r + 1, c + 1, TrayText(), m_useNozzle1 ? 1 : 2),
                                       offText(oldDx, oldDy), offText(x - raw.X, y - raw.Y)))
                co_return;
            McControl::InvalidateVisionCache();
            NoteTeachWithoutOrigin("cell ofs (" + std::to_string(r + 1) + "," + std::to_string(c + 1) + ")");
            layout->SetOffset(zAxis, r, c, x - raw.X, y - raw.Y);
            Compute();
        }
        catch (const std::exception& ex) { LogUiError("Set offset", ex.what()); ui::Dialogs::Error(ex.what()); }
        catch (...) { LogUiError("Set offset", "unknown error"); }
    }

    void TeachingPage::ClearOffset()
    {
        Guarded("Clear offset", [&] {
            McControl::InvalidateVisionCache();
            auto cell = SelectedCell();
            if (!cell) return;
            Layout()->SetOffset(CurrentZAxis(), cell->Row, cell->Col, 0, 0);
            Compute();
        });
    }

    bool TeachingPage::TryParseTrim(NumberBox const& box, double limit, std::string const& what, double& value)
    {
        value = box.Value();
        if (std::isnan(value))
        {
            ui::Dialogs::Warn(Loc::F("Teach.TrimNotNumber", what));
            return false;
        }
        if (std::fabs(value) > limit)
        {
            ui::Dialogs::Warn(Loc::F("Teach.TrimTooLarge", what, Fv(limit, "0.#")));
            value = 0;
            return false;
        }
        return true;
    }

    /// <summary>
    /// Ghi lượng bù đặt cho ô đang chọn, của đúng Tool đang chọn. Số nhập là lượng CỘNG THÊM vào đích gửi VisionMaster.
    /// Chặn trên ±1 mm / ±5° — lệch hơn thế là lỗi hiệu chuẩn, tinh chỉnh không sửa được mà còn giấu mất lỗi.
    /// </summary>
    void TeachingPage::ApplyPlaceTrim()
    {
        Guarded("Apply cell ofs", [&] {
            auto cell = SelectedCell();
            if (!cell) { ui::Dialogs::Warn(Loc::T("Teach.NoCellChosen")); return; }
            if (UseInputTray()) { ui::Dialogs::Warn(Loc::T("Teach.TrimAssyOnly")); return; }

            double dx, dy, da;
            if (!TryParseTrim(TrimXBox(), TrimLimitMm, "X", dx)) return;
            if (!TryParseTrim(TrimYBox(), TrimLimitMm, "Y", dy)) return;
            if (!TryParseTrim(TrimABox(), TrimLimitDeg, Loc::T("Teach.WordAngle"), da)) return;

            McControl::InvalidateVisionCache();   // tư thế đã cất trên tay tính theo đích cũ
            Layout()->SetPlaceTrim(CurrentZAxis(), cell->Row, cell->Col, dx, dy, da);
            Compute();
        });
    }

    void TeachingPage::ClearPlaceTrim()
    {
        Guarded("Clear cell ofs", [&] {
            auto cell = SelectedCell();
            if (!cell) return;
            McControl::InvalidateVisionCache();
            Layout()->SetPlaceTrim(CurrentZAxis(), cell->Row, cell->Col, 0, 0, 0);
            Compute();
            LoadTrimBoxes();
        });
    }

    /// <summary>Bù chung cho mọi ô của Tool đang chọn — cùng dấu, cùng ngưỡng với bù riêng; App cộng dồn hai lượng
    /// (Tuấn 2026-09-15).</summary>
    void TeachingPage::ApplyPlaceTrimAll()
    {
        Guarded("Apply common ofs", [&] {
            if (UseInputTray()) { ui::Dialogs::Warn(Loc::T("Teach.TrimAssyOnly")); return; }

            double dx, dy, da;
            if (!TryParseTrim(TrimAllXBox(), TrimLimitMm, "X", dx)) return;
            if (!TryParseTrim(TrimAllYBox(), TrimLimitMm, "Y", dy)) return;
            if (!TryParseTrim(TrimAllABox(), TrimLimitDeg, Loc::T("Teach.WordAngle"), da)) return;

            McControl::InvalidateVisionCache();
            Layout()->SetPlaceTrimAll(CurrentZAxis(), dx, dy, da);
            Compute();
        });
    }

    void TeachingPage::ClearPlaceTrimAll()
    {
        Guarded("Clear common ofs", [&] {
            McControl::InvalidateVisionCache();
            Layout()->SetPlaceTrimAll(CurrentZAxis(), 0, 0, 0);
            Compute();
        });
    }

    // ───────────────────────── chạy thử ─────────────────────────

    void TeachingPage::RunBusy(std::function<void()> work)
    {
        m_busy = true;
        RefreshBusy();
        auto weak = get_weak();
        ui::RunBackgroundThenUi(
            [work = std::move(work)] {
                try { work(); }
                // TODO(port): bản WPF để OperationCanceledException lọt ra khỏi lệnh async (không có thông báo riêng) —
                // ở đây nuốt lặng: bị huỷ là do EMG/alarm/STOP, đã có alarm/thông báo của nơi huỷ.
                catch (const ::base::OperationCanceled&) {}
                catch (const std::exception& ex)
                {
                    LogUiError("Move", ex.what());
                    ui::Dialogs::Error(std::string("Move failed: ") + ex.what());
                }
            },
            [weak] {
                if (auto s = weak.get())
                {
                    s->m_busy = false;
                    s->RefreshBusy();
                }
            });
    }

    /// <summary>
    /// Chạy thử tới một vị trí (X, trục hàng) — dùng chung cho ô lưới, mốc CAM top-down, điểm cơ cấu kẹp.
    /// <paramref name="pickNozzle"/> 1/2 = xoay luôn trục A của tay đó về góc hút; 0 = không đụng trục A.
    /// </summary>
    void TeachingPage::MoveToCellXY(std::string rowAxis, double y, double x, std::string label, int pickNozzle)
    {
        if (m_busy) return;
        auto func = McControl::Func();
        auto safety = McControl::Safety();
        if (!func || !safety) return;
        double pickAngle = pickNozzle > 0 ? McDataManager::Model()->PickAngle(pickNozzle) : 0;
        if (label.empty()) label = Loc::T("Teach.Position");

        RunBusy([func, safety, rowAxis, y, x, label, pickNozzle, pickAngle] {
            auto ct = safety->AbortToken();
            bool ok = pickNozzle > 0 ? func->MoveToCellForPick(pickNozzle, pickAngle, rowAxis, y, x, ct)
                                     : func->MoveToCell(rowAxis, y, x, ct);
            if (!ok) ui::Dialogs::Warn(Loc::F("Teach.MoveFailed", label, McControl::Status().Message()));
        });
    }

    /// <summary>
    /// Chạy thử một trục Z tới một cao độ tham chiếu qua McFunction (đủ CanMove). <paramref name="slowAt"/> = điểm giảm
    /// tốc đã teach; 0 = một mạch ở mức Auto cao. Z dương là chiều ĐI XUỐNG: đích nhỏ hơn chỗ đang đứng là đi LÊN.
    /// </summary>
    void TeachingPage::MoveZ(std::string axisName, double target, double slowAt)
    {
        if (axisName.empty() || m_busy) return;
        auto func = McControl::Func();
        auto safety = McControl::Safety();
        auto axis = AxisOf(axisName);
        if (!func || !safety || !axis) return;

        RunBusy([func, safety, axis, axisName, target, slowAt] {
            auto ct = safety->AbortToken();
            bool up = target < axis->CurrentPos();
            bool ok;
            if (slowAt > 0) ok = up ? func->MoveZUp(axisName, target, slowAt, ct) : func->MoveZDown(axisName, target, slowAt, ct);
            else
            {
                auto cfg = axis->Config();
                ok = func->Move(axisName, target, cfg ? cfg->MoveSpeed : 0, ct);
            }
            if (!ok) ui::Dialogs::Warn(Loc::F("Teach.AxisMoveFailed", axisName, target, McControl::Status().Message()));
        });
    }

    /// <summary>
    /// Go T1/T2: Z của tay đó về cao độ soi CAM bottom-up (đúng Z_SAFE — Tuấn chốt 2026-09-22) rồi X tới vị trí soi đã
    /// teach — ảnh lúc teach cùng cỡ, cùng độ nét với lúc chạy thật.
    /// </summary>
    void TeachingPage::MoveToTrigger(int nozzle)
    {
        if (m_busy) return;
        auto func = McControl::Func();
        auto safety = McControl::Safety();
        if (!func || !safety) return;
        std::string zName = nozzle == 1 ? AxisId::Z1 : AxisId::Z2;
        double zCapture = McControl::Motion().ZSafe();
        double x = McDataManager::Model()->AlignPosX(nozzle);
        auto zAxis = AxisOf(zName), xAxis = AxisOf(AxisId::X);
        if (!zAxis || !xAxis) return;

        RunBusy([func, safety, zName, zCapture, x, zAxis, xAxis] {
            auto ct = safety->AbortToken();
            auto zCfg = zAxis->Config();
            if (!func->Move(zName, zCapture, zCfg ? zCfg->MoveSpeed : 0, ct))
            {
                ui::Dialogs::Warn(Loc::F("Teach.ZCaptureFailed", zName, zCapture, McControl::Status().Message()));
                return;
            }
            auto xCfg = xAxis->Config();
            if (!func->Move(AxisId::X, x, xCfg ? xCfg->MoveSpeed : 0, ct))
                ui::Dialogs::Warn(Loc::F("Teach.AxisMoveFailed", AxisId::X, x, McControl::Status().Message()));
        });
    }

    /// <summary>Chạy thử tới ô đang chọn, xoay luôn trục A của Tool đang chọn về GÓC HÚT (Tuấn 2026-09-26).</summary>
    void TeachingPage::MoveToCell()
    {
        auto cell = SelectedCell();
        if (!cell) return;
        auto layout = Layout();
        auto p = layout->CellPos(CurrentZAxis(), cell->Row, cell->Col);
        MoveToCellXY(layout->AxisRow, p.Y, p.X, Loc::F("Teach.CellLabel", cell->Cell()), m_useNozzle1 ? 1 : 2);
    }

    /// <summary>Z của Tool đang chọn hạ tới cao độ hút (tray đầu vào) / đặt (tray lắp ráp) qua điểm giảm tốc. Không đòi
    /// gantry đang đứng đúng ô.</summary>
    void TeachingPage::MoveZAtCell()
    {
        std::string z = CurrentZAxis();
        auto layout = Layout();
        MoveZ(z, layout->ZPickFor(z), layout->ZSlowFor(z));
    }

    /// <summary>Gantry tới chỗ đứng để CAM top-down soi đúng tâm ô đang chọn (cùng toạ độ Routine_Auto.S05_ScanAssy). Chặn
    /// khi chưa teach mốc CAM: CamPosFor tính từ (0, 0) sẽ lôi gantry về đầu hành trình.</summary>
    void TeachingPage::MoveCamToCell()
    {
        auto cell = SelectedCell();
        if (!cell) return;
        auto layout = Layout();
        if (!layout->CamTopDownAnchorTaught)
        {
            ui::Dialogs::Warn(Loc::T("Teach.CamAnchorNotTaughtWarn"));
            return;
        }
        auto p = layout->CamPosFor(cell->Row, cell->Col);
        MoveToCellXY(layout->AxisRow, p.Y, p.X, Loc::F("Teach.CamPosLabel", cell->Cell()));
    }

    void TeachingPage::MoveToHandEyePick(int nozzle)
    {
        auto model = McDataManager::Model();
        sca::TeachPoint point = model->HandEyePick(nozzle);
        if (!point.Taught)
        {
            ui::Dialogs::Warn(Loc::F("Teach.JigPickNotTaughtWarn", nozzle));
            return;
        }
        MoveToCellXY(model->AssyTray->AxisRow, point.Y, point.X, Loc::F("Teach.WhatJigPick", nozzle));
    }

    void TeachingPage::MoveToHandEyeCamPos()
    {
        auto model = McDataManager::Model();
        sca::TeachPoint point = model->HandEyeCamPos;
        if (!point.Taught)
        {
            ui::Dialogs::Warn(Loc::T("Teach.JigCamNotTaughtWarn"));
            return;
        }
        MoveToCellXY(model->AssyTray->AxisRow, point.Y, point.X, Loc::T("Teach.WhatJigCam"));
    }

    void TeachingPage::MoveCamTopDownAnchor()
    {
        auto assy = McDataManager::Model()->AssyTray;
        MoveToCellXY(assy->AxisRow, assy->CamTopDownAnchorY, assy->CamTopDownAnchorX);
    }

    /// <summary>Về gốc Z cũng đi qua chặng chậm — nâng khỏi hốc tray bằng mức Auto THẤP tới điểm giảm tốc rồi mới tăng
    /// tốc (Tuấn 2026-09-24). Điểm giảm tốc lấy của TRAY ĐANG LÀM — chế độ Input là tray của ô đang chọn (đầu hút đang nằm
    /// trong hốc của chính tray đó).</summary>
    void TeachingPage::MoveZHome(std::string axisName)
    {
        auto axis = AxisOf(axisName);
        if (!axis) return;
        auto cfg = axis->Config();
        MoveZ(axisName, cfg ? cfg->HomeOffset : 0, Layout()->ZSlowFor(axisName));
    }

    /// <summary>Cao độ của tray đầu vào chứa ô đang chọn (Tray 2 teach Z riêng; chưa chọn ô = Tray 1); đang ở tray lắp ráp
    /// thì Tray 1.</summary>
    void TeachingPage::MoveZPick(std::string axisName)
    {
        auto input = UseInputTray() ? Layout() : McDataManager::Model()->InputTray;
        MoveZ(axisName, input->ZPickFor(axisName), input->ZSlowFor(axisName));
    }

    void TeachingPage::MoveZPlace(std::string axisName)
    {
        auto assy = McDataManager::Model()->AssyTray;
        MoveZ(axisName, assy->ZPickFor(axisName), assy->ZSlowFor(axisName));
    }

    // Đèn trigger CAM1/CAM2 — cùng DO với nút "Bottom-up/Top-down" trang Manual; đọc lại DO thật rồi đảo.
    void TeachingPage::ToggleCamLamp(int bit)
    {
        auto io = McControl::Io();
        if (!io) return;
        ui::PostMotionCommand([io, bit] { io->SetDo(bit, !io->GetDo(bit)); });
    }

    // ───────────────────────── model ─────────────────────────

    void TeachingPage::RefreshModelList()
    {
        std::vector<std::string> names = McDataManager::ListModels();
        if (names != m_modelNames)
        {
            m_modelNames = names;
            m_suppressModelSelection = true;
            ModelList().Items().Clear();
            for (std::string const& n : m_modelNames)
            {
                ListViewItem item;
                item.Content(box_value(ui::H(n)));
                item.MinHeight(32);
                ModelList().Items().Append(item);
            }
            m_suppressModelSelection = false;
        }
        if (m_selectedModelName.empty() || !ContainsName(m_modelNames, m_selectedModelName))
            m_selectedModelName = m_modelNames.empty() ? std::string() : m_modelNames.front();
        SelectModelInList();
    }

    void TeachingPage::SelectModelInList()
    {
        auto it = std::find(m_modelNames.begin(), m_modelNames.end(), m_selectedModelName);
        int index = it == m_modelNames.end() ? -1 : (int)(it - m_modelNames.begin());
        if (ModelList().SelectedIndex() != index)
        {
            m_suppressModelSelection = true;
            ModelList().SelectedIndex(index);
            m_suppressModelSelection = false;
        }
        if (index >= 0) ModelList().ScrollIntoView(ModelList().Items().GetAt((uint32_t)index));
    }

    void TeachingPage::LoadSelectedModel()
    {
        Guarded("Load model", [&] {
            McControl::InvalidateVisionCache();   // số quét ô / tư thế trên tay đã cất không còn khớp lưới, mốc hay model
            if (::base::IsBlank(m_selectedModelName)) return;

            auto state = McControl::Status().State();
            if (state == sca::EMcState::Running || state == sca::EMcState::Held || state == sca::EMcState::Homing)
            {
                ui::Dialogs::Warn(Loc::T("Teach.LoadModelBusy"));
                return;
            }

            if (McDataManager::LoadModel(m_selectedModelName))
            {
                // TODO(port): bản WPF không cập nhật Status.ModelName sau khi nạp model ở trang này (tên model trên thanh
                // tiêu đề còn tên cũ tới lần Init sau) — giữ nguyên hành vi.
                m_model = McDataManager::Model();
                OnTraySwitched();
            }
            else
            {
                ui::Dialogs::Error(Loc::F("Teach.LoadModelFailed", m_selectedModelName));
            }
        });
    }

    void TeachingPage::NewModel()
    {
        Guarded("New model", [&] {
            McControl::InvalidateVisionCache();
            std::string name = ::base::Trim(ui::U8(NewModelNameBox().Text()));
            if (!ValidModelName(name)) return;

            if (ContainsName(m_modelNames, name))
            {
                ui::Dialogs::Warn(Loc::F("Teach.ModelExists", name));
                return;
            }

            auto model = std::make_shared<sca::ModelData>();
            model->Name = name;
            McDataManager::UseModel(model, true);
            McDataManager::SaveModel(name);

            NewModelNameBox().Text(L"");
            RefreshModelList();
            m_selectedModelName = name;
            SelectModelInList();
            m_model = McDataManager::Model();
            OnTraySwitched();
        });
    }

    void TeachingPage::CopyModel()
    {
        Guarded("Copy model", [&] {
            std::string source = m_selectedModelName;
            std::string target = ::base::Trim(ui::U8(NewModelNameBox().Text()));

            if (::base::IsBlank(source)) { ui::Dialogs::Warn(Loc::T("Teach.ChooseSourceModel")); return; }
            if (!ValidModelName(target)) return;
            if (ContainsName(m_modelNames, target)) { ui::Dialogs::Warn(Loc::F("Teach.ModelExists", target)); return; }

            if (McDataManager::CopyModel(source, target))
            {
                NewModelNameBox().Text(L"");
                RefreshModelList();
                m_selectedModelName = target;
                SelectModelInList();
            }
            else
            {
                ui::Dialogs::Error(Loc::T("Teach.CopyModelFailed"));
            }
        });
    }

    winrt::fire_and_forget TeachingPage::DeleteModel()
    {
        auto self = get_strong();
        try
        {
            std::string name = m_selectedModelName;
            if (::base::IsBlank(name)) co_return;

            if (::base::EqualsIgnoreCase(name, McDataManager::Model()->Name))
            {
                ui::Dialogs::Warn(Loc::T("Teach.DeleteOpenModel"));
                co_return;
            }

            if (!co_await ui::Dialogs::ConfirmAsync(Loc::F("Teach.ConfirmDeleteModel", name))) co_return;

            if (McDataManager::DeleteModel(name)) RefreshModelList();
            else ui::Dialogs::Error(Loc::T("Teach.DeleteModelFailed"));
        }
        catch (const std::exception& ex) { LogUiError("Delete model", ex.what()); ui::Dialogs::Error(ex.what()); }
        catch (...) { LogUiError("Delete model", "unknown error"); }
    }

    void TeachingPage::SaveModel()
    {
        Guarded("Save model", [&] {
            if (McDataManager::SaveModel())
            {
                RefreshModelList();
                m_selectedModelName = McDataManager::Model()->Name;
                SelectModelInList();
            }
            else
            {
                ui::Dialogs::Error(Loc::T("Teach.SaveModelFailed"));
            }
        });
    }
}
