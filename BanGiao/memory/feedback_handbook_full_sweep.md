---
name: handbook-full-sweep
description: "Đổi thiết kế thì rà HẾT cẩm nang đối chiếu code (grep tên cũ, trích nguyên văn thông báo App), không chỉ các mục vừa viết lại"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: e083c7ae-0deb-4155-be33-3b7bec1d9c4b
  modified: 2026-09-15T07:27:12.768Z
---

Sau khi đổi thiết kế (ví dụ phương án B 14/09), phải rà toàn bộ cẩm nang calib đối chiếu với code, không dừng ở các mục chính vừa viết lại. Ngày 15/09 Tuấn chụp cẩm nang mới nhất vẫn còn dòng `PlacementSolver.BuildTarget` ("App dựng đích") và nói: "có vẻ nhiều chỗ bạn chưa cập nhật đầy đủ". Lần rà đó tìm ra khoảng 80 chỗ cũ. Trong số đó có: ErrDistance, "nhớ bấm Lưu model" (App đã tự lưu), kỳ vọng −5 mm của kiểm tra top-down (code so TopX đứng yên), điều kiện Auto thiếu kiểm tra top-down, và bù đặt chưa có mục nào. Cùng lúc tìm ra câu cũ ngay trong App ("Flow AlignVerify đã ghi dấu góc vào solution").

**Why:** Tuấn cầm cẩm nang ra máy làm theo từng chữ. Một câu sai về "ai tính gì" hay "nhớ bấm Lưu" là thao tác sai trên máy thật.

**How to apply:** khi đổi hợp đồng App↔VM hay luồng hiệu chuẩn:
- grep cẩm nang theo tên hàm, biến, Flow cũ và theo các cụm "App dựng", "App tự", "Flow báo".
- trích thông báo lỗi và kết quả đúng nguyên văn chuỗi trong ViewModel/Routine (đọc code, đừng nhớ).
- kiểm cả mục lý thuyết, bảng "Lý thuyết nằm trong tool nào", ví dụ số, checklist cuối và dữ liệu JS của sơ đồ Flow.

Liên quan [[name-steps-not-numbers]], [[scope-discipline]].
