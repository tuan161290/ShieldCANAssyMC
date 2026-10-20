#include "pch.h"
#include "Ui/Controls.h"

#include <cstring>

#include "Communication/ImageData.h"
#include "Ui/JogControl.h"
#include "Ui/UiUtil.h"
#include "Utility/DateTimeText.h"
#include "Utility/Log/LogWorker.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Media;
using winrt::Microsoft::UI::Xaml::Hosting::ElementCompositionPreview;
using winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap;
namespace WF = winrt::Windows::Foundation;

namespace ui
{
    namespace
    {
        FrameworkElement FindDescendant(DependencyObject const& parent, std::wstring_view name, int depth)
        {
            int count = VisualTreeHelper::GetChildrenCount(parent);
            for (int i = 0; i < count; ++i)
            {
                auto child = VisualTreeHelper::GetChild(parent, i);
                if (auto fe = child.try_as<FrameworkElement>(); fe && fe.Name() == name) return fe;
                if (depth > 0)
                    if (auto found = FindDescendant(child, name, depth - 1)) return found;
            }
            return nullptr;
        }

        bool SameLog(const sca::LogData& a, const sca::LogData& b)
        {
            return a.UpdateTime == b.UpdateTime && a.Level == b.Level && a.Source == b.Source && a.Message == b.Message;
        }
    }

    // ══════════ Đèn ══════════

    ManualLampBrushes DefaultLampBrushes()
    {
        ManualLampBrushes b;
        b.Off = Solid(0xFFD3D3D3);   // LightGray
        b.Stroke = Solid(0x40000000);
        b.On = Res(L"CriticalBrush");
        return b;
    }

    FrameworkElement ManualLamp::Build(double size, ManualLampBrushes const& brushes)
    {
        m_brushes = brushes;
        m_size = (float)size;

        m_root = Grid();
        m_root.Width(size);
        m_root.Height(size);
        m_root.VerticalAlignment(VerticalAlignment::Center);

        m_glowHost = Border();   // nằm DƯỚI chấm đèn: quầng sáng toả ra quanh chấm, không phủ lên nó
        m_dot = winrt::Microsoft::UI::Xaml::Shapes::Ellipse();   // tên trần đụng hàm GDI ::Ellipse
        m_dot.Width(size);
        m_dot.Height(size);
        m_dot.Fill(brushes.Off);
        m_dot.Stroke(brushes.Stroke);
        m_dot.StrokeThickness(1);

        m_root.Children().Append(m_glowHost);
        m_root.Children().Append(m_dot);
        m_state = 0;
        return m_root;
    }

    void ManualLamp::Set(bool on)
    {
        int state = on ? 1 : 0;
        if (!m_dot || m_state == state) return;
        m_state = state;
        m_dot.Fill(on ? m_brushes.On : m_brushes.Off);

        if (on && !m_glow)
        {
            // Glow riêng cỡ chấm (#FF5C5C, blur 6, không lệch) — bóng đổ lấy chính hình tròn làm mặt nạ.
            auto compositor = ElementCompositionPreview::GetElementVisual(m_glowHost).Compositor();
            m_glow = compositor.CreateSpriteVisual();
            m_glow.Size({ m_size, m_size });
            auto shadow = compositor.CreateDropShadow();
            shadow.BlurRadius(6.0f);
            shadow.Offset({ 0.0f, 0.0f, 0.0f });
            shadow.Opacity(1.0f);
            shadow.Color(ColorOf(0xFFFF5C5C));
            shadow.Mask(m_dot.GetAlphaMask());
            m_glow.Shadow(shadow);
            ElementCompositionPreview::SetElementChildVisual(m_glowHost, m_glow);
        }
        if (m_glow) m_glow.IsVisible(on);
    }

    FrameworkElement IoLampList::Build(bool inputs, int count, std::function<std::string(int)> name)
    {
        m_lamps.clear();
        ManualLampBrushes brushes = DefaultLampBrushes();
        StackPanel panel;
        panel.Padding(ThicknessHelper::FromLengths(12, 6, 12, 6));
        for (int bit = 0; bit < count; ++bit)
        {
            StackPanel line;
            line.Orientation(Orientation::Horizontal);
            line.MinHeight(24);
            auto lamp = std::make_unique<ManualLamp>();
            line.Children().Append(lamp->Build(11, brushes));
            TextBlock text;
            std::string label = std::format("{} 0x{:02X}", inputs ? "DI" : "DO", bit);
            std::string n = name ? name(bit) : std::string();
            if (!n.empty()) label += "  " + n;
            text.Text(H(label));
            text.FontFamily(FontFamily(L"Consolas"));
            text.FontSize(11);
            text.Margin(ThicknessHelper::FromLengths(8, 0, 0, 0));
            text.VerticalAlignment(VerticalAlignment::Center);
            text.TextTrimming(TextTrimming::CharacterEllipsis);
            line.Children().Append(text);
            panel.Children().Append(line);
            m_lamps.push_back(std::move(lamp));
        }
        return panel;
    }

    void IoLampList::Update(std::function<bool(int bit)> const& read)
    {
        if (!read) return;
        for (size_t i = 0; i < m_lamps.size(); ++i) m_lamps[i]->Set(read((int)i));
    }

    // ══════════ Nút / ô nhập hẹp ══════════

    Button ManualCompactButton(wchar_t const* text, double width, wchar_t const* tip)
    {
        Button b;
        b.Content(box_value(text));
        b.Width(width);
        b.Padding(ThicknessHelper::FromLengths(4, 3, 4, 3));
        if (tip) ToolTipService::SetToolTip(b, box_value(tip));
        return b;
    }

    void HideTextBoxDeleteButton(TextBox const& box)
    {
        auto token = std::make_shared<winrt::event_token>();
        *token = box.Loaded([token](WF::IInspectable const& sender, RoutedEventArgs const&) {
            auto tb = sender.as<TextBox>();
            tb.Loaded(*token);
            tb.ApplyTemplate();
            auto button = FindDescendant(tb, L"DeleteButton", 4).try_as<Button>();
            if (!button) return;
            button.MinWidth(0);
            button.Width(0);
            button.MaxWidth(0);
            button.Margin(ThicknessHelper::FromUniformLength(0));
            button.Padding(ThicknessHelper::FromUniformLength(0));
            button.Opacity(0);
            button.IsHitTestVisible(false);
            button.IsTabStop(false);
        });
    }

    // ══════════ Hàng jog ══════════

    StackPanel BuildJogRow(std::string const& axisName, JogRowOptions const& options, TextBlock& posText)
    {
        StackPanel row;
        row.Orientation(Orientation::Horizontal);
        row.Spacing(4);

        TextBlock name;
        name.Text(H(axisName));
        name.Width(options.nameWidth);
        name.FontSize(14);
        name.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
        name.VerticalAlignment(VerticalAlignment::Center);
        row.Children().Append(name);

        posText = TextBlock();
        posText.Text(L"0.000");
        posText.Width(options.positionWidth);
        posText.FontFamily(FontFamily(L"Consolas"));
        posText.FontSize(14);
        posText.TextAlignment(TextAlignment::Right);
        posText.Margin(ThicknessHelper::FromLengths(0, 0, 6, 0));
        posText.VerticalAlignment(VerticalAlignment::Center);
        if (!options.showPosition) posText.Visibility(Visibility::Collapsed);
        row.Children().Append(posText);

        for (int dir : { -1, 1 })
        {
            Button b;
            b.Content(box_value(dir < 0 ? L"−" : L"+"));
            b.Width(options.buttonWidth);
            b.Height(options.buttonHeight);
            b.MinHeight(options.buttonHeight);
            b.Padding(ThicknessHelper::FromUniformLength(0));
            ToolTipService::SetToolTip(b, box_value(T(dir < 0 ? "Manual.JogMinusTip" : "Manual.JogPlusTip")));
            AttachJog(b, axisName, dir);
            row.Children().Append(b);
        }
        return row;
    }

    // ══════════ Ô số ══════════

    void ConfigureNumberBox(NumberBox const& box, int decimals)
    {
        box.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Hidden);
        box.AcceptsExpression(false);
        box.ValidationMode(NumberBoxValidationMode::InvalidInputOverwritten);
        box.MinHeight(36);
        winrt::Windows::Globalization::NumberFormatting::DecimalFormatter formatter;
        formatter.IntegerDigits(1);
        formatter.FractionDigits(decimals < 0 ? 0 : decimals);
        formatter.IsGrouped(false);
        winrt::Windows::Globalization::NumberFormatting::IncrementNumberRounder rounder;
        rounder.Increment(std::pow(10.0, -(decimals < 0 ? 0 : decimals)));
        rounder.RoundingAlgorithm(winrt::Windows::Globalization::NumberFormatting::RoundingAlgorithm::RoundHalfAwayFromZero);
        formatter.NumberRounder(rounder);
        box.NumberFormatter(formatter);
    }

    StackPanel UnitNumber(NumberBox const& box, wchar_t const* unit)
    {
        StackPanel panel;
        panel.Orientation(Orientation::Horizontal);
        panel.Spacing(6);
        panel.Children().Append(box);
        TextBlock u;
        u.Text(unit ? unit : L"");
        u.FontSize(12);
        u.Foreground(Res(L"TextSecondaryBrush"));
        u.VerticalAlignment(VerticalAlignment::Center);
        panel.Children().Append(u);
        return panel;
    }

    double NumberOr(NumberBox const& box, double fallback)
    {
        double v = box ? box.Value() : std::nan("");
        return std::isnan(v) ? fallback : v;
    }

    // ══════════ Log ══════════

    ListView LogListView::Build()
    {
        m_list = ListView();
        m_list.SelectionMode(ListViewSelectionMode::None);
        m_list.IsItemClickEnabled(false);
        m_shown.clear();
        m_version = ~0ull;
        return m_list;
    }

    void LogListView::Update()
    {
        if (!m_list) return;
        auto& worker = sca::LogWorker::Instance();
        const uint64_t version = worker.Version();
        if (version == m_version) return;
        m_version = version;

        std::vector<sca::LogData> list = worker.LogList();   // mới nhất ở đầu
        auto items = m_list.Items();

        // Dòng đầu đang hiện nằm ở đâu trong bản chụp mới: những dòng đứng trước nó là dòng mới.
        size_t added = list.size();
        bool incremental = false;
        if (!m_shown.empty())
        {
            for (size_t k = 0; k < list.size(); ++k)
                if (SameLog(list[k], m_shown.front()))
                {
                    added = k;
                    incremental = true;
                    break;
                }
        }

        if (incremental)
        {
            for (size_t i = added; i-- > 0;)
            {
                items.InsertAt(0, MakeRow(list[i]));
                m_shown.push_front(list[i]);
            }
            while (items.Size() > list.size() && items.Size() > 0)
            {
                items.RemoveAtEnd();
                m_shown.pop_back();
            }
        }
        else
        {
            items.Clear();
            m_shown.clear();
            for (const auto& d : list)
            {
                items.Append(MakeRow(d));
                m_shown.push_back(d);
            }
        }
        if (added > 0 && items.Size() > 0) m_list.ScrollIntoView(items.GetAt(0));
    }

    FrameworkElement LogListView::MakeRow(const sca::LogData& d)
    {
        Brush bar = Res(L"AccentBrush");
        Brush source = Res(L"TextSecondaryBrush");
        Brush message{ nullptr };
        bool bold = false;
        switch (d.Level)
        {
        case sca::LogLevel::Warning:
            bar = Solid(0xFFE8A317);
            source = Solid(0xFF9D5D00);
            message = Solid(0xFF9D5D00);
            break;
        case sca::LogLevel::Error:
            bar = Solid(0xFFE94949);
            source = Solid(0xFFC42B1C);
            message = Solid(0xFFC42B1C);
            bold = true;
            break;
        case sca::LogLevel::Debug:
            bar = Solid(0xFFA0A0A0);
            message = Solid(0xFF707070);
            break;
        default:
            break;
        }

        TextBlock time;
        time.Text(H(sca::FormatLocal(d.UpdateTime, "HH:mm:ss")));
        time.FontFamily(FontFamily(L"Consolas"));
        time.FontSize(11);
        time.Foreground(Res(L"TextTertiaryBrush"));
        time.VerticalAlignment(VerticalAlignment::Center);

        TextBlock src;
        src.Text(H(d.Source));
        src.FontSize(11);
        src.Margin(ThicknessHelper::FromLengths(6, 0, 0, 0));
        src.Foreground(source);
        src.VerticalAlignment(VerticalAlignment::Center);

        StackPanel head;
        head.Orientation(Orientation::Horizontal);
        head.Children().Append(time);
        head.Children().Append(src);

        TextBlock msg;
        msg.Text(H(d.Message));
        msg.TextWrapping(TextWrapping::Wrap);
        msg.FontSize(12);
        if (message) msg.Foreground(message);
        if (bold) msg.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());

        StackPanel stack;
        stack.Children().Append(head);
        stack.Children().Append(msg);

        Border barBorder;
        barBorder.BorderThickness(ThicknessHelper::FromLengths(3, 0, 0, 0));
        barBorder.BorderBrush(bar);
        barBorder.Padding(ThicknessHelper::FromLengths(7, 0, 0, 0));
        barBorder.Child(stack);

        Border item;
        item.Padding(ThicknessHelper::FromLengths(0, 3, 0, 3));
        item.Child(barBorder);
        return item;
    }

    // ══════════ Khung ảnh ══════════

    std::shared_ptr<ImageView> ImageView::Create(Image const& image, UIElement const& noImagePlaceholder)
    {
        auto v = std::make_shared<ImageView>();
        v->m_image = image;
        v->m_placeholder = noImagePlaceholder;
        return v;
    }

    bool ImageView::ToBgra(const sca::ImageData& img, std::vector<uint8_t>& out)
    {
        const int w = img.Width, h = img.Height, c = img.Channels;
        if (w <= 0 || h <= 0 || (c != 1 && c != 3 && c != 4)) return false;
        const size_t row = (size_t)w * (size_t)c;
        const size_t stride = img.Stride > 0 ? (size_t)img.Stride : row;
        if (stride < row || img.Pixels.size() < stride * (size_t)(h - 1) + row) return false;

        out.resize((size_t)w * (size_t)h * 4);
        for (int y = 0; y < h; ++y)
        {
            const uint8_t* s = img.Pixels.data() + stride * (size_t)y;
            uint8_t* d = out.data() + (size_t)y * (size_t)w * 4;
            switch (c)
            {
            case 1:
                for (int x = 0; x < w; ++x, ++s, d += 4) { d[0] = d[1] = d[2] = *s; d[3] = 255; }
                break;
            case 3:
                for (int x = 0; x < w; ++x, s += 3, d += 4) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255; }
                break;
            default:
                for (int x = 0; x < w; ++x, s += 4, d += 4) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255; }
                break;
            }
        }
        return true;
    }

    bool ImageView::Set(std::shared_ptr<const sca::ImageData> src)
    {
        if (src == m_src) return false;
        m_src = src;
        const uint64_t generation = ++m_generation;
        if (!src)
        {
            if (m_image) m_image.Source(nullptr);
            m_bitmap = nullptr;
            ui::Show(m_placeholder, true);
            return true;
        }
        auto bgra = std::make_shared<std::vector<uint8_t>>();
        std::weak_ptr<ImageView> weak = weak_from_this();
        RunBackgroundThenUi([src, bgra] { if (!ToBgra(*src, *bgra)) bgra->clear(); },
                            [weak, generation, src, bgra] {
                                if (auto self = weak.lock()) self->Show(generation, src->Width, src->Height, *bgra);
                            });
        return true;
    }

    void ImageView::Show(uint64_t generation, int width, int height, std::vector<uint8_t> const& bgra)
    {
        if (generation != m_generation || !m_image) return;   // đã có ảnh mới hơn trong lúc đổi định dạng
        auto clear = [this] {
            m_image.Source(nullptr);
            m_bitmap = nullptr;
            ui::Show(m_placeholder, true);
        };
        if (bgra.empty() || width <= 0 || height <= 0)
        {
            clear();
            return;
        }
        try
        {
            bool fresh = false;
            if (!m_bitmap || m_bitmap.PixelWidth() != width || m_bitmap.PixelHeight() != height)
            {
                m_bitmap = WriteableBitmap(width, height);
                fresh = true;
            }
            auto buffer = m_bitmap.PixelBuffer();
            std::memcpy(buffer.data(), bgra.data(), std::min<size_t>(bgra.size(), buffer.Length()));
            m_bitmap.Invalidate();
            if (fresh) m_image.Source(m_bitmap);
            ui::Show(m_placeholder, false);
        }
        catch (winrt::hresult_error const& e)
        {
            sca::LogWorker::Instance().RecordLog("Cannot show CAM image — " + U8(e.message()), sca::LogLevel::Warning, "Vision");
            clear();
        }
    }
}
