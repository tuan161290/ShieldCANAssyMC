using System;
using ShieldCanAssy.Core.Lang;

namespace ShieldCanAssy.Core.Data.Model
{
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
    /// </summary>
    public class VisionCalib
    {
        /// <summary>Hand-eye: offset từ tâm quang CAM top-down tới tâm mỗi đầu hút (§6.5).</summary>
        public double HandEye1Dx { get; set; }
        public double HandEye1Dy { get; set; }
        public double HandEye2Dx { get; set; }
        public double HandEye2Dy { get; set; }

        /// <summary>Thời điểm đo hand-eye — <c>MinValue</c> nghĩa là CHƯA đo, phân biệt với "đã đo
        /// và ra đúng 0", hai số double mặc định 0 không tự nói lên được điều đó.</summary>
        public DateTime HandEye1MeasuredAt { get; set; }
        public DateTime HandEye2MeasuredAt { get; set; }

        /// <summary>
        /// Mốc góc giữa hai camera của từng tay, độ: góc AlignBottom (camera dưới) <b>+</b> góc LocateTop (camera trên),
        /// đo ở bước xác định hand-eye trên cùng một con can lắp đúng, chưa xoay A. App dựng góc đích bằng
        /// <c>mốc góc − góc LocateTop</c>, xem <see cref="ShieldCanAssy.Core.Controller.VmCycle.AddPlaceGlobals"/>.
        /// Thay cho nút Create Reference — module Single Point Alignment của VM 4.4 không có nút đó.
        ///
        /// <para><b>Cộng chứ không trừ</b> vì hai camera nhìn ngược chiều nhau nên đo góc ngược dấu: CAM trên hiệu chuẩn
        /// bằng lưới 9 điểm dạy theo toạ độ máy nên bám chiều máy, CAM dưới hiệu chuẩn bằng tịnh tiến chỉ theo trục X
        /// nên không ghim được hướng trục Y và ra hệ lật chiều. Đo trên máy 2026-09-23: xoay chi tiết đích −5.02° thì
        /// góc con can đúng đổi +5.08° — độ dốc −1.01, không phải +1.</para>
        ///
        /// <para><c>null</c> = bản ghi hand-eye cũ (chưa có mốc góc, hoặc mốc góc đo bằng công thức trừ trước
        /// 2026-09-23) — cổng Auto chặn, phải chạy lại bước xác định hand-eye.</para>
        /// </summary>
        public double? HandEye1AngleRef { get; set; }
        public double? HandEye2AngleRef { get; set; }

        /// <summary>
        /// Hand-eye đo bằng công thức đã quy lệch tâm Y của camera dưới về chiều máy (Tuấn 2026-09-30, xem
        /// <c>VmCycle.BottomYSign</c>). <c>false</c> = bản ghi trước đó: <c>HandEyeDy</c> mang lệch tâm Y ngược dấu, lệch
        /// hai lần lệch tâm Y lúc đo — cổng Auto chặn, phải chạy lại bước xác định hand-eye.
        /// </summary>
        public bool HandEye1YFixed { get; set; }
        public bool HandEye2YFixed { get; set; }

        /// <summary>
        /// Pose chi tiết đích LocateTop đo ở bước 3 của lần xác định hand-eye — điểm teach hand-eye của từng tay, toạ độ máy.
        /// Nút 6 in ΔX/ΔY/Δθ của lần soi so với điểm này (Tuấn 2026-09-30). <c>null</c> = bản ghi đo trước 30/09, chưa lưu.
        /// </summary>
        public double? HandEye1TopX { get; set; }
        public double? HandEye1TopY { get; set; }
        public double? HandEye1TopA { get; set; }
        public double? HandEye2TopX { get; set; }
        public double? HandEye2TopY { get; set; }
        public double? HandEye2TopA { get; set; }

        /// <summary>
        /// Vị trí trục X/Y2 lúc chụp master (bước 3) — khung top-down trang Main trừ quãng trục đã chạy kể từ đó, để Δ là
        /// độ lệch của chi tiết TRONG ẢNH so với ảnh master (Tuấn 2026-10-01). <c>null</c> = bản ghi trước 01/10.
        /// </summary>
        public double? HandEye1TopAxX { get; set; }
        public double? HandEye1TopAxY { get; set; }
        public double? HandEye2TopAxX { get; set; }
        public double? HandEye2TopAxY { get; set; }

        public void SetHandEyeTarget(int nozzle, double x, double y, double a, double axX, double axY)
        {
            if (nozzle == 1) { HandEye1TopX = x; HandEye1TopY = y; HandEye1TopA = a; HandEye1TopAxX = axX; HandEye1TopAxY = axY; }
            else { HandEye2TopX = x; HandEye2TopY = y; HandEye2TopA = a; HandEye2TopAxX = axX; HandEye2TopAxY = axY; }
        }

        public bool TryGetHandEyeMasterAxis(int nozzle, out double axX, out double axY)
        {
            double? px = nozzle == 1 ? HandEye1TopAxX : HandEye2TopAxX;
            double? py = nozzle == 1 ? HandEye1TopAxY : HandEye2TopAxY;
            axX = px ?? 0; axY = py ?? 0;
            return px.HasValue && py.HasValue;
        }

        public bool TryGetHandEyeTarget(int nozzle, out double x, out double y, out double a)
        {
            double? px = nozzle == 1 ? HandEye1TopX : HandEye2TopX;
            double? py = nozzle == 1 ? HandEye1TopY : HandEye2TopY;
            double? pa = nozzle == 1 ? HandEye1TopA : HandEye2TopA;
            x = px ?? 0; y = py ?? 0; a = pa ?? 0;
            return px.HasValue && py.HasValue && pa.HasValue;
        }

        /// <summary>Dấu quy đổi góc VisionMaster → trục A của từng tay: +1, −1, hoặc <b>0 = chưa
        /// xác minh</b>. Flow AlignVerify_ToolN ghi vào solution, App chép sang đây.</summary>
        public int AlignAngleSign1 { get; set; }
        public int AlignAngleSign2 { get; set; }

        public DateTime AlignVerifiedAt1 { get; set; }
        public DateTime AlignVerifiedAt2 { get; set; }

        /// <summary>Vị trí trục X lúc chạy phép thử vòng xoay — phải trùng <c>AlignPosX_NozzleN</c>
        /// lúc chạy Auto (PDF tr.45).</summary>
        public double AlignVerifiedPosX1 { get; set; }
        public double AlignVerifiedPosX2 { get; set; }

        /// <summary>Sai số dư của phép thử vòng xoay gần nhất, mm — 0 nghĩa là tâm xoay và
        /// handedness của file hiệu chuẩn bottom-up nhất quán.</summary>
        public double AlignVerifyResidual1 { get; set; }
        public double AlignVerifyResidual2 { get; set; }

        /// <summary>Bước 2 (kiểm tra top-down) đạt lúc nào — <c>MinValue</c> là chưa đạt.</summary>
        public DateTime TopDownVerifiedAt { get; set; }

        public bool IsTopDownVerified { get { return TopDownVerifiedAt > DateTime.MinValue; } }

        public double HandEyeDx(int nozzle) { return nozzle == 1 ? HandEye1Dx : HandEye2Dx; }
        public double HandEyeDy(int nozzle) { return nozzle == 1 ? HandEye1Dy : HandEye2Dy; }
        public DateTime HandEyeMeasuredAt(int nozzle) { return nozzle == 1 ? HandEye1MeasuredAt : HandEye2MeasuredAt; }
        public double HandEyeAngleRef(int nozzle) { return (nozzle == 1 ? HandEye1AngleRef : HandEye2AngleRef) ?? 0; }

        public int AlignAngleSign(int nozzle) { return nozzle == 1 ? AlignAngleSign1 : AlignAngleSign2; }
        public DateTime AlignVerifiedAt(int nozzle) { return nozzle == 1 ? AlignVerifiedAt1 : AlignVerifiedAt2; }
        public double AlignVerifiedPosX(int nozzle) { return nozzle == 1 ? AlignVerifiedPosX1 : AlignVerifiedPosX2; }

        public void SetHandEye(int nozzle, double dx, double dy, double angleRef, DateTime at)
        {
            if (nozzle == 1) { HandEye1Dx = dx; HandEye1Dy = dy; HandEye1AngleRef = angleRef; HandEye1MeasuredAt = at; HandEye1YFixed = true; }
            else { HandEye2Dx = dx; HandEye2Dy = dy; HandEye2AngleRef = angleRef; HandEye2MeasuredAt = at; HandEye2YFixed = true; }
        }

        public void SetAlignVerified(int nozzle, int sign, double posX, double residualMm, DateTime at)
        {
            if (nozzle == 1)
            {
                AlignAngleSign1 = sign; AlignVerifiedPosX1 = posX;
                AlignVerifyResidual1 = residualMm; AlignVerifiedAt1 = at;
            }
            else
            {
                AlignAngleSign2 = sign; AlignVerifiedPosX2 = posX;
                AlignVerifyResidual2 = residualMm; AlignVerifiedAt2 = at;
            }
        }

        /// <summary>Đã đo hand-eye bằng công thức hiện hành. Bản ghi cũ tính là chưa đo.</summary>
        public bool IsHandEyeMeasured(int nozzle) { return HandEyeMeasuredAt(nozzle) > DateTime.MinValue && !IsHandEyeOutdated(nozzle); }

        /// <summary>Hand-eye đo bằng công thức cũ — chưa có mốc góc (trước 23/09) hoặc chưa quy lệch tâm Y của camera dưới
        /// về chiều máy (trước 30/09) — phải chạy lại bước xác định hand-eye.</summary>
        public bool IsHandEyeOutdated(int nozzle)
        {
            if (HandEyeMeasuredAt(nozzle) <= DateTime.MinValue) return false;
            bool hasAngle = (nozzle == 1 ? HandEye1AngleRef : HandEye2AngleRef).HasValue;
            bool yFixed = nozzle == 1 ? HandEye1YFixed : HandEye2YFixed;
            return !hasAngle || !yFixed;
        }
        public bool IsAlignVerified(int nozzle) { return AlignAngleSign(nozzle) != 0 && AlignVerifiedAt(nozzle) > DateTime.MinValue; }

        /// <summary>
        /// Bản ghi có đủ hand-eye và dấu góc của cả hai tay chưa. Cổng Auto KHÔNG dựa vào đây (nó dùng
        /// <c>VmCycle.EvaluateCalib</c>); chỉ còn dùng để quyết định có nạp model giả khi chạy mô phỏng.
        /// </summary>
        public bool IsUsable
        {
            get
            {
                return IsHandEyeMeasured(1) && IsHandEyeMeasured(2) &&
                       IsAlignVerified(1) && IsAlignVerified(2);
            }
        }

        /// <summary>Quá hạn tính từ lần xác minh Align cũ nhất (§6.1 quy tắc 7, alarm 4090).</summary>
        public bool IsExpired(int validDays)
        {
            if (!IsUsable) return false;
            var oldest = AlignVerifiedAt1 < AlignVerifiedAt2 ? AlignVerifiedAt1 : AlignVerifiedAt2;
            return (DateTime.Now - oldest).TotalDays > validDays;
        }
    }

    /// <summary>Dữ liệu một model sản phẩm — lưu ở Model\&lt;tên&gt;.json.</summary>
    public class ModelData
    {
        public string Name { get; set; } = "DEFAULT";
        public string Description { get; set; }
        public DateTime ModifiedAt { get; set; } = DateTime.Now;

        public TrayLayout InputTray { get; set; } = TrayLayout.CreateInputDefault();
        public TrayLayout AssyTray { get; set; } = TrayLayout.CreateAssyDefault();

        public VisionCalib Calib { get; set; } = new VisionCalib();

        /// <summary>Vị trí X để đưa từng tay hút vào giữa khung hình CAM bottom-up.</summary>
        public double AlignPosX_Nozzle1 { get; set; }
        public double AlignPosX_Nozzle2 { get; set; }

        public double AlignPosX(int nozzle) { return nozzle == 1 ? AlignPosX_Nozzle1 : AlignPosX_Nozzle2; }

        /// <summary>
        /// <b>Góc hút</b> — toạ độ trục A đưa đầu hút về đúng tư thế ăn khớp sản phẩm nằm trong hốc tray, teach
        /// ở trang Teaching (Tuấn yêu cầu 2026-09-23). Mọi lần hạ Z xuống hút đều xoay A về đây trước.
        ///
        /// <para><b>Vì sao phải teach chứ không lấy 0.</b> Đầu hút có gờ ôm lấy sản phẩm, nên tư thế ăn khớp
        /// phụ thuộc gá đầu hút lắp lệch bao nhiêu so với gốc encoder của trục A — một con số cơ khí, không
        /// có lý do gì rơi đúng vào 0. Hút ở tư thế sai thì gờ kênh, hở khí, chân không không đạt.</para>
        ///
        /// <para>Teach RIÊNG từng tay: A1 và A2 có gốc encoder độc lập, cùng lý do với
        /// <see cref="TrayLayout.Z_PickZ1"/>. Chưa teach = 0, đúng bằng hành vi trước 23/09.</para>
        /// </summary>
        public double PickAngleA1 { get; set; }
        public double PickAngleA2 { get; set; }

        public double PickAngle(int nozzle) { return nozzle == 1 ? PickAngleA1 : PickAngleA2; }

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
        public TeachPoint HandEyePick_Nozzle1 { get; set; } = new TeachPoint();
        public TeachPoint HandEyePick_Nozzle2 { get; set; } = new TeachPoint();

        /// <summary>Vị trí gantry (X, Y2) để CAM top-down ngắm dấu định vị của chi tiết đích trên cơ cấu kẹp —
        /// một điểm cho cả hai tay, vì camera chỉ có một.</summary>
        public TeachPoint HandEyeCamPos { get; set; } = new TeachPoint();

        public TeachPoint HandEyePick(int nozzle) { return nozzle == 1 ? HandEyePick_Nozzle1 : HandEyePick_Nozzle2; }

        /// <summary>
        /// Câu nói THIẾU ĐIỂM NÀO để chạy bước xác định hand-eye với tay <paramref name="nozzle"/>; null là đủ.
        /// Trả câu chứ không trả <c>bool</c>: nút xám, hay một câu "chưa teach" chung, không nói cho người vận
        /// hành biết phải đi đâu bấm gì.
        /// </summary>
        public string HandEyeJigProblem(int nozzle)
        {
            if (!HandEyePick(nozzle).Taught)
                return Loc.F("Data.JigPickNotTaught", nozzle);
            if (!HandEyeCamPos.Taught)
                return Loc.T("Data.JigCamNotTaught");
            return null;
        }

        /// <summary>
        /// Nạp bộ số giả để chạy được chuỗi Auto ở chế độ mô phỏng: lưới tray đã teach, hand-eye
        /// và dấu góc đã xác minh. Chỉ gọi khi <c>SimulationMode</c>, và có ghi log. Toàn bộ số ở
        /// đây là bịa — mục đích là kiểm chứng LOGIC chu trình trước khi có máy.
        /// </summary>
        public static ModelData CreateSimulation()
        {
            var m = new ModelData
            {
                Name = "SIM",
                Description = "Model giả lập — số liệu bịa, chỉ dùng để chạy thử chu trình"
            };

            // Mỗi Tool có lưới riêng (TrayGeometry) — Z2 cố ý lệch Z1 một lượng KHÔNG PHẢI một
            // shift X thuần tuý (−30 X, +0.4 Y) để mô phỏng vừa pitch cơ khí vừa sai lệch nhỏ
            // ngoài trục X, bắt được bài test nào lỡ giả định "Tool2 = Tool1 trừ hằng số X" thay
            // vì đọc đúng lưới riêng của Tool2 (Tuấn xác nhận 2026-09-05).
            const double pitchX = 30.0, skewY = 0.4;

            // Lưới CỐ Ý NHỎ (Tuấn yêu cầu 2026-09-23): --selftest chạy chu trình Auto trên máy mô phỏng theo
            // thời gian thật, nên mỗi ô thêm vào là thêm một lượt chạy trục + một lần kích Flow ở MỌI kịch bản
            // có quét tray. Rút 4×5 → 2×5 và 5×6 → 3×4 cắt được hơn nửa thời gian chạy mà không bỏ phép kiểm
            // nào — mọi phép đếm trong self-test đều so theo CellCount chứ không ghi cứng số ô. Bước lưới vẫn
            // khác nhau theo hàng và theo cột (15/25 và 22/26 mm) để phép nội suy 3 điểm còn ý nghĩa.
            // 5 cột — số LẺ — là cố ý: chỉ khi số cột lẻ mới có cặp ô HÚT vắt qua hai hàng (ô cột cuối
            // hàng trên ghép với ô cột đầu hàng dưới), mà đó đúng là nhánh duy nhất luật giao ô hút theo
            // hình học sinh ra để xử lý (Tuấn chốt 2026-09-24). Để 4 cột thì nhánh ấy không bao giờ chạy
            // trong self-test và bài kiểm đạt rỗng — đừng đổi về chẵn. Vẫn giữ tray nhỏ: 2×5 = 10 ô.
            m.InputTray.Rows = 2; m.InputTray.Cols = 5;
            m.InputTray.GeoZ1.P00 = new TeachPoint(150, 50);
            m.InputTray.GeoZ1.P0C = new TeachPoint(210, 50);      // 4 bước × 15 mm
            m.InputTray.GeoZ1.PR0 = new TeachPoint(150, 75);      // 1 bước × 25 mm
            m.InputTray.GeoZ2.P00 = new TeachPoint(150 - pitchX, 50 + skewY);
            m.InputTray.GeoZ2.P0C = new TeachPoint(210 - pitchX, 50 + skewY);
            m.InputTray.GeoZ2.PR0 = new TeachPoint(150 - pitchX, 75 + skewY);
            // Z dương là chiều ĐI XUỐNG, "an toàn" (Z_SAFE mặc định 50) là vị trí NHỎ (Tuấn xác
            // nhận 2026-09-04) — Z_Pick phải LỚN HƠN Z_SAFE để mô phỏng đúng "đã hạ xuống, chưa
            // an toàn", nếu không chốt chặn Z_SAFE không được bài test nào chạy qua thật sự. Z1/Z2
            // cố ý lệch nhau 1 mm — số giả, chỉ để bài test không vô tình phụ thuộc vào việc hai
            // tay hút luôn cùng cao độ (Tuấn xác nhận 2026-09-05: hai tay không nhất thiết bằng nhau).
            m.InputTray.Z_PickZ1 = 90;
            m.InputTray.Z_PickZ2 = 91;
            // Điểm giảm tốc nằm giữa cao độ an toàn (Z_SAFE mặc định 50) và cao độ hút: 5 mm cuối
            // chạy mức Auto thấp. Lệch nhau 1 mm giữa hai tay, cùng lý do với Z_Pick ở trên.
            m.InputTray.Z_SlowZ1 = 85;
            m.InputTray.Z_SlowZ2 = 86;

            m.AssyTray.Rows = 3; m.AssyTray.Cols = 4;
            // Quét zigzag: hàng chẵn chạy ngược, nên MỌI kịch bản Auto của self-test đi qua đúng nhánh mà
            // luật giao ô theo hình học sinh ra để xử lý (Tuấn chốt 2026-09-24). Để tuần tự thì nhánh ấy
            // không bao giờ chạy trong self-test — đừng đổi về.
            m.AssyTray.ScanOrder = Infor.EScanOrder.Serpentine;
            m.AssyTray.GeoZ1.P00 = new TeachPoint(350, 60);
            m.AssyTray.GeoZ1.P0C = new TeachPoint(416, 60);       // 3 bước × 22 mm
            m.AssyTray.GeoZ1.PR0 = new TeachPoint(350, 112);      // 2 bước × 26 mm
            m.AssyTray.GeoZ2.P00 = new TeachPoint(350 - pitchX, 60 + skewY);
            m.AssyTray.GeoZ2.P0C = new TeachPoint(416 - pitchX, 60 + skewY);
            m.AssyTray.GeoZ2.PR0 = new TeachPoint(350 - pitchX, 112 + skewY);
            m.AssyTray.Z_PickZ1 = 95;
            m.AssyTray.Z_PickZ2 = 96;
            m.AssyTray.Z_SlowZ1 = 90;
            m.AssyTray.Z_SlowZ2 = 91;

            // CAM bottom-up cố định ở X = 300 (toạ độ của đầu hút khi nằm giữa khung hình), teach
            // riêng từng Tool nên không suy ra từ pitch nữa.
            m.AlignPosX_Nozzle1 = 300;
            m.AlignPosX_Nozzle2 = 300 - pitchX;
            m.PickAngleA1 = 3.5;
            m.PickAngleA2 = 356.5;

            // Ba điểm dạy của cơ cấu kẹp (bước xác định hand-eye). Số CỐ Ý không trùng bất kỳ ô tray, mốc CAM
            // top-down hay vị trí soi CAM dưới nào ở trên: nếu trùng thì một routine vẫn đọc lưới tray cũng
            // làm phép kiểm đạt rỗng. Và hai điểm gắp cố ý lệch nhau 35 mm chứ KHÔNG phải pitchX, để không
            // bài test nào suy được tay #2 từ tay #1 bằng một hằng số.
            m.HandEyePick_Nozzle1 = new TeachPoint(240, 100);
            m.HandEyePick_Nozzle2 = new TeachPoint(205, 100.7);
            m.HandEyeCamPos = new TeachPoint(282, 118);

            m.Calib.SetHandEye(1, -40, 5, 0, DateTime.Now);
            // Tay #2 lắp cách tay #1 đúng pitchX theo X (lưới Z2 = Z1 − pitchX, + skewY): muốn đích tính từ
            // camera rơi đúng ô của lưới Z2 thì hand-eye tay #2 phải là (−40 − pitchX, 5 + skewY). Bản cũ để
            // −10 thì đích tay #2 lệch lưới 60 mm, mà Auto chặn đích lệch lưới teach (thiết kế R27).
            m.Calib.SetHandEye(2, -40 - pitchX, 5 + skewY, 0, DateTime.Now);

            // Dấu góc +1 = mô phỏng coi góc VisionMaster cùng chiều trục A. Trên máy thật con số
            // này PHẢI đo bằng phép thử vòng xoay, không được chép từ đây.
            m.Calib.SetAlignVerified(1, +1, m.AlignPosX_Nozzle1, 0, DateTime.Now);
            m.Calib.SetAlignVerified(2, +1, m.AlignPosX_Nozzle2, 0, DateTime.Now);

            // Mốc CAM top-down = vị trí camera khi soi Ô (1,1) của Assy Tray — giữ tương đương
            // công thức cũ (cellPos(0,0) trừ Hand-Eye tay #1) để hành vi mô phỏng không đổi. Ô khác
            // suy ra theo lưới TrayLayout.CamGridZAxis; hai lưới giả lập ở trên cùng bước ô nên đổi
            // lưới đó không làm số liệu mô phỏng đổi theo.
            m.AssyTray.CamTopDownAnchorX = m.AssyTray.GeoZ1.P00.X - m.Calib.HandEye1Dx;
            m.AssyTray.CamTopDownAnchorY = m.AssyTray.GeoZ1.P00.Y - m.Calib.HandEye1Dy;
            m.AssyTray.CamTopDownAnchorTaught = true;

            return m;
        }
    }
}
