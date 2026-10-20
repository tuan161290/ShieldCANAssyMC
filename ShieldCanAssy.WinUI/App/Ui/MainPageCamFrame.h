#pragma once
// Khung CAM của trang Main (UI-SPEC §2.4, §9.5 CamFrame + ReadoutTable) — thay ba card CAM của MainView.xaml:
//
//   ┌ [☰] BOTTOM CAM — #1          <ghi chú lần soi, cam>   Shot 10:02:11 ┐
//   │ Cell (3,4)   X      Y      θ                                       │  bảng số cao 70 (ReadoutTemplate) — ba khung
//   │ Ofs        0.012  -0.030  0.15                                     │  cùng cao nên ba ảnh bên dưới thẳng hàng
//   │ ┌────────────────────────────────────────┐                         │
//   │ │ VmRenderControl (máy thật) / ảnh gần nhất│                         │
//   │ └────────────────────────────────────────┘                         │
//   └────────────────────────────────────────────────────────────────────┘
//
// Dựng bằng code, giữ control và giá trị đã hiện; Set* chỉ đụng control khi giá trị ĐỔI — gọi thẳng trong OnTick được.
// Phần VisionMaster (Pane, BoundKey…) do trang điều khiển từ việc đã xếp hàng (ui::QueueVmWork), xem MainPage.xaml.cpp.
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "Ui/Controls.h"
#include "Ui/VmPane.h"
#include "Utility/DateTimeText.h"

namespace sca { struct ReadoutCard; struct ImageData; }

namespace ui
{
    class MainCamFrame
    {
    public:
        MainCamFrame() = default;
        MainCamFrame(const MainCamFrame&) = delete;
        MainCamFrame& operator=(const MainCamFrame&) = delete;

        /// <summary>Dựng card. Handler của control giữ <c>this</c>: đối tượng phải nằm yên (thành viên của trang).</summary>
        winrt::Microsoft::UI::Xaml::FrameworkElement Build(std::wstring const& title);

        /// <summary>
        /// Bảng số (ReadoutTemplate của WPF). <paramref name="card"/> không null (trang thay bằng bảng "Not triggered" khi
        /// chưa soi); bảng chỉ dựng lại khi con trỏ đổi — ReadoutCard bất biến, mỗi lần soi một bản mới.
        /// <paramref name="fullText"/>: bản chữ đầy đủ làm tooltip của cả bảng.
        /// </summary>
        void SetReadout(std::shared_ptr<const sca::ReadoutCard> const& card, std::string const& fullText);
        std::shared_ptr<const sca::ReadoutCard> const& Readout() const { return m_card; }

        /// <summary>Ảnh dự phòng (LastBottomUpImageTool1/2, LastTopDownImage). Ảnh MỚI (so con trỏ) thì ghi giờ chụp
        /// "Shot HH:mm:ss" = lúc phát hiện, như MainViewModel.SyncImage.</summary>
        void SetImage(std::shared_ptr<const sca::ImageData> const& image);

        /// <summary>Hàng tiêu đề bảng được chạm (chỉ khi bảng CanToggle) — trang đổi Ofs/Tgt ↔ bảng tách Đích.</summary>
        std::function<void()> OnReadoutHeaderTapped;
        /// <summary>Nút ☰ ở góc tiêu đề — ẩn/hiện thanh công cụ của VmRenderControl.</summary>
        std::function<void()> OnToolbarClicked;

        winrt::Microsoft::UI::Xaml::Controls::Border RenderHost() const { return m_host; }
        winrt::Microsoft::UI::Xaml::Controls::Button ToolbarButton() const { return m_toolbar; }
        /// <summary>Dòng lý do khi không tạo được control VisionMaster (máy chưa đăng ký ActiveX…).</summary>
        void ShowVmError(std::string const& text);

        // ── VisionMaster (chỉ chạm từ việc đã xếp hàng, ngoài sự kiện XAML) ──
        VmPane Pane;
        /// <summary>"lần nạp|đường dẫn" đã gắn thành công; rỗng = chưa gắn.</summary>
        std::string BoundKey;
        /// <summary>Thanh công cụ của khung đang hiện — mặc định ẩn hết (Tuấn 2026-10-07).</summary>
        bool ToolbarShown{ false };
        /// <summary>Popup đang hiện (1) / ẩn (0); -1 = chưa đặt.</summary>
        int PaneShown{ -1 };
        /// <summary>Khoá của lần gắn hỏng đã ghi log (không ghi lặp mỗi giây).</summary>
        std::string FailLoggedKey;
        /// <summary>Khoá của lần bật callback kết quả hỏng đã ghi log.</summary>
        std::string CallbackFailLoggedKey;

    private:
        void RebuildTable();

        winrt::Microsoft::UI::Xaml::Controls::Button m_toolbar{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_note{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_shot{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Grid m_readoutHost{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_readoutNote{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::StackPanel m_table{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Image m_image{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_noImage{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_vmText{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Border m_host{ nullptr };
        std::shared_ptr<ImageView> m_imageView;

        std::shared_ptr<const sca::ReadoutCard> m_card;
        std::string m_tooltipText;
        bool m_tooltipSet{ false };
        /// <summary>Lúc thấy ảnh đang hiện lần đầu; nullopt = không có ảnh.</summary>
        std::optional<sca::TimePoint> m_shotAt;
    };
}
