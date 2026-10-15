using System;
using System.Collections.ObjectModel;
using System.Drawing;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using ShieldCanAssy.Core.Controller;
using ShieldCanAssy.Core.Data;
using ShieldCanAssy.Core.Infor;
using ShieldCanAssy.Core.Lang;
using ShieldCanAssy.Wpf.Mvvm;

namespace ShieldCanAssy.Wpf.ViewModels
{
    /// <summary>Trang Main: trạng thái máy, nút vận hành, hai lưới tray, bộ đếm.</summary>
    public class MainViewModel : ObservableObject
    {
        private readonly DispatcherTimer _timer, _clockTimer;

        public MainViewModel()
        {
            // Công tắc AUTO/MANUAL (Tuấn 2026-10-13): mỗi lần bấm gạt sang chế độ kia của chế độ ĐANG áp dụng
            // (Status.Mode). McControl vẫn tự từ chối AUTO khi chưa đủ điều kiện — công tắc khi đó đứng yên.
            ToggleModeCommand = new RelayCommand(() =>
                McControl.RequestSetMode(McControl.Status.Mode == EMcMode.Auto ? EMcMode.Manual : EMcMode.Auto));
            // Nút mềm trên UI CỐ Ý chỉ làm được đúng việc của Cặp A (vào Armed) — máy chạy
            // thật (Idle/Armed → Running, hay xác nhận đổi tray giữa chừng) chỉ nhận từ Cặp B
            // vật lý (2 tay), không có đường nào từ màn hình cảm ứng bỏ qua được yêu cầu 2 tay
            // (xem McControl.RequestArm/RequestGo, 2026-08-20).
            ArmCommand = new RelayCommand(McControl.RequestArm);
            StopCommand = new RelayCommand(McControl.RequestStop);
            ResetCommand = new RelayCommand(McControl.RequestReset);
            MuteCommand = new RelayCommand(McControl.RequestMuteBuzzer);
            OriginCommand = new RelayCommand(McControl.RequestOrigin);
            InitCommand = new RelayCommand(McControl.RequestInit);
            ResetCountersCommand = new RelayCommand(() => { McDataManager.RunData.ResetCounters(); Refresh(); });
            // Tray 1, và Tray 2 nếu đã teach, thành có hàng — xem McControl.ReloadInputTray.
            ResetInputTrayCommand = new RelayCommand(() => { McControl.ReloadInputTray(); Refresh(); });
            // Mọi ô thành trống, bỏ số đo quét trước của tray cũ, và nếu tay hút còn hàng thì bắt lấy
            // ra trước khi chạy tiếp — xem McControl.ResetAssyTray.
            ResetAssyTrayCommand = new RelayCommand(() => { McControl.ResetAssyTray(); Refresh(); });
            // Hai tray đầu vào (2026-10-15): card hiện một tray mỗi lúc, nút Select chọn tray — khung giữ nguyên.
            InputTray = new TrayGridViewModel("Main.InputTray", "Main.ReloadInputTray", ResetInputTrayCommand,
                                              Core.Data.Model.ModelData.InputTraySlots);
            AssyTray = new TrayGridViewModel("Main.AssyTray", "Main.ReloadAssyTray", ResetAssyTrayCommand);
            // Nút chạy rà và chọn tay hút chạy Auto đã chuyển sang trang Setting (Tuấn 2026-10-08) — SettingViewModel.

            // TrayState là POCO ghi từ thread điều khiển, không bindable — đồng bộ theo nhịp
            // hiển thị. 300 ms là quá đủ: ô tray đổi trạng thái theo nhịp giây, không phải ms.
            _timer = new DispatcherTimer(DispatcherPriority.Background)
            {
                Interval = TimeSpan.FromMilliseconds(300)
            };
            _timer.Tick += (_, __) => Refresh();
            _timer.Start();
            _clockTimer = new DispatcherTimer(DispatcherPriority.Background) { Interval = TimeSpan.FromMilliseconds(100) };
            _clockTimer.Tick += (_, __) => RefreshAssyTrayClock();
            _clockTimer.Start();
            Refresh();
            RefreshAssyTrayClock();
        }

        public McStatus Status => McControl.Status;

        /// <summary>Cho DataTrigger của nút RESET (đỏ khi còn alarm) — khớp ShellViewModel.ActiveAlarms,
        /// cần lại ở đây vì nút Tắt còi/RESET vừa dời từ MainWindow xuống trang Main.</summary>
        public ObservableCollection<AlarmData> ActiveAlarms => AlarmManager.ActiveAlarms;

        // Đèn nút trang Main (Tuấn 2026-09-28): RESET đỏ khi còn alarm, STOP đỏ khi máy không chạy — cùng ranh
        // giới với đèn nút STOP cứng (McControl.UpdateOutputs). Trước đó RESET bind HasActiveAlarms nhưng
        // property này chỉ có ở ShellViewModel nên nút không bao giờ sáng.
        private bool _hasActiveAlarms, _isStopped;
        public bool HasActiveAlarms { get => _hasActiveAlarms; private set => SetProperty(ref _hasActiveAlarms, value); }
        public bool IsStopped { get => _isStopped; private set => SetProperty(ref _isStopped, value); }

        /// <summary>Lần dừng trước là dừng giữa hành trình — phải Init (hoặc Về gốc) trước khi chạy tiếp; nút Init sáng cam.</summary>
        private bool _needInit;
        public bool NeedInit { get => _needInit; private set => SetProperty(ref _needInit, value); }

        /// <summary>Tay hút dùng trong Auto: 0 cả hai, 1/2 chỉ một tay — xem SystemData.OnlyNozzle. Chọn ở trang Setting,
        /// trang Main chỉ hiện dòng nhắc khi chạy một tay.</summary>
        private int _onlyNozzle;
        public int OnlyNozzle { get => _onlyNozzle; private set => SetProperty(ref _onlyNozzle, value); }

        private string _onlyNozzleText = "";
        public string OnlyNozzleText { get => _onlyNozzleText; private set => SetProperty(ref _onlyNozzleText, value); }

        public TrayGridViewModel InputTray { get; }
        public TrayGridViewModel AssyTray { get; }

        private int _countOk, _countNg, _countTotal, _consecutiveNg;
        public int CountOk { get => _countOk; private set => SetProperty(ref _countOk, value); }
        public int CountNg { get => _countNg; private set => SetProperty(ref _countNg, value); }
        public int CountTotal { get => _countTotal; private set => SetProperty(ref _countTotal, value); }

        /// <summary>Số sản phẩm NG liên tiếp gần nhất — dấu hiệu sớm của một xu hướng lỗi, trước
        /// khi nó kịp kéo tỉ lệ NG tổng lên đáng kể.</summary>
        public int ConsecutiveNg { get => _consecutiveNg; private set => SetProperty(ref _consecutiveNg, value); }

        private bool _hasConsecutiveNg;
        public bool HasConsecutiveNg { get => _hasConsecutiveNg; private set => SetProperty(ref _hasConsecutiveNg, value); }

        private double _ngRate;
        public double NgRate { get => _ngRate; private set => SetProperty(ref _ngRate, value); }

        // Cycle time theo tray lắp ráp (Tuấn 2026-10-09, thay hàng CT theo lượt): CT tray = giờ thực từ lúc máy chạy với
        // tray tới lúc tray hết ô trống, tính cả lúc chờ tray đầu vào và lúc STOP; CT TB = CT tray ÷ số ô đặt OK của tray.
        // Tray đang làm (đếm liên tục, phân giải 0.1 s) và tray vừa xong.
        private string _assyTrayElapsed = "—", _lastAssyTray = "—", _assyTrayAvg = "—", _lastAssyTrayAvg = "—";
        public string AssyTrayElapsed { get => _assyTrayElapsed; private set => SetProperty(ref _assyTrayElapsed, value); }
        public string LastAssyTray { get => _lastAssyTray; private set => SetProperty(ref _lastAssyTray, value); }
        public string AssyTrayAvg { get => _assyTrayAvg; private set => SetProperty(ref _assyTrayAvg, value); }
        public string LastAssyTrayAvg { get => _lastAssyTrayAvg; private set => SetProperty(ref _lastAssyTrayAvg, value); }

        private static string Sec(double sec) => sec.ToString("0.00", CultureInfo.InvariantCulture) + " s";

        private static string Clock(double sec)
        {
            var t = TimeSpan.FromSeconds(Math.Floor(Math.Max(0, sec) * 10) / 10);
            return t.TotalHours >= 1 ? t.ToString(@"h\:mm\:ss\.f") : t.ToString(@"mm\:ss\.f");
        }

        /// <summary>Chạy theo nhịp 100 ms riêng (_clockTimer) — Refresh 300 ms thì đồng hồ 0.1 s nhảy cóc 0.3 s.</summary>
        private void RefreshAssyTrayClock()
        {
            var run = McDataManager.RunData;
            if (run == null) return;
            var start = run.AssyTrayStartedAt;

            bool hasLast = run.LastAssyTrayTimeSec > 0;
            LastAssyTray = hasLast ? Clock(run.LastAssyTrayTimeSec) : "—";
            LastAssyTrayAvg = run.LastAssyTrayProducts > 0 ? Sec(run.LastAssyTrayPerProductSec) : "—";

            double currentAvg = 0;
            if (start != null)
            {
                double sec = (DateTime.Now - start.Value).TotalSeconds;
                int done = 0;
                foreach (var c in run.AssyTray.Cells.ToArray()) if (c == ECellState.Ok) done++;
                if (done > 0) currentAvg = sec / done;
                AssyTrayElapsed = Clock(sec);
                AssyTrayAvg = done > 0 ? Sec(currentAvg) : "—";
            }
            else
            {
                AssyTrayElapsed = "—";
                AssyTrayAvg = "—";
            }

            // Năng suất theo CT TB của tray vừa xong — số đã trọn một tray; chưa có tray nào xong thì theo tray đang làm.
            double avg = run.LastAssyTrayProducts > 0 ? run.LastAssyTrayPerProductSec : currentAvg;
            UphText = avg > 0 ? Loc.F("Main.Uph", 3600.0 / avg) : Loc.T("Main.UphNone");
        }

        /// <summary>Năng suất ước tính nếu máy chạy liên tục ở CT TB một tray — ước lượng thô, không phải sản lượng thực
        /// đếm được.</summary>
        private string _uphText = "";
        public string UphText { get => _uphText; private set => SetProperty(ref _uphText, value); }

        private string _lastCompensation;
        public string LastCompensation { get => _lastCompensation; private set => SetProperty(ref _lastCompensation, value); }

        private bool _waitingOperator;
        public bool WaitingOperator { get => _waitingOperator; private set => SetProperty(ref _waitingOperator, value); }

        /// <summary>Số nhịp và thời gian đã chạy rà — hiện dưới banner ở trang Main.</summary>
        public string DryRunText { get => _dryRunText; private set => SetProperty(ref _dryRunText, value); }
        private string _dryRunText = "";

        // Ảnh chụp gần nhất của mỗi khung CAM (top-down, bottom-up tay #1, tay #2) — chỉ convert lại khi Vision
        // thay bằng Bitmap MỚI (so bằng reference), vì Refresh() chạy mỗi 300ms còn ảnh chỉ đổi mỗi lần Trigger thật.
        private Bitmap _lastTopDownSrc, _lastTool1Src, _lastTool2Src;
        private BitmapSource _topDownImage, _tool1Image, _tool2Image;
        public BitmapSource TopDownImage { get => _topDownImage; private set => SetProperty(ref _topDownImage, value); }
        public BitmapSource Tool1Image { get => _tool1Image; private set => SetProperty(ref _tool1Image, value); }
        public BitmapSource Tool2Image { get => _tool2Image; private set => SetProperty(ref _tool2Image, value); }

        // Không có sẵn timestamp chụp từ IVisionDevice — ghi lại tại thời điểm phát hiện ảnh
        // MỚI trong Refresh(), để operator biết ảnh đang xem có phải vừa chụp hay đã cũ.
        private string _topDownCapturedText = "", _tool1CapturedText = "", _tool2CapturedText = "";
        public string TopDownCapturedText { get => _topDownCapturedText; private set => SetProperty(ref _topDownCapturedText, value); }
        public string Tool1CapturedText { get => _tool1CapturedText; private set => SetProperty(ref _tool1CapturedText, value); }
        public string Tool2CapturedText { get => _tool2CapturedText; private set => SetProperty(ref _tool2CapturedText, value); }

        // Kết quả soi mới nhất in trên từng khung CAM (Tuấn 2026-10-01) — xem VisionReadout.
        private string _tool1Readout, _tool2Readout, _topDownReadout;
        public string Tool1Readout { get => _tool1Readout; private set => SetProperty(ref _tool1Readout, value); }
        public string Tool2Readout { get => _tool2Readout; private set => SetProperty(ref _tool2Readout, value); }
        public string TopDownReadout { get => _topDownReadout; private set => SetProperty(ref _topDownReadout, value); }

        private static string OrWaiting(string s) => string.IsNullOrEmpty(s) ? Loc.T("Main.NotTriggered") : s;

        // Cùng kết quả đó chia thành bảng — khung CAM hiện bảng, bản chữ ở trên làm tooltip (Tuấn 2026-10-07).
        // Dựng lại khi đổi ngôn ngữ — Refresh 300 ms gán lại nên khung tự đổi chữ.
        private static ReadoutCard _waitingCard;
        private static ELanguage _waitingLang;
        private static ReadoutCard WaitingCard
        {
            get
            {
                if (_waitingCard == null || _waitingLang != Loc.Language)
                {
                    _waitingCard = new ReadoutCard("", new string[0], new ReadoutRow[0], Loc.T("Main.NotTriggered"));
                    _waitingLang = Loc.Language;
                }
                return _waitingCard;
            }
        }
        private ReadoutCard _tool1Card = WaitingCard, _tool2Card = WaitingCard, _topDownCard = WaitingCard;
        public ReadoutCard Tool1Card { get => _tool1Card; private set => SetProperty(ref _tool1Card, value); }
        public ReadoutCard Tool2Card { get => _tool2Card; private set => SetProperty(ref _tool2Card, value); }
        public ReadoutCard TopDownCard { get => _topDownCard; private set => SetProperty(ref _topDownCard, value); }

        // Bấm tiêu đề bảng khung CAM bottom-up: đổi giữa Lệch/Đích và Đích tách thành teach + bù vision + bù chung + bù
        // riêng (Tuấn 2026-10-07) — chung cho cả hai tay. Không lưu, mở lại App về kiểu Lệch/Đích.
        private bool _showBreakdown;
        public bool ShowBreakdown { get => _showBreakdown; private set => SetProperty(ref _showBreakdown, value); }

        public void ToggleBreakdown()
        {
            ShowBreakdown = !ShowBreakdown;
            SyncToolCards();
        }

        private void SyncToolCards()
        {
            Tool1Card = Shown(VisionReadout.Tool1Card);
            Tool2Card = Shown(VisionReadout.Tool2Card);
        }

        private ReadoutCard Shown(ReadoutCard c) =>
            c == null ? WaitingCard : ShowBreakdown && c.Breakdown != null ? c.Breakdown : c;

        // Giờ chụp của ảnh đang hiện ở từng khung — chữ "Chụp lúc …" dựng lại mỗi Refresh để đổi ngôn ngữ là đổi theo.
        private DateTime? _topDownAt, _tool1At, _tool2At;

        private static string CapturedText(DateTime? at) =>
            at == null ? "" : Loc.F("Main.CapturedAt", at.Value.ToString("HH:mm:ss"));

        /// <summary>Vision vừa có Bitmap mới cho một khung thì đổi ảnh và ghi giờ chụp.</summary>
        private static void SyncImage(Bitmap src, ref Bitmap last, Action<BitmapSource> setImage, ref DateTime? at)
        {
            if (ReferenceEquals(src, last)) return;
            last = src;
            setImage(src != null ? ToBitmapSource(src) : null);
            at = src != null ? DateTime.Now : (DateTime?)null;
        }

        [DllImport("gdi32.dll")]
        private static extern bool DeleteObject(IntPtr hObject);

        private static BitmapSource ToBitmapSource(Bitmap bmp)
        {
            IntPtr hBitmap = bmp.GetHbitmap();
            try
            {
                var src = Imaging.CreateBitmapSourceFromHBitmap(
                    hBitmap, IntPtr.Zero, Int32Rect.Empty, BitmapSizeOptions.FromEmptyOptions());
                src.Freeze();
                return src;
            }
            finally
            {
                DeleteObject(hBitmap);
            }
        }

        private void Refresh()
        {
            var vision = McControl.Vision;
            if (vision != null)
            {
                SyncImage(vision.LastTopDownImage, ref _lastTopDownSrc, v => TopDownImage = v, ref _topDownAt);
                SyncImage(vision.LastBottomUpImageTool1, ref _lastTool1Src, v => Tool1Image = v, ref _tool1At);
                SyncImage(vision.LastBottomUpImageTool2, ref _lastTool2Src, v => Tool2Image = v, ref _tool2At);
            }
            TopDownCapturedText = CapturedText(_topDownAt);
            Tool1CapturedText = CapturedText(_tool1At);
            Tool2CapturedText = CapturedText(_tool2At);
            Tool1Readout = OrWaiting(VisionReadout.Tool1);
            Tool2Readout = OrWaiting(VisionReadout.Tool2);
            TopDownReadout = OrWaiting(VisionReadout.Top);
            SyncToolCards();
            TopDownCard = VisionReadout.TopCard ?? WaitingCard;

            HasActiveAlarms = AlarmManager.ActiveAlarms.Count > 0;
            IsStopped = Status.State != EMcState.Running && Status.State != EMcState.Pausing &&
                        Status.State != EMcState.WaitOperator;   // chờ nạp tray vẫn là đang chạy Auto (Tuấn 01/10)
            OnlyNozzle = McDataManager.SysData?.OnlyNozzle ?? 0;
            OnlyNozzleText = Loc.F("Main.OnlyNozzle", OnlyNozzle);
            NeedInit = McDataManager.RunData.UnsafePause;

            var run = McDataManager.RunData;

            InputTray.Sync(run.InputTray);
            AssyTray.Sync(run.AssyTray, McControl.AutoRoutine?.AssyScannedCells);
            InputTray.SetCounts(run.InputCountOk, run.InputCountNg);
            AssyTray.SetCounts(run.AssyCountOk, run.AssyCountNg);

            CountOk = run.CountOk;
            CountNg = run.CountNg;
            CountTotal = run.CountTotal;
            ConsecutiveNg = run.ConsecutiveNg;
            HasConsecutiveNg = run.ConsecutiveNg > 0;
            NgRate = run.NgRatePercent;

            var dry = McControl.DryRoutine;
            DryRunText = dry == null || (dry.Strokes == 0 && dry.Elapsed.TotalSeconds < 1)
                ? Loc.T("Main.DryRunIdle")
                : string.Format(CultureInfo.InvariantCulture, Loc.T("Main.DryRunProgress"),
                                dry.Strokes, dry.Sweeps, dry.Laps, dry.Elapsed.TotalMinutes);

            LastCompensation = Loc.F("Main.LastCompensation", Status.LastDeltaTheta, Status.LastPlaceError);

            WaitingOperator = Status.State == EMcState.WaitOperator;
        }

        public ICommand ToggleModeCommand { get; }
        public ICommand ArmCommand { get; }
        public ICommand StopCommand { get; }
        public ICommand ResetCommand { get; }
        public ICommand MuteCommand { get; }
        public ICommand OriginCommand { get; }
        public ICommand InitCommand { get; }
        public ICommand ResetCountersCommand { get; }
        public ICommand ResetInputTrayCommand { get; }
        public ICommand ResetAssyTrayCommand { get; }
    }
}
