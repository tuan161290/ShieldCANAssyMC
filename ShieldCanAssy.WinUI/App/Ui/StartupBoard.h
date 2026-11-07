#pragma once
// Dashboard khởi động (Tuấn 2026-11-02: "hiển thị tiến trình và log khởi động, hoàn thành rồi mới hiển thị giao diện cuối
// cùng"). Phủ kín cửa sổ từ khung hình đầu tiên; shell và các trang vẫn dựng và tính bố cục bên dưới, nên lúc đóng dashboard
// giao diện đã sẵn sàng. Bố cục tĩnh nằm trong MainWindow.xaml, các dòng thiết bị dựng bằng code theo sca::StartupProgress.
//
// Xong hết không lỗi: tự đóng. Có lỗi: đứng lại, chờ người xem rồi bấm Continue. Quá lâu mà Core đã sẵn sàng: vẫn chờ tiếp
// nhưng cho Continue. McControl::Init chưa xong thì không có Continue — các trang chưa được đọc thiết bị (MainWindow::OnCoreReady).
#include <chrono>
#include <string>
#include <vector>

#include "Infor/StartupProgress.h"
#include "Ui/CamViews.h"
#include "Ui/Controls.h"

namespace ui
{
    class StartupBoard
    {
    public:
        /// <summary>Các phần tử có sẵn trong MainWindow.xaml.</summary>
        struct Parts
        {
            winrt::Microsoft::UI::Xaml::Controls::TextBlock Title{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::TextBlock Percent{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::TextBlock Elapsed{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::ProgressBar Bar{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::TextBlock Current{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::Button Continue{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::StackPanel Steps{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::Border LogHost{ nullptr };
        };

        void Build(Parts parts);

        /// <summary>Mỗi nhịp lúc dashboard còn hiện. True = đóng được rồi.</summary>
        bool Update(bool coreReady, CamStartupState const& cam);

        /// <summary>Người vận hành bấm Continue: nhịp kế tiếp Update trả true.</summary>
        void Continue() { m_continued = true; }

        bool SolutionFailed() const { return m_solutionFailed; }
        double ElapsedSeconds() const;
        int FailedCount() const { return m_failed; }

    private:
        struct Row
        {
            winrt::Microsoft::UI::Xaml::Shapes::Ellipse Pending{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::ProgressRing Ring{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::FontIcon Icon{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::TextBlock Name{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::TextBlock Detail{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::TextBlock Time{ nullptr };
            int Shown{ -1 };
        };

        void BuildRows(std::vector<sca::StartupStep> const& steps);
        static void UpdateRow(Row& row, sca::StartupStep const& step);
        void ShowProgress(int percent);
        void ShowOutcome(winrt::hstring const& title, std::string const& line, wchar_t const* barBrush, bool canContinue);

        Parts m_parts;
        std::vector<Row> m_rows;
        LogListView m_log;
        std::chrono::steady_clock::time_point m_started{};
        std::chrono::steady_clock::time_point m_readyAt{};
        bool m_ready{ false };
        bool m_camBegun{ false };
        bool m_camEnded{ false };
        bool m_continued{ false };
        bool m_solutionFailed{ false };
        int m_failed{ 0 };
    };
}
