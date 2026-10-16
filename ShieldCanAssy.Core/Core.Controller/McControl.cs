using System;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using ShieldCanAssy.Core.Communication;
using ShieldCanAssy.Core.Communication.Sim;
using ShieldCanAssy.Core.Data;
using ShieldCanAssy.Core.Infor;
using ShieldCanAssy.Core.Lang;
using ShieldCanAssy.Utility.Log;

namespace ShieldCanAssy.Core.Controller
{
    /// <summary>
    /// Vòng điều khiển chính và máy trạng thái — PLAN.md §3.3, §3.4.
    ///
    /// <para>Một thread nền duy nhất quét thiết bị, chạy SafetyMonitor, xử lý nút bấm và cập
    /// nhật đèn tháp với chu kỳ <c>ControlTickMs</c>. Mọi lệnh từ UI chỉ ĐẶT YÊU CẦU
    /// (<see cref="RequestStart"/>…) rồi trả về ngay — vòng quét mới là nơi quyết định làm gì,
    /// nên không có đường nào để UI phát lệnh chuyển động vòng qua interlock.</para>
    /// </summary>
    public static class McControl
    {
        public static McStatus Status { get; } = new McStatus();
        public static MotionConnect Motion { get; } = new MotionConnect();
        public static IIoDevice Io { get; private set; }
        public static IVisionDevice Vision { get; private set; }
        public static SafetyMonitor Safety { get; private set; }
        public static McFunction Func { get; private set; }
        public static Routine_Calib Calib { get; private set; }

        /// <summary>Chu trình Auto — trang Vision Edit đọc số quét ô đã đặt, --selftest đọc lệnh của lần đặt gần nhất.</summary>
        public static Routine_Auto AutoRoutine { get { return _auto; } }

        /// <summary>Chu trình chạy rà — trang Main đọc số nhịp và thời gian đã chạy.</summary>
        public static Routine_DryCycle DryRoutine { get { return _dry; } }

        /// <summary>Giây đã chạy của lượt Auto hiện tại — bộ đếm "cycle time hiện tại" ở trang Main.</summary>
        public static double CurrentCycleSec { get { return _auto?.CurrentCycleSec ?? 0; } }

        /// <summary>Bấm AUTO mà chưa đủ điều kiện vào sẵn sàng — máy ở lại MANUAL, kèm lý do để UI bật hộp thoại
        /// (Tuấn 2026-09-30). Bắn từ vòng quét (thread nền).</summary>
        public static event Action<string> AutoModeRefused;

        /// <summary>
        /// Chọn tay hút dùng trong Auto — 0 cả hai, 1 chỉ tay #1, 2 chỉ tay #2 (Tuấn 2026-09-29), nút ở trang Main. Lưu vào
        /// system.json. Không đổi được lúc đang chạy, giống chạy rà; lúc dừng (Paused) thì đổi được, vòng sau theo lựa chọn mới.
        /// </summary>
        public static void SetOnlyNozzle(int nozzle)
        {
            var sys = McDataManager.SysData;
            if (sys == null || sys.OnlyNozzle == nozzle) return;

            if (Status.State == EMcState.Running || Status.State == EMcState.Pausing || Status.State == EMcState.Held ||
                Status.State == EMcState.Homing || Status.State == EMcState.WaitOperator)
            {
                Status.Message = Loc.T("Mc.BusyNozzleChange");
                return;
            }

            sys.OnlyNozzle = nozzle == 1 || nozzle == 2 ? nozzle : 0;
            McDataManager.SaveSystemData();
            Status.Message = sys.OnlyNozzle == 0 ? Loc.T("Mc.BothNozzles") : Loc.F("Mc.OnlyNozzle", sys.OnlyNozzle);
            LogWorker.Instance.RecordLog(Status.Message, Utility.Log.LogLevel.Warning, "McControl");
        }

        /// <summary>
        /// Bật/tắt chế độ <b>chạy rà</b> (không sản phẩm, chỉ chạy trục) — nút gạt ở trang Main.
        ///
        /// <para>Chỉ CHỌN chế độ, KHÔNG chạy máy: bật xong vẫn phải chọn AUTO rồi bấm nút Start cứng như mọi lần
        /// — không có đường nào từ màn hình chạy máy (2026-08-20). Không đổi được trong lúc máy đang
        /// chạy — gạt giữa chừng thì nửa vòng sau sẽ là một chu trình khác hẳn nửa vòng trước.</para>
        /// </summary>
        public static void SetDryRun(bool on)
        {
            if (Status.DryRun == on) return;

            if (Status.State == EMcState.Running || Status.State == EMcState.Pausing || Status.State == EMcState.Held ||
                Status.State == EMcState.Homing || Status.State == EMcState.WaitOperator)
            {
                Status.Message = Loc.T("Mc.BusyDryRunChange");
                return;
            }

            if (on && (Func.HasProduct(1) || Func.HasProduct(2)))
            {
                Status.Message = Loc.T("Mc.DryRunNozzleHasProduct");
                return;
            }

            Status.DryRun = on;
            if (on) _dry.ResetCounters();
            Status.Message = on
                ? Loc.T("Mc.DryRunOn")
                : Loc.T("Mc.DryRunOff");
            LogWorker.Instance.RecordLog(Status.Message, Utility.Log.LogLevel.Warning, "McControl");
        }

        /// <summary>
        /// Bỏ mọi số đo đã cất (quét ô, tư thế trên tay) ở ranh giới bước kế tiếp — gọi sau khi teach lại
        /// lưới/mốc/vị trí soi, đổi model, hiệu chuẩn lại, về gốc hoặc nạp lại solution (thiết kế R7, R24).
        /// </summary>
        public static void InvalidateVisionCache() { _auto?.RequestVisionCacheInvalidation(); }

        /// <summary>Bỏ kết quả quét trước tray lắp ráp (S05_ScanAssy) — gọi mỗi khi UI nạp lại
        /// tray lắp ráp bằng tay, không chỉ khi Auto tự đổi tray, để không lỡ dùng số đo của
        /// tray vật lý cũ cho tray mới.</summary>
        public static void InvalidateAssyScan() { _auto?.InvalidateAssyScan(); }

        /// <summary>
        /// Nút "Nạp lại tray đầu vào" ở trang Main: operator xác nhận tray đầy hàng — mọi ô của Tray 1, và của Tray 2 nếu
        /// đã teach, thành có hàng. Không đọc cảm biến: nút này để sửa sổ bằng tay, bàn có thể đang không ở vị trí chờ.
        /// </summary>
        public static void ReloadInputTray()
        {
            McDataManager.RunData.LoadInputTrays(McDataManager.Model.UsableInputTrays);
            McDataManager.SaveRunningData();
        }

        /// <summary>
        /// Nút "Nạp lại tray lắp ráp" ở trang Main: mọi ô thành trống, bỏ kết quả quét trước của tray
        /// cũ, và cho chu trình chạy lại từ S00 ở ranh giới bước kế tiếp — quét tray mới; sản phẩm
        /// đang cầm (nếu có) được soi lại ở CAM bottom-up rồi mới đặt (Tuấn chốt 2026-09-13).
        /// </summary>
        public static void ResetAssyTray()
        {
            var run = McDataManager.RunData;
            run.AssyTray.ResetAll(ECellState.Empty);
            // Tray mới — đồng hồ thực tế đếm lại từ lần chạy kế tiếp với tray trong máy (Routine_Auto).
            run.AssyTrayStartedAt = null;
            InvalidateAssyScan();
            _auto?.RequestCycleRestart();

            bool holding = run.Nozzle1.HasProduct || run.Nozzle2.HasProduct ||
                           (Func != null && (Func.HasProduct(1) || Func.HasProduct(2)));
            if (holding)
            {
                Status.Message = Loc.T("Mc.AssyTrayReloadHolding");
                LogWorker.Instance.RecordLog(Status.Message, Utility.Log.LogLevel.Info, "McControl");
            }
            McDataManager.SaveRunningData();
        }

        /// <summary>
        /// Bấm một ô ở lưới tray trang Main lúc AUTO (Tuấn 2026-10-16): chọn hoặc bỏ chọn. Có ô được chọn thì chu trình Auto
        /// chỉ làm trên các ô đó, làm hết thì coi như xong tray — xem <see cref="TrayState.Selected"/>. Gọi được lúc máy đang
        /// chạy: có hiệu lực từ lần giao ô kế tiếp, ô đang làm dở không đổi. Chỉ chọn được ô còn việc — tray đầu vào ô có
        /// hàng, tray lắp ráp ô trống — để bấm nhầm một ô đã xong không thành "xong tray"; tray đã hết, đang chờ thay, thì ô
        /// nào cũng chọn được: chọn trước cho tray sắp đặt vào. Bỏ chọn lúc nào cũng được. Không ghi running.json ở đây —
        /// lựa chọn chỉ sống trong phiên (mở lại App là mất), chu trình ghi theo nhịp của nó.
        /// </summary>
        public static void ToggleCellSelection(bool input, int index)
        {
            if (Status.Mode != EMcMode.Auto) return;
            var tray = input ? McDataManager.RunData.InputTray : McDataManager.RunData.AssyTray;
            if (index < 0 || index >= tray.CellCount) return;
            if (!tray.IsSelected(index))
            {
                var open = input ? ECellState.Loaded : ECellState.Empty;
                bool trayDone = !tray.Cells.Contains(open) && !tray.Cells.Contains(ECellState.Processing);
                if (tray.Get(index) != open && !trayDone)
                {
                    Status.Message = Loc.T(input ? "Mc.InputCellNotSelectable" : "Mc.AssyCellNotSelectable");
                    return;
                }
            }
            tray.ToggleSelected(index);
        }

        /// <summary>Nút "Bỏ chọn" của card tray trang Main — chu trình lại làm cả tray.</summary>
        public static void ClearCellSelection(bool input)
        {
            (input ? McDataManager.RunData.InputTray : McDataManager.RunData.AssyTray).ClearSelection();
        }

        private static Routine_Origin _origin;
        private static Routine_Auto _auto;
        private static Routine_DryCycle _dry;
        private static Thread _loop;
        private static volatile bool _running;
        private static Timer _watchdog;
        private static long _lastTickMs;
        private static long _loopStartMs;

        private static Task _activeRoutine;
        private static readonly object _routineLock = new object();

        // Yêu cầu từ UI / nút cứng
        private static int _reqStart, _reqStop, _reqPause, _reqReset, _reqOrigin, _reqInit, _reqMute, _reqArm, _reqGo;

        /// <summary>Chế độ Auto/Manual đặt bằng phần mềm — máy không có công tắc chìa vật lý,
        /// chọn qua nút trên trang Main (xem RequestSetMode). Khác các _req* ở trên (cờ một
        /// lần rồi tự xoá): đây là trạng thái BỀN, vòng quét chỉ đọc ra so sánh với Status.Mode.</summary>
        private static volatile int _requestedMode = (int)EMcMode.Manual;

        private static readonly Stopwatch _clock = Stopwatch.StartNew();
        private static readonly Stopwatch _tickSw = new Stopwatch();

        public static bool IsInitialized { get; private set; }

        // ───────────────────────── khởi tạo ─────────────────────────

        public static void Init()
        {
            var sys = McDataManager.SysData;

            Status.Simulation = sys.SimulationMode;
            Status.ModelName = McDataManager.Model?.Name;   // null = "(chưa nạp)" theo ngôn ngữ, xem McStatus.ModelName
            Status.State = EMcState.Init;

            Motion.Init(sys);
            Motion.ZSafe = sys.ZSafe;

            Io = sys.SimulationMode ? (IIoDevice)new FakeIo() : new IoConnect(sys);
            Vision = sys.SimulationMode ? (IVisionDevice)new FakeVision() : new VisionConnect(sys);
            var fakeVision = Vision as FakeVision;
            if (fakeVision != null)
            {
                fakeVision.Names = () => McDataManager.SysData.VisionOutputs;
                fakeVision.FlowOutputSource = flow => VmCycle.FakeFlowOutputs(flow, McDataManager.SysData.VisionOutputs);
            }

            Safety = new SafetyMonitor(Motion, Status, () => McDataManager.SysData);
            Safety.AttachIo(Io);
            Safety.CurtainBroken = HoldForCurtain;

            Func = new McFunction(Motion, Safety, Status, () => McDataManager.SysData) { Io = Io };
            _origin = new Routine_Origin(Motion, Func, Status, () => McDataManager.SysData);
            _auto = new Routine_Auto(Motion, Func, Status, () => McDataManager.SysData, () => Vision);
            _dry = new Routine_DryCycle(Motion, Func, Status, () => McDataManager.SysData);
            Calib = new Routine_Calib(Motion, Func, () => McDataManager.SysData, () => Vision);

            Motion.ConnectAll();
            Io.Connect();
            Vision.Connect();
            LoadVisionSolution(sys);

            if (sys.SimulationMode) InstallSimulationModel();

            // Solution giả có đủ 12 biến, bản ghi hiệu chuẩn đánh dấu đã qua Bước 2 — chạy thử được Auto mà không cần
            // VisionMaster.
            if (fakeVision != null)
            {
                fakeVision.SeedVmSolution(VisionGlobalVarCatalog.Build(McDataManager.SysData));
                if (McDataManager.Model != null && McDataManager.Model.Calib.IsUsable)
                    fakeVision.StampCalibrated(McDataManager.Model.Calib);
            }

            Status.State = EMcState.NotHomed;
            IsInitialized = true;

            StartLoop(sys);
            LogWorker.Instance.RecordLog(
                sys.SimulationMode
                    ? "Khởi tạo xong — CHẾ ĐỘ MÔ PHỎNG, không điều khiển phần cứng thật"
                    : "Khởi tạo xong",
                Utility.Log.LogLevel.Info, "McControl");
        }

        /// <summary>
        /// Nạp solution vision ngay lúc khởi tạo, chạy nền để không giữ màn hình khởi động.
        ///
        /// <para>Quên bước này thì máy vẫn chạy và mọi lần chụp đều trả "không tìm thấy đối
        /// tượng" — nghĩa là <b>toàn bộ sản phẩm thành NG</b> và trên màn hình chỉ thấy alarm
        /// vision, không thấy nguyên nhân thật. Vì vậy <c>Routine_Auto</c> còn kiểm tra lại
        /// <c>IsSolutionLoaded</c> ở S00 để nói thẳng ra lý do thay vì để người vận hành đi tìm
        /// lỗi ở camera hay ở đèn.</para>
        /// </summary>
        private static void LoadVisionSolution(SystemData sys)
        {
            string path = sys.VisionSolutionPath;
            Task.Run(async () =>
            {
                try
                {
                    bool ok = await Vision.LoadSolutionAsync(path).ConfigureAwait(false);
                    if (ok)
                    {
                        Interlocked.Increment(ref _visionLoadCount);
                        LogWorker.Instance.RecordLog("Đã nạp solution vision: " + path,
                            Utility.Log.LogLevel.Info, "Vision");

                        // Thiếu dòng này là bug: Raise() ở nhánh else bên dưới không có nơi nào
                        // gọi ClearCondition tương ứng, nên một lần nạp lỗi là alarm này ConditionActive
                        // = true MÃI MÃI — Reset() chỉ gỡ alarm đã hết nguyên nhân nên RESET không bao
                        // giờ gỡ được, dù nạp lại solution qua Setting đã thành công (Tuấn phát hiện,
                        // 2026-09-08: SVO ON bị chặn toàn máy chỉ vì alarm Vision cũ không tự hết).
                        AlarmManager.ClearCondition(AlarmCode.VisionLoadFailed);
                    }
                    else
                    {
                        AlarmManager.Raise(AlarmCode.VisionLoadFailed, "Vision", path);
                    }
                }
                catch (Exception ex)
                {
                    AlarmManager.Raise(AlarmCode.VisionLoadFailed, "Vision", ex.Message);
                }
            });
        }

        /// <summary>Số lần nạp solution thành công. Trang Main chỉ gắn khung ảnh CAM sau lần nạp đầu, và gắn lại khi số này
        /// đổi (nạp lại ở Setting) — xem MainView.TryBindRenderControls.</summary>
        public static int VisionLoadCount => Volatile.Read(ref _visionLoadCount);
        private static int _visionLoadCount;

        /// <summary>
        /// Nạp lại solution theo <see cref="SystemData.VisionSolutionPath"/> HIỆN TẠI — gọi từ
        /// trang Setting sau khi đổi đường dẫn, để không cần khởi động lại app. Dùng lại đúng
        /// logic <see cref="LoadVisionSolution"/> (cùng alarm/log khi thất bại) thay vì viết lại.
        /// </summary>
        /// <returns>Lý do từ chối, null nếu đã yêu cầu nạp.</returns>
        public static string ReloadVisionSolution()
        {
            // Nạp giữ khoá lệnh vision nhiều giây — chu trình đang chạy thì lần kích Flow kế tiếp quá giờ.
            if (RoutineBusy()) return Loc.T("Mc.ReloadSolutionBusy");
            InvalidateVisionCache();
            LoadVisionSolution(McDataManager.SysData);
            return null;
        }

        /// <summary>
        /// Kiểm tra (<paramref name="apply"/> = false) hoặc tạo các biến toàn cục mà các Flow cần
        /// trong solution đang nạp (Tuấn yêu cầu 2026-09-13) — danh mục ở
        /// <see cref="VisionGlobalVarCatalog"/>. Tạo biến chỉ cho phép khi không có chu trình nào
        /// đang chạy: lưu solution giữa lúc Flow đang chạy là việc không ai muốn thử.
        /// </summary>
        public static async Task<GlobalVarSyncReport> SyncVisionGlobalVariablesAsync(bool apply)
        {
            if (Vision == null || !Vision.IsSolutionLoaded)
                return GlobalVarSyncReport.Fail(Loc.T("Mc.SolutionNotLoaded"));
            if (apply && RoutineBusy())
                return GlobalVarSyncReport.Fail(Loc.T("Mc.CreateVarsBusy"));

            var specs = VisionGlobalVarCatalog.Build(McDataManager.SysData);
            var report = await Vision.SyncGlobalVariablesAsync(specs, apply).ConfigureAwait(false);

            bool bad = !string.IsNullOrEmpty(report.Error) || report.Count(EGlobalVarState.Failed) > 0 ||
                       report.Count(EGlobalVarState.WrongType) > 0 || report.Count(EGlobalVarState.InvalidName) > 0;
            LogWorker.Instance.RecordLog(
                (apply ? "Tạo biến toàn cục: " : "Kiểm tra biến toàn cục: ") + report.ToText().Replace(Environment.NewLine, " | "),
                bad ? Utility.Log.LogLevel.Warning : Utility.Log.LogLevel.Info, "Vision");
            return report;
        }

        /// <summary>
        /// Đối chiếu tab Output Params của ba Flow với hợp đồng (Tuấn yêu cầu 2026-09-19) — đọc khai báo,
        /// KHÔNG chạy Flow nào, nên bấm lúc nào cũng được và không nạp điểm rác vào module hiệu chuẩn.
        /// </summary>
        public static async Task<VmOutCheckReport> CheckVisionFlowOutputsAsync()
        {
            if (Vision == null || !Vision.IsSolutionLoaded)
                return VmOutCheckReport.Fail(Loc.T("Mc.SolutionNotLoaded"));

            var names = McDataManager.SysData.VisionOutputs;
            var scripts = VmScriptContract.Build(names);

            var report = new VmOutCheckReport();
            foreach (var expect in VmCycle.ExpectedFlowOutputs(names))
            {
                var actual = await Vision.ReadFlowOutputsAsync(expect.Flow).ConfigureAwait(false);
                VmOutCheck.CompareFlow(expect, actual, report);

                // Cùng một lần đọc dùng cho cả hai tầng — Flow và Script bên trong nó.
                foreach (var s in scripts.Where(s => s.Flow == expect.Flow))
                    VmOutCheck.CompareScript(s, actual, report);
            }

            LogWorker.Instance.RecordLog(
                "Kiểm tra Output của các Flow: " + report.ToText().Replace(Environment.NewLine, " | "),
                report.Clean ? Utility.Log.LogLevel.Info : Utility.Log.LogLevel.Warning, "Vision");
            return report;
        }

        /// <summary>
        /// Ở chế độ mô phỏng, model mặc định chưa teach và chưa hiệu chuẩn nên chuỗi Auto sẽ
        /// dừng ngay ở S00. Nạp bộ số giả để chạy và kiểm chứng được LOGIC chu trình trước khi
        /// có máy — đây chính là lý do đầu tư làm Simulation mode tử tế (§10).
        ///
        /// <para>Chỉ nạp khi model hiện tại chưa dùng được, để không đè lên dữ liệu teach thật
        /// của ai đó đang mở bản mô phỏng để xem lại thông số.</para>
        /// </summary>
        private static void InstallSimulationModel()
        {
            var m = McDataManager.Model;
            bool usable = m != null && m.InputTray.IsTaught && m.AssyTray.IsTaught && m.Calib.IsUsable;
            if (usable) return;

            McDataManager.UseModel(Data.Model.ModelData.CreateSimulation());
            McDataManager.RunData.LoadInputTrays(McDataManager.Model.UsableInputTrays);
            McDataManager.RunData.AssyTray.ResetAll(ECellState.Empty);
            InvalidateAssyScan();
            Status.ModelName = McDataManager.Model.Name;

            LogWorker.Instance.RecordLog(
                "CHẾ ĐỘ MÔ PHỎNG: nạp model giả 'SIM' — lưới tray, hiệu chuẩn camera và tâm xoay " +
                "đều là SỐ BỊA, chỉ dùng để chạy thử chu trình",
                Utility.Log.LogLevel.Warning, "McControl");
        }

        private static void StartLoop(SystemData sys)
        {
            if (_running) return;
            _running = true;
            _lastTickMs = _clock.ElapsedMilliseconds;
            _loopStartMs = _lastTickMs;

            _loop = new Thread(() => Loop(sys.ControlTickMs))
            {
                IsBackground = true,
                Name = "McControlLoop",
                Priority = ThreadPriority.AboveNormal
            };
            _loop.Start();

            _watchdog = new Timer(CheckWatchdog, null, 1000, 200);
        }

        public static void Shutdown()
        {
            // Huỷ AbortToken TRƯỚC KHI đóng bất cứ cổng nào — mọi routine Auto/Origin VÀ mọi lệnh
            // Jog/Move/Home thủ công từ trang Manual đều observe chung Safety.AbortToken (xem
            // AxisRowVm/McFunction.MoveAsync). Không huỷ trước thì một lệnh đang dở tay (đang gửi
            // hoặc đang chờ phản hồi Modbus/RS485) có thể vẫn giữ khoá gửi lệnh của trục đúng lúc
            // Motion.DisconnectAll() đóng cổng, dễ làm driver "đơ" tới khi rút/cắm lại nguồn mới
            // kết nối lại được (Tuấn lo ngại lúc đóng app, 2026-09-10). Đợi _activeRoutine kết
            // thúc CÓ GIỚI HẠN — sau khi token bị huỷ, các vòng chờ vị trí (WaitMotionAsync…) thoát
            // gần như ngay (không đợi hết hành trình di chuyển thật); timeout chỉ đề phòng driver
            // treo thật không phản hồi, không để việc đóng app treo vô thời hạn.
            Safety?.RequestAbort("Ứng dụng đang đóng");
            Task activeRoutine;
            lock (_routineLock) activeRoutine = _activeRoutine;
            try { activeRoutine?.Wait(3000); } catch { }

            _running = false;
            try { _watchdog?.Dispose(); } catch { }
            try { _loop?.Join(1000); } catch { }

            Motion.StopAll();
            McDataManager.SaveRunningData();
            Motion.DisconnectAll();
            Io?.Disconnect();
            Vision?.Disconnect();
        }

        // ───────────────────────── vòng quét ─────────────────────────

        private static void Loop(int tickMs)
        {
            while (_running)
            {
                _tickSw.Restart();
                try
                {
                    Motion.Refresh();
                    Io.Refresh();
                    Safety.Evaluate();

                    UpdateMode();
                    HandleButtons();
                    UpdateState();
                    // Tạm dừng mà vào LỖI/EMG thì bỏ chu trình — sau RESET không còn gì để Start chạy tiếp.
                    if (!Func.RunBit && (Status.State == EMcState.Alarm || Status.State == EMcState.Emg))
                        AbandonHeld(Loc.T("Mc.WhyErrorPaused"));
                    // Chu trình đang chạy hay chờ tray mà vào LỖI/EMG thì cho nó thoát ở điểm dừng kế tiếp: RESET xong máy về
                    // DỪNG, chu trình không được chạy tiếp hay nằm chờ ngầm phía sau (Tuấn 30/09: A1 báo lỗi lúc chờ đổi tray,
                    // sau đó AUTO báo "Đang có chu trình chạy dở" mãi). Không huỷ token: lệnh trục đang chạy làm nốt như trước,
                    // lệnh kế bị chặn vì còn lỗi.
                    if ((Status.State == EMcState.Alarm || Status.State == EMcState.Emg) && CycleBusy() && !_cycleEnding)
                        EndCycle(Loc.T("Mc.WhyErrorRunning"), false);
                    UpdateOutputs();
                    Func.UpdateBrakeOutputs();
                }
                catch (Exception ex)
                {
                    // Sự kiện, không Raise trơn: không nơi nào gỡ nguyên nhân, một lỗi là RESET không bao giờ xoá được.
                    AlarmManager.RaiseEvent(AlarmCode.RoutineCrashed, "McControl", ex.Message);
                }
                _tickSw.Stop();
                _lastTickMs = _clock.ElapsedMilliseconds;

                int rest = tickMs - (int)_tickSw.ElapsedMilliseconds;
                Thread.Sleep(rest > 0 ? rest : 1);
            }
        }

        /// <summary>
        /// Watchdog chạy trên timer RIÊNG, không nằm trong vòng quét — vòng quét treo thì nó
        /// không thể tự phát hiện mình đã treo (PLAN.md §3.3).
        ///
        /// <para><b>Ân hạn khởi động (<c>WatchdogStartupGraceMs</c>).</b> Ngay sau khi
        /// <see cref="StartLoop"/> tạo thread <c>_loop</c>, hệ điều hành chưa chắc lập tức cấp
        /// CPU cho nó — nạp solution Vision chạy nền cùng lúc (xem <see cref="LoadVisionSolution"/>)
        /// có thể chiếm CPU đủ lâu để tick đầu tiên trễ hơn 500 ms bình thường, dù vòng quét không
        /// hề treo thật. Trong cửa sổ ân hạn này chỉ BỎ QUA việc RAISE alarm Fatal, không tắt hẳn
        /// watchdog — tick đầu tiên chạy xong thì <c>_lastTickMs</c> cập nhật lại như bình thường,
        /// và một treo THẬT xảy ra ngay trong lúc khởi động vẫn bị bắt ngay sau khi hết ân hạn.</para>
        /// </summary>
        private static void CheckWatchdog(object _)
        {
            if (!_running) return;

            long now = _clock.ElapsedMilliseconds;
            long silent = now - _lastTickMs;
            int limit = McDataManager.SysData.WatchdogTimeoutMs;

            if (silent > limit)
            {
                bool inStartupGrace = now - _loopStartMs < McDataManager.SysData.WatchdogStartupGraceMs;
                if (!inStartupGrace)
                    AlarmManager.Raise(AlarmCode.ControlWatchdog, "System", Loc.F("Mc.WatchdogSilent", silent, limit));
            }
            else
            {
                AlarmManager.ClearCondition(AlarmCode.ControlWatchdog);
            }
        }

        // ───────────────────────── chế độ / nút bấm ─────────────────────────

        private static void UpdateMode()
        {
            var next = (EMcMode)_requestedMode;
            if (next == Status.Mode) return;

            // Chuyển về Manual trong lúc đang chạy thì chạy nốt tới checkpoint rồi dừng (§3.4). Đang tạm dừng thì bỏ
            // chu trình: ở Manual còn jog được, Start sau đó mà phát lại lệnh dở từ chỗ đã jog tới là va.
            // Đang chờ tray cũng vậy — không thì Start sau đó chạy tiếp chu trình Auto dưới chế độ MANUAL.
            if (Status.Mode == EMcMode.Auto && (Status.State == EMcState.Running || Status.State == EMcState.WaitOperator))
                RequestPause();
            if (next == EMcMode.Manual && Status.State == EMcState.Held)
                AbandonHeld(Loc.T("Mc.WhyManualPaused"));

            // Armed chỉ có nghĩa dưới Mode Auto — rời Auto thì huỷ luôn bước arm dở.
            if (next == EMcMode.Manual && Status.State == EMcState.Armed)
                Status.State = EMcState.Idle;

            // Chọn AUTO là vào luôn trạng thái sẵn sàng, không phải bấm thêm nút (Tuấn 2026-09-30). Chưa đủ điều kiện
            // thì ở lại MANUAL và báo ngay. Chu trình còn sống (vừa gạt MANUAL lúc đang chạy/chờ tray) thì chỉ đổi lại
            // chế độ như trước, không arm.
            bool cycleAlive = Status.State == EMcState.Running || Status.State == EMcState.Pausing ||
                              Status.State == EMcState.WaitOperator;
            if (next == EMcMode.Auto && !cycleAlive)
            {
                string why = ArmBlockReason();
                if (why != null)
                {
                    _requestedMode = (int)EMcMode.Manual;
                    Status.Message = Loc.F("Mc.AutoRefused", why);
                    LogWorker.Instance.RecordLog(Status.Message, Utility.Log.LogLevel.Warning, "McControl");
                    AutoModeRefused?.Invoke(Status.Message);
                    return;
                }
                Status.Mode = next;
                LogWorker.Instance.RecordLog("Chuyển chế độ: " + next, Utility.Log.LogLevel.Info, "McControl");
                Arm("chuyển AUTO");
                return;
            }

            Status.Mode = next;
            LogWorker.Instance.RecordLog("Chuyển chế độ: " + next, Utility.Log.LogLevel.Info, "McControl");
        }

        /// <summary>Đặt chế độ Auto/Manual — máy không có công tắc chìa vật lý, gọi từ nút chọn
        /// mode trên trang Main. Gọi được từ bất kỳ thread nào; vòng quét mới thực sự áp dụng
        /// (xem UpdateMode), giữ đúng nguyên tắc UI chỉ ĐẶT YÊU CẦU của class này.</summary>
        public static void RequestSetMode(EMcMode mode) => _requestedMode = (int)mode;

        private static void HandleButtons()
        {
            // Nút cứng bắt SƯỜN LÊN, không bắt mức — nút kẹt hoặc dính tiếp điểm mà bắt theo
            // mức sẽ khiến máy tự khởi động lại liên tục (PLAN.md §3.2).
            if (Io.RisingEdge(IoMap.BtnStop)) RequestStop();
            // Không còn nút Tắt còi cứng (Tuấn 2026-10-15) — bit đó thành cảm biến Tray 1, tắt còi bằng nút trang Main.

            // Bốn nút Start như nhau (khách hàng, Tuấn chốt 2026-10-16) — trước đó Cặp A chỉ Arm, Start 1 nạp tray đầu
            // vào, Start 2 nạp tray lắp ráp. Bấm cùng lúc nhiều nút vẫn chỉ là một lần Start (một cờ yêu cầu). RESET không
            // còn nút vật lý — chỉ bấm được bằng nút mềm trang Main (MainViewModel.ResetCommand gọi thẳng RequestReset,
            // không qua DI nào ở đây).
            foreach (int bit in IoMap.StartButtons)
                if (Io.RisingEdge(bit)) RequestGo();

            if (Interlocked.Exchange(ref _reqReset, 0) == 1) DoReset();
            if (Interlocked.Exchange(ref _reqMute, 0) == 1) Status.BuzzerMuted = true;
            if (Interlocked.Exchange(ref _reqStop, 0) == 1) DoStop();
            if (Interlocked.Exchange(ref _reqPause, 0) == 1) DoPause();
            if (Interlocked.Exchange(ref _reqOrigin, 0) == 1) DoOrigin();
            if (Interlocked.Exchange(ref _reqInit, 0) == 1) DoInit();
            if (Interlocked.Exchange(ref _reqArm, 0) == 1) DoArm();
            if (Interlocked.Exchange(ref _reqGo, 0) == 1) DoGo();

            // _reqStart/DoStart: xem doc-comment RequestStart — chỉ còn SelfTest.cs dùng.
            if (Interlocked.Exchange(ref _reqStart, 0) == 1) DoStart();
        }

        // Các hàm Request* gọi được từ bất kỳ thread nào; vòng quét mới thực sự xử lý.
        public static void RequestStop() => Interlocked.Exchange(ref _reqStop, 1);

        private static readonly object _manualLock = new object();
        private static CancellationTokenSource _manualCts;
        private static CancellationToken _manualLinkedTo;

        /// <summary>
        /// Token cho các chuỗi chạy tay ở trang Vision — hiệu chuẩn, hand-eye, chạy kiểm từng bước (Tuấn 2026-10-08). Nối vào
        /// <see cref="SafetyMonitor.AbortToken"/> nên EMG/alarm vẫn huỷ được; STOP (mọi nút STOP, cả nút cứng) huỷ riêng nó ở
        /// <see cref="DoStop"/>. Trước đó trang Vision chỉ dừng trục: chuỗi nhiều bước vẫn sống và phát lệnh bước sau. Token
        /// riêng thay vì huỷ token Safety để không kéo theo chu trình Auto đang tạm dừng. Lấy mới mỗi lần bắt đầu một chuỗi.
        /// </summary>
        public static CancellationToken ManualToken
        {
            get
            {
                lock (_manualLock)
                {
                    var safety = Safety.AbortToken;
                    if (_manualCts == null || _manualCts.IsCancellationRequested || !_manualLinkedTo.Equals(safety))
                    {
                        // Không Dispose cái cũ: chuỗi đang chạy còn giữ token của nó.
                        _manualCts = CancellationTokenSource.CreateLinkedTokenSource(safety);
                        _manualLinkedTo = safety;
                    }
                    return _manualCts.Token;
                }
            }
        }

        private static void CancelManualSequences()
        {
            CancellationTokenSource cts;
            lock (_manualLock) cts = _manualCts;
            try { cts?.Cancel(); } catch (ObjectDisposedException) { }
        }

        /// <summary>Dừng ở checkpoint — chạy nốt bước đang làm rồi thoát chu trình (STOP lần 1 cũ). Dùng khi chuyển
        /// MANUAL lúc đang chạy, và trong self-test.</summary>
        public static void RequestPause() => Interlocked.Exchange(ref _reqPause, 1);
        public static void RequestReset() => Interlocked.Exchange(ref _reqReset, 1);
        public static void RequestOrigin() => Interlocked.Exchange(ref _reqOrigin, 1);

        /// <summary>Nút Init cạnh Về gốc trang Main — đưa máy về vị trí chờ, không về gốc lại. Xem <see cref="DoInit"/>.</summary>
        public static void RequestInit() => Interlocked.Exchange(ref _reqInit, 1);
        public static void RequestMuteBuzzer() => Interlocked.Exchange(ref _reqMute, 1);

        /// <summary>Vào trạng thái Armed, chờ bấm Start. Chỉ nút mềm "START" trên UI gọi — nút mềm CỐ Ý chỉ làm được đúng
        /// việc này (2026-08-20): không có đường nào từ màn hình cảm ứng chạy thẳng máy, phải qua nút Start vật lý ở
        /// <see cref="RequestGo"/>. Bốn nút cứng không còn gọi hàm này (2026-10-16).</summary>
        public static void RequestArm() => Interlocked.Exchange(ref _reqArm, 1);

        /// <summary>Một trong bốn nút Start cứng (DI 0x0E, 0x0F, 0x10, 0x11 — <see cref="IoMap.StartButtons"/>), như nhau
        /// (Tuấn chốt 2026-10-16). KHÔNG có nút mềm nào gọi hàm này. Xem <see cref="DoGo"/>.</summary>
        public static void RequestGo() => Interlocked.Exchange(ref _reqGo, 1);

        /// <summary>
        /// Lỗi nào cũng phải bấm RESET xác nhận rồi mới cho chạy tiếp (Tuấn chốt 2026-09-26) — chặn nút START mềm
        /// và nút Start cứng (cả lúc nạp tray giữa chừng). Chỉ xét alarm đã hết nguyên nhân: alarm còn nguyên nhân thì RESET
        /// chưa gỡ được, Error đã tự chặn qua trạng thái ALARM. Chờ đổi tray không phải alarm (Tuấn 2026-09-30).
        /// Warning mà máy tự chạy tiếp (hút hụt, soi hỏng trả về, bỏ ô…) tự rời danh sách nên không chặn — AlarmManager.Dismiss
        /// (Tuấn 2026-09-29).
        /// </summary>
        private static bool AlarmsAcknowledged()
        {
            var a = AlarmManager.PendingReset;
            if (a == null) return true;
            Status.Message = Loc.F("Mc.UnackedAlarm", a.Code, a.Message);
            return false;
        }

        /// <summary>Nút START mềm — vào lại sẵn sàng khi đã ở AUTO (vd. sau STOP). Chọn AUTO thì tự vào, xem
        /// <see cref="UpdateMode"/>; nút Start cứng lúc máy đứng thì vào rồi chạy luôn, xem <see cref="DoGo"/>.</summary>
        private static void DoArm()
        {
            if (Status.State == EMcState.Armed) return;
            string why = ArmRefusal();
            if (why != null) { Status.Message = why; return; }
            Arm("nút START mềm");
        }

        /// <summary>Lý do không vào Armed được từ nút bấm (START mềm, Start cứng lúc máy đứng); null = vào được.</summary>
        private static string ArmRefusal()
        {
            if (Status.Mode != EMcMode.Auto) return Loc.T("Mc.SelectAutoFirst");
            return ArmBlockReason();
        }

        /// <summary>
        /// Lý do chưa vào được trạng thái sẵn sàng; null = vào được. Chọn AUTO, nút START mềm và nút Start cứng cùng xét
        /// ở đây nên các đường không lệch nhau. Chạy dở giữa hành trình mà chưa Init thì chặn ngay từ đây (Tuấn 2026-09-30: không đủ điều
        /// kiện thì báo luôn lúc chọn AUTO), không đợi tới lúc bấm Start mới báo.
        /// </summary>
        private static string ArmBlockReason()
        {
            if (Status.State == EMcState.Held) return HeldMessage;
            if (RoutineBusy())
                return Status.State == EMcState.Idle || Status.State == EMcState.Paused || Status.State == EMcState.NotHomed
                    ? Loc.T("Mc.CycleBusyStop")
                    : Loc.T("Mc.CycleBusy");
            switch (Status.State)
            {
                case EMcState.Idle:
                case EMcState.Paused:
                    break;
                case EMcState.NotHomed:
                case EMcState.Init:
                    return Loc.T("Mc.NotHomedPressOrigin");
                case EMcState.Alarm:
                case EMcState.Emg:
                    return Loc.T("Mc.HasErrorReset");
                default:
                    return Loc.F("Mc.MachineInState", Status.StateText);
            }
            var a = AlarmManager.PendingReset;
            if (a != null) return Loc.F("Mc.UnackedAlarm", a.Code, a.Message);
            string block;
            if (NozzleClearPending(out block)) return block;
            if (McDataManager.RunData.UnsafePause)
                return Loc.T("Mc.UnsafePauseInit");
            return null;
        }

        private static void Arm(string how)
        {
            Status.State = EMcState.Armed;
            Status.Message = Status.DryRun
                ? Loc.T("Mc.ArmedDry")
                : Loc.T("Mc.Armed");
            LogWorker.Instance.RecordLog("Vào Armed (" + how + ")", Utility.Log.LogLevel.Info, "McControl");
        }

        /// <summary>
        /// Nút Start cứng — bốn nút như nhau (khách hàng, Tuấn chốt 2026-10-16). Máy tự biết đang thiếu tray nào: nạp mọi
        /// tray chưa vào máy mà cảm biến thấy ở vị trí chờ; tray không thấy thì chu trình làm được gì với tray đã có thì
        /// làm, bước cần tray kia đứng ở CHỜ THAO TÁC tới lần bấm Start sau — xem <c>Routine_Auto.LoadTrays</c>. Trước đó
        /// Start 1 chỉ nạp tray đầu vào, Start 2 chỉ nạp tray lắp ráp, Cặp A chỉ Arm (2026-09-26).
        /// <list type="bullet">
        /// <item>Tạm dừng: chạy tiếp đúng chỗ dở.</item>
        /// <item>AUTO mà máy đứng (IDLE/PAUSED, vd. sau STOP): vào Armed rồi chạy luôn — một lần bấm (Tuấn 2026-10-16).</item>
        /// <item>Armed: nạp tray rồi khởi động chu trình, không thấy tray nào cũng khởi động — chu trình chờ tray. Tray đã
        /// ở trong máy (chạy tiếp sau STOP) thì chỉ khởi động. Chạy rà không dùng tray.</item>
        /// <item>Chu trình đang chạy hoặc đang chờ tray: chỉ nạp tray.</item>
        /// </list>
        /// KHÔNG có nhánh NotHomed→DoOrigin() như DoStart cũ: về gốc là chuyển động thật, phải
        /// bấm nút "Về gốc" riêng (2026-08-20, chủ ý đơn giản hoá).
        /// </summary>
        private static void DoGo()
        {
            const string button = "Start";

            // Tạm dừng: Start bật lại bit RUN, chu trình chạy tiếp đúng chỗ dở (Tuấn 2026-09-29).
            if (Status.State == EMcState.Held)
            {
                if (!Safety.SafeToMove) { Status.Message = Loc.F("Mc.CannotResume", Safety.BlockReason); return; }
                _auto.HoldClock(false);
                Status.State = EMcState.Running;
                Status.Message = Status.DryRun ? Loc.T("Mc.DryRunning") : Loc.T("Mc.AutoRunning");
                Func.SetRunBit(true);
                LogWorker.Instance.RecordLog(button + " — bật bit RUN, chạy tiếp", Utility.Log.LogLevel.Info, "McControl");
                return;
            }

            if (RoutineBusy())
            {
                if (Status.DryRun ||
                    (Status.State != EMcState.Running && Status.State != EMcState.WaitOperator))
                { Status.Message = Loc.T("Mc.CycleBusy"); return; }
                if (!AlarmsAcknowledged()) return;
                // Nạp tray là cho chu trình chạy trục tiếp — rèm còn bị cắt thì chưa nhận (Tuấn 2026-10-01).
                if (!Status.LightCurtainOk)
                { Status.Message = Loc.F("Mc.CurtainPressAgain", button); return; }
                string loaded, notSeen, why = _auto.LoadTrays(out loaded, out notSeen);
                if (why != null) { Status.Message = why; return; }
                if (loaded == null)
                {
                    // Không nạp thêm được tray nào: tray còn thiếu chưa đặt, hoặc cả hai đã ở trong máy.
                    Status.Message = notSeen ?? Loc.T("Mc.TraysAllIn");
                    return;
                }
                Status.Message = Loc.F("Mc.TrayAccepted", loaded, button);
                LogWorker.Instance.RecordLog(Status.Message, Utility.Log.LogLevel.Info, "McControl");
                return;
            }

            // AUTO mà máy đứng (vd. sau STOP): vào sẵn sàng rồi chạy luôn, không bắt bấm hai lần (Tuấn 2026-10-16).
            if (Status.State == EMcState.Idle || Status.State == EMcState.Paused)
            {
                string why = ArmRefusal();
                if (why != null) { Status.Message = why; return; }
                Arm(button);
            }

            if (Status.State != EMcState.Armed)
            {
                Status.Message = Loc.F("Mc.CannotRunButtonInState", button, Status.StateText);
                return;
            }

            if (!AlarmsAcknowledged()) return;

            if (!Safety.SafeToMove)
            {
                Status.Message = Loc.F("Mc.CannotRun", Safety.BlockReason);
                return;
            }

            string block;
            if (!ResumeIntegrityOk(out block))
            {
                Status.Message = block;
                LogWorker.Instance.RecordLog("Chặn GO: " + block, Utility.Log.LogLevel.Warning, "McControl");
                return;
            }

            // Tray hết theo sổ (còn từ lần chạy trước) thì chu trình nhận là tray mới — không chạy tới S50 rồi mới đòi đổi
            // (Tuấn gặp 2026-09-26). Không thấy tray nào vẫn khởi động: chu trình tự vào CHỜ THAO TÁC, báo thiếu tray nào.
            // Chạy rà không dùng tray.
            if (!Status.DryRun)
            {
                string loaded, notSeen, why = _auto.LoadTrays(out loaded, out notSeen);
                if (why != null) { Status.Message = why; return; }
            }

            StartCycle();
        }

        /// <summary>
        /// Khởi động chu trình — chạy chu trình THẬT hay chu trình RÀ do <see cref="McStatus.DryRun"/> quyết,
        /// tại ĐÚNG MỘT CHỖ. Cả hai đường vào đều đi qua đây: nút Start (<see cref="DoGo"/>, đường vận hành thật)
        /// và đường START cũ (<see cref="DoStart"/>, chỉ self-test dùng).
        ///
        /// <para><b>Vì sao phải gom về một chỗ.</b> 23/09 phép rẽ nhánh chạy rà chỉ được đặt trong
        /// <c>DoStart</c> — mà <c>DoStart</c> thì chỉ self-test gọi. Trên máy thật, bật chạy rà rồi bấm Cặp B
        /// vẫn chạy chu trình thật và vẫn kích Vision; self-test không bắt được vì nó chạy đúng cái đường
        /// đã sửa (Tuấn phát hiện 2026-09-24). Một chỗ quyết thì hai đường không thể lệch nhau nữa.</para>
        /// </summary>
        private static void StartCycle()
        {
            if (Status.DryRun) StartDryRun(); else StartAuto();
        }

        /// <summary>
        /// Đường START cũ, CHỈ còn dùng cho SelfTest.cs (mô phỏng qua FakeIo, không phải vận
        /// hành thật) — giữ nguyên để không phải viết lại các kịch bản test đã có. Đường vận
        /// hành thật (DI vật lý) phải qua <see cref="RequestGo"/> (nút Start cứng), không còn gọi hàm này
        /// nữa (2026-08-20).
        /// </summary>
        public static void RequestStart() => Interlocked.Exchange(ref _reqStart, 1);

        private static void DoStart()
        {
            // Xét TRƯỚC khi hỏi routine có đang bận không: ở trạng thái chờ thao tác thì chu
            // trình vẫn đang chạy — nó đang nằm chờ chính cái xác nhận này. Đặt sau thì nút
            // START ở màn hình đổi tray sẽ mãi mãi báo "đang có chu trình chạy dở".
            // Không chặn theo AlarmsAcknowledged ở đây: đường này chỉ self-test đi, và các kịch bản test nối
            // nhau qua nó sau những cảnh báo cố ý gây ra. Đường vận hành thật (nút START mềm, nút Start cứng) có chặn.
            if (Status.State == EMcState.WaitOperator)
            {
                // Nạp mọi tray đang thiếu một lượt — tiện cho kịch bản test, không qua kiểm cảm biến tray.
                _auto.LoadBothTrays();
                Status.Message = Loc.T("Mc.ConfirmedContinue");
                LogWorker.Instance.RecordLog("Operator xác nhận đã xử lý xong", Utility.Log.LogLevel.Info, "McControl");
                return;
            }

            if (RoutineBusy())
            {
                Status.Message = Loc.T("Mc.CycleBusy");
                return;
            }

            switch (Status.State)
            {
                case EMcState.NotHomed:
                    DoOrigin();
                    return;

                case EMcState.Idle:
                case EMcState.Paused:
                case EMcState.Armed:        // chọn AUTO là vào luôn Armed (2026-09-30)
                    if (Status.Mode != EMcMode.Auto)
                    {
                        Status.Message = Loc.T("Mc.KeyToAuto");
                        return;
                    }
                    if (!Safety.SafeToMove)
                    {
                        Status.Message = Loc.F("Mc.CannotRun", Safety.BlockReason);
                        return;
                    }

                    string block;
                    if (!ResumeIntegrityOk(out block))
                    {
                        Status.Message = block;
                        LogWorker.Instance.RecordLog("Chặn START: " + block, Utility.Log.LogLevel.Warning, "McControl");
                        return;
                    }

                    _auto.LoadBothTrays();
                    StartCycle();
                    return;

                default:
                    Status.Message = Loc.F("Mc.CannotStartInState", Status.StateText);
                    return;
            }
        }

        /// <summary>
        /// Kiểm tra toàn vẹn trước khi chạy tiếp — PLAN.md §3.7.
        ///
        /// <para>So trạng thái tay hút mà PHẦN MỀM ghi nhận với cái CẢM BIẾN đang báo. Lệch nhau
        /// nghĩa là trong lúc máy dừng đã có người gỡ sản phẩm ra, hoặc sản phẩm rơi, hoặc cảm
        /// biến hỏng — chạy tiếp trong cả ba trường hợp đều dẫn tới hỏng hàng. Đây là nguồn hỏng
        /// hàng phổ biến nhất khi vận hành thật và cũng là phần hay bị bỏ sót nhất.</para>
        ///
        /// <para><b>Cảm biến chân không là nguồn sự thật, cả hai chiều, không alarm</b>: tay CÒN hàng mà sổ ghi trống —
        /// nhận hàng và chạy tiếp, coi như đã qua bước hút (Tuấn đổi 2026-09-26, <see cref="AdoptHeldProduct"/>); sổ ghi
        /// có hàng mà cảm biến báo TRỐNG — hàng đã được lấy ra (tắt hút ở trang Manual, gỡ tay sau STOP → MANUAL): ghi sổ
        /// tay trống, ô nguồn thành trống, chạy tiếp (Tuấn 2026-09-30, <see cref="ReleaseEmptyNozzle"/>). Trước đó chiều
        /// này phát alarm 5030 rồi bắt RESET và START lại, dù App đã tự đồng bộ xong.</para>
        /// </summary>
        private static bool ResumeIntegrityOk(out string reason)
        {
            reason = null;
            if (NozzleClearPending(out reason)) return false;

            var run = McDataManager.RunData;
            if (Io == null || !Io.IsConnected)
            {
                reason = Loc.T("Mc.NoVacuumSensor");
                return false;
            }

            for (int i = 1; i <= 2; i++)
            {
                var nz = i == 1 ? run.Nozzle1 : run.Nozzle2;
                bool sensor = Func.HasProduct(i);
                if (nz.HasProduct == sensor) continue;

                if (sensor) AdoptHeldProduct(i, "sổ ghi tay trống nhưng cảm biến báo có hàng");
                else ReleaseEmptyNozzle(i, "sổ ghi có hàng nhưng cảm biến báo trống — hàng đã được lấy ra");
            }

            // Dừng giữa hành trình (bỏ chu trình đang tạm dừng, có thể đã thao tác tay): phải Init — hoặc Về gốc — đưa máy
            // về vị trí chờ trước (Tuấn 2026-09-29). Trước đó chỉ đòi Z ở cao độ an toàn.
            if (run.UnsafePause)
            {
                reason = Loc.T("Mc.UnsafePauseInitResume");
                return false;
            }

            return true;
        }

        /// <summary>
        /// Yêu cầu lấy sản phẩm ra khỏi tay hút (<see cref="RunningData.NozzleClearRequired"/>) — đặt lúc sự cố và
        /// làm chu trình dừng một lần (Tuấn chốt 2026-09-13). Tới lần bấm chạy kế tiếp thì gỡ, ghi sổ theo cảm biến:
        /// <list type="bullet">
        /// <item>tay <b>còn hàng</b> — nhận hàng và chạy tiếp, coi như đã qua bước hút (Tuấn đổi 2026-09-26; trước đó
        /// bắt lấy hàng ra), xem <see cref="AdoptHeldProduct"/>;</item>
        /// <item>tay <b>đã trống</b> — tay hút trống, ô nguồn đang <c>Processing</c> thành <c>Empty</c> (sản phẩm đã
        /// bị lấy đi nên hốc tray trống — trả về <c>Loaded</c> là S10 sẽ hút vào hốc rỗng).</item>
        /// </list>
        /// Chu trình chạy lại từ S00. Chỉ còn chặn (trả true kèm lý do) khi chưa đọc được cảm biến.
        /// </summary>
        private static bool NozzleClearPending(out string reason)
        {
            reason = null;
            var run = McDataManager.RunData;
            if (!run.NozzleClearRequired) return false;

            if (Io == null || !Io.IsConnected)
            {
                reason = Loc.F("Mc.NoVacuumSensorWhy", run.NozzleClearReason);
                return true;
            }

            string why = run.NozzleClearReason;
            for (int i = 1; i <= 2; i++)
            {
                if (Func.HasProduct(i)) AdoptHeldProduct(i, "sau sự cố: " + why);
                else ReleaseEmptyNozzle(i, null);
            }

            LogWorker.Instance.RecordLog("Gỡ yêu cầu lấy sản phẩm ra (" + why + "), chu trình chạy lại từ S00",
                Utility.Log.LogLevel.Info, "McControl");
            run.NozzleClearRequired = false;
            run.NozzleClearReason = null;
            run.AutoState = EAutoState.S00_Ready;
            McDataManager.SaveRunningData();
            return false;
        }

        /// <summary>
        /// Tay hút còn hàng lúc bấm chạy — nhận hàng và chạy tiếp, <b>coi như đã qua bước hút</b> (Tuấn yêu cầu
        /// 2026-09-26). Ghi sổ tay có hàng, chưa soi; chu trình chạy lại từ S00 nên tray lắp ráp được quét và hàng được
        /// soi lại ở CAM bottom-up rồi mới đặt; S1x thấy tay đã có hàng thì bỏ bước hút. Ô nguồn giữ nếu ô đó vẫn đang
        /// <c>Processing</c> (sổ còn biết hàng lấy từ đâu), không thì −1 — soi lỗi mà phải trả về thì chu trình dừng và
        /// bắt lấy hàng ra như cũ (<c>Routine_Auto.ReturnToSourceAsync</c>).
        /// </summary>
        private static void AdoptHeldProduct(int nozzle, string why)
        {
            var run = McDataManager.RunData;
            var nz = nozzle == 1 ? run.Nozzle1 : run.Nozzle2;
            int src = nz.SourceCellIndex;
            if (src < 0 || src >= run.InputTray.CellCount || run.InputTray.Get(src) != ECellState.Processing)
                nz.SourceCellIndex = -1;
            nz.HasProduct = true;
            nz.Aligned = false;
            run.AutoState = EAutoState.S00_Ready;
            McDataManager.SaveRunningData();
            LogWorker.Instance.RecordLog(
                "Tay #" + nozzle + " còn hàng (" + why + ") — chạy tiếp, bỏ bước hút, soi lại ở CAM dưới rồi đặt" +
                (nz.SourceCellIndex < 0 ? "; không rõ ô nguồn" : ""),
                Utility.Log.LogLevel.Info, "McControl");
        }

        /// <summary>
        /// Tay hút trống theo cảm biến: ghi sổ tay trống, ô nguồn đang <c>Processing</c> thành <c>Empty</c> — hàng đã bị lấy
        /// đi nên hốc tray trống, trả về <c>Loaded</c> là S10 hút vào hốc rỗng. Sổ đang ghi có hàng thì chu trình chạy lại từ
        /// S00 và ghi một dòng log (<paramref name="why"/>); <c>null</c> = không ghi log (bước gỡ yêu cầu tự ghi).
        /// </summary>
        private static void ReleaseEmptyNozzle(int nozzle, string why)
        {
            var run = McDataManager.RunData;
            var nz = nozzle == 1 ? run.Nozzle1 : run.Nozzle2;
            bool hadProduct = nz.HasProduct;
            int src = nz.SourceCellIndex;
            if (src >= 0 && src < run.InputTray.CellCount && run.InputTray.Get(src) == ECellState.Processing)
                run.InputTray.Set(src, ECellState.Empty);
            nz.HasProduct = false;
            nz.SourceCellIndex = -1;
            nz.Aligned = false;
            if (!hadProduct || why == null) return;

            run.AutoState = EAutoState.S00_Ready;
            McDataManager.SaveRunningData();
            LogWorker.Instance.RecordLog("Tay #" + nozzle + ": " + why + " — ghi sổ tay trống theo cảm biến, chạy tiếp từ S00" +
                (src >= 0 ? ", ô nguồn " + src + " thành trống" : ""), Utility.Log.LogLevel.Warning, "McControl");
        }

        /// <summary>
        /// Lấy cảm biến chân không làm nguồn sự thật, ghi đè trạng thái phần mềm. Gọi tự động từ
        /// <see cref="ResumeIntegrityOk"/> khi phát hiện lệch lúc chạy lại; chỉ cho gọi khi máy
        /// đang đứng yên — đây là thao tác ghi đè trạng thái, không phải thao tác chạy máy.
        /// </summary>
        public static void SyncNozzleStateFromSensor()
        {
            if (Status.State == EMcState.Running || Status.State == EMcState.Homing)
            {
                Status.Message = Loc.T("Mc.SyncOnlyStopped");
                return;
            }

            var run = McDataManager.RunData;
            run.Nozzle1.HasProduct = Func.HasProduct(1);
            run.Nozzle2.HasProduct = Func.HasProduct(2);
            if (!run.Nozzle1.HasProduct) { run.Nozzle1.SourceCellIndex = -1; run.Nozzle1.Aligned = false; }
            if (!run.Nozzle2.HasProduct) { run.Nozzle2.SourceCellIndex = -1; run.Nozzle2.Aligned = false; }

            AlarmManager.ClearCondition(AlarmCode.ResumeIntegrity);
            McDataManager.SaveRunningData();

            Status.Message = Loc.T("Mc.NozzleSynced");
            LogWorker.Instance.RecordLog(Status.Message, Utility.Log.LogLevel.Warning, "McControl");
        }

        private static void StartAuto()
        {
            Safety.RenewToken();
            Func.SetRunBit(true);
            _auto.PauseRequested = false;
            McDataManager.RunData.UnsafePause = false;

            Status.State = EMcState.Running;
            Status.Message = Loc.T("Mc.AutoRunning");

            lock (_routineLock)
            {
                var ct = NewCycleToken();
                _activeRoutine = Task.Run(async () =>
                {
                    bool ok = await _auto.RunAsync(ct).ConfigureAwait(false);
                    Func.SetRunBit(true);   // chu trình đã thoát — bit tắt thì Về gốc/Calib sau đó đứng chờ mãi

                    // Routine tự đặt Paused khi dừng có kiểm soát. Mọi đường ra khác đều là dừng
                    // bất thường và không được để máy trông như đang sẵn sàng. WaitOperator cũng phải
                    // gỡ: STOP lúc chờ đổi tray làm chu trình thoát ngay trong vòng chờ, để nguyên thì
                    // Start chỉ còn "xác nhận" cho một chu trình đã thoát và máy kẹt ở đó.
                    if (Status.State == EMcState.Running || Status.State == EMcState.Pausing ||
                        Status.State == EMcState.WaitOperator || Status.State == EMcState.Held)
                        Status.State = ok ? EMcState.Idle : EMcState.Paused;

                    McDataManager.SaveRunningData();
                });
            }
        }

        /// <summary>Khởi động chu trình chạy rà — cùng khung với <see cref="StartAuto"/> nhưng KHÔNG đụng
        /// tới sổ sách của chu trình thật: không lưu tiến độ, không cộng bộ đếm, không đổi trạng thái ô.</summary>
        private static void StartDryRun()
        {
            Safety.RenewToken();
            Func.SetRunBit(true);
            _dry.PauseRequested = false;

            Status.State = EMcState.Running;
            Status.Message = Loc.T("Mc.DryRunning");

            lock (_routineLock)
            {
                var ct = NewCycleToken();
                _activeRoutine = Task.Run(async () =>
                {
                    // Chạy rà không có đích để "xong" — mọi đường ra đều là dừng, nên luôn về Paused.
                    await _dry.RunAsync(ct).ConfigureAwait(false);
                    Func.SetRunBit(true);
                    if (Status.State == EMcState.Running || Status.State == EMcState.Pausing || Status.State == EMcState.Held)
                        Status.State = EMcState.Paused;
                });
            }
        }

        private static void DoStop()
        {
            CancelManualSequences();
            switch (Status.State)
            {
                case EMcState.Homing:
                    // Huỷ về gốc: OrgOk về false, không bao giờ để trạng thái dở dang là "đã về gốc".
                    Safety.RequestAbort("operator bấm STOP khi đang về gốc");
                    Status.Message = Loc.T("Mc.HomingCancelled");
                    return;

                case EMcState.WaitOperator:
                    DoPause();
                    return;

                case EMcState.Running:
                    // Tắt bit RUN: mọi trục dừng ngay, chu trình đứng nguyên chỗ, nút Start chạy tiếp (Tuấn
                    // 2026-09-29). GIỮ servo và GIỮ vacuum. Trước đó STOP lần 1 chạy nốt tới checkpoint, lần 2 mới dừng
                    // ngay và bắt nâng Z bằng tay trước khi chạy lại.
                    Hold(HeldMessage, "STOP");
                    return;

                case EMcState.Held:
                    Status.Message = HeldMessage;
                    return;

                case EMcState.Pausing:
                    // Đang chạy nốt tới checkpoint (sau khi chuyển MANUAL): STOP thì dừng ngay tại chỗ nhưng GIỮ servo
                    // và GIỮ vacuum — cắt chân không lúc này là làm rơi sản phẩm vào trong máy (§3.1).
                    Safety.RequestAbort("operator bấm STOP lần 2");
                    Motion.StopAll();
                    McDataManager.RunData.UnsafePause = true;
                    McDataManager.SaveRunningData();
                    Status.State = EMcState.Paused;
                    Status.Message = Loc.T("Mc.StoppedInPlace");
                    LogWorker.Instance.RecordLog("STOP lần 2 — dừng ngay, đánh dấu UnsafePause",
                        Utility.Log.LogLevel.Warning, "McControl");
                    return;

                default:
                    Motion.StopAll();
                    // Chu trình còn sống mà máy không ở trạng thái chạy nào: STOP huỷ luôn (Tuấn 2026-09-30).
                    if (CycleBusy()) EndCycle(Loc.T("Mc.WhyOperatorStop"), true);
                    return;
            }
        }

        /// <summary>
        /// Kết thúc chu trình Auto/chạy rà còn sống: cờ dừng cho vòng chờ tray và mọi checkpoint. <paramref name="cancelToken"/>
        /// huỷ luôn token riêng của chu trình — dừng cả lệnh trục/Flow đang chạy (token của Safety có thể đã đổi sau RESET
        /// nên huỷ nó không tới được chu trình cũ).
        /// </summary>
        private static void EndCycle(string why, bool cancelToken)
        {
            _cycleEnding = true;
            _auto.PauseRequested = true;
            _dry.PauseRequested = true;
            if (cancelToken)
            {
                CancellationTokenSource cts;
                lock (_routineLock) cts = _cycleCts;
                try { cts?.Cancel(); } catch (ObjectDisposedException) { }
            }
            Status.Message = Loc.F("Mc.CycleCancelled", why);
            LogWorker.Instance.RecordLog(Status.Message, Utility.Log.LogLevel.Warning, "McControl");
        }

        private static string HeldMessage => Loc.T("Mc.Held");

        /// <summary>Tạm dừng chu trình đang chạy: tắt bit RUN, trục dừng tại chỗ, nút Start chạy tiếp.</summary>
        private static void Hold(string message, string why)
        {
            Func.SetRunBit(false);
            _auto.HoldClock(true);
            Status.State = EMcState.Held;
            Status.Message = message;
            LogWorker.Instance.RecordLog(why + " — tắt bit RUN, trục dừng tại chỗ", Utility.Log.LogLevel.Info, "McControl");
        }

        /// <summary>
        /// Rèm sáng vừa bị cắt — SafetyMonitor gọi trên vòng quét, TRƯỚC khi SafeToMove tắt (Tuấn 2026-10-01): dừng mọi
        /// chuyển động; chu trình Auto/chạy rà tạm dừng y như STOP, rèm thông rồi bấm Start chạy tiếp đúng chỗ dở.
        /// Về gốc/Init thì huỷ như STOP lúc về gốc; đang chạy nốt tới checkpoint (sau khi chuyển MANUAL) thì dừng ngay như
        /// STOP. Chờ tray thì không trục nào chạy — người nạp tray đi qua rèm là chuyện thường — nên để nguyên; Start lúc
        /// rèm còn cắt bị từ chối ở <see cref="DoGo"/>.
        /// </summary>
        private static void HoldForCurtain()
        {
            switch (Status.State)
            {
                case EMcState.Running:
                    Hold(Loc.T("Mc.CurtainHeld"), "Rèm sáng");
                    return;
                case EMcState.Pausing:
                case EMcState.Homing:
                    DoStop();
                    return;
            }
        }

        /// <summary>
        /// Dừng ở checkpoint (<see cref="RequestPause"/>): chỉ ĐẶT YÊU CẦU, routine chạy nốt bước đang làm rồi thoát, máy về
        /// Paused (§3.4). Chờ tray thì thoát khỏi vòng chờ — chạy lại thì quay về đúng bước đang chờ.
        /// </summary>
        private static void DoPause()
        {
            switch (Status.State)
            {
                case EMcState.WaitOperator:
                    _auto.PauseRequested = true;
                    Status.Message = Loc.T("Mc.TrayWaitCancelled");
                    return;

                case EMcState.Running:
                    // Đặt cho cả hai chu trình: chỉ một cái đang chạy, cái kia bỏ qua cờ này.
                    _dry.PauseRequested = true;
                    _auto.PauseRequested = true;
                    Status.State = EMcState.Pausing;
                    Status.Message = Loc.T("Mc.RunningToCheckpoint");
                    LogWorker.Instance.RecordLog("Dừng ở checkpoint", Utility.Log.LogLevel.Info, "McControl");
                    return;
            }
        }

        /// <summary>
        /// Bỏ chu trình đang tạm dừng (chuyển MANUAL, có lỗi): huỷ như STOP lần 2 cũ. Trục đứng giữa hành trình nên đánh
        /// dấu UnsafePause — lần chạy sau đòi Z ở cao độ an toàn trước, rồi làm lại bước dở từ đầu.
        /// </summary>
        private static void AbandonHeld(string why)
        {
            Safety.RequestAbort(why);
            Func.SetRunBit(true);
            McDataManager.RunData.UnsafePause = true;
            McDataManager.SaveRunningData();
            if (Status.State == EMcState.Held)
            {
                Status.State = EMcState.Paused;
                Status.Message = Loc.F("Mc.HeldAbandoned", why);
            }
            LogWorker.Instance.RecordLog("Bỏ chu trình đang tạm dừng — " + why, Utility.Log.LogLevel.Warning, "McControl");
        }

        private static void DoReset()
        {
            Status.BuzzerMuted = true;

            foreach (var axis in Motion.AxisList)
            {
                if (axis.Alarm)
                {
                    var t = axis.ResetAlarmAsync();
                    if (t != null) t.ConfigureAwait(false);
                }
            }

            var stuck = AlarmManager.Reset();
            Safety.RenewToken();

            if (stuck.Count > 0)
            {
                // Bấm RESET mà không xoá được gì thì phải nói rõ vì sao — đây là lúc operator
                // hay bấm loạn nhất (PLAN.md §7.4).
                Status.Message = Loc.F("Mc.ResetStuck", stuck[0].Message,
                                       stuck.Count > 1 ? Loc.F("Mc.ResetStuckMore", stuck.Count - 1) : "");
                return;
            }

            Status.Message = Loc.T("Mc.ResetDone");
            if (Status.State == EMcState.Alarm || Status.State == EMcState.Emg)
                Status.State = Status.OrgOk ? EMcState.Idle : EMcState.NotHomed;
        }

        private static void DoOrigin()
        {
            if (RoutineBusy()) { Status.Message = Loc.T("Mc.CycleBusy"); return; }
            if (!Safety.SafeToMove) { Status.Message = Loc.F("Mc.CannotHome", Safety.BlockReason); return; }

            // Không đòi lấy sản phẩm ra trước khi về gốc (Tuấn bỏ 2026-09-26).

            // Mọi trục về vị trí chờ nên hai tray ra khỏi máy — chạy lại phải bấm Start nạp lại. TraysOut bỏ luôn số quét ô
            // và tư thế trên tay: về gốc lại thì chúng không còn cùng hệ toạ độ trục (R24); chạy lại từ S00, quét lại hàng.
            _auto.TraysOut();

            Safety.RenewToken();
            Status.State = EMcState.Homing;
            Status.Message = Loc.T("Mc.HomingStarted");

            var ct = Safety.AbortToken;
            lock (_routineLock)
            {
                _activeIsCycle = false;
                _activeRoutine = Task.Run(async () =>
                {
                    bool ok = await _origin.RunAsync(ct).ConfigureAwait(false);
                    if (ok) McDataManager.RunData.UnsafePause = false;   // mọi trục đã ở vị trí chờ, như Init
                    Status.State = ok ? EMcState.Idle : EMcState.NotHomed;
                    // Routine chỉ đặt Message khi thất bại — thành công cũng phải nói, không thì
                    // trên màn hình còn treo dòng "Bắt đầu chuỗi về gốc" như đang dở dang.
                    if (ok) Status.Message = Loc.T("Mc.HomingDone");
                });
            }
        }

        /// <summary>
        /// Init (Tuấn 2026-09-29): nâng Z rồi đưa mọi trục về vị trí chờ, không về gốc lại — dùng sau khi tạm dừng, chuyển
        /// MANUAL và thao tác tay, trước khi chạy tiếp. Y1/Y2 về vị trí chờ nên hai tray ra khỏi máy, chạy tiếp phải bấm
        /// Start nạp lại (tray còn dở thì giữ sổ, nhưng hàng đang làm được chụp lại từ đầu — xem Routine_Auto.TraysOut).
        /// Chạy dưới trạng thái Homing để STOP, đèn và chốt an toàn xử lý y như lúc về gốc.
        /// </summary>
        private static void DoInit()
        {
            if (Status.State == EMcState.Held)
            {
                Status.Message = Loc.T("Mc.InitWhileHeld");
                return;
            }
            if (RoutineBusy()) { Status.Message = Loc.T("Mc.CycleBusy"); return; }
            if (!Status.OrgOk) { Status.Message = Loc.T("Mc.InitNotHomed"); return; }
            if (!Safety.SafeToMove) { Status.Message = Loc.F("Mc.CannotInit", Safety.BlockReason); return; }

            _auto.TraysOut();
            Safety.RenewToken();
            var before = Status.State;
            Status.State = EMcState.Homing;
            Status.Message = Loc.T("Mc.InitStarted");
            LogWorker.Instance.RecordLog(Status.Message, Utility.Log.LogLevel.Info, "McControl");

            var ct = Safety.AbortToken;
            lock (_routineLock)
            {
                _activeIsCycle = false;
                _activeRoutine = Task.Run(async () =>
                {
                    bool ok = await _origin.InitAsync(ct).ConfigureAwait(false);
                    if (ok)
                    {
                        McDataManager.RunData.UnsafePause = false;
                        McDataManager.SaveRunningData();
                        Status.Message = Loc.T("Mc.InitDone");
                    }
                    if (Status.State == EMcState.Homing)
                        Status.State = ok ? (before == EMcState.Paused ? EMcState.Paused : EMcState.Idle) : EMcState.Paused;
                });
            }
        }

        private static bool RoutineBusy()
        {
            lock (_routineLock)
                return _activeRoutine != null && !_activeRoutine.IsCompleted;
        }

        /// <summary>Token riêng của chu trình Auto/chạy rà đang chạy — nối vào token Safety lúc khởi động, <see cref="EndCycle"/>
        /// huỷ được cả khi Safety đã đổi token sau RESET.</summary>
        private static CancellationTokenSource _cycleCts;

        /// <summary><c>_activeRoutine</c> là chu trình Auto/chạy rà (không phải Về gốc/Init); đã xin kết thúc nó chưa.</summary>
        private static bool _activeIsCycle;
        private static volatile bool _cycleEnding;

        private static bool CycleBusy()
        {
            lock (_routineLock) return _activeIsCycle && _activeRoutine != null && !_activeRoutine.IsCompleted;
        }

        /// <summary>Gọi trong <c>_routineLock</c>, lúc chu trình cũ đã thoát.</summary>
        private static CancellationToken NewCycleToken()
        {
            _cycleCts?.Dispose();
            _cycleCts = CancellationTokenSource.CreateLinkedTokenSource(Safety.AbortToken);
            _activeIsCycle = true;
            _cycleEnding = false;
            return _cycleCts.Token;
        }

        // ───────────────────────── trạng thái & ngõ ra ─────────────────────────

        private static void UpdateState()
        {
            Status.OrgOk = Motion.AllHomed;
            Status.VisionConnected = Vision != null && Vision.IsConnected;

            // EMG và ALARM đè lên mọi trạng thái khác.
            if (AlarmManager.IsActive(AlarmCode.EmergencyStop))
            {
                if (Status.State != EMcState.Emg)
                {
                    Status.State = EMcState.Emg;
                    McDataManager.SaveRunningData();   // chốt tiến độ ngay khi vào EMG (§3.7)
                    LogWorker.Instance.RecordLog("Vào trạng thái EMG", Utility.Log.LogLevel.Error, "McControl");
                }
                return;
            }

            if (AlarmManager.HasError)
            {
                if (Status.State != EMcState.Alarm)
                {
                    Status.State = EMcState.Alarm;
                    McDataManager.SaveRunningData();
                }
                return;
            }

            // Thoát EMG/ALARM cần bấm RESET — không tự chạy lại khi lỗi chập chờn tự hết (§7.4).
            if (Status.State == EMcState.Emg || Status.State == EMcState.Alarm) return;

            if (Status.State == EMcState.Init && Motion.AllConnected)
                Status.State = EMcState.NotHomed;

            if (Status.State == EMcState.Idle && !Status.OrgOk)
                Status.State = EMcState.NotHomed;
        }

        private static void UpdateOutputs()
        {
            var lamp = ComputeLamp();
            Status.TowerLamp = lamp;

            bool blinkPhase = (_clock.ElapsedMilliseconds / 250) % 2 == 0;   // 2 Hz
            bool blinkSlow = (_clock.ElapsedMilliseconds / 500) % 2 == 0;    // 1 Hz

            bool red = lamp == ETowerLamp.Red || (lamp == ETowerLamp.RedBlink && blinkPhase);
            bool yellow = lamp == ETowerLamp.Yellow || (lamp == ETowerLamp.YellowBlink && blinkSlow);
            bool green = lamp == ETowerLamp.Green || (lamp == ETowerLamp.GreenBlink && blinkSlow);

            Io.SetDo(IoMap.DoLampRed, red);
            Io.SetDo(IoMap.DoLampYellow, yellow);
            Io.SetDo(IoMap.DoLampGreen, green);

            bool wantBuzzer = AlarmManager.HasError;
            if (!wantBuzzer) Status.BuzzerMuted = false;   // alarm mới sẽ lại kêu
            Status.Buzzer = wantBuzzer && !Status.BuzzerMuted;
            Io.SetDo(IoMap.DoBuzzer, Status.Buzzer);

            // "Đang chạy" = Running/Pausing, cùng ranh giới với đèn tháp xanh liền (xem ComputeLamp) —
            // Pausing vẫn còn di chuyển tới checkpoint, chưa thật sự dừng.
            bool running = Status.State == EMcState.Running || Status.State == EMcState.Pausing;

            // Đèn ba nút Start (chung hai nút cụm A, Start 1, Start 2) y hệt nhau — bốn nút như nhau: sáng IM khi đang chạy,
            // mọi trạng thái khác TẮT, không nhấp nháy (Tuấn 2026-10-16) — kể cả Armed, tạm dừng (Held) và chờ nạp tray (CHỜ
            // THAO TÁC). Trước đó Armed/Held nháy 1 Hz, chờ tray nháy nhanh 0.1 s gọi người nạp.
            bool waitTray = Status.State == EMcState.WaitOperator;
            Io.SetDo(IoMap.DoLampStartBtn, running);
            Io.SetDo(IoMap.DoLampStartBtnB, running);
            Io.SetDo(IoMap.DoLampStartBtnB2, running);

            // Đèn nút STOP (DO09 — tên hằng số còn chữ Reset vì đèn này trước báo "cần RESET"): sáng IM
            // mọi lúc trừ khi đang chạy (Tuấn đổi 2026-09-26). Trước đó chỉ sáng khi còn alarm — alarm
            // Error thì máy đã dừng nên vẫn sáng như cũ. Chờ nạp tray giữa chừng cũng là đang chạy Auto nên tắt
            // (Tuấn 2026-10-01).
            Io.SetDo(IoMap.DoLampResetBtn, !running && !waitTray);
        }

        /// <summary>
        /// Bảng đèn tháp — PLAN.md §3.6. Tuấn chốt 2026-09-26: dừng là VÀNG, Armed là XANH NHÁY, chạy là XANH
        /// IM, lỗi là ĐỎ — xanh chỉ dành cho "đang chạy / sắp chạy". Trước đó Idle xanh nháy, Armed vàng nháy.
        /// </summary>
        private static ETowerLamp ComputeLamp()
        {
            switch (Status.State)
            {
                case EMcState.Emg:          return ETowerLamp.RedBlink;
                case EMcState.Alarm:        return ETowerLamp.Red;
                case EMcState.Running:      return ETowerLamp.Green;
                case EMcState.Pausing:      return ETowerLamp.Green;
                case EMcState.Paused:       return ETowerLamp.Yellow;
                case EMcState.Held:         return ETowerLamp.Yellow;
                case EMcState.WaitOperator: return ETowerLamp.YellowBlink;
                case EMcState.Armed:        return ETowerLamp.GreenBlink;
                case EMcState.Idle:         return AlarmManager.HasWarning ? ETowerLamp.YellowBlink : ETowerLamp.Yellow;
                case EMcState.NotHomed:     return ETowerLamp.Yellow;
                case EMcState.Homing:       return ETowerLamp.YellowBlink;
                default:                    return ETowerLamp.Off;
            }
        }
    }
}
