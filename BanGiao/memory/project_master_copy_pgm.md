---
name: project-master-copy-pgm
description: "Từ 26/09 bản chính của dự án là C:\\Users\\Public\\PGM trên máy lắp ráp 1.2.1.218; bản OneDrive trên máy dev 1.2.1.225 đứng yên, Tuấn không làm ở đó nữa"
metadata:
  type: project
---

Tuấn chốt 2026-09-26: chỉ làm việc trên máy lắp ráp `1.2.1.218`. Bản chính của dự án là
`C:\Users\Public\PGM\2026\Actro\SO1C86-SHIELD CAN ASSY MC\ShieldCANAssyMC`, thư viện ở
`C:\Users\Public\PGM\Tuan\Projects\ServoControl` (giữ bố cục để ProjectReference tương đối còn đúng).

Bản OneDrive trên máy dev cũ `1.2.1.225` đứng yên từ 26/09 — không chép gì từ đó về đè lên PGM.

**Why:** hai bản không đồng bộ với nhau; phải có đúng một bản chính.
**How to apply:** mọi sửa code và tài liệu làm trong PGM. Máy này có động cơ và App sản xuất — luật build
và self-test ở `BanGiao/BAN-GIAO.md` §2 (build sai cờ là ghi đè App đang chạy). Xem [[feedback-build-output-remote-share]].
