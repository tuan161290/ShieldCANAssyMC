using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

namespace Snap
{
    /// <summary>
    /// Vẽ từng trang của ShieldCanAssy ra PNG ở một khổ cửa sổ cho trước — không hiện cửa sổ nào,
    /// không chụp màn hình: dựng cây giao diện rồi RenderTargetBitmap. Control HwndHost của
    /// VisionMaster không vẽ được theo cách này (ra ô trống) — chỉ để xem bố cục.
    /// Dùng: Snap.exe W H outDir
    /// </summary>
    public static class Program
    {
        const string WpfBin = @"C:\Users\Tuan\OneDrive\Tuan\2026\Actro\SO1C86-SHIELD CAN ASSY MC\ShieldCANAssyMC\ShieldCanAssy.Wpf\bin\Debug\net48";
        static readonly string[] Dirs =
        {
            WpfBin,
            @"C:\Program Files\VisionMaster4.4.0\Development\V4.x\ComControls\Assembly",
            @"C:\Program Files\VisionMaster4.4.0\Applications\myLibs",
        };

        [STAThread]
        public static int Main(string[] args)
        {
            AppDomain.CurrentDomain.AssemblyResolve += (s, e) =>
            {
                string n = new AssemblyName(e.Name).Name;
                foreach (var d in Dirs)
                    foreach (var ext in new[] { ".dll", ".exe" })
                    {
                        string p = Path.Combine(d, n + ext);
                        if (File.Exists(p)) return Assembly.LoadFrom(p);
                    }
                return null;
            };
            Environment.CurrentDirectory = WpfBin;
            try { return Run(args); }
            catch (Exception ex)
            {
                File.WriteAllText(Path.Combine(args.Length > 2 ? args[2] : ".", "snap-error.txt"), ex.ToString());
                return 2;
            }
        }

        static double W, H;
        static string Out;
        static readonly StringBuilder Report = new StringBuilder();

        [MethodImpl(MethodImplOptions.NoInlining)]
        static int Run(string[] args)
        {
            W = double.Parse(args[0]);
            H = double.Parse(args[1]);
            Out = args[2];
            Directory.CreateDirectory(Out);

            var app = new ShieldCanAssy.Wpf.App();
            app.InitializeComponent();
            app.ShutdownMode = ShutdownMode.OnExplicitShutdown;

            ShieldCanAssy.Utility.UiThread.Init();
            ShieldCanAssy.SysConfig.AppPaths.Init();
            ShieldCanAssy.Utility.Log.LogWorker.Instance.Init(ShieldCanAssy.SysConfig.AppPaths.Root);
            ShieldCanAssy.Core.Infor.AlarmCatalog.Load();
            ShieldCanAssy.Core.Data.McDataManager.LoadSystemData();
            ShieldCanAssy.Core.Data.McDataManager.LoadRunningData();
            ShieldCanAssy.Core.Data.McDataManager.LoadRecentModel();
            if (!ShieldCanAssy.Core.Data.McDataManager.SysData.SimulationMode)
                throw new InvalidOperationException("system.json không ở chế độ mô phỏng — dừng, không nối máy thật");
            ShieldCanAssy.Core.Controller.McControl.Init();
            Pump(800);
            File.AppendAllText(Path.Combine(Out, "progress.txt"), "init ok\n");
            string model = Environment.GetEnvironmentVariable("SNAP_MODEL");
            if (!string.IsNullOrEmpty(model) && !ShieldCanAssy.Core.Data.McDataManager.LoadModel(model))
                throw new InvalidOperationException("không nạp được model " + model);
            Pump(300);

            File.AppendAllText(Path.Combine(Out, "progress.txt"), "model ok\n");
            var w = new ShieldCanAssy.Wpf.MainWindow();
            File.AppendAllText(Path.Combine(Out, "progress.txt"), "window ok\n");
            var root = (FrameworkElement)w.Content;
            object dc = w.DataContext;
            w.Content = null;
            root.DataContext = dc;
            var shell = (ShieldCanAssy.Wpf.ViewModels.ShellViewModel)dc;

            var host = new Border { Width = W, Height = H, Background = new SolidColorBrush(Color.FromRgb(0xF3, 0xF3, 0xF3)), Child = root };
            host.DataContext = dc;

            string only = args.Length > 3 ? args[3] : null;
            var pages = new List<Tuple<string, System.Windows.Input.ICommand, Action>>
            {
                Tuple.Create<string, System.Windows.Input.ICommand, Action>("1-main", shell.ShowMainCommand, null),
                Tuple.Create<string, System.Windows.Input.ICommand, Action>("1-main-dryrun", shell.ShowMainCommand,
                    () => ShieldCanAssy.Core.Controller.McControl.SetDryRun(true)),
                Tuple.Create<string, System.Windows.Input.ICommand, Action>("2-manual", shell.ShowManualCommand, null),
                Tuple.Create<string, System.Windows.Input.ICommand, Action>("3-teaching", shell.ShowTeachingCommand, null),
                Tuple.Create<string, System.Windows.Input.ICommand, Action>("4-setting", shell.ShowSettingCommand, null),
                Tuple.Create<string, System.Windows.Input.ICommand, Action>("5-visionedit", shell.ShowVisionEditCommand, null),
                Tuple.Create<string, System.Windows.Input.ICommand, Action>("6-alarm", shell.ShowAlarmCommand, null),
            };

            foreach (var p in pages)
            {
                if (only != null && !p.Item1.StartsWith(only)) continue;
                p.Item3?.Invoke();
                p.Item2.Execute(null);
                Pump(400);
                Layout(host);
                Pump(200);
                Layout(host);
                Save(host, p.Item1);
                Overflow(root, p.Item1);
            }

            // Trang Vision Edit — từng bước của Combobox
            if (only == null || "5-visionedit".StartsWith(only) || only.StartsWith("5"))
            {
                shell.ShowVisionEditCommand.Execute(null);
                Pump(300);
                var ve = FindVisual<ShieldCanAssy.Wpf.Views.VisionEditView>(root);
                var vevm = ve?.DataContext as ShieldCanAssy.Wpf.ViewModels.VisionCalibViewModel;
                if (vevm != null)
                    for (int i = 0; i < vevm.CalibSteps.Length; i++)
                    {
                        vevm.StepIndex = i;
                        Pump(200);
                        Layout(host);
                        Save(host, "5-visionedit-step" + (i + 1));
                        Overflow(root, "5-visionedit-step" + (i + 1));
                        var exps = new List<Expander>();
                        CollectVisual(ve, exps);
                        foreach (var x in exps) x.IsExpanded = true;
                        Pump(150);
                        Layout(host);
                        Save(host, "5-visionedit-step" + (i + 1) + "-open");
                        foreach (var x in exps) x.IsExpanded = false;
                        var btns = new List<Button>();
                        CollectVisual(ve, btns);
                        foreach (var b in btns)
                        {
                            if (!b.IsVisible || b.ActualWidth <= 0) continue;
                            var cp = FindVisual<ContentPresenter>(b);
                            if (cp == null) continue;
                            cp.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
                            double need = cp.DesiredSize.Width + b.Padding.Left + b.Padding.Right + b.BorderThickness.Left + b.BorderThickness.Right;
                            if (need > b.ActualWidth + 1)
                                Report.AppendLine(string.Format("  NUT-CAT step{0}: [{1}] cần {2:0} px, có {3:0} px", i + 1, b.Content, need, b.ActualWidth));
                        }
                        Layout(host);
                    }
            }

            File.WriteAllText(Path.Combine(Out, "report.txt"), Report.ToString(), Encoding.UTF8);
            KillVmChildren();
            Environment.Exit(0);
            return 0;
        }

        /// <summary>SDK VisionMaster đẻ vServerApp + AwakenGpuTool làm tiến trình CON; không tắt thì chúng treo lại
        /// và giữ exe của App. Chỉ tắt con của CHÍNH tiến trình này.</summary>
        static void KillVmChildren()
        {
            int me = System.Diagnostics.Process.GetCurrentProcess().Id;
            using (var s = new System.Management.ManagementObjectSearcher(
                "SELECT ProcessId, Name FROM Win32_Process WHERE ParentProcessId=" + me))
                foreach (System.Management.ManagementObject o in s.Get())
                {
                    string n = (string)o["Name"];
                    if (n != "vServerApp.exe" && n != "AwakenGpuTool.exe") continue;
                    try { System.Diagnostics.Process.GetProcessById(Convert.ToInt32(o["ProcessId"])).Kill(); } catch { }
                }
        }

        static void Layout(FrameworkElement host)
        {
            host.Measure(new Size(W, H));
            host.Arrange(new Rect(0, 0, W, H));
            host.UpdateLayout();
        }

        static void Pump(int ms)
        {
            var frame = new DispatcherFrame();
            var t = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(ms) };
            t.Tick += (s, e) => { t.Stop(); frame.Continue = false; };
            t.Start();
            Dispatcher.PushFrame(frame);
        }

        static void Save(FrameworkElement host, string name)
        {
            File.AppendAllText(Path.Combine(Out, "progress.txt"), "save " + name + "\n");
            var rtb = new RenderTargetBitmap((int)W, (int)H, 96, 96, PixelFormats.Pbgra32);
            rtb.Render(host);
            var enc = new PngBitmapEncoder();
            enc.Frames.Add(BitmapFrame.Create(rtb));
            using (var fs = File.Create(Path.Combine(Out, name + ".png"))) enc.Save(fs);
        }

        /// <summary>Phần tử nào đòi chỗ nhiều hơn chỗ được cấp (DesiredSize ép theo Min, so với RenderSize
        /// của ô cha) — tức là bị cắt hoặc tràn đè lên thứ khác.</summary>
        static void Overflow(FrameworkElement root, string page)
        {
            Report.AppendLine("== " + page);
            Walk(root, 0, page);
        }

        static void Walk(DependencyObject d, int depth, string page)
        {
            int n = VisualTreeHelper.GetChildrenCount(d);
            for (int i = 0; i < n; i++)
            {
                var c = VisualTreeHelper.GetChild(d, i);
                var fe = c as FrameworkElement;
                if (fe != null && fe.IsVisible)
                {
                    var parent = VisualTreeHelper.GetParent(fe) as FrameworkElement;
                    // layout clip = phần tử được arrange nhỏ hơn cỡ nó cần
                    var clip = LayoutInformation.GetLayoutClip(fe);
                    if (clip != null && !(parent is ScrollContentPresenter) && fe.ActualHeight > 0)
                    {
                        var b = clip.Bounds;
                        if (b.Height + 0.5 < fe.ActualHeight || b.Width + 0.5 < fe.ActualWidth)
                        {
                            Point o;
                            try { o = fe.TransformToAncestor(RootOf(fe)).Transform(new Point(0, 0)); }
                            catch { o = new Point(double.NaN, double.NaN); }
                            Report.AppendLine(string.Format("  CLIP {0} name={1} at ({2:0},{3:0}) size {4:0}x{5:0} visible {6:0}x{7:0} {8}",
                                fe.GetType().Name, fe.Name, o.X, o.Y, fe.ActualWidth, fe.ActualHeight, b.Width, b.Height, Hint(fe)));
                        }
                    }
                }
                Walk(c, depth + 1, page);
            }
        }

        static Visual RootOf(DependencyObject d)
        {
            Visual last = d as Visual;
            DependencyObject p = d;
            while ((p = VisualTreeHelper.GetParent(p)) != null) if (p is Visual) last = (Visual)p;
            return last;
        }

        static string Hint(FrameworkElement fe)
        {
            var tb = fe as TextBlock;
            if (tb != null) return "\"" + Trim(tb.Text) + "\"";
            var cc = fe as ContentControl;
            if (cc != null && cc.Content is string) return "\"" + Trim((string)cc.Content) + "\"";
            // chữ đầu tiên bên trong
            var t = FindVisual<TextBlock>(fe);
            return t == null ? "" : "~\"" + Trim(t.Text) + "\"";
        }

        static string Trim(string s) { s = (s ?? "").Replace("\n", " "); return s.Length > 50 ? s.Substring(0, 50) + "…" : s; }

        static void CollectVisual<T>(DependencyObject d, List<T> acc) where T : DependencyObject
        {
            int n = VisualTreeHelper.GetChildrenCount(d);
            for (int i = 0; i < n; i++)
            {
                var c = VisualTreeHelper.GetChild(d, i);
                if (c is T) acc.Add((T)c);
                CollectVisual(c, acc);
            }
        }

        static T FindVisual<T>(DependencyObject d) where T : DependencyObject
        {
            if (d == null) return null;
            int n = VisualTreeHelper.GetChildrenCount(d);
            for (int i = 0; i < n; i++)
            {
                var c = VisualTreeHelper.GetChild(d, i);
                if (c is T) return (T)c;
                var r = FindVisual<T>(c);
                if (r != null) return r;
            }
            return null;
        }
    }
}
