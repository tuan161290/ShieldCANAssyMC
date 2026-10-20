#pragma once
#include "Infor/AxisId.h"

#include <string>
#include <string_view>
#include <utility>

namespace sca
{
    /// <summary>
    /// Offset lỗi dùng chung cho mọi khối trục — PLAN.md §7.2.
    ///
    /// <para><b>Hai nhóm khác hẳn nhau:</b> Disconnected/DriverAlarm/HardLimit là ĐIỀU KIỆN tiếp diễn —
    /// <c>MotionConnect::Refresh()</c> liên tục Raise lại lúc còn đúng và ClearCondition lúc hết, nên phát qua
    /// <c>AlarmManager::RaiseAxis</c>. Các mã còn lại (MoveTimeout, SoftLimit, HomeTimeout, NotHomed,
    /// ServoOffUnexpected) là SỰ KIỆN một lần thử thất bại — không có gì để tiếp tục theo dõi, phải phát qua
    /// <c>AlarmManager::RaiseAxisEvent</c> (tự ClearCondition ngay sau Raise) để RESET không bị kẹt vĩnh viễn — xem
    /// doc-comment <c>RaiseAxisEvent</c> để biết lý do (bug thật gặp 2026-09-09: HomeTimeout không tự hết đã khoá
    /// SafeToMove cả máy vĩnh viễn, không jog ra khỏi hard limit được nữa).</para>
    /// </summary>
    enum class AxisFault
    {
        Disconnected = 0,        // +00 Mất kết nối (Fatal) — điều kiện tiếp diễn, RaiseAxis
        DriverAlarm = 1,         // +01 Alarm từ driver, kèm ErrorCode đọc được (Fatal) — điều kiện tiếp diễn, RaiseAxis
        MoveTimeout = 2,         // +02 Timeout chuyển động (Error) — sự kiện một lần, RaiseAxisEvent
        SoftLimit = 3,           // +03 Chạm soft limit (Error) — sự kiện một lần, RaiseAxisEvent
        HardLimit = 4,           // +04 Chạm hard limit POT/NOT (Warning, đổi từ Fatal 2026-09-08) — điều kiện tiếp diễn
        HomeTimeout = 5,         // +05 Timeout về gốc, hoặc driver báo alarm lúc đang về gốc (Error) — sự kiện một lần
        NotHomed = 6,            // +06 Chưa về gốc mà nhận lệnh chạy (Error) — sự kiện một lần, RaiseAxisEvent
        ServoOffUnexpected = 7,  // +07 Servo OFF ngoài ý muốn (Fatal) — sự kiện một lần, RaiseAxisEvent
        FollowingError = 8       // +08 Sai lệch vị trí quá lớn (Fatal). Chưa nơi nào Raise — dùng sau này thì RaiseAxisEvent
    };

    /// <summary>
    /// Mã alarm — PLAN.md §7.2. Chia khối theo nguồn phát, mỗi trục một dải 100 và dùng chung
    /// bảng offset <see cref="AxisFault"/>. Đọc mã là biết ngay trục nào, lỗi gì:
    /// 2105 = Z1 timeout về gốc.
    /// </summary>
    struct AlarmCode
    {
        // ── 1000–1099 Hệ thống ──
        static constexpr int ConfigInvalid = 1000;
        static constexpr int RoutineCrashed = 1001;
        static constexpr int ControlWatchdog = 1002;
        static constexpr int DiskFull = 1003;
        static constexpr int UndefinedAlarm = 1099;

        // ── 1100–1199 An toàn ──
        static constexpr int EmergencyStop = 1100;
        // 1101, 1102, 1104: đã bỏ cùng cảm biến cửa — máy đã tháo hết công tắc cửa 2 kênh, mọi
        // điều kiện an toàn liên quan xoá khỏi code (Tuấn xác nhận, 2026-09-09). Không tái dùng
        // 3 mã này cho lỗi khác để tránh nhầm với log/tài liệu cũ.
        static constexpr int AirPressureLow = 1103;
        static constexpr int LightCurtainBroken = 1105;

        // ── 2000–2699 Trục (base + offset) ──
        static constexpr int AxisBaseX = 2000;
        static constexpr int AxisBaseZ1 = 2100;
        static constexpr int AxisBaseZ2 = 2200;
        static constexpr int AxisBaseY1 = 2300;
        static constexpr int AxisBaseY2 = 2400;
        static constexpr int AxisBaseA1 = 2500;
        static constexpr int AxisBaseA2 = 2600;

        // ── 3000–3099 I/O ──
        static constexpr int IoDisconnected = 3000;
        static constexpr int IoWriteFailed = 3001;

        // ── 4000–4099 Vision ──
        static constexpr int VisionDisconnected = 4000;
        static constexpr int VisionLoadFailed = 4001;
        static constexpr int VisionNotReady = 4002;
        static constexpr int VisionTriggerTimeout = 4003;
        static constexpr int VisionNotFound = 4004;
        static constexpr int VisionLowScore = 4005;
        static constexpr int VisionNotCalibrated = 4006;
        static constexpr int VisionContract = 4010;           // Flow trả số không khớp hợp đồng
        static constexpr int VisionCalibNotReady = 4011;      // hiệu chuẩn chưa sẵn sàng (cổng phía App)
        static constexpr int VisionCalibExpired = 4090;       // Warning

        // ── 5000–5099 Chu trình ──
        static constexpr int PickFailed = 5001;
        static constexpr int ProductDropped = 5002;
        static constexpr int PlaceFailed = 5003;
        static constexpr int CompensationOutOfRange = 5004;
        static constexpr int PlaceTargetGuard = 5005;         // Warning — đích lệch lưới teach, bỏ ô
        static constexpr int ConsecutiveNg = 5010;
        static constexpr int TrayEmpty = 5020;                // Warning — từ 30/09 chu trình không raise nữa, chờ đổi tray chỉ là thông báo
        static constexpr int ResumeIntegrity = 5030;
        static constexpr int CannotReturnProduct = 5031;      // soi hỏng mà không trả được về tray đầu vào — lấy hàng ra

        // ── 6000–6099 Dữ liệu ──
        static constexpr int ModelSaveFailed = 6000;
        static constexpr int ModelFileCorrupt = 6001;
        static constexpr int RunningDataFailed = 6002;

        /// <summary>Bảng tên trục → base, theo thứ tự khai báo của Dictionary bản C# (TryParseAxis duyệt theo thứ tự
        /// này).</summary>
        static constexpr std::pair<const char*, int> AxisBases[] = {
            { "X", AxisBaseX }, { "Z1", AxisBaseZ1 }, { "Z2", AxisBaseZ2 }, { "Y1", AxisBaseY1 },
            { "Y2", AxisBaseY2 }, { "A1", AxisBaseA1 }, { "A2", AxisBaseA2 } };

        /// <summary>Mã alarm của một trục: base của trục cộng offset lỗi. Tên lạ → UndefinedAlarm.</summary>
        static int ForAxis(std::string_view axisName, AxisFault fault)
        {
            for (const auto& b : AxisBases)
                if (axisName == b.first) return b.second + (int)fault;
            return UndefinedAlarm;
        }

        /// <summary>Tách ngược mã trục về (tên trục, loại lỗi). Trả về false nếu không phải mã trục.</summary>
        static bool TryParseAxis(int code, std::string& axisName, AxisFault& fault)
        {
            axisName.clear();
            fault = AxisFault::Disconnected;
            if (code < 2000 || code >= 2700) return false;

            int b = code / 100 * 100;
            int off = code - b;
            for (const auto& kv : AxisBases)
            {
                if (kv.second != b) continue;
                axisName = kv.first;
                fault = (AxisFault)off;
                return true;
            }
            return false;
        }
    };
}
