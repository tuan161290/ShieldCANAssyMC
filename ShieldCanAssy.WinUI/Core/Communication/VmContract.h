#pragma once
#include <map>
#include <string>
#include <vector>

namespace sca
{
    /// <summary>Camera của một Flow — quyết định đèn nào bật và ảnh gán vào đâu.</summary>
    enum class EVmCamera { None = 0, TopDown = 1, BottomUp = 2 };

    /// <summary>Một lần gọi Flow: ghi biến toàn cục, chạy, đọc output — trong CÙNG một lần giữ khoá lệnh của
    /// thiết bị. Không có Input Params: mọi số App gửi sang đều đi qua biến toàn cục.</summary>
    struct VmCallRequest
    {
        std::string Flow;
        EVmCamera Camera{ EVmCamera::None };

        /// <summary>Biến toàn cục ghi trước khi chạy. Ghi NGHIÊM: tên trống, tên chưa khai trong solution hoặc
        /// không đọc được danh sách biến đều là lỗi, không bỏ qua.</summary>
        std::map<std::string, double> Globals;

        /// <summary>Output bắt buộc — thiếu một cái là lần gọi thất bại.</summary>
        std::vector<std::string> Outputs;

        /// <summary>Output đọc nếu có. Tư thế khớp mẫu rỗng khi không thấy vật, nên nằm ở đây.</summary>
        std::vector<std::string> OptionalOutputs;
    };

    struct VmCallResult
    {
        bool Ok{ false };
        bool TimedOut{ false };
        std::string Error;

        std::map<std::string, double> Values;
        std::vector<std::string> MissingOptional;

        bool Has(const std::string& name) const { return Values.find(name) != Values.end(); }

        /// <summary>Giá trị output; không có thì NaN để phép kiểm số hữu hạn bắt được.</summary>
        double Get(const std::string& name) const;

        /// <summary>Output số nguyên (đi qua float). Không có hoặc không hữu hạn thì <c>INT_MIN</c>
        /// (<c>int.MinValue</c> của bản C#) — không trùng mã hợp lệ nào.</summary>
        int GetInt(const std::string& name) const;

        static VmCallResult Fail(const std::string& why, bool timedOut = false);
    };

    struct VmMath
    {
        /// <summary>Dung sai so echo: số đi App → float32 → Script → float32 → App.</summary>
        static constexpr double EchoTol = 1e-3;

        static bool IsFinite(double v);
        static bool Near(double a, double b, double tol = EchoTol);
        static bool SameInt(double a, int b);
    };

    /// <summary>
    /// RunId của mỗi lần gọi Flow. Bắt đầu ngẫu nhiên để một Flow chạy lại từ phiên trước không trùng
    /// số; quay vòng về 1. Giữ dưới 10 000 000: nếu VisionMaster in số float bằng 7 chữ số khi Script đọc
    /// biến qua chuỗi thì số lớn hơn sẽ mất chữ số.
    /// </summary>
    struct VmRunIds
    {
        static constexpr int Max = 9999999;
        static int Next();
    };

    /// <summary>
    /// Biến toàn cục của hợp đồng — 13 biến, <b>App ghi tất cả</b> (Tuấn chốt 2026-09-13). Bốn biến trục (tên cấu
    /// hình ở trang Setting) và <see cref="RunId"/> ghi trước mọi Flow; tám biến ghi trước
    /// <c>AlignBottom_ToolN</c>; <see cref="IsTriggerLocate"/> ghi trước mọi lần kích cả ba Flow (2026-10-08). Hằng số
    /// hiệu chuẩn chỉ nằm ở model của App — solution không cất bản sao nào, nên không có biến nào theo tay. Tên có số
    /// đo mang đuôi đơn vị <c>_mm</c> / <c>_deg</c> (Tuấn yêu cầu 2026-09-15); tên hằng C# giữ nguyên.
    /// </summary>
    struct VmVars
    {
        static constexpr const char* RunId = "RunId";

        /// <summary>1 = có ô đích, Group Cmd tính lệnh · 0 = chỉ soi (Bước 4).</summary>
        static constexpr const char* HasTarget = "HasTarget";
        static constexpr const char* CellX = "CellX_mm";
        static constexpr const char* CellY = "CellY_mm";
        static constexpr const char* CellA = "CellA_deg";

        /// <summary>Hand-eye và dấu góc của <b>tay đang gọi</b> — App chọn theo tay rồi ghi vào đúng ba biến này.</summary>
        static constexpr const char* HandEyeDx = "HandEyeDx_mm";
        static constexpr const char* HandEyeDy = "HandEyeDy_mm";
        static constexpr const char* AlignSign = "AlignSign";

        /// <summary>
        /// 1 = App kích Flow để soi (LocateTop quét ô, AlignBottom_ToolN soi/đặt), 0 = để nạp điểm hiệu chuẩn (top-down
        /// 9 điểm ở LocateTop, tâm xoay ở AlignBottom_ToolN). Tuấn 2026-10-08: soi và hiệu chuẩn dùng chung module Matching
        /// nên mỗi camera chỉ còn một Flow, Module rẽ nhánh theo biến này — App còn đúng ba Flow. Kiểu <b>int</b> (Module
        /// rẽ nhánh nhận int); App ghi theo đúng kiểu đang khai nên khai float cũng chạy.
        /// </summary>
        static constexpr const char* IsTriggerLocate = "IsTriggerLocate";

        /// <summary>Tám biến App ghi trước AlignBottom_ToolN (ngoài bốn biến trục). (C#: mảng <c>Place</c>.)</summary>
        static const std::vector<std::string>& Place();
    };

    /// <summary>Tên output cấp Flow — trùng cẩm nang v14, Phụ lục "dựng bảy Flow". Tên có số đo mang đuôi đơn vị
    /// <c>_mm</c> / <c>_deg</c> (2026-09-15); tên hằng C# giữ nguyên.</summary>
    struct VmOut
    {
        static constexpr const char* EchoRunId = "EchoRunId";
        static constexpr const char* EchoX = "EchoX_mm";
        static constexpr const char* EchoY2 = "EchoY2_mm";
        static constexpr const char* EchoA = "EchoA_deg";

        // LocateTop
        static constexpr const char* TopFound = "TopFound";
        static constexpr const char* TopX = "TopX_mm";
        static constexpr const char* TopY = "TopY_mm";
        static constexpr const char* TopA = "TopA_deg";
        static constexpr const char* TopRunId = "TopRunId";

        // AlignBottom_ToolN — tư thế sản phẩm
        static constexpr const char* ObjFound = "ObjFound";
        static constexpr const char* ObjX = "ObjX_mm";
        static constexpr const char* ObjY = "ObjY_mm";
        static constexpr const char* ObjA = "ObjA_deg";
        static constexpr const char* ObjRunId = "ObjRunId";

        // AlignBottom_ToolN — Group Cmd, lệnh đặt (khi HasTarget = 1)
        static constexpr const char* Status = "Status";
        static constexpr const char* CmdX = "CmdX_mm";
        static constexpr const char* CmdY2 = "CmdY2_mm";
        static constexpr const char* CmdA = "CmdA_deg";
        static constexpr const char* DiagDA = "DiagDA_deg";
        static constexpr const char* DiagEcc = "DiagEcc_mm";
        static constexpr const char* DiagTarX = "DiagTarX_mm";
        static constexpr const char* DiagTarY = "DiagTarY_mm";

        /// <summary>Output của Echo0 — module đặt trước khớp mẫu nên luôn có, bắt buộc.</summary>
        static const std::vector<std::string>& LocateEcho();
        static const std::vector<std::string>& LocateTopScript();

        static const std::vector<std::string>& AlignEcho();
        static const std::vector<std::string>& AlignBottomScript();
        static const std::vector<std::string>& AlignPlace();
    };

    /// <summary>Mã <c>Status</c>: của Group Cmd trong Flow, và của hai phép kiểm App tự phán (Bước 2, Bước 4).
    /// Cùng một số có thể mang nghĩa khác ở chỗ khác — dùng hàm mô tả đúng chỗ.</summary>
    struct VmStatus
    {
        static constexpr int NoTarget = 0;
        static constexpr int Ok = 1;
        static constexpr int NoConclusion = 2;
        static constexpr int AngleOut = 3;
        static constexpr int EccOut = 4;
        static constexpr int Invalid = 6;
        static constexpr int AxisMoved = 7;
        static constexpr int Code8 = 8;

        /// <summary>Group Cmd trong AlignBottom_ToolN.</summary>
        static std::string Place(int st);

        /// <summary>Bước 4 — App tự phán từ hai lần đo AlignBottom (<c>VmCycle.JudgeAlignSign</c>).</summary>
        static std::string AlignVerify(int st);

        /// <summary>Bước 2 — App tự phán từ ba lần đo LocateTop (<c>VmCycle.JudgeTopDown</c>).</summary>
        static std::string TopDownVerify(int st);
    };
}
