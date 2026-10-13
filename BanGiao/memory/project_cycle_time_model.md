---
name: project-cycle-time-model
description: "Mô hình thời gian chu trình đã hiệu chuẩn từ log máy (24/09): mốc 739 s cho một tray lắp ráp 5x8; X và Z chiếm 86%; thanh ghi ramp bị chặn ở 10000 nên chặng Z chạy chậm bỏ qua ô AccelTime"
metadata: 
  node_type: memory
  type: project
  originSessionId: e083c7ae-0deb-4155-be33-3b7bec1d9c4b
  modified: 2026-09-25T09:34:53.785Z
---

Script ở `scratchpad/cycle_model.py` + `cycle_variants2.py` — mô phỏng đúng trình tự
`Routine_Auto`, đọc thẳng `DEFAULT.json` và `system.json` trên `\\1.2.1.218`.

**Mốc 24/09 (ĐÃ BÁC): mô hình cho 739 s, nhưng đo thật chỉ ~452 s ≈ 7.5 phút** — mô hình sai vì lấy
X = 59 mm/s. Số đo: nhịp chạy rà 17.98 s (không khí, không vision), trong đó **Z 12.68 s = 70%**, X/Y 5.3 s.
Cộng khí + vision + quét ⇒ ~452 s/tray, ~11.3 s/con. **Z mới là chỗ tốn, không phải X.**
X 48.9% · Z 37.3% · quét 5.0% · khí 4.4% · vision 4.3%.

**Hiệu chuẩn từ `app.log` 24/09** (máy chưa từng chạy trọn chu trình hút–đặt, nên phần Z là suy từ
cấu hình, chỉ có X/Y/A là đo được):
- bước quét 25 mm + kích Flow = 1.032 s; đổi hàng zigzag (Y2 35 mm) = 1.250 s
- 5 ảnh liên tiếp cùng vị trí = 235 ms/lần kích Flow
- lưới N-point 3×3 bước 5 mm; AlignCalib bước X 1 mm = 0.147 s, 2 mm = 0.217 s (tam giác, a≈180)
- ~~X chạy đúng cái được đặt~~ **SAI — đã bác bằng số đo 24/09 tối**. Chạy rà ghi 4 chặng X quãng khác
  nhau: X **cruise ~294 mm/s** trong khi Setting ghi 59. Tỷ số **4.99 ≈ 5** = `UnitPerRev` để 10 trong
  khi bước thật ~50 mm/vòng. Cận dưới không cần giả định nào: 356 mm trong 1547 ms ⇒ ít nhất 230 mm/s.
  **ĐỪNG khuyên nâng X MoveSpeed** — đặt 150 là ra lệnh 900 rpm ≈ 750 mm/s thật.
- **Y1/Y2 KHÔNG**: đặt 500 mm/s mà đo ra ~69 mm/s, gia tốc ~159. Chưa giải thích được; slot tốc độ
  và slot acc/dec đều đã loại trừ (Tuấn kiểm PANATERM 24/09). Y hiếm khi chặn chu kỳ nên để đó.

**Ramp — ĐỔI 25/09, số đo trước đó mang ramp cũ, đừng trộn:** trước 25/09 `ServoService.MoveAbsolute`
nhân `reg = AccelMs × 3000/rpm` theo tốc độ TỪNG lệnh, `ClampRampRegister` cắt ở 10000 → chặng Z chậm
ramp 120–200 ms bất kể AccelTime. Từ 25/09 (`ServoAxisConfig.AbsoluteRampRpm` = rpm của Auto cao) độ dốc
cố định: AccelTime là ramp ở Auto cao, chặng 6 mm/s chỉ ramp ~6 ms — để thư viện khỏi ghi lại acc/dec
trước mọi lệnh (Tuấn chê độ trễ). Giờ hạ AccelTime chỉ còn tác dụng lên chặng nhanh.

**Thứ tự chu trình đã tối ưu, đừng đụng nữa** (đo bằng mô hình, 24/09): soi tay #2 trước tay #1 là
đúng (đảo thì +43 s); zigzag tray lắp ráp +12 s nếu bỏ; giao ô đặt theo hình học +7 s nếu bỏ; thứ tự
đặt và thứ tự hút đều ±0. Còn nhặt được: giao ô HÚT theo hình học (−8.5 s).

**Đòn bẩy (tính theo mô hình 739 s ĐÃ BÁC — chỉ giữ hướng, đừng trích số):** ~~X 59→150~~ (X thật đã
nhanh ~5×) · teach điểm giảm tốc Z gần cao độ hút/đặt hơn · Z MoveSpeedLow cao hơn · (ramp chặng chậm
đã xử lý 25/09, xem trên).
Phương án cho Z chạy song song X đã bị bác — xem [[feedback_z_safe_interlock_not_negotiable]].
