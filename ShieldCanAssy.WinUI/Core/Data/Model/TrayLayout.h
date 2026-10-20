#pragma once
#include "Infor/Enums.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sca
{
    /// <summary>Một điểm teach — toạ độ của trục quét cột (X) và trục quét hàng (Y1 hoặc Y2). Kiểu giá trị: bản C#
    /// luôn gán một TeachPoint mới chứ không sửa tại chỗ điểm dùng chung.</summary>
    struct TeachPoint
    {
        double X = 0;
        double Y = 0;
        bool Taught = false;

        TeachPoint() = default;
        TeachPoint(double x, double y) : X(x), Y(y), Taught(true) {}

        friend TeachPoint operator-(const TeachPoint& a, const TeachPoint& b) { return TeachPoint(a.X - b.X, a.Y - b.Y); }

        TeachPoint Scaled(double k) const { return TeachPoint(X * k, Y * k); }
    };

    /// <summary>
    /// Số tinh chỉnh riêng của MỘT ô. Hai nhóm khác nghĩa nhau, cố ý không gộp làm một:
    ///
    /// <list type="bullet">
    /// <item><b>Dx, Dy — offset teach:</b> ô thật nằm lệch so với lưới nội suy 3 điểm. Sửa vị
    /// trí HÚT ở Input Tray (không có camera bù), chỗ ngắm CAM top-down và tâm hàng rào lưới
    /// lúc đặt.</item>
    /// <item><b>PlaceDx, PlaceDy, PlaceDa — bù đặt (chỉ Assy Tray):</b> chạy thật thấy sản phẩm
    /// đặt xuống còn lệch một chút thì cộng thêm vào ĐÍCH gửi cho VisionMaster (biến CellX,
    /// CellY, CellA). Điểm đặt do camera đo ra chứ không lấy từ lưới nội suy, nên KHÔNG dùng
    /// chung số với offset teach được — cộng Dx vào đích là bù hai lần một sai số mà camera đã
    /// nhìn thấy rồi (Tuấn chốt 2026-09-14).</item>
    /// </list>
    /// </summary>
    struct CellOffset
    {
        double Dx = 0;
        double Dy = 0;

        /// <summary>Bù đặt theo X, mm — cộng vào biến <c>CellX</c>.</summary>
        double PlaceDx = 0;
        /// <summary>Bù đặt theo Y2, mm — cộng vào biến <c>CellY</c>.</summary>
        double PlaceDy = 0;
        /// <summary>Bù đặt theo góc, độ — cộng vào biến <c>CellA</c> rồi quấn về [0, 360).</summary>
        double PlaceDa = 0;

        bool HasTeachOffset() const;
        bool HasPlaceTrim() const;
        bool IsZero() const { return !HasTeachOffset() && !HasPlaceTrim(); }
    };

    /// <summary>
    /// Lưới toạ độ RIÊNG của một đầu hút trên một tray — 3 điểm góc (P00/P0C/PR0) + offset tinh
    /// chỉnh từng ô. Trước đây chỉ teach một lưới chung "theo đầu hút #1" rồi suy ra vị trí tay
    /// #2 bằng cách trừ một hằng số pitch cơ khí cố định (NozzlePitchX) — nhưng khoảng cách gắn
    /// thật giữa 2 đầu hút không nhất thiết đúng bằng thiết kế, và bước HÚT ở Input Tray không có
    /// camera bù sai số (khác bước ĐẶT ở Assy Tray, luôn được CAM top-down đo lại) nên sai số đó
    /// đi thẳng vào sản phẩm. Mỗi Tool phải có lưới riêng, teach bằng cách jog tới khi ĐÚNG đầu
    /// hút của Tool đó căn vào góc tray (Tuấn xác nhận 2026-09-05).
    /// </summary>
    struct TrayGeometry
    {
        TeachPoint P00;
        TeachPoint P0C;
        TeachPoint PR0;

        /// <summary>Ô góc đối diện, chỉ để kiểm chứng — KHÔNG tham gia tính lưới (PLAN.md §8.2).</summary>
        TeachPoint PRC_Verify;

        /// <summary>Offset từng ô, mảng [Rows][Cols]. RỖNG đứng vai null của C# (chưa có offset nào — khoá "Offsets"
        /// không ghi ra JSON); ô nullopt = null.</summary>
        std::vector<std::vector<std::optional<CellOffset>>> Offsets;

        /// <summary>
        /// Nội suy affine 3 điểm, KHÔNG cộng offset — phần "lý thuyết" của <see cref="CellPos"/>.
        /// Tách riêng để trang Teaching tính được offset của một ô: offset = vị trí đã jog tới
        /// tay − giá trị hàm này. Trừ đi <see cref="CellPos"/> thay vì hàm này sẽ trừ mất offset
        /// cũ hai lần.
        /// </summary>
        TeachPoint CellPosRaw(int row, int col, int rows, int cols) const;

        /// <summary>
        /// Nội suy affine 3 điểm (PLAN.md §8.2):
        /// <code>
        /// u = (P0C − P00) / (Cols − 1)      // vector bước theo CỘT
        /// v = (PR0 − P00) / (Rows − 1)      // vector bước theo HÀNG
        /// P(r,c) = P00 + c·u + r·v + Offsets[r][c]
        /// </code>
        /// u và v là vector 2 chiều nên phép nội suy này hấp thụ được cả tray đặt hơi xoay lẫn
        /// hai trục không thật sự vuông góc — đó là lý do dùng 3 điểm thay vì nhập pitch.
        /// </summary>
        TeachPoint CellPos(int row, int col, int rows, int cols) const;

        /// <summary>Offset của ô; nullptr nếu chưa có. Con trỏ mất hiệu lực khi <see cref="Resize"/>.</summary>
        const CellOffset* GetOffset(int row, int col) const;
        CellOffset* GetOffset(int row, int col);

        /// <summary>Ghi offset teach, GIỮ NGUYÊN bù đặt của ô — hai số đo hai việc khác nhau,
        /// teach lại lưới không có lý do gì xoá lượng bù đã mò ra khi chạy thật.</summary>
        void SetOffset(int row, int col, double dx, double dy, int rows, int cols);

        /// <summary>Ghi bù đặt của ô, GIỮ NGUYÊN offset teach.</summary>
        void SetPlaceTrim(int row, int col, double dx, double dy, double da, int rows, int cols);

        /// <summary>
        /// Bù đặt chung — cộng cho MỌI ô của tray này khi đúng Tool này đặt, dồn thêm vào bù riêng của từng ô
        /// (Tuấn yêu cầu 2026-09-15). Chỉ Assy Tray dùng. Cùng nghĩa, cùng dấu với <see cref="CellOffset::PlaceDx"/>.
        /// </summary>
        double PlaceDxAll = 0;
        double PlaceDyAll = 0;
        double PlaceDaAll = 0;

        void SetPlaceTrimAll(double dx, double dy, double da);

        /// <summary>Bù đặt tổng của một ô = bù chung + bù riêng của ô — đúng lượng App cộng vào đích gửi đi và
        /// dời tâm hàng rào lưới. Chỉ điền PlaceDx/Dy/Da, không mang offset teach.</summary>
        CellOffset PlaceTrim(int row, int col) const;

        /// <summary>
        /// Đổi kích thước lưới, GIỮ LẠI phần trùng nhau của offset (PLAN.md §8.1). Xoá sạch khi
        /// đổi kích thước sẽ làm mất công tinh chỉnh của cả tray chỉ vì thêm một hàng.
        /// </summary>
        void Resize(int rows, int cols);

        /// <summary>
        /// So ô góc đối diện đã teach với giá trị nội suy. Bắt được lỗi teach phổ biến nhất:
        /// nhầm thứ tự P0C với PR0, vốn cho ra lưới trông vẫn hợp lý nhưng sai hoàn toàn.
        /// </summary>
        bool VerifyGrid(int rows, int cols, double threshold, double& error) const;

        bool IsTaught(int rows, int cols) const
        {
            return P00.Taught && (cols == 1 || P0C.Taught) && (rows == 1 || PR0.Taught);
        }

    private:
        CellOffset& Ensure(int row, int col, int rows, int cols);
        void EnsureOffsets(int rows, int cols);
    };

    /// <summary>
    /// Lưới ô của một tray — PLAN.md §8.1. Số hàng/cột cấu hình được, KHÔNG hardcode ở bất kỳ
    /// đâu kể cả lưới hiển thị trên trang Main. Kích thước (Rows/Cols) dùng CHUNG cho cả 2 Tool
    /// vì đó là kích thước VẬT LÝ của tray, nhưng toạ độ từng ô (<see cref="TrayGeometry"/>) là
    /// RIÊNG cho từng Tool — xem ghi chú ở đó.
    ///
    /// <para>Rows/Cols là property có kẹp [MinDim, MaxDim] ở bản C# → hàm <c>Rows()</c>/<c>SetRows()</c>.</para>
    /// </summary>
    struct TrayLayout
    {
        static constexpr int MinDim = 1;
        static constexpr int MaxDim = 50;

        std::string Name = "InputTray";

        int Rows() const { return m_rows; }
        void SetRows(int v) { m_rows = Clamp(v); }
        int Cols() const { return m_cols; }
        void SetCols(int v) { m_cols = Clamp(v); }

        /// <summary>Trục quét theo hàng: "Y1" hoặc "Y2".</summary>
        std::string AxisRow = "Y1";
        /// <summary>Trục quét theo cột — luôn là X trên máy này.</summary>
        std::string AxisCol = "X";

        /// <summary>Lưới toạ độ riêng của Tool1 (Z1) và Tool2 (Z2) — xem <see cref="TrayGeometry"/>.</summary>
        TrayGeometry GeoZ1;
        TrayGeometry GeoZ2;

        /// <summary>"Z1" → GeoZ1, mọi tên khác → GeoZ2 (như bản C#).</summary>
        TrayGeometry& GeometryFor(std::string_view zAxis);
        const TrayGeometry& GeometryFor(std::string_view zAxis) const;

        double VerifyThreshold = 0.5;

        /// <summary>
        /// Cao độ hút/đặt của TỪNG tay hút — Z1 và Z2 không nhất thiết bằng nhau (dung sai cơ
        /// khí giữa hai gá, độ dày sản phẩm đo được khác nhau...), nên phải teach và lưu độc lập
        /// thay vì dùng chung một giá trị (Tuấn xác nhận 2026-09-05).
        /// </summary>
        double Z_PickZ1 = 0;
        double Z_PickZ2 = 0;

        double ZPickFor(std::string_view zAxis) const;

        /// <summary>
        /// Cao độ BẮT ĐẦU GIẢM TỐC của từng tay hút: Z chạy mức Auto CAO từ cao độ an toàn xuống
        /// tới đây, rồi mức Auto THẤP nốt đoạn còn lại tới cao độ hút/đặt (Tuấn yêu cầu
        /// 2026-09-18 — đầu hút chạm sản phẩm ở tốc độ chạy nhanh làm sứt hàng và xô ô tray).
        /// Teach riêng từng tay và từng tray, cùng lý do với <see cref="Z_PickZ1"/>.
        ///
        /// <para><b>Chiều NÂNG dùng lại chính cao độ này, đối xứng</b> (Tuấn yêu cầu 2026-09-24): mức Auto
        /// THẤP từ cao độ hút/đặt lên tới đây, rồi mức CAO nốt đoạn còn lại — xem
        /// <c>McFunction.MoveZUp</c>. Nhấc nhanh khỏi hốc thì lúc hút sản phẩm dễ xê dịch trên đầu hút,
        /// lúc đặt thì dễ kéo theo con vừa nhả. Một cao độ teach, dùng cho cả hai chiều.</para>
        ///
        /// <para>Chu trình Auto và chạy rà dùng điểm giảm tốc ở chặng nào là do 4 ô ở trang Setting quyết định
        /// (<c>SystemData::InputZSlowDown</c>… — Tuấn 2026-10-07), mặc định: tray đầu vào hạ một mạch, nâng chậm; tray lắp
        /// ráp chậm cả hai chiều. Trả hàng NG về tray đầu vào vẫn giảm tốc cả hai chiều.</para>
        ///
        /// <para>Z dương là chiều ĐI XUỐNG nên điểm giảm tốc hợp lệ phải nằm GIỮA cao độ an toàn
        /// và cao độ hút/đặt: 0 &lt; Z_Slow &lt; Z_Pick. Bằng 0 = chưa teach, lúc đó
        /// <c>McFunction.MoveZDown</c> hạ một mạch ở mức cao đúng như trước khi có tính năng
        /// này — điểm giảm tốc là TUỲ CHỌN, không bịa ra và cũng không chặn chạy.</para>
        /// </summary>
        double Z_SlowZ1 = 0;
        double Z_SlowZ2 = 0;

        double ZSlowFor(std::string_view zAxis) const;

        /// <summary>Điểm giảm tốc cho một chặng — 0 (chạy một mạch) khi Setting tắt giảm tốc chặng đó.</summary>
        double ZSlowFor(std::string_view zAxis, bool use) const { return use ? ZSlowFor(zAxis) : 0; }

        /// <summary>Điểm giảm tốc đã teach và nằm đúng phía TRÊN cao độ hút/đặt.</summary>
        bool ZSlowValid(std::string_view zAxis) const;

        double Z_Safe = 50.0;

        EScanOrder ScanOrder = EScanOrder::RowMajor;

        /// <summary>
        /// Vị trí gantry (X, <see cref="AxisRow"/>) khi CAM top-down nằm đúng tâm Ô (1,1) — mốc
        /// để tính vị trí trigger camera cho các ô còn lại khi quét trước tray lắp ráp
        /// (<c>Routine_Auto.S05_ScanAssy</c>), thay cho cách tính cũ qua Hand-Eye Dx/Dy. Ô khác suy
        /// ra bằng <see cref="CamPosFor"/>, theo lưới <see cref="CamGridZAxis"/>. Chỉ
        /// AssyTray dùng trường này — InputTray không quét top-down. Camera gắn cố định trên
        /// gantry, KHÔNG theo từng đầu hút như Hand-Eye, nên chỉ cần một mốc DÙNG CHUNG cho cả 2
        /// Tool (Tuấn xác nhận 2026-09-05).
        /// </summary>
        double CamTopDownAnchorX = 0;
        double CamTopDownAnchorY = 0;
        bool CamTopDownAnchorTaught = false;

        /// <summary>
        /// Lưới teach dùng để suy ra chỗ đứng soi của các ô KHÁC từ <see cref="CamTopDownAnchorX"/> —
        /// <b>lưới của Tool 2</b> (Tuấn chốt 2026-09-16: tay #2 lắp gần CAM top-down hơn).
        ///
        /// <para>Chỉ HIỆU giữa hai ô được dùng, mà hiệu đó tả cái tray (bước ô, độ nghiêng) chứ
        /// không tả tay hút, nên về nguyên tắc lưới nào cũng phải ra cùng chỗ đứng — phần khác
        /// nhau giữa hai lưới là một lượng dịch chung và bị triệt tiêu khi trừ. Ghi thành hằng số
        /// để CHỈ CÓ MỘT chỗ quyết định, và để đọc code là biết ngay đang theo tay nào.</para>
        ///
        /// <para>Hệ quả: teach lại P1/P2/P3 hoặc đặt offset ô của <b>Tool 2</b> là mọi điểm chụp
        /// dịch theo; làm hai việc đó bên Tool 1 thì chỗ đứng của camera không đổi.</para>
        /// </summary>
        static constexpr const char* CamGridZAxis = "Z2";

        /// <summary>
        /// Vị trí gantry (X, <see cref="AxisRow"/>) để CAM top-down soi đúng tâm ô (row, col): mốc
        /// đã teach ở ô (1,1) cộng độ lệch lưới của ô đó so với ô (1,1), tính trên
        /// <see cref="CamGridZAxis"/>. Mọi nơi cần soi một ô đều gọi hàm này — quét tray trước khi
        /// đặt, hút mẫu lúc xác định hand-eye, chạy từng bước, đo lại ô đã đặt — để bốn chỗ đó
        /// không thể lệch nhau.
        /// </summary>
        TeachPoint CamPosFor(int row, int col) const;

        int CellCount() const { return Rows() * Cols(); }

        /// <summary>
        /// Nửa bước cột (độ dài vector bước cột của lưới Tool 1, mm). Hai ô lệch X ít hơn chừng này là CÙNG CỘT — phần
        /// lệch chỉ là độ nghiêng của tray khi teach. Luật chia cặp ô cho hai tay dùng nó để giữ thứ tự ghé cho cặp cùng
        /// cột (Tuấn 2026-10-15), xem <c>Routine_Auto.Nozzle1TakesFirstPickCell</c>. Một cột thì mọi ô cùng cột
        /// (+∞).
        /// </summary>
        double HalfColPitch() const;

        TeachPoint CellPosRaw(std::string_view zAxis, int row, int col) const { return GeometryFor(zAxis).CellPosRaw(row, col, Rows(), Cols()); }
        TeachPoint CellPos(std::string_view zAxis, int row, int col) const { return GeometryFor(zAxis).CellPos(row, col, Rows(), Cols()); }
        const CellOffset* GetOffset(std::string_view zAxis, int row, int col) const { return GeometryFor(zAxis).GetOffset(row, col); }
        CellOffset* GetOffset(std::string_view zAxis, int row, int col) { return GeometryFor(zAxis).GetOffset(row, col); }
        void SetOffset(std::string_view zAxis, int row, int col, double dx, double dy)
        {
            GeometryFor(zAxis).SetOffset(row, col, dx, dy, Rows(), Cols());
        }
        void SetPlaceTrim(std::string_view zAxis, int row, int col, double dx, double dy, double da)
        {
            GeometryFor(zAxis).SetPlaceTrim(row, col, dx, dy, da, Rows(), Cols());
        }
        void SetPlaceTrimAll(std::string_view zAxis, double dx, double dy, double da) { GeometryFor(zAxis).SetPlaceTrimAll(dx, dy, da); }

        /// <summary>Bù đặt tổng (bù chung của Tool + bù riêng của ô) — xem <see cref="TrayGeometry::PlaceTrim"/>.</summary>
        CellOffset PlaceTrim(std::string_view zAxis, int row, int col) const { return GeometryFor(zAxis).PlaceTrim(row, col); }

        /// <summary>
        /// Đổi kích thước lưới — áp dụng cho CẢ HAI lưới toạ độ (GeoZ1, GeoZ2) vì Rows/Cols là
        /// kích thước vật lý dùng chung, GIỮ LẠI phần trùng nhau của offset mỗi bên (PLAN.md §8.1).
        /// </summary>
        void Resize(int rows, int cols);

        bool VerifyGrid(std::string_view zAxis, double& error) const
        {
            return GeometryFor(zAxis).VerifyGrid(Rows(), Cols(), VerifyThreshold, error);
        }

        /// <summary>Cả hai Tool đều phải teach xong — Auto có thể dùng bất kỳ tay nào cho bất kỳ ô nào.</summary>
        bool IsTaught() const { return GeoZ1.IsTaught(Rows(), Cols()) && GeoZ2.IsTaught(Rows(), Cols()); }

        bool IsTaughtFor(std::string_view zAxis) const { return GeometryFor(zAxis).IsTaught(Rows(), Cols()); }

        static std::shared_ptr<TrayLayout> CreateInputDefault(std::string_view name = "InputTray");
        static std::shared_ptr<TrayLayout> CreateAssyDefault();

    private:
        static int Clamp(int v) { return v < MinDim ? MinDim : (v > MaxDim ? MaxDim : v); }

        int m_rows = 4;
        int m_cols = 5;
    };
}
