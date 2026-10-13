using System;
using System.Windows;
using System.Windows.Threading;
using ShieldCanAssy.Core.Lang;
using ShieldCanAssy.Utility.Log;

namespace ShieldCanAssy.Wpf.Services
{
    public enum DialogKind { Info, Warn, Error, Confirm }

    /// <summary>
    /// Một yêu cầu hộp thoại đang chờ người dùng bấm nút trên overlay (MainWindow.xaml). Giữ
    /// <see cref="Result"/> + một <see cref="DispatcherFrame"/> riêng — <see cref="Complete"/> do
    /// nút OK/Có/Không trong overlay gọi, <see cref="WaitForResult"/> do <see cref="DialogHost"/>
    /// gọi để CHẶN thread gọi (giống hệt MessageBox.Show) trong lúc vẫn bơm message loop, nên
    /// overlay vẫn vẽ/click bình thường.
    /// </summary>
    public sealed class DialogRequest
    {
        private readonly DispatcherFrame _frame = new DispatcherFrame();

        public DialogKind Kind { get; set; }
        public string Title { get; set; }
        public string Message { get; set; }
        public bool Result { get; private set; }

        public void Complete(bool result)
        {
            Result = result;
            _frame.Continue = false;
        }

        internal void WaitForResult() => Dispatcher.PushFrame(_frame);
    }

    /// <summary>
    /// Trạm trung chuyển hộp thoại — thay <c>MessageBox.Show</c> (mở một cửa sổ Win32 RIÊNG, làm
    /// MainWindow mất activation nên DWM ngừng vẽ Mica/Acrylic — xem <c>WindowBlurHelper</c>)
    /// bằng một overlay TOP-MOST NGAY TRONG MainWindow (khối cuối <c>MainWindow.xaml</c>, sau
    /// banner/bảng chi tiết alarm nên luôn vẽ đè lên trên cùng). Cùng kiến trúc với
    /// <c>AlarmManager</c>: hub tĩnh phát sự kiện, <c>ShellViewModel</c> (DataContext của
    /// MainWindow) lắng nghe rồi phơi ra property bindable cho XAML.
    ///
    /// <para><see cref="Info"/>/<see cref="Warn"/>/<see cref="Error"/>/<see cref="Confirm"/> vẫn
    /// CHẶN thread gọi tới khi người dùng bấm nút — giữ đúng API đồng bộ cũ của
    /// <see cref="IDialogService"/> nên KHÔNG cần sửa bất kỳ chỗ gọi nào (TeachingViewModel,
    /// VisionCalibViewModel...). Luôn marshal qua <c>Dispatcher.Invoke</c> (không phải
    /// <c>UiThread.Run</c>, vốn Post không chờ) để hoạt động đúng dù gọi từ luồng nền hay UI
    /// thread — <c>Dispatcher.Invoke</c> tự nhận ra đã ở đúng thread thì chạy thẳng, không cần
    /// tách nhánh riêng.</para>
    /// </summary>
    public static class DialogHost
    {
        public static event Action<DialogRequest> Requested;

        // title = null → tiêu đề mặc định theo ngôn ngữ đang chọn (DefaultTitle). Giá trị mặc định của tham số phải là
        // hằng lúc biên dịch nên không đặt thẳng chữ đã dịch ở đây được.
        public static void Info(string message, string title = null) => Show(DialogKind.Info, message, title);
        public static void Warn(string message, string title = null) => Show(DialogKind.Warn, message, title);
        public static void Error(string message, string title = null) => Show(DialogKind.Error, message, title);
        public static bool Confirm(string message, string title = null) => Show(DialogKind.Confirm, message, title);

        private static string DefaultTitle(DialogKind kind)
        {
            switch (kind)
            {
                case DialogKind.Warn: return Loc.T("Dialog.Warn");
                case DialogKind.Error: return Loc.T("Dialog.Error");
                case DialogKind.Confirm: return Loc.T("Dialog.Confirm");
                default: return Loc.T("Dialog.Info");
            }
        }

        private static bool Show(DialogKind kind, string message, string title)
        {
            if (title == null) title = DefaultTitle(kind);
            var req = new DialogRequest { Kind = kind, Message = message, Title = title };
            if (kind != DialogKind.Confirm) Record(kind, title, message, null);

            var dispatcher = Application.Current?.Dispatcher;
            if (dispatcher == null) return false; // chưa có UI (chưa từng xảy ra sau khi MainWindow hiện)

            dispatcher.Invoke(() =>
            {
                Requested?.Invoke(req);
                req.WaitForResult();
            });

            if (kind == DialogKind.Confirm) Record(kind, title, message, req.Result);
            return req.Result;
        }

        /// <summary>
        /// Mọi hộp thoại người vận hành thấy đều vào log: bấm OK là bảng biến mất, log thì còn để tra lại
        /// (Tuấn yêu cầu 2026-09-22). Hộp xác nhận ghi sau khi bấm, kèm câu trả lời.
        /// </summary>
        private static void Record(DialogKind kind, string title, string message, bool? answer)
        {
            var lines = (message ?? "").Replace("\r", "").Split(new[] { '\n' }, StringSplitOptions.RemoveEmptyEntries);
            string text = "Hộp thoại \"" + title + "\": " + string.Join(" | ", lines);
            if (answer.HasValue) text += answer.Value ? " → Có" : " → Không";

            var level = kind == DialogKind.Error ? LogLevel.Error
                      : kind == DialogKind.Warn ? LogLevel.Warning
                      : LogLevel.Info;
            LogWorker.Instance.RecordLog(text, level, "UI");
        }
    }
}
