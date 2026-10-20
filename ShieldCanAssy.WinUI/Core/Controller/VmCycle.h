#pragma once
#include <climits>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "Communication/MotionConnect.h"
#include "Communication/VmContract.h"
#include "Communication/VmOutCheck.h"
#include "Data/Model/ModelData.h"
#include "Data/SystemData.h"

namespace sca
{
    struct PlacedTopRecord;   // Controller/Routine_Auto.h — kết quả quét ô đích

    /// <summary>Kết quả cổng hiệu chuẩn phía App (<see cref="VmCycle::EvaluateCalib"/>). Chỉ đọc model của App —
    /// hằng số hiệu chuẩn không còn bản sao nào trong solution để đối chiếu.</summary>
    struct VmCalibGate
    {
        /// <summary>Lý do chặn vì hiệu chuẩn chưa sẵn sàng (alarm 4011); RỖNG là không có (C#: null).</summary>
        std::string Block;

        double HandEyeDx1{ 0 }, HandEyeDy1{ 0 }, HandEyeDx2{ 0 }, HandEyeDy2{ 0 };
        /// <summary>Mốc góc giữa hai camera của từng tay, độ (<c>VisionCalib::HandEyeAngleRef</c>).</summary>
        double AngleRef1{ 0 }, AngleRef2{ 0 };
        int AlignSign1{ 0 }, AlignSign2{ 0 };

        /// <summary>Số ngày tính từ lần đo cũ nhất trong bản ghi (Bước 2, 4, 5); NaN nếu chưa có lần nào.</summary>
        double DaysSinceOldest = std::numeric_limits<double>::quiet_NaN();

        bool Ready() const { return Block.empty(); }
        std::string Describe() const;
    };

    /// <summary>Nhánh điều hướng sau hàm chặn lệnh đặt — bảng 3.5 của thiết kế.</summary>
    enum class EVmRoute
    {
        /// <summary>Cất lệnh, lúc đặt chạy trục tới đó.</summary>
        Ok = 0,
        /// <summary>N — vượt ngưỡng góc bù/lệch tâm: trả sản phẩm về ô nguồn, tính NG.</summary>
        Ng = 1,
        /// <summary>G — đích lệch lưới teach: bỏ ô, giữ sản phẩm sang ô kế.</summary>
        Skip = 2,
        /// <summary>E — lỗi hệ thống/hợp đồng: dừng, giữ hàng, chạy lại từ S00.</summary>
        Error = 3
    };

    /// <summary>Tên enum như C# <c>Route.ToString()</c>: "Ok", "Ng", "Skip", "Error".</summary>
    const char* ToString(EVmRoute r);

    struct VmPlaceCommand
    {
        EVmRoute Route{ EVmRoute::Error };
        std::string Why;
        int Status{ INT_MIN };
        /// <summary>Lệnh theo CHIỀU MÁY (CmdY2 / DiagTarY đã đổi dấu lại từ hệ camera dưới — xem BottomYSign).</summary>
        double CmdX{ 0 }, CmdY2{ 0 }, CmdA{ 0 }, DiagDA{ 0 }, DiagEcc{ 0 }, DiagTarX{ 0 }, DiagTarY{ 0 };
    };

    /// <summary>Kết quả đọc một Flow có camera.</summary>
    struct VmCameraRead
    {
        bool CallOk{ false };
        std::string CallError;

        /// <summary>Echo (RunId, trục vừa ghi) khớp — sai là đọc nhầm Flow, nhầm biến hoặc nhầm lần chạy.</summary>
        bool EchoOk{ true };
        std::string EchoIssue;

        /// <summary>Output <c>Found</c> — nguồn duy nhất của "thấy / không thấy".</summary>
        bool Found{ false };

        /// <summary>Echo khớp và (nếu thấy) mọi output mới hợp lệ.</summary>
        bool VmValid{ false };
        std::string VmIssue;

        /// <summary>TopX/TopY/TopA · ObjX/ObjY/ObjA.</summary>
        double X{ 0 }, Y{ 0 }, A{ 0 };

        /// <summary>Encoder X/Y2 App ghi vào biến lúc chụp — chỉ LocateTop điền (<see cref="VmCycle::ReadLocate"/>).</summary>
        double AxX{ std::numeric_limits<double>::quiet_NaN() }, AxY{ std::numeric_limits<double>::quiet_NaN() };
    };

    /// <summary>Trung bình N ảnh App lấy cho phép đo hằng số: tư thế vật và vị trí trục lúc chụp.</summary>
    struct VmMeanCapture
    {
        bool Ok{ false };
        std::string Reason;
        int Shots{ 0 };
        double X{ 0 }, Y{ 0 }, A{ 0 };
        double AxX{ 0 }, AxY{ 0 }, AxA{ 0 };
    };

    /// <summary>Vết một lần kích AlignBottom (<see cref="VmCycle::TraceAlignBottom"/>).</summary>
    struct VmAlignTrace
    {
        /// <summary>Dòng log đầy đủ: vị trí trục, biến App ghi, output Flow trả, phép tính lại.</summary>
        std::string Line;
        /// <summary>Có đối chiếu được không (HasTarget = 1, thấy sản phẩm, Status = 1).</summary>
        bool Checked{ false };
        bool Match{ false };
        double ErrMm{ 0 }, ErrDeg{ 0 };
        /// <summary>Riêng phần đối chiếu — để gắn vào bảng kết quả. Rỗng khi không đối chiếu (C#: null).</summary>
        std::string Check;
    };

    /// <summary>Phán quyết Bước 4 (<see cref="VmCycle::JudgeAlignSign"/>) — App tự tính, không qua Flow nào.</summary>
    struct VmAlignJudge
    {
        int Status{ 0 };
        /// <summary>+1 hoặc −1 khi kết luận được; 0 khi không.</summary>
        int Sign{ 0 };
        double Residual{ 0 }, Ecc{ 0 }, RawDTheta{ 0 }, EncDA{ 0 };
    };

    /// <summary>Phán quyết Bước 2 (<see cref="VmCycle::JudgeTopDown"/>).</summary>
    struct VmTopDownJudge
    {
        int Status{ 0 };
        double StepX{ 0 }, StepY{ 0 }, ErrX{ 0 }, ErrY{ 0 }, CrossX{ 0 }, CrossY{ 0 };
    };

    /// <summary>
    /// Phần chung của <c>Routine_Auto</c> và <c>Routine_Calib</c>: dựng yêu cầu gọi Flow, đọc và đối chiếu kết quả, hàm
    /// chặn, cổng hiệu chuẩn. Không có phép bù nào ở đây — chỉ so sánh (Tuấn chốt 2026-09-13: App chỉ chấp hành).
    ///
    /// <para>Mọi hàm là hàm thuần (không chặn), gọi được từ mọi luồng. Bộ biến toàn cục là
    /// <c>std::map&lt;std::string, double&gt;</c> — cùng kiểu với <c>VmCallRequest::Globals</c>.</para>
    /// </summary>
    class VmCycle
    {
    public:
        VmCycle() = delete;

        // ───────────────────────── biến App ghi ─────────────────────────

        /// <summary>Tên biến trục trống ở trang Setting là lỗi cứng. RỖNG = không có vấn đề (C#: null).</summary>
        static std::string AxisVarProblem(const VisionOutputNames* n);

        /// <summary>Vị trí ENCODER của bốn trục (X, Y2, A1, A2) + RunId — ghi trước mọi Flow. motion = nullptr → mọi
        /// vị trí là 0.</summary>
        static std::map<std::string, double> AxisGlobals(const MotionConnect* motion, const VisionOutputNames& n, int runId);
        static std::map<std::string, double> AxisGlobals(const MotionConnect& motion, const VisionOutputNames& n, int runId)
        {
            return AxisGlobals(&motion, n, runId);
        }

        // ───────────────────────── yêu cầu gọi Flow ─────────────────────────

        /// <summary>
        /// LocateTop: <c>Found</c> và echo bắt buộc. Output của Script đọc dạng tuỳ chọn và chỉ bị đòi khi
        /// <c>Found = 1</c> (<see cref="ReadLocate"/>) — khi không thấy mẫu, chưa chắc Script còn chạy.
        /// <para>Như bản C#: GHI <c>IsTriggerLocate = 1</c> vào chính <paramref name="globals"/> (nhánh soi của Flow,
        /// Tuấn 2026-10-08), rồi chép bộ biến vào yêu cầu.</para>
        /// </summary>
        static VmCallRequest LocateRequest(const VisionOutputNames& n, std::map<std::string, double>& globals);

        /// <summary>
        /// Thêm tám biến của lần đặt vào bộ biến sẽ ghi trước <c>AlignBottom_ToolN</c>. <paramref name="target"/> là pose
        /// ô đích (số LocateTop đo lúc quét tray) — nullptr thì <c>HasTarget = 0</c> và Group Cmd nghỉ, Flow chỉ soi
        /// (Bước 4). Hand-eye và dấu góc lấy của <b>đúng tay đang gọi</b>: đây là chỗ duy nhất chọn theo tay.
        /// <para><c>CellA_deg</c> = <b>mốc góc</b> của tay <b>−</b> góc LocateTop + bù góc của ô — quy góc camera trên về
        /// hệ góc của camera dưới (hai camera nhìn ngược chiều nhau nên đo góc ngược dấu). Thay nút Create Reference mà
        /// module Single Point Alignment của VM 4.4 không có (Tuấn duyệt 2026-09-22).</para>
        /// </summary>
        /// <param name="trim">Bù đặt tổng của đúng ô đích và đúng tay (<c>TrayLayout::PlaceTrim</c>), hoặc nullptr. Cộng
        /// thẳng vào pose đích gửi đi — Group Cmd không biết có lượng bù nào; vì vậy hàng rào lưới ở
        /// <see cref="CheckPlace"/> cũng phải dời tâm đi đúng lượng này.</param>
        static void AddPlaceGlobals(std::map<std::string, double>& g, const PlacedTopRecord* target, const VisionCalib* calib, int tool,
                                    const CellOffset* trim = nullptr);

        /// <summary>
        /// Trục Y của camera dưới NGƯỢC chiều trục Y2 của máy (Tuấn thấy trên máy 2026-09-30): CAM dưới hiệu chuẩn bằng
        /// tịnh tiến chỉ theo X nên VisionMaster tự chọn chiều Y và ra hệ lật. Single Point Alignment trong Group Cmd tính
        /// trong hệ camera dưới, nên App đổi dấu Y ở cả hai đầu: ghi <c>CellY_mm</c>/<c>HandEyeDy_mm</c> đảo dấu
        /// (<see cref="AddPlaceGlobals"/>), đọc <c>CmdY2_mm</c>/<c>DiagTarY_mm</c> rồi đảo lại (<see cref="CheckPlace"/>).
        /// Hand-eye Y trừ lệch tâm Y thay vì cộng (<c>Routine_Calib::HandEyeCommit</c>).
        /// </summary>
        static constexpr double BottomYSign = -1;

        /// <summary>
        /// AlignBottom_ToolN. <paramref name="hasTarget"/> = true thì đọc thêm output của Group Cmd — bộ biến
        /// <paramref name="globals"/> phải đã đi qua <see cref="AddPlaceGlobals"/>. Ghi <c>IsTriggerLocate = 1</c> vào
        /// <paramref name="globals"/> như <see cref="LocateRequest"/>.
        /// </summary>
        static VmCallRequest AlignBottomRequest(const VisionOutputNames& n, int tool, std::map<std::string, double>& globals, bool hasTarget);

        // ───────────────────────── hợp đồng Output Params ─────────────────────────

        /// <summary>Bộ output mà từng Flow (ba Flow) phải khai — dựng từ CHÍNH hai hàm tạo lời gọi ở trên nên không thể
        /// lệch với thứ App thật sự đọc lúc chạy. Gộp cả Outputs lẫn OptionalOutputs.</summary>
        static std::vector<VmFlowExpect> ExpectedFlowOutputs(const VisionOutputNames& n);

        /// <summary>Bản khai "đúng hết" của một Flow — mô phỏng dùng, để nút kiểm tra lúc SimulationMode vẫn ra một báo
        /// cáo có nghĩa thay vì bảy dòng lỗi.</summary>
        static VmFlowOutputs FakeFlowOutputs(const std::string& flow, const VisionOutputNames& n);

        // ───────────────────────── đọc kết quả Flow có camera ─────────────────────────

        static VmCameraRead ReadLocate(const VmCallResult& r, const std::map<std::string, double>& sent, const VisionOutputNames& n);

        /// <summary>AlignBottom_ToolN: echo trục và RunId; tư thế sản phẩm khi thấy. Phần lệnh đặt đọc ở
        /// <see cref="CheckPlace"/>.</summary>
        static VmCameraRead ReadAlignBottom(const VmCallResult& r, const VmCallRequest& req, const VisionOutputNames& n, int tool);

        // ───────────────────────── hàm chặn lệnh đặt (mục 3.4) ─────────────────────────

        /// <summary>
        /// Hàm chặn sau AlignBottom_ToolN kèm ô đích — chỉ so sánh, theo thứ tự a–h của thiết kế. Không tính lại lệnh
        /// nào: số trục lấy nguyên văn từ Flow, App chỉ quyết định có dám chạy tới đó hay không.
        /// </summary>
        /// <param name="aSent">Góc trục A của tay này App vừa ghi vào biến toàn cục — gốc của <c>CmdA</c>.</param>
        /// <param name="cellX">Toạ độ ô theo lưới teach của ĐÚNG tay đặt (+ bù đặt).</param>
        /// <param name="xInLimit">Soft limit của trục X; hàm rỗng = không xét.</param>
        /// <param name="checkGrid">false = bỏ phép chặn lưới (f) — chỉ cho chạy khô trên dấu chuẩn.</param>
        static VmPlaceCommand CheckPlace(const VmCallResult& r, const VmCallRequest& req, int tool, double aSent,
                                         double cellX, double cellY, double guardMm, double maxAngleDeg, double maxEccMm,
                                         const std::function<bool(double)>& xInLimit, const std::function<bool(double)>& yInLimit,
                                         bool checkGrid);

        // ───────────────────────── Bước 4 / Bước 2 — App tự phán ─────────────────────────

        /// <summary>
        /// Phép thử vòng xoay: hai lần đo tư thế sản phẩm (trước và sau khi xoay trục A một góc đã biết), giải đúng bài
        /// toán Single Point Alignment giữa chúng → <b>dấu góc</b> và <b>sai số dư</b> (phải bằng 0 nếu gốc toạ độ nằm
        /// đúng tâm xoay). Trước 14/09/2026 phép này chạy trong Flow <c>AlignVerify_ToolN</c>.
        /// </summary>
        static VmAlignJudge JudgeAlignSign(const VmMeanCapture& s0, const VmMeanCapture& s1,
                                           double tolMm, double angleTolDeg, double minEccMm, double minTurnDeg);

        /// <summary>Ba lần đo dấu chuẩn (gốc, sau khi chạy X, sau khi chạy Y2). <c>CamX/CamY</c> là toạ độ tuyệt đối nên
        /// dấu chuẩn đứng yên phải cho cùng một số dù trục chạy — bắt được sai thang đo, sai dấu và lẫn trục X ↔ Y.</summary>
        static VmTopDownJudge JudgeTopDown(const VmMeanCapture& s0, const VmMeanCapture& s1, const VmMeanCapture& s2, double tolMm);

        // ───────────────────────── cổng hiệu chuẩn phía App ─────────────────────────

        /// <summary>
        /// Cổng hiệu chuẩn: đủ Bước 2, 4, 5, vị trí soi chưa teach lại, ngưỡng hợp lệ. <b>Chỉ đọc model của App.</b>
        /// <para><paramref name="nozzle"/> = 0 (Auto) đòi cả hai tay. Chạy khô một tay chỉ dùng hằng số của tay đó, nên gọi
        /// với 1 hoặc 2: tay kia chưa hiệu chuẩn không được chặn (Tuấn gặp 2026-09-22).</para>
        /// </summary>
        static VmCalibGate EvaluateCalib(const ModelData* model, const SystemData* sys, int nozzle = 0);

        // ───────────────────────── vết một lần kích AlignBottom ─────────────────────────

        /// <summary>Dung sai đối chiếu Group Cmd với phép tính lại của App: output của Flow là float 32 bit.</summary>
        static constexpr double CmdCheckTolMm = 0.005, CmdCheckTolDeg = 0.01;

        /// <summary>
        /// Một dòng log cho MỖI lần kích <c>AlignBottom_ToolN</c>: vị trí trục lúc chụp, mọi biến App vừa ghi, mọi output
        /// đọc về — rồi App <b>tính lại</b> đúng phép của Group Cmd (Single Point Alignment + Script Cmd) và so với lệnh
        /// Flow trả về. Chỉ đối chiếu để ghi log, không thay số của Flow (Tuấn yêu cầu 2026-09-22).
        /// </summary>
        static VmAlignTrace TraceAlignBottom(const VmCallResult& r, const VmCallRequest& req, const VisionOutputNames& n, int tool, double zPos);

        // ───────────────────────── tiện ích ─────────────────────────

        /// <summary>Giá trị của khoá; không có thì NaN.</summary>
        static double Val(const std::map<std::string, double>& d, const std::string& key);

        /// <summary>Trung bình góc theo sin/cos, độ, (−180, 180] — không gãy khi các lần đo nằm hai bên ±180°.</summary>
        static double MeanAngleDeg(double sumSin, double sumCos);

        /// <summary>"0.###" bất biến; NaN → "NaN (missing)", ±vô cực → "Infinity"/"-Infinity" như .NET.</summary>
        static std::string Fmt(double v);
    };
}
