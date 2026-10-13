using System;
using System.Collections.Generic;
using System.Text;
using ShieldCanAssy.Core.Lang;

namespace ShieldCanAssy.Lib.Fastech
{
    /// <summary>
    /// Giải mã thanh ghi trạng thái 32 bit của Ezi-SERVO Plus-E (<c>FAS_GetAxisStatus</c> /
    /// trường <c>dwAxisStatus</c> của <c>FAS_GetAllStatus</c>).
    ///
    /// <para><b>ĐỌC TRƯỚC KHI TIN.</b> Bảng bit dưới đây lấy theo tên gọi trong tài liệu Fastech
    /// nhưng <b>chưa được kiểm chứng trên thiết bị thật</b>. Sai một bit ở đây là sai theo kiểu
    /// tệ nhất: máy tưởng trục đã về gốc trong khi chưa, hoặc bỏ qua alarm của driver. Vì vậy:
    /// </para>
    /// <list type="number">
    /// <item>Mọi bit nghi vấn nằm gọn trong <b>một file này</b>, không rải rác trong code.</item>
    /// <item><c>FastechAxis</c> lấy vị trí và tốc độ từ <c>FAS_GetAllStatus</c> — những giá trị
    /// đó là số nguyên riêng, <b>không</b> phụ thuộc bảng bit, nên chuyển động và giới hạn mềm
    /// vẫn đúng kể cả khi bảng bit sai.</item>
    /// <item>Trang Manual có ô hiện <see cref="Describe"/> và giá trị thô. Khi có máy, bật servo
    /// / chạm cữ / về gốc rồi nhìn bit nào đổi là xác minh xong trong vài phút. Chốt lại xong
    /// thì xoá cảnh báo này.</item>
    /// </list>
    /// </summary>
    public static class EziAxisStatus
    {
        // ── Lỗi ──
        public const uint ErrorAll        = 0x00000001;
        public const uint HwPosLimit      = 0x00000002;   // chạm cữ cứng chiều +
        public const uint HwNegLimit      = 0x00000004;   // chạm cữ cứng chiều −
        public const uint SwPosLimit      = 0x00000008;
        public const uint SwNegLimit      = 0x00000010;
        public const uint PosOverflow     = 0x00000040;
        public const uint OverCurrent     = 0x00000080;
        public const uint OverSpeed       = 0x00000100;
        public const uint PosTracking     = 0x00000200;
        public const uint OverLoad        = 0x00000400;
        public const uint OverHeat        = 0x00000800;
        public const uint BackEmf         = 0x00001000;
        public const uint MotorPower      = 0x00002000;
        public const uint InPositionErr   = 0x00004000;
        public const uint SystemErr       = 0x00008000;

        // ── Trạng thái vận hành ──
        public const uint EmergencyStop   = 0x00010000;
        public const uint SlowStop        = 0x00020000;
        public const uint OriginReturning = 0x00040000;
        public const uint InPosition      = 0x00080000;
        public const uint ServoOn         = 0x00100000;
        public const uint AlarmReset      = 0x00200000;
        public const uint PtStopped       = 0x00400000;
        public const uint OriginSensor    = 0x00800000;
        public const uint ZPulse          = 0x01000000;
        public const uint OriginReturnOk  = 0x02000000;
        public const uint MotionDir       = 0x04000000;
        public const uint MotionPause     = 0x08000000;
        public const uint MotionAccel     = 0x10000000;
        public const uint MotionDecel     = 0x20000000;
        public const uint MotionConst     = 0x40000000;
        public const uint Motioning       = 0x80000000;

        /// <summary>Gộp mọi bit được coi là lỗi của driver.</summary>
        public const uint AnyErrorMask =
            ErrorAll | PosOverflow | OverCurrent | OverSpeed | PosTracking |
            OverLoad | OverHeat | BackEmf | MotorPower | SystemErr;

        /// <summary>Gộp mọi bit cho biết trục đang chuyển động hoặc đang về gốc.</summary>
        public const uint BusyMask =
            Motioning | MotionAccel | MotionDecel | MotionConst | OriginReturning;

        public static bool Has(uint status, uint flag) { return (status & flag) != 0; }

        private static readonly KeyValuePair<uint, string>[] _names =
        {
            new KeyValuePair<uint, string>(ErrorAll,        "ERR_ALL"),
            new KeyValuePair<uint, string>(HwPosLimit,      "HW_LIMIT+"),
            new KeyValuePair<uint, string>(HwNegLimit,      "HW_LIMIT−"),
            new KeyValuePair<uint, string>(SwPosLimit,      "SW_LIMIT+"),
            new KeyValuePair<uint, string>(SwNegLimit,      "SW_LIMIT−"),
            new KeyValuePair<uint, string>(PosOverflow,     "POS_OVERFLOW"),
            new KeyValuePair<uint, string>(OverCurrent,     "OVER_CURRENT"),
            new KeyValuePair<uint, string>(OverSpeed,       "OVER_SPEED"),
            new KeyValuePair<uint, string>(PosTracking,     "POS_TRACKING"),
            new KeyValuePair<uint, string>(OverLoad,        "OVER_LOAD"),
            new KeyValuePair<uint, string>(OverHeat,        "OVER_HEAT"),
            new KeyValuePair<uint, string>(BackEmf,         "BACK_EMF"),
            new KeyValuePair<uint, string>(MotorPower,      "MOTOR_POWER"),
            new KeyValuePair<uint, string>(InPositionErr,   "INPOS_ERR"),
            new KeyValuePair<uint, string>(SystemErr,       "SYSTEM_ERR"),
            new KeyValuePair<uint, string>(EmergencyStop,   "EMG"),
            new KeyValuePair<uint, string>(SlowStop,        "SLOW_STOP"),
            new KeyValuePair<uint, string>(OriginReturning, "ORG_RUNNING"),
            new KeyValuePair<uint, string>(InPosition,      "IN_POSITION"),
            new KeyValuePair<uint, string>(ServoOn,         "SERVO_ON"),
            new KeyValuePair<uint, string>(AlarmReset,      "ALARM_RESET"),
            new KeyValuePair<uint, string>(PtStopped,       "PT_STOPPED"),
            new KeyValuePair<uint, string>(OriginSensor,    "ORG_SENSOR"),
            new KeyValuePair<uint, string>(ZPulse,          "Z_PULSE"),
            new KeyValuePair<uint, string>(OriginReturnOk,  "ORG_OK"),
            new KeyValuePair<uint, string>(MotionDir,       "DIR"),
            new KeyValuePair<uint, string>(MotionPause,     "PAUSE"),
            new KeyValuePair<uint, string>(MotionAccel,     "ACCEL"),
            new KeyValuePair<uint, string>(MotionDecel,     "DECEL"),
            new KeyValuePair<uint, string>(MotionConst,     "CONST"),
            new KeyValuePair<uint, string>(Motioning,       "MOTIONING"),
        };

        /// <summary>
        /// Liệt kê tên các bit đang bật. Đây là công cụ xác minh bảng bit khi có máy: bật servo
        /// rồi xem có đúng <c>SERVO_ON</c> sáng lên không, chạm cữ thì có đúng <c>HW_LIMIT</c>
        /// không. Nhanh hơn nhiều so với dò tài liệu.
        /// </summary>
        public static string Describe(uint status)
        {
            if (status == 0) return Loc.T("Axis.NoStatusBits");

            var sb = new StringBuilder();
            foreach (var kv in _names)
            {
                if ((status & kv.Key) == 0) continue;
                if (sb.Length > 0) sb.Append(' ');
                sb.Append(kv.Value);
            }

            // Bit lạ không có trong bảng: hiện ra thay vì nuốt, vì nó chính là dấu hiệu bảng bit
            // đang sai hoặc firmware khác phiên bản.
            uint known = 0;
            foreach (var kv in _names) known |= kv.Key;
            uint unknown = status & ~known;
            if (unknown != 0) sb.Append(" +0x").Append(unknown.ToString("X8"));

            return sb.ToString();
        }
    }
}
