#pragma once
#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

#include "Communication/IVisionDevice.h"
#include "Communication/MotionConnect.h"
#include "Communication/VmContract.h"
#include "Controller/McFunction.h"
#include "Controller/McStatus.h"
#include "Controller/RunClock.h"
#include "Controller/VmCycle.h"
#include "Data/Model/ModelData.h"
#include "Data/RunningData.h"
#include "Data/SystemData.h"
#include "Infor/AlarmCode.h"
#include "Infor/Enums.h"

namespace sca
{
    /// <summary>Kết quả quét ô đích theo hợp đồng mới — App cất nguyên, không cộng gì.</summary>
    struct PlacedTopRecord
    {
        double X{ 0 }, Y{ 0 }, A{ 0 };
        int Nozzle{ 0 };
    };

    /// <summary>Lệnh của lần đặt gần nhất và vị trí trục đọc được ngay khi tới nơi — cho bài tự kiểm.</summary>
    struct PlaceMoveRecord
    {
        int Nozzle{ 0 };
        double CmdX{ 0 }, CmdY{ 0 }, ATarget{ 0 };
        double ActualX{ 0 }, ActualY{ 0 }, ActualA{ 0 };
    };

    /// <summary>
    /// Chu trình chạy tự động — PLAN.md §5.2.
    ///
    /// <para><b>Cấu trúc.</b> Một vòng lặp trên <c>EAutoState</c> lưu trong <c>RunningData</c>, mỗi vòng làm đúng một bước
    /// rồi đặt bước kế. Trạng thái chu trình là <b>dữ liệu</b> chứ không phải vị trí con trỏ lệnh trong một hàm dài —
    /// trong CÙNG một phiên chạy, STOP rồi START lại tiếp đúng từ bước đã dừng. Nhưng trạng thái đó KHÔNG sống sót qua
    /// một lần tắt/mở app: <c>McDataManager::LoadRunningData</c> luôn xoá nó về S00 lúc khởi động (quyết định của Tuấn).</para>
    ///
    /// <para><b>Checkpoint.</b> Yêu cầu dừng chỉ được xét ở ranh giới giữa các bước, không xét giữa lúc Z đang hạ hay
    /// đang chuyển chân không (§3.4).</para>
    ///
    /// <para><b>Không tự kiểm tra an toàn.</b> Mọi lệnh đi qua <see cref="McFunction"/> nên interlock Z_SAFE, soft limit
    /// và điều kiện an toàn đã được chặn ở tầng dưới. Routine chỉ observe <c>std::stop_token</c> chung (§3.3).</para>
    ///
    /// <para><b>Số lượng sản phẩm mỗi vòng là 1 hoặc 2, không cố định</b> (§5.3). <b>Quét trước tray lắp ráp — S05</b>
    /// (§5.2b): CAM top-down soi hết các ô còn trống MỘT LẦN trước khi đặt hàng; kết quả giữ trong bộ nhớ
    /// (<c>m_assyScan</c>). <b>App chấp hành, VisionMaster tính bù</b> (thiết kế 2026-09-13): lúc soi CAM bottom-up App
    /// gán ô đích cho tay rồi gọi <c>AlignBottom_ToolN</c> kèm pose ô; Flow tính luôn lệnh đặt; App so sánh, cất lệnh,
    /// rồi lúc đặt chỉ chạy trục tới đó.</para>
    ///
    /// <para><b>Luồng (bản C++).</b> <see cref="Run"/> là HÀM CHẶN chạy trên luồng routine của McControl, gọi
    /// <c>McFunction::MarkCycleFlow()</c> ở đầu. Không ném: huỷ (<c>base::OperationCanceled</c>) và lỗi đều bắt bên trong
    /// rồi trả false. Các hàm public khác gọi được từ luồng vòng quét/UI: <see cref="LoadTrays"/>, <see cref="TraysOut"/>,
    /// <see cref="LoadBothTrays"/>, <see cref="HoldClock"/>, <see cref="RequestCycleRestart"/>,
    /// <see cref="RequestVisionCacheInvalidation"/>, <see cref="InvalidateAssyScan"/> và mọi getter — nên các trường dùng
    /// chung được khoá (xem ghi chú từng trường). Bản C# ghi thẳng Dictionary từ luồng UI; bản C++ khoá
    /// <c>m_cacheMutex</c>.</para>
    /// </summary>
    class Routine_Auto
    {
    public:
        Routine_Auto(MotionConnect& motion, McFunction& func, McStatus& status,
                     std::function<std::shared_ptr<SystemData>()> sys, std::function<std::shared_ptr<IVisionDevice>()> vision);
        Routine_Auto(const Routine_Auto&) = delete;
        Routine_Auto& operator=(const Routine_Auto&) = delete;

        /// <summary>Giây đã chạy của lượt hiện tại — bộ đếm "cycle time hiện tại" ở trang Main.</summary>
        double CurrentCycleSec() const { return m_cycleSw.ElapsedSeconds(); }

        /// <summary>
        /// Cho chu trình chạy lại từ S00 ở ranh giới bước kế tiếp: S00 bỏ tư thế đo cũ nên sản phẩm đang cầm được soi lại
        /// ở CAM bottom-up, S05 quét lại tray lắp ráp, rồi mới đặt. Gọi khi tray lắp ráp được nạp lại bằng tay (Tuấn chốt
        /// 2026-09-13).
        /// </summary>
        void RequestCycleRestart() { m_restartCycle = true; }

        /// <summary>Chỉ số các ô đã quét trước OK — trang Main tô xanh nhạt ô trống đã soi (Tuấn 2026-10-07). Bản chụp.</summary>
        std::vector<int> AssyScannedCells() const;

        /// <summary>Bỏ toàn bộ kết quả quét trước — gọi khi tray lắp ráp bị nạp lại/đổi mới, để không lỡ dùng số đo của
        /// tray VẬT LÝ cũ cho tray mới đang nằm ở cùng chỉ số ô. Bỏ luôn lệnh đặt đang giữ và bản ghi ô đã đặt.</summary>
        void InvalidateAssyScan();

        /// <summary>
        /// Yêu cầu bỏ mọi số đo đã cất (quét ô, tư thế trên tay) ở ranh giới bước kế tiếp — gọi sau khi teach lại
        /// lưới/mốc/vị trí soi, đổi model, hiệu chuẩn lại hoặc nạp lại solution. Không xoá ngay để khỏi giẫm lên chu
        /// trình đang chạy.
        /// </summary>
        void RequestVisionCacheInvalidation() { m_invalidateRequested = true; }

        /// <summary>Kết quả cổng hiệu chuẩn ở lần qua cổng gần nhất; nullopt = chưa qua cổng lần nào (C#: null).</summary>
        std::optional<VmCalibGate> LastCalibGate() const;

        /// <summary>Lệnh của lần đặt gần nhất và vị trí trục lúc tới nơi — cho bài tự kiểm; nullopt = chưa đặt.</summary>
        std::optional<PlaceMoveRecord> LastPlaceMove() const;

        /// <summary>Số quét ô đích của lần đặt đã xong vào ô <paramref name="index"/>; nullopt nếu chưa có.</summary>
        std::optional<PlacedTopRecord> GetPlacedTop(int index) const;

        /// <summary>Đặt true để chu trình dừng ở checkpoint gần nhất (C#: public volatile field).</summary>
        std::atomic<bool> PauseRequested{ false };

        /// <summary>Chạy chu trình (C# RunAsync). HÀM CHẶN — xem ghi chú lớp. true = dừng có kiểm soát ở checkpoint
        /// (đã tự đặt State = Paused); false = dừng vì lỗi/huỷ (Message đã ghi lý do).</summary>
        bool Run(std::stop_token ct);

        /// <summary>Bit RUN tắt thì đồng hồ lượt cũng đứng — thời gian tạm dừng không tính vào cycle time.</summary>
        void HoldClock(bool hold);

        /// <summary>Cửa sổ quanh vị trí chờ mà trong đó phép đọc cảm biến tray còn có nghĩa. Trục được LỆNH chạy tới
        /// đúng vị trí chờ nên chỉ lệch cỡ cửa sổ INP của driver; 1 mm là rộng rãi.</summary>
        static constexpr double TraySensorWindowMm = 1.0;

        /// <summary>
        /// Có <b>kết luận được</b> là thiếu tray hay không. Cảm biến "có tray" gắn CỐ ĐỊNH ở vị trí chờ của trục hàng,
        /// không chạy theo tray (Tuấn 2026-09-24): chỉ khi trục đang đứng ở vị trí chờ thì tín hiệu OFF mới có nghĩa là
        /// "thiếu tray"; tray đã chạy vào vùng làm việc thì <b>không đo được</b> — mà không đo được thì không được dừng
        /// máy. Hàm thuần (Routine_AutoStatics.cpp).
        /// </summary>
        static bool TrayMissingAt(double axisPos, double standbyPos, bool sensorOn);

        /// <summary>Tray đã vào máy, hoặc vừa được nạp và đang chờ luồng chu trình nhận.</summary>
        bool TrayIn(bool input) const;

        /// <summary>Chu trình đang chờ nạp tray — thông báo của lần chờ đó; RỖNG khi không chờ (C#: null).</summary>
        std::string WaitingMessage() const;

        /// <summary>
        /// Nút Start — bốn nút như nhau (Tuấn chốt 2026-10-16): nạp MỌI tray chưa vào máy mà cảm biến thấy ở vị trí chờ.
        /// Tray không thấy thì bỏ qua — chu trình chạy với tray đã có, bước cần tray kia thì chờ. Tray đã ở trong máy thì
        /// không làm gì. Gọi từ vòng quét McControl, lúc sẵn sàng hoặc lúc chu trình đang chạy/chờ tray.
        /// </summary>
        /// <param name="loaded">Tray vừa nạp, để báo ("input tray", "assy tray", "both trays" — Mc.InputTray/AssyTray/
        /// BothTrays); RỖNG khi không nạp thêm tray nào.</param>
        /// <param name="notSeen">Tray còn thiếu mà cảm biến không thấy; RỖNG khi không thiếu.</param>
        /// <returns>Lý do từ chối — chưa có model, thấy Tray 2 mà Tray 2 chưa teach — khi đó không nạp tray nào; RỖNG nếu
        /// không có gì sai.</returns>
        std::string LoadTrays(std::string& loaded, std::string& notSeen);

        /// <summary>
        /// Init / Về gốc đưa mọi trục về vị trí chờ — cả hai tray coi như ra khỏi máy, phải bấm Start lại. Số đo vision
        /// của hàng đang làm dở bỏ luôn, và lần chạy kế tiếp bắt đầu lại từ S00 (Tuấn 2026-10-16). Chỉ gọi lúc chu trình
        /// không chạy.
        /// </summary>
        void TraysOut();

        /// <summary>Nạp tray nào chưa vào, không qua cảm biến — chỉ đường START cũ của self-test dùng
        /// (<c>McControl.DoStart</c>).</summary>
        void LoadBothTrays();

        /// <summary>
        /// Trong một cặp ô, tay #1 có nhận ô ĐƯỢC GHÉ TRƯỚC hay không — quyết theo <b>hình học</b>: tay #1 nhận ô có X nhỏ
        /// hơn, tay #2 nhận ô có X lớn hơn (tay #1 lùi ~63 mm so với tay #2, xa hơn một bước ô — giao cho nó ô gần hơn là
        /// gantry với ngắn lại một bước ô, trả hai lần mỗi vòng). Cặp cùng cột (lệch X dưới <paramref name="sameColumnMm"/>)
        /// thì giữ thứ tự ghé. Hàm thuần (Routine_AutoStatics.cpp).
        /// </summary>
        static bool Nozzle1TakesFirstCell(double xFirst, double xSecond, double sameColumnMm = 0);

        /// <summary>
        /// Trong một CẶP ô của tray đầu vào, tay #1 có nhận ô ĐƯỢC GHÉ TRƯỚC hay không — gương của
        /// <see cref="Nozzle1TakesFirstCell"/> ở đầu kia của vòng: chọn cách ghép có <c>min</c> của hai vị trí gantry lớn
        /// hơn. Bằng nhau, hoặc cặp CÙNG CỘT (lệch X dưới <paramref name="sameColumnMm"/>, Tuấn 2026-10-15) thì giữ thứ tự
        /// ghé. Hàm thuần (Routine_AutoStatics.cpp).
        /// </summary>
        static bool Nozzle1TakesFirstPickCell(double firstAtNozzle1, double firstAtNozzle2,
                                              double secondAtNozzle1, double secondAtNozzle2,
                                              double sameColumnMm = 0);

        /// <summary>
        /// Ô CHỤP TRƯỚC trong một nhóm ô sắp chụp (cặp, hàng) — ô có X chỗ đứng chụp LỚN nhất (Tuấn 2026-10-28), gương của
        /// luật hút ô X nhỏ trước: gantry vừa đặt ở tray lắp ráp (X lớn), chụp xong về tray đầu vào (X nhỏ), nên chụp từ X
        /// lớn xuống X nhỏ là đi một chiều. Ô cùng cột (lệch X dưới <paramref name="sameColumnMm"/>) giữ thứ tự ghé. Trả chỉ
        /// số TRONG <paramref name="camX"/>, −1 khi rỗng. Hàm thuần (Routine_AutoStatics.cpp).
        /// </summary>
        static int FirstToScan(const std::vector<double>& camX, double sameColumnMm = 0);

    private:
        // ───────────── kiểu riêng ─────────────

        /// <summary>Lệnh đặt Flow AlignBottom_ToolN tính lúc soi, giữ tới lúc đặt — kèm ô đích đã gán.</summary>
        struct VmHeld
        {
            int Cell{ -1 };
            PlacedTopRecord Top;
            double CmdX{ 0 }, CmdY2{ 0 }, CmdA{ 0 }, ACap{ 0 }, DiagDA{ 0 }, DiagEcc{ 0 };
        };

        // ───────────── truy cập dữ liệu (C# property Run / Model) ─────────────

        /// <summary>C# property <c>Run</c> — đổi tên vì trùng hàm <see cref="Run"/>.</summary>
        std::shared_ptr<RunningData> RunData() const;
        std::shared_ptr<ModelData> Model() const;

        // ───────────── vòng chính / bộ nhớ đệm ─────────────
        void PublishAssyScan();                                   // gọi trong m_cacheMutex
        void TakeInvalidationRequest();
        void ClearVisionCaches(const std::string& why);

        // ───────────── S00 / S05 ─────────────
        bool S00_Ready(std::stop_token ct);
        bool S05_ScanAssy(std::stop_token ct);
        bool SkipCellNotFound(TrayState& tray, int index, const std::string& detail);
        int NextScanCell(const TrayState& tray);
        int NextUnscannedCell(const TrayState& tray);
        /// <summary>Ô của <paramref name="cells"/> chụp trước theo <see cref="FirstToScan"/>; −1 khi rỗng.</summary>
        int FirstScanCell(const TrayState& tray, const std::vector<int>& cells);

        // ───────────── cổng ─────────────
        bool CanRun(std::string& why);
        bool TrayMissing(const std::string& rowAxis, bool sensorOn);
        /// <summary>C# GateAsync.</summary>
        bool Gate(std::stop_token ct);

        // ───────────── S10 / S11 — hút ─────────────
        bool S1x_Pick(int nozzle, EAutoState next, std::stop_token ct);

        // ───────────── S20 / S21 — soi và tính lệnh đặt ─────────────
        bool S2x_Align(int nozzle, EAutoState next, std::stop_token ct);
        int AssignCell(int nozzle);
        double CellX(int index);
        /// <summary>C# AlignNotFoundAsync; <paramref name="detail"/> rỗng = C# null.</summary>
        bool AlignNotFound(int nozzle, const std::string& detail, std::stop_token ct);

        // ───────────── S30–S33 — đặt ─────────────
        bool S3x_Place(int nozzle, EAutoState next, std::stop_token ct);
        void CompletePlacement(int nozzle, int index, const PlacedTopRecord* top);
        /// <summary>C# PlaceAsync — xoay, tới điểm đặt, hạ, nhả, nâng. <paramref name="aTarget"/> là góc tuyệt đối.</summary>
        bool Place(int nozzle, double cmdX, double cmdY, double aTarget, std::stop_token ct);
        /// <summary>C# ReturnToSourceAsync.</summary>
        bool ReturnToSource(int nozzle, std::stop_token ct);

        // ───────────── S40 / S50 / tray ─────────────
        bool S40_Index();
        bool S50_TrayCheck(std::stop_token ct);
        /// <summary>Kết thúc một tray: mọi ô còn việc (<paramref name="open"/>) thành Skipped, bỏ ô chọn. Trả số ô đã
        /// đánh dấu.</summary>
        static int FinishTray(TrayState& tray, ECellState open, const std::string& what);
        void ReloadTrays(int inputMask, bool assy);
        std::string TrayWaitText();
        bool NeedAssyTray();
        /// <summary>C# WaitTrayAsync.</summary>
        bool WaitTray(const std::function<bool()>& done, std::stop_token ct);
        void TakeTrayLoads();
        bool MoveInputTrayIn(std::stop_token ct);
        bool MoveTraysToStandby(bool input, bool assy, std::stop_token ct);
        bool MoveToStandby(const std::string& axis, std::stop_token ct);
        int InputTraysSeen(std::string& why);
        bool AtStandby(const std::string& rowAxis);
        static std::string InputTraysText(int mask);

        // ───────────── tiện ích ─────────────
        NozzleState& Nozzle(int i);
        /// <summary>Bước soi của một tay — S20 soi tay #2, S21 soi tay #1 (Tuấn xác nhận 2026-09-05).</summary>
        static EAutoState AlignStepOf(int nozzle);
        double SafeZ();
        double MoveSpeed(const std::string& axis);
        double ZSpeed(const std::string& axis);
        void SettleVacuum(bool gripped, std::stop_token ct);
        bool WaitVacuum(int nozzle, bool want, std::stop_token ct);
        /// <summary>C# CallVmAsync — bật đèn CAM (chờ đèn lên nếu vừa bật), gọi Flow; không bao giờ trả "không có kết
        /// quả" rỗng (thay bằng VmCallResult::Fail).</summary>
        VmCallResult CallVm(const VmCallRequest& req, std::stop_token ct);
        static int LampFor(EAutoState state);
        void LightCamLamp(int lampBit);
        void TurnCamLampOn(int lampBit, std::stop_token ct);
        void UpdateCamLamp();
        void TurnCamLampOff();
        void WaitLamp(int lampBit, std::stop_token ct);
        VmCameraRead LocateTopVm(std::stop_token ct);
        bool VmHalt(int nozzle, int index, const std::string& why, int alarm = AlarmCode::VisionContract);
        /// <summary>Chế độ dừng khi Vision NG đang bật (McStatus::StopOnVisionNg)?</summary>
        bool StopOnVisionNg() const;
        /// <summary>Dừng ngay tại chỗ Vision NG, chưa xử lý NG — xem chú thích ở Routine_Auto.cpp.</summary>
        bool VisionNgStop(const std::string& detail);
        static int NextLoadedCell(const TrayState& tray);
        int AssignPickCell(int nozzle);
        int CountFreeCells(const TrayState& tray);
        static int NextFreeCell(const TrayState& tray);
        void ReleaseOrphanCells();
        static void Release(TrayState& tray, ECellState back, int keep1, int keep2);
        void CountNg(const std::string& what);
        bool CheckConsecutiveNg();
        void RequireNozzleClear(const std::string& reason);
        void Save();
        bool Halt(const std::string& reason);
        bool Stopped(const std::string& reason);

        // ───────────── trường ─────────────

        MotionConnect& m_motion;
        McFunction& m_func;
        McStatus& m_status;
        std::function<std::shared_ptr<SystemData>()> m_sys;
        std::function<std::shared_ptr<IVisionDevice>()> m_vision;

        /// <summary>Đồng hồ của lượt đang chạy (S00→S40) — chỉ chạy khi máy làm việc: dừng lúc rời chu trình (STOP, lỗi)
        /// và lúc chờ tray, nên cycle time không lẫn thời gian đứng.</summary>
        RunClock m_cycleSw;

        /// <summary>Bộ đếm sản phẩm lúc bắt đầu lượt — hết lượt thì hiệu là số sản phẩm của lượt.</summary>
        int m_cycleStartTotal{ 0 };

        /// <summary>Lần đặt đang chạy đã bắt đầu nhả hút chưa — khối "finally" của <c>S3x_Place</c> dựa vào đây để biết ô
        /// đích còn nguyên hay không. Chỉ luồng chu trình.</summary>
        bool m_releaseStarted{ false };

        /// <summary>Lần chạy kế tiếp phải bắt đầu lại từ S00 — xem <see cref="RequestCycleRestart"/>.</summary>
        std::atomic<bool> m_restartCycle{ false };

        /// <summary>Bước quay về khi S05 quét xong: S10 như thường, hoặc đúng bước soi vừa đòi quét. Chỉ luồng chu trình.</summary>
        EAutoState m_afterScan{ EAutoState::S10_Pick1 };

        /// <summary>
        /// Khoá các trường "bộ nhớ đệm vision" mà luồng UI (InvalidateAssyScan, TraysOut, AssyScannedCells) và luồng chu
        /// trình cùng đụng: <c>m_assyScan</c>, <c>m_assyScannedCells</c>, <c>m_scanOneCell</c>, <c>m_held</c>. Đệ quy để
        /// hàm giữ khoá gọi được hàm khác cũng giữ khoá. KHÔNG giữ khoá này trong lúc chạy trục hay gọi Flow.
        /// </summary>
        mutable std::recursive_mutex m_cacheMutex;

        /// <summary>
        /// Kết quả quét trước (S05_ScanAssy), theo chỉ số ô của <c>Run.AssyTray</c>. Chỉ giữ trong bộ nhớ — tắt app giữa
        /// chừng thì mất, quét lại từ đầu cho an toàn. <b>Chụp bằng mốc tay hút #1 cho MỌI ô</b>: <c>TopX/TopY</c> là toạ
        /// độ trục để TÂM CAM trùng chi tiết đích, không phụ thuộc tay nào; Flow <c>AlignBottom_ToolN</c> cộng hand-eye của
        /// tay THẬT SỰ đặt. (m_cacheMutex)
        /// </summary>
        std::map<int, PlacedTopRecord> m_assyScan;
        /// <summary>Bản chụp khoá của <c>m_assyScan</c> cho <see cref="AssyScannedCells"/>. (m_cacheMutex)</summary>
        std::vector<int> m_assyScannedCells;
        /// <summary>Ô mà bước soi CAM dưới vừa đòi số đo nhưng chưa có — S05 soi đúng ô này trước, bất kể tuỳ chọn quét
        /// từng hàng (không có nó thì quét từng hàng kẹt vòng S2x ↔ S05). −1 = không có. (m_cacheMutex)</summary>
        int m_scanOneCell{ -1 };
        /// <summary>Lệnh đặt đang giữ của tay 1, 2 (chỉ số 0 không dùng). (m_cacheMutex)</summary>
        std::array<std::optional<VmHeld>, 3> m_held;

        /// <summary>Cổng hiệu chuẩn lần gần nhất. (m_gateMutex)</summary>
        mutable std::mutex m_gateMutex;
        std::optional<VmCalibGate> m_calib;

        /// <summary>Số lần soi lại liên tiếp vì trục A đã xoay sau lúc chụp (nhánh R), theo tay. Chỉ luồng chu trình.</summary>
        std::array<int, 3> m_remeasure{};
        int m_guardSkipStreak{ 0 };
        std::atomic<bool> m_invalidateRequested{ false };

        /// <summary>Số quét ô đích của các ô đã đặt xong (bài tự kiểm đọc). (m_placedMutex)</summary>
        mutable std::mutex m_placedMutex;
        std::map<int, PlacedTopRecord> m_placed;
        std::optional<PlaceMoveRecord> m_lastPlaceMove;   // (m_placedMutex)

        /// <summary>
        /// Tray đang ở trong máy: operator đặt tray ở vị trí chờ rồi bấm Start (2026-10-16). Bước cần tray nào mà tray đó
        /// chưa vào thì đứng chờ. Tắt khi tray chạy ra vị trí chờ (S50, về gốc); mở App lên là chưa tray nào vào.
        /// </summary>
        std::atomic<bool> m_inputIn{ false }, m_assyIn{ false };

        /// <summary>Nút vừa nạp tray, chờ luồng chu trình nhận (<c>TakeTrayLoads</c>) — sổ tray chỉ đổi trên luồng chu
        /// trình. <c>m_loadInput</c> là MASK các tray đầu vào vừa nạp (bit 0 Tray 1, bit 1 Tray 2), 0 = chưa nạp;
        /// <c>m_loadAssy</c> là 0/1. Đọc-xoá bằng exchange (C# Interlocked.Exchange).</summary>
        std::atomic<int> m_loadInput{ 0 }, m_loadAssy{ 0 };

        /// <summary>Thông báo của lần chờ tray đang diễn ra, rỗng khi không chờ. (m_waitingMutex)</summary>
        mutable std::mutex m_waitingMutex;
        std::string m_waiting;
        /// <summary>Thông báo "Cycle complete …" của lượt vừa xong (S50), rỗng khi không có — giữ tới lúc Start nạp
        /// tray hoặc về gốc. (m_waitingMutex)</summary>
        std::string m_cycleDone;

        /// <summary>Đèn CAM đang sáng — IoMap::DoLampCam1 hoặc DoLampCam2, −1 khi cả hai đang tắt. Chỉ luồng chu trình.</summary>
        int m_lampOn{ -1 };
        /// <summary>Đặt lại lúc bật <c>m_lampOn</c> — WaitLamp chỉ chờ phần thời gian đèn lên còn thiếu. Chỉ luồng chu trình.</summary>
        base::Stopwatch m_lampSince;
    };
}
