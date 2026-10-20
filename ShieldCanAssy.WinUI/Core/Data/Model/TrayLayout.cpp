#include "Data/Model/TrayLayout.h"

#include "Data/DataJson.h"
#include "Infor/AxisId.h"

#include <cmath>
#include <limits>

namespace sca
{
    // ───────────────────────── CellOffset ─────────────────────────

    bool CellOffset::HasTeachOffset() const
    {
        return std::fabs(Dx) > 1e-9 || std::fabs(Dy) > 1e-9;
    }

    bool CellOffset::HasPlaceTrim() const
    {
        return std::fabs(PlaceDx) > 1e-9 || std::fabs(PlaceDy) > 1e-9 || std::fabs(PlaceDa) > 1e-9;
    }

    // ───────────────────────── TrayGeometry ─────────────────────────

    TeachPoint TrayGeometry::CellPosRaw(int row, int col, int rows, int cols) const
    {
        double ux = 0, uy = 0, vx = 0, vy = 0;

        if (cols > 1)
        {
            ux = (P0C.X - P00.X) / (cols - 1);
            uy = (P0C.Y - P00.Y) / (cols - 1);
        }
        if (rows > 1)
        {
            vx = (PR0.X - P00.X) / (rows - 1);
            vy = (PR0.Y - P00.Y) / (rows - 1);
        }

        return TeachPoint(P00.X + col * ux + row * vx, P00.Y + col * uy + row * vy);
    }

    TeachPoint TrayGeometry::CellPos(int row, int col, int rows, int cols) const
    {
        TeachPoint p = CellPosRaw(row, col, rows, cols);

        if (const CellOffset* off = GetOffset(row, col)) { p.X += off->Dx; p.Y += off->Dy; }

        return p;
    }

    const CellOffset* TrayGeometry::GetOffset(int row, int col) const
    {
        if (Offsets.empty()) return nullptr;
        if (row < 0 || row >= (int)Offsets.size()) return nullptr;
        const auto& line = Offsets[(size_t)row];
        if (col < 0 || col >= (int)line.size()) return nullptr;
        const auto& cell = line[(size_t)col];
        return cell ? &*cell : nullptr;
    }

    CellOffset* TrayGeometry::GetOffset(int row, int col)
    {
        return const_cast<CellOffset*>(static_cast<const TrayGeometry*>(this)->GetOffset(row, col));
    }

    void TrayGeometry::SetOffset(int row, int col, double dx, double dy, int rows, int cols)
    {
        CellOffset& cell = Ensure(row, col, rows, cols);
        cell.Dx = dx;
        cell.Dy = dy;
    }

    void TrayGeometry::SetPlaceTrim(int row, int col, double dx, double dy, double da, int rows, int cols)
    {
        CellOffset& cell = Ensure(row, col, rows, cols);
        cell.PlaceDx = dx;
        cell.PlaceDy = dy;
        cell.PlaceDa = da;
    }

    void TrayGeometry::SetPlaceTrimAll(double dx, double dy, double da)
    {
        PlaceDxAll = dx;
        PlaceDyAll = dy;
        PlaceDaAll = da;
    }

    CellOffset TrayGeometry::PlaceTrim(int row, int col) const
    {
        const CellOffset* off = GetOffset(row, col);
        CellOffset r;
        r.PlaceDx = PlaceDxAll + (off ? off->PlaceDx : 0);
        r.PlaceDy = PlaceDyAll + (off ? off->PlaceDy : 0);
        r.PlaceDa = PlaceDaAll + (off ? off->PlaceDa : 0);
        return r;
    }

    CellOffset& TrayGeometry::Ensure(int row, int col, int rows, int cols)
    {
        EnsureOffsets(rows, cols);
        // .at(): ô ngoài lưới ném std::out_of_range như IndexOutOfRangeException của bản C#.
        auto& cell = Offsets.at((size_t)row).at((size_t)col);
        if (!cell) cell = CellOffset{};
        return *cell;
    }

    void TrayGeometry::EnsureOffsets(int rows, int cols)
    {
        if (!Offsets.empty() && (int)Offsets.size() == rows && (rows == 0 || (int)Offsets[0].size() == cols))
            return;
        Resize(rows, cols);
    }

    void TrayGeometry::Resize(int rows, int cols)
    {
        std::vector<std::vector<std::optional<CellOffset>>> next((size_t)(rows > 0 ? rows : 0));
        for (int r = 0; r < rows; r++)
        {
            next[(size_t)r].resize((size_t)(cols > 0 ? cols : 0));
            for (int c = 0; c < cols; c++)
                if (const CellOffset* old = GetOffset(r, c)) next[(size_t)r][(size_t)c] = *old;
        }

        Offsets = std::move(next);
    }

    bool TrayGeometry::VerifyGrid(int rows, int cols, double threshold, double& error) const
    {
        error = 0;
        if (!PRC_Verify.Taught) return true;

        TeachPoint expect = CellPos(rows - 1, cols - 1, rows, cols);
        double dx = expect.X - PRC_Verify.X;
        double dy = expect.Y - PRC_Verify.Y;
        error = std::sqrt(dx * dx + dy * dy);
        return error <= threshold;
    }

    // ───────────────────────── TrayLayout ─────────────────────────

    TrayGeometry& TrayLayout::GeometryFor(std::string_view zAxis)
    {
        return zAxis == AxisId::Z1 ? GeoZ1 : GeoZ2;
    }

    const TrayGeometry& TrayLayout::GeometryFor(std::string_view zAxis) const
    {
        return zAxis == AxisId::Z1 ? GeoZ1 : GeoZ2;
    }

    double TrayLayout::ZPickFor(std::string_view zAxis) const
    {
        return zAxis == AxisId::Z1 ? Z_PickZ1 : Z_PickZ2;
    }

    double TrayLayout::ZSlowFor(std::string_view zAxis) const
    {
        return zAxis == AxisId::Z1 ? Z_SlowZ1 : Z_SlowZ2;
    }

    bool TrayLayout::ZSlowValid(std::string_view zAxis) const
    {
        double slow = ZSlowFor(zAxis);
        return slow > 0 && slow < ZPickFor(zAxis);
    }

    TeachPoint TrayLayout::CamPosFor(int row, int col) const
    {
        TeachPoint cell = CellPos(CamGridZAxis, row, col);
        TeachPoint cell00 = CellPos(CamGridZAxis, 0, 0);
        return TeachPoint(CamTopDownAnchorX + (cell.X - cell00.X),
                          CamTopDownAnchorY + (cell.Y - cell00.Y));
    }

    double TrayLayout::HalfColPitch() const
    {
        if (Cols() < 2) return std::numeric_limits<double>::infinity();
        double ux = (GeoZ1.P0C.X - GeoZ1.P00.X) / (Cols() - 1);
        double uy = (GeoZ1.P0C.Y - GeoZ1.P00.Y) / (Cols() - 1);
        return 0.5 * std::sqrt(ux * ux + uy * uy);
    }

    void TrayLayout::Resize(int rows, int cols)
    {
        rows = Clamp(rows);
        cols = Clamp(cols);

        m_rows = rows;
        m_cols = cols;
        GeoZ1.Resize(rows, cols);
        GeoZ2.Resize(rows, cols);
    }

    std::shared_ptr<TrayLayout> TrayLayout::CreateInputDefault(std::string_view name)
    {
        auto t = std::make_shared<TrayLayout>();
        t->Name = std::string(name);
        t->SetRows(4);
        t->SetCols(5);
        t->AxisRow = AxisId::Y1;
        t->AxisCol = AxisId::X;
        return t;
    }

    std::shared_ptr<TrayLayout> TrayLayout::CreateAssyDefault()
    {
        auto t = std::make_shared<TrayLayout>();
        t->Name = "AssyTray";
        t->SetRows(5);
        t->SetCols(6);
        t->AxisRow = AxisId::Y2;
        t->AxisCol = AxisId::X;
        return t;
    }

    // ───────────────────────── JSON (dạng Newtonsoft của model) ─────────────────────────

    namespace datajson
    {
        using jsonx::Get;

        json ToJson(const TeachPoint& p)
        {
            json j = json::object();
            j["X"] = Num(p.X);
            j["Y"] = Num(p.Y);
            j["Taught"] = p.Taught;
            return j;
        }

        void FromJson(const json& j, TeachPoint& p)
        {
            GetNum(j, "X", p.X);
            GetNum(j, "Y", p.Y);
            Get(j, "Taught", p.Taught);
        }

        json ToJson(const CellOffset& c)
        {
            json j = json::object();
            j["Dx"] = Num(c.Dx);
            j["Dy"] = Num(c.Dy);
            j["PlaceDx"] = Num(c.PlaceDx);
            j["PlaceDy"] = Num(c.PlaceDy);
            j["PlaceDa"] = Num(c.PlaceDa);
            // Property chỉ-đọc — Newtonsoft vẫn ghi, khi đọc bỏ qua.
            j["HasTeachOffset"] = c.HasTeachOffset();
            j["HasPlaceTrim"] = c.HasPlaceTrim();
            j["IsZero"] = c.IsZero();
            return j;
        }

        void FromJson(const json& j, CellOffset& c)
        {
            GetNum(j, "Dx", c.Dx);
            GetNum(j, "Dy", c.Dy);
            GetNum(j, "PlaceDx", c.PlaceDx);
            GetNum(j, "PlaceDy", c.PlaceDy);
            GetNum(j, "PlaceDa", c.PlaceDa);
        }

        namespace
        {
            void ReadPoint(const json& j, const char* key, TeachPoint& p)
            {
                auto it = j.find(key);
                if (it != j.end() && it->is_object()) FromJson(*it, p);
            }
        }

        json ToJson(const TrayGeometry& g)
        {
            json j = json::object();
            j["P00"] = ToJson(g.P00);
            j["P0C"] = ToJson(g.P0C);
            j["PR0"] = ToJson(g.PR0);
            j["PRC_Verify"] = ToJson(g.PRC_Verify);
            if (!g.Offsets.empty())   // null → bỏ khoá (NullValueHandling.Ignore); phần tử null trong mảng vẫn ghi null
            {
                json rows = json::array();
                for (const auto& line : g.Offsets)
                {
                    json cells = json::array();
                    for (const auto& cell : line) cells.push_back(cell ? ToJson(*cell) : json(nullptr));
                    rows.push_back(std::move(cells));
                }
                j["Offsets"] = std::move(rows);
            }
            j["PlaceDxAll"] = Num(g.PlaceDxAll);
            j["PlaceDyAll"] = Num(g.PlaceDyAll);
            j["PlaceDaAll"] = Num(g.PlaceDaAll);
            return j;
        }

        void FromJson(const json& j, TrayGeometry& g)
        {
            ReadPoint(j, "P00", g.P00);
            ReadPoint(j, "P0C", g.P0C);
            ReadPoint(j, "PR0", g.PR0);
            ReadPoint(j, "PRC_Verify", g.PRC_Verify);
            if (auto it = j.find("Offsets"); it != j.end())
            {
                if (it->is_null()) g.Offsets.clear();
                else if (it->is_array())
                {
                    std::vector<std::vector<std::optional<CellOffset>>> rows;
                    for (const auto& line : *it)
                    {
                        std::vector<std::optional<CellOffset>> cells;   // dòng null → dòng rỗng (GetOffset trả nullptr)
                        if (line.is_array())
                        {
                            for (const auto& cell : line)
                            {
                                if (cell.is_object())
                                {
                                    CellOffset c;
                                    FromJson(cell, c);
                                    cells.emplace_back(c);
                                }
                                else cells.emplace_back(std::nullopt);
                            }
                        }
                        rows.push_back(std::move(cells));
                    }
                    g.Offsets = std::move(rows);
                }
            }
            GetNum(j, "PlaceDxAll", g.PlaceDxAll);
            GetNum(j, "PlaceDyAll", g.PlaceDyAll);
            GetNum(j, "PlaceDaAll", g.PlaceDaAll);
        }

        json ToJson(const TrayLayout& t)
        {
            json j = json::object();
            j["Name"] = t.Name;
            j["Rows"] = t.Rows();
            j["Cols"] = t.Cols();
            j["AxisRow"] = t.AxisRow;
            j["AxisCol"] = t.AxisCol;
            j["GeoZ1"] = ToJson(t.GeoZ1);
            j["GeoZ2"] = ToJson(t.GeoZ2);
            j["VerifyThreshold"] = Num(t.VerifyThreshold);
            j["Z_PickZ1"] = Num(t.Z_PickZ1);
            j["Z_PickZ2"] = Num(t.Z_PickZ2);
            j["Z_SlowZ1"] = Num(t.Z_SlowZ1);
            j["Z_SlowZ2"] = Num(t.Z_SlowZ2);
            j["Z_Safe"] = Num(t.Z_Safe);
            j["ScanOrder"] = (int)t.ScanOrder;
            j["CamTopDownAnchorX"] = Num(t.CamTopDownAnchorX);
            j["CamTopDownAnchorY"] = Num(t.CamTopDownAnchorY);
            j["CamTopDownAnchorTaught"] = t.CamTopDownAnchorTaught;
            j["CellCount"] = t.CellCount();
            j["HalfColPitch"] = Num(t.HalfColPitch());   // một cột → +∞ → "Infinity" như Newtonsoft
            j["IsTaught"] = t.IsTaught();
            return j;
        }

        void FromJson(const json& j, TrayLayout& t)
        {
            Get(j, "Name", t.Name);
            int rows = t.Rows(), cols = t.Cols();
            Get(j, "Rows", rows);
            Get(j, "Cols", cols);
            t.SetRows(rows);   // setter kẹp [MinDim, MaxDim] như property C#
            t.SetCols(cols);
            Get(j, "AxisRow", t.AxisRow);
            Get(j, "AxisCol", t.AxisCol);
            if (auto it = j.find("GeoZ1"); it != j.end() && it->is_object()) FromJson(*it, t.GeoZ1);
            if (auto it = j.find("GeoZ2"); it != j.end() && it->is_object()) FromJson(*it, t.GeoZ2);
            GetNum(j, "VerifyThreshold", t.VerifyThreshold);
            GetNum(j, "Z_PickZ1", t.Z_PickZ1);
            GetNum(j, "Z_PickZ2", t.Z_PickZ2);
            GetNum(j, "Z_SlowZ1", t.Z_SlowZ1);
            GetNum(j, "Z_SlowZ2", t.Z_SlowZ2);
            GetNum(j, "Z_Safe", t.Z_Safe);
            GetEnum(j, "ScanOrder", t.ScanOrder);
            GetNum(j, "CamTopDownAnchorX", t.CamTopDownAnchorX);
            GetNum(j, "CamTopDownAnchorY", t.CamTopDownAnchorY);
            Get(j, "CamTopDownAnchorTaught", t.CamTopDownAnchorTaught);
        }
    }
}
