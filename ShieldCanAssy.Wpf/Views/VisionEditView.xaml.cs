using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using ShieldCanAssy.Utility.Log;
using ShieldCanAssy.Wpf.ViewModels;
using VM.Core;

namespace ShieldCanAssy.Wpf.Views
{
    public partial class VisionEditView : UserControl
    {
        public VisionEditView()
        {
            InitializeComponent();
            Loaded += VisionEditView_Loaded;
        }

        /// <summary>
        /// Bind control vào solution đang nạp. Có thể chưa có solution nào (VisionConnect vẫn
        /// đang nạp nền lúc app khởi động, hoặc đang chạy mô phỏng không có VisionMaster thật) —
        /// khi đó chỉ ghi log, không làm sập trang, vì đây là trang tiện ích chứ không nằm trên
        /// đường chạy Auto.
        /// </summary>
        private void VisionEditView_Loaded(object sender, System.Windows.RoutedEventArgs e)
        {
            // Mức Chậm/Nhanh dùng chung với trang Teaching — có thể vừa đổi ở đó.
            (DataContext as VisionCalibViewModel)?.RefreshJogFast();

            try
            {
                if (VmSolution.Instance != null)
                    ConfigControl.BindMultiProcedure();
            }
            catch (Exception ex)
            {
                LogWorker.Instance.RecordLog("Vision Edit: chưa bind được solution — " + ex.Message,
                    Utility.Log.LogLevel.Warning, "VisionEdit");
            }
        }

        /// <summary>
        /// Jog trên thanh công cụ là giữ-để-chạy, như trang Teaching/Manual: dùng sự kiện chuột chứ không dùng
        /// Command, vì Command chỉ bắn lúc nhả chuột và sẽ thành "nhấn một cái chạy mãi". MouseLeave cũng dừng,
        /// phòng khi kéo chuột ra khỏi nút rồi mới nhả.
        /// </summary>
        private void Jog_Down(object sender, MouseButtonEventArgs e)
        {
            var btn = sender as Button;
            var row = btn?.DataContext as AxisRowVm;
            if (row == null) return;

            int dir = (btn.Tag as string) == "-1" ? -1 : 1;
            row.JogStart(dir);
        }

        /// <summary>
        /// Nuốt cú bấm đơn, chỉ cho double-click đi tiếp — dùng cho hai nút Home Z1/Z2. Cùng cách
        /// với các nút chạy thử ở trang Teaching.
        /// </summary>
        private void RequireDoubleClick_PreviewMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
        {
            if (e.ClickCount != 2) e.Handled = true;
        }

        private void Jog_Up(object sender, MouseButtonEventArgs e) => StopJog(sender);

        private void Jog_Leave(object sender, MouseEventArgs e) => StopJog(sender);

        private static void StopJog(object sender)
        {
            var row = (sender as FrameworkElement)?.DataContext as AxisRowVm;
            row?.JogStop();
        }
    }
}
