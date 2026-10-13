namespace ShieldCanAssy.Core.Infor
{
    /// <summary>Trạng thái máy — PLAN.md §3.3.</summary>
    public enum EMcState
    {
        /// <summary>Nạp config, kết nối thiết bị. Chưa cho chuyển động.</summary>
        Init,
        /// <summary>Đã kết nối, chưa về gốc. Chỉ jog tay tốc độ giới hạn.</summary>
        NotHomed,
        /// <summary>Đang chạy chuỗi Origin.</summary>
        Homing,
        /// <summary>Đã về gốc, đứng yên, sẵn sàng.</summary>
        Idle,
        /// <summary>
        /// Đã bấm Cặp A (2026-08-20, PLAN.md §3.2) sau khi chọn Mode Auto — máy đứng yên, chờ
        /// operator đặt tray rồi bấm đủ Cặp B (2 tay) để bắt đầu chạy THẬT LẦN ĐẦU. Khác
        /// <see cref="WaitOperator"/> ở chỗ đây là bước TRƯỚC KHI chu trình chạy — chưa có
        /// Task nào đang chạy, không phải một chu trình đang dở đợi đổi tray giữa chừng. Đặt tên
        /// khác <c>AutoState</c> (Run.AutoState/EAutoState — bước bên trong Routine_Auto) để
        /// không đọc nhầm hai khái niệm khác nhau.
        /// </summary>
        Armed,
        /// <summary>Đang chạy Auto.</summary>
        Running,
        /// <summary>Đã nhận STOP, đang chạy nốt tới checkpoint an toàn.</summary>
        Pausing,
        /// <summary>Dừng ở checkpoint, giữ tiến độ và sản phẩm trên tay hút.</summary>
        Paused,
        /// <summary>
        /// Bấm STOP lúc đang chạy (Tuấn 2026-09-29): mọi trục dừng ngay, chu trình đứng nguyên chỗ trong lệnh chạy đang
        /// dở — Start 1/Start 2 chạy tiếp đúng lệnh đó. Chuyển MANUAL, EMG hay có lỗi thì bỏ chu trình (về Paused).
        /// </summary>
        Held,
        /// <summary>Chờ người thao tác — đổi tray, xác nhận.</summary>
        WaitOperator,
        /// <summary>Có alarm mức Error/Fatal đang active.</summary>
        Alarm,
        /// <summary>Mạch an toàn hở.</summary>
        Emg
    }

    /// <summary>Chế độ vận hành. Máy không có công tắc chìa vật lý — chọn bằng nút AUTO/MANUAL
    /// trên trang Main (xem McControl.RequestSetMode, 2026-08-20).</summary>
    public enum EMcMode
    {
        Manual,
        Auto
    }

    /// <summary>Bước của chuỗi Auto — PLAN.md §5.2. Ranh giới giữa các bước là checkpoint dừng.</summary>
    public enum EAutoState
    {
        S00_Ready = 0,

        /// <summary>Quét trước toàn bộ ô còn trống của tray lắp ráp bằng CAM top-down — một lần
        /// cho cả tray thay vì xen giữa mỗi lần đặt (§5.2b). Tự bỏ qua nếu đã quét đủ.</summary>
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
    }

    /// <summary>Bước của chuỗi Origin — PLAN.md §4.3.</summary>
    public enum EOriginState
    {
        O0_PreCheck = 0,
        O1_ServoOn = 1,
        O2_HomeZ = 2,
        O3_GateZSafe = 3,
        O4_HomeA = 4,
        O5_HomeXY = 5,
        O6_Standby = 6,
        O7_Finish = 7,
        Aborted = 99
    }

    public enum EJudge
    {
        None,
        Ok,
        Ng
    }

    /// <summary>Mức alarm — PLAN.md §7.1.</summary>
    public enum EAlarmLevel
    {
        /// <summary>Chỉ ghi log, không hiện banner.</summary>
        Info,
        /// <summary>Không dừng máy, đèn vàng nháy. Tự xoá khi hết nguyên nhân.</summary>
        Warning,
        /// <summary>Dừng chu trình ngay, giữ servo. Cần RESET + hết nguyên nhân.</summary>
        Error,
        /// <summary>Cắt servo, bắt buộc Origin lại. Cần RESET + hết nguyên nhân.</summary>
        Fatal
    }

    /// <summary>Trạng thái một ô tray.</summary>
    public enum ECellState
    {
        Empty,
        Loaded,
        Ok,
        Ng,
        Processing,
        Skipped
    }

    /// <summary>Thứ tự quét ô trong tray — cấu hình trong TrayLayout.</summary>
    public enum EScanOrder
    {
        RowMajor,
        ColMajor,
        Serpentine
    }

    /// <summary>Loại driver của một trục — quyết định lớp IAxis nào được tạo ra.</summary>
    public enum EAxisKind
    {
        /// <summary>Panasonic MINAS A6SF qua Modbus RTU (thư viện ServoControl).</summary>
        Panasonic,
        /// <summary>Fastech Ezi-Servo PE qua Ethernet (EziMOTIONPlusE).</summary>
        Fastech
    }

    /// <summary>Màu đèn tháp — PLAN.md §3.6.</summary>
    public enum ETowerLamp
    {
        Off,
        Green,
        GreenBlink,
        Yellow,
        YellowBlink,
        Red,
        RedBlink
    }
}
