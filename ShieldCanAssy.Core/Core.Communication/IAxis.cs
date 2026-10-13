using System.ComponentModel;
using System.Threading;
using System.Threading.Tasks;

namespace ShieldCanAssy.Core.Communication
{
    /// <summary>
    /// Trừu tượng hoá một trục — PLAN.md §2.4. Che khác biệt giữa Panasonic MINAS A6SF
    /// (Modbus RTU, thư viện ServoControl) và Fastech Ezi-Servo PE (Ethernet), để routine
    /// và interlock viết một lần dùng cho cả bảy trục.
    ///
    /// <para>Đơn vị vị trí và tốc độ là <b>đơn vị của trục</b> — mm với X/Z/Y, độ với A1/A2
    /// (xem <see cref="AxisConfig.Unit"/>). Tốc độ tính theo đơn vị/giây.</para>
    ///
    /// <para>Mọi thao tác dài đều nhận <see cref="CancellationToken"/>: SafetyMonitor huỷ token
    /// chung khi EMG/cửa/alarm, và đó là cơ chế dừng duy nhất mà routine cần biết (PLAN.md §3.3).</para>
    /// </summary>
    public interface IAxis : INotifyPropertyChanged
    {
        string Name { get; }
        AxisConfig Config { get; }

        bool IsConnected { get; }
        bool ServoOn { get; }
        bool Busy { get; }
        bool Alarm { get; }
        bool OrgOk { get; }

        /// <summary>Giới hạn cứng chiều dương (POT).</summary>
        bool FLS { get; }
        /// <summary>Giới hạn cứng chiều âm (NOT).</summary>
        bool RLS { get; }

        double CurrentPos { get; }

        /// <summary>Mã lỗi thô của driver, hiển thị trong Detail của alarm.</summary>
        int ErrorCode { get; }

        bool Connect();
        void Disconnect();

        Task<bool> SetServoOnAsync(bool on);

        /// <summary>Về gốc. Bên gọi chịu trách nhiệm interlock; chiều về gốc do tham số trong driver quyết định.</summary>
        Task<bool> HomeAsync(CancellationToken ct);

        /// <summary>Di chuyển tuyệt đối rồi chờ tới đích.</summary>
        /// <param name="gentle">Lệnh chậm này giữ nguyên THỜI GIAN ramp ở Setting, tức gia tốc giảm theo tốc độ lệnh
        /// (không gắt hơn mức Auto cao). Mặc định mọi lệnh dùng chung gia tốc của Auto cao (2026-09-25). Chỉ trục
        /// Panasonic có tác dụng.</param>
        Task<bool> MoveAbsAsync(double pos, double speed, CancellationToken ct, bool gentle = false);

        /// <summary>
        /// Di chuyển TƯƠNG ĐỐI (Inching) một khoảng <paramref name="delta"/> tính từ vị trí HIỆN
        /// TẠI rồi chờ tới đích — soft limit vẫn kiểm theo đích tuyệt đối quy ra từ đó, không phải
        /// theo độ lớn delta. Fastech (A1/A2) có lệnh gốc riêng cho việc này (FAS_MoveSingleAxisIncPos);
        /// Panasonic (X/Y/Z) chưa có block tương đối cấu hình sẵn trong driver nên bù bằng phần
        /// mềm — xem từng lớp triển khai (<c>FastechAxis</c>/<c>PanasonicAxis</c>).
        /// </summary>
        Task<bool> MoveIncAsync(double delta, double speed, CancellationToken ct);

        /// <summary>Jog liên tục. dir: +1 / −1 / 0 = dừng.</summary>
        void Jog(int dir, double speed);

        /// <summary>Dừng có giảm tốc.</summary>
        void Stop();

        Task<bool> ResetAlarmAsync();

        /// <summary>Đọc lại trạng thái từ driver. Gọi từ vòng quét của MotionConnect.</summary>
        void Refresh();
    }
}
