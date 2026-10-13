using System;
using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Shapes;
using ShieldCanAssy.Core.Infor;
using ShieldCanAssy.Wpf.ViewModels;

namespace ShieldCanAssy.Wpf.Views
{
    public partial class TeachingView : UserControl
    {
        public TeachingView()
        {
            InitializeComponent();

            // DataContext đã được gán tĩnh trong XAML (UserControl.DataContext) lúc
            // InitializeComponent() chạy xong, KHÔNG đổi lại sau đó — subscribe thẳng ở đây thay
            // vì DataContextChanged (event đó đã bắn xong trước khi constructor này chạy tới).
            if (DataContext is TeachingViewModel vm) vm.PropertyChanged += Vm_PropertyChanged;

            // ActualWidth/ActualHeight của Canvas chỉ có giá trị thật SAU khi layout xong — vẽ ở
            // constructor sẽ ra toàn 0.
            Loaded += (_, __) => RedrawTraversal();
        }

        /// <summary>
        /// Jog là giữ-để-chạy — chuyển từ ManualView sang đây cùng lúc với panel jog trục
        /// (2026-08-19). Dùng sự kiện chuột chứ không dùng Command: Command chỉ bắn khi nhả
        /// chuột, sẽ thành "nhấn một cái chạy mãi" — với trục thật đó là đâm vào cữ. MouseLeave
        /// cũng dừng, phòng trường hợp kéo chuột ra khỏi nút rồi mới nhả.
        /// </summary>
        private void Jog_Down(object sender, MouseButtonEventArgs e)
        {
            var btn = sender as Button;
            var row = btn?.DataContext as AxisRowVm;
            if (row == null) return;

            int dir = (btn.Tag as string) == "-1" ? -1 : 1;
            row.JogStart(dir);
        }

        private void Jog_Up(object sender, MouseButtonEventArgs e) => StopJog(sender);

        private void Jog_Leave(object sender, MouseEventArgs e) => StopJog(sender);

        /// <summary>
        /// Các nút chạy thẳng tới vị trí tuyệt đối (Về gốc/Tới Pick/Tới Place/Tới T1-T2 Trigger)
        /// di chuyển trục thật — Tuấn yêu cầu 2026-09-05 phải double-click mới chạy, tránh chạm
        /// nhầm. Nuốt sự kiện ở pha Preview khi chưa đủ cú nhấp thứ 2: Button chỉ vào trạng thái
        /// "đang nhấn" (tiền đề để bắn Click lúc nhả chuột) nếu MouseLeftButtonDown không bị
        /// Handled trước đó, nên chặn cú nhấp đơn ở đây khiến Click không bao giờ bắn — chỉ lọt
        /// qua khi ClickCount đạt 2 trong cùng một double-click thật (đúng ngưỡng thời gian/
        /// khoảng cách double-click của Windows).
        /// </summary>
        private void RequireDoubleClick_PreviewMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
        {
            if (e.ClickCount != 2) e.Handled = true;
        }

        private static void StopJog(object sender)
        {
            var row = (sender as FrameworkElement)?.DataContext as AxisRowVm;
            row?.JogStop();
        }

        // Kích thước lưới minh hoạ CỐ ĐỊNH, không theo Rows/Cols thật của tray đang chọn — mục
        // đích chỉ là giúp hình dung nhanh 2 mode ngay cạnh RadioButton, tray thật có thể tới
        // hàng chục ô và vẽ đúng tỉ lệ sẽ rối (nhiều mũi tên chéo dài) thay vì trực quan.
        private const int PreviewRows = 2;
        private const int PreviewCols = 4;

        private void Vm_PropertyChanged(object sender, PropertyChangedEventArgs e)
        {
            // IsZigZagMode/IsRowMajorMode đổi theo cặp (RadioButton) — chỉ cần theo dõi một
            // trong hai, TeachingViewModel.SetScanOrder luôn báo cả hai cùng lúc.
            if (e.PropertyName == nameof(TeachingViewModel.IsZigZagMode)) RedrawTraversal();
        }

        private void TraversalCanvas_SizeChanged(object sender, SizeChangedEventArgs e) => RedrawTraversal();

        /// <summary>
        /// Vẽ minh hoạ thứ tự quét ô trên lưới mẫu cố định <see cref="PreviewRows"/>×
        /// <see cref="PreviewCols"/>: ô đánh số thứ tự + mũi tên nối các ô liên tiếp trong hành
        /// trình, khớp cách người dùng minh hoạ (RowMajor tuần tự, Serpentine zig zag). Vẽ bằng
        /// code-behind vì vị trí pixel từng ô phụ thuộc kích thước THẬT của Canvas lúc layout,
        /// không tính tĩnh được trong XAML.
        /// </summary>
        private void RedrawTraversal()
        {
            if (!(DataContext is TeachingViewModel vm)) return;
            TraversalCanvas.Children.Clear();

            double w = TraversalCanvas.ActualWidth, h = TraversalCanvas.ActualHeight;
            if (w <= 0 || h <= 0) return;

            double cw = w / PreviewCols, ch = h / PreviewRows;
            var order = vm.IsZigZagMode ? EScanOrder.Serpentine : EScanOrder.RowMajor;

            Point CenterOf(int r, int c) => new Point(c * cw + cw / 2, r * ch + ch / 2);

            // (row,col) -> vị trí trong hành trình — cần chiều thuận index->điểm để vẽ mũi tên nối
            // đúng các ô LIÊN TIẾP theo thứ tự ghé, không phải theo thứ tự hàng/cột hình học.
            var points = new Point[PreviewRows * PreviewCols];
            for (int r = 0; r < PreviewRows; r++)
                for (int c = 0; c < PreviewCols; c++)
                    points[TeachingViewModel.VisitIndex(r, c, PreviewCols, order)] = CenterOf(r, c);

            for (int r = 0; r < PreviewRows; r++)
            {
                for (int c = 0; c < PreviewCols; c++)
                {
                    var rect = new Rectangle { Width = cw, Height = ch, Stroke = Brushes.Gray, StrokeThickness = 1 };
                    Canvas.SetLeft(rect, c * cw);
                    Canvas.SetTop(rect, r * ch);
                    TraversalCanvas.Children.Add(rect);
                }
            }

            for (int i = 0; i < points.Length; i++)
            {
                if (i > 0) DrawArrow(points[i - 1], points[i]);

                var label = new TextBlock { Text = (i + 1).ToString(), FontSize = 12, Foreground = Brushes.DarkSlateGray };
                label.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
                Canvas.SetLeft(label, points[i].X - label.DesiredSize.Width / 2);
                Canvas.SetTop(label, points[i].Y - label.DesiredSize.Height / 2);
                TraversalCanvas.Children.Add(label);
            }
        }

        private void DrawArrow(Point from, Point to)
        {
            var dir = to - from;
            double len = dir.Length;
            if (len < 1) return;
            var unit = new Vector(dir.X / len, dir.Y / len);

            // Rút ngắn 2 đầu để mũi tên không đè lên số — không rút thì đường thẳng chọc thẳng vào
            // giữa chữ số, rối mắt trên lưới nhiều ô.
            double trim = Math.Max(0, Math.Min(len / 2 - 2, 14));
            var start = from + unit * trim;
            var end = to - unit * trim;

            TraversalCanvas.Children.Add(new Line
            {
                X1 = start.X, Y1 = start.Y, X2 = end.X, Y2 = end.Y,
                Stroke = Brushes.SteelBlue, StrokeThickness = 1.5
            });

            double angle = Math.Atan2(unit.Y, unit.X);
            const double headLen = 7, headAngle = Math.PI / 7;
            var p1 = end - new Vector(Math.Cos(angle - headAngle), Math.Sin(angle - headAngle)) * headLen;
            var p2 = end - new Vector(Math.Cos(angle + headAngle), Math.Sin(angle + headAngle)) * headLen;
            TraversalCanvas.Children.Add(new Polygon
            {
                Points = new PointCollection { end, p1, p2 },
                Fill = Brushes.SteelBlue
            });
        }
    }
}
