namespace ShieldCanAssy.Wpf.Services
{
    /// <remarks><c>title</c> bỏ trống (null) thì <see cref="DialogHost"/> lấy tiêu đề mặc định theo ngôn ngữ đang chọn
    /// ("Thông báo" / "Cảnh báo" / "Lỗi" / "Xác nhận").</remarks>
    public interface IDialogService
    {
        void Info(string message, string title = null);
        void Warn(string message, string title = null);
        void Error(string message, string title = null);
        bool Confirm(string message, string title = null);
    }

    /// <summary>
    /// Đi qua <see cref="DialogHost"/> — overlay TOP-MOST ngay trong MainWindow — thay vì
    /// <c>MessageBox.Show</c> (mở cửa sổ Win32 riêng, làm MainWindow mất activation nên DWM ngừng
    /// vẽ Mica/Acrylic; xem doc-comment <see cref="DialogHost"/>). API giữ nguyên hệt cũ (kể cả
    /// <see cref="Confirm"/> vẫn chặn thread gọi và trả bool đồng bộ) nên mọi chỗ đang dùng
    /// <c>IDialogService</c> không phải sửa gì.
    /// </summary>
    public class DialogService : IDialogService
    {
        public void Info(string message, string title = null) => DialogHost.Info(message, title);
        public void Warn(string message, string title = null) => DialogHost.Warn(message, title);
        public void Error(string message, string title = null) => DialogHost.Error(message, title);
        public bool Confirm(string message, string title = null) => DialogHost.Confirm(message, title);
    }
}
