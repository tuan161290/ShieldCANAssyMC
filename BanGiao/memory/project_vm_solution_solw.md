---
name: vm-solution-extension-solw
description: File solution VisionMaster của máy này có đuôi .solw, không phải .sol
metadata:
  type: project
---

Solution VisionMaster 4.4 của máy dùng đuôi **.solw** (Tuấn nhắc ngày 2026-09-13). Không viết ".sol" trong tài liệu, nhãn UI hay bộ lọc chọn file.

**Why:** đã viết nhầm .sol khắp cẩm nang, trang Setting và bộ lọc nút Duyệt — bộ lọc sai làm hộp chọn file ẩn mất file solution thật.

**How to apply:** nhắc tới file solution, đường dẫn mặc định hay filter OpenFileDialog thì dùng .solw. Ngày 2026-09-13 đã sửa cẩm nang v13 và chuỗi trong code; PLAN.md và bản thiết kế "Kiến Trúc App Chấp Hành" vẫn còn ghi .sol. Liên quan [[reference-vm44-create-global-var]].
