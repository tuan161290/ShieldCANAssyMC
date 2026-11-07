#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

#include <dwmapi.h>

#include "Ui/AppHost.h"
#include "Ui/UiUtil.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;

namespace winrt::ShieldCanAssy::implementation
{
    App::App()
    {
        // Đối tượng Xaml không được gọi InitializeComponent trong hàm dựng.
        // Xem https://github.com/microsoft/cppwinrt/tree/master/nuget#initializecomponent
        UnhandledException([](Windows::Foundation::IInspectable const&, UnhandledExceptionEventArgs const& e) {
            e.Handled(true);
            ui::AppHost::OnUnhandledUiException(ui::U8(e.Message()));
        });
    }

    void App::OnLaunched(LaunchActivatedEventArgs const&)
    {
        ui::InitDispatcher(Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread());

        if (!ui::AppHost::Startup())
        {
            Exit();
            return;
        }

        window = make<MainWindow>();
        if (ui::AppHost::Options().ScreenshotDir.empty())
        {
            // Giấu khỏi màn hình tới khi đã phóng to và vẽ xong dashboard (MainWindow::OnRootLoaded): không có khung xám cỡ
            // mặc định lúc mới mở. Vẫn Activate bình thường, XAML vẫn dựng và vẽ.
            HWND hwnd{};
            window.as<::IWindowNative>()->get_WindowHandle(&hwnd);
            BOOL cloak = TRUE;
            DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &cloak, sizeof cloak);
            window.Activate();
        }
        else
        {
            // Chụp màn hình tự động: máy phát triển là máy người dùng đang làm việc — cửa sổ đặt ngoài
            // màn hình và không giành focus.
            window.AppWindow().MoveAndResize({ -30000, -30000, 1600, 1000 });
            window.AppWindow().Show(false);
        }
    }
}
