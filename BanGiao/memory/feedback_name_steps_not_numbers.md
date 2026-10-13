---
name: name-steps-not-numbers
description: "Gọi bước hiệu chuẩn và nút chạy kiểm bằng tên (bước xác định hand-eye), không bằng số — trong chat, cẩm nang và chữ trên App"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: e083c7ae-0deb-4155-be33-3b7bec1d9c4b
  modified: 2026-09-15T02:29:08.165Z
---

Tuấn không nhớ số thứ tự các bước. Gọi bước theo việc nó làm, ví dụ "bước xác định hand-eye", không nói "Bước 5". Số chỉ được đứng cạnh tên để chỉ thứ tự, không bao giờ đứng một mình.

Tên đã chốt 15/09/2026, cẩm nang v14 và App đều đã đổi theo:
- hiệu chuẩn top-down (lưới 9 điểm)
- kiểm tra top-down
- hiệu chuẩn bottom-up (Align từng tay hút)
- phép thử vòng xoay (đo dấu góc) — trùng tên nút trên App
- xác định hand-eye
- teach lưới tray

Sáu nút chạy kiểm trên trang Vision Edit gọi theo nhãn nút: CAM tới ô lắp ráp · Trigger LocateTop · Hút hàng · Tới vị trí soi CAM dưới · Trigger AlignBottom · Tới điểm đặt.

**Why:** Tuấn nói 15/09: "tôi sẽ không nhớ được bước 5 làm gì, hãy nói là bước xác định handeye thì tôi mới biết", rồi bảo đổi luôn cả cẩm nang lẫn App: "gọi tên chứ số tôi không nhớ được đâu".

**How to apply:** áp dụng cho chat, cẩm nang, thông báo, tooltip, nhãn trên App và tên mục self-test. Chú thích trong code (cho người lập trình) vẫn còn "Bước N" và được phép giữ. Ô tròn đánh số đầu mỗi bước trong cẩm nang và số "1 ·" trên nút chạy kiểm được giữ vì chúng chỉ thứ tự.
