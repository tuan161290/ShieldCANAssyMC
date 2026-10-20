#pragma once
// Thao tác "giữ" và các chốt chặn bấm nhầm (UI-SPEC §0.3, §9.6) dùng chung cho mọi trang:
//   [Hold]   AttachJog       — giữ để chạy, nhả là dừng
//   [Hold3s] AttachHold      — bấm/giữ có bắt chuột (vd. Brake Off: giữ 3 s = nhả phanh khẩn cấp)
//   [2×]     AttachDoubleTap — chỉ chạm đúp mới chạy, chạm đơn không làm gì
//
// Lệnh jog/dừng phải đi đúng thứ tự gửi: hai luồng nền khác nhau có thể làm lệnh DỪNG (nhả nút) chạy trước lệnh CHẠY
// (nhấn nút) và trục chạy mãi. Mọi lệnh tay ngắn (jog, dừng, Io.SetDo, Func.SetVacuum/SetBlow) vì vậy đi qua MỘT hàng
// đợi tuần tự (PostMotionCommand).
#include <functional>
#include <string>

namespace ui
{
    /// <summary>Xếp một lệnh tay ngắn vào hàng đợi tuần tự (một luồng nền, đúng thứ tự gửi). Lệnh dài (về
    /// gốc, chạy tới điểm) KHÔNG đi qua đây — dùng RunBackground.</summary>
    void PostMotionCommand(std::function<void()> fn);

    /// <summary>
    /// Gắn hành vi jog cho một nút: nhấn = McFunction::Jog(axis, dir), nhả / kéo chuột ra ngoài / mất chuột / nút bị vô
    /// hiệu / rời cây hiển thị = McFunction::Jog(axis, 0). Bắt cả sự kiện con trỏ mà Button đã xử lý (handledEventsToo),
    /// nên dùng được với Button thường (không gắn Click).
    /// </summary>
    void AttachJog(winrt::Microsoft::UI::Xaml::Controls::Button const& button, std::string axisName, int dir);

    /// <summary>Dừng mọi trục đang được giữ nút jog — MainWindow gọi khi chuyển trang và khi có lớp phủ modal mở,
    /// phòng trường hợp nút bị che trước khi kịp nhả.</summary>
    void StopActiveJogs();

    /// <summary>
    /// [2×] Chạy <paramref name="fn"/> CHỈ khi chạm đúp / nhấp đúp (sự kiện DoubleTapped). Không gắn Click: chạm đơn
    /// không làm gì — đúng RequireDoubleClick của bản WPF (Init, Go Ref…).
    /// </summary>
    void AttachDoubleTap(winrt::Microsoft::UI::Xaml::Controls::Button const& button, std::function<void()> fn);

    /// <summary>
    /// [Hold] có bắt chuột: <paramref name="onStart"/> lúc PointerPressed (CapturePointer), <paramref name="onEnd"/> lúc
    /// PointerReleased / PointerCaptureLost / PointerCanceled / nút bị vô hiệu / rời cây. onEnd CHỈ chạy nếu một lần nhấn
    /// đã thực sự bắt đầu (rê chuột qua không bao giờ gọi onEnd — không nhả phanh nhầm). Trang tự đo thời gian giữ
    /// (vd. DispatcherQueueTimer 100 ms cho Brake Off 3 s).
    /// </summary>
    void AttachHold(winrt::Microsoft::UI::Xaml::Controls::Button const& button, std::function<void()> onStart,
                    std::function<void()> onEnd);
}
