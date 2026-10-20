#pragma once
#include <array>
#include <climits>
#include <functional>
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
#include "Controller/Routine_Auto.h"
#include "Controller/VmCycle.h"
#include "Data/Model/ModelData.h"
#include "Data/RunningData.h"
#include "Data/SystemData.h"

namespace sca
{
    /// <summary>Kết quả phép kiểm tra hiệu chuẩn CAM top-down (§6.2b).</summary>
    struct TopDownVerifyResult
    {
        bool Ok{ false };
        std::string Reason;

        /// <summary>Quãng đường đã chạy thử, mm.</summary>
        double StepMm{ 0 };

        /// <summary>Sai lệch thang đo: (lượng Flow báo) − (lượng trục đã chạy), mm. 0 là hoàn hảo.</summary>
        double ErrorX{ 0 };
        double ErrorY{ 0 };

        /// <summary>Lẫn trục: chạy X mà Flow báo dịch theo Y (và ngược lại), mm.</summary>
        double CrossX{ 0 };
        double CrossY{ 0 };

        /// <summary>Dấu của từng trục có đúng không — sai dấu là đặt lệch gấp đôi, không phải lệch ít.</summary>
        bool SignXOk{ false };
        bool SignYOk{ false };

        /// <summary>Phán quyết của App (<c>VmStatus::TopDownVerify</c>); INT_MIN nếu chưa đo đủ ba chỗ.</summary>
        int Status{ INT_MIN };

        /// <summary>Ghi chú sau khi đạt: lưu bản ghi vào model.</summary>
        std::string Note;
    };

    /// <summary>Kết quả phép thử vòng xoay của một tay hút (§6.4b).</summary>
    struct AlignVerifyResult
    {
        bool Ok{ false };
        std::string Reason;

        /// <summary>Dấu quy đổi góc VisionMaster → trục A: +1, −1, hoặc 0 nếu không kết luận được.</summary>
        int Sign{ 0 };

        /// <summary>Góc đã xoay thử, độ.</summary>
        double TestAngleDeg{ 0 };

        /// <summary>Góc đo được giữa hai lần đo (<c>RawDTheta</c>), độ.</summary>
        double MeasuredDeltaThetaDeg{ 0 };

        /// <summary>Sai số dư: lượng dịch còn lại sau khi quay về, mm. Khác 0 nghĩa là tâm xoay hoặc chiều hệ toạ độ
        /// (handedness) trong file hiệu chuẩn bottom-up chưa đúng.</summary>
        double ResidualMm{ 0 };

        /// <summary>Độ lệch tâm của sản phẩm lúc thử, mm — quá nhỏ thì phép thử không kết luận được về tâm xoay.</summary>
        double EccentricityMm{ 0 };

        /// <summary>Vị trí trục X lúc thử — phải trùng vị trí soi lúc chạy Auto.</summary>
        double PosX{ 0 };

        /// <summary>Status của phép phán; INT_MIN nếu chưa phán.</summary>
        int Status{ INT_MIN };

        /// <summary>Ghi chú sau khi đạt: lưu solution, lưu model.</summary>
        std::string Note;
    };

    /// <summary>Kết quả chuỗi nạp điểm hiệu chuẩn Align (§6.4).</summary>
    struct AlignCalibSequenceResult
    {
        bool Ok{ false };
        std::string Reason;
        int PointsCaptured{ 0 };
    };

    struct HandEyeCommitResult
    {
        bool Ok{ false };
        std::string Reason;
        int Status{ INT_MIN };
        double Dx{ 0 };
        double Dy{ 0 };
        /// <summary>Mốc góc giữa hai camera, độ — <c>VisionCalib::HandEyeAngleRef</c>.</summary>
        double AngleRef{ 0 };
        std::string Note;
    };

    /// <summary>Kết quả chạy khô đặt sản phẩm mẫu lên dấu chuẩn (Bước 5b).</summary>
    struct DryRunPlaceResult
    {
        bool Ok{ false };
        std::string Reason;
        double CamX{ 0 };
        double CamY{ 0 };
        double CmdX{ 0 };
        double CmdY2{ 0 };
        double CmdA{ 0 };

        /// <summary>Đối chiếu Group Cmd với phép tính lại của App (<see cref="VmCycle::TraceAlignBottom"/>), hoặc rỗng.</summary>
        std::string CmdCheck;
    };

    /// <summary>
    /// Kết quả MỘT nút của khu "Chạy kiểm từng bước" trên trang Vision Edit — chạy tay đúng trình tự mà Auto chạy tự động
    /// (soi ô → hút → soi sản phẩm → tới điểm đặt), mỗi bước một nút để nhìn được số của từng bước.
    /// </summary>
    struct StepRunResult
    {
        bool Ok{ false };
        std::string Reason;

        /// <summary>Dòng số liệu của đúng bước vừa chạy — hiện thẳng trên trang, không phải hộp thoại bấm rồi mất.</summary>
        std::string Text;

        /// <summary>Bước 5: đã cất được lệnh, bước 6 chạy được.</summary>
        bool HasCommand{ false };

        /// <summary>Bước 5: lệnh nằm ngoài hàng rào lưới teach (chạy Auto thì bỏ ô) — vẫn cất để chạy kiểm, nhưng bước 6
        /// phải hỏi lại người bấm. Rỗng = trong hàng rào.</summary>
        std::string GuardWarn;

        /// <summary>
        /// Điểm đích Group Cmd tính ra — <b>chỉ nút 8 và nút 9 của bước xác định hand-eye</b> điền; mọi bước khác để 0.
        /// Mang ra ngoài vì đây là <b>kết luận</b> của phép thử: nó phải bằng encoder lúc gắp.
        /// </summary>
        double TargetX{ 0 };
        double TargetY2{ 0 };
        double TargetA{ 0 };
    };

    /// <summary>
    /// Hiệu chuẩn vision — PLAN.md §6.
    ///
    /// <para><b>App không còn tự fit ma trận nào</b> (Tuấn chốt 2026-09-12). Phép hiệu chuẩn làm trong VisionMaster; App
    /// chỉ còn: (1) <b>nạp điểm</b> — chạy trục tới từng vị trí/góc rồi kích nhánh hiệu chuẩn của Flow; (2) <b>chạy trục
    /// cho hai phép kiểm tra</b> (Bước 2, 4) rồi <b>tự phán</b> (<see cref="VmCycle::JudgeTopDown"/>,
    /// <see cref="VmCycle::JudgeAlignSign"/>); (3) <b>đo hand-eye</b> (Bước 5).</para>
    ///
    /// <para>Bước nào đạt thì lớp này ghi hằng số và ngày đo vào model rồi lưu model — cổng Auto dựa vào bản ghi đó.</para>
    ///
    /// <para><b>Luồng (bản C++).</b> Mọi hàm có <c>std::stop_token</c> là HÀM CHẶN, trang Vision Edit gọi trên luồng nền
    /// với <c>McControl::ManualToken()</c>; mỗi lúc chỉ một chuỗi chạy. Không ném: huỷ/lỗi trả kết quả có Reason. Các
    /// getter (<see cref="StepHasTop"/>, <see cref="StepHasCommand"/>, <see cref="StepGuardWarn"/>,
    /// <see cref="LastCalibGate"/>) luồng UI đọc — khoá <c>m_stateMutex</c>.</para>
    /// </summary>
    class Routine_Calib
    {
    public:
        Routine_Calib(MotionConnect& motion, McFunction& func, std::function<std::shared_ptr<SystemData>()> sys,
                      std::function<std::shared_ptr<IVisionDevice>()> vision);
        Routine_Calib(const Routine_Calib&) = delete;
        Routine_Calib& operator=(const Routine_Calib&) = delete;

        // ───────────────────────── §6.2 CAM top-down: lưới 9 điểm ─────────────────────────

        /// <summary>
        /// Thứ tự chạy 9 điểm của "Auto Calib", theo số trên bàn phím numpad. Hình chữ S, KHÔNG phải quét dòng: module
        /// N-point của VisionMaster tự sinh toạ độ vật lý cho các điểm theo quỹ đạo chữ S — chạy theo thứ tự dòng thì
        /// điểm 4 và 6 rơi vào đúng chỗ ngược, ma trận vẫn fit ra được, chỉ méo, không có thông báo lỗi nào.
        /// </summary>
        static constexpr std::array<int, 9> TopDownGridSOrder = { 1, 2, 3, 6, 5, 4, 7, 8, 9 };

        /// <summary>Xoá điểm cũ trong module hiệu chuẩn của Flow ứng với <paramref name="procedure"/> — bước đầu của
        /// mọi chuỗi nạp điểm (Tuấn 2026-10-09). Trả RỖNG nếu xoá được, lý do nếu không (C#: null / lý do).</summary>
        std::string ClearCalibPoints(EVisionProcedure procedure);

        /// <summary>Di chuyển tới (x, y2) rồi kích nhánh hiệu chuẩn của Flow LocateTop (IsTriggerLocate = 0) — nạp một
        /// điểm vào module N-point. false nếu di chuyển thất bại hoặc Flow báo không tìm thấy.</summary>
        bool MoveAndTriggerTopDownCalib(double x, double y2, std::stop_token ct);

        /// <summary>Kích nhánh hiệu chuẩn của Flow LocateTop tại vị trí hiện tại — <b>cũng nạp thêm một điểm</b>.</summary>
        bool TriggerTopDownCalibFlow(std::stop_token ct);

        // ───────────────────────── §6.4 CAM bottom-up: nạp điểm hiệu chuẩn ─────────────────────────

        /// <summary>
        /// Cả chuỗi nạp điểm cho module hiệu chuẩn bottom-up của tay <paramref name="nozzle"/>: xoá điểm cũ, các vị trí X
        /// lệch so với vị trí soi (phần tịnh tiến, ở góc đầu của dãy), rồi các góc của trục A tại đúng vị trí soi (phần
        /// xoay), mỗi bước kích nhánh hiệu chuẩn một lần; kết thúc bằng việc đưa trục A về 0°. Danh sách rỗng = C# null.
        /// <paramref name="progress"/> (có thể rỗng) được gọi trên luồng chạy chuỗi.
        /// </summary>
        AlignCalibSequenceResult RunAlignCalibSequence(int nozzle, double alignPosX, const std::vector<double>& xOffsets,
                                                       const std::vector<double>& anglesDeg,
                                                       const std::function<void(const std::string&)>& progress, std::stop_token ct);

        /// <summary>Xoay trục A của tay tới <paramref name="angleDeg"/> rồi kích nhánh hiệu chuẩn — nạp một điểm lẻ.</summary>
        bool MoveAndTriggerAlignCalib(int nozzle, double angleDeg, std::stop_token ct);

        /// <summary>Kích nhánh hiệu chuẩn Flow AlignBottom của tay tại góc hiện tại — nạp thêm một điểm tại chỗ.</summary>
        bool TriggerAlignCalibFlow(int nozzle, std::stop_token ct);

        /// <summary>Đưa trục A của tay về 0° — tư thế xác định sau chuỗi nạp điểm hiệu chuẩn bottom-up.</summary>
        bool MoveAlignAxisToZero(int nozzle, std::stop_token ct);

        // ───────────────────────── §6.2b, §6.4b, §6.5 — đo hằng số ─────────────────────────

        /// <summary>Kết quả kiểm tra hiệu chuẩn gần nhất; nullopt = chưa kiểm lần nào.</summary>
        std::optional<VmCalibGate> LastCalibGate() const;

        /// <summary>Cổng hiệu chuẩn như lúc START — Bước 2/4/5, vị trí soi, ngưỡng. Chỉ đọc model của App.</summary>
        VmCalibGate CheckCalib();

        /// <summary>Bước 2 (§6.2b): soi dấu chuẩn ở ba chỗ (gốc, sau khi chạy X, sau khi chạy Y2) rồi App phán. Đạt thì
        /// ghi ngày vào bản ghi của App. Mọi lần trả trục về đều được kiểm.</summary>
        TopDownVerifyResult VerifyTopDown(double stepMm, std::stop_token ct);

        /// <summary>Bước 4 (§6.4b), sản phẩm đang trên tay tại vị trí soi: trung bình N ảnh trước và sau khi xoay thử rồi
        /// App tự phán. Đạt thì ghi dấu góc, vị trí soi và sai số dư vào bản ghi.</summary>
        AlignVerifyResult VerifyAlignRotation(int nozzle, double testAngleDeg, std::stop_token ct);

        // ───────────────────────── xác định hand-eye (§6.5) — chín nút + trả ─────────────────────────

        /// <summary>Bỏ tiến trình đang dở — chỉ để bài tự kiểm dựng lại trạng thái đầu giữa các kịch bản.</summary>
        void HandEyeClear();

        /// <summary>Bước 1 — tới vị trí gắp đã dạy trên cơ cấu kẹp, A về góc hút. KHÔNG chạm Z. Bấm lại là làm lại từ đầu.</summary>
        StepRunResult HandEyeGoToPick(int nozzle, std::stop_token ct);
        /// <summary>Bước 2 — hạ Z gắp con can mẫu trên cơ cấu kẹp (cao độ của tray LẮP RÁP), nâng lên; encoder đọc khi tay
        /// còn đứng trên cơ cấu kẹp.</summary>
        StepRunResult HandEyePick(int nozzle, std::stop_token ct);
        /// <summary>Bước 3 — CAM top-down tới vị trí soi đã dạy rồi soi chi tiết đích vừa lộ ra (N ảnh). Không xoay A.</summary>
        StepRunResult HandEyeLocate(int nozzle, std::stop_token ct);
        /// <summary>Bước 6 — phép thử tổng quát: kích LocateTop tại chỗ (một ảnh), không chạy trục.</summary>
        StepRunResult HandEyeLocateHere(int nozzle, std::stop_token ct);
        /// <summary>Bước 6 có chọn ô tray lắp ráp: <paramref name="row"/> âm = soi tại chỗ.</summary>
        StepRunResult HandEyeLocateHere(int nozzle, int row, int col, std::stop_token ct);
        /// <summary>Bước 4 — tay ra vị trí soi CAM bottom-up. Không xoay A.</summary>
        StepRunResult HandEyeGoToAlign(int nozzle, std::stop_token ct);
        /// <summary>Bước 7 của phép thử tổng quát — y như nút 4.</summary>
        StepRunResult HandEyeGoToAlignForTest(int nozzle, std::stop_token ct);
        /// <summary>
        /// Bước 5 — chụp AlignBottom_ToolN N ảnh (HasTarget = 0), ghi <c>HandEyeDx = X_gắp + ObjX − TopX</c>,
        /// <c>HandEyeDy = Y_gắp + BottomYSign·ObjY − TopY</c>, <c>mốc góc = ObjA + TopA</c> (CỘNG — hai camera ngược chiều).
        /// Xoay A về đúng encoder lúc gắp trước khi chụp (2026-09-25).
        /// </summary>
        HandEyeCommitResult HandEyeCommit(int nozzle, std::stop_token ct);
        /// <summary>Bước 8 — AlignBottom soi con can đang cầm, Group Cmd tính điểm đặt lên pose nút 6. KHÔNG chạy tới lệnh,
        /// tắt hàng rào lưới teach.</summary>
        StepRunResult HandEyeCheckTarget(int nozzle, std::stop_token ct);
        /// <summary>Bước 9 — chạy X, Y2, A tới điểm đích nút 8 rồi hạ Z tới ĐIỂM GIẢM TỐC của tray lắp ráp (không chạm).</summary>
        StepRunResult HandEyeGoToTarget(int nozzle, std::stop_token ct);
        /// <summary>Trả con can mẫu về cơ cấu kẹp: về vị trí gắp (A về góc hút), hạ Z, nhả hút và thổi, nâng Z. Không đòi
        /// bước nào trước đó. Xoá sổ chỉ khi cảm biến thật sự nhả.</summary>
        StepRunResult HandEyeReturn(int nozzle, std::stop_token ct);

        /// <summary>Bước 5b — chạy khô với sản phẩm mẫu đang trên tay: LocateTop soi dấu chuẩn coi là ô đích; AlignBottom +
        /// Group Cmd tính lệnh; App đưa tay tới lệnh ở Z an toàn. Không hạ Z.</summary>
        DryRunPlaceResult DryRunPlaceAlign(int nozzle, std::stop_token ct);

        // ───────────────────────── Chạy kiểm từng bước (trang Vision Edit) ─────────────────────────

        /// <summary>Đang cất số LocateTop của một ô (bước 2 đã chạy).</summary>
        bool StepHasTop() const;
        bool StepHasCommand() const;
        /// <summary>Bước 5 vừa cất một lệnh nằm NGOÀI hàng rào lưới teach — trang hỏi lại trước khi bước 6 chạy. Rỗng = trong.</summary>
        std::string StepGuardWarn() const;
        /// <summary>Bỏ số đo đang cất — camera đổi chỗ, đổi ô, hoặc người bấm muốn làm lại từ bước 1.</summary>
        void StepClear();

        /// <summary>Bước 1 — CAM top-down tới vị trí soi ô (hàng, cột) của tray lắp ráp, y như S05.</summary>
        StepRunResult StepMoveCamToCell(int row, int col, std::stop_token ct);
        /// <summary>Bước 2 — kích LocateTop tại chỗ và cất TopX/TopY/TopA làm pose ô đích cho bước 5.</summary>
        StepRunResult StepLocateTop(int row, int col, std::stop_token ct);
        /// <summary>Bước 3 — tay hút tới ô tray đầu vào, hạ, hút, nâng. Ghi sổ tay đang cầm hàng đúng như S1x.</summary>
        StepRunResult StepPick(int nozzle, int row, int col, std::stop_token ct);
        /// <summary>Bước 4 — chạy X tới vị trí soi CAM bottom-up của tay đang chọn.</summary>
        StepRunResult StepMoveToAlign(int nozzle, std::stop_token ct);
        /// <summary>Bước 5 — AlignBottom_ToolN: soi sản phẩm trên tay, Group Cmd tính lệnh đặt vào ô đã soi ở bước 2.</summary>
        StepRunResult StepAlignBottom(int nozzle, std::stop_token ct);
        /// <summary>Nút "Z1 → Z_SAFE" / "Z2 → Z_SAFE" ở thanh Jog — lệnh Move tới Z_SAFE (không phải về gốc).</summary>
        StepRunResult StepMoveZSafe(int nozzle, std::stop_token ct);
        /// <summary>Bước 6 — chạy A, X, Y tới lệnh vừa tính, giữ nguyên Z an toàn (không hạ, không nhả).</summary>
        StepRunResult StepMoveToCommand(int nozzle, std::stop_token ct);

    private:
        // ───────────── trạng thái bước xác định hand-eye ─────────────

        /// <summary>Tiến trình của bước xác định hand-eye đang dở.</summary>
        struct HandEyeProgress
        {
            int Nozzle{ 0 };
            /// <summary>Đã gắp xong. Phải có cờ riêng: 0.000 là toạ độ hợp lệ.</summary>
            bool Picked{ false };
            /// <summary>Encoder X, Y2 đọc ĐÚNG LÚC gắp, khi tay còn đứng trên cơ cấu kẹp — vào thẳng phép trừ.</summary>
            double PickX{ 0 }, PickY{ 0 };
            /// <summary>Encoder trục A của tay này đọc cùng lúc với PickX. Nút 5 xoay A về đúng góc này trước khi chụp.</summary>
            double PickA{ 0 };
            /// <summary>Số ảnh của lần soi chi tiết đích ở vị trí soi đã dạy; 0 = chưa soi.</summary>
            int Shots{ 0 };
            double TopX{ 0 }, TopY{ 0 }, TopA{ 0 };
            /// <summary>Encoder X/Y2 trung bình lúc chụp bước 3.</summary>
            double TopAxX{ 0 }, TopAxY{ 0 };
        };

        /// <summary>Trạng thái của phép thử tổng quát (nút 6 → 9) — tách khỏi m_he (Tuấn 2026-09-26).</summary>
        struct HandEyeTest
        {
            int Nozzle{ 0 };
            /// <summary>Lần soi của nút 6.</summary>
            double HereTopX{ 0 }, HereTopY{ 0 }, HereTopA{ 0 }, HereAxX{ 0 }, HereAxY{ 0 };
            /// <summary>Điểm đích nút 8 tính từ pose của lần soi gần nhất — nút 9 chạy tới đây.</summary>
            bool HasTarget{ false };
            double TargetX{ 0 }, TargetY{ 0 }, TargetA{ 0 };
        };

        // ───────────── đèn CAM / Flow ─────────────

        /// <summary>C# WithCamLampAsync&lt;T&gt;: bật đèn CAM tương ứng, chờ đèn sáng đủ (<c>SystemData::LampMsFor</c>), chạy
        /// việc cần camera, rồi TẮT đèn — kể cả khi lỗi/quá giờ/bị huỷ (RAII). Định nghĩa trong Routine_Calib.cpp.</summary>
        template <class T>
        T WithCamLamp(EVmCamera cam, std::stop_token ct, const std::function<T()>& run);
        /// <summary>Gọi một Flow kèm đèn CAM của đúng camera Flow đó dùng.</summary>
        VmCallResult CallVm(const VmCallRequest& req, std::stop_token ct);
        /// <summary>Vị trí THẬT của các trục (encoder) ngay trước khi nạp một điểm hiệu chuẩn.</summary>
        AxisSnapshot Snapshot();

        /// <summary>Chụp N ảnh bằng LocateTop (<paramref name="tool"/> = 0) hoặc AlignBottom_ToolN không kèm ô đích, rồi
        /// lấy trung bình (góc theo sin/cos). Mỗi ảnh phải thấy vật và echo khớp — lệch là huỷ cả chuỗi.</summary>
        VmMeanCapture CaptureMean(int tool, int shots, std::stop_token ct);
        static std::string SaveModelNote();

        // ───────────── cổng chặn hand-eye ─────────────
        std::string HandEyeTestNeeds(int nozzle, int stage);
        std::string HandEyeVsPick(int nozzle, double x, double y, const std::string& rowAxis);
        std::string HandEyeNeeds(int nozzle, int stage);

        StepRunResult HandEyeGoToPickCore(int nozzle, std::stop_token ct);
        StepRunResult HandEyePickCore(int nozzle, std::stop_token ct);
        StepRunResult HandEyeLocateCore(int nozzle, bool here, std::stop_token ct, int row = -1, int col = -1);
        StepRunResult HandEyeGoToAlignCore(int nozzle, const std::string& step, std::stop_token ct);
        StepRunResult HandEyeCheckTargetCore(int nozzle, std::stop_token ct);
        StepRunResult HandEyeGoToTargetCore(int nozzle, std::stop_token ct);
        StepRunResult HandEyeReturnCore(int nozzle, std::stop_token ct);

        // ───────────── chạy kiểm từng bước ─────────────
        StepRunResult StepMoveZSafeCore(int nozzle, std::stop_token ct);
        StepRunResult StepMoveCamCore(int row, int col, std::stop_token ct);
        StepRunResult StepLocateCore(int row, int col, std::stop_token ct);
        StepRunResult StepPickCore(int nozzle, int row, int col, std::stop_token ct);
        StepRunResult StepMoveToAlignCore(int nozzle, std::stop_token ct, const std::string& step = "4");
        StepRunResult StepAlignCore(int nozzle, std::stop_token ct);
        StepRunResult StepMoveToCommandCore(int nozzle, std::stop_token ct);

        // ───────────── tiện ích ─────────────

        /// <summary>Vỏ chung của các nút chạy từng bước: bắt huỷ/lỗi như mọi routine khác (base::OperationCanceled →
        /// "Calib.Cancelled"; std::exception → alarm RoutineCrashed sự kiện + "Calib.UnexpectedError"), và ghi log MỘT dòng
        /// cho mỗi lần bấm. <paramref name="logTag"/> tách hai khu nút trong log ("Step check" / "Hand-eye").</summary>
        StepRunResult StepGuard(const std::string& what, const std::function<StepRunResult()>& body,
                                const std::string& logTag = "Step check");
        void SettleVacuum(bool gripped, std::stop_token ct);
        bool WaitVacuum(int nozzle, bool want, std::stop_token ct);
        /// <summary>Số thứ tự ô theo (hàng, cột) — phải dò vì thứ tự quét có thể là hình rắn. −1 nếu không có.</summary>
        static int CellIndexOf(const TrayState& tray, int row, int col);
        /// <summary>C# private MoveAsync(axis, pos, ct): McFunction::Move ở MoveSpeed của trục.</summary>
        bool Move(const std::string& axisName, double pos, std::stop_token ct);
        /// <summary>Cao độ soi CAM bottom-up: Z của tay đang soi về đúng Z_SAFE — mọi ảnh bottom-up, từ nạp điểm hiệu
        /// chuẩn tới chạy thật, phải chụp ở cùng một cao độ (Tuấn chốt 2026-09-22).</summary>
        bool MoveZToBottomCapture(int nozzle, std::stop_token ct);
        static std::string ZCaptureFail(int nozzle);
        static std::string ZName(int nozzle);
        static std::string AName(int nozzle);
        /// <summary>Vị trí encoder; không có trục thì NaN.</summary>
        double Pos(const std::string& axis);
        /// <summary>"a, b, c" dạng "0.###"; rỗng → "—".</summary>
        static std::string Join(const std::vector<double>& values);
        /// <summary>Một dòng log cho mỗi lần kích Flow nạp điểm hiệu chuẩn. <paramref name="nozzle"/> = 0 là CAM top-down.</summary>
        void LogCalibPoint(const std::string& what, const AxisSnapshot& s, int nozzle, bool found);

        // ───────────── trường ─────────────

        MotionConnect& m_motion;
        McFunction& m_func;
        std::function<std::shared_ptr<SystemData>()> m_sys;
        std::function<std::shared_ptr<IVisionDevice>()> m_vision;

        /// <summary>Khoá mọi trạng thái giữa các nút (m_lastCalibGate, m_he, m_heTest, m_step*) — UI đọc getter trong lúc
        /// một chuỗi đang chạy trên luồng nền. Đệ quy; KHÔNG giữ trong lúc chạy trục hay gọi Flow.</summary>
        mutable std::recursive_mutex m_stateMutex;

        std::optional<VmCalibGate> m_lastCalibGate;

        /// <summary>Tiến trình của bước xác định hand-eye đang dở; nullopt = chưa bấm nút nào. Trạng thái giữa các nút nằm
        /// ở ĐÂY, không ở ViewModel — bài tự kiểm gọi thẳng lớp này nên mọi cổng chặn đều được kiểm.</summary>
        std::optional<HandEyeProgress> m_he;
        /// <summary>Phép thử tổng quát; nullopt = chưa soi.</summary>
        std::optional<HandEyeTest> m_heTest;

        std::optional<PlacedTopRecord> m_stepTop;
        int m_stepRow{ -1 }, m_stepCol{ -1 };
        std::optional<VmPlaceCommand> m_stepCmd;
        int m_stepCmdNozzle{ 0 };
        double m_stepCmdACap{ 0 };
        std::string m_stepGuardWarn;
    };
}
