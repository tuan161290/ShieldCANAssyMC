#include "Controller/VisionReadout.h"

#include <initializer_list>
#include <mutex>
#include <optional>

#include "Base/Text.h"
#include "Communication/AxisAngle.h"
#include "Data/McDataManager.h"
#include "Infor/AxisId.h"
#include "Lang/Loc.h"

namespace sca
{
    namespace
    {
        /// <summary>Một lần soi đã dựng thành chữ + bảng. Bất biến — chữ và bảng đổi cùng lúc.</summary>
        struct Built
        {
            std::string Text;
            std::shared_ptr<const ReadoutCard> Card;
        };

        struct Slots
        {
            std::mutex Mutex;
            std::shared_ptr<const Built> Tool1, Tool2, Top;
        };

        Slots& S()
        {
            static Slots s;
            return s;
        }

        std::shared_ptr<const Built> Read(const std::shared_ptr<const Built>& slot)
        {
            std::lock_guard<std::mutex> l(S().Mutex);
            return slot;
        }

        void Write(std::shared_ptr<const Built>& slot, std::shared_ptr<const Built> built)
        {
            std::lock_guard<std::mutex> l(S().Mutex);
            slot = std::move(built);
        }

        const std::vector<std::string>& TopColumns()
        {
            static const std::vector<std::string> c = { "X (mm)", "Y (mm)", "θ (°)" };
            return c;
        }

        /// C# F(format, args) = string.Format(InvariantCulture, …).
        std::string F(std::string_view format, std::initializer_list<FmtArg> args) { return Loc::Format(format, args); }

        std::string OneLine(std::string s)
        {
            s = base::Replace(std::move(s), "\r", "");
            return base::Replace(std::move(s), "\n", " ");
        }

        std::string Stamp() { return base::NowString("HH:mm:ss"); }

        std::string CellText(int row, int col) { return row >= 0 ? Loc::F("Readout.Cell", row + 1, col + 1) : std::string(); }

        /// <summary>Lỗi gọi Flow hoặc kết quả sai hợp đồng — không đọc được tư thế.</summary>
        bool IsProblem(const VmCameraRead* read)
        {
            return read == nullptr || !read->CallOk || !read->EchoOk || (!read->VmValid && read->Found);
        }

        /// <summary>Lỗi gọi Flow hoặc kết quả sai hợp đồng, gói một dòng — khung ở trang Main cố định số dòng.</summary>
        std::string Problem(const VmCameraRead* read)
        {
            if (read == nullptr) return Loc::F("Readout.CallError", Loc::T("VmCycle.NoResult"));
            if (!read->CallOk) return Loc::F("Readout.CallError", OneLine(read->CallError));
            return Loc::F("Readout.Invalid", OneLine(read->EchoOk ? read->VmIssue : read->EchoIssue));
        }

        /// <summary>
        /// Đích tách thành từng phần (Tuấn 2026-10-07): Đích = toạ độ teach + bù vision + bù chung + bù riêng, mỗi trục một
        /// hàng. Teach là ô theo lưới teach của đúng tay đặt (X, Y — cùng tâm hàng rào lưới ở Routine_Auto) và góc trục A
        /// lúc chụp (θ). Bù chung, bù riêng là số nhập ở trang Teaching (TrayLayout::PlaceTrim); θ nhân dấu góc của tay —
        /// đúng lượng nó làm trục A quay thêm. Bù vision là phần còn lại. Tính bằng phép trừ nên tổng luôn đúng bằng Đích.
        /// </summary>
        struct BreakdownParts
        {
            double TeachX{ 0 }, TeachY{ 0 }, TeachA{ 0 }, Ax{ 0 }, Ay{ 0 }, Aa{ 0 }, Cx{ 0 }, Cy{ 0 }, Ca{ 0 };

            std::shared_ptr<const ReadoutCard> Card(const VmPlaceCommand& cmd, const std::string& cell, const std::string& note) const
            {
                auto mm = [](double v) { return F("{0:+0.000;-0.000}", { v }); };
                auto deg = [](double v) { return F("{0:+0.00;-0.00}", { v }); };
                auto card = std::make_shared<ReadoutCard>();
                card->Cell = cell;
                card->Rows = {
                    ReadoutRow("X (mm)", { F("{0:0.000}", { TeachX }), mm(cmd.CmdX - TeachX - Ax - Cx), mm(Ax), mm(Cx), F("{0:0.000}", { cmd.CmdX }) }),
                    ReadoutRow("Y (mm)", { F("{0:0.000}", { TeachY }), mm(cmd.CmdY2 - TeachY - Ay - Cy), mm(Ay), mm(Cy), F("{0:0.000}", { cmd.CmdY2 }) }),
                    // Góc trục A có dấu (−180, +180] — Teaching + các phần bù đọc liền thành Đích quanh 0° (Tuấn 2026-10-30).
                    ReadoutRow("θ (°)", { AxisAngle::SignedText(TeachA, 2), deg(cmd.DiagDA - Aa - Ca), deg(Aa), deg(Ca),
                                          AxisAngle::SignedText(cmd.CmdA, 2) }),
                };
                card->Columns = { "Teaching", "+ Vision", Loc::T("Readout.ColCommon"), Loc::T("Readout.ColCellOffset"), Loc::T("Readout.ColTarget") };
                card->Note = note;
                card->IsBreakdown = true;
                return card;
            }
        };

        /// <summary>nullopt khi không có model hoặc ô ngoài lưới. Số lấy từ model lúc soi.</summary>
        std::optional<BreakdownParts> Breakdown(int nozzle, const VmPlaceCommand& cmd, int row, int col)
        {
            auto model = McDataManager::Model();
            if (!model || !model->AssyTray) return std::nullopt;
            const TrayLayout& tray = *model->AssyTray;
            if (row >= tray.Rows() || col >= tray.Cols()) return std::nullopt;
            std::string z = nozzle == 1 ? std::string(AxisId::Z1) : std::string(AxisId::Z2);
            TeachPoint teach = tray.CellPos(z, row, col);
            const TrayGeometry& geo = tray.GeometryFor(z);
            const CellOffset* off = geo.GetOffset(row, col);
            int sign = model->Calib.AlignAngleSign(nozzle);
            if (sign == 0) sign = 1;

            BreakdownParts p;
            p.TeachX = teach.X;
            p.TeachY = teach.Y;
            p.TeachA = AxisAngle::Wrap(cmd.CmdA - cmd.DiagDA);
            p.Ax = geo.PlaceDxAll;
            p.Ay = geo.PlaceDyAll;
            p.Aa = sign * geo.PlaceDaAll;
            p.Cx = off ? off->PlaceDx : 0;
            p.Cy = off ? off->PlaceDy : 0;
            p.Ca = sign * (off ? off->PlaceDa : 0);
            return p;
        }

        bool Placed(const VmPlaceCommand* cmd) { return cmd != nullptr && (cmd->Route == EVmRoute::Ok || cmd->Route == EVmRoute::Skip); }

        std::shared_ptr<const Built> BuildTool(const std::string& stamp, int nozzle, const VmCameraRead* read, const VmPlaceCommand* cmd,
                                               int row, int col, const std::string& yName, const std::optional<BreakdownParts>& parts)
        {
            auto b = std::make_shared<Built>();
            std::string cell = CellText(row, col);
            std::string text = stamp + (!cell.empty() ? " · " + cell : std::string());
            std::vector<ReadoutRow> rows;
            std::string note;
            if (IsProblem(read)) text += " · " + (note = Problem(read));
            else if (!read->Found) text += " · " + (note = Loc::T("Readout.NoProduct"));
            else
            {
                text += " · " + Loc::F("Readout.OffsetText", read->X, read->Y, read->A);
                rows.emplace_back(Loc::T("Readout.RowOffset"), std::vector<std::string>{
                    F("{0:+0.000;-0.000}", { read->X }), F("{0:+0.000;-0.000}", { read->Y }), F("{0:0.00}", { read->A }), "" });
                if (cmd != nullptr)
                {
                    text += Placed(cmd)
                        ? "\n" + Loc::F("Readout.TargetText", cmd->CmdX, yName, cmd->CmdY2, nozzle,
                                        AxisAngle::SignedText(cmd->CmdA, 2), cmd->DiagDA) +
                          (cmd->Route == EVmRoute::Skip ? " · " + Loc::F("Readout.SkipCellInline", OneLine(cmd->Why)) : std::string())
                        : "\n" + Loc::F("Readout.NotPlaced", OneLine(cmd->Why));
                    if (Placed(cmd))
                        rows.emplace_back(Loc::T("Readout.RowTarget"), std::vector<std::string>{
                            F("{0:0.000}", { cmd->CmdX }), F("{0:0.000}", { cmd->CmdY2 }), AxisAngle::SignedText(cmd->CmdA, 2),
                            F("{0:+0.00;-0.00}", { cmd->DiagDA }) });
                    if (cmd->Route == EVmRoute::Skip) note = Loc::F("Readout.SkipCell", OneLine(cmd->Why));
                    else if (cmd->Route != EVmRoute::Ok) note = Loc::F("Readout.NotPlaced", OneLine(cmd->Why));
                }
            }

            auto card = std::make_shared<ReadoutCard>();
            card->Cell = cell;
            card->Columns = { "X (mm)", "Y (mm)", "θ (°)", Loc::T("Readout.ColCompTheta") };
            card->Rows = std::move(rows);
            card->Note = note;
            // C#: Parts?.Card(cmd, …) — Parts chỉ có khi cmd đặt được (Ok/Skip) và có nhắm ô.
            if (parts && cmd != nullptr) card->Breakdown = parts->Card(*cmd, cell, note);

            b->Text = std::move(text);
            b->Card = std::move(card);
            return b;
        }

        struct TopMaster
        {
            /// <summary>Theo số tay (1, 2): có ảnh master hand-eye không, và Δ so với nó.</summary>
            bool HasMaster[3] = { false, false, false };
            double Dx[3] = { 0, 0, 0 }, Dy[3] = { 0, 0, 0 }, Da[3] = { 0, 0, 0 };
        };

        std::shared_ptr<const Built> BuildTop(const std::string& stamp, const VmCameraRead* read, int row, int col, const TopMaster& m)
        {
            auto b = std::make_shared<Built>();
            std::string cell = CellText(row, col);
            std::string text = stamp + (!cell.empty() ? " · " + cell : std::string());
            std::vector<ReadoutRow> rows;
            std::string note;
            if (IsProblem(read)) text += " · " + (note = Problem(read));
            else if (!read->Found) text += " · " + (note = Loc::T("Readout.NoTarget"));
            else
            {
                text += " · " + Loc::F("Readout.PartText", read->X, read->Y, read->A);
                rows.emplace_back(Loc::T("Readout.RowPart"), std::vector<std::string>{
                    F("{0:0.000}", { read->X }), F("{0:0.000}", { read->Y }), F("{0:0.00}", { read->A }) });
                std::vector<std::string> noMaster;
                for (int n = 1; n <= 2; n++)
                {
                    if (m.HasMaster[n])
                    {
                        text += "\n" + Loc::F("Readout.VsMaster", n, m.Dx[n], m.Dy[n], m.Da[n]);
                        rows.emplace_back(Loc::F("Readout.DeltaNozzle", n), std::vector<std::string>{
                            F("{0:+0.000;-0.000}", { m.Dx[n] }), F("{0:+0.000;-0.000}", { m.Dy[n] }), F("{0:+0.00;-0.00}", { m.Da[n] }) });
                    }
                    else
                    {
                        text += "\n" + Loc::F("Readout.NoMasterLine", n);
                        rows.emplace_back(Loc::F("Readout.DeltaNozzle", n), std::vector<std::string>{ "—", "—", "—" });
                        noMaster.push_back("#" + std::to_string(n));
                    }
                }
                if (!noMaster.empty())
                    note = Loc::F("Readout.NoMasterNote", base::Join(noMaster, ", "));
            }

            auto card = std::make_shared<ReadoutCard>();
            card->Cell = cell;
            card->Columns = TopColumns();
            card->Rows = std::move(rows);
            card->Note = note;

            b->Text = std::move(text);
            b->Card = std::move(card);
            return b;
        }
    }

    std::string VisionReadout::Tool1() { auto b = Read(S().Tool1); return b ? b->Text : std::string(); }
    std::string VisionReadout::Tool2() { auto b = Read(S().Tool2); return b ? b->Text : std::string(); }
    std::string VisionReadout::Top() { auto b = Read(S().Top); return b ? b->Text : std::string(); }

    std::shared_ptr<const ReadoutCard> VisionReadout::Tool1Card() { auto b = Read(S().Tool1); return b ? b->Card : nullptr; }
    std::shared_ptr<const ReadoutCard> VisionReadout::Tool2Card() { auto b = Read(S().Tool2); return b ? b->Card : nullptr; }
    std::shared_ptr<const ReadoutCard> VisionReadout::TopCard() { auto b = Read(S().Top); return b ? b->Card : nullptr; }

    void VisionReadout::Tool(int nozzle, const VmCameraRead* read, const VmPlaceCommand* cmd, int row, int col)
    {
        // C#: McDataManager.Model?.AssyTray?.AxisRow ?? "Y2"
        std::string yName = "Y2";
        if (auto model = McDataManager::Model(); model && model->AssyTray) yName = model->AssyTray->AxisRow;

        std::optional<BreakdownParts> parts;
        if (Placed(cmd) && row >= 0)
            parts = Breakdown(nozzle, *cmd, row, col);

        auto built = BuildTool(Stamp(), nozzle, read, cmd, row, col, yName, parts);
        if (nozzle == 1) Write(S().Tool1, std::move(built));
        else Write(S().Tool2, std::move(built));
    }

    void VisionReadout::TopDown(const VmCameraRead* read, int row, int col)
    {
        TopMaster m;
        if (!IsProblem(read) && read->Found)
        {
            auto model = McDataManager::Model();
            for (int n = 1; n <= 2 && model; n++)
            {
                const auto& calib = model->Calib;
                double tx = 0, ty = 0, ta = 0, mx = 0, my = 0;
                if (calib.TryGetHandEyeTarget(n, tx, ty, ta) && calib.TryGetHandEyeMasterAxis(n, mx, my))
                {
                    m.HasMaster[n] = true;
                    m.Dx[n] = (read->X - read->AxX) - (tx - mx);
                    m.Dy[n] = (read->Y - read->AxY) - (ty - my);
                    m.Da[n] = AxisAngle::ShortestDelta(ta, read->A);
                }
            }
        }
        Write(S().Top, BuildTop(Stamp(), read, row, col, m));
    }
}
