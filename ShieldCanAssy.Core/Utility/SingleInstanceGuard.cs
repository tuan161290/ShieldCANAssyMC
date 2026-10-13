using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;

namespace ShieldCanAssy.Utility
{
    /// <summary>
    /// Chặn chạy hai bản app cùng lúc. Với máy sản xuất đây không phải tiện ích mà là
    /// yêu cầu an toàn: hai process cùng mở COM port / cùng phát lệnh chuyển động là
    /// tình huống không kiểm soát được.
    /// </summary>
    public static class SingleInstanceGuard
    {
        private static Mutex _mutex;

        public static bool TryLock(string mutexName)
        {
            _mutex = new Mutex(true, mutexName, out bool createdNew);
            if (!createdNew)
            {
                _mutex.Dispose();
                _mutex = null;
            }
            return createdNew;
        }

        public static void Release()
        {
            if (_mutex == null) return;
            try { _mutex.ReleaseMutex(); } catch { }
            _mutex.Dispose();
            _mutex = null;
        }

        public static void BringExistingToFront()
        {
            try
            {
                var current = Process.GetCurrentProcess();
                foreach (var p in Process.GetProcessesByName(current.ProcessName))
                {
                    if (p.Id == current.Id) continue;
                    if (p.MainWindowHandle == IntPtr.Zero) continue;
                    ShowWindow(p.MainWindowHandle, SW_RESTORE);
                    SetForegroundWindow(p.MainWindowHandle);
                    return;
                }
            }
            catch { }
        }

        private const int SW_RESTORE = 9;

        [DllImport("user32.dll")]
        private static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

        [DllImport("user32.dll")]
        private static extern bool SetForegroundWindow(IntPtr hWnd);
    }
}
