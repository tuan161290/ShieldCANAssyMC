# Bàn giao: ShieldCanAssy bản WinUI 3 C++

Ngày lập: 2026-10-18
Phạm vi: thư mục `ShieldCanAssy.WinUI\`. Đây là bản port của app WPF/C# `ShieldCanAssy.Core` + `ShieldCanAssy.Wpf` sang C++20 + WinUI 3,
cùng thư viện `ServoControl` (Panasonic A6SF, Modbus RTU) viết lại bằng C++ ở `C:\PGM\ServoControl\Cpp\`.
Người đọc: Tuấn, lập trình viên tiếp tục phát triển và đưa app lên máy lắp ráp.

Bản WPF giữ nguyên, không sửa gì. Nó là đặc tả hành vi của bản này và vẫn là bản chạy sản xuất.

---

## 1. Kết luận

1. **Lõi máy port xong, chạy được không cần phần cứng.** Đã port: dữ liệu (system.json, model, running.json, alarm), log,
   Fastech Ezi-SERVO/Ezi-IO, Panasonic A6SF, VisionMaster, máy trạng thái, an toàn, về gốc/Init, chu trình Auto, chạy rà, hiệu chuẩn
   và thao tác tay. Kiểm tự động: CoreTests 2056 phép kiểm, ServoTests 296, đều đạt (mục 7).
2. **Đủ 6 trang giao diện**: Main, Manual, Teaching, Vision Edit, Setting, Alarm. Chữ hiển thị tiếng Anh (PORTING §3). Đã xem ảnh
   chụp từng trang ở chế độ mô phỏng.
3. **Dùng chung dữ liệu với bản WPF.** Đọc và ghi cùng file, cùng định dạng. Cùng mutex một-bản-chạy, nên hai app không thể chạy cùng
   lúc (mục 6).
4. **Chưa chạy trên phần cứng thật lần nào.** Chưa thử Fastech, Panasonic, Ezi-IO và VisionMaster thật. Chạy thử trên máy phải theo
   đúng trình tự ở mục 10.

---

## 2. Môi trường

| Hạng mục | Giá trị |
|---|---|
| Visual Studio | VS 2026 Community (18), workload "Desktop development with C++" + "WinUI application development" |
| Toolset | v145 = MSVC 14.51 |
| Windows SDK | 10.0.28000.0 (min 10.0.17763.0) |
| Windows App SDK | 1.8.260921001, self-contained, unpackaged |
| C++/WinRT | 2.0.250303.1 |
| VisionMaster | 4.4 (`C:\Program Files\VisionMaster4.4.0`). Header/lib của SDK C++ lấy từ `Development\V4.x`, DLL lấy từ `Applications\PublicFile\x64` (nạp trễ) |
| Fastech | `EziMOTIONPlusE.dll`, lấy từ `C:\PGM\ShieldCANAssyMC\Lib\` |
| Thư viện ngoài | `ThirdParty\nlohmann` (nlohmann/json) |

---

## 3. Cấu trúc

```
ShieldCanAssy.WinUI\
├─ ShieldCanAssy.WinUI.sln     ServoControl + Core + CoreTests (+ App)
├─ Core\                       lõi máy, thư viện tĩnh C++20, không phụ thuộc WinRT/XAML (namespace sca; Base\ là namespace base)
│  ├─ Base\                    chuỗi UTF-8, Delay/stop_token, Event, WhenAll, RunDetached
│  ├─ Utility, SysConfig, Infor, Lang, Data   log, đường dẫn, alarm, chữ (StringsEn.inc), system/model/running data
│  ├─ Fastech\                 nạp động EziMOTIONPlusE.dll (chữ ký lấy từ P/Invoke của C#), EziAxisStatus
│  ├─ Communication\           IAxis/IIoDevice/IVisionDevice, FastechAxis, PanasonicAxis, IoConnect, VisionConnect, Sim\Fake*
│  ├─ Controller\              McControl, McFunction, SafetyMonitor, TwoHandGate, Routine_Origin/Auto/Calib/DryCycle, VmCycle
│  └─ Vision\                  VisionMasterDevice (SDK C++ iMVS-6000PlatformSDK)
├─ App\                        app WinUI 3 (C++/WinRT): MainWindow + 6 trang *Page.xaml, Styles.xaml
│  └─ Ui\                      AppHost (khởi động, tham số dòng lệnh), VmPane/AxHost (ActiveX VisionMaster trong popup),
│                              Dialogs, JogControl, Controls, TrayCard, AlarmTable, phần riêng từng trang (*PageParts, ManualPageAxisRow…)
├─ Tests\CoreTests\            kiểm thử lõi (console, chỉ dùng thiết bị giả)
├─ ThirdParty\, tools\         check.ps1 (kiểm cú pháp 1 file), build-exe.ps1 / build-app-copy.ps1 (build bản sao), gen-strings.ps1
└─ build\                      đầu ra (không đưa vào kho)

C:\PGM\ServoControl\Cpp\      ServoControl C++ (namespace servo): ServoService, Modbus RTU tự viết trên Win32 COM, FakeA6sfPort;
                              Tests\ServoTests. Xem README.md ở đó (mục "Differences" và "Not verified").
```

Quy ước port xem `PORTING.md`. Tóm tắt:
- Giữ nguyên tên C#.
- `async`/`CancellationToken` thành hàm chặn nhận `std::stop_token`.
- Chuỗi là UTF-8.
- Không có binding: UI đọc trạng thái mỗi 100 ms.
- Port trung thành từng nhánh. Bug rõ ràng của C# vẫn giữ và đánh dấu `TODO(port)`.

Lớp controller có thêm quy tắc riêng ở `CONTROLLER-NOTES.md`. Đặc tả giao diện ở `UI-SPEC.md`.

---

## 4. Build và chạy

```powershell
$msb = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
cd C:\PGM\ShieldCANAssyMC\ShieldCanAssy.WinUI
& $msb ShieldCanAssy.WinUI.sln -t:CoreTests -p:Configuration=Release -p:Platform=x64 -m    # ServoControl + Core + CoreTests
& $msb App\ShieldCanAssy.vcxproj -restore -p:Configuration=Release -p:Platform=x64 -m     # app
build\x64\Release\CoreTests\CoreTests.exe                                                 # mã thoát 0 = đạt hết
& $msb C:\PGM\ServoControl\Cpp\Tests\ServoTests.vcxproj -p:Configuration=Release -p:Platform=x64 -m
C:\PGM\ServoControl\Cpp\build\x64\Release\ServoTests\ServoTests.exe
```

| Đầu ra | Đường dẫn |
|---|---|
| App | `build\x64\Release\App\ShieldCanAssy.exe`. Cả thư mục là bản triển khai, gồm Windows App SDK và hai file chép sang ở bước build (xem dưới) |
| Kiểm thử | `build\x64\Release\CoreTests\CoreTests.exe` |

Hai file được chép cạnh exe ở bước build (target `CopyRuntimeData`):
- `EziMOTIONPlusE.dll`: Core nạp động file này. Nếu thiếu, trục/IO Fastech báo lỗi rõ và app vẫn mở.
- `Config\alarms.json`: bảng mã alarm mẫu, dùng khi `<gốc dữ liệu>\Config` chưa có.

Tham số dòng lệnh:

| Lệnh | Tác dụng |
|---|---|
| `ShieldCanAssy.exe` | Chạy bình thường. Gốc dữ liệu là `D:\ShieldCanAssy`; không có ổ D thì dùng `_RunData` cạnh exe, giống bản WPF. **Máy lắp ráp hiện không có ổ D**, nên bản WPF dùng `ShieldCanAssy.Wpf\bin\Debug\net48\_RunData`, còn bản WinUI lại dùng `_RunData` cạnh exe của nó, tức là một thư mục khác |
| `--root <thư mục>` | Dùng gốc dữ liệu khác. **Vẫn giữ mutex**, và `SimulationMode` lấy theo `system.json` ở gốc đó: nếu file đó đặt `false` thì app điều khiển phần cứng thật |
| `--shot <thư mục>` | Chế độ chụp ảnh: luôn mô phỏng (ép `SimulationMode=true` trong bộ nhớ), không đụng VisionMaster, không giữ mutex. Đặt cửa sổ ra ngoài màn hình, chụp từng trang ra PNG rồi tự thoát. Dùng kèm `--root <thư mục tạm>` |

Thêm: `CoreTests.exe vmprobe` thử thiết bị VisionMaster thật qua SDK C++. Lệnh này khởi động VisionMaster, nên chỉ chạy khi app sản
xuất và VisionMaster đều đang đóng.

Bẫy build đã biết:
- Mọi project gọi SDK VisionMaster phải dùng `/EHs`, không dùng `/EHsc`. Nếu sai, các khối catch bị bỏ và tiến trình chết với 0xC0000409.
- Trong lớp C++/WinRT viết `::base::`, không viết `base::`.
- Khi build nhiều bản cùng lúc, dùng `tools\check.ps1` hoặc `tools\build-exe.ps1 -Out <thư mục riêng>`.

---

## 5. VisionMaster

| Phần | Cách làm | File |
|---|---|---|
| Lệnh (nạp solution, chạy Flow, biến toàn cục, output, ảnh, Clear Point) | SDK C++ trong tiến trình | `Core\Vision\VisionMasterDevice.*` |
| Trạng thái, khoá lệnh, canh giờ, alarm, log | như C# | `Core\Communication\VisionConnect.*` |
| Khung ảnh CAM (Main), trình soạn (Vision Edit) | ActiveX trong popup bám theo ô giữ chỗ | `App\Ui\VmPane.*`, `AxHost.*`, `MainPageCamFrame.*` |

Callback kết quả cho khung ảnh (sửa lỗi 30/09 của bản WPF, port ngày 18/10): `Connect` gọi `DisableModulesCallback`, nên khung chỉ vẽ
được module đã được bật lại callback. `MainPage::BindOne` gọi `VisionMasterDevice::EnableResultCallbacks(path)` trước `RenderSource`:
- Nếu gắn cả Flow: bật cho mọi module trong Flow (kể cả module trong Group), rồi bật cho Flow.
- Nếu gắn một module: chỉ bật module đó.

Cách làm: SDK C++ không có hàm C tương đương. Hàm duy nhất là `CVmToolBase::EnableResultCallback()` (VmToolBase.h), gọi trên đối tượng
công cụ lấy bằng `IVmSolution::operator["Flow.Module"]`, theo đúng mẫu `Samples\VC\...\GetResultControlDlg.cpp`. Mọi lớp công cụ module
trong Flow đều thừa kế đơn `CVmToolBase`. Nếu một module lạ trả về đối tượng khác kiểu, lỗi truy cập bộ nhớ được chặn bằng SEH và
module đó chỉ bị ghi log cảnh báo. Trang Vision Edit của bản WPF không bật callback nên bản này cũng không.

SDK .NET làm được nhưng SDK C++ không có (đã tìm trong header và mẫu):
- Tạo biến toàn cục (`GlobalVariableHelper.AddVar`). `SyncGlobalVariables(apply=true)` chỉ báo được biến nào thiếu, "Create failed"
  kèm lý do; biến phải tạo tay trong VisionMaster rồi lưu solution.
- Duyệt Input/Output và ô Subscribe của module trong Flow (`VmModule.Inputs/Outputs`). `ReadFlowOutputs` trả `Modules` rỗng; phần
  kiểm Script báo "không duyệt được".
- Nguồn output thứ hai của Flow (`VmModule.Outputs` của chính Flow).

Trình tự tắt:
1. Dispose các control ActiveX (`OnClosing` của các trang).
2. `McControl::Shutdown`.
3. `VisionMasterDevice::DisposeSdk()`. Hàm này chỉ chạy một lần.

---

## 6. Tương thích dữ liệu với bản WPF

- `system.json`, `Model\<tên>.json`, `Runtime\running.json`, `alarms.json`: cùng khoá, cùng dạng giá trị như Newtonsoft ghi (PascalCase,
  enum là số, DateTime ISO, bỏ khoá null). Hai bản đọc được file của nhau. Các hàm migrate lúc nạp cũng đã port.
- Mutex `Global\ShieldCanAssy_SingleInstance_{9F3C2B41-6A7D-4E58-9C10-2B5E7A1D8C33}` trùng với bản WPF. Mở app thứ hai thì app đó báo
  "already running (WPF or WinUI)" và thoát. Riêng chế độ `--shot` không giữ mutex.
- Bản C++ chỉ hiển thị tiếng Anh. Khoá `Language` trong `system.json` được giữ nguyên, không dùng.

---

## 7. Kiểm thử

`CoreTests.exe` không cần phần cứng; mỗi nhóm dùng thư mục dữ liệu tạm. Chạy hết mất khoảng 95 giây.

| Nhóm | Số phép kiểm |
|---|---|
| Infra (dữ liệu tương thích C#, alarm, log, đường dẫn, chữ, phân quyền) | 542 |
| FastechBinding (chữ ký/struct EziMOTIONPlusE, EziAxisStatus) | 290 |
| Motion (FakeAxis/FakeIo, FastechAxis/PanasonicAxis trên thiết bị giả, MotionConnect, alarm kết nối) | 372 |
| Vision (hợp đồng Flow, VmOutCheck, GlobalVarSync, FakeVision, VisionConnect) | 201 |
| ControllerFoundation | 132 |
| RoutineCalib | 129 |
| Controller (Init, về gốc, AUTO/MANUAL, EMG, rèm, khí, đèn, chạy rà, chu trình Auto, VM gate/lỗi…) | 448 |
| **Tổng** | **2114**, đều đạt (23/10) |

`ServoTests.exe` (`C:\PGM\ServoControl\Cpp`): 296 phép kiểm, chỉ dùng cổng giả, không mở COM.

Ngày 18/10, vài phép kiểm Controller đọc trạng thái quá sớm nên lúc đạt lúc không. Nguyên nhân là test, không phải lõi:
- reset xoá alarm trước rồi mới `RenewToken`;
- `Paused` đặt trạng thái trước câu báo;
- thời gian chu kỳ chỉ ghi ở S40.

Các phép kiểm đó đã đổi sang chờ (`WaitUntil`).

Ngày 21/10, chạy CoreTests trong lúc App thật và VisionMaster đang mở, một lần hỏng 2 phép kiểm ở TH4 của kịch bản trả hàng
(`ControllerTests.cpp` ~1823: chờ alarm 5031). Chạy lại hai lần liền đều đạt hết — vẫn còn một chỗ phụ thuộc thời gian khi máy bận.
Cùng ngày một lần hỏng 1 phép kiểm chọn ô (`ControllerTests.cpp` ~793, `HasSelection` ngay sau Về gốc + AUTO; hai câu báo
đều đúng), chạy lại hai lần đều đạt — chỗ thứ hai cần đổi sang chờ (`WaitUntil`) khi rảnh.

Giao diện: `ShieldCanAssy.exe --root <tạm>\data --shot <tạm>\png` rồi xem ảnh từng trang. Chưa có kiểm thử tự động bằng chuột/bàn phím.

---

## 8. Đã kiểm chứng và chưa kiểm chứng

Đã kiểm chứng:
- Toàn bộ lõi với thiết bị giả: CoreTests và ServoTests.
- Đọc/ghi dữ liệu tương thích bản C#.
- Ảnh chụp 6 trang ở chế độ mô phỏng. Đèn IO trang Manual giống WPF: tắt là xám `#D3D3D3`, bật là đỏ `CriticalBrush #C42B1C` kèm
  quầng `#FF5C5C`. Bản WPF cũng hiện DI "EMG OK" màu đỏ khi bit bật (Tuấn chọn 2026-09-08).

**Chưa kiểm chứng (chưa từng chạy với phần cứng thật):**
- Fastech Ezi-SERVO Plus-E, Ezi-IO qua Ethernet: kết nối, servo, chạy, về gốc, IO thật.
- Bảng bit `EziAxisStatus`, xem mục 9.
- Panasonic A6SF qua COM. Lần chạy đầu phải xác nhận:
  - cổng mở ở 115200 / Even / One;
  - bộ chuyển RS485 tự đổi chiều (RTS không được điều khiển);
  - độ trễ trả lời so với `ResponseTimeoutMs` (README của ServoControl, mục "Not verified").
- VisionMaster thật:
  - nạp solution sản xuất, chạy Flow, đọc output và ảnh;
  - khung ảnh CAM trên trang Main, kể cả `EnableResultCallbacks` mới thêm;
  - `ClearCalibPoints` (nút Clear Point của N-point/Translation Calibration);
  - trình soạn ở trang Vision Edit.
- Tooltip và hộp thoại nằm đè lên khung VisionMaster (popup ActiveX luôn nằm trên cùng).
- Hình thức banner InfoBar trên máy thật (DPI/màn hình của máy).

---

## 9. Khác biệt và chỗ còn hở so với bản WPF

- **Phân quyền** (chỉ bản WinUI, Tuấn chốt 2026-10-21 — `Core\SysConfig\UserAccess.*`, `App\Ui\LoginDialog.*`):
  - Hai cấp: Operator (mở app luôn ở cấp này) và Engineer. Chỉ trang Vision Edit đòi Engineer; tab có biểu tượng khoá, bấm vào thì
    hỏi mật khẩu. Nút Login / Logout nằm cạnh RESET.
  - Mật khẩu theo cấp, không có tài khoản từng người. Mật khẩu Engineer mặc định là `1234`; đổi ở Setting → System → USER ACCESS
    (chỉ Engineer). File `Config\users.json` chỉ giữ bản băm PBKDF2-HMAC-SHA256 kèm muối, không giữ chữ thật.
  - Tự đăng xuất sau 10 phút không thao tác (đổi được 1..120 phút ở cùng card). Đo bằng `GetLastInputInfo` nên thao tác bên trong
    control VisionMaster cũng được tính. Không tính lúc có hộp thoại mở hay Vision Edit đang chạy một chuỗi hiệu chuẩn.
  - **Quên mật khẩu:** đóng app, xoá `Config\users.json`, mở lại — mật khẩu về `1234`. File hỏng thì app tự đổi tên thành
    `users.json.bad` và cũng về mặc định.
  - Mỗi lần đăng nhập, gõ sai, đăng xuất, đổi mật khẩu đều ghi log mục `User`. Bản WPF không có phân quyền, không đọc `users.json`.
  - PLAN.md §9 còn dự kiến cấp Technician (Manual, Teaching): thêm vào `EUserLevel` giữa hai cấp và sửa `MainWindow::CanOpen`.

- **Popup Origin ở trang Main** (chỉ bản WinUI, Tuấn 2026-10-21 — `MainPage::OnOriginClick`, `McControl::ServoOnAndReleaseBrakes`):
  - Z1/Z2 chưa nhả phanh mà không còn lý do chặn nào khác (`McControl::OriginBlockReason` rỗng) thì bấm Origin hiện hộp Yes/No:
    Servo On mọi trục, nhả phanh Z1/Z2, rồi về gốc. Không cần sang trang Manual. Câu trả lời vào log như mọi hộp xác nhận.
  - Bấm Yes thì làm đúng nghi thức của trang Manual: Servo On (all) bỏ qua trục ALM, chờ driver báo ServoOn, "Brake off" Z1/Z2,
    chờ qua 300 ms nhả phanh, rồi mới `RequestOrigin`. STOP, EMG hay alarm trong lúc chờ thì huỷ, báo "Cannot Origin: …".
  - Còn lý do chặn khác (EMG, rèm, khí, trục ALM/mất kết nối, đang chạy) thì không hỏi: gửi thẳng như cũ để dòng thông báo
    nói lý do. Phanh đã nhả sẵn thì về gốc ngay, không hỏi.
  - Nút Origin trang Manual giữ như cũ (operator ở ngay cạnh nút SVO / Brake Off). Bản WPF không có popup này.

- **Trang Teaching, chế độ Input** (chỉ bản WinUI, Tuấn 2026-10-21): bộ chọn tray còn Input / Assy, bỏ Input 1 / Input 2.
  Bộ chọn Tray và Tool là nút kiểu Radio (`ChoiceButtonStyle` trong `Styles.xaml`, nút đang chọn tô accent) nằm thẳng trên
  nền trang, phía trên card CELL COORDS — luôn thấy, không phải cuộn cột trái. Card TRAY bên trái còn tên tray + Tool đang làm
  và thứ tự quét. Trang Vision Edit làm y như vậy: chọn Tool nằm trên nền trang, phía trên card VISION CALIB (khoá khi Busy).
  - Input hiện cả hai tray đầu vào: hai hàng P1/P2/P3 + điểm kiểm chứng, hai cột cao độ Z hút / Z giảm tốc, hai lưới ô
    (Tray 1 trên, Tray 2 dưới, số thứ tự nối tiếp theo sổ). Số hàng/cột và thứ tự quét chỉ sửa ở hàng Tray 1.
  - Lệnh theo một tray (Move, hạ Z, Set Offset, Go Pick, về gốc Z ở khu Jog) lấy tray của ô đang chọn; chưa chọn ô là Tray 1.
  - Bản WPF vẫn chọn từng tray (ba nút).
- **Vision Edit bỏ nút Check Calib** (chỉ bản WinUI, Tuấn 2026-10-21): dòng cổng Auto đầu card VISION CALIB tự đánh giá lại
  mỗi nhịp (`VmCycle::EvaluateCalib`, chỉ đọc model + `system.json`), xanh khi sẵn sàng, đỏ khi Auto bị chặn. Bỏ luôn dòng
  kết quả trùng bên dưới nút.
- **Calibration Center Point = TOP CAM REF của tray lắp ráp** (Tuấn chốt 2026-10-21, PLAN.md §6.2): tâm lưới 9 điểm top-down
  lấy THẲNG điểm đầu tiên của camera trên tray lắp ráp, ô (1,1) (`AssyTray->CamTopDownAnchorX/Y` của model) — bỏ nút Save
  Center. Dòng "Calibration Center Point = Top CAM ref (1,1): X=… Y2=…"; chưa teach thì chữ cam và numpad / Auto Calib từ
  chối. Các ô Step X/Y, Verify travel, Test angle và hai danh sách bottom-up ghi `system.json` ngay khi đổi (trước đây chỉ
  ghi đĩa nhờ nút Save Center). `TopDownCalibCenterX/Y/Set` vẫn đọc/ghi nguyên giá trị cho bản WPF.
- **Vision Edit, chạy kiểm từng bước** (chỉ bản WinUI, Tuấn 2026-10-21): "Assy cell" / "Input cell" là nút mở lưới ô của tray
  (cùng Flyout với nút 6 của hand-eye, không có "In Place"), thay cho 4 ô gõ hàng/cột. Ô tray đầu vào là của Tray 1 —
  `Routine_Calib::StepPick` chỉ hút ở Tray 1.
- **Auto bật đèn CAM trước khi chạy tới điểm chụp** (chỉ bản WinUI, Tuấn đề xuất 2026-10-23): S05 (đèn CAM2) và S20/S21 (đèn
  CAM1) bật đèn ngay trước lệnh chạy trục tới vị trí soi (`Routine_Auto::LightCamLamp`). Thời gian chờ đèn
  (`LampSettleMs` / `TopLampMs`) tính từ lúc bật đèn, nên lúc kích Flow chỉ chờ phần còn thiếu — trục chạy lâu hơn số đó thì
  không chờ thêm. Bớt khoảng 100 ms mỗi nhịp hai sản phẩm (CAM1) và mỗi lần bắt đầu quét tray lắp ráp (CAM2). Các nút
  trang Vision Edit / hiệu chuẩn vẫn bật → chờ → chụp → tắt như cũ. Bản WPF vẫn bật đèn sau khi trục tới nơi.

- **Nút Save của thanh công cụ VisionMaster lưu thẳng, không mở Save As** (Tuấn báo 2026-10-28): nút đó
  (`VMControls.WPF.ToolControl.SaveSolution`) kiểm `VM.Core.VmSolution.Instance.SolutionPath` phía .NET, rỗng thì gọi
  Save As. App nạp solution bằng SDK C++ (`::LoadSolution`) nên biến đó luôn rỗng; bản WPF nạp bằng `VmSolution.Load` nên
  không gặp (đọc IL của VMControls.WPF.dll / VM.Core.dll 4.4.0). Sửa: `VisionMasterDevice::SolutionFile()` giữ đường dẫn
  tuyệt đối lúc nạp; mỗi lần trang Vision Edit gắn solution, `ui::SetVmDotNetSolutionPath` (App\Ui\VmDotNet.cpp) lấy CLR v4
  đang chạy trong tiến trình (CLRCreateInstance → EnumerateLoadedRuntimes → AppDomain mặc định), tìm VM.Core, gọi setter
  private `SolutionPath` qua `Type.InvokeMember` rồi đọc lại. Log Info "Vision Edit: toolbar Save now saves to …" hoặc
  Warning kèm lý do (Save As vẫn dùng được). File dùng `#import` type library mscorlib nên tắt /MP riêng trong vcxproj.
  CHƯA thử trên máy thật.

- **Vision Edit có nút Hide Calib / Show Calib** (chỉ bản WinUI, Tuấn yêu cầu 2026-10-28): mép phải hàng thanh công cụ
  VisionMaster, sát cột hiệu chuẩn (thanh Jog không còn chỗ ở màn 1280). Ẩn cột 320 px để VisionMaster lấy cả bề ngang khi
  sửa job; hai popup ActiveX tự giãn theo khung giữ chỗ (VmPane). Không khoá theo Busy, chuỗi đang chạy vẫn chạy (STOP vẫn
  ở thanh Jog). Nhớ trong phiên, mở App lại thì hiện. UI-SPEC VE-62.

- **Quét tray lắp ráp từng cặp** (chỉ bản WinUI, Tuấn yêu cầu 2026-10-28): Setting > "Scan Assy tray" nay là hộp chọn
  Whole tray / By row / By pair (thay công tắc "by row"). By pair: S05 chỉ chụp HAI ô trống đầu tiên — đúng hai ô vòng này
  đặt — rồi mới hút, đặt xong mới chụp hai ô kế (`Routine_Auto::NextUnscannedCell`). Ô chụp không thấy thành NG thì ô
  trống kế vào cặp. Tổng số lần chụp không đổi; khoảng cách đo → đặt ngắn nhất. Chạy rà đi đúng đường đó
  (`Routine_DryCycle::SweepCells`). Lưu ở khoá mới `ScanAssyByPair` trong system.json (bản WPF bỏ qua khoá lạ);
  `SystemData::ScanAssy()` gộp hai cờ, cặp thắng hàng. CoreTests kiểm: lần hút đầu sau đúng 2 LocateTop, mọi lần hút số ô
  đã chụp mà chưa đặt ≤ 2.

- **Chụp ô X lớn trước** (chỉ bản WinUI, Tuấn yêu cầu 2026-10-28): trong cặp (By pair) và trong hàng (By row, và hàng đầu
  soi trước lúc chờ tray đầu vào), S05 chụp ô có X chỗ đứng chụp (`CamPosFor`) lớn nhất trước — gương của luật hút ô X nhỏ
  trước. Gantry vừa đặt ở X lớn (tray lắp ráp ~420–667), chụp (~328–504) rồi về tray đầu vào (~28–195): đi một chiều.
  Ô cùng cột (lệch X dưới nửa bước cột) giữ thứ tự ghé. Whole tray giữ thứ tự rắn bò (một lượt mỗi tray, đảo chiều từng hàng
  chỉ thêm đường). Luật ở `Routine_Auto::FirstToScan` (Routine_AutoStatics.cpp); chạy rà theo cùng luật
  (`Routine_DryCycle::ScanOrder`). CoreTests kiểm thứ tự X của LocateTop trong hàng và trong cặp.

- **Xong một tray là hoàn thành chu trình, cả hai bàn ra vị trí chờ** (chỉ bản WinUI, Tuấn yêu cầu 2026-10-28): S50 khi
  tray đầu vào hết hàng (cả sổ Tray 1 + Tray 2) HOẶC tray lắp ráp hết ô trống. Ô còn việc của tray kia thành Skipped
  (`Routine_Auto::FinishTray`, thay `CloseSelection`), Y1 và Y2 cùng về StandbyPos, máy chờ với thông báo
  "Cycle complete — input tray empty / assy tray full. Both trays at standby: replace, then Start". Start sau đó nạp hai
  tray như tray mới. Hết hàng đầu vào mà tay còn cầm can thì đặt nốt rồi mới kết thúc; tray lắp ráp đầy mà tay còn
  cầm can thì can nằm chờ trên tay như cũ. Đồng hồ tray lắp ráp được chốt ở cả hai trường hợp. Bản WPF vẫn chỉ đẩy tray
  hết ra, tray kia làm tiếp (PLAN §5.2 mô tả bản WPF). CoreTests: TrayChangeover / AssyTrayFull cập nhật theo.

- **Cao độ Z hút/đặt và điểm giảm tốc gõ tay được** (Tuấn yêu cầu 2026-10-27): card PICK/PLACE HEIGHT trang Teaching đổi
  8 ô hiển thị thành NumberBox. Gõ số mm rồi Enter (hoặc rời ô) → hỏi Old/New như nút Teach → ghi vào model đang mở; Save mới
  ghi file. Chặn: cao độ hút/đặt phải > 0, mọi số phải nằm trong soft limit trục Z (điểm giảm tốc cho phép 0 = tắt); sai thì
  ô trả lại số cũ. Ghi log Info "Teaching: typed …". Điểm teach X/Y (P1/P2/P3, jig, REF…) vẫn chỉ Teach bằng encoder.

- **Popup control VisionMaster cắt theo vùng client cửa sổ chính** (Tuấn báo 2026-10-25): thu nhỏ cửa sổ thì ba khung
  VmRenderControl vẫn hiện nguyên chỗ cũ ngoài cửa sổ, vì popup là cửa sổ cấp cao nhất, không bị cửa sổ chủ cắt.
  `VmPane::SyncNow` giờ giao khung ô giữ chỗ với `GetClientRect` của cửa sổ chính; `AxHost::MoveInOwnerClient(r, clip)` đặt
  popup bằng phần nhìn thấy và dời control bên trong theo độ lệch, không còn phần nào thì ẩn. Áp dụng cho cả hai control
  trang Vision Edit. Chưa cắt theo viewport ScrollViewer (các ô giữ chỗ hiện không nằm trong ScrollViewer).

- **SDK C++ của VisionMaster** không tạo được biến toàn cục và không duyệt được IO của module (mục 5).
- **`EziAxisStatus`** (`Core\Fastech\EziAxisStatus.h`): bảng bit của bản C# lệch một vị trí so với header gốc của Fastech
  (`MOTION_EziSERVO2_DEFINE.h`) ở bit 6–15 và 27–31.
  - Nếu header gốc đúng thì `Motioning` thực ra là MOTIONCONST, `BusyMask` thiếu MOTIONING thật, `AnyErrorMask` bỏ sót ERRMOTORPOWER.
  - Hiện vẫn giữ giá trị của C#.
  - Kiểm trên máy: trang Manual hiện `Describe` và giá trị thô. Bật servo, chạm cữ, về gốc rồi xem bit nào đổi.
- **Bug của bản WPF giữ nguyên có chủ ý** (`TODO(port)`):
  - `EziMOTIONPlusELib`:
    - nhận chuỗi IPv6 vô nghĩa;
    - `(short)` cắt phần thập phân giá trị AD (máy không dùng card AD);
    - setter `nAlarmCount` không ghi gì.
  - `TeachingPage`:
    - nạp model ở trang Teaching không cập nhật tên model trên thanh tiêu đề;
    - lệnh bị huỷ không có thông báo riêng.
  - `Routine_Calib`: C# giả định `_he` còn; C++ bỏ qua khi null thay vì truy cập null.
  - `Routine_Auto`: `MoveToStandby` đọc `GetAxis` song song (an toàn thực tế).
  - `McControl::AbandonHeld`: câu "Paused cycle aborted" có thể bị "Cycle stopped at …: cancelled" của luồng chu trình ghi đè (bản C#
    cũng vậy).
  - ServoControl: `StbPulseMs` âm (README mục Differences).
- **ServoControl C++** khác C# ở vài điểm có chủ ý: xoá bộ đệm vào trước mỗi lệnh, Connect lần hai đóng cổng cũ, Dispose chờ lệnh
  đang chạy… Danh sách đủ ở `C:\PGM\ServoControl\Cpp\README.md`.
- Không có binding/`INotifyPropertyChanged`: UI đọc theo nhịp 100 ms, nên có thể trễ tối đa một nhịp so với WPF.

---

## 10. Trình tự đưa lên máy (đề xuất)

1. **Đóng app WPF**, chờ VisionMaster (`vServerApp`) tắt hẳn. Kiểm: `Get-Process ShieldCanAssy.Wpf,vServerApp,ShieldCanAssy` không trả gì.
   Hai app dùng chung mutex nên app WinUI sẽ không mở được khi WPF đang chạy.
2. **Chép** `ShieldCanAssy.Wpf\bin\Debug\net48\_RunData` sang một thư mục thử, ví dụ `C:\Temp\ScaTry`. Trong bản chép, sửa `Config\system.json`
   thành `"SimulationMode": true`. Chạy `ShieldCanAssy.exe --root C:\Temp\ScaTry` và kiểm:
   - model và tray nạp đúng;
   - các trang hiện đúng dữ liệu;
   - chạy được Auto và chạy rà ở mô phỏng.
3. **VisionMaster** (vẫn SimulationMode=true): Init không nối vision thật khi mô phỏng, nên thử vision bằng `CoreTests.exe vmprobe` hoặc
   bằng bước 4. Mở solution sản xuất và kiểm:
   - khung ảnh CAM có ảnh sau một lần chạy Flow;
   - log "Main: CAM views … bound" không có cảnh báo callback.
4. **Phần cứng, từng bước**, trên bản chép với `SimulationMode=false`, người đứng cạnh nút EMG:
   1. IO: Ezi-IO đọc đúng DI (EMG, rèm, khí, nút), bật/tắt DO từng bit ở trang Manual.
   2. Fastech: servo on/off từng trục, kiểm bảng bit `EziAxisStatus` (mục 9), jog chậm, về gốc từng trục.
   3. Panasonic A6SF: cổng COM, 115200/Even, đọc trạng thái, servo on, jog, về gốc.
   4. Về gốc cả máy, Init, rồi teach/kiểm vị trí với model sản xuất.
   5. Hiệu chuẩn (Clear Point, Align, top-down), sau đó chạy rà, rồi Auto với vài sản phẩm.
5. Khi mọi bước đạt mới chuyển sang dùng dữ liệu sản xuất (bỏ `--root`, hoặc trỏ `--root` vào `_RunData` của WPF). Giữ bản WPF để quay
   lại khi cần; hai app dùng chung dữ liệu.

---

## 11. Việc tiếp theo

1. Chạy trình tự mục 10 trên máy. Ghi lại các con số cần chốt: thông số COM, độ trễ A6SF, bảng bit EziAxisStatus.
2. Chốt bảng bit `EziAxisStatus` theo kết quả đo. Sau đó sửa mask và xoá cảnh báo trong header.
3. Kiểm khung ảnh VisionMaster và `EnableResultCallbacks` với solution sản xuất. Nếu module nào không bật được, log sẽ ghi tên module đó.
4. Xem lại các `TODO(port)` ở mục 9 và quyết định sửa hay giữ: grep `TODO(port)` trong `Core`, `App`, `Tests` và `C:\PGM\ServoControl\Cpp`.
5. Nếu cần bám sát bản WPF hơn: thêm kiểm thử giao diện tự động, chỉ chạy trên máy dev, không chạy trên máy lắp ráp.
