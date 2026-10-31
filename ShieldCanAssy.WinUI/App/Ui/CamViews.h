#pragma once
// Ba khung CAM (BOTTOM CAM #1, #2, TOP CAM — UI-SPEC §2.4): bảng số, ảnh gần nhất và VmRenderControl gắn vào Flow/module
// đặt ở trang Setting. Tách từ MainPage (2026-10-29) để trang Vision Edit ở cấp Engineer hiện đúng ba khung đó (Tuấn: "chỉ
// hiện ra 3 VmRenderControl tương ứng cho 3 Flow, giống trang Main").
//
// Mọi lời gọi vào control VisionMaster đi qua ui::QueueVmWork — ngoài sự kiện XAML, xem Ui/VmPane.h. Hai bộ khung (Main,
// Vision Edit) gắn cùng các module: bộ nào hiện ra mà bộ kia đã gắn SAU lần gắn cuối của mình thì gắn lại, khỏi phụ thuộc
// việc SDK có cho hai control cùng vẽ một module hay không.
#include <array>
#include <chrono>
#include <memory>
#include <string>

#include "Ui/MainPageCamFrame.h"

namespace sca { struct ReadoutCard; class VisionMasterDevice; }

namespace ui
{
    class CamViews
    {
    public:
        enum { Tool1 = 0, Tool2 = 1, Top = 2 };

        /// <summary><paramref name="logTag"/>: đầu câu log ("Main", "Vision Edit").</summary>
        explicit CamViews(std::string logTag) : m_logTag(std::move(logTag)) {}
        CamViews(const CamViews&) = delete;
        CamViews& operator=(const CamViews&) = delete;

        /// <summary>Dựng ba card vào ba ô giữ chỗ của trang — một lần, trong Loaded. Handler giữ <c>this</c>: đối tượng
        /// phải nằm yên (thành viên của trang).</summary>
        void Build(winrt::Microsoft::UI::Xaml::Controls::Border const& tool1,
                   winrt::Microsoft::UI::Xaml::Controls::Border const& tool2,
                   winrt::Microsoft::UI::Xaml::Controls::Border const& top);

        /// <summary>McControl::Init xong — từ đây mới tạo control VisionMaster.</summary>
        void SetCoreReady();
        /// <summary>Ba khung đang nằm trên màn hình (trang hiện VÀ đang dùng bộ khung này). Hiện thì xét lại việc gắn — đổi
        /// tên module ở Setting là ăn ngay.</summary>
        void SetVisible(bool visible);
        /// <summary>Mỗi nhịp lúc đang hiện: bảng số + ảnh gần nhất; còn khung chưa gắn thì thử lại (mỗi giây).</summary>
        void Update();
        /// <summary>--shot (luôn mô phỏng, dữ liệu tạm): một ảnh và một bảng số mẫu ở khung Tool 1 để ảnh chụp kiểm được bố
        /// cục. Chỉ ở phía UI — không ghi gì vào Core.</summary>
        void InjectShotSample();

        void OnWindowMoved();
        void OnModalChanged(bool modalOpen);
        /// <summary>Lúc tắt app: huỷ ba control, mọi việc đã xếp hàng bỏ qua.</summary>
        void Dispose();

    private:
        void UpdateCams();
        /// <summary>Bảng của hai khung bottom-up theo kiểu đang chọn (Ofs/Tgt ↔ bảng tách Đích) — SyncToolCards.</summary>
        void SyncToolCards();
        std::shared_ptr<const sca::ReadoutCard> Shown(std::shared_ptr<const sca::ReadoutCard> const& card) const;

        void QueueVm(bool force);
        void EnsureVmPanes();
        void ApplyPaneVisibility();
        void UpdateRenderBindings(bool force);
        bool BindOne(int index, sca::VisionMasterDevice& device, std::string const& path, int loadCount);
        /// <summary>Nền đặc màu card + thanh công cụ theo nút ở góc card (ApplyRenderLook của WPF).</summary>
        void ApplyRenderLook(int index);
        void ToggleToolbar(int index);

        std::string m_logTag;
        std::array<MainCamFrame, 3> m_cams;
        bool m_built{ false };
        bool m_coreReady{ false };
        bool m_visible{ false };
        bool m_closing{ false };

        /// <summary>Bảng "Not triggered" (WaitingCard của MainViewModel).</summary>
        std::shared_ptr<const sca::ReadoutCard> m_waitingCard;
        /// <summary>Bấm tiêu đề bảng bottom-up: Đích tách phần — chung cho cả hai tay, không lưu.</summary>
        bool m_showBreakdown{ false };
        std::shared_ptr<const sca::ReadoutCard> m_shotSample;

        bool m_vmAttempted{ false };
        bool m_vmActive{ false };
        bool m_vmQueued{ false };
        bool m_vmForce{ false };
        bool m_vmBusy{ false };
        /// <summary>Số thứ tự lần gắn cuối của bộ này (đếm chung mọi bộ khung); 0 = chưa gắn lần nào.</summary>
        unsigned m_bindSerial{ 0 };
        std::chrono::steady_clock::time_point m_lastBindTry{};
        std::string m_loggedVisionDiagnostic;
        std::string m_loggedRenderLookError;
        /// <summary>Việc đã xếp hàng giữ weak_ptr tới đây: trang bị huỷ trước khi việc chạy thì việc bỏ qua.</summary>
        std::shared_ptr<char> m_life{ std::make_shared<char>() };
    };
}
