#include "SysConfig/UserAccess.h"

#include "Base/Text.h"
#include "Lang/Loc.h"
#include "SysConfig/AppPaths.h"
#include "Utility/DateTimeText.h"
#include "Utility/JsonHelper.h"
#include "Utility/Log/LogWorker.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace sca
{
    namespace fs = std::filesystem;
    using jsonx::json;

    const char* ToString(EUserLevel level)
    {
        switch (level)
        {
        case EUserLevel::Engineer: return "Engineer";
        case EUserLevel::Master:   return "Master";
        default:                   return "Operator";
        }
    }

    namespace
    {
        // PBKDF2-HMAC-SHA256: 50 000 vòng ≈ vài chục ms một lần đăng nhập — đủ chậm để dò mật khẩu từ file băm, không ai
        // thấy trễ khi bấm Login. Số vòng ghi kèm từng mật khẩu nên tăng về sau vẫn đọc được bản cũ.
        constexpr int HashIterations = 50000;
        constexpr size_t SaltBytes = 16;
        constexpr size_t HashBytes = 32;

        struct Secret
        {
            std::vector<uint8_t> Salt, Hash;
            int Iterations{ HashIterations };
            bool IsDefault{ true };
            std::string ChangedAt;   // ISO; rỗng = chưa đổi lần nào

            bool Valid() const { return Salt.size() == SaltBytes && Hash.size() == HashBytes && Iterations > 0; }
        };

        struct State
        {
            std::mutex mutex;   // engineer, idleMin, ghi file
            Secret engineer;
            int idleMin{ UserAccess::DefaultIdleLogoutMin };
            std::atomic<int> level{ (int)EUserLevel::Operator };
            std::atomic<uint64_t> version{ 1 };
        };

        State& S()
        {
            static State s;
            return s;
        }

        void Log(const std::string& message, LogLevel level = LogLevel::Info)
        {
            LogWorker::Instance().RecordLog(message, level, "User");
        }

        std::string ToHex(const std::vector<uint8_t>& bytes)
        {
            static const char* digits = "0123456789abcdef";
            std::string s;
            s.reserve(bytes.size() * 2);
            for (uint8_t b : bytes)
            {
                s.push_back(digits[b >> 4]);
                s.push_back(digits[b & 0x0F]);
            }
            return s;
        }

        bool FromHex(const std::string& hex, std::vector<uint8_t>& out)
        {
            auto nibble = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            if (hex.size() % 2 != 0) return false;
            out.clear();
            for (size_t i = 0; i < hex.size(); i += 2)
            {
                int hi = nibble(hex[i]), lo = nibble(hex[i + 1]);
                if (hi < 0 || lo < 0) return false;
                out.push_back((uint8_t)(hi * 16 + lo));
            }
            return true;
        }

        bool RandomBytes(std::vector<uint8_t>& out, size_t count)
        {
            out.assign(count, 0);
            return BCRYPT_SUCCESS(BCryptGenRandom(nullptr, out.data(), (ULONG)count, BCRYPT_USE_SYSTEM_PREFERRED_RNG));
        }

        bool Derive(std::string_view password, const std::vector<uint8_t>& salt, int iterations, std::vector<uint8_t>& out)
        {
            BCRYPT_ALG_HANDLE alg = nullptr;
            if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG)))
                return false;
            out.assign(HashBytes, 0);
            // Mật khẩu rỗng: CNG vẫn cần con trỏ khác null.
            static const char empty = 0;
            auto* pw = reinterpret_cast<PUCHAR>(const_cast<char*>(password.empty() ? &empty : password.data()));
            NTSTATUS status = BCryptDeriveKeyPBKDF2(alg, pw, (ULONG)password.size(), const_cast<PUCHAR>(salt.data()), (ULONG)salt.size(),
                                                    (ULONGLONG)iterations, out.data(), (ULONG)out.size(), 0);
            BCryptCloseAlgorithmProvider(alg, 0);
            return BCRYPT_SUCCESS(status);
        }

        bool MakeSecret(std::string_view password, bool isDefault, Secret& result)
        {
            Secret s;
            s.Iterations = HashIterations;
            s.IsDefault = isDefault;
            if (!RandomBytes(s.Salt, SaltBytes) || !Derive(password, s.Salt, s.Iterations, s.Hash)) return false;
            if (!isDefault) s.ChangedAt = ToIsoString(std::chrono::system_clock::now());
            result = std::move(s);
            return true;
        }

        bool Verify(const Secret& s, std::string_view password)
        {
            if (!s.Valid()) return false;
            std::vector<uint8_t> hash;
            if (!Derive(password, s.Salt, s.Iterations, hash) || hash.size() != s.Hash.size()) return false;
            uint8_t diff = 0;   // so đủ mọi byte — thời gian không lộ byte sai đầu tiên
            for (size_t i = 0; i < hash.size(); ++i) diff |= (uint8_t)(hash[i] ^ s.Hash[i]);
            return diff == 0;
        }

        /// <summary>
        /// Mật khẩu Master cố định (Tuấn 2026-10-29) — chỉ giữ bản băm, cùng công thức với mật khẩu Engineer. Đổi mật khẩu
        /// Master = tính lại muối + băm rồi build lại, vd. PowerShell:
        /// <c>$s = New-Object byte[] 16; [Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($s);
        /// (New-Object Security.Cryptography.Rfc2898DeriveBytes("&lt;mật khẩu&gt;", $s, 50000,
        /// [Security.Cryptography.HashAlgorithmName]::SHA256)).GetBytes(32)</c> (in hai mảng dạng hex). Sửa cả câu kiểm
        /// trong InfraTests (TestUserAccess).
        /// </summary>
        const Secret& MasterSecret()
        {
            static const Secret s = [] {
                Secret m;
                FromHex("9c6f149348b6cd680bc1c2d247b2ca0e", m.Salt);
                FromHex("01a400cc61cea6c208109c3124b47b3c24d67ac110279325718b03ccc2eda0c7", m.Hash);
                m.Iterations = HashIterations;
                m.IsDefault = false;
                return m;
            }();
            return s;
        }

        json ToJson(const Secret& s)
        {
            json j;
            j["Salt"] = ToHex(s.Salt);
            j["Hash"] = ToHex(s.Hash);
            j["Iterations"] = s.Iterations;
            j["IsDefault"] = s.IsDefault;
            if (!s.ChangedAt.empty()) j["ChangedAt"] = s.ChangedAt;
            return j;
        }

        bool FromJson(const json& j, Secret& s)
        {
            if (!j.is_object()) return false;
            std::string salt, hash;
            jsonx::Get(j, "Salt", salt);
            jsonx::Get(j, "Hash", hash);
            jsonx::Get(j, "Iterations", s.Iterations);
            jsonx::Get(j, "IsDefault", s.IsDefault);
            jsonx::Get(j, "ChangedAt", s.ChangedAt);
            return FromHex(salt, s.Salt) && FromHex(hash, s.Hash) && s.Valid();
        }

        /// <summary>Ghi users.json qua file tạm rồi thay thế (như McDataManager). Gọi khi đang giữ khoá.</summary>
        bool SaveLocked(State& st, std::string& error)
        {
            json root;
            root["IdleLogoutMin"] = st.idleMin;
            root["Passwords"]["Engineer"] = ToJson(st.engineer);
            try
            {
                fs::path path = AppPaths::UsersFile();
                fs::create_directories(path.parent_path());
                fs::path tmp = path;
                tmp += ".tmp";
                if (!base::WriteAllText(tmp, "\xEF\xBB\xBF" + jsonx::Dump(root))) throw std::runtime_error("cannot write " + base::PathToU8(tmp));
                if (fs::exists(path)) fs::remove(path);
                fs::rename(tmp, path);
                return true;
            }
            catch (const std::exception& ex)
            {
                error = ex.what();
                Log("Cannot save users.json: " + error, LogLevel::Error);
                return false;
            }
        }
    }

    void UserAccess::Load()
    {
        auto& st = S();
        std::lock_guard<std::mutex> lock(st.mutex);
        st.level = (int)EUserLevel::Operator;

        fs::path path = AppPaths::UsersFile();
        bool ok = false;
        std::error_code ec;
        if (fs::exists(path, ec))
        {
            try
            {
                std::string text;
                json root;
                if (base::ReadAllText(path, text) && jsonx::ParseObject(text, root))
                {
                    int idle = DefaultIdleLogoutMin;
                    jsonx::Get(root, "IdleLogoutMin", idle);
                    st.idleMin = std::clamp(idle, MinIdleLogoutMin, MaxIdleLogoutMin);
                    auto passwords = root.find("Passwords");
                    Secret engineer;
                    if (passwords != root.end() && passwords->is_object())
                        if (auto it = passwords->find("Engineer"); it != passwords->end() && FromJson(*it, engineer))
                        {
                            st.engineer = std::move(engineer);
                            ok = true;
                        }
                }
            }
            catch (...) { ok = false; }

            if (!ok)
            {
                // Giữ lại file hỏng để còn tra — không ghi đè mất dấu vết.
                fs::path bad = path;
                bad += ".bad";
                fs::remove(bad, ec);
                fs::rename(path, bad, ec);
                Log("users.json unreadable — renamed to users.json.bad, Engineer password back to default", LogLevel::Warning);
            }
        }

        if (!ok)
        {
            st.idleMin = DefaultIdleLogoutMin;
            if (!MakeSecret(DefaultEngineerPassword, true, st.engineer))
                Log("Password hashing failed (Windows CNG) — Engineer login unavailable", LogLevel::Error);
            std::string error;
            if (SaveLocked(st, error))
                Log("users.json created — Engineer password is the default, change it in Setting", LogLevel::Warning);
        }
        st.version++;
    }

    EUserLevel UserAccess::Level() { return (EUserLevel)S().level.load(); }

    bool UserAccess::IsAtLeast(EUserLevel level) { return S().level.load() >= (int)level; }

    bool UserAccess::Login(std::string_view password)
    {
        auto& st = S();
        EUserLevel level = EUserLevel::Operator;
        if (Verify(MasterSecret(), password)) level = EUserLevel::Master;
        else
        {
            std::lock_guard<std::mutex> lock(st.mutex);
            if (Verify(st.engineer, password)) level = EUserLevel::Engineer;
        }
        if (level == EUserLevel::Operator)
        {
            Log("Login failed — wrong password", LogLevel::Warning);
            return false;
        }
        st.level = (int)level;
        st.version++;
        Log(std::string("Login: ") + ToString(level));
        return true;
    }

    void UserAccess::Logout(const std::string& reason)
    {
        auto& st = S();
        int previous = st.level.exchange((int)EUserLevel::Operator);
        if (previous == (int)EUserLevel::Operator) return;
        st.version++;
        Log(std::string("Logout: ") + ToString((EUserLevel)previous) + (reason.empty() ? std::string() : " — " + reason));
    }

    bool UserAccess::LogoutIfIdle(std::chrono::milliseconds idle)
    {
        if (Level() == EUserLevel::Operator) return false;
        int minutes = IdleLogoutMin();
        if (idle < std::chrono::minutes(minutes)) return false;
        Logout("idle " + std::to_string(minutes) + " min");
        return true;
    }

    int UserAccess::IdleLogoutMin()
    {
        auto& st = S();
        std::lock_guard<std::mutex> lock(st.mutex);
        return st.idleMin;
    }

    std::string UserAccess::SetIdleLogoutMin(int minutes)
    {
        if (!IsAtLeast(EUserLevel::Engineer)) return Loc::T("User.EngineerRequired");
        if (minutes < MinIdleLogoutMin || minutes > MaxIdleLogoutMin)
            return Loc::F("User.IdleRange", MinIdleLogoutMin, MaxIdleLogoutMin);

        auto& st = S();
        std::lock_guard<std::mutex> lock(st.mutex);
        if (st.idleMin == minutes) return {};
        int old = st.idleMin;
        st.idleMin = minutes;
        std::string error;
        if (!SaveLocked(st, error))
        {
            st.idleMin = old;
            return Loc::F("User.SaveFailed", error);
        }
        st.version++;
        Log("Auto logout: " + std::to_string(old) + " → " + std::to_string(minutes) + " min");
        return {};
    }

    std::string UserAccess::ChangePassword(EUserLevel level, std::string_view newPassword)
    {
        if (level != EUserLevel::Engineer) return Loc::F("User.NoPassword", ToString(level));
        if (!IsAtLeast(EUserLevel::Engineer)) return Loc::T("User.EngineerRequired");
        if ((int)newPassword.size() < MinPasswordLength) return Loc::F("User.PasswordTooShort", MinPasswordLength);
        if (Verify(MasterSecret(), newPassword)) return Loc::T("User.PasswordIsMaster");

        Secret secret;
        if (!MakeSecret(newPassword, newPassword == DefaultEngineerPassword, secret)) return Loc::T("User.HashFailed");

        auto& st = S();
        std::lock_guard<std::mutex> lock(st.mutex);
        Secret old = st.engineer;
        st.engineer = std::move(secret);
        std::string error;
        if (!SaveLocked(st, error))
        {
            st.engineer = std::move(old);
            return Loc::F("User.SaveFailed", error);
        }
        st.version++;
        Log(std::string(ToString(level)) + " password changed");
        return {};
    }

    bool UserAccess::IsDefaultPassword(EUserLevel level)
    {
        if (level != EUserLevel::Engineer) return false;
        auto& st = S();
        std::lock_guard<std::mutex> lock(st.mutex);
        return st.engineer.IsDefault;
    }

    std::string UserAccess::PasswordChangedAt(EUserLevel level)
    {
        if (level != EUserLevel::Engineer) return {};
        auto& st = S();
        std::lock_guard<std::mutex> lock(st.mutex);
        TimePoint t;
        if (st.engineer.ChangedAt.empty() || !TryParseDateTime(st.engineer.ChangedAt, t)) return {};
        return FormatLocal(t, "dd/MM/yyyy HH:mm");
    }

    uint64_t UserAccess::Version() { return S().version.load(); }
}
