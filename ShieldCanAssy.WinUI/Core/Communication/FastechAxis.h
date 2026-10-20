#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "Base/Async.h"
#include "Communication/AxisScale.h"
#include "Communication/IAxis.h"

namespace sca
{
    /// <summary>
    /// Một trục Fastech Ezi-SERVO Plus-E qua Ethernet (A1 hoặc A2), bọc <c>EziMOTIONPlusELib</c>.
    ///
    /// <para><b>Khác Panasonic ở hai điểm quan trọng.</b> (1) Thư viện Fastech là gọi hàm đồng
    /// bộ và <b>không</b> có luồng monitor riêng, nên <see cref="Refresh"/> phải tự gọi
    /// <c>FAS_GetAllStatus</c> — một lần gọi UDP mỗi 20 ms cho mỗi trục. (2) Tốc độ tính bằng
    /// <b>xung/giây</b> chứ không phải rpm.</para>
    ///
    /// <para><b>Cảnh báo về bảng bit trạng thái:</b> xem <see cref="EziAxisStatus"/>. Vị trí và
    /// tốc độ lấy từ trường số nguyên riêng của <c>FAS_GetAllStatus</c> nên không phụ thuộc
    /// bảng bit; chỉ các cờ boolean mới cần xác minh khi có máy.</para>
    ///
    /// <para><b>Số hiệu board (<c>iBdID</c>) là định danh nội bộ của thư viện, không phải IP.</b>
    /// Mọi module Fastech trong cùng process — 2 trục xoay và 2 card I/O — phải mang
    /// <c>iBdID</c> khác nhau, nếu không <c>FAS_Connect</c> của cái sau sẽ đè lên cái trước và
    /// triệu chứng là "trục A2 điều khiển ra card I/O". Bảng phân bổ nằm ở
    /// <see cref="FastechBoardId"/>.</para>
    ///
    /// <para>Luồng (bản C++): Refresh trên vòng quét; lệnh (SetServoOn/Home/MoveAbs/MoveInc/ResetAlarm) chặn trên luồng
    /// gọi; Jog/Stop đẩy lệnh sang <c>base::RunDetached</c> rồi trả về ngay. Mọi lời gọi lệnh xuống thư viện đi qua
    /// <c>m_cmdLock</c> (C# <c>lock (_cmdLock)</c>); đọc trạng thái của Refresh thì không, như bản C#.</para>
    /// </summary>
    class FastechAxis : public IAxis, public std::enable_shared_from_this<FastechAxis>
    {
    public:
        explicit FastechAxis(std::shared_ptr<AxisConfig> cfg);
        FastechAxis(const FastechAxis&) = delete;
        FastechAxis& operator=(const FastechAxis&) = delete;

        const std::string& Name() const override { return m_name; }
        std::shared_ptr<AxisConfig> Config() const override { return m_cfg; }

        /// <summary>Thanh ghi trạng thái thô — hiện trên trang Manual để xác minh bảng bit.</summary>
        uint32_t RawStatus() const { return m_rawStatus.load(); }
        std::string RawStatusText() const;

        bool IsConnected() const override { return m_connected.load(); }
        bool ServoOn() const override { return m_servoOn.load(); }
        bool Busy() const override { return m_busy.load(); }
        bool Alarm() const override { return m_alarm.load(); }
        bool OrgOk() const override { return m_orgOk.load(); }
        bool FLS() const override { return m_fls.load(); }
        bool RLS() const override { return m_rls.load(); }
        double CurrentPos() const override { return m_pos.load(); }
        int ErrorCode() const override { return m_errorCode.load(); }

        // ───────────────────────── kết nối ─────────────────────────

        bool Connect() override;
        void Disconnect() override;

        // ───────────────────────── đọc trạng thái ─────────────────────────

        void Refresh() override;

        // ───────────────────────── lệnh ─────────────────────────

        bool SetServoOn(bool on) override;
        bool Home(std::stop_token ct) override;
        bool MoveAbs(double pos, double speed, std::stop_token ct, bool gentle = false) override;

        /// <summary>
        /// Di chuyển TƯƠNG ĐỐI (Inching) — dùng lệnh gốc FAS_MoveSingleAxisIncPos của driver
        /// thay vì tự cộng dồn rồi gọi lại Move Abs (khác Panasonic, chưa có lệnh gốc tương ứng).
        /// Soft limit vẫn kiểm theo đích TUYỆT ĐỐI quy ra từ vị trí hiện tại, và dùng chung vòng
        /// chờ với Move Abs (<see cref="WaitReached"/>).
        /// </summary>
        bool MoveInc(double delta, double speed, std::stop_token ct) override;

        /// <summary>
        /// Jog/Stop bị gọi THẲNG từ sự kiện chuột trên UI thread (trang Manual Jog_Down/Up) — bắn nền bằng
        /// <c>base::RunDetached</c> thay vì gọi thẳng <c>EziMOTIONPlusELib::FAS_*</c> tại chỗ: card/driver treo (đúng lúc
        /// báo lỗi) sẽ khoá cứng UI thread nếu gọi đồng bộ ở đây, y hệt lỗi đã sửa ở PanasonicAxis (Tuấn báo lỗi,
        /// 2026-09-08).
        /// </summary>
        void Jog(int dir, double speed) override;
        void Stop() override;

        bool ResetAlarm() override;

    private:
        /// <summary>Một vòng đọc lỗi. Chỉ hạ cờ kết nối khi đã lỗi liên tiếp đủ <see cref="FailLimit"/> vòng.</summary>
        void ReadFailed(const std::string& why);

        /// <summary>Một vòng đọc được. Báo lại một dòng nếu vừa đi qua một đoạn mất kết nối.</summary>
        void ReadOk();

        /// <summary>
        /// Xung thô của driver → toạ độ máy. Với trục quay: cộng offset gốc rồi quấn về [0, 360).
        ///
        /// <para>Cảm biến gốc của A1/A2 <b>không</b> nằm đúng tư thế gắp khay input — nó nằm ở đâu
        /// là do lắp đặt. <see cref="AxisConfig::HomeOffset"/> là góc máy TẠI cảm biến gốc đó, nên
        /// cộng nó vào là đủ để 0° của app trở thành tư thế gắp thật. Cách đo: về gốc, jog tới tư
        /// thế gắp, đọc góc đang hiện là θ, đặt HomeOffset = (360 − θ) mod 360.</para>
        /// </summary>
        double ToMachine(int pulse) const;

        /// <summary>
        /// Toạ độ máy → xung tuyệt đối để gửi cho driver.
        ///
        /// <para>Với trục quay, một góc ứng với vô số con số xung cách nhau đúng một vòng. Chọn
        /// con số <b>gần vị trí hiện tại nhất</b> — đó chính là điều làm cho lệnh "xoay tới 10°"
        /// khi đang ở 350° quay +20° chứ không tháo ngược −340°. Điểm mốc <c>basePulse</c> luôn
        /// tính lại từ góc chứ không cộng dồn, nên sai số làm tròn không tích luỹ qua các lần
        /// chạy.</para>
        /// </summary>
        int TargetPulse(double pos) const;

        /// <summary>
        /// Vòng chờ chung cho Move Abs/Move Inc — cả hai chỉ khác ở lệnh KHỞI ĐỘNG chuyển động,
        /// còn chờ tới đích thì giống hệt nhau.
        ///
        /// <para>So sánh bằng <b>xung</b>, không bằng đơn vị kỹ thuật: với trục quay, toạ độ đã
        /// quấn về [0, 360) nên phép trừ thẳng cho kết quả vô nghĩa ngay tại chỗ nối vòng — đích
        /// 0° và vị trí 359.99° chênh nhau 0.01° thật nhưng trừ ra 359.99. Đối chiếu trên con số
        /// thô thì không có chỗ nối nào để mà sai.</para>
        /// </summary>
        bool WaitReached(int targetPulse, std::stop_token ct);

        /// <summary>Giữ đối tượng sống tới khi việc RunDetached xong (nullptr nếu không do shared_ptr sở hữu).</summary>
        std::shared_ptr<FastechAxis> KeepAlive();

        std::string m_name;
        std::shared_ptr<AxisConfig> m_cfg;
        AxisScale m_scale;
        const int m_bdId;
        std::mutex m_cmdLock;

        std::atomic<uint32_t> m_rawStatus{ 0 };

        /// <summary>
        /// Vị trí THÔ đọc từ driver, tính bằng xung — bộ đếm riêng của driver, chưa cộng
        /// <see cref="AxisConfig::HomeOffset"/> và chưa quấn về [0, 360).
        ///
        /// <para>Giữ lại con số thô là điều kiện bắt buộc để trục quay chạy đúng: từ một góc đã
        /// quấn (ví dụ 10°) không thể biết driver đang ở vòng nào, mà lệnh chạy tuyệt đối thì
        /// cần đúng một con số xung. Mọi phép chọn vòng gần nhất đều đo từ đây.</para>
        /// </summary>
        std::atomic<int> m_rawPulse{ 0 };

        std::atomic<bool> m_connected{ false };
        std::atomic<bool> m_servoOn{ false };
        std::atomic<bool> m_busy{ false };
        std::atomic<bool> m_alarm{ false };
        std::atomic<bool> m_orgOk{ false };
        std::atomic<bool> m_fls{ false };
        std::atomic<bool> m_rls{ false };
        std::atomic<double> m_pos{ 0.0 };
        std::atomic<int> m_errorCode{ 0 };

        /// <summary>Cờ về gốc của phần mềm — cùng lý do như PanasonicAxis (PLAN.md §4.1).</summary>
        std::atomic<bool> m_softHomed{ false };

        // ───────── Mất kết nối chớp nhoáng ≠ mất kết nối thật ─────────
        //
        // Trước 14/09/2026: MỘT lần FAS_GetAllStatus lỗi là IsConnected = false, mà Refresh() lại mở đầu bằng
        // "chưa kết nối thì thôi" — không còn đường nào đọc lại, kể cả khi driver trả lời bình thường ngay sau
        // đó. Connect() chỉ chạy đúng một lần lúc McControl.Init(), nên trục CHẾT tới khi mở lại app. Đây chính
        // là triệu chứng Tuấn báo 2026-09-14: "A1 hay báo mất kết nối, tắt app bật lại là bình thường".
        //
        // Nay làm như IoConnect: đếm số vòng quét LỖI LIÊN TIẾP, và vẫn đọc tiếp khi đang mất kết nối. Giao thức
        // là UDP — rơi một gói là chuyện thường, năm vòng liên tiếp thì không.
        static constexpr int FailLimit = 5;      // 5 × 20 ms = 100 ms

        /// <summary>Đang mất kết nối thì giãn nhịp đọc: lúc rút cáp thật, mỗi lần gọi thư viện phải chờ hết
        /// timeout của chính nó — gọi mỗi vòng 20 ms sẽ kéo chậm cả vòng quét an toàn.</summary>
        static constexpr int RetryMs = 500;

        /// <summary>Đã mở được socket chưa (Connect thành công). Khác <see cref="IsConnected"/> = "đang đọc
        /// được": Refresh chỉ nghỉ hẳn khi CHƯA mở, không nghỉ khi đang mất kết nối.</summary>
        std::atomic<bool> m_opened{ false };
        std::atomic<int> m_failStreak{ 0 };

        std::mutex m_sinceReadMutex;             // m_sinceRead, m_sinceReadRunning
        base::Stopwatch m_sinceRead;
        bool m_sinceReadRunning = false;         // C# Stopwatch.IsRunning
    };
}
