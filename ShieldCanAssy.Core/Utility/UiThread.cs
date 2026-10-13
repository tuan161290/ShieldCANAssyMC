using System;
using System.Threading;

namespace ShieldCanAssy.Utility
{
    /// <summary>
    /// Marshal lời gọi về UI thread. Các tầng nền (motion, I/O, routine) chạy trên thread pool
    /// nhưng cập nhật dữ liệu đang bind lên WPF, nên mọi thao tác chạm vào collection bind
    /// (BindingList/ObservableCollection) đều phải đi qua <see cref="Run"/>.
    ///
    /// BẮT BUỘC gọi <see cref="Init"/> sớm trên UI thread (App.OnStartup). Nếu không gọi,
    /// Run() âm thầm chạy thẳng trên thread gọi nó — vô hại phần lớn thời gian vì nhiều nơi
    /// gọi vốn đã ở UI thread, nhưng ném NotSupportedException ngay khi một continuation chạy
    /// trên thread pool chạm tới collection đang bind.
    /// </summary>
    public static class UiThread
    {
        private static SynchronizationContext _ui;

        public static void Init() => _ui = SynchronizationContext.Current;

        /// <summary>Chạy ngay nếu đã ở UI thread, ngược lại Post về UI thread (không chờ).</summary>
        public static void Run(Action action)
        {
            if (action == null) return;

            if (_ui == null || SynchronizationContext.Current == _ui)
            {
                action();
                return;
            }
            _ui.Post(_ => action(), null);
        }
    }
}
