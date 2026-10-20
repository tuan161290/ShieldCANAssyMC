# Quy ước port ShieldCanAssy C# → C++20 / WinUI 3

Ngày lập: 2026-10-18. Người đọc: người (hoặc agent) port một phần của app WPF `ShieldCanAssy.Core` + `ShieldCanAssy.Wpf` sang
`ShieldCanAssy.WinUI\`, và thư viện `ServoControl` (C#) sang `C:\PGM\ServoControl\Cpp\`.

## 1. Nguồn và đích

| Vai trò | Đường dẫn |
|---|---|
| **Đặc tả hành vi** (bản đang chạy sản xuất — KHÔNG sửa) | `C:\PGM\ShieldCANAssyMC\ShieldCanAssy.Core\`, `ShieldCanAssy.Wpf\` |
| ServoControl C# (đặc tả) | `C:\PGM\ServoControl\` (Services, Models, Constants) |
| Bản mẫu đã port (máy Slit, cùng bộ khung, cùng tác giả) — C# | `<scratch>\ref_slit\ShieldCanSlitAssy.Core\` |
| Bản mẫu đã port — C++ | `<scratch>\ref_slit\ShieldCanSlitAssy.WinUI\` |
| **Đích** — lõi C++ | `C:\PGM\ShieldCANAssyMC\ShieldCanAssy.WinUI\Core\` |
| Đích — app WinUI 3 | `ShieldCanAssy.WinUI\App\` |
| Đích — kiểm thử | `ShieldCanAssy.WinUI\Tests\CoreTests\` |
| Đích — ServoControl C++ | `C:\PGM\ServoControl\Cpp\ServoControl\` (namespace `servo`) |

`<scratch>` = `C:\Users\Hoyeon\AppData\Local\Temp\claude\c--PGM-ShieldCANAssyMC\b452042b-fdc6-4106-b769-ed6a901f786c\scratchpad` — bản clone
tạm của repo `C:\Users\Hoyeon\seadrive_root\actro\Shared with me\Actro\ShieldCanSlitAssyMC.git` (mất thì clone lại từ đó).

Cây `ShieldCanAssy.WinUI\` được dựng bằng cách chép bản Slit C++ rồi đổi namespace `scs` → `sca`, bỏ EtherCAT/SOEM. Vì vậy nhiều file
C++ trong cây hiện là **bản của máy Slit**: đó là điểm xuất phát, không phải đích. Đích luôn là **hành vi của bản C# Assy**.

- File Assy C# và Slit C# gần giống nhau (AlarmManager, VisionConnect, VmContract, VmOutCheck, GlobalVarSync, FakeAxis, FakeVision,
  IAxis, IIoDevice, VisionMasterDevice, Routine_Origin, SafetyMonitor, LogWorker…): so `diff` hai bản C#, rồi áp phần khác biệt lên file C++.
- File khác hẳn hoặc chỉ Assy có (McControl, McFunction, Routine_Auto, Routine_Calib, Routine_DryCycle, VmCycle, VisionReadout,
  TwoHandGate, SystemData, ModelData, TrayLayout, RunningData, IoMap, AxisId, FastechAxis, PanasonicAxis, IoConnect, AxisScale…): port
  thẳng từ C# Assy; dùng file C++ của Slit chỉ để học cách viết.

Port **trung thành từng nhánh**: giữ đủ mọi điều kiện, thứ tự bước, thời gian chờ, giới hạn, thông báo. Không "đơn giản hoá" logic máy;
không thêm tính năng. Chỗ nào C# có bug rõ ràng thì vẫn giữ hành vi, ghi `// TODO(port):` kèm mô tả.

## 2. Ánh xạ ngôn ngữ

| C# | C++ |
|---|---|
| `namespace ShieldCanAssy.*` | `namespace sca` (một namespace cho cả lõi); `base` cho Core\Base; `servo` cho ServoControl |
| `string` | `std::string` UTF-8. Đổi sang UTF-16 chỉ ở ranh giới Win32/WinRT (`base::Widen/Narrow`). Đường dẫn qua `std::filesystem::path` (`base::U8Path`) |
| `async Task<T> FooAsync(..., CancellationToken ct)` | hàm **CHẶN** `T Foo(..., std::stop_token ct)` chạy trên luồng nền. Bỏ hậu tố `Async` |
| `await Task.Delay(ms, ct)` | `base::Delay(ms, ct)` (ném `base::OperationCanceled`) |
| `OperationCanceledException` | `base::OperationCanceled`; `ct.ThrowIfCancellationRequested()` → `base::ThrowIfCancelled(ct)` |
| `CancellationTokenSource` | `std::stop_source`; linked → `base::LinkedStopSource` |
| `Task.WhenAll` (bool) | `base::WhenAll({...})`; `Task.Run(fire-and-forget)` → `base::RunDetached` |
| `event Action<T>` | `base::Event<T>` |
| `INotifyPropertyChanged`/`Bindable` | không có. UI đọc trạng thái theo chu kỳ 100 ms. Số dùng `std::atomic`, chuỗi trả bản sao dưới khoá (xem `McStatus.h`) |
| `lock(obj)` | `std::mutex` / `std::recursive_mutex` + `std::lock_guard` |
| `Interlocked.*`, `volatile` | `std::atomic<T>` |
| `Stopwatch` | `base::Stopwatch` |
| `DateTime` | `sca::TimePoint` (`Utility/DateTimeText.h`); `DateTime?` → `std::optional<TimePoint>` |
| `List<T>` / `Dictionary<K,V>` | `std::vector<T>` / `std::map` hoặc `std::unordered_map` (giữ thứ tự chèn khi C# dựa vào thứ tự → `std::vector<std::pair<>>`) |
| `T?` (nullable value) | `std::optional<T>` |
| Lớp tham chiếu dùng chung (thiết bị, model, dữ liệu) | `std::shared_ptr<T>` (không bao giờ null ở chỗ C# không null) |
| `out` param | tham chiếu `T&` |
| `params object[]` (Loc.F) | template variadic (`Loc::F(key, a, b)`) |
| Exception | giữ nguyên ý: `std::runtime_error` (hoặc lớp riêng) ở chỗ C# ném |

### Tên

- **Giữ nguyên tên C#** cho lớp, enum, giá trị enum, phương thức, trường. Port từng dòng phải đối chiếu được bằng tìm kiếm tên.
- Lớp dữ liệu (SystemData, ModelData, TrayLayout, TrayState, RunningData, AxisConfig, …) là `struct` với **trường public trùng tên
  property C#** (PascalCase). Property tính toán (`=> ...`) thành hàm const cùng tên (`int CellCount() const`).
- Lớp static (McControl, McDataManager, AlarmManager, AppPaths, Loc…) là lớp chỉ có hàm static; property static → hàm static cùng tên
  (`McDataManager::SysData()`), trạng thái riêng nằm trong `.cpp` (namespace ẩn danh hoặc `struct Impl`).
- Enum: `enum class` cùng tên, **cùng giá trị số** (JSON ghi số). Nếu C# dùng enum như cờ bit thì thêm toán tử `|`/`&`.
- Thành viên private: tiền tố `m_` (theo bản Slit C++). Hằng: giữ tên C#.

## 3. Chữ hiển thị — TIẾNG ANH

Tuấn chốt 2026-10-18: ngôn ngữ hiển thị là **tiếng Anh kiểu công nghiệp, ngắn gọn, đặc trưng cho chức năng; nút tối đa 2 từ**.

- Lõi: chỗ C# gọi `Loc.T("Khoá")` / `Loc.F("Khoá", ...)` thì C++ gọi `Loc::T("Khoá")` / `Loc::F("Khoá", ...)` (`Lang/Loc.h`), **cùng khoá**.
  Bảng chữ là `Core/Lang/StringsEn.inc` (sinh từ StringsEn.resx). Định dạng số `{0:0.000}`, `{0:+0.0;-0.0}`, `{0:X}`… y như .NET.
- **Log** (`LogWorker::RecordLog`): bản C# viết log tiếng Việt cứng trong mã. Bản C++ hiện log ở trang Main nên **dịch sang tiếng Anh
  ngắn gọn**, giữ đủ thông tin (giá trị số, tên trục, mã lỗi). Không thêm khoá Loc cho log.
- Chuỗi tiếng Việt cứng khác trong mã C# (thông báo, lý do từ chối không qua Loc): dịch sang tiếng Anh ngắn gọn.
- **Chú thích mã** (comment): giữ phần giải thích "vì sao" của bản C# (tiếng Việt như bản gốc, như bản Slit C++ đã làm). Không cần chú
  thích cho dòng hiển nhiên.
- Giao diện XAML: chữ tiếng Anh viết thẳng trong XAML; nút ≤ 2 từ (vd. "Origin", "Init", "Clear Tray", "Servo On", "Jog +").

## 4. Dữ liệu trên đĩa — tương thích bản C#

`system.json`, model (`Model\<tên>.json`), `Runtime\running.json`, `alarms.json`: **cùng khoá và dạng giá trị** như Newtonsoft ghi
(PascalCase = tên property, enum = số, `DateTime` = ISO `ToIsoString`, bỏ khoá null). Bản C++ phải đọc được file do bản WPF ghi và ngược
lại. Dùng `Utility/JsonHelper.h` (`jsonx::Get` — thiếu khoá/null giữ mặc định, sai kiểu bỏ trường đó) và `nlohmann::ordered_json`.
`[JsonProperty("x")]` → dùng đúng tên `x`. `[JsonIgnore]` → không ghi. Các hàm migrate lúc nạp (vd. `MigrateRotaryAxes`) port đủ.

Gốc dữ liệu: `D:\ShieldCanAssy`, không có ổ D thì `_RunData` cạnh exe (như C#); `--root <thư mục>` để chạy thử ở chỗ khác.
Mutex một-bản-chạy **trùng tên** với bản WPF (`Global\ShieldCanAssy_SingleInstance_{9F3C2B41-6A7D-4E58-9C10-2B5E7A1D8C33}`) để hai app
không bao giờ cùng điều khiển phần cứng.

## 5. Phần cứng

| Thiết bị | C# | C++ |
|---|---|---|
| Fastech Ezi-SERVO Plus-E, Ezi-IO (Ethernet) | P/Invoke `Lib.Fastech\EziMOTIONPlusELib.cs` vào `EziMOTIONPlusE.dll` | `Core/Fastech/EziMOTIONPlusELib.h/.cpp`: nạp động `EziMOTIONPlusE.dll` bằng `LoadLibrary/GetProcAddress` (thiếu DLL → báo lỗi rõ, app vẫn chạy mô phỏng). Chữ ký hàm lấy từ khai báo P/Invoke |
| Panasonic MINAS A6SF (RS485 Modbus RTU) | `ServoControl.ServoService` (FluentModbus) | thư viện `servo::ServoService` (C++, `C:\PGM\ServoControl\Cpp`), Modbus RTU tự viết trên Win32 COM, có giao diện cổng để test cắm ổ giả |
| VisionMaster 4.4 | SDK .NET | SDK C++ `iMVS-6000PlatformSDK` (đã có ở bản Slit C++: `Core/Vision/VisionMasterDevice.*`), control ActiveX trong popup (`App/Ui/VmPane`, `AxHost`) |

Mọi thiết bị có bản giả (`Communication/Sim/Fake*`) — kiểm thử và chế độ mô phỏng **không bao giờ** chạm phần cứng thật.

## 6. An toàn khi làm việc trên máy này

Máy này là **máy lắp ráp thật** (servo, IO, camera nối sẵn), app WPF sản xuất có thể đang chạy.

- KHÔNG sửa gì trong `ShieldCanAssy.Core\`, `ShieldCanAssy.Wpf\`, `ServoControl\*.cs`, thư mục `bin\` của bản WPF.
- KHÔNG chạy app WPF, KHÔNG đóng/kill app hay VisionMaster đang chạy.
- Kiểm thử luôn ở chế độ mô phỏng với thư mục dữ liệu tạm; không bao giờ đặt `SimulationMode=false`, không chép `system.json` sản xuất.
- Không mở cổng COM, không gọi `EziMOTIONPlusE.dll` thật trong test.
- Không chạy gì sinh ra VisionMaster (`vServerApp`) khi app sản xuất đang mở.

## 7. Build

Cần VS 2026 + workload "Desktop development with C++" + "WinUI application development" (toolset v145 = MSVC 14.51, Windows SDK
10.0.28000). Đã cài trên máy này (2026-10-18). Nhiều agent build cùng lúc: dùng `tools\check.ps1` (không ghi file) hoặc
`tools\build-exe.ps1 -Out <thư mục riêng>`; KHÔNG chạy MSBuild trên solution chung khi người khác có thể đang build.

```powershell
$msb = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
& $msb ShieldCanAssy.WinUI.sln -t:CoreTests -p:Configuration=Release -p:Platform=x64 -m     # ServoControl + Core + CoreTests
& $msb App\ShieldCanAssy.vcxproj -restore -p:Configuration=Release -p:Platform=x64 -m      # app
tools\check.ps1 Core\Data\SystemData.cpp      # kiểm cú pháp nhanh 1 file (không ghi gì — chạy song song được)
```

Bẫy build đã biết (từ bản Slit): `/EHs` cho mọi project gọi SDK VisionMaster; trong lớp C++/WinRT viết `::base::` thay vì `base::`.
