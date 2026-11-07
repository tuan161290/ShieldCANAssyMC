#include "SysConfig/AppUpdate.h"

#include "Base/Text.h"
#include "SysConfig/AppPaths.h"
#include "SysConfig/AppVersion.h"
#include "Utility/JsonHelper.h"
#include "Utility/Log/LogWorker.h"

#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "winhttp.lib")

namespace sca
{
    namespace fs = std::filesystem;
    using jsonx::json;

    namespace
    {
        void Log(const std::string& msg, LogLevel level = LogLevel::Info)
        {
            LogWorker::Instance().RecordLog(msg, level, "Update");
        }

        struct Internet
        {
            HINTERNET h{};
            explicit Internet(HINTERNET v) : h(v) {}
            ~Internet() { if (h) WinHttpCloseHandle(h); }
            Internet(const Internet&) = delete;
            Internet& operator=(const Internet&) = delete;
            explicit operator bool() const { return h != nullptr; }
        };

        std::string Win32Error(const std::string& what) { return std::format("{} failed (error {})", what, GetLastError()); }

        /// <summary>GET <paramref name="url"/>; thân trả về đưa từng khúc cho <paramref name="sink"/> (false = dừng).
        /// WinHTTP tự theo chuyển hướng HTTPS → HTTPS — asset của GitHub chuyển sang objects.githubusercontent.com. Proxy
        /// theo cài đặt Windows. <paramref name="total"/> = Content-Length, −1 nếu máy chủ không báo.</summary>
        bool HttpGet(const std::string& url, const wchar_t* accept, const std::function<bool(const char*, size_t)>& sink,
                     long long& total, int& status, std::stop_token ct, std::string& error)
        {
            total = -1;
            status = 0;
            std::wstring wurl = base::Widen(url);
            wchar_t host[256]{}, path[2048]{}, extra[2048]{};
            URL_COMPONENTS parts{ sizeof(URL_COMPONENTS) };
            parts.lpszHostName = host;
            parts.dwHostNameLength = (DWORD)std::size(host);
            parts.lpszUrlPath = path;
            parts.dwUrlPathLength = (DWORD)std::size(path);
            parts.lpszExtraInfo = extra;
            parts.dwExtraInfoLength = (DWORD)std::size(extra);
            if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts)) { error = "bad URL: " + url; return false; }

            Internet session(WinHttpOpen(L"ShieldCanAssy-Updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                         WINHTTP_NO_PROXY_BYPASS, 0));
            if (!session) { error = Win32Error("WinHttpOpen"); return false; }
            WinHttpSetTimeouts(session.h, 10000, 10000, 30000, 60000);
            Internet connect(WinHttpConnect(session.h, host, parts.nPort, 0));
            if (!connect) { error = Win32Error("connect"); return false; }
            std::wstring object = std::wstring(path) + extra;
            Internet request(WinHttpOpenRequest(connect.h, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0));
            if (!request) { error = Win32Error("open request"); return false; }
            std::wstring headers = std::wstring(L"Accept: ") + accept + L"\r\n";
            if (!WinHttpSendRequest(request.h, headers.c_str(), (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
                !WinHttpReceiveResponse(request.h, nullptr))
            {
                error = Win32Error("request to " + base::Narrow(host));
                return false;
            }

            DWORD code = 0, size = sizeof(code);
            WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                &code, &size, WINHTTP_NO_HEADER_INDEX);
            status = (int)code;
            if (code != 200) { error = std::format("HTTP {} from {}", code, base::Narrow(host)); return false; }
            wchar_t length[32]{};
            DWORD lengthSize = sizeof(length);
            if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, length, &lengthSize,
                                    WINHTTP_NO_HEADER_INDEX))
                total = _wtoll(length);

            std::vector<char> buffer(64 * 1024);
            while (true)
            {
                if (ct.stop_requested()) { error = "cancelled"; return false; }
                DWORD read = 0;
                if (!WinHttpReadData(request.h, buffer.data(), (DWORD)buffer.size(), &read)) { error = Win32Error("read"); return false; }
                if (read == 0) return true;
                if (!sink(buffer.data(), read)) { error = "cannot store the data"; return false; }
            }
        }

        bool GetText(const std::string& url, const wchar_t* accept, std::string& body, int& status, std::string& error)
        {
            long long total = 0;
            body.clear();
            return HttpGet(url, accept, [&](const char* p, size_t n) {
                body.append(p, n);
                return body.size() < 8 * 1024 * 1024;   // trang API / file .sha256 — không bao giờ lớn thế
            }, total, status, {}, error);
        }

        std::wstring Quoted(const fs::path& p)
        {
            std::wstring s = p.wstring();
            while (!s.empty() && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();   // "C:\dir\" nuốt dấu nháy
            return L"\"" + s + L"\"";
        }
    }

    std::string AppUpdate::CurrentVersion() { return SCA_APP_VERSION; }

    int AppUpdate::CompareVersions(std::string_view a, std::string_view b)
    {
        auto parts = [](std::string_view v) {
            std::string s = base::Trim(v);
            if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s.erase(0, 1);
            std::vector<long long> out;
            for (const std::string& part : base::Split(s, '.'))
            {
                long long n = 0;
                for (char c : part)
                {
                    if (c < '0' || c > '9') break;
                    n = n * 10 + (c - '0');
                }
                out.push_back(n);
            }
            return out;
        };
        std::vector<long long> pa = parts(a), pb = parts(b);
        for (size_t i = 0; i < std::max(pa.size(), pb.size()); ++i)
        {
            long long x = i < pa.size() ? pa[i] : 0, y = i < pb.size() ? pb[i] : 0;
            if (x != y) return x < y ? -1 : 1;
        }
        return 0;
    }

    bool AppUpdate::ParseLatestRelease(const std::string& text, ReleaseInfo& out, std::string& error)
    {
        json j;
        try
        {
            if (!jsonx::ParseObject(text, j)) { error = "empty reply from GitHub"; return false; }
        }
        catch (const std::exception& ex)
        {
            error = std::string("bad reply from GitHub: ") + ex.what();
            return false;
        }

        ReleaseInfo r;
        jsonx::Get(j, "tag_name", r.Tag);
        jsonx::Get(j, "name", r.Name);
        jsonx::Get(j, "body", r.Notes);
        jsonx::Get(j, "published_at", r.PublishedAt);
        if (base::IsBlank(r.Tag)) { error = "release has no tag"; return false; }
        r.Version = r.Tag[0] == 'v' || r.Tag[0] == 'V' ? r.Tag.substr(1) : r.Tag;

        auto assets = j.find("assets");
        if (assets == j.end() || !assets->is_array()) { error = "release " + r.Tag + " has no files"; return false; }
        for (const json& a : *assets)
        {
            std::string name;
            jsonx::Get(a, "name", name);
            std::string lower = base::ToLower(name);
            if (!base::StartsWith(lower, "shieldcanassy") || !base::EndsWith(lower, ".zip")) continue;
            r.AssetName = name;
            jsonx::Get(a, "browser_download_url", r.AssetUrl);
            jsonx::Get(a, "size", r.AssetSize);
            std::string digest;
            jsonx::Get(a, "digest", digest);
            if (base::StartsWith(base::ToLower(digest), "sha256:")) r.Sha256 = base::ToLower(digest.substr(7));
            break;
        }
        if (r.AssetName.empty() || r.AssetUrl.empty())
        {
            error = "release " + r.Tag + " has no ShieldCanAssy-*.zip";
            return false;
        }
        for (const json& a : *assets)
        {
            std::string name;
            jsonx::Get(a, "name", name);
            if (base::EqualsIgnoreCase(name, r.AssetName + ".sha256")) jsonx::Get(a, "browser_download_url", r.ShaUrl);
        }
        if (r.Sha256.empty() && r.ShaUrl.empty())
        {
            error = "release " + r.Tag + " has no SHA-256 to check the download";
            return false;
        }
        out = std::move(r);
        return true;
    }

    bool AppUpdate::FetchLatest(ReleaseInfo& out, std::string& error)
    {
        std::string url = std::format("https://api.github.com/repos/{}/releases/latest", Repo);
        std::string body;
        int status = 0;
        if (!GetText(url, L"application/vnd.github+json", body, status, error))
        {
            if (status == 404) error = std::format("no release on GitHub yet ({})", Repo);
            return false;
        }
        return ParseLatestRelease(body, out, error);
    }

    std::string AppUpdate::Sha256File(const fs::path& file)
    {
        std::ifstream in(file, std::ios::binary);
        if (!in) return {};
        BCRYPT_ALG_HANDLE alg{};
        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) return {};
        std::string hex;
        BCRYPT_HASH_HANDLE hash{};
        if (BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0)))
        {
            std::vector<char> buffer(64 * 1024);
            bool ok = true;
            while (ok && in)
            {
                in.read(buffer.data(), (std::streamsize)buffer.size());
                std::streamsize n = in.gcount();
                if (n > 0) ok = BCRYPT_SUCCESS(BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), (ULONG)n, 0));
            }
            unsigned char digest[32]{};
            if (ok && BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, (ULONG)sizeof(digest), 0)))
                for (unsigned char b : digest) hex += std::format("{:02x}", b);
            BCryptDestroyHash(hash);
        }
        BCryptCloseAlgorithmProvider(alg, 0);
        return hex;
    }

    fs::path AppUpdate::WorkDir() { return fs::temp_directory_path() / "ShieldCanAssyUpdate"; }

    bool AppUpdate::Download(const ReleaseInfo& release, const fs::path& file,
                             const std::function<void(long long, long long)>& progress, std::stop_token ct, std::string& error)
    {
        std::error_code ec;
        fs::create_directories(file.parent_path(), ec);

        std::string expected = release.Sha256;
        if (expected.empty())
        {
            std::string text;
            int status = 0;
            if (!GetText(release.ShaUrl, L"application/octet-stream", text, status, error)) return false;
            expected = base::ToLower(base::Trim(text).substr(0, 64));
        }
        if (expected.size() != 64) { error = "bad SHA-256 in release " + release.Tag; return false; }

        Log(std::format("Downloading {} ({} bytes)", release.AssetName, release.AssetSize));
        bool ok;
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            if (!out) { error = "cannot write " + base::PathToU8(file); return false; }
            long long done = 0, total = 0;
            int status = 0;
            ok = HttpGet(release.AssetUrl, L"application/octet-stream", [&](const char* p, size_t n) {
                out.write(p, (std::streamsize)n);
                done += (long long)n;
                if (progress) progress(done, total > 0 ? total : release.AssetSize);
                return (bool)out;
            }, total, status, ct, error);
        }
        if (ok && release.AssetSize > 0 && (long long)fs::file_size(file, ec) != release.AssetSize)
        {
            ok = false;
            error = std::format("download incomplete ({} of {} bytes)", fs::file_size(file, ec), release.AssetSize);
        }
        if (ok && !base::EqualsIgnoreCase(Sha256File(file), expected))
        {
            ok = false;
            error = "SHA-256 of the download does not match the release — file discarded";
        }
        if (!ok)
        {
            fs::remove(file, ec);
            Log("Download failed: " + error, LogLevel::Warning);
            return false;
        }
        Log("Downloaded and verified (SHA-256) " + base::PathToU8(file));
        return true;
    }

    bool AppUpdate::StartUpdater(const fs::path& zip, const std::string& newVersion, std::string& error)
    {
        fs::path script = AppPaths::ExeDir() / "Update" / "Updater.ps1";
        std::error_code ec;
        if (!fs::exists(script, ec)) { error = "missing " + base::PathToU8(script); return false; }
        fs::path work = WorkDir();
        fs::create_directories(work, ec);
        // Chạy bản sao: file cạnh exe sắp bị chính updater chép đè.
        fs::path copy = work / "Updater.ps1";
        fs::copy_file(script, copy, fs::copy_options::overwrite_existing, ec);
        if (ec) { error = "cannot copy Updater.ps1: " + ec.message(); return false; }

        wchar_t system[MAX_PATH]{};
        GetSystemDirectoryW(system, MAX_PATH);
        std::wstring cmd = Quoted(fs::path(system) / "WindowsPowerShell" / "v1.0" / "powershell.exe") +
                           L" -NoProfile -ExecutionPolicy Bypass -File " + Quoted(copy) +
                           L" -ProcessId " + std::to_wstring(GetCurrentProcessId()) +
                           L" -Zip " + Quoted(zip) +
                           L" -AppDir " + Quoted(AppPaths::ExeDir()) +
                           L" -NewVersion " + Quoted(base::U8Path(newVersion)) +
                           L" -OldVersion " + Quoted(base::U8Path(CurrentVersion())) +
                           L" -LogFile " + Quoted(AppPaths::LogDir() / "update.log");
        STARTUPINFOW si{ sizeof(si) };
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, work.c_str(), &si, &pi))
        {
            error = Win32Error("start updater");
            return false;
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        Log(std::format("Updater started for {} → {} — App closes now", CurrentVersion(), newVersion));
        return true;
    }
}
