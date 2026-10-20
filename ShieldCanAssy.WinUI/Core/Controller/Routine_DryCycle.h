#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>

#include "Communication/MotionConnect.h"
#include "Controller/McFunction.h"
#include "Controller/McStatus.h"
#include "Controller/RunClock.h"
#include "Data/Model/ModelData.h"
#include "Data/RunningData.h"
#include "Data/SystemData.h"

namespace sca
{
    /// <summary>
    /// <b>Chạy rà</b> — cho máy chạy liên tục KHÔNG SẢN PHẨM để rà cơ khí (Tuấn yêu cầu 2026-09-23).
    ///
    /// <para><b>Chỉ chạy trục.</b> X, Y, Z, A đi đúng quỹ đạo của chu trình thật — quét tray lắp ráp bằng CAM top-down,
    /// hút ở tray đầu vào, ra vị trí soi CAM dưới, xoay A rồi đặt vào tray lắp ráp, đủ cả hai tay. KHÔNG đụng van hút,
    /// KHÔNG đụng van thổi, KHÔNG kích Flow nào (Tuấn chọn 2026-09-23). Cũng không cộng bộ đếm OK/NG, không ghi trạng thái
    /// ô, không lưu tiến độ.</para>
    ///
    /// <para><b>Vì sao hạ Z xuống hốc trống không va.</b> Cao độ hút/đặt được teach với sản phẩm nằm trong hốc, tức đầu hút
    /// dừng ở MẶT TRÊN của sản phẩm. Bỏ sản phẩm ra thì đầu hút dừng đúng chỗ cũ — lơ lửng trên đáy hốc. Chạy rà đi đúng
    /// cao độ đã teach, không hạ thêm.</para>
    ///
    /// <para><b>Khởi động vẫn qua nút Start cứng</b> (2026-08-20). <b>Checkpoint</b> đặt giữa các chặng, mỗi chặng đều kết
    /// thúc với Z đã lên cao độ an toàn.</para>
    ///
    /// <para><b>Luồng (bản C++).</b> <see cref="Run"/> là HÀM CHẶN trên luồng routine của McControl, gọi
    /// <c>McFunction::MarkCycleFlow()</c> ở đầu; không ném. Bộ đếm và đồng hồ đọc được từ luồng UI.</para>
    /// </summary>
    class Routine_DryCycle
    {
    public:
        Routine_DryCycle(MotionConnect& motion, McFunction& func, McStatus& status, std::function<std::shared_ptr<SystemData>()> sys);
        Routine_DryCycle(const Routine_DryCycle&) = delete;
        Routine_DryCycle& operator=(const Routine_DryCycle&) = delete;

        /// <summary>Xin dừng ở checkpoint gần nhất — McControl đặt khi operator bấm STOP (C#: public volatile field).</summary>
        std::atomic<bool> PauseRequested{ false };

        /// <summary>Góc đặt vừa ra lệnh cho từng tay (1, 2) — để bài tự kiểm đọc được LỆNH, không phải đoán qua encoder.</summary>
        double LastPlaceAngle(int nozzle) const;

        /// <summary>Số nhịp hút–đặt đã chạy kể từ lần bắt đầu gần nhất.</summary>
        int Strokes() const { return m_strokes.load(); }
        /// <summary>Số lần chạy hết một lượt tray lắp ráp.</summary>
        int Laps() const { return m_laps.load(); }
        /// <summary>Số lượt CAM top-down quét tray lắp ráp — cả tray, hoặc một hàng nếu bật quét từng hàng.</summary>
        int Sweeps() const { return m_sweeps.load(); }
        /// <summary>Thời gian đã chạy rà, cộng dồn qua các lần STOP rồi chạy tiếp (C# TimeSpan Elapsed).</summary>
        std::chrono::milliseconds Elapsed() const { return std::chrono::milliseconds(m_clock.ElapsedMilliseconds()); }

        /// <summary>Về vạch xuất phát — gọi khi BẬT chế độ chạy rà, không gọi khi chạy tiếp sau STOP.</summary>
        void ResetCounters();

        /// <summary>Chạy rà (C# RunAsync). Mọi đường ra đều là dừng: true = dừng ở checkpoint (đã đặt State = Paused),
        /// false = dừng vì lỗi/huỷ.</summary>
        bool Run(std::stop_token ct);

        /// <summary>
        /// Ô tray đầu vào của tay <paramref name="nozzle"/> trong cặp bắt đầu ở <paramref name="first"/> (cặp = ô đó và ô
        /// ghé kế tiếp). Chia theo HÌNH HỌC, đúng luật của chu trình thật (<c>Routine_Auto::Nozzle1TakesFirstPickCell</c>) —
        /// Tool 1 lấy ô X nhỏ hơn; cặp cùng cột thì giữ thứ tự ghé (Tuấn 2026-10-15).
        /// </summary>
        static int PickCellFor(const ModelData& model, const TrayState& run, int first, int nozzle);

        /// <summary>Ô tray lắp ráp của tay <paramref name="nozzle"/> trong cặp bắt đầu ở <paramref name="first"/> — đúng luật
        /// của chu trình thật (<c>Routine_Auto::Nozzle1TakesFirstCell</c>): tay #1 lấy ô X nhỏ hơn.</summary>
        static int PlaceCellFor(const ModelData& model, const TrayState& run, int first, int nozzle);

    private:
        // ───────────── bốn chặng ─────────────
        /// <summary>Chặng quét trước tray lắp ráp — đi đúng đường CAM top-down của S05, theo tuỳ chọn ScanAssyByRow.</summary>
        bool Scan(std::stop_token ct);
        /// <summary>Đưa CAM top-down qua từng ô — cả tray (<paramref name="onlyRow"/> &lt; 0) hoặc một hàng; xét yêu cầu
        /// dừng NGAY TRONG vòng quét.</summary>
        bool Sweep(const TrayState& run, int onlyRow, std::stop_token ct);
        /// <summary>Tới ô tray đầu vào, hạ xuống cao độ hút rồi nâng lên — không đụng van hút.</summary>
        bool Pick(int nozzle, std::stop_token ct);
        /// <summary>X ra vị trí soi CAM bottom-up của đúng tay — chạy rà không kích Flow.</summary>
        bool Align(int nozzle, std::stop_token ct);
        /// <summary>Xoay A rồi tới ô tray lắp ráp, hạ xuống cao độ đặt rồi nâng lên — không đụng van thổi.</summary>
        bool Place(int nozzle, std::stop_token ct);

        // ───────────── đi ô, đi góc, đếm ─────────────
        /// <summary>
        /// Phần ngưỡng bù mà chạy rà quét tới. <b>Cố ý KHÔNG quét hết ±ngưỡng</b> (Tuấn chốt 2026-09-24): đầu hút <b>rỗng</b>
        /// hạ vào hốc tray ở góc bất kỳ — xoay tới biên là đẩy gờ đầu hút vào thành hốc. Theo TỶ LỆ của
        /// <c>SystemData::MaxCompensationAngleDeg</c>, không ghi cứng số độ.
        /// </summary>
        static constexpr double DrySwingFraction = 1.0 / 3.0;

        /// <summary>Góc đặt của LẦN ĐẶT NÀY: quanh góc hút 0 → +span → 0 → −span (CmdA của chu trình thật = góc lúc chụp +
        /// góc bù, mà góc lúc chụp bây giờ chính là góc hút).</summary>
        double PlaceAngle(int nozzle);
        static int Wrap(int index, int n);
        /// <summary>Hàng/cột của ô thứ <paramref name="index"/> theo đúng thứ tự đi ô của chu trình thật (rắn bò).</summary>
        static void CellOf(const TrayState& tray, int index, int& row, int& col);
        void Advance();
        double Speed(const std::string& axis);
        bool Paused();
        bool Stopped(const std::string& why);

        // ───────────── trường ─────────────
        MotionConnect& m_motion;
        McFunction& m_func;
        McStatus& m_status;
        std::function<std::shared_ptr<SystemData>()> m_sys;

        int m_inIdx{ 0 }, m_assyIdx{ 0 };

        /// <summary>
        /// Bước dải góc đặt, đếm RIÊNG TỪNG TAY (Tuấn phát hiện 2026-09-24): một biến đếm chung khoá pha với hai lần đặt
        /// mỗi nhịp — A1 không bao giờ xoay, A2 chỉ quanh quẩn ±ngưỡng. Chỉ số 0 không dùng.
        /// </summary>
        std::array<int, 3> m_angleStep{};
        /// <summary>Góc đặt vừa ra lệnh cho từng tay (chỉ số 0 không dùng).</summary>
        std::array<std::atomic<double>, 3> m_lastPlaceAngle{};

        std::atomic<int> m_strokes{ 0 }, m_laps{ 0 }, m_sweeps{ 0 };

        /// <summary>Hàng đã quét gần nhất — chỉ dùng khi bật quét từng hàng. −1 = chưa quét hàng nào.</summary>
        int m_scannedRow{ -1 };
        RunClock m_clock;
    };
}
