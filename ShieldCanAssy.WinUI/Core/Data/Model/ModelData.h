#pragma once
#include "Data/Model/TrayLayout.h"
#include "Utility/DateTimeText.h"

#include <memory>
#include <optional>
#include <string>

namespace sca
{
    struct TrayState;

    /// <summary>
    /// Bản ghi hiệu chuẩn vision của App — bản 2026-09-14 (phương án B).
    ///
    /// <para><b>Đây là nơi duy nhất giữ hằng số hiệu chuẩn.</b> App tự đo hand-eye (Bước 5) và dấu góc (Bước 4) rồi ghi
    /// vào đây kèm ngày đo và vị trí soi đã xác minh; trước mỗi lần soi sản phẩm, App ghi hằng số của đúng tay đang gọi
    /// vào biến toàn cục để Group <c>Cmd</c> dùng. Solution <b>không</b> cất bản sao nào, nên không có gì để đối chiếu
    /// ngược. Cổng Auto (<c>VmCycle.EvaluateCalib</c>) dựa vào bản ghi này: thiếu bước hoặc vị trí soi teach lại thì
    /// chặn. Hiệu chuẩn lại camera thì chạy lại Bước 2, 4, 5.</para>
    ///
    /// <para><b>Không có ma trận camera nào ở đây</b> (Tuấn chốt 2026-09-12): quy đổi pixel → mm nằm trong
    /// module Calibration Transformation của solution.</para>
    ///
    /// <para>DateTime.MinValue của C# = <c>TimePoint{}</c> ("chưa đo"; JSON "0001-01-01T00:00:00").</para>
    /// </summary>
    struct VisionCalib
    {
        /// <summary>Hand-eye: offset từ tâm quang CAM top-down tới tâm mỗi đầu hút (§6.5).</summary>
        double HandEye1Dx = 0;
        double HandEye1Dy = 0;
        double HandEye2Dx = 0;
        double HandEye2Dy = 0;

        /// <summary>Thời điểm đo hand-eye — <c>MinValue</c> nghĩa là CHƯA đo, phân biệt với "đã đo
        /// và ra đúng 0", hai số double mặc định 0 không tự nói lên được điều đó.</summary>
        TimePoint HandEye1MeasuredAt{};
        TimePoint HandEye2MeasuredAt{};

        /// <summary>
        /// Mốc góc giữa hai camera của từng tay, độ: góc AlignBottom (camera dưới) <b>+</b> góc LocateTop (camera trên),
        /// đo ở bước xác định hand-eye trên cùng một con can lắp đúng, chưa xoay A. App dựng góc đích bằng
        /// <c>mốc góc − góc LocateTop</c>, xem <c>VmCycle.AddPlaceGlobals</c>.
        /// Thay cho nút Create Reference — module Single Point Alignment của VM 4.4 không có nút đó.
        ///
        /// <para><b>Cộng chứ không trừ</b> vì hai camera nhìn ngược chiều nhau nên đo góc ngược dấu: CAM trên hiệu chuẩn
        /// bằng lưới 9 điểm dạy theo toạ độ máy nên bám chiều máy, CAM dưới hiệu chuẩn bằng tịnh tiến chỉ theo trục X
        /// nên không ghim được hướng trục Y và ra hệ lật chiều. Đo trên máy 2026-09-23: xoay chi tiết đích −5.02° thì
        /// góc con can đúng đổi +5.08° — độ dốc −1.01, không phải +1.</para>
        ///
        /// <para><c>nullopt</c> = bản ghi hand-eye cũ (chưa có mốc góc, hoặc mốc góc đo bằng công thức trừ trước
        /// 2026-09-23) — cổng Auto chặn, phải chạy lại bước xác định hand-eye.</para>
        /// </summary>
        std::optional<double> HandEye1AngleRef;
        std::optional<double> HandEye2AngleRef;

        /// <summary>
        /// Hand-eye đo bằng công thức đã quy lệch tâm Y của camera dưới về chiều máy (Tuấn 2026-09-30, xem
        /// <c>VmCycle.BottomYSign</c>). <c>false</c> = bản ghi trước đó: <c>HandEyeDy</c> mang lệch tâm Y ngược dấu, lệch
        /// hai lần lệch tâm Y lúc đo — cổng Auto chặn, phải chạy lại bước xác định hand-eye.
        /// </summary>
        bool HandEye1YFixed = false;
        bool HandEye2YFixed = false;

        /// <summary>
        /// Pose chi tiết đích LocateTop đo ở bước 3 của lần xác định hand-eye — điểm teach hand-eye của từng tay, toạ độ
        /// máy. Nút 6 in ΔX/ΔY/Δθ của lần soi so với điểm này (Tuấn 2026-09-30). <c>nullopt</c> = bản ghi đo trước 30/09.
        /// </summary>
        std::optional<double> HandEye1TopX;
        std::optional<double> HandEye1TopY;
        std::optional<double> HandEye1TopA;
        std::optional<double> HandEye2TopX;
        std::optional<double> HandEye2TopY;
        std::optional<double> HandEye2TopA;

        /// <summary>
        /// Vị trí trục X/Y2 lúc chụp master (bước 3) — khung top-down trang Main trừ quãng trục đã chạy kể từ đó, để Δ là
        /// độ lệch của chi tiết TRONG ẢNH so với ảnh master (Tuấn 2026-10-01). <c>nullopt</c> = bản ghi trước 01/10.
        /// </summary>
        std::optional<double> HandEye1TopAxX;
        std::optional<double> HandEye1TopAxY;
        std::optional<double> HandEye2TopAxX;
        std::optional<double> HandEye2TopAxY;

        void SetHandEyeTarget(int nozzle, double x, double y, double a, double axX, double axY);

        bool TryGetHandEyeMasterAxis(int nozzle, double& axX, double& axY) const;

        bool TryGetHandEyeTarget(int nozzle, double& x, double& y, double& a) const;

        /// <summary>Dấu quy đổi góc VisionMaster → trục A của từng tay: +1, −1, hoặc <b>0 = chưa
        /// xác minh</b>. Flow AlignVerify_ToolN ghi vào solution, App chép sang đây.</summary>
        int AlignAngleSign1 = 0;
        int AlignAngleSign2 = 0;

        TimePoint AlignVerifiedAt1{};
        TimePoint AlignVerifiedAt2{};

        /// <summary>Vị trí trục X lúc chạy phép thử vòng xoay — phải trùng <c>AlignPosX_NozzleN</c>
        /// lúc chạy Auto (PDF tr.45).</summary>
        double AlignVerifiedPosX1 = 0;
        double AlignVerifiedPosX2 = 0;

        /// <summary>Sai số dư của phép thử vòng xoay gần nhất, mm — 0 nghĩa là tâm xoay và
        /// handedness của file hiệu chuẩn bottom-up nhất quán.</summary>
        double AlignVerifyResidual1 = 0;
        double AlignVerifyResidual2 = 0;

        /// <summary>Bước 2 (kiểm tra top-down) đạt lúc nào — <c>MinValue</c> là chưa đạt.</summary>
        TimePoint TopDownVerifiedAt{};

        bool IsTopDownVerified() const { return TopDownVerifiedAt > TimePoint{}; }

        double HandEyeDx(int nozzle) const { return nozzle == 1 ? HandEye1Dx : HandEye2Dx; }
        double HandEyeDy(int nozzle) const { return nozzle == 1 ? HandEye1Dy : HandEye2Dy; }
        TimePoint HandEyeMeasuredAt(int nozzle) const { return nozzle == 1 ? HandEye1MeasuredAt : HandEye2MeasuredAt; }
        double HandEyeAngleRef(int nozzle) const { return (nozzle == 1 ? HandEye1AngleRef : HandEye2AngleRef).value_or(0); }

        int AlignAngleSign(int nozzle) const { return nozzle == 1 ? AlignAngleSign1 : AlignAngleSign2; }
        TimePoint AlignVerifiedAt(int nozzle) const { return nozzle == 1 ? AlignVerifiedAt1 : AlignVerifiedAt2; }
        double AlignVerifiedPosX(int nozzle) const { return nozzle == 1 ? AlignVerifiedPosX1 : AlignVerifiedPosX2; }

        void SetHandEye(int nozzle, double dx, double dy, double angleRef, TimePoint at);

        void SetAlignVerified(int nozzle, int sign, double posX, double residualMm, TimePoint at);

        /// <summary>Đã đo hand-eye bằng công thức hiện hành. Bản ghi cũ tính là chưa đo.</summary>
        bool IsHandEyeMeasured(int nozzle) const { return HandEyeMeasuredAt(nozzle) > TimePoint{} && !IsHandEyeOutdated(nozzle); }

        /// <summary>Hand-eye đo bằng công thức cũ — chưa có mốc góc (trước 23/09) hoặc chưa quy lệch tâm Y của camera dưới
        /// về chiều máy (trước 30/09) — phải chạy lại bước xác định hand-eye.</summary>
        bool IsHandEyeOutdated(int nozzle) const;

        bool IsAlignVerified(int nozzle) const { return AlignAngleSign(nozzle) != 0 && AlignVerifiedAt(nozzle) > TimePoint{}; }

        /// <summary>
        /// Bản ghi có đủ hand-eye và dấu góc của cả hai tay chưa. Cổng Auto KHÔNG dựa vào đây (nó dùng
        /// <c>VmCycle.EvaluateCalib</c>); chỉ còn dùng để quyết định có nạp model giả khi chạy mô phỏng.
        /// </summary>
        bool IsUsable() const
        {
            return IsHandEyeMeasured(1) && IsHandEyeMeasured(2) && IsAlignVerified(1) && IsAlignVerified(2);
        }

        /// <summary>Quá hạn tính từ lần xác minh Align cũ nhất (§6.1 quy tắc 7, alarm 4090).</summary>
        bool IsExpired(int validDays) const;
    };

    /// <summary>Dữ liệu một model sản phẩm — lưu ở Model\&lt;tên&gt;.json. Ba tray là shared_ptr (tham chiếu như bản
    /// C#, không bao giờ null sau khi nạp/NormalizeInputTrays).</summary>
    struct ModelData
    {
        std::string Name = "DEFAULT";
        /// <summary>Rỗng đứng vai null — không ghi khoá ra JSON.</summary>
        std::string Description;
        TimePoint ModifiedAt = std::chrono::system_clock::now();

        /// <summary>Tray đầu vào 1 — chỗ đặt PHÍA TRONG của bàn Y1. Xem <see cref="InputTray2"/>.</summary>
        std::shared_ptr<TrayLayout> InputTray = TrayLayout::CreateInputDefault();
        std::shared_ptr<TrayLayout> AssyTray = TrayLayout::CreateAssyDefault();

        /// <summary>
        /// Tray đầu vào 2 — chỗ đặt PHÍA NGOÀI của bàn Y1, từ 2026-10-15 bàn đặt hai tray (Tuấn chốt). Lưới và cao độ Z
        /// teach RIÊNG như Tray 1; số hàng/cột, thứ tự quét và trục luôn theo Tray 1 vì cùng một loại tray
        /// (<see cref="NormalizeInputTrays"/>). Chưa teach thì máy chạy một tray như trước.
        /// </summary>
        std::shared_ptr<TrayLayout> InputTray2 = TrayLayout::CreateInputDefault("InputTray2");

        /// <summary>Số chỗ đặt tray đầu vào trên bàn Y1 — Tray 1 phía trong, Tray 2 phía ngoài.</summary>
        static constexpr int InputTraySlots = 2;

        /// <summary>
        /// Số tray đầu vào máy dùng: 2 khi Tray 2 đã teach, không thì 1 — y như trước khi có Tray 2. Sổ tiến độ
        /// <c>RunningData::InputTray</c> nối các tray này liền nhau theo hàng — hàng 0..R-1 là Tray 1, hàng R..2R-1 là
        /// Tray 2 — nên máy ghé hết Tray 1 rồi mới sang Tray 2, và mọi phép tính trên chỉ số ô (giao ô hút, ô nguồn của
        /// sản phẩm, trả NG, đếm) không phải biết có mấy tray. Xem <see cref="InputCell"/>.
        /// </summary>
        int InputTraysInUse() const { return InputTray2 && InputTray2->IsTaught() ? 2 : 1; }

        /// <summary>Lưới của tray đầu vào thứ <paramref name="tray"/> (0 = Tray 1, 1 = Tray 2).</summary>
        std::shared_ptr<TrayLayout> InputLayout(int tray) const { return tray == 1 ? InputTray2 : InputTray; }

        /// <summary>Các tray đầu vào dùng được — bit 0 Tray 1, bit 1 Tray 2 (khi đã teach). Dùng khi nạp tray mà không đọc
        /// được cảm biến: Tray 1 luôn có như trước khi có Tray 2, Tray 2 chỉ khi đã teach.</summary>
        int UsableInputTrays() const { return InputTraysInUse() == 2 ? 3 : 1; }

        /// <summary>
        /// Ô thứ <paramref name="index"/> của sổ tray đầu vào nằm ở tray nào, hàng/cột nào TRONG tray đó — trả về lưới của
        /// tray ấy. Chỗ DUY NHẤT đổi chỉ số sổ ra tray: mọi chỗ cần toạ độ hay cao độ Z của một ô tray đầu vào đều qua đây.
        /// </summary>
        std::shared_ptr<TrayLayout> InputCell(const TrayState& run, int index, int& tray, int& row, int& col) const;

        /// <summary>
        /// Tray 2 cùng loại với Tray 1: số hàng/cột, thứ tự quét và trục chép theo Tray 1. Model lưu trước khi có Tray 2
        /// (lưới chưa teach, Z hút = 0) thì lấy sẵn các cao độ Z của Tray 1 — cùng loại tray nên gần đúng, vẫn phải teach
        /// lại. Gọi mỗi lần nạp/dùng model và mỗi lần trang Teaching đổi kích thước hay thứ tự quét.
        /// </summary>
        void NormalizeInputTrays();

        VisionCalib Calib;

        /// <summary>
        /// Cao độ an toàn Z (mm) và file .solw RIÊNG của model (Tuấn 2026-11-07) — trước đó chung cho cả máy ở system.json.
        /// Giá trị máy đang chạy vẫn đọc ở SystemData::ZSafe / VisionSolutionPath, chép từ đây mỗi lần nạp/dùng model
        /// (McDataManager::LoadModel / UseModel; McControl::ApplyModel đưa xuống MotionConnect và nạp solution). Model lưu
        /// trước ngày đó không có hai khoá này (nullopt / rỗng): lần nạp đầu nhận giá trị system.json đang dùng rồi ghi
        /// luôn vào file model.
        /// </summary>
        std::optional<double> ZSafe;
        std::string VisionSolutionPath;

        /// <summary>Vị trí X để đưa từng tay hút vào giữa khung hình CAM bottom-up.</summary>
        double AlignPosX_Nozzle1 = 0;
        double AlignPosX_Nozzle2 = 0;

        double AlignPosX(int nozzle) const { return nozzle == 1 ? AlignPosX_Nozzle1 : AlignPosX_Nozzle2; }

        /// <summary>
        /// <b>Góc hút</b> — toạ độ trục A đưa đầu hút về đúng tư thế ăn khớp sản phẩm nằm trong hốc tray, teach
        /// ở trang Teaching (Tuấn yêu cầu 2026-09-23). Mọi lần hạ Z xuống hút đều xoay A về đây trước.
        ///
        /// <para><b>Vì sao phải teach chứ không lấy 0.</b> Đầu hút có gờ ôm lấy sản phẩm, nên tư thế ăn khớp
        /// phụ thuộc gá đầu hút lắp lệch bao nhiêu so với gốc encoder của trục A — một con số cơ khí, không
        /// có lý do gì rơi đúng vào 0. Hút ở tư thế sai thì gờ kênh, hở khí, chân không không đạt.</para>
        ///
        /// <para>Teach RIÊNG từng tay: A1 và A2 có gốc encoder độc lập, cùng lý do với
        /// <see cref="TrayLayout::Z_PickZ1"/>. Chưa teach = 0, đúng bằng hành vi trước 23/09.</para>
        /// </summary>
        double PickAngleA1 = 0;
        double PickAngleA2 = 0;

        double PickAngle(int nozzle) const { return nozzle == 1 ? PickAngleA1 : PickAngleA2; }

        /// <summary>
        /// <b>Vị trí gantry (X, Y2) để đầu hút của đúng tay đó nằm trên con can mẫu</b> trên cơ cấu kẹp dùng
        /// cho bước xác định hand-eye (Tuấn chốt 2026-09-25).
        ///
        /// <para><b>Vì sao không dùng ô tray.</b> Cơ cấu kẹp giữ chi tiết đích đứng yên trong lúc gắp con can
        /// mẫu lên — đó chính là điều kiện để <c>TopX_mm</c> tả đúng chỗ chi tiết nằm khi con can còn ngồi
        /// trên nó, mà đúng chỗ đó mới là điều hand-eye cần biết. Cơ cấu ấy gá lên bàn Y2 ở đâu thì không
        /// trùng ô nào của lưới tray, nên vị trí gắp và vị trí soi được Jog X/Y2 rồi lưu riêng.</para>
        ///
        /// <para><b>Hai điểm gắp, không suy một từ điểm kia.</b> Hai tay lệch nhau một bước cơ khí; suy bằng
        /// hằng số là đúng cái giả định mà teach riêng đang tránh — cùng lý do
        /// <see cref="AlignPosX_Nozzle1"/> teach riêng từng tay.</para>
        ///
        /// <para><b>Không có cao độ Z riêng</b> (Tuấn chốt cùng ngày): routine hạ Z theo
        /// <see cref="AssyTray"/>, nên cơ cấu kẹp <b>phải giữ con can ở đúng cao độ như một ô tray lắp
        /// ráp</b>. Cao hơn thì đầu hút ấn xuống con can, thấp hơn thì hút hụt.</para>
        ///
        /// <para>Cơ cấu kẹp <b>tháo ra trước khi chạy Auto</b>; ba điểm này vẫn nằm trong model, gá lại là
        /// dùng được ngay.</para>
        /// </summary>
        TeachPoint HandEyePick_Nozzle1;
        TeachPoint HandEyePick_Nozzle2;

        /// <summary>Vị trí gantry (X, Y2) để CAM top-down ngắm dấu định vị của chi tiết đích trên cơ cấu kẹp —
        /// một điểm cho cả hai tay, vì camera chỉ có một.</summary>
        TeachPoint HandEyeCamPos;

        TeachPoint& HandEyePick(int nozzle) { return nozzle == 1 ? HandEyePick_Nozzle1 : HandEyePick_Nozzle2; }
        const TeachPoint& HandEyePick(int nozzle) const { return nozzle == 1 ? HandEyePick_Nozzle1 : HandEyePick_Nozzle2; }

        /// <summary>
        /// Câu nói THIẾU ĐIỂM NÀO để chạy bước xác định hand-eye với tay <paramref name="nozzle"/>; chuỗi RỖNG là đủ (null
        /// của bản C#). Trả câu chứ không trả <c>bool</c>: nút xám, hay một câu "chưa teach" chung, không nói cho người
        /// vận hành biết phải đi đâu bấm gì.
        /// </summary>
        std::string HandEyeJigProblem(int nozzle) const;

        /// <summary>
        /// Nạp bộ số giả để chạy được chuỗi Auto ở chế độ mô phỏng: lưới tray đã teach, hand-eye
        /// và dấu góc đã xác minh. Chỉ gọi khi <c>SimulationMode</c>, và có ghi log. Toàn bộ số ở
        /// đây là bịa — mục đích là kiểm chứng LOGIC chu trình trước khi có máy.
        /// </summary>
        static std::shared_ptr<ModelData> CreateSimulation();

        /// <summary>JSON dạng Newtonsoft (NullValueHandling.Ignore, property chỉ-đọc vẫn ghi).</summary>
        std::string ToJsonText() const;
        /// <summary>nullptr khi nội dung là null/rỗng; ném khi JSON hỏng. Thiếu khoá giữ mặc định. KHÔNG tự gọi
        /// NormalizeInputTrays (việc đó của McDataManager::SyncRunTrays, như bản C#).</summary>
        static std::shared_ptr<ModelData> FromJsonText(const std::string& text);
    };
}
