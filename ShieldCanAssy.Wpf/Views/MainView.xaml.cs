using System;
using System.Collections.Generic;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Threading;
using ShieldCanAssy.Core.Controller;
using ShieldCanAssy.Core.Data;
using ShieldCanAssy.Core.Infor;
using ShieldCanAssy.Utility.Log;
using VM.Core;
using VMControls.Interface;
using VMControls.WPF.Release;

namespace ShieldCanAssy.Wpf.Views
{
    public partial class MainView : UserControl
    {
        private DispatcherTimer _visionBindTimer;
        private string _loggedVisionDiagnostic;

        /// <summary>Nút làm trục chạy phải bấm đúp (quy ước 2026-09-26) — nút Init.</summary>
        private void RequireDoubleClick_PreviewMouseLeftButtonDown(object sender, System.Windows.Input.MouseButtonEventArgs e)
        {
            if (e.ClickCount != 2) e.Handled = true;
        }

        /// <summary>Con trượt công tắc AUTO/MANUAL sang nửa ứng với chế độ ĐANG áp dụng (Tag bind Status.Mode, Tuấn
        /// 2026-10-13) — không theo cú bấm, nên McControl từ chối AUTO thì nó đứng yên. Trượt khi chế độ đổi, đặt thẳng khi
        /// đổi cỡ.</summary>
        private void PlaceModeThumb(bool animate)
        {
            var t = (TranslateTransform)ModeThumb.RenderTransform;
            double x = Equals(ModeThumb.Tag, EMcMode.Manual) ? ModeSwitchTrack.ActualWidth / 2 : 0;
            if (!animate)
            {
                t.BeginAnimation(TranslateTransform.XProperty, null);
                t.X = x;
                return;
            }
            t.BeginAnimation(TranslateTransform.XProperty,
                new DoubleAnimation(x, TimeSpan.FromMilliseconds(150)) { EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut } });
        }

        private bool _logShown = true;

        /// <summary>Ẩn/hiện card Nhật ký (Tuấn 06/10): LogHost co về 0 / giãn lại bề rộng của LogCard, card mờ dần theo,
        /// thẻ dọc LogTab hiện ra khi ẩn. Bấm lại giữa chừng thì chạy ngược từ chỗ đang dừng.</summary>
        private void ToggleLog_Click(object sender, RoutedEventArgs e)
        {
            _logShown = !_logShown;
            var duration = TimeSpan.FromMilliseconds(250);
            var ease = new CubicEase { EasingMode = EasingMode.EaseInOut };

            LogHost.BeginAnimation(WidthProperty,
                new DoubleAnimation(_logShown ? LogCard.Width : 0, duration) { EasingFunction = ease });
            LogCard.BeginAnimation(OpacityProperty, new DoubleAnimation(_logShown ? 1 : 0, duration));

            var tab = new DoubleAnimation(_logShown ? 0 : 1, duration);
            if (_logShown)
                tab.Completed += (_, __) => { if (_logShown) LogTab.Visibility = Visibility.Collapsed; };
            else
                LogTab.Visibility = Visibility.Visible;
            LogTab.BeginAnimation(OpacityProperty, tab);
        }

        public MainView()
        {
            InitializeComponent();

            var list = LogWorker.Instance.LogList;
            LogBox.ItemsSource = list;

            // Tự cuộn lên dòng mới nhất — LogWorker chèn dòng mới ở ĐẦU danh sách (mới nhất lên
            // trên), nên "mới nhất" giờ là index 0 chứ không phải phần tử cuối như trước.
            list.CollectionChanged += (_, e) =>
            {
                if (e.Action != NotifyCollectionChangedAction.Add) return;
                if (list.Count == 0) return;
                LogBox.ScrollIntoView(list[0]);
            };

            Loaded += MainView_Loaded;
            Unloaded += MainView_Unloaded;

            // Trang Main sống suốt đời App nên giữ handler này không rò rỉ.
            DependencyPropertyDescriptor.FromProperty(TagProperty, typeof(Border))
                .AddValueChanged(ModeThumb, (_, __) => PlaceModeThumb(true));
            ModeSwitchTrack.SizeChanged += (_, __) => PlaceModeThumb(false);

            foreach (var r in new[] { Tool1Render, Tool2Render, TopDownRender })
                r.Loaded += (s, __) => ApplyRenderLook((VmRenderControl)s);
        }

        /// <summary>Khung ảnh nào đang hiện thanh công cụ — mặc định ẩn hết (Tuấn 2026-10-07).</summary>
        private readonly HashSet<VmRenderControl> _toolbarShown = new HashSet<VmRenderControl>();
        private string _loggedRenderLookError;

        /// <summary>Nền khung ảnh CAM = màu card nhìn thấy trên máy (card trắng 70% trên nền mờ, đo từ ảnh chụp màn hình
        /// Tuấn gửi 2026-10-07). SetBackground của SDK chỉ nhận màu đặc, không trong suốt được.</summary>
        private const string RenderBackground = "#F1F1F1";

        /// <summary>
        /// Nền đặc màu card thay cho nền caro mặc định của VmRenderControl, và ẩn/hiện thanh công cụ theo nút ở góc card
        /// (Tuấn 2026-10-07). Gọi lại sau mỗi lần gắn module — không rõ SDK có trả về mặc định khi đổi ModuleSource không.
        /// Lỗi từ control VisionMaster chỉ ghi log một lần, không để lọt ra ngoài (sự cố 30/09).
        /// </summary>
        private void ApplyRenderLook(VmRenderControl render)
        {
            try
            {
                render.SetBackground(RenderBackground);
                render.SetRenderToolbarVisible(_toolbarShown.Contains(render));
            }
            catch (Exception ex)
            {
                if (_loggedRenderLookError == ex.Message) return;
                _loggedRenderLookError = ex.Message;
                LogWorker.Instance.RecordLog("Main: không đặt được nền/thanh công cụ khung ảnh CAM — " + ex.Message,
                    Utility.Log.LogLevel.Warning, "Vision");
            }
        }

        /// <summary>Cắt nội dung khung ảnh CAM theo góc bo của viền (RenderHolderStyle) — Border của WPF không tự cắt con.</summary>
        private void HolderContent_SizeChanged(object sender, SizeChangedEventArgs e)
        {
            var content = (FrameworkElement)sender;
            var holder = content.Parent as Border;
            double r = holder == null ? 0 : Math.Max(0, holder.CornerRadius.TopLeft - holder.BorderThickness.Left);
            content.Clip = new RectangleGeometry(new Rect(e.NewSize), r, r);
        }

        /// <summary>Bấm hàng tiêu đề bảng ở khung CAM bottom-up: đổi giữa Lệch/Đích và Đích tách phần (Tuấn 2026-10-07).
        /// Khung top-down không có bảng tách nên bấm không làm gì.</summary>
        private void ReadoutHeader_Click(object sender, System.Windows.Input.MouseButtonEventArgs e)
        {
            if ((sender as FrameworkElement)?.DataContext is ReadoutCard card && card.CanToggle)
                (DataContext as ViewModels.MainViewModel)?.ToggleBreakdown();
        }

        private void ToggleRenderToolbar_Click(object sender, RoutedEventArgs e)
        {
            var render = (sender as FrameworkElement)?.Tag as VmRenderControl;
            if (render == null) return;
            if (!_toolbarShown.Remove(render)) _toolbarShown.Add(render);
            ApplyRenderLook(render);
        }

        /// <summary>
        /// Bind 3 <see cref="VmRenderControl"/> vào module đặt ở trang Setting (mặc định cả Flow AlignBottom_Tool1,
        /// AlignBottom_Tool2 và LocateTop) để hiện ảnh CAM do chính VisionMaster vẽ (kèm kết quả nhận dạng), thay ảnh
        /// Bitmap chụp lại thủ công. Solution nạp NỀN lúc khởi động (McControl.LoadVisionSolution) nên rất có thể
        /// CHƯA xong khi trang Main — trang mặc định — vừa hiện lên; thử lại mỗi giây tới khi
        /// bind được cả ba rồi dừng hẳn. Mỗi lần mở lại trang Main thì xét lại — đổi tên module ở Setting là ăn ngay.
        /// </summary>
        private void MainView_Loaded(object sender, RoutedEventArgs e)
        {
            // KHÔNG chặn theo SimulationMode — VisionEditView (đang chạy tốt) cũng không chặn,
            // chỉ dựa vào VmSolution.Instance != null. SDK khởi tạo instance tĩnh của nó độc lập
            // với cờ mô phỏng của app; chặn thêm ở đây từng khiến control không bao giờ được thử
            // bind, kể cả khi VmSolution đã sẵn sàng.
            _visionBindTimer = new DispatcherTimer(DispatcherPriority.Background)
            {
                Interval = TimeSpan.FromSeconds(1)
            };
            _visionBindTimer.Tick += (_, __) => TryBindRenderControls();
            _visionBindTimer.Start();
            TryBindRenderControls();
        }

        private void MainView_Unloaded(object sender, RoutedEventArgs e)
        {
            _visionBindTimer?.Stop();
            _visionBindTimer = null;
        }

        private void TryBindRenderControls()
        {
            // Chỉ để RENDER hình ảnh sống (kèm nhận dạng) trên trang Main, không ảnh hưởng logic Auto. Module của từng
            // khung đặt ở trang Setting (trống = cả Flow) — xem VisionOutputNames.RenderPath.
            var n = McDataManager.SysData.VisionOutputs;
            var views = new[]
            {
                Tuple.Create(Tool1Render, n.FlowAlignBottomTool1, n.RenderTool1),
                Tuple.Create(Tool2Render, n.FlowAlignBottomTool2, n.RenderTool2),
                Tuple.Create(TopDownRender, n.FlowLocateTop, n.RenderTopDown),
            };
            // Chỉ gắn SAU khi McControl nạp xong solution: nạp xong, VisionMasterDevice.LoadSolution tắt callback kết quả
            // của mọi module, gắn trước đó thì bị tắt theo (Tuấn 30/09: khung chỉ có nền caro). Nạp lại thì gắn lại.
            int load = McControl.VisionLoadCount;
            bool ready = load > 0 && VmSolution.Instance != null;

            bool all = ready;
            string diag = ready ? "" : ", chờ nạp solution";
            if (ready)
                foreach (var v in views)
                {
                    // Ô ở Setting ghi đường dẫn đầy đủ: "T1" = cả Flow T1, "T1.Module" = module trong Flow T1 (Tuấn 30/09).
                    string path = VisionOutputNames.RenderPath(v.Item2, v.Item3);
                    bool ok = BindOne(v.Item1, path, load);
                    if (ok) ApplyRenderLook(v.Item1);
                    all &= ok;
                    diag += $", '{path}'={(ok ? "đã bind" : "KHÔNG thấy")}";
                }

            // Log một lần cho mỗi bộ tên — đủ để biết kẹt ở đâu (chưa nạp xong solution, hay sai tên module), không log
            // lặp lại mỗi giây gây rác Nhật ký. Đổi tên ở Setting thì log lại.
            if (diag != _loggedVisionDiagnostic)
            {
                _loggedVisionDiagnostic = diag;
                LogWorker.Instance.RecordLog("Main: khung ảnh CAM" + diag, Utility.Log.LogLevel.Info, "Vision");
            }

            if (all && _visionBindTimer != null)
            {
                _visionBindTimer.Stop();
                _visionBindTimer = null;
            }
        }

        /// <summary>Gán module <paramref name="path"/> cho khung; đã bind đúng đường dẫn đó trong lần nạp solution đó rồi thì
        /// thôi (Tag giữ lần nạp + đường dẫn, đổi ở Setting hay nạp lại thì bind lại). Trả false khi solution chưa có module đó.</summary>
        private static bool BindOne(VmRenderControl render, string path, int load)
        {
            string key = load + "|" + path;
            if (render.ModuleSource != null && Equals(render.Tag, key)) return true;

            IVmModule module;
            try { module = VmSolution.Instance[path] as IVmModule; }
            catch { module = null; }       // SDK ném lỗi khi không có đường dẫn đó
            if (module == null) return false;

            EnableResultCallbacks(module);
            render.ModuleSource = module;
            render.Tag = key;
            return true;
        }

        /// <summary>
        /// Khung ảnh chỉ vẽ được module ĐANG BẬT callback kết quả (mẫu GetResultControl của SDK bật trước khi gán
        /// ModuleSource). Gắn cả Flow thì Display của Flow lấy ảnh và hình từ các module con, nên bật hết module trong Flow.
        /// Trước 30/09 khung chỉ có ảnh sau khi mở trang Vision Edit, vì cấu hình VisionMaster ở trang đó tự bật lại callback.
        /// </summary>
        private static void EnableResultCallbacks(IVmModule module)
        {
            try
            {
                var pro = module as VmProcedure;
                if (pro != null)
                {
                    var subs = new List<VmModule>();
                    pro.GetAllModule(subs);
                    foreach (var m in subs) m.EnableResultCallback();
                    pro.EnableResultCallback();
                }
                else (module as VmModule)?.EnableResultCallback();
            }
            catch (Exception ex)
            {
                LogWorker.Instance.RecordLog("Main: không bật được callback kết quả cho khung ảnh CAM — " + ex.Message,
                    Utility.Log.LogLevel.Warning, "Vision");
            }
        }
    }
}
