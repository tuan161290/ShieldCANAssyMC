# Kiểm chứng "Cẩm nang Calib & Teaching" — phần Vision Align

Cập nhật: 2026-09-12 · Trạng thái: **XONG**. Workflow kiểm chứng 112/116 agent (4 agent cuối dừng vì hết hạn mức phiên) → **46 phát hiện đứng vững, 2 không** · code app đã viết lại theo hướng mới (build sạch, `--selftest` **293/293**) · cẩm nang đã viết lại (`587a99f0` **v12**) · báo cáo kiểm chứng đã xuất bản (`f0f2a265`).

Việc còn lại đều phải làm **trên máy thật**: dựng solution theo hợp đồng `PLAN.md §6.8`, chạy Bước 2 để chốt dấu trục top-down, chạy Bước 4 cho cả hai tay, và chốt `DOF` của hiệu chuẩn bottom-up.

> **Bàn giao phiên:** [`Archive/2026-09-12-vision-calib/HANDOFF.md`](Archive/2026-09-12-vision-calib/HANDOFF.md) — tóm tắt mọi quyết định, hai chỗ đã tự đính chính (F13 và `World Rotate Angle` — chỗ sau đã bị **máy thật lật lại** 2026-09-12: phải bind `A1`/`A2`, HANDOFF §3.2 đã sửa theo), việc còn lại, danh sách file mã nguồn đã đổi; kèm nguồn HTML của hai artifact, text PDF Hikrobot + 26 trang Help, và bản ghi thô của phiên.

## Mục tiêu

Đối chiếu cẩm nang (artifact `587a99f0`, "Cẩm Nang Calib Vision") với:

- PDF Hikrobot *3. VisionMaster – Use of Calibration and Positioning Operation Modules_23Q3* (`E:\OneDrive\Tuan\Tuan\HIK\`).
- Help VisionMaster 4.4.0 (`C:\Program Files\VisionMaster4.4.0\Applications\Help\EN\`).
- Mã nguồn thật của app (`Routine_Calib.cs`, `PlacementSolver.cs`, `VisionConnect.cs`, `VisionMasterDevice.cs`, `VisionCalibViewModel.cs`, `VisionEditView.xaml`, `Routine_Auto.cs`, `ModelData.cs`, `SystemData.cs`, `PLAN.md §6`).

Đầu ra Tuấn đã chọn: **báo cáo kiểm chứng riêng (artifact mới) + cập nhật cẩm nang tại cùng URL**.

## Liên kết

- Cẩm nang hiện tại: https://claude.ai/code/artifact/587a99f0-64aa-4c6b-b2a6-e898c784cd1a?org=ed346aa7-6a67-4f43-8cbf-bc2f8e687eac
- **Báo cáo kiểm chứng**: https://claude.ai/code/artifact/f0f2a265-4604-4457-bc67-38657a0e5d21
- Plan đã duyệt: `C:\Users\Hoyoen\.claude\plans\memoized-moseying-waffle.md`
- Text trích từ PDF: `%LOCALAPPDATA%\Temp\claude\e--OneDrive-Tuan-2026-Actro-SO1C86-SHIELD-CAN-ASSY-MC-ShieldCANAssyMC\65e89b4f-210b-435b-8c98-d9e24daa9c5f\scratchpad\hik_calib_pdf.txt`
- Text trích từ Help (33 trang): `...\scratchpad\vm_help\*.txt` · danh sách toàn bộ tiêu đề Help: `...\scratchpad\vm_help_titles.txt`
- Workflow kiểm chứng: run `wf_285e918c-1fd` (script `...\.claude\projects\...\workflows\scripts\verify-vision-align-guide-wf_285e918c-1fd.js`)

## Phát hiện sơ bộ (đã đọc trực tiếp; đang được 3 agent phản bác độc lập mỗi phát hiện)

Mức: 🔴 sai cốt lõi · 🟠 sai quy trình · 🟡 sai nhãn/chi tiết

| # | Mức | Chỗ trong cẩm nang | Vấn đề | Bằng chứng |
|---|---|---|---|---|
| F1 | 🔴 | Bước 4, Phụ lục §1 bước 4 | "N-Point Calibration chế độ có xoay, 6 điểm, chỉ xoay" **không tồn tại**. N-point = Translation Times (thường 9, tịnh tiến X/Y) + Rotation Times (thường 3, xoay tại điểm 5), N≥4. Module chỉ-xoay là **Rotation Calibration** (≥3 lần, góc bằng nhau) nhưng chỉ cho tâm xoay, không cho scale. CAM bottom-up chỉ có chuyển động X → không dựng được 9 điểm tịnh tiến (PLAN §6.3 đã nêu). | Help N-point; Help Rotation Calibration; PDF p.16–18, p.48 |
| F2 | 🔴 | Bước 4 sơ đồ AlignBottom, Phụ lục §2 | Module **"Position Correction" không tồn tại**. Module thật (nhóm Calculation): *Single Point Alignment* (input toạ độ VẬT LÝ → cần Calibration Transformation trước; ra DeltaX/DeltaY/DeltaTheta), *Single Point Rectify*, *Single Point Grab*, *Single Point Map Alignment*. "Calib Transformation" tên đúng là **Calibration Transformation**. | Help TOC + 4 trang Help |
| F3 | 🔴 | Mục "Output → Align", Phụ lục §2 bước 5 | **Nguy cơ bù 2 lần**: Single Point Alignment tính sẵn Δx = x₁ − R(Δθ)·x₂ (đã gồm lượng dịch do xoay — xác nhận bằng hình công thức trong Help); PlacementSolver lại tính `e = R(Δθ)·p`. Cẩm nang phải chốt PoseX/PoseY = độ lệch so với **tâm xoay** (mm hệ máy, chưa xoay). Cấu hình nhất quán duy nhất với code hiện tại: AlignBottom = HPM → Calibration Transformation (file calib gốc = tâm xoay), **không** dùng Single Point*. | Help hình GUID-4FA69FF8; PDF p.35, p.60; `PlacementSolver.cs:92-111` |
| F4 | 🔴 | Công thức `Δθ = θt − θp` | Trộn 2 hệ quy chiếu góc: θt là góc pixel thô CAM top-down (MatchBoxAngle CW dương, chưa bù góc lắp camera), θp là góc trong hệ vật lý file calib bottom-up; CAM dưới là **ảnh gương** của CAM trên. Không có bước xác minh dấu góc. | PDF p.40; `PlacementSolver.cs:83,92`; `SelfTest.cs:533` |
| F5 | 🟠 | Bước 2 "Auto Calib 1→9" | Numpad app chạy **raster** (1,2,3/4,5,6/7,8,9); N-point tự sinh toạ độ theo **hình S** (hình Figure 2 trong Help: 1(−5,−5)→2→3(5,−5)→4(5,0)→5(0,0)→6(−5,0)→7(−5,5)→8→9) → điểm 4 và 6 hoán đổi. Cẩm nang không nói toạ độ 9 điểm vào VM bằng cách nào. | Help N-point Figure 2 (GUID-80C8BEB6); PDF p.18; `VisionCalibViewModel.cs:212-217,248` |
| F6 | 🟠 | Bước 2 bước 6, Bước 4 bước 5–6 | Ở Trigger Acquisition **mỗi lần Flow chạy là nạp thêm 1 điểm** — không có nút "nạp điểm" thủ công. Nút test / nút teach / chụp lại làm **bẩn danh sách điểm** → phải "Clear Point"; teach phải tách khỏi Flow calib (Demonstration cần External Input Character — App không gửi được qua `VmProcedure.Run()`; VM chỉ nhận qua giao tiếp TCP dạng `SetGlobalValue:var=…`). | Help N-point; Help Global/Local Variable; SDK samples |
| F7 | 🟠 | Bước 1, Hình 1 | Cross-check B3 dùng `OriginPixelU` = "first corner point in the upper left" → tấm chuẩn **lớn hơn FOV** thì sau khi X chạy 20 mm góc "đầu tiên" là góc **khác** → so sánh vô nghĩa. Điều kiện đúng: tấm nằm **trọn** trong FOV ở cả 2 vị trí, hoặc dùng Hikrobot I/II board có mã. Tấm phải gắn **trên gantry**. | Help CalibBoard; `Routine_Calib.cs:267-275` |
| F8 | 🟠 | Bước 3 | (a) Dịch **Y2** không làm sản phẩm trên tay hút dịch so với CAM bottom-up → phép thử vô nghĩa. (b) `BottomUpYSign` **không được dùng ở đâu** → câu "bù Y ngược chiều" sai; dấu Y nằm trong file calib VM (Coordinate System Mode / Left-Right-Handed Consistency). | grep BottomUpYSign; PLAN §6.3 |
| F9 | 🟠 | Bước 2 "Kết quả đúng" | "Dòng trạng thái hiện scale mm/px cùng RMS" **sai**: lối numpad không ghi `Calib.TopDown` → status "Chưa hiệu chuẩn", Bước 5 bị chặn, `IsUsable` chặn Auto. | `Routine_Calib.cs:177-202`; `VisionCalibViewModel.cs:97,130-138,362` |
| F10 | 🟠 | Bảng hợp đồng, sơ đồ Flow | Output cấp Flow phải **chọn kiểu** (int/float/string…) khi khai; App đọc mọi output bằng `GetOutputFloat`. Nguồn cẩm nang gợi ý (MatchNum, Module Status, CalibPointNum) là **Int** → cần **Variable Calculation** (Output Type = float) để đổi kiểu. Score: VM `MatchScore` 0..1, App coi 0..100. | Help "Configure Execution Logic of Multiple Procedures › Output Settings"; Help Variable Calculation; Help HPM |
| F11 | 🟠 | Bước 1 sơ đồ CalibBottomUp | VM CalibBoard Calibration **không có** ScaleU/ScaleV/SkewDeg; có Pixel Accuracy (mm/px), Scale (px/mm), Aspect, **Skew (radian)**, Rotate, CalibPointNum, CalibOriPointX/Y. App coi SkewDeg là **độ**. | Help CalibBoard; `Routine_Calib.cs:228-239` |
| F12 | 🟠 | Bước 2, Bước 4 | Camera Mode phải khai đúng: top-down = **Dynamic Camera**, bottom-up = **Camera Stationary Down**; Rotation Calibration cần góc **bằng nhau** + Physical Rotation Angle/Rotation Times. | Help N-point Run Parameters; Help Rotation Calibration |
| F13 | 🟠 | Bước 4 (phương án thay thế) | Cần chốt 1 trong 3 cách VM hỗ trợ cho CAM dưới chỉ có X+A: (A) **N-image Calibration** + Hikrobot board gắn trên tay hút; (B) Translation Calibration với Y=0 + DOF similarity (rủi ro suy biến); (C) CalibBoard Calibration (chỉnh Rotation Angle/Coordinate System Mode theo hướng +X) + Rotation Calibration lấy tâm xoay. Cần test trong VM. | Help N-image; PDF p.27; Help CalibBoard Figure 4 |
| F14 | 🟡 | Bước 4 bước 6 | Teach "mẫu chuẩn" = **Create Reference** trong module tính toán của Flow runtime, không nằm trong Flow calib. | Help Single Point Grab; PDF p.49 |
| F15 | 🟡 | Bước 4 AlignBottom | Đúng theo PDF p.45, nhưng thiếu ghi chú: teach lại `AlignPosX_NozzleN` thì phải calib/teach lại Flow. | PDF p.20, p.45; `Routine_Auto.cs:378-386` |
| F16 | 🟡 | Từ vựng, Bước 4, Bước 5 | Nhãn thật: "Hút #1" (không có ON); "Tới T1 Trigger" chỉ chạy **X** (double-click đúng); trang Manual **không có** nút "Chậm" (tốc độ Jog ở trang Teaching, toàn cục). | TeachingView.xaml:58-66,537-540,551; ManualView.xaml |

## Phát hiện MỚI từ vòng 1 (6/7 agent tìm lỗi đã xong — chưa qua phản bác)

Bản đầy đủ 88 phát hiện thô: `%LOCALAPPDATA%\Temp\claude\e--OneDrive-...\65e89b4f-...\scratchpad\journal_round1.txt`. Dưới đây chỉ các điểm **chưa có** trong F1–F16.

| # | Mức | Chỗ trong cẩm nang | Vấn đề | Bằng chứng |
|---|---|---|---|---|
| N1 | 🔴 | Bước 2 bước 1, Hình 2, Bước 5 | Dấu chuẩn "dán cố định trên **bàn**". Camera top-down chỉ đi theo X; hàng lưới được tạo bằng trục **Y2 (tray)**. Dấu không nằm trên tray/đồ gá đi theo Y2 thì 9 điểm co về 3 điểm thẳng hàng → N-point suy biến; hand-eye đọc Y2 cũng vô nghĩa. | `Routine_Calib.cs:189` MoveToCellAsync(Y2,…); PLAN §6.2 "dấu chuẩn trên đồ gá tray" |
| N2 | 🔴 | Bước 4 bước 6 | Sau chuỗi 0°→300°, trục A **đứng ở 300°** (lệnh góc tuyệt đối, không về 0). Ảnh mẫu chuẩn "không xoay" chụp ở 300° trong khi Auto soi ở góc lúc hút → mốc lệch 60°, Align bị loại (\|Δθ\|>15°) hoặc bù sai. | `Routine_Calib.cs:318-330`; `Routine_Auto.cs:366-370`; PDF p.49 "standard posture" |
| N3 | 🟠 | Bước 4 bước 3, Bước 6 | "Tới T1 Trigger" chạy X tới `AlignPosX_Nozzle1`, nhưng cẩm nang **không có bước teach** vị trí này (nút "Teach tay #1/#2", khung "Vị trí soi CAM1 (Bottom-up)" trang Teaching). Model mới = 0 → X chạy về 0. | `TeachingViewModel.cs:128-129,363-374`; `TeachingView.xaml:479-513` |
| N4 | 🟠 | Bước 5 | Hand-eye gọi Flow **LocateTop** (mẫu teach trên chi tiết thật trong ô tray) nhưng cẩm nang bảo soi **dấu chuẩn** → không khớp mẫu, "Không thấy dấu chuẩn". | `Routine_Calib.cs:356` |
| N5 | 🟠 | Bước 1 bước 1, Hình 1 | Cao độ tấm chuẩn ghi "bằng Z_Pick". Lúc soi bottom-up gantry ở **SafeZ** (nâng lên sau khi hút), Z_Pick là toạ độ trục lúc chạm hút trong tray → cần khớp cao độ **mặt đáy sản phẩm khi đang giữ ở SafeZ**. Z_Pick lại chỉ được teach ở Bước 6. | `Routine_Auto.cs:335,372-386,692` |
| N6 | 🟠 | Bước 2 bước 3, Quy tắc vàng 3 | Calibration Center Point + Bước X/Y nằm trong `system.json`, chỉ ghi đĩa khi bấm **Lưu ở trang Setting**; "Lưu" trang Teaching không lưu → tắt app là mất. | `VisionCalibViewModel.cs:161-179`; `SettingViewModel.cs:22-29,49-54` |
| N7 | 🟠 | Bước 4, Phụ lục §1 bước 3 | High Precision Match mặc định **Angle Range −45°…45°** → ở 60°…300° không tìm thấy. Vỏ đối xứng 180° mở full range thì dễ khớp nhầm hướng. | Help High Precision Match "Angle Range … default -45~45" |
| N8 | 🟠 | Bước 4 bước 7, Phụ lục §2 bước 4, Checklist | Ctrl+S **không tạo file calib**. Phải Set Calibration File Path + **Create Calibration File** (hoặc Update File); Calibration Transformation nạp qua Align File (Load Calibration File + Refresh Signal). | Help N-point, Single Point Grab, Load Calibration |
| N9 | 🟠 | Phụ lục §1 bước 8 | Output Flow khai ở tab **Output Params** của cửa sổ cấu hình Flow (không phải Global/Local Variable); tên phân biệt hoa/thường; phải chọn kiểu float. | Help "Configure Execution Logic of Multiple Procedures › Output Settings" |
| N10 | 🟠 | Bước 1 (thiếu) | CalibBoard Calibration bắt buộc **Physical Size = 1 mm** + Board Type = Checkboard; sai thì scale sai mà không báo lỗi. | Help CalibBoard "Physical Size … must be set according to the actual dimensions" |
| N11 | 🟠 | Cách đọc sơ đồ Flow, chú thích 3 | **Rectangle Search không có Score/MatchNum** → không bind đủ 5 output từ nhánh này. | Help Rectangle Search Module Result |
| N12 | 🟠 | Phụ lục §2 bước 6, Kiểm tra chéo | Nút góc chỉ trigger AlignCalib → **không kiểm chứng được AlignBottom**. App không có nút nào trigger AlignBottom ngoài Auto. | `Routine_Calib.cs:318-339`; `Routine_Auto.cs:385-386` |
| N13 | 🟡 | Bước 4 "Kết quả đúng" | Nút góc thành công **không hiện hộp thoại**; "Đã kích hoạt… báo tìm thấy" chỉ ở nút "Trigger AlignCalib (không xoay)". | `VisionCalibViewModel.cs:323-354` |
| N14 | 🟡 | Bước 1 "Kết quả đúng", "Nếu báo lỗi" | RMS/max luôn 0.000, góc luôn 0.00° (wizard không tính); lỗi thật là "không thấy đủ góc trên bản chuẩn"; thiếu 2 thông báo "góc mốc không dịch chuyển", "không thấy bản chuẩn tại điểm đối chiếu". | `Routine_Calib.cs:229-239,261-273`; `VisionConnect.cs:164` |
| N15 | 🟡 | Bước 1, 2 tag "làm 1 lần" | Hết hạn sau **90 ngày** (CalibValidDays, alarm 4090). | `SystemData.cs:146`; `Routine_Auto.cs:271-274` |
| N16 | 🟡 | Bước 1 proc-why | "Chưa có bước này thì Bước 4 bị chặn" sai — nút góc chỉ kiểm tra đã về gốc; Bước 1 chỉ là điều kiện chạy Auto. | `VisionCalibViewModel.cs:323-354`; `ModelData.cs:96-103` |
| N17 | 🟡 | Từ vựng | Tên Flow viết thiếu gạch dưới (AlignBottomTool1 ≠ `AlignBottom_Tool1`). | `SystemData.cs:313-319` |
| N18 | 🟡 | "Hai lỗi hay gặp" | "thiếu output" chỉ nằm trong **log**; hộp thoại chỉ báo "thất bại". Khi thiếu output, ảnh trang Main không cập nhật; log chỉ nêu output thiếu **đầu tiên**. | `VisionConnect.cs:129-140`; `VisionMasterDevice.cs:108-131` |
| N19 | 🟡 | Phụ lục §1 bước 2 | Image Source không có tham số trigger; trigger đặt ở **Global Camera** (Trigger Source = SOFTWARE). | Help Image Source; Configure a Global Camera |
| N20 | 🟡 | Hợp đồng | **Max Number to Find = 1** (App chỉ đọc phần tử [0]). | `VisionMasterDevice.cs:77,115` |
| N21 | 🟡 | Bước 2/4 "Nếu báo lỗi" | Timeout vision 3000 ms (alarm 4003) không được nhắc; nên đặt Flow Timeout/Overtime Control của HPM nhỏ hơn. | `SystemData.cs:135`; `VisionConnect.cs:191-204` |
| N22 | 🟡 | Checklist cuối | Dòng Hand-eye không bao giờ hiện "Chưa hiệu chuẩn"; "chạy mô phỏng" dùng model giả nên không kiểm chứng calib thật. | `VisionCalibViewModel.cs:126-128`; `McControl.cs:163-169` |
| N23 | 🟡 | Bước 6 bước 7 | Với Tray lắp ráp, khung đổi thành **"Cao độ đặt (Z_Place)"**. | `TeachingViewModel.cs:222-226` |
| N24 | ℹ️ | Khung "ma trận B11…B23 chưa có nguồn" | Hướng thứ 3: Load Calibration xuất "Calibration Matrix" (Float) — App đọc được nếu mở rộng đọc cả mảng. | Help Load Calibration |
| N25 | ℹ️ | Bước 4 bước 7 | Chưa xác minh Ctrl+S trong VMEditControl nhúng có lưu solution. | `VisionEditView.xaml:27-33` |
| N26 | ℹ️ | Bảng hợp đồng ImageData | App chỉ bật callback cấp Flow, mẫu SDK bật thêm cấp module → nghi đầu tiên nếu ảnh luôn null. | `GetResultControl/Form1.cs:305-308` |

## Code app đã cập nhật (2026-09-12)

Tuấn chốt hai điều, và code đã sửa theo:

1. **LocateTop trả mm** — VisionMaster quy đổi pixel → mm bằng module Calibration Transformation, App bỏ hẳn ma trận camera.
2. **VisionMaster tính luôn lượng bù Align** — App không còn tự nhân `R(Δθ)`.

Cách hiện thực (đã kiểm: build 0 lỗi/0 cảnh báo, `--selftest` **251 đạt / 0 không đạt**):

| Thay đổi | Chỗ |
|---|---|
| Flow mới `PlaceAlign` (module Single Point Alignment) — App gán 6 input float rồi Run, đọc `DeltaX/DeltaY/DeltaTheta` | `VisionMasterDevice.RunWithInputsAndGetFloats`, `VisionConnect.TriggerPlaceAlignAsync` |
| Flow mới `LocateMark` — soi dấu chuẩn cho hand-eye và phép kiểm tra (KHÔNG dùng Flow hiệu chuẩn để đo, vì mỗi lần chạy là nạp thêm một điểm) | `SystemData.VisionOutputNames`, trang Setting |
| `PlacementSolver` rút còn: dựng đích, đổi dấu góc, chặn ngưỡng | `PlacementSolver.cs` |
| Xoá: `CameraCalib`, `VisionCalibMath`, wizard checkerboard, `BottomUpYSign`, `CalibrateTopDownAsync` | `ModelData.cs`, `Routine_Calib.cs`, `VisionCalibMath.cs` (xoá file) |
| Auto Calib chạy **thứ tự hình S** `1,2,3,6,5,4,7,8,9` thay vì 1→9 (khớp quỹ đạo N-point tự sinh toạ độ) | `Routine_Calib.TopDownGridSOrder` |
| Chuỗi hiệu chuẩn Align: danh sách vị trí X + danh sách góc (cấu hình được), **tự đưa trục A về 0°** khi xong | `RunAlignCalibSequenceAsync` |
| **Kiểm tra top-down** (§6.2b): chạy X rồi Y2 một quãng đã biết, Flow phải báo đúng bấy nhiêu mm và ngược dấu — bắt sai thang đo, sai dấu, lẫn trục | `VerifyTopDownAsync` |
| **Phép thử vòng xoay** (§6.4b): xoay thử rồi hỏi Flow cách quay lại — cho ra dấu góc và bắt được tâm xoay/hệ toạ độ bị lật | `VerifyAlignRotationAsync` |
| Điều kiện chạy Auto đổi thành: hand-eye + phép thử vòng xoay, cả hai tay | `VisionCalib.IsUsable`, `Routine_Auto.CanRun` |
| Lưu Calibration Center Point ghi thẳng `system.json` (trước đây tắt app là mất) | `VisionCalibViewModel.SaveCalibrationCenter` |

`PLAN.md §6` đã viết lại theo đúng những thay đổi này (thêm §6.0 ranh giới VisionMaster ⇄ App, §6.2b, §6.4b).

**Lưu ý môi trường:** không build được bằng `dotnet build ShieldCanAssy.sln` trên máy này vì OneDrive đang không chạy cloud provider — mọi file trong `E:\OneDrive\Tuan\Tuan\Projects\ServoControl` là placeholder, đọc lên là lỗi *"The cloud file provider is not running"*. Sửa OneDrive rồi build lại bình thường.

## Các bước còn lại

1. ✅ Workflow đối kháng đã xong (run `wf_285e918c-1fd`, 112/116 agent — 4 agent cuối dừng vì hết hạn mức phiên, gồm 1 phát hiện chưa phản bác đủ và 2 agent rà "điểm cẩm nang đúng"). Kết quả đầy đủ: `%LOCALAPPDATA%\Temp\claude\e--OneDrive-...\65e89b4f-...\tasks\wm0w2xlb4.output`. Phát hiện nặng nhất đã xác nhận trùng đúng những gì đã sửa trong code: **"Position Correction" không tồn tại**, và Single Point Rectify/Grab **không dùng được cho máy này** vì bù X bằng gantry, Y bằng tray Y2, góc bằng A — VisionMaster gọi đó là "trục tách rời", không hỗ trợ. Module dùng được là **Single Point Alignment** (không có ràng buộc đó) — đúng module đã chọn cho Flow `PlaceAlign`.
   - Chạy lại lúc 2026-09-12 03:26 từ cache (6/7 agent tìm lỗi đã có kết quả; agent thứ 7 "hik-theory" chạy lại vì phiên trước bị ngắt).
   - Kiểm chứng chia cấp để giữ gần ngân sách ~60–90 agent của plan: phát hiện 🔴/🟠 → 3 agent phản bác độc lập (tài liệu / code / tác động); phát hiện 🟡/ℹ️ → kiểm theo lô 4 phát hiện, 1 agent phản bác tổng hợp.
   - Script: `.claude\workflows\verify-vision-align-guide.js` · Run ID: `wf_285e918c-1fd`. Nếu phiên lại bị ngắt, bảo Claude: *"chạy tiếp workflow wf_285e918c-1fd bằng script .claude\workflows\verify-vision-align-guide.js"* — các agent đã xong được lấy lại từ cache.
2. ✅ Artifact báo cáo kiểm chứng đã xuất bản: **`f0f2a265`** ("Kiểm Chứng Vision Align").
   - Nội dung: dải số liệu (119 thô → 60 sau gộp → 48 kiểm chứng → 46 đứng vững) · nguồn đối chiếu · sơ đồ khối cách kiểm chứng · **46 phát hiện** có bộ lọc theo mức, mỗi phát hiện gồm *chỗ trong cẩm nang / vấn đề / bằng chứng / đã xử lý ra sao* · 2 phát hiện không đứng vững · bảng 6 kết luận từ kiểm chứng tay (đọc ảnh PDF + reflection SDK) · trạng thái xử lý · giới hạn.
   - Dựng thủ công từ `result.confirmed` của `wm0w2xlb4.output` (script gom số liệu: `scratchpad\sum_wf.py`, `scratchpad\dump_ref.py`). Khung cũ `report_shell.html`/`gen_report.py`/`decisions.html` **bỏ** — soạn theo cấu trúc mới, và `decisions.html` đã lỗi thời vì mọi quyết định đều đã chốt.
   - **Phát hiện quan trọng khi đọc phần phản bác**: F13 bị bác 3/3 — lý do "phải teach mẫu chuẩn ở A = 0°, không thì Auto loại hết" là **SAI**. Camera dưới đo hướng thật của sản phẩm, không đo giá trị tuyệt đối của A; Auto cũng không bao giờ đưa A về 0 (cộng dồn `CurrentPos + Δθ`). Điều kiện đúng: **không xoay A giữa lúc hút sản phẩm mẫu và lúc chụp**. Đã sửa lại lý do trong `PLAN.md §6.4`, doc comment `RunAlignCalibSequenceAsync`, và cẩm nang Bước 3 (hành vi A→0° giữ nguyên vì vô hại và tiện).
   - F34 **không phải bị bác** — 2/3 agent phản bác không chạy được (hết hạn mức phiên) nên nó rơi khỏi ngưỡng phiếu. Nội dung của nó về sau được xác nhận độc lập bằng PDF tr.19.
3. ✅ Cẩm nang đã viết lại tại cùng URL (`587a99f0`, **version 7**, 2026-09-12) — bám theo code hiện tại chứ không theo danh sách sửa lô 2 cũ, vì app đã đổi hẳn.
   - Bản dựng: scratchpad `cam-nang-v2.html` (giữ nguyên hệ thiết kế cũ: token màu, font, khung Flow kiểu canvas VisionMaster; viết lại toàn bộ phần nội dung và dữ liệu `FLOWS` trong script). Bản sao lô 1 cũ `cam-nang-calib-vision.html` coi như bỏ.
   - **Ba sơ đồ khối mới ở đầu trang** (Tuấn yêu cầu, cho người mới): (1) ba bên — cơ khí / VisionMaster / App và ranh giới; (2) một sản phẩm đi qua máy lúc chạy Auto, từ lúc hút tới lệnh trục, ghi rõ dữ liệu chảy giữa các khối; (3) trình tự 6 bước với 2 cổng chặn.
   - Sáu bước mới: 1 top-down 9 điểm (hình S, Dynamic Camera, Create Calibration File) · 2 kiểm tra top-down · 3 hiệu chuẩn Align (3 phương án A/B/C để ngỏ, A→0°, Create Reference) · 4 phép thử vòng xoay · 5 hand-eye qua `LocateMark` · 6 teaching lưới tray (giữ nguyên).
   - Sơ đồ Flow vẽ lại cho 6 Flow: `TopDownCalib`, `LocateTop`, `AlignCalib_Tool1`, `AlignBottom_Tool1`, `PlaceAlign` + hình chú giải. Mọi câu "Kết quả đúng / Nếu báo lỗi" trích đúng chuỗi trong `VisionCalibViewModel.cs` và `Routine_Calib.cs`.
   - Có mục cuối **"Thay đổi so với bản trước"** liệt kê cái đã bỏ (checkerboard, dấu Y, Position Correction, N-point 6 điểm chỉ xoay), cái đã sửa (thứ tự hình S, LocateTop trả mm, dấu chuẩn trên đồ gá tray) và cái mới (Bước 2, Bước 4, hai Flow mới, A→0°).
4. ⬜ Cập nhật file này với link báo cáo và kết quả cuối.

## Việc cần Tuấn quyết

- ✅ **Ma trận CAM top-down lấy từ đâu** → chốt: LocateTop trả mm qua Calibration Transformation. Đã vào code.
- ✅ **Ai tính lượng bù Align** → chốt: VisionMaster tính (Flow `PlaceAlign`). Đã vào code.
- ✅ **Wizard checkerboard của App** → chốt: xoá hẳn. Đã vào code.
- ✅ **Phương án hiệu chuẩn CAM bottom-up** → chốt **A**: `Translation Calibration` · `TransAndRotateCalib` · `Camera Static` · `By Coordinate`, toạ độ vật lý lấy từ **biến toàn cục** App ghi. Căn cứ: PDF tr.48 (Hikrobot dùng đúng module đó cho kịch bản "tay hút giữ vật trên camera cố định", Physical Coordinate X lấy từ Protocol Parsing) · tr.18 mục 1 (nạp điểm theo giao thức thì mọi quỹ đạo hợp lệ → X thẳng hàng vẫn được) · tr.18 mục 3 + tr.17 (chỉ hiệu chuẩn **có xoay** mới đặt gốc toạ độ tại tâm xoay — thứ `Single Point Alignment` bắt buộc phải có, nên B và C bị loại).
  - `DOF`: thử `Affine` trước, không đạt thì hạ `Similarity` (5 điểm tịnh tiến thẳng hàng đủ cho 4 ẩn của similarity, không đủ cho 6 ẩn của affine). Đã vào cẩm nang Bước 3.
  - `Translation Times` tối thiểu **4** (dải hợp lệ [4,16]), `Rotation Times` [0,16] — đọc từ `TranslationCalibParam` trong guide.chm.
- ✅ **Cách gửi toạ độ sang VisionMaster** → chốt: **biến toàn cục** (`GlobalVariableModuleTool.SetVarFloat`), 5 biến `X/Y1/Y2/A1/A2`, ghi ngay trước khi Run trong cùng một lời gọi. Đã vào code + selftest (256/256).

## Việc phải làm trong VisionMaster trước khi chạy máy

0. **Thêm module `Global Variable`** khai 5 biến float `X`, `Y1`, `Y2`, `A1`, `A2` — App ghi vị trí encoder vào đó trước mỗi lần nạp điểm. Hai Flow hiệu chuẩn đặt `Calibration Points Input = By Coordinate` rồi subscribe *Physical Coordinate X/Y* và *World Rotate Angle* vào đúng biến (top-down: X ← `X`, Y ← `Y2`; bottom-up: X ← `X`, Y ← gõ số **0**, góc ← `A1`/`A2`). Thiếu module này thì App báo lỗi ngay lần kích Flow đầu tiên.
1. Dựng 8 Flow theo hợp đồng ở `PLAN.md §6.8`, trong đó **mới hoàn toàn**: `LocateMark` (soi dấu chuẩn, trả mm) và `PlaceAlign` (Single Point Alignment, 6 input float `TarX/TarY/TarA/ObjX/ObjY/ObjA`, 4 output `Found/DeltaX/DeltaY/DeltaTheta`).
2. `LocateTop` và `AlignBottom_Tool1/2` phải kết thúc bằng **Calibration Transformation** (trả mm), không trả pixel nữa.
3. Output cấp Flow phải là **float** — `MatchNum`, `Module Status`, `CalibPointNum` trong VM là Int, phải đi qua Variable Calculation.
4. `Max Number to Find = 1`; mở **Angle Range** của High Precision Match đủ rộng cho dãy góc hiệu chuẩn.
5. Hiệu chuẩn xong phải bấm **Create Calibration File** (Ctrl+S chỉ lưu solution).
6. Dấu chuẩn của CAM top-down dán trên **đồ gá tray** (đi theo Y2), không dán trên bàn máy.
7. **CAM top-down là trường hợp lai**: chạy X thì *camera* dịch (ảnh ngược chiều trục), chạy Y2 thì *dấu chuẩn* dịch (ảnh cùng chiều trục). Công tắc `Camera Mode` đảo dấu **cả hai trục cùng lúc** (PDF tr.19: đổi mode ≡ đảo thứ tự cả 9 điểm), nên không giá trị nào của nó làm đúng cả hai nếu hệ lệch. Cách xử theo kết quả Bước 2: sai dấu **cả hai trục** → đổi `Camera Mode`; sai dấu **đúng một trục** → thêm `Variable Calculation` tính `−X` (hoặc `−Y2`) trong Flow hiệu chuẩn rồi subscribe *Physical Coordinate* của trục đó vào kết quả ấy. App cố tình **không** tự đoán dấu này.
