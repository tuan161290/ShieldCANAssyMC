using System;
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
    /// <summary>
    /// <b>Chạy rà</b> — cho máy chạy liên tục KHÔNG SẢN PHẨM để rà cơ khí (Tuấn yêu cầu 2026-09-23).
    ///
    /// <para><b>Chỉ chạy trục.</b> X, Y, Z, A đi đúng quỹ đạo của chu trình thật — quét tray lắp ráp bằng CAM
    /// top-down, hút ở tray đầu vào, ra vị trí soi CAM dưới, xoay A rồi đặt vào tray lắp ráp, đủ cả hai tay. KHÔNG đụng van hút, KHÔNG đụng van thổi,
    /// KHÔNG kích Flow nào (Tuấn chọn 2026-09-23), nên không cần hàng, không cần khí, không cần VisionMaster mở.
    /// Cũng không cộng bộ đếm OK/NG, không ghi trạng thái ô, không lưu tiến độ — chạy rà xong thì sổ sách của
    /// chu trình thật vẫn y nguyên như trước.</para>
    ///
    /// <para><b>Vì sao hạ Z xuống hốc trống không va.</b> Cao độ hút/đặt được teach với sản phẩm nằm trong hốc,
    /// tức đầu hút dừng ở MẶT TRÊN của sản phẩm. Bỏ sản phẩm ra thì đầu hút dừng đúng chỗ cũ — lơ lửng trên đáy
    /// hốc đúng bằng chiều dày sản phẩm. Chạy rà đi đúng cao độ đã teach, không hạ thêm, và không có nhánh
    /// "hút hụt thì hạ sâu thêm" như <see cref="Routine_Auto"/>.</para>
    ///
    /// <para><b>Khởi động vẫn qua Cặp B.</b> Nút gạt ở trang Main chỉ CHỌN chế độ; máy chạy thật vẫn bắt buộc
    /// chìa AUTO → Armed → bấm đủ hai nút Cặp B ngoài jig, đúng luật an toàn hai tay đặt 2026-08-20. Không có
    /// đường nào từ màn hình cảm ứng làm trục chạy.</para>
    ///
    /// <para><b>Checkpoint</b> đặt giữa các chặng, mỗi chặng đều kết thúc với Z đã lên cao độ an toàn — STOP giữa
    /// chừng không bao giờ bỏ máy lại với đầu hút nằm trong hốc tray.</para>
    /// </summary>
    public class Routine_DryCycle
    {
        private readonly MotionConnect _motion;
        private readonly McFunction _func;
        private readonly McStatus _status;
        private readonly Func<SystemData> _sys;

        /// <summary>Xin dừng ở checkpoint gần nhất — McControl đặt khi operator bấm STOP.</summary>
        public volatile bool PauseRequested;

        private int _inIdx, _assyIdx;

        /// <summary>
        /// Bước dải góc đặt, đếm RIÊNG TỪNG TAY (Tuấn phát hiện 2026-09-24).
        ///
        /// <para>Trước đó chỉ có một biến đếm chung, tăng mỗi lần đặt. Mỗi nhịp có đúng hai lần đặt và dải góc
        /// dài đúng bốn bước, nên hai số đó khoá pha với nhau vĩnh viễn: tay #1 luôn rơi vào bước chẵn (0° và
        /// 0°) còn tay #2 luôn rơi vào bước lẻ (+ngưỡng và −ngưỡng). Kết quả là A1 không bao giờ xoay, còn A2
        /// chỉ quanh quẩn ±ngưỡng — ngược hẳn ý định rà cả dải góc trên cả hai tay.</para>
        /// </summary>
        private readonly int[] _angleStep = new int[3];

        /// <summary>Góc đặt vừa ra lệnh cho từng tay — để bài tự kiểm tra đọc được LỆNH, không phải đoán qua encoder
        /// (encoder còn mang cả góc hút, dễ nhìn nhầm thành đã xoay).</summary>
        private readonly double[] _lastPlaceAngle = new double[3];

        public double LastPlaceAngle(int nozzle) { return _lastPlaceAngle[nozzle]; }
        private int _strokes, _laps, _sweeps;

        /// <summary>Hàng đã quét gần nhất — chỉ dùng khi bật quét từng hàng. −1 = chưa quét hàng nào.</summary>
        private int _scannedRow = -1;
        private readonly Stopwatch _clock = new Stopwatch();

        /// <summary>Số nhịp hút–đặt đã chạy kể từ lần bắt đầu gần nhất.</summary>
        public int Strokes { get { return _strokes; } }

        /// <summary>Số lần chạy hết một lượt tray lắp ráp.</summary>
        public int Laps { get { return _laps; } }

        /// <summary>Số lượt CAM top-down quét tray lắp ráp — cả tray, hoặc một hàng nếu bật quét từng hàng.</summary>
        public int Sweeps { get { return _sweeps; } }

        /// <summary>Thời gian đã chạy rà, cộng dồn qua các lần STOP rồi chạy tiếp.</summary>
        public TimeSpan Elapsed { get { return _clock.Elapsed; } }

        public Routine_DryCycle(MotionConnect motion, McFunction func, McStatus status, Func<SystemData> sys)
        {
            _motion = motion;
            _func = func;
            _status = status;
            _sys = sys;
        }

        /// <summary>Về vạch xuất phát — gọi khi BẬT chế độ chạy rà, không gọi khi chạy tiếp sau STOP.</summary>
        public void ResetCounters()
        {
            _inIdx = 0;
            _assyIdx = 0;
            _angleStep[1] = _angleStep[2] = 0;
            _lastPlaceAngle[1] = _lastPlaceAngle[2] = 0;
            _strokes = 0;
            _laps = 0;
            _sweeps = 0;
            _scannedRow = -1;
            _clock.Reset();
        }

        public async Task<bool> RunAsync(CancellationToken ct)
        {
            McFunction.MarkCycleFlow();
            try
            {
                var model = McDataManager.Model;
                if (model == null) return Stopped(Loc.T("Dry.NoModel"));
                if (!model.InputTray.IsTaught || !model.AssyTray.IsTaught)
                    return Stopped(Loc.T("Dry.TraysNotTaught"));

                // Chạy rà là chu trình KHÔNG sản phẩm: còn hàng trên tay thì đầu hút sẽ dúi sản phẩm xuống hốc
                // tray mà không có bước nhả nào. Chặn ngay từ đầu, đừng để phát hiện lúc đã hạ Z.
                if (_func.HasProduct(1) || _func.HasProduct(2))
                    return Stopped(Loc.T("Dry.NozzleHasProduct"));

                _clock.Start();
                LogWorker.Instance.RecordLog(
                    "Bắt đầu chạy rà (không sản phẩm, chỉ chạy trục)", Utility.Log.LogLevel.Info, "DryRun");

                var legs = new Func<CancellationToken, Task<bool>>[]
                {
                    c => ScanAsync(c),
                    c => PickAsync(1, c),
                    c => PickAsync(2, c),
                    // Tay #2 gần CAM1 hơn nên ra trước, giống thứ tự S20/S21 của chu trình thật.
                    c => AlignAsync(2, c),
                    c => AlignAsync(1, c),
                    c => PlaceAsync(1, c),
                    c => PlaceAsync(2, c)
                };

                while (true)
                {
                    foreach (var leg in legs)
                    {
                        if (ct.IsCancellationRequested) return Stopped(Loc.T("Mc.Cancelled"));
                        if (PauseRequested) { PauseRequested = false; return Paused(); }
                        if (!await leg(ct).ConfigureAwait(false)) return false;
                    }
                    Advance();
                }
            }
            catch (OperationCanceledException)
            {
                return Stopped(Loc.T("Mc.Cancelled"));
            }
            catch (Exception ex)
            {
                AlarmManager.RaiseEvent(AlarmCode.RoutineCrashed, "DryRun", ex.Message);   // sự kiện: RESET gỡ được (30/09)
                return Stopped(Loc.F("Mc.Unexpected", ex.Message));
            }
        }

        // ───────────────────────── bốn chặng ─────────────────────────

        /// <summary>
        /// Chặng quét trước tray lắp ráp — đi đúng đường CAM top-down của <c>S05_ScanAssy</c>, chỉ không kích
        /// Flow nào (Tuấn hỏi 2026-09-24: chạy rà thiếu hẳn chặng này). Đây là chặng gantry chạy nhiều nhất
        /// trong cả chu trình, bỏ nó ra thì chạy rà không rà đúng quãng đường máy thật đi.
        ///
        /// <para><b>Theo đúng tuỳ chọn của chu trình thật</b> (<see cref="SystemData.ScanAssyByRow"/>): tắt thì
        /// quét cả tray MỘT LƯỢT ở đầu mỗi lượt tray; bật thì quét một hàng ngay trước khi làm hàng đó. Hai
        /// cách đi cùng một quãng đường nhưng khác THỨ TỰ, mà thứ tự mới là thứ Tuấn cần rà — tuỳ chọn ấy sinh
        /// ra chính vì nghi chạy Y2 hết chiều dài tray làm xê dịch tray.</para>
        /// </summary>
        private async Task<bool> ScanAsync(CancellationToken ct)
        {
            var run = McDataManager.RunData.AssyTray;

            if (_sys().ScanAssyByRow)
            {
                int row, col;
                CellOf(run, _assyIdx, out row, out col);
                if (row == _scannedRow) return true;
                _scannedRow = row;
                return await SweepAsync(run, row, ct).ConfigureAwait(false);
            }

            _scannedRow = -1;
            if (_assyIdx != 0) return true;
            return await SweepAsync(run, -1, ct).ConfigureAwait(false);
        }

        /// <summary>
        /// Đưa CAM top-down qua từng ô — cả tray (<paramref name="onlyRow"/> &lt; 0) hoặc một hàng.
        ///
        /// <para>Xét yêu cầu dừng NGAY TRONG vòng quét, không đợi hết chặng: quét cả tray mất hàng chục giây,
        /// bắt operator chờ ngần ấy sau khi bấm STOP là sai. Bỏ dở giữa chừng vẫn an toàn — mỗi lần tới ô đều
        /// kết thúc với Z ở cao độ an toàn — và chặng ngoài sẽ thấy cờ dừng ở checkpoint kế tiếp.</para>
        /// </summary>
        private async Task<bool> SweepAsync(TrayState run, int onlyRow, CancellationToken ct)
        {
            var tray = McDataManager.Model.AssyTray;
            _sweeps++;

            for (int i = 0; i < run.CellCount; i++)
            {
                if (ct.IsCancellationRequested) return Stopped(Loc.T("Mc.Cancelled"));
                if (PauseRequested) return true;

                int row, col;
                CellOf(run, i, out row, out col);
                if (onlyRow >= 0 && row != onlyRow) continue;

                var cam = tray.CamPosFor(row, col);
                if (!await _func.MoveToCellAsync(tray.AxisRow, cam.Y, cam.X, ct).ConfigureAwait(false))
                    return Stopped(Loc.F("Dry.ScanCellFailed", row + 1, col + 1));
            }
            return true;
        }

        // ───────────────────────── ba chặng còn lại ─────────────────────────

        /// <summary>Tới ô tray đầu vào, hạ xuống cao độ hút rồi nâng lên — không đụng van hút.</summary>
        private async Task<bool> PickAsync(int nozzle, CancellationToken ct)
        {
            var model = McDataManager.Model;
            var tray = model.InputTray;
            string zName = nozzle == 1 ? AxisId.Z1 : AxisId.Z2;

            int row, col;
            CellOf(McDataManager.RunData.InputTray, _inIdx + nozzle - 1, out row, out col);
            var cell = tray.CellPos(zName, row, col);

            if (!await _func.MoveToCellForPickAsync(nozzle, model.PickAngle(nozzle), tray.AxisRow, cell.Y, cell.X, ct).ConfigureAwait(false))
                return Stopped(Loc.F("Dry.PickCellFailed", nozzle));
            // Cùng nhịp chu trình thật: qua điểm giảm tốc hay không theo 4 ô ở trang Setting (Tuấn 2026-10-07).
            var sys = McDataManager.SysData;
            if (!await _func.MoveZDownAsync(zName, tray.ZPickFor(zName), tray.ZSlowFor(zName, sys.InputZSlowDown), ct).ConfigureAwait(false))
                return Stopped(Loc.F("Dry.ZPickDownFailed", zName));
            if (!await _func.MoveZUpAsync(zName, _motion.ZSafe, tray.ZSlowFor(zName, sys.InputZSlowUp), ct).ConfigureAwait(false))
                return Stopped(Loc.F("Dry.ZUpFailed", zName));
            return true;
        }

        /// <summary>X ra vị trí soi CAM bottom-up của đúng tay — chặng này chu trình thật kích Flow, chạy rà thì không.</summary>
        private async Task<bool> AlignAsync(int nozzle, CancellationToken ct)
        {
            double x = McDataManager.Model.AlignPosX(nozzle);
            if (!await _func.MoveAsync(AxisId.X, x, Speed(AxisId.X), ct).ConfigureAwait(false))
                return Stopped(Loc.F("Dry.AlignPosFailed", nozzle));
            return true;
        }

        /// <summary>Xoay A rồi tới ô tray lắp ráp, hạ xuống cao độ đặt rồi nâng lên — không đụng van thổi.</summary>
        private async Task<bool> PlaceAsync(int nozzle, CancellationToken ct)
        {
            var model = McDataManager.Model;
            var tray = model.AssyTray;
            string zName = nozzle == 1 ? AxisId.Z1 : AxisId.Z2;
            string aName = nozzle == 1 ? AxisId.A1 : AxisId.A2;

            int row, col;
            CellOf(McDataManager.RunData.AssyTray, _assyIdx + nozzle - 1, out row, out col);
            var cell = tray.CellPos(zName, row, col);

            if (!await _func.EnsureZSafeAsync(ct).ConfigureAwait(false))
                return Stopped(Loc.T("Dry.ZSafeFailed"));

            // A chạy song song với X/Y đúng như lúc đặt thật, nên khớp quay được rà mà không tốn thêm thời gian.
            var moves = new[]
            {
                _func.MoveAsync(aName, PlaceAngle(nozzle), Speed(aName), ct),
                _func.MoveAsync(AxisId.X, cell.X, Speed(AxisId.X), ct),
                _func.MoveAsync(tray.AxisRow, cell.Y, Speed(tray.AxisRow), ct)
            };
            var done = await Task.WhenAll(moves).ConfigureAwait(false);
            foreach (bool r in done)
                if (!r) return Stopped(Loc.F("Dry.PlaceCellFailed", nozzle));

            // Tray lắp ráp: theo Setting như chu trình thật.
            var sys = McDataManager.SysData;
            if (!await _func.MoveZDownAsync(zName, tray.ZPickFor(zName), tray.ZSlowFor(zName, sys.AssyZSlowDown), ct).ConfigureAwait(false))
                return Stopped(Loc.F("Dry.ZPlaceDownFailed", zName));
            if (!await _func.MoveZUpAsync(zName, _motion.ZSafe, tray.ZSlowFor(zName, sys.AssyZSlowUp), ct).ConfigureAwait(false))
                return Stopped(Loc.F("Dry.ZUpFailed", zName));
            return true;
        }

        // ───────────────────────── đi ô, đi góc, đếm ─────────────────────────

        /// <summary>
        /// Góc đặt của LẦN ĐẶT NÀY: chạy vòng quanh 0 → +ngưỡng → 0 → −ngưỡng, đổi sau mỗi lần đặt đúng như
        /// chu trình thật (mỗi sản phẩm có lượng bù riêng). Chu trình thật xoay A tới <c>CmdA</c> = góc lúc chụp
        /// + góc bù, mà góc bù bị chặn ở <see cref="SystemData.MaxCompensationAngleDeg"/> và giờ lúc hút A luôn
        /// ở 0 — nên đây đúng là dải góc trục A làm việc thật, không phải một dải bịa ra cho to.
        /// </summary>
        /// <summary>
        /// Phần ngưỡng bù mà chạy rà quét tới. <b>Cố ý KHÔNG quét hết ±ngưỡng</b> (Tuấn chốt 2026-09-24):
        /// chu trình thật chỉ xoay tới góc làm sản phẩm khớp hốc, còn chạy rà thì đầu hút <b>rỗng</b> hạ vào
        /// hốc tray ở góc bất kỳ — xoay tới biên là đẩy cái gờ đầu hút vào thành hốc mà không được gì.
        /// Một phần ba đủ để chứng minh cả hai trục A xoay được và không kẹt.
        ///
        /// <para>Để theo TỶ LỆ chứ không ghi cứng số độ: đổi <see cref="SystemData.MaxCompensationAngleDeg"/>
        /// thì chạy rà tự co theo, không có con số thứ hai nằm lạc chỗ khác rồi quên.</para>
        /// </summary>
        private const double DrySwingFraction = 1.0 / 3.0;

        private double PlaceAngle(int nozzle)
        {
            double span = Math.Abs(_sys().MaxCompensationAngleDeg) * DrySwingFraction;
            int step = _angleStep[nozzle]++ & 3;
            double bu = step == 1 ? span : step == 3 ? -span : 0.0;
            _lastPlaceAngle[nozzle] = bu;

            // Lượng bù chỉ quét MỘT PHẦN ngưỡng — xem DrySwingFraction.
            // CmdA của chu trình thật = GÓC LÚC CHỤP + góc bù, mà góc lúc chụp bây giờ chính là góc hút
            // (A về góc hút trước khi hạ Z, không còn đứng ở CmdA cũ). Chạy rà phải đi theo đúng công thức
            // ấy. Trước đây tôi trả về góc TUYỆT ĐỐI 0/±ngưỡng, nên tay #2 luôn dừng đúng 15° thay vì quanh
            // quẩn góc hút — Tuấn phát hiện 2026-09-24: "hút thì đúng góc, thả mới bị quay về 15 độ".
            return AxisAngle.Wrap(McDataManager.Model.PickAngle(nozzle) + bu);
        }

        /// <summary>Hàng/cột của ô thứ <paramref name="index"/> theo đúng thứ tự đi ô của chu trình thật (rắn bò).</summary>
        private static void CellOf(TrayState tray, int index, out int row, out int col)
        {
            int n = tray.CellCount;
            if (n <= 0) { row = 0; col = 0; return; }
            tray.RowCol(((index % n) + n) % n, out row, out col);
        }

        private void Advance()
        {
            _strokes += 2;

            var run = McDataManager.RunData;
            int assyCount = run.AssyTray.CellCount;
            _inIdx += 2;
            _assyIdx += 2;

            if (assyCount > 0 && _assyIdx >= assyCount)
            {
                _assyIdx %= assyCount;
                _laps++;
                LogWorker.Instance.RecordLog(string.Format(CultureInfo.InvariantCulture,
                    "Chạy rà: xong lượt tray thứ {0} — {1} nhịp, {2:0} phút",
                    _laps, _strokes, _clock.Elapsed.TotalMinutes),
                    Utility.Log.LogLevel.Info, "DryRun");
            }

            int inCount = run.InputTray.CellCount;
            if (inCount > 0) _inIdx %= inCount;

            _status.Message = string.Format(CultureInfo.InvariantCulture, Loc.T("Dry.Progress"),
                _laps + 1, _strokes, _sweeps, _clock.Elapsed.TotalMinutes);
        }

        private double Speed(string axis) { return _sys().GetAxis(axis).MoveSpeed; }

        private bool Paused()
        {
            _clock.Stop();
            _status.State = EMcState.Paused;
            _status.Message = string.Format(CultureInfo.InvariantCulture, Loc.T("Dry.Paused"),
                _strokes, _laps, _clock.Elapsed.TotalMinutes);
            LogWorker.Instance.RecordLog(_status.Message, Utility.Log.LogLevel.Info, "DryRun");
            return true;
        }

        private bool Stopped(string why)
        {
            _clock.Stop();
            _status.Message = Loc.F("Dry.Stopped", why);
            LogWorker.Instance.RecordLog(_status.Message, Utility.Log.LogLevel.Warning, "DryRun");
            return false;
        }
    }
}
