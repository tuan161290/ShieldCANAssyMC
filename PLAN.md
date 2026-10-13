# Shield CAN Assy MC — Kế hoạch phát triển phần mềm

Mã máy: **SO1C86-SCAM** · Nền tảng: **WPF .NET Framework 4.8 / x64** · Cập nhật: 2026-09-08 · ba Flow vision (§6.8): 2026-10-09

---

## 1. Tổng quan máy

Máy lắp shield can tự động. Cụm gắp mang **2 tay hút**, lấy sản phẩm từ tray đầu vào,
đưa qua camera bottom-up để đo tư thế (vision align), rồi đặt vào tray lắp ráp với
vị trí/góc đã được bù sai lệch. Camera top-down gắn trên cụm gắp đo tư thế chi tiết
có sẵn trong tray lắp ráp để xác định điểm đặt thực.

### 1.1 Bản đồ trục (7 trục)

| Trục | Chức năng | Driver | Giao tiếp | Ghi chú |
|------|-----------|--------|-----------|---------|
| X  | Cụm gắp di chuyển ngang | Panasonic MINAS A6SF | Modbus RTU, **COM riêng** | dùng để bù sai lệch X |
| Z1 | Lên/xuống tay hút #1 | Panasonic MINAS A6SF | Modbus RTU, **COM riêng** | độc lập với Z2 |
| Z2 | Lên/xuống tay hút #2 | Panasonic MINAS A6SF | Modbus RTU, **COM riêng** | độc lập với Z1 |
| Y1 | Tray đầu vào ra/vào | Panasonic MINAS A6SF | Modbus RTU, **COM riêng** | lưới ô cấu hình được, mặc định 4 hàng × 5 cột |
| Y2 | Tray lắp ráp ra/vào | Panasonic MINAS A6SF | Modbus RTU, **COM riêng** | lưới ô cấu hình được, mặc định 5 hàng × 6 cột; dùng để bù sai lệch Y |
| A1 | Xoay tay hút #1 | Fastech Ezi-Servo PE | Ethernet (EziMOTIONPlusE) | bù góc θ |
| A2 | Xoay tay hút #2 | Fastech Ezi-Servo PE | Ethernet (EziMOTIONPlusE) | bù góc θ |

**Đã chốt:** mỗi trục Panasonic dùng **một cổng COM riêng** → 5 instance `ServoService`
độc lập. Tránh nghẽn bus, cách ly lỗi tốt, phản hồi trạng thái nhanh.

### 1.2 Thiết bị khác

| Thiết bị | Loại | Ghi chú |
|---|---|---|
| CAM Bottom-up | 1 camera cố định, hướng lên | soi **lần lượt** từng tay hút; đo pose sản phẩm đang được hút |
| CAM Top-down | 1 camera gắn trên cụm gắp | đo pose chi tiết có sẵn trong ô tray lắp ráp |
| Vision | HIKROBOT **VisionMaster 4.4.0** SDK | Ba Flow (2026-10-08): `LocateTop` và `AlignBottom_Tool1/2` (đều trả mm; Group Cmd trong `AlignBottom_ToolN` tính luôn lệnh đặt). Mỗi Flow vừa soi vừa nạp điểm hiệu chuẩn, rẽ nhánh theo biến `IsTriggerLocate` App ghi — hợp đồng đầy đủ ở §6.8 |
| I/O | Fastech **Ezi-IO Ethernet 32Di / 32Do** | vacuum, blow, cảm biến chân không, đèn tháp, nút bấm, rèm sáng, sensor tray |
| Rèm sáng | 1 bộ, tín hiệu đọc về Ezi-IO | OSSD **phải** đi thẳng vào relay an toàn — xem §3.1 |
| Mạch an toàn | Relay an toàn cứng + STO của driver | **độc lập với phần mềm** — xem §3.1 |
| Encoder | **Tương đối**, về gốc bằng cảm biến DOG | không có encoder tuyệt đối → về gốc mỗi lần bật máy, xem §4.3 |

### 1.3 Ràng buộc cơ khí đã xác nhận

- Hai tay hút cách nhau theo **phương X**, **pitch cố định**.
- Pitch tay hút **không khớp** pitch ô tray → **hút lần lượt**, **đặt lần lượt**.
- Z1/Z2 độc lập → khi một tay đang thao tác, tay kia giữ nguyên trên cao.

---

## 2. Kiến trúc phần mềm

### 2.1 Cấu trúc solution

Dấu ✔ = đã dựng ở Phase 0 và build sạch; mục ghi → Px sẽ thêm ở phase tương ứng (§10).

```
ShieldCANAssyMC/
├─ ShieldCanAssy.sln                                                         ✔
├─ Config/alarms.json                     bảng mã alarm, copy cạnh exe       ✔
├─ ShieldCanAssy.Core/                    net48, x64, classlib               ✔
│   ├─ Core.Communication/
│   │     IAxis.cs                        trừu tượng hoá 1 trục              ✔
│   │     IIoDevice.cs, IVisionDevice.cs                                     ✔
│   │     AxisConfig.cs                   COM/IP, scale, soft limit, HomeDir ✔
│   │     MotionConnect.cs                quản 7 trục, quét, ZSafeOk         ✔
│   │     Sim/FakeAxis.cs                 mô phỏng theo thời gian thực       ✔
│   │     Sim/FakeIo.cs, Sim/FakeVision.cs                                   ✔
│   │     AxisScale.cs                    quy đổi mm/độ ⇄ xung/rpm           ✔
│   │     PanasonicAxis.cs                bọc ServoControl.ServoService      ✔ (chưa ra máy)
│   │     FastechAxis.cs                  bọc EziMOTIONPlusELib              ✔ (chưa ra máy)
│   │     IoConnect.cs                    Ezi-IO 32Di/32Do thật              ✔ (chưa ra máy)
│   │     VisionConnect.cs                bọc VisionMasterDevice (§6.8)      ✔ (chưa ra máy/solution thật)
│   ├─ Core.Controller/
│   │     McStatus.cs                     trạng thái máy, bind thẳng lên UI  ✔
│   │     McControl.cs                    vòng quét, máy trạng thái, nút bấm ✔
│   │     McFunction.cs                   lệnh chuyển động + toàn bộ interlock ✔
│   │     SafetyMonitor.cs                EMG/rèm/khí/watchdog, giữ AbortToken ✔
│   │     Routine_Origin.cs               chuỗi về gốc O0–O7                 ✔
│   │     Routine_Auto.cs                 chuỗi chạy tự động S00–S50         ✔
│   │     PlacementSolver.cs              dựng đích + đổi dấu góc + chặn ngưỡng §6.6 ✔
│   │     SinglePointAlignMath.cs         bản sao công thức Single Point Alignment của
│   │                                     VisionMaster — chỉ cho mô phỏng/selftest §6.6 ✔
│   │     Routine_Calib.cs                nạp điểm hiệu chuẩn, kiểm tra lại bằng chuyển động
│   │                                     trục thật (§6.2b/§6.4b), hand-eye ✔ — App không còn
│   │                                     tự fit ma trận nào, xem §6.0
│   ├─ Core.Data/                         SystemData, RunningData, TrayState,
│   │                                     NozzleState, McDataManager        ✔
│   ├─ Core.Data.Model/                   TrayLayout, TeachPoint, CellOffset,
│   │                                     ModelData, VisionCalib (hand-eye + dấu góc) ✔
│   ├─ Core.Infor/                        Enums, AxisId, AlarmCode, AlarmData,
│   │                                     AlarmCatalog, AlarmManager, IoMap  ✔
│   ├─ SysConfig/AppPaths.cs                                                 ✔
│   ├─ Utility/                           UiThread, Bindable, SingleInstanceGuard ✔
│   ├─ Utility.Log/                       LogWorker, LogData, LogLevel       ✔
│   ├─ Lib.Fastech/                       EziMOTIONPlusELib.cs (vendor)      ✔
│   │                                     EziAxisStatus.cs, FastechBoardId.cs ✔
│   └─ Lib.VisionMaster/                  VisionMasterDevice.cs (vendor)     ✔ (chưa ra máy/solution thật)
└─ ShieldCanAssy.Wpf/                     net48, x64, WinExe                 ✔
    ├─ App.xaml(.cs), MainWindow.xaml(.cs)                                   ✔
    ├─ SelfTest.cs                        chế độ `--selftest`, xem §10.1     ✔
    ├─ Views/       MainView, ManualView, TeachingView, SettingView,
    │               VisionEditView (nhúng VmMainViewConfigControl thật)      ✔
    ├─ ViewModels/  Shell, Main, Manual, Teaching, Setting, TrayGrid, AxisRow ✔
    ├─ Models/      TrayCellVm, IoLampVm                                     ✔
    └─ Mvvm/ Themes/ Converters/ Services/ Resources/ Fonts/                 ✔
```

### 2.2 Tái sử dụng từ project `Ama_Actro_HousingCorrosion`

Copy gần như nguyên trạng: `Utility`, `Utility.Log` (LogWorker/LogData/LogLevel),
`Mvvm` (ObservableObject, RelayCommand), `Themes` (Palette.xaml, Controls.xaml),
`Converters`, `Services/DialogService`, `Core.Infor/AlarmManager`,
`Lib.Fastech/Ezi32Di|Do`, `Lib.VisionMaster/VisionMasterDevice`,
cùng target MSBuild `CopyVmRuntimeFiles` và `AppDomain.AssemblyResolve` trong `App.xaml.cs`.

Viết mới: toàn bộ `Core.Controller`, `Core.Data*`, `IAxis`/`PanasonicAxis`/`FastechAxis`,
`MotionConnect`, `SafetyMonitor`, và 5 màn hình.

### 2.3 Ghi chú kỹ thuật đã xác minh

- `EziMOTIONPlusE.dll` hiện có là **x64** → chạy chung process với VisionMaster (x64) được,
  không cần tách process/IPC.
- `ServoControl` tham chiếu bằng **ProjectReference** tới
  `C:\Users\Tuan\OneDrive\Tuan\Tuan\Projects\ServoControl\ServoControl.csproj`.
- VisionMaster SDK tham chiếu với `Private=false`, nạp runtime từ thư mục cài đặt
  `C:\Program Files\VisionMaster4.4.0\Development\V4.x\ComControls\Assembly`.
- **Đã chốt: tỉ lệ xung/đơn vị là hằng số theo thiết kế, không phải số đo.** Trục tịnh
  tiến Panasonic dùng **1000 xung/mm** (mm 3 chữ số thập phân — 12.345 mm ↔ 12345 xung),
  trục xoay Fastech dùng **36000 xung/360° của đầu hút** = **100 xung/độ** chẵn, phân giải
  0.01°/xung (Tuấn đổi động cơ 2026-09-12: **18000 xung/vòng động cơ**, nối qua hộp số
  **1/2** nên 2 vòng động cơ mới được 1 vòng đầu hút). Con số này đã đi qua hai lần sai cùng
  một kiểu — 10000/360 = 27.7778 (quên hộp số), rồi 20000/360 = 55.5556 (động cơ cũ) — và
  sai tỉ lệ ở đây không hiện ra ở đâu ngoài chính góc: trục vẫn chạy, vẫn báo "tới nơi",
  chỉ xoay sai góc. Trong code chỉ sửa hai hằng số nguồn `RotaryMotorPulsePerRev` và
  `RotaryMotorRevPerOutputRev` (`SystemData`), đừng gõ số dẫn xuất ở chỗ khác; `system.json`
  cũ tự được ghi đè lúc nạp qua `MigrateRotaryAxes()`. Đây là tỉ số truyền điện tử (electronic
  gear — Pr0.08/Pr0.09 của A6SF, tham số tương ứng của Ezi-Servo) do người cấu hình driver
  đặt để RA ĐÚNG con số này, không phải một đại lượng cơ khí phải jog-rồi-đo-bằng-thước.
  Việc của P1 khi ra máy là cấu hình driver khớp hằng số đã chốt, không phải "xác định"
  nó — khác với chiều dương và điểm gốc từng trục (§11 #6), vốn thật sự phải xác định trên
  máy vì phụ thuộc cách lắp cơ khí.
- **Về gốc: phần mềm chỉ gọi hàm ORG và chờ tín hiệu hoàn tất, không quan tâm cơ chế bên
  trong.** Kiểu về gốc (chạm DOG, bắt xung Z, hay kiểu khác) là tham số của driver (Pr5.xx
  của A6SF / thanh ghi origin của Ezi-Servo), cấu hình một lần bằng PANATERM/EZI Motion
  Studio. `HomeAsync` không có logic nào rẽ nhánh theo cách driver về gốc — nó gọi
  `StartHome`/`FAS_MoveOriginSingleAxis` rồi đợi `ORG_CMP`. `AxisConfig.HomeUseZPhase` chỉ
  là cờ ghi lại chủ ý để đối chiếu lúc cài driver và hiện trên trang Setting, không điều
  khiển hành vi phần mềm.
- **Đã chốt: số hiệu block của trục Panasonic theo đúng mặc định của
  `ServoControl.ServoAxisConfig`** — Block0 = về gốc, Block1 = MoveAbs, Block2 = Jog dương,
  Block3 = Jog âm. Không phải một lựa chọn của project này nên không cần bàn lại; việc ở
  P1 là dùng PANATERM lập trình **nội dung** 4 block đó trong driver (mã lệnh ở Pr56–59),
  không phải chọn số hiệu.

### 2.4 `IAxis`

```csharp
public interface IAxis : INotifyPropertyChanged {
    string     Name { get; }
    AxisConfig Config { get; }

    bool   IsConnected { get; }
    bool   ServoOn { get; }
    bool   Busy { get; }
    bool   Alarm { get; }
    bool   OrgOk { get; }
    bool   FLS { get; }           // giới hạn cứng +  (POT)
    bool   RLS { get; }           // giới hạn cứng −  (NOT)
    double CurrentPos { get; }    // mm với X/Z/Y, độ với A1/A2 — xem Config.Unit
    int    ErrorCode { get; }     // mã lỗi thô của driver, hiện trong Detail của alarm

    bool Connect();
    void Disconnect();
    Task<bool> SetServoOnAsync(bool on);
    Task<bool> HomeAsync(CancellationToken ct);
    Task<bool> MoveAbsAsync(double pos, double speed, CancellationToken ct);
    void Jog(int dir, double speed);           // dir: +1 / −1 / 0 = dừng
    void Stop();
    Task<bool> ResetAlarmAsync();
    void Refresh();                            // vòng quét của MotionConnect gọi
}
```

Ba điểm khác với bản phác thảo ban đầu, đều do yêu cầu ở §3 và §4 kéo theo:

- **`CancellationToken` trên mọi thao tác dài.** SafetyMonitor huỷ một token chung khi
  EMG/rèm/alarm; đó là cơ chế dừng DUY NHẤT mà routine cần biết. Không có nó thì mỗi
  routine phải tự rải kiểm tra an toàn và sẽ có chỗ quên.
- **`CurrentPos` thay cho `CurrentPos_mm`.** A1/A2 tính bằng độ, hậu tố `_mm` sẽ sai.
  Đơn vị nằm ở `Config.Unit`.
- **Trục quay A1/A2 dùng toạ độ quấn vòng `[0, 360)`** (`AxisConfig.WrapDegrees`, các phép
  tính ở `AxisAngle`). Ba hệ quả, phải nhớ cả ba:
  1. **Lệnh chạy tuyệt đối luôn đi đường ngắn.** Từ 350° tới 10° là **+20°**, không phải
     −340°. `FastechAxis.TargetPulse` chọn vòng gần vị trí thô hiện tại nhất; vì vậy lớp
     này phải giữ lại số xung THÔ của driver — từ một góc đã quấn không thể biết driver
     đang ở vòng nào. Đúng nửa vòng thì quy ước đi chiều **dương**.
  2. **Không có hàng rào hành trình.** `InSoftLimit` luôn đúng với trục quay, nên
     `Routine_Auto` cộng `CurrentPos + Δθ` ra 370° cũng không bị chặn oan — lệnh tự quấn.
     `SoftLimitNeg/Pos` của A1/A2 (0…360) chỉ còn để hiển thị dải hợp lệ.
  3. **So hai góc phải dùng `AxisAngle.Distance`, không được trừ thẳng.** Trục dừng
     *quanh* đích chứ không đúng đích, nên "về 0°" trong thực tế là 359.996° hoặc 0.004°:
     trừ thẳng ra 359.99 trong khi khoảng cách thật là 0.008°. Đây là cái bẫy duy nhất mà
     việc quấn vòng tạo ra, và nó đã bắt được một khẳng định sai trong `--selftest` ngay
     lần chạy đầu.
- **`ServoOn` chỉ đọc, bật bằng `SetServoOnAsync`.** Bật servo là thao tác có thể thất
  bại và cần chờ driver báo sẵn sàng; property setter không trả về được kết quả đó.

`PanasonicAxis` ánh xạ sang `ServoService`: `StartHome`, `MoveAbsolute` +
`WaitForMotionComplete`, `JogPositive/JogNegative`, `DecelerateStop`, `AlarmClear`,
đọc trạng thái từ `ServoAxisState` (SON/ALM/INP/BUSY/ORG_CMP/POT/NOT/ActualPosition).
`FastechAxis` ánh xạ sang `FAS_MoveSingleAxisAbsPos`, `FAS_MoveOriginSingleAxis`,
`FAS_MoveVelocity`, `FAS_MoveStop`, `FAS_ServoAlarmReset`, `FAS_GetAxisStatus`.

**Lệnh chạy chỉ gửi thứ driver chưa có** (Tuấn yêu cầu 2026-09-25: ghi đi ghi lại thanh ghi làm tăng
độ trễ). Mỗi giao dịch Modbus trước xung STB là thời gian chết giữa lúc ra lệnh và lúc trục nhúc nhích.
`ServoService` nhớ giá trị các ô tốc độ/acc/dec và số block mà chính nó đã ghi, chỉ ghi lại khi số cần
gửi khác. Hai word vị trí đi chung một lần ghi nhiều thanh ghi (0x10), và acc/dec thành độ dốc cố định
theo mức Auto cao (§5.4) nên không đổi theo từng lệnh. Số giao dịch trước STB: trước đây luôn là **7**;
nay X/Y cùng tốc độ lệnh trước là **2**, Z hai chặng (đổi tốc độ mỗi lệnh) là **3**. Bộ nhớ ấy được phép
quên chứ không được nói sai: xoá sạch khi về gốc (việc máy làm sau mọi lần driver có thể đã khởi động
lại), khi một giao dịch hay một vòng đọc trạng thái lỗi, và khi ghi tham số hay bảng block. Jog ghi ramp
của nó vào chính ô acc/dec mà block ABS đọc, nên lệnh ABS kế tiếp thấy khác và ghi lại.

**Biết trục đã tới sớm hơn** (cùng ngày). App chỉ biết trục đã tới ở lượt đọc trạng thái kế tiếp sau khi
trục dừng. `PanasonicAxis` đặt khoảng nghỉ giữa hai lượt đọc là 10 ms thay vì 50 — thực tế ~16 ms thay vì
~63 ms, vì `Task.Delay` làm tròn lên bội 15.6 ms của đồng hồ Windows. Cái giá: lệnh mới dễ gặp một lượt
đọc (5 giao dịch) đang dở hơn và phải chờ nó xong. Đo bằng `ServoService` thật với driver giả có thời gian
giao dịch, trừ phần đó rồi mỗi lệnh chạy vẫn xong sớm hơn trung bình ~20–25 ms. Số đo cũng cho thấy thời
gian một giao dịch (dây + bộ chuyển USB-RS485) mới là phần lớn: 18 ms thay vì 4 ms thì mỗi lệnh chậm thêm
~120 ms — bộ chuyển FTDI để Latency Timer mặc định 16 ms rơi vào trường hợp 18 ms.

---

## 3. An toàn, nút bấm và trạng thái máy

### 3.1 Nguyên tắc — phần mềm không phải là lớp an toàn

Đây là điều kiện tiên quyết cho toàn bộ phần còn lại của mục này:

> **Ezi-IO Ethernet không phải thiết bị an toàn (không có chứng nhận SIL/PL).**
> Không được dùng DI của Ezi-IO làm mạch bảo vệ. Nút EMG và rèm sáng nên đi
> **thẳng vào relay an toàn cứng**, relay này cắt nguồn động lực / kích **STO** của
> 5 driver Panasonic và 2 driver Fastech. Phần mềm chỉ **đọc trạng thái** để hiển thị,
> ghi log, dừng chu trình và chặn khởi động lại.

Lý do: I/O qua Ethernet có độ trễ chu kỳ quét 1–10 ms, cộng jitter của Windows (không
phải hệ thời gian thực) — có thể lên vài trăm ms khi máy bận. Không có con số nào trong
dải đó dùng làm thời gian đáp ứng an toàn được. Windows treo hoặc app crash thì DO giữ
nguyên trạng thái cuối — máy vẫn chạy.

> **Quyết định của chủ đầu tư (2026-07-31):** máy này **không** áp mức an toàn nói trên.
> **EMG và rèm sáng đều đọc về Ezi-IO**, không có relay an toàn riêng. Đây là lựa chọn có
> ý thức của bên đặt hàng, ghi lại ở đây để sau này truy được nguồn gốc chứ không phải để
> tranh luận lại. Phần mềm được viết theo đúng kiến trúc đã chọn.
>
> Hệ quả kỹ thuật phải xử lý, và đã xử lý: **Ezi-IO trở thành điểm hỏng đơn của toàn bộ
> mạch an toàn.** Mất kết nối Ezi-IO nghĩa là mù hoàn toàn — không còn đường nào biết EMG
> có đang bị nhấn hay rèm có bị cắt. Vì vậy `SafetyMonitor` xử lý mất kết nối I/O như
> chính sự cố tệ nhất mà nó che mất: **huỷ token, dừng ngay mọi chuyển động**, không chỉ
> chặn lệnh mới (alarm 3000, mức Fatal).
>
> Hai giới hạn còn lại **không** khắc phục được bằng phần mềm, ghi ra để mọi người biết
> mình đang đứng ở đâu: (a) app crash hoặc Windows treo thì DO giữ nguyên trạng thái cuối
> và máy vẫn chạy — không có gì cắt được nó; (b) thời gian đáp ứng từ lúc nhấn EMG tới lúc
> trục dừng phụ thuộc chu kỳ quét và jitter Windows, có thể lên vài trăm ms khi máy bận.
> Nếu sau này cần nghiệm thu theo tiêu chuẩn an toàn thì đây chính là hai điểm sẽ bị bắt,
> và cách sửa là bổ sung relay an toàn cứng chứ không phải sửa phần mềm.

Sáu quy tắc bắt buộc khi đấu nối:

1. **Toàn bộ tín hiệu an toàn dùng tiếp điểm NC** (thường đóng). Đứt dây = kích hoạt an
   toàn. Phần mềm đọc DI và **đảo logic** — quy ước này ghi rõ trong `IO_MAP`.
2. ~~Công tắc cửa 2 kênh.~~ **Đã tháo khỏi máy (2026-09-09).** Mọi điều kiện liên quan xoá
   khỏi code — xem §3.5.
3. **Van hút KHÔNG nối vào mạch cắt của EMG.** Nếu cắt khí khi EMG, sản phẩm đang hút
   rơi xuống trong máy — vừa hỏng hàng vừa thành vật cản cho lần về gốc kế tiếp.
   **Đã xác nhận (§11 #11):** mạch khí giữ nguyên trạng thái khi EMG. Phần mềm cũng
   không được tự tắt van ở bất kỳ đường dừng nào — có bài tự kiểm tra canh điều này.
4. **Rèm sáng và EMG đọc về Ezi-IO** (DI13, DI00) theo quyết định ở trên. Phần mềm coi
   mất kết nối Ezi-IO là điều kiện dừng khẩn, xem khối trích dẫn phía trên.

   **Tạm dừng: `SystemData.HasLightCurtain = false` — coi như CHƯA có rèm sáng.** Phần
   cứng đã chốt là có 1 rèm, nhưng chu trình hoạt động khi rèm tác động (bảo vệ đúng vị
   trí nào, tray ra/vào có tự động cắt qua nó không, có cần muting hay không) chưa quyết
   định — xem §11 #18, bàn sau. `SafetyMonitor` đã viết sẵn để tôn trọng cờ này: tắt thì
   không đọc DI13, không phát alarm 1105, không chặn gì. Bật lại đúng lúc #18 có câu trả
   lời, không cần sửa code.
5. **Trục Z1, Z2 thẳng đứng phải có phanh giữ.** Khi mất mô-men, trục đứng mang tải sẽ
   tụt xuống cho tới khi chạm cữ hoặc chạm tray. **Đã xác nhận (§11 #19):** có tín hiệu
   cấp nhả phanh, và tín hiệu này bị ngắt khi driver báo alarm hoặc Servo OFF — tức là
   phanh đóng đúng lúc cần. Còn một chi tiết phải chỉnh khi cài driver ở P1: **thời điểm
   đóng phanh so với thời điểm cắt mô-men**. Cắt mô-men trước khi phanh kịp bắt thì trục
   Z vẫn tụt vài mm mỗi lần Servo OFF, đủ để đầu hút cắm vào tray. Trên A6SF là Pr4.38 /
   Pr4.39; nghiệm thu bằng cách Servo OFF 10 lần ở cao độ giữa hành trình và đo độ tụt,
   yêu cầu < 0.5 mm.
6. **Không có nút EMG trên màn hình.** Nút mềm tạo cảm giác an toàn sai. Trang Main chỉ
   *hiển thị* trạng thái EMG.

### 3.2 Bản đồ I/O an toàn và nút bấm

**Cập nhật 2026-08-20/2026-09-04 (Tuấn yêu cầu 4 nút Start vật lý kiểu an toàn 2 tay):** không
còn 1 nút START đơn — thay bằng **Cặp A** (2 nút, OR — bấm 1 trong 2 là đủ, đưa máy vào trạng
thái `Armed` chờ nạp tray) và **Cặp B** (2 nút, AND kiểu 2 tay thật ISO 13851 IIIC — phải bấm
**đồng thời cả hai** mới thật sự cho máy chạy, xem `TwoHandGate`). **Từ 2026-09-26 (Tuấn chốt) cụm B
không còn là cặp 2 tay:** hai nút riêng, bắt sườn lên, mỗi nút nạp một tray — **Start 1** (DI16)
nạp tray đầu vào, **Start 2** (DI17) nạp tray lắp ráp; lúc `ARMED` nút nào cũng nạp tray của nó rồi
khởi động chu trình. Thiếu tray nào thì bước cần tray đó đứng chờ (§3.4). `TwoHandGate` giữ lại trong
code nhưng máy không dùng. Máy cũng **không có công tắc
chìa AUTO/MANUAL vật lý** — chọn mode bằng nút mềm trên trang Main (`McControl.RequestSetMode`);
bit DI06 dành cho công tắc chìa cũ nay bỏ trống, không tái dùng. Bảng dưới đây khớp
`ShieldCanAssy.Core.Infor.IoMap` hiện tại — đây vẫn là bản dự kiến, chốt lại khi có sơ đồ điện.

| DI | Tín hiệu | Kiểu | Ghi chú |
|---|---|---|---|
| DI00 | EMG feedback từ relay an toàn | NC | 1 = mạch an toàn đang đóng (bình thường) |
| DI01 | *(trống)* | | Trước là công tắc cửa kênh A — đã tháo khỏi máy (2026-09-09), xem §3.5. Không tái dùng khi chưa hỏi lại, có thể còn dây đấu dở dang. |
| DI02 | *(trống)* | | Trước là công tắc cửa kênh B — đã tháo khỏi máy (2026-09-09), xem §3.5. Không tái dùng khi chưa hỏi lại, có thể còn dây đấu dở dang. |
| DI03 | *(trống)* | | Đã thử đấu chung 2 nút Start Cụm A vào bit này (2026-09-08) nhưng Tuấn đấu lại điện, quay về 2 bit riêng DI14/DI15. Không tái dùng. |
| DI04 | Nút STOP | NO | **bắt sườn lên**, chống dội 20 ms |
| DI05 | *(trống)* | | Máy KHÔNG có nút RESET vật lý — RESET chỉ bấm được bằng nút mềm trang Main (`McControl.RequestReset` gọi thẳng, không qua DI) |
| DI06 | *(trống)* | | Máy không có công tắc chìa AUTO/MANUAL vật lý — chọn mode bằng nút mềm trên trang Main. |
| DI07 | Công tắc áp suất khí | NC | thiếu khí → không cho hút, alarm |
| DI08 | Cảm biến chân không tay hút #1 | NO | |
| DI09 | Cảm biến chân không tay hút #2 | NO | |
| DI10 | Có tray đầu vào | NO | |
| DI11 | Có tray lắp ráp | NO | |
| DI12 | Nút tắt còi | NO | |
| DI13 | Rèm sáng thông suốt | NC | bản sao tín hiệu để hiển thị/dừng chu trình; OSSD vẫn đi thẳng vào relay an toàn |
| DI14 | Cặp A — nút Arm #1 (OR) | NO | bắt sườn lên; xem `IoMap.BtnArmA1` |
| DI15 | Cặp A — nút Arm #2 (OR) | NO | bấm 1 trong 2 nút Cặp A là đủ |
| DI16 | Cụm B — Start 1 (tray đầu vào) | NO | bắt sườn lên; nạp tray đầu vào, lúc ARMED thì chạy luôn (26/09 — trước là Cặp B nút #1, 2 tay) |
| DI17 | Cụm B — Start 2 (tray lắp ráp) | NO | bắt sườn lên; nạp tray lắp ráp, lúc ARMED thì chạy luôn (26/09 — trước là Cặp B nút #2, 2 tay) |

| DO | Tín hiệu | Ghi chú |
|---|---|---|
| DO00/DO01 | Van hút tay #1 / #2 | |
| DO02/DO03 | Van thổi tay #1 / #2 | |
| DO04/DO05/DO06 | Đèn tháp đỏ / vàng / xanh | |
| DO07 | Còi | |
| DO08 | Đèn nút Arm (Cặp A) | **y hệt DO10** (Tuấn chốt 26/09): sáng im khi RUNNING/PAUSING, nhấp nháy 1 Hz khi ARMED, tắt khi máy đứng. Trước đó sáng khi state ∈ {IDLE, PAUSED} — lúc bấm Cặp A có tác dụng |
| DO09 | Đèn nút STOP | sáng im mọi lúc **trừ khi đang chạy** (state ∈ {RUNNING, PAUSING}) — Tuấn đổi 26/09; trước đó là đèn "cần RESET", chỉ sáng khi còn alarm (không có nút RESET riêng, xem DI05) |
| DO10 | Đèn nút Start Cụm B | sáng im khi máy **đang hoạt động** (state ∈ {RUNNING, PAUSING}); **nhấp nháy 1 Hz khi ARMED** — đã bấm RUN, chờ bấm Start 1/Start 2, nút RUN trên trang Main nháy xanh cùng lúc (26/09) — DO08 y hệt |
| DO11 | Đèn chiếu sáng CAM1 (vision) | |
| DO12 | Đèn chiếu sáng CAM2 (vision) | |
| DO13 | Nhả phanh Z1 | 1 = nhả (cho phép di chuyển), 0 = khoá; điều khiển thẳng bằng Ezi-IO, không dùng phanh tích hợp của driver Panasonic — xem `McFunction.UpdateBrakeOutputs`/`RequestBrakeRelease` |
| DO14 | Nhả phanh Z2 | như DO13 |

**Bắt sườn nút bấm** làm trong vòng lặp `IoConnect.RefreshAsync()`: giữ ảnh DI của chu kỳ
trước, sườn lên = `now && !prev`, kèm bộ lọc chống dội 20 ms. Dùng sườn chứ không dùng
mức, nếu không thì nút bị kẹt hoặc dính tiếp điểm sẽ khiến máy tự khởi động lại liên tục. Cụm B
(DI16/17) trước 26/09 đọc theo MỨC qua `TwoHandGate`; nay cũng bắt sườn lên như mọi nút khác.

### 3.3 Máy trạng thái `EMcState`

| Trạng thái | Ý nghĩa | Cho phép chuyển động? |
|---|---|---|
| `INIT` | nạp config, kết nối thiết bị | không |
| `NOT_HOMED` | đã kết nối, chưa về gốc | chỉ jog tay, tốc độ giới hạn |
| `HOMING` | đang chạy chuỗi Origin | có, chỉ Routine_Origin |
| `IDLE` | đã về gốc, đứng yên, sẵn sàng | có |
| `ARMED` | đã bấm Cặp A sau khi chọn mode Auto — đứng yên, chờ nạp tray bằng Start 1/Start 2 (cụm B) mới chạy thật | không |
| `RUNNING` | đang chạy Auto | có |
| `PAUSING` | đã nhận STOP, đang chạy nốt tới checkpoint | có |
| `PAUSED` | dừng ở checkpoint, giữ tiến độ và sản phẩm trên tay hút | không |
| `WAIT_OPERATOR` | chờ người thao tác (nạp/đổi tray, xác nhận) | không |
| `ALARM` | có alarm mức Error/Fatal đang active | không |
| `EMG` | mạch an toàn hở | không |

Chuyển trạng thái chính:

```
INIT ──kết nối OK──► NOT_HOMED ──(nút Về gốc)──► HOMING ──đủ 7 trục OrgOk──► IDLE
IDLE/PAUSED ──Cặp A (Arm)──► ARMED ──Start 1 hoặc Start 2──► RUNNING ──STOP──► PAUSING
PAUSING ──tới checkpoint──► PAUSED
RUNNING ──bước cần tray đầu vào mà tray chưa vào──► WAIT_OPERATOR ──Start 1──► RUNNING
RUNNING ──bước cần tray lắp ráp mà tray chưa vào──► WAIT_OPERATOR ──Start 2──► RUNNING
        (hết tray đầu vào / đầy tray lắp ráp: S50 đưa tray đó ra vị trí chờ — tray ra khỏi máy)
* bất kỳ ──EMG hở──► EMG              (ưu tiên tuyệt đối, cắt mọi routine)
* bất kỳ ──alarm Error/Fatal──► ALARM
EMG/ALARM ──RESET + hết nguyên nhân──► NOT_HOMED (mặc định) hoặc IDLE nếu còn giữ gốc
```

**Về gốc không còn qua Cặp A/B** (chủ ý đơn giản hoá, 2026-08-20) — về gốc là chuyển động thật,
phải bấm nút "Về gốc" riêng trên UI, không mượn đường Arm/Go.

`SafetyMonitor` chạy độc lập với các routine, chu kỳ ≤ 20 ms, và có quyền ép trạng thái
sang `EMG`/`ALARM` bất cứ lúc nào bằng `CancellationToken` chung mà mọi routine đều
observe. Routine **không được** tự kiểm tra an toàn rải rác — chỉ kiểm tra token.

`SafetyMonitor` giám sát: DI00 (EMG), DI13 (rèm sáng), DI07 (khí), `IsConnected` của 7 trục và
Ezi-IO, `ALM` của 7 trục, và **watchdog**: nếu vòng `McControl` không tick trong 500 ms
→ alarm 1002 và dừng.

### 3.4 Hành vi từng nút

| Nút | Điều kiện | Hành động |
|---|---|---|
| **Cặp A — Arm** (DI14 *hoặc* DI15, bắt sườn lên) | mode = Auto, state ∈ {IDLE, PAUSED} | → `ARMED`: đứng yên, chờ operator đặt tray. Không tự chạy — đây KHÔNG phải nút chạy máy. |
| **Start 1** (DI16, bắt sườn lên) | state = `ARMED` | nạp tray đầu vào, kiểm an toàn + điều kiện §5.1 rồi → `RUNNING`. Tray đầu vào đã ở trong máy (chạy tiếp sau STOP) thì chỉ khởi động |
| **Start 2** (DI17, bắt sườn lên) | state = `ARMED` | nạp tray lắp ráp rồi → `RUNNING`, như Start 1 |
| **Start 1 / Start 2** | chu trình đang chạy hoặc `WAIT_OPERATOR` | chỉ nạp tray của nút đó; tray đã ở trong máy thì báo và bỏ qua |

**Nạp tray (Tuấn chốt 2026-09-26).** Tray đặt ở vị trí chờ của trục hàng (Y1/Y2); cảm biến DI10/DI11 phải
thấy tray ở đó mới nhận nút. Tray hết theo sổ (đầu vào không còn ô có hàng, lắp ráp không còn ô trống) thì
là tray mới, làm lại sổ; tray còn dở thì giữ sổ. Mỗi tray chỉ cho phép phần việc của nó:

- **Chưa có tray lắp ráp:** không soi. Tray đầu vào đã nạp thì Y1 đưa tray vào tới hàng hút kế tiếp rồi
  chờ Start 2 (TH1).
- **Chưa có tray đầu vào:** không hút. Tray lắp ráp đã nạp thì chỉ soi hàng đầu (hàng của ô trống đầu
  tiên) rồi chờ Start 1 (TH2); nạp xong mới quét nốt và hút. Hàng đang cầm trên tay thì vẫn soi và đặt nốt.
  Soi hỏng mà không trả về được tray đầu vào (tray chưa vào máy, hoặc tray mới đầy hàng — không rõ ô nguồn)
  thì báo **5031**, người lấy hàng ra rồi RESET (Tuấn chốt 2026-09-26).
- **Hết tray đầu vào / đầy tray lắp ráp:** S50 đưa tray đó ra vị trí chờ — tray ra khỏi máy, bước cần nó
  chờ nút như trên. Về gốc cũng đưa cả hai tray ra.
| **(nút mềm) Về gốc** | state = `NOT_HOMED` | chạy `Routine_Origin` — nút riêng trên UI, không qua Cặp A/B |
| **STOP** lần 1 | state = RUNNING | → PAUSING: chạy nốt tới checkpoint an toàn gần nhất rồi → PAUSED. Sản phẩm không bị bỏ giữa chừng. |
| **STOP** lần 2 (hoặc giữ > 2 s) | state = PAUSING | dừng giảm tốc **tất cả** trục ngay tại chỗ, **giữ servo ON, giữ vacuum** → PAUSED với cờ `UnsafePause = true` |
| **STOP** | state = HOMING | huỷ origin, tất cả trục decel stop → NOT_HOMED |
| **RESET** | luôn nhận | tắt còi; `AlarmClear` cho các trục đang ALM; xoá các alarm đã hết nguyên nhân; nếu sạch alarm → NOT_HOMED hoặc IDLE tuỳ `OrgOk` |
| **EMG** | luôn | phần cứng đã cắt động lực. Phần mềm: huỷ token, dừng mọi routine, ghi log alarm 1100, đèn đỏ nháy + còi, lưu `RunningData` |
| **Tắt còi** | luôn | tắt DO07, **không** xoá alarm |
| **Chọn mode Auto/Manual** (nút mềm trang Main — không có công tắc chìa vật lý) | | đổi mode. Nếu đang RUNNING mà chuyển về Manual → xử lý như STOP lần 1. Nếu đang ARMED mà chuyển về Manual → về lại IDLE. |

**Nút mềm "START" trên UI chỉ gọi được `RequestArm()` (Cặp A) — không có đường nào từ màn hình
cảm ứng chạy thẳng máy được nữa** (2026-08-20, chủ ý). Muốn chạy thật (vào `RUNNING` từ `ARMED`,
hoặc nạp tray lúc `WAIT_OPERATOR`) bắt buộc phải bấm nút cứng Start 1/Start 2 ở cụm B — không có
nút mềm nào gọi `RequestGoInput()`/`RequestGoAssy()`. (Trước 26/09 là cặp 2 tay AND qua `TwoHandGate`.)

**Checkpoint an toàn** là ranh giới giữa các state của chuỗi Auto (S00/S10/S11/…/S50).
Routine chỉ kiểm tra `PauseRequested` tại các ranh giới này, **không kiểm tra giữa lúc
Z đang hạ hoặc đang chuyển vacuum** — dừng ở đó để lại máy trong trạng thái không xác
định được. Do đó STOP lần 1 có thể mất tới ~1 bước mới dừng; đó là chủ ý.

### 3.5 Rèm sáng và chế độ Manual

> **Cảm biến cửa đã THÁO KHỎI MÁY (Tuấn xác nhận, 2026-09-09).** Mục này trước mô tả một
> công tắc cửa 2 kênh (DI01/DI02) cấm hẳn chuyển động khi mở, bất kể chế độ nào — phần cứng
> đó không còn tồn tại nữa. Đã xoá HẾT khỏi code: `SafetyMonitor` (đọc DI, so lệch 2 kênh,
> chặn `SafeToMove`, ghì tốc độ Manual), `Routine_Origin` (precheck), `McFunction` (cảnh báo
> 1104), `McStatus.DoorClosed`, `SystemData.HasEnablingDevice`/`BypassDoorSafety`/
> `DoorChannelMismatchMs`, `AxisConfig.ManualSpeedLimitDoorOpen`, alarm 1101/1102/1104, và
> 2 bit DI01/DI02 trong `IoMap` (còn trống, chưa tái dùng). Đây không phải bypass tạm — là bỏ
> hẳn vì phần cứng không còn, khác hẳn quyết định "CHỐT: KHÔNG" từng ghi ở §11 #12 (giữ lại để
> tham khảo lịch sử, không còn áp dụng).

- **Rèm sáng bị cắt → chặn chuyển động ở MỌI chế độ** (alarm 1105, mức Error), không có
  ngoại lệ cho Manual — người có thể thò tay qua rèm bất cứ lúc nào kể cả khi thợ đang jog.
  Rèm thông trở lại **không** tự cho chạy tiếp, phải bấm RESET — người vừa đi qua rèm có thể
  vẫn đang đứng trong vùng nguy hiểm, và tự khởi động lại sau khi chùm sáng nối lại là đúng
  kịch bản tai nạn mà rèm sinh ra để ngăn.

### 3.6 Đèn tháp và còi

| Trạng thái | Đỏ | Vàng | Xanh | Còi |
|---|---|---|---|---|
| RUNNING | | | sáng | |
| IDLE / đã sẵn sàng | | sáng | | |
| ARMED (đã bấm Cặp A, chờ Cặp B) | | | nháy 1 Hz | |
| PAUSED | | sáng | | |
| WAIT_OPERATOR (đổi tray, xác nhận) | | nháy 1 Hz | | ngắt quãng |
| Warning (tỉ lệ NG cao, sắp hết tray, calib quá hạn) | | nháy 0.5 Hz | sáng | |
| ALARM | sáng | | | liên tục |
| EMG | nháy 2 Hz | | | liên tục |
| NOT_HOMED | | sáng | | |

### 3.7 Khôi phục sau khi dừng — kiểm tra toàn vẹn

Đây là phần hay bị bỏ sót và là nguồn hỏng hàng phổ biến nhất khi vận hành thật.

**Bấm START ở trạng thái PAUSED:**

```
1. Đọc lại cảm biến chân không DI08, DI09.
2. So với RunningData.Nozzle[i].HasProduct đã lưu.
3. Khớp  → tiếp tục từ RunningData.AutoState.
4. Cảm biến báo CÓ hàng mà hệ thống ghi không (mở lại app, sau Origin…) → KHÔNG phải lỗi
   (Tuấn đổi 2026-09-26): nhận hàng đó, chạy lại từ S00, bỏ bước hút của tay đó, soi lại
   ở CAM bottom-up rồi đặt. Không rõ ô nguồn thì ghi −1.
5. Hệ thống ghi CÓ hàng mà cảm biến báo không (hàng có thể đã rơi trong máy) → CHẶN,
   alarm 5030, tự đồng bộ theo cảm biến, chờ RESET:
     "tay #i: phần mềm ghi có sản phẩm, cảm biến báo không"
6. Nếu UnsafePause = true (dừng giữa hành trình): nâng Z1, Z2 lên Z_SAFE trước,
   xác nhận đủ Z_SAFE, rồi mới cho chọn tiếp tục. Mặc định an toàn là yêu cầu Origin lại.
```

**Khôi phục sau EMG:**

```
1. Xoay nhả nút EMG.
2. Bấm RESET → relay an toàn đóng lại, cấp lại nguồn động lực.
3. Servo ON lại 7 trục, chờ S_RDY.
4. Xử lý sản phẩm còn trên tay hút theo bước 4 ở trên (làm TRƯỚC khi cho chuyển động).
5. RequireHomeAfterEmg (mặc định true) → bắt buộc chạy Origin.
   Chỉ đặt false sau khi đã xác nhận cả 5 driver Panasonic dùng encoder tuyệt đối
   còn pin và 2 driver Fastech giữ được vị trí qua lần mất động lực.
```

`RunningData` được ghi xuống đĩa **sau mỗi ô hoàn tất** và **ngay khi vào EMG/ALARM** —
phục vụ log/điều tra sự cố và giữ bộ đếm OK/NG, nhưng **không dùng để resume tiến độ tray
qua một lần tắt/mở app** (đã chốt lại — xem §8): mỗi lần mở app đều bắt buộc Init lại toàn
bộ máy (về gốc, tray trống), STOP/START trong cùng phiên chạy thì vẫn tiếp đúng bước.

---

## 4. Chu trình về gốc (Origin)

### 4.1 Khi nào bắt buộc về gốc

**Đã chốt (§11 #14): máy dùng encoder tương đối, về gốc bằng cảm biến DOG.** Điều này
định đoạt cả mục 4: vị trí không sống sót qua một lần cắt nguồn driver, nên về gốc không
phải là thủ tục cho chắc mà là điều kiện để mọi toạ độ trong máy có nghĩa.

- Sau mỗi lần khởi động phần mềm.
- Sau EMG. `RequireHomeAfterEmg` vẫn còn trong `system.json` cho tương lai, nhưng
  **getter của nó bỏ qua giá trị đã đặt khi `HasAbsoluteEncoder = false`** — tắt được
  bằng file cấu hình thì sớm muộn sẽ có người tắt để chạy nhanh hơn. Ràng buộc thuộc về
  phần cứng thì phải do code giữ.
- Sau khi xoá alarm loại mất vị trí của driver (encoder error, over-speed, quá tải).
- ~~Sau khi Servo OFF một trục bất kỳ.~~ **Bỏ (Tuấn, 2026-09-26):** tắt/bật servo không làm mất gốc — driver
  vẫn đếm encoder khi tắt servo, trục có tụt (Z, trước khi phanh bắt) hay bị đẩy tay thì vị trí đọc về vẫn
  đúng. Chỉ đúng khi driver còn nguồn; mất nguồn driver thì vẫn phải về gốc (mục kế tiếp và §3.7).
- Sau khi thay đổi tham số scale/offset của trục trong Setting.

`OrgOk` là cờ **của phần mềm**, đặt `false` ngay khi xảy ra bất kỳ điều kiện trên, và chỉ
đặt `true` khi cả 7 trục báo hoàn tất về gốc. Không suy ra `OrgOk` từ riêng bit `ORG_CMP`
của driver — bit đó không biết phần mềm đã khởi động lại.

### 4.2 Tiền điều kiện — bước O0, không có chuyển động

Toàn bộ danh sách phải PASS mới sang O1; nếu fail thì hiện đúng dòng bị fail:

| # | Kiểm tra | Fail thì |
|---|---|---|
| 1 | 7 trục + Ezi-IO đều `IsConnected` | alarm mất kết nối, dừng |
| 2 | Không trục nào `ALM` | yêu cầu RESET trước |
| 3 | Mạch an toàn đóng: EMG nhả, rèm sáng thông | yêu cầu nhả EMG / rút vật khỏi rèm |
| 4 | Áp suất khí đủ (DI07) | alarm thiếu khí |
| 5 | ~~Không có sản phẩm trên 2 tay hút~~ | **Bỏ (Tuấn, 2026-09-26)** — về gốc được cả khi tay hút còn hàng, kể cả khi đang có yêu cầu lấy hàng ra (§5.2). Về gốc nâng Z lên trước rồi mới chạy ngang, hàng đi theo tay như lúc chạy Auto. Chạy Auto sau đó thì nhận hàng trên tay, bỏ bước hút, soi lại rồi đặt (§3.7). |
| 6 | Mode = Manual hoặc state ∈ {NOT_HOMED, IDLE} | không cho origin khi đang RUNNING |
| 7 | Đĩa còn dung lượng ghi log | warning |

### 4.3 Trình tự — an toàn theo thứ tự "nâng lên trước, tách ra sau"

```
O1  SERVO_ON      Servo ON 7 trục, chờ S_RDY tất cả (timeout 5 s)

O2  HOME_Z        Z1 ∥ Z2 về gốc ĐỒNG THỜI, chiều đi LÊN
                  ── nâng đầu hút ra khỏi mọi thứ trước khi có chuyển động ngang

O3  GATE_Z_SAFE   CHỐT CHẶN: đọc lại vị trí thực Z1, Z2; cả hai phải ≥ Z_SAFE
                  và cả hai OrgOk. Không đạt → huỷ toàn bộ, KHÔNG đi tiếp.

O4  HOME_A        A1 ∥ A2 về gốc  (tuần tự nếu pitch tay hút hẹp — xem ghi chú)

O5  HOME_XY       Y1 ∥ Y2 ∥ X về gốc đồng thời — lúc này đều nằm trong mặt phẳng an toàn

O6  STANDBY       di chuyển tới vị trí chờ cấu hình trong system.json:
                  X_standby, Y1/Y2 ở vị trí nạp tray (phía operator), A1/A2 = 0°
O7  FINISH        OrgOk = true, reset con trỏ ô của RunningData nếu operator chọn,
                  state → IDLE, đèn xanh nháy
```

**Vì sao thứ tự này:** hai nguy cơ va chạm duy nhất của máy là (a) đầu hút còn thấp mà
X/Y1/Y2 chạy ngang, (b) đầu hút xoay khi còn nằm trong hốc tray. O2 + chốt chặn O3 khử
cả hai, và O3 là điều kiện *đọc lại vị trí thực*, không phải giả định từ việc lệnh O2 đã
trả về thành công.

**Chiều và cách về gốc:**

1. **Đã chốt: chiều về gốc của Z1, Z2 là ĐI LÊN.** Trước 2026-09-09 ràng buộc này còn được
   bảo vệ thêm bằng phần mềm (`HomeDirectionGuard` — đã bỏ, xem ngay dưới đây), nay chỉ còn
   do tham số trong driver quyết định.
2. **Đã chốt (§11 #10): A1/A2 có cảm biến gốc và xoay tự do**, không va nhau. Về gốc
   **song song** ở bước O4. Cờ `HomeRotateSequential` giữ lại (mặc định false) để đổi
   được nếu sau này gá đầu hút to ra tới mức đường kính bao khi xoay vượt pitch hai tay.

   **Đã chốt (Tuấn, 2026-09-12): cảm biến gốc của A1/A2 KHÔNG nằm đúng tư thế gắp khay
   input** — nó nằm ở đâu là do lắp đặt. `AxisConfig.HomeOffset` của trục quay chính là
   **góc máy tại vị trí cảm biến gốc**, và `FastechAxis` cộng nó vào vị trí đọc từ driver:
   toạ độ app dùng là toạ độ máy, còn driver vẫn đếm từ 0 tại cảm biến của nó. Sửa ô "Offset
   gốc" ở trang Setting có hiệu lực ngay, không phải nạp lại tham số driver.

   **Cách đo ở P1** (làm một lần cho mỗi tay, ghi lại vào system.json):

   1. Đặt `HomeOffset = 0`, về gốc A1.
   2. Jog A1 tới đúng tư thế gắp khay input — tư thế muốn gọi là 0°.
   3. Đọc góc đang hiện, gọi là θ.
   4. Đặt `HomeOffset = (360 − θ) mod 360`, lưu, về gốc lại.
   5. Kiểm: tư thế gắp phải đọc 0.0°, và về gốc lại phải ra đúng `HomeOffset` mỗi lần.

   Với X/Y/Z thì offset gốc nằm trong tham số về gốc của driver (Pr5.xx, PANATERM) chứ
   **không** phải ở trường này — đừng đặt khác 0 cho chúng mà mong toạ độ dịch theo.
3. **Đã chốt (§11 #14): về gốc bằng cảm biến DOG, và phải bắt XUNG Z sau khi chạm DOG**
   (`AxisConfig.HomeUseZPhase`, tham số Pr5.xx của A6SF / thanh ghi origin của Ezi-Servo).

   Đây không phải chi tiết cấu hình vặt. Máy dùng encoder tương đối nên về gốc **mỗi lần
   bật máy**, và mọi điểm teach đều đo từ cái gốc đó — **độ lặp lại của gốc chính là sai
   số của mọi điểm teach sau mỗi lần khởi động**. Dừng ở cạnh DOG lặp lại được cỡ
   ±0.05–0.5 mm tuỳ cảm biến, tốc độ tiếp cận và nhiệt độ; bắt xung Z lặp lại trong một
   xung encoder. Chênh lệch đó lớn hơn cả ngưỡng nghiệm thu hiệu chuẩn camera (0.02 mm) —
   tức là teach hôm nay đúng, mai bật máy lên đã sai, và triệu chứng sẽ giống hệt "vision
   đo sai" nên rất tốn công tìm.

   **Nghiệm thu ở P1 (bắt buộc, làm trước khi teach bất cứ điểm nào):** về gốc 10 lần
   liên tiếp cho từng trục, mỗi lần xong chạy tới cùng một vị trí đích rồi ghi
   `ActualPosition`. Độ phân tán (max − min) phải ≤ 0.02 mm với X/Y/Z và ≤ 0.05° với
   A1/A2. Không đạt thì dừng lại chỉnh cảm biến DOG hoặc tốc độ tiếp cận, đừng teach vội.

**`HomeDirectionGuard` — ĐÃ BỎ (2026-09-09, theo yêu cầu Tuấn).** Từng có: mỗi trục khai
báo `HomeDir` (+1 / −1) trong `system.json`; với Z1, Z2 là chiều đi lên; trong khi chạy O2,
phần mềm giám sát `ActualPosition`, nếu trục đi **ngược** `HomeDir` quá `HomeReverseTolerance`
(mặc định 2 mm) so với vị trí xuất phát thì dừng ngay, alarm `+05`, huỷ origin.

Lý do CŨ khi còn chốt chặn này: chiều về gốc là **tham số trong driver** (Pr5.xx của A6SF),
sau này thay driver, nạp lại tham số hoặc ai đó sửa nhầm một ô trong phần mềm cấu hình của
Panasonic là đủ để lệnh về gốc lao đầu hút xuống tray — cơ khí đúng hôm nay không đảm bảo
tham số đúng sau sáu tháng.

Tuấn xác nhận chủ động cấu hình/khoá chiều về gốc ngay trong driver (Pr5.xx A6SF/EziMOTION)
và chấp nhận bỏ hẳn lớp kiểm tra chéo phần mềm này — không giữ cờ bật/tắt, xoá thẳng khỏi
`PanasonicAxis.HomeAsync`/`FastechAxis.HomeAsync`/`FakeAxis.HomeAsync` cùng toàn bộ hạ tầng
mô phỏng/self-test đi kèm (`SimHomeReverse`, nút "Home↕" ở trang Manual, `TestHomeDirectionGuard`).
**Rủi ro còn lại nằm hoàn toàn ở cấu hình driver** — nạp lại tham số hoặc sửa nhầm PANATERM sau
này sẽ không còn được phần mềm phát hiện; vẫn kiểm tra bằng tay từng trục ở trang Manual trước
lần chạy `Routine_Origin` đầu tiên (P1) và sau bất kỳ lần nạp lại tham số driver nào.

**Chi tiết thực thi:**

- Xác nhận hoàn tất bằng **`ORG_CMP`** (Panasonic) / bit origin của `FAS_GetAxisStatus`
  (Fastech) — không phải chỉ chờ `BUSY` xuống 0.
- Timeout mỗi trục cấu hình được, mặc định 60 s (`WaitForMotionComplete` mặc định 30 s
  của `ServoService` là quá ngắn cho hành trình dài, phải truyền tham số).
- **Origin offset** riêng từng trục: gốc cơ khí ≠ toạ độ 0. Lưu trong `system.json`.
- Chạy song song bằng `Task.WhenAll` với **một `CancellationToken` chung** — một trục
  fail thì huỷ hết.

### 4.4 Huỷ giữa chừng

Bất kỳ tình huống nào sau đây → huỷ ngay, tất cả trục `DecelerateStop`, `OrgOk = false`,
state → `NOT_HOMED` (hoặc `ALARM`/`EMG`), ghi log **trục nào ở bước nào** đã gây huỷ:

EMG · rèm sáng bị cắt · alarm driver · timeout một trục · mất kết nối · operator bấm STOP ·
chốt chặn O3 không đạt.

Không bao giờ để trạng thái "về gốc dở" được đánh dấu là đã về gốc.

### 4.5 Về gốc / Move Abs / Jog / Inching từng trục ở trang Manual

Cho phép, dùng cho lúc chỉnh máy — nhưng vẫn qua interlock trong `McFunction`. Mỗi trục có 4
cách chạy tay: **Home** (về gốc), **Move Abs** (nhập toạ độ đích), **Jog +/−** (giữ nút chạy
liên tục), **Inching** (nhấn 1 lần dịch đúng `StepSize` đã nhập — Fastech dùng lệnh Inc gốc của
driver `FAS_MoveSingleAxisIncPos`, Panasonic mô phỏng bằng `MoveAbs(CurrentPos + step)` vì
PANATERM chỉ có 4 block Home/Abs/Jog+/Jog− sẵn, không có block Inc riêng).

**Home và Move Abs của X/Y1/Y2 có thêm một điều kiện mà Jog và Inching KHÔNG có:** ngoài điều
kiện chung `CanMove` (an toàn, phanh, Z1/Z2 đang ở trên `Z_SAFE`), hai nút này còn đòi **Z1 VÀ
Z2 đã về gốc xong** (`OrgOk`), không chỉ đang ở độ cao an toàn — về gốc/Move Abs cho X/Y1/Y2
lúc Z chưa từng về gốc thì toạ độ Z đang đọc được có thể không phải toạ độ thật. Không áp cho
Jog/Inching vì hai lệnh đó là "giữ/nhấn rồi thả", operator quan sát trực tiếp trong lúc chạy —
khác Home/Move Abs là lệnh "bấm rồi trục tự chạy tới đích" không giám sát được liên tục.

Về gốc riêng một trục **không** đặt `OrgOk = true` cho cả máy; muốn vào Auto vẫn phải chạy đủ
chuỗi §4.3. Điều kiện Z1/Z2 `OrgOk` ở trên **chỉ áp dụng cho nút bấm trang Manual**, không đưa
vào `McFunction.MoveAsync` dùng chung với `Routine_Auto`/`Routine_Calib` — để nguyên `CanMove`
cho hai routine đó, vì trong lúc xử lý NG/Resume, `Routine_Auto` có thể hợp lệ gọi di chuyển
X/Y trong khi một trục Z vừa mất `OrgOk` thoáng qua.

---

## 5. Chu trình chạy tự động

### 5.1 Điều kiện cho phép chạy (kiểm tra ở S00 và liên tục trong khi chạy)

- `OrgOk = true`, mode = Auto, không alarm mức Error/Fatal.
- Mạch an toàn đóng, áp suất khí đủ.
- Có tray đầu vào (DI10) và tray lắp ráp (DI11) — **chỉ kết luận được khi trục hàng của tray đó đang
  đứng ở vị trí chờ** (`AxisConfig.StandbyPos`, cửa sổ 1 mm). Cảm biến gắn cố định ở vị trí chờ, không
  chạy theo tray: tray vào vùng làm việc là mất tín hiệu, mà mất tín hiệu ở đó chỉ có nghĩa *không đo
  được*, không phải *thiếu tray* (Tuấn nêu 2026-09-24). Trước ngày đó App đọc thẳng hai DI ở đầu mỗi
  vòng nên từ vòng thứ hai máy sẽ dừng oan — xem `Routine_Auto.TrayMissingAt`.
- Đã nạp `ModelData` và `VisionCalib` hợp lệ, chưa quá hạn hiệu chuẩn.
- **Interlock chống va:** X, Y1, Y2 chỉ chuyển động khi **cả Z1 và Z2** ≥ `Z_SAFE`.
  Kiểm tra trong `McFunction`, không phải trong routine.
- Soft limit theo từng trục, kiểm tra trước khi phát lệnh.

### 5.2 Chuỗi Auto (mỗi vòng xử lý 2 sản phẩm)

```
S00 READY        kiểm tra điều kiện; dọn ô Processing mồ côi; nâng Z an toàn
S05 SCAN_ASSY    quét trước MỌI ô trống còn lại của tray lắp ráp bằng CAM top-down (§5.2b);
                 tự bỏ qua nếu đã quét đủ — chỉ chạy thật sau khi đổi tray lắp ráp
S10 PICK_1       chỉ hút nếu còn ô lắp ráp trống ; Y1 → hàng r1 ; X → cột c1 ; Z1↓ ; vacuum N1 ON ; chờ vac sensor ; Z1↑
S11 PICK_2       chỉ hút nếu số ô trống > số hàng đang cầm ; ô kế tiếp ; Z2↓ ; vacuum N2 ON ; chờ vac sensor ; Z2↑
S20 ALIGN_1      X → trên CAM bottom-up (tay #2 soi TRƯỚC, nó gần CAM hơn) ; trigger AlignBottom → pose
S21 ALIGN_2      X → vị trí soi tay #2 ; trigger AlignBottomTool2 → pose p2
S30 LOCATE_1     tra kết quả LocateTop đã quét trước cho ô đích #1 (§5.2b) ; tính (Δθ, X, Y2)
S31 PLACE_1      A1 xoay ∥ X, Y2 → vị trí đã bù ; Z1↓ ; vacuum OFF + blow ; Z1↑
S32 LOCATE_2     tra kết quả đã quét trước cho ô đích #2
S33 PLACE_2      A2 xoay ∥ X, Y2 → vị trí đã bù ; Z2↓ ; vacuum OFF + blow ; Z2↑
S40 INDEX        ghi cycle time ; ghi RunningData ; cập nhật UI
S50 TRAY_CHECK   hết tray in → WAIT_OPERATOR đổi tray in (giữ tiến độ tray assy)
                 hết tray assy → WAIT_OPERATOR đổi tray assy (tay còn hàng thì giữ, Warning), xoá cache quét trước (§5.2b)
                 đổi tray xong luôn về S00: quét tray mới, soi lại sản phẩm đang cầm rồi mới đặt
```

**Giao ô cho hai tay — theo HÌNH HỌC, không theo thứ tự ghé** (Tuấn chốt 2026-09-24). Tay #1 nằm lùi
~63 mm so với tay #2, xa hơn một bước ô (~25 mm), nên điểm X xa nhất mà gantry phải với tới trong cả
vòng chính là chỗ đứng để **tay #1** lên ô của nó. Vì vậy **tay #1 nhận ô có X nhỏ hơn, tay #2 nhận ô
có X lớn hơn** — quãng đường một vòng bằng `2 × (xa nhất − gần nhất)` nên mỗi bước ô tiết kiệm được
trả hai lần. Quét tuần tự thì luật này trùng hành vi cũ; quét **zigzag** thì hàng chạy ngược đảo thứ
tự ghé, và luật cũ ("ô ghé trước là của tay #1") bắt gantry với thêm một bước ô ở mọi vòng của những
hàng ấy — ăn mất quá nửa phần lợi zigzag kiếm được ở chặng quét tray. Xem `Routine_Auto.AssignCell`.

**Giao ô HÚT cũng theo hình học** — gương của luật trên, soi đầu kia của vòng. Quãng đường một vòng
là `2 × (xa nhất − gần nhất)`: đầu **xa** do ô lắp ráp của tay #1 quyết (đoạn trên), đầu **gần** do ô
hút quyết, vì tray đầu vào nằm cuối bên trái. Nên cặp ô hút được ghép sao cho **điểm quay đầu nằm xa
về bên phải nhất**. Cặp ô liền nhau trong một hàng thì ra đúng hành vi cũ; chỗ khác là **cặp vắt qua
hai hàng** (ô cột cuối hàng trên ghép với ô cột đầu hàng dưới) — luật cũ giao ô cột đầu cho tay #2,
tay lùi 63 mm, nên gantry với thêm 63 mm về bên trái ở mọi cặp vắt hàng. Tay #1 hút trước nên nó là
chỗ duy nhất còn nhìn thấy cả hai ô; tới lượt tay #2 thì ô kia đã mang nhãn `Processing`, cứ lấy ô
`Loaded` đầu tiên là đúng ô còn lại. Xem `Routine_Auto.AssignPickCell`.

**Zigzag hay tuần tự?** Chặng hút–đặt gần như bằng nhau (mỗi vòng đều chạy về tray đầu vào, quãng
đường chỉ phụ thuộc ô nào được dùng chứ không phụ thuộc thứ tự). Cái lợi nằm ở **chặng quét tray lắp
ráp**: tuần tự phải chạy ngược cả chiều ngang tray mỗi lần đổi hàng. Tray 5×8 bước 25 mm: 7 × 25 =
175 mm × 4 lần đổi hàng = **~700 mm mỗi lượt quét cả tray**.

Ranh giới giữa các state là **checkpoint** cho STOP (§3.4) — kể cả S05: mỗi lượt gọi chỉ quét
đúng một ô rồi mới xét lại yêu cầu dừng, không quét hết cả tray trong một lượt.

**Trạng thái chu trình là dữ liệu, không phải vị trí con trỏ lệnh.** `EAutoState` nằm
trong `RunningData` và được ghi xuống đĩa ở mỗi ô hoàn tất, nên trong cùng một phiên, STOP
rồi chạy lại tiếp được từ đúng bước. Tắt app hoặc mất điện rồi mở lại thì **không** chạy tiếp:
phải về gốc (Origin) lại toàn bộ máy, chu trình bắt đầu lại từ S00 với tray và tay hút trống
(`RunningData.ResetCycleProgress`). Đây là lý do chuỗi được viết thành vòng lặp trên một
enum thay vì một hàm dài chạy tuần tự.

**Xoay dồn về sau bước LOCATE, khác bản phác thảo đầu tiên.** Bản đầu đặt lệnh xoay ở
S20/S21, nhưng góc đích `θ_t` chỉ biết được sau khi CAM top-down soi ô đích ở S30 — xoay
ở S20 thì phải xoay lần thứ hai. Toàn bộ lượng xoay dồn vào bước PLACE, khi đó trục A
chạy **song song** với X/Y2 nên không tốn thêm giây nào của chu kỳ. Cùng lý do đó,
`e` (độ lệch tâm sau khi xoay) cũng chỉ tính được sau LOCATE, nên thứ tự này là bắt buộc
chứ không phải một tối ưu.

**Số sản phẩm mỗi vòng là 1 hoặc 2, không cố định.** Hai tray có số ô khác nhau và cấu
hình được (§5.3), nên tray đầu vào có thể hết ngay sau khi hút được một sản phẩm. Chuỗi
xử lý đúng số sản phẩm đang thực sự nằm trên tay hút — mọi bước đều bỏ qua nếu tay hút
tương ứng trống.

**Chỉ hút khi còn chỗ đặt (Tuấn chốt 2026-09-13).** S10/S11 chỉ hút khi số ô lắp ráp còn trống
**nhiều hơn** số sản phẩm đang nằm trên tay hút. Còn ô trống nào chưa quét thì quay về S05 quét xong rồi
mới đếm, nên ô được đếm luôn là ô đã quét thấy chi tiết đích. Còn đúng một ô thì chỉ tay #1 làm.

**Còn hàng trên tay mà hết ô đặt: giữ hàng, chỉ báo Warning (Tuấn chốt lại 2026-09-13).** S1x hết hẳn ô,
hoặc S30/S32 không còn ô trống, thì đi thẳng S50: báo 5020 (Warning) và chờ đổi tray lắp ráp, sản phẩm
vẫn nằm trên tay hút. Đổi tray xong chu trình **luôn** chạy lại từ S00: S00 bỏ tư thế đo cũ, S05 quét tray
lắp ráp mới, tay hút ra CAM bottom-up soi lại, rồi đặt vào ô mới. Cùng cách chạy lại này áp dụng khi bấm
"Nạp lại tray lắp ráp" (dù tay có đang cầm hàng hay không) và khi bước đặt dừng giữa chừng **trước khi nhả
hút** — ô đích đã nhận được trả về trống ngay. Đổi cả tray đầu vào trong lúc tay còn hàng thì số ô nguồn của
sản phẩm đó thuộc tray cũ nên bị bỏ (sản phẩm thành "không rõ ô nguồn").

**Sự cố với hàng trên tay hút: dừng một lần, chạy lại thì nhận hàng còn trên tay (Tuấn chốt 2026-09-13,
đổi 2026-09-26).** Ba tình huống bật cờ `RunningData.NozzleClearRequired`, đều là lúc sản phẩm có thể đã
hỏng hoặc không trả về được: (1) bước đặt dừng giữa chừng **sau khi đã nhả hút** — sản phẩm có thể đã nằm
trong ô hoặc dính trên đầu hút; (2) trả về ô nguồn mà cảm biến vẫn báo còn hàng — dừng trước khi ghi sổ;
(3) phải trả về ô nguồn (align fail, Group Cmd báo lỗi, lượng bù vượt ngưỡng) mà không rõ ô nguồn hoặc tray
đầu vào chưa vào máy — báo 5031 dạng sự kiện, RESET gỡ được (trước 26/09 dùng chung 5030). Cờ bật thì chu trình dừng ở ranh giới bước kế tiếp. Lần bấm chạy sau App gỡ cờ
và ghi sổ theo cảm biến: tay **còn hàng** thì nhận hàng và chạy tiếp, bỏ bước hút, soi lại ở CAM bottom-up
rồi đặt (trước 26/09 bắt lấy hàng ra); tay **đã trống** thì ghi tay trống, ô nguồn đang `Processing` thành
`Empty`. Chu trình chạy lại từ S00. Chỉ còn chặn khi chưa đọc được cảm biến. Tình huống (3) mà soi lại vẫn
hỏng thì dừng lại y như cũ. Bật/tắt hút, Jog và Origin không bị chặn (Origin bỏ chặn 2026-09-26). Cờ ghi
trong running.json và không bị xoá khi mở lại app.

**Ô `Processing` là ô đã có tay hút nhận.** Hàm tìm ô kế tiếp **không** được nhận ô mang
nhãn này, nếu không tay #2 sẽ lao tới đúng ô mà tay #1 vừa lấy hàng. Nhãn còn sót lại
sau một lần dừng bất thường được dọn ở S00 (`ReleaseOrphanCells`) — không dọn thì mỗi lần
dừng máy lại mất vĩnh viễn vài ô.

### 5.2b Quét trước tray lắp ráp (S05_ScanAssy) — đổi lấy cycle time

**Bản đầu** trigger `LocateTop` ngay trước từng lần đặt (di chuyển CAM tới đúng ô đích,
chụp, đặt — lặp lại cho từng ô trong số ~20-30 ô một tray). **Bản hiện tại** tách rời hai
việc: `S05_ScanAssy` soi hết MỌI ô còn trống của tray lắp ráp **một lần** trước khi bắt đầu
hút/đặt, giữ kết quả trong bộ nhớ; `S30/S32` chỉ **tra lại** kết quả đó, không còn di
chuyển CAM hay trigger vision ngay trước mỗi lần đặt nữa — bớt hẳn phần lớn thời gian di
chuyển + chụp trên đường găng của mỗi chu kỳ.

**Đánh đổi đã CHẤP NHẬN (quyết định của Tuấn, 2026-08-02):** `LocateTop` đo trước lúc đặt
đúng, có thể vài phút, cho những ô cuối của một tray lớn — sai lệch cơ khí phát sinh
*giữa* lúc quét và lúc đặt thật (trôi nhiệt, backlash tích luỹ...) sẽ không được bắt kịp
như cách đo-ngay-trước-khi-đặt của bản đầu. Đổi lấy chu kỳ nhanh hơn.

**Mốc camera dùng chung cho mọi tay hút.** `S05_ScanAssy` luôn di chuyển CAM theo hand-eye
của tay #1, bất kể sau này tay nào thật sự đặt vào ô đó — đúng theo công thức
`targetX = captureX + top.X + handEyeDx(tay đặt)` (§6.6): `captureX + top.X` đã là toạ độ để
TÂM CAM trùng chi tiết đích, không phụ thuộc tay nào đứng đó lúc chụp; `handEyeDx` của tay
THẬT SỰ đặt mới quy đổi sang toạ độ của tay đó lúc dựng đích. Không cần quét riêng cho từng
tay — xem chi tiết ở doc-comment `Routine_Auto._assyScan`.

**Không bắt buộc phải quét trước mới đặt được.** Nếu một ô nào đó thiếu trong cache (tray
đổi giữa chừng, cache bị dọn...), `S30/S32` tự rơi về đo trực tiếp như bản đầu cho đúng ô
đó — quét trước là một tối ưu, không phải điều kiện tiên quyết.

**Bộ nhớ, không phải đĩa.** Kết quả quét trước chỉ giữ trong RAM của `Routine_Auto`, mất
khi tắt app — giống `_bottomPose` (đo lại từ đầu sau khi bật lại, không tin vào số đo của
phiên chạy trước). Bị xoá khi tray lắp ráp đổi mới, dù đổi từ chính Auto (S50, hết tray)
hay operator bấm "Nạp lại tray lắp ráp" bằng tay ở trang Main — cả hai đường đều gọi
`Routine_Auto.InvalidateAssyScan()` để không lỡ dùng số đo của tray VẬT LÝ cũ.

### 5.3 Chênh lệch số ô giữa hai tray

Số ô hai tray **không bằng nhau và cấu hình được** — với mặc định 4×5 = 20 ô đầu vào và
5×6 = 30 ô lắp ráp thì một tray lắp ráp cần 1.5 tray đầu vào. Chuỗi Auto **không được
giả định tỉ lệ nào cả**: logic phải đúng với mọi cặp `(R₁×C₁, R₂×C₂)`, kể cả khi số ô
tray đầu vào lớn hơn tray lắp ráp. Hệ quả thiết kế:

- `TrayState` của hai tray là hai đối tượng độc lập, mỗi cái có con trỏ ô riêng.
- Ghi `RunningData` xuống đĩa sau mỗi ô hoàn tất và khi dừng khẩn cấp — phục vụ log, KHÔNG
  còn dùng để resume qua một lần tắt máy: mở lại app luôn bắt buộc Init lại toàn bộ (§8).
- Trang Main phải cho phép reset riêng từng tray, và đánh dấu thủ công ô trống/đã có hàng.

### 5.4 Xử lý NG (đã chốt)

**Chặng chậm hai chiều quanh hốc tray (`Z_Slow`).** Hạ xuống: mức Auto CAO từ cao độ an toàn tới điểm
giảm tốc, rồi mức THẤP nốt đoạn tới cao độ hút/đặt (2026-09-18). **Nâng lên dùng lại chính cao độ ấy,
đối xứng** (Tuấn yêu cầu 2026-09-24): mức THẤP từ cao độ hút/đặt lên tới điểm giảm tốc, rồi mức CAO nốt
đoạn còn lại — `McFunction.MoveZUpAsync`, cùng phép xét với chiều xuống, chỉ đổi chỗ chỗ-đứng và đích.
Lý do giống hệt chiều xuống: nhấc nhanh khỏi hốc thì lúc hút sản phẩm dễ xê dịch trên đầu hút, lúc đặt
thì luồng khí và cái gờ dễ kéo theo con vừa nhả. Áp cho cả bảy chỗ Z nâng khỏi hốc — Auto (hút, đặt,
trả về ô nguồn), chạy từng bước, bước xác định hand-eye, chạy rà, và nút ở trang Teaching. Giá: khoảng
**+1.6 s mỗi sản phẩm** với số đã teach hiện tại.

Mỗi lần hạ/nâng hai chặng ghi **một dòng log thời gian thật của từng chặng**
(`McFunction.LogTwoStageZ`, 2026-09-24). Lý do: phần Z của phép tính thời gian chu trình là suy từ cấu
hình chứ chưa có số đo nào — log máy không có lần nào chạy Z trong Auto — mà riêng bốn chặng chạy chậm
đã chiếm hơn một phần tư chu kỳ. Dòng này cũng cho thấy ngay một trục Z bắt đầu ì.

**Cú dừng ở điểm giảm tốc.** Chuyển động hai chặng **bắt buộc dừng hẳn** giữa hai chặng: driver
Panasonic A6SF nhận từng block Move Abs một. Chỉ rút ngắn được thời gian ramp, không bỏ được cú dừng.
Ô AccelTime/DecelTime ở trang Setting là **thời gian ramp ở tốc độ Auto cao**; lệnh chậm hơn dùng cùng
gia tốc đó nên ramp ngắn lại theo tỉ lệ (2026-09-25) — Z đặt 300 ms ở 300 mm/s thì chặng 6 mm/s chỉ ramp
6 ms. Trước đó thư viện nhân lại độ dốc theo tốc độ **từng lệnh** (`reg = AccelMs × 3000/rpm`) rồi
`ClampRampRegister` cắt ở 10000, nên chặng chậm mắc ở ramp 120–200 ms bất kể AccelTime đặt bao nhiêu. Đổi
vì độ trễ lệnh, không vì ramp: độ dốc nhân theo từng lệnh bắt `MoveAbsolute` ghi lại hai thanh ghi acc/dec
trước mọi lệnh đổi tốc độ, mà Z hai chặng đổi tốc độ ở mọi lệnh (§2, `ServoAxisConfig.AbsoluteRampRpm`).
Gia tốc không bao giờ gắt hơn mức Auto cao đã chạy bấy lâu. Đổi lại, chặng hạ chậm giữ đủ tốc độ Auto
thấp tới sát cao độ đặt, thay vì chậm dần trong đoạn cuối (khoảng 0.4 mm với 6 mm/s đang cài).

**Góc hút (`A_Pick`, Tuấn yêu cầu 2026-09-23).** Đầu hút có gờ ôm lấy sản phẩm, nên mọi lần hạ Z
xuống hút đều xoay trục A về **góc hút đã teach của đúng tay đó** (`ModelData.PickAngle`, teach ở
trang Teaching) trước khi hạ — xoay sau khi Z lên cao độ an toàn, song song với X/Y nên không tốn
thêm thời gian chu kỳ. Trước 23/09 không chỗ nào đưa A về đâu cả: đặt xong A nằm nguyên ở `CmdA`
rồi đi hút con kế tiếp bằng chính góc ấy, gờ không ăn khớp, hở khí, hút hụt. Áp cho cả bốn đường
hút: chu trình Auto, nút hút ở chạy từng bước, bước xác định hand-eye, chạy rà.

| Tình huống | Xử lý |
|---|---|
| Hút hụt (không có tín hiệu vacuum sensor) | retry N lần (mặc định 2), Z hạ thêm `ΔZ_retry` |
| Retry vẫn hụt | đánh dấu ô đầu vào là NG/trống, bỏ qua, sang ô kế |
| Align fail (vision không tìm thấy / score thấp) | trả sản phẩm về **đúng ô cũ**, đánh dấu ô NG, sang ô kế |
| Locate fail (top-down không thấy chi tiết đích) — ở S05 quét trước hoặc S30/S32 đo dự phòng | đánh dấu ô đó `Skipped`, bỏ qua; nếu đang giữ sản phẩm (S30/S32) thì chuyển sang ô đích kế tiếp; hết ô thì giữ trên tay, báo Warning, chờ đổi tray lắp ráp rồi chạy lại từ S00 |
| Rơi sản phẩm giữa chừng (mất vacuum) | dừng chu trình, alarm 5002, yêu cầu người kiểm tra |
| NG liên tiếp > `MaxConsecutiveNg` (mặc định 5) | dừng chu trình, alarm 5010 — dấu hiệu hỏng cơ hoặc sai teach |
| Alarm driver / EMG / rèm sáng bị cắt | dừng ngay, đèn đỏ, ghi log |

Máy **không dừng** với NG thường — tiếp tục chạy, tô đỏ ô trên lưới UI, tăng counter NG.
Ngưỡng NG liên tiếp là chốt chặn để máy không âm thầm hỏng cả tray.

### 5.5 Chạy rà — chu trình không sản phẩm (Tuấn yêu cầu 2026-09-23)

Cho máy chạy liên tục **không sản phẩm** để rà cơ khí. `Routine_DryCycle`, tách hẳn khỏi
`Routine_Auto` — không nhét cờ `if (dry)` vào chu trình thật.

| | Chạy rà làm gì |
|---|---|
| Trục | X, Y, Z, A đi đúng quỹ đạo chu trình thật: **quét tray lắp ráp bằng CAM top-down** → hút ở tray đầu vào → ra vị trí soi CAM dưới → xoay A rồi đặt vào tray lắp ráp, đủ cả hai tay |
| Quét tray | theo đúng `ScanAssyByRow` của chu trình thật — tắt thì quét cả tray một lượt ở đầu mỗi lượt tray, bật thì quét một hàng ngay trước khi làm hàng đó. Đây là chặng gantry chạy nhiều nhất, bỏ ra thì không rà đúng quãng đường máy đi (Tuấn hỏi 2026-09-24) |
| Góc A | hút: về **góc hút đã teach** như chu trình thật. Đặt: **góc hút + lượng bù**, lượng bù chạy vòng `0 → +⅓ngưỡng → 0 → −⅓ngưỡng`, đếm RIÊNG từng tay. **Chỉ quét một phần ba ngưỡng** vì chạy rà hạ đầu hút RỖNG vào hốc tray ở góc bất kỳ — xoay tới biên là ép gờ đầu hút vào thành hốc mà không rà thêm được gì (Tuấn chốt 24/09). Đúng công thức `CmdA = góc lúc chụp + góc bù` của chu trình thật |
| Khí | **không đụng** van hút, van thổi (Tuấn chọn) |
| Vision | **không kích Flow nào** — không cần VisionMaster mở |
| Sổ sách | không cộng bộ đếm OK/NG, không đổi trạng thái ô, không lưu tiến độ |
| Dừng | STOP như Auto; checkpoint giữa các chặng, chặng nào cũng kết thúc với Z ở cao độ an toàn |

**Hạ Z xuống hốc trống không va**: cao độ hút/đặt teach với sản phẩm trong hốc, tức đầu hút dừng ở
mặt trên sản phẩm. Bỏ sản phẩm ra thì đầu hút dừng đúng chỗ cũ, lơ lửng trên đáy hốc đúng bằng chiều
dày sản phẩm. Chạy rà đi đúng cao độ đã teach và **không có** nhánh "hút hụt thì hạ sâu thêm" của §5.4.

**Khởi động vẫn qua nút cứng.** Nút gạt ở trang Main chỉ *chọn chế độ*; máy chạy thật vẫn bắt buộc
chìa AUTO → Armed → bấm Start 1 ở cụm B — §3.2 không có ngoại lệ nào, kể cả chu trình
không sản phẩm. Cờ chế độ **không lưu xuống đĩa**: mở lại app là tắt.

Chặn: đang chạy thì không gạt được; tay hút còn sản phẩm thì không bật được (không có bước nhả nào
trong chu trình rà, còn hàng là dúi sản phẩm xuống hốc).

---

## 6. Vision — hiệu chuẩn và bù sai số

**CẬP NHẬT LỚN (Tuấn chốt 2026-09-12).** Toàn bộ phép hiệu chuẩn và phép tính bù chuyển vào
**solution VisionMaster**; App không còn giữ ma trận camera nào, không còn tự fit, không còn tự
nhân `R(Δθ)`. Lý do và hệ quả nằm ở §6.0 ngay dưới. Các mục sau đã viết lại theo quyết định này;
nội dung cũ (fit affine 9 điểm bằng C#, wizard checkerboard, `CameraCalib`, `BottomUpYSign`) đã
**xoá hẳn khỏi code**, không giữ lại bản song song để tránh hai nguồn sự thật cho cùng một con số.

### 6.0 Ranh giới: VisionMaster làm gì, App làm gì

| Việc | Ai làm | Ghi chú |
|---|---|---|
| Quy đổi pixel → mm | **VisionMaster** | Module *Calibration Transformation* ở cuối mọi Flow đo tư thế |
| Hiệu chuẩn CAM top-down (9 điểm) | **VisionMaster** | Module *N-point Calibration*, chế độ Trigger Acquisition |
| Hiệu chuẩn CAM bottom-up (thang đo + tâm xoay) | **VisionMaster** | Phương án cụ thể chưa chốt — xem §6.4 |
| Dạy tư thế mẫu (Master) | **VisionMaster** | Nút *Create Reference* trong Flow runtime |
| Tính lượng bù Align (Δθ, lệnh X/Y2) | **VisionMaster** | Module *Single Point Alignment* trong Group Cmd của Flow `AlignBottom_ToolN` — §6.6 |
| Chạy trục tới từng điểm/góc rồi kích Flow | App | VisionMaster không điều khiển trục |
| Dựng toạ độ đích từ encoder | App | VisionMaster không đọc được vị trí trục |
| Kiểm tra lại hiệu chuẩn bằng chuyển động thật | App | §6.2b và §6.4b — VisionMaster không tự kiểm được |
| Hand-eye (offset camera ↔ đầu hút) | App | §6.5, là hiệu toạ độ trục |
| Dấu góc VisionMaster ↔ trục A | App | Đo bằng §6.4b, ghi vào model |

**Vì sao chọn hướng này:** sửa được bằng GUI VisionMaster, không phải build lại app mỗi lần đổi
mẫu hay đổi cách hiệu chuẩn — cùng lý do đã chọn ở bản 2026-09-08, nay áp dụng nốt cho phần còn
lại. **Cái giá phải trả:** App không đọc được chất lượng phép fit (residual, số điểm) nữa, nên
phải có hai phép kiểm tra độc lập bằng chuyển động trục (§6.2b, §6.4b) — nếu không thì không còn
gì bắt được một hiệu chuẩn sai trước khi máy đặt lệch hàng loạt.

### 6.1 Nguyên tắc chung (vẫn còn hiệu lực)

1. Lưới 3×3 phủ **70–80 % FOV**, không chạm mép — méo ống kính lớn nhất ở mép.
2. **Cùng một chiều tiếp cận** cho mọi điểm; Auto Calib quét kiểu rắn bò để giảm số lần đảo chiều.
3. Ghi **`ActualPosition` đọc từ encoder**, không phải vị trí lệnh.
4. Mỗi điểm chụp `CalibShotsPerPoint` ảnh, lấy trung bình → khử nhiễu đo (App làm khi ĐO, còn khi
   NẠP ĐIỂM thì mỗi lần kích Flow là một điểm nên không lấy trung bình được).
5. **Kiểm chứng độc lập bắt buộc**: §6.2b cho top-down, §6.4b cho bottom-up. Sai số dư của chính
   các điểm tham gia fit luôn lạc quan hơn thực tế, và bản thân VisionMaster không biết trục đã đi
   bao xa.
6. **Vị trí chụp lúc chạy phải trùng lúc hiệu chuẩn** (PDF Hikrobot tr.45) — teach lại
   `AlignPosX_NozzleN` là phải hiệu chuẩn lại tay đó. App ghi lại vị trí đã kiểm chứng trong
   `VisionCalib.AlignVerifiedPosX{N}`.
7. Cảnh báo khi hiệu chuẩn quá hạn (`CalibValidDays`, mặc định 90) → Warning 4090, tính từ lần
   xác minh §6.4b cũ nhất.

### 6.2 CAM top-down — lưới 9 điểm

Camera gắn trên gantry (chạy theo **X**), dấu chuẩn nằm **trên đồ gá tray lắp ráp** (chạy theo
**Y2**) → đủ chuyển động tương đối hai chiều. Dấu chuẩn **không được dán trên bàn máy**: camera
chỉ đi theo X, nên các hàng của lưới chỉ sinh ra được bằng cách chạy tray theo Y2 — dán trên bàn
thì 9 điểm co về 3 điểm thẳng hàng và phép fit suy biến.

App (trang Vision Edit): lưu *Calibration Center Point* → chạy 9 điểm quanh tâm đó theo **thứ tự
hình S** `1,2,3,6,5,4,7,8,9` (`Routine_Calib.TopDownGridSOrder`), mỗi điểm kích nhánh hiệu chuẩn của Flow
`LocateTop` (`IsTriggerLocate = 0`, §6.8) một lần.

Thứ tự hình S chỉ bắt buộc khi để **VisionMaster tự sinh** toạ độ vật lý theo quỹ đạo (Movement
First + Commutation Number) — chạy theo dòng 1→9 thì điểm 4 và 6 lệch chỗ, ma trận vẫn fit ra nhưng
méo và **không có thông báo lỗi nào**. Từ 2026-09-12 App **gửi kèm toạ độ encoder** qua biến toàn
cục (§6.8) nên ràng buộc đó hết hiệu lực (PDF Hikrobot tr.18: nạp điểm theo giao thức thì mọi quỹ
đạo đều được); vẫn giữ thứ tự hình S vì nó ít đảo chiều nhất — đúng §6.1 quy tắc 2.

Trong VisionMaster: **bật** module N-point trước khi nạp điểm; Camera Mode = **Dynamic Camera**; xong thì Calibrate →
**Create Calibration File** (Ctrl+S chỉ lưu solution, KHÔNG tạo file hiệu chuẩn) rồi **tắt** module đi. Nút "Auto Calib"
**tự bấm Clear Point** của module N-point trước điểm đầu tiên (`ModuParams.DoClearPoint()`, 2026-10-09); không xoá được
thì dừng, chưa chạy trục nào. Nút nạp lẻ (numpad, "Trigger LocateTop (hiệu chuẩn)") không xoá — mỗi lần bấm là nạp
thêm một điểm vào danh sách đang có.

Nhánh soi của Flow `LocateTop` (chạy Auto) nạp chính file hiệu chuẩn đó qua *Calibration Transformation* và trả
**mm**: `TopX_mm/TopY_mm/TopA_deg` = vị trí vật lý của chi tiết đích (Script `TopAbs` đã cộng vị trí trục lúc chụp).

### 6.2b Kiểm tra hiệu chuẩn top-down (App)

`Routine_Calib.VerifyTopDownAsync`: soi dấu chuẩn bằng nhánh soi của Flow `LocateTop`, chạy X đúng
`TopDownVerifyStepMm`, soi lại; rồi làm y hệt với Y2. Flow phải báo lượng dịch **đúng bằng quãng
đường trục đã chạy và ngược dấu**. Bắt được ba lỗi mà VisionMaster không tự thấy: sai thang đo,
**sai dấu** (khai nhầm Camera Mode, hoặc nhập toạ độ vật lý ngược chiều trục), và lẫn trục X ↔ Y.

Phép kiểm tra đi **nhánh soi** (`IsTriggerLocate = 1`) và chạy sau khi đã tắt module N-point (module chỉ bật lúc
nạp điểm, §6.8) — không thì mỗi lần đo là nạp thêm điểm rác vào danh sách.

### 6.3 CAM bottom-up — thang đo (đã bỏ wizard checkerboard của App)

**Bỏ hẳn 2026-09-12 (Tuấn chốt).** Wizard checkerboard B1–B6 (Flow `CalibBottomUp`, đối chiếu chéo
B3, `CameraCalib.BottomUp`, `BottomUpYSign`) đã xoá khỏi code: từ 2026-09-08 nó không còn nuôi phép
tính nào, chỉ còn là điều kiện chặn Auto — bắt người vận hành làm một bước không ảnh hưởng kết quả.
Hai lý do giữ nó trước đây đều hết hiệu lực:

- *"Là nguồn thang đo mm/px độc lập"* — nếu hiệu chuẩn bottom-up bằng **chuyển động trục X thật với
  chính sản phẩm đang hút** (§6.4), thang đo đo ở đúng cao độ làm việc, không cần đồ gá bản chuẩn
  và mất luôn cả lớp sai số cao độ mà §6.3 cũ phải cảnh báo dài dòng.
- *"`IsCalibrated` nằm trong `IsUsable`"* — điều kiện chạy Auto nay là §6.4b + §6.5, những thứ App
  thật sự đo được.

Nếu phương án hiệu chuẩn được chọn ở §6.4 vẫn cần bản chuẩn, việc đó làm **trọn trong VisionMaster**
(module *CalibBoard Calibration*, Physical Size = 1 mm, Board Type = Checkboard) — App không tham gia.

### 6.4 CAM bottom-up — tâm xoay và thang đo, trong VisionMaster

Bối cảnh: camera cố định nhìn lên, vật trong khung hình là sản phẩm do gantry mang — chỉ có **X**
và **A**, không có chuyển động tương đối theo Y. Vì vậy **không dựng được lưới 9 điểm**, và
"N-point 6 điểm chỉ xoay" **không tồn tại** trong VisionMaster (N-point = Translation Times +
Rotation Times, xoay tại điểm thứ 5).

**ĐÃ CHỐT: phương án A** (2026-09-12) — *Translation Calibration*, `Calibration Type =
TransAndRotateCalib`, `Camera Mode = Camera Static`, `Calibration Points Input = By Coordinate`.
Bind `ImagePointX/Y` + `Image Angle` ← *High Precision Match*, `Physical Coordinate X` ← biến toàn
cục `X`, `Physical Coordinate Y` = 0, **`World Rotate Angle` ← biến toàn cục `A1`/`A2`** đúng trục
của tay đang hiệu chuẩn (**bắt buộc** — để trống thì Calibrate báo `10103002 Matrix calculating
error`, xem §6.8). `Translation Times = 5`, `Rotation Times = 6`, `UnionCalibEnable` tắt.

Hai phương án còn lại giữ lại làm hồ sơ, không dùng:

| | Phương án | Rủi ro cần thử |
|---|---|---|
| A ✔ | *Translation Calibration* kiểu "tịnh tiến + xoay": tịnh tiến theo X, xoay tại vị trí soi | Các điểm tịnh tiến thẳng hàng — VM có thể từ chối |
| B | *CalibBoard Calibration* (thang đo) + *Rotation Calibration* (tâm xoay) | Help chỉ mô tả Rotation Calibration đi kèm file của Translation Calibration |
| C | *N-image Calibration* + bản chuẩn Hikrobot gắn trên tay hút | Cần làm bản chuẩn; phải thử xem tịnh tiến một chiều có đủ không |

**DOF = `Scale, Rotation and Translation` (Similarity)** — mục **thứ ba** trong danh sách, KHÔNG
phải mặc định (mặc định là Affine). Ba lý do, lý do thứ hai là lý do quyết định:

1. **Đúng mô hình vật lý.** Camera cố định nhìn vuông góc lên một mặt phẳng ở độ cao cố định (đáy
   sản phẩm luôn ở `Z_Pick`), cảm biến pixel vuông → ánh xạ pixel→mm *là* một phép similarity. Hai
   bậc tự do Affine thêm vào (tỉ lệ ngang/dọc, shear) không có nguồn vật lý nào ở đây.
2. **Similarity bảo toàn góc, Affine thì không.** Cả bài toán này là đo góc sản phẩm rồi bù bằng
   trục A. Similarity là phép bảo giác. Affine fit ra chút aspect ratio hoặc shear là góc bị méo,
   **và mức méo đổi theo hướng đặt của sản phẩm** — máy bù tới ±15° (`MaxCompensationAngleDeg`) nên
   sai số bù sẽ khác nhau tuỳ sản phẩm nằm nghiêng bao nhiêu. Sai số kiểu đó trông như ngẫu nhiên.
3. **Ít ẩn thì vững hơn.** 11 điểm (5 vị trí X + 6 góc), và các điểm tịnh tiến **thẳng hàng** nên
   Affine (6 ẩn) không được xác định đầy đủ từ phần tịnh tiến; aspect ratio của nó chỉ dựa vào độ
   dẹt của elip do các điểm xoay vẽ ra, mà lệch tâm vài mm thì elip gần như tròn → fit ra nhiễu.

**Không chọn DOF bằng cách so residual.** Similarity là Affine bị ràng buộc thêm (hai mô hình lồng
nhau), nên về mặt toán học Affine **luôn** cho sai số fit ≤ Similarity trên cùng bộ điểm. Quy tắc
"fit cả hai rồi giữ cái nhỏ hơn" vì vậy sẽ luôn chọn Affine kể cả khi phần dôi ra chỉ là nhiễu.
Trọng tài là **§6.4b chạy ở nhiều góc** (0/90/180/270°), không phải residual. Chỉ lên Affine khi
Similarity trượt §6.4b *và* Affine sửa được hẳn — lúc đó là có tilt cơ khí thật, nên chỉnh camera
cho vuông góc lại thay vì hấp thụ tilt vào mô hình.

Ghi chú: Help của HIK khuyên *"use the default option"* (Affine). Ở đây cố ý làm khác — câu đó là
hướng dẫn chung cho mọi module hiệu chuẩn, gồm cả loại dùng bảng checkerboard hàng trăm điểm trải
đều hai chiều và không nhằm đo góc.

App hỗ trợ cả ba mà không phải sửa code: `RunAlignCalibSequenceAsync` chạy **danh sách vị trí X**
(`AlignCalibXOffsetsMm`) rồi **danh sách góc A** (`AlignCalibAnglesDeg`), cấu hình ở ngay trang
Vision Edit — để trống ô X là bỏ phần tịnh tiến (phương án B).

**Chuỗi kết thúc bằng việc đưa trục A về 0°** — để máy ở trạng thái xác định trước khi chụp ảnh mẫu
chuẩn (*Create Reference* trong Flow runtime `AlignBottom_ToolN`).

Điều kiện đúng của mẫu chuẩn **không phải** "A = 0°": camera dưới đo hướng THẬT của sản phẩm, không
đo giá trị tuyệt đối của A, và Auto cũng không bao giờ đưa A về 0° (nó cộng dồn `CurrentPos + Δθ`).
Điều kiện đúng là: **không xoay A giữa lúc hút sản phẩm mẫu và lúc chụp**, và số 0 của mẫu bottom-up
phải ứng với cùng tư thế vật lý như số 0 của mẫu top-down. (Lý do cũ — "teach ở góc khác 0° thì mốc
lệch nguyên một góc" — đã bị bác 3/3 trong đợt kiểm chứng 2026-09-12.)

Ràng buộc khác: Camera Mode = **Camera Stationary Down**; các bước xoay phải **bằng nhau**; mở
**Angle Range** của *High Precision Match* đủ rộng (mặc định −45…45 không khớp nổi ở 60°…300°);
xong thì **Create Calibration File**. Module hiệu chuẩn chỉ bật trong lúc nạp điểm, xong thì tắt (§6.8). Chuỗi nạp
điểm tự bấm **Clear Point** của module Translation Calibration trước điểm đầu tiên, như Auto Calib top-down (§6.2).

Nạp điểm hiệu chuẩn tâm xoay đi nhánh hiệu chuẩn của chính Flow `AlignBottom_ToolN` (`IsTriggerLocate = 0`, §6.8).
Nhánh soi (chạy Auto) trả `ObjX_mm/ObjY_mm` = tâm sản phẩm trong hệ vật lý của file hiệu chuẩn tay đó, **gốc tại tâm
xoay**, và `ObjA_deg` = góc sản phẩm.

### 6.4b Phép thử vòng xoay (App) — điều kiện chạy Auto

`Routine_Calib.VerifyAlignRotationAsync`: hút sản phẩm, đứng tại vị trí soi, đo tư thế → xoay A
thêm `AlignVerifyAngleDeg` → đo lại (nhánh soi của `AlignBottom_ToolN`) → App tự giải bài toán Single Point
Alignment giữa hai lần đo (`VmCycle.JudgeAlignSign`; trước 14/09 phép này chạy trong một Flow riêng): "muốn quay lại
tư thế ban đầu thì phải làm gì". Câu trả lời đúng là **"xoay ngược lại đúng bấy nhiêu độ, không dịch gì cả"**. Cho ra
hai thứ cùng lúc:

- **Dấu góc** (`VisionCalib.AlignAngleSign{N}`): VisionMaster tính chiều kim đồng hồ dương, trục A
  quy ước ngược lại (§11 #6). Đo rồi ghi, **không suy đoán** — đoán sai là xoay ngược, hỏng hàng.
  Chưa xác minh thì `PlacementSolver` từ chối tính (không có mặc định +1).
- **Sai số dư**: lượng dịch còn lại phải bằng 0. Khác 0 nghĩa là gốc toạ độ file hiệu chuẩn không
  nằm đúng tâm xoay, hoặc hệ toạ độ bị **lật** (camera nhìn từ dưới lên là ảnh gương) — đúng chỗ
  mà `BottomUpYSign` ngày trước định giải quyết bằng một hằng số đoán tay. Sản phẩm càng lệch tâm
  thì phép thử càng nhạy; gần trùng tâm xoay thì App báo rõ là không kết luận được.

### 6.5 Xác định hand-eye: offset CAM top-down ↔ tâm mỗi tay hút

Vector cố định `(dx, dy)` giữa hệ toạ độ camera trên và hệ gantry, cộng một **mốc góc** giữa hai
camera — đo **riêng từng tay**, ghi kèm thời điểm (`HandEye{N}MeasuredAt`); chưa đo thì Auto không
chạy. Ba công thức, đúng như code (`Routine_Calib.HandEyeCommitAsync`):

```
HandEyeDx_mm = X_gắp + ObjX_mm − TopX_mm
HandEyeDy_mm = Y2_gắp + ObjY_mm − TopY_mm
mốc góc      = Wrap(ObjA_deg + TopA_deg)        ← CỘNG, không trừ (sửa 23/09)
```

**Đo bằng CƠ CẤU KẸP, không bằng ô tray** (Tuấn chốt 25/09). Cơ cấu kẹp giữ **chi tiết đích** trên
bàn tray lắp ráp cho nó không xê dịch; trên chi tiết đặt một shield can lắp đúng. Lý do: cách cũ gắp
con can ra khỏi một ô tray thì **chi tiết ở dưới xê dịch một chút**, nên `TopX` đo sau khi gắp không
còn tả đúng chỗ chi tiết nằm lúc con can còn ngồi trên nó — mà đúng chỗ đó mới là điều hand-eye cần.

**Cái gì được phép xê dịch, cái gì không** — chỗ này dễ hiểu ngược:

- **Con can ĐƯỢC xê dịch.** Nó lệch tâm đầu hút bao nhiêu thì `AlignBottom_ToolN` đo đúng lượng đó
  ra `ObjX/ObjY`, và `X_gắp + ObjX` vẫn là chỗ tâm con can thật sự nằm. Bước này chỉ chặn khi lệch
  quá `MaxCompensationDistance`.
- **Chi tiết đích KHÔNG được xê dịch.** Cơ cấu phải kẹp **chi tiết**, không kẹp con can — kẹp con
  can thì không gắp lên được.
- **Giới hạn còn lại, có từ trước:** con can mẫu ngồi lệch chi tiết bao nhiêu thì hand-eye sai đúng
  bấy nhiêu. Cơ cấu kẹp chỉ khử phần xê dịch **do lực gắp**.

**Không thêm Flow nào** — dùng đúng `LocateTop` với đúng mẫu khớp thật, nên dấu định vị y như lúc
chạy Auto. (Phương án "khối nhôm chuẩn + Flow riêng" đã bỏ: dấu trên khối nhôm khác hàng thật, và cái
lệch dấu-tới-chỗ-ngồi ấy đi thẳng vào hand-eye thành sai số đều ở mọi ô mà không phép kiểm nào trong
App bắt được.)

**Ba điểm Jog-dạy** (trang Teaching, card *Cơ cấu kẹp*) — xem §8.3. Cơ cấu gá ở đâu trên bàn cũng
được, không trùng ô nào, nên vị trí gắp và vị trí soi lưu riêng chứ không lấy theo hàng/cột tray.
**Cao độ hạ Z và điểm giảm tốc lấy của tray lắp ráp**, nên cơ cấu kẹp phải giữ con can ở đúng cao độ
như một ô tray. **Tháo cơ cấu kẹp ra trước khi chạy Auto.**

**Chín nút** trên trang Hiệu chuẩn Vision (Tuấn yêu cầu 25/09; 26/09 nút tính điểm đích tách thành 7 và 8, thêm
nút 9 — cùng khuôn nút 4 → 5 → 6 của khu chạy kiểm từng bước) — trạng thái giữa các nút nằm trong `Routine_Calib`,
không ở ViewModel, để `--selftest` kiểm được các cổng chặn thật:

| Nút | Máy làm gì |
|---|---|
| 1 · Tới vị trí gắp trên cơ cấu kẹp | X, Y2 tới điểm gắp đã dạy, A về góc hút; không chạm Z. Bấm lại là làm lại từ đầu |
| 2 · Gắp con can mẫu | ra lệnh lại tới đúng điểm đó, **đọc encoder X/Y2 và A tại chỗ**, hạ Z theo tray lắp ráp, hút, nâng. Hạ Z nên hỏi lại trước (nút trả cũng vậy; nút 9 thì bấm đúp thay cho hỏi lại — Tuấn đổi 26/09) |
| 3 · Soi chi tiết đích | tới vị trí soi đã dạy, `LocateTop` N ảnh. Gắp trước soi sau vì con can che mất chi tiết |
| 4 · Tới vị trí soi CAM dưới | X tới `AlignPosX`; dùng chung lõi với nút 4 của khu chạy kiểm từng bước. Không xoay A |
| 5 · Ghi hand-eye | **xoay A về đúng encoder lúc gắp**, rồi `AlignBottom_ToolN` N ảnh, chặn ngưỡng lệch tâm, ba công thức trên, ghi model. Đòi **X đang thật sự** ở `AlignPosX` trong `AlignXTolMm`, không chỉ "đã bấm nút 4" |
| 6 · Soi chi tiết tại chỗ | phép thử tổng quát — xem §6.7. Nút nhỏ cạnh bên chọn một ô tray lắp ráp: có ô thì CAM tới vị trí soi ô đó (`CamPosFor`, như S05) rồi mới kích. Kích **một** ảnh (26/09) |
| 7 · Tới vị trí soi CAM dưới | y như nút 4, đặt lại dưới nút 6 cho đúng thứ tự bấm của phép thử |
| 8 · Trigger AlignBottom, tính điểm đích | phép thử tổng quát — xem §6.7. Không chạy X: đòi X **đang** ở `AlignPosX` (nút 7), cùng phép xét với nút 5. Chỉ tính và ghi lại, không chạy tới |
| 9 · Tới điểm đích vừa tính | **Bấm đúp** (không hộp thoại hỏi lại). X, Y2 và A tới điểm nút 8 vừa tính, rồi hạ Z tới **điểm giảm tốc** của tray lắp ráp và dừng — con can lơ lửng ngay trên chi tiết, không chạm, không nhả, để nhìn bằng mắt. Soi lại (nút 6), ghi hand-eye mới (nút 5) hay trả con can là điểm cũ hết hiệu lực, phải bấm 8 lại. Chưa teach điểm giảm tốc thì từ chối. **Không** chặn theo khoảng cách tới chỗ đã gắp: cơ cấu kẹp gá nhiều sản phẩm và Tuấn soi sang sản phẩm khác |
| Trả con can về cơ cấu kẹp | về điểm gắp và A về góc hút, hạ Z, nhả + thổi, nâng. Chỉ đòi tay đang cầm hàng, nên bỏ dở ở bước nào cũng gọi được |

**Vì sao nút 5 và nút trả tự xoay A** (thêm 25/09): thanh Jog của trang có A1/A2. Jog A đi δ độ giữa nút 2
và nút 5 thì mốc góc lệch đúng δ và **mọi lần đặt của Auto xoay lệch đều δ** — kiểm tra top-down, phép thử
vòng xoay và phép thử tổng quát đều không thấy, vì cái lệch nằm chung trong lần đo và lần thử; chỉ
nhìn hàng thật sau khi đặt hay nhìn xuống sau chạy khô mới lộ ra. Nút 5 ra lệnh lại chứ không từ chối như với X: xoay A
tại chỗ soi là việc phép thử vòng xoay vẫn làm, và không cần ngưỡng nào. Jog A **giữa nút 6 và 8** thì vô
hại — Group Cmd đo góc con can lúc chụp và tự bù đúng lượng đó. Nút trả xoay A vì con can được nhấc lên ở
góc hút: hạ xuống ở góc khác thì nó không ngồi vào chi tiết.

Sổ sách: gắp được thì ghi `HasProduct`, `SourceCellIndex = −1` (con can không đến từ ô tray nào — nếu
người vận hành bấm START lúc còn cầm, `Routine_Auto.ReturnToSourceAsync` phát `ResumeIntegrity` và
bắt lấy hàng ra rồi RESET, đúng việc cần làm). Ghi hand-eye xong **không** xoá `HasProduct` — con can
còn nằm đó thật. Nút trả xoá sổ **chỉ khi** cảm biến thật sự nhả.

### 6.6 Công thức tính điểm đặt

```
App dựng đích (VisionMaster không đọc được encoder):
  target = (X_chụp + top.X + HandEye_i.dx , Y2_chụp + top.Y + HandEye_i.dy , θ_t)

VisionMaster tính (Group Cmd trong Flow AlignBottom_ToolN, module Single Point Alignment):
  Δθ      = chuẩn hoá(θ_t − θ_p)
  (Δx,Δy) = target − R(Δθ)·p          ← p = tâm sản phẩm so với tâm xoay (AlignBottom_ToolN)

App phát lệnh:
  X_cmd  = Δx ,  Y2_cmd = Δy ,  A_cmd += AlignAngleSign_i · Δθ
```

**Lượng dịch do xoay đã nằm trong Δx/Δy** — App nhân `R(Δθ)` lần nữa là bù hai lần (PDF Hikrobot
tr.35, tr.60). `PlacementSolver` vì vậy chỉ còn: dựng đích, đổi dấu góc, và chặn ngưỡng
(`MaxCompensationAngleDeg` 15°, `MaxCompensationDistance` 3 mm — vượt ngưỡng là vision đo nhầm vật,
coi là Align fail chứ không lao trục đi bù).

`SinglePointAlignMath` giữ đúng công thức trên bằng C# nhưng **không nằm trên đường chạy thật**: nó
nuôi `FakeVision` (để chuỗi Auto mô phỏng chạy đủ) và bị khoá bằng `--selftest`, để khi số của
VisionMaster lệch khỏi nó thì biết ngay lỗi nằm ở cấu hình solution (đơn vị, dấu góc, gốc toạ độ).

### 6.7 Kiểm chứng độ chính xác (nghiệm thu Phase 4)

- §6.2b và §6.4b phải ĐẠT cho cả hai tay — đây là điều kiện chạy Auto, không phải khuyến nghị.
- Đo lặp lại tư thế cùng một sản phẩm ≥ 30 lần: yêu cầu 3σ trong dung sai.
- Đặt thử ≥ 20 sản phẩm, đo sai lệch thực tế bằng camera top-down sau khi đặt.
- Chạy lại hiệu chuẩn 3 lần liên tiếp, so kết quả §6.2b: lệch thang đo < 0.2 %.

**Phép thử tổng quát sau khi ghi hand-eye** (Tuấn nêu 25/09) — nút 6 → 7 → 8 → 9 của §6.5. Chỉ đòi hand-eye
của tay đó **đã ghi trong model** — không phải làm lại nút 1 → 5, kể cả sau khi mở lại App (Tuấn 26/09); con
can trên tay gắp bằng nút 2 hay hút ở trang Manual đều được (chỉ nút 2 mới có "chỗ đã gắp" để in lệch). Con can
trên tay: Jog X/Y2 tuỳ ý miễn chi tiết còn trong khung hình CAM trên, hoặc chọn một ô tray lắp ráp cho CAM
tự tới → kích `LocateTop` **một ảnh** (6) →
ra vị trí soi CAM dưới (7) → `AlignBottom_ToolN` tính điểm đích (8) → tay tới điểm đích, Z dừng ở điểm
giảm tốc ngay trên chi tiết để nhìn bằng mắt (9). Chạy được vì `TopX_mm/TopY_mm` là **toạ độ máy tuyệt
đối** (Script `TopAbs` cộng encoder X/Y2 mà App ghi vào biến toàn cục), nên pose ra y nhau bất kể Jog
tới đâu.

**Soi chính chi tiết đã gắp, hay sang sản phẩm khác.** Cơ cấu kẹp gá nhiều sản phẩm, và trên máy Tuấn soi
sang một sản phẩm **khác** ngay sau khi ghi hand-eye (26/09) — điểm đích cách chỗ gắp vài chục mm là đúng.
Hai cách cho hai thứ khác nhau: soi lại chính chi tiết đã gắp thì so được bằng **số** (đoạn dưới); soi sản
phẩm khác thì không có số để so, nhưng hand-eye sai đều **hiện ra** thành khoảng lệch giữa con can và chi
tiết mới ở nút 9 — đúng cái vế mà cách soi lại chính chi tiết ấy không thấy.

**Soi lại chính chi tiết đã gắp — tiêu chí là SỐ:** bước gắp đã ghi encoder lúc gắp, nên App tự so được —

```
Dx    = X_gắp + ObjX − TopX                (lúc ghi hand-eye)
Cmd   = TopX_mới + Dx − ObjX_mới           (Group Cmd, góc bù ≈ 0)
      = X_gắp                               nếu TopX_mới = TopX và ObjX_mới = ObjX
```

**Đúng đẳng thức đó cũng nói ra giới hạn.** Phép thử:

- **chứng minh** pose top-down **bất biến khắp khung hình** — chạy lại ở 4–5 chỗ Jog rải khắp, kể cả
  sát rìa; `Cmd − X_gắp` tản ra bao nhiêu chính là chất lượng hiệu chuẩn top-down trong khung hình,
  con số mà §6.2b chỉ đo được ở 3 vị trí với một bước cố định. Cộng với: camera dưới lặp lại được, sổ
  góc A đúng, Group Cmd nối đúng;
- **KHÔNG chứng minh** giá trị tuyệt đối của hand-eye. `Dx` đo từ **chính con can ấy ở chính chỗ ấy**,
  nên mọi sai chung cho cả lần đo và lần thử **triệt tiêu**: chi tiết xê dịch lúc gắp, hay con can ngồi
  lệch chi tiết, đều cho `Cmd` ra đúng `X_gắp` trong khi hand-eye sai đều. Cái tuyệt đối chỉ có
  **nhìn hàng thật sau khi đặt**, hoặc nhìn xuống sau khi chạy khô, nói được (phép đo sau đặt bằng Flow
  `MeasurePlaced` đã bỏ 2026-10-08).

Đọc kết quả: **tản ra theo chỗ Jog** → hiệu chuẩn top-down (thang đo, xoay, méo ở rìa).
**Lệch đều ở mọi chỗ** → cách soi lại chính chi tiết ấy không thấy; soi sang sản phẩm khác rồi nhìn ở nút 9,
hoặc đặt hàng thật rồi nhìn.

### 6.8 Hợp đồng phần mềm ⇄ solution VisionMaster

Tên Flow và tên bốn biến trục **không hardcode**: nằm trong `SystemData.VisionOutputs` (`VisionOutputNames`), sửa ở
trang Setting, `VisionConnect` đọc lại mỗi lần trigger. Tên output và tên các biến còn lại là hằng số trong
`VmContract.cs` (`VmOut`, `VmVars`). Hai nút ở trang Setting đối chiếu solution với đúng các hằng số đó: *Kiểm tra
Output* (output cấp Flow và Input của Script) và *Kiểm tra biến* (biến toàn cục — tạo luôn biến còn thiếu).

**Ba Flow** (Tuấn chốt 2026-10-08). Mỗi Flow vừa soi vừa nạp điểm hiệu chuẩn: App ghi biến `IsTriggerLocate`
trước mỗi lần kích, Module rẽ nhánh trong Flow chọn việc theo biến đó.

| Flow | Camera | `IsTriggerLocate = 1` — soi | `IsTriggerLocate = 0` — nạp điểm hiệu chuẩn |
|---|---|---|---|
| `LocateTop` | Top-down | Quét ô tray lắp ráp trong Auto; soi chi tiết ở bước xác định hand-eye (§6.5); kiểm tra top-down (§6.2b); chạy kiểm từng bước | N-point Calibration 9 điểm (§6.2) |
| `AlignBottom_Tool1/2` | Bottom-up | Soi sản phẩm đang hút; `HasTarget = 1` thì Group Cmd tính luôn lệnh đặt bằng *Single Point Alignment* (§6.6) | Hiệu chuẩn tâm xoay của tay đó (§6.4) |

**Module hiệu chuẩn (N-point, tâm xoay) chỉ bật lúc hiệu chuẩn** (Tuấn chốt 2026-10-09): bật module trước khi
chạy chuỗi nạp điểm, xong thì tắt đi. Lúc chạy Auto module đang tắt, nên dù có sai nhánh, lần quét ô cũng không nạp
được điểm rác nào vào danh sách hiệu chuẩn. Module đặt trong nhánh 0. Ở nhánh hiệu chuẩn App chỉ đọc `Found`, nên
cả hai nhánh đều phải ra `Found`. Module đang tắt mà vẫn bấm nạp điểm thì Flow vẫn có thể báo `Found = 1` dù không
điểm nào được nạp — App không phân biệt được.

Các Flow đã bỏ: `TopDownCalib`, `AlignCalib_Tool1/2` (gộp vào hai Flow trên) và `MeasurePlaced` (bỏ hẳn phép đo
sau đặt) — 2026-10-08; `LocateMark` (dấu chuẩn soi bằng chính `LocateTop`) và `PlaceAlign` (*Single Point
Alignment* chuyển vào Group Cmd của `AlignBottom_ToolN`) — phương án B, 2026-09-14. `system.json` cũ còn tên các
Flow này vẫn nạp bình thường, tên thừa bị bỏ qua.

**Output cấp Flow** (`VmOut`) — kiểu float, VisionMaster không có kiểu bool nên 1.0 = có:

| Flow | Bắt buộc | Của Script / Group trong Flow |
|---|---|---|
| `LocateTop` — 9 output | `Found`; echo `EchoRunId`, `EchoX_mm`, `EchoY2_mm` | Script `TopAbs`: `TopFound`, `TopX_mm`, `TopY_mm`, `TopA_deg`, `TopRunId` — vị trí vật lý của chi tiết đích (đã cộng vị trí trục lúc chụp) |
| `AlignBottom_ToolN` — 17 output | `Found`; echo `EchoRunId`, `EchoX_mm`, `EchoA_deg` | `ObjFound`, `ObjX_mm`, `ObjY_mm`, `ObjA_deg`, `ObjRunId` — tâm sản phẩm so với **tâm xoay**; Group Cmd khi `HasTarget = 1`: `Status`, `CmdX_mm`, `CmdY2_mm`, `CmdA_deg`, `DiagDA_deg`, `DiagEcc_mm`, `DiagTarX_mm`, `DiagTarY_mm` |

Thêm `ImageData` (không bắt buộc) cho khung ảnh trang Main. Echo là số Flow trả lại nguyên văn từ biến App vừa ghi
— lệch là App đọc nhầm Flow, nhầm biến hoặc nhầm lần chạy, và dừng.

**Biến toàn cục — 13 biến, App ghi hết** (`VisionGlobalVarCatalog`; solution phải có module *Global Variable*):

| Biến | Kiểu | App ghi khi nào |
|---|---|---|
| `X_mm`, `Y2_mm`, `A1_deg`, `A2_deg` (tên sửa ở Setting) | float | vị trí **encoder** trước mọi lần kích, cả soi lẫn hiệu chuẩn |
| `RunId` | float | trước mỗi lần soi; Flow trả lại qua `EchoRunId` |
| `IsTriggerLocate` | **int** — Module rẽ nhánh nhận int | trước mọi lần kích: 1 soi, 0 nạp điểm hiệu chuẩn |
| `HasTarget`, `CellX_mm`, `CellY_mm`, `CellA_deg`, `HandEyeDx_mm`, `HandEyeDy_mm`, `AlignSign` | float | trước `AlignBottom_ToolN`: pose ô đích (số `LocateTop` lúc quét ô + bù đặt) và hằng số của **đúng tay đang gọi**; `HasTarget = 0` thì Group Cmd nghỉ, Flow chỉ soi. `CellY_mm`, `HandEyeDy_mm` ghi **đảo dấu** vì trục Y camera dưới ngược chiều Y2 (`VmCycle.BottomYSign`) |

App ghi biến theo đúng kiểu đang khai trong solution (int thì `SetVarInt`), nên lỡ khai `IsTriggerLocate` là float
vẫn chạy. Ghi biến và kích Flow nằm trong **cùng một lời gọi** (`IVisionDevice.TriggerCalibAsync` /
`CallFlowAsync`) — tách ra là mở đường cho một lần kích Flow lọt vào giữa, nạp điểm mới với toạ độ cũ mà không có
dấu hiệu gì.

Module hiệu chuẩn đặt *Calibration Points Input* = **By Coordinate** rồi subscribe *Physical
Coordinate X/Y* vào các biến trục. Nhánh hiệu chuẩn của hai Flow `AlignBottom_ToolN` subscribe thêm *World Rotate
Angle* ← `A1_deg` (Tool1) / `A2_deg` (Tool2).

**⚠ *World Rotate Angle* PHẢI bind vào biến A** (đính chính 2026-09-12 — **máy thật đã bác bỏ**
bản đính chính trước đó, bản bảo để TRỐNG dựa trên câu Help và hai ví dụ tr.22, tr.48).

- **Bằng chứng trên máy.** Để trống: nạp đủ 11 điểm, bấm Calibrate, nhận `Error code:10103002
  Matrix calculating error` — lặp lại y hệt qua nhiều lần thử. Bind vào `Global Variable1.A2`:
  hiệu chuẩn chạy ngay, và §6.4b sau đó đạt — sai số dư 0.011 mm trên độ lệch tâm 4.535 mm, dấu
  góc Tool2 = +1. Góc thử chỉ 4° nên kết quả này xác nhận cấu hình **chạy được và đúng dấu**; độ
  chính xác của tâm xoay còn phải thử lại ở góc lớn.
- **Vì sao đọc Help sai.** Câu dưới ô đó — *"This parameter and the Physical Coordinate X/Y cannot
  be used at the same time"* — nói về việc **sinh toạ độ điểm**, không nói về kênh cho phép fit
  tâm xoay biết vật đã xoay bao nhiêu. Trong pha xoay, Physical Coordinate X/Y đứng yên theo đúng
  thiết kế, nên kênh góc duy nhất còn lại là *Image Angle* — mà ở máy này nó lấy từ *Match Box
  Angle*, **quấn vòng mỗi 180°** vì sản phẩm gần đối xứng. Pha xoay không còn kênh góc nào dùng
  được → phép fit vô nghiệm, đúng như mã lỗi nói. Hai ví dụ tr.22, tr.48 không sai: chúng khớp mẫu
  vật không đối xứng, nên *Image Angle* của chúng đủ dùng.
- **Bind đúng trục.** Nhánh hiệu chuẩn của `AlignBottom_Tool1` ← `A1_deg`, của `AlignBottom_Tool2` ← `A2_deg`.
  Nhầm sang trục kia thì trong pha xoay giá trị đứng im, và lại gặp đúng mã lỗi trên.
- **Vẫn bind *Image Angle*** — hai kênh không loại trừ nhau; ở pha tịnh tiến nó xác nhận vật không
  bị xoay ngoài ý muốn.
- **Toạ độ A quấn vòng [0, 360)** (`AxisConfig.WrapDegrees`): dãy góc hiệu chuẩn không được cắt qua
  vạch 0 — `0, 60, …, 300` là an toàn.

Hệ quả: §6.4b **vẫn bắt buộc**. Có góc vật lý rồi thì module biết chiều xoay, nhưng đó là chiều
trong hệ toạ độ của file hiệu chuẩn, chưa chắc trùng quy ước dấu của App khi phát lệnh trục A —
§6.4b đo dấu đó và ghi vào model, và là chỗ duy nhất bắt được lỗi trỏ nhầm file hiệu chuẩn. Biến
`A1`/`A2` App gửi mỗi lần trigger nay là **đầu vào thật** của hiệu chuẩn bottom-up, không còn là
thứ gửi kèm cho tiện đối chiếu log.

Hai hệ quả của việc nạp điểm kèm toạ độ:

- Phép fit chạy trên **toạ độ thật** chứ không phải toạ độ VisionMaster tự sinh từ bước lưới khai
  sẵn — đúng §6.1 quy tắc 3, và bắt được cả sai lệch bám servo lẫn khe hở cơ khí.
- Theo PDF Hikrobot tr.18, nạp điểm kèm toạ độ thì **mọi quỹ đạo đều hợp lệ** — không bắt buộc hình
  S, không bắt buộc lưới hai chiều. Đây chính là điều kiện để hiệu chuẩn được CAM bottom-up, vốn
  chỉ có chuyển động theo X và xoay A (§6.4).

SDK: `GlobalVariableModuleTool` (kế thừa `VM.Core.VmModule`, hiện trong `VmSolution.Instance.Modules`)
với `SetVarFloat` / `SetVarInt(tên, mảng)`. App đối chiếu tên và kiểu biến bằng `GetAllGlobalVar()` trước khi ghi —
thiếu biến thì **ném lỗi kèm danh sách biến đang có**, không nạp điểm với toạ độ cũ.

**Ba điều bắt buộc khi dựng solution:**

1. **Output cấp Flow phải là kiểu float.** `MatchNum`, `Module Status`, `CalibPointNum` trong
   VisionMaster là **Int** — đi qua một module *Variable Calculation* (Output Type = float) rồi mới
   khai ra output của Flow. App đọc tất cả bằng `ModuResult.GetOutputFloat`.
2. **`Max Number to Find = 1`** ở module định vị — App chỉ đọc phần tử `[0]` của mỗi output.
3. **`Score` của VisionMaster là 0..1**, không phải 0..100. App chỉ ghi log, không dùng làm ngưỡng
   loại hàng — ngưỡng khớp đặt bên trong *High Precision Match*.

Đặt sai tên thì `VisionConnect` ném `VmException` ngay lần trigger đầu — báo lỗi rõ ràng chứ không
âm thầm trả số sai. Thiếu output thì lỗi ghi trong **log**, hộp thoại chỉ báo "thất bại".

**Trang Vision Edit đã nhúng thật** `VMControls.WPF.Release.VmMainViewConfigControl`. Control cần
thư mục `myLibs\` (log config nội bộ của SDK) nằm cạnh exe — `ShieldCanAssy.Wpf.csproj` đã thêm
rule copy `Applications\myLibs` (~34 MB).

---

## 7. Hệ thống Alarm

### 7.1 Phân cấp

| Mức | Hành vi máy | Cách xoá |
|---|---|---|
| `Fatal` | cắt servo, đèn đỏ + còi, bắt buộc Origin lại | RESET + hết nguyên nhân |
| `Error` | dừng chu trình ngay, đèn đỏ + còi, giữ servo | RESET + hết nguyên nhân |
| `Warning` | **không dừng**, đèn vàng nháy | RESET + hết nguyên nhân (từ 2026-09-26, trước đó tự hết) |
| `Info` | chỉ ghi log, không hiện banner | tự hết |

### 7.2 Bảng mã

Chia khối theo nguồn phát; mỗi khối trục dùng chung một bảng offset:

| Dải | Nhóm |
|---|---|
| 1000–1099 | Hệ thống — config lỗi, watchdog (1002), đĩa đầy, crash routine |
| 1100–1199 | An toàn — EMG (1100), thiếu khí (1103), rèm sáng bị cắt (1105). 1101/1102/1104 (cửa) đã bỏ cùng cảm biến cửa, 2026-09-09 — không tái dùng |
| 2000–2099 | Trục **X** |
| 2100–2199 | Trục **Z1** |
| 2200–2299 | Trục **Z2** |
| 2300–2399 | Trục **Y1** |
| 2400–2499 | Trục **Y2** |
| 2500–2599 | Trục **A1** |
| 2600–2699 | Trục **A2** |
| 3000–3099 | I/O — mất kết nối Ezi-IO, DI xung đột |
| 4000–4099 | Vision — không kết nối, load solution fail, trigger timeout (4003), không tìm thấy (4004), score thấp (4005), chưa hiệu chuẩn (4006), calib quá hạn (4090, Warning) |
| 5000–5099 | Chu trình — hút hụt (5001), rơi sản phẩm (5002), NG liên tiếp (5010), hết tray (5020, Warning) |
| 6000–6099 | Dữ liệu — không lưu được model, file hỏng, không ghi được log |

Offset dùng chung cho mọi khối trục (`base + offset`):

| Offset | Nội dung | Mức |
|---|---|---|
| +00 | Mất kết nối | Fatal |
| +01 | Alarm từ driver (kèm `ErrorCode` đọc được) | Fatal |
| +02 | Timeout chuyển động | Error |
| +03 | Chạm soft limit | Error |
| +04 | Chạm hard limit (POT/NOT) | Fatal |
| +05 | Timeout về gốc | Error |
| +06 | Chưa về gốc mà nhận lệnh chạy | Error |
| +07 | Servo OFF ngoài ý muốn | Fatal |
| +08 | Sai lệch vị trí quá lớn (following error) | Fatal |

Ví dụ: **2105** = Z1 timeout về gốc. Đọc mã là biết ngay trục nào, lỗi gì, không cần tra.

### 7.3 `AlarmData` và `AlarmManager`

```csharp
public class AlarmData {
    public int      Code;
    public EAlarmLevel Level;
    public string   Source;        // "Z1" | "Vision" | "Safety" | "System"
    public string   MessageVi;
    public string   MessageEn;
    public string   Cause;         // nguyên nhân có thể
    public string   Remedy;        // hướng xử lý cho operator
    public string   Detail;        // dữ liệu động: ErrorCode driver, vị trí, score...
    public DateTime RaisedAt;
    public DateTime? ClearedAt;
    public bool     IsActive;
    public bool     IsRootCause;   // xem dưới
}
```

Nội dung tĩnh (message/cause/remedy) nằm trong **`Config\alarms.json`**, không nhúng vào
code — sửa lời văn và dịch được mà không phải build lại. Mã nào chưa có trong file thì
hiện "Alarm chưa định nghĩa: <mã>" chứ không nuốt.

**Ai sở hữu cái gì trong alarms.json.** Lời văn thuộc về người dùng, phần mềm không bao
giờ ghi đè. Còn **`level` thuộc về phần mềm** và được lấy lại theo bản mẫu cạnh exe mỗi
lần nạp (có ghi log khi khác nhau): mức alarm quyết định máy có dừng hay không, để nó
trong tay một file text là mở đường cho một lần sửa nhầm hạ Fatal xuống Warning và máy
chạy tiếp qua sự cố. Ngoài ra bản mẫu được **hoà vào** file của người dùng lúc nạp — mã
thêm ở phiên bản mới sẽ tự xuất hiện trên máy đã cài. Không có bước hoà này thì mã mới
không bao giờ tới được máy đang chạy, vì thư mục `Config` đã tồn tại nên nhánh chép bản
mẫu không chạy nữa, và alarm mới sẽ hiện dạng "chưa định nghĩa" đúng lúc người vận hành
cần đọc cách xử lý nhất.

`AlarmManager`: `Raise(code, source, detail)`, `Clear(code)`, `ClearAllClearable()`,
`ActiveAlarms` (ObservableCollection, bind thẳng lên UI), `History`, event `AlarmRaised`.
Tự đẩy sang `LogWorker` và ghi `Log\yyyyMMdd\alarm.csv`.

**Ghi nhận lỗi gốc (`IsRootCause`).** Một sự cố thật thường kéo theo chùm alarm phụ: mất
điện driver X → 2000 mất kết nối → 2002 timeout → 5002 rơi sản phẩm. Nếu chỉ hiện danh
sách phẳng thì operator sẽ đi sửa cái cuối cùng. Quy tắc: alarm **đầu tiên** trong một
cụm phát sinh trong vòng 200 ms được đánh `IsRootCause = true`, UI hiện nó lên đầu và tô
đậm, các alarm còn lại thu gọn bên dưới.

### 7.4 Quy tắc vận hành

- **Không tự động xoá alarm Error/Fatal.** Alarm chỉ mất khi nguyên nhân đã hết **và**
  operator bấm RESET. Tự xoá thì máy có thể tự chạy lại khi lỗi chập chờn.
- **Lỗi nào cũng phải RESET xác nhận** (Tuấn chốt 2026-09-26): alarm mức `Warning` hết nguyên nhân
  cũng nằm lại tới khi bấm RESET, không tự xoá nữa. Còn alarm đã hết nguyên nhân mà chưa RESET thì
  Cặp A và Start 1/Start 2 (kể cả nạp tray giữa chừng) bị chặn, báo "bấm RESET trước". Alarm còn nguyên nhân
  (vd. 5020 lúc đang chờ đổi tray, 4090 hiệu chuẩn quá hạn) không chặn — RESET chưa gỡ được chúng.
  Thao tác tay (Jog, Move, về gốc) không bị chặn vì Warning.
- Banner alarm nằm ở `ShellViewModel` → **hiện trên mọi trang**, không riêng trang Main.
  Click banner mở popup: danh sách active (kèm Cause/Remedy) + lịch sử 200 dòng gần nhất.
- Bấm RESET không xoá được alarm nào thì phải **nói rõ vì sao** ("EMG vẫn đang nhấn"),
  chứ không im lặng không phản ứng — đây là tình huống operator hay bấm loạn nhất.
- Trang Setting có nút xuất lịch sử alarm ra CSV theo khoảng ngày.

---

## 8. Dữ liệu và file

Gốc mặc định là `D:\ShieldCanAssy` — tách khỏi ổ hệ thống để cài lại Windows không mất
dữ liệu teach và log sản xuất. Máy không có ổ D: thì tự rơi về `_RunData` cạnh exe và
hiện cảnh báo trên status bar (`AppPaths`), nhờ vậy chạy thử ở đâu cũng được mà không
phải sửa hằng số rồi quên sửa lại lúc bàn giao.

```
D:\ShieldCanAssy\
├─ Config\
│   ├─ system.json          COM/IP từng trục, tốc độ, accel, soft limit, origin offset,
│   │                       chiều về gốc, timeout, Z_SAFE, vị trí chờ, số lần retry,
│   │                       RequireHomeAfterEmg, IO map
│   ├─ alarms.json          bảng mã alarm: message / cause / remedy (VI + EN)
│   └─ users.json           tài khoản + phân quyền
├─ Model\
│   └─ <tên model>.json     TrayLayout ×2, TeachPoint, VisionCalib, offset từng ô
├─ Vision\
│   ├─ Solution.solw                 MỘT solution VisionMaster chứa cả ba Flow (§6.8):
│   │                                LocateTop, AlignBottom_Tool1/2 —
│   │                                VisionMasterDevice chỉ nạp một đường dẫn solution
│   └─ *.calib                       file hiệu chuẩn do VisionMaster tạo (Create Calibration
│                                    File) — nguồn sự thật duy nhất, App không giữ bản sao
├─ Runtime\
│   └─ running.json         bộ đếm OK/NG, model gần nhất + tiến độ 2 tray/AutoState/tay hút
│                           để log (ghi sau mỗi ô và ngay khi vào EMG/ALARM) — tiến độ chu
│                           trình bị XOÁ ngay lúc nạp lại, mỗi lần mở app đều Init lại từ
│                           đầu, không resume qua tắt máy (§3)
├─ Log\yyyyMMdd\app.log     nhật ký, TSV: thời gian ⇥ mức ⇥ nguồn ⇥ nội dung
├─ Log\yyyyMMdd\alarm.csv   lịch sử alarm
├─ Data\yyyyMM\*.csv        dữ liệu sản xuất, kết quả từng sản phẩm
└─ Image\NG\yyyyMMdd\       ảnh lưu khi NG
```

### 8.1 `TrayLayout` — số hàng/cột cấu hình được

Mỗi tray là một `TrayLayout` nằm trong `ModelData`, **không hardcode kích thước ở bất kỳ
đâu** (kể cả lưới hiển thị trên trang Main — render động theo `Rows`/`Cols`):

```csharp
public class TrayLayout {
    public string Name;              // "InputTray" | "AssyTray"
    public int    Rows;              // mặc định 4 (input) / 5 (assy)
    public int    Cols;              // mặc định 5 (input) / 6 (assy)
    public string AxisRow;           // trục quét theo hàng: "Y1" | "Y2"
    public string AxisCol;           // trục quét theo cột:  "X"

    public TeachPoint P00;           // ô (0, 0)
    public TeachPoint P0C;           // ô (0, Cols-1)      — cuối hàng đầu
    public TeachPoint PR0;           // ô (Rows-1, 0)      — cuối cột đầu
    public TeachPoint PRC_Verify;    // (tuỳ chọn) ô (Rows-1, Cols-1) chỉ để kiểm chứng

    public double Z_Pick;            // cao độ hút/thả của tray này
    public double Z_Safe;

    public CellOffset[,] Offsets;    // offset tinh chỉnh riêng từng ô, mặc định 0
    public EScanOrder ScanOrder;     // RowMajor | ColMajor | Serpentine
}
```

Ràng buộc: `Rows`, `Cols` trong khoảng 1–50. Khi người dùng đổi kích thước, mảng
`Offsets` và trạng thái ô được **resize giữ lại phần trùng nhau** (không xoá sạch), và
hiện cảnh báo nếu con trỏ ô hiện tại vượt ra ngoài lưới mới.

### 8.2 Teach 3 điểm — công thức

Teach ba ô ở ba góc, suy ra toàn bộ lưới bằng nội suy affine:

```
u = (P0C − P00) / (Cols − 1)      // vector bước theo CỘT  (2 thành phần: X, Y)
v = (PR0 − P00) / (Rows − 1)      // vector bước theo HÀNG  (2 thành phần: X, Y)

P(r, c) = P00 + c·u + r·v + Offsets[r, c]
```

`u` và `v` đều là vector 2 chiều `(X, Y1)` hoặc `(X, Y2)`, nên phép nội suy này xử lý
được cả **tray đặt hơi xoay** lẫn **hai trục không thật sự vuông góc** — chứ không chỉ
là "pitch theo X, pitch theo Y". Đây là lý do dùng 3 điểm thay vì nhập pitch bằng tay.

**Trường hợp biên:** nếu `Cols == 1` thì `u = 0`; nếu `Rows == 1` thì `v = 0` (tránh chia
cho 0). Khi đó chỉ cần teach `P00` và điểm còn lại tương ứng.

**Kiểm chứng lưới (khuyến nghị):** teach thêm ô góc đối diện `PRC_Verify`, so sánh với
giá trị nội suy `P(Rows-1, Cols-1)`. Sai lệch vượt ngưỡng cấu hình (mặc định 0.5 mm) →
cảnh báo teach sai hoặc tray cong vênh, không cho lưu. Điểm này **không** tham gia tính
toán, chỉ để bắt lỗi teach — bắt được lỗi phổ biến nhất là teach nhầm thứ tự ô.

**Quy trình trên trang Teaching:** chọn tray → nhập `Rows`/`Cols` → jog tới ô (0,0) bấm
*Teach P00* → jog tới ô (0, Cols−1) bấm *Teach P0C* → jog tới ô (Rows−1, 0) bấm *Teach
PR0* → (tuỳ chọn) *Teach Verify* → *Tính lưới* → bảng liệt kê toạ độ tính ra của tất cả
các ô, có nút *Move* để chạy thử tới từng ô và nút *Set Offset* để tinh chỉnh ô lệch.

### 8.3 Cơ cấu kẹp — ba điểm dạy cho bước xác định hand-eye

Ba `TeachPoint` scalar trên `ModelData`, **không** phải một `TrayLayout` thứ ba (Tuấn chốt 25/09):

| Trường | Nghĩa vật lý |
|---|---|
| `HandEyePick_Nozzle1`, `HandEyePick_Nozzle2` | vị trí gantry (X, Y2) để **đầu hút của đúng tay đó** nằm trên con can mẫu trên cơ cấu kẹp |
| `HandEyeCamPos` | vị trí gantry (X, Y2) để **CAM top-down** ngắm dấu định vị của chi tiết đích |

Cùng khuôn với `AlignPosX_NozzleN` và `PickAngleAN`: scalar trên model, card riêng trên trang
Teaching **không có binding `Visibility`** — ba điểm này không thuộc tray nào. Trục hàng lấy của
`AssyTray` (buộc là Y2: `VmCycle.AxisGlobals` ghi cứng encoder Y2 vào biến toàn cục, và Auto chặn
`AssyTray.AxisRow != Y2`), nên cơ cấu kẹp **phải gá trên bàn Y2**.

`new TeachPoint(x, y)` là đường **duy nhất** bật cờ `Taught`, nên handler teach **thay cả đối tượng**;
`ModelData.HandEyeJigProblem(nozzle)` trả câu nói thiếu điểm nào, và bước xác định hand-eye từ chối
chạy kèm đúng câu đó.

**Không có cao độ Z riêng:** bước gắp hạ Z theo `AssyTray.ZPickFor/ZSlowFor`, nên cơ cấu kẹp phải giữ
con can ở **đúng cao độ như một ô tray lắp ráp** — cao hơn thì đầu hút ấn xuống con can, thấp hơn thì
gắp hụt. Và **tháo cơ cấu kẹp ra trước khi chạy Auto**; ba điểm vẫn nằm trong model, gá lại là dùng
được ngay.

---

## 9. Năm màn hình

| Trang | Nội dung chính |
|---|---|
| **Main** | Trạng thái máy + mode, nút Start / Stop / Reset / Origin, đèn báo EMG-khí, **hai lưới tray** render động theo `Rows`/`Cols`, tô màu theo trạng thái ô (trống / có hàng / OK / NG / đang xử lý), ảnh live 2 camera, counter OK-NG-tổng, cycle time, tỉ lệ NG, banner alarm |
| **Manual** | Home / Move Abs / Jog +− / Inching (bước chỉnh `StepSize`) từng trục — nhóm `Gantry (X, Z1, Z2)`, `Rotate (A1, A2)`, `Tray (Y1, Y2)`; Home/Move Abs của X/Y1/Y2 đòi Z1&Z2 đã về gốc (§4.5); vacuum / blow ON-OFF từng tay; bảng đèn DI/DO 32+32; chạy từng bước routine Auto |
| **Teaching** | Teach điểm gắp / đặt / vị trí soi camera (`P00`/`P0C`/`PR0` mỗi tray, `Z_Pick`, `A_Pick` — góc hút, chỉ ở tray đầu vào); bảng lưới ô + offset từng ô; quản lý model (tạo / sao chép / xoá / nạp) |
| **Setting** | Tham số trục (tốc độ, accel, soft limit, origin offset, chiều home, Z_SAFE, vị trí chờ), COM/IP (kể cả 2 module Ezi-IO Ethernet), timeout, số lần retry, ngưỡng NG liên tiếp, `RequireHomeAfterEmg`, tên Flow/Output VisionMaster (§6.8), phân quyền, xuất lịch sử alarm, backup / restore cấu hình |
| **Vision Edit** | Cột trái: `VmMainViewConfigControl` — GUI VisionMaster thật để mở/sửa solution, dựng Flow và chạy phép hiệu chuẩn từ các điểm App vừa nạp. Cột phải (280px) **chọn bước bằng Combobox** (Tuấn đổi 25/09): luôn hiện dòng cổng Auto + nút làm mới nó, dòng tiến trình, ô chọn **Tool 1/Tool 2**, rồi Combobox; chỉ khối của bước đang chọn hiện ra. Sáu bước, xếp đúng thứ tự phải làm và **gọi bằng tên, không đánh số**: hiệu chuẩn + kiểm tra CAM top-down (dùng chung, §6.2/§6.2b) · hiệu chuẩn CAM bottom-up (§6.4) · phép thử vòng xoay (§6.4b) · xác định hand-eye (§6.5, chín nút) · nghiệm thu chạy khô + đo sau đặt (§6.7) · chạy kiểm từng bước cả chu trình |

Phân quyền: `Operator` (chỉ Main), `Technician` (+ Manual, Teaching), `Engineer` (tất cả).

---

## 10. Lộ trình triển khai

| Phase | Nội dung | Tiêu chí hoàn thành |
|---|---|---|
| **P0 ✔ XONG** | Khung solution, hạ tầng dùng chung, shell 5 tab, log, **AlarmManager + alarms.json**, **Simulation mode**, và (kéo sớm từ P2) `SafetyMonitor` + `Routine_Origin` | Đạt — build sạch 0 cảnh báo, app chạy không cần phần cứng, `--selftest` 141/141 đạt (con số tăng dần qua các phase sau, ghi ở §10.1) |
| **P1a ✔ (code)** | `AxisScale`, `PanasonicAxis`, `FastechAxis`, `IoConnect` viết xong, build sạch, tham chiếu `ServoControl` + `EziMOTIONPlusE.dll`; `PulsePerUnit` cố định 1000 xung/mm (Panasonic) và 20000 xung/360° đầu hút (Fastech, hộp số 1/2) — hằng số theo thiết kế, không phải số đo | `--selftest` 141/141, hành vi mô phỏng không đổi so với P0. Chưa kiểm chứng được gì thêm — cần máy |
| **P1b** | Ra máy: PANATERM lập trình nội dung Block0–3 (đã chốt số hiệu, xem trên) cho 5 driver A6SF; cấu hình electronic gear của driver khớp 1000 xung/mm (Panasonic) và 18000 xung/vòng động cơ cho Ezi-SERVO A1/A2 — qua hộp số 1/2 ra 36000 xung/360° đầu hút, xem §"tỉ lệ xung/đơn vị" ở trên; xác định chiều dương và gốc từng trục (cơ khí, không suy ra được); đối chiếu `IoMap` với sơ đồ điện; xác minh bảng bit `EziAxisStatus` qua ô chẩn đoán ở trang Manual | Jog / home / servo on đủ 7 trục thật; đọc ghi được DI/DO; chiều home của Z1, Z2 đã xác nhận là đi lên; **độ lặp lại về gốc 10 lần ≤ 0.02 mm / 0.05°** (§4.3) |
| **P2** | Kiểm chứng lại `SafetyMonitor` / `Routine_Origin` / đèn tháp trên **phần cứng thật**; đấu nút cứng và mạch EMG; khôi phục sau EMG (§3.7). **Rèm sáng tạm gác** (§11 #18, `HasLightCurtain = false`) — đấu và bật lại khi có quyết định muting | Về gốc an toàn và huỷ được giữa chừng trên máy thật; EMG/khí/mất kết nối đều dừng đúng; **xác nhận Z1/Z2 không tụt khi cắt STO** (§11 #19) |
| **P3 ✔** | `ModelData`, `TrayLayout`, trang Teaching (Teach 3 điểm + kiểm chứng, Rows/Cols, Z_Pick, bảng ô kèm Move/Set Offset/Xoá offset, quản lý model tạo/sao chép/xoá/nạp/lưu) | Teach 3 điểm → chạy tay tới đúng mọi ô của cả 2 tray (nút Move); teach xong `Z_Pick` — điều kiện để làm đồ gá bản chuẩn ở P4. `--selftest` 179/179 |
| **P4 ✔ (code)** | `VisionConnect`/`VisionMasterDevice` bọc HIKROBOT VisionMaster 4.4.0 SDK thật (SDK đã cài trên máy dev, build thẳng vào API `VM.Core` theo đúng mẫu chính hãng — §6.8), kể cả gán input của Flow (`SetInputFloat`) cho Flow tính bù `PlaceAlign`; `Routine_Calib` (nạp điểm top-down theo thứ tự hình S, chuỗi nạp điểm Align + tự về 0°, hai phép kiểm tra bằng chuyển động trục §6.2b/§6.4b, hand-eye qua `LocateMark`) — `PlaceAlign`, `LocateMark` đã thay ở phương án B 14/09, còn ba Flow từ 08/10 (§6.8); trang Vision Edit nhúng `VmMainViewConfigControl` thật cùng cột phải (§9) | `--selftest` **293/293** (2026-09-12, sau khi chuyển hiệu chuẩn + phép tính bù sang VisionMaster — §6.0). Phép kiểm tra chạy qua `FakeVision` đúng kỳ vọng TỪ CHỐI vì mô phỏng không có quang học thật; công thức bù khoá riêng bằng dữ liệu biết trước đáp số. **Còn lại, cần solution + camera thật:** Tuấn dựng đủ ba Flow ở §6.8 (`LocateTop`, `AlignBottom_Tool1/2`, mỗi Flow có nhánh hiệu chuẩn theo `IsTriggerLocate`) đúng tên output và biến; chốt phương án hiệu chuẩn bottom-up (§6.4 A/B/C); rồi §6.2b và §6.4b phải ĐẠT cho cả hai tay trước khi đo lặp lại và đặt thử 20 sản phẩm |
| **P5 ✔ (mô phỏng)** | `Routine_Auto` đầy đủ, `PlacementSolver`, xử lý NG, Pause/Resume + kiểm tra toàn vẹn, đổi tray giữa chừng, counter, cycle time | Đạt trên tầng mô phỏng: `--selftest` chạy hết chu trình, dừng/chạy lại, chặn khi lệch trạng thái tay hút, đổi tray. **Còn lại trên máy thật:** chạy liên tục hết 1 tray lắp ráp; tối ưu cycle time |
| **P6** | Report CSV, backup/restore, phân quyền, tài liệu vận hành | Bàn giao |

**Simulation mode (P0) làm nghiêm túc:** `FakeAxis : IAxis`, `FakeIo`, `FakeVision` trả
kết quả có nhiễu ngẫu nhiên, và mô phỏng được cả lỗi (hút hụt, vision fail, EMG, mất kết
nối, về gốc sai chiều). `FakeAxis` chạy theo **thời gian thực**: vị trí tiến dần về đích
với tốc độ đã lệnh, cập nhật trong `Refresh()` đúng như driver thật được vòng quét đọc
trạng thái — nhờ vậy timeout, interlock và điều kiện chờ có ý nghĩa thật chứ không hoàn
tất tức thời làm mọi thứ luôn "đúng". Cho phép viết và kiểm thử toàn bộ chuỗi Auto,
chuỗi Origin, xử lý NG, Pause/Resume và đổi tray **trước khi** phần cơ khí hoàn thiện —
đây là phần tốn thời gian debug nhất nếu để tới lúc có máy mới làm.

### 10.1 Tự kiểm tra: `ShieldCanAssy.Wpf.exe --selftest`

Chạy không mở giao diện, kết quả ghi ra `%Root%\selftest.txt`, mã thoát 0 = tất cả đạt.
Hiện có **646 phép kiểm tra**, chạy hết khoảng 5 phút (tăng dần qua từng phase — P0: 141, P3: 179,
P4: 204, 252 sau các bổ sung Inching/interlock Z-home/Align-VisionMaster 2026-09-08; **2026-09-12**
chuyển hiệu chuẩn sang VisionMaster: bỏ nhóm fit affine/fit vòng tròn và wizard checkerboard, thêm
nhóm công thức Single Point Alignment + hai phép kiểm tra bằng chuyển động trục; **2026-09-25** thêm
28 phép cho bước xác định hand-eye trên cơ cấu kẹp và phép thử tổng quát §6.7, và 19 phép cho lệnh
Modbus xuống driver A6SF), phủ:

- khởi động và kết nối; chốt chặn Z_SAFE (gồm trường hợp chỉ nâng một trục Z — vẫn phải
  chặn); chuỗi Origin O0–O7;
- EMG cùng quy tắc "RESET không xoá được alarm còn nguyên nhân"; rèm sáng (chặn mọi chế
  độ, phải RESET mới chạy lại); **van hút giữ nguyên qua EMG**;
- các ràng buộc phần cứng đã chốt ở §11: không tắt được `RequireHomeAfterEmg`, Z về gốc
  chiều lên, bắt xung Z, A1/A2 song song;
- phân cấp alarm và đánh dấu lỗi gốc; nội suy lưới tray 3 điểm; quản lý model trên đĩa;
  quy đổi mm/độ ⇄ xung/rpm và số hiệu block đã chốt;
- **lệnh Modbus xuống driver A6SF** (§2) trên một driver giả nói Modbus RTU trong bộ nhớ: đếm giao
  dịch trước STB, và chụp bảng thanh ghi ở cạnh STB để chắc bớt ghi rồi driver vẫn chạy đúng tốc độ,
  ramp, block, đích — kể cả sau Jog, sau khi driver bật lại nguồn rồi về gốc, và sau một giao dịch lỗi;
- **ghép hai camera thành lệnh đặt** (§6.6): đích = vị trí chụp + LocateTop + hand-eye riêng
  từng tay; lệnh lấy NGUYÊN số VisionMaster trả về (không bù lại lần nữa); dấu góc ±1 đổi đúng
  chiều; **chưa xác minh dấu góc thì từ chối tính**; ngưỡng góc/khoảng cách; Flow lỗi hoặc trả
  NaN thì từ chối;
- **công thức Single Point Alignment** (§6.6) khoá bằng dữ liệu biết trước đáp số: lệch tâm
  quay theo Δθ, vòng xoay khép kín phải ra "xoay ngược lại, không dịch gì", hệ toạ độ bị lật
  phải để lại sai số dư khác 0, và chuẩn hoá góc;
- **hiệu chuẩn vision** (`Routine_Calib`, §6.2–§6.5) chạy trên `FakeVision`: chuỗi nạp điểm
  Align chạy đủ và **kết thúc với trục A ở 0°** tại đúng vị trí soi; hai phép kiểm tra (§6.2b
  top-down, §6.4b vòng xoay) đúng kỳ vọng TỪ CHỐI vì `FakeVision` không mô phỏng quang học —
  đây là hành vi đúng cần khoá lại, không phải nhánh "đạt" — và **trả trục về đúng vị trí/góc
  ban đầu kể cả khi không đạt**; hand-eye qua Flow `LocateTop` không treo/crash; mỗi lần soi ghi
  `IsTriggerLocate = 1`, mỗi lần nạp điểm hiệu chuẩn ghi 0;
- **chu trình Auto chạy thật trên tầng mô phỏng**: đặt được sản phẩm, đánh dấu ô đúng,
  ghi cycle time, STOP dừng ở checkpoint, START chạy tiếp, chặn khi trạng thái tay hút
  lệch với cảm biến, hết tray → chờ thao tác → xác nhận → chạy tiếp.

Nhóm "ràng buộc phần cứng" đáng nói riêng: nó không kiểm tra logic mà kiểm tra rằng
**các kết luận ở §11 vẫn còn đúng trong code**. Sáu tháng nữa có người sửa một giá trị
mặc định trong `SystemData` hoặc bật `HasAbsoluteEncoder` cho tiện thử nghiệm rồi quên
tắt, bài test này sẽ đỏ ngay thay vì để máy lao đầu hút xuống tray sau một lần EMG.

Chạy lại sau mỗi lần sửa `SafetyMonitor`, `McFunction` hay `Routine_Origin`. Những đường
này rất khó lặp lại đúng bằng thao tác tay, và là những đường mà một lỗi âm thầm sẽ biểu
hiện thành va chạm cơ khí chứ không phải một thông báo trên màn hình.

---

## 11. Các điểm còn cần chốt

| # | Vấn đề | Giả định tạm dùng nếu chưa có trả lời |
|---|---|---|
| 1 | Mỗi tay hút có cảm biến chân không riêng không? | Có — 2 DI riêng (DI08, DI09). Không có thì mất khả năng kiểm tra toàn vẹn ở §3.7 |
| 2 | Đổi tray thủ công hay tự động? | Thủ công: máy vào `WAIT_OPERATOR`, đèn vàng nháy, nút xác nhận trên Main |
| 3 | Số model sản phẩm cần quản lý | Nhiều model, chọn từ trang Teaching |
| 4 | ~~Kích thước tray cố định hay đổi theo model?~~ | **Đã chốt:** cấu hình được theo model, §8.1 |
| 5 | Có cần ghi dữ liệu lên MES / server không? | Không — chỉ CSV cục bộ |
| 6 | ~~Chiều dương từng trục~~ (gốc toạ độ/điểm 0 và `HomeDir` vẫn còn mở) | **Đã chốt (Tuấn, 2026-09-07):** nhìn từ phía người thao tác — X dương là trái→phải; Y dương là từ trong ra phía người thao tác (hướng ra ngoài); A1/A2 nhìn từ trên xuống, dương là ngược chiều kim đồng hồ (CCW). Gốc toạ độ (điểm 0) và dấu `HomeDir` của X/Y1/Y2/A1/A2 phụ thuộc vị trí lắp cảm biến gốc thật — vẫn xác định thực tế ở P1, ghi vào `system.json` |
| 7 | Thứ tự gắp trong tray | Theo hàng, trái→phải, trên→dưới; cấu hình qua `ScanOrder` |
| 8 | ~~CAM bottom-up gắn cố định hay gắn lên Y1/Y2?~~ | **Đã chốt:** gắn cố định. *Cập nhật 2026-09-12:* phần "hiệu chuẩn bằng checkerboard" không còn là quyết định của App — wizard checkerboard đã bỏ, cách hiệu chuẩn chọn trong VisionMaster (3 phương án, chưa chốt) — §6.3, §6.4 |
| 9 | ~~Chiều về gốc của Z1, Z2?~~ | **Đã chốt:** đi lên. Từng bảo vệ thêm bằng `HomeDirectionGuard`, đã bỏ 2026-09-09 theo yêu cầu Tuấn — chiều nay do tham số driver quyết định — §4.3 |
| 10 | ~~A1/A2 có cảm biến gốc không? Xoay đồng thời có va nhau không?~~ | **Đã chốt:** có cảm biến gốc, xoay tự do → về gốc song song, `HomeRotateSequential = false` — §4.3 |
| 11 | ~~Van hút có bị cắt khi EMG không?~~ | **Đã chốt:** giữ nguyên trạng thái khi EMG. Phần mềm cũng không tự tắt, có bài tự kiểm tra canh — §3.1 quy tắc 3 |
| 12 | ~~Có công tắc cho phép 3 vị trí cho chế độ Manual cửa mở không?~~ | **ĐÃ BỎ (2026-09-09):** cảm biến cửa tháo khỏi máy, câu hỏi này không còn áp dụng — mọi điều kiện an toàn cửa xoá khỏi code, xem §3.5. Giữ dòng này để tham khảo lịch sử quyết định cũ |
| 13 | ~~Có light curtain / cảm biến vùng không?~~ | **Đã chốt phần cứng:** có 1 rèm, đọc về Ezi-IO (DI13) → alarm 1105, chặn mọi chế độ, phải RESET. Chủ đầu tư chọn **không dùng relay an toàn cứng**; EMG cũng qua Ezi-IO → mất kết nối Ezi-IO là dừng khẩn — §3.1. **Phần mềm tạm tắt** (`HasLightCurtain = false`) cho tới khi #18 có trả lời |
| 14 | ~~Driver Panasonic dùng encoder tuyệt đối có pin không?~~ | **Đã chốt: KHÔNG**, về gốc bằng DOG → `RequireHomeAfterEmg` bị code khoá ở true; bắt buộc bắt xung Z sau DOG — §4.1, §4.3 |
| 15 | ~~Ống kính CAM bottom-up có phải telecentric không?~~ | **Đã chốt: KHÔNG** → hiệu chuẩn phải làm ở đúng cao độ mặt đáy sản phẩm. *Cập nhật 2026-09-12:* hiệu chuẩn bằng chính sản phẩm đang hút (§6.4 phương án A/C) thì cao độ tự đúng và ràng buộc "teach `Z_Pick` trước" biến mất; chỉ phương án B (bản chuẩn) mới còn cần đồ gá đúng cao độ |
| 16 | ~~FOV thực tế của CAM bottom-up là bao nhiêu?~~ | **Đã chốt: ≈ 30 × 30 mm** → 841 góc, bật bù méo ống kính — §6.3 |
| 17 | ~~Bản chuẩn checkerboard là loại chrome-on-glass hay in?~~ | **Đã chốt:** chrome-on-glass 1 × 1 mm. *Cập nhật 2026-09-12:* chỉ còn dùng nếu §6.4 chọn phương án B hoặc C (khi đó là bản chuẩn Hikrobot có mã), và việc hiệu chuẩn làm trọn trong VisionMaster |
| **18** | **Rèm sáng bảo vệ vị trí nào, và tray ra/vào có đi qua nó không?** (§3.5) | **Tạm gác — bàn sau.** Đến lúc đó coi như máy CHƯA có rèm sáng: `SystemData.HasLightCurtain = false`. Nếu tray ra/vào tự động cắt qua rèm thì cần **muting** (2–4 cảm biến muting + bộ điều khiển) trước khi bật cờ này lên, không thì alarm 1105 sẽ dừng máy mỗi chu kỳ |
| 19 | ~~Z1, Z2 có phanh giữ không, và phanh nhả bằng tín hiệu nào?~~ | **Đã chốt:** có tín hiệu cấp nhả phanh, bị ngắt khi Alarm hoặc Servo OFF. Còn phải chỉnh **thời điểm đóng phanh** khi cài driver ở P1 và nghiệm thu độ tụt < 0.5 mm — §3.1 quy tắc 5 |
