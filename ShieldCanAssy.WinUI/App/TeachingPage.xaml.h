#pragma once
#include "TeachingPage.g.h"

#include <memory>
#include <string>
#include <vector>

#include "Communication/IAxis.h"
#include "Data/Model/ModelData.h"
#include "Ui/Page.h"
#include "Ui/TeachingPageParts.h"

namespace winrt::ShieldCanAssy::implementation
{
    /// <summary>
    /// Trang Teaching (UI-SPEC §4) — bản WPF: TeachingView + TeachingViewModel.
    ///
    /// <para><b>Quy trình:</b> chọn tray (Input 1 / Input 2 / Assy) × Tool (1/2) → nhập Rows/Cols → jog X/Z/Y tới đúng ô
    /// bằng panel jog RÚT GỌN ngay trên trang (chỉ Jog −/+ và vị trí; Stop/Home/SVO/RST/Move ở lại trang Manual) → bấm P1,
    /// P2, P3, (tuỳ chọn) điểm kiểm chứng → lưới nút ô bên phải hiện thứ tự ghé từng ô, có Move chạy thử, hạ Z, Set Offset
    /// tinh chỉnh ô lệch và bù đặt (chỉ Assy).</para>
    ///
    /// <para><b>Điểm teach chỉ đọc, không cho gõ tay:</b> <c>TeachPoint</c> chỉ bật <c>Taught</c> khi tạo bằng constructor
    /// có toạ độ — gõ tay đổi được số nhưng không bật cờ, và Routine_Auto chặn Auto vĩnh viễn. Mọi nút Teach đọc encoder,
    /// hỏi lại (số CŨ và số MỚI, số mới đọc TRƯỚC khi hỏi) rồi mới ghi vào model đang mở; "Save" mới ghi xuống file.</para>
    ///
    /// <para>Không binding: OnTick đọc model/IO và chỉ đặt thuộc tính khi giá trị đổi. Lưới ô (m_cells) chỉ dựng lại trong
    /// Compute() — sau đổi tray/Tool, teach lại lưới, offset, bù đặt, nạp model — như Compute() của bản WPF; các ô nhập bù
    /// chỉ được nạp ở đó và khi đổi ô chọn (OnTick không bao giờ ghi đè số người dùng đang gõ).</para>
    /// </summary>
    struct TeachingPage : TeachingPageT<TeachingPage>, ui::IPage
    {
        TeachingPage() = default;

        void OnRootLoaded(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        void OnCoreReady() override;
        void OnTick() override;
        void OnShown(bool visible) override;
        /// <summary>--shot: ảnh 1 = Input tray 1 × Tool 1; 2 = Assy tray × Tool 2, ô (1,1) chọn (TOP CAM REF, Move CAM, bù
        /// đặt); 3 = như 2, cột trái cuộn giữa; 4 = Input tray 1, cột trái cuộn tới đáy (góc hút, cao độ, CAM1, cơ cấu kẹp).</summary>
        int ShotVariants() const override { return 4; }
        void ShowShotVariant(int index) override;

    private:
        static constexpr int AssyTraySel = 2;
        /// <summary>Ngưỡng lượng bù nhập tay — mm và độ.</summary>
        static constexpr double TrimLimitMm = 1.0;
        static constexpr double TrimLimitDeg = 5.0;

        /// <summary>Một hàng jog (X, Z1, Z2, Y1, Y2): ô vị trí + đơn vị cập nhật trong OnTick.</summary>
        struct JogAxisRow
        {
            std::string Name;
            std::shared_ptr<sca::IAxis> Axis;
            Microsoft::UI::Xaml::Controls::TextBlock Pos{ nullptr };
            Microsoft::UI::Xaml::Controls::TextBlock Unit{ nullptr };
        };

        /// <summary>Điểm của lưới teach: P00 / P0C / PR0 / điểm kiểm chứng.</summary>
        enum class GridPoint { P00, P0C, PR0, Verify };

        // ── dựng ──
        void WireEvents();
        void BuildJog();
        Microsoft::UI::Xaml::Controls::Button JogExtraButton(wchar_t const* text, double width, char const* tipKey);
        void SetTips();

        // ── tray / Tool ──
        bool UseInputTray() const { return m_selectedTray != AssyTraySel; }
        bool IsAssyTray() const { return !UseInputTray(); }
        std::shared_ptr<sca::TrayLayout> Layout() const;
        std::string CurrentZAxis() const;
        bool ShowCamMove() const { return IsAssyTray() && !m_useNozzle1; }
        std::string TrayText() const;
        std::string PickPlaceWord() const;
        std::string ZSlowLine(sca::TrayLayout const& layout, std::string const& zAxis) const;
        std::string HandEyeJigText() const;

        void SelectTray(int tray);
        void OnTraySwitched();
        void SetScanOrder(sca::EScanOrder order);
        void ApplySize();
        void SyncSelectors();
        void SyncScanRadios();

        // ── lưới ô ──
        void Compute();
        const ui::teaching::CellInfo* SelectedCell() const;
        void SelectCell(int row, int col);
        void LoadTrimBoxes();
        void LoadTrimAllBoxes();
        bool HasPlaceTrimAll() const;
        void UpdateCellUi();
        /// <summary>Mọi chữ lấy từ model (toạ độ đã teach, cao độ, góc, dòng kiểm chứng…) — gọi mỗi nhịp, SetText chỉ đổi
        /// khi khác.</summary>
        void RefreshTexts();
        void RefreshBusy();

        // ── teach (ghi vào model đang mở, sau khi hỏi lại) ──
        winrt::fire_and_forget TeachGridPoint(GridPoint which);
        winrt::fire_and_forget TeachZPick(std::string zAxis);
        winrt::fire_and_forget TeachZSlow(std::string zAxis);
        winrt::fire_and_forget TeachPickAngle(int nozzle);
        winrt::fire_and_forget TeachAlignPos(int nozzle);
        winrt::fire_and_forget TeachHandEyePick(int nozzle);
        winrt::fire_and_forget TeachHandEyeCamPos();
        winrt::fire_and_forget TeachCamTopDownAnchor();
        winrt::fire_and_forget SetOffset();
        void ClearOffset();
        void ApplyPlaceTrim();
        void ClearPlaceTrim();
        void ApplyPlaceTrimAll();
        void ClearPlaceTrimAll();
        bool TryParseTrim(Microsoft::UI::Xaml::Controls::NumberBox const& box, double limit, std::string const& what, double& value);

        // ── chạy thử (luồng nền, khoá Busy) ──
        void MoveToCellXY(std::string rowAxis, double y, double x, std::string label = {}, int pickNozzle = 0);
        void MoveZ(std::string axisName, double target, double slowAt = 0);
        void MoveToTrigger(int nozzle);
        void MoveToCell();
        void MoveZAtCell();
        void MoveCamToCell();
        void MoveToHandEyePick(int nozzle);
        void MoveToHandEyeCamPos();
        void MoveCamTopDownAnchor();
        void MoveZHome(std::string axisName);
        void MoveZPick(std::string axisName);
        void MoveZPlace(std::string axisName);
        /// <summary>Chạy <paramref name="work"/> trên luồng nền với cờ Busy; gỡ cờ trên luồng UI khi xong (kể cả lỗi).</summary>
        void RunBusy(std::function<void()> work);

        // ── khí nén / đèn ──
        void ToggleCamLamp(int bit);

        // ── model ──
        void RefreshModelList();
        void SelectModelInList();
        void LoadSelectedModel();
        void NewModel();
        void CopyModel();
        winrt::fire_and_forget DeleteModel();
        void SaveModel();

        bool m_loaded{ false };
        bool m_coreReady{ false };
        bool m_busy{ false };
        int m_busyShown{ -1 };

        int m_selectedTray{ 0 };      // 0 = Tray đầu vào 1, 1 = Tray đầu vào 2, 2 = Tray lắp ráp
        bool m_useNozzle1{ true };
        int m_rowsInput{ 4 };
        int m_colsInput{ 5 };
        bool m_suppressSelectors{ false };

        /// <summary>Model mà lưới ô đang hiện — đối tượng khác (nạp ở nơi khác, model mô phỏng) thì dựng lại.</summary>
        std::shared_ptr<sca::ModelData> m_model;

        std::vector<ui::teaching::CellInfo> m_cells;
        int m_selRow{ -1 };
        int m_selCol{ -1 };
        ui::teaching::CellGridView m_cellView;

        std::vector<std::string> m_modelNames;
        std::string m_selectedModelName;
        bool m_suppressModelSelection{ false };

        std::vector<JogAxisRow> m_jogRows;
        /// <summary>Các nút chạy tới vị trí của khu Jog (Go T1/T2, Origin/Go Pick/Go Place ×2) — [Busy-off].</summary>
        std::vector<Microsoft::UI::Xaml::Controls::Button> m_jogMoveButtons;
        int m_jogFastShown{ -1 };
        /// <summary>Nền xanh nhạt của P1/P2/P3 đang hiện (đã teach) — -1 = chưa vẽ.</summary>
        int m_stepTaughtShown[3]{ -1, -1, -1 };

        // DO thật đọc lại mỗi nhịp (_lampTimer của bản WPF) — tô màu nút và quyết định bật hay tắt.
        bool m_vac1On{ false }, m_vac2On{ false }, m_blow1On{ false }, m_blow2On{ false };
        bool m_cam1LampOn{ false }, m_cam2LampOn{ false };
    };
}

namespace winrt::ShieldCanAssy::factory_implementation
{
    struct TeachingPage : TeachingPageT<TeachingPage, implementation::TeachingPage>
    {
    };
}
