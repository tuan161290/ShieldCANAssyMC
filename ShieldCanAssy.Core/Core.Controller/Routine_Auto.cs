using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Threading;
using System.Threading.Tasks;
using ShieldCanAssy.Core.Communication;
using ShieldCanAssy.Core.Data;
using ShieldCanAssy.Core.Data.Model;
using ShieldCanAssy.Core.Infor;
using ShieldCanAssy.Core.Lang;
using ShieldCanAssy.Utility.Log;

namespace ShieldCanAssy.Core.Controller
{
    /// <summary>Kết quả quét ô đích theo hợp đồng mới — App cất nguyên, không cộng gì.</summary>
    public class PlacedTopRecord
    {
        public double X, Y, A;
        public int Nozzle;
    }

    /// <summary>Lệnh của lần đặt gần nhất và vị trí trục đọc được ngay khi tới nơi — cho --selftest.</summary>
    public class PlaceMoveRecord
    {
        public int Nozzle;
        public double CmdX, CmdY, ATarget;
        public double ActualX, ActualY, ActualA;
    }

    /// <summary>
    /// Chu trình chạy tự động — PLAN.md §5.2.
    ///
    /// <para><b>Cấu trúc.</b> Một vòng lặp trên <see cref="EAutoState"/> lưu trong
    /// <c>RunningData</c>, mỗi vòng làm đúng một bước rồi đặt bước kế. Trạng thái chu trình là
    /// <b>dữ liệu</b> chứ không phải vị trí con trỏ lệnh trong một hàm dài — trong CÙNG một
    /// phiên chạy, STOP rồi START lại tiếp đúng từ bước đã dừng. Nhưng trạng thái đó KHÔNG sống
    /// sót qua một lần tắt/mở app: <see cref="Data.McDataManager.LoadRunningData"/> luôn xoá nó
    /// về S00 lúc khởi động, buộc Init lại toàn bộ máy (về gốc, tray trống) mỗi lần mở app thay
    /// vì âm thầm chạy tiếp một chu trình mà không ai biết máy đã bị đụng vào gì trong lúc tắt
    /// (quyết định của Tuấn, đổi lấy sự tường minh thay vì tiện lợi resume qua mất điện).</para>
    ///
    /// <para><b>Checkpoint.</b> Yêu cầu dừng chỉ được xét ở ranh giới giữa các bước, không xét
    /// giữa lúc Z đang hạ hay đang chuyển chân không. Dừng ở giữa những đoạn đó để lại máy ở
    /// trạng thái không mô tả được: sản phẩm lơ lửng, không biết đã nhả hay chưa (§3.4).</para>
    ///
    /// <para><b>Không tự kiểm tra an toàn.</b> Mọi lệnh đi qua <see cref="McFunction"/> nên
    /// interlock Z_SAFE, soft limit và điều kiện an toàn đã được chặn ở tầng dưới. Routine chỉ
    /// observe <c>CancellationToken</c> chung (§3.3).</para>
    ///
    /// <para><b>Số lượng sản phẩm mỗi vòng là 1 hoặc 2, không cố định.</b> Hai tray có số ô
    /// khác nhau và cấu hình được (§5.3), nên tray đầu vào có thể hết ngay sau khi hút được
    /// một sản phẩm. Chu trình xử lý đúng số sản phẩm đang thực sự nằm trên tay hút thay vì
    /// giả định luôn đủ cặp.</para>
    ///
    /// <para><b>Quét trước tray lắp ráp — S05_ScanAssy (§5.2b).</b> CAM top-down soi hết các ô
    /// còn trống MỘT LẦN trước khi đặt hàng, thay vì di chuyển camera + chụp riêng cho từng ô
    /// ngay trước lúc đặt như bản đầu. Kết quả giữ trong bộ nhớ (<see cref="_assyScan"/>) và
    /// S3x_LocateAndPlace chỉ tra lại, không di chuyển/chụp nữa — đổi lấy chu kỳ nhanh hơn, đánh
    /// đổi việc không bắt được sai lệch cơ khí phát sinh GIỮA lúc quét và lúc đặt thật (quyết
    /// định của Tuấn, chấp nhận đánh đổi này để giảm cycle time). Vẫn có đường đo trực tiếp dự
    /// phòng nếu một ô nào đó thiếu trong cache, nên đặt hàng không bao giờ phụ thuộc bắt buộc
    /// vào việc đã quét trước hay chưa.</para>
    ///
    /// <para><b>App chấp hành, VisionMaster tính bù</b> (thiết kế 2026-09-13; bản rút gọn cùng ngày). Lúc soi CAM
    /// bottom-up, App gán ô đích cho tay rồi gọi <c>AlignBottom_ToolN</c> kèm pose ô (Input Params); Flow soi sản phẩm và
    /// tính luôn lệnh đặt. App so sánh (echo, ToolId, hằng số, ngưỡng, lưới teach, soft limit…), cất lệnh, rồi lúc đặt
    /// chỉ chạy trục tới đó — không có phép bù nào ở App. Sản phẩm chưa nhả thì luôn giữ trên tay khi dừng vì lỗi vision,
    /// và lần chạy sau bắt đầu lại từ S00 (Tuấn chốt 2026-09-13, D10).</para>
    /// </summary>
    public class Routine_Auto
    {
        private readonly MotionConnect _motion;
        private readonly McFunction _func;
        private readonly McStatus _status;
        private readonly Func<SystemData> _sys;
        private readonly Func<IVisionDevice> _vision;

        /// <summary>Đồng hồ của lượt đang chạy (S00→S40) — chỉ chạy khi máy làm việc: dừng lúc rời chu trình (STOP, lỗi)
        /// và lúc chờ tray, nên cycle time không lẫn thời gian đứng.</summary>
        private readonly Stopwatch _cycleSw = new Stopwatch();

        /// <summary>Bộ đếm sản phẩm lúc bắt đầu lượt — hết lượt thì hiệu là số sản phẩm của lượt.</summary>
        private int _cycleStartTotal;

        /// <summary>Giây đã chạy của lượt hiện tại — bộ đếm "cycle time hiện tại" ở trang Main.</summary>
        public double CurrentCycleSec { get { return _cycleSw.Elapsed.TotalSeconds; } }

        /// <summary>Lần đặt đang chạy đã bắt đầu nhả hút chưa — khối finally của
        /// <see cref="S3x_LocateAndPlace"/> dựa vào đây để biết ô đích còn nguyên hay không.</summary>
        private bool _releaseStarted;

        /// <summary>Lần chạy kế tiếp phải bắt đầu lại từ S00 — xem <see cref="RequestCycleRestart"/>.</summary>
        private volatile bool _restartCycle;

        /// <summary>
        /// Cho chu trình chạy lại từ S00 ở ranh giới bước kế tiếp: S00 bỏ tư thế đo cũ nên sản phẩm
        /// đang cầm được soi lại ở CAM bottom-up, S05 quét lại tray lắp ráp, rồi mới đặt. Gọi khi tray
        /// lắp ráp được nạp lại bằng tay (Tuấn chốt 2026-09-13).
        /// </summary>
        public void RequestCycleRestart() { _restartCycle = true; }

        /// <summary>Bước quay về khi S05 quét xong: S10 như thường, hoặc đúng bước soi vừa đòi quét — để chạy tiếp giữa
        /// chừng không quay lại hút thêm hàng.</summary>
        private EAutoState _afterScan = EAutoState.S10_Pick1;

        /// <summary>
        /// Kết quả quét trước (S05_ScanAssy), theo chỉ số ô của <c>Run.AssyTray</c>. Chỉ giữ trong bộ nhớ — như
        /// <see cref="_bottomVm"/>, tắt app giữa chừng thì mất, quét lại từ đầu cho an toàn thay vì tin vào số
        /// đo của một phiên chạy trước không rõ máy đã dịch chuyển gì chưa.
        ///
        /// <para><b>Chụp bằng mốc tay hút #1 cho MỌI ô, bất kể sau này tay nào đặt vào đó.</b> <c>TopX/TopY</c>
        /// là toạ độ trục để TÂM CAM trùng chi tiết đích (vị trí lúc chụp + lượng dịch khớp mẫu), không phụ thuộc
        /// tay nào đứng đó lúc chụp; Flow <c>AlignBottom_ToolN</c> cộng hand-eye của tay THẬT SỰ đặt. Không cần quét
        /// riêng cho từng tay.</para>
        /// </summary>
        private readonly Dictionary<int, PlacedTopRecord> _assyScan = new Dictionary<int, PlacedTopRecord>();

        /// <summary>Chỉ số các ô đã quét trước OK — trang Main tô xanh nhạt ô trống đã soi (Tuấn 2026-10-07). Bản chụp
        /// thay nguyên mảng mỗi lần <see cref="_assyScan"/> đổi, nên luồng giao diện đọc không cần khoá.</summary>
        public int[] AssyScannedCells { get; private set; } = new int[0];

        private void PublishAssyScan()
        {
            var cells = new int[_assyScan.Count];
            _assyScan.Keys.CopyTo(cells, 0);
            AssyScannedCells = cells;
        }

        /// <summary>Ô mà bước soi CAM dưới vừa đòi số đo nhưng chưa có — S05 soi đúng ô này trước, bất kể
        /// tuỳ chọn quét từng hàng. Không có nó thì bật quét từng hàng sẽ kẹt vòng S2x ↔ S05 khi ô được giao
        /// rơi sang hàng chưa quét. −1 = không có ô nào đang chờ.</summary>
        private int _scanOneCell = -1;

        /// <summary>Bỏ toàn bộ kết quả quét trước — gọi khi tray lắp ráp bị nạp lại/đổi mới, để
        /// không lỡ dùng số đo của tray VẬT LÝ cũ cho tray mới đang nằm ở cùng chỉ số ô.</summary>
        public void InvalidateAssyScan()
        {
            _assyScan.Clear();
            PublishAssyScan();
            _scanOneCell = -1;
            _held[1] = null;
            _held[2] = null;
            lock (_placed) _placed.Clear();
        }

        // ───────────── lệnh đã tính, cổng hiệu chuẩn ─────────────

        /// <summary>Lệnh đặt Flow AlignBottom_ToolN tính lúc soi, giữ tới lúc đặt — kèm ô đích đã gán.</summary>
        private class VmHeld
        {
            public int Cell;
            public PlacedTopRecord Top;
            public double CmdX, CmdY2, CmdA, ACap, DiagDA, DiagEcc;
        }

        private readonly VmHeld[] _held = new VmHeld[3];

        private VmCalibGate _calib;
        private readonly int[] _remeasure = new int[3];
        private int _guardSkipStreak;
        private volatile bool _invalidateRequested;
        private readonly Dictionary<int, PlacedTopRecord> _placed = new Dictionary<int, PlacedTopRecord>();

        /// <summary>
        /// Yêu cầu bỏ mọi số đo đã cất (quét ô, tư thế trên tay) ở ranh giới bước kế tiếp — gọi sau khi
        /// teach lại lưới/mốc/vị trí soi, đổi model, hiệu chuẩn lại hoặc nạp lại solution. Không xoá ngay
        /// từ luồng giao diện để khỏi giẫm lên chu trình đang chạy.
        /// </summary>
        public void RequestVisionCacheInvalidation() { _invalidateRequested = true; }

        /// <summary>Kết quả cổng hiệu chuẩn ở lần qua cổng gần nhất.</summary>
        public VmCalibGate LastCalibGate { get { return _calib; } }

        /// <summary>Lệnh của lần đặt gần nhất và vị trí trục lúc tới nơi — cho --selftest.</summary>
        public PlaceMoveRecord LastPlaceMove { get; private set; }

        /// <summary>Số quét ô đích của lần đặt đã xong vào ô <paramref name="index"/> — cho --selftest đối chiếu bù đặt
        /// (phép đo sau đặt dùng nó đã bỏ cùng Flow MeasurePlaced, 2026-10-08).</summary>
        public PlacedTopRecord GetPlacedTop(int index)
        {
            lock (_placed)
            {
                PlacedTopRecord r;
                return _placed.TryGetValue(index, out r) ? r : null;
            }
        }

        public Routine_Auto(MotionConnect motion, McFunction func, McStatus status,
                            Func<SystemData> sys, Func<IVisionDevice> vision)
        {
            _motion = motion;
            _func = func;
            _status = status;
            _sys = sys;
            _vision = vision;
        }

        /// <summary>Đặt true để chu trình dừng ở checkpoint gần nhất (STOP lần 1).</summary>
        public volatile bool PauseRequested;

        /// <summary>
        /// Tray đang ở trong máy: operator đặt tray ở vị trí chờ rồi bấm nút của nó — Start 1 tray đầu vào, Start 2 tray
        /// lắp ráp (Tuấn chốt 2026-09-26). Bước cần tray nào mà tray đó chưa vào thì đứng chờ: chưa có tray lắp ráp thì
        /// không soi, chưa có tray đầu vào thì hai tay hút không làm gì (chỉ soi trước hàng đầu tray lắp ráp). Tắt khi
        /// tray chạy ra vị trí chờ (S50, về gốc); mở App lên là chưa tray nào vào.
        /// </summary>
        private volatile bool _inputIn, _assyIn;

        /// <summary>Nút vừa nạp tray, chờ luồng chu trình nhận (<see cref="TakeTrayLoads"/>) — sổ tray chỉ đổi trên
        /// luồng chu trình.</summary>
        private int _loadInput, _loadAssy;

        /// <summary>Thông báo của lần chờ tray đang diễn ra, null khi không chờ.</summary>
        private volatile string _waiting;

        private RunningData Run { get { return McDataManager.RunData; } }
        private ModelData Model { get { return McDataManager.Model; } }

        // ───────────────────────── vòng chính ─────────────────────────

        public async Task<bool> RunAsync(CancellationToken ct)
        {
            McFunction.MarkCycleFlow();
            LogWorker.Instance.RecordLog("Bắt đầu chu trình Auto ở bước " + Run.AutoState,
                Utility.Log.LogLevel.Info, "Auto");

            try
            {
                TakeTrayLoads();
                _cycleSw.Start();          // chạy tiếp lượt dở dang; S00 thì tự đặt lại về 0

                // Cổng hiệu chuẩn chạy ở MỌI lần START, kể cả chạy tiếp giữa chừng — teach lại vị trí soi lúc
                // đang dừng phải bị chặn trước khi trục nào chạy (thiết kế R13). S00 tự gọi lại.
                if (Run.AutoState != EAutoState.S00_Ready &&
                    !_restartCycle && !Run.NozzleClearRequired && !PauseRequested)
                {
                    TakeInvalidationRequest();
                    if (!await GateAsync(ct).ConfigureAwait(false)) return false;
                }

                while (true)
                {
                    if (ct.IsCancellationRequested) return Stopped(Loc.T("Auto.Cancelled"));

                    // Bit RUN tắt (bấm STOP): trục đã dừng, đứng chờ ở đây tới khi Start bật lại — không làm bước mới.
                    // Lệnh chạy trục đang dở thì chờ ngay trong McFunction (Tuấn 2026-09-29).
                    await _func.WaitRunBitAsync(ct).ConfigureAwait(false);

                    // Vừa có sự cố với hàng trên tay hút: dừng ngay ở ranh giới bước, không làm thêm thao tác
                    // nào (Tuấn chốt 2026-09-13, an toàn). Lần bấm chạy kế tiếp McControl gỡ yêu cầu — tay còn
                    // hàng thì chạy tiếp, bỏ bước hút (Tuấn đổi 2026-09-26).
                    if (Run.NozzleClearRequired)
                        return Halt(Loc.F("Auto.HaltNozzleIncident", Run.NozzleClearReason));

                    // Checkpoint: chỉ ở đây mới xét yêu cầu dừng.
                    if (PauseRequested)
                    {
                        PauseRequested = false;
                        Save();
                        _status.State = EMcState.Paused;
                        _status.Message = Loc.F("Auto.PausedAtStep", Run.AutoState);
                        LogWorker.Instance.RecordLog(_status.Message, Utility.Log.LogLevel.Info, "Auto");
                        return true;
                    }

                    TakeInvalidationRequest();
                    TakeTrayLoads();

                    // Đồng hồ thực tế của tray lắp ráp (Tuấn 2026-10-08): bắt đầu ở bước đầu tiên chạy với tray đó trong
                    // máy — tray chưa vào hoặc đã đầy (chờ thay) thì chưa đếm; đầy thì S50 chốt. Đã đếm thì không đụng.
                    if (Run.AssyTrayStartedAt == null && _assyIn && NextFreeCell(Run.AssyTray) >= 0)
                        Run.StartAssyTrayClock(DateTime.Now);

                    // Tray lắp ráp vừa được nạp lại, hoặc bước đặt vừa dừng giữa chừng trước khi nhả hút:
                    // chạy lại từ S00 để quét tray lắp ráp và soi lại sản phẩm đang cầm trước khi đặt
                    // (Tuấn chốt 2026-09-13).
                    if (_restartCycle)
                    {
                        _restartCycle = false;
                        Run.AutoState = EAutoState.S00_Ready;
                    }

                    bool ok;
                    switch (Run.AutoState)
                    {
                        case EAutoState.S00_Ready:    ok = await S00_Ready(ct).ConfigureAwait(false); break;
                        case EAutoState.S05_ScanAssy: ok = await S05_ScanAssy(ct).ConfigureAwait(false); break;
                        case EAutoState.S10_Pick1:    ok = await S1x_Pick(1, EAutoState.S11_Pick2, ct).ConfigureAwait(false); break;
                        case EAutoState.S11_Pick2:    ok = await S1x_Pick(2, EAutoState.S20_Align1, ct).ConfigureAwait(false); break;
                        // Tay #2 gần CAM1 hơn tay #1 nên soi trước — S20_Align1/S21_Align2 vẫn là
                        // tên BƯỚC (thứ tự chạy), không phải số tay hút (Tuấn xác nhận 2026-09-05).
                        case EAutoState.S20_Align1:   ok = await S2x_Align(2, EAutoState.S21_Align2, ct).ConfigureAwait(false); break;
                        case EAutoState.S21_Align2:   ok = await S2x_Align(1, EAutoState.S30_Locate1, ct).ConfigureAwait(false); break;
                        case EAutoState.S30_Locate1:  ok = await S3x_Place(1, EAutoState.S32_Locate2, ct).ConfigureAwait(false); break;
                        case EAutoState.S32_Locate2:  ok = await S3x_Place(2, EAutoState.S40_Index, ct).ConfigureAwait(false); break;
                        case EAutoState.S40_Index:    ok = S40_Index(); break;
                        case EAutoState.S50_TrayCheck: ok = await S50_TrayCheck(ct).ConfigureAwait(false); break;

                        // S31/S33 nằm trong S30/S32: lệnh đặt đã tính lúc soi (S20/S21), S30/S32 chỉ
                        // kiểm rồi chạy trục tới lệnh.
                        case EAutoState.S31_Place1: Run.AutoState = EAutoState.S32_Locate2; ok = true; break;
                        case EAutoState.S33_Place2: Run.AutoState = EAutoState.S40_Index; ok = true; break;

                        default:
                            return Stopped(Loc.F("Auto.InvalidStep", Run.AutoState));
                    }

                    _status.AutoStep = Run.AutoState;
                    UpdateCamLamp();
                    if (!ok) return false;
                }
            }
            catch (OperationCanceledException)
            {
                return Stopped(Loc.T("Auto.Cancelled"));
            }
            catch (Exception ex)
            {
                AlarmManager.RaiseEvent(AlarmCode.RoutineCrashed, "Auto", ex.Message);   // sự kiện: RESET gỡ được (30/09)
                return Stopped(Loc.F("Auto.UnexpectedError", ex.Message));
            }
            finally
            {
                // Chốt an toàn: rời chu trình bằng đường nào — dừng, NG, lỗi, huỷ — đèn CAM cũng phải tắt.
                // Không còn try/finally quanh từng lần gọi Flow nữa nên đây là chỗ duy nhất bảo đảm điều đó.
                TurnCamLampOff();
                _cycleSw.Stop();
            }
        }

        /// <summary>Bit RUN tắt thì đồng hồ lượt cũng đứng — thời gian tạm dừng không tính vào cycle time.</summary>
        public void HoldClock(bool hold)
        {
            if (hold) _cycleSw.Stop();
            else _cycleSw.Start();
        }

        private void TakeInvalidationRequest()
        {
            if (!_invalidateRequested) return;
            _invalidateRequested = false;
            ClearVisionCaches("teach lại, đổi model, hiệu chuẩn lại hoặc nạp lại solution");
        }

        private void ClearVisionCaches(string why)
        {
            _assyScan.Clear();
            PublishAssyScan();
            _scanOneCell = -1;
            _held[1] = null;
            _held[2] = null;
            LogWorker.Instance.RecordLog("Bỏ số đo đã cất (quét ô, tư thế trên tay) — " + why + "; sẽ quét và soi lại",
                Utility.Log.LogLevel.Info, "Auto");
        }

        // ───────────────────────── S00 ─────────────────────────

        private async Task<bool> S00_Ready(CancellationToken ct)
        {
            if (!await GateAsync(ct).ConfigureAwait(false)) return false;

            // Vào vòng mới: dọn trạng thái đo của vòng trước để không có đường nào dùng nhầm
            // tư thế của sản phẩm đã đặt xong.
            _held[1] = null;
            _held[2] = null;
            ReleaseOrphanCells();
            _cycleSw.Restart();
            _cycleStartTotal = Run.CountTotal;

            if (!await _func.EnsureZSafeAsync(ct).ConfigureAwait(false))
                return Halt(Loc.T("Auto.ZSafeFail"));

            _afterScan = EAutoState.S10_Pick1;
            Run.AutoState = EAutoState.S05_ScanAssy;
            return true;
        }

        // ───────────────────────── S05 — quét trước tray lắp ráp ─────────────────────────

        /// <summary>
        /// Quét đúng MỘT ô còn trống chưa có trong <see cref="_assyScan"/> mỗi lần gọi — giữ đúng
        /// nguyên tắc "một bước, một việc" của cả state machine này để checkpoint dừng vẫn xét
        /// được sau mỗi ô thay vì phải chờ quét hết cả tray mới có chỗ dừng. Hết ô cần quét thì
        /// sang thẳng S10 — sau lần quét đầu của một tray, các vòng S00 sau chỉ tốn một lượt kiểm
        /// tra rỗng rồi đi tiếp ngay, không quét lại (§5.2b).
        /// </summary>
        private async Task<bool> S05_ScanAssy(CancellationToken ct)
        {
            if (!_assyIn)
            {
                // Chưa có tray lắp ráp thì không soi (Tuấn chốt 2026-09-26). Tray đầu vào đã nạp mà còn nằm ở vị trí
                // chờ thì đưa vào máy trước; Start 1 đến giữa lúc chờ cũng đánh thức vòng chờ để đưa tray vào.
                if (!await MoveInputTrayInAsync(ct).ConfigureAwait(false))
                    return Halt(Loc.T("Auto.InputTrayInFail"));
                bool hadInput = _inputIn;
                return await WaitTrayAsync(() => _assyIn || _inputIn != hadInput, ct).ConfigureAwait(false);
            }

            var tray = Run.AssyTray;
            int index = NextScanCell(tray);
            if (index < 0)
            {
                Run.AutoState = _afterScan;
                _afterScan = EAutoState.S10_Pick1;
                return true;
            }

            int row, col;
            tray.RowCol(index, out row, out col);

            // Mốc tay #1 cố định cho mọi ô — xem ghi chú ở _assyScan. Vị trí camera lấy từ mốc đã
            // teach trực tiếp (CamTopDownAnchorX/Y) cộng độ lệch lưới của ô so với ô (1,1) — không
            // còn qua Hand-Eye Dx/Dy nữa (Tuấn xác nhận 2026-09-05). Theo lưới tay nào thì xem
            // TrayLayout.CamGridZAxis; đừng chép lại phép tính ra đây.
            var cam = Model.AssyTray.CamPosFor(row, col);
            double camX = cam.X, camY = cam.Y;

            if (!await _func.MoveToCellAsync(Model.AssyTray.AxisRow, camY, camX, ct).ConfigureAwait(false))
                return Halt(Loc.F("Auto.ScanMoveFail", row, col));

            string cellText = "(" + row + "," + col + ")";
            var read = await LocateTopVmAsync(ct).ConfigureAwait(false);
            VisionReadout.TopDown(read, row, col);
            if (!read.CallOk) return VmHalt(0, -1, Loc.F("Auto.ScanLocateTop", cellText, read.CallError));
            if (!read.EchoOk) return VmHalt(0, -1, Loc.F("Auto.ScanLocateTop", cellText, read.EchoIssue));

            // Không thấy chi tiết đích ngay từ vòng quét: bỏ ô này luôn, không chờ tới lúc đặt mới phát hiện.
            if (!read.Found) return SkipCellNotFound(tray, index, Loc.F("Auto.ScanLocateTop", cellText, Loc.T("Auto.SolutionNotFound")));
            if (!read.VmValid) return VmHalt(0, -1, Loc.F("Auto.ScanLocateTop", cellText, read.VmIssue));

            _assyScan[index] = new PlacedTopRecord { X = read.X, Y = read.Y, A = read.A };
            PublishAssyScan();
            Save();
            return true;          // ở nguyên S05 cho tới khi NextUnscannedCell hết ô
        }

        private bool SkipCellNotFound(TrayState tray, int index, string detail)
        {
            AlarmManager.Raise(AlarmCode.VisionNotFound, "Vision", detail);
            tray.Set(index, ECellState.Skipped);
            Run.AssyCountNg++;
            AlarmManager.Dismiss(AlarmCode.VisionNotFound);
            Save();
            return true;
        }

        /// <summary>
        /// Ô S05 soi lượt này: ưu tiên ô mà bước soi CAM dưới vừa đòi (<see cref="_scanOneCell"/>), sau đó mới
        /// tới ô chưa quét theo <see cref="NextUnscannedCell"/>.
        /// </summary>
        private int NextScanCell(TrayState tray)
        {
            int one = _scanOneCell;
            _scanOneCell = -1;
            if (one >= 0 && one < tray.CellCount && tray.Get(one) == ECellState.Empty && !_assyScan.ContainsKey(one))
                return one;
            return NextUnscannedCell(tray);
        }

        /// <summary>
        /// Ô lắp ráp còn trống và CHƯA có kết quả quét trước. Bật <see cref="SystemData.ScanAssyByRow"/> thì chỉ
        /// xét HÀNG đang làm — hàng của ô trống đầu tiên, tức hàng mà <see cref="AssignCell"/> sắp giao tới. Tray đầu vào
        /// chưa vào cũng chỉ xét hàng đó: soi trước hàng đầu trong lúc chờ Start 1 (Tuấn chốt 2026-09-26).
        /// </summary>
        private int NextUnscannedCell(TrayState tray)
        {
            int onlyRow = -1;
            if (_sys().ScanAssyByRow || !_inputIn)
            {
                int first = -1;
                for (int i = 0; i < tray.CellCount; i++)
                    if (tray.Get(i) == ECellState.Empty) { first = i; break; }
                if (first < 0) return -1;
                int r0, c0;
                tray.RowCol(first, out r0, out c0);
                onlyRow = r0;
            }

            for (int i = 0; i < tray.CellCount; i++)
            {
                if (tray.Get(i) != ECellState.Empty || _assyScan.ContainsKey(i)) continue;
                if (onlyRow >= 0)
                {
                    int r, c;
                    tray.RowCol(i, out r, out c);
                    if (r != onlyRow) continue;
                }
                return i;
            }
            return -1;
        }

        /// <summary>Điều kiện máy cho phép chạy — PLAN.md §5.1, xét lại ở đầu mỗi vòng. Điều kiện hiệu chuẩn nằm ở
        /// <see cref="GateAsync"/>.</summary>
        private bool CanRun(out string why)
        {
            why = null;

            if (!_status.OrgOk) { why = Loc.T("Auto.NotHomed"); return false; }
            if (_status.Mode != EMcMode.Auto) { why = Loc.T("Auto.KeyNotAuto"); return false; }
            if (AlarmManager.HasError) { why = Loc.T("Auto.AlarmNotCleared"); return false; }

            var model = Model;
            if (model == null) { why = Loc.T("Auto.NoModel"); return false; }
            if (!model.InputTray.IsTaught) { why = Loc.T("Auto.InputTrayNotTaught"); return false; }
            if (!model.AssyTray.IsTaught) { why = Loc.T("Auto.AssyTrayNotTaught"); return false; }
            // Kiểm tra ở đây để "chưa nạp solution" hiện ra đúng như nó là, chứ không biến thành
            // một chuỗi NG vision mà người vận hành sẽ đi tìm nguyên nhân ở camera và đèn.
            var vision = _vision();
            if (vision == null || !vision.IsConnected) { why = Loc.T("Auto.VisionNotConnected"); return false; }
            if (!vision.IsSolutionLoaded) { why = Loc.T("Auto.SolutionNotLoaded"); return false; }

            // Chỉ tray đã vào máy: tray chưa nạp thì vắng ở vị trí chờ là chuyện bình thường, bước cần nó tự chờ.
            if (_inputIn && TrayMissing(model.InputTray.AxisRow, _func.HasTrayIn()))
            { why = Loc.T("Auto.NoInputTray"); return false; }
            if (_assyIn && TrayMissing(model.AssyTray.AxisRow, _func.HasTrayAssy()))
            { why = Loc.T("Auto.NoAssyTray"); return false; }

            return true;
        }

        /// <summary>
        /// Cửa sổ quanh vị trí chờ mà trong đó phép đọc cảm biến tray còn có nghĩa. Trục được LỆNH chạy tới
        /// đúng vị trí chờ nên chỉ lệch cỡ cửa sổ INP của driver; 1 mm là rộng rãi.
        /// </summary>
        public const double TraySensorWindowMm = 1.0;

        /// <summary>
        /// Có <b>kết luận được</b> là thiếu tray hay không.
        ///
        /// <para>Cảm biến "có tray" gắn CỐ ĐỊNH ở vị trí chờ của trục hàng, không chạy theo tray (Tuấn nêu
        /// 2026-09-24). Nên chỉ khi trục đang đứng ở vị trí chờ thì tín hiệu OFF mới có nghĩa là "thiếu tray".
        /// Tray đã chạy vào vùng làm việc thì thứ duy nhất ta biết là <b>không đo được</b> — mà không đo được
        /// thì không được phép dừng máy.</para>
        ///
        /// <para>Trước 2026-09-24 <see cref="CanRun"/> đọc thẳng hai cảm biến ở đầu MỖI vòng, nên từ vòng thứ
        /// hai trở đi máy sẽ dừng với "không có tray đầu vào" dù tray nằm nguyên tại chỗ. Chưa ai gặp vì chu
        /// trình Auto còn đang bị cổng hiệu chuẩn chặn.</para>
        /// </summary>
        public static bool TrayMissingAt(double axisPos, double standbyPos, bool sensorOn)
        {
            return !sensorOn && Math.Abs(axisPos - standbyPos) <= TraySensorWindowMm;
        }

        private bool TrayMissing(string rowAxis, bool sensorOn)
        {
            var axis = _motion[rowAxis];
            if (axis == null) return false;
            return TrayMissingAt(axis.CurrentPos, _sys().GetAxis(rowAxis).StandbyPos, sensorOn);
        }

        /// <summary>
        /// Cổng hiệu chuẩn: điều kiện máy như <see cref="CanRun"/>, rồi cổng phía App — Bước 2/4/5, vị trí soi, ngưỡng.
        /// Chưa sẵn sàng thì chặn.
        /// </summary>
        private Task<bool> GateAsync(CancellationToken ct)
        {
            string why;
            if (!CanRun(out why)) return Task.FromResult(Halt(why));

            var sys = _sys();
            string axisProblem = VmCycle.AxisVarProblem(sys.VisionOutputs);
            if (axisProblem != null) return Task.FromResult(Halt(axisProblem));
            if (!Model.AssyTray.CamTopDownAnchorTaught)
                return Task.FromResult(Halt(Loc.T("Auto.TopAnchorNotTaught")));
            if (Model.AssyTray.AxisRow != AxisId.Y2)
                return Task.FromResult(Halt(Loc.T("Auto.AssyRowAxisY2")));

            // Chỉ một tay chạy thì chỉ đòi hiệu chuẩn của tay đó — trừ khi tay kia còn cầm hàng, hàng đó vẫn phải soi và đặt.
            int only = sys.OnlyNozzle == 1 || sys.OnlyNozzle == 2 ? sys.OnlyNozzle : 0;
            if (only != 0 && Nozzle(3 - only).HasProduct) only = 0;
            var gate = VmCycle.EvaluateCalib(Model, sys, only);
            _calib = gate;
            if (gate.Block != null)
            {
                string blockWhy = Loc.F("Auto.CalibNotReady", gate.Block);
                AlarmManager.RaiseEvent(AlarmCode.VisionCalibNotReady, "Vision", blockWhy);
                return Task.FromResult(Halt(blockWhy));
            }

            if (VmMath.IsFinite(gate.DaysSinceOldest) && gate.DaysSinceOldest > sys.CalibValidDays)
                AlarmManager.Raise(AlarmCode.VisionCalibExpired, "Vision",
                    Loc.F("Auto.CalibExpired", (int)gate.DaysSinceOldest));
            else
                AlarmManager.ClearCondition(AlarmCode.VisionCalibExpired);   // đã hiệu chuẩn lại thì gỡ
            return Task.FromResult(true);
        }

        // ───────────────────────── S10 / S11 — hút ─────────────────────────

        private async Task<bool> S1x_Pick(int nozzle, EAutoState next, CancellationToken ct)
        {
            var nz = Nozzle(nozzle);

            // Đã có sản phẩm sẵn trên tay (chạy tiếp sau khi dừng) thì bỏ qua bước hút. Tay đang tắt (chế độ chỉ một tay) cũng không hút.
            if (nz.HasProduct || !_sys().NozzleEnabled(nozzle)) { Run.AutoState = next; return true; }

            // Còn ô trống CHƯA quét: tray lắp ráp vừa được nạp lại bằng tay lúc máy dừng giữa vòng,
            // hoặc một lần đặt dừng giữa chừng vừa trả ô về trống. Quét xong đã rồi mới đếm, không
            // thì phép đếm dưới đây tính cả những ô chưa biết có chi tiết đích hay không (rà soát
            // 2026-09-13). S05 xong tự về S10; tay nào đã cầm hàng thì S10/S11 bỏ qua như thường.
            if (NextUnscannedCell(Run.AssyTray) >= 0)
            {
                _afterScan = EAutoState.S10_Pick1;
                Run.AutoState = EAutoState.S05_ScanAssy;
                return true;
            }

            // Chỉ hút khi còn chỗ đặt (Tuấn chốt 2026-09-13): số ô lắp ráp còn trống phải NHIỀU
            // HƠN số sản phẩm đang nằm trên tay hút. Qua dòng kiểm tra trên thì ô trống nào cũng là
            // ô quét thấy chi tiết đích — ô không thấy đã thành Skipped. Tay #1 hút trước, nên còn
            // đúng một ô thì chỉ tay #1 làm.
            int held = (Run.Nozzle1.HasProduct ? 1 : 0) + (Run.Nozzle2.HasProduct ? 1 : 0);
            int free = CountFreeCells(Run.AssyTray);
            if (free <= held)
            {
                // Còn ô cho hàng đang cầm thì đi soi/đặt nốt. Hết hẳn ô thì đi đổi tray ngay — hàng
                // đang cầm (nếu có) nằm chờ trên tay, S50 chỉ báo Warning (Tuấn chốt 2026-09-13).
                Run.AutoState = held > 0 && free > 0 ? EAutoState.S20_Align1 : EAutoState.S50_TrayCheck;
                return true;
            }

            // Chưa có tray đầu vào thì không hút (Tuấn chốt 2026-09-26) — chờ Start 1. Hàng đang cầm thì soi và đặt nốt
            // trước, chỉ cần tray lắp ráp. Nạp xong thì làm lại bước này từ đầu: tray lắp ráp mới soi hàng đầu, giờ mới
            // quét nốt.
            if (!_inputIn)
            {
                if (held > 0) { Run.AutoState = EAutoState.S20_Align1; return true; }
                return await WaitTrayAsync(() => _inputIn, ct).ConfigureAwait(false);
            }

            var tray = Run.InputTray;
            int index = AssignPickCell(nozzle);
            if (index < 0)
            {
                // Hết tray đầu vào. Nếu tay #1 cũng chưa có gì thì phải đổi tray trước khi
                // làm gì tiếp; nếu tay #1 đã có hàng thì chạy nốt sản phẩm đó rồi mới đổi.
                if (nozzle == 2 && Nozzle(1).HasProduct) { Run.AutoState = EAutoState.S20_Align1; return true; }
                Run.AutoState = EAutoState.S50_TrayCheck;
                return true;
            }

            tray.CurrentIndex = index;
            int row, col;
            tray.RowCol(index, out row, out col);

            string zName = nozzle == 1 ? AxisId.Z1 : AxisId.Z2;
            var cell = Model.InputTray.CellPos(zName, row, col);

            tray.Set(index, ECellState.Processing);

            int retries = _sys().PickRetryCount;
            // 5001 hiện trong lúc hút lại, hết bước hút thì tự rời danh sách ở MỌI đường ra — hút được ở lần sau, hết
            // lần thử (sang ô khác), Halt hay STOP — không cần RESET (Tuấn 2026-09-29; trước đó nằm lại chờ RESET).
            try
            {
                for (int attempt = 0; attempt <= retries; attempt++)
                {
                    // A về góc hút đã teach, song song với X/Y: đặt xong A còn nằm ở CmdA, xuống hút bằng góc đó
                    // thì gờ đầu hút không ăn khớp và chân không không đạt — xem McFunction.MoveToCellForPickAsync.
                    if (!await _func.MoveToCellForPickAsync(nozzle, Model.PickAngle(nozzle), Model.InputTray.AxisRow, cell.Y, cell.X, ct).ConfigureAwait(false))
                        return Halt(Loc.F("Auto.PickMoveFail", row, col));

                    // Hút hụt thì hạ thêm một ít: nguyên nhân phổ biến nhất là sản phẩm nằm thấp
                    // hơn dự kiến trong hốc tray, không phải hỏng chân không (§5.4). Z dương là
                    // chiều ĐI XUỐNG (Tuấn xác nhận 2026-09-04) nên "hạ thêm" là CỘNG thêm, không
                    // phải trừ.
                    double zPick = Model.InputTray.ZPickFor(zName) + attempt * _sys().PickRetryDeltaZ;

                    // Tray đầu vào: qua điểm giảm tốc hay không theo Setting (Tuấn 2026-10-07) — mặc định hạ một mạch mức cao
                    // (tay trống, không có gì để va), chỉ NÂNG mới chậm cho sản phẩm không giật rơi (Tuấn 2026-09-30).
                    if (!await _func.MoveZDownAsync(zName, zPick, Model.InputTray.ZSlowFor(zName, _sys().InputZSlowDown), ct).ConfigureAwait(false))
                        return Halt(Loc.F("Auto.ZDownPickFail", zName));

                    _func.SetVacuum(nozzle, true);
                    bool gripped = await WaitVacuumAsync(nozzle, true, ct).ConfigureAwait(false);
                    await SettleVacuumAsync(gripped, ct).ConfigureAwait(false);

                    if (!await _func.MoveZUpAsync(zName, SafeZ(), Model.InputTray.ZSlowFor(zName, _sys().InputZSlowUp), ct).ConfigureAwait(false))
                        return Halt(Loc.F("Auto.ZUpFail", zName));

                    if (gripped)
                    {
                        nz.HasProduct = true;
                        nz.SourceCellIndex = index;
                        nz.Aligned = false;
                        Run.AutoState = next;
                        return true;
                    }

                    _func.SetVacuum(nozzle, false);
                    AlarmManager.Raise(AlarmCode.PickFailed, "Auto",
                        Loc.F("Auto.PickRetry", nozzle, row, col, attempt + 1, retries + 1));
                }
            }
            finally
            {
                AlarmManager.Dismiss(AlarmCode.PickFailed);
            }

            // Hết số lần thử: đánh dấu ô hỏng và đi tiếp, KHÔNG dừng máy. Một ô lỗi không phải
            // lý do để cả tray đứng lại (§5.4).
            tray.Set(index, ECellState.Ng);
            CountNg("hút hụt ô (" + row + "," + col + ")");
            return CheckConsecutiveNg();
        }

        // ───────────────────────── S20 / S21 — soi và tính lệnh đặt ─────────────────────────

        /// <summary>
        /// Đưa tay hút vào khung hình CAM bottom-up, soi sản phẩm VÀ tính lệnh đặt trong cùng một lần chạy
        /// AlignBottom_ToolN (bản rút gọn 2026-09-13, Tuấn đề xuất): App gán ô đích cho tay ngay tại đây, gửi pose ô — số
        /// quét ở S05 — qua Input Params, Flow trả CmdX/CmdY2/CmdA. App so sánh rồi cất lệnh tới lúc đặt.
        ///
        /// <para>Lợi hơn tính lúc đặt: vượt ngưỡng thì trả sản phẩm về ô nguồn ngay, chưa phải chạy ra tray; ô lệch lưới
        /// thì bỏ ô và soi lại với ô kế khi tay vẫn đứng trên camera. Không mất độ chính xác: lệnh chỉ dựa vào ảnh chụp ở
        /// bước này, lúc đặt không có thông tin mới nào.</para>
        ///
        /// <para><b>Không xoay ở bước này.</b> Toàn bộ lượng xoay dồn vào lúc di chuyển tới ô đặt, khi đó trục A chạy song
        /// song với X/Y2 nên không tốn thêm giây nào của chu kỳ.</para>
        /// </summary>
        private async Task<bool> S2x_Align(int nozzle, EAutoState next, CancellationToken ct)
        {
            var nz = Nozzle(nozzle);
            if (!nz.HasProduct) { Run.AutoState = next; return true; }
            if (_held[nozzle] != null) { Run.AutoState = next; return true; }

            // Soi và đặt hàng đang cầm chỉ cần tray lắp ráp — chưa có thì về S05 chờ. Không chờ tray đầu vào: soi hỏng mà
            // không trả về được thì báo 5031, người lấy hàng ra (Tuấn chốt 2026-09-26) — xem ReturnToSourceAsync.
            if (!_assyIn) return NeedAssyTray();

            var sys = _sys();
            var names = sys.VisionOutputs;
            var tray = Run.AssyTray;

            int index = AssignCell(nozzle);
            if (index < 0)
            {
                // Không còn ô cho tay này — chỉ khi có sự cố xen giữa, vì S1x không cho hút quá số ô quét thấy chi tiết
                // đích. Giữ sản phẩm trên tay để tay kia (nếu có lệnh) đặt trước; S3x không có lệnh và không có ô thì đi
                // tiếp, S50 báo Warning và chờ đổi tray (Tuấn chốt 2026-09-13).
                LogWorker.Instance.RecordLog("Hết ô lắp ráp cho tay #" + nozzle + " — giữ sản phẩm, chờ đổi tray lắp ráp",
                    Utility.Log.LogLevel.Warning, "Auto");
                Run.AutoState = next;
                return true;
            }

            // Pose ô đích lấy từ lần quét trước (S05). Thiếu thì quét những ô chưa quét rồi quay lại đúng bước soi này —
            // S1x đã bảo đảm quét đủ nên chỉ gặp khi số đo vừa bị bỏ giữa chừng.
            PlacedTopRecord top;
            if (!_assyScan.TryGetValue(index, out top))
            {
                _scanOneCell = index;
                _afterScan = Run.AutoState;
                Run.AutoState = EAutoState.S05_ScanAssy;
                return true;
            }

            // Soi ở đúng Z_SAFE — cùng cao độ mọi bước hiệu chuẩn bottom-up chụp
            // (Routine_Calib.MoveZToBottomCaptureAsync). Vừa hút xong thì Z đã ở đó; lệnh này giữ đúng cả khi chạy
            // tiếp sau PAUSE mà Z đã bị jog.
            string zAxis = nozzle == 1 ? AxisId.Z1 : AxisId.Z2;
            if (!await _func.MoveAsync(zAxis, SafeZ(), ZSpeed(zAxis), ct).ConfigureAwait(false))
                return Halt(Loc.F("Auto.ZAlignHeightFail", zAxis));

            double alignX = nozzle == 1 ? Model.AlignPosX_Nozzle1 : Model.AlignPosX_Nozzle2;
            if (!await _func.MoveAsync(AxisId.X, alignX, MoveSpeed(AxisId.X), ct).ConfigureAwait(false))
                return Halt(Loc.F("Auto.AlignMoveFail", nozzle));

            int row, col;
            tray.RowCol(index, out row, out col);
            string cellText = "(" + row + "," + col + ")";

            // Bù đặt của tay này cho ô này = bù chung mọi ô + bù riêng của ô (trang Teaching nhập) — cộng vào đích
            // trước khi gửi, VisionMaster không biết có lượng bù nào.
            var trim = Model.AssyTray.PlaceTrim(zAxis, row, col);

            await _func.WaitSettleAsync(EVmCamera.BottomUp, ct).ConfigureAwait(false);
            int rid = VmRunIds.Next();
            var g = VmCycle.AxisGlobals(_motion, names, rid);
            VmCycle.AddPlaceGlobals(g, top, Model.Calib, nozzle, trim);
            double aSent = VmCycle.Val(g, nozzle == 1 ? names.GvAxisA1 : names.GvAxisA2);
            var req = VmCycle.AlignBottomRequest(names, nozzle, g, true);
            var r = await CallVmAsync(req, ct).ConfigureAwait(false);
            var read = VmCycle.ReadAlignBottom(r, req, names, nozzle);
            VisionReadout.Tool(nozzle, read, null, row, col);
            // Đối chiếu Group Cmd với phép tính lại của App: chỉ ghi khi KHÔNG khớp — mỗi lần soi một dòng dài là log thừa
            // khi máy chạy đúng (Tuấn 2026-10-01 bỏ log đo đạc). Các nút trang Vision Edit vẫn ghi đủ vết.
            var trace = VmCycle.TraceAlignBottom(r, req, names, nozzle, _motion[zAxis]?.CurrentPos ?? double.NaN);
            if (trace.Checked && !trace.Match)
                LogWorker.Instance.RecordLog("Tay #" + nozzle + " ô " + cellText + " — " + trace.Line, Utility.Log.LogLevel.Warning, "Auto");
            if (!read.CallOk) return VmHalt(nozzle, -1, Loc.F("Auto.AlignBottomNozzle", nozzle, read.CallError));
            if (!read.EchoOk) return VmHalt(nozzle, -1, Loc.F("Auto.AlignBottomNozzle", nozzle, read.EchoIssue));
            if (!read.Found) return await AlignNotFoundAsync(nozzle, Loc.T("Auto.SolutionNotFound"), ct).ConfigureAwait(false);
            if (!read.VmValid) return VmHalt(nozzle, -1, Loc.F("Auto.AlignBottomNozzle", nozzle, read.VmIssue));

            // Tâm hàng rào lưới dời theo đúng lượng bù đặt: đích gửi đi đã lệch lưới teach từng ấy rồi,
            // để nguyên tâm cũ là lượng bù ăn dần vào ngưỡng ±PlaceTargetGuardMm.
            var cellPos = Model.AssyTray.CellPos(zAxis, row, col);
            double guardX = cellPos.X + (trim == null ? 0 : trim.PlaceDx);
            double guardY = cellPos.Y + (trim == null ? 0 : trim.PlaceDy);
            var cmd = VmCycle.CheckPlace(r, req, nozzle, aSent, guardX, guardY, sys.PlaceTargetGuardMm,
                sys.MaxCompensationAngleDeg, sys.MaxCompensationDistance,
                sys.GetAxis(AxisId.X).InSoftLimit, sys.GetAxis(Model.AssyTray.AxisRow).InSoftLimit, true);
            VisionReadout.Tool(nozzle, read, cmd, row, col);

            switch (cmd.Route)
            {
                case EVmRoute.Ok:
                    break;

                case EVmRoute.Ng:
                    // N — vision đo nhầm vật hoặc sản phẩm hút quá lệch: trả về ô nguồn ngay, tính NG.
                    AlarmManager.Raise(AlarmCode.CompensationOutOfRange, "Auto", Loc.F("Auto.NozzleCellWhy", nozzle, cellText, cmd.Why));
                    // Tự rời danh sách ở mọi đường ra, không cần RESET — trả về không được thì 5031/lỗi trục mới là cái chặn máy.
                    try { if (!await ReturnToSourceAsync(nozzle, ct).ConfigureAwait(false)) return false; }
                    finally { AlarmManager.Dismiss(AlarmCode.CompensationOutOfRange); }
                    CountNg("lượng bù vượt ngưỡng tay #" + nozzle);
                    return CheckConsecutiveNg();

                case EVmRoute.Skip:
                    // G — lỗi nằm ở ô đích chứ không ở sản phẩm: bỏ ô, soi lại với ô kế (tay vẫn đứng trên camera).
                    tray.Set(index, ECellState.Skipped);
                    Run.AssyCountNg++;
                    _guardSkipStreak++;
                    AlarmManager.Raise(AlarmCode.PlaceTargetGuard, "Auto", Loc.F("Auto.NozzleCellWhy", nozzle, cellText, cmd.Why));
                    AlarmManager.Dismiss(AlarmCode.PlaceTargetGuard);
                    Save();
                    if (_guardSkipStreak >= Math.Max(1, sys.MaxGuardSkipStreak))
                        return VmHalt(nozzle, -1, Loc.F("Auto.GuardSkipStreak", _guardSkipStreak, cmd.Why));
                    LogWorker.Instance.RecordLog("Bỏ ô " + cellText + " — " + cmd.Why + "; giữ sản phẩm tay #" + nozzle + ", soi lại với ô kế",
                        Utility.Log.LogLevel.Warning, "Auto");
                    return true;

                default:
                    return VmHalt(nozzle, -1, cmd.Why);
            }

            _guardSkipStreak = 0;
            _held[nozzle] = new VmHeld
            {
                Cell = index, Top = top, CmdX = cmd.CmdX, CmdY2 = cmd.CmdY2, CmdA = cmd.CmdA, ACap = aSent,
                DiagDA = cmd.DiagDA, DiagEcc = cmd.DiagEcc
            };
            nz.MeasuredAngle = read.A;
            nz.Aligned = true;
            Run.AutoState = next;
            return true;
        }

        /// <summary>
        /// Ô đích cho tay <paramref name="nozzle"/>: ô trống theo thứ tự tray, trừ ô tay kia đang giữ lệnh. Tay #1 luôn nhận
        /// ô trước tay #2 (S30 đặt tay #1 trước), kể cả khi S20 soi tay #2 trước: tay #2 chừa ô đầu cho tay #1 nếu tay #1
        /// đang cầm hàng mà chưa có lệnh.
        /// </summary>
        private int AssignCell(int nozzle)
        {
            var tray = Run.AssyTray;
            int other = nozzle == 1 ? 2 : 1;
            var otherHeld = _held[other];
            int reserved = otherHeld != null ? otherHeld.Cell : -1;

            int first = -1, second = -1;
            for (int i = 0; i < tray.CellCount; i++)
            {
                if (tray.Get(i) != ECellState.Empty || i == reserved) continue;
                if (first < 0) first = i;
                else { second = i; break; }
            }
            if (first < 0) return -1;

            // Tay kia cũng đang cầm hàng mà chưa nhận ô → hai tay sắp chia nhau CẶP ô này.
            bool sharing = otherHeld == null && Nozzle(other).HasProduct && second >= 0;
            if (!sharing) return first;

            return Nozzle1TakesFirstCell(CellX(first), CellX(second)) == (nozzle == 1) ? first : second;
        }

        /// <summary>
        /// Trong một cặp ô, tay #1 có nhận ô ĐƯỢC GHÉ TRƯỚC hay không — quyết theo <b>hình học</b>, không theo
        /// thứ tự ghé: tay #1 nhận ô có X nhỏ hơn, tay #2 nhận ô có X lớn hơn.
        ///
        /// <para><b>Vì sao.</b> Tay #1 nằm lùi khoảng 63 mm so với tay #2, xa hơn một bước ô (25 mm). Nên điểm
        /// X xa nhất mà gantry phải với tới trong cả vòng chính là chỗ đứng để TAY #1 lên ô của nó. Giao cho
        /// tay #1 ô gần hơn thì gantry với ngắn lại đúng một bước ô, mà quãng đường một vòng bằng
        /// <c>2 × (xa nhất − gần nhất)</c> nên khoản đó được trả hai lần, đi và về.</para>
        ///
        /// <para>Quét tuần tự thì ô ghé trước cũng chính là ô X nhỏ hơn, nên luật này trùng với hành vi cũ —
        /// không đổi gì. Quét zigzag thì hàng chạy ngược đảo lại, và luật cũ (cứ ô ghé trước là của tay #1)
        /// bắt gantry với thêm một bước ô ở mọi vòng của những hàng ấy, ăn mất quá nửa phần lợi mà zigzag
        /// kiếm được ở chặng quét tray (Tuấn hỏi 2026-09-24).</para>
        /// </summary>
        public static bool Nozzle1TakesFirstCell(double xFirst, double xSecond)
        {
            return xFirst <= xSecond;
        }

        /// <summary>Toạ độ X của gantry để tay #1 đứng trên ô — thước đo "ô nào xa hơn". So bằng lưới của một
        /// tay là đủ: lưới hai tay chỉ lệch nhau một hằng số.</summary>
        private double CellX(int index)
        {
            int row, col;
            Run.AssyTray.RowCol(index, out row, out col);
            return Model.AssyTray.CellPos(AxisId.Z1, row, col).X;
        }

        /// <summary>Align fail: trả sản phẩm về ĐÚNG ô cũ rồi đánh dấu ô đó NG. Thả bừa ở đâu đó là mất
        /// dấu sản phẩm và lần về gốc sau sẽ đâm vào nó (§5.4).</summary>
        private async Task<bool> AlignNotFoundAsync(int nozzle, string detail, CancellationToken ct)
        {
            AlarmManager.Raise(AlarmCode.VisionNotFound, "Vision",
                detail == null ? Loc.F("Auto.AlignBottomNozzleOnly", nozzle) : Loc.F("Auto.AlignBottomNozzle", nozzle, detail));
            // Tự rời danh sách ở mọi đường ra, không cần RESET — trả về không được thì 5031/lỗi trục mới là cái chặn máy.
            try { if (!await ReturnToSourceAsync(nozzle, ct).ConfigureAwait(false)) return false; }
            finally { AlarmManager.Dismiss(AlarmCode.VisionNotFound); }
            CountNg("align fail tay #" + nozzle);
            return CheckConsecutiveNg();
        }

        // ───────────────────────── S30–S33 — đặt ─────────────────────────

        /// <summary>
        /// Đặt sản phẩm của tay <paramref name="nozzle"/> theo lệnh đã tính lúc soi — bước này không gọi Flow nào. Trước khi
        /// chạy trục vẫn kiểm: trục A chưa xoay kể từ lúc chụp (nhánh R), ô đích vẫn trống.
        /// </summary>
        private async Task<bool> S3x_Place(int nozzle, EAutoState next, CancellationToken ct)
        {
            var nz = Nozzle(nozzle);
            if (!nz.HasProduct) { Run.AutoState = next; return true; }

            // Tray lắp ráp đã ra vị trí chờ thì lệnh đặt cất từ trước thuộc tray cũ — bỏ, chờ tray mới rồi soi lại.
            if (!_assyIn) { _held[nozzle] = null; return NeedAssyTray(); }

            var held = _held[nozzle];
            if (held == null)
            {
                // Chưa có lệnh: chạy tiếp sau khi số đo bị bỏ (tắt app, teach lại…), hoặc lúc soi không còn ô cho tay này.
                // Còn ô thì soi lại; hết ô thì để tay kia đặt trước, S50 chờ đổi tray. S20_Align1 soi tay #2 trước,
                // S21_Align2 soi tay #1 (Tuấn xác nhận 2026-09-05) — ánh xạ ngược với số tay vì tên BƯỚC không đổi theo.
                if (AssignCell(nozzle) < 0) { Run.AutoState = next; return true; }
                Run.AutoState = AlignStepOf(nozzle);
                LogWorker.Instance.RecordLog("Chưa có lệnh đặt của tay #" + nozzle + " — soi lại trước khi đặt",
                    Utility.Log.LogLevel.Info, "Auto");
                return true;
            }

            // Góc gốc của lệnh A là góc lúc chụp bottom-up. A đã xoay kể từ đó (jog lúc dừng…) thì lệnh không còn đúng:
            // soi lại, lần hai liên tiếp thì dừng (thiết kế nhánh R).
            string aName = nozzle == 1 ? AxisId.A1 : AxisId.A2;
            double aNow = _motion[aName].CurrentPos;
            if (AxisAngle.Distance(aNow, held.ACap) > _sys().ACapTolDeg)
            {
                _held[nozzle] = null;
                _remeasure[nozzle]++;
                string moved = string.Format(CultureInfo.InvariantCulture,
                    Loc.T("Auto.AMovedSinceCapture"), aName, held.ACap, aNow);
                if (_remeasure[nozzle] >= 2)
                    return VmHalt(nozzle, -1, Loc.F("Auto.AMovedTwice", moved, nozzle));
                LogWorker.Instance.RecordLog(moved + " — soi lại tay #" + nozzle, Utility.Log.LogLevel.Warning, "Auto");
                Run.AutoState = AlignStepOf(nozzle);
                return true;
            }

            var tray = Run.AssyTray;
            int index = held.Cell;
            if (tray.Get(index) != ECellState.Empty)
            {
                // Ô đích đã đổi trạng thái kể từ lúc soi: bỏ lệnh, soi lại.
                _held[nozzle] = null;
                Run.AutoState = AlignStepOf(nozzle);
                return true;
            }

            tray.CurrentIndex = index;
            tray.Set(index, ECellState.Processing);
            _releaseStarted = false;
            try
            {
                _status.LastDeltaTheta = held.DiagDA;
                _status.LastPlaceError = held.DiagEcc;

                // Góc TUYỆT ĐỐI do Flow tính từ góc lúc chụp — không cộng vào vị trí hiện tại.
                if (!await PlaceAsync(nozzle, held.CmdX, held.CmdY2, AxisAngle.Wrap(held.CmdA), ct).ConfigureAwait(false)) return false;

                CompletePlacement(nozzle, index, held.Top);
                Run.AutoState = next;
                return true;
            }
            finally
            {
                // Ô còn Processing nghĩa là bước đặt dừng giữa chừng (Halt vì không tới được điểm đặt,
                // EMG, STOP lần 2...). Mọi nhánh bỏ đặt có chủ ý đều tự đặt lại trạng thái ô trước khi
                // return, nên tới được đây chỉ còn các lần dừng giữa chuyển động.
                // - CHƯA nhả hút: ô đích vẫn nguyên, sản phẩm vẫn trên tay. Trả ô về trống và cho lần
                //   chạy lại bắt đầu từ S00 — quét lại tray lắp ráp, soi lại sản phẩm rồi mới đặt
                //   (Tuấn chốt 2026-09-13).
                // - ĐÃ bắt đầu nhả: sản phẩm có thể đã nằm trong ô hoặc dính lại trên đầu hút. Giữ
                //   nguyên nhãn, bắt lấy sản phẩm ra khỏi tay hút trước thao tác kế tiếp (an toàn).
                if (tray.Get(index) == ECellState.Processing)
                {
                    if (!_releaseStarted)
                    {
                        tray.Set(index, ECellState.Empty);
                        _restartCycle = true;
                        Save();
                        _status.Message += " — " + Loc.T("Auto.RestartRescanNote");
                    }
                    else
                    {
                        RequireNozzleClear(Loc.F("Auto.PlaceStoppedAfterRelease", nozzle));
                        _status.Message += " — " + Loc.T("Auto.CheckNozzleNote");
                    }
                }
            }
        }

        /// <summary>Ghi sổ một lần đặt thành công.</summary>
        private void CompletePlacement(int nozzle, int index, PlacedTopRecord top)
        {
            var nz = Nozzle(nozzle);
            Run.AssyTray.Set(index, ECellState.Ok);
            Run.CountOk++;
            Run.CountTotal++;
            Run.ConsecutiveNg = 0;
            Run.InputCountOk++;
            Run.AssyCountOk++;

            int src = nz.SourceCellIndex;
            if (src >= 0) Run.InputTray.Set(src, ECellState.Ok);

            nz.HasProduct = false;
            nz.SourceCellIndex = -1;
            nz.Aligned = false;
            _held[nozzle] = null;
            _remeasure[nozzle] = 0;
            _guardSkipStreak = 0;

            if (top != null)
                lock (_placed) _placed[index] = new PlacedTopRecord { X = top.X, Y = top.Y, A = top.A, Nozzle = nozzle };

            Save();
        }

        /// <summary>Xoay, di chuyển tới điểm đặt, hạ, nhả, nâng.</summary>
        /// <param name="aTarget">Góc tuyệt đối của trục A — <c>CmdA</c> của Flow, không cộng dồn.</param>
        private async Task<bool> PlaceAsync(int nozzle, double cmdX, double cmdY, double aTarget, CancellationToken ct)
        {
            string aName = nozzle == 1 ? AxisId.A1 : AxisId.A2;
            string zName = nozzle == 1 ? AxisId.Z1 : AxisId.Z2;
            string yName = Model.AssyTray.AxisRow;

            // Trục A chạy song song với X/Y2 — xoay không tốn thêm thời gian chu kỳ. Y2 mang tray lắp ráp: tới điểm bù ở
            // mức Auto thấp, gia tốc giảm theo, cho hàng trên tray khỏi xê dịch (Tuấn 2026-09-30).
            var moves = new Task<bool>[]
            {
                _func.MoveAsync(aName, aTarget, MoveSpeed(aName), ct),
                _func.MoveAsync(AxisId.X, cmdX, MoveSpeed(AxisId.X), ct),
                _func.MoveGentleAsync(yName, cmdY, _sys().GetAxis(yName).MoveSpeedLow, ct)
            };

            var results = await Task.WhenAll(moves).ConfigureAwait(false);
            foreach (bool r in results)
                if (!r) return Halt(Loc.F("Auto.PlaceMoveFail", nozzle));

            LastPlaceMove = new PlaceMoveRecord
            {
                Nozzle = nozzle, CmdX = cmdX, CmdY = cmdY, ATarget = aTarget,
                ActualX = _motion[AxisId.X].CurrentPos, ActualY = _motion[yName].CurrentPos, ActualA = _motion[aName].CurrentPos
            };

            if (!await _func.MoveZDownAsync(zName, Model.AssyTray.ZPickFor(zName), Model.AssyTray.ZSlowFor(zName, _sys().AssyZSlowDown), ct).ConfigureAwait(false))
                return Halt(Loc.F("Auto.ZDownPlaceFail", zName));

            _releaseStarted = true;
            _func.SetVacuum(nozzle, false);
            _func.SetBlow(nozzle, true);

            // Đặt vào tray lắp ráp: thổi đủ BlowTimeMs rồi tắt, KHÔNG chờ cảm biến chân không báo nhả (Tuấn 2026-10-06).
            await Task.Delay(_sys().BlowTimeMs, ct).ConfigureAwait(false);
            _func.SetBlow(nozzle, false);

            // Tray lắp ráp: mặc định nâng cũng qua điểm giảm tốc — nhấc chậm cho lò xo trên tool nhả dần, không bật vào
            // tray (Tuấn 2026-09-30). Tắt được ở Setting (2026-10-07).
            if (!await _func.MoveZUpAsync(zName, SafeZ(), Model.AssyTray.ZSlowFor(zName, _sys().AssyZSlowUp), ct).ConfigureAwait(false))
                return Halt(Loc.F("Auto.ZUpAfterPlaceFail", zName));

            return true;
        }

        /// <summary>
        /// Trả sản phẩm về đúng ô nguồn rồi đánh dấu ô đó NG. Dùng khi align fail hoặc lượng bù
        /// vượt ngưỡng — hai trường hợp mà sản phẩm còn nguyên nhưng máy không dám đặt nó.
        /// </summary>
        private async Task<bool> ReturnToSourceAsync(int nozzle, CancellationToken ct)
        {
            var nz = Nozzle(nozzle);
            int index = nz.SourceCellIndex;

            if (index < 0 || !_inputIn)
            {
                // Không có chỗ trả, phải người lấy hàng ra (Tuấn chốt 2026-09-26):
                // - tray đầu vào chưa vào máy — nằm ở vị trí chờ chỗ người thao tác, Y1 không được chạy tới;
                // - không rõ ô nguồn — sản phẩm của tray đầu vào CŨ (tray mới đầy hàng, không đặt ngược lại được), hoặc
                //   phần mềm vừa đồng bộ tay hút theo cảm biến (mở lại app lúc tay còn hút sản phẩm).
                // Phát dạng SỰ KIỆN: Raise trơn để ConditionActive = true mãi mãi, mà 5031 là mức Error nên RESET không
                // gỡ được và SafeToMove khoá cả máy tới khi mở lại app (rà soát 2026-09-13).
                string why = !_inputIn ? Loc.T("Auto.InputTrayNotIn") : Loc.T("Auto.SourceUnknown");
                AlarmManager.RaiseEvent(AlarmCode.CannotReturnProduct, "Auto", Loc.F("Auto.NozzleWhy", nozzle, why));
                RequireNozzleClear(Loc.F("Auto.CannotReturnReason", nozzle, why));
                return Halt(Loc.F("Auto.CannotReturnHalt", nozzle, why));
            }

            int row, col;
            Run.InputTray.RowCol(index, out row, out col);

            string zName = nozzle == 1 ? AxisId.Z1 : AxisId.Z2;
            var cell = Model.InputTray.CellPos(zName, row, col);

            if (!await _func.MoveToCellAsync(Model.InputTray.AxisRow, cell.Y, cell.X, ct).ConfigureAwait(false))
                return Halt(Loc.T("Auto.ReturnMoveFail"));
            if (!await _func.MoveZDownAsync(zName, Model.InputTray.ZPickFor(zName), Model.InputTray.ZSlowFor(zName), ct).ConfigureAwait(false))
                return Halt(Loc.T("Auto.ReturnZDownFail"));

            _func.SetVacuum(nozzle, false);
            _func.SetBlow(nozzle, true);
            bool released = await WaitVacuumAsync(nozzle, false, ct).ConfigureAwait(false);
            await Task.Delay(_sys().BlowTimeMs, ct).ConfigureAwait(false);
            _func.SetBlow(nozzle, false);

            if (!await _func.MoveZUpAsync(zName, SafeZ(), Model.InputTray.ZSlowFor(zName), ct).ConfigureAwait(false))
                return Halt(Loc.T("Auto.ReturnZUpFail"));

            if (!released)
            {
                // Cảm biến vẫn báo còn hàng sau khi thổi — xử lý như PlaceAsync, và dừng TRƯỚC khi
                // ghi sổ: ghi tay hút thành trống trong lúc sản phẩm còn dính trên đầu hút là để lần
                // hút sau hạ một tay "trống" đang cầm hàng xuống ô có sản phẩm.
                AlarmManager.RaiseEvent(AlarmCode.PlaceFailed, "Auto",
                    Loc.F("Auto.StillHoldingAfterReturn", nozzle));
                RequireNozzleClear(Loc.F("Auto.ReturnReleaseFailReason", nozzle));
                return Halt(Loc.F("Auto.ReturnReleaseFailHalt", nozzle));
            }

            Run.InputTray.Set(index, ECellState.Ng);
            nz.HasProduct = false;
            nz.SourceCellIndex = -1;
            nz.Aligned = false;
            _held[nozzle] = null;
            Save();
            return true;
        }

        // ───────────────────────── S40 / S50 ─────────────────────────

        private bool S40_Index()
        {
            _cycleSw.Stop();
            if (_cycleSw.Elapsed.TotalSeconds > 0.01)
                Run.LastCycleTimeSec = _cycleSw.Elapsed.TotalSeconds;
            Run.RecordCycle(_cycleSw.Elapsed.TotalSeconds, Run.CountTotal - _cycleStartTotal);

            Save();
            Run.AutoState = EAutoState.S50_TrayCheck;
            return true;
        }

        private async Task<bool> S50_TrayCheck(CancellationToken ct)
        {
            bool needInput = NextLoadedCell(Run.InputTray) < 0;
            bool needAssy = NextFreeCell(Run.AssyTray) < 0;

            // Tray lắp ráp vừa hết ô trống: chốt thời gian thực tế của tray (Tuấn 2026-10-08) — chia cho số ô đặt OK.
            if (needAssy && Run.AssyTrayStartedAt != null)
            {
                int ok = 0;
                foreach (var c in Run.AssyTray.Cells.ToArray()) if (c == ECellState.Ok) ok++;
                Run.FinishAssyTray(DateTime.Now, ok);
                LogWorker.Instance.RecordLog(string.Format(CultureInfo.InvariantCulture,
                    "Xong tray lắp ráp: {0} sản phẩm trong {1} (thực tế, tính cả lúc chờ/dừng) — trung bình {2:0.00} s/sp",
                    ok, TimeSpan.FromSeconds(Math.Floor(Run.LastAssyTrayTimeSec * 10) / 10).ToString(@"h\:mm\:ss\.f"), Run.LastAssyTrayPerProductSec),
                    Utility.Log.LogLevel.Info, "Auto");
            }

            // Còn hàng trên tay hút thì chưa được đổi tray đầu vào — phải đặt nốt đã.
            bool holding = Run.Nozzle1.HasProduct || Run.Nozzle2.HasProduct;
            if (needInput && !needAssy && holding)
            {
                Run.AutoState = EAutoState.S20_Align1;
                return true;
            }

            if (!needInput && !needAssy)
            {
                Run.AutoState = EAutoState.S00_Ready;
                return true;
            }

            string what = TrayWhat(needInput, needAssy);

            // Hết ô lắp ráp mà tay còn cầm hàng: giữ nguyên sản phẩm trên tay, chờ đổi tray (Tuấn chốt 2026-09-13).
            // Chờ đổi tray chỉ là thông báo, không alarm (Tuấn 2026-09-30): Warning 5020 còn nguyên nhân tới lúc bấm
            // Start, nên có lỗi khác trong lúc chờ thì RESET không đưa máy ra khỏi LỖI được — Start nào cũng bị từ chối.
            string holdNote = holding
                ? " — tay hút còn cầm sản phẩm, đổi tray xong sẽ quét tray mới và soi lại trước khi đặt"
                : "";
            Save();

            // Tray cần thay chạy ra vị trí chờ của nó (StandbyPos trục hàng) cho operator lấy ra, đặt tray mới — từ lúc
            // đó coi như tray đã ra khỏi máy. Bước nào cần tray đó thì tự chờ nút của nó: Start 1 tray đầu vào, Start 2
            // tray lắp ráp (Tuấn chốt 2026-09-26).
            if (!await MoveTraysToStandbyAsync(needInput, needAssy, ct).ConfigureAwait(false))
                return Halt(Loc.F("Auto.TrayToStandbyFail", Loc.T(needInput && needAssy ? "Auto.TrayBoth" : needInput ? "Auto.TrayInput" : "Auto.TrayAssy")));
            if (needInput) _inputIn = false;
            if (needAssy) _assyIn = false;
            LogWorker.Instance.RecordLog("Đưa " + what + " ra vị trí chờ" + holdNote, Utility.Log.LogLevel.Info, "Auto");

            // Luôn chạy lại từ S00, kể cả khi tay còn cầm hàng (Tuấn chốt 2026-09-13): S00 bỏ tư thế
            // đo cũ nên sản phẩm đang cầm được soi lại ở CAM bottom-up, S05 quét tray lắp ráp mới
            // trước khi đặt, và ReleaseOrphanCells giữ nguyên ô nguồn của sản phẩm đang cầm.
            Run.AutoState = EAutoState.S00_Ready;
            return true;
        }

        /// <summary>Operator vừa đặt tray mới: tray đầu vào đầy hàng, tray lắp ráp trống hết.</summary>
        private void ReloadTrays(bool input, bool assy)
        {
            if (input)
            {
                Run.InputTray.ResetAll(ECellState.Loaded);
                // Ô nguồn của sản phẩm đang cầm thuộc tray đầu vào CŨ — giữ số ô đó là lát nữa đánh
                // dấu nhầm một ô còn hàng của tray mới. Bỏ đi: sản phẩm thành "không rõ ô nguồn".
                if (Run.Nozzle1.HasProduct) Run.Nozzle1.SourceCellIndex = -1;
                if (Run.Nozzle2.HasProduct) Run.Nozzle2.SourceCellIndex = -1;
            }
            if (assy)
            {
                Run.AssyTray.ResetAll(ECellState.Empty);
                // Tray lắp ráp VẬT LÝ vừa đổi — số đo quét trước của tray cũ không còn đúng cho
                // tray mới dù trùng chỉ số ô, phải bỏ hết để S05_ScanAssy quét lại từ đầu.
                InvalidateAssyScan();
                _restartCycle = true;
                // Tray mới bắt đầu đếm từ lúc vào máy (chu trình đang chạy, nhận tray ở đây).
                Run.AssyTrayStartedAt = DateTime.Now;
            }
        }

        private static string TrayWhat(bool input, bool assy)
        {
            return input && assy ? "cả hai tray" : input ? "tray đầu vào" : assy ? "tray lắp ráp" : null;
        }

        /// <summary>Thông báo chờ nạp tray, theo tray còn thiếu — "hết/đầy" khi sổ tray đó đã dùng xong, không thì "chờ".</summary>
        private string TrayWaitText()
        {
            string input = NextLoadedCell(Run.InputTray) < 0 ? Loc.T("Auto.WaitInputEmpty")
                                                             : Loc.T("Auto.WaitInput");
            string assy = NextFreeCell(Run.AssyTray) < 0 ? Loc.T("Auto.WaitAssyFull")
                                                         : Loc.T("Auto.WaitAssy");
            return !_inputIn && !_assyIn ? input + "; " + assy : _inputIn ? assy : input;
        }

        /// <summary>Bước cần tray lắp ráp mà tray chưa vào: về S05 chờ Start 2, quét xong quay lại đúng bước này.</summary>
        private bool NeedAssyTray()
        {
            _afterScan = Run.AutoState;
            Run.AutoState = EAutoState.S05_ScanAssy;
            return true;
        }

        /// <summary>
        /// Chờ operator nạp tray: trạng thái CHỜ THAO TÁC với thông báo theo tray còn thiếu, cho tới khi
        /// <paramref name="done"/>, vẫn xét STOP. Tray đã được nạp sẵn thì đi tiếp ngay, không nháy sang CHỜ THAO TÁC.
        /// Trả false khi bị dừng — chạy lại thì vào đúng bước này và chờ tiếp.
        /// </summary>
        private async Task<bool> WaitTrayAsync(Func<bool> done, CancellationToken ct)
        {
            TakeTrayLoads();
            if (done()) return true;

            string message = TrayWaitText();
            _waiting = message;
            _status.State = EMcState.WaitOperator;
            _status.Message = message;
            LogWorker.Instance.RecordLog("Chờ tray: " + message, Utility.Log.LogLevel.Info, "Auto");
            _cycleSw.Stop();               // chờ người thao tác không tính vào cycle time
            try
            {
                while (true)
                {
                    if (ct.IsCancellationRequested) return Stopped(Loc.T("Auto.CancelledWaitingTray"));
                    if (PauseRequested) { PauseRequested = false; return Stopped(Loc.T("Auto.StopWhileWaitingTray")); }
                    await Task.Delay(50, ct).ConfigureAwait(false);
                    TakeTrayLoads();
                    if (done()) break;
                }
            }
            finally
            {
                _waiting = null;
            }

            _cycleSw.Start();
            _status.State = EMcState.Running;
            _status.Message = Loc.T("Auto.Running");
            return true;
        }

        /// <summary>
        /// Nhận tray operator vừa nạp (<see cref="LoadTray"/>) — trên luồng chu trình, ở đầu mỗi bước và trong lúc chờ
        /// tray. Tray hết theo sổ (đầu vào không còn ô có hàng, lắp ráp không còn ô trống) thì là tray mới: làm lại sổ. Tray
        /// còn dở thì giữ sổ — cùng tray đưa vào lại sau khi về gốc.
        /// </summary>
        private void TakeTrayLoads()
        {
            bool input = Interlocked.Exchange(ref _loadInput, 0) == 1;
            bool assy = Interlocked.Exchange(ref _loadAssy, 0) == 1;
            if (!input && !assy) return;

            bool newInput = input && NextLoadedCell(Run.InputTray) < 0;
            bool newAssy = assy && NextFreeCell(Run.AssyTray) < 0;
            ReloadTrays(newInput, newAssy);
            if (input) _inputIn = true;
            if (assy) _assyIn = true;
            Save();

            if (input) LogWorker.Instance.RecordLog("Nhận tray đầu vào (Start 1)" + (newInput ? " — tray mới" : " — tray còn dở, giữ sổ"),
                Utility.Log.LogLevel.Info, "Auto");
            if (assy) LogWorker.Instance.RecordLog("Nhận tray lắp ráp (Start 2)" + (newAssy ? " — tray mới" : " — tray còn dở, giữ sổ"),
                Utility.Log.LogLevel.Info, "Auto");
        }

        /// <summary>Tray đầu vào vừa nạp còn nằm ở vị trí chờ: Z lên an toàn, Y1 đưa tray vào tới hàng hút kế tiếp —
        /// "Start 1 thì tray đầu vào đi vào" (Tuấn 2026-09-26). Chưa nạp hoặc đã ở trong máy thì không chạy gì.</summary>
        private async Task<bool> MoveInputTrayInAsync(CancellationToken ct)
        {
            if (!_inputIn) return true;
            string axis = Model.InputTray.AxisRow;
            if (Math.Abs(_motion[axis].CurrentPos - _sys().GetAxis(axis).StandbyPos) > TraySensorWindowMm) return true;
            int index = NextLoadedCell(Run.InputTray);
            if (index < 0) return true;

            int row, col;
            Run.InputTray.RowCol(index, out row, out col);
            if (!await _func.EnsureZSafeAsync(ct).ConfigureAwait(false)) return false;
            return await _func.MoveAsync(axis, Model.InputTray.CellPos(AxisId.Z1, row, col).Y, MoveSpeed(axis), ct).ConfigureAwait(false);
        }

        /// <summary>Z lên an toàn, rồi trục hàng của tray cần thay về vị trí chờ (StandbyPos) — hai tray chạy song song.</summary>
        private async Task<bool> MoveTraysToStandbyAsync(bool input, bool assy, CancellationToken ct)
        {
            if (!await _func.EnsureZSafeAsync(ct).ConfigureAwait(false)) return false;
            var moves = new List<Task<bool>>();
            if (input) moves.Add(MoveToStandbyAsync(Model.InputTray.AxisRow, ct));
            if (assy) moves.Add(MoveToStandbyAsync(Model.AssyTray.AxisRow, ct));
            var ok = await Task.WhenAll(moves).ConfigureAwait(false);
            return Array.TrueForAll(ok, b => b);
        }

        private Task<bool> MoveToStandbyAsync(string axis, CancellationToken ct)
        {
            return _func.MoveAsync(axis, _sys().GetAxis(axis).StandbyPos, MoveSpeed(axis), ct);
        }

        /// <summary>Tray đã vào máy, hoặc vừa được nạp và đang chờ luồng chu trình nhận.</summary>
        public bool TrayIn(bool input)
        {
            return input ? _inputIn || Volatile.Read(ref _loadInput) == 1
                         : _assyIn || Volatile.Read(ref _loadAssy) == 1;
        }

        /// <summary>Chu trình đang chờ nạp tray — thông báo của lần chờ đó; null khi không chờ.</summary>
        public string WaitingMessage { get { return _waiting; } }

        /// <summary>Đang chờ nạp tray đầu vào / tray lắp ráp — đèn Start 1 / Start 2 nháy nhanh gọi người nạp (Tuấn 2026-10-01).</summary>
        public bool WaitingInputTray => _waiting != null && !_inputIn;
        public bool WaitingAssyTray => _waiting != null && !_assyIn;

        /// <summary>
        /// Start 1 (<paramref name="input"/> = true) / Start 2 — operator đã đặt tray ở vị trí chờ (Tuấn chốt 2026-09-26).
        /// Gọi từ vòng quét McControl, lúc sẵn sàng hoặc lúc chu trình đang chạy/chờ tray. Cảm biến phải thấy tray ở vị trí
        /// chờ. Tray đã ở trong máy thì không làm gì. Trả về lý do từ chối, null nếu nhận.
        /// </summary>
        public string LoadTray(bool input)
        {
            if (TrayIn(input)) return null;
            if (Model == null) return Loc.T("Auto.NoModelLoaded");

            bool sensor = input ? _func.HasTrayIn() : _func.HasTrayAssy();
            if (TrayMissing(input ? Model.InputTray.AxisRow : Model.AssyTray.AxisRow, sensor))
                return Loc.T(input ? "Auto.InputTrayNotSeen" : "Auto.AssyTrayNotSeen");

            if (input) Interlocked.Exchange(ref _loadInput, 1);
            else Interlocked.Exchange(ref _loadAssy, 1);
            return null;
        }

        /// <summary>Về gốc đưa mọi trục về vị trí chờ — cả hai tray coi như ra khỏi máy, phải bấm Start 1/Start 2 lại.</summary>
        public void TraysOut()
        {
            Interlocked.Exchange(ref _loadInput, 0);
            Interlocked.Exchange(ref _loadAssy, 0);
            _inputIn = false;
            _assyIn = false;
        }

        /// <summary>Nạp tray nào chưa vào, không qua cảm biến — chỉ đường START cũ của self-test dùng
        /// (<c>McControl.DoStart</c>).</summary>
        public void LoadBothTrays()
        {
            if (!_inputIn) Interlocked.Exchange(ref _loadInput, 1);
            if (!_assyIn) Interlocked.Exchange(ref _loadAssy, 1);
        }

        // ───────────────────────── tiện ích ─────────────────────────

        private NozzleState Nozzle(int i) { return i == 1 ? Run.Nozzle1 : Run.Nozzle2; }

        /// <summary>Bước soi của một tay — S20 soi tay #2, S21 soi tay #1 (Tuấn xác nhận 2026-09-05).</summary>
        private static EAutoState AlignStepOf(int nozzle) { return nozzle == 1 ? EAutoState.S21_Align2 : EAutoState.S20_Align1; }

        // Z dương là chiều ĐI XUỐNG, "an toàn" là vị trí NHỎ (Tuấn xác nhận 2026-09-04) —
        // xem MotionConnect.ZSafeOk.
        // Dừng đúng Z_SAFE — chốt chặn đã nới MotionConnect.ZSafeMargin (Tuấn chốt 2026-09-22).
        private double SafeZ() { return _motion.ZSafe; }
        private double MoveSpeed(string axis) { return _sys().GetAxis(axis).MoveSpeed; }
        // Chỉ còn dùng cho chiều NÂNG Z lên — mọi lần HẠ Z xuống tray đi qua
        // McFunction.MoveZDownAsync để tôn trọng điểm giảm tốc đã teach.
        private double ZSpeed(string axis) { return _sys().GetAxis(axis).MoveSpeed; }

        /// <summary>
        /// Chờ lực hút lên đủ sau khi cảm biến đã báo hút được, rồi mới cho nâng Z
        /// (<see cref="SystemData.VacuumSettleMs"/>). Hút hụt thì không chờ — đằng nào cũng đi thử lại.
        /// </summary>
        private async Task SettleVacuumAsync(bool gripped, CancellationToken ct)
        {
            int ms = _sys().VacuumSettleMs;
            if (gripped && ms > 0) await Task.Delay(ms, ct).ConfigureAwait(false);
        }

        /// <summary>Chờ cảm biến chân không đạt trạng thái mong muốn, timeout thì trả false.</summary>
        private async Task<bool> WaitVacuumAsync(int nozzle, bool want, CancellationToken ct)
        {
            var sw = Stopwatch.StartNew();
            int limit = _sys().VacuumTimeoutMs;

            while (sw.ElapsedMilliseconds < limit)
            {
                if (ct.IsCancellationRequested) return false;
                if (_func.HasProduct(nozzle) == want) return true;
                await Task.Delay(10, ct).ConfigureAwait(false);
            }
            return _func.HasProduct(nozzle) == want;
        }

        /// <summary>
        /// Gọi một Flow qua <c>VisionConnect</c> (ghi biến, chạy, đọc trong một lần giữ khoá; quá giờ do nó canh — một
        /// đồng hồ duy nhất). Đèn CAM bật trước khi gọi Flow có camera, <b>chờ đèn sáng đủ</b>
        /// (<see cref="SystemData.LampMsFor"/>), rồi mới kích Flow (Tuấn yêu cầu 2026-09-04, chờ đèn thêm
        /// 2026-09-23): bottom-up là đèn CAM1, top-down là đèn CAM2, Flow chỉ tính không bật đèn.
        ///
        /// <para><b>Không tắt đèn ở đây.</b> Đèn sáng suốt cả nhóm bước và chỉ tắt khi chu trình rời khỏi nhóm —
        /// xem <see cref="TurnCamLampOnAsync"/> và <see cref="UpdateCamLamp"/>.</para>
        /// </summary>
        private async Task<VmCallResult> CallVmAsync(VmCallRequest req, CancellationToken ct)
        {
            var vision = _vision();
            if (vision == null || !vision.IsConnected)
            {
                // Sự kiện (mức Error): không có vòng quét nào gỡ 4000, Raise trơn thì RESET không bao giờ xoá được.
                AlarmManager.RaiseEvent(AlarmCode.VisionDisconnected, "Vision");
                return VmCallResult.Fail(Loc.T("Auto.VisionNotConnected"));
            }

            int lampBit = req.Camera == EVmCamera.BottomUp ? IoMap.DoLampCam1
                        : req.Camera == EVmCamera.TopDown ? IoMap.DoLampCam2 : -1;
            await TurnCamLampOnAsync(lampBit, ct).ConfigureAwait(false);

            var r = await vision.CallFlowAsync(req, ct).ConfigureAwait(false) ?? VmCallResult.Fail(Loc.T("VmCycle.NoResult"));
            if (r.Ok) AlarmManager.ClearCondition(AlarmCode.VisionTriggerTimeout);
            return r;
        }

        /// <summary>Đèn CAM đang sáng — <see cref="IoMap.DoLampCam1"/> hoặc <see cref="IoMap.DoLampCam2"/>,
        /// −1 khi cả hai đang tắt.</summary>
        private int _lampOn = -1;

        /// <summary>
        /// Đèn CAM mà một bước cần sáng. <b>Chỗ duy nhất</b> khai "nhóm bước nào dùng đèn nào" — thêm một bước có
        /// soi ảnh thì khai ở đây là xong, không phải đi sửa chỗ bật/tắt.
        /// </summary>
        private static int LampFor(EAutoState state)
        {
            switch (state)
            {
                case EAutoState.S05_ScanAssy:
                    return IoMap.DoLampCam2;
                case EAutoState.S20_Align1:
                case EAutoState.S21_Align2:
                    return IoMap.DoLampCam1;
                default:
                    return -1;
            }
        }

        /// <summary>
        /// Bật đèn CAM cần cho lần soi này, và <b>chỉ chờ đèn sáng khi vừa mới bật</b>.
        ///
        /// <para><b>Đèn sáng suốt cả nhóm bước, không nháy từng lần kích</b> (Tuấn yêu cầu 2026-09-25). Trước đây
        /// mỗi lần gọi Flow đều bật rồi tắt ngay, nên hai lần soi CAM dưới của một nhịp thành hai nháy và một
        /// lượt quét tray lắp ráp thành 40 nháy. Ngoài chuyện nhìn rối và tuổi thọ đèn, mỗi nháy còn phải trả
        /// lại khoảng chờ đèn lên đủ sáng — đèn đã sáng sẵn thì không phải chờ.</para>
        ///
        /// <para>Đổi sang đèn khác thì tắt đèn cũ trước: hai đèn cùng sáng là ánh sáng của camera này lọt vào
        /// khung hình camera kia.</para>
        /// </summary>
        private async Task TurnCamLampOnAsync(int lampBit, CancellationToken ct)
        {
            if (lampBit < 0 || lampBit == _lampOn) return;   // Flow chỉ tính, hoặc đèn đang sáng sẵn
            if (_lampOn >= 0) _func.Io?.SetDo(_lampOn, false);
            _func.Io?.SetDo(lampBit, true);
            _lampOn = lampBit;
            await WaitLampAsync(lampBit, ct).ConfigureAwait(false);
        }

        /// <summary>
        /// Tắt đèn CAM khi chu trình đã rời khỏi nhóm bước dùng nó. Gọi sau MỖI bước, ngay trước checkpoint kế
        /// tiếp: bước nào cũng có thể nhảy sang nhánh khác (soi lại, quét lại, hết ô, NG), nên không chỗ nào
        /// khác biết chắc nhóm đã hết.
        /// </summary>
        private void UpdateCamLamp()
        {
            if (_lampOn >= 0 && _lampOn != LampFor(Run.AutoState)) TurnCamLampOff();
        }

        private void TurnCamLampOff()
        {
            if (_lampOn < 0) return;
            _func.Io?.SetDo(_lampOn, false);
            _lampOn = -1;
        }

        /// <summary>
        /// Chờ đèn CAM sáng đủ sau khi vừa bật, trước khi kích Flow — mỗi đèn một số (<see cref="SystemData.BottomLampMs"/>
        /// cho đèn CAM1, <see cref="SystemData.TopLampMs"/> cho đèn CAM2). Flow chỉ tính — không dùng camera, không bật
        /// đèn nào — thì không chờ giây nào.
        /// </summary>
        private async Task WaitLampAsync(int lampBit, CancellationToken ct)
        {
            if (lampBit < 0) return;
            int ms = lampBit == IoMap.DoLampCam2 ? _sys().TopLampMs : _sys().BottomLampMs;
            if (ms > 0) await Task.Delay(ms, ct).ConfigureAwait(false);
        }

        private async Task<VmCameraRead> LocateTopVmAsync(CancellationToken ct)
        {
            var names = _sys().VisionOutputs;
            await _func.WaitSettleAsync(EVmCamera.TopDown, ct).ConfigureAwait(false);
            int rid = VmRunIds.Next();
            var g = VmCycle.AxisGlobals(_motion, names, rid);
            var r = await CallVmAsync(VmCycle.LocateRequest(names, g), ct).ConfigureAwait(false);
            return VmCycle.ReadLocate(r, g, names);
        }

        /// <summary>
        /// Nhánh C/E/R: dừng, GIỮ sản phẩm trên tay (chưa nhả), trả ô đích đang nhận về trống,
        /// và cho lần chạy sau bắt đầu lại từ S00 — quét lại, soi lại (Tuấn chốt 2026-09-13; D10 chọn giữ hàng).
        /// Phát alarm dạng sự kiện mức Error: bắt RESET có ý thức, nhưng RESET gỡ được.
        /// </summary>
        private bool VmHalt(int nozzle, int index, string why, int alarm = AlarmCode.VisionContract)
        {
            if (index >= 0 && Run.AssyTray.Get(index) == ECellState.Processing)
                Run.AssyTray.Set(index, ECellState.Empty);
            _guardSkipStreak = 0;
            if (nozzle >= 1 && nozzle <= 2) _remeasure[nozzle] = 0;

            AlarmManager.RaiseEvent(alarm, "Vision", why);
            bool holding = Run.Nozzle1.HasProduct || Run.Nozzle2.HasProduct;
            _restartCycle = true;
            return Halt(why + " — " + Loc.T(holding ? "Auto.VmHaltHolding" : "Auto.VmHaltRestart"));
        }

        /// <summary>
        /// Ô đầu vào kế tiếp còn hàng.
        ///
        /// <para><b>Quét từ đầu tray, và KHÔNG nhận ô đang <c>Processing</c>.</b> Hai điểm này
        /// đều quan trọng. Ô <c>Processing</c> là ô đã có tay hút nhận — nhận nó lần nữa thì
        /// tay #2 sẽ lao tới đúng ô mà tay #1 vừa lấy hàng, và vì cảm biến chân không bám theo
        /// van nên máy vẫn "thành công" rồi đặt hai lần cùng một sản phẩm tưởng tượng. Quét từ
        /// đầu thay vì từ con trỏ thì không có trạng thái con trỏ nào để hỏng: ô đã dùng mang
        /// nhãn khác <c>Loaded</c> nên tự khắc bị bỏ qua.</para>
        /// </summary>
        private static int NextLoadedCell(TrayState tray)
        {
            for (int i = 0; i < tray.CellCount; i++)
                if (tray.Get(i) == ECellState.Loaded) return i;
            return -1;
        }

        /// <summary>
        /// Ô tray đầu vào giao cho tay này.
        ///
        /// <para>Tay #1 hút TRƯỚC, nên nó là chỗ duy nhất trong nhịp còn nhìn thấy cả HAI ô sắp dùng —
        /// nó chọn theo hình học (<see cref="Nozzle1TakesFirstPickCell"/>). Tới lượt tay #2 thì ô tay #1
        /// vừa nhận đã mang nhãn <c>Processing</c>, nên nó cứ lấy ô <c>Loaded</c> đầu tiên là đúng ô còn
        /// lại của cặp — không có đường nào cho hai tay chọn lệch nhau.</para>
        ///
        /// <para>Không ghép cặp khi chỉ còn một ô, hoặc khi tay #2 đang cầm hàng (chạy tiếp sau khi dừng)
        /// nên nhịp này nó không hút: lúc ấy không có cặp nào để cân, cứ ô ghé trước mà lấy.</para>
        /// </summary>
        private int AssignPickCell(int nozzle)
        {
            var tray = Run.InputTray;

            int first = -1, second = -1;
            for (int i = 0; i < tray.CellCount; i++)
            {
                if (tray.Get(i) != ECellState.Loaded) continue;
                if (first < 0) first = i;
                else { second = i; break; }
            }

            if (nozzle != 1 || first < 0 || second < 0 || Run.Nozzle2.HasProduct || !_sys().NozzleEnabled(2)) return first;

            int r1, c1, r2, c2;
            tray.RowCol(first, out r1, out c1);
            tray.RowCol(second, out r2, out c2);
            return Nozzle1TakesFirstPickCell(
                Model.InputTray.CellPos(AxisId.Z1, r1, c1).X, Model.InputTray.CellPos(AxisId.Z2, r1, c1).X,
                Model.InputTray.CellPos(AxisId.Z1, r2, c2).X, Model.InputTray.CellPos(AxisId.Z2, r2, c2).X)
                ? first : second;
        }

        /// <summary>
        /// Trong một CẶP ô của tray đầu vào, tay #1 có nhận ô ĐƯỢC GHÉ TRƯỚC hay không.
        ///
        /// <para><b>Gương của <see cref="Nozzle1TakesFirstCell"/>, nhưng soi đầu kia của vòng.</b> Quãng
        /// đường một vòng bằng <c>2 × (X xa nhất − X gần nhất)</c>. Đầu XA do ô lắp ráp của tay #1 quyết —
        /// đó là luật kia. Đầu GẦN do ô HÚT quyết, vì tray đầu vào nằm cuối bên trái. Nên ở đây chọn cách
        /// ghép sao cho điểm quay đầu nằm xa về bên PHẢI nhất: lấy cách có <c>min</c> của hai vị trí gantry
        /// lớn hơn.</para>
        ///
        /// <para><b>Cặp ô liền nhau trong một hàng</b> thì luật này ra đúng hành vi cũ — tay #1 lấy ô cột
        /// nhỏ, tay #2 lấy ô cột lớn hơn, và vì tay #2 lùi 63 mm nên chính nó là điểm quay đầu, lùi phải
        /// được một bước ô. <b>Chỗ luật này khác</b> là cặp <b>vắt qua hai hàng</b>: ô cột cuối hàng trên
        /// ghép với ô cột đầu hàng dưới. Luật cũ giao ô cột đầu cho tay #2, bắt gantry với thêm 63 mm về
        /// bên trái ở mọi cặp vắt hàng (Tuấn hỏi 2026-09-24).</para>
        ///
        /// <para>Bằng nhau thì giữ thứ tự ghé — đổi chỗ không được gì mà lại làm vết chạy khó đọc.</para>
        /// </summary>
        public static bool Nozzle1TakesFirstPickCell(double firstAtNozzle1, double firstAtNozzle2,
                                                     double secondAtNozzle1, double secondAtNozzle2)
        {
            return Math.Min(firstAtNozzle1, secondAtNozzle2) >= Math.Min(secondAtNozzle1, firstAtNozzle2);
        }

        /// <summary>Số ô lắp ráp còn trống. Sau S05, đó đúng là số ô quét thấy chi tiết đích.</summary>
        /// <summary>
        /// Ô lắp ráp ĐẶT ĐƯỢC: còn trống <b>và</b> đã soi thấy chi tiết đích (có bản ghi trong
        /// <see cref="_assyScan"/>). Ô chưa soi không được tính — bật <see cref="SystemData.ScanAssyByRow"/>
        /// thì các hàng sau chưa soi, mà soi xong có thể thành <c>Skipped</c> vì không thấy chi tiết; đếm cả
        /// chúng là hút quá số chỗ đặt rồi ôm hàng chờ đổi tray (Tuấn nhắc 2026-09-23).
        ///
        /// <para>Tắt tuỳ chọn thì cổng hút đã đòi quét HẾT tray trước khi đếm, nên ô trống nào cũng có bản
        /// ghi — hai cách đếm ra cùng một số, luật "còn đúng một ô thì chỉ tay #1 hút" không đổi.</para>
        /// </summary>
        private int CountFreeCells(TrayState tray)
        {
            int n = 0;
            for (int i = 0; i < tray.CellCount; i++)
                if (tray.Get(i) == ECellState.Empty && _assyScan.ContainsKey(i)) n++;
            return n;
        }

        /// <summary>Ô lắp ráp kế tiếp còn trống — cùng quy tắc với <see cref="NextLoadedCell"/>.</summary>
        private static int NextFreeCell(TrayState tray)
        {
            for (int i = 0; i < tray.CellCount; i++)
                if (tray.Get(i) == ECellState.Empty) return i;
            return -1;
        }

        /// <summary>
        /// Trả các ô <c>Processing</c> không có tay hút nào nhận về trạng thái ban đầu.
        ///
        /// <para>Ô mang nhãn <c>Processing</c> nghĩa là "đã có tay hút nhận ô này". Sau khi tắt
        /// app giữa chừng hoặc dừng bất thường, nhãn đó còn nằm trên đĩa trong khi tay hút thì
        /// trống — không dọn thì ô đó bị bỏ qua vĩnh viễn và mỗi lần dừng máy lại mất thêm vài ô.
        /// </para>
        /// </summary>
        private void ReleaseOrphanCells()
        {
            int c1 = Run.Nozzle1.HasProduct ? Run.Nozzle1.SourceCellIndex : -1;
            int c2 = Run.Nozzle2.HasProduct ? Run.Nozzle2.SourceCellIndex : -1;

            Release(Run.InputTray, ECellState.Loaded, c1, c2);
            Release(Run.AssyTray, ECellState.Empty, -1, -1);
        }

        private static void Release(TrayState tray, ECellState back, int keep1, int keep2)
        {
            for (int i = 0; i < tray.CellCount; i++)
            {
                if (tray.Get(i) != ECellState.Processing) continue;
                if (i == keep1 || i == keep2) continue;
                tray.Set(i, back);
            }
        }

        private void CountNg(string what)
        {
            Run.CountNg++;
            Run.CountTotal++;
            Run.ConsecutiveNg++;
            Run.InputCountNg++;          // mọi NG của sản phẩm đều là hàng lấy từ tray đầu vào
            LogWorker.Instance.RecordLog(
                string.Format("NG: {0} (liên tiếp {1})", what, Run.ConsecutiveNg),
                Utility.Log.LogLevel.Warning, "Auto");
            Save();
        }

        /// <summary>
        /// Chốt chặn để máy không âm thầm làm hỏng cả tray: NG lẻ tẻ thì chạy tiếp, nhưng NG
        /// liên tiếp quá ngưỡng gần như luôn là hỏng cơ hoặc sai teach chứ không phải xui (§5.4).
        /// </summary>
        private bool CheckConsecutiveNg()
        {
            int limit = _sys().MaxConsecutiveNg;
            if (Run.ConsecutiveNg < limit) return true;

            // Sự kiện, không phải điều kiện tiếp diễn: 5010 mức Error, Raise trơn thì RESET không gỡ được
            // và HasError khoá cả máy tới khi mở lại App.
            AlarmManager.RaiseEvent(AlarmCode.ConsecutiveNg, "Auto",
                Loc.F("Auto.ConsecutiveNgDetail", Run.ConsecutiveNg, limit));
            return Halt(Loc.F("Auto.ConsecutiveNgHalt", Run.ConsecutiveNg));
        }

        /// <summary>Đánh dấu sự cố với hàng trên tay hút (Tuấn chốt 2026-09-13, an toàn) — chu trình dừng ở
        /// ranh giới bước kế tiếp. Lần bấm chạy sau McControl gỡ, ghi sổ theo cảm biến và chạy lại từ S00; tay còn
        /// hàng thì chạy tiếp, bỏ bước hút (Tuấn đổi 2026-09-26 — trước đó bắt lấy hàng ra).</summary>
        private void RequireNozzleClear(string reason)
        {
            Run.NozzleClearRequired = true;
            Run.NozzleClearReason = reason;
            Save();
            LogWorker.Instance.RecordLog("Yêu cầu lấy sản phẩm ra khỏi tay hút: " + reason,
                Utility.Log.LogLevel.Warning, "Auto");
        }

        private void Save() { McDataManager.SaveRunningData(); }

        /// <summary>Dừng vì lỗi: ghi log, ghi tiến độ, để McControl chuyển trạng thái.</summary>
        private bool Halt(string reason)
        {
            _status.Message = Loc.F("Auto.HaltAtStep", Run.AutoState, reason);
            LogWorker.Instance.RecordLog(_status.Message, Utility.Log.LogLevel.Error, "Auto");
            Save();
            return false;
        }

        private bool Stopped(string reason)
        {
            _status.Message = Loc.F("Auto.StoppedAtStep", Run.AutoState, reason);
            LogWorker.Instance.RecordLog(_status.Message, Utility.Log.LogLevel.Info, "Auto");
            Save();
            return false;
        }
    }
}
