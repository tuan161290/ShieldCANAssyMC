using System;
using System.IO;
using System.Reflection;

namespace ShieldCanAssy.SysConfig
{
    /// <summary>
    /// Đường dẫn dữ liệu chạy máy — khớp cây thư mục ở PLAN.md §8.
    ///
    /// Gốc mặc định là <c>D:\ShieldCanAssy</c> (tách khỏi ổ hệ thống để ghost/cài lại Windows
    /// không mất dữ liệu teach và log sản xuất). Máy phát triển thường không có ổ D: nên
    /// <see cref="Init"/> tự rơi về thư mục <c>_RunData</c> cạnh exe — nhờ vậy chạy thử ở đâu
    /// cũng được mà không phải sửa hằng số rồi quên sửa lại lúc bàn giao.
    /// </summary>
    public static class AppPaths
    {
        public const string DefaultRoot = @"D:\ShieldCanAssy";

        public static string Root { get; private set; }

        public static string ConfigDir   => Path.Combine(Root, "Config");
        public static string ModelDir    => Path.Combine(Root, "Model");
        public static string VisionDir   => Path.Combine(Root, "Vision");
        public static string RuntimeDir  => Path.Combine(Root, "Runtime");
        public static string DataDir     => Path.Combine(Root, "Data");
        public static string ImageNgDir  => Path.Combine(Root, "Image", "NG");
        public static string LogDir      => Path.Combine(Root, "Log");

        public static string SystemFile   => Path.Combine(ConfigDir, "system.json");
        public static string AlarmsFile   => Path.Combine(ConfigDir, "alarms.json");
        public static string UsersFile    => Path.Combine(ConfigDir, "users.json");
        public static string RunningFile  => Path.Combine(RuntimeDir, "running.json");

        /// <summary>Thư mục chứa exe — nơi tìm bản mẫu alarms.json khi Config chưa có.</summary>
        public static string ExeDir =>
            Path.GetDirectoryName(Assembly.GetEntryAssembly()?.Location
                                  ?? Assembly.GetExecutingAssembly().Location);

        /// <summary>true nếu đang chạy ở thư mục dự phòng cạnh exe (hiện lên status bar).</summary>
        public static bool UsingFallbackRoot { get; private set; }

        public static void Init(string overrideRoot = null)
        {
            if (!string.IsNullOrWhiteSpace(overrideRoot))
            {
                Root = overrideRoot;
                UsingFallbackRoot = false;
            }
            else if (CanUse(DefaultRoot))
            {
                Root = DefaultRoot;
                UsingFallbackRoot = false;
            }
            else
            {
                Root = Path.Combine(ExeDir ?? Environment.CurrentDirectory, "_RunData");
                UsingFallbackRoot = true;
            }

            foreach (var dir in new[] { ConfigDir, ModelDir, VisionDir, RuntimeDir, DataDir, ImageNgDir, LogDir })
            {
                try { Directory.CreateDirectory(dir); } catch { }
            }
        }

        private static bool CanUse(string root)
        {
            try
            {
                string drive = Path.GetPathRoot(root);
                if (!Directory.Exists(drive)) return false;
                Directory.CreateDirectory(root);
                return true;
            }
            catch { return false; }
        }
    }
}
