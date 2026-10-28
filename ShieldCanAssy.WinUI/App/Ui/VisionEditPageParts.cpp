#include "pch.h"
#include "Ui/VisionEditPageParts.h"

#include "Ui/JogControl.h"
#include "Ui/UiUtil.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace ui::visionedit
{
    StackPanel BuildJogCell(std::string const& axisName, std::vector<Button>& buttons)
    {
        StackPanel cell;
        cell.Orientation(Orientation::Horizontal);
        cell.Margin(ThicknessHelper::FromLengths(0, 2, 16, 2));

        TextBlock name;
        name.Text(ui::H(axisName));
        name.Width(28);
        name.FontSize(14);
        name.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
        name.VerticalAlignment(VerticalAlignment::Center);
        cell.Children().Append(name);

        for (int dir : { -1, 1 })
        {
            Button b;
            b.Content(box_value(dir < 0 ? L"−" : L"+"));
            b.Width(48);
            b.Height(40);
            b.FontSize(18);
            b.Padding(ThicknessHelper::FromUniformLength(0));
            if (dir < 0) b.Margin(ThicknessHelper::FromLengths(0, 0, 2, 0));
            ToolTipService::SetToolTip(b, box_value(ui::T(dir < 0 ? "Vision.JogBackTip" : "Vision.JogFwdTip")));
            ui::AttachJog(b, axisName, dir);
            cell.Children().Append(b);
            buttons.push_back(b);
        }
        return cell;
    }

    void SetBadge(Button const& button, int number, wchar_t const* label, TextBlock* caption)
    {
        Grid root;
        root.ColumnSpacing(10);
        ColumnDefinition c0; c0.Width(GridLengthHelper::Auto());
        ColumnDefinition c1; c1.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        root.ColumnDefinitions().Append(c0);
        root.ColumnDefinitions().Append(c1);

        Border badge;
        badge.Width(26);
        badge.Height(26);
        badge.CornerRadius(CornerRadiusHelper::FromUniformRadius(13));
        badge.BorderThickness(ThicknessHelper::FromUniformLength(1.5));
        badge.BorderBrush(ui::Res(L"AccentBrush"));
        badge.VerticalAlignment(VerticalAlignment::Center);
        TextBlock num;
        num.Text(winrt::to_hstring(number));
        num.FontSize(13);
        num.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        num.Foreground(ui::Res(L"AccentBrush"));
        num.HorizontalAlignment(HorizontalAlignment::Center);
        num.VerticalAlignment(VerticalAlignment::Center);
        badge.Child(num);
        root.Children().Append(badge);

        StackPanel text;
        text.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(text, 1);
        TextBlock name;
        name.Text(label);
        name.TextWrapping(TextWrapping::Wrap);
        text.Children().Append(name);
        if (caption)
        {
            TextBlock cap;
            cap.FontSize(11);
            cap.Foreground(ui::Res(L"TextSecondaryBrush"));
            cap.TextWrapping(TextWrapping::Wrap);
            text.Children().Append(cap);
            *caption = cap;
        }
        root.Children().Append(text);
        button.Content(root);
    }

    FrameworkElement BuildCellPicker(winrt::hstring const& titleText, int rows, int cols, int selRow, int selCol,
                                     bool allowInPlace, std::function<void(int, int)> onPick)
    {
        StackPanel root;
        root.Padding(ThicknessHelper::FromUniformLength(4));

        TextBlock title;
        title.Text(titleText);
        title.FontSize(12);
        title.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        title.Margin(ThicknessHelper::FromLengths(0, 0, 0, 6));
        root.Children().Append(title);

        Grid grid;
        grid.RowSpacing(4);
        grid.ColumnSpacing(4);
        for (int r = 0; r < rows; ++r)
        {
            RowDefinition rd; rd.Height(GridLengthHelper::Auto());
            grid.RowDefinitions().Append(rd);
        }
        for (int c = 0; c < cols; ++c)
        {
            ColumnDefinition cd; cd.Width(GridLengthHelper::Auto());
            grid.ColumnDefinitions().Append(cd);
        }
        auto pick = std::make_shared<std::function<void(int, int)>>(std::move(onPick));
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c)
            {
                Button b;
                b.Content(box_value(ui::H("(" + std::to_string(r + 1) + "," + std::to_string(c + 1) + ")")));
                b.MinWidth(48);
                b.MinHeight(40);
                b.FontSize(11);
                b.Padding(ThicknessHelper::FromLengths(4, 2, 4, 2));
                if (r == selRow && c == selCol)
                {
                    b.BorderBrush(ui::Res(L"AccentBrush"));
                    b.BorderThickness(ThicknessHelper::FromUniformLength(2.5));
                }
                b.Click([pick, r, c](auto&&, auto&&) { if (*pick) (*pick)(r, c); });
                Grid::SetRow(b, r);
                Grid::SetColumn(b, c);
                grid.Children().Append(b);
            }
        root.Children().Append(grid);

        if (allowInPlace)
        {
            Button inPlace;
            inPlace.Content(box_value(L"In Place"));
            inPlace.HorizontalAlignment(HorizontalAlignment::Stretch);
            inPlace.Margin(ThicknessHelper::FromLengths(0, 6, 0, 0));
            inPlace.Click([pick](auto&&, auto&&) { if (*pick) (*pick)(-1, -1); });
            root.Children().Append(inPlace);
        }

        // Tray lớn: lưới cuộn được, không tràn khỏi màn hình.
        ScrollViewer sv;
        sv.HorizontalScrollBarVisibility(ScrollBarVisibility::Auto);
        sv.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        sv.MaxHeight(560);
        sv.MaxWidth(900);
        sv.Content(root);
        return sv;
    }
}
