# Bàn giao phiên làm việc — Vision Calib & Trục quay

**Ngày:** 2026-09-12 · **Phạm vi:** hiệu chuẩn vision (CAM top-down + bottom-up) và quy đổi
toạ độ trục quay A1/A2.

Đọc file này trước, rồi mở `PLAN.md` §6 (vision) và §2.4/§4.3 (trục) để vào chi tiết.

> **Cập nhật 2026-09-13:** máy thật đã bác bỏ đính chính §3.2 — `World Rotate Angle` phải
> **bind** vào biến `A1`/`A2`, không để trống. Các chỗ nói về ô đó trong file này đã sửa theo;
> phần còn lại giữ nguyên như lúc bàn giao.

---

## 1. Trạng thái lúc bàn giao

| | |
|---|---|
| Build | `dotnet build ShieldCanAssy.sln -p:BaseOutputPath=bin\` → **0 Error** |
| Tự kiểm tra | `ShieldCanAssy.Wpf.exe --selftest` → **293 đạt / 0 không đạt** |
| Kết quả tự kiểm ghi ở | `D:\ShieldCanAssy\selftest.txt` |
| Cẩm nang (artifact) | **v14** (14/09/2026, phương án B; sửa 22/09: mốc góc thay Create Reference, soi CAM bottom-up ở Z_SAFE) — https://claude.ai/code/artifact/587a99f0-64aa-4c6b-b2a6-e898c784cd1a |
| Báo cáo kiểm chứng | **v1** — https://claude.ai/code/artifact/f0f2a265-4604-4457-bc67-38657a0e5d21 |

> `BaseOutputPath` phải truyền vào vì `ShieldCanAssy.Core.csproj` đặt
> `<BaseOutputPath>\\1.2.1.218\Users\Public\App</BaseOutputPath>` — không có cờ đó thì build
> ghi ra đường dẫn mạng.

---

## 2. Những gì đã chốt trong phiên này

### 2.1 Hiệu chuẩn CAM bottom-up — đã chốt hoàn toàn

Camera dưới chỉ nhìn được **chuyển động X và xoay A**, không có Y. Phương án:

- Module **Translation Calibration** (không phải N-Point — nó có thêm `TransAndRotateCalib`
  và `UnionCalibEnable`).
- `Calibration Type` = **TransAndRotateCalib** · `Camera Mode` = **Camera Static**
- `Calibration Points Input` = **By Coordinate**
- Bind: `ImagePointX`, `ImagePointY`, `Image Angle` ← High Precision Match ·
  `Physical Coordinate X` ← biến toàn cục `X` · `Physical Coordinate Y` = gõ `0`
- **`World Rotate Angle` ← biến `A1`/`A2`** (đúng trục của tay) — **bắt buộc**, xem §3.2 bên dưới.
- `Translation Times` = **5** · `Rotation Times` = **6** · `UnionCalibEnable` = tắt
- `DOF`: thử **Affine** rồi **Similarity** trên *cùng bộ điểm đã chụp* (đổi DOF + Calibrate
  lại, không phải chụp lại), giữ cái có `Real Error of …` nhỏ hơn. Trọng tài cuối là Bước 4.

Thao tác chụp:

```
X = −8 → −4 → +4 → +8 → 0     5 lần chụp, A đứng yên
rồi 6 góc xoay, X đứng yên tại 0
```

Điểm giữa rơi đúng lần chụp thứ 5 — khớp Help (*"the rotation is performed at the position of
the 5th point"*) và tr.48 (*"Returning to the center point… keeping XY fixed during rotation"*).

**Bằng chứng:** PDF tr.17 (hiệu chuẩn chỉ-tịnh-tiến ra tâm xoay chứ không ra điểm gắp) ·
tr.18 §1 (nạp điểm kèm toạ độ thì *"any trajectory of the machine is possible"* → các điểm
thẳng hàng theo X là hợp lệ) · tr.18 §3 + tr.17 (chỉ hiệu chuẩn CÓ XOAY mới đặt gốc toạ độ
tại tâm xoay — thứ Single Point Alignment đòi) · tr.22 và tr.48 (hai cấu hình mẫu của chính
Hikrobot).

**Đã cân nhắc và loại:** phương pháp *mapping* cam trên/cam dưới bằng vật mẫu (tr.52) — nó
cần **cả hai** file (mapping **và** hiệu chuẩn tịnh tiến+xoay của cam dưới), tức là cộng thêm
chứ không thay thế; lại giả định cam trên chỉ chụp ở một tư thế cố định.

### 2.2 Trục quay A1/A2 — truyền động và toạ độ

- Động cơ **10000 xung/vòng động cơ**, hộp số **1/2** → **20000 xung/360° đầu hút** =
  **55.5556 xung/độ**, phân giải 0.018°/xung. (Trước đó code để 27.7778 — trục xoay đúng
  **một nửa** góc được lệnh mà không báo lỗi gì.)
- Toạ độ A1/A2 **quấn vòng [0, 360)**. Ba hệ quả, phải nhớ cả ba:
  1. Lệnh chạy tuyệt đối **luôn đi đường ngắn** (350° → 10° là +20°).
  2. **Không có hàng rào hành trình** — `InSoftLimit` luôn đúng với trục quay.
  3. **So hai góc phải dùng `AxisAngle.Distance`**, không trừ thẳng. Trục dừng *quanh* đích,
     nên "về 0°" thực tế đọc ra 359.996°.
- `AxisConfig.HomeOffset` của trục quay = **góc máy tại vị trí cảm biến gốc**, áp trong phần
  mềm (không ghi vào bộ đếm driver) nên sửa ở trang Setting có hiệu lực ngay.

### 2.3 Biến toàn cục gửi vị trí trục

Solution phải có module **Global Variable** khai 5 biến float `X`, `Y1`, `Y2`, `A1`, `A2`
(tên sửa được ở trang Setting). App ghi vị trí encoder vào đó **trong cùng một lời gọi** với
lệnh kích Flow (`IVisionDevice.TriggerCalibAsync`) — tách ra là mở đường cho một lần kích
Flow lọt vào giữa, nạp điểm mới với toạ độ cũ mà không có dấu hiệu gì.

SDK: `GlobalVariableModuleTool.SetVarFloat(tên, float[])`. App đối chiếu tên bằng
`GetAllGlobalVar()` trước khi ghi — thiếu biến thì **ném lỗi kèm danh sách biến đang có**.

Flow hiệu chuẩn bottom-up subscribe `X` (vào *Physical Coordinate X*) **và** `A1`/`A2` (vào
*World Rotate Angle*) — xem §3.2.

---

## 3. Hai chỗ đã tự đính chính — đọc kỹ (§3.2 đã bị máy thật lật lại)

### 3.1 F13 — lý do "teach mẫu chuẩn ở A = 0°" là SAI

Đợt kiểm chứng bác 3/3. Camera dưới đo **hướng thật của sản phẩm**, không đo giá trị tuyệt
đối của A; và Auto **không bao giờ** đưa A về 0° (nó cộng dồn `CurrentPos + Δθ`), nên không
tồn tại tư thế "A = 0° lúc chạy Auto" để mà lệch so với.

Điều kiện đúng: **không xoay A giữa lúc hút sản phẩm mẫu và lúc chụp.**

Hành vi A→0° ở cuối chuỗi hiệu chuẩn **vẫn giữ** (vô hại, để máy ở trạng thái xác định),
nhưng lý do đã sửa ở ba chỗ: `PLAN.md` §6.4, doc comment của
`Routine_Calib.RunAlignCalibSequenceAsync`, và Bước 3 của cẩm nang.

### 3.2 `World Rotate Angle` PHẢI bind vào biến A — bản "để TRỐNG" đã bị máy thật bác

Bản bàn giao gốc ghi ở đây: *để TRỐNG*, dựa trên câu Help *"This parameter and the Physical
Coordinate X/Y cannot be used at the same time"* và hai cấu hình mẫu tr.22, tr.48 đều bỏ trống ô
này. **Sai với máy này.**

**Bằng chứng (2026-09-12):** để trống thì nạp đủ 11 điểm, bấm Calibrate, nhận `Error
code:10103002 Matrix calculating error` — lặp lại y hệt qua nhiều lần. Bind vào
`Global Variable1.A2` là chạy ngay; Bước 4 sau đó đạt, sai số dư 0.011 mm trên độ lệch tâm
4.535 mm, dấu góc +1. Góc thử chỉ 4° nên đó là bằng chứng cấu hình **chạy được và đúng dấu**,
chưa phải bằng chứng tâm xoay chính xác tới 0.01 mm.

**Vì sao lập luận cũ hỏng.** Câu Help nói về việc **sinh toạ độ điểm**. Phép fit tâm xoay của
module thì cần một kênh cho biết vật đã xoay bao nhiêu: trong pha xoay `Physical Coordinate X/Y`
đứng yên theo đúng thiết kế, nên kênh góc duy nhất còn lại là `Image Angle` — ở máy này lấy từ
`Match Box Angle`, **quấn vòng mỗi 180°** vì sản phẩm gần đối xứng. Không còn kênh góc nào dùng
được → phép fit vô nghiệm, đúng như mã lỗi nói. Ví dụ của Hikrobot khớp mẫu vật không đối xứng
nên không gặp chuyện này.

Cấu hình đúng:

- `AlignCalib_Tool1`: `World Rotate Angle` ← `A1` · `AlignCalib_Tool2`: ← `A2`. Nhầm trục thì
  giá trị đứng im trong pha xoay và lại báo đúng mã lỗi trên.
- **Vẫn bind `Image Angle`** — hai kênh không loại trừ nhau.
- Toạ độ A quấn vòng [0, 360): dãy góc không được cắt qua vạch 0 (`0→60→…→300` thì an toàn).
- **Bước 4 vẫn bắt buộc.** Module biết chiều xoay trong hệ toạ độ của file hiệu chuẩn, chưa có
  nghĩa là trùng dấu lệnh trục A của App; Bước 4 đo dấu đó, và bắt được lỗi trỏ nhầm file.

---

## 4. Việc còn lại — đều cần máy thật

1. **Dựng solution VisionMaster** theo `PLAN.md` §6.8: module Global Variable (5 biến float)
   + 8 Flow (`LocateTop`, `LocateMark`, `TopDownCalib`, `AlignBottom_Tool1/2`,
   `AlignCalib_Tool1/2`, `PlaceAlign`).
2. **Đo `HomeOffset` của A1 và A2** (quy trình 6 bước ở cẩm nang, mục *Trước khi bắt đầu*).
3. **Chạy Bước 2** để chốt dấu trục của hiệu chuẩn top-down. Đây là **trường hợp lai**: chạy
   X thì *camera* dịch, chạy Y2 thì *dấu chuẩn* dịch → hai trục ngược dấu nhau, mà công tắc
   Camera Mode đảo **cả hai cùng lúc** (tr.19). Không giá trị nào của nó làm đúng cả hai —
   sửa bằng một module **Variable Calculation** đảo dấu một trục trong Flow hiệu chuẩn.
   **App cố ý không đoán dấu.**
4. **Chạy Bước 4** cho cả hai tay để lấy dấu góc và kiểm tâm xoay.
5. **Chốt DOF** của hiệu chuẩn bottom-up (Affine → Similarity, xem §2.1).

### Hai điều chưa xác minh được lúc bàn giao, phải nhìn màn hình VisionMaster

- ~~`World Rotate Angle` có tự xám đi sau khi bind `Physical Coordinate X` không?~~
  **Đã rõ trên máy (2026-09-12):** không xám, bind được, và là **bắt buộc** — xem §3.2.
- `Translation Times` có nhận giá trị **5** không (Help ghi khoảng hợp lệ là 4…16).

---

## 5. Mã nguồn đã đổi trong phiên này

### Vision / biến toàn cục

| File | Nội dung |
|---|---|
| `Core.Communication\IVisionDevice.cs` | thêm `AxisSnapshot`, `TriggerCalibAsync` |
| `Core.Communication\VisionConnect.cs` | `TriggerCoreAsync`, `TriggerCalibAsync`, `AxisVars` — ghi biến + kích Flow trong **một** lời gọi đã khoá |
| `Lib.VisionMaster\VisionMasterDevice.cs` | `SetGlobalFloats`, `GetGlobalVarModule`, `RunWithInputsAndGetFloatsWithImage`, `GetInputNames` |
| `Core.Controller\Routine_Calib.cs` | `Snapshot()`; cả 5 lệnh kích Flow hiệu chuẩn đi qua `TriggerCalibAsync` |
| `Core.Data\SystemData.cs` | `VisionOutputNames`: `GlobalVarModule`, `GvAxisX/Y1/Y2/A1/A2` |
| `Core.Communication\Sim\FakeVision.cs` | `LastCalibAxes`, `CalibPointCount` |
| `Wpf\Views\SettingView.xaml` | ô nhập tên module + tên 5 biến toàn cục |
| `ShieldCanAssy.Core.csproj` | tham chiếu `GlobalVariableModuleCs.dll` |

### Trục quay

| File | Nội dung |
|---|---|
| `Core.Communication\AxisAngle.cs` | **MỚI** — `Wrap`, `ShortestDelta`, `Distance` |
| `Core.Communication\AxisConfig.cs` | `WrapDegrees`; `InSoftLimit` luôn đúng với trục quay; doc `HomeOffset` |
| `Core.Communication\AxisScale.cs` | `PulsePerRev` (phải tròn — mọi phép chọn vòng cộng trừ bội số của nó) |
| `Core.Communication\FastechAxis.cs` | `_rawPulse`, `ToMachine`, `TargetPulse` (chọn vòng gần nhất), `WaitReachedAsync` so **theo xung** |
| `Core.Communication\Sim\FakeAxis.cs` | `_pos` giữ thô, `CurrentPos` quấn; `RunAbsAsync`; bỏ cữ hành trình khi quay |
| `Core.Data\SystemData.cs` | hằng số `Rotary*`; mặc định A1/A2; `MigrateRotaryAxes()` |
| `Core.Data\McDataManager.cs` | gọi migration lúc nạp `system.json` + ghi log cảnh báo |
| `Wpf\SelfTest.cs` | `TestRotaryWrap` (37 khẳng định mới) |

> **`system.json` cũ tự sửa.** File viết trước 2026-09-12 còn ghi 10000 xung/360°;
> `MigrateRotaryAxes()` nhận ra (cờ `WrapDegrees` còn tắt trên trục Fastech đơn vị `deg`),
> ghi đè **hằng số thiết kế** và bật quấn vòng, nhưng **giữ nguyên `HomeOffset`** vì đó là
> số đo được. Chạy một lần, có log.

---

## 6. Nội dung trong thư mục này

```
HANDOFF.md                              file này
artifacts/
  cam-nang-calib-vision-v14.html        nguồn của artifact 587a99f0 (v14 — bản đang dùng)
  cam-nang-calib-vision-v12/v13.html    bản cũ, giữ để tra lại
  bao-cao-kiem-chung-v1.html            nguồn của artifact f0f2a265 (v1)
reference/
  hik_calib_pdf.txt                     text trích từ PDF Hikrobot 61 trang
  vm_help/*.txt                         26 trang Help VisionMaster 4.4.0 liên quan
transcript/
  65e89b4f-….jsonl                      bản ghi thô của phiên (30 MB)
```

**Ảnh từng trang PDF** (thứ đọc được rõ hơn hẳn bản text, và là nguồn của mọi kết luận quan
trọng nhất) nằm ở `E:\OneDrive\Tuan\Tuan\HIK\Positioning` — không chép vào đây vì đã ở trên
OneDrive rồi. Các trang hay dùng: **17, 18, 19, 22, 33, 35, 39, 40, 45, 48, 49, 52, 60**.

**Sửa cẩm nang/báo cáo:** sửa file HTML trong `artifacts/` rồi xuất bản lại **vào đúng URL cũ**
(truyền `url` của artifact). Xuất bản mà không truyền `url` sẽ tạo ra một artifact **mới**,
không phải cập nhật cái đang có.

### Mở lại phiên Claude trên máy khác

Chép `transcript\65e89b4f-….jsonl` vào
`%USERPROFILE%\.claude\projects\e--OneDrive-Tuan-2026-Actro-SO1C86-SHIELD-CAN-ASSY-MC-ShieldCANAssyMC\`
rồi chạy `claude --resume` trong thư mục dự án.

Chỉ hoạt động khi máy kia mở dự án ở **đúng đường dẫn**
`E:\OneDrive\Tuan\2026\Actro\SO1C86-SHIELD CAN ASSY MC\ShieldCANAssyMC` — tên thư mục kia
chính là đường dẫn đã mã hoá. Ổ đĩa hay cấu trúc khác thì tên khác và `--resume` không thấy.

Không khôi phục được thì cũng không sao: file này cộng `PLAN.md` cộng cẩm nang là đủ để đi
tiếp, `--selftest` xác nhận mã nguồn còn nguyên trạng thái đã ghi ở §1.
