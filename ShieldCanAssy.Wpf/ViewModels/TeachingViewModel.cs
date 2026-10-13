using System;
using System.Globalization;
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
using ShieldCanAssy.Utility.Log;
using ShieldCanAssy.Wpf.Mvvm;
using ShieldCanAssy.Wpf.Services;

namespace ShieldCanAssy.Wpf.ViewModels
{
    /// <summary>Một dòng trong bảng toạ độ tính ra — thuần dữ liệu, không giữ command riêng.</summary>
    public class TeachCellRow
    {
        public int Row { get; set; }
        public int Col { get; set; }
        public double X { get; set; }
        public double Y { get; set; }
        public double Dx { get; set; }
        public double Dy { get; set; }

        /// <summary>Bù đặt của ô (chỉ Assy Tray) — xem <see cref="CellOffset"/>.</summary>
        public double PlaceDx { get; set; }
        public double PlaceDy { get; set; }
        public double PlaceDa { get; set; }

        public string Cell => $"({Row + 1}, {Col + 1})";
        public bool HasOffset => Math.Abs(Dx) > 1e-9 || Math.Abs(Dy) > 1e-9;
        public bool HasPlaceTrim => Math.Abs(PlaceDx) > 1e-9 || Math.Abs(PlaceDy) > 1e-9 || Math.Abs(PlaceDa) > 1e-9;

        /// <summary>Thứ tự máy sẽ ghé ô này khi chạy Auto (1-based) — khác <see cref="Cell"/> (vị
        /// trí hình học cố định) khi ScanOrder khác RowMajor, xem <see cref="TeachingViewModel.VisitIndex"/>.</summary>
        public int VisitOrder { get; set; }
    }

    /// <summary>
    /// Trang Teaching — PLAN.md §8.2, §9.
    ///
    /// <para><b>Quy trình:</b> chọn tray → nhập Rows/Cols → jog X/Z/Y tới đúng ô bằng panel jog
    /// RÚT GỌN ngay trên trang này (chỉ Jog −/+ và vị trí, mượn <see cref="AxisRowVm"/> của trang
    /// Manual chứ không phải bản sao trạng thái riêng — không có Stop/Home/SVO/RST/Move, những
    /// cái đó vẫn chỉ ở trang Manual) → bấm Teach P00 → lặp lại cho P0C, PR0, (tuỳ chọn) điểm
    /// kiểm chứng → Tính lưới → bảng liệt kê toạ độ từng ô, có nút Move chạy thử và Set Offset
    /// tinh chỉnh ô lệch.</para>
    ///
    /// <para><b>P00/P0C/PR0/điểm kiểm chứng chỉ đọc, không cho gõ tay.</b> <see cref="TeachPoint"/>
    /// chỉ đặt <c>Taught = true</c> khi được tạo qua constructor có toạ độ; gõ thẳng vào ô X/Y
    /// (như bản Phase 0) đổi được số nhưng KHÔNG bật cờ đó, nên <c>TrayLayout.IsTaught</c> mãi
    /// mãi false và <c>Routine_Auto</c> chặn Auto vĩnh viễn dù nhìn màn hình toạ độ vẫn đúng —
    /// một lớp lỗi im lặng rất khó dò. Bắt teach đi qua nút là loại bỏ hẳn đường đó.</para>
    /// </summary>
    public class TeachingViewModel : ObservableObject
    {
        private readonly IDialogService _dialog = new DialogService();

        public TeachingViewModel()
        {
            // Panel jog RÚT GỌN — chỉ Jog −/+ và vị trí, không Stop/Home/SVO/RST/Move (những cái
            // đó ở lại trang Manual, xem ManualViewModel). Chỉ 2 nhóm trục thật sự cần khi teach
            // (X/Z cho toạ độ + Z_Pick, Y1/Y2 cho trục hàng) — KHÔNG kéo A1/A2 (xoay tay hút) sang
            // vì không dùng tới lúc teach tray. Dùng chung AxisRowVm/McControl.Motion với trang
            // Manual, không phải bản sao trạng thái riêng (quyết định của Tuấn, 2026-08-19).
            GantryX = new ObservableCollection<AxisRowVm>(
                McControl.Motion.AxisList.Where(a => a.Name == AxisId.X)
                                          .Select(a => new AxisRowVm(a)));
            GantryZ = new ObservableCollection<AxisRowVm>(
                McControl.Motion.AxisList.Where(a => a.Name == AxisId.Z1 || a.Name == AxisId.Z2)
                                          .Select(a => new AxisRowVm(a)));
            Tray = new ObservableCollection<AxisRowVm>(
                McControl.Motion.AxisList.Where(a => a.Name == AxisId.Y1 || a.Name == AxisId.Y2)
                                          .Select(a => new AxisRowVm(a)));

            // Hút/Thổi — 1 nút toggle mỗi van thay vì cặp ON/OFF riêng (Tuấn yêu cầu 2026-09-10,
            // cùng kiểu CAM1/CAM2 bên dưới): tự đọc Vacuum1On/Blow1On... (đã có sẵn qua _lampTimer)
            // để quyết định bật hay tắt. Bỏ luôn "Tắt thổi" (gộp tắt cả 2 van) — giờ tắt từng van
            // bằng cách bấm lại đúng nút Thổi #1/#2 đang bật.
            Vacuum1ToggleCommand = new RelayCommand(() => McControl.Func.SetVacuum(1, !Vacuum1On));
            Vacuum2ToggleCommand = new RelayCommand(() => McControl.Func.SetVacuum(2, !Vacuum2On));
            Blow1ToggleCommand = new RelayCommand(() => McControl.Func.SetBlow(1, !Blow1On));
            Blow2ToggleCommand = new RelayCommand(() => McControl.Func.SetBlow(2, !Blow2On));

            // Đèn trigger CAM1/CAM2 — cùng DO với nút "CAM Bottom-up/Top-down" ở trang Manual
            // (ToggleCamBtmLampCommand/ToggleCamTopLampCommand), thêm lối bấm nhanh ngay tại trang
            // Teaching vì hay cần bật đèn để canh nét/kiểm tra camera lúc đang teach (Tuấn yêu cầu,
            // 2026-09-10). Màu nút tô theo Cam1LampOn/Cam2LampOn (đọc lại DO thật qua _lampTimer
            // bên dưới) — cùng kiểu với SvoToggleButtonStyle của trang Manual, không phải cờ lạc
            // quan phía UI.
            ToggleCam1LampCommand = new RelayCommand(() =>
            {
                var io = McControl.Io;
                if (io == null) return;
                io.SetDo(IoMap.DoLampCam1, !io.GetDo(IoMap.DoLampCam1));
            });
            ToggleCam2LampCommand = new RelayCommand(() =>
            {
                var io = McControl.Io;
                if (io == null) return;
                io.SetDo(IoMap.DoLampCam2, !io.GetDo(IoMap.DoLampCam2));
            });

            SelectInputCommand = new RelayCommand(() => { UseInputTray = true; OnTraySwitched(); });
            SelectAssyCommand = new RelayCommand(() => { UseInputTray = false; OnTraySwitched(); });

            SelectNozzle1Command = new RelayCommand(() => { UseNozzle1 = true; OnToolSwitched(); });
            SelectNozzle2Command = new RelayCommand(() => { UseNozzle1 = false; OnToolSwitched(); });

            SetJogSlowCommand = new RelayCommand(() => { McControl.Func.JogFast = false; OnPropertyChanged(nameof(JogFast)); });
            SetJogFastCommand = new RelayCommand(() => { McControl.Func.JogFast = true; OnPropertyChanged(nameof(JogFast)); });

            ApplySizeCommand = new RelayCommand(ApplySize);

            TeachP00Command = new RelayCommand(() => TeachPoint(Loc.T("Teach.PointP1"), CurrentGeometry.P00, p => CurrentGeometry.P00 = p));
            TeachP0CCommand = new RelayCommand(() => TeachPoint(Loc.T("Teach.PointP2"), CurrentGeometry.P0C, p => CurrentGeometry.P0C = p));
            TeachPR0Command = new RelayCommand(() => TeachPoint(Loc.T("Teach.PointP3"), CurrentGeometry.PR0, p => CurrentGeometry.PR0 = p));
            TeachVerifyCommand = new RelayCommand(() => TeachPoint(Loc.T("Teach.PointVerify"), CurrentGeometry.PRC_Verify, p => CurrentGeometry.PRC_Verify = p));

            TeachZPickZ1Command = new RelayCommand(() => TeachZPick(AxisId.Z1));
            TeachZPickZ2Command = new RelayCommand(() => TeachZPick(AxisId.Z2));
            TeachPickAngleA1Command = new RelayCommand(() => TeachPickAngle(1));
            TeachPickAngleA2Command = new RelayCommand(() => TeachPickAngle(2));

            TeachZSlowZ1Command = new RelayCommand(() => TeachZSlow(AxisId.Z1));
            TeachZSlowZ2Command = new RelayCommand(() => TeachZSlow(AxisId.Z2));

            TeachAlignNozzle1Command = new RelayCommand(() => TeachAlignPos(1));
            TeachAlignNozzle2Command = new RelayCommand(() => TeachAlignPos(2));

            TeachCamTopDownAnchorCommand = new RelayCommand(TeachCamTopDownAnchor);
            MoveCamTopDownAnchorCommand = new RelayCommand(async () => await MoveToCellXYAsync(
                Model.AssyTray.AxisRow, Model.AssyTray.CamTopDownAnchorY, Model.AssyTray.CamTopDownAnchorX));

            MoveToCellCommand = new RelayCommand<TeachCellRow>(async row => await MoveToCellAsync(row));
            MoveCamToCellCommand = new RelayCommand<TeachCellRow>(async row => await MoveCamToCellAsync(row));
            MoveZAtCellCommand = new RelayCommand(async () => await MoveZAtCellAsync());
            // Về gốc Z cũng đi qua chặng chậm — nâng khỏi hốc tray bằng mức Auto THẤP tới điểm giảm tốc rồi
            // mới tăng tốc, gương của chiều xuống (Tuấn yêu cầu 2026-09-24). Điểm giảm tốc lấy của TRAY ĐANG
            // TEACH, vì đầu hút đang nằm trong hốc của chính tray đó.
            MoveZHomeCommand = new RelayCommand<string>(async axis =>
                await MoveZAsync(axis, McControl.Motion[axis].Config.HomeOffset, Layout.ZSlowFor(axis)));
            // Tới Pick/Tới Place chạy THỬ đúng đường mà chu trình Auto chạy — kể cả chặng giảm tốc,
            // để cái nhìn thấy lúc teach là cái máy làm lúc chạy thật.
            MoveZPickCommand = new RelayCommand<string>(async axis => await MoveZAsync(axis,
                McDataManager.Model.InputTray.ZPickFor(axis), McDataManager.Model.InputTray.ZSlowFor(axis)));
            MoveZPlaceCommand = new RelayCommand<string>(async axis => await MoveZAsync(axis,
                McDataManager.Model.AssyTray.ZPickFor(axis), McDataManager.Model.AssyTray.ZSlowFor(axis)));
            MoveXTrigger1Command = new RelayCommand(async () => await MoveToTriggerAsync(1));
            MoveXTrigger2Command = new RelayCommand(async () => await MoveToTriggerAsync(2));
            TeachHandEyePick1Command = new RelayCommand(() => TeachHandEyePick(1));
            TeachHandEyePick2Command = new RelayCommand(() => TeachHandEyePick(2));
            TeachHandEyeCamPosCommand = new RelayCommand(TeachHandEyeCamPos);
            MoveHandEyePick1Command = new RelayCommand(async () => await MoveToHandEyePickAsync(1));
            MoveHandEyePick2Command = new RelayCommand(async () => await MoveToHandEyePickAsync(2));
            MoveHandEyeCamPosCommand = new RelayCommand(async () => await MoveToHandEyeCamPosAsync());
            SetOffsetCommand = new RelayCommand<TeachCellRow>(SetOffset);
            ClearOffsetCommand = new RelayCommand<TeachCellRow>(ClearOffset);
            ApplyPlaceTrimCommand = new RelayCommand<TeachCellRow>(ApplyPlaceTrim);
            ClearPlaceTrimCommand = new RelayCommand<TeachCellRow>(ClearPlaceTrim);
            ApplyPlaceTrimAllCommand = new RelayCommand(ApplyPlaceTrimAll);
            ClearPlaceTrimAllCommand = new RelayCommand(ClearPlaceTrimAll);
            SelectCellCommand = new RelayCommand<TeachCellRow>(row => SelectedCell = row);

            NewModelCommand = new RelayCommand(NewModel);
            CopyModelCommand = new RelayCommand(CopyModel);
            DeleteModelCommand = new RelayCommand(DeleteModel);
            LoadSelectedModelCommand = new RelayCommand(LoadSelectedModel);
            SaveModelCommand = new RelayCommand(SaveModel);

            RefreshModelList();
            OnTraySwitched();

            // Đổi ngôn ngữ: đọc lại các chữ VM tự ghép. Trang Teaching sống suốt đời App — đăng ký sự kiện tĩnh không
            // rò rỉ. Không gọi Compute() — nó nạp lại ô nhập bù, mất số người dùng đang gõ dở.
            Loc.LanguageChanged += () =>
            {
                OnPropertyChanged(nameof(TrayTitle));
                OnPropertyChanged(nameof(ZPickSectionTitle));
                OnPropertyChanged(nameof(ZPickSectionHint));
                OnPropertyChanged(nameof(ZSlowSectionTitle));
                OnPropertyChanged(nameof(ZSlowSectionHint));
                OnPropertyChanged(nameof(ZSlowText));
                OnPropertyChanged(nameof(CamPosText));
                OnPropertyChanged(nameof(HandEyeJigText));
                OnPropertyChanged(nameof(SelectedCellTitle));
                UpdateVerifyText();
            };

            // Đọc lại DO đèn CAM1/CAM2 + van hút/thổi để tô màu nút — không có nguồn
            // PropertyChanged nào khác báo khi DO đổi (kể cả đổi bởi Routine_Auto lúc chạy Auto),
            // phải polling như ManualViewModel.Refresh() đang làm cho cả bảng DO. Vacuum/Blow thêm
            // 2026-09-10 (Tuấn phản hồi: nút Hút/Thổi ở trang Teaching chưa phản ánh IO thật, chỉ
            // 2 nút đèn CAM có màu theo trạng thái).
            _lampTimer = new DispatcherTimer(DispatcherPriority.Background) { Interval = TimeSpan.FromMilliseconds(150) };
            _lampTimer.Tick += (_, __) => RefreshLamps();
            _lampTimer.Start();
        }

        private readonly DispatcherTimer _lampTimer;

        private void RefreshLamps()
        {
            var io = McControl.Io;
            if (io == null) return;
            Cam1LampOn = io.GetDo(IoMap.DoLampCam1);
            Cam2LampOn = io.GetDo(IoMap.DoLampCam2);
            Vacuum1On = io.GetDo(IoMap.DoVacuum1);
            Vacuum2On = io.GetDo(IoMap.DoVacuum2);
            Blow1On = io.GetDo(IoMap.DoBlow1);
            Blow2On = io.GetDo(IoMap.DoBlow2);
        }

        private bool _cam1LampOn;
        public bool Cam1LampOn { get => _cam1LampOn; private set => SetProperty(ref _cam1LampOn, value); }

        private bool _cam2LampOn;
        public bool Cam2LampOn { get => _cam2LampOn; private set => SetProperty(ref _cam2LampOn, value); }

        private bool _vacuum1On;
        public bool Vacuum1On { get => _vacuum1On; private set => SetProperty(ref _vacuum1On, value); }

        private bool _vacuum2On;
        public bool Vacuum2On { get => _vacuum2On; private set => SetProperty(ref _vacuum2On, value); }

        private bool _blow1On;
        public bool Blow1On { get => _blow1On; private set => SetProperty(ref _blow1On, value); }

        private bool _blow2On;
        public bool Blow2On { get => _blow2On; private set => SetProperty(ref _blow2On, value); }

        // ───────────────────────── jog trục (rút gọn) + khí nén ─────────────────────────

        public ObservableCollection<AxisRowVm> GantryX { get; }
        public ObservableCollection<AxisRowVm> GantryZ { get; }
        public ObservableCollection<AxisRowVm> Tray { get; }

        /// <summary>Chỉ để hiển thị (DataTrigger tô sáng nút đang chọn) — nguồn thật là
        /// <see cref="McFunction.JogFast"/>, chung cho mọi trục và mọi trang (Manual lẫn Teaching
        /// jog qua cùng AxisRowVm.JogStart → McFunction.Jog).</summary>
        public bool JogFast => McControl.Func.JogFast;

        // ───────────────────────── chọn tray ─────────────────────────

        private bool _useInputTray = true;
        public bool UseInputTray
        {
            get => _useInputTray;
            set
            {
                if (SetProperty(ref _useInputTray, value))
                {
                    OnPropertyChanged(nameof(Layout));
                    OnPropertyChanged(nameof(IsAssyTray));
                    OnPropertyChanged(nameof(TrayTitle));
                    OnPropertyChanged(nameof(ZPickSectionTitle));
                    OnPropertyChanged(nameof(ZPickSectionHint));
                    OnPropertyChanged(nameof(ZSlowSectionTitle));
                    OnPropertyChanged(nameof(ZSlowSectionHint));
                    OnPropertyChanged(nameof(ZSlowText));
                }
            }
        }

        public TrayLayout Layout => UseInputTray ? McDataManager.Model.InputTray : McDataManager.Model.AssyTray;

        /// <summary>Chỉ Tray lắp ráp mới có bù đặt — hút ở Tray đầu vào không qua camera, lệch ở đó
        /// sửa bằng offset teach chứ không có đích nào để bù.</summary>
        public bool IsAssyTray => !UseInputTray;
        public string TrayTitle => Loc.T(UseInputTray ? "Teach.TrayInput" : "Teach.TrayAssy");

        /// <summary>
        /// Tray đầu vào = nơi tay hút SẢN PHẨM lên (cao độ HÚT); Tray lắp ráp = nơi tay ĐẶT
        /// sản phẩm xuống (cao độ ĐẶT). Cùng một cặp Z1/Z2 trên Layout nhưng ý nghĩa vật lý khác
        /// nhau tuỳ tray đang chọn — đổi nhãn theo UseInputTray để tránh nhầm "hút/đặt" chung chung.
        /// </summary>
        public string ZPickSectionTitle => Loc.T(UseInputTray ? "Teach.ZPickTitle" : "Teach.ZPlaceTitle");   // tiêu đề card viết hoa (2026-10-07)

        public string ZPickSectionHint => Loc.T(UseInputTray ? "Teach.ZPickHint" : "Teach.ZPlaceHint");

        public string ZSlowSectionTitle => Loc.T(UseInputTray ? "Teach.ZSlowPickTitle" : "Teach.ZSlowPlaceTitle");

        public string ZSlowSectionHint => Loc.F("Teach.ZSlowHint", PickPlaceWord);

        /// <summary>"hút" / "đặt" theo tray đang chọn — chèn vào các câu về cao độ hút/đặt.</summary>
        private string PickPlaceWord => Loc.T(UseInputTray ? "Teach.WordPick" : "Teach.WordPlace");

        /// <summary>
        /// Một dòng cho mỗi tay: đã teach chưa, và teach có đúng phía không. Không chặn thao tác —
        /// chỉ nói ra máy sẽ làm gì với con số đang có (cùng tinh thần với dòng kiểm chứng lưới).
        /// </summary>
        public string ZSlowText => ZSlowLine(AxisId.Z1) + "\n" + ZSlowLine(AxisId.Z2);

        private string ZSlowLine(string zAxis)
        {
            double slow = Layout.ZSlowFor(zAxis);
            double pick = Layout.ZPickFor(zAxis);
            string what = PickPlaceWord;

            if (slow <= 0)
                return Loc.F("Teach.ZSlowNotTaught", zAxis, what);
            if (slow >= pick)
                return Loc.F("Teach.ZSlowWrongSide", zAxis, slow, what, pick);
            return Loc.F("Teach.ZSlowOk", zAxis, pick - slow);
        }

        public ModelData Model => McDataManager.Model;

        private TrayState RunTray => UseInputTray ? McDataManager.RunData.InputTray : McDataManager.RunData.AssyTray;

        private void OnTraySwitched()
        {
            RowsInput = Layout.Rows;
            ColsInput = Layout.Cols;
            OnPropertyChanged(nameof(IsZigZagMode));
            OnPropertyChanged(nameof(IsRowMajorMode));
            Compute();
        }

        // ───────────────────────── chọn Tool (mỗi Tool có lưới toạ độ riêng) ─────────────────────────

        private bool _useNozzle1 = true;
        /// <summary>Tool đang teach lưới toạ độ — độc lập với <see cref="UseInputTray"/> (2 trục
        /// chọn: tray nào × Tool nào), xem <see cref="TrayGeometry"/> vì sao mỗi Tool cần lưới
        /// riêng thay vì suy ra bằng một hằng số pitch.</summary>
        public bool UseNozzle1
        {
            get => _useNozzle1;
            set { if (SetProperty(ref _useNozzle1, value)) OnPropertyChanged(nameof(CurrentGeometry)); }
        }

        public string CurrentZAxis => UseNozzle1 ? AxisId.Z1 : AxisId.Z2;
        public TrayGeometry CurrentGeometry => Layout.GeometryFor(CurrentZAxis);

        /// <summary>
        /// Chỗ đứng soi của CAM top-down chỉ hiện ra khi đang ở <b>Tray lắp ráp × Tool 2</b>
        /// (Tuấn yêu cầu 2026-09-18: "chưa có cách nào kiểm chứng vị trí di chuyển của Camera").
        ///
        /// <para>Tray đầu vào không quét top-down nên không có mốc CAM nào để suy ra. Còn Tool thì
        /// chỗ đứng soi tính theo lưới của <b>Tool 2</b> — <see cref="TrayLayout.CamGridZAxis"/> —
        /// nên để nút này hiện lúc đang chọn Tool 1 là mời người dùng hiểu nhầm rằng nó đi theo
        /// lưới đang xem.</para>
        /// </summary>
        public bool ShowCamMove => IsAssyTray && !UseNozzle1;

        /// <summary>
        /// Toạ độ gantry để CAM top-down soi đúng tâm ô đang chọn — chính con số
        /// <c>Routine_Auto.S05_ScanAssy</c> chạy tới lúc quét tray, đọc thẳng từ
        /// <see cref="TrayLayout.CamPosFor"/> chứ không tính lại ở đây.
        /// </summary>
        public string CamPosText
        {
            get
            {
                var row = SelectedCell;
                if (row == null) return Loc.T("Teach.NoCellSelected");
                if (!Layout.CamTopDownAnchorTaught)
                    return Loc.T("Teach.CamPosNoAnchor");

                var p = Layout.CamPosFor(row.Row, row.Col);
                return Loc.F("Teach.CamPosAt", row.Cell, p.X, Layout.AxisRow, p.Y);
            }
        }

        private void OnToolSwitched() => Compute();

        // ───────────────────────── thứ tự quét ─────────────────────────

        /// <summary>Chỉ có 2 chế độ chọn được qua UI (Teaching page) — ColMajor tồn tại trong
        /// EScanOrder nhưng TrayState.RowCol chưa cài (xem RunningData.cs), không phơi ra đây để
        /// tránh chọn nhầm một chế độ chưa thật sự hoạt động.</summary>
        public bool IsZigZagMode
        {
            get => Layout.ScanOrder == EScanOrder.Serpentine;
            set => SetScanOrder(value ? EScanOrder.Serpentine : EScanOrder.RowMajor);
        }

        public bool IsRowMajorMode
        {
            get => !IsZigZagMode;
            set { if (value) SetScanOrder(EScanOrder.RowMajor); }
        }

        private void SetScanOrder(EScanOrder order)
        {
            McControl.InvalidateVisionCache();   // số quét ô / tư thế trên tay đã cất không còn khớp lưới, mốc hay model
            if (Layout.ScanOrder == order) return;
            Layout.ScanOrder = order;
            // TrayState (RunningData, đọc bởi Routine_Auto lúc chạy thật) là bản sao riêng của
            // TrayLayout.ScanOrder (Model) — không tự đồng bộ, phải gán tay ở mọi nơi ScanOrder
            // có thể đổi (xem McDataManager.LoadModel/UseModel cho 2 chỗ còn lại).
            RunTray.ScanOrder = order;
            OnPropertyChanged(nameof(Layout));
            OnPropertyChanged(nameof(IsZigZagMode));
            OnPropertyChanged(nameof(IsRowMajorMode));
            Compute();
        }

        /// <summary>Thứ tự ghé ô (row,col) theo ScanOrder hiện tại — nghịch đảo của
        /// <c>TrayState.RowCol</c> (RunningData.cs): ở đó cho index ra (row,col), ở đây cho
        /// (row,col) ra index, để vẽ số thứ tự lên bảng/hình minh hoạ.</summary>
        public static int VisitIndex(int row, int col, int cols, EScanOrder order)
        {
            return TrayState.VisitIndex(row, col, cols, order);
        }

        // ───────────────────────── kích thước ─────────────────────────

        private int _rowsInput = 4;
        public int RowsInput { get => _rowsInput; set => SetProperty(ref _rowsInput, value); }

        private int _colsInput = 5;
        public int ColsInput { get => _colsInput; set => SetProperty(ref _colsInput, value); }

        /// <summary>
        /// Đổi kích thước lưới KHÔNG tự làm mỗi lần gõ số — bấm nút mới áp dụng, tránh vừa gõ
        /// "10" là resize hai lần (một lần ở số "1", một lần ở số "10").
        /// </summary>
        private void ApplySize()
        {
            McControl.InvalidateVisionCache();   // số quét ô / tư thế trên tay đã cất không còn khớp lưới, mốc hay model
            Layout.Resize(RowsInput, ColsInput);
            RunTray.Resize(Layout.Rows, Layout.Cols);
            RunTray.ScanOrder = Layout.ScanOrder;
            RowsInput = Layout.Rows;
            ColsInput = Layout.Cols;
            Compute();
        }

        // ───────────────────────── teach 3 điểm ─────────────────────────

        /// <summary>
        /// Trang Teaching KHÔNG chặn lúc chưa về gốc nữa — Tuấn chốt 2026-09-17: "bỏ qua việc
        /// kiểm tra về gốc, tôi sẽ chịu trách nhiệm kiểm tra xác nhận toạ độ có đúng hay không".
        /// Trước đó mọi nút teach đều từ chối khi <c>OrgOk</c> false vì chưa về gốc thì con số
        /// encoder chưa có hệ quy chiếu; nay việc xác nhận thuộc về người vận hành.
        ///
        /// <para>Đổi lại chỉ giữ một dòng log mức Cảnh báo, để sau này còn truy được điểm nào
        /// được ghi trong tình trạng đó. Không hỏi lại, không thêm ô cấu hình.</para>
        ///
        /// <para>Giới hạn: chỉ nới cho các nút GHI TOẠ ĐỘ ở trang Teaching. Các nút CHẠY (Move,
        /// Tới mốc CAM…) và toàn bộ trang Hiệu chuẩn Vision vẫn đòi đã về gốc — chúng phát lệnh
        /// chuyển động thật nên chưa có gốc là chạy sai chỗ.</para>
        /// </summary>
        private static void NoteTeachWithoutOrigin(string what)
        {
            if (McControl.Status.OrgOk) return;
            LogWorker.Instance.RecordLog(
                "Teaching: ghi " + what + " khi CHƯA về gốc — toạ độ do người vận hành tự xác nhận",
                LogLevel.Warning, "Teaching");
        }

        /// <summary>
        /// Hỏi lại trước khi ghi đè một điểm teach (Tuấn yêu cầu 2026-09-26, kẻo ấn nhầm): bấm nhầm Teach là số cũ
        /// mất ngay trên model đang mở, bấm Lưu sau đó là mất hẳn. Câu hỏi nêu điểm nào, số CŨ và số MỚI — nhìn là
        /// biết có đang đứng đúng chỗ không.
        ///
        /// <para>Số mới đọc từ encoder TRƯỚC khi hỏi, và chính số đó được ghi: hộp thoại là modal nên trục không
        /// nhận lệnh Jog nào trong lúc chờ, số hiện ra là số ghi vào.</para>
        /// </summary>
        private bool ConfirmTeach(string what, string oldValue, string newValue)
            => _dialog.Confirm(Loc.F("Teach.ConfirmWrite", what, oldValue, newValue));

        private static string XyText(double x, string rowAxis, double y)
            => string.Format(CultureInfo.InvariantCulture, "X {0:0.000}, {1} {2:0.000}", x, rowAxis, y);

        private static string XyText(TeachPoint p, string rowAxis)
            => p != null && p.Taught ? XyText(p.X, rowAxis, p.Y) : Loc.T("Teach.NotTaught");

        private static string MmText(double v) => v.ToString("0.000", CultureInfo.InvariantCulture) + " mm";

        private string TrayText => Loc.T(UseInputTray ? "Teach.TrayInputLower" : "Teach.TrayAssyLower");

        /// <summary>
        /// Đọc vị trí THỰC của trục X và trục hàng (Y1/Y2 tuỳ tray), hỏi lại, rồi giao cho
        /// <paramref name="assign"/> ghi vào đúng điểm teach. Không chặn khi chưa về gốc —
        /// xem <see cref="NoteTeachWithoutOrigin"/>.
        /// </summary>
        private void TeachPoint(string name, TeachPoint old, Action<TeachPoint> assign)
        {
            double x = McControl.Motion[AxisId.X].CurrentPos;
            double y = McControl.Motion[Layout.AxisRow].CurrentPos;
            // Lưới teach riêng theo tray × Tool — câu hỏi phải nói đang ghi vào bản nào.
            if (!ConfirmTeach(Loc.F("Teach.WhatGridPoint", name, TrayText, UseNozzle1 ? 1 : 2),
                              XyText(old, Layout.AxisRow), XyText(x, Layout.AxisRow, y))) return;
            McControl.InvalidateVisionCache();   // số quét ô / tư thế trên tay đã cất không còn khớp lưới, mốc hay model
            NoteTeachWithoutOrigin("điểm teach của lưới");
            assign(new TeachPoint(x, y));
            Compute();
        }

        private void TeachZPick(string zAxis)
        {
            double z = McControl.Motion[zAxis].CurrentPos;
            double old = zAxis == AxisId.Z1 ? Layout.Z_PickZ1 : Layout.Z_PickZ2;
            if (!ConfirmTeach(Loc.F(UseInputTray ? "Teach.WhatZPick" : "Teach.WhatZPlace", zAxis, TrayText),
                              MmText(old), MmText(z))) return;
            NoteTeachWithoutOrigin("cao độ hút " + zAxis);
            if (zAxis == AxisId.Z1) Layout.Z_PickZ1 = z; else Layout.Z_PickZ2 = z;
            OnPropertyChanged(nameof(Layout));
            OnPropertyChanged(nameof(ZSlowText));   // quãng chạy chậm đo từ cao độ hút/đặt
        }

        /// <summary>
        /// Ghi cao độ BẮT ĐẦU GIẢM TỐC của trục Z đang jog — cùng cách teach với cao độ hút/đặt.
        /// Không chặn khi số nằm sai phía: <see cref="ZSlowText"/> nói rõ máy sẽ bỏ qua, còn việc
        /// xác nhận toạ độ thuộc về người vận hành (xem <see cref="NoteTeachWithoutOrigin"/>).
        /// </summary>
        /// <summary>
        /// Ghi GÓC HÚT của tay <paramref name="nozzle"/> — toạ độ trục A đang jog, quấn về [0, 360).
        ///
        /// <para>Cách teach: đặt một sản phẩm vào hốc tray đầu vào, jog A cho tới khi gờ đầu hút ăn khớp
        /// đúng sản phẩm đó rồi bấm nút. Mọi lần hạ Z xuống hút — chu trình Auto, nút hút ở chạy từng bước,
        /// bước xác định hand-eye, chạy rà — đều xoay A về đây trước (Tuấn yêu cầu 2026-09-23).</para>
        /// </summary>
        private void TeachPickAngle(int nozzle)
        {
            string aAxis = nozzle == 1 ? AxisId.A1 : AxisId.A2;
            double a = AxisAngle.Wrap(McControl.Motion[aAxis].CurrentPos);
            double old = nozzle == 1 ? McDataManager.Model.PickAngleA1 : McDataManager.Model.PickAngleA2;
            if (!ConfirmTeach(Loc.F("Teach.WhatPickAngle", aAxis), old.ToString("0.00", CultureInfo.InvariantCulture) + "°",
                              a.ToString("0.00", CultureInfo.InvariantCulture) + "°")) return;
            NoteTeachWithoutOrigin("góc hút " + aAxis);
            if (nozzle == 1) McDataManager.Model.PickAngleA1 = a; else McDataManager.Model.PickAngleA2 = a;
            OnPropertyChanged(nameof(Model));
        }

        private void TeachZSlow(string zAxis)
        {
            double z = McControl.Motion[zAxis].CurrentPos;
            double old = zAxis == AxisId.Z1 ? Layout.Z_SlowZ1 : Layout.Z_SlowZ2;
            if (!ConfirmTeach(Loc.F("Teach.WhatZSlow", zAxis, TrayText), MmText(old), MmText(z))) return;
            NoteTeachWithoutOrigin("điểm giảm tốc " + zAxis);
            if (zAxis == AxisId.Z1) Layout.Z_SlowZ1 = z; else Layout.Z_SlowZ2 = z;
            OnPropertyChanged(nameof(Layout));
            OnPropertyChanged(nameof(ZSlowText));
        }

        // ───────────────────────── vị trí soi CAM1 (bottom-up) mỗi tay hút ─────────────────────────

        /// <summary>
        /// Toạ độ X của gantry để đưa tay hút <paramref name="nozzle"/> vào giữa khung hình CAM
        /// bottom-up trước khi trigger AlignBottom (S20_Align1/S21_Align2, xem Routine_Auto). CAM
        /// cố định, mỗi tay có thể lệch cơ khí khác thiết kế nên phải teach RIÊNG từng tay —
        /// không suy ra được từ tay còn lại.
        /// </summary>
        private void TeachAlignPos(int nozzle)
        {
            double x = McControl.Motion[AxisId.X].CurrentPos;
            double old = nozzle == 1 ? Model.AlignPosX_Nozzle1 : Model.AlignPosX_Nozzle2;
            if (!ConfirmTeach(Loc.F("Teach.WhatAlignPos", nozzle), "X " + MmText(old), "X " + MmText(x))) return;
            McControl.InvalidateVisionCache();   // số quét ô / tư thế trên tay đã cất không còn khớp lưới, mốc hay model
            NoteTeachWithoutOrigin("vị trí soi CAM bottom-up của tay #" + nozzle);
            if (nozzle == 1) Model.AlignPosX_Nozzle1 = x; else Model.AlignPosX_Nozzle2 = x;
            OnPropertyChanged(nameof(Model));
        }

        // ───────────────────────── cơ cấu kẹp (xác định hand-eye) ─────────────────────────

        /// <summary>
        /// Vị trí gantry (X, Y2) để đầu hút của tay <paramref name="nozzle"/> nằm trên con can mẫu trên cơ cấu kẹp
        /// dùng cho bước xác định hand-eye (Tuấn chốt 2026-09-25) — xem
        /// <see cref="ModelData.HandEyePick_Nozzle1"/> để biết vì sao không dùng ô tray và vì sao teach riêng
        /// từng tay.
        ///
        /// <para><b>Không gọi <see cref="McControl.InvalidateVisionCache"/></b> — theo đúng luật của nó ("chỉ những
        /// thứ mà toạ độ do vision đo phụ thuộc vào"): ba điểm này chỉ dùng trong bước xác định hand-eye, không nuôi
        /// số quét ô hay tư thế trên tay nào đang được cất. <see cref="TeachPickAngle"/> cũng không gọi.</para>
        /// </summary>
        private void TeachHandEyePick(int nozzle)
        {
            string what = Loc.F("Teach.WhatJigPick", nozzle);
            var point = ReadJigPoint();
            string rowAxis = McDataManager.Model.AssyTray.AxisRow;
            if (!ConfirmTeach(what, XyText(Model.HandEyePick(nozzle), rowAxis), XyText(point, rowAxis))) return;
            NoteTeachWithoutOrigin(what);
            if (nozzle == 1) Model.HandEyePick_Nozzle1 = point; else Model.HandEyePick_Nozzle2 = point;
            AfterTeachHandEyeJig();
        }

        /// <summary>Vị trí gantry (X, Y2) để CAM top-down ngắm dấu định vị của chi tiết đích trên cơ cấu kẹp.</summary>
        private void TeachHandEyeCamPos()
        {
            string what = Loc.T("Teach.WhatJigCam");
            var point = ReadJigPoint();
            string rowAxis = McDataManager.Model.AssyTray.AxisRow;
            if (!ConfirmTeach(what, XyText(Model.HandEyeCamPos, rowAxis), XyText(point, rowAxis))) return;
            NoteTeachWithoutOrigin(what);
            Model.HandEyeCamPos = point;
            AfterTeachHandEyeJig();
        }

        /// <summary>
        /// Dựng điểm teach từ encoder. <c>new TeachPoint(x, y)</c> là đường DUY NHẤT bật cờ <c>Taught</c>
        /// (xem <see cref="TeachPoint"/>), nên phải THAY cả đối tượng chứ không sửa X/Y của đối tượng cũ.
        ///
        /// <para>Trục hàng lấy của tray LẮP RÁP: cơ cấu kẹp gá trên bàn đó, và Flow top-down chỉ quy được toạ độ
        /// máy theo Y2 (<c>VmCycle.AxisGlobals</c> ghi cứng Y2).</para>
        /// </summary>
        private static TeachPoint ReadJigPoint()
        {
            string rowAxis = McDataManager.Model.AssyTray.AxisRow;
            return new TeachPoint(McControl.Motion[AxisId.X].CurrentPos, McControl.Motion[rowAxis].CurrentPos);
        }

        private void AfterTeachHandEyeJig()
        {
            OnPropertyChanged(nameof(Model));
            OnPropertyChanged(nameof(HandEyeJigText));
        }

        /// <summary>
        /// Một dòng: còn thiếu điểm nào, và hai điều kiện CƠ KHÍ mà bước xác định hand-eye dựa vào. Báo chứ không
        /// chặn — cùng tinh thần với <see cref="ZSlowText"/>.
        /// </summary>
        public string HandEyeJigText
        {
            get
            {
                var m = McDataManager.Model;
                var missing = new System.Collections.Generic.List<string>();
                if (!m.HandEyePick_Nozzle1.Taught) missing.Add(Loc.F("Teach.JigMissingPick", 1));
                if (!m.HandEyePick_Nozzle2.Taught) missing.Add(Loc.F("Teach.JigMissingPick", 2));
                if (!m.HandEyeCamPos.Taught) missing.Add(Loc.T("Teach.JigMissingCam"));

                string head = missing.Count > 0
                    ? Loc.F("Teach.JigNotTaught", string.Join(", ", missing))
                    : Loc.T("Teach.JigAllTaught");
                return head + "\n" + string.Format(CultureInfo.InvariantCulture, Loc.T("Teach.JigNote"),
                    m.AssyTray.Z_PickZ1, m.AssyTray.Z_PickZ2);
            }
        }

        private async Task MoveToHandEyePickAsync(int nozzle)
        {
            var point = McDataManager.Model.HandEyePick(nozzle);
            if (!point.Taught)
            {
                _dialog.Warn(Loc.F("Teach.JigPickNotTaughtWarn", nozzle));
                return;
            }
            await MoveToCellXYAsync(McDataManager.Model.AssyTray.AxisRow, point.Y, point.X,
                Loc.F("Teach.WhatJigPick", nozzle)).ConfigureAwait(true);
        }

        private async Task MoveToHandEyeCamPosAsync()
        {
            var point = McDataManager.Model.HandEyeCamPos;
            if (!point.Taught)
            {
                _dialog.Warn(Loc.T("Teach.JigCamNotTaughtWarn"));
                return;
            }
            await MoveToCellXYAsync(McDataManager.Model.AssyTray.AxisRow, point.Y, point.X,
                Loc.T("Teach.WhatJigCam")).ConfigureAwait(true);
        }

        // ───────────────────────── mốc CAM top-down (Assy Tray) ─────────────────────────

        /// <summary>
        /// Teach vị trí gantry khi CAM top-down nằm đúng tâm Ô (1,1) của Assy Tray — jog thủ
        /// công dựa vào ảnh camera trực tiếp (không đo vision tự động), dùng làm mốc tính vị trí
        /// trigger cho các ô còn lại (Routine_Auto.S05_ScanAssy). Chỉ dùng CHUNG một mốc cho cả 2
        /// Tool vì camera gắn cố định trên gantry, không theo từng đầu hút (Tuấn xác nhận
        /// 2026-09-05).
        /// </summary>
        private void TeachCamTopDownAnchor()
        {
            var assy = Model.AssyTray;
            double x = McControl.Motion[AxisId.X].CurrentPos;
            double y = McControl.Motion[assy.AxisRow].CurrentPos;
            if (!ConfirmTeach(Loc.T("Teach.WhatCamAnchor"),
                              assy.CamTopDownAnchorTaught ? XyText(assy.CamTopDownAnchorX, assy.AxisRow, assy.CamTopDownAnchorY) : Loc.T("Teach.NotTaught"),
                              XyText(x, assy.AxisRow, y))) return;
            McControl.InvalidateVisionCache();   // số quét ô / tư thế trên tay đã cất không còn khớp lưới, mốc hay model
            NoteTeachWithoutOrigin("mốc CAM top-down");
            assy.CamTopDownAnchorX = x;
            assy.CamTopDownAnchorY = y;
            assy.CamTopDownAnchorTaught = true;
            OnPropertyChanged(nameof(Model));
        }

        // ───────────────────────── bảng ô, Move, Offset ─────────────────────────

        public ObservableCollection<TeachCellRow> Cells { get; } = new ObservableCollection<TeachCellRow>();

        private TeachCellRow _selectedCell;
        /// <summary>Ô đang chọn trên lưới nút bên phải — Move/Set Offset/Xoá đều thao tác trên
        /// ô này thay vì tham số riêng từng nút như bảng DataGrid cũ.</summary>
        public TeachCellRow SelectedCell
        {
            get => _selectedCell;
            set
            {
                if (!SetProperty(ref _selectedCell, value)) return;
                LoadTrimBoxes();
                OnPropertyChanged(nameof(CamPosText));
                OnPropertyChanged(nameof(SelectedCellTitle));
            }
        }

        /// <summary>Dòng đầu khu nút Move: ô đang chọn, hoặc lời nhắc chọn ô (trước đây là TargetNullValue trong XAML —
        /// chỗ đó không gắn được chữ theo ngôn ngữ).</summary>
        public string SelectedCellTitle => SelectedCell?.Cell ?? Loc.T("Teach.NoCellHint");

        private bool _busy;
        public bool Busy { get => _busy; private set => SetProperty(ref _busy, value); }

        private string _verifyText;
        public string VerifyText { get => _verifyText; private set => SetProperty(ref _verifyText, value); }

        private void Compute()
        {
            // TrayLayout/TeachPoint không phải Bindable — báo lại đúng token đầu của mọi
            // binding "Layout.…"/"CurrentGeometry.…" để WPF đọc lại cả nhánh, thay vì rải
            // OnPropertyChanged ở từng nơi gọi Compute().
            OnPropertyChanged(nameof(Layout));
            OnPropertyChanged(nameof(CurrentGeometry));

            var layout = Layout;
            string zAxis = CurrentZAxis;
            // Cells bị dựng lại (instance mới) mỗi lần Compute — giữ SelectedCell qua (Row,Col)
            // thay vì instance cũ, không thì lưới nút vẫn hiện chọn một ô đã không còn tồn tại.
            int selRow = SelectedCell?.Row ?? -1, selCol = SelectedCell?.Col ?? -1;
            Cells.Clear();

            for (int r = 0; r < layout.Rows; r++)
            {
                for (int c = 0; c < layout.Cols; c++)
                {
                    var p = layout.CellPos(zAxis, r, c);
                    var off = layout.GetOffset(zAxis, r, c);
                    Cells.Add(new TeachCellRow
                    {
                        Row = r, Col = c, X = p.X, Y = p.Y,
                        Dx = off?.Dx ?? 0, Dy = off?.Dy ?? 0,
                        PlaceDx = off?.PlaceDx ?? 0, PlaceDy = off?.PlaceDy ?? 0, PlaceDa = off?.PlaceDa ?? 0,
                        VisitOrder = VisitIndex(r, c, layout.Cols, layout.ScanOrder) + 1
                    });
                }
            }

            SelectedCell = selRow >= 0 ? Cells.FirstOrDefault(c => c.Row == selRow && c.Col == selCol) : null;
            LoadTrimAllBoxes();

            // Compute() chạy sau MỌI lần đổi tray/đổi Tool/teach lại lưới — báo luôn ở đây để
            // hai thứ theo lưới CAM khỏi phải rải OnPropertyChanged ở từng nơi gọi.
            OnPropertyChanged(nameof(ShowCamMove));
            OnPropertyChanged(nameof(CamPosText));
            OnPropertyChanged(nameof(ZCellMoveLabel));

            UpdateVerifyText();
        }

        /// <summary>Dòng kiểm chứng lưới — tách khỏi <see cref="Compute"/> để đổi ngôn ngữ dựng lại được mà không nạp lại
        /// ô nhập bù.</summary>
        private void UpdateVerifyText()
        {
            var layout = Layout;
            string zAxis = CurrentZAxis;

            if (!layout.IsTaughtFor(zAxis))
            {
                VerifyText = Loc.T("Teach.VerifyNotTaught");
                return;
            }

            double err;
            bool ok = layout.VerifyGrid(zAxis, out err);
            var geo = CurrentGeometry;
            VerifyText = geo.PRC_Verify.Taught
                ? (ok
                    ? Loc.F("Teach.VerifyOk", err, layout.VerifyThreshold)
                    : Loc.F("Teach.VerifyFail", err, layout.VerifyThreshold))
                : Loc.T("Teach.VerifyNoPoint");
        }

        /// <summary>
        /// Chạy thử tới một ô đã tính — đúng tiêu chí nghiệm thu P3 (PLAN.md §10). Xoay luôn trục A của Tool đang
        /// chọn về GÓC HÚT (Tuấn yêu cầu 2026-09-26), cùng lệnh Auto dùng trước mỗi lần hạ Z — nút Z Pick/Place bên
        /// cạnh hạ Z ngay tại đây, và gờ đầu hút phải ăn khớp sản phẩm (xem McFunction.MoveToCellForPickAsync).
        /// </summary>
        private async Task MoveToCellAsync(TeachCellRow row)
        {
            if (row == null) return;
            var p = Layout.CellPos(CurrentZAxis, row.Row, row.Col);
            await MoveToCellXYAsync(Layout.AxisRow, p.Y, p.X, Loc.F("Teach.CellLabel", row.Cell), UseNozzle1 ? 1 : 2);
        }

        /// <summary>Nhãn nút hạ Z ở khu nút Move — trục theo Tool đang chọn, Pick/Place theo tray đang teach.</summary>
        public string ZCellMoveLabel => CurrentZAxis + (UseInputTray ? " Pick" : " Place");

        /// <summary>
        /// Nút Z Pick/Place ở khu nút Move (Tuấn yêu cầu 2026-09-26): Z của Tool đang chọn hạ tới cao độ hút (tray
        /// đầu vào) hoặc đặt (tray lắp ráp) đã teach, qua điểm giảm tốc — cùng đường với nút Tới Pick/Tới Place ở
        /// thanh Jog Z. Không đòi gantry đang đứng đúng ô: teach là Move, hạ, nâng, jog chỉnh, hạ lại.
        /// </summary>
        private async Task MoveZAtCellAsync()
        {
            string z = CurrentZAxis;
            await MoveZAsync(z, Layout.ZPickFor(z), Layout.ZSlowFor(z));
        }

        /// <summary>
        /// Chạy gantry tới chỗ đứng để CAM top-down soi đúng tâm ô đang chọn — cùng toạ độ mà
        /// <c>Routine_Auto.S05_ScanAssy</c> chạy tới lúc quét tray, để kiểm chứng bằng mắt qua ảnh
        /// camera trực tiếp rằng mốc CAM và lưới Tool 2 đã teach đúng.
        ///
        /// <para>Chặn khi chưa teach mốc CAM: lúc đó <see cref="TrayLayout.CamPosFor"/> tính từ mốc
        /// (0, 0) và sẽ lôi gantry về đầu hành trình — không phải một "chỗ đứng soi" nào cả.</para>
        /// </summary>
        private async Task MoveCamToCellAsync(TeachCellRow row)
        {
            if (row == null) return;
            if (!Layout.CamTopDownAnchorTaught)
            {
                _dialog.Warn(Loc.T("Teach.CamAnchorNotTaughtWarn"));
                return;
            }

            var p = Layout.CamPosFor(row.Row, row.Col);
            await MoveToCellXYAsync(Layout.AxisRow, p.Y, p.X, Loc.F("Teach.CamPosLabel", row.Cell));
        }

        /// <summary>
        /// Chạy thử tới một vị trí (X, trục hàng) bất kỳ — dùng chung cho lưới ô
        /// (<see cref="MoveToCellAsync(TeachCellRow)"/>) lẫn mốc CAM top-down
        /// (<see cref="MoveCamTopDownAnchorCommand"/>), cùng một thao tác "tới toạ độ trong mặt
        /// phẳng tray" chỉ khác nguồn toạ độ.
        /// </summary>
        /// <param name="pickNozzle">1/2 = xoay luôn trục A của tay đó về góc hút; 0 = không đụng trục A.</param>
        /// <param name="label">Tên chỗ tới trong câu báo lỗi; null = "vị trí".</param>
        private async Task MoveToCellXYAsync(string rowAxis, double y, double x, string label = null, int pickNozzle = 0)
        {
            if (Busy) return;

            Busy = true;
            try
            {
                var ct = McControl.Safety.AbortToken;
                bool ok = pickNozzle > 0
                    ? await McControl.Func.MoveToCellForPickAsync(pickNozzle, McDataManager.Model.PickAngle(pickNozzle),
                                                                  rowAxis, y, x, ct).ConfigureAwait(true)
                    : await McControl.Func.MoveToCellAsync(rowAxis, y, x, ct).ConfigureAwait(true);

                if (!ok) _dialog.Warn(Loc.F("Teach.MoveFailed", label ?? Loc.T("Teach.Position"), McControl.Status.Message));
            }
            finally { Busy = false; }
        }

        /// <summary>
        /// Chạy thử một trục Z tới một cao độ tham chiếu (Home/Pick/Place) — cùng cơ chế với
        /// <see cref="MoveToCellAsync"/>, chỉ khác là 1 trục thay vì cặp X/hàng. Đi qua
        /// McFunction.MoveAsync nên vẫn qua đủ CanMove (servo, phanh, alarm...) như mọi lệnh chạy
        /// khác — không phải đường tắt bỏ qua an toàn.
        /// </summary>
        /// <param name="slowAt">Điểm giảm tốc đã teach; 0 = chạy một mạch ở mức Auto cao. Chỉ
        /// nút Tới Pick/Tới Place truyền số này — các nút còn lại (Về gốc) không hạ xuống tray nên
        /// không có điểm giảm tốc nào để tôn trọng.</param>
        private async Task MoveZAsync(string axisName, double target, double slowAt = 0)
        {
            if (string.IsNullOrEmpty(axisName) || Busy) return;

            Busy = true;
            try
            {
                var axis = McControl.Motion[axisName];
                var ct = McControl.Safety.AbortToken;
                // Z dương là chiều ĐI XUỐNG, nên đích nhỏ hơn chỗ đang đứng nghĩa là đi LÊN.
                bool up = target < axis.CurrentPos;
                bool ok = slowAt > 0
                    ? (up
                        ? await McControl.Func.MoveZUpAsync(axisName, target, slowAt, ct).ConfigureAwait(true)
                        : await McControl.Func.MoveZDownAsync(axisName, target, slowAt, ct).ConfigureAwait(true))
                    : await McControl.Func.MoveAsync(axisName, target, axis.Config.MoveSpeed, ct).ConfigureAwait(true);

                if (!ok) _dialog.Warn(Loc.F("Teach.AxisMoveFailed", axisName, target, McControl.Status.Message));
            }
            finally { Busy = false; }
        }

        /// <summary>
        /// Tới T1/T2 Trigger: Z của tay đó về cao độ soi CAM bottom-up (đúng Z_SAFE, cùng cao độ Auto và các
        /// bước hiệu chuẩn chụp — Tuấn chốt 2026-09-22) rồi X tới vị trí soi đã teach. Nhờ vậy ảnh thấy lúc teach vị trí X
        /// cùng cỡ, cùng độ nét với lúc chạy thật.
        /// </summary>
        private async Task MoveToTriggerAsync(int nozzle)
        {
            if (Busy) return;

            Busy = true;
            try
            {
                string zName = nozzle == 1 ? AxisId.Z1 : AxisId.Z2;
                double zCapture = McControl.Motion.ZSafe;
                double x = McDataManager.Model.AlignPosX(nozzle);
                var ct = McControl.Safety.AbortToken;

                if (!await McControl.Func.MoveAsync(zName, zCapture, McControl.Motion[zName].Config.MoveSpeed, ct).ConfigureAwait(true))
                {
                    _dialog.Warn(Loc.F("Teach.ZCaptureFailed", zName, zCapture, McControl.Status.Message));
                    return;
                }
                if (!await McControl.Func.MoveAsync(AxisId.X, x, McControl.Motion[AxisId.X].Config.MoveSpeed, ct).ConfigureAwait(true))
                    _dialog.Warn(Loc.F("Teach.AxisMoveFailed", AxisId.X, x, McControl.Status.Message));
            }
            finally { Busy = false; }
        }

        /// <summary>
        /// Ghi lại độ lệch giữa vị trí VỪA JOG TỚI TAY và giá trị nội suy thuần (không offset).
        /// Trừ đi <c>CellPosRaw</c> chứ không phải <c>CellPos</c> — trừ nhầm sẽ trừ mất offset
        /// cũ hai lần và mỗi lần Set Offset lại làm số liệu lệch xa hơn thay vì đúng hơn.
        /// </summary>
        private void SetOffset(TeachCellRow row)
        {
            if (row == null) return;
            double x = McControl.Motion[AxisId.X].CurrentPos;
            double y = McControl.Motion[Layout.AxisRow].CurrentPos;
            var raw = Layout.CellPosRaw(CurrentZAxis, row.Row, row.Col);
            var oldOff = Layout.GetOffset(CurrentZAxis, row.Row, row.Col);
            Func<double, double, string> offText = (dx, dy) => string.Format(CultureInfo.InvariantCulture,
                Loc.T("Teach.OffsetText"), dx, Layout.AxisRow, dy);
            if (!ConfirmTeach(Loc.F("Teach.WhatCellPos", row.Row + 1, row.Col + 1, TrayText, UseNozzle1 ? 1 : 2),
                              offText(oldOff?.Dx ?? 0, oldOff?.Dy ?? 0), offText(x - raw.X, y - raw.Y))) return;
            McControl.InvalidateVisionCache();   // số quét ô / tư thế trên tay đã cất không còn khớp lưới, mốc hay model
            NoteTeachWithoutOrigin(string.Format("bù đặt của ô ({0},{1})", row.Row + 1, row.Col + 1));

            Layout.SetOffset(CurrentZAxis, row.Row, row.Col, x - raw.X, y - raw.Y);
            Compute();
        }

        private void ClearOffset(TeachCellRow row)
        {
            McControl.InvalidateVisionCache();   // số quét ô / tư thế trên tay đã cất không còn khớp lưới, mốc hay model
            if (row == null) return;
            Layout.SetOffset(CurrentZAxis, row.Row, row.Col, 0, 0);
            Compute();
        }

        // ───────────────────────── bù đặt từng ô (chỉ Tray lắp ráp) ─────────────────────────

        private string _trimDxText = "0", _trimDyText = "0", _trimDaText = "0";
        public string TrimDxText { get => _trimDxText; set => SetProperty(ref _trimDxText, value); }
        public string TrimDyText { get => _trimDyText; set => SetProperty(ref _trimDyText, value); }
        public string TrimDaText { get => _trimDaText; set => SetProperty(ref _trimDaText, value); }

        /// <summary>Ngưỡng lượng bù nhập tay — mm và độ.</summary>
        public const double TrimLimitMm = 1.0;
        public const double TrimLimitDeg = 5.0;

        /// <summary>Nạp 3 ô nhập theo ô đang chọn — không nạp lại thì số của ô trước còn nằm đó và một
        /// cái bấm Ghi là chép nhầm lượng bù sang ô khác.</summary>
        private void LoadTrimBoxes()
        {
            var row = SelectedCell;
            TrimDxText = Fmt3(row?.PlaceDx ?? 0);
            TrimDyText = Fmt3(row?.PlaceDy ?? 0);
            TrimDaText = Fmt3(row?.PlaceDa ?? 0);
        }

        private static string Fmt3(double v) => v.ToString("0.###", CultureInfo.InvariantCulture);

        /// <summary>
        /// Ghi lượng bù đặt cho ô đang chọn, của đúng Tool đang chọn. Số nhập là lượng CỘNG THÊM vào
        /// đích gửi VisionMaster: sản phẩm đặt xuống lệch +0,05 mm theo X thì nhập −0,05. Chặn trên
        /// ±<see cref="TrimLimitMm"/> mm / ±<see cref="TrimLimitDeg"/>° — lệch hơn thế là lỗi hiệu chuẩn
        /// (hand-eye, dấu góc, lưới teach), tinh chỉnh không sửa được mà còn giấu mất lỗi.
        /// </summary>
        private void ApplyPlaceTrim(TeachCellRow row)
        {
            if (row == null) { _dialog.Warn(Loc.T("Teach.NoCellChosen")); return; }
            if (UseInputTray) { _dialog.Warn(Loc.T("Teach.TrimAssyOnly")); return; }

            double dx, dy, da;
            if (!TryParseTrim(TrimDxText, TrimLimitMm, "X", out dx)) return;
            if (!TryParseTrim(TrimDyText, TrimLimitMm, "Y", out dy)) return;
            if (!TryParseTrim(TrimDaText, TrimLimitDeg, Loc.T("Teach.WordAngle"), out da)) return;

            McControl.InvalidateVisionCache();   // tư thế đã cất trên tay tính theo đích cũ
            Layout.SetPlaceTrim(CurrentZAxis, row.Row, row.Col, dx, dy, da);
            Compute();
        }

        private void ClearPlaceTrim(TeachCellRow row)
        {
            if (row == null) return;
            McControl.InvalidateVisionCache();
            Layout.SetPlaceTrim(CurrentZAxis, row.Row, row.Col, 0, 0, 0);
            Compute();
            LoadTrimBoxes();
        }

        // ───────────────────────── bù đặt chung — mọi ô của Tool đang chọn (chỉ Tray lắp ráp) ─────────────────────────

        private string _trimAllDxText = "0", _trimAllDyText = "0", _trimAllDaText = "0";
        public string TrimAllDxText { get => _trimAllDxText; set => SetProperty(ref _trimAllDxText, value); }
        public string TrimAllDyText { get => _trimAllDyText; set => SetProperty(ref _trimAllDyText, value); }
        public string TrimAllDaText { get => _trimAllDaText; set => SetProperty(ref _trimAllDaText, value); }

        public bool HasPlaceTrimAll
        {
            get
            {
                var geo = CurrentGeometry;
                return Math.Abs(geo.PlaceDxAll) > 1e-9 || Math.Abs(geo.PlaceDyAll) > 1e-9 || Math.Abs(geo.PlaceDaAll) > 1e-9;
            }
        }

        /// <summary>Nạp 3 ô nhập bù chung theo tray + Tool đang chọn — gọi trong <see cref="Compute"/>, nên đổi Tool,
        /// đổi tray hay nạp model đều hiện đúng lượng bù của lưới đó.</summary>
        private void LoadTrimAllBoxes()
        {
            var geo = CurrentGeometry;
            TrimAllDxText = Fmt3(geo.PlaceDxAll);
            TrimAllDyText = Fmt3(geo.PlaceDyAll);
            TrimAllDaText = Fmt3(geo.PlaceDaAll);
            OnPropertyChanged(nameof(HasPlaceTrimAll));
        }

        /// <summary>
        /// Ghi bù chung cho mọi ô của Tool đang chọn — cùng dấu, cùng ngưỡng với bù riêng của ô; App cộng dồn hai lượng.
        /// Dùng khi đo sau đặt thấy mọi ô của một tay cùng lệch một lượng (Tuấn yêu cầu 2026-09-15).
        /// </summary>
        private void ApplyPlaceTrimAll()
        {
            if (UseInputTray) { _dialog.Warn(Loc.T("Teach.TrimAssyOnly")); return; }

            double dx, dy, da;
            if (!TryParseTrim(TrimAllDxText, TrimLimitMm, "X", out dx)) return;
            if (!TryParseTrim(TrimAllDyText, TrimLimitMm, "Y", out dy)) return;
            if (!TryParseTrim(TrimAllDaText, TrimLimitDeg, Loc.T("Teach.WordAngle"), out da)) return;

            McControl.InvalidateVisionCache();   // tư thế đã cất trên tay tính theo đích cũ
            Layout.SetPlaceTrimAll(CurrentZAxis, dx, dy, da);
            Compute();
        }

        private void ClearPlaceTrimAll()
        {
            McControl.InvalidateVisionCache();
            Layout.SetPlaceTrimAll(CurrentZAxis, 0, 0, 0);
            Compute();
        }

        private bool TryParseTrim(string text, double limit, string what, out double value)
        {
            if (!double.TryParse((text ?? "").Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out value))
            {
                _dialog.Warn(Loc.F("Teach.TrimNotNumber", what));
                return false;
            }
            if (Math.Abs(value) > limit)
            {
                _dialog.Warn(Loc.F("Teach.TrimTooLarge", what, limit.ToString("0.#", CultureInfo.InvariantCulture)));
                value = 0;
                return false;
            }
            return true;
        }

        // ───────────────────────── quản lý model ─────────────────────────

        public ObservableCollection<string> ModelNames { get; } = new ObservableCollection<string>();

        private string _selectedModelName;
        public string SelectedModelName { get => _selectedModelName; set => SetProperty(ref _selectedModelName, value); }

        private string _newModelNameText = "";
        public string NewModelNameText { get => _newModelNameText; set => SetProperty(ref _newModelNameText, value); }

        public string CurrentModelName => McDataManager.Model.Name;

        private void RefreshModelList()
        {
            ModelNames.Clear();
            foreach (var n in McDataManager.ListModels()) ModelNames.Add(n);
            if (SelectedModelName == null || !ModelNames.Contains(SelectedModelName))
                SelectedModelName = ModelNames.FirstOrDefault();
        }

        private void LoadSelectedModel()
        {
            McControl.InvalidateVisionCache();   // số quét ô / tư thế trên tay đã cất không còn khớp lưới, mốc hay model
            if (string.IsNullOrWhiteSpace(SelectedModelName)) return;

            if (McControl.Status.State == EMcState.Running || McControl.Status.State == EMcState.Held ||
                McControl.Status.State == EMcState.Homing)
            {
                _dialog.Warn(Loc.T("Teach.LoadModelBusy"));
                return;
            }

            if (McDataManager.LoadModel(SelectedModelName))
            {
                OnPropertyChanged(nameof(CurrentModelName));
                OnTraySwitched();
            }
            else
            {
                _dialog.Error(Loc.F("Teach.LoadModelFailed", SelectedModelName));
            }
        }

        private void NewModel()
        {
            McControl.InvalidateVisionCache();   // số quét ô / tư thế trên tay đã cất không còn khớp lưới, mốc hay model
            string name = (NewModelNameText ?? "").Trim();
            if (!ValidModelName(name)) return;

            if (ModelNames.Contains(name))
            {
                _dialog.Warn(Loc.F("Teach.ModelExists", name));
                return;
            }

            var model = new ModelData { Name = name };
            McDataManager.UseModel(model, countsAsLoaded: true);
            McDataManager.SaveModel(name);

            NewModelNameText = "";
            RefreshModelList();
            SelectedModelName = name;
            OnPropertyChanged(nameof(CurrentModelName));
            OnTraySwitched();
        }

        private void CopyModel()
        {
            string source = SelectedModelName;
            string target = (NewModelNameText ?? "").Trim();

            if (string.IsNullOrWhiteSpace(source)) { _dialog.Warn(Loc.T("Teach.ChooseSourceModel")); return; }
            if (!ValidModelName(target)) return;
            if (ModelNames.Contains(target)) { _dialog.Warn(Loc.F("Teach.ModelExists", target)); return; }

            if (McDataManager.CopyModel(source, target))
            {
                NewModelNameText = "";
                RefreshModelList();
                SelectedModelName = target;
            }
            else
            {
                _dialog.Error(Loc.T("Teach.CopyModelFailed"));
            }
        }

        private void DeleteModel()
        {
            string name = SelectedModelName;
            if (string.IsNullOrWhiteSpace(name)) return;

            if (string.Equals(name, McDataManager.Model.Name, StringComparison.OrdinalIgnoreCase))
            {
                _dialog.Warn(Loc.T("Teach.DeleteOpenModel"));
                return;
            }

            if (!_dialog.Confirm(Loc.F("Teach.ConfirmDeleteModel", name))) return;

            if (McDataManager.DeleteModel(name)) RefreshModelList();
            else _dialog.Error(Loc.T("Teach.DeleteModelFailed"));
        }

        private void SaveModel()
        {
            if (McDataManager.SaveModel())
            {
                RefreshModelList();
                SelectedModelName = McDataManager.Model.Name;
            }
            else
            {
                _dialog.Error(Loc.T("Teach.SaveModelFailed"));
            }
        }

        private bool ValidModelName(string name)
        {
            if (string.IsNullOrWhiteSpace(name)) { _dialog.Warn(Loc.T("Teach.EnterModelName")); return false; }
            if (name.IndexOfAny(System.IO.Path.GetInvalidFileNameChars()) >= 0)
            {
                _dialog.Warn(Loc.T("Teach.ModelNameInvalid"));
                return false;
            }
            return true;
        }

        // ───────────────────────── commands ─────────────────────────

        public ICommand Vacuum1ToggleCommand { get; }
        public ICommand Vacuum2ToggleCommand { get; }
        public ICommand Blow1ToggleCommand { get; }
        public ICommand Blow2ToggleCommand { get; }
        public ICommand ToggleCam1LampCommand { get; }
        public ICommand ToggleCam2LampCommand { get; }

        public ICommand SelectInputCommand { get; }
        public ICommand SelectAssyCommand { get; }
        public ICommand SelectNozzle1Command { get; }
        public ICommand SelectNozzle2Command { get; }
        public ICommand SetJogSlowCommand { get; }
        public ICommand SetJogFastCommand { get; }
        public ICommand ApplySizeCommand { get; }

        public ICommand TeachP00Command { get; }
        public ICommand TeachP0CCommand { get; }
        public ICommand TeachPR0Command { get; }
        public ICommand TeachVerifyCommand { get; }
        public ICommand TeachZPickZ1Command { get; }
        public ICommand TeachZPickZ2Command { get; }
        public ICommand TeachPickAngleA1Command { get; }
        public ICommand TeachPickAngleA2Command { get; }
        public ICommand TeachZSlowZ1Command { get; }
        public ICommand TeachZSlowZ2Command { get; }
        public ICommand TeachAlignNozzle1Command { get; }
        public ICommand TeachAlignNozzle2Command { get; }

        public ICommand TeachHandEyePick1Command { get; }
        public ICommand TeachHandEyePick2Command { get; }
        public ICommand TeachHandEyeCamPosCommand { get; }
        public ICommand MoveHandEyePick1Command { get; }
        public ICommand MoveHandEyePick2Command { get; }
        public ICommand MoveHandEyeCamPosCommand { get; }
        public ICommand TeachCamTopDownAnchorCommand { get; }
        public ICommand MoveCamTopDownAnchorCommand { get; }

        public ICommand MoveToCellCommand { get; }
        public ICommand MoveCamToCellCommand { get; }
        public ICommand MoveZAtCellCommand { get; }
        public ICommand MoveZHomeCommand { get; }
        public ICommand MoveZPickCommand { get; }
        public ICommand MoveZPlaceCommand { get; }
        public ICommand MoveXTrigger1Command { get; }
        public ICommand MoveXTrigger2Command { get; }
        public ICommand SetOffsetCommand { get; }
        public ICommand ApplyPlaceTrimCommand { get; }
        public ICommand ClearPlaceTrimCommand { get; }
        public ICommand ApplyPlaceTrimAllCommand { get; }
        public ICommand ClearPlaceTrimAllCommand { get; }
        public ICommand ClearOffsetCommand { get; }
        public ICommand SelectCellCommand { get; }

        public ICommand NewModelCommand { get; }
        public ICommand CopyModelCommand { get; }
        public ICommand DeleteModelCommand { get; }
        public ICommand LoadSelectedModelCommand { get; }
        public ICommand SaveModelCommand { get; }
    }
}
