#pragma once
#include <array>
#include <string>
#include <vector>

namespace sca
{
    /// <summary>
    /// Bản đồ I/O — PLAN.md §3.2. Số thứ tự bit trên Ezi-IO 32Di / 32Do.
    ///
    /// <para><b>Quy ước NC.</b> Các tín hiệu an toàn (<see cref="EmgOk"/>, <see cref="LightCurtainOk"/>,
    /// <see cref="AirOk"/>) đấu tiếp điểm thường đóng, nên mức đọc được là
    /// <b>1 = bình thường</b>, <b>0 = đã tác động hoặc đứt dây</b>. Tên biến đặt theo nghĩa
    /// "…Ok" để đọc code không phải nhớ ngược logic — <c>if (!io.Di[IoMap::EmgOk])</c> đọc thẳng
    /// là "nếu EMG không ổn".</para>
    ///
    /// <para>Đây là bản dự kiến, chốt lại khi có sơ đồ điện. Mọi nơi khác trong code chỉ được
    /// tham chiếu qua hằng số ở đây, không viết số bit trực tiếp.</para>
    /// </summary>
    struct IoMap
    {
        // ── DI ──
        /// <summary>NC — 1 = mạch an toàn đang đóng (bình thường).</summary>
        static constexpr int EmgOk = 0;
        // Bit 1, 2: còn trống — trước là công tắc cửa 2 kênh (DoorA/DoorB), đã THÁO KHỎI MÁY và
        // xoá hết điều kiện an toàn liên quan trong code (Tuấn xác nhận, 2026-09-09). Không tái
        // dùng cho chức năng khác mà không hỏi lại — có thể còn dây đấu vật lý dở dang.
        // Bit 3: còn trống. Đã thử đấu chung 2 nút Start Cụm A vào bit này (2026-09-08) nhưng
        // Tuấn đấu lại điện, quay về bản gốc: 2 bit riêng BtnArmA1/BtnArmA2 bên dưới. Không tái
        // dùng bit này cho chức năng khác.
        static constexpr int BtnStop = 4;
        // Bit 5: còn trống — máy KHÔNG có nút RESET vật lý riêng (Tuấn xác nhận, 2026-09-08).
        // RESET chỉ bấm được bằng nút mềm trên trang Main (McControl.RequestReset gọi thẳng từ
        // UI, không qua DI nào) — xem DoLampResetBtn cho đèn báo tương ứng.
        // Bit 6: còn trống. Máy KHÔNG có công tắc chìa AUTO/MANUAL vật lý — chế độ chọn bằng
        // phần mềm qua nút trên trang Main, xem McControl.RequestSetMode (2026-08-20).
        /// <summary>NC — 1 = đủ áp suất khí.</summary>
        static constexpr int AirOk = 7;
        static constexpr int Vacuum1 = 8;
        static constexpr int Vacuum2 = 9;
        /// <summary>Có Tray đầu vào 2 — chỗ đặt PHÍA NGOÀI của bàn Y1. Đây là cảm biến tray đầu vào cũ, từ 2026-10-15
        /// bàn đặt hai tray (Tuấn chốt). Như mọi cảm biến tray, gắn cố định ở vị trí chờ — xem
        /// <c>Routine_Auto.TrayMissingAt</c>.</summary>
        static constexpr int TrayIn2Present = 10;
        static constexpr int TrayAssyPresent = 11;
        /// <summary>Có Tray đầu vào 1 — chỗ đặt PHÍA TRONG của bàn Y1, cảm biến thêm 2026-10-15. Bit này trước là nút
        /// Tắt còi cứng; nút đó đã bỏ, tắt còi chỉ còn bằng nút trên trang Main (Tuấn chốt cùng ngày).</summary>
        static constexpr int TrayIn1Present = 12;

        /// <summary>NC — 1 = rèm sáng thông suốt (PLAN.md §11 #13). Đây là bản SAO của tín hiệu để hiển
        /// thị và dừng chu trình; ngõ ra OSSD của rèm sáng vẫn phải đi thẳng vào relay an toàn
        /// (§3.1). Ezi-IO không có chứng nhận an toàn nên đường này một mình không bảo vệ ai.</summary>
        static constexpr int LightCurtainOk = 13;

        /// <summary>
        /// Bốn nút Start cứng — cụm A hai nút (DI14, DI15), cụm B hai nút (DI16, DI17) — tác dụng NHƯ NHAU (khách hàng,
        /// Tuấn chốt 2026-10-16): bấm nút nào cũng là <c>McControl.RequestGo</c>, máy tự biết đang chờ tray nào. Bắt
        /// sườn lên. Trước đó cụm A chỉ Arm (OR 2 nút), cụm B Start 1 nạp tray đầu vào, Start 2 nạp tray lắp ráp
        /// (2026-09-26); trước nữa cụm B là cặp an toàn 2 tay (AND, <c>TwoHandGate</c>). Tên hằng số giữ nguyên theo
        /// vị trí nút.
        /// </summary>
        static constexpr int BtnArmA1 = 14;
        static constexpr int BtnArmA2 = 15;
        static constexpr int BtnGoB1 = 16;
        static constexpr int BtnGoB2 = 17;
        /// <summary>Bốn nút Start — xem <see cref="BtnArmA1"/>.</summary>
        static constexpr std::array<int, 4> StartButtons = { BtnArmA1, BtnArmA2, BtnGoB1, BtnGoB2 };

        // ── DO ──
        static constexpr int DoVacuum1 = 0;
        static constexpr int DoVacuum2 = 1;
        static constexpr int DoBlow1 = 2;
        static constexpr int DoBlow2 = 3;
        static constexpr int DoLampRed = 4;
        static constexpr int DoLampYellow = 5;
        static constexpr int DoLampGreen = 6;
        static constexpr int DoBuzzer = 7;
        static constexpr int DoLampStartBtn = 8;
        /// <summary>
        /// Đèn RESET — KHÔNG còn là đèn của một nút RESET vật lý riêng (đã bỏ, xem <c>BtnStop</c>
        /// ghi chú), mà lắp CHUNG vào đúng nút STOP vật lý (Tuấn xác nhận sơ đồ điện thật,
        /// 2026-09-08). Từ 2026-09-26 đèn này là đèn nút STOP thuần: sáng im mọi lúc trừ khi máy
        /// đang chạy (Running/Pausing) — trước đó chỉ sáng khi còn alarm. Tên hằng số giữ nguyên để
        /// khỏi phải sửa lại mọi nơi đang tham chiếu.
        /// </summary>
        static constexpr int DoLampResetBtn = 9;
        /// <summary>Đèn nút Start 1 cụm B. Ba đèn nút Start — <see cref="DoLampStartBtn"/> (chung hai nút cụm A), đèn này,
        /// <see cref="DoLampStartBtnB2"/> — báo y hệt nhau vì bốn nút như nhau (2026-10-16): sáng im khi đang chạy
        /// (Running/Pausing), mọi trạng thái khác tắt, không nhấp nháy (Tuấn 2026-10-16). Xem
        /// <c>McControl.UpdateOutputs</c>.</summary>
        static constexpr int DoLampStartBtnB = 10;
        /// <summary>Đèn chiếu sáng CAM1 (vision).</summary>
        static constexpr int DoLampCam1 = 11;
        /// <summary>Đèn chiếu sáng CAM2 (vision).</summary>
        static constexpr int DoLampCam2 = 12;

        /// <summary>
        /// Nhả phanh Z1 — điều khiển trực tiếp bằng Ezi-IO, KHÔNG dùng tín hiệu phanh tích hợp
        /// của driver Panasonic (Tuấn quyết định, 2026-09-04). <b>1 = nhả phanh</b> (mở, cho
        /// phép di chuyển); <b>0 = phanh đang khoá</b>. Mặc định OFF khi mất kết nối IO hoặc mới
        /// khởi động là KHOÁ — an toàn, tải không tụt khi chưa xác nhận được trạng thái servo.
        /// Xem <c>McFunction.UpdateBrakeOutputs</c> (tự nhả khi ServoOn &amp; không Alarm, tự
        /// khoá lại ngay khi Alarm hoặc Servo OFF) và <c>McFunction.CanMove</c> (chặn Jog/Move
        /// tới khi phanh đã nhả đủ lâu).
        /// </summary>
        static constexpr int DoBrakeReleaseZ1 = 13;
        /// <summary>Nhả phanh Z2 — xem <see cref="DoBrakeReleaseZ1"/>.</summary>
        static constexpr int DoBrakeReleaseZ2 = 14;
        /// <summary>Đèn nút Start 2 cụm B — như <see cref="DoLampStartBtnB"/> (Tuấn cho số DO 2026-10-01).</summary>
        static constexpr int DoLampStartBtnB2 = 15;

        static constexpr int DiCount = 32;
        static constexpr int DoCount = 32;

        /// <summary>Nhãn hiển thị trên bảng đèn DI của trang Manual — chữ qua Loc (khoá "Io.Di…"), mỗi lần đọc dựng
        /// mảng mới như bản C#. Tiền tố "DIx 0x.." do UI tự thêm — để trống ở đây khỏi lặp lại.</summary>
        static std::vector<std::string> DiNames();
        /// <summary>Nhãn hiển thị trên bảng đèn DO của trang Manual — xem <see cref="DiNames"/>.</summary>
        static std::vector<std::string> DoNames();

        /// <summary>Khoá chữ (Loc) của từng bit — "Io.Spare" cho bit trống.</summary>
        static const std::array<const char*, DiCount>& DiNameKeys();
        static const std::array<const char*, DoCount>& DoNameKeys();
    };
}
