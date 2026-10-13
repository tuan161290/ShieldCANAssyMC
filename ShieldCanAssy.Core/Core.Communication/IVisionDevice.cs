using System.ComponentModel;
using System.Threading;
using System.Threading.Tasks;

namespace ShieldCanAssy.Core.Communication
{
    /// <summary>
    /// Kết quả một lần nạp điểm hiệu chuẩn (<see cref="IVisionDevice.TriggerCalibAsync"/>): chỉ còn "thấy / không
    /// thấy". Mọi số đo của chu trình đi qua <see cref="IVisionDevice.CallFlowAsync"/> — output tư thế cũ
    /// <c>PoseX/PoseY/PoseAngle/Score</c> không còn được đọc (chu trình cũ bỏ hẳn 2026-09-13).
    /// </summary>
    public class VisionPose
    {
        public bool Found { get; set; }
        public string Error { get; set; }

        public static VisionPose NotFound(string why = null)
            => new VisionPose { Found = false, Error = why };
    }

    /// <summary>Phép nạp điểm hiệu chuẩn — PLAN.md §6.2, §6.4. Từ 2026-10-08 không còn Flow riêng: mỗi phép chạy trên Flow
    /// soi của chính camera đó (LocateTop, AlignBottom_ToolN), App ghi IsTriggerLocate = 0 để Flow rẽ sang nhánh hiệu
    /// chuẩn. Lần soi thì App gọi qua <see cref="IVisionDevice.CallFlowAsync"/> với IsTriggerLocate = 1.</summary>
    public enum EVisionProcedure
    {
        /// <summary>
        /// CAM top-down (Flow LocateTop), hiệu chuẩn lưới 9 điểm: mỗi lần chạy nạp thêm một điểm vào
        /// module N-point Calibration (chế độ Trigger Acquisition).
        /// </summary>
        TopDownCalib,

        /// <summary>
        /// CAM bottom-up (Flow AlignBottom_Tool1), hiệu chuẩn tâm xoay tay #1: mỗi lần chạy nạp thêm một điểm
        /// vào module hiệu chuẩn của VisionMaster, đúng khuôn <see cref="TopDownCalib"/>.
        /// </summary>
        AlignCalibTool1,

        /// <summary>Như <see cref="AlignCalibTool1"/>, tay hút #2.</summary>
        AlignCalibTool2
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
    public class AxisSnapshot
    {
        public double X { get; set; }
        public double Y2 { get; set; }
        public double A1 { get; set; }
        public double A2 { get; set; }
    }

    public interface IVisionDevice : INotifyPropertyChanged
    {
        bool IsConnected { get; }
        bool IsSolutionLoaded { get; }

        bool Connect();
        void Disconnect();

        Task<bool> LoadSolutionAsync(string solutionPath);

        /// <summary>
        /// Kích nhánh <b>hiệu chuẩn</b> của một Flow (<see cref="EVisionProcedure.TopDownCalib"/>,
        /// <see cref="EVisionProcedure.AlignCalibTool1"/>/<see cref="EVisionProcedure.AlignCalibTool2"/>):
        /// ghi vị trí trục thật và IsTriggerLocate = 0 vào biến toàn cục của solution TRƯỚC, rồi mới chạy Flow — nên module
        /// hiệu chuẩn nạp điểm kèm đúng toạ độ vật lý của lần chụp đó.
        ///
        /// <para>Ghi biến và chạy Flow phải nằm trong CÙNG một lời gọi: tách ra là mở đường cho một
        /// lần kích Flow lọt vào giữa hai bước, nạp điểm mới với toạ độ cũ mà không có dấu hiệu gì.</para>
        /// </summary>
        Task<VisionPose> TriggerCalibAsync(EVisionProcedure procedure, AxisSnapshot axes, CancellationToken ct);

        /// <summary>
        /// Xoá danh sách điểm của module hiệu chuẩn trong Flow của <paramref name="procedure"/> — nút "Clear Point"
        /// của module (Tuấn yêu cầu 2026-10-09). App gọi trước mỗi chuỗi nạp điểm: chạy chồng lên danh sách cũ là
        /// ra file hiệu chuẩn trộn hai lần nạp, không có thông báo lỗi nào. Không chạy Flow. Trả null nếu xoá được,
        /// lý do nếu không.
        /// </summary>
        Task<string> ClearCalibPointsAsync(EVisionProcedure procedure);

        /// <summary>
        /// Đối chiếu danh mục biến toàn cục với solution đang nạp. <paramref name="apply"/> = false
        /// chỉ kiểm tra; true thì tạo biến còn thiếu, ghi giá trị đầu, rồi lưu solution nếu có biến
        /// mới — không bao giờ đổi giá trị hay kiểu của biến đã có (<see cref="GlobalVarSync"/>).
        /// </summary>
        Task<GlobalVarSyncReport> SyncGlobalVariablesAsync(System.Collections.Generic.IList<GlobalVarSpec> specs, bool apply);

        /// <summary>
        /// Đọc tên + kiểu các output một Flow đang khai ở tab <b>Output Params</b>, KHÔNG chạy Flow —
        /// nền của tool "Kiểm tra Output của các Flow" (Tuấn yêu cầu 2026-09-19). Không chạy Flow là
        /// điều kiện bắt buộc: ba Flow nạp điểm hiệu chuẩn mà chạy thì nạp thêm một điểm rác vào
        /// module N-point.
        /// </summary>
        Task<VmFlowOutputs> ReadFlowOutputsAsync(string flow);

        /// <summary>
        /// Gọi một Flow theo hợp đồng "App chấp hành": ghi NGHIÊM các biến toàn cục, chạy Flow, đọc
        /// output số — tất cả trong một lần giữ khoá lệnh. Không bao giờ ném lỗi: lỗi và quá giờ trả về
        /// <see cref="VmCallResult.Ok"/> = false kèm lý do.
        /// </summary>
        Task<VmCallResult> CallFlowAsync(VmCallRequest request, CancellationToken ct);

        /// <summary>Lưu solution đang nạp. Trả null nếu lưu được, chuỗi lỗi nếu không.</summary>
        Task<string> SaveSolutionAsync();

        /// <summary>Ảnh chụp được của lần gọi Flow CAM top-down gần nhất — trang Main hiển thị để
        /// operator nhìn được CAM đang thấy gì, không bắt buộc phải có (null nếu solution chưa khai
        /// output ảnh, hoặc đang chạy mô phỏng).</summary>
        System.Drawing.Bitmap LastTopDownImage { get; }

        /// <summary>Ảnh CAM bottom-up gần nhất của tay #1 / tay #2 — cùng ý nghĩa với <see cref="LastTopDownImage"/>,
        /// tách theo tay để trang Main hiện riêng từng tay (2026-09-28).</summary>
        System.Drawing.Bitmap LastBottomUpImageTool1 { get; }
        System.Drawing.Bitmap LastBottomUpImageTool2 { get; }
    }
}
