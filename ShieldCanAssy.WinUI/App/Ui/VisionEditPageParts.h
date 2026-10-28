#pragma once
// Phần dựng bằng code của trang Vision Edit (UI-SPEC §6): ô jog của thanh trên, nút có huy hiệu số bước, lưới chọn ô tray
// (nút 6 của bước hand-eye, hai ô của khu chạy kiểm). Tách khỏi VisionEditPage.xaml.cpp cho trang đỡ dài; không giữ trạng
// thái máy.
#include <functional>
#include <string>
#include <vector>

namespace ui::visionedit
{
    /// <summary>"X [−][+]" — tên trục (Bold 14, rộng 28) + hai nút jog 48×40 chữ 18 [Hold] (ui::AttachJog). Tooltip
    /// Vision.JogBackTip / JogFwdTip. Hai nút được thêm vào <paramref name="buttons"/> để trang khoá theo Busy.</summary>
    winrt::Microsoft::UI::Xaml::Controls::StackPanel BuildJogCell(std::string const& axisName,
                                                                  std::vector<winrt::Microsoft::UI::Xaml::Controls::Button>& buttons);

    /// <summary>Nội dung nút có huy hiệu số bước: vòng tròn 26 px (viền accent, chữ accent) + nhãn ≤ 2 từ. Nếu
    /// <paramref name="caption"/> khác null thì thêm một dòng chú thích nhỏ dưới nhãn (vd. "(in place)" của nút 6) và trả
    /// TextBlock đó qua con trỏ để trang cập nhật.</summary>
    void SetBadge(winrt::Microsoft::UI::Xaml::Controls::Button const& button, int number, wchar_t const* label,
                  winrt::Microsoft::UI::Xaml::Controls::TextBlock* caption = nullptr);

    /// <summary>
    /// Nội dung Flyout chọn ô tray: tiêu đề <paramref name="title"/>, lưới rows×cols nút 48×40 "(r,c)" (ô đang chọn: viền
    /// Accent 2.5), nút "In Place" ở đáy khi <paramref name="allowInPlace"/>. Dùng cho nút 6 của hand-eye (VE-44/45) và hai
    /// ô của khu chạy kiểm từng bước. <paramref name="onPick"/>(row, col) — 0-based; (−1, −1) = tại chỗ.
    /// </summary>
    winrt::Microsoft::UI::Xaml::FrameworkElement BuildCellPicker(winrt::hstring const& title, int rows, int cols, int selRow,
                                                                 int selCol, bool allowInPlace,
                                                                 std::function<void(int, int)> onPick);
}
