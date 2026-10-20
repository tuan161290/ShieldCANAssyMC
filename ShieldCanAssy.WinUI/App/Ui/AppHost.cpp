#include "pch.h"
#include "Ui/AppHost.h"

#include <shellapi.h>
#include <thread>

#include "Base/Async.h"
#include "Base/Text.h"
#include "Communication/VisionConnect.h"
#include "Controller/McControl.h"
#include "Data/McDataManager.h"
#include "Infor/AlarmCatalog.h"
#include "Infor/AlarmCode.h"
#include "Infor/AlarmManager.h"
#include "SysConfig/AppPaths.h"
#include "Ui/UiUtil.h"
#include "Utility/Log/LogWorker.h"
#include "Utility/SingleInstanceGuard.h"
#include "Utility/UiThread.h"
#include "Vision/VisionMasterDevice.h"

namespace ui
{
    namespace
    {
        // Cùng tên mutex với bản WPF (App.xaml.cs): hai app không bao giờ cùng điều khiển phần cứng.
        constexpr char SingleInstanceMutexName[] =
            "Global\\ShieldCanAssy_SingleInstance_{9F3C2B41-6A7D-4E58-9C10-2B5E7A1D8C33}";

        AppOptions ParseCommandLine()
        {
            AppOptions options;
            int argc = 0;
            LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
            if (argv == nullptr) return options;
            for (int i = 1; i < argc; ++i)
            {
                std::wstring arg = argv[i];
                if (arg == L"--root" && i + 1 < argc) options.DataRoot = base::Narrow(argv[++i]);
                else if (arg == L"--shot" && i + 1 < argc) options.ScreenshotDir = base::Narrow(argv[++i]);
            }
            LocalFree(argv);
            return options;
        }

        AppOptions& MutableOptions()
        {
            static AppOptions options = ParseCommandLine();
            return options;
        }

        bool g_started = false;
        bool g_shutdown = false;
        bool g_ownsInstanceLock = false;
        std::atomic<bool> g_coreReady{ false };
        std::thread g_initThread;
    }

    const AppOptions& AppHost::Options() { return MutableOptions(); }

    bool AppHost::CoreReady() { return g_coreReady.load(); }

    bool AppHost::Startup()
    {
        using sca::LogLevel;
        using sca::LogWorker;
        const AppOptions& options = Options();

        // Core chuyển việc về luồng UI qua đây (không phụ thuộc WinUI). Gọi trên luồng UI: ghi nhận luồng này là UI.
        sca::UiThread::SetDispatcher([](std::function<void()> fn) { PostUi(std::move(fn)); });

        // Mutex một-bản-chạy: chỉ chế độ chụp ảnh (luôn mô phỏng, dữ liệu tạm) được bỏ qua. --root thôi thì VẪN giữ mutex
        // — system.json ở gốc khác có thể đặt SimulationMode=false, và khi đó app này sẽ điều khiển phần cứng.
        if (!options.ShotMode())
        {
            if (!sca::SingleInstanceGuard::TryLock(SingleInstanceMutexName))
            {
                sca::SingleInstanceGuard::BringExistingToFront();
                MessageBoxW(nullptr, L"Shield CAN Assy MC is already running (WPF or WinUI).", L"Shield CAN Assy MC",
                            MB_OK | MB_ICONEXCLAMATION);
                return false;
            }
            g_ownsInstanceLock = true;
        }

        sca::AppPaths::Init(options.DataRoot);
        LogWorker::Instance().Init(sca::AppPaths::Root());
        LogWorker::Instance().RecordLog("===== App start (WinUI) =====");
        if (sca::AppPaths::UsingFallbackRoot())
            LogWorker::Instance().RecordLog(std::string("Cannot use ") + sca::AppPaths::DefaultRoot + " — run data at " +
                                                base::PathToU8(sca::AppPaths::Root()),
                                            LogLevel::Warning);

        base::SetBackgroundErrorHandler([](const char* what) {
            LogWorker::Instance().RecordLog(std::string("Background task failed: ") + what, LogLevel::Error, "UI");
        });

        sca::AlarmCatalog::Load();
        sca::McDataManager::LoadSystemData();
        // Bản C++ chỉ hiển thị tiếng Anh (PORTING §3): không gọi Loc.SetLanguage; SysData.Language giữ nguyên trong file.
        sca::McDataManager::LoadRunningData();
        sca::McDataManager::LoadRecentModel();

        if (options.ShotMode())
        {
            // Chụp ảnh: KHÔNG BAO GIỜ chạm phần cứng hay VisionMaster, bất kể system.json ở gốc dữ liệu nói gì. Chỉ đổi trong
            // bộ nhớ (không lưu); gốc dữ liệu của chế độ này là thư mục tạm.
            sca::McDataManager::SysData()->SimulationMode = true;
            LogWorker::Instance().RecordLog("Screenshot mode: SimulationMode forced on", LogLevel::Warning, "UI");
        }
        else
        {
            // Thiết bị VisionMaster thật (SDK C++, trong tiến trình này). McControl::Init chọn FakeVision khi SimulationMode
            // (như bản C#), nên đăng ký ở đây không làm mô phỏng chạm VisionMaster. Đăng ký TRƯỚC McControl::Init.
            sca::VisionConnect::SetRealDeviceFactory([] {
                return sca::VisionMasterDevice::Create([] { return sca::McDataManager::SysData(); });
            });
        }

        g_started = true;

        // McControl::Init CHẶN trong lúc kết nối thiết bị (COM, Ethernet, nạp solution) — chạy trên luồng nền để cửa sổ
        // hiện ngay; MainWindow chờ CoreReady() rồi mới cho các trang đọc thiết bị.
        g_initThread = std::thread([] {
            SetThreadDescription(GetCurrentThread(), L"McControl::Init");
            try
            {
                sca::McControl::Init();
            }
            catch (const std::exception& ex)
            {
                // Lỗi driver/thiết bị không được chặn cả shell — thanh trạng thái đã hiện rõ thiết bị nào chưa kết nối.
                LogWorker::Instance().RecordLog(std::string("McControl::Init failed: ") + ex.what(), LogLevel::Error);
                sca::AlarmManager::Raise(sca::AlarmCode::ConfigInvalid, "System", ex.what());
            }
            catch (...)
            {
                LogWorker::Instance().RecordLog("McControl::Init failed: unknown exception", LogLevel::Error);
                sca::AlarmManager::Raise(sca::AlarmCode::ConfigInvalid, "System", "unknown exception");
            }
            g_coreReady = true;
        });
        return true;
    }

    void AppHost::Shutdown()
    {
        if (g_shutdown) return;
        g_shutdown = true;

        if (g_started)
        {
            // Init còn đang kết nối thì phải chờ nó xong — Shutdown giữa lúc Init đang dựng thiết bị là tranh chấp.
            if (g_initThread.joinable()) g_initThread.join();
            try
            {
                sca::LogWorker::Instance().RecordLog("===== App exit (WinUI) =====");
                sca::McControl::Shutdown();
                // DisposeResource của SDK VisionMaster: đúng một lần, sau khi các control ActiveX (OnClosing của các trang)
                // và thiết bị (McControl::Shutdown) đã giải phóng — bàn giao VM-WinUI3 §6.5.
                sca::VisionMasterDevice::DisposeSdk();
                sca::McDataManager::SaveRunningData();
                sca::AlarmManager::FlushCsv();
            }
            catch (...) {}

            sca::LogWorker::Instance().Shutdown();
        }
        if (g_ownsInstanceLock) sca::SingleInstanceGuard::Release();
    }

    void AppHost::OnUnhandledUiException(const std::string& message)
    {
        sca::LogWorker::Instance().RecordLog("Unhandled UI exception: " + message, sca::LogLevel::Error, "UI");
        // Phát dạng SỰ KIỆN: Raise trơn thì không nơi nào gỡ nguyên nhân, RESET không bao giờ xoá được.
        sca::AlarmManager::RaiseEvent(sca::AlarmCode::RoutineCrashed, "UI", message);
    }
}
