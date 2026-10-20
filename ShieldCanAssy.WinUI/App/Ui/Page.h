#pragma once
// Hợp đồng giữa MainWindow và các trang.
//
// Giao diện bản này KHÔNG dùng binding tới view-model như bản WPF: lõi máy là C++ thuần (không có
// INotifyPropertyChanged), nên mỗi trang tự đọc trạng thái từ Core và đẩy lên control của mình trong
// OnTick() — MainWindow gọi 10 lần/giây, CHỈ cho trang đang hiện. Lệnh của người dùng đi qua
// ui::RunBackground vì mọi thao tác dài của Core là hàm chặn.
//
// Thứ tự gọi (luồng UI):
//   1. Trang được dựng từ MainWindow.xaml; Loaded của trang có thể chạy TRƯỚC khi McControl::Init xong (Init chạy nền,
//      xem AppHost::CoreReady). Trong Loaded chỉ dựng control và đọc dữ liệu tĩnh (McDataManager, AlarmManager, Loc).
//   2. OnCoreReady() — một lần cho MỌI trang, ngay khi McControl::Init xong. Từ đây mới được đụng McControl::Io()/
//      Vision()/Func()/Safety()/AutoRoutine()…, Motion().AxisList() (có thể vẫn nullptr nếu Init lỗi — luôn kiểm).
//   3. OnShown(true/false), OnTick(), OnModalChanged(), OnWindowMoved() — CHỈ sau OnCoreReady.
//   4. OnClosing() — mọi trang, lúc tắt app (kể cả khi Core chưa sẵn sàng), trước McControl::Shutdown.
namespace ui
{
    struct IPage
    {
        /// <summary>McControl::Init vừa xong (thành công hay lỗi). Gọi một lần, trước OnShown/OnTick đầu tiên.</summary>
        virtual void OnCoreReady() {}

        /// <summary>Một nhịp cập nhật (= ControlTickMs, kẹp 33..100 ms; luồng UI) — không đếm nhịp để đo thời gian. Chỉ đọc trạng thái và đặt thuộc tính control;
        /// không gọi hàm chặn. Đặt thuộc tính chỉ khi giá trị đổi (ui::SetText, ui::Show, ui::SetTint).</summary>
        virtual void OnTick() = 0;

        /// <summary>Trang vừa được chuyển tới (true) hoặc rời đi (false) — Loaded/Unloaded của WPF TabControl. Trang có
        /// control VisionMaster phải ẩn/hiện popup của nó ở đây — popup không tự biết ô giữ chỗ đã biến mất.</summary>
        virtual void OnShown(bool /*visible*/) {}

        /// <summary>Cửa sổ chính đổi vị trí/kích thước: popup VisionMaster phải bám theo ô giữ chỗ.</summary>
        virtual void OnWindowMoved() {}

        /// <summary>Một lớp phủ modal (hộp thoại, bảng alarm) vừa mở (true) hoặc đóng hết (false): popup
        /// VisionMaster phải bị vô hiệu trong lúc đó.</summary>
        virtual void OnModalChanged(bool /*modalOpen*/) {}

        /// <summary>App sắp tắt: giải phóng control VisionMaster và mọi thứ gắn với cửa sổ.</summary>
        virtual void OnClosing() {}

        /// <summary>Chế độ --shot: số ảnh trang muốn chụp (mỗi ảnh một trạng thái hiển thị, vd. từng bước của Vision
        /// Edit). Trước ảnh thứ i (i ≥ 1) MainWindow gọi ShowShotVariant(i).</summary>
        virtual int ShotVariants() const { return 1; }
        virtual void ShowShotVariant(int /*index*/) {}

    protected:
        ~IPage() = default;
    };
}
