---
name: project-selftest-how-to-run
description: "Cách chạy --selftest trên máy dev — máy dev không có động cơ nên system.json nằm sẵn SimulationMode=true, không được lật về false; ~315 s; sửa XAML thì phải xoá ShieldCanAssy.Wpf/obj trước khi build; kết quả ở D:\\ShieldCanAssy\\selftest.txt"
metadata: 
  node_type: memory
  type: project
  originSessionId: e083c7ae-0deb-4155-be33-3b7bec1d9c4b
  modified: 2026-09-25T09:40:17.041Z
---

Chạy self-test: `dotnet build ShieldCanAssy.sln -p:BaseOutputPath=bin\` rồi
`ShieldCanAssy.Wpf\bin\Debug\net48\ShieldCanAssy.Wpf.exe --selftest`.

- Exe là WPF nên **tách khỏi console** — lệnh trả về ngay, phải `WaitForExit` trên tiến trình
  `ShieldCanAssy.Wpf`. Kết quả ghi ra `%AppPaths.Root%\selftest.txt`, trên máy này là
  `D:\ShieldCanAssy\selftest.txt`. Không có tuỳ chọn dòng lệnh để đổi thư mục dữ liệu.
- **Máy dev không có động cơ hay thiết bị ngoại vi** — `D:\ShieldCanAssy\Config\system.json` ở đó
  để **`SimulationMode = true` là đúng** (Tuấn nói 2026-09-21). KHÔNG bao giờ lật về `false`, kể cả khi
  tưởng là "kẹt". Hôm 21/09 tôi thấy `true`, đoán là runner bị cắt ngang rồi tự trả về `false` — sai,
  Tuấn phải bảo mới trả lại. Self-test phải chạy với `true` (với `false` app đi tìm servo thật và treo).
- `run_selftest.py` (scratchpad): file đang `true` thì chạy nguyên trạng; đang `false` (máy khác) thì
  lật tạm sang `true`; lúc nào cũng sao lưu và trả về đúng từng byte.
- Một lượt mất **~315 giây** (23/09 rút từ 435 s) — vẫn đặt timeout lệnh **600000 ms**.
- **Sửa XAML rồi build thì phải `rm -rf ShieldCanAssy.Wpf/obj` trước.** Không thì exe chạy lên là chết ngay
  `System.IO.IOException: Cannot locate resource 'app.xaml'` (~1.6 s, exit 0xE0434352) và `run_selftest.py`
  chỉ báo REPORT NOT UPDATED. Nguyên nhân: markup compiler của WPF chạy qua project tạm
  `*_wpftmp.csproj`, gặp `-p:BaseOutputPath=bin\` truyền ngoài dòng lệnh thì `.g.resources` nằm lệch chỗ.
  **Build vẫn báo thành công** — đừng đi tìm lỗi trong code C# (25/09 mất một lượt vì chuyện này).
- Cuối báo cáo có bảng **thời gian từng bài**. Thời gian nằm ở ~10 kịch bản chạy máy mô phỏng (98%), không ở số mục: 24 bài logic thuần có 355/545 mục mà chỉ tốn 8 s. Muốn nhanh nữa thì hạ `VisionSettleMs` (200 ms) và `FakeVision.ProcessTimeMs` (120 ms) khi chạy --selftest — ước còn ~80–100 s nữa, chưa làm.
- **Chậm ×25 (mỗi Check ~10 s) = có chương trình giữ kênh debug output mà không đọc.** `Check()` → `Debug.WriteLine`
  → `OutputDebugString`, hàm này chờ tối đa 10 s. 21/09 thủ phạm là **Lightroom** (giữ section `DBWIN_BUFFER`).
  Đo nhanh: gọi `OutputDebugStringW` một lần trong PowerShell, ~10000 ms là bị. Dò chủ: scratchpad `dbwin_owner.ps1`.
  Không tự tắt app của Tuấn — báo Tuấn đóng Lightroom. 22/09 Lightroom mở lại vẫn giữ kênh nhưng có đọc (2 ms) → chạy
  bình thường; nên đo trước khi kết luận. Lượt chậm 21/09 còn làm hỏng dây chuyền các bài canh giờ (TestResumeIntegrity,
  TestTrayChangeover) — lỗi đó là do chậm, không phải code.
- `run_selftest.py` in dòng KẾT QUẢ **trước**, sau đó in một đoạn TestAssyTrayFull — đừng `tail` output của nó.
- `TestResumeIntegrity` → bài "Sau khi đồng bộ thì chạy lại được" **chập chờn theo thời điểm**
  (thỉnh thoảng lỗi "còn alarm chưa xoá"). Chạy lại là hết. Không phải hồi quy — cân nhắc kỹ
  trước khi kết luận bài này là lỗi thật.
- Ngoại lệ không bắt trong một bài làm app **đứng im** (tiến trình còn sống, không cửa sổ, CPU gần 0) thay vì thoát:
  xem `D:\ShieldCanAssy\Log\<ngày>\app.log` tìm "Ngoại lệ không xử lý", rồi tắt tiến trình để `run_selftest.py` trả system.json.
  Đã gặp 13/09: bài đếm `run.InputTray.Cells.Count(...)` trong lúc Auto đang ghi ô → sửa bằng `Cells.ToArray()` (15 chỗ).
- Mốc hiện tại: **646 đạt / 0 không đạt — 316 s** (2026-09-25 chiều, thêm 19 mục `TestServoModbusWrites` —
  xem [[reference-fake-a6sf-modbus]]). Trước đó 627 sau khi bước xác định hand-eye chuyển sang cơ cấu kẹp,
  và nút 5 / nút trả tự xoay A về góc lúc gắp. Hai bài jog-A đã qua phép thử đột biến: gỡ bản sửa thì cả hai đổ.
  `ForcedObjPose` trả góc con can cố định bất kể A, nên bài nào về góc A phải chụp encoder trong hook `AfterFlow`/`AfterSetDo`,
  không được suy từ mốc góc.
  Ba điểm dạy của cơ cấu kẹp trong `CreateSimulation` (gắp #1 240/100 · gắp #2 205/100.7 · soi 282/118)
  **cố ý không trùng** ô tray nào, mốc CAM nào hay `AlignPosX` nào, và hai điểm gắp cố ý lệch 35 mm chứ
  không phải `pitchX` — đổi số là làm bài kiểm đạt rỗng. Tray mô phỏng cố ý nhỏ: lắp ráp 3×4, đầu vào 2×5 — đừng phình lại.
  Đầu vào để **5 cột, số LẺ, là cố ý**: cột chẵn thì cặp ô hút luôn nằm gọn trong một hàng, nhánh cặp-vắt-hàng của
  `AssignPickCell` không bao giờ chạy và bài kiểm đạt rỗng. Tương tự `AssyTray.ScanOrder = Serpentine`. Góc hút mô phỏng cố ý khác 0 (A1 3.5°, A2 356.5°) để bài kiểm không đạt rỗng — đừng đặt về 0.
- `SelfTest.Run()` đặt `SysData.LampSettleMs = 0` ngay đầu: chờ đèn CAM sáng (mặc định 100 ms) × hàng trăm lần kích Flow là thời gian chết thuần. Bài đèn CAM trong `TestVmCalib` tự đặt 300 ms rồi đo cận dưới — đừng bỏ dòng tắt đó đi.
- Đã sửa 13/09 (Tuấn duyệt): STOP lúc chờ đổi tray trước đây để máy kẹt ở WaitOperator; giờ StartAuto đưa về Paused. Bài TestTrayChangeover có 4 check cho tình huống này.
