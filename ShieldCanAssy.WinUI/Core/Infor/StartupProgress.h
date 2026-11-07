#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace sca
{
    enum class EStartupState { Pending, Running, Ok, Failed };

    /// <summary>Một dòng trên dashboard khởi động: một thiết bị hoặc một việc.</summary>
    struct StartupStep
    {
        std::string Group;    // nhãn nhóm ("MOTION")
        std::string Key;      // tên dòng ("Y2")
        std::string Detail;   // địa chỉ thiết bị; lỗi thì là lý do
        int Weight = 1;       // phần của dòng này trên thanh tiến trình
        EStartupState State = EStartupState::Pending;
        double Seconds = 0;   // đang chạy: tới giờ; xong: tổng
    };

    struct StartupSummary
    {
        int Percent = 0;
        int Failed = 0;
        bool AllEnded = false;
    };

    /// <summary>
    /// Tiến trình khởi động cho dashboard (Tuấn 2026-11-02). McControl::Init khai báo danh sách dòng rồi báo từng việc,
    /// UI đọc bản chụp theo nhịp. Khoá không có trong danh sách thì bỏ qua: sau khởi động và trong kiểm thử các đường
    /// kết nối vẫn gọi vào đây mà không có tác dụng gì.
    /// </summary>
    class StartupProgress
    {
    public:
        static constexpr const char* Io = "Ezi-IO";
        static constexpr const char* VisionMaster = "VisionMaster";
        static constexpr const char* Solution = "Solution";
        /// <summary>Do UI báo (ui::CamViews): control VisionMaster nằm ở phía App, Core không biết.</summary>
        static constexpr const char* CamViews = "CAM views";

        static void Reset(std::vector<StartupStep> steps);
        static void Begin(std::string_view key);
        /// <summary><paramref name="detail"/> rỗng thì giữ chữ đang có.</summary>
        static void End(std::string_view key, bool ok, std::string_view detail = {});
        /// <summary>Init ném exception: mọi dòng chưa xong của Core thành lỗi, để dashboard không chờ thứ không bao giờ
        /// tới. Dòng CamViews giữ nguyên, UI vẫn tự báo.</summary>
        static void FailUnfinished(std::string_view reason);

        static std::vector<StartupStep> Snapshot();
        static StartupSummary Summarize(const std::vector<StartupStep>& steps);
    };
}
