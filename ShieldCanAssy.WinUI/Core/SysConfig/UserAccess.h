#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace sca
{
    /// <summary>
    /// Cấp người dùng — mật khẩu theo CẤP, không có tài khoản từng người (Tuấn chốt 2026-10-21). Mở app là Operator; chỉ
    /// trang Vision Edit đòi Engineer, mọi trang khác mở cho mọi cấp. Ở Vision Edit, Engineer chỉ xem ba khung CAM, Master
    /// mới sửa job VisionMaster (Tuấn 2026-10-29). PLAN.md §9 dự kiến thêm Technician (Manual, Teaching): khi cần thì chèn
    /// vào GIỮA Operator và Engineer — quyền so theo số (<see cref="UserAccess::IsAtLeast"/>).
    /// </summary>
    enum class EUserLevel { Operator = 0, Engineer = 1, Master = 2 };

    const char* ToString(EUserLevel level);

    /// <summary>
    /// Đăng nhập theo cấp, lưu ở <c>Config\users.json</c> (PLAN.md §8). Mật khẩu chỉ lưu dạng băm PBKDF2-HMAC-SHA256 kèm
    /// muối ngẫu nhiên (Windows CNG), không bao giờ lưu chữ thật. Chưa có file thì tạo với mật khẩu Engineer mặc định
    /// (<see cref="DefaultEngineerPassword"/>), trang Setting nhắc đổi. Quên mật khẩu: xoá users.json rồi mở lại app — về
    /// mặc định.
    ///
    /// <para><b>Mật khẩu Master cố định</b> (Tuấn 2026-10-29: "master password luôn dùng"): không đổi được, không nằm trong
    /// users.json — xoá file vẫn dùng được. Trong exe chỉ có bản băm (UserAccess.cpp, MasterSecret), không có chữ thật.</para>
    ///
    /// <para>Mỗi lần đăng nhập, gõ sai, đăng xuất, đổi mật khẩu hay đổi thời gian tự đăng xuất đều ghi log mục "User".
    /// Gọi được từ luồng bất kỳ.</para>
    /// </summary>
    class UserAccess
    {
    public:
        UserAccess() = delete;

        static constexpr const char* DefaultEngineerPassword = "1234";
        static constexpr int DefaultIdleLogoutMin = 10;
        static constexpr int MinIdleLogoutMin = 1;
        static constexpr int MaxIdleLogoutMin = 120;
        static constexpr int MinPasswordLength = 4;

        /// <summary>Nạp users.json — gọi sau AppPaths::Init; về Operator. Chưa có file, hoặc file hỏng (đổi tên thành
        /// users.json.bad trước), thì tạo file mới với mật khẩu mặc định.</summary>
        static void Load();

        static EUserLevel Level();
        static bool IsAtLeast(EUserLevel level);

        /// <summary>Một ô mật khẩu cho mọi cấp: mật khẩu Master → Master, mật khẩu Engineer → Engineer, trả true. Sai thì
        /// giữ nguyên cấp, ghi log Warning, trả false.</summary>
        static bool Login(std::string_view password);

        /// <summary>Về Operator; <paramref name="reason"/> vào log ("by user", "idle 10 min"…). Đang là Operator thì thôi.</summary>
        static void Logout(const std::string& reason);

        /// <summary>Tự đăng xuất (Tuấn chốt 2026-10-21): đang trên Operator mà không thao tác đủ
        /// <see cref="IdleLogoutMin"/> phút thì về Operator và trả true để UI rời trang bị khoá.</summary>
        static bool LogoutIfIdle(std::chrono::milliseconds idle);

        static int IdleLogoutMin();
        /// <summary>Chỉ Engineer đổi được; ngoài khoảng Min..Max thì từ chối. Trả câu lỗi, rỗng = đã đổi và lưu.</summary>
        static std::string SetIdleLogoutMin(int minutes);

        /// <summary>Đổi mật khẩu của <paramref name="level"/> — chỉ mật khẩu Engineer, chỉ khi đang từ Engineer trở lên;
        /// không nhận đúng mật khẩu Master (gõ vào sẽ lên Master). Trả câu lỗi, rỗng = đã đổi và lưu.</summary>
        static std::string ChangePassword(EUserLevel level, std::string_view newPassword);

        /// <summary>Mật khẩu của cấp vẫn là mặc định (chưa ai đổi).</summary>
        static bool IsDefaultPassword(EUserLevel level);
        /// <summary>Lúc đổi mật khẩu gần nhất, "dd/MM/yyyy HH:mm"; rỗng = chưa đổi lần nào.</summary>
        static std::string PasswordChangedAt(EUserLevel level);

        /// <summary>Tăng mỗi khi cấp, mật khẩu hay thời gian tự đăng xuất đổi — UI so để biết khi nào vẽ lại.</summary>
        static uint64_t Version();

        /// <summary>Chỉ CoreTests: dùng mật khẩu giả thay mật khẩu Master trong tiến trình này (không ghi đi đâu); rỗng = về
        /// bản thật. Chữ thật không được nằm trong bài kiểm nào (Tuấn 2026-11-02).</summary>
        static void SetMasterPasswordForTests(std::string_view password);
        /// <summary>Chỉ CoreTests: bản băm thật trong code đọc được — đổi mật khẩu mà chép sai hex thì bài kiểm báo.</summary>
        static bool MasterSecretValidForTests();
    };
}
