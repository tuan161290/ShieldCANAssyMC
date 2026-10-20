#pragma once
// Card tray của trang Main (UI-SPEC §2.3, §9.1) — thay TrayGridTemplate + TrayGridViewModel của bản WPF.
//
//   ┌ TITLE                         [Clear (3)]   4 × 5 — cell 7/20 ┐
//   │ Tray 1                                                        │  (caption chỉ khi slots > 1)
//   │ ┌──┬──┬──┐ ô "r-c", màu theo TrayCellLook, ô hiện tại quầng xanh │
//   │ Tray 2 · not taught  (lưới mờ 0.45)                            │
//   ├───────────────────────────────────────────────────────────────┤
//   │ [Clear Tray]                │ OK 12   NG 1   Total 13          │
//   └───────────────────────────────────────────────────────────────┘
//
// Lưới vẽ theo VỊ TRÍ HÌNH HỌC của ô (hàng trên xuống, trái sang), không theo thứ tự ghé: ô ở (r, c) của khối t là
// tray.IndexOf(t·RowsPerTray + r, c). Chỉ dựng lại ô khi RowsPerTray/Cols/ScanOrder đổi; còn lại chỉ đổi màu ô đổi.
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
        /// Đồng thời cập nhật nút "Clear (n)" (ẩn khi không chọn ô nào), dòng tóm tắt và chữ tên từng khối.
        /// </summary>
        void Sync(const sca::TrayState& tray, const std::vector<int>* scanned, bool canSelect);

        /// <summary>Bộ đếm của card (Total = ok + ng).</summary>
        void SetCounts(int ok, int ng);

        /// <summary>Chạm một ô lúc canSelect → McControl::ToggleCellSelection(input, index). Trang Sync lại ở nhịp kế.</summary>
        std::function<void(int index)> OnCellTapped;
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
            winrt::Microsoft::UI::Xaml::Controls::Grid Root{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::Border Glow{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::Border Fill{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::Border Selection{ nullptr };
            int ShownLook{ -1 };
            int ShownCurrent{ -1 };
            int ShownSelected{ -1 };
            int ShownOut{ -1 };
        };
        struct Group
        {
            winrt::Microsoft::UI::Xaml::Controls::TextBlock Caption{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::Grid Cells{ nullptr };
            std::vector<Cell> Items;
            int ShownTaught{ -1 };
            int ShownActive{ -1 };
        };

        void Rebuild(const sca::TrayState& tray, int rowsPerTray);

        int m_slots{ 1 };
        bool m_canSelect{ false };
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
