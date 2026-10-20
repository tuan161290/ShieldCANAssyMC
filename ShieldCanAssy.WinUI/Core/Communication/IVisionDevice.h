#pragma once
#include "Communication/GlobalVarSync.h"
#include "Communication/ImageData.h"
#include "Communication/VmContract.h"
#include "Communication/VmOutCheck.h"

#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <vector>

namespace sca
{
    /// <summary>
    /// Kết quả một lần nạp điểm hiệu chuẩn (<see cref="IVisionDevice::TriggerCalib"/>): chỉ còn "thấy / không
    /// thấy". Mọi số đo của chu trình đi qua <see cref="IVisionDevice::CallFlow"/> — output tư thế cũ
    /// <c>PoseX/PoseY/PoseAngle/Score</c> không còn được đọc (chu trình cũ bỏ hẳn 2026-09-13).
    /// </summary>
    struct VisionPose
    {
        bool Found{ false };
        std::string Error;

        static VisionPose NotFound(const std::string& why = {})
        {
            VisionPose p;
            p.Found = false;
            p.Error = why;
            return p;
        }
    };

    /// <summary>Phép nạp điểm hiệu chuẩn — PLAN.md §6.2, §6.4. Từ 2026-10-08 không còn Flow riêng: mỗi phép chạy trên Flow
    /// soi của chính camera đó (LocateTop, AlignBottom_ToolN), App ghi IsTriggerLocate = 0 để Flow rẽ sang nhánh hiệu
    /// chuẩn. Lần soi thì App gọi qua <see cref="IVisionDevice::CallFlow"/> với IsTriggerLocate = 1.</summary>
    enum class EVisionProcedure
    {
        /// <summary>CAM top-down (Flow LocateTop), hiệu chuẩn lưới 9 điểm: mỗi lần chạy nạp thêm một điểm vào
        /// module N-point Calibration (chế độ Trigger Acquisition).</summary>
        TopDownCalib = 0,

        /// <summary>CAM bottom-up (Flow AlignBottom_Tool1), hiệu chuẩn tâm xoay tay #1: mỗi lần chạy nạp thêm một điểm
        /// vào module hiệu chuẩn của VisionMaster, đúng khuôn <see cref="TopDownCalib"/>.</summary>
        AlignCalibTool1 = 1,

        /// <summary>Như <see cref="AlignCalibTool1"/>, tay hút #2.</summary>
        AlignCalibTool2 = 2
    };

    /// <summary><c>procedure.ToString()</c> của C#: đúng tên giá trị enum ("TopDownCalib", "AlignCalibTool1",
    /// "AlignCalibTool2") — FakeVision đưa chuỗi này cho móc AfterFlow lúc nạp điểm.</summary>
    inline const char* ToString(EVisionProcedure p)
    {
        switch (p)
        {
        case EVisionProcedure::TopDownCalib:    return "TopDownCalib";
        case EVisionProcedure::AlignCalibTool1: return "AlignCalibTool1";
        case EVisionProcedure::AlignCalibTool2: return "AlignCalibTool2";
        }
        return "?";
    }

    /// <summary>
    /// Vị trí thật (đọc encoder) của các trục liên quan tới vision, gửi sang VisionMaster qua
    /// <b>biến toàn cục</b> ngay trước mỗi lần kích Flow hiệu chuẩn (Tuấn chốt 2026-09-12).
    ///
    /// <para>Module hiệu chuẩn trong solution đặt <i>Calibration Points Input = By Coordinate</i>
    /// rồi subscribe <i>Physical Coordinate X/Y</i> và <i>World Rotate Angle</i> vào đúng những
    /// biến này. Gửi cả bốn trục X, Y2, A1, A2 chứ không chỉ trục đang chạy: một chỗ duy nhất giữ trạng thái máy,
    /// Flow nào cần trục nào thì tự subscribe trục đó — đổi cách hiệu chuẩn sau này không phải sửa
    /// code App.</para>
    ///
    /// <para>Đây là thứ khiến hiệu chuẩn CAM bottom-up khả thi: theo tài liệu Hikrobot "Calibration
    /// and Locating Theory" tr.18, nạp điểm kèm toạ độ thật thì <b>mọi quỹ đạo đều hợp lệ</b> —
    /// không bắt buộc hình S, không bắt buộc lưới hai chiều, mà CAM bottom-up chỉ có X và xoay A.</para>
    /// </summary>
    struct AxisSnapshot
    {
        double X{ 0 };
        double Y2{ 0 };
        double A1{ 0 };
        double A2{ 0 };
    };

    /// <summary>Lỗi do thiết bị vision/SDK báo (thay cho <c>VmException</c> của SDK .NET): <c>what()</c> là
    /// thông báo lỗi (<c>errorMessage</c>), <see cref="Code"/> là mã lỗi SDK. Thiết bị thật ném kiểu này;
    /// VisionConnect đổi nó thành kết quả thất bại kèm log.</summary>
    struct VisionDeviceError : std::runtime_error
    {
        explicit VisionDeviceError(const std::string& message, int code = -1) : std::runtime_error(message), Code(code) {}
        int Code;
    };

    /// <summary>Thiết bị vision. Mọi hàm là hàm CHẶN (bản C# là <c>Task</c>), gọi từ luồng nền; getter rẻ và an toàn
    /// đa luồng. (INotifyPropertyChanged của bản C# bỏ: UI đọc trạng thái theo chu kỳ.)</summary>
    class IVisionDevice
    {
    public:
        virtual ~IVisionDevice() = default;

        virtual bool IsConnected() const = 0;
        virtual bool IsSolutionLoaded() const = 0;

        virtual bool Connect() = 0;
        virtual void Disconnect() = 0;

        /// <summary>LoadSolutionAsync của bản C#.</summary>
        virtual bool LoadSolution(const std::string& solutionPath) = 0;

        /// <summary>
        /// Kích nhánh <b>hiệu chuẩn</b> của một Flow (<see cref="EVisionProcedure::TopDownCalib"/>,
        /// <see cref="EVisionProcedure::AlignCalibTool1"/>/<see cref="EVisionProcedure::AlignCalibTool2"/>):
        /// ghi vị trí trục thật và IsTriggerLocate = 0 vào biến toàn cục của solution TRƯỚC, rồi mới chạy Flow — nên module
        /// hiệu chuẩn nạp điểm kèm đúng toạ độ vật lý của lần chụp đó.
        ///
        /// <para>Ghi biến và chạy Flow phải nằm trong CÙNG một lời gọi: tách ra là mở đường cho một
        /// lần kích Flow lọt vào giữa hai bước, nạp điểm mới với toạ độ cũ mà không có dấu hiệu gì.</para>
        /// </summary>
        virtual VisionPose TriggerCalib(EVisionProcedure procedure, const AxisSnapshot& axes, std::stop_token ct) = 0;

        /// <summary>
        /// Xoá danh sách điểm của module hiệu chuẩn trong Flow của <paramref name="procedure"/> — nút "Clear Point"
        /// của module (Tuấn yêu cầu 2026-10-09). App gọi trước mỗi chuỗi nạp điểm: chạy chồng lên danh sách cũ là
        /// ra file hiệu chuẩn trộn hai lần nạp, không có thông báo lỗi nào. Không chạy Flow. Trả CHUỖI RỖNG nếu xoá
        /// được (bản C# trả null), lý do nếu không.
        /// </summary>
        virtual std::string ClearCalibPoints(EVisionProcedure procedure) = 0;

        /// <summary>Tên các module đã xoá ở lần <see cref="ClearCalibPoints"/> THÀNH CÔNG gần nhất — VisionConnect ghi
        /// vào log (bản C# nhận danh sách này thẳng từ <c>VisionMasterDevice.ClearCalibPoints</c>). Hàm thêm của bản
        /// C++; mặc định rỗng.</summary>
        virtual std::vector<std::string> LastClearedCalibModules() const { return {}; }

        /// <summary>
        /// Đối chiếu danh mục biến toàn cục với solution đang nạp. <paramref name="apply"/> = false
        /// chỉ kiểm tra; true thì tạo biến còn thiếu, ghi giá trị đầu, rồi lưu solution nếu có biến
        /// mới — không bao giờ đổi giá trị hay kiểu của biến đã có (<see cref="GlobalVarSync"/>).
        /// </summary>
        virtual GlobalVarSyncReport SyncGlobalVariables(const std::vector<GlobalVarSpec>& specs, bool apply) = 0;

        /// <summary>
        /// Đọc tên + kiểu các output một Flow đang khai ở tab <b>Output Params</b>, KHÔNG chạy Flow —
        /// nền của tool "Kiểm tra Output của các Flow" (Tuấn yêu cầu 2026-09-19). Không chạy Flow là
        /// điều kiện bắt buộc: ba Flow nạp điểm hiệu chuẩn mà chạy thì nạp thêm một điểm rác vào
        /// module N-point.
        /// </summary>
        virtual VmFlowOutputs ReadFlowOutputs(const std::string& flow) = 0;

        /// <summary>
        /// Gọi một Flow theo hợp đồng "App chấp hành": ghi NGHIÊM các biến toàn cục, chạy Flow, đọc
        /// output số — tất cả trong một lần giữ khoá lệnh. Lỗi và quá giờ trả về <see cref="VmCallResult::Ok"/> = false
        /// kèm lý do. (FakeVision bị huỷ giữa lúc "chụp" thì ném <c>base::OperationCanceled</c>, như Task.Delay của C#.)
        /// </summary>
        virtual VmCallResult CallFlow(const VmCallRequest& request, std::stop_token ct) = 0;

        /// <summary>Lưu solution đang nạp. Trả CHUỖI RỖNG nếu lưu được, chuỗi lỗi nếu không (bản C# trả null
        /// khi thành công).</summary>
        virtual std::string SaveSolution() = 0;

        /// <summary>Ảnh chụp được của lần gọi Flow CAM top-down gần nhất — trang Main hiển thị để
        /// operator nhìn được CAM đang thấy gì, không bắt buộc phải có (nullptr nếu solution chưa khai
        /// output ảnh, hoặc đang chạy mô phỏng).</summary>
        virtual std::shared_ptr<const ImageData> LastTopDownImage() const = 0;

        /// <summary>Ảnh CAM bottom-up gần nhất của tay #1 / tay #2 — cùng ý nghĩa với <see cref="LastTopDownImage"/>,
        /// tách theo tay để trang Main hiện riêng từng tay (2026-09-28).</summary>
        virtual std::shared_ptr<const ImageData> LastBottomUpImageTool1() const = 0;
        virtual std::shared_ptr<const ImageData> LastBottomUpImageTool2() const = 0;
    };
}
