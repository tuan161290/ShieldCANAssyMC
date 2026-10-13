using System;
using System.Collections.Generic;
using System.Globalization;
using System.Threading;
using System.Threading.Tasks;
using ShieldCanAssy.Core.Controller;
using ShieldCanAssy.Core.Data;
using ShieldCanAssy.Core.Data.Model;
using ShieldCanAssy.Core.Lang;
using ShieldCanAssy.Utility;

namespace ShieldCanAssy.Core.Communication.Sim
{
    /// <summary>
    /// Vision mô phỏng. Trả về tư thế mm/độ có nhiễu ngẫu nhiên, kèm xác suất không tìm thấy để
    /// chạy được nhánh NG của chuỗi Auto (PLAN.md §5.4).
    ///
    /// <para><b>Đơn vị là mm ở MỌI Flow</b> (Tuấn chốt 2026-09-12): solution thật kết thúc bằng
    /// module Calibration Transformation nên không Flow nào còn trả pixel, mô phỏng phải giống
    /// đúng như vậy — nếu không, <c>--selftest</c> sẽ khoá lại một hợp đồng không tồn tại.</para>
    ///
    /// <para><b>Bản sao Script</b> (<see cref="CallFlowAsync"/>): mỗi Flow chạy đúng phép tính của Script
    /// trong cẩm nang trên kho biến toàn cục giả và Input Params, nên <c>--selftest</c> kiểm được App đối chiếu,
    /// chặn và điều hướng đúng với cùng những con số solution thật sẽ trả. Bản rút gọn 2026-09-13: 11 biến toàn cục,
    /// AlignBottom_ToolN tính luôn lệnh đặt, không còn PlaceAlign/TopDownVerify/CalibStatus.</para>
    /// </summary>
    public class FakeVision : Bindable, IVisionDevice
    {
        private static readonly CultureInfo Inv = CultureInfo.InvariantCulture;
        private readonly Random _rng = new Random(20260731);

        private bool _connected;
        public bool IsConnected { get => _connected; private set => SetProperty(ref _connected, value); }

        private bool _loaded;
        public bool IsSolutionLoaded { get => _loaded; private set => SetProperty(ref _loaded, value); }

        /// <summary>Thời gian chụp + xử lý giả lập, ms.</summary>
        public int ProcessTimeMs { get; set; } = 120;

        /// <summary>Xác suất tìm thấy đối tượng.</summary>
        public double FoundRate { get; set; } = 1.0;

        /// <summary>Xác suất riêng của CAM bottom-up (AlignBottom_ToolN), nhân thêm với <see cref="FoundRate"/> — để bài test làm
        /// hỏng lần soi sản phẩm mà LocateTop vẫn quét được ô đích.</summary>
        public double BottomFoundRate { get; set; } = 1.0;

        /// <summary>Độ lệch tối đa mô phỏng của CAM top-down quanh tâm khung hình, mm.</summary>
        public double NoiseTopMm { get; set; } = 0.4;

        /// <summary>Độ lệch góc tối đa mô phỏng, độ.</summary>
        public double NoiseAngle { get; set; } = 4.0;

        /// <summary>Nhiễu đo của CAM bottom-up, mm.</summary>
        public double NoiseAlignMm { get; set; } = 0.05;

        /// <summary>
        /// Độ lệch tâm mô phỏng: sản phẩm KHÔNG bao giờ nằm đúng tâm xoay, nên Flow AlignBottom
        /// phải trả về một vector khác 0. Nhờ vậy chuỗi Auto mô phỏng thật sự đi qua phần "xoay
        /// làm tâm sản phẩm dịch đi" (§6.6) thay vì luôn nhận lượng bù bằng 0 — bỏ số này về 0 là
        /// mất luôn khả năng phát hiện lỗi ở đúng chỗ dễ sai nhất.
        /// </summary>
        public double SimEccentricityMm { get; set; } = 1.5;

        /// <summary>Số lần đã trigger từng Flow — để --selftest kiểm chứng những thay đổi kiểu
        /// "bớt chụp lại" (vd. S05_ScanAssy) thật sự bớt trigger chứ không chỉ vẫn ra đúng kết quả
        /// theo cách cũ (nhiều trigger hơn) mà tình cờ vẫn đạt.</summary>
        public int AlignBottomCount { get; private set; }
        public int LocateTopCount { get; private set; }

        /// <summary>Vị trí trục kèm theo lần nạp điểm hiệu chuẩn gần nhất — thay cho các biến toàn
        /// cục của solution thật, để <c>--selftest</c> kiểm được App có gửi đúng toạ độ ENCODER hay
        /// không. Null nghĩa là chưa có lần nạp điểm nào.</summary>
        public AxisSnapshot LastCalibAxes { get; private set; }

        /// <summary>Số lần đã nạp điểm hiệu chuẩn (mọi Flow calib).</summary>
        public int CalibPointCount { get; private set; }

        /// <summary>Số lần đã xoá điểm hiệu chuẩn, phép của lần xoá gần nhất, và số điểm nạp từ lần xoá đó — để
        /// <c>--selftest</c> kiểm chuỗi nạp điểm xoá trước rồi mới nạp.</summary>
        public int CalibClearCount { get; private set; }
        public EVisionProcedure? LastCalibClear { get; private set; }
        public int CalibPointsSinceClear { get; private set; }

        /// <summary><c>--selftest</c> bật để giả module hiệu chuẩn báo lỗi khi xoá điểm.</summary>
        public bool FailCalibClear { get; set; }

        // Mô phỏng không có camera thật để chụp — luôn null, trang Main tự hiện chỗ trống thay
        // vì hình khi chạy --selftest/dev không có máy.
        public System.Drawing.Bitmap LastTopDownImage => null;
        public System.Drawing.Bitmap LastBottomUpImageTool1 => null;
        public System.Drawing.Bitmap LastBottomUpImageTool2 => null;

        public bool Connect() { IsConnected = true; return true; }

        public void Disconnect() { IsConnected = false; IsSolutionLoaded = false; }

        public async Task<bool> LoadSolutionAsync(string solutionPath)
        {
            await Task.Delay(200).ConfigureAwait(false);
            IsSolutionLoaded = IsConnected;
            return IsSolutionLoaded;
        }

        /// <summary>
        /// Mô phỏng nạp một điểm hiệu chuẩn: ghi lại vị trí trục "gửi sang VisionMaster" rồi báo thấy / không
        /// thấy theo <see cref="FoundRate"/>. Solution thật nhận toạ độ này qua biến toàn cục; ở đây chỉ giữ lại
        /// để <c>--selftest</c> đối chiếu.
        /// </summary>
        public async Task<VisionPose> TriggerCalibAsync(EVisionProcedure procedure, AxisSnapshot axes, CancellationToken ct)
        {
            if (axes == null) return VisionPose.NotFound(Loc.T("Vm.NoCalibAxes"));
            if (!IsConnected) return VisionPose.NotFound(Loc.T("Vm.NotConnected"));
            if (!IsSolutionLoaded) return VisionPose.NotFound(Loc.T("Vm.NoSolution"));

            LastCalibAxes = axes;
            CalibPointCount++;
            CalibPointsSinceClear++;
            // Như VisionConnect.TriggerCoreAsync: nhánh hiệu chuẩn của Flow (ba Flow, 2026-10-08).
            lock (GlobalVars) SetG(VmVars.IsTriggerLocate, 0);
            AfterFlow?.Invoke(procedure.ToString());   // --selftest soi trạng thái máy NGAY LÚC chụp (đèn CAM)
            await Task.Delay(ProcessTimeMs, ct).ConfigureAwait(false);
            return _rng.NextDouble() <= FoundRate ? new VisionPose { Found = true } : VisionPose.NotFound(Loc.T("Vm.SimObjectNotFound"));
        }

        public Task<string> ClearCalibPointsAsync(EVisionProcedure procedure)
        {
            if (!IsSolutionLoaded) return Task.FromResult(Loc.T("Vm.NoSolution"));
            if (FailCalibClear) return Task.FromResult(Loc.T("Vm.SimClearFail"));
            CalibClearCount++;
            LastCalibClear = procedure;
            CalibPointsSinceClear = 0;
            return Task.FromResult<string>(null);
        }

        /// <summary>Kho biến toàn cục giả: tên → (kiểu, giá trị InvariantCulture). <c>--selftest</c>
        /// dựng sẵn biến trong này để kiểm luật "chỉ thêm, không sửa".</summary>
        public Dictionary<string, KeyValuePair<string, string>> GlobalVars { get; } =
            new Dictionary<string, KeyValuePair<string, string>>(StringComparer.Ordinal);

        /// <summary>Số lần đã "lưu solution".</summary>
        public int SolutionSaveCount { get; private set; }

        public Task<GlobalVarSyncReport> SyncGlobalVariablesAsync(IList<GlobalVarSpec> specs, bool apply)
        {
            if (!IsSolutionLoaded) return Task.FromResult(GlobalVarSyncReport.Fail(Loc.T("Vm.NoSolution")));
            lock (GlobalVars) return Task.FromResult(GlobalVarSync.Run(new FakeGlobalVarStore(this), specs, apply));
        }


        /// <summary>Nguồn bản khai output khi mô phỏng — McControl gắn vào <c>VmCycle.FakeFlowOutputs</c>;
        /// <c>--selftest</c> thay bằng bản có lỗi để kiểm tool báo đúng chỗ.</summary>
        public Func<string, VmFlowOutputs> FlowOutputSource { get; set; }

        public Task<VmFlowOutputs> ReadFlowOutputsAsync(string flow)
        {
            if (!IsSolutionLoaded) return Task.FromResult(VmFlowOutputs.Fail(flow, Loc.T("Vm.NoSolution")));
            if (FlowOutputSource == null)
                return Task.FromResult(VmFlowOutputs.Fail(flow, Loc.T("Vm.SimNoOutputSource")));
            return Task.FromResult(FlowOutputSource(flow));
        }

        public Task<string> SaveSolutionAsync()
        {
            if (!IsSolutionLoaded) return Task.FromResult(Loc.T("Vm.NoSolution"));
            SolutionSaveCount++;
            return Task.FromResult<string>(null);
        }

        private sealed class FakeGlobalVarStore : IGlobalVarStore
        {
            private readonly FakeVision _v;
            public FakeGlobalVarStore(FakeVision v) { _v = v; }

            public IDictionary<string, string> ReadAll()
            {
                var d = new Dictionary<string, string>(StringComparer.Ordinal);
                foreach (var kv in _v.GlobalVars) d[kv.Key] = kv.Value.Key;
                return d;
            }

            public void Add(string name, string type, string remark)
            {
                if (_v.GlobalVars.ContainsKey(name)) throw new InvalidOperationException("name is already exit");
                _v.GlobalVars[name] = new KeyValuePair<string, string>(type, type == "string" ? "" : "0");
            }

            public void SetValue(string name, string type, string invariantValue)
            {
                if (!_v.GlobalVars.ContainsKey(name)) throw new InvalidOperationException("name is not exit");
                _v.GlobalVars[name] = new KeyValuePair<string, string>(_v.GlobalVars[name].Key, invariantValue ?? "");
            }

            public string Save() { _v.SolutionSaveCount++; return ""; }
        }

        private double Noise(double amplitude)
        {
            return (_rng.NextDouble() - 0.5) * 2 * amplitude;
        }

        // ═════════════════════════ bản sao Script của solution ═════════════════════════

        /// <summary>Tên Flow/biến trục đang cấu hình — McControl gắn vào <c>SystemData.VisionOutputs</c>.</summary>
        public Func<VisionOutputNames> Names { get; set; } = () => new VisionOutputNames();

        /// <summary>Ghi ngày Bước 2 vào bản ghi hiệu chuẩn — như máy đã chạy xong Bước 2 (model mô phỏng có sẵn Bước 4, 5).
        /// Chỉ dùng khi mô phỏng.</summary>
        public void StampCalibrated(VisionCalib calib)
        {
            if (calib != null) calib.TopDownVerifiedAt = DateTime.Now;
        }

        /// <summary>Bật quang học giả: LocateTop thấy dấu chuẩn cố định ở (<see cref="MarkX"/>,
        /// <see cref="MarkY"/>), AlignBottom thấy sản phẩm xoay theo trục A. Tắt thì lượng dịch khớp
        /// mẫu chỉ là nhiễu quanh 0, không gắn với trục — TestRoutineCalib dựa vào đó để thấy phép kiểm tra từ chối.</summary>
        public bool SimulateOptics { get; set; }
        public double MarkX { get; set; }
        public double MarkY { get; set; }
        /// <summary>Góc LocateTop báo cho dấu chuẩn khi bật quang học giả — mốc 0° của mẫu khớp camera trên
        /// không trùng mốc của camera dưới, bài hand-eye đo mốc góc dựa vào đó.</summary>
        public double MarkA { get; set; }

        /// <summary>Chiều quay của ảnh bottom-up so với trục A khi bật quang học giả (±1).</summary>
        public int BottomAngleSign { get; set; } = 1;

        /// <summary>Tư thế sản phẩm ép cho AlignBottom (X, Y, A); null thì theo quang học giả hoặc nhiễu — TestVmTwinMath
        /// dựng đáp số từ đây.</summary>
        public double[] ForcedObjPose { get; set; }

        /// <summary>Số lần AlignBottom_ToolN được gọi kèm ô đích (tính lệnh đặt).</summary>
        public int PlaceComputeCount { get; private set; }

        /// <summary>Biến toàn cục của lần AlignBottom_ToolN kèm ô đích gần nhất.</summary>
        public Dictionary<string, double> LastPlaceGlobals { get; private set; }

        /// <summary>Thứ tự các Flow đã gọi (tối đa 5000 lần gần nhất).</summary>
        private readonly List<string> _flowHistory = new List<string>();
        public List<string> FlowHistorySnapshot() { lock (GlobalVars) return new List<string>(_flowHistory); }
        public void ClearFlowHistory() { lock (GlobalVars) _flowHistory.Clear(); }
        public int VmCallCount { get; private set; }

        // ── gây lỗi có chủ ý cho --selftest ──
        /// <summary>Ép <c>Status</c> phần tính lệnh của AlignBottom_ToolN trong <see cref="ForcePlaceStatusTimes"/> lần tới.</summary>
        public int ForcePlaceStatus { get; set; }
        public int ForcePlaceStatusTimes { get; set; }
        /// <summary>Ép góc bù (CmdA đi theo) bằng số này trong <see cref="ForceDAngleTimes"/> lần tính lệnh tới — dựng nhánh N.</summary>
        public double ForceDAngleDeg { get; set; }
        public int ForceDAngleTimes { get; set; }
        /// <summary>Cộng vào đích và lệnh X trong <see cref="TargetShiftTimes"/> lần tính lệnh tới — dựng nhánh G.</summary>
        public double TargetShiftMm { get; set; }
        public int TargetShiftTimes { get; set; }
        /// <summary>Lần AlignBottom_ToolN kèm ô đích kế tiếp trả EchoRunId sai.</summary>
        public bool CorruptEchoOnce { get; set; }
        /// <summary>Gọi sau khi Flow chạy xong, trước khi App đọc output — để bài test chen một chuyển động vào.</summary>
        public Action<string> AfterFlow { get; set; }

        /// <summary>Dựng kho biến theo danh mục tạo biến (giá trị đầu lấy từ bản ghi hiệu chuẩn của model).</summary>
        public void SeedVmSolution(IList<GlobalVarSpec> specs)
        {
            lock (GlobalVars)
            {
                GlobalVars.Clear();
                if (specs != null)
                    foreach (var s in specs)
                        GlobalVars[s.Name] = new KeyValuePair<string, string>(s.Type, s.InitialValue ?? (s.Type == "string" ? "" : "0"));
            }
        }

        /// <summary>Đọc một biến giả (NaN nếu không có) — cho --selftest.</summary>
        public double GetGlobal(string name) { lock (GlobalVars) return G(name); }

        /// <summary>Ghi một biến giả giữ nguyên kiểu — cho --selftest.</summary>
        public void SetGlobal(string name, double value) { lock (GlobalVars) SetG(name, value); }

        public async Task<VmCallResult> CallFlowAsync(VmCallRequest req, CancellationToken ct)
        {
            if (req == null || string.IsNullOrEmpty(req.Flow)) return VmCallResult.Fail(Loc.T("Vm.FlowNameMissing"));
            if (!IsConnected) return VmCallResult.Fail(Loc.T("Vm.NotConnected"));
            if (!IsSolutionLoaded) return VmCallResult.Fail(Loc.T("Vm.NoSolution"));

            var n = Names == null ? new VisionOutputNames() : (Names() ?? new VisionOutputNames());
            int tool;
            string kind = FlowKind(n, req.Flow, out tool);
            if (kind == null) return VmCallResult.Fail(Loc.F("Vm.SimFlowNotFound", req.Flow));

            bool camera = true;   // cả ba Flow đều có camera
            await Task.Delay(camera ? ProcessTimeMs : 5, ct).ConfigureAwait(false);

            var o = new Dictionary<string, double>(StringComparer.Ordinal);
            lock (GlobalVars)
            {
                if (req.Globals != null)
                {
                    foreach (var kv in req.Globals)
                    {
                        if (string.IsNullOrEmpty(kv.Key)) return VmCallResult.Fail(Loc.T("Vm.SimVarNameEmpty"));
                        if (!GlobalVars.ContainsKey(kv.Key))
                            return VmCallResult.Fail(Loc.F("Vm.SimVarNotDeclared", kv.Key));
                        if (!Fin(kv.Value)) return VmCallResult.Fail(Loc.F("Vm.SimValueNotFinite", kv.Key));
                    }
                }

                if (req.Globals != null)
                    foreach (var kv in req.Globals) SetG(kv.Key, kv.Value);

                VmCallCount++;
                _flowHistory.Add(req.Flow);
                if (_flowHistory.Count > 5000) _flowHistory.RemoveAt(0);

                switch (kind)
                {
                    case "LocateTop": RunTopCamera(n, o); break;
                    case "AlignBottom": RunAlignBottom(n, o, tool); break;
                }
            }

            AfterFlow?.Invoke(req.Flow);

            var res = new VmCallResult { Ok = true };
            if (req.Outputs != null)
                foreach (var name in req.Outputs)
                {
                    double v;
                    if (!o.TryGetValue(name ?? "", out v))
                        return VmCallResult.Fail(Loc.F("Vm.SimFlowMissingOutput", req.Flow, name));
                    res.Values[name] = v;
                }
            if (req.OptionalOutputs != null)
                foreach (var name in req.OptionalOutputs)
                {
                    double v;
                    if (o.TryGetValue(name ?? "", out v)) res.Values[name] = v;
                    else res.MissingOptional.Add(name);
                }
            return res;
        }

        private static string FlowKind(VisionOutputNames n, string flow, out int tool)
        {
            tool = 0;
            if (flow == n.FlowLocateTop) return "LocateTop";
            for (int t = 1; t <= 2; t++)
            {
                tool = t;
                if (flow == n.FlowAlignBottom(t)) return "AlignBottom";
            }
            tool = 0;
            return null;
        }

        // ── Flow có camera ──

        private void RunTopCamera(VisionOutputNames n, Dictionary<string, double> o)
        {
            LocateTopCount++;

            double x = G(n.GvAxisX), y2 = G(n.GvAxisY2), rid = G(VmVars.RunId);
            o[VmOut.EchoRunId] = F32(rid);
            o[VmOut.EchoX] = F32(x);
            o[VmOut.EchoY2] = F32(y2);

            bool found = _rng.NextDouble() <= FoundRate;
            double px, py, pa;
            if (SimulateOptics)
            {
                // Dấu chuẩn (sản phẩm mẫu) đứng yên trong hệ máy: lượng dịch camera báo = vị trí dấu − vị trí trục.
                px = MarkX - x + Noise(0.002);
                py = MarkY - y2 + Noise(0.002);
                pa = MarkA + Noise(0.01);
            }
            else
            {
                px = Noise(NoiseTopMm);
                py = Noise(NoiseTopMm);
                pa = Noise(NoiseAngle);
            }

            o[n.OutFound] = found ? 1 : 0;
            o[VmOut.TopFound] = found ? 1 : 0;
            o[VmOut.TopX] = F32(found ? x + px : 0);
            o[VmOut.TopY] = F32(found ? y2 + py : 0);
            o[VmOut.TopA] = F32(found ? pa : 0);
            o[VmOut.TopRunId] = F32(rid);
        }

        /// <summary>
        /// AlignBottom_ToolN: soi sản phẩm; nếu biến <c>HasTarget</c> = 1 thì Group Cmd tính luôn lệnh đặt lên ô đích
        /// trong cùng lần chạy — bản sao Script "Cmd" của cẩm nang: đích = pose ô + hand-eye App vừa ghi, Single Point
        /// Alignment, nhân dấu góc, A tuyệt đối = góc lúc chụp + góc bù. Ngưỡng không nằm ở đây — App so.
        /// </summary>
        private void RunAlignBottom(VisionOutputNames n, Dictionary<string, double> o, int tool)
        {
            AlignBottomCount++;

            double x = G(n.GvAxisX), a = G(tool == 1 ? n.GvAxisA1 : n.GvAxisA2), rid = G(VmVars.RunId);
            float echoA = (float)a;
            o[VmOut.EchoRunId] = F32(rid);
            o[VmOut.EchoX] = F32(x);
            o[VmOut.EchoA] = echoA;

            bool found = _rng.NextDouble() <= Math.Min(FoundRate, BottomFoundRate);
            double ex = tool == 2 ? -SimEccentricityMm : SimEccentricityMm;
            double ey = tool == 2 ? SimEccentricityMm * 0.5 : -SimEccentricityMm * 0.5;
            double ox, oy, oa;
            if (ForcedObjPose != null && ForcedObjPose.Length >= 3)
            {
                ox = ForcedObjPose[0];
                oy = ForcedObjPose[1];
                oa = ForcedObjPose[2];
            }
            else if (SimulateOptics)
            {
                // Sản phẩm dính trên đầu hút, xoay cùng trục A quanh tâm xoay (gốc toạ độ của file bottom-up).
                double th = BottomAngleSign * a * Math.PI / 180.0;
                ox = ex * Math.Cos(th) - ey * Math.Sin(th) + Noise(0.002);
                oy = ex * Math.Sin(th) + ey * Math.Cos(th) + Noise(0.002);
                oa = N180(BottomAngleSign * a) + Noise(0.01);
            }
            else
            {
                ox = ex + Noise(NoiseAlignMm);
                oy = ey + Noise(NoiseAlignMm);
                oa = Noise(NoiseAngle);
            }

            o[n.OutFound] = found ? 1 : 0;
            o[VmOut.ObjFound] = found ? 1 : 0;
            o[VmOut.ObjX] = F32(found ? ox : 0);
            o[VmOut.ObjY] = F32(found ? oy : 0);
            o[VmOut.ObjA] = F32(found ? oa : 0);
            o[VmOut.ObjRunId] = F32(rid);

            if (!(G(VmVars.HasTarget) >= 0.5))
            {
                o[VmOut.Status] = 0;   // Group Cmd nghỉ — chỉ soi
                return;
            }

            // ── Group Cmd: module In đọc biến toàn cục, rồi Single Point Alignment, rồi Script Cmd ──
            PlaceComputeCount++;
            LastPlaceGlobals = new Dictionary<string, double>(StringComparer.Ordinal);
            foreach (var name in VmVars.Place) LastPlaceGlobals[name] = G(name);

            double heDx = G(VmVars.HandEyeDx), heDy = G(VmVars.HandEyeDy);
            double tarX = G(VmVars.CellX) + heDx, tarY = G(VmVars.CellY) + heDy, tarA = G(VmVars.CellA);
            double px = o[VmOut.ObjX], py = o[VmOut.ObjY], pa = o[VmOut.ObjA], aCap = echoA;

            double dx, dy, dt;
            SinglePointAlignMath.Solve(tarX, tarY, tarA, px, py, pa, out dx, out dy, out dt);
            dt = N180(dt);

            int s = Gi(VmVars.AlignSign);
            int st = 1;
            if (s != 1 && s != -1) st = 6;
            else if (!(found && Fin(dt) && Fin(dx) && Fin(dy) && Fin(aCap) && Fin(px) && Fin(py))) st = 6;

            double dA = Fin(dt) && (s == 1 || s == -1) ? N180(s * dt) : 0;
            if (ForceDAngleTimes > 0) { ForceDAngleTimes--; dA = ForceDAngleDeg; }
            double ecc = Math.Sqrt(px * px + py * py);

            if (ForcePlaceStatusTimes > 0) { ForcePlaceStatusTimes--; st = ForcePlaceStatus; }
            double shift = 0;
            if (TargetShiftTimes > 0) { TargetShiftTimes--; shift = TargetShiftMm; }

            o[VmOut.Status] = st;
            o[VmOut.CmdX] = F32(dx + shift);
            o[VmOut.CmdY2] = F32(dy);
            o[VmOut.CmdA] = F32(W360(aCap + dA));
            o[VmOut.DiagDA] = F32(dA);
            o[VmOut.DiagEcc] = F32(ecc);
            o[VmOut.DiagTarX] = F32(tarX + shift);
            o[VmOut.DiagTarY] = F32(tarY);

            if (CorruptEchoOnce)
            {
                CorruptEchoOnce = false;
                o[VmOut.EchoRunId] = o[VmOut.EchoRunId] + 1;
            }
        }


        // ── kho biến (gọi trong lock GlobalVars) ──

        private static double In(Dictionary<string, double> inp, string name)
        {
            double v;
            return inp != null && inp.TryGetValue(name, out v) ? v : double.NaN;
        }

        private double G(string name)
        {
            KeyValuePair<string, string> kv;
            if (name == null || !GlobalVars.TryGetValue(name, out kv)) return double.NaN;
            double d;
            return double.TryParse(kv.Value, NumberStyles.Float, Inv, out d) ? d : double.NaN;
        }

        private int Gi(string name)
        {
            double d = G(name);
            return Fin(d) ? (int)Math.Round(d) : 0;
        }

        private void SetG(string name, double value)
        {
            KeyValuePair<string, string> kv;
            if (name == null || !GlobalVars.TryGetValue(name, out kv)) return;
            string text = kv.Key == "int"
                ? ((int)Math.Round(value)).ToString(Inv)
                : kv.Key == "string" ? value.ToString("R", Inv) : ((float)value).ToString("R", Inv);
            GlobalVars[name] = new KeyValuePair<string, string>(kv.Key, text);
        }

        private static double F32(double v) { return (float)v; }
        private static bool Fin(double v) { return !double.IsNaN(v) && !double.IsInfinity(v); }
        private static double N180(double a) { return a - 360.0 * Math.Ceiling((a - 180.0) / 360.0); }

        private static double W360(double a)
        {
            double r = a - 360.0 * Math.Floor(a / 360.0);
            return r >= 360.0 - 1e-9 ? 0 : r;
        }
    }
}
