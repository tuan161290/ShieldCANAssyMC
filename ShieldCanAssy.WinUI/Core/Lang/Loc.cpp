#include "Lang/Loc.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace sca
{
    namespace
    {
        struct Entry { const char* key; const char* text; };

        const Entry kStrings[] = {
#include "Lang/StringsEn.inc"
        };

        const std::unordered_map<std::string_view, std::string_view>& Table()
        {
            static const std::unordered_map<std::string_view, std::string_view> table = [] {
                std::unordered_map<std::string_view, std::string_view> t;
                t.reserve(sizeof(kStrings) / sizeof(kStrings[0]));
                for (const auto& e : kStrings) t.emplace(e.key, e.text);
                return t;
            }();
            return table;
        }

        // ── Chữ số thập phân của một double theo cách .NET Framework: 15 chữ số có nghĩa, rồi làm tròn
        //    "nửa lên" (xa số 0) trên CHUỖI chữ số đó — nên 2.675 với "0.00" ra 2.68 như bản C#, không ra
        //    2.67 như printf("%.2f"). ──
        struct Digits
        {
            std::string intPart;   // không dấu, không số 0 thừa đầu ("0" nếu < 1)
            std::string fracPart;  // đúng `decimals` chữ số
        };

        Digits RoundToDecimals(double absValue, int decimals)
        {
            Digits d;
            if (!(absValue > 0) || !std::isfinite(absValue))
            {
                d.intPart = "0";
                d.fracPart.assign(decimals, '0');
                return d;
            }
            char buf[64];
            std::snprintf(buf, sizeof buf, "%.14e", absValue);   // d.dddddddddddddde±XX
            std::string mant;
            mant.push_back(buf[0]);
            const char* p = buf + 2;
            while (*p && *p != 'e') mant.push_back(*p++);
            int exp = std::atoi(p + 1);
            // Giá trị = 0.mant × 10^(exp+1). Vị trí của chữ số đầu tiên bị bỏ = (exp+1) + decimals.
            int pointPos = exp + 1;              // số chữ số trước dấu chấm (có thể ≤ 0)
            int keep = pointPos + decimals;      // số chữ số của mant được giữ
            std::string digits = mant;
            if (keep < 0)
            {
                d.intPart = "0";
                d.fracPart.assign(decimals, '0');
                return d;
            }
            if (keep < (int)digits.size())
            {
                bool up = digits[keep] >= '5';
                digits.resize(keep);
                if (up)
                {
                    int i = keep - 1;
                    while (i >= 0)
                    {
                        if (digits[i] == '9') { digits[i] = '0'; --i; }
                        else { ++digits[i]; break; }
                    }
                    if (i < 0) { digits.insert(digits.begin(), '1'); ++pointPos; }
                }
            }
            else
            {
                digits.append(keep - digits.size(), '0');
            }
            // Ghép lại theo pointPos.
            if (pointPos <= 0)
            {
                d.intPart = "0";
                std::string frac(-pointPos, '0');
                frac += digits;
                frac.resize(decimals, '0');
                d.fracPart = frac;
            }
            else
            {
                d.intPart = digits.substr(0, pointPos);
                d.fracPart = digits.substr(pointPos);
                d.fracPart.resize(decimals, '0');
                size_t nz = d.intPart.find_first_not_of('0');
                d.intPart = nz == std::string::npos ? "0" : d.intPart.substr(nz);
            }
            return d;
        }

        bool AllZero(const Digits& d)
        {
            return d.intPart.find_first_not_of('0') == std::string::npos &&
                   d.fracPart.find_first_not_of('0') == std::string::npos;
        }

        std::string Group3(const std::string& s)
        {
            std::string out;
            int n = (int)s.size();
            for (int i = 0; i < n; ++i)
            {
                out.push_back(s[i]);
                int rest = n - 1 - i;
                if (rest > 0 && rest % 3 == 0) out.push_back(',');
            }
            return out;
        }

        // ── Định dạng tuỳ biến: "0.000", "+0.0;-0.0", "0.###", "#,##0.0", hằng chữ trong '...' / "..." / \x ──
        struct Section
        {
            std::string prefix, suffix;
            int minInt = 0;
            int minFrac = 0, maxFrac = 0;
            bool group = false;
            bool percent = false;
        };

        Section ParseSection(std::string_view s)
        {
            Section sec;
            enum { Pre, Int, Frac, Post } st = Pre;
            for (size_t i = 0; i < s.size(); ++i)
            {
                char c = s[i];
                std::string* lit = (st == Pre) ? &sec.prefix : &sec.suffix;
                if (c == '\'' || c == '"')
                {
                    size_t j = s.find(c, i + 1);
                    if (j == std::string_view::npos) j = s.size();
                    std::string text(s.substr(i + 1, j - i - 1));
                    if (st == Int || st == Frac) st = Post, lit = &sec.suffix;
                    *lit += text;
                    i = j;
                    continue;
                }
                if (c == '\\' && i + 1 < s.size())
                {
                    if (st == Int || st == Frac) st = Post, lit = &sec.suffix;
                    lit->push_back(s[++i]);
                    continue;
                }
                if (c == '0' || c == '#')
                {
                    if (st == Pre) st = Int;
                    if (st == Int) { if (c == '0') ++sec.minInt; }
                    else if (st == Frac) { ++sec.maxFrac; if (c == '0') sec.minFrac = sec.maxFrac; }
                    else sec.suffix.push_back(c);
                    continue;
                }
                if (c == '.')
                {
                    if (st == Pre || st == Int) { st = Frac; continue; }
                    lit->push_back(c);
                    continue;
                }
                if (c == ',')
                {
                    if (st == Int) { sec.group = true; continue; }
                    lit->push_back(c);
                    continue;
                }
                if (c == '%') sec.percent = true;
                if (st == Int || st == Frac) st = Post, lit = &sec.suffix;
                lit->push_back(c);
            }
            return sec;
        }

        std::string ApplySection(const Section& sec, const Digits& d, bool minus)
        {
            std::string ip = d.intPart;
            if (ip == "0" && sec.minInt == 0) ip.clear();
            if ((int)ip.size() < sec.minInt) ip.insert(ip.begin(), sec.minInt - ip.size(), '0');
            if (sec.group) ip = Group3(ip);
            std::string fp = d.fracPart;
            while ((int)fp.size() > sec.minFrac && !fp.empty() && fp.back() == '0') fp.pop_back();
            std::string out;
            if (minus) out.push_back('-');
            out += sec.prefix;
            out += ip;
            if (!fp.empty()) { out.push_back('.'); out += fp; }
            out += sec.suffix;
            return out;
        }

        std::vector<std::string_view> SplitSections(std::string_view spec)
        {
            std::vector<std::string_view> parts;
            size_t start = 0;
            char quote = 0;
            for (size_t i = 0; i < spec.size(); ++i)
            {
                char c = spec[i];
                if (quote) { if (c == quote) quote = 0; continue; }
                if (c == '\'' || c == '"') { quote = c; continue; }
                if (c == '\\') { ++i; continue; }
                if (c == ';') { parts.push_back(spec.substr(start, i - start)); start = i + 1; }
            }
            parts.push_back(spec.substr(start));
            return parts;
        }

        std::string FormatCustom(double value, std::string_view spec)
        {
            auto parts = SplitSections(spec);
            if (parts.size() >= 2 && value < 0)
            {
                Section neg = ParseSection(parts[1]);
                double v = -value * (neg.percent ? 100.0 : 1.0);
                Digits d = RoundToDecimals(v, neg.maxFrac);
                if (!AllZero(d)) return ApplySection(neg, d, false);
                Section pos = ParseSection(parts[0]);
                return ApplySection(pos, RoundToDecimals(0, pos.maxFrac), false);
            }
            if (parts.size() >= 3 && value == 0)
            {
                Section z = ParseSection(parts[2]);
                return ApplySection(z, RoundToDecimals(0, z.maxFrac), false);
            }
            Section sec = ParseSection(parts[0]);
            double v = std::fabs(value) * (sec.percent ? 100.0 : 1.0);
            Digits d = RoundToDecimals(v, sec.maxFrac);
            return ApplySection(sec, d, value < 0 && !AllZero(d));
        }

        // .NET Framework double.ToString() / "G": 15 chữ số có nghĩa, bỏ số 0 thừa, dạng mũ khi số mũ < -5 hoặc ≥ 15.
        std::string FormatG(double v, int precision = 15)
        {
            if (std::isnan(v)) return "NaN";
            if (std::isinf(v)) return v > 0 ? "Infinity" : "-Infinity";
            if (v == 0) return "0";
            char buf[64];
            std::snprintf(buf, sizeof buf, "%.*g", precision, v);
            std::string s = buf;
            size_t e = s.find('e');
            if (e != std::string::npos)
            {
                // printf: 1.5e-07 → .NET: 1.5E-07
                std::string mant = s.substr(0, e);
                std::string ex = s.substr(e + 1);
                char sign = ex[0] == '-' ? '-' : '+';
                if (ex[0] == '+' || ex[0] == '-') ex = ex.substr(1);
                while (ex.size() > 2 && ex[0] == '0') ex.erase(ex.begin());
                if (ex.size() < 2) ex.insert(ex.begin(), '0');
                s = mant + "E" + sign + ex;
            }
            return s;
        }

        std::string FormatFixed(double v, int decimals, bool group)
        {
            Digits d = RoundToDecimals(std::fabs(v), decimals);
            std::string ip = group ? Group3(d.intPart) : d.intPart;
            std::string out;
            if (v < 0 && !AllZero(d)) out.push_back('-');
            out += ip;
            if (decimals > 0) { out.push_back('.'); out += d.fracPart; }
            return out;
        }

        bool IsStandardSpec(std::string_view spec, char& kind, int& prec)
        {
            if (spec.empty()) return false;
            char c = spec[0];
            if (!std::strchr("FfNnDdXxGgPpEe", c)) return false;
            for (size_t i = 1; i < spec.size(); ++i)
                if (spec[i] < '0' || spec[i] > '9') return false;
            kind = c;
            prec = spec.size() > 1 ? std::atoi(std::string(spec.substr(1)).c_str()) : -1;
            return true;
        }

        std::string FormatNumber(double v, bool isInteger, long long iv, unsigned long long uv, bool isUnsigned,
                                 std::string_view spec)
        {
            char kind = 0;
            int prec = -1;
            if (IsStandardSpec(spec, kind, prec))
            {
                switch (kind)
                {
                case 'F': case 'f': return FormatFixed(v, prec < 0 ? 2 : prec, false);
                case 'N': case 'n': return FormatFixed(v, prec < 0 ? 2 : prec, true);
                case 'P': case 'p': return FormatFixed(v * 100.0, prec < 0 ? 2 : prec, true) + " %";
                case 'G': case 'g': return FormatG(v, prec <= 0 ? 15 : prec);
                case 'E': case 'e':
                {
                    char buf[64];
                    std::snprintf(buf, sizeof buf, kind == 'E' ? "%.*E" : "%.*e", prec < 0 ? 6 : prec, v);
                    std::string s = buf;   // printf: 1.5E+07 (2+ chữ số mũ) → .NET: 1.5E+007 (3 chữ số)
                    size_t e = s.find_first_of("Ee");
                    if (e != std::string::npos && s.size() - e - 2 < 3) s.insert(e + 2, 3 - (s.size() - e - 2), '0');
                    return s;
                }
                case 'D': case 'd':
                {
                    if (!isInteger) break;
                    std::string digits = isUnsigned ? std::to_string(uv) : std::to_string(iv < 0 ? -(iv + 1) + 1ULL : (unsigned long long)iv);
                    if (prec > (int)digits.size()) digits.insert(digits.begin(), prec - digits.size(), '0');
                    return (!isUnsigned && iv < 0 ? "-" : "") + digits;
                }
                case 'X': case 'x':
                {
                    if (!isInteger) break;
                    unsigned long long u = isUnsigned ? uv : (unsigned long long)iv;
                    if (!isUnsigned && iv < 0) u &= 0xFFFFFFFFULL;   // Int32 âm của C# in dạng bù hai 32 bit
                    char buf[32];
                    std::snprintf(buf, sizeof buf, kind == 'X' ? "%llX" : "%llx", u);
                    std::string s = buf;
                    if (prec > (int)s.size()) s.insert(s.begin(), prec - s.size(), '0');
                    return s;
                }
                default: break;
                }
            }
            if (spec.empty())
            {
                if (isInteger) return isUnsigned ? std::to_string(uv) : std::to_string(iv);
                return FormatG(v);
            }
            return FormatCustom(v, spec);
        }

        // Ngày giờ: dd MM yyyy HH mm ss fff — các ký tự khác giữ nguyên (FormatLocal).
        std::string FormatTime(TimePoint t, std::string_view spec)
        {
            if (spec.empty()) return FormatLocal(t, "dd/MM/yyyy HH:mm:ss");
            return FormatLocal(t, spec);
        }
    }

    std::string Loc::T(std::string_view key)
    {
        if (key.empty()) return {};
        const auto& t = Table();
        auto it = t.find(key);
        return it == t.end() ? std::string(key) : std::string(it->second);
    }

    bool Loc::Has(std::string_view key)
    {
        const auto& t = Table();
        return t.find(key) != t.end();
    }

    std::string Loc::FormatValue(const FmtArg& a, std::string_view spec)
    {
        if (auto p = std::get_if<long long>(&a.v)) return FormatNumber((double)*p, true, *p, 0, false, spec);
        if (auto p = std::get_if<unsigned long long>(&a.v)) return FormatNumber((double)*p, true, 0, *p, true, spec);
        if (auto p = std::get_if<double>(&a.v)) return FormatNumber(*p, false, 0, 0, false, spec);
        if (auto p = std::get_if<bool>(&a.v)) return *p ? "True" : "False";
        if (auto p = std::get_if<TimePoint>(&a.v)) return FormatTime(*p, spec);
        return std::get<std::string>(a.v);
    }

    std::string Loc::Format(std::string_view fmt, std::initializer_list<FmtArg> args)
    {
        const FmtArg* argv = args.begin();
        const int argc = (int)args.size();
        std::string out;
        out.reserve(fmt.size() + 16);
        for (size_t i = 0; i < fmt.size(); ++i)
        {
            char c = fmt[i];
            if (c == '{')
            {
                if (i + 1 < fmt.size() && fmt[i + 1] == '{') { out.push_back('{'); ++i; continue; }
                size_t close = fmt.find('}', i + 1);
                if (close == std::string_view::npos) return std::string(fmt);
                std::string_view item = fmt.substr(i + 1, close - i - 1);
                std::string_view spec;
                size_t colon = item.find(':');
                if (colon != std::string_view::npos) { spec = item.substr(colon + 1); item = item.substr(0, colon); }
                int width = 0;
                size_t comma = item.find(',');
                if (comma != std::string_view::npos)
                {
                    width = std::atoi(std::string(item.substr(comma + 1)).c_str());
                    item = item.substr(0, comma);
                }
                if (item.empty() || item.find_first_not_of(" 0123456789") != std::string_view::npos) return std::string(fmt);
                int index = std::atoi(std::string(item).c_str());
                if (index < 0 || index >= argc) return std::string(fmt);   // như C#: FormatException → trả nguyên mẫu
                std::string s = FormatValue(argv[index], spec);
                // Độ rộng tính theo ký tự (code point), không theo byte UTF-8.
                int chars = 0;
                for (unsigned char ch : s) if ((ch & 0xC0) != 0x80) ++chars;
                if (width > chars) s.insert(s.begin(), width - chars, ' ');
                else if (-width > chars) s.append(-width - chars, ' ');
                out += s;
                i = close;
                continue;
            }
            if (c == '}')
            {
                if (i + 1 < fmt.size() && fmt[i + 1] == '}') { out.push_back('}'); ++i; continue; }
                return std::string(fmt);
            }
            out.push_back(c);
        }
        return out;
    }
}
