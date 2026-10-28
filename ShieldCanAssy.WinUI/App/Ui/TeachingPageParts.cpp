#include "pch.h"
#include "Ui/TeachingPageParts.h"

#include <cmath>

#include "Data/RunningData.h"
#include "Infor/Enums.h"
#include "Ui/UiUtil.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Media;
namespace WF = winrt::Windows::Foundation;

namespace ui::teaching
{
    // ───────────────────────── CellInfo ─────────────────────────

    std::string CellInfo::Cell() const
    {
        return "(" + std::to_string(Row + 1) + ", " + std::to_string(Col + 1) + ")";
    }

    bool CellInfo::HasOffset() const { return std::fabs(Dx) > 1e-9 || std::fabs(Dy) > 1e-9; }

    bool CellInfo::HasPlaceTrim() const
    {
        return std::fabs(PlaceDx) > 1e-9 || std::fabs(PlaceDy) > 1e-9 || std::fabs(PlaceDa) > 1e-9;
    }

    // ───────────────────────── CellGridView ─────────────────────────

    namespace
    {
        constexpr double CellMinWidth = 44;   // nút MinWidth 40 + lề 2 mỗi bên
    }

    void CellGridView::Attach(Grid const& grid)
    {
        m_grid = grid;
    }

    void CellGridView::Rebuild(int rows, int cols)
    {
        m_rows = rows;
        m_cols = cols;
        m_views.clear();
        m_grid.Children().Clear();
        m_grid.RowDefinitions().Clear();
        m_grid.ColumnDefinitions().Clear();

        for (int r = 0; r < rows; ++r)
        {
            RowDefinition rd;
            rd.Height(GridLengthHelper::Auto());
            m_grid.RowDefinitions().Append(rd);
        }
        for (int c = 0; c < cols; ++c)
        {
            ColumnDefinition cd;
            cd.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            cd.MinWidth(CellMinWidth);
            m_grid.ColumnDefinitions().Append(cd);
        }

        for (int r = 0; r < rows; ++r)
        {
            for (int c = 0; c < cols; ++c)
            {
                CellView v;
                v.Button = Button();
                v.Button.Margin(ThicknessHelper::FromUniformLength(2));
                v.Button.MinWidth(40);
                v.Button.MinHeight(40);
                v.Button.Padding(ThicknessHelper::FromLengths(2, 2, 2, 2));
                v.Button.HorizontalAlignment(HorizontalAlignment::Stretch);
                v.Button.VerticalAlignment(VerticalAlignment::Stretch);
                v.Button.HorizontalContentAlignment(HorizontalAlignment::Center);
                v.Button.VerticalContentAlignment(VerticalAlignment::Center);
                ToolTipService::SetToolTip(v.Button, box_value(H("(" + std::to_string(r + 1) + ", " + std::to_string(c + 1) + ")")));

                StackPanel content;
                v.Order = TextBlock();
                v.Order.FontSize(14);
                v.Order.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
                v.Order.HorizontalAlignment(HorizontalAlignment::Center);
                content.Children().Append(v.Order);

                // Chấm nhỏ = ô có bù đặt riêng (khác nền xanh = có offset teach).
                v.Dot = TextBlock();
                v.Dot.Text(L"●");
                v.Dot.FontSize(8);
                v.Dot.HorizontalAlignment(HorizontalAlignment::Center);
                v.Dot.Foreground(Res(L"AccentBrush"));
                v.Dot.Visibility(Visibility::Collapsed);
                content.Children().Append(v.Dot);
                v.Button.Content(content);

                Grid::SetRow(v.Button, r);
                Grid::SetColumn(v.Button, c);
                int row = r, col = c;
                v.Button.Click([this, row, col](auto&&, auto&&) {
                    if (OnCellTapped) OnCellTapped(row, col);
                });
                m_grid.Children().Append(v.Button);
                m_views.push_back(v);
            }
        }
        FitWidth(m_viewport);
    }

    void CellGridView::Update(std::vector<CellInfo> const& cells, int tray, int rows, int cols, int selRow, int selCol)
    {
        if (!m_grid) return;
        if (rows != m_rows || cols != m_cols) Rebuild(rows, cols);

        for (CellInfo const& cell : cells)
        {
            if (cell.Tray != tray) continue;
            if (cell.Row < 0 || cell.Row >= rows || cell.Col < 0 || cell.Col >= cols) continue;
            CellView& v = m_views[(size_t)(cell.Row * cols + cell.Col)];

            if (v.Order_ != cell.VisitOrder)
            {
                v.Order_ = cell.VisitOrder;
                v.Order.Text(to_hstring(cell.VisitOrder));
            }

            bool offset = cell.HasOffset();
            bool trim = cell.HasPlaceTrim();
            bool selected = cell.Row == selRow && cell.Col == selCol;
            int look = (offset ? 1 : 0) | (trim ? 2 : 0) | (selected ? 4 : 0);
            if (look == v.Look) continue;
            v.Look = look;

            if (offset) v.Button.Background(Res(L"VisionOkBrush"));
            else v.Button.ClearValue(Control::BackgroundProperty());

            if (selected)
            {
                v.Button.BorderBrush(Res(L"AccentBrush"));
                v.Button.BorderThickness(ThicknessHelper::FromUniformLength(2.5));
            }
            else
            {
                v.Button.ClearValue(Control::BorderBrushProperty());
                v.Button.ClearValue(Control::BorderThicknessProperty());
            }
            Show(v.Dot, trim);
        }
    }

    void CellGridView::FitWidth(double viewportWidth)
    {
        m_viewport = viewportWidth;
        if (!m_grid || viewportWidth <= 0) return;
        double width = std::max(viewportWidth - 2, (m_cols > 0 ? m_cols : 1) * CellMinWidth);
        if (std::fabs(m_grid.Width() - width) > 0.5 || std::isnan(m_grid.Width())) m_grid.Width(width);
    }

    // ───────────────────────── hình minh hoạ thứ tự quét ─────────────────────────

    namespace
    {
        constexpr int PreviewRows = 2;
        constexpr int PreviewCols = 4;

        void DrawArrow(Canvas const& canvas, WF::Point from, WF::Point to, Brush const& brush)
        {
            double dx = to.X - from.X, dy = to.Y - from.Y;
            double len = std::sqrt(dx * dx + dy * dy);
            if (len < 1) return;
            double ux = dx / len, uy = dy / len;

            // Rút ngắn 2 đầu để mũi tên không đè lên số.
            double trim = std::max(0.0, std::min(len / 2 - 2, 14.0));
            double sx = from.X + ux * trim, sy = from.Y + uy * trim;
            double ex = to.X - ux * trim, ey = to.Y - uy * trim;

            Shapes::Line line;
            line.X1(sx);
            line.Y1(sy);
            line.X2(ex);
            line.Y2(ey);
            line.Stroke(brush);
            line.StrokeThickness(1.5);
            canvas.Children().Append(line);

            double angle = std::atan2(uy, ux);
            constexpr double headLen = 7, headAngle = 3.14159265358979323846 / 7;
            Shapes::Polygon head;
            PointCollection pts;
            pts.Append(WF::Point((float)ex, (float)ey));
            pts.Append(WF::Point((float)(ex - std::cos(angle - headAngle) * headLen), (float)(ey - std::sin(angle - headAngle) * headLen)));
            pts.Append(WF::Point((float)(ex - std::cos(angle + headAngle) * headLen), (float)(ey - std::sin(angle + headAngle) * headLen)));
            head.Points(pts);
            head.Fill(brush);
            canvas.Children().Append(head);
        }
    }

    void DrawTraversal(Canvas const& canvas, bool zigzag)
    {
        if (!canvas) return;
        canvas.Children().Clear();

        double w = canvas.ActualWidth(), h = canvas.ActualHeight();
        if (w <= 0 || h <= 0) return;

        double cw = w / PreviewCols, ch = h / PreviewRows;
        auto order = zigzag ? sca::EScanOrder::Serpentine : sca::EScanOrder::RowMajor;

        // (row,col) → vị trí trong hành trình: mũi tên nối đúng các ô LIÊN TIẾP theo thứ tự ghé.
        WF::Point points[PreviewRows * PreviewCols]{};
        for (int r = 0; r < PreviewRows; ++r)
            for (int c = 0; c < PreviewCols; ++c)
                points[sca::TrayState::VisitIndex(r, c, PreviewCols, order)] =
                    WF::Point((float)(c * cw + cw / 2), (float)(r * ch + ch / 2));

        auto gray = Solid(0xFF808080);
        for (int r = 0; r < PreviewRows; ++r)
        {
            for (int c = 0; c < PreviewCols; ++c)
            {
                Shapes::Rectangle rect;
                rect.Width(cw);
                rect.Height(ch);
                rect.Stroke(gray);
                rect.StrokeThickness(1);
                Canvas::SetLeft(rect, c * cw);
                Canvas::SetTop(rect, r * ch);
                canvas.Children().Append(rect);
            }
        }

        auto steelBlue = Solid(0xFF4682B4);
        auto label = Solid(0xFF2F4F4F);   // DarkSlateGray
        for (int i = 0; i < PreviewRows * PreviewCols; ++i)
        {
            if (i > 0) DrawArrow(canvas, points[i - 1], points[i], steelBlue);

            // Số thứ tự căn giữa ô bằng một khung cỡ ô (không cần đo chữ).
            Grid box;
            box.Width(cw);
            box.Height(ch);
            TextBlock text;
            text.Text(to_hstring(i + 1));
            text.FontSize(12);
            text.Foreground(label);
            text.HorizontalAlignment(HorizontalAlignment::Center);
            text.VerticalAlignment(VerticalAlignment::Center);
            box.Children().Append(text);
            Canvas::SetLeft(box, points[i].X - cw / 2);
            Canvas::SetTop(box, points[i].Y - ch / 2);
            canvas.Children().Append(box);
        }
    }
}
