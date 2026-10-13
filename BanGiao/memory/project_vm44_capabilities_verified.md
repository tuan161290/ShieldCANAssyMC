---
name: vm44-capabilities-verified
description: "Hành vi VisionMaster 4.4.0 Tuấn kiểm trực tiếp trên máy 13/09/2026 — biến toàn cục, Variable Calculation, mảng, Script"
metadata: 
  node_type: memory
  type: project
  originSessionId: e083c7ae-0deb-4155-be33-3b7bec1d9c4b
  modified: 2026-09-13T03:22:24.357Z
---

Tuấn kiểm trên VisionMaster 4.4.0 của máy thật, 2026-09-13:

1. Variable Calculation trong một Flow chạy thường (không phải Flow hiệu chuẩn) đọc Global Variable và ra **giá trị mới ngay trong lần chạy đó**.
2. Hàm lượng giác dùng **độ**: `sin(30) = 0.5`.
3. Flow **ghi được** giá trị vào Global Variable, và giá trị còn nguyên sau khi tắt/mở lại app.
4. Global Variable **không có kiểu mảng**.
5. Ô input của Single Point Alignment **subscribe được** output của Variable Calculation.
6. Tách PlaceAlign thành hai Flow (mỗi tay một Flow) là chấp nhận được.
7. Có module **C# Script**.

**Why:** thư mục `reference/vm_help` không có trang Help cho các module tính toán này — đây là bằng chứng duy nhất, đừng suy ngược hay nghi lại khi chưa có bằng chứng mới.

**How to apply:** dùng khi thiết kế logic phía VM cho [[app-actuator-vm-compensation]]. Không có mảng nên dữ liệu theo từng ô (kết quả quét trước) phải do App giữ nguyên vẹn rồi gửi lại, hoặc dùng biến vô hướng riêng. Chưa rõ: giá trị ghi từ Flow có cần Ctrl+S mới vào file .sol không.
