using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using ShieldCanAssy.Wpf.ViewModels;

namespace ShieldCanAssy.Wpf.Views
{
    public partial class ManualView : UserControl
    {
        public ManualView()
        {
            InitializeComponent();
        }

        /// <summary>
        /// Jog là giữ-để-chạy. Dùng sự kiện chuột chứ không dùng Command: Command chỉ bắn khi
        /// nhả chuột, sẽ thành "nhấn một cái chạy mãi" — với trục thật đó là đâm vào cữ.
        /// MouseLeave cũng dừng, phòng trường hợp kéo chuột ra khỏi nút rồi mới nhả.
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

        private static void StopJog(object sender)
        {
            var row = (sender as FrameworkElement)?.DataContext as AxisRowVm;
            row?.JogStop();
        }

        /// <summary>
        /// Nút "Mở phanh" cũng giữ-để-chạy như Jog: bấm nhanh (nhả trước 3s) là nghi thức bình
        /// thường, GIỮ đủ 3s là nhả phanh khẩn cấp kiểu người-chết (Tuấn yêu cầu, 2026-09-04) —
        /// xem AxisRowVm.BrakeHoldStart/BrakeHoldTick/BrakeHoldEnd. Cùng lý do dùng sự kiện chuột
        /// thay vì Command như Jog: Command Click chỉ bắn lúc NHẢ, không phân biệt được đang giữ
        /// hay đã buông.
        ///
        /// <para><b>CaptureMouse thay vì dựa vào MouseLeave</b> (Tuấn báo lỗi, 2026-09-09): nút
        /// nhỏ, chỉ cần con trỏ trượt ra khỏi vùng nút — dù chuột trái VẪN ĐANG giữ xuống — là
        /// MouseLeave bắn, coi như đã buông, phanh khoá lại ngay dù operator không hề nhả tay.
        /// Bắt capture lúc Down thì MouseUp vẫn bắn đúng chỗ dù con trỏ đã ra khỏi nút, không cần
        /// giữ con trỏ chính xác trên nút suốt 3+ giây. LostMouseCapture thay cho MouseLeave làm
        /// lưới an toàn khi capture bị hệ thống lấy lại bất ngờ (vẫn đúng tinh thần người-chết:
        /// mất kiểm soát tín hiệu buông tay là khoá lại ngay).</para>
        /// </summary>
        private void BrakeRelease_Down(object sender, MouseButtonEventArgs e)
        {
            (sender as UIElement)?.CaptureMouse();
            var row = (sender as FrameworkElement)?.DataContext as AxisRowVm;
            row?.BrakeHoldStart();
        }

        private void BrakeRelease_Up(object sender, MouseButtonEventArgs e)
        {
            (sender as UIElement)?.ReleaseMouseCapture();
            StopBrakeHold(sender);
        }

        private void BrakeRelease_LostCapture(object sender, MouseEventArgs e) => StopBrakeHold(sender);

        private static void StopBrakeHold(object sender)
        {
            var row = (sender as FrameworkElement)?.DataContext as AxisRowVm;
            row?.BrakeHoldEnd();
        }
    }
}
