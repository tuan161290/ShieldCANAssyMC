---
name: name-quantities-by-meaning
description: "15/09 Tuấn sửa \"số quét / các số đã nhớ\" thành \"vị trí vật lý\" — gọi đại lượng theo nghĩa của nó, không gọi chung là \"số\""
metadata: 
  node_type: memory
  type: feedback
  originSessionId: e083c7ae-0deb-4155-be33-3b7bec1d9c4b
  modified: 2026-09-15T09:00:43.930Z
---

Trong cẩm nang, thông báo App và chat, gọi đại lượng theo đúng nghĩa vật lý của nó, không gọi chung là "số" ("số quét", "ba số", "các số đã nhớ"). Ví dụ `TopX_mm / TopY_mm / TopA_deg` là **vị trí vật lý của chi tiết đích trong ô** do camera đo (mm tính từ dấu chuẩn vì K ≈ −318, góc tính bằng độ). Nó khác **vị trí teach của ô** (lưới tray), thứ App dùng làm hàng rào lưới.

**Why:** 15/09 Tuấn không hiểu câu "quét lại nếu số quét của ô đích bị mất" trong cẩm nang, rồi chỉ ra "gọi là số là không đúng, phải là vị trí vật lý của Cell mới sát nghĩa". Cùng tinh thần với yêu cầu thêm đuôi đơn vị vào tên biến.

**How to apply:** trước khi viết chữ "số" cho một đại lượng, xác định nó là vị trí, góc, độ lệch hay ngưỡng rồi gọi đúng tên đó. Liên quan [[app-actuator-vm-compensation]], [[name-steps-not-numbers]].
