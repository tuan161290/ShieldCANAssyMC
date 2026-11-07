#pragma once
#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>
#include <string_view>

namespace sca
{
    /// <summary>Một bản phát hành trên GitHub Releases (releases/latest).</summary>
    struct ReleaseInfo
    {
        std::string Tag;          // "v2026.11.8"
        std::string Version;      // "2026.11.8" — tag bỏ chữ v
        std::string Name;
        std::string Notes;        // phần mô tả của Release
        std::string PublishedAt;  // ISO, như GitHub trả về
        std::string AssetName;    // ShieldCanAssy-<số>.zip
        std::string AssetUrl;
        long long AssetSize = 0;
        /// <summary>SHA-256 (hex thường) của zip — trường digest của GitHub; không có thì đọc file .sha256 đi kèm
        /// (ShaUrl).</summary>
        std::string Sha256;
        std::string ShaUrl;
    };

    /// <summary>
    /// Cập nhật App qua GitHub Releases (Tuấn 2026-11-07). tools\Publish-Release.ps1 build App vào thư mục sạch, đóng zip
    /// (chỉ đầu ra của build, không có _RunData) kèm SHA-256 và tạo Release <c>v&lt;số&gt;</c>. App hỏi Release mới nhất
    /// khi bấm Check ở Setting, tải zip, kiểm SHA-256, rồi giao cho <c>Update\Updater.ps1</c> (chạy bản sao trong %TEMP%)
    /// và tự đóng: updater chờ App thoát, chép file mới, giữ bản cũ trong <c>_Update\backup-…</c> và khôi phục nếu App mới
    /// không lên. Mọi hàm mạng đều CHẶN — gọi từ luồng nền.
    /// </summary>
    class AppUpdate
    {
    public:
        AppUpdate() = delete;

        /// <summary>Repo GitHub chứa các Release.</summary>
        static constexpr const char* Repo = "tuan161290/ShieldCANAssyMC";

        /// <summary>SCA_APP_VERSION (Core\SysConfig\AppVersion.h).</summary>
        static std::string CurrentVersion();

        /// <summary>So hai số phiên bản dạng 2026.11.7(.2), bỏ chữ v đầu: &lt;0 nếu a cũ hơn b, 0 bằng nhau, &gt;0 mới hơn.
        /// Phần thiếu tính là 0 (2026.11.7 = 2026.11.7.0).</summary>
        static int CompareVersions(std::string_view a, std::string_view b);

        /// <summary>Đọc JSON của <c>releases/latest</c>. False khi JSON hỏng, không có zip ShieldCanAssy-*.zip, hoặc không
        /// có cách nào kiểm SHA-256 (thiếu digest lẫn file .sha256).</summary>
        static bool ParseLatestRelease(const std::string& json, ReleaseInfo& out, std::string& error);

        /// <summary>Hỏi GitHub Release mới nhất.</summary>
        static bool FetchLatest(ReleaseInfo& out, std::string& error);

        /// <summary>Tải zip của <paramref name="release"/> về <paramref name="file"/> rồi kiểm kích thước và SHA-256 — sai thì
        /// xoá file, trả false. <paramref name="progress"/>(đã tải, tổng) gọi trên luồng tải.</summary>
        static bool Download(const ReleaseInfo& release, const std::filesystem::path& file,
                             const std::function<void(long long, long long)>& progress, std::stop_token ct,
                             std::string& error);

        /// <summary>SHA-256 của file, hex thường; rỗng nếu không đọc được.</summary>
        static std::string Sha256File(const std::filesystem::path& file);

        /// <summary>Thư mục tạm của bản tải về và updater (%TEMP%\ShieldCanAssyUpdate).</summary>
        static std::filesystem::path WorkDir();

        /// <summary>Chép Update\Updater.ps1 (cạnh exe) sang <see cref="WorkDir"/> rồi chạy nó trong cửa sổ console riêng:
        /// chờ tiến trình này thoát, cài <paramref name="zip"/>. App phải tự đóng ngay sau khi hàm trả true.</summary>
        static bool StartUpdater(const std::filesystem::path& zip, const std::string& newVersion, std::string& error);
    };
}
