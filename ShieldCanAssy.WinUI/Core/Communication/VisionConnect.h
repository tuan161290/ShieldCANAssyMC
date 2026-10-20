#pragma once
#include "Communication/IVisionDevice.h"
#include "Communication/Sim/FakeVision.h"
#include "Data/SystemData.h"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

namespace sca
{
    struct VisionCmdGate;   // khoá lệnh, sống lâu hơn VisionConnect khi còn một lần Run bị bỏ dở

    /// <summary>
    /// CAM bottom-up + CAM top-down thật, qua HIKROBOT VisionMaster 4.4.0 — bọc thiết bị VisionMaster. PLAN.md §2.1, §6.
    /// Tên Flow/output đọc từ <see cref="SystemData::VisionOutputs"/> (cấu hình được, không hardcode) — đó cũng là hợp
    /// đồng Tuấn cần khớp khi dựng solution trong VisionMaster IDE (PLAN.md §6.8).
    ///
    /// <para>Lớp này KHÔNG biết SDK VisionMaster. Thiết bị thật là một <see cref="IVisionDevice"/> "thô"
    /// (<c>VisionMasterDevice</c>) do hàm tạo đăng ký ở <see cref="SetRealDeviceFactory"/> cấp; VisionConnect bọc nó bằng
    /// đúng những gì bản C# làm quanh <c>VisionMasterDevice</c>: kiểm trạng thái + thông báo, khoá lệnh, canh giờ
    /// <c>VisionTriggerTimeoutMs</c>, alarm 4003 dạng sự kiện, log và đổi exception thành kết quả thất bại.
    /// Thiết bị thô vì vậy: hàm CHẶN, không tự canh giờ, không phát alarm, lỗi SDK thì NÉM
    /// <see cref="VisionDeviceError"/> (what() = thông báo SDK).</para>
    ///
    /// <para><b>Mô phỏng.</b> Bản C# chọn ở McControl: <c>SimulationMode ? new FakeVision() : new VisionConnect(sys)</c>
    /// — mô phỏng không bao giờ chạm VisionMaster. Lớp này giữ đúng điều đó cả khi bị tạo lúc mô phỏng: đọc
    /// <c>SimulationMode</c> lúc tạo, bật thì mọi lời gọi đi THẲNG vào một <see cref="FakeVision"/> riêng (không qua
    /// khoá/canh giờ). <c>SimulationMode</c> tắt mà chưa đăng ký hàm tạo (hoặc hàm tạo hỏng): KHÔNG rơi về bản giả —
    /// log lỗi + alarm 4001, Connect/LoadSolution trả false (máy thật mà "thấy" bằng số bịa là đặt sai hàng).</para>
    /// </summary>
    class VisionConnect : public IVisionDevice
    {
    public:
        explicit VisionConnect(std::shared_ptr<SystemData> sys);

        /// <summary>Đăng ký nơi tạo thiết bị VisionMaster thật (thay cho <c>new VisionMasterDevice()</c>).
        /// Gọi một lần lúc khởi động app, trước khi Connect(). Truyền hàm rỗng để gỡ.</summary>
        static void SetRealDeviceFactory(std::function<std::shared_ptr<IVisionDevice>()> factory);
        static bool HasRealDeviceFactory();

        bool IsSimulation() const { return m_fake != nullptr; }
        /// <summary>Bản giả đang dùng (nullptr khi chạy thiết bị thật).</summary>
        std::shared_ptr<FakeVision> Fake() const { return m_fake; }
        /// <summary>Thiết bị đang dùng: bản giả, thiết bị thật, hoặc nullptr khi chưa tạo được thiết bị thật.</summary>
        std::shared_ptr<IVisionDevice> Device() const;

        bool IsConnected() const override;
        bool IsSolutionLoaded() const override;

        /// <summary>
        /// PlatformSDK không có bước "connect" tách rời khỏi nạp solution — kết nối camera thật diễn ra bên
        /// trong <c>LoadSolution</c>. Bản C# chỉ đánh dấu "sẵn sàng gọi nạp solution", đúng thứ tự
        /// <c>McControl.Init()</c> đang gọi: <c>Connect()</c> rồi <c>LoadSolution()</c>. Bản C++ tạo luôn instance
        /// solution của SDK ở đây (<c>VisionMasterDevice::Connect</c>). Mô phỏng: nối bản giả và dựng kho biến giả
        /// theo <see cref="VisionGlobalVarCatalog"/>.
        /// </summary>
        bool Connect() override;
        void Disconnect() override;
        bool LoadSolution(const std::string& solutionPath) override;

        /// <summary>Kích Flow hiệu chuẩn kèm vị trí trục thật (§6.2, §6.4). Ghi biến toàn cục rồi Run trong cùng một lần
        /// gọi — xem <see cref="IVisionDevice::TriggerCalib"/>.</summary>
        VisionPose TriggerCalib(EVisionProcedure procedure, const AxisSnapshot& axes, std::stop_token ct) override;
        /// <summary>Xoá điểm cũ của module hiệu chuẩn — xem <see cref="IVisionDevice::ClearCalibPoints"/>. Không canh giờ
        /// (không chạy Flow), nhưng giữ khoá lệnh để không lọt lần kích Flow nào vào giữa.</summary>
        std::string ClearCalibPoints(EVisionProcedure procedure) override;
        std::vector<std::string> LastClearedCalibModules() const override;
        GlobalVarSyncReport SyncGlobalVariables(const std::vector<GlobalVarSpec>& specs, bool apply) override;
        /// <summary>Đọc Output Params của một Flow — xem <see cref="IVisionDevice::ReadFlowOutputs"/>. Không canh giờ như
        /// Run(): đây chỉ là đọc phần khai báo, không kích camera.</summary>
        VmFlowOutputs ReadFlowOutputs(const std::string& flow) override;
        /// <summary>Gọi một Flow — xem <see cref="IVisionDevice::CallFlow"/>.</summary>
        VmCallResult CallFlow(const VmCallRequest& request, std::stop_token ct) override;
        std::string SaveSolution() override;

        std::shared_ptr<const ImageData> LastTopDownImage() const override;
        std::shared_ptr<const ImageData> LastBottomUpImageTool1() const override;
        std::shared_ptr<const ImageData> LastBottomUpImageTool2() const override;

        // ── phần dựng lời gọi của bản C# (private ở C#), để thiết bị thật dùng lại đúng một chỗ ──
        /// <summary>Hiệu chuẩn chạy trên chính Flow soi của camera đó, nhánh IsTriggerLocate = 0 (Tuấn 2026-10-08):
        /// AlignCalibTool1/2 → FlowAlignBottomTool1/2, còn lại → FlowLocateTop.</summary>
        static std::string FlowNameOf(const VisionOutputNames& names, EVisionProcedure procedure);
        /// <summary>Vị trí trục → tên biến toàn cục khai ở trang Setting. Tên để trống thì bỏ qua
        /// trục đó — Flow nạp điểm không nhất thiết cần cả bốn trục.</summary>
        static std::map<std::string, double> AxisVars(const VisionOutputNames& names, const AxisSnapshot& axes);
        /// <summary>Biến ghi trước một lần nạp điểm: <see cref="AxisVars"/> + <c>VmVars::IsTriggerLocate</c> = 0. Flow soi
        /// và nạp điểm hiệu chuẩn là một, rẽ nhánh theo cờ này — lần soi ghi 1 ở VmCycle.LocateRequest / AlignBottomRequest.</summary>
        static std::map<std::string, double> CalibVars(const VisionOutputNames& names, const AxisSnapshot& axes);
        /// <summary>Hiệu chuẩn tâm xoay là CAM bottom-up, hiệu chuẩn top-down là CAM top-down.</summary>
        static bool IsBottomUp(EVisionProcedure procedure);
        /// <summary>Ảnh CAM bottom-up thuộc tay nào: Flow của tay #2 thì 2, còn lại 1 (SetBottomUpImage của bản C#).</summary>
        static int BottomUpToolOf(const VisionOutputNames& names, const std::string& flow);

    private:
        std::shared_ptr<IVisionDevice> EnsureDevice(std::string& why);

        std::shared_ptr<SystemData> m_sys;
        std::shared_ptr<FakeVision> m_fake;
        std::shared_ptr<VisionCmdGate> m_gate;
        mutable std::mutex m_deviceMutex;
        std::shared_ptr<IVisionDevice> m_device;
        std::atomic<bool> m_connected{ false };
    };
}
