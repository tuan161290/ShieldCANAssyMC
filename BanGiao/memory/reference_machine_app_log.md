---
name: reference-machine-app-log
description: Log của App trên máy thật đọc được từ máy dev qua share \\1.2.1.218 — dùng để xem số đo khi Tuấn chỉ gửi ảnh bảng báo lỗi
metadata: 
  node_type: memory
  type: reference
  originSessionId: e083c7ae-0deb-4155-be33-3b7bec1d9c4b
  modified: 2026-09-22T08:22:02.616Z
---

Log App trên máy thật: `\\1.2.1.218\Users\Public\App\_RunData\Log\<yyyyMMdd>\app.log` (TSV: giờ, mức, nguồn, nội dung).
Máy đó không có `D:\ShieldCanAssy` nên App dùng thư mục dự phòng `_RunData` cạnh exe — config/model/log đều ở đó.
Chỉ đọc, không ghi (xem [[feedback-build-output-remote-share]]).

22/09 dùng nó chẩn đoán phép thử vòng xoay tay #2 (sai số dư 0.065 mm, Z_SAFE đổi 20 → 0 sau khi hiệu chuẩn).
Từ 22/09 các bước hiệu chuẩn ghi đủ số đo (từng điểm nạp, trung bình + dao động mỗi lần chụp, Z lúc chụp) và mọi
hộp thoại người vận hành thấy đều vào log với nguồn "UI".
