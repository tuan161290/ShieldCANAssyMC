---
name: reference-group-cmd-trace-signatures
description: "Đọc dòng vết AlignBottom trong app.log để chỉ đúng biến nào không tới được Group Cmd, thay vì đoán cơ khí"
metadata: 
  node_type: memory
  type: reference
  originSessionId: e083c7ae-0deb-4155-be33-3b7bec1d9c4b
  modified: 2026-09-23T01:10:08.404Z
---

Dòng `VmCycle.TraceAlignBottom` in cả "App ghi" (7 biến) lẫn "Flow trả" (Diag*), nên so tay được:

- `DiagTarX_mm` đúng bằng `CellX_mm` (không phải `CellX_mm + HandEyeDx_mm`) → Group Cmd **không cộng hand-eye**.
- `DiagDA_deg` đúng bằng −`ObjA_deg` → góc đích trong Single Point Alignment đọc ra **0**, tức `CellA_deg`
  không tới được module.
- `Status` = 1 → `HasTarget` tới nơi; `DiagDA` khác 0 → `AlignSign` tới nơi.

23/09: đúng ba biến `HandEyeDx_mm`, `HandEyeDy_mm`, `CellA_deg` không tới Group Cmd của Flow bottom-up tay #2 →
góc bù ra ≈ 180° (RunId 5959174/5959175, log 08:04–08:05). **Trước đó tôi đoán nhầm là gốc trục A nhảy nửa vòng
sau Origin — sai; vết log bác bỏ, cùng triệu chứng 179.81° hôm 22/09 17:22 cũng là lỗi này.**

**Why:** góc bù ≈ 180° trông giống lỗi cơ khí nhưng thường là một input rỗng trong solution.
**How to apply:** gặp góc bù lớn, mở `[[reference-machine-app-log]]` đọc dòng vết trước, đừng đoán cơ khí.
Liên quan [[project-app-actuator-vm-compensation]].
