#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>

#include "Base/Async.h"
#include "Communication/AxisScale.h"
#include "Communication/IAxis.h"
#include "ServoControl/ServoService.h"

namespace sca
{
    /// <summary>
    /// Một trục Panasonic MINAS A6SF qua Modbus RTU, bọc <c>servo::ServoService</c>.
    ///
    /// <para><b>Mỗi trục một cổng COM riêng, mỗi trục một <c>ServoService</c> riêng</b>
    /// (PLAN.md §1.1). <c>ServoService</c> vốn quản được nhiều trục trên một bus RS485, nhưng
    /// máy này tách bus nên mỗi instance chỉ khai báo đúng một trục và <c>UnitId</c> của cả 5
    /// trục đều có thể để là 1. Đổi lại: 5 luồng polling chạy song song, phải đo lại thời gian
    /// quét thực tế khi có máy.</para>
    ///
    /// <para><b>Trạng thái đọc từ luồng monitor của ServoService</b>, không hỏi Modbus trong
    /// <see cref="Refresh"/>. <c>Refresh</c> được gọi mỗi 20 ms từ vòng điều khiển — hỏi Modbus
    /// ở đó sẽ khoá vòng điều khiển theo tốc độ đường truyền nối tiếp.</para>
    ///
    /// <para><b>Chưa kiểm chứng trên thiết bị thật.</b> Viết trước để khi máy về chỉ còn cấu
    /// hình driver và xác định chiều/giới hạn, không phải vừa gõ code vừa đứng cạnh máy đang
    /// bật servo. Hai việc chắc chắn phải làm ở P1: (1) dựng bảng block trong PANATERM — số hiệu
    /// block là tham số của driver, phần mềm chỉ kích chúng; (2) cấu hình tỉ số truyền điện tử
    /// (Pr0.08/Pr0.09) sao cho ra đúng 1000 xung/mm — <see cref="AxisScale"/> đã cố định con số
    /// này ở phía phần mềm, không phải đo trên máy. Cách về gốc (DOG + xung Z hay kiểu khác) là
    /// tham số bên trong driver (Pr5.xx); <see cref="Home"/> chỉ gọi <c>StartHome</c> rồi
    /// chờ <c>ORG_CMP</c>, không cần biết driver về gốc bằng cách nào.</para>
    ///
    /// <para>Luồng (bản C++): Refresh trên vòng quét (chỉ đọc bộ nhớ đệm của thư viện); lệnh dài chặn trên luồng gọi;
    /// Jog/Stop đẩy lệnh sang <c>base::RunDetached</c>. Mọi lệnh ghi Modbus qua chung <c>m_cmdLock</c>.</para>
    /// </summary>
    class PanasonicAxis : public IAxis, public std::enable_shared_from_this<PanasonicAxis>
    {
    public:
        explicit PanasonicAxis(std::shared_ptr<AxisConfig> cfg);

        /// <summary>
        /// Chỉ cho kiểm thử: nối qua <paramref name="port"/> (vd. ổ A6SF giả <c>servo::FakeA6sfPort</c>) thay vì mở
        /// <see cref="AxisConfig::ComPort"/> — C# không cần vì self-test của nó chỉ thử ServoService trên cổng giả.
        /// nullptr = mở cổng COM như constructor thường.
        /// </summary>
        PanasonicAxis(std::shared_ptr<AxisConfig> cfg, std::shared_ptr<servo::ISerialPort> port);

        PanasonicAxis(const PanasonicAxis&) = delete;
        PanasonicAxis& operator=(const PanasonicAxis&) = delete;

        const std::string& Name() const override { return m_name; }
        std::shared_ptr<AxisConfig> Config() const override { return m_cfg; }

        bool IsConnected() const override { return m_connected.load(); }
        bool ServoOn() const override { return m_servoOn.load(); }
        bool Busy() const override { return m_busy.load(); }
        bool Alarm() const override { return m_alarm.load(); }
        bool OrgOk() const override { return m_orgOk.load(); }
        bool FLS() const override { return m_fls.load(); }
        bool RLS() const override { return m_rls.load(); }
        double CurrentPos() const override { return m_pos.load(); }
        int ErrorCode() const override { return m_errorCode.load(); }

        /// <summary>Thư viện bên dưới — cho kiểm thử đọc trạng thái/OnLog. Không gọi lệnh qua đây (bỏ qua m_cmdLock).</summary>
        std::shared_ptr<servo::ServoService> Service() const { return m_servo; }

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
        /// Di chuyển TƯƠNG ĐỐI (Inching). Driver Panasonic hiện chỉ có 4 block cấu hình sẵn qua
        /// PANATERM — Home/Abs/Jog+/Jog− (xem <see cref="AxisConfig::HomeBlock"/>/<see cref="AxisConfig::AbsoluteBlock"/>/
        /// Jog*Block) — CHƯA có block lệnh tương đối (khác Fastech, có sẵn FAS_MoveSingleAxisIncPos).
        /// Bù bằng phần mềm: cộng dồn vào vị trí hiện tại rồi gọi lại đúng Move Abs — nếu sau này
        /// lập thêm một block Inc riêng trong PANATERM thì đổi thẳng ở đây, chỗ gọi (McFunction/
        /// AxisRowVm) không phải đổi gì.
        /// </summary>
        bool MoveInc(double delta, double speed, std::stop_token ct) override;

        /// <summary>
        /// Jog/Stop là <c>void</c> (interface <c>IAxis</c>) và bị gọi THẲNG từ sự kiện chuột trên
        /// UI thread (trang Manual Jog_Down/Up) — không được chờ hay chặn ở đây, phải bắn nền bằng
        /// <c>base::RunDetached</c> rồi quay về ngay. Vẫn qua chung <c>m_cmdLock</c> với Home/MoveAbs để giữ đúng thứ
        /// tự lệnh trên bus RS485.
        /// </summary>
        void Jog(int dir, double speed) override;
        void Stop() override;

        bool ResetAlarm() override;

        // ───────────────────────── luật thuần (self-test khoá được không cần driver) ─────────────────────────

        /// <summary>
        /// Dung sai mà thư viện dùng để BỎ QUA lệnh chạy (<c>ServoConfig::SkipMoveTolerance</c>) — tách khỏi dung sai "đã
        /// tới" (Tuấn 2026-10-15: "nhận lệnh chính xác 0.01"). Kẹp không quá <see cref="AxisConfig::PositionTolerancePulse"/>:
        /// thư viện bỏ một lệnh mà App vẫn thấy chưa tới thì App gửi lại vô ích rồi báo 2302 — đúng lỗi Y1 hôm 15/10.
        /// </summary>
        static int LibrarySkipTolerance(const AxisConfig& cfg);

        /// <summary>Gửi lại tối đa từng này lần khi driver không nhận lệnh chạy; quá thì báo lỗi ngay, không đợi hết 30 s.</summary>
        static constexpr int MaxMoveResends = 3;

        enum class EMoveWait { Done, Wait, Resend, Lost };

        /// <summary>
        /// Một lượt xét lệnh chạy tuyệt đối đang chờ (Tuấn 2026-10-01, theo cách AutoScrews làm). Tách thành hàm thuần cho
        /// self-test khoá được luật mà không cần driver thật.
        /// <list type="bullet">
        /// <item><b>Xong</b> = vào dung sai và driver hết BUSY — KHÔNG chờ INP. Trước đây chờ cả INP: trục đã tới nơi mà
        /// driver còn báo INP muộn, chặng nâng Z cùng quãng lúc 0.3 s lúc 0.6 s. Rung dư sau khi dừng không ảnh hưởng ảnh
        /// chụp: trước khi kích Flow có camera đã có <c>McFunction.WaitSettle</c>.</item>
        /// <item><b>Gửi lại</b> khi hai lượt đọc trọn vẹn SAU lúc gửi vẫn thấy driver không BUSY, trục chưa tới và CHƯA HỀ
        /// nhúc nhích khỏi chỗ đứng lúc gửi: driver không nhận lệnh (mất khung Modbus — ServoService nuốt lỗi ghi; 30/09 09:42
        /// Z1 đứng yên rồi quá 30 s). Phải đủ hai lượt đọc mới sau lúc gửi: số đọc trước đó là của trước lệnh, gửi lại lúc
        /// trục đang chạy là chồng lệnh. Phải chưa nhúc nhích: BUSY tắt khi driver phát xong lệnh, trục chạy nhanh có thể còn
        /// đang về đích ngoài dung sai — đó là đang tới, không phải mất lệnh.</item>
        /// <item><b>Lệnh đã xuống driver</b> (<paramref name="sent"/>) thì chưa đủ hai lượt đọc sau lúc gửi chưa được coi là
        /// xong, kể cả khi đang trong dung sai (Tuấn 2026-10-15): thư viện nay gửi cả lệnh 0.01 mm, nằm gọn trong dung sai
        /// "đã tới" 0.05 mm — coi là xong ngay thì App đi tiếp (ghi toạ độ cho vision…) trong lúc trục còn đang nhích. Thư
        /// viện KHÔNG gửi (đã ở đích) thì xong ngay, không tốn thêm lượt đọc nào.</item>
        /// </list>
        /// </summary>
        static EMoveWait JudgeMove(bool there, bool busy, int pollsSinceSend, bool movedSinceSend, int resends, bool sent);

        /// <summary>
        /// "Đã tới" — so bằng XUNG (Tuấn 2026-10-15). Trước đây so bằng mm số thực: lệch đúng 50 xung thì thư viện cho là đã
        /// tới, không gửi gì xuống driver, còn App tính 46.520 − 46.470 = 0.0500000004 &gt; 0.05 là chưa tới → gửi lại 3 lần,
        /// lần nào thư viện cũng bỏ qua → 2302. Gặp ở Y1 khi hai tay hút cùng hàng mà lưới hai tay lệch nhau ~0.05 mm. Nay
        /// thư viện chỉ bỏ qua trong <see cref="LibrarySkipTolerance"/> (≤ dung sai này) nên thư viện bỏ ⇒ App thấy đã tới.
        /// </summary>
        static bool WithinPulses(int actualPulse, int targetPulse, int tolPulse);

    private:
        /// <summary>
        /// Khoá lệnh — C# <c>SemaphoreSlim(1, 1)</c>: khoá nhị phân không đệ quy, chờ được có huỷ (stop_token) hoặc có hạn
        /// giờ. Bản C# cũ dùng <c>lock (_cmdLock) { task.GetAwaiter().GetResult() }</c>: nút Jog/Stop gọi thẳng từ sự
        /// kiện chuột trên UI thread, nên khi RS485 treo (driver báo lỗi, không phản hồi) thì UI thread bị khoá cứng theo
        /// — cả app đơ, không bấm được gì (Tuấn báo lỗi, 2026-09-08). Giữ nguyên MỘT khoá dùng chung cho MỌI lệnh ghi
        /// Modbus (Jog/Stop/Home/MoveAbs/SVO on-off/Reset alarm) — không jog được trong lúc đang Home/MoveAbs và ngược lại.
        ///
        /// <para><see cref="Disconnect"/> cũng xin khoá này trước khi đóng cổng COM — đóng cổng giữa lúc một lệnh khác
        /// đang gửi/đợi phản hồi trên CÙNG cổng dễ làm driver/bus RS485 kẹt tới khi rút/cắm lại nguồn, không kết nối lại
        /// được (Tuấn lo ngại lúc đóng app, 2026-09-10). <c>McControl::Shutdown</c> đã huỷ <c>Safety.AbortToken</c>
        /// trước khi gọi tới đây nên các lệnh đang chờ vị trí (Home/MoveAbs) thoát nhanh, không giữ khoá lâu.</para>
        /// </summary>
        class CmdLock
        {
        public:
            void Wait();
            /// <summary>C# <c>WaitAsync(ct)</c>: huỷ trước khi có khoá thì ném <c>base::OperationCanceled</c>.</summary>
            void Wait(std::stop_token ct);
            /// <summary>C# <c>Wait(timeoutMs)</c>: false nếu hết giờ mà chưa có khoá.</summary>
            bool Wait(int timeoutMs);
            void Release();

        private:
            std::mutex m_mutex;
            std::condition_variable_any m_cv;
            bool m_taken = false;
        };

        /// <summary>Lệnh chạy: lỗi phát lệnh / thư viện không gửi (đã ở đích, driver chưa sẵn sàng) / đã xuống driver.</summary>
        enum class ESend { Failed, Skipped, Sent };

        /// <summary>Accel/decel đồng bộ vào <c>m_axisCfg</c> ngay trước lệnh chạy — xem <see cref="MoveAbs"/>.</summary>
        struct RampUpdate
        {
            uint16_t AccelMs;
            uint16_t DecelMs;
            uint16_t RampRpm;
        };

        ESend SendMove(int target, uint16_t rpm, std::stop_token ct, const RampUpdate* ramp = nullptr);

        /// <summary>Số lượt đọc trạng thái thành công của driver tới lúc này — xem <c>ServoAxisState::PollCount</c>.</summary>
        int PollNow() const;

        /// <summary>Chờ lệnh chạy tới nơi — luật ở <see cref="JudgeMove"/>.</summary>
        bool WaitMotion(double target, int targetPulse, uint16_t rpm, bool sent, std::stop_token ct);

        /// <summary>Một vòng đọc lỗi. Chỉ hạ cờ kết nối khi đã lỗi liên tiếp đủ <see cref="FailLimit"/> vòng.</summary>
        void ReadFailed(const std::string& why);

        /// <summary>Một vòng đọc được. Báo lại một dòng nếu vừa đi qua một đoạn mất kết nối.</summary>
        void ReadOk();

        /// <summary>
        /// Đưa thời gian tăng/giảm tốc gõ ở trang Setting về dải dùng được.
        ///
        /// <para>Số ≤ 0 KHÔNG hạ về 1ms mà quay lại 250ms mặc định: 0 là con số gõ nhầm hoặc ô
        /// còn trống, mà hiểu nó theo nghĩa đen thì thành lệnh tăng tốc gần như tức thời — hỏng
        /// máy hoặc bung alarm quá dòng. Rơi về giá trị máy vẫn chạy bấy lâu nay là hướng sai an
        /// toàn hơn hẳn.</para>
        /// </summary>
        static uint16_t ClampRampMs(int ms);

        /// <summary>
        /// C# <c>task.Wait(timeoutMs)</c> quanh một lệnh của thư viện: chạy <paramref name="work"/> trên luồng nền, chờ tối
        /// đa timeoutMs. Hết giờ thì bỏ lại (việc vẫn chạy nốt ở nền, giữ ServoService sống) — false.
        /// </summary>
        bool RunWithTimeout(std::function<void(servo::ServoService&)> work, int timeoutMs);

        /// <summary>Giữ đối tượng sống tới khi việc RunDetached xong (nullptr nếu không do shared_ptr sở hữu).</summary>
        std::shared_ptr<PanasonicAxis> KeepAlive();

        std::string m_name;
        std::shared_ptr<AxisConfig> m_cfg;
        AxisScale m_scale;
        std::shared_ptr<servo::ServoService> m_servo;
        const int m_unitId;
        std::shared_ptr<servo::ISerialPort> m_port;     // nullptr = mở AxisConfig::ComPort

        /// <summary>
        /// Chính đối tượng cấu hình đã trao cho thư viện lúc <see cref="Connect"/> — giữ lại để
        /// đồng bộ thời gian tăng/giảm tốc trước mỗi lệnh chạy.
        ///
        /// <para>Tốc độ đi kèm từng lệnh nên sửa ở trang Setting là ăn ngay; còn accel/decel nằm
        /// trong cấu hình chỉ đọc một lần lúc kết nối, không cập nhật thì phải khởi động lại app
        /// mới có tác dụng — trái với mong đợi của người dùng khi hai ô nằm cùng một bảng.</para>
        ///
        /// <para>Đồng bộ trước mỗi lệnh KHÔNG có nghĩa là ghi xuống driver trước mỗi lệnh: thư viện
        /// chỉ ghi thanh ghi acc/dec khi con số đổi (sửa ở Setting, hoặc Jog vừa ghi đè cùng ô), vì
        /// độ dốc ramp đã cố định theo mức Auto cao — xem <see cref="MoveAbs"/>. Bản C++ chỉ sửa các trường này khi đang
        /// giữ <c>m_cmdLock</c> (thư viện đọc chúng trong lệnh, trên luồng gọi lệnh).</para>
        /// </summary>
        std::shared_ptr<servo::ServoAxisConfig> m_axisCfg;
        mutable std::mutex m_axisCfgMutex;       // con trỏ m_axisCfg (Connect thay, lệnh chạy đọc)
        std::shared_ptr<servo::ServoAxisConfig> AxisCfg() const;

        CmdLock m_cmdLock;

        std::atomic<bool> m_connected{ false };
        std::atomic<bool> m_servoOn{ false };
        std::atomic<bool> m_busy{ false };
        std::atomic<bool> m_alarm{ false };
        std::atomic<bool> m_orgOk{ false };
        std::atomic<bool> m_fls{ false };
        std::atomic<bool> m_rls{ false };
        std::atomic<double> m_pos{ 0.0 };
        std::atomic<int> m_errorCode{ 0 };

        /// <summary>
        /// Cờ về gốc của PHẦN MỀM, tách khỏi <c>ORG_CMP</c> của driver.
        ///
        /// <para><c>ORG_CMP</c> chỉ biết driver đã từng về gốc kể từ lần bật nguồn của NÓ; nó
        /// không biết phần mềm vừa khởi động lại, không biết ai vừa đổi tham số scale. Tin nó là
        /// tin rằng mọi thứ ngoài driver đều không đổi (PLAN.md §4.1). Tắt/bật servo thì KHÔNG
        /// xoá cờ này (Tuấn bỏ 2026-09-26).</para>
        /// </summary>
        std::atomic<bool> m_softHomed{ false };

        // ───────── Mất kết nối chớp nhoáng ≠ mất kết nối thật ─────────
        //
        // Trước 14/09/2026: Refresh() mở đầu bằng "chưa kết nối thì thôi", mà chính nó lại là chỗ duy nhất
        // đặt lại cờ kết nối. Vòng quét Modbus của thư viện chỉ cần lỗi MỘT nhịp (timeout, sai CRC, nhiễu trên
        // cáp USB-RS485) là cờ hạ xuống, và từ đó App không bao giờ hỏi lại nữa — dù thư viện vẫn poll đều và
        // đã tự dựng cờ lên lại ở nhịp sau (ServoService::PollAxisState đặt IsConnected = true ở dòng cuối).
        // Trục chết tới khi mở lại app: đúng triệu chứng Tuấn báo 2026-09-14 ("Y1 hay mất kết nối").
        //
        // Nay đếm số vòng quét LỖI LIÊN TIẾP như IoConnect. Ngưỡng rộng hơn bản Fastech vì ở đây App đọc bộ
        // nhớ đệm của thư viện: một nhịp poll hỏng còn treo cờ tới lượt quét kế của chính thư viện
        // (MonitorIntervalMs đặt trong Connect, cộng thời gian 5 giao dịch Modbus của trục đó).
        // 30/09: 15 → 50. Y1 lỗi đọc quá 300 ms rồi đọc lại được ngay nhịp thử kế (log 09:51:01) — vẫn dừng máy oan.
        static constexpr int FailLimit = 50;     // 50 × 20 ms = 1 s

        /// <summary>Đang mất kết nối thì giãn nhịp hỏi lại — xem bản Fastech.</summary>
        static constexpr int RetryMs = 500;

        /// <summary>Đã mở được cổng chưa (Connect thành công). Khác <see cref="IsConnected"/> = "đang đọc được".</summary>
        std::atomic<bool> m_opened{ false };
        std::atomic<int> m_failStreak{ 0 };

        std::mutex m_sinceReadMutex;             // m_sinceRead, m_sinceReadRunning
        base::Stopwatch m_sinceRead;
        bool m_sinceReadRunning = false;         // C# Stopwatch.IsRunning
    };
}
