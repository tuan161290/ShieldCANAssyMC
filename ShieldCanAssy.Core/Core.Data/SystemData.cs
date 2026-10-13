using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using ShieldCanAssy.Core.Communication;
using ShieldCanAssy.Core.Infor;
using Newtonsoft.Json;

namespace ShieldCanAssy.Core.Data
{
    /// <summary>
    /// Tham số máy — lưu ở Config\system.json. Chỉ chứa thứ gắn với PHẦN CỨNG và cách vận hành,
    /// không chứa dữ liệu sản phẩm (thứ đó nằm trong ModelData).
    /// </summary>
    public class SystemData
    {
        public string MachineName { get; set; } = "Shield CAN Assy MC";
        public string MachineCode { get; set; } = "SO1C86-SCAM";

        /// <summary>
        /// Chạy không cần phần cứng: mọi thiết bị được thay bằng bản mô phỏng. Trạng thái này
        /// LUÔN hiện trên status bar — không được để ai nhầm bản mô phỏng với máy thật.
        /// </summary>
        public bool SimulationMode { get; set; } = true;

        /// <summary>Ngôn ngữ giao diện: "vi" hoặc "en" (Tuấn 2026-10-09) — đổi bằng nút VI/EN trên thanh tiêu đề,
        /// xem <c>Core.Lang.Loc</c>.</summary>
        public string Language { get; set; } = "vi";

        // ── An toàn ──
        /// <summary>Cao độ an toàn của Z (mm). Mỗi lần nâng lên (chạy đi lại, soi CAM bottom-up) Z dừng đúng tại đây;
        /// X/Y1/Y2 chỉ chạy khi cả Z1 và Z2 ≤ giá trị này + 1 mm (<c>MotionConnect.ZSafeMargin</c>). Z dương là chiều đi xuống.</summary>
        public double ZSafe { get; set; } = 50.0;

        /// <summary>
        /// <b>Đã chốt (§11 #14): máy KHÔNG dùng encoder tuyệt đối</b>, về gốc bằng cảm biến DOG.
        /// Encoder tương đối mất vị trí khi cắt nguồn driver, nên đây là nguồn sự thật cho việc
        /// có được phép bỏ qua bước về gốc hay không — không phải một tuỳ chọn để ai đó tắt.
        /// </summary>
        public bool HasAbsoluteEncoder { get; set; } = false;

        private bool _requireHomeAfterEmg = true;

        /// <summary>
        /// Bắt buộc về gốc lại sau EMG (PLAN.md §3.7).
        ///
        /// <para>Getter cố tình <b>không</b> trả về nguyên giá trị đã đặt: không có encoder tuyệt
        /// đối thì luôn là true, đặt cách nào cũng vậy. Vị trí sau một lần cắt STO là không xác
        /// định — trục Z thẳng đứng còn có thể tụt xuống trước khi phanh bắt — nên cho phép bỏ
        /// qua Origin ở đây là mở đường cho một lần lao đầu hút xuống tray. Ràng buộc này thuộc
        /// về phần cứng, phải được thực thi bằng code chứ không bằng một dòng ghi chú trong
        /// file cấu hình.</para>
        /// </summary>
        public bool RequireHomeAfterEmg
        {
            get { return _requireHomeAfterEmg || !HasAbsoluteEncoder; }
            set { _requireHomeAfterEmg = value; }
        }

        /// <summary>
        /// Có rèm sáng không (PLAN.md §11 #13, #18).
        ///
        /// <para><b>Tạm để <c>false</c> — coi như CHƯA có rèm sáng.</b> Phần cứng đã chốt là có
        /// 1 rèm đọc qua Ezi-IO, nhưng chu trình hoạt động khi rèm tác động (rèm bảo vệ đúng vị
        /// trí nào, tray ra/vào có tự động cắt qua nó không, có cần muting hay không — §11 #18)
        /// chưa quyết định. Bật cờ này lên đúng lúc đã trả lời xong #18; <c>SafetyMonitor</c> đã
        /// viết sẵn để tôn trọng cờ này ngay, không cần sửa gì thêm.</para>
        ///
        /// <para>2026-10-01 Tuấn chốt cách xử lý: rèm bị cắt thì dừng mọi trục, Auto tạm dừng như STOP, rèm thông rồi bấm
        /// Start 1/Start 2 chạy tiếp — không alarm, không RESET. Bật/tắt ở trang Setting.</para>
        /// </summary>
        public bool HasLightCurtain { get; set; } = false;

        /// <summary>
        /// Có cảm biến áp suất khí không (DI <see cref="IoMap.AirOk"/>).
        ///
        /// <para><b>Để <c>false</c> — máy này KHÔNG lắp cảm biến áp suất khí</b> (Tuấn xác nhận,
        /// 2026-09-08). DI07 bỏ trống, không đấu — nếu vẫn bắt buộc kiểm tra thì <c>SafeToMove</c>
        /// bị chặn vĩnh viễn vì chân đó không bao giờ lên mức. <see cref="SafetyMonitor"/> đã viết
        /// sẵn để tôn trọng cờ này ngay (giống hệt <see cref="HasLightCurtain"/>) — tắt cờ thì
        /// DI07 không còn được đọc để chặn chuyển động hay phát alarm 1103 (AirPressureLow) nữa.
        /// Bật lại nếu sau này lắp cảm biến thật.</para>
        /// </summary>
        public bool HasAirSensor { get; set; } = false;

        /// <summary>
        /// Về gốc A1, A2 tuần tự thay vì song song (PLAN.md §4.3).
        ///
        /// <para><b>Đã chốt (§11 #10): A1/A2 có cảm biến gốc và xoay tự do</b> nên để false —
        /// về gốc song song. Chỉ bật lại nếu sau này gá đầu hút to ra tới mức đường kính bao khi
        /// xoay vượt pitch hai tay hút.</para>
        /// </summary>
        public bool HomeRotateSequential { get; set; } = false;

        /// <summary>Chu kỳ quét của SafetyMonitor và vòng điều khiển, ms.</summary>
        public int ControlTickMs { get; set; } = 20;

        /// <summary>Watchdog: vòng điều khiển im lặng quá lâu thì phát alarm 1002.</summary>
        public int WatchdogTimeoutMs { get; set; } = 500;

        /// <summary>
        /// Ân hạn watchdog ngay sau khi khởi động app, ms (PLAN.md §3.3).
        ///
        /// <para>Chu kỳ đầu tiên có thể im lặng lâu hơn <see cref="WatchdogTimeoutMs"/> bình
        /// thường vì nạp solution Vision chạy nền cùng lúc chiếm CPU (Tuấn phản ánh, 2026-09-09:
        /// alarm 1002 Fatal nổi ngay lúc mở app dù vòng quét không hề treo thật). Trong cửa sổ
        /// này, <c>McControl.CheckWatchdog</c> chỉ bỏ qua việc RAISE alarm — không tắt hẳn giám
        /// sát, một treo THẬT lúc khởi động vẫn bị bắt ngay sau khi hết ân hạn.</para>
        /// </summary>
        public int WatchdogStartupGraceMs { get; set; } = 5000;

        // ── Chu trình ──

        /// <summary>
        /// Tay hút được dùng trong Auto (Tuấn 2026-09-29): 0 = cả hai, 1 = chỉ tay #1, 2 = chỉ tay #2 — chọn ở trang Main.
        /// Tay tắt không hút nữa; hàng đang cầm sẵn trên nó vẫn soi và đặt nốt. Chạy rà không theo lựa chọn này.
        /// </summary>
        public int OnlyNozzle { get; set; }

        public bool NozzleEnabled(int nozzle) => (OnlyNozzle != 1 && OnlyNozzle != 2) || OnlyNozzle == nozzle;

        public int PickRetryCount { get; set; } = 2;
        public double PickRetryDeltaZ { get; set; } = 0.3;
        public int MaxConsecutiveNg { get; set; } = 5;

        /// <summary>
        /// Quét tray lắp ráp <b>từng hàng</b>: S05 chỉ soi các ô còn trống của HÀNG đang làm, hút và đặt hết hàng
        /// đó rồi mới soi hàng kế. Mặc định <c>false</c> = soi cả tray một lượt trước khi hút (hành vi từ 13/09).
        ///
        /// <para>Lý do bật (Tuấn nêu 2026-09-23): chạy trục Y2 hết chiều dài tray có thể làm xê dịch chính tray
        /// lắp ráp, mà số đo của lần quét trước thì đã cất từ đầu ca. Quét từng hàng rút khoảng cách giữa lúc đo
        /// một ô và lúc đặt vào ô đó xuống còn một hàng, nên tray có xê dịch cũng ít ảnh hưởng.</para>
        ///
        /// <para>Ô mà tay hút được giao nhưng chưa có số đo — gặp khi hai tay cùng cầm hàng và ô thứ hai rơi sang
        /// hàng kế — vẫn được soi riêng ngay trước lúc dùng, nên không có thế kẹt nào.</para>
        /// </summary>
        public bool ScanAssyByRow { get; set; } = false;

        /// <summary>
        /// Thời gian chờ cảm biến chân không đổi trạng thái sau khi đóng/mở van. Hết thời gian
        /// mà chưa đổi là hút hụt (lúc hút) hoặc sản phẩm dính đầu hút (lúc nhả).
        /// </summary>
        public int VacuumTimeoutMs { get; set; } = 800;

        /// <summary>
        /// Chờ thêm sau khi cảm biến chân không đã báo hút được, TRƯỚC khi nâng Z. Cảm biến lên ở
        /// ngưỡng đặt trên đồng hồ, lúc đó lực hút vẫn còn đang lên — nâng ngay thì con can dễ xê
        /// dịch hoặc rơi (Tuấn nêu 2026-09-23). 0 = nâng ngay, giữ đúng hành vi cũ.
        /// </summary>
        public int VacuumSettleMs { get; set; } = 0;

        /// <summary>Thời gian thổi để dứt sản phẩm khỏi đầu hút trước khi nâng Z.</summary>
        public int BlowTimeMs { get; set; } = 120;

        // ── Z qua điểm giảm tốc (Tuấn 2026-10-07) ──
        // Auto và chạy rà: chặng bật thì Z chạy mức Auto thấp đoạn giữa điểm giảm tốc và cao độ hút/đặt, tắt thì chạy một
        // mạch mức Auto cao. Mặc định giữ nhịp 30/09: tray đầu vào hạ một mạch, nâng chậm; tray lắp ráp chậm cả hai chiều.
        // Trả hàng soi hỏng về tray đầu vào, hiệu chuẩn và trang Teaching không theo các ô này — luôn qua điểm giảm tốc.
        public bool InputZSlowDown { get; set; } = false;
        public bool InputZSlowUp { get; set; } = true;
        public bool AssyZSlowDown { get; set; } = true;
        public bool AssyZSlowUp { get; set; } = true;

        /// <summary>
        /// Ngưỡng góc bù (độ) và độ lệch tâm của sản phẩm trên đầu hút (mm). Flow <c>AlignBottom_ToolN</c> tính lệnh đặt, App so
        /// hai số chẩn đoán của nó với ngưỡng này: vượt thì trả sản phẩm về ô nguồn, tính NG (alarm 5004).
        /// </summary>
        public double MaxCompensationAngleDeg { get; set; } = 15.0;
        public double MaxCompensationDistance { get; set; } = 3.0;

        // ── Vision: App chấp hành, VisionMaster tính bù (thiết kế 2026-09-13) ──

        /// <summary>Chặn đích theo lưới teach của đúng tay đặt, mm (D13 — chờ chốt, mặc định 1.5).</summary>
        public double PlaceTargetGuardMm { get; set; } = 1.5;

        /// <summary>Trục A được lệch khỏi góc lúc chụp bottom-up bao nhiêu độ trước khi phải soi lại (D13).</summary>
        public double ACapTolDeg { get; set; } = 0.3;

        /// <summary>Số ô liên tiếp bị bỏ vì lệch lưới trước khi dừng máy (D13).</summary>
        public int MaxGuardSkipStreak { get; set; } = 2;

        /// <summary>Vị trí soi đã teach (<c>AlignPosX_NozzleN</c>) được lệch khỏi vị trí đã xác minh ở Bước 4 bao nhiêu mm
        /// trước khi cổng Auto chặn.</summary>
        public double AlignXTolMm { get; set; } = 0.05;

        // ── Thiết bị ──
        /// <summary>
        /// Ezi-IO là HAI module riêng biệt, mỗi cái một IP: 32Di và 32Do. Gộp thành một địa chỉ
        /// là sai ngay từ cấu hình.
        /// </summary>
        public string IoDiIpAddress { get; set; } = "192.168.0.10";
        public string IoDoIpAddress { get; set; } = "192.168.0.11";

        /// <summary>
        /// Đường dẫn file .solw nạp vào <c>VmSolution</c> lúc khởi động (<see cref="McControl.Init"/>)
        /// — CẤU HÌNH ĐƯỢC ở trang Setting. Chỉ MỘT solution: VmSolution là singleton của SDK
        /// VisionMaster nên mọi Flow (AlignBottom, LocateTop, CalibBottomUp — xem VisionOutputs
        /// bên dưới) đều sống trong cùng một file .solw, không phải mỗi Flow một file riêng.
        /// </summary>
        public string VisionSolutionPath { get; set; } = "AlignBottom.solw";

        public int VisionTriggerTimeoutMs { get; set; } = 3000;

        /// <summary>
        /// Chờ ngần này sau khi một lệnh chạy kết thúc rồi mới ĐỌC ENCODER và kích Flow có camera
        /// (Tuấn yêu cầu 2026-09-19). Driver báo "đã vào vị trí" theo cửa sổ INP của nó, nhưng cơ
        /// khí còn rung thêm một lúc; chụp ngay lúc đó thì ảnh và con số encoder tả hai vị trí
        /// khác nhau, và sai lệch ấy đi thẳng vào phép hiệu chuẩn.
        ///
        /// <para>Chỉ tính TỪ LÚC KẾT THÚC LỆNH CHẠY: trục đã đứng yên lâu hơn ngần này thì không
        /// chờ thêm giây nào — ảnh thứ hai trở đi trong một loạt chụp, hay nút Trigger bấm tay,
        /// đều không mất gì. Đặt 0 để tắt hẳn.</para>
        ///
        /// <para>Mỗi camera một số (Tuấn 2026-10-07): CAM bottom-up soi đầu hút treo trên Z, CAM top-down đi theo X/Y2
        /// — rung khác nhau. Khoá JSON của CAM bottom-up giữ tên cũ "VisionSettleMs" để số đang dùng trên máy không mất.</para>
        /// </summary>
        [JsonProperty("VisionSettleMs")]
        public int BottomSettleMs { get; set; } = 200;

        /// <summary>Chờ ổn định trước khi chụp của CAM top-down — xem <see cref="BottomSettleMs"/>.</summary>
        public int TopSettleMs { get; set; } = 200;

        /// <summary>
        /// Chờ đèn CAM sáng đủ sau khi App bật đèn, TRƯỚC khi kích Flow (Tuấn yêu cầu 2026-09-23).
        /// Đèn lên gần như tức thì nhưng độ sáng còn tăng thêm một lúc; chụp ngay thì ảnh đầu tối
        /// hơn ảnh sau, mà độ sáng lại đổi cả ngưỡng tìm cạnh lẫn tâm chi tiết đo được.
        ///
        /// <para>Trả đúng ngần này mỗi lần chụp, kể cả khi đèn vừa tắt xong — App tắt đèn sau mỗi
        /// Flow nên lần nào cũng là lần bật nguội. Đặt 0 để tắt hẳn.</para>
        ///
        /// <para>Mỗi camera một số (Tuấn 2026-10-07) — đèn CAM1 và CAM2 là hai đèn khác nhau. Khoá JSON của CAM
        /// bottom-up giữ tên cũ "LampSettleMs".</para>
        /// </summary>
        [JsonProperty("LampSettleMs")]
        public int BottomLampMs { get; set; } = 100;

        /// <summary>Chờ đèn sáng của CAM top-down (đèn CAM2) — xem <see cref="BottomLampMs"/>.</summary>
        public int TopLampMs { get; set; } = 100;

        /// <summary>Chờ ổn định trước khi chụp của camera <paramref name="cam"/>; Flow chỉ tính thì 0.</summary>
        public int SettleMsFor(EVmCamera cam) =>
            cam == EVmCamera.TopDown ? TopSettleMs : cam == EVmCamera.BottomUp ? BottomSettleMs : 0;

        /// <summary>Chờ đèn sáng của camera <paramref name="cam"/>; Flow chỉ tính thì 0.</summary>
        public int LampMsFor(EVmCamera cam) =>
            cam == EVmCamera.TopDown ? TopLampMs : cam == EVmCamera.BottomUp ? BottomLampMs : 0;

        /// <summary>
        /// Tên Flow và tên biến output trong solution VisionMaster — CẤU HÌNH ĐƯỢC ở trang Setting,
        /// không hardcode trong code (PLAN.md §6.8). Tên thật chỉ có sau khi Tuấn dựng xong file
        /// .solw; sửa ở đây rồi bấm Lưu là VisionConnect dùng ngay, không cần sửa code hay build lại
        /// — cùng kiểu màn hình nhập tên Output đã dùng ở app HousingCorrosion.
        /// </summary>
        public VisionOutputNames VisionOutputs { get; set; } = new VisionOutputNames();

        /// <summary>Số ngày hiệu lực của hiệu chuẩn camera trước khi cảnh báo 4090.</summary>
        public int CalibValidDays { get; set; } = 90;

        // ── Hiệu chuẩn vision — PLAN.md §6 ──

        /// <summary>Bước lưới 9 điểm CAM top-down, mm — PLAN.md §6.1 quy tắc 1: phủ 70–80 % FOV,
        /// không chạm mép. Chỉnh theo FOV thật của camera khi lắp máy.</summary>
        public double TopDownCalibStepXMm { get; set; } = 10.0;
        public double TopDownCalibStepYMm { get; set; } = 10.0;

        /// <summary>"Calibration Center Point" — toạ độ trục (X, Y2) ứng với điểm giữa khung hình
        /// camera top-down (điểm 5 trong lưới numpad), lưu bằng nút Save (trang Vision Edit) sau
        /// khi jog máy tới đó qua trang Manual. Lưới 9 điểm/Auto Calib tính offset từ đây — tách
        /// lưu và dùng thành 2 bước để không phải jog lại tâm mỗi lần mở app hay bấm từng điểm.
        /// <see cref="TopDownCalibCenterSet"/> phân biệt "chưa từng Save" với "đã Save đúng tại
        /// (0,0)" — hai giá trị double mặc định 0 không tự nói lên được điều đó.</summary>
        public double TopDownCalibCenterX { get; set; }
        public double TopDownCalibCenterY { get; set; }
        public bool TopDownCalibCenterSet { get; set; }

        /// <summary>Dung sai, mm: Bước 2 (App so vị trí dấu chuẩn) và Bước 4 (App gửi cho Flow AlignVerify_ToolN qua input
        /// <c>VerifyTolMm</c>) — §6.2b, §6.4b.</summary>
        public double CalibMaxResidualThresholdMm { get; set; } = 0.05;

        /// <summary>Mỗi điểm chụp lại bấy nhiêu lần, lấy trung bình — PLAN.md §6.1 quy tắc 4.</summary>
        public int CalibShotsPerPoint { get; set; } = 5;

        /// <summary>Quãng đường chạy thử khi kiểm tra hiệu chuẩn CAM top-down (§6.2b): chạy X rồi
        /// Y2 đúng bấy nhiêu mm, toạ độ dấu chuẩn Flow LocateTop tính ra (<c>TopX/TopY</c>) phải đứng yên.
        /// Đủ lớn để vượt nhiễu đo, đủ nhỏ để dấu chuẩn không ra khỏi khung hình.</summary>
        public double TopDownVerifyStepMm { get; set; } = 5.0;

        /// <summary>Góc xoay thử của phép thử vòng xoay (§6.4b) — xoay trục A đúng bấy nhiêu độ
        /// rồi soi lại, dùng để đo dấu góc và kiểm tâm xoay trong file hiệu chuẩn bottom-up.</summary>
        public double AlignVerifyAngleDeg { get; set; } = 10.0;

        /// <summary>Sai lệch góc cho phép của phép thử vòng xoay, độ — App gửi cho Flow AlignVerify_ToolN qua input
        /// <c>VerifyAngleTolDeg</c>.</summary>
        public double AlignVerifyAngleTolDeg { get; set; } = 1.5;

        /// <summary>Độ lệch tâm tối thiểu của sản phẩm mẫu ở Bước 4, mm — dưới mức này không kết luận được tâm xoay
        /// (input <c>MinEccMm</c>).</summary>
        public double AlignVerifyMinEccMm { get; set; } = 0.5;

        /// <summary>Góc xoay thử tối thiểu của Bước 4, độ (input <c>MinVerifyTurnDeg</c>).</summary>
        public double AlignVerifyMinTurnDeg { get; set; } = 1.0;

        /// <summary>
        /// Chuỗi hiệu chuẩn Align trong VisionMaster (§6.4): App chạy tay hút qua các vị trí X
        /// lệch so với <c>AlignPosX_NozzleN</c> (phần tịnh tiến) rồi xoay qua các góc (phần xoay),
        /// mỗi bước kích Flow AlignBottom_ToolN (nhánh IsTriggerLocate = 0) một lần để module hiệu chuẩn nạp thêm một điểm.
        ///
        /// <para>Để dạng danh sách vì mỗi phương án hiệu chuẩn của VisionMaster đòi một dãy điểm
        /// khác nhau — Translation Calibration "tịnh tiến + xoay" cần cả hai phần, còn Rotation
        /// Calibration (đi kèm file CalibBoard) chỉ cần phần xoay: để trống ô X là bỏ hẳn phần
        /// tịnh tiến, không phải sửa code.</para>
        ///
        /// <para><b>Số 0 phải nằm CUỐI danh sách.</b> Help của Translation Calibration ghi ở mục
        /// Rotation Times: <i>"the rotation is performed at the position of the 5th point"</i> —
        /// module giả định phần xoay được chụp tại đúng vị trí của điểm tịnh tiến cuối cùng, mà
        /// <see cref="Core.Controller.Routine_Calib"/> thì luôn chạy X về <c>AlignPosX</c> (tức
        /// offset 0) trước khi xoay. Để 0 ở giữa dãy là phần xoay bị gán nhầm vào một vị trí X
        /// cách đó vài mm, hình học mâu thuẫn, và VisionMaster báo
        /// <c>10103002 Matrix calculating error</c> lúc bấm Calibrate. Mặc định cũ
        /// "-4, -2, 0, 2, 4" mắc đúng lỗi này (Tuấn gặp 2026-09-12).</para>
        /// </summary>
        public string AlignCalibXOffsetsMm { get; set; } = "-4, -2, 2, 4, 0";

        /// <summary>Các góc tuyệt đối của trục A trong chuỗi hiệu chuẩn Align, độ. VisionMaster
        /// đòi các bước xoay BẰNG NHAU (Help Rotation Calibration).</summary>
        public string AlignCalibAnglesDeg { get; set; } = "0, 60, 120, 180, 240, 300";

        public List<double> AlignCalibXOffsets() { return ParseNumberList(AlignCalibXOffsetsMm); }
        public List<double> AlignCalibAngles() { return ParseNumberList(AlignCalibAnglesDeg); }

        /// <summary>
        /// Tách một danh sách số gõ tay thành mảng. Bỏ qua phần tử không đọc được thay vì ném lỗi
        /// — ô nhập nằm trên trang Vision Edit, gõ thừa dấu phẩy không phải lý do để cả chuỗi hiệu
        /// chuẩn đứng lại. Nhận cả dấu trừ Unicode "−" vì chuỗi mặc định và tài liệu đều dùng nó.
        /// </summary>
        public static List<double> ParseNumberList(string text)
        {
            var list = new List<double>();
            if (string.IsNullOrWhiteSpace(text)) return list;

            foreach (string raw in text.Replace('−', '-').Split(new[] { ',', ';', ' ', '\t' },
                                                                     StringSplitOptions.RemoveEmptyEntries))
            {
                double v;
                if (double.TryParse(raw, NumberStyles.Float, CultureInfo.InvariantCulture, out v))
                    list.Add(v);
            }
            return list;
        }

        // ── Trục ──
        public List<AxisConfig> Axes { get; set; } = new List<AxisConfig>();

        public AxisConfig GetAxis(string name)
        {
            var cfg = Axes.FirstOrDefault(a => a.Name == name);
            if (cfg == null)
            {
                cfg = DefaultAxis(name);
                Axes.Add(cfg);
            }
            return cfg;
        }

        // ── Hằng số truyền động của trục quay A1/A2 ──

        /// <summary>
        /// Xung trên một vòng ĐỘNG CƠ (phân giải đặt trong driver Ezi-SERVO).
        /// Tuấn đổi động cơ 2026-09-12: 10000 → <b>18000</b> xung/vòng động cơ.
        /// </summary>
        public const double RotaryMotorPulsePerRev = 18000.0;

        /// <summary>
        /// Số vòng động cơ cho một vòng đầu hút — hộp số 1/2 nên là 2.
        /// Đổi hộp số thì sửa đúng con số này, mọi thứ khác tự theo.
        /// </summary>
        public const double RotaryMotorRevPerOutputRev = 2.0;

        /// <summary>Độ quay của ĐẦU HÚT trên một vòng động cơ — 180°. Chỉ dùng đổi tốc độ sang rpm.</summary>
        public const double RotaryDegPerMotorRev = 360.0 / RotaryMotorRevPerOutputRev;

        /// <summary>Xung trên một độ của đầu hút: 18000 × 2 / 360 = 100 chẵn.</summary>
        public const double RotaryPulsePerDeg =
            RotaryMotorPulsePerRev * RotaryMotorRevPerOutputRev / 360.0;

        /// <summary>
        /// Sửa lại cấu hình trục quay đọc từ <c>system.json</c> viết trước 2026-09-12 — cùng cơ
        /// chế tự chữa như việc sắp lại thứ tự trục trong <c>McDataManager.LoadSystemData</c>,
        /// để không phải xoá file thủ công trên máy.
        ///
        /// <para>Nhận ra file cũ bằng HAI dấu hiệu trên một trục Fastech đơn vị "deg":
        /// <see cref="AxisConfig.WrapDegrees"/> còn tắt (file viết trước 2026-09-12), HOẶC
        /// <see cref="AxisConfig.PulsePerUnit"/> lệch <see cref="RotaryPulsePerDeg"/> hiện hành.
        /// Dấu hiệu thứ hai là bắt buộc: chỉ xét cờ quấn thì file đã migrate một lần rồi sẽ
        /// không bao giờ được sửa nữa, nên mỗi lần đổi động cơ/hộp số là số cũ nằm lại trong
        /// <c>system.json</c> và ghi đè hằng số mới ngay lúc nạp — trục xoay sai tỉ lệ mà không
        /// báo lỗi gì. So sánh thẳng với hằng số nên cách này đúng cho mọi lần đổi sau này.</para>
        ///
        /// <para>Mọi thứ bị ghi đè ở đây đều là <b>hằng số theo thiết kế</b>, không phải số đo
        /// được trên máy, nên ghi đè là đúng (xem ghi chú của
        /// <see cref="AxisConfig.PulsePerUnit"/>). <see cref="AxisConfig.HomeOffset"/> thì GIỮ
        /// NGUYÊN — đó mới là số đo thật.</para>
        ///
        /// <para><b>Cả <see cref="AxisConfig.BoardId"/> nữa</b> (thêm 2026-09-14): <c>iBdID</c> là ô của thư
        /// viện Fastech, do bảng phân bổ trong mã quyết định (<c>FastechBoardId</c>), không phải thứ để file
        /// cấu hình đặt lại. Hai module trùng <c>iBdID</c> thì <c>FAS_Connect</c> của cái sau ĐÈ lên cái trước
        /// — đầu <c>Connect()</c> của <c>FastechAxis</c> gọi <c>FAS_Close(bdID)</c>, đóng luôn phiên của module
        /// kia — và triệu chứng là trục này đọc ra trạng thái của trục kia, hoặc mất kết nối không rõ lý do.
        /// File lưu từ bản cũ không có trường này nên A2 đọc ra 0, trùng đúng ô của A1.</para>
        /// </summary>
        public IList<string> MigrateRotaryAxes()
        {
            var changed = new List<string>();

            foreach (var cfg in Axes)
            {
                if (cfg == null) continue;
                if (cfg.Kind != EAxisKind.Fastech) continue;
                if (!string.Equals(cfg.Unit, "deg", StringComparison.OrdinalIgnoreCase)) continue;

                // iBdID trước — nó độc lập với tỉ lệ góc, file có thể đúng cái này mà sai cái kia.
                int wantBoard = cfg.Name == AxisId.A1 ? Lib.Fastech.FastechBoardId.AxisA1
                              : cfg.Name == AxisId.A2 ? Lib.Fastech.FastechBoardId.AxisA2
                              : cfg.BoardId;
                if (cfg.BoardId != wantBoard)
                {
                    cfg.BoardId = wantBoard;
                    changed.Add(cfg.Name);
                }

                bool stale = !cfg.WrapDegrees
                          || Math.Abs(cfg.PulsePerUnit - RotaryPulsePerDeg) > 1e-6
                          || Math.Abs(cfg.UnitPerRev - RotaryDegPerMotorRev) > 1e-6;
                if (!stale) continue;

                cfg.WrapDegrees = true;
                cfg.PulsePerUnit = RotaryPulsePerDeg;
                cfg.UnitPerRev = RotaryDegPerMotorRev;
                cfg.SoftLimitNeg = 0;
                cfg.SoftLimitPos = 360;
                cfg.HomeOffset = AxisAngle.Wrap(cfg.HomeOffset);
                cfg.StandbyPos = AxisAngle.Wrap(cfg.StandbyPos);

                if (!changed.Contains(cfg.Name)) changed.Add(cfg.Name);
            }

            return changed;
        }

        public static SystemData CreateDefault()
        {
            var sys = new SystemData();
            foreach (string name in AxisId.All)
                sys.Axes.Add(DefaultAxis(name));
            return sys;
        }

        /// <summary>
        /// Giá trị mặc định để app chạy được ngay lần đầu. Phần lớn số ở đây (tốc độ, soft
        /// limit, vị trí chờ, gốc toạ độ/điểm 0, dấu <c>HomeDir</c> của X/Y1/Y2/A1/A2) là PHỎNG
        /// ĐOÁN, phải xác định lại trên máy thật ở P1 (PLAN.md §11 #6) — phụ thuộc vị trí lắp
        /// cảm biến gốc thật. Ba thứ đã chốt sẵn, không phải xác định lại:
        /// <list type="bullet">
        /// <item><b>Chiều dương từng trục</b> (Tuấn chốt 2026-09-07, PLAN.md §11 #6) — xem ghi
        /// chú tại từng case bên dưới (X, Y1/Y2, A1/A2) và Z1/Z2.</item>
        /// <item><c>HomeDir</c> của Z1/Z2 là -1 (về gốc đi lên, nhưng quy ước "xuống = dương" nên
        /// đi lên là chiều âm — xem ghi chú tại case Z1/Z2 bên dưới).</item>
        /// <item><c>PulsePerUnit</c> là hằng số theo thiết kế (1000 xung/mm Panasonic, 10000
        /// xung/360° Fastech) — cấu hình driver khớp nó, không đo nó.</item>
        /// </list>
        /// </summary>
        private static AxisConfig DefaultAxis(string name)
        {
            var cfg = new AxisConfig { Name = name };

            switch (name)
            {
                case AxisId.X:
                    // ĐÃ CHỐT (Tuấn, 2026-09-07, PLAN.md §11 #6): nhìn từ phía người thao tác,
                    // chiều dương của X là TRÁI → PHẢI. HomeDir = -1 dưới đây vẫn là PHỎNG ĐOÁN —
                    // dấu thật phụ thuộc cảm biến gốc lắp ở đầu trái hay đầu phải, xác định ở P1.
                    cfg.Kind = EAxisKind.Panasonic; cfg.ComPort = "COM3"; cfg.UnitId = 1;
                    cfg.Unit = "mm"; cfg.SoftLimitNeg = -1; cfg.SoftLimitPos = 600;
                    cfg.JogSpeedSlow = 30; cfg.JogSpeedFast = 60; cfg.MoveSpeed = 300; cfg.HomeDir = -1; cfg.StandbyPos = 100;
                    cfg.PulsePerUnit = 1000.0;         // ĐÃ CHỐT: 1000 xung/mm, cấu hình trong driver
                    // ĐÃ CHỐT (Tuấn xác nhận qua PANATERM, 2026-09-09): trục X dùng động cơ tuyến
                    // tính Alpharobot ALR160-S1 + driver Panasonic A6L — KHÔNG có "vòng quay" vật
                    // lý như trục vít me. Driver quy đổi tốc độ rpm dựa trên bước cực từ (magnetic
                    // pole pitch), đọc trực tiếp từ Pr9.02 trên driver = 27.00mm — PHẢI dùng đúng
                    // số này cho UnitPerRev, không phải mặc định 10.0 (vốn là bước vít me của các
                    // trục Y/Z). Nếu để sai, AxisScale.ToRpm quy đổi lệch, khiến tốc độ thực trên
                    // trục X khác hẳn giá trị mm/s cấu hình (không ảnh hưởng độ chính xác vị trí,
                    // vì đó là việc của PulsePerUnit — độc lập với UnitPerRev).
                    cfg.UnitPerRev = 27.0;
                    break;

                case AxisId.Z1:
                case AxisId.Z2:
                    cfg.Kind = EAxisKind.Panasonic;
                    cfg.ComPort = name == AxisId.Z1 ? "COM4" : "COM5";
                    cfg.UnitId = 1; cfg.Unit = "mm";
                    cfg.SoftLimitNeg = -1; cfg.SoftLimitPos = 120;
                    cfg.JogSpeedSlow = 15; cfg.JogSpeedFast = 30; cfg.MoveSpeed = 150;
                    // ĐÃ CHỐT (Tuấn xác nhận 2026-09-04, PLAN.md §4.3): cảm biến gốc ở phía TRÊN
                    // hành trình, gán toạ độ gốc = 0 — KHÔNG lệch như trục khác. Chiều dương của Z
                    // là chiều ĐI XUỐNG (xuống gần bàn/khay = giá trị tăng), ngược với X/Y/A —
                    // "an toàn" (rút lên) là vị trí NHỎ, xem MotionConnect.ZSafeOk (≤ Z_SAFE + 1 mm, không
                    // phải ≥). Về gốc đi LÊN — dưới quy ước "xuống = dương" thì đi lên là chiều
                    // ÂM, nên HomeDir = -1 (không phải +1 như trục khác).
                    cfg.HomeDir = -1;
                    cfg.PulsePerUnit = 1000.0;         // ĐÃ CHỐT: 1000 xung/mm, cấu hình trong driver
                    // StandbyPos nhỏ hơn Z_SAFE — vẫn còn rất gần gốc, an toàn.
                    cfg.HomeOffset = 0; cfg.StandbyPos = 10;
                    break;

                case AxisId.Y1:
                case AxisId.Y2:
                    // ĐÃ CHỐT (Tuấn, 2026-09-07, PLAN.md §11 #6): chiều dương của Y là từ PHÍA
                    // TRONG đi RA phía người thao tác. HomeDir = -1 dưới đây vẫn là PHỎNG ĐOÁN —
                    // dấu thật phụ thuộc cảm biến gốc lắp ở đầu trong hay đầu ngoài, xác định ở P1.
                    cfg.Kind = EAxisKind.Panasonic;
                    cfg.ComPort = name == AxisId.Y1 ? "COM6" : "COM7";
                    cfg.UnitId = 1; cfg.Unit = "mm";
                    cfg.SoftLimitNeg = -1; cfg.SoftLimitPos = 400;
                    cfg.JogSpeedSlow = 30; cfg.JogSpeedFast = 60; cfg.MoveSpeed = 250; cfg.HomeDir = -1; cfg.StandbyPos = 20;
                    cfg.PulsePerUnit = 1000.0;         // ĐÃ CHỐT: 1000 xung/mm, cấu hình trong driver
                    break;

                case AxisId.A1:
                case AxisId.A2:
                    // ĐÃ CHỐT (Tuấn, 2026-09-07, PLAN.md §11 #6): nhìn từ TRÊN XUỐNG, chiều dương
                    // của A1/A2 là NGƯỢC CHIỀU KIM ĐỒNG HỒ (CCW). HomeDir = +1 dưới đây vẫn là
                    // PHỎNG ĐOÁN — dấu thật phụ thuộc cảm biến gốc nằm ở góc nào, xác định ở P1.
                    cfg.Kind = EAxisKind.Fastech;
                    cfg.IpAddress = name == AxisId.A1 ? "192.168.0.2" : "192.168.0.3";
                    cfg.BoardId = name == AxisId.A1
                        ? Lib.Fastech.FastechBoardId.AxisA1
                        : Lib.Fastech.FastechBoardId.AxisA2;
                    cfg.AxisIndex = 0; cfg.Unit = "deg";

                    // Toạ độ quấn vòng về [0, 360) — xem AxisConfig.WrapDegrees và AxisAngle.
                    cfg.WrapDegrees = true;
                    cfg.SoftLimitNeg = 0; cfg.SoftLimitPos = 360;   // chỉ để hiện dải hợp lệ
                    cfg.JogSpeedSlow = 45; cfg.JogSpeedFast = 90; cfg.MoveSpeed = 180;

                    // ĐÃ CHỐT (Tuấn, 2026-09-12, sau khi đổi động cơ): động cơ trả 18000
                    // xung/vòng ĐỘNG CƠ, nối qua hộp số 1/2 nên 2 vòng động cơ mới được 1 vòng
                    // đầu hút → 36000 xung cho 360° của đầu hút = 100 xung/độ chẵn, phân giải
                    // 0.01°/xung.
                    //
                    // Con số này đã đi qua hai lần sai, cùng một kiểu: 10000/360 = 27.7778 (quên
                    // hộp số), rồi 20000/360 = 55.5556 (động cơ cũ 10000 xung/vòng). Sai tỉ lệ ở
                    // đây KHÔNG hiện ra ở đâu ngoài chính góc: trục vẫn chạy, vẫn báo "tới nơi",
                    // chỉ là xoay sai góc. Đổi động cơ hay hộp số thì sửa hai hằng số nguồn
                    // (RotaryMotorPulsePerRev / RotaryMotorRevPerOutputRev), đừng gõ số ở đây.
                    cfg.PulsePerUnit = RotaryPulsePerDeg;
                    cfg.UnitPerRev = RotaryDegPerMotorRev;

                    // HomeOffset = góc máy TẠI cảm biến gốc. Để 0 nghĩa là "chưa đo" — phải đo ở
                    // P1 vì cảm biến gốc không nằm đúng tư thế gắp khay input (cách đo ghi ở
                    // FastechAxis.ToMachine).
                    cfg.HomeDir = +1; cfg.StandbyPos = 0; cfg.HomeOffset = 0;
                    break;
            }

            return cfg;
        }
    }

    /// <summary>
    /// Tên Flow (Procedure) và tên biến output trong solution VisionMaster — PLAN.md §6.8.
    /// Giá trị mặc định dưới đây là QUY ƯỚC TẠM của dự án, sửa được tự do ở trang Setting khi có
    /// tên thật từ solution do Tuấn dựng. <c>VisionConnect</c> đọc các tên này lúc trigger, không
    /// đọc hằng số cứng trong code — đổi tên Flow/Output không cần build lại.
    /// </summary>
    public class VisionOutputNames
    {
        // ── Flow (Procedure) ──

        // Ba Flow (Tuấn chốt 2026-10-08): mỗi Flow vừa soi vừa nạp điểm hiệu chuẩn, rẽ nhánh bằng biến toàn cục
        // IsTriggerLocate (1 soi, 0 hiệu chuẩn — VmVars.IsTriggerLocate). Bỏ TopDownCalib, AlignCalib_ToolN (gộp vào
        // đây) và MeasurePlaced (bỏ hẳn phép nghiệm thu đó). system.json cũ còn tên các Flow này thì bị bỏ qua lúc nạp.

        /// <summary>CAM top-down: soi chi tiết đích lúc chạy Auto (kết thúc bằng Calibration Transformation nên trả mm,
        /// Tuấn chốt 2026-09-12) và nạp điểm hiệu chuẩn N-point top-down.</summary>
        public string FlowLocateTop { get; set; } = "LocateTop";

        /// <summary>Flow AlignBottom RIÊNG từng tay hút (PLAN.md §6.4, Tuấn chốt 2026-09-08) —
        /// đã tự hiệu chuẩn tâm xoay + Teach Master bên trong VisionMaster. Từ 2026-09-14 Flow này cũng tính lệnh đặt
        /// ngay trong Group "Cmd", dùng pose ô đích App ghi vào biến toàn cục. Flow chung
        /// "AlignBottom" (không theo tay) đã bỏ hẳn (2026-09-10) — chỉ phục vụ 2 wizard hiệu
        /// chuẩn kiểu C# cũ đã orphan, nay xoá luôn cùng lúc. Từ 2026-10-08 cũng là Flow nạp điểm hiệu chuẩn tâm xoay
        /// của tay đó (nhánh IsTriggerLocate = 0, xem Routine_Calib.RunAlignCalibSequenceAsync).</summary>
        public string FlowAlignBottomTool1 { get; set; } = "AlignBottom_Tool1";
        public string FlowAlignBottomTool2 { get; set; } = "AlignBottom_Tool2";

        public string FlowAlignBottom(int tool) { return tool == 1 ? FlowAlignBottomTool1 : FlowAlignBottomTool2; }

        // ── Output chung của Flow có camera và Flow nạp điểm hiệu chuẩn ──
        /// <summary>1.0 = tìm thấy, 0.0 = không — VisionMaster không có kiểu bool cho output nên quy ước
        /// bằng float. Nguồn duy nhất của "thấy / không thấy" ở LocateTop, AlignBottom_ToolN và các Flow
        /// nạp điểm hiệu chuẩn.</summary>
        public string OutFound { get; set; } = "Found";

        /// <summary>Tên output ảnh (nếu solution có khai) — trang Main hiển thị ảnh chụp gần nhất
        /// của mỗi camera. Để trống thì không lấy ảnh (ảnh không bắt buộc cho việc đo hay đặt hàng).</summary>
        public string OutImage { get; set; } = "ImageData";

        // ── Khung ảnh trang Main (2026-09-28) ──

        /// <summary>Module VisionMaster hiện ở ba khung ảnh trang Main — CAM bottom-up tay #1, tay #2, CAM top-down.
        /// Chỉ để xem, không ảnh hưởng chu trình. Cách ghi xem <see cref="RenderPath"/>.</summary>
        public string RenderTool1 { get; set; } = "";
        public string RenderTool2 { get; set; } = "";
        public string RenderTopDown { get; set; } = "";

        /// <summary>Đường dẫn trong solution của một khung ảnh — ghi ĐẦY ĐỦ (Tuấn 2026-09-30): "T1" → Display của cả Flow
        /// T1; "T1.Module" → module đó trong Flow T1. Trống → cả Flow <paramref name="flow"/> của khung.</summary>
        public static string RenderPath(string flow, string path)
        {
            path = (path ?? "").Trim();
            return path.Length == 0 ? flow : path;
        }

        // ── Biến toàn cục chứa vị trí trục thật (§6.2, §6.4 — Tuấn chốt 2026-09-12) ──
        //
        // App ghi vị trí encoder vào các biến này NGAY TRƯỚC mỗi Flow có camera, Flow nạp điểm hiệu chuẩn và
        // Flow đo hằng số; module
        // hiệu chuẩn trong solution đặt "Calibration Points Input = By Coordinate" rồi subscribe
        // Physical Coordinate X/Y và World Rotate Angle vào đúng các biến đó.
        //
        // Nhờ vậy phép fit chạy trên toạ độ THẬT đọc từ encoder (PLAN.md §6.1 quy tắc 3) chứ không
        // phải toạ độ VisionMaster tự sinh từ bước lưới — và theo tài liệu Hikrobot "Calibration
        // and Locating Theory" tr.18, nạp điểm theo cách này thì MỌI quỹ đạo đều hợp lệ, không bắt
        // buộc hình S, không bắt buộc lưới hai chiều. Đó là điều kiện để hiệu chuẩn được CAM
        // bottom-up, vốn chỉ có chuyển động theo X và xoay A.

        /// <summary>Tên (hoặc đường dẫn) module Global Variable trong solution. Để TRỐNG thì App tự
        /// dò module đầu tiên đúng kiểu — tên hiển thị của module này đổi theo ngôn ngữ cài đặt của
        /// VisionMaster nên không đặt mặc định được.</summary>
        public string GlobalVarModule { get; set; } = "";

        public string GvAxisX { get; set; } = "X_mm";
        public string GvAxisY2 { get; set; } = "Y2_mm";
        public string GvAxisA1 { get; set; } = "A1_deg";
        public string GvAxisA2 { get; set; } = "A2_deg";

        /// <summary>
        /// Tên biến trục mang đơn vị từ 2026-09-15 (Tuấn yêu cầu: nhìn tên là biết mm hay độ). <c>system.json</c> cũ còn
        /// đúng tên mặc định X / Y2 / A1 / A2 thì đổi sang tên mới; tên đã tự đặt khác thì giữ nguyên.
        /// Trả danh sách đã đổi để ghi log.
        /// </summary>
        public List<string> MigrateAxisVarUnits()
        {
            var changed = new List<string>();
            GvAxisX = RenameDefault(GvAxisX, "X", "X_mm", changed);
            GvAxisY2 = RenameDefault(GvAxisY2, "Y2", "Y2_mm", changed);
            GvAxisA1 = RenameDefault(GvAxisA1, "A1", "A1_deg", changed);
            GvAxisA2 = RenameDefault(GvAxisA2, "A2", "A2_deg", changed);
            return changed;
        }

        private static string RenameDefault(string current, string oldDefault, string newDefault, List<string> changed)
        {
            if (current != oldDefault) return current;
            changed.Add(oldDefault + " → " + newDefault);
            return newDefault;
        }
    }
}
