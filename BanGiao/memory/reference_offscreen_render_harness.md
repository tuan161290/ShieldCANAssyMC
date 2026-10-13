---
name: reference-offscreen-render-harness
description: "Vẽ các trang WPF ra PNG ở khổ bất kỳ bằng chương trình phụ (RenderTargetBitmap trong tiến trình, không chụp màn hình) — chạy được; nhưng dựng MainView/VisionEditView là kéo vServerApp lên, phải dọn sau mỗi lượt"
metadata:
  node_type: memory
  type: reference
  originSessionId: e083c7ae-0deb-4155-be33-3b7bec1d9c4b
  modified: 2026-09-25T08:45:39.897Z
---

Kiểm bố cục ở khổ màn máy thật (1280×1024, trừ taskbar → thử 1280×976) mà không cần Tuấn chụp:
chương trình phụ net48 x64 trong scratchpad (25/09 đặt ở `scratchpad\snap\`, Snap.csproj + Program.cs),
tham chiếu `ShieldCanAssy.Wpf.exe` + `ShieldCanAssy.Core.dll` với `Private=false`, AssemblyResolve trỏ về
`ShieldCanAssy.Wpf\bin\Debug\net48` rồi hai thư mục VisionMaster. Trình tự: `new App().InitializeComponent()`
(lấy resource), rồi đúng các bước của `App.OnStartup` (UiThread, AppPaths, LogWorker, AlarmCatalog, 3 lần Load,
**chặn nếu SimulationMode = false**), `McControl.Init()`; tạo `MainWindow` nhưng KHÔNG Show — tách `w.Content`
ra, gán lại DataContext, bọc trong Border W×H, gọi `shell.ShowXxxCommand` rồi Measure/Arrange/RenderTargetBitmap.
Bơm Dispatcher bằng DispatcherFrame + DispatcherTimer. Control VisionMaster vẫn vẽ ra được (không phải HwndHost).
Env `SNAP_MODEL=DEFAULT` → `McDataManager.LoadModel` sau Init: mô phỏng mặc định chỉ có tray 3 hàng, máy thật 5 hàng.
Đây KHÔNG phải kiểu chụp màn hình mà [[feedback-no-self-screenshot]] cấm — nó chạy được vì không đụng desktop.

**Bẫy (mất 20 phút ngày 25/09):** dựng MainView/VisionEditView là SDK VisionMaster đẻ ra `vServerApp.exe` +
`AwakenGpuTool.exe` làm tiến trình CON. Chương trình phụ không bao giờ đóng cửa sổ nên không dọn chúng: thoát
xong nó thành xác (HasExited=True, ~1300 handle) và **khoá `ShieldCanAssy.Wpf.exe` trong bin** — build sản phẩm
lỗi MSB3021 — còn lượt vẽ sau treo ở bước bơm Dispatcher. Cách dọn: tắt đúng những tiến trình
`vServerApp`/`AwakenGpuTool` có `ParentProcessId` là PID chương trình phụ (kiểm bằng `Win32_Process`), rồi chờ
vài phút cho xác tự biến mất. Không đụng tiến trình nào khác. Muốn build chương trình phụ lúc xác còn giữ
file thì `-p:OutDir=bin2\`. Bản cuối (25/09) tự làm việc này trước `Environment.Exit` qua `System.Management`
(`Win32_Process WHERE ParentProcessId = PID mình`), KHÔNG gọi `McControl.Shutdown` (treo/văng) — chạy sạch.

**Đo nút cắt chữ:** đừng so `Button.ActualWidth` — WPF xếp nút theo đúng bề rộng chữ rồi mới cắt khi vẽ, nên
nó luôn "đủ". Đo thẳng bằng `FormattedText` trong PowerShell (Segoe UI 14, cộng đệm 22 px của FlatButtonStyle)
rồi so với bề rộng cột trong ảnh.
