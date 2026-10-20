#pragma once

namespace sca
{
    /// <summary>
    /// Bản sao phép toán module <i>Single Point Alignment</i> (单点对位) trong VisionMaster 4.4.0 —
    /// Help "Calculation › Single Point Alignment":
    ///
    /// <code>
    /// Δθ      = chuẩn hoá(θ_đích − θ_đối_tượng)          (Help: chuẩn hoá về −π…π)
    /// (Δx,Δy) = đích − R(Δθ)·đối_tượng
    /// </code>
    ///
    /// <para><b>KHÔNG dùng để ra lệnh chạy máy.</b> Đường chạy thật đọc số do chính VisionMaster tính
    /// (Flow <c>AlignBottom_ToolN</c>, Group Cmd) — để sửa được bằng GUI VisionMaster mà không phải
    /// build lại app. Lớp này tồn tại cho ba việc: (1) <c>FakeVision</c> mô phỏng các Flow đó, nhờ vậy
    /// chuỗi Auto chạy được đầy đủ ở chế độ mô phỏng; (2) bài tự kiểm khoá lại chính công thức này bằng
    /// dữ liệu biết trước đáp số — nếu một ngày số của VisionMaster lệch khỏi nó thì lỗi nằm ở cấu hình
    /// solution (đơn vị, dấu góc, gốc toạ độ), không phải ở chỗ khác; (3) <c>VmCycle.TraceAlignBottom</c>
    /// tính lại lệnh từ chính số Flow trả về rồi ghi vào log KHỚP/LỆCH, chỉ để đối chiếu.</para>
    /// </summary>
    class SinglePointAlignMath
    {
    public:
        static void Solve(double targetX, double targetY, double targetAngleDeg,
                          double objectX, double objectY, double objectAngleDeg,
                          double& deltaX, double& deltaY, double& deltaThetaDeg);

        /// <summary>Đưa góc về khoảng (−180, 180] — xoay 350° và xoay −10° là một, chọn cái ngắn.</summary>
        static double NormalizeDeg(double deg);
    };
}
