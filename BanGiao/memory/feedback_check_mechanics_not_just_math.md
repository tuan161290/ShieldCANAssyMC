---
name: feedback-check-mechanics-not-just-math
description: "Hỏi \"cái này có cần không\" thì phải xét cả mặt CƠ KHÍ, không chỉ chứng minh công thức đúng"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: e083c7ae-0deb-4155-be33-3b7bec1d9c4b
  modified: 2026-09-23T09:57:34.253Z
---

Tuấn hỏi "bước xác định hand-eye có đưa trục A về 0 không?" — tôi trả lời "không, và không cần",
kèm chứng minh mốc góc không phụ thuộc A. Toán đúng, nhưng trả lời sai trọng tâm: lý do thật là
**đầu hút có gờ ôm lấy sản phẩm**, hạ xuống hút trong lúc A còn ở góc xoay của lệnh đặt trước đó
thì gờ không ăn khớp, hở khí, chân không không đạt và không nhấc được hàng lên (Tuấn nêu 2026-09-23).

**Why:** tôi chỉ hỏi "con số có sai không" mà không hỏi "cơ cấu có làm được không". Máy này có gờ,
hốc tray, lực hút — những ràng buộc không nằm trong công thức nào và không suy ra được từ code.
Cùng dạng với chuyện "tool hút có gờ nên chi tiết không xê dịch" mà Tuấn phải nói tôi mới biết.

**How to apply:** câu hỏi dạng "có cần / có ảnh hưởng không" thì trả lời hai vế — (1) con số có đổi
không, (2) cơ cấu có va, có kẹt, có hở khí, có với tới không. Vế (2) mà không chắc thì hỏi Tuấn
thẳng, đừng im lặng cho qua. Xem [[project-app-actuator-vm-compensation]].
