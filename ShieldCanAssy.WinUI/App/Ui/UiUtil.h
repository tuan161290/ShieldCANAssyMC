#pragma once
// Tiện ích dùng chung cho mã giao diện (namespace ui). Lõi máy (Core) dùng std::string UTF-8 và hàm
// chặn; các hàm ở đây là cầu nối sang WinUI: đổi chuỗi, lấy brush/style, tô màu nút trạng thái,
// chuyển việc giữa luồng UI và luồng nền.
#include <functional>
#include <string>
#include <string_view>

namespace sca { class IAxis; }

namespace ui
{
    // ── Chuỗi và số ──
    winrt::hstring H(std::string_view utf8);
    std::string U8(winrt::hstring const& text);
    /// <summary>Chữ của khoá Loc (Core/Lang/StringsEn.inc) dạng hstring — tooltip/nhãn lấy từ resx (vd. T("Main.RunTip")).
    /// Chuỗi có tham số: H(sca::Loc::F("Key", a, b)).</summary>
    winrt::hstring T(std::string_view key);
    /// <summary>Số thực với số chữ số thập phân cố định, dấu chấm — vd. Num(1.5) = "1.500".</summary>
    winrt::hstring Num(double value, int decimals = 3);
    /// <summary>Toạ độ trục để hiển thị: trục quay (A1/A2, AxisConfig.WrapDegrees) có dấu (−180, +180] — 359 thành "-1.000"
    /// (sca::AxisAngle::SignedText, Tuấn 2026-10-30); trục thẳng như Num.</summary>
    winrt::hstring AxisPos(sca::IAxis const& axis, int decimals = 3);
    /// <summary>Đọc số người dùng gõ; nhận cả dấu phẩy lẫn dấu chấm thập phân. False nếu không phải số.</summary>
    bool ParseDouble(winrt::hstring const& text, double& value);
    bool ParseInt(winrt::hstring const& text, int& value);

    // ── Tài nguyên trong Styles.xaml ──
    winrt::Microsoft::UI::Xaml::Media::Brush Res(wchar_t const* key);
    winrt::Microsoft::UI::Xaml::Style ResStyle(wchar_t const* key);
    winrt::Windows::UI::Color ColorOf(uint32_t argb);
    winrt::Microsoft::UI::Xaml::Media::SolidColorBrush Solid(uint32_t argb);

    /// <summary>true = xanh lá (bình thường), false = xám đậm ("tắt" trung tính, không phải lỗi).</summary>
    winrt::Microsoft::UI::Xaml::Media::Brush StatusBrush(bool on);
    /// <summary>Đèn ALM/POT/NOT: true = đỏ, false = xám đậm — trạng thái bình thường KHÔNG tô xanh.</summary>
    winrt::Microsoft::UI::Xaml::Media::Brush AlarmLampBrush(bool on);

    inline void Show(winrt::Microsoft::UI::Xaml::UIElement const& element, bool visible)
    {
        using winrt::Microsoft::UI::Xaml::Visibility;
        if (!element) return;
        auto v = visible ? Visibility::Visible : Visibility::Collapsed;
        if (element.Visibility() != v) element.Visibility(v);
    }

    /// <summary>Ẩn/hiện mà VẪN GIỮ CHỖ trong layout (Opacity + IsHitTestVisible). Dùng cho mọi thứ bật/tắt lúc máy chạy —
    /// vòng bận, cảnh báo, dòng tiến trình: Show (Collapsed ↔ Visible) làm các nút quanh đó xô theo mỗi lần nó hiện
    /// (Tuấn 2026-10-20). Show chỉ còn cho chỗ đổi theo ngữ cảnh: chuyển trang/bước/tray, chế độ cài ở Setting.</summary>
    inline void ShowInPlace(winrt::Microsoft::UI::Xaml::UIElement const& element, bool visible)
    {
        if (!element) return;
        double opacity = visible ? 1.0 : 0.0;
        if (element.Opacity() != opacity) element.Opacity(opacity);
        if (element.IsHitTestVisible() != visible) element.IsHitTestVisible(visible);
    }

    /// <summary>Đặt Text chỉ khi khác — vòng cập nhật 100 ms không được làm bẩn layout mỗi nhịp.</summary>
    void SetText(winrt::Microsoft::UI::Xaml::Controls::TextBlock const& block, winrt::hstring const& text);
    void SetText(winrt::Microsoft::UI::Xaml::Controls::TextBlock const& block, std::string_view utf8);
    /// <summary>Chữ trong ô CAO CỐ ĐỊNH (MaxLines + TextTrimming trong XAML): đặt chữ và đủ câu ở tooltip của
    /// <paramref name="tipHost"/> (mặc định chính TextBlock). Chữ dài thêm không đẩy layout, chỉ bị cắt "…".</summary>
    void SetClampedText(winrt::Microsoft::UI::Xaml::Controls::TextBlock const& block, std::string_view utf8,
                        winrt::Microsoft::UI::Xaml::FrameworkElement const& tipHost = nullptr);
    /// <summary>Ô cảnh báo giữ chỗ (NoticeSlotStyle): chữ + tooltip như SetClampedText, trống thì ẩn ô bằng ShowInPlace.</summary>
    void SetNotice(winrt::Microsoft::UI::Xaml::FrameworkElement const& slot,
                   winrt::Microsoft::UI::Xaml::Controls::TextBlock const& text, std::string_view message);
    /// <summary>Nội dung chữ của nút, chỉ khi khác.</summary>
    void SetContent(winrt::Microsoft::UI::Xaml::Controls::ContentControl const& control, winrt::hstring const& text);
    /// <summary>IsEnabled chỉ khi khác.</summary>
    void SetEnabled(winrt::Microsoft::UI::Xaml::Controls::Control const& control, bool enabled);

    // ── Nút trạng thái (UI-SPEC §0.4) ──
    /// <summary>Màu của nút "đang BẬT": tô đặc + chữ trắng. None = trả về kiểu gốc của nút (style).
    /// Accent = lựa chọn đang chọn trong một nhóm: nền accent, chữ trắng, KHÔNG bao giờ có quầng sáng.
    /// Yellow = vàng cảnh báo #FFC800, chữ nâu đậm (jog Fast).</summary>
    enum class Tint { None = 0, Green, Red, Caution, Blue, Lime, Accent, Yellow };

    /// <summary>
    /// Tô nút theo trạng thái. <paramref name="glow"/> = quầng sáng quanh nút: chỉ cho trạng thái CẦN CHÚ Ý (đang chạy,
    /// cần Init, alarm, SVO on, IO on); trạng thái "đang chọn" bình thường thì tô đặc không quầng (thumb mode, STOP lúc
    /// đứng, Origin xong ở Main, chọn tay hút, Dry run) — WPF 2026-10-13. Màu: Green #198754, Red #DC3545, Caution
    /// #D35400, Blue #0078D4. Gọi lặp lại với cùng giá trị không tốn gì — dùng thẳng trong OnTick. Nút phải dùng
    /// FlatButtonStyle (mặc định của mọi Button trong app) — template có "GlowHost".
    /// </summary>
    void SetTint(winrt::Microsoft::UI::Xaml::Controls::Button const& button, Tint tint, bool glow = true);

    /// <summary>Cặp nút tốc độ jog (Teaching, Vision Edit — Tuấn 2026-10-18): Slow đang chọn = xanh lá đặc; Fast đang chọn
    /// = vàng cảnh báo có quầng sáng (chạy nhanh cần chú ý); nút còn lại về kiểu thường.</summary>
    void SetJogSpeedButtons(winrt::Microsoft::UI::Xaml::Controls::Button const& slow,
                            winrt::Microsoft::UI::Xaml::Controls::Button const& fast, bool isFast);

    /// <summary>Quầng sáng phẳng quanh một phần tử bất kỳ (vd. Border sau mục Alarm của thanh điều hướng, ô tray đang làm).
    /// argb = 0: tắt. Mặc định như glow nút của WPF (blur 16, opacity 0.75); ô tray hiện tại: blur 9, opacity 0.9.</summary>
    void SetGlow(winrt::Microsoft::UI::Xaml::FrameworkElement const& host, uint32_t argb, float blur = 16.0f,
                 float opacity = 0.75f);

    // ── Luồng ──
    void InitDispatcher(winrt::Microsoft::UI::Dispatching::DispatcherQueue const& queue);
    bool OnUiThread();
    /// <summary>Chạy trên luồng UI (xếp hàng, không chờ). Gọi được từ luồng bất kỳ. Exception trong fn bị nuốt.</summary>
    void PostUi(std::function<void()> fn);
    /// <summary>Chạy một việc dài (lệnh trục, gọi vision, lưu file…) trên luồng nền — luồng UI KHÔNG BAO
    /// GIỜ gọi thẳng hàm chặn của Core. Exception lọt ra được ghi log (base::SetBackgroundErrorHandler).</summary>
    void RunBackground(std::function<void()> work);
    /// <summary>Như RunBackground, xong (kể cả khi work ném) thì chạy <paramref name="thenOnUi"/> trên luồng UI — dùng
    /// để gỡ cờ Busy của trang.</summary>
    void RunBackgroundThenUi(std::function<void()> work, std::function<void()> thenOnUi);
}
