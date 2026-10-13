using System;
using System.Collections.ObjectModel;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Input;
using System.Windows.Threading;
using ShieldCanAssy.Core.Communication;
using ShieldCanAssy.Core.Controller;
using ShieldCanAssy.Core.Data;
using ShieldCanAssy.Core.Data.Model;
using ShieldCanAssy.Core.Infor;
using ShieldCanAssy.Core.Lang;
using ShieldCanAssy.Wpf.Mvvm;
using ShieldCanAssy.Wpf.Services;

namespace ShieldCanAssy.Wpf.ViewModels
{
    /// <summary>Một ô trong lưới chọn ô tray lắp ráp của nút 6 (xác định hand-eye). Row/Col tính từ 0.</summary>
    public class HandEyeCellItem
    {
        public HandEyeCellItem(int row, int col) { Row = row; Col = col; }
        public int Row { get; }
        public int Col { get; }
        public string Label => "(" + (Row + 1) + "," + (Col + 1) + ")";
    }

    /// <summary>Một mục của Combobox chọn bước hiệu chuẩn. Chữ đổi TẠI CHỖ khi đổi ngôn ngữ (<see cref="Refresh"/>) —
    /// không thay ItemsSource, vì ComboBox đặt lại SelectedIndex khi ItemsSource đổi.</summary>
    public class CalibStepItem : ObservableObject
    {
        public CalibStepItem(string key) { Key = key; }
        public string Key { get; }
        public string Text => Loc.T(Key);
        public void Refresh() => OnPropertyChanged(nameof(Text));
        public override string ToString() => Text;
    }

    /// <summary>
    /// Hiệu chuẩn vision (§6) — DataContext cột phải trang Vision Edit, đặt cạnh công cụ sửa
    /// solution VisionMaster (cột trái) vì hai bên phải làm xen kẽ nhau.
    ///
    /// <para><b>Phép hiệu chuẩn nằm trong VisionMaster, không nằm ở đây</b> (Tuấn chốt
    /// 2026-09-12). Các nút bên dưới chỉ làm ba việc: nạp điểm (chạy trục + kích Flow), chạy trục cho
    /// phép kiểm tra, và đo hand-eye. Bước 2 App tự so vị trí dấu chuẩn; Bước 4, 5 đi qua Flow đo hằng số, Flow
    /// phán và ghi vào solution (bản rút gọn 2026-09-13). Bước nào đạt thì <c>Routine_Calib</c> ghi bản ghi hiệu
    /// chuẩn vào model và lưu model ngay, vì cổng Auto dựa vào bản ghi đó.</para>
    /// </summary>
    public class VisionCalibViewModel : ObservableObject
    {
        private readonly IDialogService _dialog = new DialogService();

        public VisionCalibViewModel()
        {
            // 9 command riêng thay vì 1 command tham số hoá (int) — CommandParameter gõ thẳng số
            // trong XAML (vd. CommandParameter="7") tới tay ICommand.Execute dưới dạng string, không
            // tự ép kiểu sang int (khác trường hợp CommandParameter="{Binding}" trỏ object thật) —
            // RelayCommand<T>.Execute lặng lẽ dùng default(T) khi ép kiểu thất bại, sai điểm mà
            // không có lỗi nào báo. Đóng luôn số điểm vào closure là tránh hẳn lớp lỗi đó.
            TopDownPoint1Command = new RelayCommand(async () => await RunTopDownGridPointAsync(1));
            TopDownPoint2Command = new RelayCommand(async () => await RunTopDownGridPointAsync(2));
            TopDownPoint3Command = new RelayCommand(async () => await RunTopDownGridPointAsync(3));
            TopDownPoint4Command = new RelayCommand(async () => await RunTopDownGridPointAsync(4));
            TopDownPoint5Command = new RelayCommand(async () => await RunTopDownGridPointAsync(5));
            TopDownPoint6Command = new RelayCommand(async () => await RunTopDownGridPointAsync(6));
            TopDownPoint7Command = new RelayCommand(async () => await RunTopDownGridPointAsync(7));
            TopDownPoint8Command = new RelayCommand(async () => await RunTopDownGridPointAsync(8));
            TopDownPoint9Command = new RelayCommand(async () => await RunTopDownGridPointAsync(9));
            SaveCalibrationCenterCommand = new RelayCommand(SaveCalibrationCenter);
            AutoCalibCommand = new RelayCommand(async () => await RunAutoCalibAsync());
            TriggerTopDownFlowCommand = new RelayCommand(async () => await TriggerTopDownFlowAsync());
            VerifyTopDownCommand = new RelayCommand(async () => await VerifyTopDownAsync());

            RunAlignCalibSequenceCommand = new RelayCommand(async () => await RunAlignCalibSequenceAsync());
            TriggerAlignCalibFlowCommand = new RelayCommand(async () => await TriggerAlignCalibFlowAsync());
            AlignAxisToZeroCommand = new RelayCommand(async () => await MoveAlignAxisToZeroAsync());
            VerifyAlignRotationCommand = new RelayCommand(async () => await VerifyAlignRotationAsync());

            HandEyeGoToPickCommand = new RelayCommand(async () => await HandEyeStepAsync(Loc.T("Vision.HeTitleGoToPick"),
                n => McControl.Calib.HandEyeGoToPickAsync(n, McControl.ManualToken)));
            HandEyePickCommand = new RelayCommand(async () => await HandEyePickWithConfirmAsync());
            HandEyeLocateCommand = new RelayCommand(async () => await HandEyeStepAsync(Loc.T("Vision.HeTitleLocate"),
                n => McControl.Calib.HandEyeLocateAsync(n, McControl.ManualToken)));
            HandEyeGoToAlignCommand = new RelayCommand(async () => await HandEyeStepAsync(Loc.T("Vision.GoToAlign4"),
                n => McControl.Calib.HandEyeGoToAlignAsync(n, McControl.ManualToken)));
            HandEyeCommitCommand = new RelayCommand(async () => await HandEyeCommitAsync(SelectedTool));
            HandEyeLocateHereCommand = new RelayCommand(async () =>
            {
                // Chốt ô lúc bấm — chọn ô khác trong lúc trục đang chạy không được đổi đích giữa chừng.
                var cell = HandEyeCell;
                await HandEyeStepAsync(HandEyeLocateLabel, n => McControl.Calib.HandEyeLocateHereAsync(n,
                    cell?.Row ?? -1, cell?.Col ?? -1, McControl.ManualToken));
            });
            OpenHandEyeCellPickerCommand = new RelayCommand(OpenHandEyeCellPicker);
            SelectHandEyeCellCommand = new RelayCommand<HandEyeCellItem>(c => { HandEyeCell = c; HandEyeCellPickerOpen = false; });
            ClearHandEyeCellCommand = new RelayCommand(() => { HandEyeCell = null; HandEyeCellPickerOpen = false; });
            HandEyeGoToAlignForTestCommand = new RelayCommand(async () => await HandEyeStepAsync(Loc.T("Vision.GoToAlign7"),
                n => McControl.Calib.HandEyeGoToAlignForTestAsync(n, McControl.ManualToken)));
            HandEyeCheckTargetCommand = new RelayCommand(async () => await HandEyeStepAsync(Loc.T("Vision.HeCheckTarget"),
                n => McControl.Calib.HandEyeCheckTargetAsync(n, McControl.ManualToken)));
            HandEyeGoToTargetCommand = new RelayCommand(async () => await HandEyeGoToTargetAsync());
            HandEyeReturnCommand = new RelayCommand(async () => await HandEyeReturnWithConfirmAsync());

            CheckCalibCommand = new RelayCommand(() => RefreshGate());
            DryRunPlaceAlignCommand = new RelayCommand(async () => await DryRunPlaceAlignAsync());

            StepMoveCamCommand = new RelayCommand(async () => await StepMoveCamAsync());
            StepLocateTopCommand = new RelayCommand(async () => await StepLocateTopAsync());
            StepPickCommand = new RelayCommand(async () => await StepPickAsync());
            StepMoveToAlignCommand = new RelayCommand(async () => await StepMoveToAlignAsync());
            MoveZSafeCommand = new RelayCommand<string>(async axis => await MoveZSafeAsync(axis));
            StepAlignBottomCommand = new RelayCommand(async () => await StepAlignBottomAsync());
            StepMoveToCommandCommand = new RelayCommand(async () => await StepMoveToCommandAsync());

            // Thanh Jog cạnh toolbar VisionMaster — đưa camera top-down tới dấu chuẩn ngay tại trang này,
            // khỏi sang Teaching/Manual (Tuấn yêu cầu 2026-09-13). Dùng chung AxisRowVm và McFunction.Jog với
            // hai trang kia: cùng chốt chặn CanMove, cùng mức Chậm/Nhanh.
            //
            // Thêm Z1/Z2 (Tuấn yêu cầu 2026-09-19): các bước hiệu chuẩn ở trang này có hạ Z xuống tray
            // (hút con can mẫu lúc xác định hand-eye), nên cần chỉnh tay và về gốc được trục Z ngay tại
            // chỗ đang làm. Thêm A1/A2 (Tuấn yêu cầu 2026-09-22) để xoay đầu hút ngay tại trang này.
            // Thứ tự X, Z1, Z2, Y2, A1, A2 cho lưới 3 cột × 2 hàng (Tuấn sắp lại 2026-09-26): cột 1 gantry X/Y2,
            // cột 2 tay #1 Z1/A1, cột 3 tay #2 Z2/A2.
            JogAxes = new ObservableCollection<AxisRowVm>(
                new[] { AxisId.X, AxisId.Z1, AxisId.Z2, AxisId.Y2, AxisId.A1, AxisId.A2 }
                    .Select(n => McControl.Motion.AxisList.FirstOrDefault(a => a.Name == n))
                    .Where(a => a != null)
                    .Select(a => new AxisRowVm(a)));
            SetJogSlowCommand = new RelayCommand(() => { McControl.Func.JogFast = false; RefreshJogFast(); });
            SetJogFastCommand = new RelayCommand(() => { McControl.Func.JogFast = true; RefreshJogFast(); });
            // STOP giống nút STOP trang Main, cộng huỷ chuỗi hiệu chuẩn/hand-eye/chạy kiểm đang chạy (Tuấn 2026-10-08) — mọi
            // chuỗi của trang này chạy trên McControl.ManualToken. Trước đó chỉ Motion.StopAll: trục dừng nhưng chuỗi phát tiếp bước sau.
            StopAllCommand = new RelayCommand(McControl.RequestStop);

            // Bật/tắt van hút ngay ở thanh Jog (Tuấn yêu cầu 2026-09-26) — cùng kiểu nút Hút #1/#2 trang Teaching:
            // một nút toggle mỗi van, màu theo DO thật đọc lại 150 ms một lần (không có PropertyChanged nào báo khi
            // DO đổi, kể cả khi routine hiệu chuẩn tự bật/tắt hút).
            Vacuum1ToggleCommand = new RelayCommand(() => McControl.Func.SetVacuum(1, !Vacuum1On));
            Vacuum2ToggleCommand = new RelayCommand(() => McControl.Func.SetVacuum(2, !Vacuum2On));
            _ioTimer = new DispatcherTimer(DispatcherPriority.Background) { Interval = TimeSpan.FromMilliseconds(150) };
            _ioTimer.Tick += (_, __) =>
            {
                var io = McControl.Io;
                if (io == null) return;
                Vacuum1On = io.GetDo(IoMap.DoVacuum1);
                Vacuum2On = io.GetDo(IoMap.DoVacuum2);
            };
            _ioTimer.Start();

            // Đổi ngôn ngữ: dựng lại mọi chữ tự ghép trong getter. Trang Vision Edit sống suốt đời App (một TabItem
            // của MainWindow) — đăng ký sự kiện tĩnh không rò rỉ. Câu báo kết quả đã dựng xong giữ ngôn ngữ lúc dựng.
            Loc.LanguageChanged += () =>
            {
                foreach (var s in CalibSteps) s.Refresh();
                RefreshCalibText();
                OnPropertyChanged(nameof(CalibrationCenterStatusText));
                OnPropertyChanged(nameof(HandEyeLocateLabel));
                OnPropertyChanged(nameof(HandEyeCellButtonText));
                OnPropertyChanged(nameof(VerifyResultText));
                OnPropertyChanged(nameof(VmStatusText));
                OnPropertyChanged(nameof(HandEyeStepText));
                OnPropertyChanged(nameof(StepResultText));
            };

            RefreshCalibText();
        }

        private VisionCalib Calib => McDataManager.Model.Calib;
        private static SystemData Sys => McDataManager.SysData;

        private readonly DispatcherTimer _ioTimer;

        private bool _vacuum1On;
        public bool Vacuum1On { get => _vacuum1On; private set => SetProperty(ref _vacuum1On, value); }

        private bool _vacuum2On;
        public bool Vacuum2On { get => _vacuum2On; private set => SetProperty(ref _vacuum2On, value); }

        public ICommand Vacuum1ToggleCommand { get; }
        public ICommand Vacuum2ToggleCommand { get; }

        private bool _busy;
        public bool Busy { get => _busy; private set => SetProperty(ref _busy, value); }

        // ───────────────────────── thanh Jog ─────────────────────────

        public ObservableCollection<AxisRowVm> JogAxes { get; }

        /// <summary>Chỉ để tô nút đang chọn — nguồn thật là <see cref="McFunction.JogFast"/>, chung mọi trang.</summary>
        public bool JogFast => McControl.Func.JogFast;

        /// <summary>Mức Chậm/Nhanh có thể vừa đổi ở trang Teaching — view gọi lại mỗi lần trang hiện lên.</summary>
        public void RefreshJogFast() => OnPropertyChanged(nameof(JogFast));

        // ───────────────────────── chọn bước (Combobox trên cùng) ─────────────────────────
        //
        // Trang này trước đây là MỘT cột cuộn dài xếp nối cả năm khối (Tuấn đổi 2026-09-25). Các bước hiệu chuẩn
        // phải làm ĐÚNG THỨ TỰ — top-down trước, rồi bottom-up từng tay, rồi vòng xoay, rồi hand-eye — mà một cột
        // cuộn dài không nói ra thứ tự đó, và khối của bước đang làm thì lẫn giữa các khối chưa tới lượt.
        //
        // Tên bước KHÔNG mang số thứ tự (luật Tuấn chốt 15/09: gọi bước bằng tên, không bằng số). Thứ tự đã nằm
        // trong thứ tự của danh sách, và nhãn trên Combobox nói "làm lần lượt từ trên xuống". Đánh số ở đây còn
        // đâm vào cách đánh số riêng của cẩm nang — cẩm nang tách "kiểm tra top-down" thành một bước, App gộp
        // nó vào bước hiệu chuẩn top-down, nên hai dãy số sẽ lệch nhau từ bước thứ hai trở đi.
        //
        // Tên bước ghi "(Tool đang chọn)" chứ KHÔNG ghép số Tool vào chuỗi: ghép thì <c>CalibSteps</c> phải dựng
        // lại mỗi lần đổi Tool, mà ComboBox đặt lại <c>SelectedIndex</c> khi ItemsSource đổi — đổi Tool giữa chừng
        // sẽ nhảy về bước 1. Ô chọn Tool nằm NGAY TRÊN Combobox nên vẫn thấy đang làm cho tay nào.

        /// <summary>Tên các bước, đúng thứ tự phải làm.</summary>
        public CalibStepItem[] CalibSteps { get; } =
        {
            new CalibStepItem("Vision.StepTopDown"),
            new CalibStepItem("Vision.StepBottomUp"),
            new CalibStepItem("Vision.StepRotation"),
            new CalibStepItem("Vision.StepHandEye"),
            new CalibStepItem("Vision.StepVerify"),
            new CalibStepItem("Vision.StepSequence"),
        };

        private int _stepIndex;

        /// <summary>Bước đang chọn, 0-based theo <see cref="CalibSteps"/>.</summary>
        public int StepIndex
        {
            get => _stepIndex;
            set
            {
                if (!SetProperty(ref _stepIndex, value)) return;
                OnPropertyChanged(nameof(IsStepTopDown));
                OnPropertyChanged(nameof(IsStepBottomUp));
                OnPropertyChanged(nameof(IsStepRotation));
                OnPropertyChanged(nameof(IsStepHandEye));
                OnPropertyChanged(nameof(IsStepVerify));
                OnPropertyChanged(nameof(IsStepSequence));
            }
        }

        // Một cờ cho mỗi bước, bind qua BooleanToVisibilityConverter — cùng nếp UseInputTray/IsAssyTray của trang
        // Teaching, không phải dựng converter so enum mới.
        public bool IsStepTopDown  => _stepIndex == 0;
        public bool IsStepBottomUp => _stepIndex == 1;
        public bool IsStepRotation => _stepIndex == 2;
        public bool IsStepHandEye  => _stepIndex == 3;
        public bool IsStepVerify   => _stepIndex == 4;
        public bool IsStepSequence => _stepIndex == 5;

        // ───────────────────────── chọn Tool (tay hút #1/#2) ─────────────────────────

        private int _selectedTool = 1;
        public int SelectedTool
        {
            get => _selectedTool;
            private set
            {
                if (SetProperty(ref _selectedTool, value))
                {
                    OnPropertyChanged(nameof(IsTool1));
                    OnPropertyChanged(nameof(IsTool2));
                    RefreshCalibText();
                }
            }
        }

        public bool IsTool1 { get => SelectedTool == 1; set { if (value) SelectedTool = 1; } }
        public bool IsTool2 { get => SelectedTool == 2; set { if (value) SelectedTool = 2; } }

        // ───────────────────────── trạng thái ─────────────────────────

        /// <summary>
        /// Trạng thái hiệu chuẩn Align của tay đang chọn. KHÔNG hiện "scale mm/px" hay "RMS" nữa —
        /// App không còn giữ ma trận nào để mà hiện; chất lượng phép fit đọc trong chính module
        /// hiệu chuẩn của VisionMaster ở cột trái.
        /// </summary>
        public string AlignStatusText
        {
            get
            {
                int n = SelectedTool;
                if (!Calib.IsAlignVerified(n)) return Loc.T("Vision.AlignNotVerified");

                string expired = Calib.IsExpired(Sys.CalibValidDays) ? Loc.T("Vision.Expired") + " — " : "";
                return Loc.F("Vision.AlignStatus",
                    expired,
                    Calib.AlignAngleSign(n) > 0 ? "+1" : "−1",
                    n == 1 ? Calib.AlignVerifyResidual1 : Calib.AlignVerifyResidual2,
                    Calib.AlignVerifiedPosX(n),
                    Calib.AlignVerifiedAt(n));
            }
        }

        public string HandEyeStatusText
        {
            get
            {
                int n = SelectedTool;
                if (Calib.IsHandEyeOutdated(n)) return Loc.T("Vision.HandEyeOutdated");
                if (!Calib.IsHandEyeMeasured(n)) return Loc.T("Vision.HandEyeNotMeasured");
                return Loc.F("Vision.HandEyeStatus",
                    Calib.HandEyeDx(n), Calib.HandEyeDy(n), Calib.HandEyeMeasuredAt(n), Calib.HandEyeAngleRef(n));
            }
        }

        /// <summary>Điều kiện hiệu chuẩn của Auto, gom về một dòng: lần kiểm tra hiệu chuẩn gần nhất (Bước 2/4/5,
        /// vị trí soi, sáu hằng số trong solution).</summary>
        public string ReadyStatusText
        {
            get
            {
                var s = McControl.Calib?.LastCalibGate;
                return s == null ? Loc.T("Vision.ReadyHint") : s.Describe();
            }
        }

        // null = chưa có kết quả nào: getter trả câu chờ theo ngôn ngữ đang chọn (đổi ngôn ngữ thì câu chờ đổi theo).
        private string _verifyResultText;
        /// <summary>Kết quả phép kiểm tra gần nhất — giữ trên màn hình thay vì chỉ hiện một hộp
        /// thoại rồi mất, vì đây là số operator cần đọc lại khi chỉnh solution bên cột trái.</summary>
        public string VerifyResultText { get => _verifyResultText ?? Loc.T("Vision.NotChecked"); private set => SetProperty(ref _verifyResultText, value); }

        private string _sequenceStatusText = "";
        public string SequenceStatusText { get => _sequenceStatusText; private set => SetProperty(ref _sequenceStatusText, value); }

        private void RefreshCalibText()
        {
            OnPropertyChanged(nameof(AlignStatusText));
            OnPropertyChanged(nameof(HandEyeStatusText));
            OnPropertyChanged(nameof(ReadyStatusText));
        }

        // ───────────────────────── tham số (pass-through vào SystemData) ─────────────────────────

        public double TopDownCalibStepXMm
        {
            get => Sys.TopDownCalibStepXMm;
            set { Sys.TopDownCalibStepXMm = value; OnPropertyChanged(); }
        }

        public double TopDownCalibStepYMm
        {
            get => Sys.TopDownCalibStepYMm;
            set { Sys.TopDownCalibStepYMm = value; OnPropertyChanged(); }
        }

        public double TopDownVerifyStepMm
        {
            get => Sys.TopDownVerifyStepMm;
            set { Sys.TopDownVerifyStepMm = value; OnPropertyChanged(); }
        }

        public string AlignCalibXOffsetsMm
        {
            get => Sys.AlignCalibXOffsetsMm;
            set { Sys.AlignCalibXOffsetsMm = value; OnPropertyChanged(); }
        }

        public string AlignCalibAnglesDeg
        {
            get => Sys.AlignCalibAnglesDeg;
            set { Sys.AlignCalibAnglesDeg = value; OnPropertyChanged(); }
        }

        public double AlignVerifyAngleDeg
        {
            get => Sys.AlignVerifyAngleDeg;
            set { Sys.AlignVerifyAngleDeg = value; OnPropertyChanged(); }
        }

        // ───────────────────────── lưới 9 điểm CAM top-down (numpad) ─────────────────────────

        /// <summary>"Calibration Center Point" — toạ độ (X, Y2) ứng với điểm 5 (giữa khung hình).</summary>
        public double CalibrationCenterX
        {
            get => Sys.TopDownCalibCenterX;
            set { Sys.TopDownCalibCenterX = value; OnPropertyChanged(); }
        }

        public double CalibrationCenterY
        {
            get => Sys.TopDownCalibCenterY;
            set { Sys.TopDownCalibCenterY = value; OnPropertyChanged(); }
        }

        /// <summary>Phân biệt "chưa từng Save" với "đã Save đúng tại (0,0)" — 2 giá trị double mặc
        /// định 0 không tự nói lên được điều đó.</summary>
        public bool CalibrationCenterSet
        {
            get => Sys.TopDownCalibCenterSet;
            set { Sys.TopDownCalibCenterSet = value; OnPropertyChanged(); OnPropertyChanged(nameof(CalibrationCenterStatusText)); }
        }

        public string CalibrationCenterStatusText => Loc.T(CalibrationCenterSet ? "Vision.CenterSaved" : "Vision.CenterNotSaved");

        /// <summary>
        /// Đọc vị trí trục (X, Y2) HIỆN TẠI rồi lưu làm Calibration Center Point — và ghi thẳng
        /// xuống <c>system.json</c>.
        ///
        /// <para>Ghi đĩa ngay tại đây chứ không chờ nút Lưu của trang Setting: tâm lưới và bước
        /// lưới nằm trong SystemData, mà trang này không có nút Lưu nào của riêng nó — trước đây
        /// tắt app là mất, và người dùng không có cách nào biết điều đó.</para>
        /// </summary>
        private void SaveCalibrationCenter()
        {
            if (!McControl.Status.OrgOk) { _dialog.Warn(Loc.T("Vision.NoOriginCoords")); return; }

            CalibrationCenterX = McControl.Motion[AxisId.X].CurrentPos;
            CalibrationCenterY = McControl.Motion[AxisId.Y2].CurrentPos;
            CalibrationCenterSet = true;

            bool saved = McDataManager.SaveSystemData();
            _dialog.Info(Loc.F(saved ? "Vision.CenterSavedOk" : "Vision.CenterSavedDiskFail",
                CalibrationCenterX, CalibrationCenterY));
        }

        /// <summary>Số numpad 1-9 → (hệ số hàng, hệ số cột) quanh tâm, khớp hình minh hoạ người
        /// dùng gửi (1,2,3 hàng dưới; 4,5,6 hàng giữa; 7,8,9 hàng trên).</summary>
        private static void TopDownGridOffset(int number, out int rowMul, out int colMul)
        {
            int idx = number - 1;
            rowMul = idx / 3 - 1;
            colMul = idx % 3 - 1;
        }

        private async Task<bool> MoveAndTriggerGridPointAsync(int number)
        {
            int rowMul, colMul;
            TopDownGridOffset(number, out rowMul, out colMul);
            double x = CalibrationCenterX + colMul * TopDownCalibStepXMm;
            double y = CalibrationCenterY + rowMul * TopDownCalibStepYMm;
            return await McControl.Calib.MoveAndTriggerTopDownCalibAsync(x, y, McControl.ManualToken).ConfigureAwait(true);
        }

        private bool CheckReadyForGrid()
        {
            if (!McControl.Status.OrgOk) { _dialog.Warn(Loc.T("Vision.NoOriginCalib")); return false; }
            if (!CalibrationCenterSet) { _dialog.Warn(Loc.T("Vision.NoCenter")); return false; }
            return true;
        }

        private async Task RunTopDownGridPointAsync(int number)
        {
            if (Busy || !CheckReadyForGrid()) return;

            Busy = true;
            try
            {
                bool ok = await MoveAndTriggerGridPointAsync(number).ConfigureAwait(true);
                if (!ok) _dialog.Warn(Loc.F("Vision.GridPointFail", number));
            }
            finally { Busy = false; }
        }

        /// <summary>
        /// Auto Calib — chạy hết 9 điểm theo thứ tự hình S của VisionMaster
        /// (<see cref="Routine_Calib.TopDownGridSOrder"/>), dừng ngay khi một điểm thất bại thay vì
        /// âm thầm bỏ qua rồi báo "xong" sai sự thật.
        /// </summary>
        private async Task RunAutoCalibAsync()
        {
            if (Busy || !CheckReadyForGrid()) return;

            // Mỗi lần chạy Flow LocateTop (hiệu chuẩn) là module N-point nạp thêm một điểm. Chạy đè lên danh
            // sách cũ thì ra một ma trận trộn hai lần hiệu chuẩn mà không có cảnh báo nào — nên App xoá trước.
            if (!_dialog.Confirm(Loc.T("Vision.AutoCalibConfirm")))
                return;

            Busy = true;
            try
            {
                SequenceStatusText = Loc.T("Vision.ClearingPoints");
                string clearErr = await McControl.Calib.ClearCalibPointsAsync(EVisionProcedure.TopDownCalib).ConfigureAwait(true);
                if (clearErr != null)
                {
                    SequenceStatusText = Loc.T("Vision.ClearFailStatus");
                    _dialog.Warn(Loc.F("Vision.ClearFail", clearErr));
                    return;
                }

                var order = Routine_Calib.TopDownGridSOrder;
                for (int i = 0; i < order.Length; i++)
                {
                    int number = order[i];
                    SequenceStatusText = Loc.F("Vision.AutoCalibPoint", i + 1, order.Length, number);

                    bool ok = await MoveAndTriggerGridPointAsync(number).ConfigureAwait(true);
                    if (!ok)
                    {
                        SequenceStatusText = Loc.F("Vision.AutoCalibStopStatus", i + 1, number);
                        _dialog.Warn(Loc.F("Vision.AutoCalibStop", i + 1, number));
                        return;
                    }
                }

                SequenceStatusText = Loc.T("Vision.AutoCalibLoaded");
                McControl.InvalidateVisionCache();
                _dialog.Info(Loc.T("Vision.AutoCalibDone"));
            }
            finally { Busy = false; }
        }

        /// <summary>Kích hoạt Flow LocateTop (hiệu chuẩn) tại vị trí hiện tại — chụp lại một điểm lẻ.</summary>
        private async Task TriggerTopDownFlowAsync()
        {
            if (Busy) return;

            Busy = true;
            try
            {
                bool ok = await McControl.Calib.TriggerTopDownCalibFlowAsync(McControl.ManualToken).ConfigureAwait(true);
                _dialog.Info(Loc.T(ok ? "Vision.TriggerTopOk" : "Vision.TriggerTopNotFound"));
            }
            finally { Busy = false; }
        }

        /// <summary>Kiểm tra hiệu chuẩn top-down bằng chuyển động trục thật (§6.2b).</summary>
        private async Task VerifyTopDownAsync()
        {
            if (Busy) return;
            if (!McControl.Status.OrgOk) { _dialog.Warn(Loc.T("Vision.NoOrigin")); return; }

            Busy = true;
            try
            {
                SequenceStatusText = Loc.T("Vision.VerifyingTopDown");
                var r = await McControl.Calib.VerifyTopDownAsync(TopDownVerifyStepMm, McControl.ManualToken).ConfigureAwait(true);
                McControl.InvalidateVisionCache();
                RefreshGate();

                SequenceStatusText = "";
                VerifyResultText = (r.Ok
                    ? Loc.F("Vision.TopDownPass",
                        r.StepMm, r.ErrorX, r.ErrorY, r.CrossX, r.CrossY)
                    : Loc.F("Vision.TopDownFail", r.Reason))
                    + (string.IsNullOrEmpty(r.Note) ? "" : "\n" + r.Note);

                if (r.Ok) _dialog.Info(VerifyResultText);
                else _dialog.Warn(VerifyResultText);
            }
            finally { Busy = false; }
        }

        // ───────────────────────── hiệu chuẩn Align trong VisionMaster (§6.4) ─────────────────────────

        private double AlignPosXOfSelectedTool => McDataManager.Model.AlignPosX(SelectedTool);

        /// <summary>Chạy hết chuỗi nạp điểm: các vị trí X rồi các góc, cuối cùng đưa trục A về 0°.</summary>
        private async Task RunAlignCalibSequenceAsync()
        {
            if (Busy) return;
            if (!McControl.Status.OrgOk) { _dialog.Warn(Loc.T("Vision.NoOriginCalib")); return; }

            double alignX = AlignPosXOfSelectedTool;
            if (Math.Abs(alignX) < 1e-9)
            {
                _dialog.Warn(Loc.F("Vision.NoAlignPosX", SelectedTool));
                return;
            }

            var xOffsets = Sys.AlignCalibXOffsets();
            var angles = Sys.AlignCalibAngles();
            if (angles.Count < 3) { _dialog.Warn(Loc.T("Vision.NeedThreeAngles")); return; }

            if (!_dialog.Confirm(Loc.F("Vision.AlignSeqConfirm", SelectedTool, xOffsets.Count, angles.Count)))
                return;

            Busy = true;
            try
            {
                var r = await McControl.Calib.RunAlignCalibSequenceAsync(
                    SelectedTool, alignX, xOffsets, angles,
                    msg => SequenceStatusText = msg,
                    McControl.ManualToken).ConfigureAwait(true);

                SequenceStatusText = r.Ok ? Loc.F("Vision.AlignSeqLoaded", r.PointsCaptured) : Loc.T("Vision.StoppedMidway");
                if (r.Ok) McControl.InvalidateVisionCache();

                if (r.Ok)
                    _dialog.Info(Loc.F("Vision.AlignSeqDone", r.PointsCaptured, SelectedTool));
                else
                    _dialog.Warn(Loc.F("Vision.AlignSeqStop", SelectedTool, r.Reason, r.PointsCaptured));
            }
            finally { Busy = false; }
        }

        private async Task TriggerAlignCalibFlowAsync()
        {
            if (Busy) return;

            Busy = true;
            try
            {
                bool ok = await McControl.Calib.TriggerAlignCalibFlowAsync(SelectedTool, McControl.ManualToken).ConfigureAwait(true);
                _dialog.Info(Loc.F(ok ? "Vision.TriggerAlignOk" : "Vision.TriggerAlignNotFound", SelectedTool));
            }
            finally { Busy = false; }
        }

        private async Task MoveAlignAxisToZeroAsync()
        {
            if (Busy) return;
            if (!McControl.Status.OrgOk) { _dialog.Warn(Loc.T("Vision.NoOrigin")); return; }

            Busy = true;
            try
            {
                bool ok = await McControl.Calib.MoveAlignAxisToZeroAsync(SelectedTool, McControl.ManualToken).ConfigureAwait(true);
                if (!ok) _dialog.Warn(Loc.F("Vision.AxisAZeroFail", SelectedTool));
            }
            finally { Busy = false; }
        }

        /// <summary>
        /// Phép thử vòng xoay (§6.4b): đo dấu góc và kiểm tâm xoay/chiều hệ toạ độ của file hiệu
        /// chuẩn bottom-up. App tự so hai lần soi; ĐẠT thì ghi dấu góc và vị trí soi vào model; KHÔNG ĐẠT thì
        /// không ghi gì.
        /// </summary>
        private async Task VerifyAlignRotationAsync()
        {
            if (Busy) return;
            if (!McControl.Status.OrgOk) { _dialog.Warn(Loc.T("Vision.NoOrigin")); return; }

            double alignX = AlignPosXOfSelectedTool;
            double currentX = McControl.Motion[AxisId.X].CurrentPos;
            if (Math.Abs(currentX - alignX) > 0.05)
            {
                // Vị trí chụp lúc thử phải trùng vị trí chụp lúc chạy Auto, nếu không thì dấu đo
                // được đúng nhưng tâm xoay lại đo ở một chỗ khác (PDF Hikrobot tr.45).
                if (!_dialog.Confirm(Loc.F("Vision.RotationMoveXConfirm", currentX, SelectedTool, alignX)))
                    return;

                Busy = true;
                try
                {
                    if (!await McControl.Func.MoveAsync(AxisId.X, alignX, Sys.GetAxis(AxisId.X).MoveSpeed,
                            McControl.ManualToken).ConfigureAwait(true))
                    {
                        _dialog.Warn(Loc.T("Vision.MoveXToAlignFail"));
                        return;
                    }
                }
                finally { Busy = false; }
            }

            Busy = true;
            try
            {
                SequenceStatusText = Loc.T("Vision.RotationRunning");
                var r = await McControl.Calib.VerifyAlignRotationAsync(SelectedTool, AlignVerifyAngleDeg, McControl.ManualToken).ConfigureAwait(true);
                SequenceStatusText = "";
                McControl.InvalidateVisionCache();
                RefreshGate();

                if (!r.Ok)
                {
                    VerifyResultText = Loc.F("Vision.RotationFail", SelectedTool, r.Reason) + (string.IsNullOrEmpty(r.Note) ? "" : "\n" + r.Note);
                    RefreshCalibText();
                    _dialog.Warn(Loc.F("Vision.NothingWritten", VerifyResultText));
                    return;
                }

                RefreshCalibText();

                VerifyResultText = Loc.F("Vision.RotationPass",
                    SelectedTool, r.TestAngleDeg, r.MeasuredDeltaThetaDeg, r.Sign > 0 ? "+1" : "−1",
                    r.ResidualMm, r.EccentricityMm);

                _dialog.Info(VerifyResultText +
                    (string.IsNullOrEmpty(r.Reason) ? "" : "\n\n" + Loc.F("Vision.Note", r.Reason)) +
                    (string.IsNullOrEmpty(r.Note) ? "" : "\n\n" + r.Note));
            }
            finally { Busy = false; }
        }

        // ───────────────────────── xác định hand-eye (§6.5) ─────────────────────────
        //
        // Bảy nút, mỗi nút một việc (Tuấn yêu cầu 2026-09-25); 26/09 nút tính điểm đích tách thành 7 · tới vị trí soi CAM
        // dưới và 8 · Trigger AlignBottom, thêm 9 · tới điểm đích vừa tính. Con can mẫu gắp trên CƠ CẤU KẸP giữ chi tiết đích
        // đứng yên, ở vị trí Jog-dạy riêng trên trang Teaching — không còn ô hàng/cột của tray lắp ráp nữa.
        //
        // TIẾN TRÌNH GIỮA CÁC NÚT KHÔNG Ở ĐÂY mà ở Routine_Calib: --selftest gọi thẳng lớp routine, nên mọi cổng
        // chặn ("chưa gắp", "sai tay", "X không ở vị trí soi") đều được kiểm; để ở ViewModel là để ở chỗ test
        // không tới được.
        //
        // Chặn LÚC CHẠY, không dùng CanExecute: cả trang này đều vậy (RelayCommand trơn, chỉ IsEnabled theo Busy ở
        // mức card). Thêm lý do kỹ thuật — CanExecuteChanged của RelayCommand nối vào CommandManager
        // .RequerySuggested, tức hỏi lại theo chuột/bàn phím chứ không theo trạng thái routine, nên nút sẽ trễ sau
        // một chuyển động dài. Và nút xám không nói gì, còn câu "tay hút #1 chưa gắp con can — bấm 2 trước" mới là
        // thứ người vận hành cần.

        private string _handEyeStepText;   // null = chưa chạy bước nào (câu chờ theo ngôn ngữ đang chọn)

        /// <summary>Số liệu của bước xác định hand-eye vừa chạy — ô RIÊNG, không dùng chung với khu chạy kiểm
        /// từng bước bên dưới.</summary>
        public string HandEyeStepText { get => _handEyeStepText ?? Loc.T("Vision.NotRunYet"); private set => SetProperty(ref _handEyeStepText, value); }

        // ── Chọn ô tray lắp ráp cho nút 6 (Tuấn thêm 2026-09-26) ──
        // Nút nhỏ cạnh nút 6 mở lưới ô của tray lắp ráp; chọn một ô thì nút 6 đưa CAM top-down tới ô đó rồi mới
        // kích LocateTop. Không chọn ô (hoặc bấm "Tại chỗ") thì nút 6 soi ngay chỗ gantry đang đứng như trước.

        public ObservableCollection<HandEyeCellItem> HandEyeCells { get; } = new ObservableCollection<HandEyeCellItem>();

        private int _handEyeCellRows = 1, _handEyeCellCols = 1;
        public int HandEyeCellRows { get => _handEyeCellRows; private set => SetProperty(ref _handEyeCellRows, value); }
        public int HandEyeCellCols { get => _handEyeCellCols; private set => SetProperty(ref _handEyeCellCols, value); }

        private HandEyeCellItem _handEyeCell;
        public HandEyeCellItem HandEyeCell
        {
            get => _handEyeCell;
            private set
            {
                if (!SetProperty(ref _handEyeCell, value)) return;
                OnPropertyChanged(nameof(HandEyeLocateLabel));
                OnPropertyChanged(nameof(HandEyeCellButtonText));
            }
        }

        private bool _handEyeCellPickerOpen;
        /// <summary>Hai chiều với Popup.IsOpen — StaysOpen=False tự đóng khi bấm ra ngoài.</summary>
        public bool HandEyeCellPickerOpen { get => _handEyeCellPickerOpen; set => SetProperty(ref _handEyeCellPickerOpen, value); }

        public string HandEyeLocateLabel => HandEyeCell == null
            ? Loc.T("Vision.HeLocateHere")
            : Loc.F("Vision.HeLocateCell", HandEyeCell.Label);

        public string HandEyeCellButtonText => HandEyeCell == null ? Loc.T("Vision.HeCellButton") : HandEyeCell.Label;

        /// <summary>Dựng lại lưới mỗi lần mở — đổi model thì kích thước tray đổi theo. Giữ ô đang chọn nếu còn nằm
        /// trong lưới mới.</summary>
        private void OpenHandEyeCellPicker()
        {
            var tray = McDataManager.Model?.AssyTray;
            if (tray == null) { _dialog.Warn(Loc.T("Vision.NoModel")); return; }
            int selRow = HandEyeCell?.Row ?? -1, selCol = HandEyeCell?.Col ?? -1;
            HandEyeCells.Clear();
            for (int r = 0; r < tray.Rows; r++)
                for (int c = 0; c < tray.Cols; c++)
                    HandEyeCells.Add(new HandEyeCellItem(r, c));
            HandEyeCellRows = Math.Max(1, tray.Rows);
            HandEyeCellCols = Math.Max(1, tray.Cols);
            HandEyeCell = HandEyeCells.FirstOrDefault(c => c.Row == selRow && c.Col == selCol);
            HandEyeCellPickerOpen = true;
        }

        /// <summary>Vỏ chung của sáu nút chạy được bằng <see cref="StepRunResult"/>.</summary>
        private Task<StepRunResult> HandEyeStepAsync(string title, Func<int, Task<StepRunResult>> body)
        {
            int nozzle = SelectedTool;
            return StepRunAsync(title, () => body(nozzle), line => HandEyeStepText = line);
        }

        /// <summary>
        /// Nút 2 — hạ Z xuống gắp nên hỏi lại (nút trả cũng vậy; nút 9 thì bấm đúp). Câu hỏi nói đúng hai điều
        /// kiện cơ khí: cơ cấu kẹp phải đang kẹp chi tiết đích, và cao độ hạ lấy của tray lắp ráp.
        /// </summary>
        private async Task HandEyePickWithConfirmAsync()
        {
            if (Busy) return;
            int nozzle = SelectedTool;
            if (!_dialog.Confirm(Loc.F("Vision.HePickConfirm", nozzle)))
                return;
            await HandEyeStepAsync(Loc.T("Vision.HePick"),
                n => McControl.Calib.HandEyePickAsync(n, McControl.ManualToken)).ConfigureAwait(true);
        }

        /// <summary>Nút 9 — hạ Z tới điểm giảm tốc (không chạm). Chặn bấm nhầm bằng BẤM ĐÚP ở nút, không hỏi lại
        /// bằng hộp thoại (Tuấn đổi 2026-09-26) — cùng cách các nút chạy trục khác của trang.</summary>
        private async Task HandEyeGoToTargetAsync()
        {
            await HandEyeStepAsync(Loc.T("Vision.HeGoToTarget"),
                n => McControl.Calib.HandEyeGoToTargetAsync(n, McControl.ManualToken)).ConfigureAwait(true);
        }

        private async Task HandEyeReturnWithConfirmAsync()
        {
            if (Busy) return;
            int nozzle = SelectedTool;
            if (!_dialog.Confirm(Loc.F("Vision.HeReturnConfirm", nozzle)))
                return;
            await HandEyeStepAsync(Loc.T("Vision.HeReturn"),
                n => McControl.Calib.HandEyeReturnAsync(n, McControl.ManualToken)).ConfigureAwait(true);
        }

        /// <summary>
        /// Nút 5 — ghi <c>HandEyeDx_mm = X_gắp + ObjX_mm − TopX_mm</c> và mốc góc <c>ObjA_deg + TopA_deg</c> vào model.
        /// Giữ khuôn riêng (không qua <see cref="HandEyeStepAsync"/>) vì trả <see cref="HandEyeCommitResult"/> và còn
        /// phải chạy lại cổng hiệu chuẩn.
        /// </summary>
        private async Task HandEyeCommitAsync(int nozzle)
        {
            if (Busy) return;
            if (!McControl.Status.OrgOk) { _dialog.Warn(Loc.T("Vision.NoOrigin")); return; }

            HandEyeCommitResult r;
            Busy = true;
            try
            {
                r = await McControl.Calib.HandEyeCommitAsync(nozzle, McControl.ManualToken).ConfigureAwait(true);
            }
            finally { Busy = false; }

            RefreshGate();
            RefreshCalibText();
            if (!r.Ok)
            {
                HandEyeStepText = Loc.F("Vision.StepStopped", Loc.T("Vision.HeCommit"), r.Reason);
                _dialog.Warn(Loc.F("Vision.HeCommitFail", nozzle, r.Reason) + (string.IsNullOrEmpty(r.Note) ? "" : "\n\n" + r.Note));
                return;
            }

            HandEyeStepText = Loc.F("Vision.HeCommitLine",
                nozzle, r.Dx, r.Dy, r.AngleRef);
            _dialog.Info(Loc.F("Vision.HeCommitDone", nozzle, r.Dx, r.Dy, r.AngleRef) +
                         (string.IsNullOrEmpty(r.Note) ? "" : "\n\n" + r.Note));
        }


        // ───────────────────────── cổng hiệu chuẩn ─────────────────────────

        private string _vmStatusText;   // null = chưa kiểm tra (câu chờ theo ngôn ngữ đang chọn)
        public string VmStatusText { get => _vmStatusText ?? Loc.T("Vision.NotCalibChecked"); private set => SetProperty(ref _vmStatusText, value); }

        /// <summary>Chạy lại cổng hiệu chuẩn (Bước 2/4/5, vị trí soi, ngưỡng) rồi hiện kết quả. Chỉ đọc model.</summary>
        private void RefreshGate()
        {
            VmStatusText = McControl.Calib.CheckCalib().Describe();
            RefreshCalibText();
        }

        /// <summary>Bước 5b: tay cầm sản phẩm mẫu; soi dấu chuẩn, AlignBottom_ToolN tính lệnh đặt lên dấu, tay tới lệnh ở Z an toàn.</summary>
        private async Task DryRunPlaceAlignAsync()
        {
            if (Busy) return;
            if (!McControl.Status.OrgOk) { _dialog.Warn(Loc.T("Vision.NoOrigin")); return; }
            if (!_dialog.Confirm(Loc.F("Vision.DryRunConfirm", SelectedTool)))
                return;

            Busy = true;
            try
            {
                var r = await McControl.Calib.DryRunPlaceAlignAsync(SelectedTool, McControl.ManualToken).ConfigureAwait(true);
                VmStatusText = McControl.Calib.LastCalibGate?.Describe() ?? _vmStatusText;
                RefreshCalibText();
                if (r.Ok)
                    _dialog.Info(Loc.F("Vision.DryRunDone",
                                       SelectedTool, r.CamX, r.CamY, r.CmdX, r.CmdY2) +
                                 (string.IsNullOrEmpty(r.CmdCheck) ? "" : "\n\n" + r.CmdCheck));
                else
                    _dialog.Warn(Loc.F("Vision.DryRunStop", SelectedTool, r.Reason));
            }
            finally { Busy = false; }
        }

        // ───────────────────────── chạy kiểm từng bước cả chu trình ─────────────────────────
        //
        // Sáu nút chạy đúng trình tự Auto nhưng dừng lại giữa từng bước để đọc số (Tuấn yêu cầu 2026-09-14).
        // Tay hút lấy theo Tool đang chọn ở khối trên — không có ô chọn tay riêng, hai chỗ chọn tay trên
        // cùng một trang là kiểu lỗi "bấm nút của tay này, xem số của tay kia".

        private int _stepAssyRow = 1, _stepAssyCol = 1, _stepInputRow = 1, _stepInputCol = 1;

        /// <summary>Ô tray lắp ráp sẽ soi ở bước 1-2, đánh số từ 1 như trang Main.</summary>
        public int StepAssyRow { get => _stepAssyRow; set => SetProperty(ref _stepAssyRow, value); }
        public int StepAssyCol { get => _stepAssyCol; set => SetProperty(ref _stepAssyCol, value); }

        /// <summary>Ô tray đầu vào sẽ hút ở bước 3.</summary>
        public int StepInputRow { get => _stepInputRow; set => SetProperty(ref _stepInputRow, value); }
        public int StepInputCol { get => _stepInputCol; set => SetProperty(ref _stepInputCol, value); }

        private string _stepResultText;   // null = chưa chạy bước nào (câu chờ theo ngôn ngữ đang chọn)
        /// <summary>Số liệu của bước vừa chạy — giữ trên màn hình để đối chiếu với số bên VisionMaster.</summary>
        public string StepResultText { get => _stepResultText ?? Loc.T("Vision.NotRunYet"); private set => SetProperty(ref _stepResultText, value); }

        /// <summary>
        /// Vỏ chung: chặn khi đang bận/chưa về gốc, chạy, hiện kết quả. Trả null nếu chưa chạy được.
        ///
        /// <para><paramref name="report"/> để khối xác định hand-eye ghi vào ô kết quả RIÊNG của nó
        /// (<see cref="HandEyeStepText"/>) — hai khối nút ghi chung một ô thì bấm khối này lại xoá số của khối kia.
        /// Bỏ trống thì ghi vào <see cref="StepResultText"/> như cũ.</para>
        /// </summary>
        private async Task<StepRunResult> StepRunAsync(string title, Func<Task<StepRunResult>> body,
                                                      Action<string> report = null)
        {
            if (Busy) return null;
            if (!McControl.Status.OrgOk) { _dialog.Warn(Loc.T("Vision.NoOrigin")); return null; }

            Busy = true;
            try
            {
                var r = await body().ConfigureAwait(true);
                string line = r.Ok ? r.Text : Loc.F("Vision.StepStopped", title, r.Reason);
                if (report != null) report(line); else StepResultText = line;
                if (!r.Ok) _dialog.Warn(Loc.F("Vision.StepStopped", title, r.Reason));
                return r;
            }
            finally { Busy = false; }
        }

        private async Task StepMoveCamAsync()
        {
            await StepRunAsync(Loc.T("Vision.SeqMoveCamTitle"), () => McControl.Calib.StepMoveCamToCellAsync(
                StepAssyRow - 1, StepAssyCol - 1, McControl.ManualToken));
        }

        private async Task StepLocateTopAsync()
        {
            await StepRunAsync("Trigger LocateTop", () => McControl.Calib.StepLocateTopAsync(
                StepAssyRow - 1, StepAssyCol - 1, McControl.ManualToken));
        }

        private async Task StepPickAsync()
        {
            if (Busy) return;
            if (!_dialog.Confirm(Loc.F("Vision.SeqPickConfirm", SelectedTool, StepInputRow, StepInputCol)))
                return;
            await StepRunAsync(Loc.T("Vision.SeqPickTitle"), () => McControl.Calib.StepPickAsync(
                SelectedTool, StepInputRow - 1, StepInputCol - 1, McControl.ManualToken));
        }

        /// <summary>Nút "Z1 → Z_SAFE" / "Z2 → Z_SAFE" ở thanh Jog — một lệnh Move, không phải chu trình về gốc.</summary>
        private async Task MoveZSafeAsync(string axis)
        {
            int nozzle = axis == AxisId.Z2 ? 2 : 1;
            await StepRunAsync(Loc.F("Vision.ZSafeTitle", axis),
                () => McControl.Calib.StepMoveZSafeAsync(nozzle, McControl.ManualToken));
        }

        private async Task StepMoveToAlignAsync()
        {
            await StepRunAsync(Loc.T("Vision.SeqMoveToAlignTitle"), () => McControl.Calib.StepMoveToAlignAsync(SelectedTool, McControl.ManualToken));
        }

        private async Task StepAlignBottomAsync()
        {
            var r = await StepRunAsync("Trigger AlignBottom", () => McControl.Calib.StepAlignBottomAsync(SelectedTool, McControl.ManualToken));
            if (r != null && r.Ok && !string.IsNullOrEmpty(r.GuardWarn))
                _dialog.Warn(Loc.F("Vision.GuardWarn", r.GuardWarn));
        }

        private async Task StepMoveToCommandAsync()
        {
            if (Busy) return;
            string warn = McControl.Calib.StepGuardWarn;
            if (!string.IsNullOrEmpty(warn) &&
                !_dialog.Confirm(Loc.F("Vision.GuardConfirm", warn)))
                return;
            await StepRunAsync(Loc.T("Vision.SeqMoveToCmdTitle"), () => McControl.Calib.StepMoveToCommandAsync(SelectedTool, McControl.ManualToken));
        }

        // ───────────────────────── commands ─────────────────────────

        public ICommand TopDownPoint1Command { get; }
        public ICommand TopDownPoint2Command { get; }
        public ICommand TopDownPoint3Command { get; }
        public ICommand TopDownPoint4Command { get; }
        public ICommand TopDownPoint5Command { get; }
        public ICommand TopDownPoint6Command { get; }
        public ICommand TopDownPoint7Command { get; }
        public ICommand TopDownPoint8Command { get; }
        public ICommand TopDownPoint9Command { get; }
        public ICommand SaveCalibrationCenterCommand { get; }
        public ICommand AutoCalibCommand { get; }
        public ICommand TriggerTopDownFlowCommand { get; }
        public ICommand VerifyTopDownCommand { get; }

        public ICommand RunAlignCalibSequenceCommand { get; }
        public ICommand TriggerAlignCalibFlowCommand { get; }
        public ICommand AlignAxisToZeroCommand { get; }
        public ICommand VerifyAlignRotationCommand { get; }

        public ICommand HandEyeGoToPickCommand { get; }
        public ICommand HandEyePickCommand { get; }
        public ICommand HandEyeLocateCommand { get; }
        public ICommand HandEyeGoToAlignCommand { get; }
        public ICommand HandEyeCommitCommand { get; }
        public ICommand HandEyeLocateHereCommand { get; }
        public ICommand OpenHandEyeCellPickerCommand { get; }
        public ICommand SelectHandEyeCellCommand { get; }
        public ICommand ClearHandEyeCellCommand { get; }
        public ICommand HandEyeGoToAlignForTestCommand { get; }
        public ICommand HandEyeCheckTargetCommand { get; }
        public ICommand HandEyeGoToTargetCommand { get; }
        public ICommand HandEyeReturnCommand { get; }

        public ICommand CheckCalibCommand { get; }
        public ICommand DryRunPlaceAlignCommand { get; }

        public ICommand StepMoveCamCommand { get; }
        public ICommand StepLocateTopCommand { get; }
        public ICommand StepPickCommand { get; }
        public ICommand StepMoveToAlignCommand { get; }
        public ICommand MoveZSafeCommand { get; }
        public ICommand StepAlignBottomCommand { get; }
        public ICommand StepMoveToCommandCommand { get; }

        public ICommand SetJogSlowCommand { get; }
        public ICommand SetJogFastCommand { get; }
        public ICommand StopAllCommand { get; }
    }
}
