#pragma once
// Giá trị số của các enum được lưu trong JSON/log — KHÔNG đổi thứ tự (Newtonsoft ghi enum dạng số).
#include <string_view>

namespace sca
{
    /// <summary>Trạng thái máy — PLAN.md §3.3.</summary>
    enum class EMcState
    {
        Init = 0,       // Nạp config, kết nối thiết bị. Chưa cho chuyển động.
        NotHomed,       // Đã kết nối, chưa về gốc. Chỉ jog tay tốc độ giới hạn.
        Homing,         // Đang chạy chuỗi Origin.
        Idle,           // Đã về gốc, đứng yên, sẵn sàng.
        /// Sẵn sàng ở Mode Auto — chọn AUTO là vào (2026-09-30), hoặc nút START mềm sau STOP. Máy đứng yên, chờ
        /// operator đặt tray rồi bấm nút Start cứng để bắt đầu chạy (PLAN.md §3.4). Khác WaitOperator ở chỗ đây là
        /// bước TRƯỚC KHI chu trình chạy — chưa có Task nào đang chạy, không phải một chu trình đang dở đợi đổi tray
        /// giữa chừng. Đặt tên khác AutoState (Run.AutoState/EAutoState — bước bên trong Routine_Auto) để không đọc
        /// nhầm hai khái niệm khác nhau.
        Armed,
        Running,        // Đang chạy Auto.
        Pausing,        // Đã nhận STOP, đang chạy nốt tới checkpoint an toàn.
        Paused,         // Dừng ở checkpoint, giữ tiến độ và sản phẩm trên tay hút.
        /// Bấm STOP lúc đang chạy (Tuấn 2026-09-29): mọi trục dừng ngay, chu trình đứng nguyên chỗ trong lệnh chạy
        /// đang dở — nút Start chạy tiếp đúng lệnh đó. Chuyển MANUAL, EMG hay có lỗi thì bỏ chu trình (về Paused).
        Held,
        WaitOperator,   // Chờ người thao tác — đổi tray, xác nhận.
        Alarm,          // Có alarm mức Error/Fatal đang active.
        Emg             // Mạch an toàn hở.
    };

    /// <summary>Chế độ vận hành. Máy không có công tắc chìa vật lý — chọn bằng nút AUTO/MANUAL trên trang Main
    /// (xem McControl.RequestSetMode, 2026-08-20).</summary>
    enum class EMcMode { Manual = 0, Auto };

    /// <summary>Bước của chuỗi Auto — PLAN.md §5.2. Ranh giới giữa các bước là checkpoint dừng.</summary>
    enum class EAutoState
    {
        S00_Ready = 0,

        /// Quét trước toàn bộ ô còn trống của tray lắp ráp bằng CAM top-down — một lần cho cả tray thay vì xen giữa
        /// mỗi lần đặt (§5.2b). Tự bỏ qua nếu đã quét đủ.
        S05_ScanAssy = 5,

        S10_Pick1 = 10,
        S11_Pick2 = 11,
        S20_Align1 = 20,
        S21_Align2 = 21,
        S30_Locate1 = 30,
        S31_Place1 = 31,
        S32_Locate2 = 32,
        S33_Place2 = 33,
        S40_Index = 40,
        S50_TrayCheck = 50
    };

    /// <summary>Bước của chuỗi Origin — PLAN.md §4.3.</summary>
    enum class EOriginState
    {
        O0_PreCheck = 0, O1_ServoOn = 1, O2_HomeZ = 2, O3_GateZSafe = 3, O4_HomeA = 4,
        O5_HomeXY = 5, O6_Standby = 6, O7_Finish = 7, Aborted = 99
    };

    enum class EJudge { None = 0, Ok, Ng };

    /// <summary>Mức alarm — PLAN.md §7.1.</summary>
    enum class EAlarmLevel
    {
        Info = 0,   // Chỉ ghi log, không hiện banner.
        Warning,    // Không dừng máy, đèn vàng nháy. Tự xoá khi hết nguyên nhân.
        Error,      // Dừng chu trình ngay, giữ servo. Cần RESET + hết nguyên nhân.
        Fatal       // Cắt servo, bắt buộc Origin lại. Cần RESET + hết nguyên nhân.
    };

    /// <summary>Trạng thái một ô tray.</summary>
    enum class ECellState { Empty = 0, Loaded, Ok, Ng, Processing, Skipped };

    /// <summary>Thứ tự quét ô trong tray — cấu hình trong TrayLayout.</summary>
    enum class EScanOrder { RowMajor = 0, ColMajor, Serpentine };

    /// <summary>Loại driver của một trục — quyết định lớp IAxis nào được tạo ra.</summary>
    enum class EAxisKind
    {
        Panasonic = 0,  // Panasonic MINAS A6SF qua Modbus RTU (thư viện ServoControl).
        Fastech         // Fastech Ezi-Servo PE qua Ethernet (EziMOTIONPlusE).
    };

    /// <summary>Màu đèn tháp — PLAN.md §3.6.</summary>
    enum class ETowerLamp { Off = 0, Green, GreenBlink, Yellow, YellowBlink, Red, RedBlink };

    // ── Tên enum như Enum.ToString() của C# (log, CSV, alarms.json "level"). Giá trị ngoài bảng → "?". ──

    inline const char* ToString(EMcState v)
    {
        static const char* n[] = { "Init", "NotHomed", "Homing", "Idle", "Armed", "Running", "Pausing", "Paused", "Held",
                                   "WaitOperator", "Alarm", "Emg" };
        return (int)v >= 0 && (int)v < 12 ? n[(int)v] : "?";
    }
    inline const char* ToString(EMcMode v) { return v == EMcMode::Manual ? "Manual" : v == EMcMode::Auto ? "Auto" : "?"; }
    inline const char* ToString(EAutoState v)
    {
        switch (v)
        {
            case EAutoState::S00_Ready:     return "S00_Ready";
            case EAutoState::S05_ScanAssy:  return "S05_ScanAssy";
            case EAutoState::S10_Pick1:     return "S10_Pick1";
            case EAutoState::S11_Pick2:     return "S11_Pick2";
            case EAutoState::S20_Align1:    return "S20_Align1";
            case EAutoState::S21_Align2:    return "S21_Align2";
            case EAutoState::S30_Locate1:   return "S30_Locate1";
            case EAutoState::S31_Place1:    return "S31_Place1";
            case EAutoState::S32_Locate2:   return "S32_Locate2";
            case EAutoState::S33_Place2:    return "S33_Place2";
            case EAutoState::S40_Index:     return "S40_Index";
            case EAutoState::S50_TrayCheck: return "S50_TrayCheck";
        }
        return "?";
    }
    inline const char* ToString(EOriginState v)
    {
        static const char* n[] = { "O0_PreCheck", "O1_ServoOn", "O2_HomeZ", "O3_GateZSafe", "O4_HomeA", "O5_HomeXY",
                                   "O6_Standby", "O7_Finish" };
        if (v == EOriginState::Aborted) return "Aborted";
        return (int)v >= 0 && (int)v < 8 ? n[(int)v] : "?";
    }
    inline const char* ToString(EJudge v)
    {
        static const char* n[] = { "None", "Ok", "Ng" };
        return (int)v >= 0 && (int)v < 3 ? n[(int)v] : "?";
    }
    inline const char* ToString(EAlarmLevel v)
    {
        static const char* n[] = { "Info", "Warning", "Error", "Fatal" };
        return (int)v >= 0 && (int)v < 4 ? n[(int)v] : "?";
    }
    inline const char* ToString(ECellState v)
    {
        static const char* n[] = { "Empty", "Loaded", "Ok", "Ng", "Processing", "Skipped" };
        return (int)v >= 0 && (int)v < 6 ? n[(int)v] : "?";
    }
    inline const char* ToString(EScanOrder v)
    {
        static const char* n[] = { "RowMajor", "ColMajor", "Serpentine" };
        return (int)v >= 0 && (int)v < 3 ? n[(int)v] : "?";
    }
    inline const char* ToString(EAxisKind v)
    {
        static const char* n[] = { "Panasonic", "Fastech" };
        return (int)v >= 0 && (int)v < 2 ? n[(int)v] : "?";
    }
    inline const char* ToString(ETowerLamp v)
    {
        static const char* n[] = { "Off", "Green", "GreenBlink", "Yellow", "YellowBlink", "Red", "RedBlink" };
        return (int)v >= 0 && (int)v < 7 ? n[(int)v] : "?";
    }
}
