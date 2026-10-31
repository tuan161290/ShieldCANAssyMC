#include "pch.h"
#include "Ui/UiUtil.h"

#include <charconv>

#include "Base/Async.h"
#include "Base/Text.h"
#include "Communication/AxisAngle.h"
#include "Communication/IAxis.h"
#include "Lang/Loc.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Media;
using winrt::Microsoft::UI::Xaml::Hosting::ElementCompositionPreview;

namespace ui
{
    namespace
    {
        winrt::Microsoft::UI::Dispatching::DispatcherQueue g_queue{ nullptr };

        // Tint hiện tại của nút (tint * 2 + glow), giữ ngay trên nút bằng attached property: không cần bảng tra ngoài,
        // và nút bị huỷ thì trạng thái tự mất theo.
        DependencyProperty TintProperty()
        {
            static DependencyProperty property = DependencyProperty::RegisterAttached(
                L"UiTint", xaml_typename<int32_t>(), xaml_typename<Button>(), PropertyMetadata{ box_value<int32_t>(0) });
            return property;
        }

        FrameworkElement FindChild(DependencyObject const& parent, std::wstring_view name, int depth)
        {
            int count = VisualTreeHelper::GetChildrenCount(parent);
            for (int i = 0; i < count; ++i)
            {
                auto child = VisualTreeHelper::GetChild(parent, i);
                if (auto fe = child.try_as<FrameworkElement>(); fe && fe.Name() == name) return fe;
                if (depth > 0)
                    if (auto found = FindChild(child, name, depth - 1)) return found;
            }
            return nullptr;
        }

        struct TintColors { wchar_t const* brushKey; uint32_t glow; };

        // Màu quầng sáng như Controls.xaml của bản WPF (GreenGlowEffect…).
        TintColors ColorsOf(Tint tint)
        {
            switch (tint)
            {
            case Tint::Green:   return { L"GreenOnBrush",   0xFF22C55E };
            case Tint::Red:     return { L"RedOnBrush",     0xFFFF4D5E };
            case Tint::Caution: return { L"CautionOnBrush", 0xFFFF7A1A };
            case Tint::Blue:    return { L"BlueOnBrush",    0xFF3AA0FF };
            case Tint::Lime:    return { L"LimeOnBrush",    0xFF84CC16 };
            case Tint::Yellow:  return { L"YellowOnBrush",  0xFFFFD43B };
            case Tint::Accent:  return { L"AccentFillBrush", 0 };
            default:            return { nullptr, 0 };
            }
        }

        uint32_t GlowOf(int32_t encoded)
        {
            Tint tint = (Tint)(encoded / 2);
            bool glow = (encoded % 2) != 0;
            return glow ? ColorsOf(tint).glow : 0;
        }

        void ApplyButtonGlow(Button const& button, uint32_t argb)
        {
            if (auto host = FindChild(button, L"GlowHost", 2)) SetGlow(host, argb);
        }
    }

    hstring H(std::string_view utf8) { return winrt::to_hstring(utf8); }

    std::string U8(hstring const& text) { return winrt::to_string(text); }

    hstring T(std::string_view key) { return H(sca::Loc::T(key)); }

    hstring Num(double value, int decimals)
    {
        if (decimals < 0) decimals = 0;
        return hstring(std::format(L"{:.{}f}", value, decimals));
    }

    hstring AxisPos(sca::IAxis const& axis, int decimals)
    {
        auto cfg = axis.Config();
        double pos = axis.CurrentPos();
        return cfg && cfg->WrapDegrees ? H(sca::AxisAngle::SignedText(pos, decimals)) : Num(pos, decimals);
    }

    bool ParseDouble(hstring const& text, double& value)
    {
        std::string s = base::Trim(winrt::to_string(text));
        for (char& c : s) if (c == ',') c = '.';
        if (s.empty()) return false;
        const char* begin = s.data();
        const char* end = begin + s.size();
        if (*begin == '+') ++begin;
        double parsed = 0;
        auto r = std::from_chars(begin, end, parsed);
        if (r.ec != std::errc{} || r.ptr != end || !std::isfinite(parsed)) return false;
        value = parsed;
        return true;
    }

    bool ParseInt(hstring const& text, int& value)
    {
        std::string s = base::Trim(winrt::to_string(text));
        if (s.empty()) return false;
        const char* begin = s.data();
        const char* end = begin + s.size();
        if (*begin == '+') ++begin;
        int parsed = 0;
        auto r = std::from_chars(begin, end, parsed);
        if (r.ec != std::errc{} || r.ptr != end) return false;
        value = parsed;
        return true;
    }

    Brush Res(wchar_t const* key)
    {
        auto value = Application::Current().Resources().TryLookup(box_value(key));
        return value ? value.try_as<Brush>() : nullptr;
    }

    Style ResStyle(wchar_t const* key)
    {
        auto value = Application::Current().Resources().TryLookup(box_value(key));
        return value ? value.try_as<Style>() : nullptr;
    }

    Windows::UI::Color ColorOf(uint32_t argb)
    {
        return Windows::UI::Color{ (uint8_t)(argb >> 24), (uint8_t)(argb >> 16), (uint8_t)(argb >> 8), (uint8_t)argb };
    }

    SolidColorBrush Solid(uint32_t argb) { return SolidColorBrush(ColorOf(argb)); }

    Brush StatusBrush(bool on) { return Res(on ? L"SuccessBrush" : L"StatusOffBrush"); }

    Brush AlarmLampBrush(bool on) { return Res(on ? L"CriticalBrush" : L"StatusOffBrush"); }

    void SetText(TextBlock const& block, hstring const& text)
    {
        if (block && block.Text() != text) block.Text(text);
    }

    void SetText(TextBlock const& block, std::string_view utf8) { SetText(block, H(utf8)); }

    void SetClampedText(TextBlock const& block, std::string_view utf8, FrameworkElement const& tipHost)
    {
        if (!block) return;
        hstring text = H(utf8);
        SetText(block, text);
        FrameworkElement host = tipHost ? tipHost : block;
        auto tip = ToolTipService::GetToolTip(host);
        if (unbox_value_or<hstring>(tip, hstring{}) == text) return;
        ToolTipService::SetToolTip(host, text.empty() ? nullptr : box_value(text));
    }

    void SetNotice(FrameworkElement const& slot, TextBlock const& text, std::string_view message)
    {
        SetClampedText(text, message, slot);
        ShowInPlace(slot, !::base::IsBlank(message));
    }

    void SetContent(ContentControl const& control, hstring const& text)
    {
        if (!control) return;
        auto current = control.Content().try_as<winrt::Windows::Foundation::IReference<hstring>>();
        if (current && current.Value() == text) return;
        control.Content(box_value(text));
    }

    void SetEnabled(Control const& control, bool enabled)
    {
        if (control && control.IsEnabled() != enabled) control.IsEnabled(enabled);
    }

    void SetGlow(FrameworkElement const& host, uint32_t argb, float blur, float opacity)
    {
        if (!host) return;

        // Tắt = ẨN visual đang gắn, không gỡ: SetElementChildVisual(host, nullptr) không gỡ được quầng đã gắn — nút
        // Manual tắt rồi vẫn còn quầng (Tuấn 2026-10-18). Bật lại thì dùng lại chính visual đó (như đèn IO, Controls.cpp).
        namespace comp = winrt::Microsoft::UI::Composition;
        auto child = ElementCompositionPreview::GetElementChildVisual(host);
        auto existing = child ? child.try_as<comp::SpriteVisual>() : nullptr;
        if (argb == 0)
        {
            if (existing) existing.IsVisible(false);
            return;
        }
        if (existing)
        {
            auto current = existing.Shadow();
            if (auto shadow = current ? current.try_as<comp::DropShadow>() : nullptr)
            {
                existing.Brush(existing.Compositor().CreateColorBrush(ColorOf(argb)));
                shadow.BlurRadius(blur);
                shadow.Opacity(opacity);
                shadow.Color(ColorOf(argb));
                existing.IsVisible(true);
                return;
            }
        }

        // Quầng sáng phẳng: bóng đổ không lệch (Offset 0), toả đều quanh phần tử. Visual bám kích thước
        // của host (RelativeSizeAdjustment) nên không cần nghe SizeChanged.
        auto compositor = ElementCompositionPreview::GetElementVisual(host).Compositor();
        auto sprite = compositor.CreateSpriteVisual();
        sprite.RelativeSizeAdjustment({ 1.0f, 1.0f });
        sprite.Brush(compositor.CreateColorBrush(ColorOf(argb)));
        auto shadow = compositor.CreateDropShadow();
        shadow.BlurRadius(blur);
        shadow.Offset({ 0.0f, 0.0f, 0.0f });
        shadow.Opacity(opacity);
        shadow.Color(ColorOf(argb));
        sprite.Shadow(shadow);
        ElementCompositionPreview::SetElementChildVisual(host, sprite);
    }

    void SetJogSpeedButtons(Button const& slow, Button const& fast, bool isFast)
    {
        SetTint(slow, isFast ? Tint::None : Tint::Green, false);
        SetTint(fast, isFast ? Tint::Yellow : Tint::None, true);
    }

    void SetTint(Button const& button, Tint tint, bool glow)
    {
        if (!button) return;
        if (tint == Tint::None || tint == Tint::Accent) glow = false;
        const int32_t encoded = (int32_t)tint * 2 + (glow ? 1 : 0);
        int32_t current = unbox_value_or<int32_t>(button.GetValue(TintProperty()), 0);
        if (current == encoded) return;
        button.SetValue(TintProperty(), box_value<int32_t>(encoded));

        TintColors colors = ColorsOf(tint);
        if (colors.brushKey == nullptr)
        {
            // Trả về giá trị của style (nút thường: trắng; PrimaryButton: accent).
            button.ClearValue(Control::BackgroundProperty());
            button.ClearValue(Control::BorderBrushProperty());
            button.ClearValue(Control::ForegroundProperty());
        }
        else
        {
            Brush fill = Res(colors.brushKey);
            button.Background(fill);
            button.BorderBrush(fill);
            button.Foreground(Res(tint == Tint::Yellow ? L"TextOnYellowBrush" : L"TextOnAccentBrush"));
        }

        if (button.IsLoaded())
        {
            ApplyButtonGlow(button, GlowOf(encoded));
        }
        else
        {
            // Template chưa dựng thì chưa có GlowHost: gắn quầng sáng khi nút vào cây hiển thị, theo
            // tint tại THỜI ĐIỂM đó (có thể đã đổi tiếp trong lúc chờ).
            auto token = std::make_shared<winrt::event_token>();
            *token = button.Loaded([token](Windows::Foundation::IInspectable const& sender, RoutedEventArgs const&) {
                auto b = sender.as<Button>();
                b.Loaded(*token);
                ApplyButtonGlow(b, GlowOf(unbox_value_or<int32_t>(b.GetValue(TintProperty()), 0)));
            });
        }
    }

    void InitDispatcher(winrt::Microsoft::UI::Dispatching::DispatcherQueue const& queue) { g_queue = queue; }

    bool OnUiThread() { return g_queue && g_queue.HasThreadAccess(); }

    void PostUi(std::function<void()> fn)
    {
        if (!fn) return;
        if (!g_queue)
        {
            fn();
            return;
        }
        g_queue.TryEnqueue([fn = std::move(fn)] {
            // Exception lọt ra khỏi một việc xếp hàng sẽ đi thẳng tới ranh giới WinRT và kết thúc app.
            try { fn(); }
            catch (winrt::hresult_error const&) {}
            catch (std::exception const&) {}
        });
    }

    void RunBackground(std::function<void()> work) { base::RunDetached(std::move(work)); }

    void RunBackgroundThenUi(std::function<void()> work, std::function<void()> thenOnUi)
    {
        base::RunDetached([work = std::move(work), thenOnUi = std::move(thenOnUi)]() mutable {
            // Việc nền lỗi vẫn phải trả giao diện về trạng thái rảnh (nút "đang bận"…), rồi mới để lỗi
            // đi tiếp tới nơi ghi log của RunDetached.
            try { work(); }
            catch (...)
            {
                PostUi(std::move(thenOnUi));
                throw;
            }
            PostUi(std::move(thenOnUi));
        });
    }
}
