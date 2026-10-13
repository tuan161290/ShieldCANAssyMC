---
name: reference-vm44-create-global-var
description: VM 4.4 SDK tạo được biến toàn cục qua VM.ExtensionMethod.GlobalVariableHelper.AddVar — không có trong tài liệu, đã tích hợp vào trang Setting
metadata:
  type: reference
---

Tài liệu SDK .NET/C++ của VisionMaster 4.4 (Development/V4.x/Documentations) CHỈ có đọc/ghi giá trị
biến toàn cục (SetVarFloat, GetAllGlobalVar...). Hàm tạo/xoá/đổi tên biến có thật nhưng không ghi:
`VM.ExtensionMethod.GlobalVariableHelper.AddVar(name, type, value, remark)`, `DeleteVar`, `ReNameVar`,
nằm ở `Development/V4.x/ComControls/Assembly/VM.ExtensionMethod.dll` (cửa sổ Global Variable của VM
dùng đúng đường này). Lưu solution: `VmSolution.Save()`.

- Kiểu tạo được: int, float, string. Tên ≤ 125 byte, cấm `? ; . / \ ` { } [ ] < > ( )`.
- AddVar đọc số theo culture Windows → tạo với "0" rồi ghi giá trị bằng SetVarFloat/SetVarInt.
- Dịch ngược bằng `~/.dotnet/tools/ilspycmd.exe` (đã cài); ildasm cũng có trong Windows SDK.
- App tích hợp 2026-09-13: GlobalVarSync.cs + nút trang Setting; CHƯA chạy trên VM thật.

Liên quan [[project-vm44-capabilities-verified]], [[project-app-actuator-vm-compensation]].
