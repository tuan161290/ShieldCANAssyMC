# Bàn giao phiên làm việc — Shield CAN Assy (26/09/2026)

Tài liệu này để một phiên Claude Code mới, chạy trên máy remote, làm tiếp đúng chỗ phiên cũ trên máy dev
dừng lại. Phiên cũ kéo dài từ 13/09 đến 26/09. Transcript của nó (~200 MB) nằm trên máy dev và không chép
sang đây, nên mọi điều cần biết phải có trong file này, trong `PLAN.md`, hoặc trong `BanGiao/memory/`.

## 0. Việc đầu tiên khi mở phiên mới

1. Đọc hết file này, rồi đọc `PLAN.md` ở gốc dự án. `PLAN.md` là tài liệu thiết kế sống: bản đồ trục,
   an toàn, về gốc, Auto, vision, alarm, dữ liệu, self-test.
2. Chép `BanGiao/memory/*.md` vào thư mục memory của phiên mới. Đường dẫn thư mục ấy nằm trong system
   prompt, mục Memory. Giữ nguyên tên file; `MEMORY.md` là mục lục. Đây là các luật Tuấn đã chốt trong
   hai tuần qua — thiếu chúng là lặp lại những lỗi đã bị nhắc.
3. **Phiên mới chạy ngay trên máy lắp ráp `1.2.1.218`** (Tuấn xác nhận 26/09) — máy có động cơ thật và
   App sản xuất. Đọc kỹ §2 trước khi build hay chạy bất cứ thứ gì: ở máy này, build sai một cờ là ghi đè
   App đang chạy. Mở thêm thư mục ServoControl làm thư mục làm việc bổ sung (§1).
4. Trả lời Tuấn bằng tiếng Việt, xưng "anh". Ngắn gọn; làm đúng phạm vi được hỏi.

## 1. Dự án

- Máy lắp shield can SO1C86 của Actro. C# / .NET Framework 4.8 WPF, VisionMaster 4.4 SDK.
  Có git từ 13/10/2026: hai repo bare trên SeaDrive, `ShieldCANAssyMC.git` và `ServoControl.git`
  (commit đầu "Initial import from ownCloud"). Trước đó không có lịch sử.
- `ShieldCanAssy.sln` gồm hai project:
  - `ShieldCanAssy.Core` — trục, I/O, routine (Auto, Origin, Calib), liên lạc VisionMaster, alarm, dữ liệu.
  - `ShieldCanAssy.Wpf` — giao diện, `SelfTest.cs` (chạy bằng `--selftest`), `FakeA6sfPort.cs`.
- **ServoControl** — thư viện Modbus cho driver Panasonic A6SF, **dùng chung với dự án
  Ama_Actro_HousingCorrosion**. Sửa nó thì hành vi mặc định phải giữ nguyên; cái mới bật bằng cấu hình.
- Core tham chiếu ServoControl bằng đường dẫn tương đối `..\..\ServoControl\ServoControl.csproj`
  (đổi 13/10/2026), nên ServoControl phải clone nằm cạnh thư mục dự án:

  ```
  R\ShieldCANAssyMC   ← dự án
  R\ServoControl      ← thư viện (repo git riêng)
  ```

  - Máy lắp ráp (máy có Servo Panasonic COM3–7, Fastech, remote IO; Wi-Fi hiện là `1.2.1.217`):
    `R` = `C:\PGM` từ 13/10/2026. **Đây là bản chính** — Tuấn chốt 26/09 chỉ làm việc trên máy lắp ráp.
    Bản trước đó, bố cục cũ, còn ở `C:\Users\Hoyeon\ownCloud\Actro\ShieldCanAssy\PGM\2026\Actro\SO1C86-SHIELD CAN ASSY MC\ShieldCANAssyMC`
    (ServoControl ở `...\ownCloud\Actro\ShieldCanAssy\PGM\Tuan\Projects\ServoControl`); shortcut
    "ShieldCanAssy.Wpf" trên Desktop vẫn chạy exe trong `bin\Debug\net48` của bản ownCloud này.
  - Máy dev cũ (`1.2.1.225`): bản ở `C:\Users\Tuan\OneDrive\Tuan`, bố cục cũ. Đứng yên từ 26/09, không dùng
    nữa; đừng chép gì từ đó về đè lên PGM.
- Trục (PLAN §1.1): X, Y1, Y2, Z1, Z2 là Panasonic A6SF qua Modbus RTU 115200 8E1, **mỗi trục một cổng
  COM** (X COM3, Y1 COM4, Y2 COM5, Z1 COM6, Z2 COM7). A1, A2 là Fastech.
- Cẩm nang hiệu chuẩn vision: `Archive/2026-09-12-vision-calib/artifacts/cam-nang-calib-vision-v14.html`,
  bản xuất bản là artifact `587a99f0` trên claude.ai. Skill `verify-vision-align-guide` kiểm cẩm nang
  đối chiếu PDF Hikrobot, trợ giúp VM 4.4 và code.

## 2. Máy nào là máy nào — đọc trước khi build hay chạy

| | Máy dev cũ `1.2.1.225` | Máy lắp ráp `1.2.1.218` — phiên mới chạy ở đây |
|---|---|---|
| Động cơ, I/O, camera | không có | **có thật** |
| App sản xuất | — | `C:\Users\Public\App` (share `\\1.2.1.218\Users\Public\App`); máy không có ổ D: nên dữ liệu (Config, Model, Log) nằm ở `_RunData` cạnh exe; `SimulationMode = false` |
| Bản build thử cục bộ | `ShieldCanAssy.Wpf\bin\Debug\net48\`, dữ liệu ở `D:\ShieldCanAssy`, `SimulationMode = true` — đúng, không bao giờ lật về `false` | `PGM\…\ShieldCanAssy.Wpf\bin\Debug\net48\`; exe này dùng `_RunData` **riêng** cạnh nó, tách khỏi App sản xuất; chưa có `system.json` thì App lấy mặc định `SimulationMode = true` |
| Log App | `D:\ShieldCanAssy\Log\<ngày>\app.log` | `C:\Users\Public\App\_RunData\Log\<yyyyMMdd>\app.log` (TSV: giờ, mức, nguồn, nội dung) |

`AppPaths.Init` chọn thư mục dữ liệu: `D:\ShieldCanAssy` nếu máy có ổ D:, không thì `_RunData` cạnh exe.

Luật đi kèm:

- **Trên máy này, build sai một cờ là ghi đè App đang chạy.** `ShieldCanAssy.Core.csproj` đặt
  `<BaseOutputPath>\\1.2.1.218\Users\Public\App</BaseOutputPath>` không điều kiện — chính là thư mục App
  sản xuất. Build kiểm tra luôn dùng:

  ```
  dotnet build ShieldCanAssy.sln -c Debug "-p:BaseOutputPath=bin\\"
  ```

  **Không bao giờ build cấu hình Remote** (Wpf đặt `OutputPath` tuyệt đối, `-p:BaseOutputPath` không ghi đè
  được). Đưa bản sửa vào App sản xuất là việc của Tuấn — hỏi, mỗi lần.
- **App sản xuất: config, model, log chỉ để đọc chẩn đoán.** Không ghi `system.json` hay `DEFAULT.json` của
  nó khi Tuấn chưa cho phép rõ ràng.
- **Bản build thử không được đụng phần cứng.** Đừng chép `system.json` của App sản xuất sang `_RunData` của
  bản build thử, đừng đặt `SimulationMode = false` ở đó: với `false` App mở cổng COM thật và điều khiển
  trục thật, trong khi các cổng ấy đang thuộc App sản xuất.
- **Self-test chỉ chạy khi App sản xuất đã đóng.** `run_selftest.py` tự từ chối khi thấy bất kỳ
  `ShieldCanAssy.Wpf.exe` nào đang chạy (App sản xuất cùng tên) — giữ nguyên chốt đó, vì hai App sẽ cùng kéo
  VisionMaster lên một máy. Cần chạy thì nhờ Tuấn đóng App.
- **Không đóng hay kill chương trình của Tuấn**, nhất là App sản xuất và VisionMaster. Chỉ được tắt tiến trình
  do chính mình mở, và các tiến trình VisionMaster con của nó (`vServerApp`, `AwakenGpuTool`), nhận ra qua
  `ParentProcessId`.
- **Không tự chụp màn hình.** Cần ảnh thì xin Tuấn. Vẽ trang WPF ra PNG trong tiến trình (`tools/snap`) thì được.

## 3. Luật làm việc Tuấn đã chốt

Bản đầy đủ nằm trong `BanGiao/memory/`. Tóm tắt những điều hay vấp:

- **Chốt Z_SAFE không thương lượng:** X/Y1/Y2 chỉ chạy khi **cả hai** Z ≤ Z_SAFE + 1 mm. 24/09 Tuấn đã bác
  phương án cho Z chạy song song X để rút chu kỳ — đừng đề xuất lại.
- **Không chạy máy từ màn hình cảm ứng.** Mọi lần chạy qua Cặp B hai tay; nút mềm chỉ để chọn chế độ.
- **Sự cố còn hàng trên tay hút:** chưa nhả thì quét lại tray, soi lại ở CAM bottom-up rồi đặt; đã nhả,
  không trả được, hay không rõ ô nguồn thì bắt người lấy hàng ra. Mở lại App thì phải Origin.
- **Làm đơn giản, trả lời ngắn.** Không thêm cơ chế phòng xa cần cấu hình. Tuấn chê "overthinking".
- **Đúng phạm vi.** "Sửa X cho khớp Y" nghĩa là đúng việc đó; việc liên quan chỉ đề nghị một dòng.
- **Gọi bước bằng tên** ("bước xác định hand-eye"), không bằng số. **Gọi đại lượng theo nghĩa vật lý**
  (`TopX_mm` là vị trí chi tiết đích trong ô), không gọi chung là "số".
- **Đổi thiết kế thì rà hết** `PLAN.md` và cẩm nang; trích nguyên văn thông báo của App.
- **Xét cả mặt cơ khí, không chỉ toán.**
- **Sửa XAML thì xoá `ShieldCanAssy.Wpf/obj` trước khi build** — không thì exe chết ngay lúc mở
  (`Cannot locate resource 'app.xaml'`) dù build vẫn báo thành công.
- Solution VisionMaster có đuôi **`.solw`**, không phải `.sol`.
- Không quét đệ quy rộng cả thư mục OneDrive (Tuấn đã từ chối một lần). Trong PowerShell không dùng
  `cd`/`Set-Location` — nó đổi thư mục làm việc của cả phiên.

## 4. Việc đã làm gần nhất

### 4.1 Bớt ghi Modbus trước mỗi lệnh chạy (25/09)

Tuấn: *"Việc ghi đi ghi lại các thanh ghi Modbus này làm tăng độ trễ phản hồi"*. PLAN §2 đoạn
"Lệnh chạy chỉ gửi thứ driver chưa có", §5.4.

- `ServoService` (ServoControl) nhớ giá trị các ô tốc độ/acc/dec và số block mà chính nó đã ghi (`_held`),
  chỉ ghi khi khác. Quên sạch khi về gốc, khi một giao dịch hay một lượt đọc trạng thái lỗi, và khi ghi
  tham số hay bảng block. Hai word vị trí đi chung một lần ghi (mã hàm 0x10).
- `ServoAxisConfig.AbsoluteRampRpm` (mới; 0 = cách cũ, mặc định cho HousingCorrosion). `PanasonicAxis` đặt
  nó bằng rpm của **Auto cao** (`MoveSpeed`), nên ô **Acc/Dec ở trang Setting nay là thời gian ramp ở tốc độ
  Auto cao**; lệnh chậm hơn dùng cùng gia tốc. Chặng Z chạy chậm 6 mm/s ramp ~6 ms thay vì 120 ms.
- Giao dịch trước STB: trước luôn 7; nay X/Y cùng tốc độ lệnh trước 2, Z hai chặng 3, ABS đầu tiên sau
  Jog 5, lệnh đầu sau về gốc hay sau lỗi 6.
- Kiểm chứng: bài `TestServoModbusWrites` (19 mục, driver giả `FakeA6sfPort`); gài 6 lỗi vào ServoService,
  cả 6 bị bắt; self-test **646 đạt / 0 không đạt, 316 s**; build 0 lỗi.

### 4.2 Nhịp đọc trạng thái 50 → 10 ms (26/09)

Tuấn: *"Vậy giảm thời gian polling mỗi vòng nữa nhé"*. PLAN §2 đoạn "Biết trục đã tới sớm hơn".

- Chỉ đổi `MonitorIntervalMs = 10` trong `PanasonicAxis.Connect`; thư viện giữ mặc định 50.
- `Task.Delay` làm tròn lên bội 15.6 ms của đồng hồ Windows: 50 thực tế ~63 ms, 10 thực tế ~16 ms. Đặt nhỏ
  hơn 10 không nhanh hơn.
- Đo bằng `tools/polltest` (vòng đọc thật của ServoService, driver giả có thời gian giao dịch, 200 lệnh),
  trễ trung bình mỗi lệnh ngoài thời gian trục chạy:

  | Thời gian một giao dịch Modbus | Nghỉ 50 ms | Nghỉ 10 ms |
  |---|---|---|
  | 4 ms (bộ chuyển USB nhanh) | 72 ms | 52 ms |
  | 18 ms (FTDI để Latency Timer mặc định 16 ms) | 198 ms | 173 ms |

  Cái giá đã tính trong bảng: lệnh mới hay phải chờ lượt đọc đang dở hơn (chờ khoá 2→6 ms và 18→38 ms).
- Build 0 lỗi. Self-test **không phủ** thay đổi này — chế độ mô phỏng dùng `FakeAxis`, không tạo `PanasonicAxis`.

### 4.3 Trước đó (25/09)

- **Bước xác định hand-eye chuyển sang cơ cấu kẹp** (PLAN §6.5, §8.3): ba điểm dạy trong model, bảy nút
  trên trang Hiệu chuẩn Vision, phép thử tổng quát ở chỗ Jog bất kỳ. Code và self-test xong; chưa đo trên máy.
- Trang Vision Edit: chọn bước bằng ComboBox; các chú thích dài gập lại dưới tiêu đề in đậm.

### 4.4 Trạng thái trên máy thật

Mọi thứ ở §4 phải chờ Tuấn build Remote mới lên máy. Chưa rõ máy đang chạy bản nào — so ngày giờ
`ShieldCanAssy.Wpf.exe` và `ServoControl.dll` trong thư mục App với ngày sửa (25–26/09).

## 5. Cần kiểm trên máy thật

1. **Ghi vị trí bằng mã hàm 0x10** — lần đầu gặp A6SF thật (thư viện vẫn dùng 0x10 để ghi tham số, nên
   nhiều khả năng ổn). Nếu driver từ chối: trục không nhúc nhích, `MoveTimeoutMs` (30 000 ms) sau có alarm
   với chi tiết `quá 30000 ms; đích …, hiện …`. Lỗi Modbus bên trong **không vào `app.log`** —
   `ServoService.OnLog` chưa nối vào log của App.
2. **Dòng log Z hai chặng** (`McFunction.LogTwoStageZ`, dạng `… hạ a→b: nhanh … mm … ms · chậm … mm … ms`):
   ramp chặng nhanh không đổi, nên cột "nhanh" giảm bao nhiêu là phần Modbus đã bớt; cột "chậm" giảm thêm
   vì ramp mới.
3. **Ramp mới** gắt hơn ở chặng chậm (vẫn không quá gia tốc ở Auto cao). Để ý driver báo quá dòng.
4. **Sửa bảng block bằng PANATERM trong lúc App đang mở thì về gốc lại** — App không biết driver đã đổi.
5. **Bộ chuyển USB-RS485 của COM3–COM7 là chip gì?** Nếu FTDI ("USB Serial Port"): Device Manager → Port
   Settings → Advanced → Latency Timer 16 → 1 ms. Theo số đo ở §4.2 đây là đòn bẩy lớn nhất (~120 ms mỗi lệnh).
6. **Hand-eye bằng cơ cấu kẹp:**
   - Gá cơ cấu kẹp (kẹp **chi tiết đích**, giữ con can đúng cao độ đặt của tray lắp ráp); tháo ra trước khi chạy Auto.
   - Dạy ba điểm; chạy năm nút cho **cả hai tay**.
   - Đo lại 2–3 lần trên cùng thế gá, so `Dx/Dy` ở dòng log `Hand-eye tay #N: dx … dy …`.
   - Phép thử tổng quát (nút 6 → 7) ở 4–5 chỗ Jog rải khắp khung hình, kể cả sát rìa.
   - Cuối cùng đặt một con hàng thật rồi nhìn (hoặc chạy khô ở bước nghiệm thu) — chỉ cách này nói được hand-eye
     có sai đều hay không. (Phép đo sau đặt bằng Flow `MeasurePlaced` đã bỏ 08/10.)
7. Còn từ trước: dạy `PickAngleA2` (đang 0.0); chốt `MaxCompensationAngleDeg`; đo bước vít / rpm thật của
   X trong PANATERM trước khi đụng `MoveSpeed` của X (X thật chạy ~5× số ở Setting vì `UnitPerRev` = 10 trong
   khi bước thật ~50 mm/vòng — **đừng khuyên nâng MoveSpeed X**); `MoveSpeedLow` 6 mm/s giữ hay đổi; Y1/Y2
   đặt 500 mm/s mà đo ra ~69 mm/s, chưa giải thích được.

## 6. Đề nghị đang treo — chỉ làm khi Tuấn bảo

- **Khoá Modbus theo từng lần đọc** thay vì cả lượt 5 lần đọc: lệnh chỉ phải chờ một giao dịch. Lấy lại
  phần chờ khoá ở §4.2 (khoảng 5–30 ms mỗi lệnh). Phải sửa ServoControl và bật bằng tuỳ chọn.
- Tắt nhấp nháy đèn CAM trong `Routine_Calib` (các bước hiệu chuẩn và chạy từng bước vẫn nháy).
- Bộ đếm "ô hiện tại N/M" ở trang Main đếm lùi thoáng qua khi cặp ô hút vắt qua hai hàng (chỉ là hiển thị).
- ~~`PLAN.md` §6.8: bảng Flow còn ghi `LocateMark` và `PlaceAlign`, tên output còn `PoseX/PoseY`.~~ Đã sửa
  09/10 cùng lúc chuyển sang **ba Flow** (`LocateTop`, `AlignBottom_Tool1/2`, rẽ nhánh soi / hiệu chuẩn theo biến
  `IsTriggerLocate`) — xem `PLAN.md` §6.8.
- ~~Câu hỏi mở: solution đã có Flow `MeasurePlaced` chưa?~~ Không còn: Tuấn bỏ hẳn phép đo sau đặt ngày 08/10.
- Chú thích gập mở cho trang Teaching, Setting; nới cột phải trang Vision Edit.
- Self-test nhanh hơn: hạ `VisionSettleMs` (200 ms) và `FakeVision.ProcessTimeMs` (120 ms) khi chạy
  `--selftest`, ước bớt 80–100 s.

## 7. Self-test

- Build cục bộ (§2), đóng App sản xuất (nhờ Tuấn), rồi `python BanGiao/tools/run_selftest.py`. Script tự
  tìm exe và thư mục dữ liệu theo đúng luật của `AppPaths`; kết quả ghi ra `<thư mục dữ liệu>\selftest.txt`.
  Mốc trên máy dev: **646 đạt / 0 không đạt, ~316 s**; đặt timeout 600 s. Máy lắp ráp chưa chạy lượt nào.
- Exe là WPF nên tách khỏi console; script chờ tiến trình `ShieldCanAssy.Wpf` thoát.
- `TestResumeIntegrity` → "Sau khi đồng bộ thì chạy lại được" thỉnh thoảng lỗi theo thời điểm; chạy lại là hết.
- Mỗi `Check` mất ~10 s (chậm ×25) là có chương trình giữ kênh debug output mà không đọc (21/09 là
  Lightroom). Dò bằng `tools/dbwin_owner.ps1`; báo Tuấn đóng, không tự tắt.
- Chi tiết và các bẫy khác: `memory/project_selftest_how_to_run.md`.

## 8. Công cụ trong `BanGiao/tools`

Chép từ scratchpad của phiên cũ. Trừ `run_selftest.py`, **đường dẫn trong các file đang trỏ về máy dev** (`C:\Users\Tuan\OneDrive\…`,
`D:\ShieldCanAssy\…`, `\\1.2.1.218\…`) — sửa cho khớp máy remote trước khi chạy.

| Công cụ | Dùng để |
|---|---|
| `run_selftest.py` | chạy `--selftest`, đường dẫn tự suy ra; lật tạm `SimulationMode` nếu cần, trả `system.json` về đúng từng byte; từ chối chạy khi có `ShieldCanAssy.Wpf.exe` đang mở |
| `modbustest/` | chạy riêng bài `TestServoModbusWrites` bằng reflection (vài giây) |
| `mutate_modbus.py` | gài 6 lỗi vào `ServoService.cs`, kiểm bài trên bắt được. Bẫy: phải chép cả thư mục bin ra bản sao rồi thay DLL trong đó (xem `memory/reference_fake_a6sf_modbus.md`) |
| `polltest/` | đo trễ đầu-cuối của lệnh chạy: `PollTest.exe <số lệnh> [Tx ms] [nghỉ ms]` |
| `ramp_table.py` | đọc `system.json` của máy (chỉ đọc), so thanh ghi ramp cũ/mới |
| `cycle_model.py`, `cycle_variants2.py` | mô hình thời gian chu trình. Phần tốc độ X đã bị số đo bác — chỉ tin hướng, đừng trích số |
| `dbwin_owner.ps1` | tìm chương trình giữ kênh debug output |
| `snap/` | vẽ trang WPF ra PNG ở khổ màn máy thật. Sau mỗi lượt phải tắt `vServerApp`/`AwakenGpuTool` con của nó (bản trong đây tự làm) |

## 9. Tham khảo

- `PLAN.md` — thiết kế; §10.1 mô tả self-test.
- `KIEM_CHUNG_VISION_ALIGN.md` — kết quả kiểm chứng cẩm nang ngày 12/09.
- `BanGiao/memory/` — luật và kiến thức đã kiểm trên máy (năng lực VM 4.4, tạo biến toàn cục qua SDK,
  vết Group Cmd, mô hình chu trình…).
- Transcript đầy đủ phiên cũ (chỉ trên máy dev cũ `1.2.1.225`):
  `C:\Users\Tuan\.claude\projects\c--Users-Tuan-OneDrive-Tuan-2026-Actro-SO1C86-SHIELD-CAN-ASSY-MC-ShieldCANAssyMC\e083c7ae-0deb-4155-be33-3b7bec1d9c4b.jsonl`
