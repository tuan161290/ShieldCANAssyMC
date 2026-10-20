#pragma once
#include <atomic>
#include <climits>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>

#include "Base/Async.h"
#include "Communication/IIoDevice.h"
#include "Communication/MotionConnect.h"
#include "Communication/VmContract.h"
#include "Controller/McStatus.h"
#include "Controller/SafetyMonitor.h"
#include "Data/SystemData.h"

namespace sca
{
    /// <summary>
    /// Lớp phát lệnh chuyển động cấp cao. <b>Toàn bộ interlock nằm ở đây</b>, không nằm trong
    /// routine (PLAN.md §5.1) — routine chỉ mô tả trình tự công việc, còn "được phép chạy hay
    /// không" là câu hỏi chỉ có một chỗ trả lời. Thêm một điều kiện chống va mới thì sửa ở đây
    /// là mọi routine đều được bảo vệ.
    ///
    /// <para>Các hàm Move*/EnsureZSafe/Wait* là HÀM CHẶN — gọi từ luồng nền, không gọi từ luồng UI. Bị huỷ
    /// (<c>ct</c>) thì có thể ném <c>base::OperationCanceled</c> (như C# ném OperationCanceledException từ
    /// <c>WaitRunBitAsync</c>/<c>MoveAbsAsync</c>) — routine bắt ở vòng ngoài.</para>
    /// </summary>
    class McFunction
    {
    public:
        McFunction(MotionConnect& motion, SafetyMonitor& safety, McStatus& status, std::function<std::shared_ptr<SystemData>()> sys);
        McFunction(const McFunction&) = delete;
        McFunction& operator=(const McFunction&) = delete;

        /// <summary>C# property Io. Đặt lúc khởi tạo, trước khi chia sẻ đối tượng.</summary>
        std::shared_ptr<IIoDevice> Io() const { return m_io; }
        void SetIo(std::shared_ptr<IIoDevice> io) { m_io = std::move(io); }

        // ── Phanh Z1/Z2 (DO qua Ezi-IO, KHÔNG dùng tín hiệu phanh tích hợp của driver
        // Panasonic — Tuấn quyết định, 2026-09-04) ──
        //
        // Phanh KHÔNG tự nhả chỉ vì bật servo — operator phải chủ động vào Manual, bật servo,
        // rồi bấm "Mở phanh" MỘT LẦN; sau đó phanh giữ nhả liên tục xuyên suốt Về gốc VÀ chu
        // trình Auto tiếp theo, vì cờ yêu cầu chỉ mất đi khi Alarm hoặc Servo OFF thật sự xảy ra
        // — không phải vì đổi Mode hay chuyển bước routine (Tuấn yêu cầu, 2026-09-04). Không có
        // đường nào khác (kể cả Routine_Auto/Routine_Origin) tự đặt cờ này hộ operator được.

        /// <summary>
        /// Nút "Mở phanh" trên trang Manual — operator phải CHỦ ĐỘNG bấm sau khi đã bật servo,
        /// phanh KHÔNG tự nhả chỉ vì ServoOn (Tuấn yêu cầu, 2026-09-04). Gọi được từ bất kỳ
        /// thread nào (UI); vòng quét mới thực sự áp dụng qua <see cref="UpdateBrakeOutputs"/>.
        /// Bấm lúc trục chưa ServoOn không có tác dụng gì — <c>UpdateBrakeOutput</c> chỉ
        /// đọc cờ này khi trục đã sẵn sàng, và tự xoá cờ ngay khi mất ServoOn/có Alarm.
        /// </summary>
        void RequestBrakeRelease(std::string_view axisName);

        /// <summary>
        /// Nhả phanh KHẨN CẤP kiểu TOGGLE — để đẩy tay được trục Z ngay cả khi Alarm hoặc Servo
        /// OFF (Tuấn yêu cầu, 2026-09-04): sự cố thường xảy ra đúng lúc servo đã mất, nên đường
        /// bình thường (<see cref="RequestBrakeRelease"/>, đòi ServoOn) không dùng được lúc cần nhất.
        ///
        /// <para><b>Đổi từ dead-man sang toggle (Tuấn yêu cầu, 2026-09-09, đã xác nhận rủi ro).</b> Bật
        /// (<paramref name="armed"/>=true) thì phanh nhả và GIỮ NGUYÊN dù buông tay/UI mất focus — KHÔNG tự khoá
        /// lại, chỉ tắt khi gọi lại hàm này với <paramref name="armed"/>=false. Rủi ro trục rơi tự do nếu quên tắt
        /// đã được operator chấp nhận.</para>
        ///
        /// <para>KHÔNG kiểm tra ServoOn/Alarm/SafeToMove — chủ ý để dùng được trong lúc có sự cố. Không tự cho phép
        /// chuyển động bằng động cơ: <see cref="CanMove"/> vẫn từ chối bình thường khi Servo OFF.</para>
        /// </summary>
        void SetEmergencyBrakeRelease(std::string_view axisName, bool armed);
        bool IsEmergencyBrakeReleaseArmed(std::string_view axisName) const;

        /// <summary>
        /// Điều khiển 2 tín hiệu DO nhả phanh Z1/Z2 — gọi mỗi vòng quét từ McControl, đây là ĐƯỜNG DUY NHẤT ghi hai
        /// DO này. Nhả bình thường khi ĐỦ CẢ BA: ServoOn, không Alarm, VÀ operator đã bấm "Mở phanh" — TỰ ĐỘNG khoá
        /// lại và XOÁ LUÔN yêu cầu ngay khi Alarm hoặc Servo OFF. HOẶC nhả khi đang bật cờ khẩn cấp.
        /// </summary>
        void UpdateBrakeOutputs();

        /// <summary>Phanh của trục này (nếu có) đã nhả đủ lâu để coi là an toàn di chuyển chưa —
        /// trục không phanh (khác Z1/Z2) luôn true. Public vì <c>Routine_Origin</c> cũng cần hỏi
        /// điều kiện này trước khi cho Về gốc chạy (Tuấn yêu cầu, 2026-09-04).</summary>
        bool BrakeReady(std::string_view axisName) const;

        /// <summary>
        /// Kiểm tra được phép chạy trục này chưa. Trả về false kèm lý do đọc được để UI hiện
        /// thẳng cho operator thay vì im lặng không phản ứng.
        /// </summary>
        bool CanMove(std::string_view axisName, std::string& reason) const;

        /// <summary>
        /// Cùng chốt chặn với <see cref="CanMove"/>, cộng điều kiện cho nút "Về gốc"/"Move Abs" bấm RIÊNG LẺ ở trang
        /// Manual: bit RUN bật, và Z1 VÀ Z2 phải ĐÃ VỀ GỐC THẬT (OrgOk), không chỉ đang đọc vị trí "trong vùng an
        /// toàn" — CurrentPos của trục chưa về gốc là số rác từ lúc bật nguồn.
        /// <para><b>CỐ Ý không đưa vào <see cref="Move"/></b> — hàm đó dùng chung cho Routine_Auto/Routine_Calib,
        /// những nơi tự quản trình tự Z/X/Y và có thể hợp lệ gọi Move đúng lúc một trục Z tạm mất OrgOk (Tuấn
        /// 2026-09-07 — áp thẳng vào Move từng làm Routine_Auto mô phỏng bị treo).</para>
        /// </summary>
        bool CanMoveAutonomous(std::string_view axisName, std::string& reason) const;

        /// <summary>Chạy tuyệt đối (chờ tới đích) qua đủ chốt chặn và bit RUN — C# MoveAsync.</summary>
        bool Move(const std::string& axisName, double pos, double speed, std::stop_token ct);

        /// <summary>Như <see cref="Move"/> nhưng gia tốc giảm theo tốc độ lệnh — xem <c>IAxis::MoveAbs(gentle)</c>.</summary>
        bool MoveGentle(const std::string& axisName, double pos, double speed, std::stop_token ct);

        // ── Bit RUN (Tuấn 2026-09-29) ──
        //
        // STOP tắt bit: mọi trục nhận lệnh dừng ngay, chu trình KHÔNG thoát mà đứng chờ — ở đầu vòng Routine_Auto, hoặc
        // ngay trong lệnh chạy đang dở. Start bật lại bit thì lệnh dở đó được phát lại tới đúng đích cũ, nên chu trình
        // không bao giờ phải làm lại một bước từ giữa chừng (Z đang hạ, van hút đang mở…). Chỉ trục dừng — hút, thổi,
        // soi đang làm thì làm nốt.

        bool RunBit() const { return m_runBit.load(); }
        void SetRunBit(bool on);

        /// <summary>
        /// Gọi ở đầu Run của chu trình (Routine_Auto, Routine_DryCycle) — đánh dấu mọi lệnh chạy trục phát ra từ luồng
        /// đó. Chỉ lệnh của chu trình được chờ bit RUN; lệnh bấm tay (Teaching, Manual, Calib) lúc tạm dừng bị từ chối,
        /// không xếp hàng rồi chạy cùng lúc với chu trình khi bấm Start.
        /// <para>Cờ gắn theo LUỒNG (thay AsyncLocal của C#): việc chạy song song bằng <c>base::WhenAll</c> từ luồng chu
        /// trình phải bọc thân mỗi việc trong <see cref="CycleFlowScope"/> (giá trị lấy bằng <see cref="InCycleFlow"/> ở
        /// luồng gọi) để cờ đi theo — các hàm MoveToCell/EnsureZSafe của lớp này đã tự làm.</para>
        /// </summary>
        static void MarkCycleFlow();
        static bool InCycleFlow();

        /// <summary>Đặt cờ chu trình cho luồng hiện tại trong một khối, trả lại giá trị cũ khi ra khỏi khối.</summary>
        struct CycleFlowScope
        {
            explicit CycleFlowScope(bool inCycle);
            ~CycleFlowScope();
            CycleFlowScope(const CycleFlowScope&) = delete;
            CycleFlowScope& operator=(const CycleFlowScope&) = delete;
        private:
            bool m_before;
        };

        /// <summary>Chờ tới khi bit RUN bật. Bị huỷ thì ném <c>base::OperationCanceled</c>.</summary>
        void WaitRunBit(std::stop_token ct);

        /// <summary>Giá trị của <c>long.MinValue</c> trong C#: chưa có lệnh chạy nào kết thúc.</summary>
        static constexpr long long NeverMoved = LLONG_MIN;

        /// <summary>
        /// Còn phải chờ bao nhiêu ms nữa cho đủ <paramref name="settleMs"/> kể từ lúc lệnh chạy cuối cùng kết thúc.
        /// Hàm thuần để bài tự kiểm khoá được luật mà không phải đo thời gian thật.
        /// <c>lastMoveEndedMs == NeverMoved</c> nghĩa là chưa có lệnh chạy nào.
        /// </summary>
        static int SettleRemainingMs(long long lastMoveEndedMs, long long nowMs, int settleMs);

        /// <summary>
        /// Chờ cơ khí thôi rung sau lệnh chạy cuối cùng, trước khi ĐỌC ENCODER và kích Flow có camera
        /// <paramref name="cam"/> — xem <c>SystemData::SettleMsFor</c>. Gọi TRƯỚC khi dựng bộ biến toàn cục: con số
        /// encoder ghi vào biến phải tả đúng vị trí lúc camera phơi sáng. Bị huỷ thì ném <c>base::OperationCanceled</c>.
        /// </summary>
        void WaitSettle(EVmCamera cam, std::stop_token ct);

        /// <summary>
        /// Điểm giảm tốc có được dùng hay không. Z dương là chiều ĐI XUỐNG, nên chặng giảm tốc chỉ có nghĩa khi điểm
        /// giảm tốc nằm HẲN giữa chỗ đang đứng và đích: chưa teach (≤ 0), teach nhầm xuống dưới cả đích, hoặc Z đã ở
        /// dưới nó rồi thì đều hạ một mạch.
        /// </summary>
        static bool UseTwoStageZ(double now, double slowAt, double target);

        /// <summary>Chặng chậm khi ĐI LÊN — đúng phép xét của chiều xuống với "chỗ đang đứng" và "đích" đổi chỗ cho
        /// nhau: một luật, không phải hai, nên không có đường nào để hai chiều lệch nhau về sau.</summary>
        static bool UseTwoStageZUp(double now, double slowAt, double target);

        /// <summary>
        /// Hạ trục Z xuống cao độ làm việc qua HAI chặng: mức Auto CAO tới <paramref name="slowAt"/>, rồi mức Auto
        /// THẤP nốt đoạn còn lại (Tuấn 2026-09-18). Cả hai chặng đều qua <see cref="Move"/>. Không dùng được điểm giảm
        /// tốc (<see cref="UseTwoStageZ"/>) thì hạ một mạch ở mức cao.
        /// </summary>
        bool MoveZDown(const std::string& zAxis, double target, double slowAt, std::stop_token ct);

        /// <summary>
        /// Nâng Z RA KHỎI hốc tray theo hai mức — gương của <see cref="MoveZDown"/>: mức Auto THẤP từ cao độ hút/đặt
        /// lên tới điểm giảm tốc, rồi mức CAO nốt (Tuấn 2026-09-24). Nhấc nhanh khỏi hốc thì lúc hút sản phẩm dễ xê
        /// dịch trên đầu hút, lúc đặt thì luồng khí và cái gờ dễ kéo theo con vừa nhả.
        /// </summary>
        bool MoveZUp(const std::string& zAxis, double target, double slowAt, std::stop_token ct);

        /// <summary>Di chuyển TƯƠNG ĐỐI (Inching) — nút Move −/Move + ở trang Manual. Cùng chốt chặn CanMove với
        /// <see cref="Move"/>; không chờ bit RUN (bit tắt thì từ chối).</summary>
        bool MoveInc(const std::string& axisName, double delta, double speed, std::stop_token ct);

        /// <summary>Chọn Nhanh/Chậm cho jog tay — CHUNG cho cả 7 trục (Tuấn 2026-09-05). Về gốc/MoveAbs/Auto không đọc cờ này.</summary>
        bool JogFast() const { return m_jogFast.load(); }
        void SetJogFast(bool v) { m_jogFast = v; }

        /// <summary>Jog liên tục (không chặn). dir: +1 / −1 / 0 = dừng; speed ≤ 0 → tốc độ jog theo <see cref="JogFast"/>.</summary>
        void Jog(const std::string& axisName, int dir, double speed = 0);

        /// <summary>
        /// Nâng cả hai trục Z lên cao độ an toàn (dừng đúng Z_SAFE — chốt chặn đã nới <c>MotionConnect::ZSafeMargin</c>).
        /// Điều kiện tiên quyết của mọi chuyển động ngang, nên có hàm riêng thay vì để mỗi routine tự viết lại.
        /// </summary>
        bool EnsureZSafe(std::stop_token ct);

        /// <summary>
        /// Tới một ô: nâng Z an toàn trước, rồi trục hàng và trục X chạy song song. Dùng chung cho Routine_Auto và trang
        /// Teaching. <paramref name="slowRow"/>: trục hàng chạy mức Auto thấp, gia tốc giảm theo (như Y2 tới điểm đặt).
        /// </summary>
        bool MoveToCell(const std::string& rowAxis, double rowPos, double colPos, std::stop_token ct, bool slowRow = false);

        /// <summary>
        /// Như <see cref="MoveToCell"/> nhưng xoay luôn trục A của tay <paramref name="pickNozzle"/> về <b>góc hút</b>
        /// (<c>ModelData::PickAngle</c>), song song với X/Y — gọi ở MỌI chỗ hạ Z xuống hút sản phẩm. Đầu hút có gờ ôm
        /// sản phẩm: xuống hút trong lúc A còn ở góc của lệnh đặt trước thì gờ không ăn khớp, hở khí, không hút được
        /// (Tuấn 2026-09-23). Xoay SAU khi Z đã lên cao độ an toàn.
        /// </summary>
        bool MoveToCellForPick(int pickNozzle, double pickAngleDeg, const std::string& rowAxis, double rowPos, double colPos,
                               std::stop_token ct, bool slowRow = false);

        void StopAll() { m_motion.StopAll(); }

        // ── I/O nghiệp vụ ──
        //
        // Van hút KHÔNG bị cắt khi EMG (§11 #11 đã xác nhận mạch khí giữ nguyên trạng thái), và
        // phần mềm cũng không được tự tắt nó ở bất kỳ đường dừng nào — SafetyMonitor.Abort,
        // McControl.DoStop và Routine_Origin.Abort đều chỉ dừng trục. Cắt chân không lúc máy
        // đang dừng khẩn là làm rơi sản phẩm vào trong máy: hỏng hàng, và thành vật cản cho
        // lần về gốc kế tiếp. Sản phẩm còn trên tay hút được xử lý có kiểm soát ở §3.7.

        void SetVacuum(int nozzle, bool on);
        void SetBlow(int nozzle, bool on);
        bool HasProduct(int nozzle) const;

        /// <summary>Cảm biến có tray đầu vào thứ <paramref name="tray"/> (0 = Tray 1 phía trong, 1 = Tray 2 phía ngoài).</summary>
        bool HasTrayIn(int tray) const;
        bool HasTrayAssy() const;

    private:
        /// <summary>Trạng thái phanh của một trục: cờ operator đã bấm "Mở phanh" chưa, mốc bắt đầu nhả liên tục
        /// (cho <c>BrakeReleaseDelayMs</c>; −1 = đang khoá), và cờ nhả phanh KHẨN CẤP kiểu toggle.</summary>
        struct BrakeState
        {
            std::atomic<bool> Requested{ false };
            std::atomic<bool> EmergencyReleaseArmed{ false };
            /// <summary>Thay Stopwatch ReleaseTimer: ms theo <c>m_clock</c> lúc bắt đầu nhả; −1 = không chạy.</summary>
            std::atomic<long long> ReleaseStartMs{ -1 };
        };

        /// <summary>Thời gian tối thiểu từ lúc ra lệnh nhả phanh (DO lên 1) tới lúc coi là ĐÃ NHẢ THẬT và cho phép di
        /// chuyển — phanh cơ có độ trễ nhả vật lý. Ước lượng an toàn, CHƯA đo trên máy thật — chỉnh lại ở P1.</summary>
        static constexpr int BrakeReleaseDelayMs = 300;

        const BrakeState* BrakeOf(std::string_view axisName) const;
        BrakeState* BrakeOf(std::string_view axisName);
        void UpdateBrakeOutput(const std::string& axisName, int doBit, BrakeState& state);

        /// <summary>Một lệnh chạy tuyệt đối theo bit RUN: bit tắt thì chờ, bị tắt giữa hành trình thì chờ bật lại rồi
        /// phát lại đúng lệnh cũ. <paramref name="interlock"/> = false chỉ cho lệnh nâng Z của EnsureZSafe.</summary>
        bool MoveHoldable(const std::string& axisName, double pos, double speed, std::stop_token ct, bool interlock, bool gentle = false);
        bool MoveRow(const std::string& rowAxis, double pos, bool slow, std::stop_token ct);

        /// <summary>Ghi mốc thời gian vừa kết thúc một lệnh chạy. Đánh dấu cả khi lệnh THẤT BẠI: trục vẫn đã nhúc
        /// nhích rồi dừng, vẫn còn rung như thường.</summary>
        void MarkMoveEnded() { m_lastMoveEndedMs = m_clock.ElapsedMilliseconds(); }

        static std::string HeldReason();

        MotionConnect& m_motion;
        SafetyMonitor& m_safety;
        McStatus& m_status;
        std::function<std::shared_ptr<SystemData>()> m_sys;
        std::shared_ptr<IIoDevice> m_io;

        /// <summary>Đồng hồ ĐƠN ĐIỆU cho <see cref="WaitSettle"/> và phanh — đổi giờ hệ thống hay giờ mùa hè không
        /// được phép làm lệch phép chờ này.</summary>
        base::Stopwatch m_clock;
        std::atomic<long long> m_lastMoveEndedMs{ NeverMoved };

        BrakeState m_brakeZ1;
        BrakeState m_brakeZ2;

        std::mutex m_runLock;                    // m_runCts (đổi cùng m_runBit)
        std::atomic<bool> m_runBit{ true };
        std::stop_source m_runCts;
        std::atomic<bool> m_jogFast{ false };
    };
}
