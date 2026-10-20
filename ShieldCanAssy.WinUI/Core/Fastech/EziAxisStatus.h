#pragma once
// Bản C++ của Lib.Fastech\EziAxisStatus.cs.
#include <cstdint>
#include <string>

namespace sca
{
    /// <summary>
    /// Giải mã thanh ghi trạng thái 32 bit của Ezi-SERVO Plus-E (<c>FAS_GetAxisStatus</c> /
    /// trường <c>dwAxisStatus</c> của <c>FAS_GetAllStatus</c>).
    ///
    /// <para><b>ĐỌC TRƯỚC KHI TIN.</b> Bảng bit dưới đây lấy theo tên gọi trong tài liệu Fastech
    /// nhưng <b>chưa được kiểm chứng trên thiết bị thật</b>. Sai một bit ở đây là sai theo kiểu
    /// tệ nhất: máy tưởng trục đã về gốc trong khi chưa, hoặc bỏ qua alarm của driver. Vì vậy:</para>
    /// <list type="number">
    /// <item>Mọi bit nghi vấn nằm gọn trong <b>một file này</b>, không rải rác trong code.</item>
    /// <item><c>FastechAxis</c> lấy vị trí và tốc độ từ <c>FAS_GetAllStatus</c> — những giá trị
    /// đó là số nguyên riêng, <b>không</b> phụ thuộc bảng bit, nên chuyển động và giới hạn mềm
    /// vẫn đúng kể cả khi bảng bit sai.</item>
    /// <item>Trang Manual có ô hiện <see cref="Describe"/> và giá trị thô. Khi có máy, bật servo
    /// / chạm cữ / về gốc rồi nhìn bit nào đổi là xác minh xong trong vài phút. Chốt lại xong
    /// thì xoá cảnh báo này.</item>
    /// </list>
    ///
    /// <para>TODO(port): bảng bit của bản C# (giữ nguyên dưới đây) KHÁC header gốc của Fastech cho Ezi-SERVO II
    /// (EZISERVO2_AXISSTATUS trong MOTION_EziSERVO2_DEFINE.h, SDK "Ezi-MOTION Plus-E V6" cài ở máy này; mọi header
    /// Ezi-SERVO khác trong SDK cũng cùng bảng). Header gốc: 0x20, 0x40 = dự phòng; ERRPOSOVERFLOW = 0x80,
    /// ERROVERCURRENT = 0x100, ERROVERSPEED = 0x200, ERRPOSTRACKING = 0x400, ERROVERLOAD = 0x800, ERROVERHEAT = 0x1000,
    /// ERRBACKEMF = 0x2000, ERRMOTORPOWER = 0x4000, ERRINPOSITION = 0x8000; MOTIONING = 0x08000000,
    /// MOTIONPAUSE = 0x10000000, MOTIONACCEL = 0x20000000, MOTIONDECEL = 0x40000000, MOTIONCONST = 0x80000000.
    /// Tức là bit 6…15 và 27…31 của bản C# lệch một vị trí (0x01…0x10 và 0x10000…0x04000000 thì khớp). Hệ quả nếu
    /// header gốc đúng: Motioning (0x80000000) thực ra là MOTIONCONST — FastechAxis.WaitReached có thể coi là "đã tới"
    /// khi trục còn đang giảm tốc trong dung sai; BusyMask thiếu MOTIONING thật (và tính cả MOTIONPAUSE);
    /// AnyErrorMask bỏ sót ERRMOTORPOWER (0x4000) và lại tính ERRINPOSITION (0x8000) mà bản C# định loại ra — ERR_ALL
    /// (0x01) vẫn bắt được mọi lỗi. Port giữ đúng hành vi C# — Tuấn quyết định sau khi kiểm trên máy (trang Manual
    /// hiện Describe + giá trị thô).</para>
    /// </summary>
    class EziAxisStatus
    {
    public:
        EziAxisStatus() = delete;

        // ── Lỗi ──
        static constexpr uint32_t ErrorAll        = 0x00000001;
        static constexpr uint32_t HwPosLimit      = 0x00000002;   // chạm cữ cứng chiều +
        static constexpr uint32_t HwNegLimit      = 0x00000004;   // chạm cữ cứng chiều −
        static constexpr uint32_t SwPosLimit      = 0x00000008;
        static constexpr uint32_t SwNegLimit      = 0x00000010;
        static constexpr uint32_t PosOverflow     = 0x00000040;
        static constexpr uint32_t OverCurrent     = 0x00000080;
        static constexpr uint32_t OverSpeed       = 0x00000100;
        static constexpr uint32_t PosTracking     = 0x00000200;
        static constexpr uint32_t OverLoad        = 0x00000400;
        static constexpr uint32_t OverHeat        = 0x00000800;
        static constexpr uint32_t BackEmf         = 0x00001000;
        static constexpr uint32_t MotorPower      = 0x00002000;
        static constexpr uint32_t InPositionErr   = 0x00004000;
        static constexpr uint32_t SystemErr       = 0x00008000;

        // ── Trạng thái vận hành ──
        static constexpr uint32_t EmergencyStop   = 0x00010000;
        static constexpr uint32_t SlowStop        = 0x00020000;
        static constexpr uint32_t OriginReturning = 0x00040000;
        static constexpr uint32_t InPosition      = 0x00080000;
        static constexpr uint32_t ServoOn         = 0x00100000;
        static constexpr uint32_t AlarmReset      = 0x00200000;
        static constexpr uint32_t PtStopped       = 0x00400000;
        static constexpr uint32_t OriginSensor    = 0x00800000;
        static constexpr uint32_t ZPulse          = 0x01000000;
        static constexpr uint32_t OriginReturnOk  = 0x02000000;
        static constexpr uint32_t MotionDir       = 0x04000000;
        static constexpr uint32_t MotionPause     = 0x08000000;
        static constexpr uint32_t MotionAccel     = 0x10000000;
        static constexpr uint32_t MotionDecel     = 0x20000000;
        static constexpr uint32_t MotionConst     = 0x40000000;
        static constexpr uint32_t Motioning       = 0x80000000;

        /// <summary>Gộp mọi bit được coi là lỗi của driver.</summary>
        static constexpr uint32_t AnyErrorMask =
            ErrorAll | PosOverflow | OverCurrent | OverSpeed | PosTracking |
            OverLoad | OverHeat | BackEmf | MotorPower | SystemErr;

        /// <summary>Gộp mọi bit cho biết trục đang chuyển động hoặc đang về gốc.</summary>
        static constexpr uint32_t BusyMask =
            Motioning | MotionAccel | MotionDecel | MotionConst | OriginReturning;

        static constexpr bool Has(uint32_t status, uint32_t flag) { return (status & flag) != 0; }

        /// <summary>
        /// Liệt kê tên các bit đang bật. Đây là công cụ xác minh bảng bit khi có máy: bật servo
        /// rồi xem có đúng <c>SERVO_ON</c> sáng lên không, chạm cữ thì có đúng <c>HW_LIMIT</c>
        /// không. Nhanh hơn nhiều so với dò tài liệu. status = 0 → Loc "Axis.NoStatusBits".
        /// </summary>
        static std::string Describe(uint32_t status);
    };
}
