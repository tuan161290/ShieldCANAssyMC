#include "pch.h"
#include "ManualPage.xaml.h"
#if __has_include("ManualPage.g.cpp")
#include "ManualPage.g.cpp"
#endif

#include "Base/Async.h"
#include "Base/Text.h"
#include "Communication/FastechAxis.h"
#include "Communication/Sim/FakeAxis.h"
#include "Communication/Sim/FakeIo.h"
#include "Controller/McControl.h"
#include "Infor/AxisId.h"
#include "Infor/IoMap.h"
#include "Lang/Loc.h"
#include "Ui/Dialogs.h"
#include "Ui/JogControl.h"
#include "Ui/UiUtil.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
namespace WF = winrt::Windows::Foundation;

namespace
{
    template <size_t N>
    bool InGroup(const std::array<const char*, N>& group, const std::string& name)
    {
        for (const char* member : group)
            if (name == member) return true;
        return false;
    }

    /// <summary>Io mô phỏng — nullptr trên máy thật (khi đó Bảng mô phỏng bị ẩn và mọi nút Sim* không làm gì).</summary>
    std::shared_ptr<sca::FakeIo> SimIo()
    {
        return std::dynamic_pointer_cast<sca::FakeIo>(sca::McControl::Io());
    }

    /// <summary>Bit 31 bên trái, bit 0 bên phải — cùng chiều với cách đọc sơ đồ điện; cách một khoảng mỗi 8 bit.</summary>
    std::string Bits(uint32_t v)
    {
        std::string s;
        s.reserve(39);
        for (int i = 31; i >= 0; --i)
        {
            s.push_back((v & (1u << i)) != 0 ? '1' : '0');
            if (i % 8 == 0 && i > 0) s.push_back(' ');
        }
        return s;
    }

    /// <summary>Đặt Text chỉ khi chuỗi UTF-8 đổi so với lần trước — khỏi đổi sang hstring và đọc lại Text() mỗi nhịp.</summary>
    void SetIfChanged(TextBlock const& block, std::string& shown, std::string value)
    {
        if (value == shown) return;
        shown = std::move(value);
        block.Text(ui::H(shown));
    }

    void Tip(FrameworkElement const& element, char const* key)
    {
        ToolTipService::SetToolTip(element, box_value(ui::T(key)));
    }
}

namespace winrt::ShieldCanAssy::implementation
{
    void ManualPage::OnRootLoaded(WF::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_loaded) return;
        m_loaded = true;
        BuildStatic();
        if (m_coreReady) OnTick();
    }

    // ══════════ Dựng ══════════

    // Phần không cần Core: chữ lấy từ resx, tooltip, hai danh sách đèn I/O (tên bit qua Loc — tĩnh).
    void ManualPage::BuildStatic()
    {
        static_assert(sca::IoMap::DiCount <= 32 && sca::IoMap::DoCount <= 32, "raw text packs bits into uint32");
        if (m_lampBrushes.On) return;   // đã dựng
        m_lampBrushes = ui::DefaultLampBrushes();

        Tip(StopAllButton(), "Manual.StopAllTip");
        Tip(ResetAllButton(), "Manual.ResetTip");
        Tip(ServoOnAllButton(), "Manual.ServoOnAllTip");
        Tip(BrakeAllButton(), "Manual.BrakeReleaseAllTip");
        Tip(OriginAllButton(), "Manual.OriginTip");
        Tip(CamBtmLampButton(), "Manual.CamBtmLampTip");
        Tip(CamTopLampButton(), "Manual.CamTopLampTip");
        Tip(SimStartButton(), "Manual.SimStartTip");
        Tip(SimCurtainButton(), "Manual.SimLightCurtainTip");

        ZSafeText().Text(ui::T("Manual.ZSafe"));
        ColAxisText().Text(ui::T("Manual.ColAxis"));
        ColPosText().Text(ui::T("Manual.ColPosition"));
        ColLimitText().Text(ui::T("Manual.ColLimit"));
        ColHomeText().Text(ui::T("Manual.ColHome"));
        ColJogText().Text(ui::T("Manual.ColJog"));
        ColInchText().Text(ui::T("Manual.ColInching"));
        ColMoveText().Text(ui::T("Manual.ColMoveAbs"));
        ColStopText().Text(ui::T("Manual.ColStop"));
        RotateSectionText().Text(ui::T("Manual.SectionRotate"));
        SimHeaderText().Text(ui::T("Manual.SimPanel"));
        SimNoteText().Text(ui::T("Manual.SimPanelNote"));
        SimFaultsText().Text(ui::T("Manual.SimDriverFaults"));
        DiagHeaderText().Text(ui::T("Manual.Diagnostics"));
        DiagNoteText().Text(ui::T("Manual.DiagnosticsNote"));
        InputsHeaderText().Text(ui::T("Manual.Inputs"));
        OutputsHeaderText().Text(ui::T("Manual.Outputs"));

        // Ezi-IO đánh số kênh theo hex (0x00–0x1F) trong tool cấu hình và tài liệu của hãng — "DI 0x00  Tên" đối chiếu
        // thẳng, không phải quy đổi. Đèn là bit THÔ: nhiều bit NC (EmgOk…) ON là bình thường. Chỉ đọc (như bản WPF).
        auto diNames = std::make_shared<std::vector<std::string>>(sca::IoMap::DiNames());
        auto doNames = std::make_shared<std::vector<std::string>>(sca::IoMap::DoNames());
        DiScroll().Content(m_diList.Build(true, sca::IoMap::DiCount, [diNames](int bit) {
            return bit < (int)diNames->size() ? (*diNames)[bit] : std::string();
        }));
        DoScroll().Content(m_doList.Build(false, sca::IoMap::DoCount, [doNames](int bit) {
            return bit < (int)doNames->size() ? (*doNames)[bit] : std::string();
        }));

        ZSafeLamp().Fill(ui::StatusBrush(false));
    }

    void ManualPage::OnCoreReady()
    {
        m_coreReady = true;
        BuildStatic();
        BuildAxisRows();
        BuildSimAxisGrid();
    }

    // Ba nhóm theo AxisId (Gantry X, Z1, Z2 | Tray Y1, Y2 | xoay A1, A2), giữ thứ tự của AxisList (AxisId::All) trong
    // mỗi nhóm — như ManualViewModel.Gantry/Tray/Rotate.
    void ManualPage::BuildAxisRows()
    {
        if (m_rowsBuilt) return;
        m_rowsBuilt = true;
        for (const auto& axis : sca::McControl::Motion().AxisList())
        {
            if (!axis) continue;
            if (auto fa = std::dynamic_pointer_cast<sca::FastechAxis>(axis)) m_fastechAxes.push_back(fa);

            const std::string& name = axis->Name();
            StackPanel host{ nullptr };
            if (name == sca::AxisId::X || InGroup(sca::AxisId::ZAxes, name)) host = GantryRows();
            else if (name == sca::AxisId::Y1 || name == sca::AxisId::Y2) host = TrayRows();
            else if (InGroup(sca::AxisId::RotateAxes, name)) host = RotateRows();
            if (!host) continue;

            auto row = ui::manualpage::AxisRow::Create(axis, m_lampBrushes);
            host.Children().Append(row->Root());
            m_rows.push_back(std::move(row));
        }
    }

    // Lỗi driver giả lập: mỗi trục mô phỏng một ô "tên | ALM | Offline" (bấm lại để gỡ). Không tô màu (như WPF).
    void ManualPage::BuildSimAxisGrid()
    {
        if (SimAxisGrid().Children().Size() > 0) return;
        for (const auto& axis : sca::McControl::Motion().AxisList())
        {
            auto sim = std::dynamic_pointer_cast<sca::FakeAxis>(axis);
            if (!sim) continue;

            StackPanel cell;
            cell.Orientation(Orientation::Horizontal);
            cell.VerticalAlignment(VerticalAlignment::Top);

            TextBlock name;
            name.Text(ui::H(sim->Name()));
            name.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
            name.Width(28);
            name.VerticalAlignment(VerticalAlignment::Center);
            cell.Children().Append(name);

            Button alarm = ui::ManualCompactButton(L"ALM", 56, ui::T("Manual.SimAlarmTip").c_str());
            alarm.Click([sim](auto&&, auto&&) { sim->SetSimDriverAlarm(!sim->SimDriverAlarm()); });
            cell.Children().Append(alarm);

            Button cut = ui::ManualCompactButton(L"Offline", 64, ui::T("Manual.SimDisconnectTip").c_str());
            cut.Margin(ThicknessHelper::FromLengths(3, 0, 0, 0));
            cut.Click([sim](auto&&, auto&&) { sim->SetSimDisconnected(!sim->SimDisconnected()); });
            cell.Children().Append(cut);

            SimAxisGrid().Children().Append(cell);
        }
    }

    // ══════════ Cập nhật (10 Hz, chỉ lúc trang đang hiện) ══════════

    void ManualPage::OnTick()
    {
        if (!m_coreReady) return;
        BuildStatic();

        for (const auto& row : m_rows) row->Update();

        auto io = sca::McControl::Io();
        UpdateCommands(io);
        UpdateBanners();
        UpdateIo(io);
    }

    void ManualPage::ShowShotVariant(int index)
    {
        if (index >= 1) LeftScroll().ChangeView(nullptr, LeftScroll().ScrollableHeight(), nullptr, true);
    }

    void ManualPage::UpdateCommands(const std::shared_ptr<sca::IIoDevice>& io)
    {
        auto& motion = sca::McControl::Motion();

        // Nút "tất cả" kiêm đèn báo (Tuấn 2026-10-13): xanh khi MỌI trục liên quan đã ở trạng thái nút đó bật.
        int allServo = motion.AllServoOn() ? 1 : 0;
        if (allServo != m_shownAllServo)
        {
            m_shownAllServo = allServo;
            ui::SetTint(ServoOnAllButton(), allServo ? ui::Tint::Green : ui::Tint::None);
        }

        // Đèn nút Brake Off — đọc THẲNG DO thật, không suy luận lại từ ServoOn/Alarm ở tầng UI, để khớp chính xác với
        // những gì McFunction::UpdateBrakeOutputs đang ghi.
        bool z1 = io && io->GetDo(sca::IoMap::DoBrakeReleaseZ1);
        bool z2 = io && io->GetDo(sca::IoMap::DoBrakeReleaseZ2);
        for (const auto& row : m_rows)
        {
            if (row->Name() == sca::AxisId::Z1) row->SetBrakeReleased(z1);
            else if (row->Name() == sca::AxisId::Z2) row->SetBrakeReleased(z2);
        }
        int allBrake = (z1 && z2) ? 1 : 0;
        if (allBrake != m_shownAllBrake)
        {
            m_shownAllBrake = allBrake;
            ui::SetTint(BrakeAllButton(), allBrake ? ui::Tint::Green : ui::Tint::None);
        }

        // Origin trang Manual tô XANH khi Status.OrgOk (trang Main tô xanh dương — như WPF).
        int org = sca::McControl::Status().OrgOk() ? 1 : 0;
        if (org != m_shownOrgOk)
        {
            m_shownOrgOk = org;
            ui::SetTint(OriginAllButton(), org ? ui::Tint::Green : ui::Tint::None);
        }

        int zSafe = motion.ZSafeOk() ? 1 : 0;
        if (zSafe != m_shownZSafe)
        {
            m_shownZSafe = zSafe;
            ZSafeLamp().Fill(ui::StatusBrush(zSafe != 0));
        }
    }

    void ManualPage::UpdateBanners()
    {
        auto safety = sca::McControl::Safety();
        std::string block = (safety && !safety->SafeToMove()) ? safety->BlockReason() : std::string();
        std::string message = sca::McControl::Status().Message();
        if (block == m_shownBlockReason && message == m_shownMessage) return;
        m_shownBlockReason = block;
        m_shownMessage = message;

        // Ô cao cố định 2 dòng (NoticeSlot): có lý do chặn thì nó chiếm dòng đầu, câu Status còn một dòng. Ẩn/hiện dòng
        // BÊN TRONG ô không đổi chiều cao ô nên không xô gì bên ngoài.
        bool hasBlock = !::base::IsBlank(block);
        bool hasMessage = !::base::IsBlank(message);
        BlockReasonRun().Text(ui::H(block));
        ui::Show(BlockText(), hasBlock);
        MessageText().MaxLines(hasBlock ? 1 : 2);
        ui::SetText(MessageText(), message);

        std::string tip = hasBlock ? "Motion blocked: " + block + (hasMessage ? "\n" + message : std::string()) : message;
        ToolTipService::SetToolTip(NoticeSlot(), tip.empty() ? nullptr : box_value(ui::H(tip)));
        ui::ShowInPlace(NoticeSlot(), hasBlock || hasMessage);
    }

    void ManualPage::UpdateIo(const std::shared_ptr<sca::IIoDevice>& io)
    {
        if (io.get() != m_shownIo)
        {
            m_shownIo = io.get();
            ui::Show(SimCard(), std::dynamic_pointer_cast<sca::FakeIo>(io) != nullptr);
        }
        if (!io) return;

        uint32_t di = 0, doo = 0;
        for (int bit = 0; bit < sca::IoMap::DiCount; ++bit)
            if (io->GetDi(bit)) di |= 1u << bit;
        for (int bit = 0; bit < sca::IoMap::DoCount; ++bit)
            if (io->GetDo(bit)) doo |= 1u << bit;

        m_diList.Update([di](int bit) { return (di & (1u << bit)) != 0; });
        m_doList.Update([doo](int bit) { return (doo & (1u << bit)) != 0; });
        UpdateDiagnostics(di, doo);
    }

    void ManualPage::UpdateDiagnostics(uint32_t di, uint32_t doo)
    {
        SetIfChanged(RawDiText(), m_shownRawDi, std::format("DI  0x{:08X}   {}", di, Bits(di)));
        SetIfChanged(RawDoText(), m_shownRawDo, std::format("DO  0x{:08X}   {}", doo, Bits(doo)));

        // Trạng thái thô của các trục Fastech, giải mã thành tên bit — một dòng mỗi trục.
        while (m_fastechLines.size() < m_fastechAxes.size())
        {
            TextBlock line;
            line.FontFamily(Media::FontFamily(L"Consolas"));
            line.FontSize(12);
            line.TextWrapping(TextWrapping::Wrap);
            line.IsTextSelectionEnabled(true);
            line.Margin(ThicknessHelper::FromLengths(0, 0, 0, 2));
            FastechStatusPanel().Children().Append(line);
            m_fastechLines.push_back(line);
            m_shownFastechLines.emplace_back("\x01");
        }
        for (size_t i = 0; i < m_fastechAxes.size(); ++i)
        {
            const auto& fa = m_fastechAxes[i];
            SetIfChanged(m_fastechLines[i], m_shownFastechLines[i],
                         std::format("{}  0x{:08X}  {}", fa->Name(), fa->RawStatus(), fa->RawStatusText()));
        }
    }

    // ══════════ Lệnh chung cho tất cả trục ══════════

    // STOP giống hệt nút STOP trang Main (Tuấn 2026-10-07): McControl::RequestStop xử lý theo trạng thái máy — dừng trục
    // VÀ huỷ chu trình đang chạy (về gốc, Init, chạy rà…), không chỉ Motion.StopAll.
    void ManualPage::OnStopAllClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        sca::McControl::RequestStop();
    }

    // RESET ngay tại trang Manual — cùng McControl::RequestReset với nút RESET trang Main (Tuấn 2026-09-09).
    void ManualPage::OnResetClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        sca::McControl::RequestReset();
    }

    // Cùng McControl::RequestOrigin với nút trang Main nên cùng điều kiện chặn — lý do từ chối hiện ở MessageBar.
    void ManualPage::OnOriginClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        sca::McControl::RequestOrigin();
    }

    // Servo On tất cả — bật servo TỪNG trục chưa ServoOn, bỏ qua (không chặn cả nút) trục đang Alarm, đúng điều kiện của
    // nút SVO riêng lẻ — phải RST trục đó trước. Chạy song song để không phải đợi tuần tự qua 7 trục (Tuấn 2026-09-08).
    void ManualPage::OnServoOnAllClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_servoAllBusy->exchange(true)) return;

        std::vector<std::string> alarmed;
        std::vector<std::shared_ptr<sca::IAxis>> targets;
        for (const auto& axis : sca::McControl::Motion().AxisList())
        {
            if (!axis || axis->ServoOn()) continue;
            if (axis->Alarm()) alarmed.push_back(axis->Name());
            else targets.push_back(axis);
        }

        ui::RunBackgroundThenUi(
            [targets, alarmed] {
                try
                {
                    std::vector<std::function<bool()>> jobs;
                    for (const auto& axis : targets) jobs.push_back([axis] { return axis->SetServoOn(true); });
                    if (!jobs.empty()) ::base::WhenAll(std::move(jobs));
                }
                catch (const ::base::OperationCanceled&) {}
                catch (const std::exception& ex)
                {
                    ui::Dialogs::Error(std::string("Servo On (all): ") + ex.what());
                }
                if (!alarmed.empty())
                    sca::McControl::Status().SetMessage(sca::Loc::F("Manual.ServoOnSkipped", ::base::Join(alarmed, ", ")));
            },
            [busy = m_servoAllBusy] { busy->store(false); });
    }

    // Brake Off tất cả — chỉ Z1/Z2 có phanh. Như một cú bấm NHANH bình thường (không phải nhả khẩn cấp kiểu giữ 3 s): vẫn
    // đòi ServoOn + không Alarm mới thực sự nhả (McFunction::UpdateBrakeOutputs).
    void ManualPage::OnBrakeAllClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        if (auto func = sca::McControl::Func())
        {
            func->RequestBrakeRelease(sca::AxisId::Z1);
            func->RequestBrakeRelease(sca::AxisId::Z2);
        }
    }

    // Đèn trigger CAM Bottom-up (DoLampCam1, 0x0B) / Top-down (DoLampCam2, 0x0C) — Routine_Auto tự bật/tắt quanh lúc
    // trigger, nhưng lúc thao tác tay (canh nét, chỉnh camera) cần bật/tắt độc lập. Đọc-đảo-ghi trong hàng đợi lệnh tay
    // (Io.SetDo, UI-SPEC §0.1): bấm hai lần liên tiếp đảo đúng hai lần.
    void ManualPage::OnCamBtmLampClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        ui::PostMotionCommand([] {
            if (auto io = sca::McControl::Io()) io->SetDo(sca::IoMap::DoLampCam1, !io->GetDo(sca::IoMap::DoLampCam1));
        });
    }

    void ManualPage::OnCamTopLampClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        ui::PostMotionCommand([] {
            if (auto io = sca::McControl::Io()) io->SetDo(sca::IoMap::DoLampCam2, !io->GetDo(sca::IoMap::DoLampCam2));
        });
    }

    // ══════════ Bảng mô phỏng — nút cứng và tín hiệu an toàn (FakeIo) ══════════

    // Bốn nút Start như nhau (2026-10-16) — một nút mô phỏng, bấm nút cụm B #1.
    void ManualPage::OnSimStartClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        if (auto io = SimIo()) io->SimPress(sca::IoMap::BtnGoB1);
    }

    void ManualPage::OnSimStopClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        if (auto io = SimIo()) io->SimPress(sca::IoMap::BtnStop);
    }

    void ManualPage::OnSimEmgClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        if (auto io = SimIo()) io->SimToggle(sca::IoMap::EmgOk);
    }

    void ManualPage::OnSimCurtainClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        if (auto io = SimIo()) io->SimToggle(sca::IoMap::LightCurtainOk);
    }

    void ManualPage::OnSimAirClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        if (auto io = SimIo()) io->SimToggle(sca::IoMap::AirOk);
    }

    // Hai cảm biến tray đầu vào — đặt/lấy tray giả lập. Mặc định chỉ Tray 1 có (FakeIo).
    void ManualPage::OnSimTray1Click(WF::IInspectable const&, RoutedEventArgs const&)
    {
        if (auto io = SimIo()) io->SimToggle(sca::IoMap::TrayIn1Present);
    }

    void ManualPage::OnSimTray2Click(WF::IInspectable const&, RoutedEventArgs const&)
    {
        if (auto io = SimIo()) io->SimToggle(sca::IoMap::TrayIn2Present);
    }
}
