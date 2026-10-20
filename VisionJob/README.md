# Job VisionMaster — ShieldCanAssy

Job KHÔNG nằm trong git: file `.solw` khoảng 120 MB, mà git giữ mọi bản đã commit nên mỗi lần sửa job là repo lớn
thêm chừng đó. Bản lưu chung ở thư viện Seafile, cạnh các repo:

`Shared with me\Actro\ShieldCANAssyMC.VisionJob\`

Sửa job xong thì chép ĐÈ lên bản cũ ở đó; lịch sử phiên bản do Seafile giữ (Tuấn 2026-10-20).

- `ShieldCanAssyyJob.solw` — solution VisionMaster 4.4: các Flow `LocateTop`, `AlignBottom_Tool1`, `AlignBottom_Tool2` và
  module Global Variable.
- `Calib\` — file hiệu chuẩn đi kèm job: N-point CAM top-down (`TOP_CAM_NPoint.xml`), CAM bottom-up của từng tay
  (`Tool1_CamBTM.xml`, `Tool2_CamBTM.xml`).

Máy chạy job ở `C:\Users\Hoyeon\Desktop\Job\` (đường dẫn solution trong Setting, `system.json`). App (WPF lẫn WinUI)
không đọc thư mục Seafile.
