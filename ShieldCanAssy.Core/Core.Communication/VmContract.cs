using System;
using System.Collections.Generic;
using System.Threading;
using ShieldCanAssy.Core.Lang;

namespace ShieldCanAssy.Core.Communication
{
    /// <summary>Camera của một Flow — quyết định đèn nào bật và ảnh gán vào đâu.</summary>
    public enum EVmCamera { None, TopDown, BottomUp }

    /// <summary>Một lần gọi Flow: ghi biến toàn cục, chạy, đọc output — trong CÙNG một lần giữ khoá lệnh của
    /// thiết bị. Không có Input Params: mọi số App gửi sang đều đi qua biến toàn cục.</summary>
    public class VmCallRequest
    {
        public string Flow { get; set; }
        public EVmCamera Camera { get; set; }

        /// <summary>Biến toàn cục ghi trước khi chạy. Ghi NGHIÊM: tên trống, tên chưa khai trong solution hoặc
        /// không đọc được danh sách biến đều là lỗi, không bỏ qua.</summary>
        public IDictionary<string, double> Globals { get; set; } = new Dictionary<string, double>(StringComparer.Ordinal);

        /// <summary>Output bắt buộc — thiếu một cái là lần gọi thất bại.</summary>
        public IList<string> Outputs { get; set; } = new List<string>();

        /// <summary>Output đọc nếu có. Tư thế khớp mẫu rỗng khi không thấy vật, nên nằm ở đây.</summary>
        public IList<string> OptionalOutputs { get; set; } = new List<string>();
    }

    public class VmCallResult
    {
        public bool Ok { get; set; }
        public bool TimedOut { get; set; }
        public string Error { get; set; }

        public Dictionary<string, double> Values { get; } = new Dictionary<string, double>(StringComparer.Ordinal);
        public List<string> MissingOptional { get; } = new List<string>();

        public bool Has(string name) { return name != null && Values.ContainsKey(name); }

        /// <summary>Giá trị output; không có thì NaN để phép kiểm số hữu hạn bắt được.</summary>
        public double Get(string name)
        {
            double v;
            return name != null && Values.TryGetValue(name, out v) ? v : double.NaN;
        }

        /// <summary>Output số nguyên (đi qua float). Không có hoặc không hữu hạn thì <c>int.MinValue</c> —
        /// không trùng mã hợp lệ nào.</summary>
        public int GetInt(string name)
        {
            double v = Get(name);
            return VmMath.IsFinite(v) ? (int)Math.Round(v) : int.MinValue;
        }

        public static VmCallResult Fail(string why, bool timedOut = false)
        {
            return new VmCallResult { Ok = false, Error = why, TimedOut = timedOut };
        }
    }

    public static class VmMath
    {
        /// <summary>Dung sai so echo: số đi App → float32 → Script → float32 → App.</summary>
        public const double EchoTol = 1e-3;

        public static bool IsFinite(double v) { return !double.IsNaN(v) && !double.IsInfinity(v); }

        public static bool Near(double a, double b, double tol = EchoTol)
        {
            return IsFinite(a) && IsFinite(b) && Math.Abs(a - b) <= tol;
        }

        public static bool SameInt(double a, int b) { return IsFinite(a) && Math.Abs(a - b) < 0.5; }
    }

    /// <summary>
    /// RunId của mỗi lần gọi Flow. Bắt đầu ngẫu nhiên để một Flow chạy lại từ phiên trước không trùng
    /// số; quay vòng về 1. Giữ dưới 10 000 000: nếu VisionMaster in số float bằng 7 chữ số khi Script đọc
    /// biến qua chuỗi thì số lớn hơn sẽ mất chữ số.
    /// </summary>
    public static class VmRunIds
    {
        public const int Max = 9999999;
        private static int _last = new Random().Next(1, Max);

        public static int Next()
        {
            while (true)
            {
                int cur = _last;
                int next = cur >= Max ? 1 : cur + 1;
                if (Interlocked.CompareExchange(ref _last, next, cur) == cur) return next;
            }
        }
    }

    /// <summary>
    /// Biến toàn cục của hợp đồng — 13 biến, <b>App ghi tất cả</b> (Tuấn chốt 2026-09-13). Bốn biến trục (tên cấu
    /// hình ở trang Setting) và <see cref="RunId"/> ghi trước mọi Flow; tám biến ghi trước
    /// <c>AlignBottom_ToolN</c>; <see cref="IsTriggerLocate"/> ghi trước mọi lần kích cả ba Flow (2026-10-08). Hằng số hiệu chuẩn chỉ nằm ở model của App — solution không cất bản sao nào,
    /// nên không có biến nào theo tay. Tên có số đo mang đuôi đơn vị <c>_mm</c> / <c>_deg</c> (Tuấn yêu cầu 2026-09-15);
    /// tên hằng C# giữ nguyên.
    /// </summary>
    public static class VmVars
    {
        public const string RunId = "RunId";

        /// <summary>1 = có ô đích, Group Cmd tính lệnh · 0 = chỉ soi (Bước 4).</summary>
        public const string HasTarget = "HasTarget";
        public const string CellX = "CellX_mm";
        public const string CellY = "CellY_mm";
        public const string CellA = "CellA_deg";

        /// <summary>Hand-eye và dấu góc của <b>tay đang gọi</b> — App chọn theo tay rồi ghi vào đúng ba biến này.</summary>
        public const string HandEyeDx = "HandEyeDx_mm";
        public const string HandEyeDy = "HandEyeDy_mm";
        public const string AlignSign = "AlignSign";

        /// <summary>
        /// 1 = App kích Flow để soi (LocateTop quét ô, AlignBottom_ToolN soi/đặt), 0 = để nạp điểm hiệu chuẩn (top-down
        /// 9 điểm ở LocateTop, tâm xoay ở AlignBottom_ToolN). Tuấn 2026-10-08: soi và hiệu chuẩn dùng chung module Matching
        /// nên mỗi camera chỉ còn một Flow, Module rẽ nhánh theo biến này — App còn đúng ba Flow. Kiểu <b>int</b> (Module
        /// rẽ nhánh nhận int); App ghi theo đúng kiểu đang khai nên khai float cũng chạy.
        /// </summary>
        public const string IsTriggerLocate = "IsTriggerLocate";

        /// <summary>Tám biến App ghi trước AlignBottom_ToolN (ngoài bốn biến trục).</summary>
        public static readonly string[] Place =
        {
            RunId, HasTarget, CellX, CellY, CellA, HandEyeDx, HandEyeDy, AlignSign
        };
    }

    /// <summary>Tên output cấp Flow — trùng cẩm nang v14, Phụ lục "dựng bảy Flow". Tên có số đo mang đuôi đơn vị
    /// <c>_mm</c> / <c>_deg</c> (2026-09-15); tên hằng C# giữ nguyên.</summary>
    public static class VmOut
    {
        public const string EchoRunId = "EchoRunId";
        public const string EchoX = "EchoX_mm";
        public const string EchoY2 = "EchoY2_mm";
        public const string EchoA = "EchoA_deg";

        // LocateTop
        public const string TopFound = "TopFound";
        public const string TopX = "TopX_mm";
        public const string TopY = "TopY_mm";
        public const string TopA = "TopA_deg";
        public const string TopRunId = "TopRunId";

        // AlignBottom_ToolN — tư thế sản phẩm
        public const string ObjFound = "ObjFound";
        public const string ObjX = "ObjX_mm";
        public const string ObjY = "ObjY_mm";
        public const string ObjA = "ObjA_deg";
        public const string ObjRunId = "ObjRunId";

        // AlignBottom_ToolN — Group Cmd, lệnh đặt (khi HasTarget = 1)
        public const string Status = "Status";
        public const string CmdX = "CmdX_mm";
        public const string CmdY2 = "CmdY2_mm";
        public const string CmdA = "CmdA_deg";
        public const string DiagDA = "DiagDA_deg";
        public const string DiagEcc = "DiagEcc_mm";
        public const string DiagTarX = "DiagTarX_mm";
        public const string DiagTarY = "DiagTarY_mm";

        /// <summary>Output của Echo0 — module đặt trước khớp mẫu nên luôn có, bắt buộc.</summary>
        public static readonly string[] LocateEcho = { EchoRunId, EchoX, EchoY2 };
        public static readonly string[] LocateTopScript = { TopFound, TopX, TopY, TopA, TopRunId };

        public static readonly string[] AlignEcho = { EchoRunId, EchoX, EchoA };
        public static readonly string[] AlignBottomScript = { ObjFound, ObjX, ObjY, ObjA, ObjRunId };
        public static readonly string[] AlignPlace =
        {
            Status, CmdX, CmdY2, CmdA, DiagDA, DiagEcc, DiagTarX, DiagTarY
        };
    }

    /// <summary>Mã <c>Status</c>: của Group Cmd trong Flow, và của hai phép kiểm App tự phán (Bước 2, Bước 4).
    /// Cùng một số có thể mang nghĩa khác ở chỗ khác — dùng hàm mô tả đúng chỗ.</summary>
    public static class VmStatus
    {
        public const int NoTarget = 0;
        public const int Ok = 1;
        public const int NoConclusion = 2;
        public const int AngleOut = 3;
        public const int EccOut = 4;
        public const int Invalid = 6;
        public const int AxisMoved = 7;
        public const int Code8 = 8;

        /// <summary>Group Cmd trong AlignBottom_ToolN.</summary>
        public static string Place(int st)
        {
            switch (st)
            {
                case NoTarget: return Loc.T("Vm.StPlaceNoTarget");
                case Ok: return Loc.T("Vm.StPlaceOk");
                case Invalid: return Loc.T("Vm.StPlaceInvalid");
                default: return Loc.F("Vm.StPlaceOdd", st);
            }
        }

        /// <summary>Bước 4 — App tự phán từ hai lần đo AlignBottom (<c>VmCycle.JudgeAlignSign</c>).</summary>
        public static string AlignVerify(int st)
        {
            switch (st)
            {
                case Ok: return Loc.T("Vm.StAlignOk");
                case NoConclusion: return Loc.T("Vm.StAlignNoConclusion");
                case AngleOut: return Loc.T("Vm.StAlignAngleOut");
                case EccOut: return Loc.T("Vm.StAlignEccOut");
                case Invalid: return Loc.T("Vm.StNotFinite");
                case AxisMoved: return Loc.T("Vm.StAlignAxisMoved");
                case Code8: return Loc.T("Vm.StAlignTurnSmall");
                default: return Loc.F("Vm.StOdd", st);
            }
        }

        /// <summary>Bước 2 — App tự phán từ ba lần đo LocateTop (<c>VmCycle.JudgeTopDown</c>).</summary>
        public static string TopDownVerify(int st)
        {
            switch (st)
            {
                case Ok: return Loc.T("Vm.StTopOk");
                case NoConclusion: return Loc.T("Vm.StTopStepSmall");
                case EccOut: return Loc.T("Vm.StTopEccOut");
                case Invalid: return Loc.T("Vm.StNotFinite");
                case AxisMoved: return Loc.T("Vm.StTopAxisMoved");
                case Code8: return Loc.T("Vm.StTopWrongSign");
                default: return Loc.F("Vm.StOdd", st);
            }
        }
    }
}
