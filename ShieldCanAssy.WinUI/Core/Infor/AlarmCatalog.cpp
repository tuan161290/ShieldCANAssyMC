#include "Infor/AlarmCatalog.h"

#include "Base/Text.h"
#include "Infor/AlarmCode.h"
#include "SysConfig/AppPaths.h"
#include "Utility/Log/LogWorker.h"
#include "nlohmann/json.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <vector>

namespace sca
{
    namespace fs = std::filesystem;
    using json = nlohmann::ordered_json;

    namespace
    {
        /// <summary>Dictionary giữ thứ tự chèn (như Dictionary của .NET khi chưa xoá phần tử).</summary>
        using Section = std::vector<std::pair<std::string, AlarmDefinition>>;

        /// <summary>Cấu trúc file alarms.json: _comment, axisFaults (khoá "0".."8" — offset AxisFault, {AXIS} thay
        /// bằng tên trục lúc phát; nhờ mẫu này bảng chỉ có 9 dòng thay vì 63 dòng lặp lại cho 7 trục), codes (khoá
        /// là chuỗi số "1100").</summary>
        struct CatalogFile
        {
            std::optional<std::string> Comment;
            Section AxisFaults;
            Section Codes;
        };

        struct State
        {
            std::mutex mutex;
            CatalogFile file;
            bool loaded{ false };
        };

        State& S()
        {
            static State s;
            return s;
        }

        void Log(const std::string& msg, LogLevel level)
        {
            LogWorker::Instance().RecordLog(msg, level, "Alarm");
        }

        AlarmDefinition* Find(Section& sec, const std::string& key)
        {
            for (auto& kv : sec) if (kv.first == key) return &kv.second;
            return nullptr;
        }

        EAlarmLevel ParseLevel(const json& j)
        {
            if (j.is_number_integer()) return (EAlarmLevel)j.get<int>();
            if (j.is_string())
            {
                // StringEnumConverter: tên enum không phân biệt hoa thường, hoặc chuỗi số.
                std::string s = base::ToLower(base::Trim(j.get<std::string>()));
                if (s == "info") return EAlarmLevel::Info;
                if (s == "warning") return EAlarmLevel::Warning;
                if (s == "error") return EAlarmLevel::Error;
                if (s == "fatal") return EAlarmLevel::Fatal;
                int n = 0;
                auto r = std::from_chars(s.data(), s.data() + s.size(), n);
                if (r.ec == std::errc() && r.ptr == s.data() + s.size() && !s.empty()) return (EAlarmLevel)n;
            }
            throw std::runtime_error("alarms.json: invalid level value: " + j.dump());
        }

        void ReadText(const json& j, const char* key, std::string& field)
        {
            auto it = j.find(key);
            if (it != j.end() && it->is_string()) field = it->get<std::string>();
        }

        void ReadSection(const json& root, const char* key, Section& sec)
        {
            auto it = root.find(key);
            if (it == root.end() || !it->is_object()) return;
            for (auto e = it->begin(); e != it->end(); ++e)
            {
                if (!e.value().is_object()) continue;   // null → như "không có bản ghi" (C# coi mine == null là thiếu)
                AlarmDefinition def;
                auto lv = e.value().find("level");
                if (lv != e.value().end() && !lv->is_null()) def.Level = ParseLevel(*lv);
                ReadText(e.value(), "vi", def.Vi);
                ReadText(e.value(), "en", def.En);
                ReadText(e.value(), "cause", def.Cause);
                ReadText(e.value(), "causeEn", def.CauseEn);
                ReadText(e.value(), "remedy", def.Remedy);
                ReadText(e.value(), "remedyEn", def.RemedyEn);
                if (auto* existing = Find(sec, e.key())) *existing = def;
                else sec.emplace_back(e.key(), def);
            }
        }

        /// <summary>Đọc file; nullopt khi nội dung là JSON null/rỗng. JSON hỏng thì ném.</summary>
        std::optional<CatalogFile> ReadFile(const fs::path& path)
        {
            std::string text;
            if (!base::ReadAllText(path, text)) throw std::runtime_error("cannot open " + base::PathToU8(path));
            if (base::IsBlank(text)) return std::nullopt;

            json root = json::parse(text);
            if (root.is_null()) return std::nullopt;
            if (!root.is_object()) throw std::runtime_error("alarms.json: root is not an object");

            CatalogFile f;
            auto c = root.find("_comment");
            if (c != root.end() && c->is_string()) f.Comment = c->get<std::string>();
            ReadSection(root, "axisFaults", f.AxisFaults);
            ReadSection(root, "codes", f.Codes);
            return f;
        }

        bool MergeSection(Section& user, const Section& tpl, const char* what)
        {
            bool changed = false;
            int filledEn = 0;
            for (const auto& kv : tpl)
            {
                AlarmDefinition* mine = Find(user, kv.first);
                if (mine == nullptr)
                {
                    user.emplace_back(kv.first, kv.second);
                    changed = true;
                    Log(std::format("alarms.json: added {} {} from the new version's template", what, kv.first), LogLevel::Info);
                    continue;
                }

                // Bản tiếng Anh (2026-10-09): file của máy có từ trước chưa có causeEn/remedyEn — chỉ điền ô TRỐNG, chữ
                // người dùng đã viết thì giữ nguyên.
                bool filled = false;
                if (mine->En.empty() && !kv.second.En.empty()) { mine->En = kv.second.En; filled = true; }
                if (mine->CauseEn.empty() && !kv.second.CauseEn.empty()) { mine->CauseEn = kv.second.CauseEn; filled = true; }
                if (mine->RemedyEn.empty() && !kv.second.RemedyEn.empty()) { mine->RemedyEn = kv.second.RemedyEn; filled = true; }
                if (filled) { filledEn++; changed = true; }

                if (mine->Level != kv.second.Level)
                {
                    Log(std::format("alarms.json: {} {} takes level {} from the software (file had {}) — alarm level is "
                                    "machine behaviour, not editable via file",
                                    what, kv.first, ToString(kv.second.Level), ToString(mine->Level)),
                        LogLevel::Warning);
                    mine->Level = kv.second.Level;
                    changed = true;
                }
            }

            if (filledEn > 0)
                Log(std::format("alarms.json: filled empty English text of {} {} from the template", filledEn, what), LogLevel::Info);
            return changed;
        }

        /// <summary>
        /// Hoà bản mẫu vào file của người dùng. Trả về true nếu có thay đổi cần ghi lại.
        ///
        /// <para>Không có bước này thì mã alarm thêm ở phiên bản mới KHÔNG BAO GIỜ tới được máy
        /// đã chạy — file Config đã tồn tại nên nhánh chép bản mẫu không chạy nữa, và alarm mới
        /// sẽ hiện ra dạng "chưa định nghĩa" đúng lúc người vận hành cần đọc cách xử lý nhất.</para>
        /// </summary>
        bool Merge(CatalogFile& user, const CatalogFile& tpl)
        {
            bool changed = false;
            changed |= MergeSection(user.Codes, tpl.Codes, "code");
            changed |= MergeSection(user.AxisFaults, tpl.AxisFaults, "axis fault template");
            return changed;
        }

        bool TryInt(const std::string& s, int& v)
        {
            auto r = std::from_chars(s.data(), s.data() + s.size(), v);
            return !s.empty() && r.ec == std::errc() && r.ptr == s.data() + s.size();
        }

        Section SortByCode(Section src)
        {
            std::stable_sort(src.begin(), src.end(), [](const auto& a, const auto& b) {
                int x, y;
                bool ax = TryInt(a.first, x), by = TryInt(b.first, y);
                if (ax && by) return x < y;
                return a.first < b.first;   // string.CompareOrdinal
            });
            return src;
        }

        /// <summary>Newtonsoft mặc định (không NullValueHandling.Ignore) ghi chuỗi null thành null — chuỗi rỗng bên
        /// C++ đứng vai null.</summary>
        json TextOrNull(const std::string& s)
        {
            return s.empty() ? json(nullptr) : json(s);
        }

        json SectionToJson(const Section& sec)
        {
            json j = json::object();
            for (const auto& kv : sec)
            {
                json d = json::object();
                d["level"] = ToString(kv.second.Level);
                d["vi"] = TextOrNull(kv.second.Vi);
                d["en"] = TextOrNull(kv.second.En);
                d["cause"] = TextOrNull(kv.second.Cause);
                d["causeEn"] = TextOrNull(kv.second.CauseEn);
                d["remedy"] = TextOrNull(kv.second.Remedy);
                d["remedyEn"] = TextOrNull(kv.second.RemedyEn);
                j[kv.first] = std::move(d);
            }
            return j;
        }

        /// <summary>Ghi UTF-8 CÓ BOM: không có BOM thì Notepad và PowerShell trên Windows đọc file theo bảng
        /// mã ANSI và toàn bộ tiếng Việt thành ký tự rác — mà sửa lời văn bằng Notepad ngay trên máy chính là lý
        /// do file này tồn tại.</summary>
        void WriteWithBom(const fs::path& path, const std::string& text)
        {
            if (!base::WriteAllText(path, "\xEF\xBB\xBF" + text))
                throw std::runtime_error("cannot write " + base::PathToU8(path));
        }

        void Save(const fs::path& path, CatalogFile& file)
        {
            try
            {
                // Sắp lại theo thứ tự mã để file đọc được sau khi bổ sung, thay vì dồn mã mới
                // xuống cuối theo thứ tự nạp.
                CatalogFile ordered;
                ordered.Comment = file.Comment;
                ordered.Codes = SortByCode(file.Codes);
                ordered.AxisFaults = SortByCode(file.AxisFaults);

                json root = json::object();
                if (ordered.Comment) root["_comment"] = *ordered.Comment; else root["_comment"] = nullptr;
                root["axisFaults"] = SectionToJson(ordered.AxisFaults);
                root["codes"] = SectionToJson(ordered.Codes);
                std::string text = root.dump(2, ' ', false, json::error_handler_t::replace);
                WriteWithBom(path, base::Replace(text, "\n", "\r\n"));   // Formatting.Indented trên Windows: CRLF
                file = std::move(ordered);
            }
            catch (const std::exception& ex)
            {
                // Ghi không được thì vẫn chạy tiếp bằng bản đã hoà trong bộ nhớ.
                Log(std::string("Cannot rewrite alarms.json: ") + ex.what(), LogLevel::Warning);
            }
        }

        std::string Fill(const std::string& s, const std::string& axis)
        {
            return s.empty() ? s : base::Replace(s, "{AXIS}", axis);
        }

        void Commit(CatalogFile file, bool loaded)
        {
            auto& s = S();
            std::lock_guard<std::mutex> lock(s.mutex);
            s.file = std::move(file);
            s.loaded = loaded;
        }
    }

    bool AlarmCatalog::Loaded()
    {
        auto& s = S();
        std::lock_guard<std::mutex> lock(s.mutex);
        return s.loaded;
    }

    void AlarmCatalog::Load()
    {
        fs::path target = AppPaths::AlarmsFile();
        fs::path templ = AppPaths::ExeDir() / "Config" / "alarms.json";
        std::error_code ec;

        try
        {
            if (!fs::exists(target, ec) && fs::exists(templ, ec))
            {
                // Chép qua đường đọc-ghi chứ không File.Copy, để bản nằm trên máy chạy LUÔN
                // có BOM. Không có BOM thì Notepad và PowerShell trên Windows đọc file theo
                // bảng mã ANSI và toàn bộ tiếng Việt thành ký tự rác — mà sửa lời văn bằng
                // Notepad ngay trên máy chính là lý do file này tồn tại.
                fs::create_directories(target.parent_path());
                std::string text;
                if (!base::ReadAllText(templ, text)) throw std::runtime_error("cannot read " + base::PathToU8(templ));
                WriteWithBom(target, text);
            }

            if (fs::exists(target, ec))
            {
                CatalogFile file = ReadFile(target).value_or(CatalogFile{});
                Commit(file, true);

                std::optional<CatalogFile> tpl;
                if (fs::exists(templ, ec)) tpl = ReadFile(templ);
                if (tpl && Merge(file, *tpl)) Save(target, file);
                Commit(file, true);

                Log(std::format("Alarm table loaded: {} codes + {} axis fault templates", file.Codes.size(), file.AxisFaults.size()),
                    LogLevel::Info);
                return;
            }

            Log("alarms.json not found — alarms will show as bare codes", LogLevel::Warning);
        }
        catch (const std::exception& ex)
        {
            Log(std::string("alarms.json read error: ") + ex.what(), LogLevel::Error);
        }

        Commit(CatalogFile{}, false);
    }

    AlarmDefinition AlarmCatalog::Get(int code)
    {
        {
            auto& s = S();
            std::lock_guard<std::mutex> lock(s.mutex);

            if (auto* def = Find(s.file.Codes, std::to_string(code))) return *def;

            std::string axis;
            AxisFault fault;
            if (AlarmCode::TryParseAxis(code, axis, fault))
            {
                if (auto* def = Find(s.file.AxisFaults, std::to_string((int)fault)))
                {
                    AlarmDefinition d;
                    d.Level = def->Level;
                    d.Vi = Fill(def->Vi, axis);
                    d.En = Fill(def->En, axis);
                    d.Cause = Fill(def->Cause, axis);
                    d.CauseEn = Fill(def->CauseEn, axis);
                    d.Remedy = Fill(def->Remedy, axis);
                    d.RemedyEn = Fill(def->RemedyEn, axis);
                    return d;
                }
            }
        }

        AlarmDefinition d;
        d.Level = EAlarmLevel::Error;
        d.Vi = std::format("Alarm chưa định nghĩa: {}", code);
        d.En = std::format("Undefined alarm: {}", code);
        d.Cause = "Mã alarm này chưa có trong Config\\alarms.json.";
        d.CauseEn = "This alarm code is not in Config\\alarms.json.";
        d.Remedy = "Bổ sung mã vào alarms.json rồi khởi động lại app.";
        d.RemedyEn = "Add the code to alarms.json, then restart the app.";
        return d;
    }
}
