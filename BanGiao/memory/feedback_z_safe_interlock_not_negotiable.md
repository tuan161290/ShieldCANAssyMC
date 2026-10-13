---
name: feedback-z-safe-interlock-not-negotiable
description: "Không được cho X/Y chạy khi Z chưa ở Z_SAFE — chốt chặn chống va là bất khả xâm phạm, kể cả để rút thời gian chu kỳ (Tuấn bác 2026-09-24)"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: e083c7ae-0deb-4155-be33-3b7bec1d9c4b
  modified: 2026-09-24T10:17:05.197Z
---

Chốt chặn chống va — **X, Y1, Y2 chỉ được chạy khi CẢ HAI trục Z ≤ Z_SAFE + 1 mm**
(`McFunction.CanMove` → `MotionConnect.ZSafeOk`) — **không được nới ra vì bất kỳ lý do tối ưu nào**.

24/09 tôi đề xuất teach thêm "cao độ thoát hốc tray" rồi cho Z nâng/hạ SONG SONG với X trong đoạn
giữa cao độ đó và Z_SAFE, ước rút 35–53 s mỗi tray. Tuấn bác thẳng: *"Không được gộp X và Z di
chuyển đồng thời khi Z chưa ở Z Safe."*

**Why:** đầu hút cộng sản phẩm treo dưới nó quét ngang qua thành hốc tray và qua thân camera
bottom-up nằm giữa đường (X 291–355). Một lần va là gãy đầu hút hoặc vỡ camera; khoản vài chục giây
mỗi tray không mua nổi rủi ro đó. Chốt chặn hiện đọc vị trí encoder THẬT chứ không tin lệnh nâng Z
trước đó đã trả về thành công — cố ý như vậy.

**How to apply:** khi tính tối ưu thời gian chu kỳ, coi "Z phải về Z_SAFE xong rồi X/Y mới đi" là
ràng buộc cứng của bài toán, đừng đưa vào danh sách đòn bẩy nữa. Các đòn bẩy còn hợp lệ nằm ở tốc
độ/gia tốc từng trục và ở điểm giảm tốc teach được — xem [[project_cycle_time_model]].
Liên quan: [[feedback_check_mechanics_not_just_math]], [[feedback_keep_simple]].
