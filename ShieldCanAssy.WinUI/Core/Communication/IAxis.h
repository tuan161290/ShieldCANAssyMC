#pragma once
#include <memory>
#include <stop_token>
#include <string>

#include "Communication/AxisConfig.h"

namespace sca
{
    /// <summary>
    /// Trừu tượng hoá một trục — PLAN.md §2.4. Che khác biệt giữa Panasonic MINAS A6SF
    /// (Modbus RTU, thư viện ServoControl — <c>PanasonicAxis</c>) và Fastech Ezi-Servo PE (Ethernet —
    /// <c>FastechAxis</c>), để routine và interlock viết một lần dùng cho cả bảy trục. <c>FakeAxis</c> khi mô phỏng.
    ///
    /// <para>Đơn vị vị trí và tốc độ là <b>đơn vị của trục</b> — mm với X/Z/Y, độ với A1/A2
    /// (xem <see cref="AxisConfig::Unit"/>). Tốc độ tính theo đơn vị/giây.</para>
    ///
    /// <para>Mọi thao tác dài đều nhận <c>std::stop_token</c>: SafetyMonitor huỷ token
    /// chung khi EMG/cửa/alarm, và đó là cơ chế dừng duy nhất mà routine cần biết (PLAN.md §3.3).</para>
    ///
    /// <para><b>Luồng (bản C++).</b> Các hàm dài (SetServoOn/Home/MoveAbs/MoveInc/ResetAlarm) là HÀM CHẶN — gọi từ
    /// luồng nền, không bao giờ từ luồng UI. Huỷ giữa lúc đang chờ (chỗ C# <c>await Task.Delay(ms, ct)</c>) ném
    /// <c>base::OperationCanceled</c> y như C# ném OperationCanceledException; huỷ được thấy ở đầu vòng chờ thì
    /// trả false. Jog/Stop không chặn (tự đẩy lệnh sang luồng nền như C# <c>Task.Run</c>) nên UI gọi thẳng được.
    /// Các getter trạng thái đọc được từ mọi luồng; <see cref="Refresh"/> chỉ do vòng quét của MotionConnect gọi.</para>
    /// </summary>
    class IAxis
    {
    public:
        virtual ~IAxis() = default;

        virtual const std::string& Name() const = 0;
        /// <summary>Cùng một đối tượng với phần tử trong <c>SystemData::Axes</c> — sửa ở trang Setting có
        /// hiệu lực ngay.</summary>
        virtual std::shared_ptr<AxisConfig> Config() const = 0;

        virtual bool IsConnected() const = 0;
        virtual bool ServoOn() const = 0;
        virtual bool Busy() const = 0;
        virtual bool Alarm() const = 0;
        virtual bool OrgOk() const = 0;

        /// <summary>Giới hạn cứng chiều dương (POT).</summary>
        virtual bool FLS() const = 0;
        /// <summary>Giới hạn cứng chiều âm (NOT).</summary>
        virtual bool RLS() const = 0;

        virtual double CurrentPos() const = 0;

        /// <summary>Mã lỗi thô của driver, hiển thị trong Detail của alarm.</summary>
        virtual int ErrorCode() const = 0;

        virtual bool Connect() = 0;
        virtual void Disconnect() = 0;

        /// <summary>C# <c>SetServoOnAsync</c>.</summary>
        virtual bool SetServoOn(bool on) = 0;

        /// <summary>Về gốc. Bên gọi chịu trách nhiệm interlock; chiều về gốc do tham số trong driver quyết định.</summary>
        virtual bool Home(std::stop_token ct) = 0;

        /// <summary>Di chuyển tuyệt đối rồi chờ tới đích.</summary>
        /// <param name="gentle">Lệnh chậm này giữ nguyên THỜI GIAN ramp ở Setting, tức gia tốc giảm theo tốc độ lệnh
        /// (không gắt hơn mức Auto cao). Mặc định mọi lệnh dùng chung gia tốc của Auto cao (2026-09-25). Chỉ trục
        /// Panasonic có tác dụng.</param>
        virtual bool MoveAbs(double pos, double speed, std::stop_token ct, bool gentle = false) = 0;

        /// <summary>
        /// Di chuyển TƯƠNG ĐỐI (Inching) một khoảng <paramref name="delta"/> tính từ vị trí HIỆN
        /// TẠI rồi chờ tới đích — soft limit vẫn kiểm theo đích tuyệt đối quy ra từ đó, không phải
        /// theo độ lớn delta. Fastech (A1/A2) có lệnh gốc riêng cho việc này (FAS_MoveSingleAxisIncPos);
        /// Panasonic (X/Y/Z) chưa có block tương đối cấu hình sẵn trong driver nên bù bằng phần
        /// mềm — xem từng lớp triển khai (<c>FastechAxis</c>/<c>PanasonicAxis</c>).
        /// </summary>
        virtual bool MoveInc(double delta, double speed, std::stop_token ct) = 0;

        /// <summary>Jog liên tục. dir: +1 / −1 / 0 = dừng.</summary>
        virtual void Jog(int dir, double speed) = 0;

        /// <summary>Dừng có giảm tốc.</summary>
        virtual void Stop() = 0;

        /// <summary>C# <c>ResetAlarmAsync</c>.</summary>
        virtual bool ResetAlarm() = 0;

        /// <summary>Đọc lại trạng thái từ driver. Gọi từ vòng quét của MotionConnect.</summary>
        virtual void Refresh() = 0;
    };
}
