#pragma once
#include <memory>
#include <string>
#include <vector>

#include "Controller/VmCycle.h"

namespace sca
{
    /// <summary>Một hàng của <see cref="ReadoutCard"/>: nhãn + số theo cột.</summary>
    struct ReadoutRow
    {
        ReadoutRow() = default;
        ReadoutRow(std::string label, std::vector<std::string> values) : Label(std::move(label)), Values(std::move(values)) {}

        std::string Label;
        std::vector<std::string> Values;
    };

    /// <summary>
    /// Một khung kết quả soi chia thành bảng (Tuấn 2026-10-07): mỗi hàng một đại lượng (Lệch, Đích, Chi tiết, Δ so với
    /// master) với số theo cột <see cref="Columns"/>, ô được nhắm ở góc bảng. Giờ chụp đã có trên tiêu đề khung CAM nên
    /// không lặp lại. Bất biến — mỗi lần trigger tạo bản mới (shared_ptr&lt;const&gt;), trang Main đọc từ luồng giao diện
    /// không cần khoá.
    /// </summary>
    struct ReadoutCard
    {
        /// <summary>Ô tray lắp ráp được nhắm, vd "Cell (2,8)"; rỗng khi không nhắm ô nào.</summary>
        std::string Cell;
        std::vector<std::string> Columns;
        std::vector<ReadoutRow> Rows;
        /// <summary>Lỗi, không thấy, không đặt, bỏ ô, chưa có ảnh master — RỖNG khi không có gì phải báo (C#: null).</summary>
        std::string Note;

        /// <summary>Cùng lần soi, Đích tách thành teach + bù vision + bù chung + bù riêng — trang Main bấm tiêu đề bảng để
        /// đổi qua lại (Tuấn 2026-10-07). nullptr khi không có lệnh đặt hoặc không nhắm ô nào.</summary>
        std::shared_ptr<const ReadoutCard> Breakdown;
        /// <summary>Đây chính là bảng tách Đích (<see cref="Breakdown"/> của một bảng khác).</summary>
        bool IsBreakdown{ false };

        /// <summary>Bấm tiêu đề bảng có đổi được kiểu hiển thị không.</summary>
        bool CanToggle() const { return Breakdown != nullptr || IsBreakdown; }
        bool HasRows() const { return !Rows.empty(); }
    };

    /// <summary>
    /// Kết quả soi mới nhất của từng khung CAM trang Main, ghi sẵn thành chữ (Tuấn 2026-10-01): CAM bottom-up tay #1/#2
    /// hiện ô lắp ráp được gán, độ lệch sản phẩm trên tay và toạ độ tuyệt đối của điểm đích; CAM top-down hiện độ lệch
    /// so với điểm teach hand-eye (master). Mọi chỗ Trigger — Auto lẫn các nút trang Vision Edit — đều ghi vào đây,
    /// nên chữ luôn khớp ảnh VisionMaster đang vẽ trong khung. Trang Main đọc theo nhịp 300 ms.
    ///
    /// <para>Bản C# giữ số liệu thô để dựng lại chữ khi đổi ngôn ngữ; bản C++ chỉ có tiếng Anh nên dựng một lần lúc soi.
    /// Mọi hàm an toàn đa luồng.</para>
    /// </summary>
    class VisionReadout
    {
    public:
        VisionReadout() = delete;

        /// <summary>Chữ của khung (rỗng = chưa trigger).</summary>
        static std::string Tool1();
        static std::string Tool2();
        static std::string Top();

        /// <summary>Cùng kết quả với Tool1/Tool2/Top nhưng chia sẵn thành bảng cho trang Main (Tuấn 2026-10-07);
        /// nullptr = chưa trigger. Bản chữ giữ lại làm tooltip và cho bài tự kiểm.</summary>
        static std::shared_ptr<const ReadoutCard> Tool1Card();
        static std::shared_ptr<const ReadoutCard> Tool2Card();
        static std::shared_ptr<const ReadoutCard> TopCard();

        /// <summary>
        /// AlignBottom_ToolN vừa chạy. <paramref name="cmd"/> nullptr = chưa có (hoặc không tính) lệnh đặt;
        /// <paramref name="row"/> âm = không nhắm ô nào của tray lắp ráp. <paramref name="read"/> nullptr = không có
        /// kết quả (C#: read null).
        /// </summary>
        static void Tool(int nozzle, const VmCameraRead* read, const VmPlaceCommand* cmd, int row = -1, int col = -1);
        static void Tool(int nozzle, const VmCameraRead& read, const VmPlaceCommand* cmd, int row = -1, int col = -1)
        {
            Tool(nozzle, &read, cmd, row, col);
        }
        static void Tool(int nozzle, const VmCameraRead& read, const VmPlaceCommand& cmd, int row = -1, int col = -1)
        {
            Tool(nozzle, &read, &cmd, row, col);
        }

        /// <summary>
        /// LocateTop vừa chạy — Δ của chi tiết TRONG ẢNH so với ảnh master chụp ở bước soi chi tiết đích lúc xác định
        /// hand-eye (Tuấn 2026-10-01): pose LocateTop là toạ độ máy (Script TopAbs cộng encoder X/Y2), nên trừ quãng trục
        /// đã chạy từ chỗ chụp master — không thì soi ô tray lắp ráp ra Δ cả trăm mm. Góc không phụ thuộc trục chạy.
        /// </summary>
        static void TopDown(const VmCameraRead* read, int row = -1, int col = -1);
        static void TopDown(const VmCameraRead& read, int row = -1, int col = -1) { TopDown(&read, row, col); }
    };
}
