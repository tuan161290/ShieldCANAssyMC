using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Windows;
using ShieldCanAssy.Core.Controller;
using ShieldCanAssy.Core.Data;
using ShieldCanAssy.Core.Infor;
using ShieldCanAssy.SysConfig;
using ShieldCanAssy.Utility;
using ShieldCanAssy.Utility.Log;

namespace ShieldCanAssy.Wpf
{
    public partial class App : Application
    {
        private const string SingleInstanceMutexName =
            "Global\\ShieldCanAssy_SingleInstance_{9F3C2B41-6A7D-4E58-9C10-2B5E7A1D8C33}";

        /// <summary>
        /// HIKROBOT VisionMaster 4.4.0 SDK — tham chiếu ở ShieldCanAssy.Core.csproj/.Wpf.csproj
        /// đặt <c>Private=false</c> (không copy DLL vào thư mục exe, xem lý do ở csproj), nên lúc
        /// chạy thật phải tự trỏ về đúng thư mục cài SDK. Đăng ký SỚM, trước bất kỳ code nào có
        /// thể chạm tới kiểu VM.Core/VMControls — <see cref="McControl.Init"/> ở chế độ không mô
        /// phỏng là chỗ đầu tiên chạm tới.
        /// </summary>
        private static readonly string VisionMasterSdkDir =
            @"C:\Program Files\VisionMaster4.4.0\Development\V4.x\ComControls\Assembly";

        /// <summary>Thư mục thư viện giao diện của VisionMaster. Một số DLL mà hàm tạo biến toàn
        /// cục của SDK kéo theo (vd. <c>VM.WatchDog</c>) chỉ nằm ở đây, không có trong thư mục
        /// Assembly của SDK — tìm ở đây sau cùng.</summary>
        private static readonly string VisionMasterLibsDir =
            @"C:\Program Files\VisionMaster4.4.0\Applications\myLibs";

        private static readonly Dictionary<string, Assembly> _resolvedVisionAssemblies = new Dictionary<string, Assembly>();

        private static void RegisterVisionMasterAssemblyResolve()
        {
            AppDomain.CurrentDomain.AssemblyResolve += (sender, args) =>
            {
                string name = new AssemblyName(args.Name).Name;

                Assembly cached;
                if (_resolvedVisionAssemblies.TryGetValue(name, out cached)) return cached;

                string path = Path.Combine(VisionMasterSdkDir, name + ".dll");
                if (!File.Exists(path)) path = Path.Combine(VisionMasterLibsDir, name + ".dll");
                Assembly loaded = File.Exists(path) ? Assembly.LoadFrom(path) : null;
                _resolvedVisionAssemblies[name] = loaded;
                return loaded;
            };
        }

        protected override void OnStartup(StartupEventArgs e)
        {
            base.OnStartup(e);

            RegisterVisionMasterAssemblyResolve();

            // BẮT BUỘC gọi sớm và trên UI thread. Toàn bộ Core dùng UiThread.Run để marshal cập
            // nhật dữ liệu bind-WPF từ thread nền (vòng quét motion/IO, LogWorker, AlarmManager).
            // Không gọi Init() thì Run() âm thầm chạy thẳng trên thread gọi — vô hại phần lớn
            // thời gian, nhưng ném NotSupportedException ngay khi một continuation trên thread
            // pool chạm vào ObservableCollection đang bind.
            UiThread.Init();

            if (!SingleInstanceGuard.TryLock(SingleInstanceMutexName))
            {
                SingleInstanceGuard.BringExistingToFront();
                MessageBox.Show("Ứng dụng đang chạy rồi.", "Shield CAN Assy MC",
                    MessageBoxButton.OK, MessageBoxImage.Exclamation);
                Shutdown();
                return;
            }

            AppPaths.Init();
            LogWorker.Instance.Init(AppPaths.Root);
            LogWorker.Instance.RecordLog("===== Khởi động ứng dụng =====");
            if (AppPaths.UsingFallbackRoot)
                LogWorker.Instance.RecordLog(
                    "Không dùng được " + AppPaths.DefaultRoot + " — dữ liệu chạy máy đặt tại " + AppPaths.Root,
                    Utility.Log.LogLevel.Warning);

            AlarmCatalog.Load();
            McDataManager.LoadSystemData();
            Core.Lang.Loc.SetLanguage(Core.Lang.Loc.Parse(McDataManager.SysData?.Language));
            McDataManager.LoadRunningData();
            McDataManager.LoadRecentModel();

            try
            {
                McControl.Init();
            }
            catch (Exception ex)
            {
                // Lỗi driver/thiết bị không được chặn cả shell không mở được — status bar đã
                // hiển thị rõ thiết bị nào chưa kết nối.
                LogWorker.Instance.RecordLog("McControl.Init lỗi: " + ex, Utility.Log.LogLevel.Error);
                AlarmManager.Raise(AlarmCode.ConfigInvalid, "System", ex.Message);
            }

            DispatcherUnhandledException += (_, args) =>
            {
                LogWorker.Instance.RecordLog("Ngoại lệ không xử lý: " + args.Exception, Utility.Log.LogLevel.Error);
                args.Handled = true;

                // Lỗi nằm hẳn trong khung ảnh của VisionMaster (VmRenderControl) — chỉ là phần hiển thị, không đụng máy.
                // 30/09: bấm nút đồng hồ trên thanh trạng thái của khung ảnh, SDK ném NullReferenceException trong
                // ShowTimeDialog, App dừng chu trình ngay lúc Z1 sắp hạ xuống hút. Ghi log, máy chạy tiếp.
                var site = args.Exception.TargetSite?.DeclaringType?.Namespace ?? "";
                if (site.StartsWith("VMControls", StringComparison.Ordinal))
                {
                    LogWorker.Instance.RecordLog("Lỗi bên trong khung ảnh VisionMaster (" + args.Exception.Message +
                        ") — bỏ qua, không dừng máy", Utility.Log.LogLevel.Warning, "UI");
                    return;
                }

                // Phát dạng SỰ KIỆN: Raise trơn thì không nơi nào gỡ nguyên nhân, RESET không bao giờ xoá được — 30/09
                // phải tắt mở lại App.
                AlarmManager.RaiseEvent(AlarmCode.RoutineCrashed, "UI", args.Exception.Message);
            };

            // Chạy `ShieldCanAssy.Wpf.exe --selftest` để kiểm tra hồi quy tầng Core mà không mở
            // giao diện. Kết quả ghi ra %Root%\selftest.txt, mã thoát 0 = tất cả đạt.
            if (Array.Exists(e.Args, a => string.Equals(a, "--selftest", StringComparison.OrdinalIgnoreCase)))
            {
                int code = SelfTest.Run();
                McControl.Shutdown();
                AlarmManager.FlushCsv();
                LogWorker.Instance.Shutdown();
                SingleInstanceGuard.Release();
                Environment.Exit(code);
                return;
            }

            new MainWindow().Show();
        }

        protected override void OnExit(ExitEventArgs e)
        {
            try
            {
                LogWorker.Instance.RecordLog("===== Thoát ứng dụng =====");
                McControl.Shutdown();
                McDataManager.SaveRunningData();
                AlarmManager.FlushCsv();
            }
            catch { }

            LogWorker.Instance.Shutdown();
            SingleInstanceGuard.Release();
            base.OnExit(e);
        }
    }
}
