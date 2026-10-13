---
name: reference-fake-a6sf-modbus
description: "Kiểm lệnh Modbus xuống driver Panasonic không cần máy: FakeA6sfPort + TestServoModbusWrites; ServoControl là thư viện dùng chung với HousingCorrosion nên mặc định phải giữ hành vi cũ"
metadata:
  node_type: memory
  type: reference
  originSessionId: e083c7ae-0deb-4155-be33-3b7bec1d9c4b
  modified: 2026-09-26T01:31:10.762Z
---

`ServoControl` (c:\Users\Tuan\OneDrive\Tuan\Tuan\Projects\ServoControl) **dùng chung với dự án
Ama_Actro_HousingCorrosion** (ghi trong ServoControl.csproj). Sửa thư viện thì hành vi MẶC ĐỊNH phải giữ
nguyên, cái mới bật bằng cấu hình — như `ServoAxisConfig.AbsoluteRampRpm` (0 = cách cũ) ngày 25/09.

Kiểm không cần cổng COM: `ServoService.Connect(config, IModbusRtuSerialPort)` + `ShieldCanAssy.Wpf/FakeA6sfPort.cs`
(driver giả nói Modbus RTU, ghi từng giao dịch, chụp bảng thanh ghi ở cạnh lên STB). Bài
`SelfTest.TestServoModbusWrites`. Gọi ServoService từ self-test phải qua `Task.Run` — nó không
ConfigureAwait, gọi chặn trên luồng UI là treo.

Chạy riêng một bài: `scratchpad/modbustest` (gọi hàm private bằng reflection, vài giây). **Bẫy:** gài lỗi
vào ServoControl.dll rồi trỏ AssemblyResolve tới bản đó là VÔ ÍCH — `LoadFrom` lấy DLL cạnh
ShieldCanAssy.Wpf.exe trước; phải chép bin ra bản sao rồi thay DLL trong đó (`mutate_modbus.py`).
Lần đầu quên, báo "0/6 bị bắt" tưởng bài kiểm rỗng.

Đo độ trễ đầu-cuối (26/09, `scratchpad/polltest`): vòng đọc thật + driver giả tốn TxMs/giao dịch, 200 lệnh.
Nhịp đọc 50→10 ms (PanasonicAxis): trễ TB/lệnh 72→52 ms (Tx 4) và 198→173 ms (Tx 18). Tx mới là đòn bẩy
lớn (~120 ms/lệnh) — hỏi bộ chuyển USB-RS485 trên máy là chip gì, FTDI thì Latency Timer 16→1.
`Task.Delay` ở process không gọi timeBeginPeriod làm tròn lên bội 15.6 ms (Delay(10)≈15.6, Delay(50)≈63).
