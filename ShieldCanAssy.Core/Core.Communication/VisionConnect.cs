using System;
using System.Collections.Generic;
using System.Drawing;
using System.Threading;
using System.Threading.Tasks;
using ShieldCanAssy.Core.Data;
using ShieldCanAssy.Core.Infor;
using ShieldCanAssy.Core.Lang;
using ShieldCanAssy.Lib.VisionMaster;
using ShieldCanAssy.Utility;
using ShieldCanAssy.Utility.Log;
using VM.PlatformSDKCS;

namespace ShieldCanAssy.Core.Communication
{
    /// <summary>
    /// CAM bottom-up + CAM top-down thật, qua HIKROBOT VisionMaster 4.4.0 — bọc
    /// <see cref="VisionMasterDevice"/>. PLAN.md §2.1, §6.
    ///
    /// <para><b>Chưa kiểm chứng với solution thật.</b> Viết trước để khi có file .solw thật chỉ
    /// còn sửa tên Flow/biến output ở trang Setting, không phải vừa gõ code vừa đứng cạnh máy —
    /// giống <see cref="PanasonicAxis"/>. Tên Flow/output đọc từ
    /// <see cref="SystemData.VisionOutputs"/> (cấu hình được, không hardcode) — đó cũng là hợp
    /// đồng Tuấn cần khớp khi dựng solution trong VisionMaster IDE (PLAN.md §6.8).</para>
    /// </summary>
    public class VisionConnect : Bindable, IVisionDevice
    {
        private readonly SystemData _sys;
        private readonly VisionMasterDevice _device = new VisionMasterDevice();
        private readonly object _cmdLock = new object();

        public VisionConnect(SystemData sys)
        {
            _sys = sys;
        }

        private bool _connected;
        public bool IsConnected { get { return _connected; } private set { SetProperty(ref _connected, value); } }

        public bool IsSolutionLoaded { get { return _device.IsSolutionLoaded; } }

        private Bitmap _lastTopDownImage;
        public Bitmap LastTopDownImage { get { return _lastTopDownImage; } private set { SetProperty(ref _lastTopDownImage, value); } }

        private Bitmap _lastBottomUpImageTool1, _lastBottomUpImageTool2;
        public Bitmap LastBottomUpImageTool1 { get { return _lastBottomUpImageTool1; } private set { SetProperty(ref _lastBottomUpImageTool1, value); } }
        public Bitmap LastBottomUpImageTool2 { get { return _lastBottomUpImageTool2; } private set { SetProperty(ref _lastBottomUpImageTool2, value); } }

        /// <summary>Cất ảnh CAM bottom-up vào đúng tay: Flow của tay #2 thì tay #2, còn lại tay #1.</summary>
        private void SetBottomUpImage(string flow, Bitmap image)
        {
            var n = _sys.VisionOutputs;
            if (flow == n.FlowAlignBottomTool2) LastBottomUpImageTool2 = image;
            else LastBottomUpImageTool1 = image;
        }

        /// <summary>
        /// PlatformSDK không có bước "connect" tách rời khỏi nạp solution — kết nối camera thật
        /// diễn ra bên trong <c>LoadSolution</c>. Cờ này chỉ đánh dấu "sẵn sàng gọi nạp solution",
        /// đúng thứ tự <c>McControl.Init()</c> đang gọi: <c>Connect()</c> rồi <c>LoadSolutionAsync()</c>.
        /// </summary>
        public bool Connect()
        {
            IsConnected = true;
            return true;
        }

        public void Disconnect()
        {
            try { _device.Shutdown(); } catch { }
            IsConnected = false;
        }

        public async Task<bool> LoadSolutionAsync(string solutionPath)
        {
            try
            {
                await Task.Run(() =>
                {
                    lock (_cmdLock) { _device.LoadSolution(solutionPath, ""); }
                }).ConfigureAwait(false);
                return true;
            }
            catch (Exception ex)
            {
                LogWorker.Instance.RecordLog("Vision: nạp solution lỗi — " + ex.Message,
                    Utility.Log.LogLevel.Error, "Vision");
                return false;
            }
        }

        /// <summary>
        /// Kích Flow hiệu chuẩn kèm vị trí trục thật (§6.2, §6.4). Ghi biến toàn cục rồi Run trong
        /// cùng một lần gọi — xem <see cref="IVisionDevice.TriggerCalibAsync"/>.
        /// </summary>
        public Task<VisionPose> TriggerCalibAsync(EVisionProcedure procedure, AxisSnapshot axes, CancellationToken ct)
        {
            if (axes == null) return Task.FromResult(VisionPose.NotFound(Loc.T("Vm.NoCalibAxes")));
            return TriggerCoreAsync(procedure, axes, ct);
        }

        /// <summary>Hiệu chuẩn chạy trên chính Flow soi của camera đó, nhánh IsTriggerLocate = 0 (Tuấn 2026-10-08).</summary>
        private string FlowNameOf(EVisionProcedure procedure)
        {
            var names = _sys.VisionOutputs;
            switch (procedure)
            {
                case EVisionProcedure.AlignCalibTool1: return names.FlowAlignBottomTool1;
                case EVisionProcedure.AlignCalibTool2: return names.FlowAlignBottomTool2;
                default: return names.FlowLocateTop;
            }
        }

        /// <summary>Vị trí trục → tên biến toàn cục khai ở trang Setting. Tên để trống thì bỏ qua
        /// trục đó — Flow nạp điểm không nhất thiết cần cả bốn trục.</summary>
        private Dictionary<string, double> AxisVars(AxisSnapshot axes)
        {
            var names = _sys.VisionOutputs;
            var vars = new Dictionary<string, double>();
            if (!string.IsNullOrEmpty(names.GvAxisX)) vars[names.GvAxisX] = axes.X;
            if (!string.IsNullOrEmpty(names.GvAxisY2)) vars[names.GvAxisY2] = axes.Y2;
            if (!string.IsNullOrEmpty(names.GvAxisA1)) vars[names.GvAxisA1] = axes.A1;
            if (!string.IsNullOrEmpty(names.GvAxisA2)) vars[names.GvAxisA2] = axes.A2;
            return vars;
        }

        private async Task<VisionPose> TriggerCoreAsync(EVisionProcedure procedure, AxisSnapshot axes, CancellationToken ct)
        {
            if (!IsConnected) return VisionPose.NotFound(Loc.T("Vm.NotConnected"));
            if (!IsSolutionLoaded) return VisionPose.NotFound(Loc.T("Vm.NoSolution"));

            var names = _sys.VisionOutputs;
            string flow = FlowNameOf(procedure);

            try
            {
                var vars = AxisVars(axes);
                // Flow soi và nạp điểm hiệu chuẩn là một, rẽ nhánh theo cờ này (Tuấn 2026-10-08) — lần soi ghi 1 ở
                // VmCycle.LocateRequest / AlignBottomRequest.
                vars[VmVars.IsTriggerLocate] = 0;
                var result = await RunWithTimeoutAsync(() =>
                    {
                        // Ghi trước, chạy sau, trong cùng một lượt giữ khoá lệnh của thiết bị.
                        _device.SetGlobalFloats(vars, names.GlobalVarModule);
                        return _device.RunAndGetFloatsWithImage(flow, names.OutImage, names.OutFound);
                    },
                    ct).ConfigureAwait(false);

                if (result == null)
                {
                    if (ct.IsCancellationRequested) return VisionPose.NotFound(Loc.T("Vm.Cancelled"));
                    AlarmManager.RaiseEvent(AlarmCode.VisionTriggerTimeout, "Vision", flow);
                    return VisionPose.NotFound(Loc.T("Vm.Timeout"));
                }

                // Hiệu chuẩn tâm xoay là CAM bottom-up, hiệu chuẩn top-down là CAM top-down.
                bool isBottomUp = procedure == EVisionProcedure.AlignCalibTool1 || procedure == EVisionProcedure.AlignCalibTool2;
                if (isBottomUp) SetBottomUpImage(flow, result.Image);
                else LastTopDownImage = result.Image;

                // Flow nạp điểm chỉ cần báo thấy hay không — module hiệu chuẩn tự giữ điểm vừa nạp.
                return result.Floats[0] > 0.5f ? new VisionPose { Found = true } : VisionPose.NotFound(Loc.T("Vm.SolutionNotFound"));
            }
            catch (VmException ex)
            {
                LogWorker.Instance.RecordLog("Vision: " + flow + " lỗi — " + ex.errorMessage,
                    Utility.Log.LogLevel.Error, "Vision");
                return VisionPose.NotFound(ex.errorMessage);
            }
            catch (Exception ex)
            {
                LogWorker.Instance.RecordLog("Vision: " + flow + " lỗi — " + ex.Message,
                    Utility.Log.LogLevel.Error, "Vision");
                return VisionPose.NotFound(ex.Message);
            }
        }

        /// <summary>Xoá điểm cũ của module hiệu chuẩn — xem <see cref="IVisionDevice.ClearCalibPointsAsync"/>.</summary>
        public async Task<string> ClearCalibPointsAsync(EVisionProcedure procedure)
        {
            if (!IsConnected) return Loc.T("Vm.NotConnected");
            if (!IsSolutionLoaded) return Loc.T("Vm.NoSolution");

            string flow = FlowNameOf(procedure);
            try
            {
                var modules = await Task.Run(() => { lock (_cmdLock) { return _device.ClearCalibPoints(flow); } }).ConfigureAwait(false);
                LogWorker.Instance.RecordLog("Vision: " + flow + " — đã xoá điểm hiệu chuẩn cũ của module " + string.Join(", ", modules),
                    Utility.Log.LogLevel.Info, "Vision");
                return null;
            }
            catch (VmException ex) { return ex.errorMessage; }
            catch (Exception ex) { return ex.Message; }
        }

        public async Task<GlobalVarSyncReport> SyncGlobalVariablesAsync(IList<GlobalVarSpec> specs, bool apply)
        {
            if (!IsSolutionLoaded) return GlobalVarSyncReport.Fail(Loc.T("Vm.NoVisionSolution"));
            string hint = _sys.VisionOutputs.GlobalVarModule;
            try
            {
                // Không canh giờ như Run(): lưu solution có thể mất vài giây, và bỏ dở giữa chừng tệ
                // hơn chờ. Vẫn giữ khoá lệnh để không lọt lần kích Flow nào vào giữa.
                return await Task.Run(() =>
                {
                    lock (_cmdLock) { return GlobalVarSync.Run(new VmGlobalVarStore(_device, hint), specs, apply); }
                }).ConfigureAwait(false);
            }
            catch (Exception ex)
            {
                return GlobalVarSyncReport.Fail(ex.Message);
            }
        }

        /// <summary>Đọc Output Params của một Flow — xem <see cref="IVisionDevice.ReadFlowOutputsAsync"/>.
        /// Không canh giờ như <c>Run()</c>: đây chỉ là đọc phần khai báo, không kích camera.</summary>
        public async Task<VmFlowOutputs> ReadFlowOutputsAsync(string flow)
        {
            if (string.IsNullOrEmpty(flow)) return VmFlowOutputs.Fail(flow, Loc.T("Vm.FlowNameNotSet"));
            if (!IsConnected) return VmFlowOutputs.Fail(flow, Loc.T("Vm.NotConnected"));
            if (!IsSolutionLoaded) return VmFlowOutputs.Fail(flow, Loc.T("Vm.NoSolution"));

            try
            {
                var io = await Task.Run(() => { lock (_cmdLock) { return _device.GetFlowIo(flow); } }).ConfigureAwait(false);

                var r = new VmFlowOutputs { Flow = flow, InputParamCount = io.InputCount };
                foreach (var kv in io.Outputs)
                    r.Declared.Add(new VmDeclaredOut
                    {
                        Name = kv.Key,
                        TypeCode = kv.Value,
                        TypeText = VmOutCheck.TypeName(kv.Value),
                        Numeric = VmOutCheck.IsNumeric(kv.Value)
                    });

                foreach (var m in io.Modules)
                {
                    var mi = new VmModuleIo { Name = m.Name };
                    foreach (var i in m.Inputs) mi.Inputs.Add(new VmIoInfo { Name = i.Name, TypeText = i.TypeText, Source = i.Source });
                    foreach (var o in m.Outputs) mi.Outputs.Add(new VmIoInfo { Name = o.Name, TypeText = o.TypeText, Source = o.Source });
                    r.Modules.Add(mi);
                }
                return r;
            }
            catch (VmException ex) { return VmFlowOutputs.Fail(flow, ex.errorMessage); }
            catch (Exception ex) { return VmFlowOutputs.Fail(flow, ex.Message); }
        }

        /// <summary>Gọi một Flow — xem <see cref="IVisionDevice.CallFlowAsync"/>.</summary>
        public async Task<VmCallResult> CallFlowAsync(VmCallRequest request, CancellationToken ct)
        {
            if (request == null || string.IsNullOrEmpty(request.Flow)) return VmCallResult.Fail(Loc.T("Vm.FlowNameMissing"));
            if (!IsConnected) return VmCallResult.Fail(Loc.T("Vm.NotConnected"));
            if (!IsSolutionLoaded) return VmCallResult.Fail(Loc.T("Vm.NoSolution"));

            var names = _sys.VisionOutputs;
            try
            {
                var r = await RunWithTimeoutAsync(() =>
                    {
                        // Ghi biến, chạy, đọc ngay — cùng một lượt giữ khoá lệnh của thiết bị.
                        _device.SetGlobalFloatsStrict(request.Globals, names.GlobalVarModule);
                        return _device.RunAndRead(request.Flow,
                            request.Camera == EVmCamera.None ? null : names.OutImage,
                            request.Outputs, request.OptionalOutputs);
                    },
                    ct).ConfigureAwait(false);

                if (r == null)
                {
                    // Huỷ (EMG, rèm, STOP huỷ chu trình) giữa lúc Flow chạy không phải quá giờ — không alarm. Quá giờ thật
                    // thì phát dạng SỰ KIỆN cho RESET gỡ được: Raise trơn chỉ được gỡ bởi một lần soi Auto thành công, mà
                    // còn alarm Error thì Auto không chạy được — 13/09 máy kẹt LỖI, RESET 13 lần, phải tắt App.
                    if (ct.IsCancellationRequested) return VmCallResult.Fail(Loc.F("Vm.CancelledInFlow", request.Flow));
                    AlarmManager.RaiseEvent(AlarmCode.VisionTriggerTimeout, "Vision", request.Flow);
                    return VmCallResult.Fail(Loc.F("Vm.FlowTimeout", request.Flow), true);
                }

                if (request.Camera == EVmCamera.BottomUp) SetBottomUpImage(request.Flow, r.Image);
                else if (request.Camera == EVmCamera.TopDown) LastTopDownImage = r.Image;

                var result = new VmCallResult { Ok = true };
                foreach (var kv in r.Values) result.Values[kv.Key] = kv.Value;
                result.MissingOptional.AddRange(r.Missing);
                return result;
            }
            catch (VmException ex)
            {
                LogWorker.Instance.RecordLog("Vision: " + request.Flow + " lỗi — " + ex.errorMessage,
                    Utility.Log.LogLevel.Error, "Vision");
                return VmCallResult.Fail(request.Flow + ": " + ex.errorMessage);
            }
            catch (Exception ex)
            {
                LogWorker.Instance.RecordLog("Vision: " + request.Flow + " lỗi — " + ex.Message,
                    Utility.Log.LogLevel.Error, "Vision");
                return VmCallResult.Fail(request.Flow + ": " + ex.Message);
            }
        }

        public async Task<string> SaveSolutionAsync()
        {
            if (!IsSolutionLoaded) return Loc.T("Vm.NoSolution");
            try
            {
                await Task.Run(() => { lock (_cmdLock) { _device.SaveSolution(); } }).ConfigureAwait(false);
                return null;
            }
            catch (VmException ex) { return ex.errorMessage; }
            catch (Exception ex) { return ex.Message; }
        }

        private sealed class VmGlobalVarStore : IGlobalVarStore
        {
            private readonly VisionMasterDevice _dev;
            private readonly string _hint;
            public VmGlobalVarStore(VisionMasterDevice dev, string hint) { _dev = dev; _hint = hint; }
            public IDictionary<string, string> ReadAll() { return _dev.GetGlobalVarTypes(_hint); }
            public void Add(string name, string type, string remark) { _dev.AddGlobalVar(name, type, remark); }
            public void SetValue(string name, string type, string invariantValue) { _dev.SetGlobalVarValue(_hint, name, type, invariantValue); }
            public string Save() { return _dev.SaveSolution(); }
        }

        /// <summary>
        /// <c>VmProcedure.Run()</c> là lệnh CHẶN, không nhận token huỷ — bọc trong
        /// <c>Task.Run</c> rồi canh thời gian bằng <c>Task.WhenAny</c>. Hết giờ thì trả về
        /// <c>null</c> cho lớp gọi báo alarm 4003, nhưng luồng chạy <c>Run()</c> bên dưới KHÔNG bị
        /// huỷ thật — SDK không cho — nó tự trôi tới khi solution phản hồi. Cùng một giới hạn với
        /// timeout đọc trạng thái driver Panasonic.
        /// </summary>
        private async Task<T> RunWithTimeoutAsync<T>(Func<T> body, CancellationToken ct) where T : class
        {
            int abandoned = 0;
            var runTask = Task.Run(() =>
            {
                lock (_cmdLock)
                {
                    // Lần gọi đã bị bỏ (quá giờ/huỷ) trong lúc còn xếp hàng chờ khoá — ví dụ sau một Run treo — thì không
                    // chạy muộn nữa: chạy muộn là ghi biến cũ, và Flow hiệu chuẩn nạp thêm một điểm sai.
                    if (Volatile.Read(ref abandoned) == 1) return null;
                    return body();
                }
            });
            var timeoutTask = Task.Delay(_sys.VisionTriggerTimeoutMs, ct);
            var done = await Task.WhenAny(runTask, timeoutTask).ConfigureAwait(false);
            if (done == runTask) return await runTask.ConfigureAwait(false);
            Interlocked.Exchange(ref abandoned, 1);
            return null;
        }
    }
}
