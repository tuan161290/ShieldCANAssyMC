using System;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Input;
using Microsoft.Win32;
using ShieldCanAssy.Core.Communication;
using ShieldCanAssy.Core.Controller;
using ShieldCanAssy.Core.Data;
using ShieldCanAssy.Core.Lang;
using ShieldCanAssy.SysConfig;
using ShieldCanAssy.Utility.Log;
using ShieldCanAssy.Wpf.Mvvm;

namespace ShieldCanAssy.Wpf.ViewModels
{
    /// <summary>Trang Setting: tham số hệ thống và cấu hình 7 trục.</summary>
    public class SettingViewModel : ObservableObject
    {
        public SettingViewModel()
        {
            foreach (var cfg in McDataManager.SysData.Axes)
                AxisConfigs.Add(cfg);

            SaveCommand = new RelayCommand(() =>
            {
                McDataManager.SysData.ZSafe = ZSafe;
                McControl.Motion.ZSafe = ZSafe;
                SaveResult = McDataManager.SaveSystemData()
                    ? Loc.T("Setting.Saved")
                    : Loc.T("Setting.SaveFailed");
            });

            OpenDataFolderCommand = new RelayCommand(() =>
            {
                try { Process.Start("explorer.exe", AppPaths.Root); } catch { }
            });

            BrowseVisionSolutionCommand = new RelayCommand(() =>
            {
                var dlg = new OpenFileDialog
                {
                    Filter = Loc.T("Setting.SolutionFilter"),
                    CheckFileExists = true
                };
                if (dlg.ShowDialog() == true) VisionSolutionPath = dlg.FileName;
            });

            // Nạp lại ngay để có hiệu lực không cần khởi động lại app — khác với tên Flow/Output
            // (VisionConnect đọc lại mỗi lần trigger, không cần bước riêng), đường dẫn solution
            // chỉ được VmSolution đọc MỘT LẦN lúc Init nên phải chủ động gọi lại.
            ReloadVisionSolutionCommand = new RelayCommand(() =>
            {
                McDataManager.SaveSystemData();
                string refused = McControl.ReloadVisionSolution();
                VisionReloadResult = refused ?? Loc.T("Setting.ReloadRequested");
            });

            CheckGlobalVarsCommand = new RelayCommand(async () => await RunGlobalVarSyncAsync(false));
            CreateGlobalVarsCommand = new RelayCommand(async () => await RunGlobalVarSyncAsync(true));
            CheckFlowOutputsCommand = new RelayCommand(async () => await RunFlowOutputCheckAsync());

            // Chạy rà và tay hút chạy Auto chuyển từ trang Main sang đây (Tuấn 2026-10-08). Cùng lệnh McControl: chỉ CHỌN
            // chế độ, không chạy máy (vẫn phải qua nút Start cứng), có hiệu lực ngay không cần bấm Lưu, và bị từ chối khi
            // máy đang chạy — lý do hiện ở dòng Status.Message dưới nút.
            ToggleDryRunCommand = new RelayCommand(() => McControl.SetDryRun(!McControl.Status.DryRun));
            SetOnlyNozzleCommand = new RelayCommand<string>(p =>
            {
                McControl.SetOnlyNozzle(int.Parse(p));
                OnPropertyChanged(nameof(OnlyNozzle));
            });

            ZSafe = McDataManager.SysData.ZSafe;
        }

        public McStatus Status => McControl.Status;

        /// <summary>Tay hút dùng trong Auto: 0 cả hai, 1/2 chỉ một tay — xem SystemData.OnlyNozzle.</summary>
        public int OnlyNozzle => Sys.OnlyNozzle;

        public ICommand ToggleDryRunCommand { get; }
        public ICommand SetOnlyNozzleCommand { get; }

        /// <summary>
        /// Kiểm tra hoặc tạo biến toàn cục cho solution đang nạp. Tạo thì kiểm tra trước để hỏi xác
        /// nhận kèm số biến sẽ tạo và file .solw sẽ bị lưu.
        /// </summary>
        private async Task RunGlobalVarSyncAsync(bool apply)
        {
            GlobalVarReport = apply ? Loc.T("Setting.CheckingBeforeCreate") : Loc.T("Setting.Checking");
            try
            {
                if (apply)
                {
                    var dry = await McControl.SyncVisionGlobalVariablesAsync(false);
                    if (!string.IsNullOrEmpty(dry.Error)) { GlobalVarReport = dry.ToText(); return; }
                    int missing = dry.Count(EGlobalVarState.Missing);
                    if (missing == 0) { GlobalVarReport = Loc.T("Setting.NoMissingVars") + Environment.NewLine + dry.ToText(); return; }

                    var answer = MessageBox.Show(
                        Loc.F("Setting.CreateVarsConfirm", missing) +
                        Environment.NewLine + Sys.VisionSolutionPath + Environment.NewLine + Environment.NewLine +
                        Loc.T("Setting.CreateVarsConfirmTail"),
                        Loc.T("Setting.CreateVarsTitle"), MessageBoxButton.YesNo, MessageBoxImage.Question);
                    if (answer != MessageBoxResult.Yes) { GlobalVarReport = Loc.T("Setting.CreateVarsCancelled"); return; }
                    GlobalVarReport = Loc.F("Setting.CreatingVars", missing);
                }

                var report = await McControl.SyncVisionGlobalVariablesAsync(apply);
                GlobalVarReport = report.ToText();
            }
            catch (Exception ex)
            {
                GlobalVarReport = Loc.F("Setting.Error", ex.Message);
            }
        }

        /// <summary>
        /// Đối chiếu Output Params của ba Flow với hợp đồng. Chỉ đọc khai báo — không chạy Flow nào,
        /// nên không cần hỏi xác nhận như nút tạo biến (nút đó lưu đè file .solw).
        /// </summary>
        private async Task RunFlowOutputCheckAsync()
        {
            FlowOutputReport = Loc.T("Setting.ReadingFlowOutputs");
            try
            {
                var report = await McControl.CheckVisionFlowOutputsAsync();
                FlowOutputReport = report.ToText();
            }
            catch (Exception ex)
            {
                FlowOutputReport = Loc.F("Setting.Error", ex.Message);
            }
        }

        public SystemData Sys => McDataManager.SysData;
        public ObservableCollection<AxisConfig> AxisConfigs { get; } = new ObservableCollection<AxisConfig>();

        /// <summary>
        /// Bọc <c>Sys.VisionSolutionPath</c> qua property riêng thay vì bind thẳng — SystemData là
        /// POCO thuần, không tự báo PropertyChanged, nên nút Duyệt... đặt giá trị từ code sẽ
        /// KHÔNG hiện lên TextBox nếu bind thẳng vào Sys (chỉ gõ tay mới ăn, vì TextBox.Text tự
        /// đẩy ngược vào nguồn TwoWay mà không cần nguồn báo lại).
        /// </summary>
        public string VisionSolutionPath
        {
            get => Sys.VisionSolutionPath;
            set { if (Sys.VisionSolutionPath != value) { Sys.VisionSolutionPath = value; OnPropertyChanged(); } }
        }

        public string DataRoot => AppPaths.Root;
        public bool UsingFallbackRoot => AppPaths.UsingFallbackRoot;
        public bool AlarmCatalogLoaded => Core.Infor.AlarmCatalog.Loaded;

        private double _zSafe;
        public double ZSafe { get => _zSafe; set => SetProperty(ref _zSafe, value); }

        private string _saveResult;
        public string SaveResult { get => _saveResult; private set => SetProperty(ref _saveResult, value); }

        private string _visionReloadResult;
        public string VisionReloadResult { get => _visionReloadResult; private set => SetProperty(ref _visionReloadResult, value); }

        public ObservableCollection<LogData> LogList => LogWorker.Instance.LogList;

        public ICommand SaveCommand { get; }
        public ICommand OpenDataFolderCommand { get; }
        public ICommand BrowseVisionSolutionCommand { get; }
        public ICommand ReloadVisionSolutionCommand { get; }

        private string _globalVarReport;
        public string GlobalVarReport { get => _globalVarReport; private set => SetProperty(ref _globalVarReport, value); }

        public ICommand CheckGlobalVarsCommand { get; }
        public ICommand CreateGlobalVarsCommand { get; }

        private string _flowOutputReport;
        public string FlowOutputReport { get => _flowOutputReport; private set => SetProperty(ref _flowOutputReport, value); }

        public ICommand CheckFlowOutputsCommand { get; }
    }
}
