---
name: feedback-build-output-remote-share
description: Build nào không ghi đè BaseOutputPath cũng chép thẳng lên máy thật \\1.2.1.218 — phải hỏi Tuấn trước khi deploy
metadata:
  type: feedback
---

`ShieldCanAssy.Core.csproj` đặt `<BaseOutputPath>\\1.2.1.218\Users\Public\App</BaseOutputPath>`
**không điều kiện** (không chỉ cấu hình Remote); Wpf cấu hình Remote đặt OutputPath tuyệt đối
mà `-p:BaseOutputPath` không ghi đè được. Build kiểm tra cục bộ luôn dùng
`-c Debug -p:BaseOutputPath='bin\'`.

**Why:** thư mục đó là app đang chạy trên máy lắp ráp thật; một lần build nhầm là đã deploy.
**How to apply:** muốn đưa bản sửa lên máy (build Remote hoặc build không ghi đè đường dẫn) thì
hỏi Tuấn trước, mỗi lần. Xem thêm [[project-selftest-how-to-run]].
