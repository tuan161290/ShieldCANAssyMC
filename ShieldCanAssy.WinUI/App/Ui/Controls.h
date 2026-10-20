#pragma once
// Thành phần giao diện dùng chung giữa các trang (UI-SPEC §9): đèn bit, danh sách đèn IO, hàng jog, ô số có đơn vị,
// danh sách log, khung ảnh. Mọi thứ dựng bằng code (không DataTemplate), giữ control của mình và giá trị đã hiện lần
// trước; Update/Set chỉ đụng control khi giá trị ĐỔI — gọi thẳng trong OnTick được.
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Utility/Log/LogData.h"

namespace sca { struct ImageData; }

namespace ui
{
    // ══════════ Đèn bit (§9.4) ══════════

    /// <summary>Brush dùng chung của các đèn (LampOffBrush / LampStrokeBrush / CriticalBrush của bản WPF). Tạo một
    /// lần trên luồng UI (DefaultLampBrushes) rồi chia cho mọi đèn.</summary>
    struct ManualLampBrushes
    {
        winrt::Microsoft::UI::Xaml::Media::Brush Off{ nullptr };
        winrt::Microsoft::UI::Xaml::Media::Brush Stroke{ nullptr };
        winrt::Microsoft::UI::Xaml::Media::Brush On{ nullptr };
    };

    /// <summary>Off = LightGray (LampOffBrush), viền #40000000, On = CriticalBrush.</summary>
    ManualLampBrushes DefaultLampBrushes();

    /// <summary>
    /// Đèn bit thô (DI/DO, POT/NOT): OFF xám nhạt trung tính, ON đỏ + quầng sáng đỏ #FF5C5C blur 6 — bắt mắt ngay bit nào
    /// đang lên (nhiều bit ON là BÌNH THƯỜNG, vd. NC như EmgOk; không phải kiểu "tốt = xanh").
    /// </summary>
    class ManualLamp
    {
    public:
        winrt::Microsoft::UI::Xaml::FrameworkElement Build(double size, ManualLampBrushes const& brushes);
        /// <summary>Gọi mỗi nhịp được: chỉ đụng tới control khi trạng thái đổi.</summary>
        void Set(bool on);

    private:
        ManualLampBrushes m_brushes;
        winrt::Microsoft::UI::Xaml::Controls::Grid m_root{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Border m_glowHost{ nullptr };
        winrt::Microsoft::UI::Xaml::Shapes::Ellipse m_dot{ nullptr };
        winrt::Microsoft::UI::Composition::SpriteVisual m_glow{ nullptr };
        float m_size{ 10 };
        int m_state{ -1 };   // -1 = chưa vẽ lần nào
    };

    /// <summary>
    /// Danh sách đèn IO (Manual MN-37/38): mỗi dòng "● DI 0x00  Tên" (Consolas 11, đèn 11 px). Đặt phần tử trả về vào
    /// một ScrollViewer của trang.
    /// </summary>
    class IoLampList
    {
    public:
        /// <param name="inputs">true: tiền tố "DI", false: "DO".</param>
        /// <param name="name">Tên bit (vd. IoMap DiNames[bit]); rỗng thì chỉ hiện số bit.</param>
        winrt::Microsoft::UI::Xaml::FrameworkElement Build(bool inputs, int count, std::function<std::string(int)> name);
        /// <summary>Đọc từng bit qua <paramref name="read"/> (vd. io->GetDi(bit)); chỉ đèn đổi mới bị vẽ lại.</summary>
        void Update(std::function<bool(int bit)> const& read);

    private:
        std::vector<std::unique_ptr<ManualLamp>> m_lamps;
    };

    // ══════════ Nút / ô nhập hẹp ══════════

    /// <summary>Nút hẹp (CompactButtonStyle của bản WPF): Padding 4,3, bề rộng cố định theo cột. MinHeight vẫn 40 theo style
    /// (compact = hẹp ngang, không thấp hơn).</summary>
    winrt::Microsoft::UI::Xaml::Controls::Button ManualCompactButton(wchar_t const* text, double width, wchar_t const* tip = nullptr);

    /// <summary>TextBox hẹp: gỡ nút "xoá chữ" (chiếm ~30 px bên phải lúc đang gõ) khi template đã dựng.</summary>
    void HideTextBoxDeleteButton(winrt::Microsoft::UI::Xaml::Controls::TextBox const& box);

    // ══════════ Hàng jog (§9.3) — Teaching, Vision Edit ══════════

    struct JogRowOptions
    {
        bool showPosition = true;
        double buttonHeight = 36;   // ngoại lệ compact của UI-SPEC §10.4
        double buttonWidth = 36;
        double nameWidth = 28;
        double positionWidth = 76;
    };

    /// <summary>
    /// "X  123.456 [−][+]" — tên trục (Bold), vị trí (Consolas, chỉ khi showPosition), hai nút jog [Hold] gắn
    /// ui::AttachJog(btn, axis, ∓1). Trả StackPanel ngang: trang Append thêm nút riêng (Go T1, Origin, Go Pick…) vào
    /// Children(). <paramref name="posText"/> nhận TextBlock vị trí để trang cập nhật trong OnTick (ui::SetText(pos,
    /// ui::Num(axis->CurrentPos()))). Tooltip − / + = Manual.JogMinusTip / JogPlusTip.
    /// </summary>
    winrt::Microsoft::UI::Xaml::Controls::StackPanel BuildJogRow(std::string const& axisName, JogRowOptions const& options,
                                                                 winrt::Microsoft::UI::Xaml::Controls::TextBlock& posText);

    // ══════════ Ô số có đơn vị (§9.7) ══════════

    /// <summary>
    /// Đặt NumberBox theo quy ước app: SpinButtonPlacementMode Hidden, AcceptsExpression false, ValidationMode
    /// InvalidInputOverwritten, MinHeight 36, định dạng IntegerDigits 1 + FractionDigits <paramref name="decimals"/>
    /// (0 = số nguyên). KHÔNG đặt Minimum/Maximum ở chỗ WPF kiểm bằng thông báo — NumberBox sẽ âm thầm kẹp giá trị.
    /// </summary>
    void ConfigureNumberBox(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& box, int decimals = 3);

    /// <summary>NumberBox + chữ đơn vị bên phải ("mm", "ms", "°") trong một StackPanel ngang.</summary>
    winrt::Microsoft::UI::Xaml::Controls::StackPanel UnitNumber(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& box,
                                                                wchar_t const* unit);

    /// <summary>Giá trị NumberBox; NaN (ô trống) → <paramref name="fallback"/>.</summary>
    double NumberOr(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& box, double fallback);

    // ══════════ Danh sách log (§9.8, Main MA-36) ══════════

    /// <summary>
    /// LogWorker::LogList (mới nhất ở đầu) trong một ListView (SelectionMode None). Update() so LogWorker::Version, chỉ
    /// chèn dòng MỚI lên đầu (không dựng lại cả danh sách), cắt đuôi theo dung lượng LogWorker, giữ dòng đầu trong tầm
    /// nhìn. Dòng: vạch trái 3 px + giờ HH:mm:ss (Consolas 11, Tertiary) + nguồn (11, Secondary) + nội dung (12, wrap);
    /// màu theo mức như WPF (Warning vạch #E8A317 chữ #9D5D00; Error vạch #E94949 chữ #C42B1C đậm; Debug #A0A0A0/#707070).
    /// </summary>
    class LogListView
    {
    public:
        winrt::Microsoft::UI::Xaml::Controls::ListView Build();
        void Update();
        static winrt::Microsoft::UI::Xaml::FrameworkElement MakeRow(sca::LogData const& data);

    private:
        winrt::Microsoft::UI::Xaml::Controls::ListView m_list{ nullptr };
        std::deque<sca::LogData> m_shown;   // song song với Items, mới nhất ở đầu
        uint64_t m_version{ ~0ull };
    };

    // ══════════ Khung ảnh (Main CAM, Vision Edit) ══════════

    /// <summary>
    /// Ảnh của Vision (Gray8/BGR24/BGRA32) hiện vào một Image qua WriteableBitmap. Set() chỉ làm việc khi con trỏ ảnh
    /// ĐỔI; đổi định dạng chạy nền, ghi bitmap trên luồng UI; ảnh cũ về muộn bị bỏ. Phải sống lâu hơn việc nền: giữ
    /// trong shared_ptr (Create()).
    /// </summary>
    class ImageView : public std::enable_shared_from_this<ImageView>
    {
    public:
        static std::shared_ptr<ImageView> Create(winrt::Microsoft::UI::Xaml::Controls::Image const& image,
                                                 winrt::Microsoft::UI::Xaml::UIElement const& noImagePlaceholder = nullptr);
        /// <summary>true nếu ảnh ĐỔI (trang dùng để ghi "Captured hh:mm:ss").</summary>
        bool Set(std::shared_ptr<const sca::ImageData> image);
        std::shared_ptr<const sca::ImageData> Current() const { return m_src; }

        /// <summary>Gray8 / BGR24 / BGRA32 (có Stride) → BGRA8 liền dòng, alpha 255. False = ảnh hỏng.</summary>
        static bool ToBgra(sca::ImageData const& img, std::vector<uint8_t>& out);

    private:
        void Show(uint64_t generation, int width, int height, std::vector<uint8_t> const& bgra);

        winrt::Microsoft::UI::Xaml::Controls::Image m_image{ nullptr };
        winrt::Microsoft::UI::Xaml::UIElement m_placeholder{ nullptr };
        winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap m_bitmap{ nullptr };
        std::shared_ptr<const sca::ImageData> m_src;
        uint64_t m_generation{ 0 };
    };
}
