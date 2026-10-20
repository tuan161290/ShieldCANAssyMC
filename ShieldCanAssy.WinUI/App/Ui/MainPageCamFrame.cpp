#include "pch.h"
#include "Ui/MainPageCamFrame.h"

#include <chrono>

#include "Communication/ImageData.h"
#include "Controller/VisionReadout.h"
#include "Lang/Loc.h"
#include "Ui/UiUtil.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Media;

namespace ui
{
    namespace
    {
        constexpr double ReadoutLabelWidth = 64;   // cột nhãn/ô của ReadoutTemplate

        TextBlock Text(std::string const& s, double size, bool semibold, Brush const& foreground, bool center)
        {
            TextBlock t;
            t.Text(H(s));
            t.FontSize(size);
            if (semibold) t.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            if (foreground) t.Foreground(foreground);
            t.VerticalAlignment(VerticalAlignment::Center);
            t.TextTrimming(TextTrimming::CharacterEllipsis);
            if (center) t.HorizontalAlignment(HorizontalAlignment::Center);
            return t;
        }

        /// <summary>Một hàng của bảng: ô nhãn rộng 64 bên trái, các ô số chia đều phần còn lại (DockPanel + UniformGrid
        /// Rows=1 của WPF).</summary>
        Grid TableRow(TextBlock const& label, std::vector<TextBlock> const& cells)
        {
            Grid g;
            ColumnDefinition first;
            first.Width(GridLengthHelper::FromPixels(ReadoutLabelWidth));
            g.ColumnDefinitions().Append(first);
            for (size_t i = 0; i < cells.size(); ++i)
            {
                ColumnDefinition c;
                c.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
                g.ColumnDefinitions().Append(c);
            }
            g.Children().Append(label);
            for (size_t i = 0; i < cells.size(); ++i)
            {
                Grid::SetColumn(cells[i], (int32_t)i + 1);
                g.Children().Append(cells[i]);
            }
            return g;
        }
    }

    FrameworkElement MainCamFrame::Build(std::wstring const& title)
    {
        Border card;
        card.Style(ResStyle(L"CardBorderStyle"));
        card.Padding(ThicknessHelper::FromUniformLength(0));

        Grid layout;
        for (auto h : { GridLengthHelper::Auto(), GridLengthHelper::Auto(), GridLengthHelper::Auto(),
                        GridLengthHelper::FromValueAndType(1, GridUnitType::Star) })
        {
            RowDefinition r;
            r.Height(h);
            layout.RowDefinitions().Append(r);
        }

        // ── Tiêu đề: tên | ghi chú lần soi (cam, cắt "…") | Shot hh:mm:ss | ☰ ──
        Border header;
        header.Style(ResStyle(L"CardHeaderStyle"));
        header.Padding(ThicknessHelper::FromLengths(12, 0, 8, 0));
        Grid hg;
        for (auto w : { GridLengthHelper::Auto(), GridLengthHelper::FromValueAndType(1, GridUnitType::Star),
                        GridLengthHelper::Auto(), GridLengthHelper::Auto() })
        {
            ColumnDefinition c;
            c.Width(w);
            hg.ColumnDefinitions().Append(c);
        }
        TextBlock titleText;
        titleText.Text(title);
        titleText.Style(ResStyle(L"SectionHeaderStyle"));
        titleText.TextTrimming(TextTrimming::CharacterEllipsis);

        m_note = TextBlock();
        m_note.FontSize(11);
        m_note.Foreground(Res(L"CautionBrush"));
        m_note.VerticalAlignment(VerticalAlignment::Center);
        m_note.TextAlignment(TextAlignment::Right);
        m_note.TextTrimming(TextTrimming::CharacterEllipsis);
        m_note.Margin(ThicknessHelper::FromLengths(8, 0, 8, 0));
        m_note.Visibility(Visibility::Collapsed);
        Grid::SetColumn(m_note, 1);

        m_shot = TextBlock();
        m_shot.FontSize(11);
        m_shot.Foreground(Res(L"TextTertiaryBrush"));
        m_shot.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(m_shot, 2);

        m_toolbar = Button();
        m_toolbar.Width(32);
        m_toolbar.Height(28);
        m_toolbar.MinHeight(0);
        m_toolbar.Padding(ThicknessHelper::FromUniformLength(0));
        m_toolbar.Margin(ThicknessHelper::FromLengths(8, 0, 0, 0));
        m_toolbar.VerticalAlignment(VerticalAlignment::Center);
        FontIcon icon;
        icon.Glyph(L"");
        icon.FontSize(12);
        icon.FontFamily(FontFamily(L"Segoe Fluent Icons, Segoe MDL2 Assets"));
        m_toolbar.Content(icon);
        m_toolbar.Visibility(Visibility::Collapsed);   // chỉ khi có control VisionMaster
        ToolTipService::SetToolTip(m_toolbar, box_value(T("Main.RenderToolbarTip")));
        m_toolbar.Click([this](auto&&, auto&&) { if (OnToolbarClicked) OnToolbarClicked(); });
        Grid::SetColumn(m_toolbar, 3);

        hg.Children().Append(titleText);
        hg.Children().Append(m_note);
        hg.Children().Append(m_shot);
        hg.Children().Append(m_toolbar);
        header.Child(hg);

        Border divider;
        divider.Style(ResStyle(L"DividerStyle"));
        Grid::SetRow(divider, 1);

        // ── Bảng số, cao cố định 70 (CamReadoutHostStyle) ──
        m_readoutHost = Grid();
        m_readoutHost.Height(70);
        m_readoutHost.Margin(ThicknessHelper::FromLengths(12, 8, 12, 4));
        m_readoutHost.Background(Solid(0x00000000));   // nhận tooltip cả trên khoảng trống
        m_readoutNote = TextBlock();
        m_readoutNote.FontSize(12);
        m_readoutNote.TextWrapping(TextWrapping::Wrap);
        m_readoutNote.TextTrimming(TextTrimming::CharacterEllipsis);
        m_table = StackPanel();
        m_table.Visibility(Visibility::Collapsed);
        m_readoutHost.Children().Append(m_readoutNote);
        m_readoutHost.Children().Append(m_table);
        Grid::SetRow(m_readoutHost, 2);

        // ── Khung ảnh bo góc: chữ (no image) dưới ảnh dự phòng, control VisionMaster (popup) phủ trên ──
        Border holder;
        holder.MinHeight(140);
        // Không viền (Tuấn 2026-10-18: "4 cạnh vẫn xuất hiện" — viền 1 px CardStroke của RenderHolderStyle WPF quanh khung
        // control VM nhìn như viền của chính control). Góc bo giữ để cắt ảnh dự phòng.
        holder.CornerRadius(CornerRadiusHelper::FromUniformRadius(6));
        holder.BorderThickness(ThicknessHelper::FromUniformLength(0));
        holder.Background(Solid(0x00000000));
        holder.Margin(ThicknessHelper::FromLengths(12, 0, 12, 12));
        Grid::SetRow(holder, 3);

        Grid holderGrid;
        m_noImage = TextBlock();
        m_noImage.Text(T("Main.NoImage"));
        m_noImage.Foreground(Res(L"TextTertiaryBrush"));
        m_noImage.HorizontalAlignment(HorizontalAlignment::Center);
        m_noImage.VerticalAlignment(VerticalAlignment::Center);
        m_image = Image();
        m_image.Stretch(Stretch::Uniform);
        m_vmText = TextBlock();
        m_vmText.FontSize(11);
        m_vmText.TextWrapping(TextWrapping::Wrap);
        m_vmText.Foreground(Res(L"CautionBrush"));
        m_vmText.VerticalAlignment(VerticalAlignment::Bottom);
        m_vmText.Margin(ThicknessHelper::FromUniformLength(6));
        m_vmText.Visibility(Visibility::Collapsed);
        m_host = Border();   // ô giữ chỗ của VmPane
        holderGrid.Children().Append(m_noImage);
        holderGrid.Children().Append(m_image);
        holderGrid.Children().Append(m_vmText);
        holderGrid.Children().Append(m_host);
        holder.Child(holderGrid);
        m_imageView = ImageView::Create(m_image, m_noImage);

        layout.Children().Append(header);
        layout.Children().Append(divider);
        layout.Children().Append(m_readoutHost);
        layout.Children().Append(holder);
        card.Child(layout);
        return card;
    }

    void MainCamFrame::SetReadout(std::shared_ptr<const sca::ReadoutCard> const& card, std::string const& fullText)
    {
        if (!m_readoutHost) return;
        if (!m_tooltipSet || fullText != m_tooltipText)
        {
            m_tooltipSet = true;
            m_tooltipText = fullText;
            ToolTipService::SetToolTip(m_readoutHost, box_value(H(fullText)));
        }
        if (card == m_card) return;
        m_card = card;
        RebuildTable();
    }

    // ReadoutTemplate: không có hàng → chữ ghi chú chiếm chỗ bảng; có hàng → tiêu đề (ô + cột) và các hàng số, ghi chú lên
    // tiêu đề card.
    void MainCamFrame::RebuildTable()
    {
        const sca::ReadoutCard* c = m_card.get();
        const bool hasRows = c && c->HasRows();
        const std::string note = c ? c->Note : std::string();

        SetText(m_readoutNote, hasRows ? std::string() : note);
        Show(m_readoutNote, !hasRows);
        Show(m_note, hasRows && !note.empty());
        SetText(m_note, hasRows ? note : std::string());
        ToolTipService::SetToolTip(m_note, hasRows && !note.empty() ? box_value(H(note)) : nullptr);

        m_table.Children().Clear();
        Show(m_table, hasRows);
        if (!hasRows) return;

        // Hàng tiêu đề — chạm được khi đổi kiểu được (CanToggle): nền trong suốt để chạm trúng cả khoảng trống.
        std::vector<TextBlock> heads;
        for (const auto& col : c->Columns) heads.push_back(Text(col, 11, false, Res(L"TextSecondaryBrush"), true));
        Grid head = TableRow(Text(c->Cell, 11, true, nullptr, false), heads);
        head.Background(Solid(0x00000000));
        if (c->CanToggle())
        {
            ToolTipService::SetToolTip(head, box_value(T("Main.ReadoutToggleTip")));
            head.Tapped([this](auto&&, auto&&) {
                if (m_card && m_card->CanToggle() && OnReadoutHeaderTapped) OnReadoutHeaderTapped();
            });
        }
        m_table.Children().Append(head);

        for (const auto& row : c->Rows)
        {
            std::vector<TextBlock> cells;
            for (const auto& v : row.Values) cells.push_back(Text(v, 13, true, nullptr, true));
            m_table.Children().Append(TableRow(Text(row.Label, 11, false, Res(L"TextSecondaryBrush"), false), cells));
        }
    }

    void MainCamFrame::SetImage(std::shared_ptr<const sca::ImageData> const& image)
    {
        if (!m_imageView) return;
        if (m_imageView->Set(image))
        {
            if (image) m_shotAt = std::chrono::system_clock::now();
            else m_shotAt.reset();
        }
        SetText(m_shot, m_shotAt ? sca::Loc::F("Main.CapturedAt", sca::FormatLocal(*m_shotAt, "HH:mm:ss")) : std::string());
    }

    void MainCamFrame::ShowVmError(std::string const& text)
    {
        SetText(m_vmText, text);
        Show(m_vmText, !text.empty());
    }
}
