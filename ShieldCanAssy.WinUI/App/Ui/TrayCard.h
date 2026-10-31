#pragma once
// Card tray của trang Main (UI-SPEC §2.3, §9.1) — thay TrayGridTemplate + TrayGridViewModel của bản WPF.
//
//   ┌ TITLE                         [Clear (3)]   4 × 5 — cell 7/20 ┐
//   │ Tray 1                                                        │  (caption chỉ khi slots > 1)
//   │ ┌──┬──┬──┐ ô "r-c", màu theo TrayCellLook, ô hiện tại quầng xanh (chỉ khi chu trình đang dở), ô chọn viền 2 px │
//   │ Tray 2 · not taught  (lưới mờ 0.45)                            │
//   ├───────────────────────────────────────────────────────────────┤
//   │ [Clear Tray]                │ OK 12   NG 1   Total 13          │
//   └───────────────────────────────────────────────────────────────┘
//
// Lưới vẽ theo VỊ TRÍ HÌNH HỌC của ô (hàng trên xuống, trái sang), không theo thứ tự ghé: ô ở (r, c) của khối t là
// tray.IndexOf(t·RowsPerTray + r, c). Chỉ dựng lại ô khi RowsPerTray/Cols/ScanOrder đổi; còn lại chỉ đổi màu ô đổi.
//
// Chọn ô (lúc canSelect): chạm một ô = bật/tắt ô đó. Nhấn giữ một ô rồi kéo sang ô khác = cả vùng chữ nhật giữa hai ô
// (Tuấn 2026-10-30): ô bắt đầu chưa chọn → chọn cả vùng (chỉ ô chọn được), đã chọn → bỏ chọn cả vùng. Lúc kéo, viền
// chọn hiện trước kết quả; nhả mới áp. Vùng tính theo hàng của sổ nên kéo được từ khối Tray 1 sang Tray 2.
#include <functional>
#include <string>
#include <vector>

namespace sca { struct TrayState; }

namespace ui
{
    /// <summary>= ETrayCellLook của WPF: ECellState gộp với cờ "đã quét trước" (ô trống của tray lắp ráp đã soi OK).</summary>
    enum class TrayCellLook { Empty, Scanned, Loaded, Processing, Ok, Ng, Skipped };

    /// <summary>Brush key của Styles.xaml cho một look (TrayCellEmptyBrush…).</summary>
    wchar_t const* TrayCellBrushKey(TrayCellLook look);

    class TrayCard
    {
    public:
        TrayCard() = default;
        TrayCard(const TrayCard&) = delete;
        TrayCard& operator=(const TrayCard&) = delete;

        /// <summary>Dựng card. slots: 2 = tray đầu vào (Tray 1 trên Tray 2), 1 = tray lắp ráp. Handler của control giữ
        /// <c>this</c>: đối tượng phải nằm yên (thành viên của trang) suốt đời card.</summary>
        winrt::Microsoft::UI::Xaml::FrameworkElement Build(std::wstring const& title, int slots);

        /// <summary>
        /// Một nhịp OnTick. <paramref name="scanned"/>: chỉ số ô đã quét trước (Routine_Auto::AssyScannedCells — chỉ tray
        /// lắp ráp; nullptr = không có). <paramref name="canSelect"/>: chạm ô có tác dụng (Status.Mode == Auto).
        /// <paramref name="showCurrent"/>: quầng xanh ở ô CurrentIndex — trang chỉ bật khi chu trình đang dở.
        /// Đồng thời cập nhật nút "Clear (n)" (ẩn khi không chọn ô nào), dòng tóm tắt và chữ tên từng khối.
        /// </summary>
        void Sync(const sca::TrayState& tray, const std::vector<int>* scanned, bool canSelect, bool showCurrent);

        /// <summary>Bộ đếm của card (Total = ok + ng).</summary>
        void SetCounts(int ok, int ng);

        /// <summary>Chạm một ô lúc canSelect → McControl::ToggleCellSelection(input, index). Trang Sync lại ở nhịp kế.</summary>
        std::function<void(int index)> OnCellTapped;
        /// <summary>Kéo chọn vùng xong → McControl::SetCellsSelected(input, indices, select). indices: mọi ô trong vùng.</summary>
        std::function<void(std::vector<int> const& indices, bool select)> OnCellsDragged;
        /// <summary>McControl::CanSelectCell(input, index) — lúc kéo chọn, chỉ ô chọn được mới hiện viền xem trước.</summary>
        std::function<bool(int index)> CanSelectCell;
        /// <summary>Nút "Clear (n)" → McControl::ClearCellSelection(input).</summary>
        std::function<void()> OnClearSelection;
        /// <summary>Nút "Clear Tray" → McControl::ReloadInputTray() / ResetAssyTray(). Không hỏi xác nhận (như WPF).</summary>
        std::function<void()> OnReload;

        winrt::Microsoft::UI::Xaml::Controls::Button ReloadButton() const { return m_reload; }
        winrt::Microsoft::UI::Xaml::Controls::Button ClearSelectionButton() const { return m_clearSel; }

    private:
        struct Cell
        {
            int Index{ -1 };
            int Row{ -1 };   // hàng trong sổ (t·RowsPerTray + r) — vùng kéo chọn
            int Col{ -1 };
            winrt::Windows::Foundation::Rect Bounds{};   // trong m_body, chụp lúc nhấn (layout không đổi lúc kéo)
            bool Selectable{ false };                    // CanSelectCell, chụp lúc nhấn
            winrt::Microsoft::UI::Xaml::Controls::Grid Root{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::Border Glow{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::Border Fill{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::Border Selection{ nullptr };
            int ShownLook{ -1 };
            int ShownCurrent{ -1 };
            int ShownSelected{ -1 };
        };
        struct Group
        {
            winrt::Microsoft::UI::Xaml::Controls::TextBlock Caption{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::Grid Cells{ nullptr };
            std::vector<Cell> Items;
            int ShownTaught{ -1 };
            int ShownActive{ -1 };
        };

        /// <summary>Một lần nhấn giữ trên lưới. Anchor/End trỏ vào m_groups — dựng lại lưới thì CancelDrag trước.</summary>
        struct Drag
        {
            bool Active{ false };
            uint32_t PointerId{ 0 };
            const Cell* Anchor{ nullptr };
            const Cell* End{ nullptr };
            bool Add{ false };   // ô bắt đầu chưa chọn → kéo là chọn
        };

        void Rebuild(const sca::TrayState& tray, int rowsPerTray);
        void OnPressed(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnMoved(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnReleased(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        /// <summary>Ô có trong sổ dưới điểm <paramref name="p"/> (toạ độ m_body, theo Bounds lúc nhấn); nullptr ở khe/ngoài.</summary>
        const Cell* CellAt(winrt::Windows::Foundation::Point p) const;
        bool InDragRect(const Cell& cell) const;
        /// <summary>Bỏ lần kéo đang dở (không áp) — đổi chế độ, dựng lại lưới, mất bắt chuột.</summary>
        void CancelDrag();
        /// <summary>Viền chọn của mọi ô: vùng đang kéo theo Drag.Add, ô khác theo m_selected.</summary>
        void RefreshSelection();

        int m_slots{ 1 };
        bool m_canSelect{ false };
        int m_cellCount{ 0 };          // TrayState.CellCount() của nhịp Sync gần nhất — khối tray chưa có trong sổ nằm ngoài
        std::vector<int> m_selected;   // TrayState.Selected() của nhịp Sync gần nhất, đã sắp xếp
        Drag m_drag;
        int m_builtRows{ -1 };
        int m_builtCols{ -1 };
        int m_builtOrder{ -1 };

        winrt::Microsoft::UI::Xaml::Controls::Grid m_body{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_summary{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Button m_clearSel{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Button m_reload{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_ok{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_ng{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_total{ nullptr };
        std::vector<Group> m_groups;
    };
}
