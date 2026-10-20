#include "Data/RunningData.h"

#include "Data/DataJson.h"

#include <algorithm>

namespace sca
{
    using datajson::json;
    using jsonx::Get;

    // ───────────────────────── TrayState ─────────────────────────

    TrayState::TrayState(const TrayState& other)
        : Name(other.Name), Rows(other.Rows), Cols(other.Cols), Cells(other.Cells), CurrentIndex(other.CurrentIndex),
          ScanOrder(other.ScanOrder), Trays(other.Trays), m_selected(other.Selected())
    {
    }

    TrayState& TrayState::operator=(const TrayState& other)
    {
        if (this == &other) return *this;
        Name = other.Name;
        Rows = other.Rows;
        Cols = other.Cols;
        Cells = other.Cells;
        CurrentIndex = other.CurrentIndex;
        ScanOrder = other.ScanOrder;
        Trays = other.Trays;
        SetSelected(other.Selected());
        return *this;
    }

    std::vector<int> TrayState::Selected() const
    {
        std::lock_guard<std::mutex> lock(m_selMutex);
        return m_selected;
    }

    void TrayState::SetSelected(std::vector<int> selected)
    {
        std::lock_guard<std::mutex> lock(m_selMutex);
        m_selected = std::move(selected);
    }

    bool TrayState::HasSelection() const
    {
        std::lock_guard<std::mutex> lock(m_selMutex);
        return !m_selected.empty();
    }

    bool TrayState::IsSelected(int index) const
    {
        std::lock_guard<std::mutex> lock(m_selMutex);
        return std::find(m_selected.begin(), m_selected.end(), index) != m_selected.end();
    }

    bool TrayState::InScope(int index) const
    {
        std::lock_guard<std::mutex> lock(m_selMutex);
        return m_selected.empty() || std::find(m_selected.begin(), m_selected.end(), index) != m_selected.end();
    }

    void TrayState::ToggleSelected(int index)
    {
        std::lock_guard<std::mutex> lock(m_selMutex);
        // List.Remove: bỏ phần tử ĐẦU TIÊN bằng index; không có thì thêm vào cuối.
        auto it = std::find(m_selected.begin(), m_selected.end(), index);
        if (it != m_selected.end()) m_selected.erase(it);
        else m_selected.push_back(index);
    }

    void TrayState::ClearSelection()
    {
        std::lock_guard<std::mutex> lock(m_selMutex);
        m_selected.clear();
    }

    void TrayState::Resize(int rows, int cols)
    {
        // Giữ lại trạng thái phần lưới trùng nhau — cùng lý do với TrayLayout.Resize.
        const std::vector<ECellState> old = Cells;
        int oldCols = Cols;
        int oldRows = Rows;
        // Lưới đổi cỡ thì chỉ số ô không còn trỏ đúng ô cũ — bỏ lựa chọn.
        if (rows != oldRows || cols != oldCols) ClearSelection();

        std::vector<ECellState> next;
        next.reserve((size_t)std::max(0, rows * cols));
        for (int r = 0; r < rows; r++)
        {
            for (int c = 0; c < cols; c++)
            {
                bool inOld = r < oldRows && c < oldCols && (r * oldCols + c) < (int)old.size();
                next.push_back(inOld ? old[(size_t)(r * oldCols + c)] : ECellState::Empty);
            }
        }

        Rows = rows;
        Cols = cols;
        Cells = std::move(next);
        if (CurrentIndex >= CellCount()) CurrentIndex = CellCount() > 0 ? 0 : -1;
    }

    void TrayState::RowCol(int index, int& row, int& col) const
    {
        if (Cols <= 0) { row = 0; col = 0; return; }
        row = index / Cols;
        col = index % Cols;

        // Serpentine: hàng lẻ quét ngược để không phải chạy không tải về đầu hàng.
        if (ScanOrder == EScanOrder::Serpentine && (row % 2) == 1)
            col = Cols - 1 - col;
    }

    int TrayState::VisitIndex(int row, int col, int cols, EScanOrder order)
    {
        if (cols <= 0) return -1;
        int c = col;
        if (order == EScanOrder::Serpentine && (row % 2) == 1) c = cols - 1 - col;
        return row * cols + c;
    }

    void TrayState::ResetAll(ECellState state)
    {
        for (auto& c : Cells) c = state;
        CurrentIndex = !Cells.empty() ? 0 : -1;
    }

    bool TrayState::Advance()
    {
        if (CurrentIndex < 0) return false;
        CurrentIndex++;
        if (CurrentIndex >= CellCount()) { CurrentIndex = -1; return false; }
        return true;
    }

    std::shared_ptr<TrayState> TrayState::Create(const std::string& name, int rows, int cols)
    {
        auto t = std::make_shared<TrayState>();
        t->Name = name;
        t->Rows = 0;
        t->Cols = 0;
        t->Resize(rows, cols);
        t->CurrentIndex = 0;
        return t;
    }

    // ───────────────────────── RunningData ─────────────────────────

    void RunningData::LoadInputTrays(int mask)
    {
        auto& in = *InputTray;
        int n = in.CellsPerTray();
        auto& cells = in.Cells;
        for (size_t i = 0; i < cells.size(); i++)
        {
            int tray = n > 0 ? (int)i / n : 0;
            // 1 << tray với tray ≥ 32 là hành vi không xác định trong C++ (C# lấy tray mod 32) — sổ chỉ có 1–2 tray.
            bool present = tray < 31 && (mask & (1 << tray)) != 0;
            cells[i] = present ? ECellState::Loaded : ECellState::Empty;
        }
        auto it = std::find(cells.begin(), cells.end(), ECellState::Loaded);
        in.CurrentIndex = it == cells.end() ? -1 : (int)(it - cells.begin());
    }

    bool RunningData::InputTrayNeeded(int tray) const
    {
        const auto& in = *InputTray;
        int n = in.CellsPerTray();
        const auto& cells = in.Cells;
        for (int i = tray * n; i < (tray + 1) * n && i < (int)cells.size(); i++)
        {
            if (i < 0) continue;
            if ((cells[(size_t)i] == ECellState::Loaded && in.InScope(i)) || cells[(size_t)i] == ECellState::Processing) return true;
        }
        return false;
    }

    void RunningData::RecordCycle(double sec, int products)
    {
        if (sec <= 0.01 || products <= 0) return;
        LastProductTimeSec = sec / products;
        // Danh sách mới thay cho danh sách cũ — luồng khác đang ghi running.json không bị đổi dưới chân.
        std::vector<double> list(RecentProductTimes);
        for (int i = 0; i < products; i++) list.push_back(LastProductTimeSec);
        if ((int)list.size() > CycleAvgWindow) list.erase(list.begin(), list.end() - CycleAvgWindow);
        double sum = 0;
        for (double t : list) sum += t;
        AvgProductTimeSec = sum / (double)list.size();
        RecentProductTimes = std::move(list);
    }

    void RunningData::StartAssyTrayClock(TimePoint now)
    {
        if (!AssyTrayStartedAt) RestartAssyTrayClock(now);
    }

    void RunningData::RestartAssyTrayClock(TimePoint now)
    {
        if (AssyTrayDone)
        {
            LastAssyTrayTimeSec = AssyTrayDoneSec;
            LastAssyTrayProducts = AssyTrayDoneProducts;
            AssyTrayDone = false;
            AssyTrayDoneSec = 0;
            AssyTrayDoneProducts = 0;
        }
        AssyTrayStartedAt = now;
    }

    void RunningData::FinishAssyTray(TimePoint now, int products)
    {
        if (!AssyTrayStartedAt) return;
        AssyTrayDoneSec = std::max(0.0, std::chrono::duration<double>(now - *AssyTrayStartedAt).count());
        AssyTrayDoneProducts = products;
        AssyTrayStartedAt.reset();
        AssyTrayDone = true;
    }

    void RunningData::ResetCounters()
    {
        CountOk = CountNg = CountTotal = ConsecutiveNg = 0;
        InputCountOk = InputCountNg = AssyCountOk = AssyCountNg = 0;
        LastCycleTimeSec = LastProductTimeSec = AvgProductTimeSec = 0;
        RecentProductTimes = std::vector<double>();
        LastAssyTrayTimeSec = 0;
        LastAssyTrayProducts = 0;
        AssyTrayDone = false;
        AssyTrayDoneSec = 0;
        AssyTrayDoneProducts = 0;
    }

    void RunningData::ResetCycleProgress()
    {
        AutoState = EAutoState::S00_Ready;
        UnsafePause = false;
        // Rows/Cols/ScanOrder đúng của model chỉ có sau LoadModel/LoadRecentModel (chạy
        // ngay sau lệnh gọi này lúc khởi động) — Resize ở đó tự khớp lại kích thước lưới.
        auto inputTray = TrayState::Create(InputTray->Name, InputTray->Rows, InputTray->Cols);
        inputTray->Trays = InputTray->Trays;
        InputTray = inputTray;
        AssyTray = TrayState::Create(AssyTray->Name, AssyTray->Rows, AssyTray->Cols);
        Nozzle1 = MakeNozzle(1);
        Nozzle2 = MakeNozzle(2);
        AssyTrayStartedAt.reset();      // sổ tray lắp ráp làm lại từ đầu thì đồng hồ tray cũng vậy
    }

    // ───────────────────────── JSON ─────────────────────────

    namespace datajson
    {
        json ToJson(const TrayState& t)
        {
            json j = json::object();
            if (!t.Name.empty()) j["Name"] = t.Name;   // rỗng đứng vai null → bỏ khoá (Create luôn đặt tên)
            j["Rows"] = t.Rows;
            j["Cols"] = t.Cols;
            json cells = json::array();
            for (ECellState c : t.Cells) cells.push_back((int)c);
            j["Cells"] = std::move(cells);
            j["CurrentIndex"] = t.CurrentIndex;
            j["ScanOrder"] = (int)t.ScanOrder;
            j["CellCount"] = t.CellCount();
            j["Trays"] = t.Trays;
            j["RowsPerTray"] = t.RowsPerTray();
            j["CellsPerTray"] = t.CellsPerTray();
            j["Selected"] = t.Selected();
            j["HasSelection"] = t.HasSelection();
            return j;
        }

        void FromJson(const json& j, TrayState& t)
        {
            Get(j, "Name", t.Name);
            Get(j, "Rows", t.Rows);
            Get(j, "Cols", t.Cols);
            if (auto it = j.find("Cells"); it != j.end() && it->is_array())
            {
                // Bản C# (ObjectCreationHandling.Auto) nối thêm vào danh sách có sẵn của đối tượng mặc định — vô hại vì
                // LoadRunningData luôn ResetCycleProgress ngay sau đó. Ở đây thay hẳn bằng danh sách trong file.
                std::vector<ECellState> cells;
                for (const auto& e : *it)
                {
                    ECellState s = ECellState::Empty;
                    if (e.is_number_integer()) s = (ECellState)e.get<int>();
                    else if (e.is_string()) ParseName(e.get_ref<const std::string&>(), s);
                    cells.push_back(s);
                }
                t.Cells = std::move(cells);
            }
            Get(j, "CurrentIndex", t.CurrentIndex);
            GetEnum(j, "ScanOrder", t.ScanOrder);
            Get(j, "Trays", t.Trays);
            if (auto it = j.find("Selected"); it != j.end() && it->is_array())
            {
                std::vector<int> sel;
                for (const auto& e : *it)
                    if (e.is_number_integer()) sel.push_back(e.get<int>());
                t.SetSelected(std::move(sel));
            }
        }

        json ToJson(const NozzleState& n)
        {
            json j = json::object();
            j["Index"] = n.Index;
            j["HasProduct"] = n.HasProduct;
            j["SourceCellIndex"] = n.SourceCellIndex;
            j["MeasuredAngle"] = Num(n.MeasuredAngle);
            j["Aligned"] = n.Aligned;
            return j;
        }

        void FromJson(const json& j, NozzleState& n)
        {
            Get(j, "Index", n.Index);
            Get(j, "HasProduct", n.HasProduct);
            Get(j, "SourceCellIndex", n.SourceCellIndex);
            GetNum(j, "MeasuredAngle", n.MeasuredAngle);
            Get(j, "Aligned", n.Aligned);
        }
    }

    std::string RunningData::ToJsonText() const
    {
        using namespace datajson;
        json j = json::object();
        if (!RecentModelName.empty()) j["RecentModelName"] = RecentModelName;   // null → không ghi
        j["SavedAt"] = ToIsoString(SavedAt);
        j["AutoState"] = (int)AutoState;
        j["UnsafePause"] = UnsafePause;
        j["NozzleClearRequired"] = NozzleClearRequired;
        if (!NozzleClearReason.empty()) j["NozzleClearReason"] = NozzleClearReason;
        if (InputTray) j["InputTray"] = ToJson(*InputTray);
        if (AssyTray) j["AssyTray"] = ToJson(*AssyTray);
        if (Nozzle1) j["Nozzle1"] = ToJson(*Nozzle1);
        if (Nozzle2) j["Nozzle2"] = ToJson(*Nozzle2);
        j["CountOk"] = CountOk;
        j["CountNg"] = CountNg;
        j["CountTotal"] = CountTotal;
        j["ConsecutiveNg"] = ConsecutiveNg;
        j["LastCycleTimeSec"] = Num(LastCycleTimeSec);
        j["InputCountOk"] = InputCountOk;
        j["InputCountNg"] = InputCountNg;
        j["AssyCountOk"] = AssyCountOk;
        j["AssyCountNg"] = AssyCountNg;
        j["LastProductTimeSec"] = Num(LastProductTimeSec);
        json times = json::array();
        for (double t : RecentProductTimes) times.push_back(Num(t));
        j["RecentProductTimes"] = std::move(times);
        j["AvgProductTimeSec"] = Num(AvgProductTimeSec);
        if (AssyTrayStartedAt) j["AssyTrayStartedAt"] = ToIsoString(*AssyTrayStartedAt);
        j["AssyTrayDone"] = AssyTrayDone;
        j["AssyTrayDoneSec"] = Num(AssyTrayDoneSec);
        j["AssyTrayDoneProducts"] = AssyTrayDoneProducts;
        j["AssyTrayDonePerProductSec"] = Num(AssyTrayDonePerProductSec());   // chỉ-đọc: Newtonsoft vẫn ghi
        j["LastAssyTrayTimeSec"] = Num(LastAssyTrayTimeSec);
        j["LastAssyTrayProducts"] = LastAssyTrayProducts;
        j["LastAssyTrayPerProductSec"] = Num(LastAssyTrayPerProductSec());
        j["NgRatePercent"] = Num(NgRatePercent());
        return DumpIndented(j);
    }

    std::shared_ptr<RunningData> RunningData::FromJsonText(const std::string& text)
    {
        using namespace datajson;
        json j;
        if (!jsonx::ParseObject(text, j)) return nullptr;

        auto r = std::make_shared<RunningData>();
        Get(j, "RecentModelName", r->RecentModelName);
        jsonx::GetTime(j, "SavedAt", r->SavedAt);
        GetEnum(j, "AutoState", r->AutoState);
        Get(j, "UnsafePause", r->UnsafePause);
        Get(j, "NozzleClearRequired", r->NozzleClearRequired);
        Get(j, "NozzleClearReason", r->NozzleClearReason);
        if (auto it = j.find("InputTray"); it != j.end() && it->is_object()) FromJson(*it, *r->InputTray);
        if (auto it = j.find("AssyTray"); it != j.end() && it->is_object()) FromJson(*it, *r->AssyTray);
        if (auto it = j.find("Nozzle1"); it != j.end() && it->is_object()) FromJson(*it, *r->Nozzle1);
        if (auto it = j.find("Nozzle2"); it != j.end() && it->is_object()) FromJson(*it, *r->Nozzle2);
        Get(j, "CountOk", r->CountOk);
        Get(j, "CountNg", r->CountNg);
        Get(j, "CountTotal", r->CountTotal);
        Get(j, "ConsecutiveNg", r->ConsecutiveNg);
        GetNum(j, "LastCycleTimeSec", r->LastCycleTimeSec);
        Get(j, "InputCountOk", r->InputCountOk);
        Get(j, "InputCountNg", r->InputCountNg);
        Get(j, "AssyCountOk", r->AssyCountOk);
        Get(j, "AssyCountNg", r->AssyCountNg);
        GetNum(j, "LastProductTimeSec", r->LastProductTimeSec);
        if (auto it = j.find("RecentProductTimes"); it != j.end() && it->is_array())
        {
            std::vector<double> times;
            for (const auto& e : *it)
                if (e.is_number()) times.push_back(e.get<double>());
            r->RecentProductTimes = std::move(times);
        }
        GetNum(j, "AvgProductTimeSec", r->AvgProductTimeSec);
        if (auto it = j.find("AssyTrayStartedAt"); it != j.end())
        {
            TimePoint t{};
            if (it->is_string() && TryParseDateTime(it->get<std::string>(), t)) r->AssyTrayStartedAt = t;
            else if (it->is_null()) r->AssyTrayStartedAt.reset();
        }
        Get(j, "AssyTrayDone", r->AssyTrayDone);
        GetNum(j, "AssyTrayDoneSec", r->AssyTrayDoneSec);
        Get(j, "AssyTrayDoneProducts", r->AssyTrayDoneProducts);
        GetNum(j, "LastAssyTrayTimeSec", r->LastAssyTrayTimeSec);
        Get(j, "LastAssyTrayProducts", r->LastAssyTrayProducts);
        return r;
    }
}
