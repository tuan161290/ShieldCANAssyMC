#pragma once
// Phần dựng bằng code của trang Teaching (UI-SPEC §4): lưới nút ô tray (TE-43) và hình minh hoạ thứ tự quét (TE-14).
// Tách khỏi TeachingPage.xaml.cpp để trang chỉ còn logic của TeachingViewModel.
#include <functional>
#include <string>
#include <vector>

namespace ui::teaching
{
    /// <summary>Một ô trong bảng toạ độ tính ra — TeachCellRow của bản WPF (thuần dữ liệu).</summary>
    struct CellInfo
    {
        /// <summary>0 = Tray đầu vào 1, 1 = Tray đầu vào 2, 2 = Tray lắp ráp (TeachingPage::AssyTraySel).</summary>
        int Tray = 0;
        int Row = 0;
        int Col = 0;
        double X = 0;
        double Y = 0;
        double Dx = 0;
        double Dy = 0;
        /// <summary>Bù đặt của ô (chỉ Assy Tray).</summary>
        double PlaceDx = 0;
        double PlaceDy = 0;
        double PlaceDa = 0;
        /// <summary>Thứ tự máy ghé ô này khi chạy Auto (1-based) — khác vị trí hình học khi ScanOrder khác RowMajor.</summary>
        int VisitOrder = 0;

        /// <summary>"(r, c)" 1-based.</summary>
        std::string Cell() const;
        bool HasOffset() const;
        bool HasPlaceTrim() const;
    };

    /// <summary>
    /// Lưới nút ô (UniformGrid Rows×Cols của bản WPF): số thứ tự ghé (14 Bold) + chấm "●" (8, Accent) khi ô có bù đặt
    /// riêng; nền VisionOk khi ô có offset teach; ô đang chọn viền Accent 2.5. Chỉ dựng lại nút khi đổi kích thước lưới,
    /// còn lại cập nhật tại chỗ.
    /// </summary>
    class CellGridView
    {
    public:
        void Attach(winrt::Microsoft::UI::Xaml::Controls::Grid const& grid);
        /// <summary>Chỉ vẽ các ô thuộc <paramref name="tray"/> — trang Teaching giữ ô của cả hai tray đầu vào trong một
        /// danh sách, mỗi tray một lưới. selRow/selCol = -1 khi ô đang chọn nằm ở tray khác.</summary>
        void Update(std::vector<CellInfo> const& cells, int tray, int rows, int cols, int selRow, int selCol);
        /// <summary>Bề ngang khung cuộn đổi: lưới giãn đầy bề ngang, nhưng mỗi cột không hẹp hơn 44 px (nút 40 + lề) — hẹp
        /// hơn thì cuộn ngang.</summary>
        void FitWidth(double viewportWidth);

        std::function<void(int row, int col)> OnCellTapped;

    private:
        struct CellView
        {
            winrt::Microsoft::UI::Xaml::Controls::Button Button{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::TextBlock Order{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::TextBlock Dot{ nullptr };
            int Order_{ -1 };
            int Look{ -1 };   // bit0 offset, bit1 trim, bit2 selected
        };

        void Rebuild(int rows, int cols);

        winrt::Microsoft::UI::Xaml::Controls::Grid m_grid{ nullptr };
        std::vector<CellView> m_views;
        int m_rows{ -1 };
        int m_cols{ -1 };
        double m_viewport{ 0 };
    };

    /// <summary>
    /// Minh hoạ thứ tự quét trên lưới mẫu CỐ ĐỊNH 2×4 (không theo Rows/Cols thật — chỉ để hình dung nhanh hai chế độ cạnh
    /// nút chọn): ô xám, số thứ tự 1–8, mũi tên SteelBlue nối các ô liên tiếp theo thứ tự ghé (rút ngắn hai đầu để không đè
    /// lên số, đầu mũi tên 7 px). Vẽ theo kích thước thật của Canvas — gọi lại khi SizeChanged.
    /// </summary>
    void DrawTraversal(winrt::Microsoft::UI::Xaml::Controls::Canvas const& canvas, bool zigzag);
}
