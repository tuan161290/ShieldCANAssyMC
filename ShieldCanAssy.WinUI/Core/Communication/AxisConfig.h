#pragma once
#include "Infor/Enums.h"

#include <string>

namespace sca
{
    /// <summary>
    /// Cấu hình một trục, lưu trong system.json. Mỗi trục Panasonic có cổng COM riêng
    /// (PLAN.md §1.1) nên <see cref="ComPort"/> nằm ở cấp trục chứ không phải cấp bus.
    ///
    /// <para>Đối tượng được chia sẻ qua shared_ptr (SystemData::Axes ↔ IAxis::Config): sửa ở trang Setting có tác
    /// dụng ngay trên trục. JSON (khoá = tên trường, enum = số) ở Data/DataJson.h — chỉ dùng từ file .cpp.</para>
    /// </summary>
    struct AxisConfig
    {
        /// <summary>Tên trục (AxisId). Rỗng đứng vai null của C# — không ghi khoá "Name".</summary>
        std::string Name;
        EAxisKind Kind = EAxisKind::Panasonic;

        /// <summary>"mm" cho trục tịnh tiến, "deg" cho A1/A2.</summary>
        std::string Unit = "mm";

        // ── Panasonic (Modbus RTU) ──
        std::string ComPort = "COM1";
        int BaudRate = 115200;
        int UnitId = 1;

        /// <summary>
        /// Số hiệu block — <b>đã chốt theo quy ước của <c>ServoControl.ServoAxisConfig</c></b>:
        /// Block0 = về gốc (ORG), Block1 = MoveAbs, Block2 = Jog dương, Block3 = Jog âm. Đây là
        /// mặc định sẵn có trong thư viện, không phải một lựa chọn tuỳ ý của project này, nên
        /// bốn con số dưới đây không phải "quyết định" mà là chép lại đúng quy ước đó.
        ///
        /// <para>Việc còn lại ở P1 <b>không phải chọn số hiệu block</b> — số đã cố định — mà là
        /// dùng PANATERM lập trình <b>nội dung</b> của đúng 4 block đó trong driver (mã lệnh
        /// ORG/ABS/JOG ở Pr56–59, dữ liệu vị trí/tốc độ). <c>StartHome</c>/<c>MoveAbsolute</c>/
        /// jog chỉ kích số block tương ứng; chưa lập trình nội dung thì lệnh vẫn "gửi thành
        /// công" nhưng trục làm chuyện khác hoặc không làm gì.</para>
        /// </summary>
        int HomeBlock = 0;
        int AbsoluteBlock = 1;
        int JogPositiveBlock = 2;
        int JogNegativeBlock = 3;

        /// <summary>
        /// Ô (slot) tốc độ 0-15 mà block ABS thật sự đọc — <b>không phải số block ABS</b>.
        ///
        /// <para>Bảng block của A6SF tách riêng hai thứ: block nào chạy (<see cref="AbsoluteBlock"/>,
        /// ghi vào BLOCK_NUMBER) và block đó lấy tốc độ ở ô nào (nằm trong command word của block,
        /// lập một lần bằng PANATERM). Ghi tốc độ vào ô mà block đang chạy KHÔNG trỏ tới thì driver
        /// lặng lẽ bỏ qua — trục vẫn chạy đủ hành trình, chỉ là chạy bằng tốc độ cũ nằm sẵn trong ô
        /// của nó.</para>
        ///
        /// <para>Máy này: block ABS đọc ô <b>0</b>, còn Jog đọc ô <b>1</b> (Tuấn tra bảng block
        /// trong PANATERM, 2026-09-09). Trước đây code luôn ghi tốc độ vào ô 1 cho mọi lệnh, nên
        /// Jog đổi tốc độ được còn Move Abs/Tới Pick/Tới Place/chu trình Auto thì tăng giảm tốc độ
        /// bao nhiêu cũng không đổi — cả X lẫn Y1/Y2 đều vậy. Sửa ô này nếu lập lại bảng block
        /// trong PANATERM.</para>
        /// </summary>
        int AbsoluteVelocityIndex = 0;

        /// <summary>
        /// Thời gian tăng tốc / giảm tốc của lệnh chạy tới vị trí (Move Abs, Inching, Tới Pick/
        /// Place, chu trình Auto), tính bằng <b>ms</b> — thời gian THẬT từ đứng yên tới tốc độ
        /// <b>Auto cao</b> (<see cref="MoveSpeed"/>). Lệnh chậm hơn dùng CÙNG gia tốc đó nên tới
        /// tốc độ của nó sớm hơn theo tỉ lệ: Z đặt 300 ms ở 300 mm/s thì chặng 6 mm/s ramp 6 ms.
        ///
        /// <para>Thanh ghi accel/decel của A6SF không phải thời gian mà là độ dốc (ms cho mỗi
        /// 3000 rpm). Trước 2026-09-25 thư viện nhân lại độ dốc theo tốc độ TỪNG lệnh để thời gian
        /// ramp không đổi, nên mọi lệnh đổi tốc độ — Z hai chặng đổi ở mọi lệnh — phải ghi lại hai
        /// thanh ghi này trước khi trục chạy (Tuấn yêu cầu bớt vì độ trễ). Nay độ dốc cố định theo
        /// mức Auto cao (<c>ServoAxisConfig.AbsoluteRampRpm</c>), chỉ ghi khi con số đổi; gia tốc
        /// không bao giờ gắt hơn mức Auto cao. 250ms là giá trị vốn bị hard-code trong thư viện
        /// trước khi cho chỉnh (Tuấn yêu cầu đưa ra trang Setting, 2026-09-09).</para>
        ///
        /// <para><b>Chỉ có tác dụng với 5 trục Panasonic.</b> A1/A2 (Fastech) không đặt accel qua
        /// code — dùng thẳng tham số nạp sẵn trong driver Ezi-Servo.</para>
        ///
        /// <para>Đặt quá ngắn là tăng tốc rất gắt: dễ kêu, rung máy, hoặc driver báo quá dòng/quá
        /// tải. Hạ dần từng bước và theo dõi driver, đừng nhảy thẳng xuống giá trị nhỏ.</para>
        /// </summary>
        int AccelTimeMs = 250;

        /// <summary>Thời gian giảm tốc, ms. Xem <see cref="AccelTimeMs"/>.</summary>
        int DecelTimeMs = 250;

        /// <summary>
        /// Dung sai coi là "đã tới nơi", tính bằng xung — dùng trong <c>WaitMotion</c>/<c>WaitReached</c>
        /// (PanasonicAxis/FastechAxis).
        ///
        /// <para><b>Phải luôn nhỏ hơn hẳn bước Inching nhỏ nhất muốn dùng.</b> Điều kiện "đã tới
        /// nơi" so vị trí hiện tại với đích trong phạm vi dung sai này — nếu bước di chuyển yêu
        /// cầu (delta) ≤ dung sai, vị trí CŨ (trước khi trục kịp nhúc nhích) đã nằm trong phạm vi
        /// đó rồi, nên phần mềm có thể báo "xong" gần như ngay lập tức mà trục chưa chắc đã di
        /// chuyển đúng đích. 50 xung (=0.05mm ở trục 1000 xung/mm) từng khớp chính xác giá trị
        /// Inching 0.05mm Tuấn thử — đúng nguyên nhân của hiện tượng "nhập 0.05 không di chuyển
        /// được" (2026-09-09). Hạ xuống 10 xung (=0.01mm) để còn margin an toàn cho bước Inching
        /// nhỏ nhất 0.02mm Tuấn yêu cầu, đồng thời vẫn đủ rộng hơn nhiễu encoder thông thường.</para>
        /// </summary>
        int PositionTolerancePulse = 10;

        /// <summary>
        /// Lệnh chạy tới đích cách vị trí hiện tại ≤ từng này xung thì thư viện ServoControl KHÔNG gửi xuống driver, coi
        /// như đã ở đó (<c>ServoConfig.SkipMoveTolerance</c>, chỉ trục Panasonic).
        ///
        /// <para>Tách khỏi <see cref="PositionTolerancePulse"/> từ 2026-10-15 (Tuấn: "nhận lệnh chính xác 0.01"). Trước
        /// đó dùng chung: system.json đặt 50 xung thì mọi lệnh ≤ 0.05 mm bị bỏ — Y1 không chạy 0.05 mm giữa hai lần hút
        /// cùng hàng, App gửi lại rồi báo 2302. 5 xung = 0.005 mm nên lệnh 0.01 mm vẫn chạy; lệnh đã gửi thì App chờ
        /// driver chạy xong mới đi tiếp (<c>PanasonicAxis.JudgeMove</c>). Không bao giờ lớn hơn
        /// <see cref="PositionTolerancePulse"/> (<c>PanasonicAxis.LibrarySkipTolerance</c> kẹp lại).</para>
        /// </summary>
        int SkipMoveTolerancePulse = 5;

        // ── Fastech (Ethernet) ──
        std::string IpAddress = "192.168.0.2";
        int AxisIndex = 0;

        /// <summary><c>iBdID</c> của thư viện Fastech. Phải khác nhau giữa MỌI module Fastech trong
        /// process, kể cả 2 card I/O — xem <c>FastechBoardId</c>.</summary>
        int BoardId = 0;

        // ── Quy đổi ──
        /// <summary>
        /// Số xung trên một đơn vị (mm hoặc deg) — <b>hằng số cố định theo thiết kế, không phải
        /// số đo được</b>.
        ///
        /// <para>Đã chốt: mọi trục tịnh tiến Panasonic dùng <b>1000 xung/mm</b> (mm 3 chữ số
        /// thập phân — 12.345 mm ↔ 12345 xung), trục xoay Fastech dùng <b>36000 xung/360° của
        /// đầu hút</b> (động cơ 18000 xung/vòng qua hộp số 1/2).
        /// Đây là tỉ số truyền điện tử (electronic gear, Pr0.08/Pr0.09 của A6SF và tham số
        /// tương ứng của Ezi-Servo) do chính mình cấu hình trong driver bằng PANATERM/EZI
        /// Motion Studio để RA ĐÚNG con số này — không phải một đại lượng cơ khí phải đo bằng
        /// thước rồi suy ra. Việc của P1 khi ra máy là cấu hình driver đúng theo hằng số đã
        /// chốt, không phải "đo" nó.</para>
        /// </summary>
        double PulsePerUnit = 1000.0;

        /// <summary>
        /// Bước tiến cơ khí trên một vòng động cơ — dùng đổi tốc độ đơn vị/giây sang rpm.
        /// Khác <see cref="PulsePerUnit"/> ở chỗ đây LÀ một đại lượng cơ khí (bước vít me,
        /// tỉ số hộp giảm tốc) nên có thể lệch so với bản vẽ; sai nó chỉ làm tốc độ hiển thị
        /// không khớp tốc độ thật, không tạo sai số vị trí — độ ưu tiên chỉnh thấp hơn nhiều
        /// so với <see cref="PulsePerUnit"/>.
        /// </summary>
        double UnitPerRev = 10.0;

        int MaxRpm = 3000;

        // ── Giới hạn và tốc độ ──
        double SoftLimitPos = 500.0;
        double SoftLimitNeg = -1.0;

        /// <summary>
        /// Hai mức tốc độ Jog tay — chuyển bằng nút chọn Nhanh/Chậm chung cho toàn bộ trục ở
        /// trang Manual/Teaching (Tuấn yêu cầu, 2026-09-05), không phải cấu hình riêng từng lần
        /// jog. <see cref="JogSpeedSlow"/> cũng là tốc độ về gốc (Về gốc luôn chạy chậm, không
        /// theo nút chọn — an toàn không phụ thuộc thao tác tay của operator).
        /// </summary>
        double JogSpeedSlow = 10.0;
        double JogSpeedFast = 20.0;

        /// <summary>
        /// Ba mức tốc độ của chu trình Auto, RIÊNG TỪNG TRỤC (Tuấn chốt 2026-09-18) — X chạy
        /// 300 mm/s trong khi Z chỉ 150, một hệ số phần trăm dùng chung cho cả máy không tả được.
        ///
        /// <para><see cref="MoveSpeed"/> là mức CAO — giữ nguyên tên để system.json cũ và mọi chỗ
        /// gọi hiện có không phải sửa. <see cref="MoveSpeedLow"/> là mức THẤP, dùng cho đoạn Z đi từ
        /// điểm giảm tốc xuống cao độ hút/đặt (<c>McFunction.MoveZDown</c>) và cho Y2 tới điểm bù
        /// lúc đặt, kèm gia tốc giảm theo (từ 2026-09-30). <see cref="MoveSpeedMid"/> là mức VỪA: có ô cài đặt
        /// nhưng CHƯA gắn vào đoạn chạy nào — Tuấn để dành 2026-09-18.</para>
        ///
        /// <para>Hai mức mới suy ra từ mức cao khi chưa đặt (≤ 0): Vừa = 50%, Thấp = 20%. Nhờ vậy
        /// system.json viết trước hôm nay chạy được ngay, không phải sửa tay; lưu trang Setting một
        /// lần là số thật được ghi xuống file. JSON ghi giá trị ĐÃ SUY RA (Newtonsoft đọc getter).</para>
        /// </summary>
        double MoveSpeed = 100.0;

        double MoveSpeedMid() const { return m_moveSpeedMid > 0 ? m_moveSpeedMid : MoveSpeed * 0.5; }
        void SetMoveSpeedMid(double v) { m_moveSpeedMid = v; }
        double MoveSpeedLow() const { return m_moveSpeedLow > 0 ? m_moveSpeedLow : MoveSpeed * 0.2; }
        void SetMoveSpeedLow(double v) { m_moveSpeedLow = v; }

        // ── Về gốc ──
        /// <summary>
        /// Chiều về gốc: +1 hoặc −1. Với Z1, Z2 phải là chiều ĐI LÊN (đã chốt, PLAN.md §4.3).
        ///
        /// <para>Trước 2026-09-09 còn có <c>HomeDirectionGuard</c> giám sát phần mềm để chặn
        /// trục đi ngược chiều này lúc về gốc — theo yêu cầu của Tuấn, chốt chặn phần mềm đó đã
        /// bỏ hẳn, chiều về gốc nay chỉ còn do tham số trong driver (Pr5.xx A6SF/EziMOTION)
        /// quyết định. Trường này giữ lại vì vẫn dùng để đặt vị trí ban đầu của trục mô phỏng
        /// (<c>FakeAxis</c>) lệch khỏi gốc.</para>
        /// </summary>
        int HomeDir = 1;

        /// <summary>
        /// Về gốc theo kiểu <b>chạm DOG rồi bắt xung Z</b> thay vì dừng ngay ở cạnh DOG
        /// (PLAN.md §4.3, §11 #14).
        ///
        /// <para>Đây là tham số nằm trong driver (Pr5.xx của A6SF / thanh ghi origin của
        /// Ezi-Servo); trường này chỉ ghi lại chủ ý để đối chiếu khi cài driver ở P1 và để hiện
        /// trên trang Setting. Máy dùng encoder tương đối nên phải về gốc mỗi lần bật máy, và
        /// mọi điểm teach đều đo từ gốc đó — độ lặp lại của gốc CHÍNH LÀ sai số của mọi điểm
        /// teach sau mỗi lần khởi động. Cạnh DOG lặp lại được cỡ ±0.05–0.5 mm tuỳ cảm biến và
        /// tốc độ tiếp cận; xung Z lặp lại trong một xung encoder. Chênh lệch đó lớn hơn cả
        /// ngưỡng nghiệm thu hiệu chuẩn camera (0.02 mm), nên dừng ở cạnh DOG là không dùng được.</para>
        /// </summary>
        bool HomeUseZPhase = true;

        int HomeTimeoutMs = 60000;
        int MoveTimeoutMs = 30000;

        /// <summary>
        /// Toạ độ gán cho vị trí gốc cơ khí — gốc cơ khí thường không phải toạ độ 0.
        ///
        /// <para><b>Chỉ có tác dụng thật trên trục quay</b> (<see cref="WrapDegrees"/> = true).
        /// Ở đó <c>FastechAxis</c> cộng con số này vào vị trí đọc từ driver, nên toạ độ app dùng
        /// là <b>toạ độ máy</b> còn driver vẫn đếm từ 0 tại cảm biến gốc của nó — sửa ô này ở
        /// trang Setting có hiệu lực ngay, không phải nạp lại tham số driver.</para>
        ///
        /// <para>Với X/Y/Z (Panasonic) thì offset gốc nằm trong tham số về gốc của driver
        /// (Pr5.xx, lập bằng PANATERM) chứ không phải ở đây; trường này với chúng chỉ còn là
        /// toạ độ đích của nút "Về gốc" trên trang Teaching và vị trí gốc của trục mô phỏng.
        /// Đừng đặt khác 0 cho chúng mà mong toạ độ dịch theo.</para>
        /// </summary>
        double HomeOffset = 0;

        /// <summary>Vị trí chờ sau khi về gốc xong (bước O6).</summary>
        double StandbyPos = 0;

        /// <summary>
        /// Trục quay tự do, toạ độ quấn vòng về <b>[0, 360)</b> — đúng cho A1/A2 (PLAN.md §11 #10:
        /// "A1/A2 có cảm biến gốc và xoay tự do").
        ///
        /// <para>Bật cờ này đổi ba thứ cùng lúc, và phải đổi cả ba mới nhất quán: (1) vị trí đọc
        /// về được quấn về [0, 360) sau khi cộng <see cref="HomeOffset"/>; (2) lệnh chạy tuyệt đối
        /// chọn vòng gần vị trí hiện tại nhất nên luôn đi đường ngắn — xoay từ 350° tới 10° là
        /// +20°, không phải −340°; (3) <see cref="InSoftLimit"/> luôn đúng, vì mọi góc đều tới
        /// được nên hàng rào hành trình không còn nghĩa gì.</para>
        ///
        /// <para><see cref="SoftLimitNeg"/>/<see cref="SoftLimitPos"/> khi đó chỉ còn để hiển thị
        /// dải hợp lệ (0…360) trên trang Setting.</para>
        /// </summary>
        bool WrapDegrees = false;

        /// <summary>Trục quay tự do thì không có hàng rào hành trình — mọi góc đều tới được, và lệnh
        /// chạy tự quấn về [0, 360) trước khi gửi đi.</summary>
        bool InSoftLimit(double pos) const { return WrapDegrees || (pos >= SoftLimitNeg && pos <= SoftLimitPos); }

    private:
        double m_moveSpeedMid = 0;
        double m_moveSpeedLow = 0;
    };
}
