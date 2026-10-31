#include "pch.h"
#include "Ui/TrayCard.h"

#include <algorithm>

#include "Data/RunningData.h"
#include "Lang/Loc.h"
#include "Ui/UiUtil.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Input;
using namespace winrt::Microsoft::UI::Xaml::Media;

namespace ui
{
    wchar_t const* TrayCellBrushKey(TrayCellLook look)
    {
        switch (look)
        {
        case TrayCellLook::Scanned:    return L"TrayCellScannedBrush";
        case TrayCellLook::Loaded:     return L"TrayCellLoadedBrush";
        case TrayCellLook::Processing: return L"TrayCellProcessingBrush";
        case TrayCellLook::Ok:         return L"TrayCellOkBrush";
        case TrayCellLook::Ng:         return L"TrayCellNgBrush";
        case TrayCellLook::Skipped:    return L"TrayCellSkippedBrush";
        default:                       return L"TrayCellEmptyBrush";
        }
    }

    namespace
    {
        TrayCellLook LookOf(sca::ECellState state, bool scanned)
        {
            switch (state)
            {
            case sca::ECellState::Loaded:     return TrayCellLook::Loaded;
            case sca::ECellState::Processing: return TrayCellLook::Processing;
            case sca::ECellState::Ok:         return TrayCellLook::Ok;
            case sca::ECellState::Ng:         return TrayCellLook::Ng;
            case sca::ECellState::Skipped:    return TrayCellLook::Skipped;
            default:                          return scanned ? TrayCellLook::Scanned : TrayCellLook::Empty;
            }
        }

        TextBlock Caption(wchar_t const* text)
        {
            TextBlock t;
            t.Text(text);
            t.FontSize(11);
            t.Foreground(Res(L"TextSecondaryBrush"));
            t.HorizontalAlignment(HorizontalAlignment::Center);
            return t;
        }

        TextBlock Counter(Brush const& foreground)
        {
            TextBlock t;
            t.Text(L"0");
            t.FontSize(16);
            t.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
            t.HorizontalAlignment(HorizontalAlignment::Center);
            if (foreground) t.Foreground(foreground);
            return t;
        }
    }

    FrameworkElement TrayCard::Build(std::wstring const& title, int slots)
    {
        m_slots = std::max(1, slots);
        m_groups.clear();
        m_builtRows = m_builtCols = m_builtOrder = -1;

        Border card;
        card.Style(ResStyle(L"CardBorderStyle"));
        card.Padding(ThicknessHelper::FromUniformLength(0));

        Grid layout;
        for (auto h : { GridLengthHelper::Auto(), GridLengthHelper::Auto(), GridLengthHelper::FromValueAndType(1, GridUnitType::Star),
                        GridLengthHelper::Auto(), GridLengthHelper::Auto() })
        {
            RowDefinition r;
            r.Height(h);
            layout.RowDefinitions().Append(r);
        }

        // ── Tiêu đề: tên | Clear (n) | tóm tắt. Nút Clear (n) cao 28 với lề âm — KHÔNG được đổi chiều cao dòng tiêu đề. ──
        Border header;
        header.Style(ResStyle(L"CardHeaderStyle"));
        Grid headerGrid;
        for (auto w : { GridLengthHelper::Auto(), GridLengthHelper::Auto(), GridLengthHelper::FromValueAndType(1, GridUnitType::Star) })
        {
            ColumnDefinition c;
            c.Width(w);
            headerGrid.ColumnDefinitions().Append(c);
        }
        TextBlock titleText;
        titleText.Text(title);
        titleText.Style(ResStyle(L"SectionHeaderStyle"));
        m_clearSel = Button();
        m_clearSel.Height(28);
        m_clearSel.MinHeight(28);
        m_clearSel.FontSize(12);
        m_clearSel.Padding(ThicknessHelper::FromLengths(8, 0, 8, 0));
        m_clearSel.Margin(ThicknessHelper::FromLengths(10, -6, 0, -6));
        m_clearSel.VerticalAlignment(VerticalAlignment::Center);
        m_clearSel.Visibility(Visibility::Collapsed);
        ToolTipService::SetToolTip(m_clearSel, box_value(T("Tray.ClearSelectionTip")));
        m_clearSel.Click([this](auto&&, auto&&) { if (OnClearSelection) OnClearSelection(); });
        m_summary = TextBlock();
        m_summary.Style(ResStyle(L"CaptionStyle"));
        m_summary.HorizontalAlignment(HorizontalAlignment::Right);
        m_summary.VerticalAlignment(VerticalAlignment::Center);
        m_summary.TextTrimming(TextTrimming::CharacterEllipsis);
        m_summary.Margin(ThicknessHelper::FromLengths(12, 0, 0, 0));
        Grid::SetColumn(m_clearSel, 1);
        Grid::SetColumn(m_summary, 2);
        headerGrid.Children().Append(titleText);
        headerGrid.Children().Append(m_clearSel);
        headerGrid.Children().Append(m_summary);
        header.Child(headerGrid);

        Border divider1;
        divider1.Style(ResStyle(L"DividerStyle"));
        Grid::SetRow(divider1, 1);

        // ── Thân: một khối mỗi chỗ đặt tray, chia đều chiều cao. ──
        m_body = Grid();
        m_body.Margin(ThicknessHelper::FromUniformLength(12));
        m_body.RowSpacing(8);
        Grid::SetRow(m_body, 2);
        // Chạm / kéo chọn ô bắt trên cả thân lưới (xem đầu TrayCard.h). ManipulationMode khác System: kéo bằng ngón tay
        // bắt đầu trên lưới không bị TrayScroll giành để cuộn giữa chừng.
        m_body.ManipulationMode(ManipulationModes::TranslateX | ManipulationModes::TranslateY);
        m_body.PointerPressed([this](auto&&, PointerRoutedEventArgs const& e) { OnPressed(e); });
        m_body.PointerMoved([this](auto&&, PointerRoutedEventArgs const& e) { OnMoved(e); });
        m_body.PointerReleased([this](auto&&, PointerRoutedEventArgs const& e) { OnReleased(e); });
        m_body.PointerCanceled([this](auto&&, auto&&) { CancelDrag(); });
        m_body.PointerCaptureLost([this](auto&&, auto&&) { CancelDrag(); });

        Border divider2;
        divider2.Style(ResStyle(L"DividerStyle"));
        Grid::SetRow(divider2, 3);

        // ── Chân: Clear Tray | OK NG Total ──
        Grid footer;
        footer.Padding(ThicknessHelper::FromLengths(12, 8, 12, 8));
        for (int i = 0; i < 2; ++i)
        {
            ColumnDefinition c;
            c.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            footer.ColumnDefinitions().Append(c);
        }
        m_reload = Button();
        m_reload.Content(box_value(L"Clear Tray"));
        m_reload.MinHeight(36);
        m_reload.Height(36);
        m_reload.HorizontalAlignment(HorizontalAlignment::Stretch);
        m_reload.VerticalAlignment(VerticalAlignment::Center);
        m_reload.Margin(ThicknessHelper::FromLengths(0, 0, 12, 0));
        m_reload.Click([this](auto&&, auto&&) { if (OnReload) OnReload(); });

        Border countsHost;
        countsHost.BorderBrush(Res(L"DividerBrush"));
        countsHost.BorderThickness(ThicknessHelper::FromLengths(1, 0, 0, 0));
        countsHost.Padding(ThicknessHelper::FromLengths(12, 0, 0, 0));
        Grid counts;
        for (int i = 0; i < 3; ++i)
        {
            ColumnDefinition c;
            c.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            counts.ColumnDefinitions().Append(c);
        }
        m_ok = Counter(Res(L"SuccessBrush"));
        m_ng = Counter(Res(L"CriticalBrush"));
        m_total = Counter(nullptr);
        std::pair<wchar_t const*, TextBlock> items[] = { { L"OK", m_ok }, { L"NG", m_ng }, { L"Total", m_total } };
        for (int i = 0; i < 3; ++i)
        {
            StackPanel s;
            s.Children().Append(Caption(items[i].first));
            s.Children().Append(items[i].second);
            Grid::SetColumn(s, i);
            counts.Children().Append(s);
        }
        countsHost.Child(counts);
        Grid::SetColumn(countsHost, 1);
        footer.Children().Append(m_reload);
        footer.Children().Append(countsHost);
        Grid::SetRow(footer, 4);

        layout.Children().Append(header);
        layout.Children().Append(divider1);
        layout.Children().Append(m_body);
        layout.Children().Append(divider2);
        layout.Children().Append(footer);
        card.Child(layout);
        return card;
    }

    void TrayCard::Rebuild(const sca::TrayState& tray, int rowsPerTray)
    {
        CancelDrag();   // Anchor/End trỏ vào ô sắp bị xoá
        m_builtRows = rowsPerTray;
        m_builtCols = tray.Cols;
        m_builtOrder = (int)tray.ScanOrder;
        m_groups.clear();
        m_body.Children().Clear();
        m_body.RowDefinitions().Clear();

        const int cols = std::max(1, tray.Cols);
        const int rows = std::max(0, rowsPerTray);
        auto star = GridLengthHelper::FromValueAndType(1, GridUnitType::Star);

        for (int t = 0; t < m_slots; ++t)
        {
            RowDefinition def;
            def.Height(star);
            m_body.RowDefinitions().Append(def);

            Group g;
            Grid block;
            RowDefinition capRow; capRow.Height(GridLengthHelper::Auto());
            RowDefinition gridRow; gridRow.Height(star);
            block.RowDefinitions().Append(capRow);
            block.RowDefinitions().Append(gridRow);
            if (m_slots > 1)
            {
                g.Caption = TextBlock();
                g.Caption.FontSize(11);
                g.Caption.Margin(ThicknessHelper::FromLengths(0, 0, 0, 4));
                block.Children().Append(g.Caption);
            }

            g.Cells = Grid();
            g.Cells.RowSpacing(2);
            g.Cells.ColumnSpacing(2);
            for (int c = 0; c < cols; ++c)
            {
                ColumnDefinition cd;
                cd.Width(star);
                g.Cells.ColumnDefinitions().Append(cd);
            }
            for (int r = 0; r < rows; ++r)
            {
                RowDefinition rd;
                rd.Height(star);
                g.Cells.RowDefinitions().Append(rd);
            }
            const int r0 = t * rows;
            for (int r = 0; r < rows; ++r)
            {
                for (int c = 0; c < cols; ++c)
                {
                    Cell cell;
                    cell.Index = tray.IndexOf(r0 + r, c);
                    cell.Row = r0 + r;
                    cell.Col = c;
                    cell.Root = Grid();
                    cell.Root.MinWidth(30);
                    cell.Root.MinHeight(34);
                    cell.Glow = Border();
                    cell.Glow.CornerRadius(CornerRadiusHelper::FromUniformRadius(4));
                    cell.Fill = Border();
                    cell.Fill.CornerRadius(CornerRadiusHelper::FromUniformRadius(4));
                    cell.Fill.BorderThickness(ThicknessHelper::FromUniformLength(1));
                    cell.Fill.BorderBrush(Res(L"TrayCellStrokeBrush"));
                    cell.Fill.Background(Res(L"TrayCellEmptyBrush"));
                    TextBlock label;
                    label.Text(H(std::format("{}-{}", r + 1, c + 1)));
                    label.FontSize(12);
                    label.HorizontalAlignment(HorizontalAlignment::Center);
                    label.VerticalAlignment(VerticalAlignment::Center);
                    label.Foreground(Res(L"TextPrimaryBrush"));
                    cell.Fill.Child(label);
                    cell.Selection = Border();
                    cell.Selection.CornerRadius(CornerRadiusHelper::FromUniformRadius(4));
                    cell.Selection.BorderThickness(ThicknessHelper::FromUniformLength(2));
                    cell.Selection.BorderBrush(Res(L"AccentPressedBrush"));
                    cell.Selection.IsHitTestVisible(false);
                    cell.Selection.Visibility(Visibility::Collapsed);
                    cell.Root.Children().Append(cell.Glow);
                    cell.Root.Children().Append(cell.Fill);
                    cell.Root.Children().Append(cell.Selection);
                    Grid::SetRow(cell.Root, r);
                    Grid::SetColumn(cell.Root, c);
                    g.Cells.Children().Append(cell.Root);
                    g.Items.push_back(std::move(cell));
                }
            }
            Grid::SetRow(g.Cells, 1);
            block.Children().Append(g.Cells);
            Grid::SetRow(block, t);
            m_body.Children().Append(block);
            m_groups.push_back(std::move(g));
        }
    }

    void TrayCard::Sync(const sca::TrayState& tray, const std::vector<int>* scanned, bool canSelect, bool showCurrent)
    {
        if (!m_body) return;
        m_canSelect = canSelect;
        if (!canSelect) CancelDrag();
        m_cellCount = (int)tray.Cells.size();

        const int trays = std::max(1, tray.Trays);      // tray có trong sổ — Tray 2 chỉ khi đã teach
        const int rows = tray.RowsPerTray();
        const int perTray = tray.CellsPerTray();
        const int active = tray.CurrentIndex >= 0 && perTray > 0 ? std::min(tray.CurrentIndex / perTray, trays - 1) : -1;

        if (m_builtRows != rows || m_builtCols != tray.Cols || m_builtOrder != (int)tray.ScanOrder ||
            (int)m_groups.size() != m_slots)
            Rebuild(tray, rows);

        m_selected = tray.Selected();
        std::sort(m_selected.begin(), m_selected.end());
        const bool anySelected = !m_selected.empty();

        for (int t = 0; t < (int)m_groups.size(); ++t)
        {
            Group& g = m_groups[(size_t)t];
            for (Cell& cell : g.Items)
            {
                if (cell.Index < 0 || cell.Index >= (int)tray.Cells.size()) continue;   // khối của tray chưa có trong sổ
                bool isScanned = scanned && std::find(scanned->begin(), scanned->end(), cell.Index) != scanned->end();
                int look = (int)LookOf(tray.Cells[(size_t)cell.Index], isScanned);
                if (look != cell.ShownLook)
                {
                    cell.ShownLook = look;
                    cell.Fill.Background(Res(TrayCellBrushKey((TrayCellLook)look)));
                }
                // Quầng ô hiện tại chỉ khi chu trình đang dở — máy đứng yên thì CurrentIndex (vd. 0 sau Clear Tray) chỉ là ô
                // sẽ làm tiếp, quầng xanh nằm mãi ở ô 1-1 (Tuấn 2026-10-30).
                int current = showCurrent && cell.Index == tray.CurrentIndex ? 1 : 0;
                if (current != cell.ShownCurrent)
                {
                    cell.ShownCurrent = current;
                    SetGlow(cell.Glow, current ? 0xFF0078D4 : 0, 9.0f, 0.9f);
                }
            }

            int taught = t < trays ? 1 : 0;
            int isActive = t == active ? 1 : 0;
            if (taught != g.ShownTaught)
            {
                g.ShownTaught = taught;
                g.Cells.Opacity(taught ? 1.0 : 0.45);
            }
            if (g.Caption)
            {
                SetText(g.Caption, taught ? sca::Loc::F("Tray.N", t + 1)
                                          : sca::Loc::F("Tray.SummaryOfTray", t + 1, sca::Loc::T("Tray.NotTaught")));
                if (isActive != g.ShownActive)
                {
                    g.ShownActive = isActive;
                    g.Caption.FontWeight(isActive ? winrt::Windows::UI::Text::FontWeights::SemiBold()
                                                  : winrt::Windows::UI::Text::FontWeights::Normal());
                    g.Caption.FontSize(isActive ? 12 : 11);
                    g.Caption.Foreground(Res(isActive ? L"TextPrimaryBrush" : L"TextSecondaryBrush"));
                }
            }
        }

        RefreshSelection();
        Show(m_clearSel, anySelected);
        if (anySelected) SetContent(m_clearSel, H(sca::Loc::F("Tray.ClearSelection", (int)m_selected.size())));

        std::string summary;
        if (m_slots == 1)
            summary = tray.CurrentIndex >= 0
                          ? sca::Loc::F("Tray.SummaryCurrent", tray.Rows, tray.Cols, tray.CurrentIndex + 1, tray.CellCount())
                          : sca::Loc::F("Tray.SummaryDone", rows, tray.Cols);
        else
            summary = active >= 0
                          ? sca::Loc::F("Tray.SummaryOfTray", active + 1,
                                        sca::Loc::F("Tray.SummaryCurrent", rows, tray.Cols,
                                                    tray.CurrentIndex - active * perTray + 1, perTray))
                          : sca::Loc::F("Tray.SummaryDone", rows, tray.Cols);
        SetText(m_summary, summary);
    }

    void TrayCard::RefreshSelection()
    {
        // Ô chọn chỉ có viền — KHÔNG làm mờ ô còn lại (Tuấn 2026-10-30). Mờ 0.35 còn để lộ khối màu của quầng ô hiện tại
        // qua nền ô (Opacity của Grid áp riêng từng visual con), ô 1-1 thành xanh đặc.
        for (Group& g : m_groups)
        {
            for (Cell& cell : g.Items)
            {
                if (cell.Index < 0 || cell.Index >= m_cellCount) continue;
                bool sel = std::binary_search(m_selected.begin(), m_selected.end(), cell.Index);
                if (InDragRect(cell)) sel = m_drag.Add && (sel || cell.Selectable);
                int shown = sel ? 1 : 0;
                if (shown != cell.ShownSelected)
                {
                    cell.ShownSelected = shown;
                    Show(cell.Selection, sel);
                }
            }
        }
    }

    TrayCard::Cell const* TrayCard::CellAt(winrt::Windows::Foundation::Point p) const
    {
        for (const Group& g : m_groups)
            for (const Cell& cell : g.Items)
            {
                const auto& b = cell.Bounds;
                if (cell.Index >= 0 && cell.Index < m_cellCount && p.X >= b.X && p.X < b.X + b.Width && p.Y >= b.Y &&
                    p.Y < b.Y + b.Height)
                    return &cell;
            }
        return nullptr;
    }

    bool TrayCard::InDragRect(const Cell& cell) const
    {
        if (!m_drag.Active || cell.Index < 0 || cell.Index >= m_cellCount) return false;
        auto [r0, r1] = std::minmax(m_drag.Anchor->Row, m_drag.End->Row);
        auto [c0, c1] = std::minmax(m_drag.Anchor->Col, m_drag.End->Col);
        return cell.Row >= r0 && cell.Row <= r1 && cell.Col >= c0 && cell.Col <= c1;
    }

    void TrayCard::OnPressed(PointerRoutedEventArgs const& e)
    {
        if (!m_canSelect || m_drag.Active) return;   // ngón tay thứ hai lúc đang kéo: bỏ qua
        auto point = e.GetCurrentPoint(m_body);
        if (e.Pointer().PointerDeviceType() == winrt::Microsoft::UI::Input::PointerDeviceType::Mouse &&
            !point.Properties().IsLeftButtonPressed())
            return;
        for (Group& g : m_groups)
            for (Cell& cell : g.Items)
            {
                cell.Bounds = cell.Root.TransformToVisual(m_body).TransformBounds(
                    { 0, 0, (float)cell.Root.ActualWidth(), (float)cell.Root.ActualHeight() });
                cell.Selectable = cell.Index >= 0 && cell.Index < m_cellCount && (!CanSelectCell || CanSelectCell(cell.Index));
            }
        const Cell* cell = CellAt(point.Position());
        if (!cell) return;   // khe giữa ô
        m_drag.Active = true;
        m_drag.PointerId = e.Pointer().PointerId();
        m_drag.Anchor = m_drag.End = cell;
        m_drag.Add = !std::binary_search(m_selected.begin(), m_selected.end(), cell->Index);
        m_body.CapturePointer(e.Pointer());   // kéo ra ngoài lưới / nhả ở ngoài vẫn nhận được
        e.Handled(true);
        RefreshSelection();
    }

    void TrayCard::OnMoved(PointerRoutedEventArgs const& e)
    {
        if (!m_drag.Active || e.Pointer().PointerId() != m_drag.PointerId) return;
        e.Handled(true);
        const Cell* cell = CellAt(e.GetCurrentPoint(m_body).Position());
        if (!cell || cell == m_drag.End) return;   // khe / ngoài lưới: giữ ô cuối
        m_drag.End = cell;
        RefreshSelection();
    }

    void TrayCard::OnReleased(PointerRoutedEventArgs const& e)
    {
        if (!m_drag.Active || e.Pointer().PointerId() != m_drag.PointerId) return;
        e.Handled(true);
        const bool tap = m_drag.Anchor == m_drag.End;
        const int anchor = m_drag.Anchor->Index;
        const bool add = m_drag.Add;
        std::vector<int> indices;
        for (const Group& g : m_groups)
            for (const Cell& cell : g.Items)
                if (InDragRect(cell)) indices.push_back(cell.Index);
        m_drag = {};   // trước khi nhả bắt chuột — PointerCaptureLost → CancelDrag thấy không còn kéo
        m_body.ReleasePointerCapture(e.Pointer());
        // Trang gọi McControl rồi Sync lại ngay (có thể dựng lại lưới) — không dùng ô của lần kéo sau dòng này.
        if (tap) { if (OnCellTapped) OnCellTapped(anchor); }
        else if (OnCellsDragged) OnCellsDragged(indices, add);
        RefreshSelection();
    }

    void TrayCard::CancelDrag()
    {
        if (!m_drag.Active) return;
        m_drag = {};
        if (m_body) m_body.ReleasePointerCaptures();
        RefreshSelection();
    }

    void TrayCard::SetCounts(int ok, int ng)
    {
        if (!m_ok) return;
        SetText(m_ok, H(std::to_string(ok)));
        SetText(m_ng, H(std::to_string(ng)));
        SetText(m_total, H(std::to_string(ok + ng)));
    }
}
