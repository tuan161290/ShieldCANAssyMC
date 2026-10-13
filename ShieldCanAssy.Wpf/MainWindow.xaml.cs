using System;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;

namespace ShieldCanAssy.Wpf
{
    /// <summary>
    /// Frameless (WindowChrome) + Mica/Acrylic backdrop Windows 11 — xem WindowBlurHelper. Port
    /// từ Ama_Actro_HousingCorrosion.Wpf (dự án WPF chị em, cùng công ty).
    /// </summary>
    public partial class MainWindow : Window
    {
        public MainWindow()
        {
            InitializeComponent();

            // Cỡ lúc KHÔNG phóng to — chạm đúp thanh tiêu đề là về cỡ này — không được lớn hơn vùng làm việc.
            // Để nguyên 1280×1024 trên màn 1280×1024 thì đáy cửa sổ, cả thanh trạng thái, lọt xuống dưới
            // taskbar (Tuấn báo kèm ảnh 2026-09-25).
            var work = SystemParameters.WorkArea;
            if (Width > work.Width) Width = work.Width;
            if (Height > work.Height) Height = work.Height;

            SourceInitialized += (_, __) =>
            {
                WindowBlurHelper.TryEnableMica(this);
                // Cửa sổ frameless (WindowStyle=None) + WindowChrome không tự giới hạn đúng vùng
                // Maximized theo màn hình hiện tại trên máy nhiều màn hình — cửa sổ có thể tràn ra
                // ngoài biên (lớn hơn cả 1 màn hình, đè lên taskbar). Hook WM_GETMINMAXINFO (giải
                // pháp chuẩn cho lỗi này, khớp Ama_Actro_HousingCorrosion.Wpf) để tự tính đúng vị
                // trí/kích thước Maximized theo WorkArea của màn hình chứa cửa sổ.
                HwndSource.FromHwnd(new WindowInteropHelper(this).Handle)?.AddHook(WindowProc);
            };
        }

        private static IntPtr WindowProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
        {
            const int WM_GETMINMAXINFO = 0x0024;
            if (msg == WM_GETMINMAXINFO)
            {
                WmGetMinMaxInfo(hwnd, lParam);
                handled = true;
            }
            return IntPtr.Zero;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct POINT { public int X; public int Y; }

        [StructLayout(LayoutKind.Sequential)]
        private struct MINMAXINFO
        {
            public POINT ptReserved;
            public POINT ptMaxSize;
            public POINT ptMaxPosition;
            public POINT ptMinTrackSize;
            public POINT ptMaxTrackSize;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct RECT { public int Left; public int Top; public int Right; public int Bottom; }

        [StructLayout(LayoutKind.Sequential)]
        private struct MONITORINFO
        {
            public int cbSize;
            public RECT rcMonitor;
            public RECT rcWork;
            public int dwFlags;
        }

        [DllImport("user32.dll")]
        private static extern IntPtr MonitorFromWindow(IntPtr handle, int flags);

        [DllImport("user32.dll")]
        private static extern bool GetMonitorInfo(IntPtr hMonitor, ref MONITORINFO lpmi);

        private const int MONITOR_DEFAULTTONEAREST = 2;

        private static void WmGetMinMaxInfo(IntPtr hwnd, IntPtr lParam)
        {
            MINMAXINFO mmi = (MINMAXINFO)Marshal.PtrToStructure(lParam, typeof(MINMAXINFO));
            IntPtr monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            if (monitor != IntPtr.Zero)
            {
                MONITORINFO monitorInfo = new MONITORINFO { cbSize = Marshal.SizeOf(typeof(MONITORINFO)) };
                GetMonitorInfo(monitor, ref monitorInfo);
                RECT workArea = monitorInfo.rcWork;
                RECT monitorArea = monitorInfo.rcMonitor;
                mmi.ptMaxPosition.X = Math.Abs(workArea.Left - monitorArea.Left);
                mmi.ptMaxPosition.Y = Math.Abs(workArea.Top - monitorArea.Top);
                mmi.ptMaxSize.X = Math.Abs(workArea.Right - workArea.Left);
                mmi.ptMaxSize.Y = Math.Abs(workArea.Bottom - workArea.Top);
            }
            Marshal.StructureToPtr(mmi, lParam, true);
        }

        /// <summary>Kéo di chuyển cửa sổ / double-click để Maximize-Restore — thay thanh tiêu đề
        /// Windows mặc định (đã tắt qua WindowStyle="None" + WindowChrome CaptionHeight=0).</summary>
        private void TitleBar_MouseDown(object sender, MouseButtonEventArgs e)
        {
            if (e.ChangedButton != MouseButton.Left) return;

            if (e.ClickCount == 2)
            {
                SendSysCommand(WindowState == WindowState.Maximized ? SC_RESTORE : SC_MAXIMIZE);
            }
            else
            {
                DragMove();
            }
        }

        private void MinimizeButton_Click(object sender, RoutedEventArgs e) => SendSysCommand(SC_MINIMIZE);

        private void MaximizeButton_Click(object sender, RoutedEventArgs e) =>
            SendSysCommand(WindowState == WindowState.Maximized ? SC_RESTORE : SC_MAXIMIZE);

        private void CloseButton_Click(object sender, RoutedEventArgs e) => Close();

        [DllImport("user32.dll")]
        private static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

        private const uint WM_SYSCOMMAND = 0x0112;
        private const int SC_MINIMIZE = 0xF020;
        private const int SC_MAXIMIZE = 0xF030;
        private const int SC_RESTORE = 0xF120;

        /// <summary>
        /// Gửi thẳng WM_SYSCOMMAND — ĐÚNG message mà nút minimize/maximize/restore THẬT của
        /// Windows gửi khi bấm. Set trực tiếp WindowState = Minimized bằng code (cách cũ) khiến
        /// cửa sổ WindowStyle="None" ẩn/hiện TỨC THÌ, không chạy animation "genie" thu về taskbar
        /// như app khác — WPF không đi qua đường native ShowWindow có animation cho trường hợp
        /// này. Gửi WM_SYSCOMMAND để DefWindowProc xử lý y hệt như khi bấm nút thật, animation
        /// theo đúng cấu hình hệ thống (Settings > Accessibility > Visual effects).
        /// </summary>
        private void SendSysCommand(int command)
        {
            var hwnd = new WindowInteropHelper(this).Handle;
            if (hwnd != IntPtr.Zero) PostMessage(hwnd, WM_SYSCOMMAND, (IntPtr)command, IntPtr.Zero);
        }

        /// <summary>
        /// Bật Mica/Acrylic backdrop (Windows 11) qua DWM API — port từ
        /// Ama_Actro_HousingCorrosion.Wpf.MainWindow.xaml.cs.WindowBlurHelper (dự án WPF chị em,
        /// cùng công ty), vốn là bản gốc mà comment cũ trong Palette.xaml của app này từng nhắc
        /// tới nhưng chưa từng thật sự port qua.
        ///
        /// <para>Composition target của WPF tự vẽ nền ĐẶC ở tầng dưới Window.Background dù Brush
        /// đã "Transparent" — phải tắt hẳn bằng CompositionTarget.BackgroundColor thì Mica mới lộ
        /// ra được.</para>
        ///
        /// <para>DwmExtendFrameIntoClientArea (margin -1 cả 4 cạnh) — BẮT BUỘC để DWM composite
        /// backdrop vào toàn bộ client area thay vì chỉ phần title bar do chính nó vẽ. Trước khi
        /// cửa sổ này chuyển sang frameless (WindowStyle="None"), gọi lệnh này từng gây dải màu
        /// caption hệ thống đè lên viền trên/dưới vì cửa sổ vẫn giữ title bar chuẩn của OS — giờ
        /// đã frameless (WindowChrome GlassFrameThickness="-1" trong XAML cũng tự gọi lệnh này,
        /// gọi lại ở đây theo đúng cách bản gốc làm, không gây xung đột) nên an toàn.</para>
        ///
        /// <para>MainWindow.xaml khai Background="Transparent" NGAY TỪ ĐẦU (không còn
        /// ShellBackgroundBrush đặc) — đổi Window.Background bằng code SAU KHI cửa sổ đã dựng
        /// xong (khung hình đầu tiên đã composite với nền đặc) không đủ để bật lại kênh alpha cho
        /// DWM blend. Khai Transparent ngay trong XAML để khung hình ĐẦU TIÊN đã có kênh alpha.</para>
        /// </summary>
        private static class WindowBlurHelper
        {
            [DllImport("dwmapi.dll", PreserveSig = true)]
            private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int value, int size);

            [DllImport("dwmapi.dll", PreserveSig = true)]
            private static extern int DwmExtendFrameIntoClientArea(IntPtr hwnd, ref MARGINS margins);

            [StructLayout(LayoutKind.Sequential)]
            private struct MARGINS { public int Left, Right, Top, Bottom; }

            private const int DWMWA_USE_IMMERSIVE_DARK_MODE = 20;
            private const int DWMWA_SYSTEMBACKDROP_TYPE = 38;
            private const int DWMSBT_TRANSIENTWINDOW = 3; // Acrylic — blur rõ nhất, thử trước
            private const int DWMSBT_MAINWINDOW = 2;       // Mica — fallback nếu Acrylic không nhận

            // Windows 10 (máy lắp ráp là Win10 22H2 — Tuấn muốn Acrylic, 2026-10-07): DWMWA_SYSTEMBACKDROP_TYPE chỉ có từ
            // Win11 22621, Win10 dùng accent policy của SetWindowCompositionAttribute (không có tài liệu). Dùng BLURBEHIND, KHÔNG
            // dùng ACRYLICBLURBEHIND: trên máy 218 Acrylic chỉ hiện lúc kéo cửa sổ, đứng yên hay phóng to là về nền đặc dù
            // Transparency effects đang bật (Tuấn báo 2026-10-07); blur thường hiện đều, và không làm cửa sổ giật khi kéo.
            [DllImport("user32.dll")]
            private static extern int SetWindowCompositionAttribute(IntPtr hwnd, ref WindowCompositionAttribData data);

            [StructLayout(LayoutKind.Sequential)]
            private struct WindowCompositionAttribData { public int Attribute; public IntPtr Data; public int SizeOfData; }

            [StructLayout(LayoutKind.Sequential)]
            private struct AccentPolicy { public int AccentState; public int AccentFlags; public uint GradientColor; public int AnimationId; }

            private const int WCA_ACCENT_POLICY = 19;
            private const int ACCENT_ENABLE_BLURBEHIND = 3;

            /// <summary>Màu phủ lớp nền (lớp 1 trong 3 lớp Nền — Card — Control): xám sáng 60%, vẽ bằng Window.Background trên
            /// lớp blur của DWM nên Win10 và Win11 cùng một tông.</summary>
            private static readonly Color BackdropTint = Color.FromArgb(0x99, 0xF3, 0xF3, 0xF3);

            /// <summary>Đặt accent cho Win10. Tint nằm ở Window.Background, BLURBEHIND không dùng GradientColor.</summary>
            private static bool SetAccent(IntPtr hwnd, int state)
            {
                var accent = new AccentPolicy { AccentState = state, AccentFlags = 2 };
                int size = Marshal.SizeOf(accent);
                IntPtr ptr = Marshal.AllocHGlobal(size);
                try
                {
                    Marshal.StructureToPtr(accent, ptr, false);
                    var data = new WindowCompositionAttribData { Attribute = WCA_ACCENT_POLICY, Data = ptr, SizeOfData = size };
                    return SetWindowCompositionAttribute(hwnd, ref data) != 0;
                }
                finally { Marshal.FreeHGlobal(ptr); }
            }

            public static void TryEnableMica(Window window)
            {
                var hwnd = new WindowInteropHelper(window).Handle;
                if (hwnd == IntPtr.Zero) return;

                var hwndSource = HwndSource.FromHwnd(hwnd);
                if (hwndSource?.CompositionTarget != null)
                    hwndSource.CompositionTarget.BackgroundColor = Colors.Transparent;

                var margins = new MARGINS { Left = -1, Right = -1, Top = -1, Bottom = -1 };
                DwmExtendFrameIntoClientArea(hwnd, ref margins);

                // Ép light — không đặt cờ này DWM tự chọn theo app-mode mặc định của Windows,
                // trả về tông nền TỐI nếu máy đang ở Dark app-mode, lệch hẳn với theme sáng của app.
                int useDarkMode = 0;
                DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, ref useDarkMode, sizeof(int));

                int backdrop = DWMSBT_TRANSIENTWINDOW;
                int hr = DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, ref backdrop, sizeof(int));
                if (hr != 0)
                {
                    backdrop = DWMSBT_MAINWINDOW;
                    hr = DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, ref backdrop, sizeof(int));
                }

                // Win11 cũ hơn 22621 và Win10: blur qua accent policy.
                if (hr != 0 && SetAccent(hwnd, ACCENT_ENABLE_BLURBEHIND)) hr = 0;

                if (hr == 0)
                {
                    // Tint bán trong suốt — KHÔNG dùng Brushes.Transparent (0% alpha) thẳng:
                    // không có gì để DWM blend màu sáng lên trên, kết quả phụ thuộc hoàn toàn nền
                    // phía sau (có thể tối/không đúng theme sáng của app).
                    window.Background = new SolidColorBrush(BackdropTint);
                }
                else
                {
                    // Không bật được kiểu nào — XAML khai Transparent ngay từ đầu nên PHẢI tự trả về
                    // nền đặc ở đây, không thì cửa sổ sẽ trong suốt thật (nhìn xuyên ra desktop) vì
                    // không có DWM backdrop nào bù lại.
                    window.Background = new SolidColorBrush(Color.FromRgb(0xF3, 0xF3, 0xF3));
                }
            }
        }

        private void Button_Click(object sender, RoutedEventArgs e)
        {

        }
    }
}
