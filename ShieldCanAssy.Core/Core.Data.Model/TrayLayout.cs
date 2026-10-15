using System;
using ShieldCanAssy.Core.Infor;

namespace ShieldCanAssy.Core.Data.Model
{
    /// <summary>Một điểm teach — toạ độ của trục quét cột (X) và trục quét hàng (Y1 hoặc Y2).</summary>
    public class TeachPoint
    {
        public double X { get; set; }
        public double Y { get; set; }
        public bool Taught { get; set; }

        public TeachPoint() { }
        public TeachPoint(double x, double y) { X = x; Y = y; Taught = true; }

        public static TeachPoint operator -(TeachPoint a, TeachPoint b)
            => new TeachPoint(a.X - b.X, a.Y - b.Y);

        public TeachPoint Scaled(double k) => new TeachPoint(X * k, Y * k);
    }

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
    public class CellOffset
    {
        public double Dx { get; set; }
        public double Dy { get; set; }

        /// <summary>Bù đặt theo X, mm — cộng vào biến <c>CellX</c>.</summary>
        public double PlaceDx { get; set; }
        /// <summary>Bù đặt theo Y2, mm — cộng vào biến <c>CellY</c>.</summary>
        public double PlaceDy { get; set; }
        /// <summary>Bù đặt theo góc, độ — cộng vào biến <c>CellA</c> rồi quấn về [0, 360).</summary>
        public double PlaceDa { get; set; }

        public bool HasTeachOffset => Math.Abs(Dx) > 1e-9 || Math.Abs(Dy) > 1e-9;
        public bool HasPlaceTrim => Math.Abs(PlaceDx) > 1e-9 || Math.Abs(PlaceDy) > 1e-9 || Math.Abs(PlaceDa) > 1e-9;
        public bool IsZero => !HasTeachOffset && !HasPlaceTrim;
    }

    /// <summary>
    /// Lưới toạ độ RIÊNG của một đầu hút trên một tray — 3 điểm góc (P00/P0C/PR0) + offset tinh
    /// chỉnh từng ô. Trước đây chỉ teach một lưới chung "theo đầu hút #1" rồi suy ra vị trí tay
    /// #2 bằng cách trừ một hằng số pitch cơ khí cố định (NozzlePitchX) — nhưng khoảng cách gắn
    /// thật giữa 2 đầu hút không nhất thiết đúng bằng thiết kế, và bước HÚT ở Input Tray không có
    /// camera bù sai số (khác bước ĐẶT ở Assy Tray, luôn được CAM top-down đo lại) nên sai số đó
    /// đi thẳng vào sản phẩm. Mỗi Tool phải có lưới riêng, teach bằng cách jog tới khi ĐÚNG đầu
    /// hút của Tool đó căn vào góc tray (Tuấn xác nhận 2026-09-05).
    /// </summary>
    public class TrayGeometry
    {
        public TeachPoint P00 { get; set; } = new TeachPoint();
        public TeachPoint P0C { get; set; } = new TeachPoint();
        public TeachPoint PR0 { get; set; } = new TeachPoint();

        /// <summary>Ô góc đối diện, chỉ để kiểm chứng — KHÔNG tham gia tính lưới (PLAN.md §8.2).</summary>
        public TeachPoint PRC_Verify { get; set; } = new TeachPoint();

        /// <summary>Offset từng ô, mảng [Rows, Cols]. Null nghĩa là chưa có offset nào.</summary>
        public CellOffset[][] Offsets { get; set; }

        /// <summary>
        /// Nội suy affine 3 điểm, KHÔNG cộng offset — phần "lý thuyết" của <see cref="CellPos"/>.
        /// Tách riêng để trang Teaching tính được offset của một ô: offset = vị trí đã jog tới
        /// tay − giá trị hàm này. Trừ đi <see cref="CellPos"/> thay vì hàm này sẽ trừ mất offset
        /// cũ hai lần.
        /// </summary>
        public TeachPoint CellPosRaw(int row, int col, int rows, int cols)
        {
            double ux = 0, uy = 0, vx = 0, vy = 0;

            if (cols > 1)
            {
                ux = (P0C.X - P00.X) / (cols - 1);
                uy = (P0C.Y - P00.Y) / (cols - 1);
            }
            if (rows > 1)
            {
                vx = (PR0.X - P00.X) / (rows - 1);
                vy = (PR0.Y - P00.Y) / (rows - 1);
            }

            return new TeachPoint(P00.X + col * ux + row * vx, P00.Y + col * uy + row * vy);
        }

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
        public TeachPoint CellPos(int row, int col, int rows, int cols)
        {
            var p = CellPosRaw(row, col, rows, cols);

            var off = GetOffset(row, col);
            if (off != null) { p.X += off.Dx; p.Y += off.Dy; }

            return p;
        }

        public CellOffset GetOffset(int row, int col)
        {
            if (Offsets == null) return null;
            if (row < 0 || row >= Offsets.Length) return null;
            var line = Offsets[row];
            if (line == null || col < 0 || col >= line.Length) return null;
            return line[col];
        }

        /// <summary>Ghi offset teach, GIỮ NGUYÊN bù đặt của ô — hai số đo hai việc khác nhau,
        /// teach lại lưới không có lý do gì xoá lượng bù đã mò ra khi chạy thật.</summary>
        public void SetOffset(int row, int col, double dx, double dy, int rows, int cols)
        {
            var cell = Ensure(row, col, rows, cols);
            cell.Dx = dx;
            cell.Dy = dy;
        }

        /// <summary>Ghi bù đặt của ô, GIỮ NGUYÊN offset teach.</summary>
        public void SetPlaceTrim(int row, int col, double dx, double dy, double da, int rows, int cols)
        {
            var cell = Ensure(row, col, rows, cols);
            cell.PlaceDx = dx;
            cell.PlaceDy = dy;
            cell.PlaceDa = da;
        }

        /// <summary>
        /// Bù đặt chung — cộng cho MỌI ô của tray này khi đúng Tool này đặt, dồn thêm vào bù riêng của từng ô
        /// (Tuấn yêu cầu 2026-09-15). Chỉ Assy Tray dùng. Cùng nghĩa, cùng dấu với <see cref="CellOffset.PlaceDx"/>.
        /// </summary>
        public double PlaceDxAll { get; set; }
        public double PlaceDyAll { get; set; }
        public double PlaceDaAll { get; set; }

        public void SetPlaceTrimAll(double dx, double dy, double da)
        {
            PlaceDxAll = dx;
            PlaceDyAll = dy;
            PlaceDaAll = da;
        }

        /// <summary>Bù đặt tổng của một ô = bù chung + bù riêng của ô — đúng lượng App cộng vào đích gửi đi và
        /// dời tâm hàng rào lưới. Chỉ điền PlaceDx/Dy/Da, không mang offset teach.</summary>
        public CellOffset PlaceTrim(int row, int col)
        {
            var off = GetOffset(row, col);
            return new CellOffset
            {
                PlaceDx = PlaceDxAll + (off?.PlaceDx ?? 0),
                PlaceDy = PlaceDyAll + (off?.PlaceDy ?? 0),
                PlaceDa = PlaceDaAll + (off?.PlaceDa ?? 0)
            };
        }

        private CellOffset Ensure(int row, int col, int rows, int cols)
        {
            EnsureOffsets(rows, cols);
            var cell = Offsets[row][col];
            if (cell == null) { cell = new CellOffset(); Offsets[row][col] = cell; }
            return cell;
        }

        private void EnsureOffsets(int rows, int cols)
        {
            if (Offsets != null && Offsets.Length == rows && (rows == 0 || Offsets[0].Length == cols))
                return;
            Resize(rows, cols);
        }

        /// <summary>
        /// Đổi kích thước lưới, GIỮ LẠI phần trùng nhau của offset (PLAN.md §8.1). Xoá sạch khi
        /// đổi kích thước sẽ làm mất công tinh chỉnh của cả tray chỉ vì thêm một hàng.
        /// </summary>
        public void Resize(int rows, int cols)
        {
            var next = new CellOffset[rows][];
            for (int r = 0; r < rows; r++)
            {
                next[r] = new CellOffset[cols];
                for (int c = 0; c < cols; c++)
                    next[r][c] = GetOffset(r, c);
            }

            Offsets = next;
        }

        /// <summary>
        /// So ô góc đối diện đã teach với giá trị nội suy. Bắt được lỗi teach phổ biến nhất:
        /// nhầm thứ tự P0C với PR0, vốn cho ra lưới trông vẫn hợp lý nhưng sai hoàn toàn.
        /// </summary>
        public bool VerifyGrid(int rows, int cols, double threshold, out double error)
        {
            error = 0;
            if (!PRC_Verify.Taught) return true;

            var expect = CellPos(rows - 1, cols - 1, rows, cols);
            double dx = expect.X - PRC_Verify.X;
            double dy = expect.Y - PRC_Verify.Y;
            error = Math.Sqrt(dx * dx + dy * dy);
            return error <= threshold;
        }

        public bool IsTaught(int rows, int cols) => P00.Taught && (cols == 1 || P0C.Taught) && (rows == 1 || PR0.Taught);
    }

    /// <summary>
    /// Lưới ô của một tray — PLAN.md §8.1. Số hàng/cột cấu hình được, KHÔNG hardcode ở bất kỳ
    /// đâu kể cả lưới hiển thị trên trang Main. Kích thước (Rows/Cols) dùng CHUNG cho cả 2 Tool
    /// vì đó là kích thước VẬT LÝ của tray, nhưng toạ độ từng ô (<see cref="TrayGeometry"/>) là
    /// RIÊNG cho từng Tool — xem ghi chú ở đó.
    /// </summary>
    public class TrayLayout
    {
        public const int MinDim = 1;
        public const int MaxDim = 50;

        public string Name { get; set; } = "InputTray";

        private int _rows = 4;
        private int _cols = 5;

        public int Rows
        {
            get => _rows;
            set => _rows = Clamp(value);
        }

        public int Cols
        {
            get => _cols;
            set => _cols = Clamp(value);
        }

        /// <summary>Trục quét theo hàng: "Y1" hoặc "Y2".</summary>
        public string AxisRow { get; set; } = AxisId.Y1;
        /// <summary>Trục quét theo cột — luôn là X trên máy này.</summary>
        public string AxisCol { get; set; } = AxisId.X;

        /// <summary>Lưới toạ độ riêng của Tool1 (Z1) và Tool2 (Z2) — xem <see cref="TrayGeometry"/>.</summary>
        public TrayGeometry GeoZ1 { get; set; } = new TrayGeometry();
        public TrayGeometry GeoZ2 { get; set; } = new TrayGeometry();

        public TrayGeometry GeometryFor(string zAxis) => zAxis == AxisId.Z1 ? GeoZ1 : GeoZ2;

        public double VerifyThreshold { get; set; } = 0.5;

        /// <summary>
        /// Cao độ hút/đặt của TỪNG tay hút — Z1 và Z2 không nhất thiết bằng nhau (dung sai cơ
        /// khí giữa hai gá, độ dày sản phẩm đo được khác nhau...), nên phải teach và lưu độc lập
        /// thay vì dùng chung một giá trị (Tuấn xác nhận 2026-09-05).
        /// </summary>
        public double Z_PickZ1 { get; set; }
        public double Z_PickZ2 { get; set; }

        public double ZPickFor(string zAxis) => zAxis == AxisId.Z1 ? Z_PickZ1 : Z_PickZ2;

        /// <summary>
        /// Cao độ BẮT ĐẦU GIẢM TỐC của từng tay hút: Z chạy mức Auto CAO từ cao độ an toàn xuống
        /// tới đây, rồi mức Auto THẤP nốt đoạn còn lại tới cao độ hút/đặt (Tuấn yêu cầu
        /// 2026-09-18 — đầu hút chạm sản phẩm ở tốc độ chạy nhanh làm sứt hàng và xô ô tray).
        /// Teach riêng từng tay và từng tray, cùng lý do với <see cref="Z_PickZ1"/>.
        ///
        /// <para><b>Chiều NÂNG dùng lại chính cao độ này, đối xứng</b> (Tuấn yêu cầu 2026-09-24): mức Auto
        /// THẤP từ cao độ hút/đặt lên tới đây, rồi mức CAO nốt đoạn còn lại — xem
        /// <c>McFunction.MoveZUpAsync</c>. Nhấc nhanh khỏi hốc thì lúc hút sản phẩm dễ xê dịch trên đầu hút,
        /// lúc đặt thì dễ kéo theo con vừa nhả. Một cao độ teach, dùng cho cả hai chiều.</para>
        ///
        /// <para>Chu trình Auto và chạy rà dùng điểm giảm tốc ở chặng nào là do 4 ô ở trang Setting quyết định
        /// (<c>SystemData.InputZSlowDown</c>… — Tuấn 2026-10-07), mặc định: tray đầu vào hạ một mạch, nâng chậm; tray lắp
        /// ráp chậm cả hai chiều. Trả hàng NG về tray đầu vào vẫn giảm tốc cả hai chiều.</para>
        ///
        /// <para>Z dương là chiều ĐI XUỐNG nên điểm giảm tốc hợp lệ phải nằm GIỮA cao độ an toàn
        /// và cao độ hút/đặt: 0 &lt; Z_Slow &lt; Z_Pick. Bằng 0 = chưa teach, lúc đó
        /// <c>McFunction.MoveZDownAsync</c> hạ một mạch ở mức cao đúng như trước khi có tính năng
        /// này — điểm giảm tốc là TUỲ CHỌN, không bịa ra và cũng không chặn chạy.</para>
        /// </summary>
        public double Z_SlowZ1 { get; set; }
        public double Z_SlowZ2 { get; set; }

        public double ZSlowFor(string zAxis) => zAxis == AxisId.Z1 ? Z_SlowZ1 : Z_SlowZ2;

        /// <summary>Điểm giảm tốc cho một chặng — 0 (chạy một mạch) khi Setting tắt giảm tốc chặng đó.</summary>
        public double ZSlowFor(string zAxis, bool use) => use ? ZSlowFor(zAxis) : 0;

        /// <summary>Điểm giảm tốc đã teach và nằm đúng phía TRÊN cao độ hút/đặt.</summary>
        public bool ZSlowValid(string zAxis)
        {
            double slow = ZSlowFor(zAxis);
            return slow > 0 && slow < ZPickFor(zAxis);
        }

        public double Z_Safe { get; set; } = 50.0;

        public EScanOrder ScanOrder { get; set; } = EScanOrder.RowMajor;

        /// <summary>
        /// Vị trí gantry (X, <see cref="AxisRow"/>) khi CAM top-down nằm đúng tâm Ô (1,1) — mốc
        /// để tính vị trí trigger camera cho các ô còn lại khi quét trước tray lắp ráp
        /// (<c>Routine_Auto.S05_ScanAssy</c>), thay cho cách tính cũ qua Hand-Eye Dx/Dy. Ô khác suy
        /// ra bằng <see cref="CamPosFor"/>, theo lưới <see cref="CamGridZAxis"/>. Chỉ
        /// AssyTray dùng trường này — InputTray không quét top-down. Camera gắn cố định trên
        /// gantry, KHÔNG theo từng đầu hút như Hand-Eye, nên chỉ cần một mốc DÙNG CHUNG cho cả 2
        /// Tool (Tuấn xác nhận 2026-09-05).
        /// </summary>
        public double CamTopDownAnchorX { get; set; }
        public double CamTopDownAnchorY { get; set; }
        public bool CamTopDownAnchorTaught { get; set; }

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
        public const string CamGridZAxis = AxisId.Z2;

        /// <summary>
        /// Vị trí gantry (X, <see cref="AxisRow"/>) để CAM top-down soi đúng tâm ô (row, col): mốc
        /// đã teach ở ô (1,1) cộng độ lệch lưới của ô đó so với ô (1,1), tính trên
        /// <see cref="CamGridZAxis"/>. Mọi nơi cần soi một ô đều gọi hàm này — quét tray trước khi
        /// đặt, hút mẫu lúc xác định hand-eye, chạy từng bước, đo lại ô đã đặt — để bốn chỗ đó
        /// không thể lệch nhau.
        /// </summary>
        public TeachPoint CamPosFor(int row, int col)
        {
            var cell = CellPos(CamGridZAxis, row, col);
            var cell00 = CellPos(CamGridZAxis, 0, 0);
            return new TeachPoint(CamTopDownAnchorX + (cell.X - cell00.X),
                                  CamTopDownAnchorY + (cell.Y - cell00.Y));
        }

        public int CellCount => Rows * Cols;

        /// <summary>
        /// Nửa bước cột (độ dài vector bước cột của lưới Tool 1, mm). Hai ô lệch X ít hơn chừng này là CÙNG CỘT — phần
        /// lệch chỉ là độ nghiêng của tray khi teach. Luật chia cặp ô cho hai tay dùng nó để giữ thứ tự ghé cho cặp cùng
        /// cột (Tuấn 2026-10-15), xem <c>Routine_Auto.Nozzle1TakesFirstPickCell</c>. Một cột thì mọi ô cùng cột.
        /// </summary>
        public double HalfColPitch
        {
            get
            {
                if (Cols < 2) return double.PositiveInfinity;
                double ux = (GeoZ1.P0C.X - GeoZ1.P00.X) / (Cols - 1);
                double uy = (GeoZ1.P0C.Y - GeoZ1.P00.Y) / (Cols - 1);
                return 0.5 * Math.Sqrt(ux * ux + uy * uy);
            }
        }

        private static int Clamp(int v) => v < MinDim ? MinDim : (v > MaxDim ? MaxDim : v);

        public TeachPoint CellPosRaw(string zAxis, int row, int col) => GeometryFor(zAxis).CellPosRaw(row, col, Rows, Cols);
        public TeachPoint CellPos(string zAxis, int row, int col) => GeometryFor(zAxis).CellPos(row, col, Rows, Cols);
        public CellOffset GetOffset(string zAxis, int row, int col) => GeometryFor(zAxis).GetOffset(row, col);
        public void SetOffset(string zAxis, int row, int col, double dx, double dy) => GeometryFor(zAxis).SetOffset(row, col, dx, dy, Rows, Cols);
        public void SetPlaceTrim(string zAxis, int row, int col, double dx, double dy, double da)
            => GeometryFor(zAxis).SetPlaceTrim(row, col, dx, dy, da, Rows, Cols);
        public void SetPlaceTrimAll(string zAxis, double dx, double dy, double da) => GeometryFor(zAxis).SetPlaceTrimAll(dx, dy, da);

        /// <summary>Bù đặt tổng (bù chung của Tool + bù riêng của ô) — xem <see cref="TrayGeometry.PlaceTrim"/>.</summary>
        public CellOffset PlaceTrim(string zAxis, int row, int col) => GeometryFor(zAxis).PlaceTrim(row, col);

        /// <summary>
        /// Đổi kích thước lưới — áp dụng cho CẢ HAI lưới toạ độ (GeoZ1, GeoZ2) vì Rows/Cols là
        /// kích thước vật lý dùng chung, GIỮ LẠI phần trùng nhau của offset mỗi bên (PLAN.md §8.1).
        /// </summary>
        public void Resize(int rows, int cols)
        {
            rows = Clamp(rows);
            cols = Clamp(cols);

            _rows = rows;
            _cols = cols;
            GeoZ1.Resize(rows, cols);
            GeoZ2.Resize(rows, cols);
        }

        public bool VerifyGrid(string zAxis, out double error) => GeometryFor(zAxis).VerifyGrid(Rows, Cols, VerifyThreshold, out error);

        /// <summary>Cả hai Tool đều phải teach xong — Auto có thể dùng bất kỳ tay nào cho bất kỳ ô nào.</summary>
        public bool IsTaught => GeoZ1.IsTaught(Rows, Cols) && GeoZ2.IsTaught(Rows, Cols);

        public bool IsTaughtFor(string zAxis) => GeometryFor(zAxis).IsTaught(Rows, Cols);

        public static TrayLayout CreateInputDefault(string name = "InputTray") => new TrayLayout
        {
            Name = name, Rows = 4, Cols = 5, AxisRow = AxisId.Y1, AxisCol = AxisId.X
        };

        public static TrayLayout CreateAssyDefault() => new TrayLayout
        {
            Name = "AssyTray", Rows = 5, Cols = 6, AxisRow = AxisId.Y2, AxisCol = AxisId.X
        };
    }
}
