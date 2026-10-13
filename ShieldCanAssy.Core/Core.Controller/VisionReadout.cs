using System;
using System.Collections.Generic;
using System.Globalization;
using System.Threading;
using ShieldCanAssy.Core.Communication;
using ShieldCanAssy.Core.Data;
using ShieldCanAssy.Core.Infor;
using ShieldCanAssy.Core.Lang;

namespace ShieldCanAssy.Core.Controller
{
    /// <summary>
    /// Kết quả soi mới nhất của từng khung CAM trang Main, ghi sẵn thành chữ (Tuấn 2026-10-01): CAM bottom-up tay #1/#2
    /// hiện ô lắp ráp được gán, độ lệch sản phẩm trên tay và toạ độ tuyệt đối của điểm đích; CAM top-down hiện độ lệch
    /// so với điểm teach hand-eye (master). Mọi chỗ Trigger — Auto lẫn các nút trang Vision Edit — đều ghi vào đây,
    /// nên chữ luôn khớp ảnh VisionMaster đang vẽ trong khung. Trang Main đọc theo nhịp 300 ms.
    ///
    /// <para>Giữ lại số liệu thô của lần soi (<see cref="Shot"/>): đổi ngôn ngữ thì lần đọc kế tiếp dựng lại chữ và bảng
    /// theo ngôn ngữ mới (Tuấn 2026-10-09). Số đo, giờ chụp, số lấy từ model/hiệu chuẩn chốt lúc soi — không đổi theo.</para>
    /// </summary>
    public static class VisionReadout
    {
        private static Built _tool1, _tool2, _top;

        public static string Tool1 => Current(ref _tool1)?.Text ?? "";
        public static string Tool2 => Current(ref _tool2)?.Text ?? "";
        public static string Top => Current(ref _top)?.Text ?? "";

        /// <summary>Cùng kết quả với <see cref="Tool1"/>/<see cref="Tool2"/>/<see cref="Top"/> nhưng chia sẵn thành bảng cho
        /// trang Main (Tuấn 2026-10-07); null = chưa trigger. Bản chữ giữ lại làm tooltip và cho self-test.</summary>
        public static ReadoutCard Tool1Card => Current(ref _tool1)?.Card;
        public static ReadoutCard Tool2Card => Current(ref _tool2)?.Card;
        public static ReadoutCard TopCard => Current(ref _top)?.Card;

        private static readonly string[] TopColumns = { "X (mm)", "Y (mm)", "θ (°)" };

        /// <summary>Số liệu thô của một lần soi — dựng ra chữ và bảng theo ngôn ngữ đang chọn.</summary>
        private abstract class Shot
        {
            public abstract void Build(out string text, out ReadoutCard card);
        }

        /// <summary>Một lần soi đã dựng theo một ngôn ngữ. Bất biến — chữ và bảng đổi cùng lúc, không cần khoá.</summary>
        private sealed class Built
        {
            public Built(Shot shot)
            {
                Shot = shot;
                Lang = Loc.Language;
                shot.Build(out Text, out Card);
            }

            public readonly Shot Shot;
            public readonly ELanguage Lang;
            public readonly string Text;
            public readonly ReadoutCard Card;
        }

        /// <summary>Bản đang hiện của một khung; đã đổi ngôn ngữ thì dựng lại từ số liệu thô. Lần soi mới chen vào giữa thì
        /// giữ lần soi mới — không ghi đè nó bằng bản dựng lại của lần cũ.</summary>
        private static Built Current(ref Built slot)
        {
            var b = Volatile.Read(ref slot);
            if (b == null || b.Lang == Loc.Language) return b;
            var fresh = new Built(b.Shot);
            var prev = Interlocked.CompareExchange(ref slot, fresh, b);
            return ReferenceEquals(prev, b) ? fresh : prev;
        }

        /// <summary>
        /// AlignBottom_ToolN vừa chạy. <paramref name="cmd"/> null = chưa có (hoặc không tính) lệnh đặt;
        /// <paramref name="row"/> âm = không nhắm ô nào của tray lắp ráp.
        /// </summary>
        public static void Tool(int nozzle, VmCameraRead read, VmPlaceCommand cmd, int row = -1, int col = -1)
        {
            var shot = new ToolShot
            {
                Stamp = Stamp(), Nozzle = nozzle, Read = read, Cmd = cmd, Row = row, Col = col,
                YName = McDataManager.Model?.AssyTray?.AxisRow ?? "Y2"
            };
            if (cmd != null && (cmd.Route == EVmRoute.Ok || cmd.Route == EVmRoute.Skip) && row >= 0)
                shot.Parts = Breakdown(nozzle, cmd, row, col);
            var built = new Built(shot);
            if (nozzle == 1) Volatile.Write(ref _tool1, built);
            else Volatile.Write(ref _tool2, built);
        }

        private sealed class ToolShot : Shot
        {
            public string Stamp, YName;
            public int Nozzle, Row, Col;
            public VmCameraRead Read;
            public VmPlaceCommand Cmd;
            public BreakdownParts Parts;

            public override void Build(out string text, out ReadoutCard card)
            {
                var read = Read;
                var cmd = Cmd;
                string cell = CellText(Row, Col);
                text = Stamp + (cell.Length > 0 ? " · " + cell : "");
                var rows = new List<ReadoutRow>();
                string note = null;
                if (IsProblem(read)) text += " · " + (note = Problem(read));
                else if (!read.Found) text += " · " + (note = Loc.T("Readout.NoProduct"));
                else
                {
                    text += " · " + F(Loc.T("Readout.OffsetText"), read.X, read.Y, read.A);
                    rows.Add(new ReadoutRow(Loc.T("Readout.RowOffset"), F("{0:+0.000;-0.000}", read.X), F("{0:+0.000;-0.000}", read.Y),
                                            F("{0:0.00}", read.A), ""));
                    if (cmd != null)
                    {
                        text += cmd.Route == EVmRoute.Ok || cmd.Route == EVmRoute.Skip
                            ? "\n" + F(Loc.T("Readout.TargetText"),
                                cmd.CmdX, YName, cmd.CmdY2, Nozzle, cmd.CmdA, cmd.DiagDA) +
                              (cmd.Route == EVmRoute.Skip ? " · " + Loc.F("Readout.SkipCellInline", OneLine(cmd.Why)) : "")
                            : "\n" + Loc.F("Readout.NotPlaced", OneLine(cmd.Why));
                        if (cmd.Route == EVmRoute.Ok || cmd.Route == EVmRoute.Skip)
                            rows.Add(new ReadoutRow(Loc.T("Readout.RowTarget"), F("{0:0.000}", cmd.CmdX), F("{0:0.000}", cmd.CmdY2),
                                                    F("{0:0.00}", cmd.CmdA), F("{0:+0.00;-0.00}", cmd.DiagDA)));
                        if (cmd.Route == EVmRoute.Skip) note = Loc.F("Readout.SkipCell", OneLine(cmd.Why));
                        else if (cmd.Route != EVmRoute.Ok) note = Loc.F("Readout.NotPlaced", OneLine(cmd.Why));
                    }
                }
                var breakdown = Parts?.Card(cmd, cell, note);
                string[] columns = { "X (mm)", "Y (mm)", "θ (°)", Loc.T("Readout.ColCompTheta") };
                card = new ReadoutCard(cell, columns, rows.ToArray(), note, breakdown);
            }
        }

        /// <summary>
        /// Đích tách thành từng phần (Tuấn 2026-10-07): Đích = toạ độ teach + bù vision + bù chung + bù riêng, mỗi trục một
        /// hàng. Teach là ô theo lưới teach của đúng tay đặt (X, Y — cùng tâm hàng rào lưới ở <c>Routine_Auto</c>) và góc
        /// trục A lúc chụp (θ). Bù chung, bù riêng là số nhập ở trang Teaching (<see cref="Data.Model.TrayLayout.PlaceTrim"/>);
        /// θ nhân dấu góc của tay — đúng lượng nó làm trục A quay thêm. Bù vision là phần còn lại: chi tiết đích lệch khỏi
        /// lưới (LocateTop) cộng sản phẩm lệch trên đầu hút (CAM bottom-up). Tính bằng phép trừ nên tổng luôn đúng bằng
        /// Đích. Null khi không có model. Số lấy từ model lúc soi; chữ dựng ở <see cref="BreakdownParts.Card"/>.
        /// </summary>
        private static BreakdownParts Breakdown(int nozzle, VmPlaceCommand cmd, int row, int col)
        {
            var model = McDataManager.Model;
            var tray = model?.AssyTray;
            if (tray == null || row >= tray.Rows || col >= tray.Cols) return null;
            string z = nozzle == 1 ? AxisId.Z1 : AxisId.Z2;
            var teach = tray.CellPos(z, row, col);
            var geo = tray.GeometryFor(z);
            var off = geo.GetOffset(row, col);
            int sign = model.Calib?.AlignAngleSign(nozzle) ?? 0;
            if (sign == 0) sign = 1;

            return new BreakdownParts
            {
                TeachX = teach.X, TeachY = teach.Y, TeachA = AxisAngle.Wrap(cmd.CmdA - cmd.DiagDA),
                Ax = geo.PlaceDxAll, Ay = geo.PlaceDyAll, Aa = sign * geo.PlaceDaAll,
                Cx = off?.PlaceDx ?? 0, Cy = off?.PlaceDy ?? 0, Ca = sign * (off?.PlaceDa ?? 0)
            };
        }

        private sealed class BreakdownParts
        {
            public double TeachX, TeachY, TeachA, Ax, Ay, Aa, Cx, Cy, Ca;

            public ReadoutCard Card(VmPlaceCommand cmd, string cell, string note)
            {
                Func<double, string> mm = v => F("{0:+0.000;-0.000}", v), deg = v => F("{0:+0.00;-0.00}", v);
                var rows = new[]
                {
                    new ReadoutRow("X (mm)", F("{0:0.000}", TeachX), mm(cmd.CmdX - TeachX - Ax - Cx), mm(Ax), mm(Cx), F("{0:0.000}", cmd.CmdX)),
                    new ReadoutRow("Y (mm)", F("{0:0.000}", TeachY), mm(cmd.CmdY2 - TeachY - Ay - Cy), mm(Ay), mm(Cy), F("{0:0.000}", cmd.CmdY2)),
                    new ReadoutRow("θ (°)", F("{0:0.00}", TeachA), deg(cmd.DiagDA - Aa - Ca), deg(Aa), deg(Ca), F("{0:0.00}", cmd.CmdA)),
                };
                string[] columns =
                {
                    "Teaching", "+ Vision", Loc.T("Readout.ColCommon"), Loc.T("Readout.ColCellOffset"), Loc.T("Readout.ColTarget")
                };
                return new ReadoutCard(cell, columns, rows, note, isBreakdown: true);
            }
        }

        /// <summary>
        /// LocateTop vừa chạy — Δ của chi tiết TRONG ẢNH so với ảnh master chụp ở bước soi chi tiết đích lúc xác định hand-eye
        /// (Tuấn 2026-10-01): pose LocateTop là toạ độ máy (Script TopAbs cộng encoder X/Y2), nên trừ quãng trục đã chạy từ
        /// chỗ chụp master — không thì soi ô tray lắp ráp ra Δ cả trăm mm. Mốc CAM top-down của tray trùng chỗ chụp master,
        /// nên ở ô lắp ráp đây là độ lệch của chi tiết so với chỗ lưới teach đặt nó. Góc không phụ thuộc trục chạy.
        /// </summary>
        public static void TopDown(VmCameraRead read, int row = -1, int col = -1)
        {
            var shot = new TopShot { Stamp = Stamp(), Read = read, Row = row, Col = col };
            if (!IsProblem(read) && read.Found)
            {
                var calib = McDataManager.Model?.Calib;
                for (int n = 1; n <= 2; n++)
                {
                    double tx, ty, ta, mx, my;
                    if (calib != null && calib.TryGetHandEyeTarget(n, out tx, out ty, out ta) &&
                        calib.TryGetHandEyeMasterAxis(n, out mx, out my))
                    {
                        shot.HasMaster[n] = true;
                        shot.Dx[n] = (read.X - read.AxX) - (tx - mx);
                        shot.Dy[n] = (read.Y - read.AxY) - (ty - my);
                        shot.Da[n] = AxisAngle.ShortestDelta(ta, read.A);
                    }
                }
            }
            Volatile.Write(ref _top, new Built(shot));
        }

        private sealed class TopShot : Shot
        {
            public string Stamp;
            public int Row, Col;
            public VmCameraRead Read;
            /// <summary>Theo số tay (1, 2): có ảnh master hand-eye không, và Δ so với nó.</summary>
            public readonly bool[] HasMaster = new bool[3];
            public readonly double[] Dx = new double[3], Dy = new double[3], Da = new double[3];

            public override void Build(out string text, out ReadoutCard card)
            {
                var read = Read;
                string cell = CellText(Row, Col);
                text = Stamp + (cell.Length > 0 ? " · " + cell : "");
                var rows = new List<ReadoutRow>();
                string note = null;
                if (IsProblem(read)) text += " · " + (note = Problem(read));
                else if (!read.Found) text += " · " + (note = Loc.T("Readout.NoTarget"));
                else
                {
                    text += " · " + F(Loc.T("Readout.PartText"), read.X, read.Y, read.A);
                    rows.Add(new ReadoutRow(Loc.T("Readout.RowPart"), F("{0:0.000}", read.X), F("{0:0.000}", read.Y), F("{0:0.00}", read.A)));
                    var noMaster = new List<string>();
                    for (int n = 1; n <= 2; n++)
                    {
                        if (HasMaster[n])
                        {
                            text += "\n" + F(Loc.T("Readout.VsMaster"), n, Dx[n], Dy[n], Da[n]);
                            rows.Add(new ReadoutRow(Loc.F("Readout.DeltaNozzle", n), F("{0:+0.000;-0.000}", Dx[n]), F("{0:+0.000;-0.000}", Dy[n]),
                                                    F("{0:+0.00;-0.00}", Da[n])));
                        }
                        else
                        {
                            text += "\n" + Loc.F("Readout.NoMasterLine", n);
                            rows.Add(new ReadoutRow(Loc.F("Readout.DeltaNozzle", n), "—", "—", "—"));
                            noMaster.Add("#" + n);
                        }
                    }
                    if (noMaster.Count > 0)
                        note = Loc.F("Readout.NoMasterNote", string.Join(", ", noMaster));
                }
                card = new ReadoutCard(cell, TopColumns, rows.ToArray(), note);
            }
        }

        /// <summary>Lỗi gọi Flow hoặc kết quả sai hợp đồng — không đọc được tư thế.</summary>
        private static bool IsProblem(VmCameraRead read) =>
            read == null || !read.CallOk || !read.EchoOk || !read.VmValid && read.Found;

        /// <summary>Lỗi gọi Flow hoặc kết quả sai hợp đồng, gói một dòng — khung ở trang Main cố định số dòng.</summary>
        private static string Problem(VmCameraRead read)
        {
            if (read == null) return Loc.F("Readout.CallError", Loc.T("VmCycle.NoResult"));
            if (!read.CallOk) return Loc.F("Readout.CallError", OneLine(read.CallError));
            return Loc.F("Readout.Invalid", OneLine(read.EchoOk ? read.VmIssue : read.EchoIssue));
        }

        private static string OneLine(string s) => (s ?? "").Replace("\r", "").Replace("\n", " ");

        private static string Stamp() => DateTime.Now.ToString("HH:mm:ss");

        private static string CellText(int row, int col) => row >= 0 ? Loc.F("Readout.Cell", row + 1, col + 1) : "";

        private static string F(string format, params object[] args) => string.Format(CultureInfo.InvariantCulture, format, args);
    }

    /// <summary>
    /// Một khung kết quả soi chia thành bảng (Tuấn 2026-10-07): mỗi hàng một đại lượng (Lệch, Đích, Chi tiết, Δ so với
    /// master) với số theo cột <see cref="Columns"/>, ô được nhắm ở góc bảng. Giờ chụp đã có trên tiêu đề khung CAM nên
    /// không lặp lại. Bất biến — mỗi lần trigger tạo bản mới, trang Main đọc từ luồng giao diện không cần khoá.
    /// </summary>
    public sealed class ReadoutCard
    {
        public ReadoutCard(string cell, string[] columns, ReadoutRow[] rows, string note,
                           ReadoutCard breakdown = null, bool isBreakdown = false)
        {
            Cell = cell;
            Columns = columns;
            Rows = rows;
            Note = note;
            Breakdown = breakdown;
            IsBreakdown = isBreakdown;
        }

        /// <summary>Cùng lần soi, Đích tách thành teach + bù vision + bù chung + bù riêng — trang Main bấm tiêu đề bảng để
        /// đổi qua lại (Tuấn 2026-10-07). Null khi không có lệnh đặt hoặc không nhắm ô nào.</summary>
        public ReadoutCard Breakdown { get; }
        /// <summary>Đây chính là bảng tách Đích (<see cref="Breakdown"/> của một bảng khác).</summary>
        public bool IsBreakdown { get; }
        /// <summary>Bấm tiêu đề bảng có đổi được kiểu hiển thị không.</summary>
        public bool CanToggle => Breakdown != null || IsBreakdown;

        /// <summary>Ô tray lắp ráp được nhắm, vd "Ô (2,8)"; rỗng khi không nhắm ô nào.</summary>
        public string Cell { get; }
        public string[] Columns { get; }
        public ReadoutRow[] Rows { get; }
        /// <summary>Lỗi, không thấy, không đặt, bỏ ô, chưa có ảnh master — null khi không có gì phải báo.</summary>
        public string Note { get; }
        public bool HasRows => Rows.Length > 0;
    }

    public sealed class ReadoutRow
    {
        public ReadoutRow(string label, params string[] values)
        {
            Label = label;
            Values = values;
        }

        public string Label { get; }
        public string[] Values { get; }
    }
}
