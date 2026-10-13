using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using ShieldCanAssy.Core.Communication;
using ShieldCanAssy.Core.Data;
using ShieldCanAssy.Core.Data.Model;
using ShieldCanAssy.Core.Infor;
using ShieldCanAssy.Core.Lang;

namespace ShieldCanAssy.Core.Controller
{
    /// <summary>Kết quả cổng hiệu chuẩn phía App (<see cref="VmCycle.EvaluateCalib"/>). Chỉ đọc model của App —
    /// hằng số hiệu chuẩn không còn bản sao nào trong solution để đối chiếu.</summary>
    public class VmCalibGate
    {
        /// <summary>Lý do chặn vì hiệu chuẩn chưa sẵn sàng (alarm 4011); null là không có.</summary>
        public string Block;

        public double HandEyeDx1, HandEyeDy1, HandEyeDx2, HandEyeDy2;
        /// <summary>Mốc góc giữa hai camera của từng tay, độ (<see cref="VisionCalib.HandEyeAngleRef"/>).</summary>
        public double AngleRef1, AngleRef2;
        public int AlignSign1, AlignSign2;

        /// <summary>Số ngày tính từ lần đo cũ nhất trong bản ghi (Bước 2, 4, 5); NaN nếu chưa có lần nào.</summary>
        public double DaysSinceOldest = double.NaN;

        public bool Ready { get { return Block == null; } }

        public string Describe()
        {
            if (Block != null) return Loc.F("VmCycle.GateNotReady", Block);
            return string.Format(CultureInfo.InvariantCulture, Loc.T("VmCycle.GateReady"),
                HandEyeDx1, HandEyeDy1, HandEyeDx2, HandEyeDy2, AlignSign1, AlignSign2, AngleRef1, AngleRef2);
        }
    }

    /// <summary>Nhánh điều hướng sau hàm chặn lệnh đặt — bảng 3.5 của thiết kế.</summary>
    public enum EVmRoute
    {
        /// <summary>Cất lệnh, lúc đặt chạy trục tới đó.</summary>
        Ok,
        /// <summary>N — vượt ngưỡng góc bù/lệch tâm: trả sản phẩm về ô nguồn, tính NG.</summary>
        Ng,
        /// <summary>G — đích lệch lưới teach: bỏ ô, giữ sản phẩm sang ô kế.</summary>
        Skip,
        /// <summary>E — lỗi hệ thống/hợp đồng: dừng, giữ hàng, chạy lại từ S00.</summary>
        Error
    }

    public class VmPlaceCommand
    {
        public EVmRoute Route;
        public string Why;
        public int Status = int.MinValue;
        public double CmdX, CmdY2, CmdA, DiagDA, DiagEcc, DiagTarX, DiagTarY;
    }

    /// <summary>Kết quả đọc một Flow có camera.</summary>
    public class VmCameraRead
    {
        public bool CallOk;
        public string CallError;

        /// <summary>Echo (RunId, trục vừa ghi) khớp — sai là đọc nhầm Flow, nhầm biến hoặc nhầm lần chạy.</summary>
        public bool EchoOk = true;
        public string EchoIssue;

        /// <summary>Output <c>Found</c> — nguồn duy nhất của "thấy / không thấy".</summary>
        public bool Found;

        /// <summary>Echo khớp và (nếu thấy) mọi output mới hợp lệ.</summary>
        public bool VmValid;
        public string VmIssue;

        /// <summary>TopX/TopY/TopA · ObjX/ObjY/ObjA.</summary>
        public double X, Y, A;

        /// <summary>Encoder X/Y2 App ghi vào biến lúc chụp — chỉ LocateTop điền (<see cref="VmCycle.ReadLocate"/>).</summary>
        public double AxX = double.NaN, AxY = double.NaN;
    }

    /// <summary>Trung bình N ảnh App lấy cho phép đo hằng số: tư thế vật và vị trí trục lúc chụp.</summary>
    public class VmMeanCapture
    {
        public bool Ok;
        public string Reason;
        public int Shots;
        public double X, Y, A;
        public double AxX, AxY, AxA;
    }

    /// <summary>Vết một lần kích AlignBottom (<see cref="VmCycle.TraceAlignBottom"/>).</summary>
    public class VmAlignTrace
    {
        /// <summary>Dòng log đầy đủ: vị trí trục, biến App ghi, output Flow trả, phép tính lại.</summary>
        public string Line;
        /// <summary>Có đối chiếu được không (HasTarget = 1, thấy sản phẩm, Status = 1).</summary>
        public bool Checked;
        public bool Match;
        public double ErrMm, ErrDeg;
        /// <summary>Riêng phần đối chiếu — để gắn vào bảng kết quả.</summary>
        public string Check;
    }

    /// <summary>Phán quyết Bước 4 (<see cref="VmCycle.JudgeAlignSign"/>) — App tự tính, không qua Flow nào.</summary>
    public class VmAlignJudge
    {
        public int Status;
        /// <summary>+1 hoặc −1 khi kết luận được; 0 khi không.</summary>
        public int Sign;
        public double Residual, Ecc, RawDTheta, EncDA;
    }

    /// <summary>Phán quyết Bước 2 (<see cref="VmCycle.JudgeTopDown"/>).</summary>
    public class VmTopDownJudge
    {
        public int Status;
        public double StepX, StepY, ErrX, ErrY, CrossX, CrossY;
    }

    /// <summary>
    /// Phần chung của <see cref="Routine_Auto"/> và <see cref="Routine_Calib"/>: dựng yêu cầu gọi Flow, đọc và đối
    /// chiếu kết quả, hàm chặn, cổng hiệu chuẩn. Không có phép bù nào ở đây — chỉ so sánh (Tuấn chốt 2026-09-13: App
    /// chỉ chấp hành).
    /// </summary>
    public static class VmCycle
    {
        // ───────────────────────── biến App ghi ─────────────────────────

        /// <summary>Tên biến trục trống ở trang Setting là lỗi cứng.</summary>
        public static string AxisVarProblem(VisionOutputNames n)
        {
            if (n == null) return Loc.T("VmCycle.NoVisionNames");
            if (string.IsNullOrEmpty(n.GvAxisX) || string.IsNullOrEmpty(n.GvAxisY2) ||
                string.IsNullOrEmpty(n.GvAxisA1) || string.IsNullOrEmpty(n.GvAxisA2))
                return Loc.T("VmCycle.AxisVarsEmpty");
            return null;
        }

        /// <summary>Vị trí ENCODER của bốn trục + RunId — ghi trước mọi Flow.</summary>
        public static Dictionary<string, double> AxisGlobals(MotionConnect motion, VisionOutputNames n, int runId)
        {
            var g = new Dictionary<string, double>(StringComparer.Ordinal);
            g[n.GvAxisX] = Pos(motion, AxisId.X);
            g[n.GvAxisY2] = Pos(motion, AxisId.Y2);
            g[n.GvAxisA1] = Pos(motion, AxisId.A1);
            g[n.GvAxisA2] = Pos(motion, AxisId.A2);
            g[VmVars.RunId] = runId;
            return g;
        }

        private static double Pos(MotionConnect motion, string axis)
        {
            var a = motion == null ? null : motion[axis];
            return a == null ? 0 : a.CurrentPos;
        }

        // ───────────────────────── yêu cầu gọi Flow ─────────────────────────

        /// <summary>
        /// LocateTop: <c>Found</c> và echo bắt buộc. Output của Script đọc dạng tuỳ chọn và chỉ bị đòi
        /// khi <c>Found = 1</c> (<see cref="ReadLocate"/>) — khi không thấy mẫu, chưa chắc Script còn chạy.
        /// </summary>
        public static VmCallRequest LocateRequest(VisionOutputNames n, IDictionary<string, double> globals)
        {
            // Nhánh soi của Flow (Tuấn 2026-10-08) — nạp điểm hiệu chuẩn top-down ghi 0 ở VisionConnect.
            if (globals != null) globals[VmVars.IsTriggerLocate] = 1;
            var req = new VmCallRequest { Flow = n.FlowLocateTop, Camera = EVmCamera.TopDown, Globals = globals };
            req.Outputs.Add(n.OutFound);
            foreach (var o in VmOut.LocateEcho) req.Outputs.Add(o);
            foreach (var o in VmOut.LocateTopScript) req.OptionalOutputs.Add(o);
            return req;
        }

        /// <summary>
        /// Thêm tám biến của lần đặt vào bộ biến sẽ ghi trước <c>AlignBottom_ToolN</c>. <paramref name="target"/> là pose ô
        /// đích (số LocateTop đo lúc quét tray) — null thì <c>HasTarget = 0</c> và Group Cmd nghỉ, Flow chỉ soi (Bước 4).
        /// Hand-eye và dấu góc lấy của <b>đúng tay đang gọi</b>: đây là chỗ duy nhất chọn theo tay.
        /// <para><c>CellA_deg</c> = <b>mốc góc</b> của tay (<see cref="VisionCalib.HandEyeAngleRef"/>) <b>−</b> góc LocateTop
        /// + bù góc của ô — quy góc camera trên về hệ góc của camera dưới, để Single Point Alignment trừ hai góc cùng hệ.
        /// Thay nút Create Reference mà module Single Point Alignment của VM 4.4 không có (Tuấn duyệt 2026-09-22).</para>
        /// <para><b>TRỪ góc LocateTop, không cộng</b>: hai camera nhìn ngược chiều nhau nên đo góc ngược dấu — xem
        /// <see cref="VisionCalib.HandEye1AngleRef"/>. Bù góc của ô thì vẫn cộng, vì nó được định nghĩa là lượng cộng
        /// thẳng vào <c>CellA</c> (<see cref="CellOffset.PlaceDa"/>), không phải một góc đo bằng camera trên.</para>
        /// </summary>
        /// <param name="trim">Bù đặt tổng của đúng ô đích và đúng tay — bù chung + bù riêng của ô
        /// (<see cref="TrayLayout.PlaceTrim"/>), hoặc null.
        /// Cộng thẳng vào pose đích gửi đi — Group Cmd không biết có lượng bù nào, nó chỉ thấy một ô đích
        /// dịch đi vài phần trăm mm. Vì vậy hàng rào lưới ở <see cref="CheckPlace"/> cũng phải dời tâm đi
        /// đúng lượng này, không thì bù nhiều là bị chính hàng rào bỏ ô.</param>
        public static void AddPlaceGlobals(IDictionary<string, double> g, PlacedTopRecord target, VisionCalib calib, int tool,
                                           CellOffset trim = null)
        {
            if (g == null) return;
            double tdx = trim == null ? 0 : trim.PlaceDx;
            double tdy = trim == null ? 0 : trim.PlaceDy;
            double tda = trim == null ? 0 : trim.PlaceDa;
            g[VmVars.HasTarget] = target == null ? 0 : 1;
            g[VmVars.CellX] = target == null ? 0 : target.X + tdx;
            g[VmVars.CellY] = target == null ? 0 : BottomYSign * (target.Y + tdy);
            double refA = calib == null ? 0 : calib.HandEyeAngleRef(tool);
            g[VmVars.CellA] = target == null ? 0 : AxisAngle.Wrap(refA - target.A + tda);
            g[VmVars.HandEyeDx] = calib == null ? 0 : calib.HandEyeDx(tool);
            g[VmVars.HandEyeDy] = calib == null ? 0 : BottomYSign * calib.HandEyeDy(tool);
            g[VmVars.AlignSign] = calib == null ? 0 : calib.AlignAngleSign(tool);
        }

        /// <summary>
        /// Trục Y của camera dưới NGƯỢC chiều trục Y2 của máy (Tuấn thấy trên máy 2026-09-30, đầu hút mới rộng hơn): CAM
        /// dưới hiệu chuẩn bằng tịnh tiến chỉ theo X nên VisionMaster tự chọn chiều Y và ra hệ lật — cùng nguyên nhân với
        /// hai camera đo góc ngược dấu (<see cref="VisionCalib.HandEye1AngleRef"/>, 23/09). Trục X thì đã ghim đúng chiều.
        ///
        /// <para>Single Point Alignment trong Group Cmd tính trong hệ camera dưới, nên App đổi dấu Y ở cả hai đầu: ghi
        /// <c>CellY_mm</c>/<c>HandEyeDy_mm</c> đảo dấu (<see cref="AddPlaceGlobals"/>), đọc <c>CmdY2_mm</c>/<c>DiagTarY_mm</c>
        /// rồi đảo lại (<see cref="CheckPlace"/>). Solution không đổi. Hand-eye Y trừ lệch tâm Y thay vì cộng
        /// (<c>Routine_Calib.HandEyeCommitAsync</c>).</para>
        ///
        /// <para>Trước đây sai số đặt theo Y = 2 × (lệch tâm Y lúc đặt − lúc đo hand-eye): đầu hút cũ ôm chặt con can nên
        /// lệch tâm gần như không đổi và sai số nằm gọn trong <c>HandEyeDy</c>; đầu hút rộng cho con can xê dịch nên lộ ra.</para>
        /// </summary>
        public const double BottomYSign = -1;

        /// <summary>
        /// AlignBottom_ToolN. <paramref name="hasTarget"/> = true thì đọc thêm output của Group Cmd — bộ biến
        /// <paramref name="globals"/> phải đã đi qua <see cref="AddPlaceGlobals"/>.
        /// </summary>
        public static VmCallRequest AlignBottomRequest(VisionOutputNames n, int tool, IDictionary<string, double> globals, bool hasTarget)
        {
            // Nhánh soi của Flow (Tuấn 2026-10-08) — nạp điểm hiệu chuẩn tâm xoay ghi 0 ở VisionConnect.
            if (globals != null) globals[VmVars.IsTriggerLocate] = 1;
            var req = new VmCallRequest
            {
                Flow = n.FlowAlignBottom(tool),
                Camera = EVmCamera.BottomUp,
                Globals = globals
            };
            req.Outputs.Add(n.OutFound);
            foreach (var o in VmOut.AlignEcho) req.Outputs.Add(o);
            foreach (var o in VmOut.AlignBottomScript) req.OptionalOutputs.Add(o);
            if (hasTarget)
                foreach (var o in VmOut.AlignPlace) req.OptionalOutputs.Add(o);
            return req;
        }

        // ───────────────────────── hợp đồng Output Params ─────────────────────────

        /// <summary>
        /// Bộ output mà từng Flow phải khai — dựng từ CHÍNH hai hàm tạo lời gọi ở trên
        /// (<see cref="LocateRequest"/>, <see cref="AlignBottomRequest"/>) nên không thể lệch với thứ App thật sự đọc
        /// lúc chạy: đổi hợp đồng ở một chỗ là tool kiểm tra đổi theo.
        ///
        /// <para>Ở đây gộp cả <c>Outputs</c> lẫn <c>OptionalOutputs</c>: "tuỳ chọn" chỉ nói về lúc chạy
        /// (tư thế rỗng khi không thấy vật), còn <b>khai thiếu</b> thì lúc nào cũng là lỗi dựng solution.</para>
        ///
        /// <para>Ba Flow (2026-10-08): nhánh hiệu chuẩn (IsTriggerLocate = 0) App chỉ đọc <c>Found</c>, đã nằm trong bộ
        /// output của nhánh soi.</para>
        /// </summary>
        public static List<VmFlowExpect> ExpectedFlowOutputs(VisionOutputNames n)
        {
            var list = new List<VmFlowExpect>();
            if (n == null) return list;
            var empty = new Dictionary<string, double>();

            AddExpect(list, LocateRequest(n, empty), n.OutImage, null);
            AddExpect(list, AlignBottomRequest(n, 1, empty, true), n.OutImage, Loc.T("VmCycle.NoteGroupCmdOutputs"));
            AddExpect(list, AlignBottomRequest(n, 2, empty, true), n.OutImage, Loc.T("VmCycle.NoteTool2Echo"));
            return list;
        }

        private static void AddExpect(List<VmFlowExpect> list, VmCallRequest req, string image, string note)
        {
            var e = new VmFlowExpect { Flow = req.Flow, ImageName = image, Note = note };
            foreach (var o in req.Outputs) if (!e.Names.Contains(o)) e.Names.Add(o);
            foreach (var o in req.OptionalOutputs) if (!e.Names.Contains(o)) e.Names.Add(o);
            list.Add(e);
        }

        /// <summary>Bản khai "đúng hết" của một Flow — mô phỏng dùng, để bấm nút kiểm tra lúc chạy
        /// SimulationMode vẫn ra một báo cáo có nghĩa thay vì bảy dòng lỗi.</summary>
        public static VmFlowOutputs FakeFlowOutputs(string flow, VisionOutputNames n)
        {
            var expect = ExpectedFlowOutputs(n).FirstOrDefault(e => e.Flow == flow);
            if (expect == null) return VmFlowOutputs.Fail(flow, Loc.T("VmCycle.NoSuchFlow"));

            var r = new VmFlowOutputs { Flow = flow, InputParamCount = 0 };
            foreach (var name in expect.Names)
                r.Declared.Add(new VmDeclaredOut
                {
                    Name = name,
                    TypeCode = VmOutCheck.TypeFloat,
                    TypeText = VmOutCheck.TypeName(VmOutCheck.TypeFloat),
                    Numeric = true
                });

            // Module Script giả — bind đủ, để bản mô phỏng ra báo cáo sạch.
            var script = VmScriptContract.Build(n).FirstOrDefault(e => e.Flow == flow);
            if (script != null)
            {
                var m = new VmModuleIo { Name = script.ScriptName };
                foreach (var i in script.Inputs)
                    m.Inputs.Add(new VmIoInfo { Name = i, TypeText = "float", Source = Loc.T("VmCycle.SimSource") });
                foreach (var o in script.Outputs)
                    m.Outputs.Add(new VmIoInfo { Name = o, TypeText = "float" });
                r.Modules.Add(m);
            }
            return r;
        }

        // ───────────────────────── đọc kết quả Flow có camera ─────────────────────────

        public static VmCameraRead ReadLocate(VmCallResult r, IDictionary<string, double> sent, VisionOutputNames n)
        {
            var c = new VmCameraRead { AxX = Val(sent, n.GvAxisX), AxY = Val(sent, n.GvAxisY2) };
            if (r == null || !r.Ok) { c.CallError = r == null ? Loc.T("VmCycle.NoResult") : r.Error; return c; }
            c.CallOk = true;

            double rid = Val(sent, VmVars.RunId);
            var echo = new List<string>();
            CheckEcho(r, VmOut.EchoRunId, rid, 0.5, echo);
            CheckEcho(r, VmOut.EchoX, Val(sent, n.GvAxisX), VmMath.EchoTol, echo);
            CheckEcho(r, VmOut.EchoY2, Val(sent, n.GvAxisY2), VmMath.EchoTol, echo);
            c.EchoOk = echo.Count == 0;
            c.EchoIssue = Join(echo);

            c.Found = r.Get(n.OutFound) > 0.5;

            var vm = new List<string>(echo);
            if (c.Found)
            {
                if (!VmMath.SameInt(r.Get(VmOut.TopFound), 1)) vm.Add("TopFound ≠ 1");
                if (!VmMath.Near(r.Get(VmOut.TopRunId), rid, 0.5)) vm.Add(Loc.F("VmCycle.RunIdNotReached", "TopRunId"));

                c.X = r.Get(VmOut.TopX);
                c.Y = r.Get(VmOut.TopY);
                c.A = r.Get(VmOut.TopA);
                if (!VmMath.IsFinite(c.X) || !VmMath.IsFinite(c.Y) || !VmMath.IsFinite(c.A))
                    vm.Add(Loc.F("VmCycle.PoseNotFinite", VmOut.TopX, VmOut.TopY, VmOut.TopA));
            }
            c.VmValid = vm.Count == 0;
            c.VmIssue = Join(vm);
            return c;
        }

        /// <summary>AlignBottom_ToolN: echo trục và RunId; tư thế sản phẩm khi thấy. Phần lệnh đặt đọc ở
        /// <see cref="CheckPlace"/>.</summary>
        public static VmCameraRead ReadAlignBottom(VmCallResult r, VmCallRequest req, VisionOutputNames n, int tool)
        {
            var c = new VmCameraRead();
            if (r == null || !r.Ok) { c.CallError = r == null ? Loc.T("VmCycle.NoResult") : r.Error; return c; }
            c.CallOk = true;

            var sent = req == null ? null : req.Globals;
            double rid = Val(sent, VmVars.RunId);
            double aSent = Val(sent, tool == 1 ? n.GvAxisA1 : n.GvAxisA2);
            var echo = new List<string>();
            CheckEcho(r, VmOut.EchoRunId, rid, 0.5, echo);
            CheckEcho(r, VmOut.EchoX, Val(sent, n.GvAxisX), VmMath.EchoTol, echo);
            CheckEcho(r, VmOut.EchoA, aSent, VmMath.EchoTol, echo);
            c.EchoOk = echo.Count == 0;
            c.EchoIssue = Join(echo);

            c.Found = r.Get(n.OutFound) > 0.5;

            var vm = new List<string>(echo);
            if (c.Found)
            {
                if (!VmMath.SameInt(r.Get(VmOut.ObjFound), 1)) vm.Add("ObjFound ≠ 1");
                if (!VmMath.Near(r.Get(VmOut.ObjRunId), rid, 0.5)) vm.Add(Loc.F("VmCycle.RunIdNotReached", "ObjRunId"));
                c.X = r.Get(VmOut.ObjX);
                c.Y = r.Get(VmOut.ObjY);
                c.A = r.Get(VmOut.ObjA);
                if (!VmMath.IsFinite(c.X) || !VmMath.IsFinite(c.Y) || !VmMath.IsFinite(c.A))
                    vm.Add(Loc.F("VmCycle.PoseNotFinite", VmOut.ObjX, VmOut.ObjY, VmOut.ObjA));
            }
            c.VmValid = vm.Count == 0;
            c.VmIssue = Join(vm);
            return c;
        }

        // ───────────────────────── hàm chặn lệnh đặt (mục 3.4) ─────────────────────────

        /// <summary>
        /// Hàm chặn sau AlignBottom_ToolN kèm ô đích — chỉ so sánh, theo thứ tự a–h của thiết kế. Không tính lại lệnh nào:
        /// số trục lấy nguyên văn từ Flow, App chỉ quyết định có dám chạy tới đó hay không.
        /// </summary>
        /// <param name="aSent">Góc trục A của tay này App vừa ghi vào biến toàn cục — gốc của <c>CmdA</c>.</param>
        /// <param name="cellX">Toạ độ ô theo lưới teach của ĐÚNG tay đặt.</param>
        /// <param name="maxAngleDeg">Ngưỡng góc bù ở trang Setting.</param>
        /// <param name="maxEccMm">Ngưỡng lệch tâm ở trang Setting.</param>
        /// <param name="checkGrid">Bỏ phép chặn lưới (f) — chỉ cho chạy khô trên dấu chuẩn.</param>
        public static VmPlaceCommand CheckPlace(VmCallResult r, VmCallRequest req, int tool, double aSent,
                                                double cellX, double cellY, double guardMm, double maxAngleDeg, double maxEccMm,
                                                Func<double, bool> xInLimit, Func<double, bool> yInLimit, bool checkGrid)
        {
            var c = new VmPlaceCommand { Route = EVmRoute.Error };
            string flow = "AlignBottom_Tool" + tool + ": ";
            var sent = req == null ? null : req.Globals;

            // a — đọc được, đúng lần chạy, đúng số vừa gửi
            if (r == null || !r.Ok) return Fail(c, flow + (r == null ? Loc.T("VmCycle.NoResult") : r.Error));
            if (!VmMath.Near(r.Get(VmOut.EchoRunId), Val(sent, VmVars.RunId), 0.5))
                return Fail(c, flow + Loc.T("VmCycle.EchoRunIdMismatch"));
            if (!VmMath.Near(r.Get(VmOut.ObjRunId), Val(sent, VmVars.RunId), 0.5))
                return Fail(c, flow + Loc.T("VmCycle.ObjRunIdMismatch"));

            // b — Status
            c.Status = r.GetInt(VmOut.Status);
            c.DiagDA = r.Get(VmOut.DiagDA);
            c.DiagEcc = r.Get(VmOut.DiagEcc);
            if (c.Status != VmStatus.Ok) return Fail(c, flow + VmStatus.Place(c.Status));

            // c — số hữu hạn, góc trong dải
            c.CmdX = r.Get(VmOut.CmdX);
            c.CmdY2 = BottomYSign * r.Get(VmOut.CmdY2);        // Group Cmd tính trong hệ camera dưới — xem BottomYSign
            c.CmdA = r.Get(VmOut.CmdA);
            c.DiagTarX = r.Get(VmOut.DiagTarX);
            c.DiagTarY = BottomYSign * r.Get(VmOut.DiagTarY);
            if (!VmMath.IsFinite(c.CmdX) || !VmMath.IsFinite(c.CmdY2) || !VmMath.IsFinite(c.CmdA) || !VmMath.IsFinite(c.DiagTarX) ||
                !VmMath.IsFinite(c.DiagTarY) || !VmMath.IsFinite(c.DiagDA) || !VmMath.IsFinite(c.DiagEcc))
                return Fail(c, flow + Loc.T("VmCycle.CmdNotFinite"));
            if (c.CmdA < 0 || c.CmdA > 360) return Fail(c, flow + Loc.F("VmCycle.CmdAOutOfRange", VmOut.CmdA, Fmt(c.CmdA)));

            // e — ngưỡng góc bù và lệch tâm (N)
            if (!VmMath.IsFinite(maxAngleDeg) || Math.Abs(c.DiagDA) > maxAngleDeg)
                return Ng(c, string.Format(CultureInfo.InvariantCulture, Loc.T("VmCycle.NgAngle"),
                    c.DiagDA, maxAngleDeg, c.DiagEcc));
            if (!VmMath.IsFinite(maxEccMm) || c.DiagEcc > maxEccMm)
                return Ng(c, string.Format(CultureInfo.InvariantCulture, Loc.T("VmCycle.NgEcc"),
                    c.DiagEcc, maxEccMm, c.DiagDA));

            // f — chặn theo lưới teach của đúng tay (G)
            if (checkGrid)
            {
                double ex = c.DiagTarX - cellX, ey = c.DiagTarY - cellY;
                if (Math.Abs(ex) > guardMm || Math.Abs(ey) > guardMm)
                    return Skip(c, string.Format(CultureInfo.InvariantCulture,
                        Loc.T("VmCycle.SkipTargetOffGrid"), ex, ey, guardMm));
                double lim = guardMm + maxEccMm;
                double cx = c.CmdX - cellX, cy = c.CmdY2 - cellY;
                if (Math.Abs(cx) > lim || Math.Abs(cy) > lim)
                    return Skip(c, string.Format(CultureInfo.InvariantCulture,
                        Loc.T("VmCycle.SkipCmdOffGrid"), cx, cy, lim));
            }

            // g — soft limit trên đúng tham số sẽ truyền cho trục
            if (xInLimit != null && !xInLimit(c.CmdX)) return Fail(c, flow + Loc.F("VmCycle.CmdXSoftLimit", Fmt(c.CmdX)));
            if (yInLimit != null && !yInLimit(c.CmdY2)) return Fail(c, flow + Loc.F("VmCycle.CmdY2SoftLimit", Fmt(c.CmdY2)));

            // h — lưới an toàn góc (không phải ngưỡng thứ hai). Góc lúc chụp là số App vừa ghi vào biến trục.
            if (!VmMath.IsFinite(aSent) || AxisAngle.Distance(aSent, c.CmdA) > maxAngleDeg + 1)
                return Fail(c, flow + string.Format(CultureInfo.InvariantCulture,
                    Loc.T("VmCycle.CmdAFarFromCapture"), c.CmdA, Fmt(aSent)));

            c.Route = EVmRoute.Ok;
            return c;
        }

        // ───────────────────────── Bước 4 — App tự phán ─────────────────────────

        /// <summary>
        /// Phép thử vòng xoay: hai lần đo tư thế sản phẩm (trước và sau khi xoay trục A một góc đã biết), giải đúng bài
        /// toán Single Point Alignment giữa chúng. Cho ra <b>dấu góc</b> — số duy nhất cho biết chiều góc dương của hệ
        /// toạ độ bottom-up so với chiều dương của trục A — và <b>sai số dư</b>, lượng dịch còn lại sau khi trừ phần xoay,
        /// phải bằng 0 nếu gốc toạ độ nằm đúng tâm xoay.
        ///
        /// <para>Trước 14/09/2026 phép này chạy trong Flow <c>AlignVerify_ToolN</c>; nay App tự tính, thứ tự phán và
        /// công thức dấu giữ nguyên.</para>
        /// </summary>
        public static VmAlignJudge JudgeAlignSign(VmMeanCapture s0, VmMeanCapture s1,
                                                  double tolMm, double angleTolDeg, double minEccMm, double minTurnDeg)
        {
            var j = new VmAlignJudge();
            if (s0 == null || s1 == null) { j.Status = VmStatus.Invalid; return j; }

            // Single Point Alignment: Demonstrate = tư thế trước khi xoay, Run = tư thế sau khi xoay
            double dx, dy, raw;
            SinglePointAlignMath.Solve(s0.X, s0.Y, s0.A, s1.X, s1.Y, s1.A, out dx, out dy, out raw);
            j.RawDTheta = SinglePointAlignMath.NormalizeDeg(raw);
            j.EncDA = SinglePointAlignMath.NormalizeDeg(s1.AxA - s0.AxA);
            j.Residual = Math.Sqrt(dx * dx + dy * dy);
            j.Ecc = Math.Sqrt(s0.X * s0.X + s0.Y * s0.Y);

            if (!(VmMath.IsFinite(j.RawDTheta) && VmMath.IsFinite(j.Residual) && VmMath.IsFinite(j.Ecc) && VmMath.IsFinite(j.EncDA)))
                j.Status = VmStatus.Invalid;
            else if (Math.Abs(s1.AxX - s0.AxX) > 0.01) j.Status = VmStatus.AxisMoved;
            else if (Math.Abs(j.EncDA) < minTurnDeg) j.Status = VmStatus.Code8;
            else if (Math.Abs(Math.Abs(j.RawDTheta) - Math.Abs(j.EncDA)) > angleTolDeg) j.Status = VmStatus.AngleOut;
            else if (j.Residual > tolMm) j.Status = VmStatus.EccOut;
            else if (j.Ecc < minEccMm) j.Status = VmStatus.NoConclusion;
            else j.Status = VmStatus.Ok;

            // Dấu tính theo encoder nên góc thử âm vẫn ra đúng dấu.
            j.Sign = (j.Status == VmStatus.Ok || j.Status == VmStatus.NoConclusion)
                ? -Math.Sign(j.EncDA) * Math.Sign(j.RawDTheta) : 0;
            return j;
        }

        // ───────────────────────── Bước 2 — App tự phán ─────────────────────────

        /// <summary>
        /// Ba lần đo dấu chuẩn (gốc, sau khi chạy X, sau khi chạy Y2). <c>CamX/CamY</c> là toạ độ tuyệt đối nên dấu chuẩn
        /// đứng yên trong hệ máy phải cho cùng một số dù trục chạy — chỉ là phép trừ và so ngưỡng, bắt được sai thang đo,
        /// sai dấu và lẫn trục X ↔ Y.
        /// </summary>
        public static VmTopDownJudge JudgeTopDown(VmMeanCapture s0, VmMeanCapture s1, VmMeanCapture s2, double tolMm)
        {
            var j = new VmTopDownJudge();
            j.StepX = s1.AxX - s0.AxX;
            j.StepY = s2.AxY - s0.AxY;
            j.ErrX = s1.X - s0.X;
            j.CrossX = s1.Y - s0.Y;
            j.ErrY = s2.Y - s0.Y;
            j.CrossY = s2.X - s0.X;

            bool finite = VmMath.IsFinite(j.ErrX) && VmMath.IsFinite(j.ErrY) && VmMath.IsFinite(j.CrossX) &&
                          VmMath.IsFinite(j.CrossY) && VmMath.IsFinite(j.StepX) && VmMath.IsFinite(j.StepY) && VmMath.IsFinite(tolMm);
            if (!finite) j.Status = VmStatus.Invalid;
            else if (Math.Abs(s1.AxY - s0.AxY) > 0.01 || Math.Abs(s2.AxX - s0.AxX) > 0.01) j.Status = VmStatus.AxisMoved;
            else if (Math.Abs(j.StepX) < 0.5 || Math.Abs(j.StepY) < 0.5) j.Status = VmStatus.NoConclusion;
            else if (Math.Abs(j.ErrX) >= Math.Abs(j.StepX) || Math.Abs(j.ErrY) >= Math.Abs(j.StepY)) j.Status = VmStatus.Code8;
            else if (Math.Abs(j.ErrX) > tolMm || Math.Abs(j.ErrY) > tolMm || Math.Abs(j.CrossX) > tolMm || Math.Abs(j.CrossY) > tolMm)
                j.Status = VmStatus.EccOut;
            else j.Status = VmStatus.Ok;
            return j;
        }

        // ───────────────────────── cổng hiệu chuẩn phía App ─────────────────────────

        /// <summary>
        /// Cổng hiệu chuẩn: đủ Bước 2, 4, 5, vị trí soi chưa teach lại, ngưỡng hợp lệ. <b>Chỉ đọc model của App</b> —
        /// hằng số hiệu chuẩn không có bản sao nào trong solution để đối chiếu. App không theo dõi file hiệu chuẩn:
        /// hiệu chuẩn lại camera trong VisionMaster thì chạy lại Bước 2, 4, 5.
        ///
        /// <para><paramref name="nozzle"/> = 0 (Auto) đòi cả hai tay. Chạy khô một tay chỉ dùng hằng số của tay đó, nên
        /// gọi với 1 hoặc 2: tay kia chưa hiệu chuẩn không được chặn — làm xong và kiểm xong từng tay một (Tuấn gặp
        /// 2026-09-22: chạy khô tay #2 bị chặn vì tay #1 chưa làm).</para>
        /// </summary>
        public static VmCalibGate EvaluateCalib(ModelData model, SystemData sys, int nozzle = 0)
        {
            var g = new VmCalibGate();
            if (model == null || sys == null) { g.Block = Loc.T("VmCycle.GateNoModel"); return g; }

            var c = model.Calib ?? new VisionCalib();
            var why = new List<string>();
            var tools = nozzle == 1 || nozzle == 2 ? new[] { nozzle } : new[] { 1, 2 };
            if (!c.IsTopDownVerified) why.Add(Loc.T("VmCycle.GateTopDown"));
            foreach (int n in tools)
                if (!c.IsAlignVerified(n)) why.Add(Loc.F("VmCycle.GateNoSign", n));
            foreach (int n in tools)
            {
                if (c.IsHandEyeOutdated(n))
                    why.Add(Loc.F("VmCycle.GateHandEyeOld", n));
                else if (!c.IsHandEyeMeasured(n)) why.Add(Loc.F("VmCycle.GateHandEyeMissing", n));
            }

            bool thresholdsOk = sys.MaxCompensationAngleDeg > 0 && sys.MaxCompensationAngleDeg <= 45 &&
                                sys.MaxCompensationDistance > 0 && sys.MaxCompensationDistance <= 10 &&
                                sys.AlignXTolMm > 0 && sys.AlignXTolMm <= 1;
            if (!thresholdsOk) why.Add(Loc.T("VmCycle.GateThresholds"));

            foreach (int n in tools)
            {
                if (!c.IsAlignVerified(n) || !thresholdsOk) continue;
                double taught = model.AlignPosX(n), verified = c.AlignVerifiedPosX(n);
                if (Math.Abs(taught - verified) > sys.AlignXTolMm)
                    why.Add(string.Format(CultureInfo.InvariantCulture, Loc.T("VmCycle.GateAlignPosMoved"),
                        n, taught, verified, sys.AlignXTolMm));
            }

            var dates = new List<DateTime> { c.TopDownVerifiedAt };
            foreach (int n in tools)
            {
                dates.Add(n == 1 ? c.AlignVerifiedAt1 : c.AlignVerifiedAt2);
                dates.Add(n == 1 ? c.HandEye1MeasuredAt : c.HandEye2MeasuredAt);
            }
            double oldest = double.NaN;
            foreach (var at in dates)
            {
                if (at <= DateTime.MinValue) continue;
                double days = (DateTime.Now - at).TotalDays;
                oldest = double.IsNaN(oldest) ? days : Math.Max(oldest, days);
            }
            g.DaysSinceOldest = oldest;

            g.HandEyeDx1 = c.HandEyeDx(1); g.HandEyeDy1 = c.HandEyeDy(1); g.AngleRef1 = c.HandEyeAngleRef(1);
            g.HandEyeDx2 = c.HandEyeDx(2); g.HandEyeDy2 = c.HandEyeDy(2); g.AngleRef2 = c.HandEyeAngleRef(2);
            g.AlignSign1 = c.AlignAngleSign(1); g.AlignSign2 = c.AlignAngleSign(2);

            g.Block = Join(why);
            return g;
        }

        // ───────────────────────── vết một lần kích AlignBottom ─────────────────────────

        /// <summary>Dung sai đối chiếu Group Cmd với phép tính lại của App: output của Flow là float 32 bit.</summary>
        public const double CmdCheckTolMm = 0.005, CmdCheckTolDeg = 0.01;

        /// <summary>
        /// Một dòng log cho MỖI lần kích <c>AlignBottom_ToolN</c>: vị trí trục lúc chụp, mọi biến toàn cục App vừa ghi,
        /// mọi output đọc về — rồi App <b>tính lại</b> đúng phép của Group Cmd (Single Point Alignment + Script Cmd, xem
        /// <see cref="SinglePointAlignMath"/>) từ chính các số đó và so với lệnh Flow trả về. Chỉ đối chiếu để ghi log,
        /// không thay số của Flow: lệnh chạy máy vẫn là số VisionMaster tính (Tuấn yêu cầu 2026-09-22).
        ///
        /// <para>Lệch nghĩa là Group Cmd trong solution không làm đúng phép tính trong cẩm nang — nối nhầm input, sai
        /// dấu, quên cộng hand-eye… — thứ mà ngưỡng và hàng rào lưới chỉ bắt được khi đã lệch nhiều.</para>
        /// </summary>
        public static VmAlignTrace TraceAlignBottom(VmCallResult r, VmCallRequest req, VisionOutputNames n, int tool, double zPos)
        {
            var t = new VmAlignTrace();
            var inv = CultureInfo.InvariantCulture;
            var g = req == null ? null : req.Globals;
            string aName = tool == 2 ? n.GvAxisA2 : n.GvAxisA1;
            var sb = new System.Text.StringBuilder();
            sb.AppendFormat(inv, "{0} RunId {1}: trục X {2}, Y2 {3}, {4} {5}°, {6} {7}", req == null ? "AlignBottom" : req.Flow,
                Fmt4(Val(g, VmVars.RunId)), Fmt4(Val(g, n.GvAxisX)), Fmt4(Val(g, n.GvAxisY2)), aName, Fmt4(Val(g, aName)),
                tool == 2 ? AxisId.Z2 : AxisId.Z1, Fmt4(zPos));

            sb.Append(" | App ghi:");
            if (g != null)
                foreach (var kv in g)
                    if (kv.Key != n.GvAxisX && kv.Key != n.GvAxisY2 && kv.Key != n.GvAxisA1 && kv.Key != n.GvAxisA2 && kv.Key != VmVars.RunId)
                        sb.Append(' ').Append(kv.Key).Append(' ').Append(Fmt4(kv.Value)).Append(',');
            if (sb[sb.Length - 1] == ',') sb.Length--;

            if (r == null || !r.Ok)
            {
                sb.Append(" | gọi Flow lỗi: ").Append(r == null ? "không có kết quả" : r.Error);
                t.Line = sb.ToString();
                return t;
            }
            sb.Append(" | Flow trả:");
            var names = new List<string>();
            if (req != null)
                foreach (var o in req.Outputs.Concat(req.OptionalOutputs)) if (!names.Contains(o)) names.Add(o);
            foreach (var o in names)
                sb.Append(' ').Append(o).Append(' ').Append(r.Has(o) ? Fmt4(r.Get(o)) : "(thiếu)").Append(',');
            if (sb[sb.Length - 1] == ',') sb.Length--;

            // ── App tính lại Group Cmd ──
            double hasTarget = Val(g, VmVars.HasTarget);
            if (!(hasTarget >= 0.5)) { sb.Append(" | Group Cmd nghỉ (HasTarget = 0)"); t.Line = sb.ToString(); return t; }
            if (!VmMath.SameInt(r.Get(VmOut.ObjFound), 1)) { sb.Append(" | không đối chiếu: không thấy sản phẩm"); t.Line = sb.ToString(); return t; }
            if (!VmMath.SameInt(r.Get(VmOut.Status), VmStatus.Ok))
            { sb.Append(" | không đối chiếu: Group Cmd báo Status ").Append(Fmt4(r.Get(VmOut.Status))); t.Line = sb.ToString(); return t; }

            double tarX = Val(g, VmVars.CellX) + Val(g, VmVars.HandEyeDx);
            double tarY = Val(g, VmVars.CellY) + Val(g, VmVars.HandEyeDy);
            double tarA = Val(g, VmVars.CellA);
            double ox = r.Get(VmOut.ObjX), oy = r.Get(VmOut.ObjY), oa = r.Get(VmOut.ObjA);
            double dx, dy, dt;
            SinglePointAlignMath.Solve(tarX, tarY, tarA, ox, oy, oa, out dx, out dy, out dt);
            int sign = (int)Math.Round(Val(g, VmVars.AlignSign));
            double dA = SinglePointAlignMath.NormalizeDeg(sign * dt);
            double cmdA = AxisAngle.Wrap(Val(g, aName) + dA);
            double ecc = Math.Sqrt(ox * ox + oy * oy);

            t.Checked = true;
            t.ErrMm = new[]
            {
                Math.Abs(r.Get(VmOut.CmdX) - dx), Math.Abs(r.Get(VmOut.CmdY2) - dy),
                Math.Abs(r.Get(VmOut.DiagTarX) - tarX), Math.Abs(r.Get(VmOut.DiagTarY) - tarY), Math.Abs(r.Get(VmOut.DiagEcc) - ecc)
            }.Max();
            t.ErrDeg = Math.Max(AxisAngle.Distance(r.Get(VmOut.CmdA), cmdA),
                                Math.Abs(SinglePointAlignMath.NormalizeDeg(r.Get(VmOut.DiagDA) - dA)));
            if (!VmMath.IsFinite(t.ErrMm)) t.ErrMm = double.PositiveInfinity;
            if (!VmMath.IsFinite(t.ErrDeg)) t.ErrDeg = double.PositiveInfinity;
            t.Match = t.ErrMm <= CmdCheckTolMm && t.ErrDeg <= CmdCheckTolDeg;

            // So trong hệ camera dưới (đúng số Flow thấy), nhưng in đích và lệnh theo chiều máy — xem BottomYSign.
            t.Check = string.Format(inv, Loc.T("VmCycle.CheckRecalc"),
                tarX, BottomYSign * tarY, tarA, dt, sign, dA, dx, BottomYSign * dy, cmdA,
                t.Match
                    ? string.Format(inv, Loc.T("VmCycle.CheckMatch"), t.ErrMm, t.ErrDeg)
                    : string.Format(inv, Loc.T("VmCycle.CheckMismatch"),
                        Fmt4(r.Get(VmOut.CmdX)), Fmt4(r.Get(VmOut.CmdY2)), Fmt4(r.Get(VmOut.CmdA)), Fmt4(r.Get(VmOut.DiagDA)), t.ErrMm, t.ErrDeg));
            sb.Append(" | ").Append(t.Check);
            t.Line = sb.ToString();
            return t;
        }

        private static string Fmt4(double v)
        {
            return VmMath.IsFinite(v) ? v.ToString("0.####", CultureInfo.InvariantCulture) : "NaN";
        }

        // ───────────────────────── tiện ích ─────────────────────────

        public static double Val(IDictionary<string, double> d, string key)
        {
            double v;
            return d != null && key != null && d.TryGetValue(key, out v) ? v : double.NaN;
        }

        /// <summary>Trung bình góc theo sin/cos, độ, (−180, 180] — không gãy khi các lần đo nằm hai bên ±180°.</summary>
        public static double MeanAngleDeg(double sumSin, double sumCos)
        {
            return Math.Atan2(sumSin, sumCos) * 180.0 / Math.PI;
        }

        private static void CheckEcho(VmCallResult r, string name, double expect, double tol, List<string> issues)
        {
            if (!r.Has(name)) { issues.Add(Loc.F("VmCycle.MissingOutput", name)); return; }
            double got = r.Get(name);
            if (!VmMath.Near(got, expect, tol)) issues.Add(name + " = " + Fmt(got) + " ≠ " + Fmt(expect));
        }

        private static VmPlaceCommand Fail(VmPlaceCommand c, string why) { c.Route = EVmRoute.Error; c.Why = why; return c; }
        private static VmPlaceCommand Skip(VmPlaceCommand c, string why) { c.Route = EVmRoute.Skip; c.Why = why; return c; }
        private static VmPlaceCommand Ng(VmPlaceCommand c, string why) { c.Route = EVmRoute.Ng; c.Why = why; return c; }

        private static string Join(List<string> parts) { return parts.Count == 0 ? null : string.Join("; ", parts.ToArray()); }

        public static string Fmt(double v)
        {
            if (!VmMath.IsFinite(v)) return double.IsNaN(v) ? Loc.T("VmCycle.NaNMissing") : v.ToString(CultureInfo.InvariantCulture);
            return v.ToString("0.###", CultureInfo.InvariantCulture);
        }

        private static string Fmt(int v) { return v == int.MinValue ? Loc.T("VmCycle.Missing") : v.ToString(CultureInfo.InvariantCulture); }
    }
}
