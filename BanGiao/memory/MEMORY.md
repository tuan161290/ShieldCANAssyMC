# Memory Index

- [Bản chính ở PGM](project_master_copy_pgm.md) — từ 26/09 chỉ làm trên máy lắp ráp 1.2.1.218, C:\Users\Public\PGM; bản OneDrive máy dev đứng yên

- [Don't self-screenshot](feedback_no_self_screenshot.md) — ask user for screenshots instead of PowerShell window capture, it doesn't work in this sandbox
- [Vẽ trang WPF ra PNG](reference_offscreen_render_harness.md) — chương trình phụ RenderTargetBitmap ở khổ 1280×976 chạy được; phải tắt vServerApp con sau mỗi lượt, không thì khoá exe trong bin
- [Scope discipline](feedback_scope_discipline.md) — "update X to match Y" means the literal narrow thing, don't cascade into related PLAN.md sections unasked
- [Chạy self-test](project_selftest_how_to_run.md) — máy dev để SimulationMode=true là đúng (không động cơ), đừng lật về false; ~400 s, timeout 600 s; chậm ×25 là do Lightroom giữ kênh debug output
- [App là cơ cấu chấp hành](project_app_actuator_vm_compensation.md) — 14/09 phương án B: 7 Flow, 12 biến App ghi hết, Group Cmd trong AlignBottom_ToolN; 22/09 mốc góc thay Create Reference, Z dừng đúng Z_SAFE
- [VM 4.4 đã kiểm trên máy](project_vm44_capabilities_verified.md) — biến toàn cục đọc/ghi được trong Flow, sin theo độ, không có mảng, có C# Script
- [Build ghi lên máy thật](feedback_build_output_remote_share.md) — Core.csproj trỏ BaseOutputPath tới \\1.2.1.218, build cục bộ phải ghi đè, deploy phải hỏi trước
- [Sự cố còn hàng trên tay hút](feedback_safety_remove_product.md) — chưa nhả thì quét lại + soi lại rồi đặt; đã nhả/không trả được thì bắt lấy hàng ra; mở lại app phải Origin
- [Tạo biến toàn cục qua SDK](reference_vm44_create_global_var.md) — GlobalVariableHelper.AddVar (không có trong tài liệu), đã gắn vào trang Setting, chưa thử máy thật
- [Solution VM là .solw](project_vm_solution_solw.md) — đuôi file solution là .solw, không phải .sol; PLAN.md và bản thiết kế còn ghi .sol
- [Làm đơn giản](feedback_keep_simple.md) — không thêm cơ chế phòng xa cần cấu hình, trả lời ngắn; Tuấn chê overthinking 13/09
- [Gọi bước bằng tên](feedback_name_steps_not_numbers.md) — "bước xác định hand-eye", không "Bước 5"; chat, cẩm nang, App đều theo tên (15/09)
- [Rà hết cẩm nang khi đổi thiết kế](feedback_handbook_full_sweep.md) — 15/09 Tuấn thấy bản mới nhất còn PlacementSolver; grep tên cũ, trích nguyên văn thông báo App
- [Gọi đại lượng theo nghĩa](feedback_name_quantities_by_meaning.md) — không gọi chung là "số"; TopX_mm… là vị trí vật lý của chi tiết đích trong ô (15/09)
- [Log App trên máy thật](reference_machine_app_log.md) — \\1.2.1.218\Users\Public\App\_RunData\Log\<ngày>\app.log đọc được từ máy dev; xem số đo thay vì đoán từ ảnh
- [Không chạy máy từ màn hình cảm ứng](project_no_touchscreen_start.md) — luật 20/08, mọi lần chạy phải qua nút Start cứng; nút mềm chỉ được chọn chế độ
- [Xét cả mặt cơ khí, không chỉ toán](feedback_check_mechanics_not_just_math.md) — "có cần không" phải trả lời cả vế cơ cấu; 23/09 quên gờ đầu hút nên trả lời hụt chuyện A về 0
- [Đọc vết AlignBottom để bắt lỗi Group Cmd](reference_group_cmd_trace_signatures.md) — DiagTar = Cell → thiếu hand-eye; DiagDA = −ObjA → góc đích đọc ra 0 (23/09)
- [Không nới chốt Z_SAFE](feedback_z_safe_interlock_not_negotiable.md) — X/Y chỉ chạy khi cả hai Z ≤ Z_SAFE+1mm; 24/09 Tuấn bác phương án cho Z chạy song song X để rút chu kỳ
- [Driver A6SF giả](reference_fake_a6sf_modbus.md) — kiểm từng khung Modbus không cần máy; ServoControl dùng chung với HousingCorrosion, mặc định giữ hành vi cũ
- [Mô hình thời gian chu trình](project_cycle_time_model.md) — đo thật ~452 s/tray, Z chiếm 70%; X thật nhanh ~5× số Setting; từ 25/09 ramp acc/dec cố định theo Auto cao (chặng chậm ~6 ms)
