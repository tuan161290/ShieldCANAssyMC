#pragma once
// JSON của các kiểu dữ liệu lồng nhau (AxisConfig, TrayLayout, TrayState…) theo đúng dạng Newtonsoft của bản C#:
// khoá = tên property (PascalCase), enum = số, DateTime = ISO, khoá null bị bỏ (McDataManager dùng
// NullValueHandling.Ignore), property chỉ-đọc (CellCount, IsTaught…) VẪN ghi ra như Newtonsoft — khi đọc thì bỏ qua.
// CHỈ include từ file .cpp (json.hpp nặng). Thiếu khoá / null → giữ mặc định; sai kiểu ở một trường → bỏ trường đó.
#include "Utility/JsonHelper.h"

#include "Base/Text.h"
#include "Communication/AxisConfig.h"
#include "Data/Model/TrayLayout.h"
#include "Data/RunningData.h"

#include <cmath>
#include <initializer_list>
#include <limits>
#include <string>

namespace sca::datajson
{
    using jsonx::json;

    /// <summary>double như Newtonsoft (FloatFormatHandling.String mặc định): NaN/±∞ ghi thành chuỗi "NaN"/"Infinity"/
    /// "-Infinity" thay vì null.</summary>
    inline json Num(double v)
    {
        if (std::isnan(v)) return json("NaN");
        if (std::isinf(v)) return json(v > 0 ? "Infinity" : "-Infinity");
        return json(v);
    }

    /// <summary>Đọc double: số, hoặc chuỗi "NaN"/"Infinity"/"-Infinity" (như Newtonsoft). Thiếu/null/sai kiểu → giữ.</summary>
    inline void GetNum(const json& j, const char* key, double& field)
    {
        auto it = j.find(key);
        if (it == j.end() || it->is_null()) return;
        if (it->is_number()) { field = it->get<double>(); return; }
        if (it->is_string())
        {
            const std::string& s = it->get_ref<const std::string&>();
            if (s == "NaN") field = std::numeric_limits<double>::quiet_NaN();
            else if (s == "Infinity") field = std::numeric_limits<double>::infinity();
            else if (s == "-Infinity") field = -std::numeric_limits<double>::infinity();
        }
    }

    /// <summary>Tên enum → giá trị, không phân biệt hoa thường (Newtonsoft đọc được tên enum dạng chuỗi).</summary>
    template <class E>
    bool ParseName(const std::string& s, E& v, std::initializer_list<E> values)
    {
        std::string t = base::Trim(s);
        for (E e : values)
            if (base::EqualsIgnoreCase(t, ToString(e))) { v = e; return true; }
        return false;
    }

    inline bool ParseName(const std::string& s, EAxisKind& v)
    {
        return ParseName(s, v, { EAxisKind::Panasonic, EAxisKind::Fastech });
    }
    inline bool ParseName(const std::string& s, EScanOrder& v)
    {
        return ParseName(s, v, { EScanOrder::RowMajor, EScanOrder::ColMajor, EScanOrder::Serpentine });
    }
    inline bool ParseName(const std::string& s, ECellState& v)
    {
        return ParseName(s, v, { ECellState::Empty, ECellState::Loaded, ECellState::Ok, ECellState::Ng,
                                 ECellState::Processing, ECellState::Skipped });
    }
    inline bool ParseName(const std::string& s, EAutoState& v)
    {
        return ParseName(s, v, { EAutoState::S00_Ready, EAutoState::S05_ScanAssy, EAutoState::S10_Pick1,
                                 EAutoState::S11_Pick2, EAutoState::S20_Align1, EAutoState::S21_Align2,
                                 EAutoState::S30_Locate1, EAutoState::S31_Place1, EAutoState::S32_Locate2,
                                 EAutoState::S33_Place2, EAutoState::S40_Index, EAutoState::S50_TrayCheck });
    }

    /// <summary>Đọc enum: số (Newtonsoft ghi số) hoặc tên enum dạng chuỗi. Thiếu/null/tên lạ → giữ.</summary>
    template <class E>
    void GetEnum(const json& j, const char* key, E& field)
    {
        auto it = j.find(key);
        if (it == j.end() || it->is_null()) return;
        if (it->is_number_integer()) { field = (E)it->get<int>(); return; }
        if (it->is_string())
        {
            E v{};
            if (ParseName(it->get_ref<const std::string&>(), v)) field = v;
        }
    }

    // ── Communication/AxisConfig.cpp ──
    json ToJson(const AxisConfig& c);
    void FromJson(const json& j, AxisConfig& c);

    // ── Data/Model/TrayLayout.cpp ──
    json ToJson(const TeachPoint& p);
    void FromJson(const json& j, TeachPoint& p);
    json ToJson(const CellOffset& c);
    void FromJson(const json& j, CellOffset& c);
    json ToJson(const TrayGeometry& g);
    void FromJson(const json& j, TrayGeometry& g);
    json ToJson(const TrayLayout& t);
    void FromJson(const json& j, TrayLayout& t);

    // ── Data/RunningData.cpp ──
    json ToJson(const TrayState& t);
    void FromJson(const json& j, TrayState& t);
    json ToJson(const NozzleState& n);
    void FromJson(const json& j, NozzleState& n);

    /// <summary>Text cuối cùng như Newtonsoft Formatting.Indented trên Windows: thụt 2 dấu cách, xuống dòng CRLF.
    /// (Chuỗi JSON không bao giờ chứa ký tự xuống dòng thô — nlohmann thoát thành \n — nên thay toàn bộ là an toàn.)</summary>
    inline std::string DumpIndented(const json& j)
    {
        std::string s = jsonx::Dump(j);
        std::string out;
        out.reserve(s.size() + s.size() / 16);
        for (char c : s)
        {
            if (c == '\n') out += "\r\n";
            else out.push_back(c);
        }
        return out;
    }
}
