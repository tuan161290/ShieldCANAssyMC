using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.Windows.Input;
using System.Windows.Threading;
using ShieldCanAssy.Core.Communication;
using ShieldCanAssy.Core.Controller;
using ShieldCanAssy.Core.Data;
using ShieldCanAssy.Core.Infor;
using ShieldCanAssy.Core.Lang;
using ShieldCanAssy.SysConfig;
using ShieldCanAssy.Utility;
using ShieldCanAssy.Wpf.Mvvm;
using ShieldCanAssy.Wpf.Services;

namespace ShieldCanAssy.Wpf.ViewModels
{
    public enum ShellSection
    {
        Main = 0,
        Manual = 1,
        Teaching = 2,
        Setting = 3,
        VisionEdit = 4,
        /// <summary>Trang Alarm riêng (Tuấn yêu cầu, 2026-09-10) — thêm ở CUỐI enum để không đổi
        /// số thứ tự 5 trang trước đó (CurrentSectionIndex khớp thẳng vị trí TabItem trong
        /// MainWindow.xaml).</summary>
        Alarm = 5
    }

    /// <summary>
    /// Shell: điều hướng 5 trang, thanh trạng thái, banner alarm.
    ///
    /// <para>Không có timer poll trạng thái: <see cref="McStatus"/> kế thừa Bindable và tự bắn
    /// PropertyChanged về UI thread, nên XAML bind thẳng vào <see cref="Status"/>. Poll định kỳ
    /// vừa trễ vừa dễ bỏ sót thay đổi ngắn (ví dụ một lần nháy alarm rồi tự hết).</para>
    /// </summary>
    public class ShellViewModel : ObservableObject
    {
        /// <summary>Banner nổi tự ẩn sau chừng này nếu không có alarm mới — đủ để đọc, không
        /// đứng chắn thao tác lâu (theo yêu cầu: "hiện khoảng 10s rồi tự ẩn").</summary>
        private static readonly TimeSpan AlarmBannerAutoHide = TimeSpan.FromSeconds(10);

        private readonly DispatcherTimer _alarmBannerTimer;

        public ShellViewModel()
        {
            ShowMainCommand = new RelayCommand(() => CurrentSection = ShellSection.Main);
            ShowManualCommand = new RelayCommand(() => CurrentSection = ShellSection.Manual);
            ShowTeachingCommand = new RelayCommand(() => CurrentSection = ShellSection.Teaching);
            ShowSettingCommand = new RelayCommand(() => CurrentSection = ShellSection.Setting);
            ShowVisionEditCommand = new RelayCommand(() => CurrentSection = ShellSection.VisionEdit);
            // Nút "⚠ Alarm" ở status bar (MainWindow.xaml) — trước gọi ToggleAlarmPanelCommand để
            // mở overlay che kín màn hình, giờ chuyển sang trang Alarm riêng (Tuấn yêu cầu,
            // 2026-09-10) để xem được cả Active lẫn History mà không phải che các trang khác.
            // ToggleAlarmPanelCommand/AlarmPanelOpen vẫn giữ nguyên cho banner nổi (Chi tiết) —
            // không đụng tới, chỉ đổi đường của riêng nút status bar.
            ShowAlarmCommand = new RelayCommand(() => CurrentSection = ShellSection.Alarm);

            ResetCommand = new RelayCommand(McControl.RequestReset);
            MuteCommand = new RelayCommand(McControl.RequestMuteBuzzer);
            ToggleAlarmPanelCommand = new RelayCommand(() => AlarmPanelOpen = !AlarmPanelOpen);
            HideAlarmBannerCommand = new RelayCommand(() =>
            {
                _alarmBannerTimer.Stop();
                AlarmBannerVisible = false;
            });

            _alarmBannerTimer = new DispatcherTimer { Interval = AlarmBannerAutoHide };
            _alarmBannerTimer.Tick += (_, __) =>
            {
                _alarmBannerTimer.Stop();
                AlarmBannerVisible = false;
            };

            // ActiveAlarms.Count đổi thì ShowAlarmBanner/HasActiveAlarms phải tính lại — collection
            // không tự bắn PropertyChanged cho property TÍNH RA từ nó.
            AlarmManager.ActiveAlarms.CollectionChanged += (_, __) =>
            {
                OnPropertyChanged(nameof(ShowAlarmBanner));
                OnPropertyChanged(nameof(HasActiveAlarms));
                OnPropertyChanged(nameof(ActiveAlarmCountText));
            };

            // AlarmRaised chỉ bắn cho alarm THẬT SỰ mới (không bắn khi raise lại alarm cũ) — đúng
            // ý "có việc mới thì hiện lại", không phải hiện liên tục trong khi alarm cũ vẫn còn.
            // Bắn từ thread nền (vòng quét an toàn) nên phải marshal về UI thread mới đụng được
            // DispatcherTimer/property bindable.
            //
            // Chỉ Error/Fatal mới bật banner (Tuấn 2026-09-29): Warning như hút hụt thì máy vẫn chạy tiếp sang ô sau,
            // không cần nổi lên che màn hình — vẫn nằm trong danh sách alarm và nút RESET vẫn sáng.
            AlarmManager.AlarmRaised += a => UiThread.Run(() =>
            {
                if (a.Level < EAlarmLevel.Error) return;
                BannerAlarm = a;
                AlarmBannerVisible = true;
                _alarmBannerTimer.Stop();
                _alarmBannerTimer.Start();
            });

            // Bấm AUTO mà chưa đủ điều kiện: máy ở lại MANUAL, bật hộp thoại nêu lý do (Tuấn 2026-09-30). Post về UI
            // thread (không chờ) — hộp thoại chặn tới lúc bấm OK, không được chặn vòng quét.
            McControl.AutoModeRefused += why => UiThread.Run(() => DialogHost.Warn(why, Loc.T("Shell.AutoRefusedTitle")));

            // DialogHost.Show đã tự Dispatcher.Invoke về UI thread trước khi bắn sự kiện này
            // (xem DialogHost) — không cần UiThread.Run ở đây nữa.
            DialogHost.Requested += ShowDialog;

            ToggleLanguageCommand = new RelayCommand(() =>
            {
                var next = Loc.Language == ELanguage.Vi ? ELanguage.En : ELanguage.Vi;
                Loc.SetLanguage(next);
                McDataManager.SysData.Language = Loc.Code(next);
                McDataManager.SaveSystemData();
            });
            Loc.LanguageChanged += () =>
            {
                OnPropertyChanged(nameof(LanguageCode));
                OnPropertyChanged(nameof(ActiveAlarmCountText));
            };

            DialogOkCommand = new RelayCommand(() => CloseDialog(true));
            DialogYesCommand = new RelayCommand(() => CloseDialog(true));
            DialogNoCommand = new RelayCommand(() => CloseDialog(false));

            CurrentSection = ShellSection.Main;
        }

        public McStatus Status => McControl.Status;
        public ObservableCollection<AlarmData> ActiveAlarms => AlarmManager.ActiveAlarms;

        /// <summary>Dùng cho DataTrigger Value="True" của các nút glow theo alarm (RESET, nút Nav
        /// tab Alarm...) — KHÔNG bind thẳng ActiveAlarms.Count rồi so Value="0": tránh phải đặt
        /// Setter Property="Effect" Value="{x:Null}" để "xoá" Effect khi hết alarm, cách đó từng
        /// làm WPF ném XamlParseException "'{DependencyProperty.UnsetValue}' is not a valid value
        /// for property 'Effect'" ngay lúc MainWindow.InitializeComponent (phát hiện 2026-09-10).
        /// Với property bool này, style mặc định KHÔNG đặt Effect gì cả (an toàn), trigger chỉ
        /// THÊM Effect khi HasActiveAlarms=True, không bao giờ phải gỡ lại.</summary>
        public bool HasActiveAlarms => ActiveAlarms.Count > 0;

        /// <summary>Dòng nhỏ trên banner alarm: "3 alarm đang tác động".</summary>
        public string ActiveAlarmCountText => Loc.F("Shell.ActiveAlarmCount", ActiveAlarms.Count);

        /// <summary>Tới 200 alarm gần nhất (kể cả đã hết) — dùng cho trang Alarm riêng, xem
        /// <see cref="AlarmManager.History"/>.</summary>
        public ObservableCollection<AlarmData> AlarmHistory => AlarmManager.History;

        /// <summary>Header bar đọc toạ độ 7 trục trực tiếp qua đây (vd. "Motion[X].CurrentPos")
        /// — MotionConnect và từng IAxis đều là Bindable, tự bắn PropertyChanged khi CurrentPos
        /// đổi, không cần timer poll (cùng lý do McStatus không cần, xem doc-comment class).</summary>
        public MotionConnect Motion => McControl.Motion;

        public string MachineName => McDataManager.SysData.MachineName;

        /// <summary>Ngôn ngữ đang dùng, hiện trên nút đổi ngôn ngữ ở thanh tiêu đề.</summary>
        public string LanguageCode => Loc.Language == ELanguage.En ? "EN" : "VI";
        public string DataRoot => AppPaths.Root;
        public bool UsingFallbackRoot => AppPaths.UsingFallbackRoot;

        // ── Điều hướng ──
        private ShellSection _currentSection;
        public ShellSection CurrentSection
        {
            get => _currentSection;
            set
            {
                if (!SetProperty(ref _currentSection, value)) return;
                OnPropertyChanged(nameof(IsMain));
                OnPropertyChanged(nameof(IsManual));
                OnPropertyChanged(nameof(IsTeaching));
                OnPropertyChanged(nameof(IsSetting));
                OnPropertyChanged(nameof(IsVisionEdit));
                OnPropertyChanged(nameof(IsAlarm));
                OnPropertyChanged(nameof(CurrentSectionIndex));
            }
        }

        public bool IsMain => CurrentSection == ShellSection.Main;
        public bool IsManual => CurrentSection == ShellSection.Manual;
        public bool IsTeaching => CurrentSection == ShellSection.Teaching;
        public bool IsSetting => CurrentSection == ShellSection.Setting;
        public bool IsVisionEdit => CurrentSection == ShellSection.VisionEdit;
        public bool IsAlarm => CurrentSection == ShellSection.Alarm;

        /// <summary>Thứ tự enum khớp thứ tự TabItem trong MainWindow.xaml.</summary>
        public int CurrentSectionIndex => (int)CurrentSection;

        // ── Banner alarm ──
        private bool _alarmPanelOpen;
        public bool AlarmPanelOpen { get => _alarmPanelOpen; set => SetProperty(ref _alarmPanelOpen, value); }

        /// <summary>Cờ hiện/ẩn banner nổi — bấm "Ẩn" hoặc hết 10s thì false, có alarm mới thì lại
        /// true. Tách khỏi <see cref="ShowAlarmBanner"/> vì banner còn phải ẩn khi hết alarm luôn
        /// (Count = 0), không chỉ theo mỗi cờ này.</summary>
        private bool _alarmBannerVisible;
        public bool AlarmBannerVisible
        {
            get => _alarmBannerVisible;
            set { if (SetProperty(ref _alarmBannerVisible, value)) OnPropertyChanged(nameof(ShowAlarmBanner)); }
        }

        /// <summary>Điều kiện hiện banner thật sự: còn alarm VÀ chưa bị ẩn/hết giờ.</summary>
        public bool ShowAlarmBanner => ActiveAlarms.Count > 0 && AlarmBannerVisible;

        /// <summary>Alarm Error/Fatal vừa bật banner — banner hiện đúng nó, không phải dòng đầu danh sách (có thể là
        /// một Warning cũ).</summary>
        private AlarmData _bannerAlarm;
        public AlarmData BannerAlarm { get => _bannerAlarm; private set => SetProperty(ref _bannerAlarm, value); }

        // ── Overlay hộp thoại (thay MessageBox — xem DialogHost) ──
        private DialogRequest _currentDialog;
        public DialogRequest CurrentDialog
        {
            get => _currentDialog;
            private set
            {
                if (SetProperty(ref _currentDialog, value))
                {
                    OnPropertyChanged(nameof(DialogOpen));
                    OnPropertyChanged(nameof(DialogIsConfirm));
                }
            }
        }

        public bool DialogOpen => CurrentDialog != null;
        public bool DialogIsConfirm => CurrentDialog?.Kind == DialogKind.Confirm;

        /// <summary>Hộp thoại bị hộp mới đè lên — đóng hộp trên thì hiện lại hộp dưới. Không giữ lại thì hộp dưới mất nút bấm,
        /// nơi gọi nó chờ mãi (vd. trang Vision Edit kẹt "đang bận" tới khi tắt App).</summary>
        private readonly Stack<DialogRequest> _coveredDialogs = new Stack<DialogRequest>();

        private void ShowDialog(DialogRequest req)
        {
            if (CurrentDialog != null) _coveredDialogs.Push(CurrentDialog);
            CurrentDialog = req;
        }

        private void CloseDialog(bool result)
        {
            var closing = CurrentDialog;
            CurrentDialog = _coveredDialogs.Count > 0 ? _coveredDialogs.Pop() : null;
            closing?.Complete(result);
        }

        public ICommand ShowMainCommand { get; }
        public ICommand ShowManualCommand { get; }
        public ICommand ShowTeachingCommand { get; }
        public ICommand ShowSettingCommand { get; }
        public ICommand ShowVisionEditCommand { get; }
        public ICommand ShowAlarmCommand { get; }
        public ICommand ResetCommand { get; }
        public ICommand MuteCommand { get; }
        public ICommand ToggleAlarmPanelCommand { get; }
        public ICommand HideAlarmBannerCommand { get; }
        public ICommand ToggleLanguageCommand { get; }
        public ICommand DialogOkCommand { get; }
        public ICommand DialogYesCommand { get; }
        public ICommand DialogNoCommand { get; }
    }
}
