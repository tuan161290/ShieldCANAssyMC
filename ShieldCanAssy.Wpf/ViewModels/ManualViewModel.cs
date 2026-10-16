using System;
using System.Collections.ObjectModel;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Input;
using System.Windows.Threading;
using ShieldCanAssy.Core.Communication.Sim;
using ShieldCanAssy.Core.Controller;
using ShieldCanAssy.Core.Infor;
using ShieldCanAssy.Core.Lang;
using ShieldCanAssy.Wpf.Models;
using ShieldCanAssy.Wpf.Mvvm;

namespace ShieldCanAssy.Wpf.ViewModels
{
    /// <summary>
    /// Trang Manual: điều khiển đầy đủ từng trục (jog/Stop/Home/SVO/RST/Move), bảng đèn I/O, và
    /// bảng mô phỏng.
    ///
    /// <para>Khí nén (Vacuum/Blow) đã chuyển sang trang Teaching (<see cref="TeachingViewModel"/>)
    /// — teach cần bật/tắt hút liên tục trong lúc thao tác. Jog trục thì VẪN ở đây đầy đủ (Stop/
    /// Home/SVO/RST/Move) — Teaching chỉ mượn một panel jog RÚT GỌN (chỉ nút Jog −/+ và vị trí,
    /// không Stop/Home/SVO/RST/Move) để đủ jog vào đúng ô lúc teach, không thay thế trang này
    /// (quyết định của Tuấn, 2026-08-19 — lần đầu chuyển nhầm cả cụm sang, đã rút lại).</para>
    ///
    /// <para>Bảng mô phỏng không phải đồ chơi: nó là cách duy nhất chạy được các nhánh EMG, cửa
    /// mở và mất kết nối mà không phải rút cáp máy thật (PLAN.md §10).</para>
    /// </summary>
    public class ManualViewModel : ObservableObject
    {
        private readonly DispatcherTimer _timer;

        public ManualViewModel()
        {
            foreach (var axis in McControl.Motion.AxisList)
                Axes.Add(new AxisRowVm(axis));

            Gantry = new ObservableCollection<AxisRowVm>(Axes.Where(a => a.Name == AxisId.X || a.Name == AxisId.Z1 || a.Name == AxisId.Z2));
            Rotate = new ObservableCollection<AxisRowVm>(Axes.Where(a => a.Name == AxisId.A1 || a.Name == AxisId.A2));
            Tray = new ObservableCollection<AxisRowVm>(Axes.Where(a => a.Name == AxisId.Y1 || a.Name == AxisId.Y2));

            BuildIoLamps();
            // Trang Manual sống suốt đời App — đăng ký sự kiện tĩnh không rò rỉ.
            Loc.LanguageChanged += BuildIoLamps;

            OriginCommand = new RelayCommand(McControl.RequestOrigin);
            // STOP giống hệt nút STOP trang Main (Tuấn 2026-10-07): trước đây chỉ Motion.StopAll — trục dừng nhưng chu trình
            // đang chạy (về gốc, Init, chạy rà…) vẫn sống và phát lệnh tiếp. RequestStop xử lý theo trạng thái máy.
            StopAllCommand = new RelayCommand(McControl.RequestStop);
            // RESET ngay tại trang Manual — gọi thẳng McControl.RequestReset giống hệt nút RESET
            // trang Main, để khỏi phải chuyển qua lại 2 trang mỗi lần xoá alarm lúc đang thao tác
            // tay (Tuấn yêu cầu, 2026-09-09).
            ResetCommand = new RelayCommand(McControl.RequestReset);

            // SVO ON tất cả — bật servo TỪNG trục chưa ServoOn, bỏ qua (không chặn cả nút) trục
            // nào đang Alarm giống hệt điều kiện của ServoToggleCommand riêng lẻ (AxisRowVm) — vẫn
            // phải RST trục đó trước. Chạy song song (Task.WhenAll) để không phải đợi tuần tự qua
            // 7 trục (Tuấn yêu cầu, 2026-09-08).
            ServoOnAllCommand = new RelayCommand(async () =>
            {
                var alarmed = Axes.Where(a => !a.Axis.ServoOn && a.Axis.Alarm).Select(a => a.Name).ToList();
                var tasks = Axes.Where(a => !a.Axis.ServoOn && !a.Axis.Alarm).Select(a => a.Axis.SetServoOnAsync(true));
                await Task.WhenAll(tasks);
                if (alarmed.Count > 0)
                    McControl.Status.Message = Loc.F("Manual.ServoOnSkipped", string.Join(", ", alarmed));
            });

            // Mở phanh tất cả — chỉ Z1/Z2 có phanh (HasBrakeButton). Gọi thẳng RequestBrakeRelease
            // như một cú bấm nhanh bình thường (KHÔNG phải nhả khẩn cấp kiểu giữ 3s của nút riêng
            // từng trục) — vẫn đòi ServoOn + không Alarm mới thực sự nhả, y hệt điều kiện của nút
            // "Mở phanh" riêng lẻ (xem McFunction.UpdateBrakeOutputs).
            BrakeReleaseAllCommand = new RelayCommand(() =>
            {
                McControl.Func.RequestBrakeRelease(AxisId.Z1);
                McControl.Func.RequestBrakeRelease(AxisId.Z2);
            });

            // Bốn nút Start như nhau (2026-10-16) — một nút mô phỏng, bấm nút cụm B #1.
            SimPressStartCommand = new RelayCommand(() => SimIo?.SimPress(IoMap.BtnGoB1));
            SimPressStopCommand = new RelayCommand(() => SimIo?.SimPress(IoMap.BtnStop));
            // Không còn SimPressResetCommand — máy KHÔNG có nút RESET vật lý (Tuấn xác nhận,
            // 2026-09-08), RESET chỉ bấm được bằng nút mềm ở trang Main (ResetCommand đó gọi
            // thẳng McControl.RequestReset, không qua DI nào nên vẫn dùng được y hệt lúc mô
            // phỏng lẫn máy thật — không cần nút giả lập riêng ở đây nữa).
            // Không còn nút Tắt còi cứng (Tuấn 2026-10-15) — bit đó thành cảm biến Tray đầu vào 1.

            SimToggleEmgCommand = new RelayCommand(() => SimIo?.SimToggle(IoMap.EmgOk));
            SimToggleLightCurtainCommand = new RelayCommand(() => SimIo?.SimToggle(IoMap.LightCurtainOk));
            SimToggleAirCommand = new RelayCommand(() => SimIo?.SimToggle(IoMap.AirOk));
            // Hai cảm biến tray đầu vào — đặt/lấy tray giả lập. Mặc định chỉ Tray 1 có (FakeIo).
            SimToggleTray1Command = new RelayCommand(() => SimIo?.SimToggle(IoMap.TrayIn1Present));
            SimToggleTray2Command = new RelayCommand(() => SimIo?.SimToggle(IoMap.TrayIn2Present));

            // Đèn trigger CAM Bottom-up (DoLampCam1, 0x0B) / CAM Top-down (DoLampCam2, 0x0C) —
            // Routine_Auto tự bật/tắt quanh lúc trigger Vision trong chu trình Auto, nhưng lúc
            // thao tác tay (canh nét, kiểm tra đèn, chỉnh camera) cần bật/tắt thủ công độc lập với
            // Auto (Tuấn yêu cầu, 2026-09-09). Ghi thẳng qua McControl.Io (không phải SimIo) nên
            // chạy được cả trên máy thật lẫn mô phỏng — khác các nút SimToggle* phía trên chỉ có
            // tác dụng khi SimulationMode bật.
            ToggleCamBtmLampCommand = new RelayCommand(() =>
            {
                var io = McControl.Io;
                if (io == null) return;
                io.SetDo(IoMap.DoLampCam1, !io.GetDo(IoMap.DoLampCam1));
            });
            ToggleCamTopLampCommand = new RelayCommand(() =>
            {
                var io = McControl.Io;
                if (io == null) return;
                io.SetDo(IoMap.DoLampCam2, !io.GetDo(IoMap.DoLampCam2));
            });

            _timer = new DispatcherTimer(DispatcherPriority.Background) { Interval = TimeSpan.FromMilliseconds(150) };
            _timer.Tick += (_, __) => Refresh();
            _timer.Start();
        }

        public McStatus Status => McControl.Status;

        public ObservableCollection<AxisRowVm> Axes { get; } = new ObservableCollection<AxisRowVm>();
        public ObservableCollection<AxisRowVm> Gantry { get; }
        public ObservableCollection<AxisRowVm> Rotate { get; }
        public ObservableCollection<AxisRowVm> Tray { get; }

        public ObservableCollection<IoLampVm> DiLamps { get; } = new ObservableCollection<IoLampVm>();
        public ObservableCollection<IoLampVm> DoLamps { get; } = new ObservableCollection<IoLampVm>();

        /// <summary>
        /// Dựng (lần đầu) hoặc thay từng đèn I/O với nhãn theo ngôn ngữ đang chọn. <see cref="IoLampVm.Caption"/> không
        /// báo đổi, nên đổi ngôn ngữ thì thay hẳn đối tượng đèn — giữ trạng thái On, Refresh 150 ms sau cũng ghi lại.
        /// </summary>
        private void BuildIoLamps()
        {
            var di = IoMap.DiNames;
            for (int i = 0; i < IoMap.DiCount; i++)
            {
                var lamp = new IoLampVm { Bit = i, Name = di[i], IsInput = true };
                if (i < DiLamps.Count) { lamp.On = DiLamps[i].On; DiLamps[i] = lamp; }
                else DiLamps.Add(lamp);
            }
            var doo = IoMap.DoNames;
            for (int i = 0; i < IoMap.DoCount; i++)
            {
                var lamp = new IoLampVm { Bit = i, Name = doo[i], IsInput = false };
                if (i < DoLamps.Count) { lamp.On = DoLamps[i].On; DoLamps[i] = lamp; }
                else DoLamps.Add(lamp);
            }
        }

        private FakeIo SimIo => McControl.Io as FakeIo;
        public bool IsSimulation => SimIo != null;

        private string _blockReason;
        public string BlockReason { get => _blockReason; private set => SetProperty(ref _blockReason, value); }

        private bool _zSafeOk;
        public bool ZSafeOk { get => _zSafeOk; private set => SetProperty(ref _zSafeOk, value); }

        // Nút "SVO ON tất cả" / "Mở phanh tất cả" kiêm đèn báo (Tuấn 2026-10-13): xanh khi MỌI trục liên quan đã ở trạng
        // thái nút đó bật — servo cả 7 trục, phanh cả Z1 lẫn Z2. Nút Về gốc dùng sẵn Status.OrgOk.
        private bool _allServoOn, _allBrakesReleased;
        public bool AllServoOn { get => _allServoOn; private set => SetProperty(ref _allServoOn, value); }
        public bool AllBrakesReleased { get => _allBrakesReleased; private set => SetProperty(ref _allBrakesReleased, value); }

        private string _rawDi, _rawDo;
        public string RawDiText { get => _rawDi; private set => SetProperty(ref _rawDi, value); }
        public string RawDoText { get => _rawDo; private set => SetProperty(ref _rawDo, value); }

        /// <summary>Trạng thái thô của các trục Fastech, giải mã thành tên bit.</summary>
        public ObservableCollection<string> FastechStatus { get; } = new ObservableCollection<string>();

        private void Refresh()
        {
            var io = McControl.Io;
            if (io == null) return;

            foreach (var lamp in DiLamps) lamp.On = io.GetDi(lamp.Bit);
            foreach (var lamp in DoLamps) lamp.On = io.GetDo(lamp.Bit);

            // Đèn nút "Mở phanh" — đọc thẳng DO thật, không suy luận lại từ ServoOn/Alarm ở tầng
            // UI, để khớp chính xác với những gì McFunction.UpdateBrakeOutputs đang ghi.
            foreach (var row in Axes)
            {
                if (row.Name == AxisId.Z1) row.BrakeReleased = io.GetDo(IoMap.DoBrakeReleaseZ1);
                else if (row.Name == AxisId.Z2) row.BrakeReleased = io.GetDo(IoMap.DoBrakeReleaseZ2);
            }
            AllServoOn = McControl.Motion.AllServoOn;
            AllBrakesReleased = io.GetDo(IoMap.DoBrakeReleaseZ1) && io.GetDo(IoMap.DoBrakeReleaseZ2);

            BlockReason = McControl.Safety.SafeToMove ? "" : McControl.Safety.BlockReason;
            ZSafeOk = McControl.Motion.ZSafeOk;

            RefreshDiagnostics(io);
        }

        private void RefreshDiagnostics(Core.Communication.IIoDevice io)
        {
            uint di = 0, doo = 0;
            for (int i = 0; i < IoMap.DiCount; i++) if (io.GetDi(i)) di |= 1u << i;
            for (int i = 0; i < IoMap.DoCount; i++) if (io.GetDo(i)) doo |= 1u << i;

            RawDiText = "DI  0x" + di.ToString("X8") + "   " + Bits(di);
            RawDoText = "DO  0x" + doo.ToString("X8") + "   " + Bits(doo);

            int row = 0;
            foreach (var axis in McControl.Motion.AxisList)
            {
                var fa = axis as Core.Communication.FastechAxis;
                if (fa == null) continue;

                string line = string.Format("{0}  0x{1:X8}  {2}", fa.Name, fa.RawStatus, fa.RawStatusText);
                if (row < FastechStatus.Count) FastechStatus[row] = line;
                else FastechStatus.Add(line);
                row++;
            }
            while (FastechStatus.Count > row) FastechStatus.RemoveAt(FastechStatus.Count - 1);
        }

        /// <summary>Bit 31 bên trái, bit 0 bên phải — cùng chiều với cách đọc sơ đồ điện.</summary>
        private static string Bits(uint v)
        {
            var sb = new System.Text.StringBuilder(39);
            for (int i = 31; i >= 0; i--)
            {
                sb.Append((v & (1u << i)) != 0 ? '1' : '0');
                if (i % 8 == 0 && i > 0) sb.Append(' ');
            }
            return sb.ToString();
        }

        public ICommand OriginCommand { get; }
        public ICommand StopAllCommand { get; }
        public ICommand ResetCommand { get; }
        public ICommand ServoOnAllCommand { get; }
        public ICommand BrakeReleaseAllCommand { get; }

        public ICommand SimPressStartCommand { get; }
        public ICommand SimPressStopCommand { get; }
        public ICommand SimToggleTray1Command { get; }
        public ICommand SimToggleTray2Command { get; }
        public ICommand SimToggleEmgCommand { get; }
        public ICommand SimToggleLightCurtainCommand { get; }
        public ICommand ToggleCamBtmLampCommand { get; }
        public ICommand ToggleCamTopLampCommand { get; }
        public ICommand SimToggleAirCommand { get; }
    }
}
