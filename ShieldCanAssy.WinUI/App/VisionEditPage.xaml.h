#pragma once
#include "VisionEditPage.g.h"

#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

#include "Controller/Routine_Calib.h"
#include "Ui/Page.h"
#include "Ui/VmPane.h"

namespace winrt::ShieldCanAssy::implementation
{
    /// <summary>
    /// Trang Vision Edit (UI-SPEC §6) — port của <c>VisionEditView</c> + <c>VisionCalibViewModel</c> (WPF): thanh Jog toàn bề
    /// ngang, khung soạn Flow của VisionMaster (VmGlobalToolControl + VmMainViewConfigControl, popup ActiveX qua ui::VmPane)
    /// và cột hiệu chuẩn vision.
    ///
    /// <para><b>Phép hiệu chuẩn nằm trong VisionMaster, không nằm ở đây</b> (Tuấn chốt 2026-09-12). Các nút chỉ nạp điểm,
    /// chạy trục cho các phép kiểm tra và đo hand-eye; logic và trạng thái giữa các nút ở <c>sca::Routine_Calib</c>. Mọi
    /// chuỗi chạy trên luồng nền với <c>McControl::ManualToken()</c> (STOP huỷ được).</para>
    ///
    /// <para><b>Busy</b> (cờ <c>Busy</c> của view-model): một việc một lúc; trong lúc chạy cả cột hiệu chuẩn, jog, Z Safe và
    /// van hút bị khoá, STOP thì không bao giờ. Chặn LÚC CHẠY bằng hộp thoại có lý do — không dùng nút xám theo trạng thái
    /// routine (như bản WPF).</para>
    /// </summary>
    struct VisionEditPage : VisionEditPageT<VisionEditPage>, ui::IPage
    {
        VisionEditPage() = default;

        void OnRootLoaded(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        // ── thanh Jog ──
        void OnVac1Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnVac2Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnJogSlowClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnJogFastClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnStopAllClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        // ── cột hiệu chuẩn: luôn hiện ──
        void OnCheckCalibClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnToolChanged(Microsoft::UI::Xaml::Controls::SelectorBar const&, Microsoft::UI::Xaml::Controls::SelectorBarSelectionChangedEventArgs const&);
        void OnStepChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);

        // ── nút bấm một lần ──
        void OnSaveCenterClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnTopLoadHereClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnAlignListLostFocus(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnHeCellClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSeq2Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        // ── ui::IPage ──
        void OnCoreReady() override;
        void OnTick() override;
        void OnShown(bool visible) override;
        void OnWindowMoved() override;
        void OnModalChanged(bool modalOpen) override;
        void OnClosing() override;
        /// <summary>--shot chụp cả 6 bước của cột hiệu chuẩn.</summary>
        int ShotVariants() const override { return 6; }
        void ShowShotVariant(int index) override;

    private:
        using StepBody = std::function<sca::StepRunResult(sca::Routine_Calib&, int nozzle, std::stop_token)>;
        /// <summary>Ô kết quả của một nút chạy-từng-bước: khu chạy kiểm (StepResultText) hay khu hand-eye (HandEyeStepText).</summary>
        enum class Report { Step, HandEye };

        // ── dựng ──
        void BuildJogGrid();
        void BuildNumpad();
        void BuildNumberBoxes();
        void BuildStepCombo();
        void ApplyStaticTexts();
        void AttachHandlers();
        /// <summary>Nạp ô nhập từ SystemData (đầu trang và mỗi lần trang hiện — trang Setting có thể vừa đổi).</summary>
        void LoadInputs();
        /// <summary>Ghi hai ô danh sách bottom-up vào SystemData (WPF: binding cập nhật khi mất focus).</summary>
        void CommitAlignLists();

        // ── trạng thái ──
        void SelectTool(int tool);
        void SelectStep(int step);
        void SetBusy(bool busy);
        void ApplyBusy();
        void SetSequenceStatus(std::string text);
        void SetVerifyResult(std::string text);
        void SetHandEyeStep(std::string text);
        void SetStepResult(std::string text);
        void SetGate(std::string text);
        /// <summary>RefreshGate của view-model: chạy lại cổng hiệu chuẩn (chỉ đọc model) rồi hiện kết quả.</summary>
        void RefreshGate();
        /// <summary>Các dòng chữ tính từ model/SystemData (cổng gần nhất, Align, hand-eye, tâm lưới) — mỗi nhịp, chỉ khi đổi.</summary>
        void UpdateCalibTexts();
        std::string HandEyeLocateLabel() const;
        bool OrgOk() const;

        // ── chạy ──
        /// <summary>
        /// Một việc dài: đặt Busy, chạy <paramref name="work"/> trên luồng nền (nhận Routine_Calib + ManualToken mới),
        /// xong thì gỡ Busy và chạy <paramref name="done"/> trên luồng UI. Exception (kể cả OperationCanceled) được bắt và
        /// đưa vào <paramref name="done"/> dưới dạng chuỗi lỗi (rỗng = không lỗi).
        /// </summary>
        void RunJob(std::function<void(sca::Routine_Calib&)> work, std::function<void(VisionEditPage&, std::string const&)> done);
        /// <summary>StepRunAsync của view-model: Busy → bỏ qua; chưa về gốc → Warn Vision.NoOrigin; chạy; dòng kết quả vào ô của
        /// <paramref name="report"/>; thất bại → Warn Vision.StepStopped. <paramref name="after"/> chạy khi đã có kết quả.</summary>
        void StepRun(std::string title, StepBody body, Report report, int nozzle,
                     std::function<void(VisionEditPage&, sca::StepRunResult const&)> after = {});
        void HandEyeStep(std::string title, StepBody body);
        /// <summary>Post lên luồng UI với tham chiếu yếu tới trang (gọi được từ luồng nền).</summary>
        void PostToPage(std::function<void(VisionEditPage&)> fn);

        // ── 6.1 ──
        void MoveZSafe(int nozzle);
        // ── 6.4 top-down ──
        bool CheckReadyForGrid();
        void RunTopDownGridPoint(int number);
        winrt::fire_and_forget RunAutoCalib();
        void VerifyTopDown();
        // ── 6.5 bottom-up ──
        winrt::fire_and_forget RunAlignCalibSequence();
        void TriggerAlignCalibFlow();
        void MoveAlignAxisToZero();
        // ── 6.6 rotation ──
        winrt::fire_and_forget VerifyAlignRotation();
        void RunRotationTest(int tool, bool moveX, double alignX);
        // ── 6.7 hand-eye ──
        winrt::fire_and_forget HandEyePickWithConfirm();
        winrt::fire_and_forget HandEyeReturnWithConfirm();
        void HandEyeLocateHere();
        void HandEyeCommit();
        // ── 6.8 ──
        winrt::fire_and_forget DryRunPlaceAlign();
        // ── 6.9 ──
        winrt::fire_and_forget StepPick();
        void StepAlignBottom();
        winrt::fire_and_forget StepMoveToCommand();
        int SeqValue(Microsoft::UI::Xaml::Controls::NumberBox const& box, int& field);

        // ── VisionMaster ──
        void QueueVmWork();
        void EnsureVmPanes();
        void BindProcedures();

        bool m_loaded{ false };
        bool m_coreReady{ false };
        bool m_visible{ false };
        bool m_closing{ false };
        bool m_busy{ false };
        bool m_appliedBusy{ true };   // ≠ m_busy lúc đầu để ApplyBusy lần đầu luôn ghi
        bool m_syncing{ false };      // đang đặt control bằng code — bỏ qua sự kiện đổi giá trị
        int m_tool{ 1 };
        int m_step{ 0 };

        std::vector<Microsoft::UI::Xaml::Controls::Button> m_jogButtons;
        Microsoft::UI::Xaml::Controls::NumberBox m_gridStepXBox{ nullptr };
        Microsoft::UI::Xaml::Controls::NumberBox m_gridStepYBox{ nullptr };
        Microsoft::UI::Xaml::Controls::NumberBox m_verifyTravelBox{ nullptr };
        Microsoft::UI::Xaml::Controls::NumberBox m_testAngleBox{ nullptr };
        Microsoft::UI::Xaml::Controls::TextBlock m_he6Caption{ nullptr };
        Microsoft::UI::Xaml::Controls::Flyout m_cellFlyout{ nullptr };

        // Van hút đọc lại từ DO (WPF: timer 150 ms) — nút Vac đảo theo giá trị đang HIỆN.
        bool m_vac1On{ false };
        bool m_vac2On{ false };

        // Chữ do sự kiện đặt (rỗng/nullopt = câu chờ theo Loc, như các trường null của view-model).
        std::string m_sequenceStatus;
        std::optional<std::string> m_verifyResult;
        std::optional<std::string> m_handEyeStep;
        std::optional<std::string> m_stepResult;
        std::optional<std::string> m_gate;

        // Ô đang chọn cho nút 6 của hand-eye (0-based); −1 = tại chỗ.
        int m_heCellRow{ -1 };
        int m_heCellCol{ -1 };

        // Ô chạy kiểm từng bước (1-based, như trang Main).
        int m_stepAssyRow{ 1 }, m_stepAssyCol{ 1 }, m_stepInputRow{ 1 }, m_stepInputCol{ 1 };

        // ── VisionMaster: tạo lần đầu trang hiện, không tạo trong chế độ chụp ảnh ──
        bool m_vmAttempted{ false };
        ui::VmPane m_globalTool;
        ui::VmPane m_config;
        int m_boundLoadCount{ -1 };
        bool m_vmQueued{ false };
        bool m_vmBusy{ false };
        bool m_bindOnShow{ false };
    };
}

namespace winrt::ShieldCanAssy::factory_implementation
{
    struct VisionEditPage : VisionEditPageT<VisionEditPage, implementation::VisionEditPage>
    {
    };
}
