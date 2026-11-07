#pragma once
#include "SettingPage.g.h"

#include "Data/Model/ModelData.h"
#include "SysConfig/AppUpdate.h"

#include "Ui/Page.h"
#include "Ui/SettingPageFields.h"
#include "Ui/SettingPageUi.h"

namespace winrt::ShieldCanAssy::implementation
{
    /// <summary>
    /// Trang Setting (UI-SPEC §5) — port từ SettingView/SettingViewModel của bản WPF: chế độ Auto (tay hút, chạy rà),
    /// tham số hệ thống, vision (solution, công cụ biến toàn cục / Output Flow, ngưỡng, tên Flow), cấu hình 7 trục + IP
    /// Ezi-IO, thời gian chờ, nhật ký. Mỗi phần một mục ở NavigationView trái; khối Lưu system.json ở PaneFooter.
    ///
    /// <para>Ô nhập gắn thẳng vào dữ liệu sống (SystemData / AxisConfig) — ghi khi rời ô, như binding của bản WPF; riêng
    /// Z_SAFE là giá trị riêng của trang, chỉ áp khi bấm Save System (như ViewModel cũ). Chọn tay hút / chạy rà và các nút
    /// solution có hiệu lực ngay, không qua Lưu. OnTick chỉ cập nhật phần chỉ đọc và trạng thái nút.</para>
    /// </summary>
    struct SettingPage : SettingPageT<SettingPage>, ui::IPage
    {
        SettingPage() = default;

        void OnRootLoaded(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSectionChanged(Microsoft::UI::Xaml::Controls::NavigationView const&,
                              Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const&);

        // ── Khối Lưu ──
        void OnSaveClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnOpenFolderClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        // ── Chế độ Auto ──
        void OnNozzleClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnDryRunClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnNgStopClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        // ── Phân quyền ──
        void OnChangePasswordClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        // ── Cập nhật qua GitHub (Tuấn 2026-11-07) ──
        void OnCheckUpdateClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        winrt::fire_and_forget OnInstallUpdateClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        // ── Vision ──
        void OnSolutionPathChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
        winrt::fire_and_forget OnBrowseSolutionClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnReloadSolutionClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnCheckVarsClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnCreateVarsClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        winrt::fire_and_forget OnCheckOutputsClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        // ── ui::IPage ──
        void OnCoreReady() override;
        void OnTick() override;
        void OnShown(bool visible) override;
        int ShotVariants() const override { return SectionCount; }
        void ShowShotVariant(int index) override { SelectSection(index); }

    private:
        enum { SectionSystem = 0, SectionVision, SectionServo, SectionDelay, SectionLog, SectionUsers, SectionCount };

        void SelectSection(int section);
        void SetupNumberBoxes();
        void BindFields();
        void BuildAxisTable();
        void LoadAll(bool force);
        void LoadZSafe();
        void UpdateStaticInfo();
        void UpdateAutoMode();
        void UpdateCommandStates();
        /// <summary>Card USER ACCESS — chỉ vẽ lại khi UserAccess::Version đổi.</summary>
        void UpdateUserAccess(bool force = false);
        void SetupIdleLogoutBox();

        /// <summary>Ghi mọi ô đã sửa vào dữ liệu sống trước Lưu / Nạp lại solution. Còn ô sai thì cảnh báo (kèm hành
        /// động bị từ chối) và trả false.</summary>
        bool CommitEdits(bool includeZSafe, const std::string& action);

        void ShowSaveResult(bool ok, const std::string& text);

        /// <summary>Card SOFTWARE UPDATE: số đang chạy, nút Update theo cấp đăng nhập và bản vừa hỏi được.</summary>
        void UpdateUpdateCard();
        void ShowLatestRelease();
        void SetUpdateStatus(const std::string& text, bool error = false);
        /// <summary>Lý do chưa cài được lúc này (máy đang chạy / chưa dừng ở MANUAL); rỗng = cài được.</summary>
        static std::string UpdateBlockReason();
        void SetReport(Microsoft::UI::Xaml::Controls::TextBlock const& block, const std::string& text);
        winrt::fire_and_forget RunGlobalVarSync(bool apply);

        bool m_loaded{ false };
        bool m_coreReady{ false };
        /// <summary>Model của lần nạp ô Z_SAFE gần nhất — đổi model ở Teaching thì mở lại trang là thấy Z_SAFE của model mới.</summary>
        std::shared_ptr<sca::ModelData> m_zSafeModel;
        /// <summary>Release mới nhất hỏi được ở lần bấm Check gần nhất; đang hỏi / tải thì m_updateBusy.</summary>
        sca::ReleaseInfo m_latest;
        bool m_hasLatest{ false };
        bool m_updateBusy{ false };
        int m_section{ SectionSystem };
        /// <summary>Đang chạy Check Vars / Create Vars / Check Outputs — không cho chạy chồng.</summary>
        bool m_visionBusy{ false };

        ui::settingpage::FieldGroup m_texts;         // ô chữ: IP, tên Flow/khung ảnh
        ui::settingpage::NumberToggleGroup m_numbers; // NumberBox + ToggleSwitch của SystemData
        ui::settingpage::FieldGroup m_axisFields;    // bảng 7 trục
        ui::settingpage::SimpleTable m_axisTable;

        ui::settingpage::LogTable m_log;

        // Trạng thái đã vẽ (OnTick chỉ ghi khi đổi).
        int m_shownNozzle{ -1 };
        int m_shownDryRun{ -1 };
        int m_shownNgStop{ -1 };
        uint64_t m_shownUserVersion{ 0 };
        bool m_syncingIdle{ false };   // đang đặt ô Auto logout từ code — bỏ qua ValueChanged
    };
}

namespace winrt::ShieldCanAssy::factory_implementation
{
    struct SettingPage : SettingPageT<SettingPage, implementation::SettingPage>
    {
    };
}
