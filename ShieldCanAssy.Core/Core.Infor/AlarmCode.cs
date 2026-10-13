using System;
using System.Collections.Generic;

namespace ShieldCanAssy.Core.Infor
{
    /// <summary>
    /// Mã alarm — PLAN.md §7.2. Chia khối theo nguồn phát, mỗi trục một dải 100 và dùng chung
    /// bảng offset <see cref="AxisFault"/>. Đọc mã là biết ngay trục nào, lỗi gì:
    /// 2105 = Z1 timeout về gốc.
    /// </summary>
    public static class AlarmCode
    {
        // ── 1000–1099 Hệ thống ──
        public const int ConfigInvalid      = 1000;
        public const int RoutineCrashed     = 1001;
        public const int ControlWatchdog    = 1002;
        public const int DiskFull           = 1003;
        public const int UndefinedAlarm     = 1099;

        // ── 1100–1199 An toàn ──
        public const int EmergencyStop      = 1100;
        // 1101, 1102, 1104: đã bỏ cùng cảm biến cửa — máy đã tháo hết công tắc cửa 2 kênh, mọi
        // điều kiện an toàn liên quan xoá khỏi code (Tuấn xác nhận, 2026-09-09). Không tái dùng
        // 3 mã này cho lỗi khác để tránh nhầm với log/tài liệu cũ.
        public const int AirPressureLow     = 1103;
        public const int LightCurtainBroken  = 1105;

        // ── 2000–2699 Trục (base + offset) ──
        public const int AxisBaseX  = 2000;
        public const int AxisBaseZ1 = 2100;
        public const int AxisBaseZ2 = 2200;
        public const int AxisBaseY1 = 2300;
        public const int AxisBaseY2 = 2400;
        public const int AxisBaseA1 = 2500;
        public const int AxisBaseA2 = 2600;

        // ── 3000–3099 I/O ──
        public const int IoDisconnected     = 3000;
        public const int IoWriteFailed      = 3001;

        // ── 4000–4099 Vision ──
        public const int VisionDisconnected = 4000;
        public const int VisionLoadFailed   = 4001;
        public const int VisionNotReady     = 4002;
        public const int VisionTriggerTimeout = 4003;
        public const int VisionNotFound     = 4004;
        public const int VisionLowScore     = 4005;
        public const int VisionNotCalibrated = 4006;
        public const int VisionContract     = 4010;   // Flow trả số không khớp hợp đồng
        public const int VisionCalibNotReady = 4011;  // hiệu chuẩn chưa sẵn sàng (cổng phía App)
        public const int VisionCalibExpired = 4090;   // Warning

        // ── 5000–5099 Chu trình ──
        public const int PickFailed         = 5001;
        public const int ProductDropped     = 5002;
        public const int PlaceFailed        = 5003;
        public const int CompensationOutOfRange = 5004;
        public const int PlaceTargetGuard   = 5005;   // Warning — đích lệch lưới teach, bỏ ô
        public const int ConsecutiveNg      = 5010;
        public const int TrayEmpty          = 5020;   // Warning — từ 30/09 chu trình không raise nữa, chờ đổi tray chỉ là thông báo
        public const int ResumeIntegrity    = 5030;
        public const int CannotReturnProduct = 5031;  // soi hỏng mà không trả được về tray đầu vào — lấy hàng ra

        // ── 6000–6099 Dữ liệu ──
        public const int ModelSaveFailed    = 6000;
        public const int ModelFileCorrupt   = 6001;
        public const int RunningDataFailed  = 6002;

        private static readonly Dictionary<string, int> _axisBase = new Dictionary<string, int>(StringComparer.Ordinal)
        {
            { AxisId.X,  AxisBaseX  },
            { AxisId.Z1, AxisBaseZ1 },
            { AxisId.Z2, AxisBaseZ2 },
            { AxisId.Y1, AxisBaseY1 },
            { AxisId.Y2, AxisBaseY2 },
            { AxisId.A1, AxisBaseA1 },
            { AxisId.A2, AxisBaseA2 },
        };

        /// <summary>Mã alarm của một trục: base của trục cộng offset lỗi.</summary>
        public static int ForAxis(string axisName, AxisFault fault)
        {
            int b;
            if (!_axisBase.TryGetValue(axisName ?? string.Empty, out b))
                return UndefinedAlarm;
            return b + (int)fault;
        }

        /// <summary>Tách ngược mã trục về (tên trục, loại lỗi). Trả về false nếu không phải mã trục.</summary>
        public static bool TryParseAxis(int code, out string axisName, out AxisFault fault)
        {
            axisName = null;
            fault = default(AxisFault);
            if (code < 2000 || code >= 2700) return false;

            int b = code / 100 * 100;
            int off = code - b;
            foreach (var kv in _axisBase)
            {
                if (kv.Value != b) continue;
                axisName = kv.Key;
                fault = (AxisFault)off;
                return true;
            }
            return false;
        }
    }

    /// <summary>
    /// Offset lỗi dùng chung cho mọi khối trục — PLAN.md §7.2.
    ///
    /// <para><b>Hai nhóm khác hẳn nhau:</b> <see cref="Disconnected"/>/<see cref="DriverAlarm"/>/
    /// <see cref="HardLimit"/> là ĐIỀU KIỆN tiếp diễn — <c>MotionConnect.Refresh()</c> liên tục
    /// Raise lại lúc còn đúng và ClearCondition lúc hết, nên phát qua <c>AlarmManager.RaiseAxis</c>.
    /// Các mã còn lại (<see cref="MoveTimeout"/>, <see cref="SoftLimit"/>, <see cref="HomeTimeout"/>,
    /// <see cref="NotHomed"/>, <see cref="ServoOffUnexpected"/>) là SỰ KIỆN một lần thử thất bại —
    /// không có gì để tiếp tục theo dõi, phải phát qua <c>AlarmManager.RaiseAxisEvent</c> (tự
    /// ClearCondition ngay sau Raise) để RESET không bị kẹt vĩnh viễn — xem doc-comment
    /// <c>RaiseAxisEvent</c> để biết lý do (bug thật gặp 2026-09-09: HomeTimeout không tự hết đã
    /// khoá SafeToMove cả máy vĩnh viễn, không jog ra khỏi hard limit được nữa).</para>
    /// </summary>
    public enum AxisFault
    {
        /// <summary>+00 Mất kết nối (Fatal) — điều kiện tiếp diễn, RaiseAxis</summary>
        Disconnected = 0,
        /// <summary>+01 Alarm từ driver, kèm ErrorCode đọc được (Fatal) — điều kiện tiếp diễn, RaiseAxis</summary>
        DriverAlarm = 1,
        /// <summary>+02 Timeout chuyển động (Error) — sự kiện một lần, RaiseAxisEvent</summary>
        MoveTimeout = 2,
        /// <summary>+03 Chạm soft limit (Error) — sự kiện một lần, RaiseAxisEvent</summary>
        SoftLimit = 3,
        /// <summary>+04 Chạm hard limit POT/NOT (Warning, đổi từ Fatal 2026-09-08) — điều kiện tiếp diễn, RaiseAxis</summary>
        HardLimit = 4,
        /// <summary>+05 Timeout về gốc, hoặc driver báo alarm lúc đang về gốc (Error) — sự kiện một lần, RaiseAxisEvent</summary>
        HomeTimeout = 5,
        /// <summary>+06 Chưa về gốc mà nhận lệnh chạy (Error) — sự kiện một lần, RaiseAxisEvent</summary>
        NotHomed = 6,
        /// <summary>+07 Servo OFF ngoài ý muốn (Fatal) — sự kiện một lần, RaiseAxisEvent</summary>
        ServoOffUnexpected = 7,
        /// <summary>+08 Sai lệch vị trí quá lớn — following error (Fatal). Chưa nơi nào Raise — nếu
        /// dùng sau này thì cũng là sự kiện một lần, phát qua RaiseAxisEvent.</summary>
        FollowingError = 8
    }
}
