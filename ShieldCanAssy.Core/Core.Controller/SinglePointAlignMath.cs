using System;

namespace ShieldCanAssy.Core.Controller
{
    /// <summary>
    /// Bản sao C# của phép toán module <i>Single Point Alignment</i> (单点对位) trong VisionMaster
    /// 4.4.0 — Help "Calculation › Single Point Alignment":
    ///
    /// <code>
    /// Δθ      = chuẩn hoá(θ_đích − θ_đối_tượng)          (Help: chuẩn hoá về −π…π)
    /// (Δx,Δy) = đích − R(Δθ)·đối_tượng
    /// </code>
    ///
    /// <para><b>KHÔNG dùng để ra lệnh chạy máy.</b> Đường chạy thật đọc số do chính VisionMaster tính
    /// (Flow <c>AlignBottom_ToolN</c>, Group Cmd) — để sửa được bằng GUI VisionMaster mà không
    /// phải build lại app. Lớp này tồn tại cho ba việc (việc thứ ba: <c>VmCycle.TraceAlignBottom</c> tính lại lệnh từ
    /// chính số Flow trả về rồi ghi vào log KHỚP/LỆCH, chỉ để đối chiếu):</para>
    /// <list type="number">
    /// <item><see cref="ShieldCanAssy.Core.Communication.Sim.FakeVision"/> mô phỏng các Flow đó, nhờ
    /// vậy chuỗi Auto chạy được đầy đủ ở chế độ mô phỏng;</item>
    /// <item><c>--selftest</c> khoá lại chính công thức này bằng dữ liệu biết trước đáp số — nếu
    /// một ngày số của VisionMaster lệch khỏi nó thì lỗi nằm ở cấu hình solution (đơn vị, dấu góc,
    /// gốc toạ độ), không phải ở chỗ khác.</item>
    /// </list>
    /// </summary>
    public static class SinglePointAlignMath
    {
        public static void Solve(double targetX, double targetY, double targetAngleDeg,
                                 double objectX, double objectY, double objectAngleDeg,
                                 out double deltaX, out double deltaY, out double deltaThetaDeg)
        {
            deltaThetaDeg = NormalizeDeg(targetAngleDeg - objectAngleDeg);

            double rad = deltaThetaDeg * Math.PI / 180.0;
            double cos = Math.Cos(rad), sin = Math.Sin(rad);

            // R(Δθ)·đối_tượng — đối tượng đo trong hệ có GỐC ĐẶT TẠI TÂM XOAY nên phép quay này
            // chính là "tâm sản phẩm dịch đi đâu sau khi xoay Δθ".
            double rx = objectX * cos - objectY * sin;
            double ry = objectX * sin + objectY * cos;

            deltaX = targetX - rx;
            deltaY = targetY - ry;
        }

        /// <summary>Đưa góc về khoảng (−180, 180] — xoay 350° và xoay −10° là một, chọn cái ngắn.</summary>
        public static double NormalizeDeg(double deg)
        {
            while (deg > 180.0) deg -= 360.0;
            while (deg <= -180.0) deg += 360.0;
            return deg;
        }
    }
}
