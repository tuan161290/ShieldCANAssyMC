#pragma once
// Vòng đời của app phía lõi máy: khởi động (mutex một-bản-chạy, đường dẫn, log, bảng alarm, dữ liệu, McControl) và tắt.
// Tách khỏi App.xaml.cpp để phần XAML chỉ còn việc dựng cửa sổ. Thứ tự theo App.xaml.cs của bản WPF; khác một chỗ:
// McControl::Init (CHẶN trong lúc kết nối thiết bị) chạy trên luồng nền để cửa sổ hiện ngay — xem CoreReady().
#include <string>

namespace ui
{
    struct AppOptions
    {
        /// <summary>--root &lt;thư mục&gt;: gốc dữ liệu thay cho D:\ShieldCanAssy (chạy thử không đụng dữ liệu máy).</summary>
        std::string DataRoot;
        /// <summary>--shot &lt;thư mục&gt;: chụp từng trang ra PNG rồi tự thoát; cửa sổ không giành focus. Chế độ này LUÔN
        /// mô phỏng (SimulationMode ép true trong bộ nhớ, không đăng ký VisionMaster thật) và không giữ mutex.</summary>
        std::string ScreenshotDir;

        bool ShotMode() const { return !ScreenshotDir.empty(); }
    };

    class AppHost
    {
    public:
        static const AppOptions& Options();

        /// <summary>Luồng UI. False = app phải thoát ngay (đã có một bản đang chạy — WPF hoặc C++).
        /// Trả về khi dữ liệu đã nạp; McControl::Init chạy tiếp trên luồng nền.</summary>
        static bool Startup();

        /// <summary>McControl::Init đã chạy xong (thành công hay lỗi). Trước đó KHÔNG được đụng con trỏ thiết bị của
        /// McControl (Io/Vision/Func/Safety/routine) hay Motion().AxisList() — Init đang dựng chúng trên luồng khác.
        /// McStatus, AlarmManager, McDataManager, LogWorker, Loc thì dùng được từ đầu.</summary>
        static bool CoreReady();

        /// <summary>Gọi được nhiều lần; chỉ lần đầu có tác dụng. Chờ luồng Init (nếu còn chạy) rồi mới
        /// McControl::Shutdown → VisionMasterDevice::DisposeSdk → lưu running.json → FlushCsv → tắt log → nhả mutex.
        /// Control ActiveX của VisionMaster phải được dispose TRƯỚC (MainWindow gọi OnClosing của các trang trước).</summary>
        static void Shutdown();

        /// <summary>Ngoại lệ lọt tới vòng lặp UI: ghi log + phát alarm dạng sự kiện, app chạy tiếp.</summary>
        static void OnUnhandledUiException(const std::string& message);
    };
}
