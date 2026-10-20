#pragma once
// Chữ hiển thị — bản C++ chỉ có TIẾNG ANH (Tuấn 2026-10-18: "ngôn ngữ hiển thị là tiếng Anh kiểu công nghiệp, ngắn
// gọn"). Bảng chữ ở Lang\StringsEn.inc, cùng bộ khoá với Core.Lang\StringsEn.resx của bản WPF (sinh lần đầu bằng
// tools\gen-strings.ps1, sau đó sửa thẳng trong .inc). Code lõi gọi Loc::T("Khoá") / Loc::F("Khoá", đối số...) đúng chỗ
// bản C# gọi Loc.T / Loc.F, nên port từng dòng không phải nghĩ lại câu chữ.
//
// Thiếu khoá thì trả chính tên khoá — nhìn màn hình là thấy chỗ quên (như bản C#).
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <variant>

#include "Utility/DateTimeText.h"

namespace sca
{
    /// <summary>Một đối số của Loc::F / Loc::Format — số nguyên, số thực, chuỗi, bool hoặc thời điểm.</summary>
    struct FmtArg
    {
        std::variant<long long, unsigned long long, double, std::string, bool, TimePoint> v;

        FmtArg(int x) : v((long long)x) {}
        FmtArg(long x) : v((long long)x) {}
        FmtArg(long long x) : v(x) {}
        FmtArg(unsigned x) : v((unsigned long long)x) {}
        FmtArg(unsigned long x) : v((unsigned long long)x) {}
        FmtArg(unsigned long long x) : v(x) {}
        FmtArg(unsigned short x) : v((unsigned long long)x) {}
        FmtArg(short x) : v((long long)x) {}
        FmtArg(double x) : v(x) {}
        FmtArg(float x) : v((double)x) {}
        FmtArg(bool x) : v(x) {}
        FmtArg(const char* s) : v(std::string(s ? s : "")) {}
        FmtArg(std::string s) : v(std::move(s)) {}
        FmtArg(std::string_view s) : v(std::string(s)) {}
        FmtArg(TimePoint t) : v(t) {}
    };

    class Loc
    {
    public:
        Loc() = delete;

        /// <summary>Chữ của khoá; thiếu khoá thì trả tên khoá.</summary>
        static std::string T(std::string_view key);

        /// <summary>Có khoá trong bảng không.</summary>
        static bool Has(std::string_view key);

        /// <summary>string.Format trên chữ của khoá. Sai số chỗ {n} thì trả nguyên mẫu, không ném.</summary>
        template <class... A>
        static std::string F(std::string_view key, const A&... args)
        {
            return Format(T(key), { FmtArg(args)... });
        }

        /// <summary>
        /// string.Format kiểu .NET với văn hoá bất biến (dấu chấm thập phân): {n}, {n,w}, {n:0.000}, {n:+0.0;-0.0},
        /// {n:0.###}, {n:0}, {n:X}, {n:dd/MM/yyyy HH:mm}, {n:F2}, {n:N0}; "{{" / "}}" là ngoặc thường. Số thực không
        /// có định dạng in như .NET Framework ("G15": 15 chữ số có nghĩa, bỏ số 0 thừa).
        /// </summary>
        static std::string Format(std::string_view fmt, std::initializer_list<FmtArg> args);

        /// <summary>Định dạng MỘT giá trị theo chuỗi định dạng .NET (phần sau dấu ':' trong {n:...}).</summary>
        static std::string FormatValue(const FmtArg& a, std::string_view spec);
    };
}
