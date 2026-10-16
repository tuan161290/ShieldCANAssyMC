---
name: project-no-touchscreen-start
description: Luật 20/08 — không có đường nào từ màn hình cảm ứng làm máy chạy; mọi lần chạy phải qua nút Start cứng
metadata: 
  node_type: memory
  type: project
  originSessionId: e083c7ae-0deb-4155-be33-3b7bec1d9c4b
  modified: 2026-10-16T00:00:00.000Z
---

Máy chạy thật **chỉ** nhận từ **nút Start cứng** ngoài jig — bốn nút DI14–DI17 như nhau từ 2026-10-16
(trước đó Cặp B hai tay, rồi Start 1/Start 2 mỗi nút một tray). Không nút mềm nào trên
màn hình được phép làm trục chạy — nút RUN/IDLE to ở trang Main cũng chỉ đưa vào Armed, và
banner "đã đặt tray xong?" đã bị gỡ Command vì lý do này. Luật đặt 2026-08-20, ghi trong comment ở
`MainView.xaml` và `MainViewModel`.

**Why:** đây là lớp an toàn duy nhất giữ tay người ra khỏi vùng chạy. Một nút bấm phát là chạy trên
màn hình cảm ứng vô hiệu hoá nó, và kiểu chạy nguy hiểm nhất — bật rồi bỏ đi — lại chính là kiểu
người ta dùng nút đó.

**Cái bẫy khi sửa code khởi động chu trình:** `McControl.DoStart` (qua `RequestStart`) **chỉ còn
SelfTest.cs dùng** — doc-comment của nó nói thẳng thế. Đường chạy thật là `RequestArm` → `RequestGo`
→ **`DoGo`**. 24/09 tôi gắn nhánh chạy rà vào riêng `DoStart`, self-test đạt hết, nhưng trên máy bật
chạy rà rồi bấm Cặp B vẫn chạy chu trình thật và vẫn kích Vision. Sửa bằng cách gom về một hàm
`StartCycle()` mà cả hai đường cùng gọi. **Kịch bản self-test nào dùng `RequestStart` thì không phủ
được đường vận hành thật** — muốn phủ phải bấm nút Start qua `FakeIo.SimPress` hoặc gọi `RequestGo()`.

**How to apply:** trước khi thêm BẤT KỲ nút nào có thể làm trục chuyển động, kiểm tra luật này. Nút
mới chỉ được *chọn chế độ* hoặc *đặt tham số*, còn lệnh chạy vẫn đi qua nút Start cứng. 23/09 Tuấn xin "nút
Toggle ở trang Main" cho chế độ chạy rà; tôi nêu xung đột, Tuấn chọn phương án giữ luật — toggle chỉ
gạt chế độ, chạy vẫn bấm nút cứng. Xem [[project-app-actuator-vm-compensation]].
