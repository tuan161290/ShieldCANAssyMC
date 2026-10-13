---
name: feedback-safety-remove-product
description: Hàng trên tay hút gặp sự cố — chưa nhả thì quét lại tray + soi lại rồi đặt; đã nhả/không trả được thì bắt lấy hàng ra
metadata:
  type: feedback
---

Quy tắc Tuấn chốt 2026-09-13 cho sự cố lúc tay hút còn sản phẩm:
- Hết ô đặt mà tay còn hàng: giữ hàng trên tay, chỉ báo Warning, chờ đổi tray lắp ráp. Đổi xong luôn
  chạy lại từ S00: quét tray lắp ráp mới, tay hút ra CAM bottom-up soi lại, rồi đặt ô mới. Cùng cách
  này cho nạp lại tray lắp ráp và bước đặt dừng giữa chừng TRƯỚC khi nhả hút.
- Sản phẩm có thể đã hỏng hoặc không trả về được (dừng SAU khi đã nhả hút, trả về không nhả được,
  phải trả về mà không rõ ô nguồn): bắt người lấy hàng ra trước thao tác kế tiếp (cờ
  RunningData.NozzleClearRequired chặn ARM/GO/START/Origin tới khi cảm biến chân không báo trống).
- Mở lại app thì không chạy tiếp, phải Origin lại hết.

**Why:** an toàn trước, nhưng không bắt người thao tác thừa khi sản phẩm còn nguyên trên tay — soi lại
ở bottom-up là đủ để đặt đúng.
**How to apply:** nhánh lỗi mới có sản phẩm trên tay: nếu chưa nhả thì chạy lại từ S00 (quét + soi
lại); nếu đã nhả/thổi hoặc không xác định được thì bắt lấy hàng ra. Tuấn đổi ý trong cùng ngày
(ban đầu muốn trả về ô nguồn, rồi bắt lấy ra) — hỏi lại khi gặp trường hợp chưa có trong danh sách.
