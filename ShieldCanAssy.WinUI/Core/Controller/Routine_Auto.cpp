// Chu trình chạy tự động — port từ ShieldCanAssy.Core\Core.Controller\Routine_Auto.cs (đặc tả).
// Các hàm static thuần TrayMissingAt, Nozzle1TakesFirstCell, Nozzle1TakesFirstPickCell ở Routine_AutoStatics.cpp —
// KHÔNG định nghĩa lại ở đây.
#include "Controller/Routine_Auto.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

#include "Base/Async.h"
#include "Communication/AxisAngle.h"
#include "Controller/VisionReadout.h"
#include "Data/McDataManager.h"
#include "Infor/AlarmManager.h"
#include "Infor/AxisId.h"
#include "Infor/IoMap.h"
#include "Lang/Loc.h"
#include "Utility/Log/LogWorker.h"

namespace sca
{
    namespace
    {
        void Log(const std::string& msg, LogLevel level) { LogWorker::Instance().RecordLog(msg, level, "Auto"); }

        /// <summary>Tên bước như C# <c>EAutoState.ToString()</c> — giá trị ngoài enum thì in số như .NET.</summary>
        std::string StepName(EAutoState s)
        {
            std::string n = ToString(s);
            return n == "?" ? std::to_string((int)s) : n;
        }

        std::string CellText(int row, int col) { return "(" + std::to_string(row) + "," + std::to_string(col) + ")"; }

        /// <summary>Khối <c>finally</c> của C#: chạy ở MỌI đường ra khỏi phạm vi, kể cả khi có exception. Lỗi trong
        /// thân bị nuốt — destructor không được ném.</summary>
        template <class F>
        struct Finally
        {
            explicit Finally(F f) : m_f(std::move(f)) {}
            ~Finally()
            {
                try { m_f(); }
                catch (...) {}
            }
            Finally(const Finally&) = delete;
            Finally& operator=(const Finally&) = delete;
        private:
            F m_f;
        };

        /// <summary>
        /// <c>_motion[name].CurrentPos</c> của C# — trục không có thì C# ném NullReferenceException (vòng ngoài bắt,
        /// phát RoutineCrashed); bản C++ ném std::runtime_error cho cùng đường đó thay vì đọc con trỏ null.
        /// </summary>
        double AxisPos(const MotionConnect& motion, std::string_view name)
        {
            auto axis = motion[name];
            if (!axis) throw std::runtime_error("axis " + std::string(name) + " not found");
            return axis->CurrentPos();
        }

        /// <summary>TimeSpan.ToString(@"h\:mm\:ss\.f") — h là THÀNH PHẦN giờ (0–23, bỏ ngày) như .NET.</summary>
        std::string FormatHmsTenths(double sec)
        {
            if (!(sec > 0)) sec = 0;
            long long tt = (long long)std::floor(sec * 10.0 + 1e-9);
            long long h = (tt / 36000) % 24, m = (tt / 600) % 60, s = (tt / 10) % 60, f = tt % 10;
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld.%lld", h, m, s, f);
            return buf;
        }
    }

    Routine_Auto::Routine_Auto(MotionConnect& motion, McFunction& func, McStatus& status,
                               std::function<std::shared_ptr<SystemData>()> sys, std::function<std::shared_ptr<IVisionDevice>()> vision)
        : m_motion(motion), m_func(func), m_status(status), m_sys(std::move(sys)), m_vision(std::move(vision))
    {
    }

    std::shared_ptr<RunningData> Routine_Auto::RunData() const { return McDataManager::RunData(); }
    std::shared_ptr<ModelData> Routine_Auto::Model() const { return McDataManager::Model(); }

    // ───────────────────────── getter / bộ nhớ đệm ─────────────────────────

    std::vector<int> Routine_Auto::AssyScannedCells() const
    {
        std::lock_guard<std::recursive_mutex> l(m_cacheMutex);
        return m_assyScannedCells;
    }

    void Routine_Auto::PublishAssyScan()
    {
        std::lock_guard<std::recursive_mutex> l(m_cacheMutex);
        std::vector<int> cells;
        cells.reserve(m_assyScan.size());
        for (const auto& kv : m_assyScan) cells.push_back(kv.first);
        m_assyScannedCells = std::move(cells);
    }

    void Routine_Auto::InvalidateAssyScan()
    {
        {
            std::lock_guard<std::recursive_mutex> l(m_cacheMutex);
            m_assyScan.clear();
            PublishAssyScan();
            m_scanOneCell = -1;
            m_held[1].reset();
            m_held[2].reset();
        }
        std::lock_guard<std::mutex> l(m_placedMutex);
        m_placed.clear();
    }

    std::optional<VmCalibGate> Routine_Auto::LastCalibGate() const
    {
        std::lock_guard<std::mutex> l(m_gateMutex);
        return m_calib;
    }

    std::optional<PlaceMoveRecord> Routine_Auto::LastPlaceMove() const
    {
        std::lock_guard<std::mutex> l(m_placedMutex);
        return m_lastPlaceMove;
    }

    std::optional<PlacedTopRecord> Routine_Auto::GetPlacedTop(int index) const
    {
        std::lock_guard<std::mutex> l(m_placedMutex);
        auto it = m_placed.find(index);
        if (it == m_placed.end()) return std::nullopt;
        return it->second;
    }

    // ───────────────────────── vòng chính ─────────────────────────

    bool Routine_Auto::Run(std::stop_token ct)
    {
        McFunction::MarkCycleFlow();
        Log("Auto cycle started at step " + StepName(RunData()->AutoState), LogLevel::Info);

        // Chốt an toàn: rời chu trình bằng đường nào — dừng, NG, lỗi, huỷ — đèn CAM cũng phải tắt.
        // Không còn try/finally quanh từng lần gọi Flow nữa nên đây là chỗ duy nhất bảo đảm điều đó.
        Finally fin([this] {
            TurnCamLampOff();
            m_cycleSw.Stop();
        });

        try
        {
            TakeTrayLoads();
            m_cycleSw.Start();          // chạy tiếp lượt dở dang; S00 thì tự đặt lại về 0

            // Cổng hiệu chuẩn chạy ở MỌI lần START, kể cả chạy tiếp giữa chừng — teach lại vị trí soi lúc
            // đang dừng phải bị chặn trước khi trục nào chạy (thiết kế R13). S00 tự gọi lại.
            {
                auto run = RunData();
                if (run->AutoState != EAutoState::S00_Ready &&
                    !m_restartCycle && !run->NozzleClearRequired && !PauseRequested)
                {
                    TakeInvalidationRequest();
                    if (!Gate(ct)) return false;
                }
            }

            while (true)
            {
                if (ct.stop_requested()) return Stopped(Loc::T("Auto.Cancelled"));

                // Bit RUN tắt (bấm STOP): trục đã dừng, đứng chờ ở đây tới khi Start bật lại — không làm bước mới.
                // Lệnh chạy trục đang dở thì chờ ngay trong McFunction (Tuấn 2026-09-29).
                m_func.WaitRunBit(ct);

                auto run = RunData();

                // Vừa có sự cố với hàng trên tay hút: dừng ngay ở ranh giới bước, không làm thêm thao tác
                // nào (Tuấn chốt 2026-09-13, an toàn). Lần bấm chạy kế tiếp McControl gỡ yêu cầu — tay còn
                // hàng thì chạy tiếp, bỏ bước hút (Tuấn đổi 2026-09-26).
                if (run->NozzleClearRequired)
                    return Halt(Loc::F("Auto.HaltNozzleIncident", run->NozzleClearReason));

                // Checkpoint: chỉ ở đây mới xét yêu cầu dừng.
                if (PauseRequested)
                {
                    PauseRequested = false;
                    Save();
                    m_status.SetState(EMcState::Paused);
                    m_status.SetMessage(Loc::F("Auto.PausedAtStep", StepName(run->AutoState)));
                    Log(m_status.Message(), LogLevel::Info);
                    return true;
                }

                TakeInvalidationRequest();
                TakeTrayLoads();

                // Đồng hồ thực tế của tray lắp ráp (Tuấn 2026-10-08): bắt đầu ở bước đầu tiên chạy với tray đó trong
                // máy — tray chưa vào hoặc đã đầy (chờ thay) thì chưa đếm; đầy thì S50 chốt. Đã đếm thì không đụng.
                if (!run->AssyTrayStartedAt && m_assyIn && NextFreeCell(*run->AssyTray) >= 0)
                    run->StartAssyTrayClock(std::chrono::system_clock::now());

                // Tray lắp ráp vừa được nạp lại, hoặc bước đặt vừa dừng giữa chừng trước khi nhả hút:
                // chạy lại từ S00 để quét tray lắp ráp và soi lại sản phẩm đang cầm trước khi đặt
                // (Tuấn chốt 2026-09-13).
                if (m_restartCycle)
                {
                    m_restartCycle = false;
                    run->AutoState = EAutoState::S00_Ready;
                }

                bool ok;
                switch (run->AutoState)
                {
                case EAutoState::S00_Ready:     ok = S00_Ready(ct); break;
                case EAutoState::S05_ScanAssy:  ok = S05_ScanAssy(ct); break;
                case EAutoState::S10_Pick1:     ok = S1x_Pick(1, EAutoState::S11_Pick2, ct); break;
                case EAutoState::S11_Pick2:     ok = S1x_Pick(2, EAutoState::S20_Align1, ct); break;
                // Tay #2 gần CAM1 hơn tay #1 nên soi trước — S20_Align1/S21_Align2 vẫn là
                // tên BƯỚC (thứ tự chạy), không phải số tay hút (Tuấn xác nhận 2026-09-05).
                case EAutoState::S20_Align1:    ok = S2x_Align(2, EAutoState::S21_Align2, ct); break;
                case EAutoState::S21_Align2:    ok = S2x_Align(1, EAutoState::S30_Locate1, ct); break;
                case EAutoState::S30_Locate1:   ok = S3x_Place(1, EAutoState::S32_Locate2, ct); break;
                case EAutoState::S32_Locate2:   ok = S3x_Place(2, EAutoState::S40_Index, ct); break;
                case EAutoState::S40_Index:     ok = S40_Index(); break;
                case EAutoState::S50_TrayCheck: ok = S50_TrayCheck(ct); break;

                // S31/S33 nằm trong S30/S32: lệnh đặt đã tính lúc soi (S20/S21), S30/S32 chỉ
                // kiểm rồi chạy trục tới lệnh.
                case EAutoState::S31_Place1: run->AutoState = EAutoState::S32_Locate2; ok = true; break;
                case EAutoState::S33_Place2: run->AutoState = EAutoState::S40_Index; ok = true; break;

                default:
                    return Stopped(Loc::F("Auto.InvalidStep", StepName(run->AutoState)));
                }

                m_status.SetAutoStep(RunData()->AutoState);
                UpdateCamLamp();
                if (!ok) return false;
            }
        }
        catch (const base::OperationCanceled&)
        {
            return Stopped(Loc::T("Auto.Cancelled"));
        }
        catch (const std::exception& ex)
        {
            AlarmManager::RaiseEvent(AlarmCode::RoutineCrashed, "Auto", ex.what());   // sự kiện: RESET gỡ được (30/09)
            return Stopped(Loc::F("Auto.UnexpectedError", ex.what()));
        }
        catch (...)
        {
            // Bản C# không có nhánh này (mọi exception .NET đều là Exception); giữ hợp đồng "Run không ném".
            AlarmManager::RaiseEvent(AlarmCode::RoutineCrashed, "Auto", "unknown error");
            return Stopped(Loc::F("Auto.UnexpectedError", "unknown error"));
        }
    }

    void Routine_Auto::HoldClock(bool hold)
    {
        if (hold) m_cycleSw.Stop();
        else m_cycleSw.Start();
    }

    void Routine_Auto::TakeInvalidationRequest()
    {
        if (!m_invalidateRequested.exchange(false)) return;
        ClearVisionCaches("re-teach, model change, recalibration or solution reload");
    }

    void Routine_Auto::ClearVisionCaches(const std::string& why)
    {
        {
            std::lock_guard<std::recursive_mutex> l(m_cacheMutex);
            m_assyScan.clear();
            PublishAssyScan();
            m_scanOneCell = -1;
            m_held[1].reset();
            m_held[2].reset();
        }
        Log("Dropped stored measurements (cell scan, nozzle pose) — " + why + "; will rescan and re-align", LogLevel::Info);
    }

    // ───────────────────────── S00 ─────────────────────────

    bool Routine_Auto::S00_Ready(std::stop_token ct)
    {
        if (!Gate(ct)) return false;

        // Vào vòng mới: dọn trạng thái đo của vòng trước để không có đường nào dùng nhầm
        // tư thế của sản phẩm đã đặt xong.
        {
            std::lock_guard<std::recursive_mutex> l(m_cacheMutex);
            m_held[1].reset();
            m_held[2].reset();
        }
        ReleaseOrphanCells();
        m_cycleSw.Restart();
        auto run = RunData();
        m_cycleStartTotal = run->CountTotal;

        if (!m_func.EnsureZSafe(ct))
            return Halt(Loc::T("Auto.ZSafeFail"));

        m_afterScan = EAutoState::S10_Pick1;
        run->AutoState = EAutoState::S05_ScanAssy;
        return true;
    }

    // ───────────────────────── S05 — quét trước tray lắp ráp ─────────────────────────

    /// Quét đúng MỘT ô còn trống chưa có trong m_assyScan mỗi lần gọi — giữ đúng nguyên tắc "một bước, một việc" của
    /// cả state machine này để checkpoint dừng vẫn xét được sau mỗi ô thay vì phải chờ quét hết cả tray mới có chỗ dừng.
    /// Hết ô cần quét thì sang thẳng S10 — sau lần quét đầu của một tray, các vòng S00 sau chỉ tốn một lượt kiểm tra
    /// rỗng rồi đi tiếp ngay, không quét lại (§5.2b).
    bool Routine_Auto::S05_ScanAssy(std::stop_token ct)
    {
        if (!m_assyIn)
        {
            // Chưa có tray lắp ráp thì không soi (Tuấn chốt 2026-09-26). Tray đầu vào đã nạp mà còn nằm ở vị trí
            // chờ thì đưa vào máy trước; Start nạp tray đầu vào giữa lúc chờ cũng đánh thức vòng chờ để đưa tray vào.
            if (!MoveInputTrayIn(ct))
                return Halt(Loc::T("Auto.InputTrayInFail"));
            bool hadInput = m_inputIn;
            return WaitTray([this, hadInput] { return m_assyIn.load() || m_inputIn.load() != hadInput; }, ct);
        }

        auto run = RunData();
        auto tray = run->AssyTray;
        int index = NextScanCell(*tray);
        if (index < 0)
        {
            run->AutoState = m_afterScan;
            m_afterScan = EAutoState::S10_Pick1;
            return true;
        }

        int row, col;
        tray->RowCol(index, row, col);

        // Mốc tay #1 cố định cho mọi ô — xem ghi chú ở m_assyScan. Vị trí camera lấy từ mốc đã
        // teach trực tiếp (CamTopDownAnchorX/Y) cộng độ lệch lưới của ô so với ô (1,1) — không
        // còn qua Hand-Eye Dx/Dy nữa (Tuấn xác nhận 2026-09-05). Theo lưới tay nào thì xem
        // TrayLayout::CamGridZAxis; đừng chép lại phép tính ra đây.
        auto model = Model();
        auto cam = model->AssyTray->CamPosFor(row, col);
        double camX = cam.X, camY = cam.Y;

        LightCamLamp(LampFor(run->AutoState));   // đèn lên sáng trong lúc trục chạy — xem LightCamLamp
        if (!m_func.MoveToCell(model->AssyTray->AxisRow, camY, camX, ct))
            return Halt(Loc::F("Auto.ScanMoveFail", row, col));

        std::string cellText = CellText(row, col);
        auto read = LocateTopVm(ct);
        VisionReadout::TopDown(read, row, col);
        if (!read.CallOk) return VmHalt(0, -1, Loc::F("Auto.ScanLocateTop", cellText, read.CallError));
        if (!read.EchoOk) return VmHalt(0, -1, Loc::F("Auto.ScanLocateTop", cellText, read.EchoIssue));

        // Không thấy chi tiết đích ngay từ vòng quét: bỏ ô này luôn, không chờ tới lúc đặt mới phát hiện. Ô thành NG — lưới
        // trang Main tô đỏ (Tuấn 2026-10-17; trước đó Skipped, cùng màu ô bỏ vì lệch lưới hay không được chọn).
        if (!read.Found)
        {
            std::string detail = Loc::F("Auto.ScanLocateTop", cellText, Loc::T("Auto.SolutionNotFound"));
            if (StopOnVisionNg()) return VisionNgStop(detail);
            return SkipCellNotFound(*tray, index, detail);
        }
        if (!read.VmValid) return VmHalt(0, -1, Loc::F("Auto.ScanLocateTop", cellText, read.VmIssue));

        {
            std::lock_guard<std::recursive_mutex> l(m_cacheMutex);
            PlacedTopRecord rec;
            rec.X = read.X;
            rec.Y = read.Y;
            rec.A = read.A;
            m_assyScan[index] = rec;
            PublishAssyScan();
        }
        Save();
        return true;          // ở nguyên S05 cho tới khi NextUnscannedCell hết ô
    }

    bool Routine_Auto::SkipCellNotFound(TrayState& tray, int index, const std::string& detail)
    {
        AlarmManager::Raise(AlarmCode::VisionNotFound, "Vision", detail);
        tray.Set(index, ECellState::Ng);
        RunData()->AssyCountNg++;
        AlarmManager::Dismiss(AlarmCode::VisionNotFound);
        Save();
        return true;
    }

    /// Ô S05 soi lượt này: ưu tiên ô mà bước soi CAM dưới vừa đòi (m_scanOneCell), sau đó mới tới ô chưa quét theo
    /// NextUnscannedCell.
    int Routine_Auto::NextScanCell(const TrayState& tray)
    {
        std::lock_guard<std::recursive_mutex> l(m_cacheMutex);
        int one = m_scanOneCell;
        m_scanOneCell = -1;
        if (one >= 0 && one < tray.CellCount() && tray.Get(one) == ECellState::Empty && m_assyScan.find(one) == m_assyScan.end())
            return one;
        return NextUnscannedCell(tray);
    }

    /// Ô lắp ráp còn trống và CHƯA có kết quả quét trước. Quét từng hàng thì chỉ xét HÀNG đang làm — hàng của ô trống
    /// đầu tiên, tức hàng mà AssignCell sắp giao tới. Tray đầu vào chưa vào cũng chỉ xét hàng đó: soi trước hàng đầu
    /// trong lúc chờ tray đầu vào (Tuấn chốt 2026-09-26).
    ///
    /// Quét từng cặp (Tuấn 2026-10-28) chỉ xét HAI ô trống đầu tiên — đúng hai ô AssignCell sắp giao cho hai tay (ô tay
    /// đang giữ lệnh vẫn Empty tới lúc đặt nên vẫn nằm trong hai ô này). Ô soi không thấy thành NG, ô trống kế vào cặp và
    /// được soi tiếp, nên cặp luôn là hai ô đặt được (hoặc ít hơn khi tray sắp đầy). CountFreeCells khi đó ≤ 2: S1x hút
    /// đúng số ô đã soi.
    ///
    /// Trong cặp / hàng, ô X lớn chụp trước (FirstToScan, Tuấn 2026-10-28). Cả tray thì giữ thứ tự đi ô (rắn bò): quét một
    /// lượt mỗi tray, đảo chiều từng hàng ở đó chỉ thêm đường đi.
    int Routine_Auto::NextUnscannedCell(const TrayState& tray)
    {
        std::vector<int> group;
        if (m_sys()->ScanAssy() == SystemData::EScanAssy::Pair)
        {
            std::lock_guard<std::recursive_mutex> l(m_cacheMutex);
            int seen = 0;
            for (int i = 0; i < tray.CellCount() && seen < 2; i++)
            {
                if (tray.Get(i) != ECellState::Empty || !tray.InScope(i)) continue;
                seen++;
                if (m_assyScan.find(i) == m_assyScan.end()) group.push_back(i);
            }
            return FirstScanCell(tray, group);
        }

        int onlyRow = -1;
        if (m_sys()->ScanAssyByRow || !m_inputIn)
        {
            int first = NextFreeCell(tray);
            if (first < 0) return -1;
            int r0, c0;
            tray.RowCol(first, r0, c0);
            onlyRow = r0;
        }

        {
            std::lock_guard<std::recursive_mutex> l(m_cacheMutex);
            // Có chọn ô thì chỉ soi ô đã chọn (Tuấn 2026-10-16).
            for (int i = 0; i < tray.CellCount(); i++)
            {
                if (tray.Get(i) != ECellState::Empty || !tray.InScope(i) || m_assyScan.find(i) != m_assyScan.end()) continue;
                if (onlyRow < 0) return i;
                int r, c;
                tray.RowCol(i, r, c);
                if (r == onlyRow) group.push_back(i);
            }
        }
        return FirstScanCell(tray, group);
    }

    /// Ô chụp trước trong nhóm — theo X của chỗ CAM top-down đứng chụp (TrayLayout::CamPosFor), không phải X của tay.
    int Routine_Auto::FirstScanCell(const TrayState& tray, const std::vector<int>& cells)
    {
        if (cells.empty()) return -1;
        auto layout = Model()->AssyTray;
        std::vector<double> camX;
        for (int i : cells)
        {
            int r, c;
            tray.RowCol(i, r, c);
            camX.push_back(layout->CamPosFor(r, c).X);
        }
        return cells[(size_t)FirstToScan(camX, layout->HalfColPitch())];
    }

    // ───────────────────────── cổng ─────────────────────────

    /// Điều kiện máy cho phép chạy — PLAN.md §5.1, xét lại ở đầu mỗi vòng. Điều kiện hiệu chuẩn nằm ở Gate.
    bool Routine_Auto::CanRun(std::string& why)
    {
        why.clear();

        if (!m_status.OrgOk()) { why = Loc::T("Auto.NotHomed"); return false; }
        if (m_status.Mode() != EMcMode::Auto) { why = Loc::T("Auto.KeyNotAuto"); return false; }
        if (AlarmManager::HasError()) { why = Loc::T("Auto.AlarmNotCleared"); return false; }

        auto model = Model();
        if (!model) { why = Loc::T("Auto.NoModel"); return false; }
        if (!model->InputTray->IsTaught()) { why = Loc::T("Auto.InputTrayNotTaught"); return false; }
        if (!model->AssyTray->IsTaught()) { why = Loc::T("Auto.AssyTrayNotTaught"); return false; }
        // Kiểm tra ở đây để "chưa nạp solution" hiện ra đúng như nó là, chứ không biến thành
        // một chuỗi NG vision mà người vận hành sẽ đi tìm nguyên nhân ở camera và đèn.
        auto vision = m_vision();
        if (!vision || !vision->IsConnected()) { why = Loc::T("Auto.VisionNotConnected"); return false; }
        if (!vision->IsSolutionLoaded()) { why = Loc::T("Auto.SolutionNotLoaded"); return false; }

        // Chỉ tray đã vào máy: tray chưa nạp thì vắng ở vị trí chờ là chuyện bình thường, bước cần nó tự chờ. Bàn đầu
        // vào có hai tray: chỉ xét tray còn ô máy sẽ ghé — tray không nạp, hay đã xong hết, vắng cũng không sao.
        auto run = RunData();
        if (m_inputIn)
            for (int t = 0; t < run->InputTray->Trays; t++)
                if (run->InputTrayNeeded(t) && TrayMissing(model->InputTray->AxisRow, m_func.HasTrayIn(t)))
                {
                    why = Loc::F("Auto.NoInputTrayN", t + 1);
                    return false;
                }
        if (m_assyIn && TrayMissing(model->AssyTray->AxisRow, m_func.HasTrayAssy()))
        {
            why = Loc::T("Auto.NoAssyTray");
            return false;
        }

        return true;
    }

    bool Routine_Auto::TrayMissing(const std::string& rowAxis, bool sensorOn)
    {
        auto axis = m_motion[rowAxis];
        if (!axis) return false;
        return TrayMissingAt(axis->CurrentPos(), m_sys()->GetAxis(rowAxis)->StandbyPos, sensorOn);
    }

    /// Cổng hiệu chuẩn: điều kiện máy như CanRun, rồi cổng phía App — Bước 2/4/5, vị trí soi, ngưỡng. Chưa sẵn sàng thì
    /// chặn.
    bool Routine_Auto::Gate(std::stop_token /*ct*/)
    {
        std::string why;
        if (!CanRun(why)) return Halt(why);

        auto sys = m_sys();
        auto model = Model();
        std::string axisProblem = VmCycle::AxisVarProblem(&sys->VisionOutputs);
        if (!axisProblem.empty()) return Halt(axisProblem);
        if (!model->AssyTray->CamTopDownAnchorTaught)
            return Halt(Loc::T("Auto.TopAnchorNotTaught"));
        if (model->AssyTray->AxisRow != AxisId::Y2)
            return Halt(Loc::T("Auto.AssyRowAxisY2"));

        // Chỉ một tay chạy thì chỉ đòi hiệu chuẩn của tay đó — trừ khi tay kia còn cầm hàng, hàng đó vẫn phải soi và đặt.
        int only = sys->OnlyNozzle == 1 || sys->OnlyNozzle == 2 ? sys->OnlyNozzle : 0;
        if (only != 0 && Nozzle(3 - only).HasProduct) only = 0;
        VmCalibGate gate = VmCycle::EvaluateCalib(model.get(), sys.get(), only);
        {
            std::lock_guard<std::mutex> l(m_gateMutex);
            m_calib = gate;
        }
        if (!gate.Block.empty())
        {
            std::string blockWhy = Loc::F("Auto.CalibNotReady", gate.Block);
            AlarmManager::RaiseEvent(AlarmCode::VisionCalibNotReady, "Vision", blockWhy);
            return Halt(blockWhy);
        }

        if (VmMath::IsFinite(gate.DaysSinceOldest) && gate.DaysSinceOldest > sys->CalibValidDays)
            AlarmManager::Raise(AlarmCode::VisionCalibExpired, "Vision",
                Loc::F("Auto.CalibExpired", (int)gate.DaysSinceOldest));
        else
            AlarmManager::ClearCondition(AlarmCode::VisionCalibExpired);   // đã hiệu chuẩn lại thì gỡ
        return true;
    }

    // ───────────────────────── S10 / S11 — hút ─────────────────────────

    bool Routine_Auto::S1x_Pick(int nozzle, EAutoState next, std::stop_token ct)
    {
        auto run = RunData();
        NozzleState& nz = Nozzle(nozzle);

        // Đã có sản phẩm sẵn trên tay (chạy tiếp sau khi dừng) thì bỏ qua bước hút. Tay đang tắt (chế độ chỉ một tay) cũng không hút.
        if (nz.HasProduct || !m_sys()->NozzleEnabled(nozzle)) { run->AutoState = next; return true; }

        // Còn ô trống CHƯA quét: tray lắp ráp vừa được nạp lại bằng tay lúc máy dừng giữa vòng,
        // hoặc một lần đặt dừng giữa chừng vừa trả ô về trống. Quét xong đã rồi mới đếm, không
        // thì phép đếm dưới đây tính cả những ô chưa biết có chi tiết đích hay không (rà soát
        // 2026-09-13). S05 xong tự về S10; tay nào đã cầm hàng thì S10/S11 bỏ qua như thường.
        if (NextUnscannedCell(*run->AssyTray) >= 0)
        {
            m_afterScan = EAutoState::S10_Pick1;
            run->AutoState = EAutoState::S05_ScanAssy;
            return true;
        }

        // Chỉ hút khi còn chỗ đặt (Tuấn chốt 2026-09-13): số ô lắp ráp còn trống phải NHIỀU
        // HƠN số sản phẩm đang nằm trên tay hút. Qua dòng kiểm tra trên thì ô trống nào cũng là
        // ô quét thấy chi tiết đích — ô không thấy đã thành Skipped. Tay #1 hút trước, nên còn
        // đúng một ô thì chỉ tay #1 làm.
        int held = (run->Nozzle1->HasProduct ? 1 : 0) + (run->Nozzle2->HasProduct ? 1 : 0);
        int free = CountFreeCells(*run->AssyTray);
        if (free <= held)
        {
            // Còn ô cho hàng đang cầm thì đi soi/đặt nốt. Hết hẳn ô thì đi đổi tray ngay — hàng
            // đang cầm (nếu có) nằm chờ trên tay, S50 chỉ báo Warning (Tuấn chốt 2026-09-13).
            run->AutoState = held > 0 && free > 0 ? EAutoState::S20_Align1 : EAutoState::S50_TrayCheck;
            return true;
        }

        // Chưa có tray đầu vào thì không hút (Tuấn chốt 2026-09-26) — chờ nạp tray. Hàng đang cầm thì soi và đặt nốt
        // trước, chỉ cần tray lắp ráp. Nạp xong thì làm lại bước này từ đầu: tray lắp ráp mới soi hàng đầu, giờ mới
        // quét nốt.
        if (!m_inputIn)
        {
            if (held > 0) { run->AutoState = EAutoState::S20_Align1; return true; }
            return WaitTray([this] { return m_inputIn.load(); }, ct);
        }

        auto tray = run->InputTray;
        int index = AssignPickCell(nozzle);
        if (index < 0)
        {
            // Hết tray đầu vào. Nếu tay #1 cũng chưa có gì thì phải đổi tray trước khi
            // làm gì tiếp; nếu tay #1 đã có hàng thì chạy nốt sản phẩm đó rồi mới đổi.
            if (nozzle == 2 && Nozzle(1).HasProduct) { run->AutoState = EAutoState::S20_Align1; return true; }
            run->AutoState = EAutoState::S50_TrayCheck;
            return true;
        }

        tray->CurrentIndex = index;
        auto model = Model();
        int trayNo, row, col;
        auto layout = model->InputCell(*tray, index, trayNo, row, col);
        std::string where = Loc::F("Auto.InputCellLabel", trayNo + 1, row, col);

        std::string zName = nozzle == 1 ? AxisId::Z1 : AxisId::Z2;
        auto cell = layout->CellPos(zName, row, col);

        tray->Set(index, ECellState::Processing);

        int retries = m_sys()->PickRetryCount;
        // 5001 hiện trong lúc hút lại, hết bước hút thì tự rời danh sách ở MỌI đường ra — hút được ở lần sau, hết
        // lần thử (sang ô khác), Halt hay STOP — không cần RESET (Tuấn 2026-09-29; trước đó nằm lại chờ RESET).
        {
            Finally dismiss([] { AlarmManager::Dismiss(AlarmCode::PickFailed); });

            for (int attempt = 0; attempt <= retries; attempt++)
            {
                // A về góc hút đã teach, song song với X/Y: đặt xong A còn nằm ở CmdA, xuống hút bằng góc đó
                // thì gờ đầu hút không ăn khớp và chân không không đạt — xem McFunction::MoveToCellForPick.
                if (!m_func.MoveToCellForPick(nozzle, model->PickAngle(nozzle), layout->AxisRow, cell.Y, cell.X, ct))
                    return Halt(Loc::F("Auto.PickMoveFail", where));

                // Hút hụt thì hạ thêm một ít: nguyên nhân phổ biến nhất là sản phẩm nằm thấp
                // hơn dự kiến trong hốc tray, không phải hỏng chân không (§5.4). Z dương là
                // chiều ĐI XUỐNG (Tuấn xác nhận 2026-09-04) nên "hạ thêm" là CỘNG thêm, không
                // phải trừ.
                double zPick = layout->ZPickFor(zName) + attempt * m_sys()->PickRetryDeltaZ;

                // Tray đầu vào: qua điểm giảm tốc hay không theo Setting (Tuấn 2026-10-07) — mặc định hạ một mạch mức cao
                // (tay trống, không có gì để va), chỉ NÂNG mới chậm cho sản phẩm không giật rơi (Tuấn 2026-09-30).
                if (!m_func.MoveZDown(zName, zPick, layout->ZSlowFor(zName, m_sys()->InputZSlowDown), ct))
                    return Halt(Loc::F("Auto.ZDownPickFail", zName));

                m_func.SetVacuum(nozzle, true);
                bool gripped = WaitVacuum(nozzle, true, ct);
                SettleVacuum(gripped, ct);

                if (!m_func.MoveZUp(zName, SafeZ(), layout->ZSlowFor(zName, m_sys()->InputZSlowUp), ct))
                    return Halt(Loc::F("Auto.ZUpFail", zName));

                if (gripped)
                {
                    nz.HasProduct = true;
                    nz.SourceCellIndex = index;
                    nz.Aligned = false;
                    run->AutoState = next;
                    return true;
                }

                m_func.SetVacuum(nozzle, false);
                AlarmManager::Raise(AlarmCode::PickFailed, "Auto",
                    Loc::F("Auto.PickRetry", nozzle, where, attempt + 1, retries + 1));
            }
        }

        // Hết số lần thử: đánh dấu ô hỏng và đi tiếp, KHÔNG dừng máy. Một ô lỗi không phải
        // lý do để cả tray đứng lại (§5.4).
        tray->Set(index, ECellState::Ng);
        CountNg("pick miss Tray " + std::to_string(trayNo + 1) + " " + CellText(row, col));
        return CheckConsecutiveNg();
    }

    // ───────────────────────── S20 / S21 — soi và tính lệnh đặt ─────────────────────────

    /// Đưa tay hút vào khung hình CAM bottom-up, soi sản phẩm VÀ tính lệnh đặt trong cùng một lần chạy
    /// AlignBottom_ToolN (bản rút gọn 2026-09-13, Tuấn đề xuất): App gán ô đích cho tay ngay tại đây, gửi pose ô — số
    /// quét ở S05 — qua biến toàn cục, Flow trả CmdX/CmdY2/CmdA. App so sánh rồi cất lệnh tới lúc đặt.
    ///
    /// Lợi hơn tính lúc đặt: vượt ngưỡng thì trả sản phẩm về ô nguồn ngay, chưa phải chạy ra tray; ô lệch lưới thì bỏ ô
    /// và soi lại với ô kế khi tay vẫn đứng trên camera. Không mất độ chính xác: lệnh chỉ dựa vào ảnh chụp ở bước này,
    /// lúc đặt không có thông tin mới nào.
    ///
    /// Không xoay ở bước này. Toàn bộ lượng xoay dồn vào lúc di chuyển tới ô đặt, khi đó trục A chạy song song với X/Y2
    /// nên không tốn thêm giây nào của chu kỳ.
    bool Routine_Auto::S2x_Align(int nozzle, EAutoState next, std::stop_token ct)
    {
        auto run = RunData();
        NozzleState& nz = Nozzle(nozzle);
        if (!nz.HasProduct) { run->AutoState = next; return true; }
        {
            std::lock_guard<std::recursive_mutex> l(m_cacheMutex);
            if (m_held[nozzle]) { run->AutoState = next; return true; }
        }

        // Soi và đặt hàng đang cầm chỉ cần tray lắp ráp — chưa có thì về S05 chờ. Không chờ tray đầu vào: soi hỏng mà
        // không trả về được thì báo 5031, người lấy hàng ra (Tuấn chốt 2026-09-26) — xem ReturnToSource.
        if (!m_assyIn) return NeedAssyTray();

        auto sys = m_sys();
        const VisionOutputNames& names = sys->VisionOutputs;
        auto tray = run->AssyTray;

        int index = AssignCell(nozzle);
        if (index < 0)
        {
            // Không còn ô cho tay này — chỉ khi có sự cố xen giữa, vì S1x không cho hút quá số ô quét thấy chi tiết
            // đích. Giữ sản phẩm trên tay để tay kia (nếu có lệnh) đặt trước; S3x không có lệnh và không có ô thì đi
            // tiếp, S50 báo Warning và chờ đổi tray (Tuấn chốt 2026-09-13).
            Log("No assy cell left for nozzle #" + std::to_string(nozzle) + " — holding product, waiting for assy tray change",
                LogLevel::Warning);
            run->AutoState = next;
            return true;
        }

        // Pose ô đích lấy từ lần quét trước (S05). Thiếu thì quét những ô chưa quét rồi quay lại đúng bước soi này —
        // S1x đã bảo đảm quét đủ nên chỉ gặp khi số đo vừa bị bỏ giữa chừng.
        PlacedTopRecord top;
        {
            std::lock_guard<std::recursive_mutex> l(m_cacheMutex);
            auto it = m_assyScan.find(index);
            if (it == m_assyScan.end())
            {
                m_scanOneCell = index;
                m_afterScan = run->AutoState;
                run->AutoState = EAutoState::S05_ScanAssy;
                return true;
            }
            top = it->second;
        }

        auto model = Model();

        // Soi ở đúng Z_SAFE — cùng cao độ mọi bước hiệu chuẩn bottom-up chụp
        // (Routine_Calib::MoveZToBottomCapture). Vừa hút xong thì Z đã ở đó; lệnh này giữ đúng cả khi chạy
        // tiếp sau PAUSE mà Z đã bị jog.
        std::string zAxis = nozzle == 1 ? AxisId::Z1 : AxisId::Z2;
        LightCamLamp(LampFor(run->AutoState));   // đèn lên sáng trong lúc trục chạy — xem LightCamLamp
        if (!m_func.Move(zAxis, SafeZ(), ZSpeed(zAxis), ct))
            return Halt(Loc::F("Auto.ZAlignHeightFail", zAxis));

        double alignX = nozzle == 1 ? model->AlignPosX_Nozzle1 : model->AlignPosX_Nozzle2;
        if (!m_func.Move(AxisId::X, alignX, MoveSpeed(AxisId::X), ct))
            return Halt(Loc::F("Auto.AlignMoveFail", nozzle));

        int row, col;
        tray->RowCol(index, row, col);
        std::string cellText = CellText(row, col);

        // Bù đặt của tay này cho ô này = bù chung mọi ô + bù riêng của ô (trang Teaching nhập) — cộng vào đích
        // trước khi gửi, VisionMaster không biết có lượng bù nào.
        CellOffset trim = model->AssyTray->PlaceTrim(zAxis, row, col);

        m_func.WaitSettle(EVmCamera::BottomUp, ct);
        int rid = VmRunIds::Next();
        auto g = VmCycle::AxisGlobals(m_motion, names, rid);
        VmCycle::AddPlaceGlobals(g, &top, &model->Calib, nozzle, &trim);
        double aSent = VmCycle::Val(g, nozzle == 1 ? names.GvAxisA1 : names.GvAxisA2);
        VmCallRequest req = VmCycle::AlignBottomRequest(names, nozzle, g, true);
        VmCallResult r = CallVm(req, ct);
        VmCameraRead read = VmCycle::ReadAlignBottom(r, req, names, nozzle);
        VisionReadout::Tool(nozzle, read, static_cast<const VmPlaceCommand*>(nullptr), row, col);
        // Đối chiếu Group Cmd với phép tính lại của App: chỉ ghi khi KHÔNG khớp — mỗi lần soi một dòng dài là log thừa
        // khi máy chạy đúng (Tuấn 2026-10-01 bỏ log đo đạc). Các nút trang Vision Edit vẫn ghi đủ vết.
        auto zAx = m_motion[zAxis];
        VmAlignTrace trace = VmCycle::TraceAlignBottom(r, req, names, nozzle,
            zAx ? zAx->CurrentPos() : std::numeric_limits<double>::quiet_NaN());
        if (trace.Checked && !trace.Match)
            Log("Nozzle #" + std::to_string(nozzle) + " cell " + cellText + " — " + trace.Line, LogLevel::Warning);
        if (!read.CallOk) return VmHalt(nozzle, -1, Loc::F("Auto.AlignBottomNozzle", nozzle, read.CallError));
        if (!read.EchoOk) return VmHalt(nozzle, -1, Loc::F("Auto.AlignBottomNozzle", nozzle, read.EchoIssue));
        if (!read.Found)
        {
            if (StopOnVisionNg())
                return VisionNgStop(Loc::F("Auto.AlignBottomNozzle", nozzle, Loc::T("Auto.SolutionNotFound")));
            return AlignNotFound(nozzle, Loc::T("Auto.SolutionNotFound"), ct);
        }
        if (!read.VmValid) return VmHalt(nozzle, -1, Loc::F("Auto.AlignBottomNozzle", nozzle, read.VmIssue));

        // Tâm hàng rào lưới dời theo đúng lượng bù đặt: đích gửi đi đã lệch lưới teach từng ấy rồi,
        // để nguyên tâm cũ là lượng bù ăn dần vào ngưỡng ±PlaceTargetGuardMm.
        auto cellPos = model->AssyTray->CellPos(zAxis, row, col);
        double guardX = cellPos.X + trim.PlaceDx;
        double guardY = cellPos.Y + trim.PlaceDy;
        auto xCfg = sys->GetAxis(AxisId::X);
        auto yCfg = sys->GetAxis(model->AssyTray->AxisRow);
        std::function<bool(double)> xInLimit = [xCfg](double p) { return xCfg->InSoftLimit(p); };
        std::function<bool(double)> yInLimit = [yCfg](double p) { return yCfg->InSoftLimit(p); };
        VmPlaceCommand cmd = VmCycle::CheckPlace(r, req, nozzle, aSent, guardX, guardY, sys->PlaceTargetGuardMm,
            sys->MaxCompensationAngleDeg, sys->MaxCompensationDistance, xInLimit, yInLimit, true);
        VisionReadout::Tool(nozzle, read, cmd, row, col);

        switch (cmd.Route)
        {
        case EVmRoute::Ok:
            break;

        case EVmRoute::Ng:
        {
            if (StopOnVisionNg()) return VisionNgStop(Loc::F("Auto.NozzleCellWhy", nozzle, cellText, cmd.Why));
            // N — vision đo nhầm vật hoặc sản phẩm hút quá lệch: trả về ô nguồn ngay, tính NG.
            AlarmManager::Raise(AlarmCode::CompensationOutOfRange, "Auto", Loc::F("Auto.NozzleCellWhy", nozzle, cellText, cmd.Why));
            // Tự rời danh sách ở mọi đường ra, không cần RESET — trả về không được thì 5031/lỗi trục mới là cái chặn máy.
            {
                Finally dismiss([] { AlarmManager::Dismiss(AlarmCode::CompensationOutOfRange); });
                if (!ReturnToSource(nozzle, ct)) return false;
            }
            CountNg("compensation out of range nozzle #" + std::to_string(nozzle));
            return CheckConsecutiveNg();
        }

        case EVmRoute::Skip:
        {
            if (StopOnVisionNg()) return VisionNgStop(Loc::F("Auto.NozzleCellWhy", nozzle, cellText, cmd.Why));
            // G — lỗi nằm ở ô đích chứ không ở sản phẩm: bỏ ô, soi lại với ô kế (tay vẫn đứng trên camera). Ô thành
            // NG, lưới trang Main tô đỏ như ô LocateTop không thấy (Tuấn 2026-10-17; trước đó Skipped, màu xám).
            tray->Set(index, ECellState::Ng);
            run->AssyCountNg++;
            m_guardSkipStreak++;
            AlarmManager::Raise(AlarmCode::PlaceTargetGuard, "Auto", Loc::F("Auto.NozzleCellWhy", nozzle, cellText, cmd.Why));
            AlarmManager::Dismiss(AlarmCode::PlaceTargetGuard);
            Save();
            if (m_guardSkipStreak >= std::max(1, sys->MaxGuardSkipStreak))
                return VmHalt(nozzle, -1, Loc::F("Auto.GuardSkipStreak", m_guardSkipStreak, cmd.Why));
            Log("Skipped cell " + cellText + " — " + cmd.Why + "; keeping product on nozzle #" + std::to_string(nozzle) +
                ", re-aligning with next cell", LogLevel::Warning);
            return true;
        }

        default:
            return VmHalt(nozzle, -1, cmd.Why);
        }

        m_guardSkipStreak = 0;
        {
            std::lock_guard<std::recursive_mutex> l(m_cacheMutex);
            VmHeld h;
            h.Cell = index;
            h.Top = top;
            h.CmdX = cmd.CmdX;
            h.CmdY2 = cmd.CmdY2;
            h.CmdA = cmd.CmdA;
            h.ACap = aSent;
            h.DiagDA = cmd.DiagDA;
            h.DiagEcc = cmd.DiagEcc;
            m_held[nozzle] = h;
        }
        nz.MeasuredAngle = read.A;
        nz.Aligned = true;
        run->AutoState = next;
        return true;
    }

    /// Ô đích cho tay nozzle: ô trống theo thứ tự tray, trừ ô tay kia đang giữ lệnh. Tay #1 luôn nhận ô trước tay #2
    /// (S30 đặt tay #1 trước), kể cả khi S20 soi tay #2 trước: tay #2 chừa ô đầu cho tay #1 nếu tay #1 đang cầm hàng mà
    /// chưa có lệnh.
    int Routine_Auto::AssignCell(int nozzle)
    {
        auto tray = RunData()->AssyTray;
        int other = nozzle == 1 ? 2 : 1;
        std::optional<VmHeld> otherHeld;
        {
            std::lock_guard<std::recursive_mutex> l(m_cacheMutex);
            otherHeld = m_held[other];
        }
        int reserved = otherHeld ? otherHeld->Cell : -1;

        int first = -1, second = -1;
        for (int i = 0; i < tray->CellCount(); i++)
        {
            if (tray->Get(i) != ECellState::Empty || i == reserved || !tray->InScope(i)) continue;
            if (first < 0) first = i;
            else { second = i; break; }
        }
        if (first < 0) return -1;

        // Tay kia cũng đang cầm hàng mà chưa nhận ô → hai tay sắp chia nhau CẶP ô này.
        bool sharing = !otherHeld && Nozzle(other).HasProduct && second >= 0;
        if (!sharing) return first;

        return Nozzle1TakesFirstCell(CellX(first), CellX(second), Model()->AssyTray->HalfColPitch()) == (nozzle == 1)
            ? first : second;
    }

    /// Toạ độ X của gantry để tay #1 đứng trên ô — thước đo "ô nào xa hơn". So bằng lưới của một tay là đủ: lưới hai
    /// tay chỉ lệch nhau một hằng số.
    double Routine_Auto::CellX(int index)
    {
        int row, col;
        RunData()->AssyTray->RowCol(index, row, col);
        return Model()->AssyTray->CellPos(AxisId::Z1, row, col).X;
    }

    /// Align fail: trả sản phẩm về ĐÚNG ô cũ rồi đánh dấu ô đó NG. Thả bừa ở đâu đó là mất dấu sản phẩm và lần về gốc
    /// sau sẽ đâm vào nó (§5.4).
    bool Routine_Auto::AlignNotFound(int nozzle, const std::string& detail, std::stop_token ct)
    {
        AlarmManager::Raise(AlarmCode::VisionNotFound, "Vision",
            detail.empty() ? Loc::F("Auto.AlignBottomNozzleOnly", nozzle) : Loc::F("Auto.AlignBottomNozzle", nozzle, detail));
        // Tự rời danh sách ở mọi đường ra, không cần RESET — trả về không được thì 5031/lỗi trục mới là cái chặn máy.
        {
            Finally dismiss([] { AlarmManager::Dismiss(AlarmCode::VisionNotFound); });
            if (!ReturnToSource(nozzle, ct)) return false;
        }
        CountNg("align fail nozzle #" + std::to_string(nozzle));
        return CheckConsecutiveNg();
    }

    // ───────────────────────── S30–S33 — đặt ─────────────────────────

    /// Đặt sản phẩm của tay nozzle theo lệnh đã tính lúc soi — bước này không gọi Flow nào. Trước khi chạy trục vẫn
    /// kiểm: trục A chưa xoay kể từ lúc chụp (nhánh R), ô đích vẫn trống.
    bool Routine_Auto::S3x_Place(int nozzle, EAutoState next, std::stop_token ct)
    {
        auto run = RunData();
        NozzleState& nz = Nozzle(nozzle);
        if (!nz.HasProduct) { run->AutoState = next; return true; }

        // Tray lắp ráp đã ra vị trí chờ thì lệnh đặt cất từ trước thuộc tray cũ — bỏ, chờ tray mới rồi soi lại.
        if (!m_assyIn)
        {
            {
                std::lock_guard<std::recursive_mutex> l(m_cacheMutex);
                m_held[nozzle].reset();
            }
            return NeedAssyTray();
        }

        std::optional<VmHeld> held;
        {
            std::lock_guard<std::recursive_mutex> l(m_cacheMutex);
            held = m_held[nozzle];
        }
        if (!held)
        {
            // Chưa có lệnh: chạy tiếp sau khi số đo bị bỏ (tắt app, teach lại…), hoặc lúc soi không còn ô cho tay này.
            // Còn ô thì soi lại; hết ô thì để tay kia đặt trước, S50 chờ đổi tray. S20_Align1 soi tay #2 trước,
            // S21_Align2 soi tay #1 (Tuấn xác nhận 2026-09-05) — ánh xạ ngược với số tay vì tên BƯỚC không đổi theo.
            if (AssignCell(nozzle) < 0) { run->AutoState = next; return true; }
            run->AutoState = AlignStepOf(nozzle);
            Log("No place command for nozzle #" + std::to_string(nozzle) + " — re-aligning before place", LogLevel::Info);
            return true;
        }

        // Góc gốc của lệnh A là góc lúc chụp bottom-up. A đã xoay kể từ đó (jog lúc dừng…) thì lệnh không còn đúng:
        // soi lại, lần hai liên tiếp thì dừng (thiết kế nhánh R).
        std::string aName = nozzle == 1 ? AxisId::A1 : AxisId::A2;
        double aNow = AxisPos(m_motion, aName);
        if (AxisAngle::Distance(aNow, held->ACap) > m_sys()->ACapTolDeg)
        {
            {
                std::lock_guard<std::recursive_mutex> l(m_cacheMutex);
                m_held[nozzle].reset();
            }
            m_remeasure[nozzle]++;
            std::string moved = Loc::F("Auto.AMovedSinceCapture", aName, held->ACap, aNow);
            if (m_remeasure[nozzle] >= 2)
                return VmHalt(nozzle, -1, Loc::F("Auto.AMovedTwice", moved, nozzle));
            Log(moved + " — re-aligning nozzle #" + std::to_string(nozzle), LogLevel::Warning);
            run->AutoState = AlignStepOf(nozzle);
            return true;
        }

        auto tray = run->AssyTray;
        int index = held->Cell;
        if (tray->Get(index) != ECellState::Empty)
        {
            // Ô đích đã đổi trạng thái kể từ lúc soi: bỏ lệnh, soi lại.
            {
                std::lock_guard<std::recursive_mutex> l(m_cacheMutex);
                m_held[nozzle].reset();
            }
            run->AutoState = AlignStepOf(nozzle);
            return true;
        }

        tray->CurrentIndex = index;
        tray->Set(index, ECellState::Processing);
        m_releaseStarted = false;

        Finally fin([this, tray, index, nozzle] {
            // Ô còn Processing nghĩa là bước đặt dừng giữa chừng (Halt vì không tới được điểm đặt,
            // EMG, STOP lần 2...). Mọi nhánh bỏ đặt có chủ ý đều tự đặt lại trạng thái ô trước khi
            // return, nên tới được đây chỉ còn các lần dừng giữa chuyển động.
            // - CHƯA nhả hút: ô đích vẫn nguyên, sản phẩm vẫn trên tay. Trả ô về trống và cho lần
            //   chạy lại bắt đầu từ S00 — quét lại tray lắp ráp, soi lại sản phẩm rồi mới đặt
            //   (Tuấn chốt 2026-09-13).
            // - ĐÃ bắt đầu nhả: sản phẩm có thể đã nằm trong ô hoặc dính lại trên đầu hút. Giữ
            //   nguyên nhãn, bắt lấy sản phẩm ra khỏi tay hút trước thao tác kế tiếp (an toàn).
            if (tray->Get(index) == ECellState::Processing)
            {
                if (!m_releaseStarted)
                {
                    tray->Set(index, ECellState::Empty);
                    m_restartCycle = true;
                    Save();
                    m_status.AppendMessage(" — " + Loc::T("Auto.RestartRescanNote"));
                }
                else
                {
                    RequireNozzleClear(Loc::F("Auto.PlaceStoppedAfterRelease", nozzle));
                    m_status.AppendMessage(" — " + Loc::T("Auto.CheckNozzleNote"));
                }
            }
        });

        m_status.SetLastDeltaTheta(held->DiagDA);
        m_status.SetLastPlaceError(held->DiagEcc);

        // Góc TUYỆT ĐỐI do Flow tính từ góc lúc chụp — không cộng vào vị trí hiện tại.
        if (!Place(nozzle, held->CmdX, held->CmdY2, AxisAngle::Wrap(held->CmdA), ct)) return false;

        CompletePlacement(nozzle, index, &held->Top);
        run->AutoState = next;
        return true;
    }

    /// Ghi sổ một lần đặt thành công.
    void Routine_Auto::CompletePlacement(int nozzle, int index, const PlacedTopRecord* top)
    {
        auto run = RunData();
        NozzleState& nz = Nozzle(nozzle);
        run->AssyTray->Set(index, ECellState::Ok);
        run->CountOk++;
        run->CountTotal++;
        run->ConsecutiveNg = 0;
        run->InputCountOk++;
        run->AssyCountOk++;

        int src = nz.SourceCellIndex;
        if (src >= 0) run->InputTray->Set(src, ECellState::Ok);

        nz.HasProduct = false;
        nz.SourceCellIndex = -1;
        nz.Aligned = false;
        {
            std::lock_guard<std::recursive_mutex> l(m_cacheMutex);
            m_held[nozzle].reset();
        }
        m_remeasure[nozzle] = 0;
        m_guardSkipStreak = 0;

        if (top)
        {
            std::lock_guard<std::mutex> l(m_placedMutex);
            PlacedTopRecord rec;
            rec.X = top->X;
            rec.Y = top->Y;
            rec.A = top->A;
            rec.Nozzle = nozzle;
            m_placed[index] = rec;
        }

        Save();
    }

    /// Xoay, di chuyển tới điểm đặt, hạ, nhả, nâng. aTarget: góc tuyệt đối của trục A — CmdA của Flow, không cộng dồn.
    bool Routine_Auto::Place(int nozzle, double cmdX, double cmdY, double aTarget, std::stop_token ct)
    {
        auto model = Model();
        std::string aName = nozzle == 1 ? AxisId::A1 : AxisId::A2;
        std::string zName = nozzle == 1 ? AxisId::Z1 : AxisId::Z2;
        std::string yName = model->AssyTray->AxisRow;

        // Trục A chạy song song với X/Y2 — xoay không tốn thêm thời gian chu kỳ. Y2 mang tray lắp ráp: tới điểm bù ở
        // mức Auto thấp, gia tốc giảm theo, cho hàng trên tray khỏi xê dịch (Tuấn 2026-09-30).
        // Tốc độ đọc trước khi tách luồng (như C# tính đối số lúc dựng mảng Task) — GetAxis không chạy song song.
        double speedA = MoveSpeed(aName);
        double speedX = MoveSpeed(AxisId::X);
        double speedY = m_sys()->GetAxis(yName)->MoveSpeedLow();
        bool inCycle = McFunction::InCycleFlow();
        std::vector<std::function<bool()>> moves;
        moves.push_back([this, inCycle, &aName, aTarget, speedA, &ct] {
            McFunction::CycleFlowScope scope(inCycle);
            return m_func.Move(aName, aTarget, speedA, ct);
        });
        moves.push_back([this, inCycle, cmdX, speedX, &ct] {
            McFunction::CycleFlowScope scope(inCycle);
            return m_func.Move(AxisId::X, cmdX, speedX, ct);
        });
        moves.push_back([this, inCycle, &yName, cmdY, speedY, &ct] {
            McFunction::CycleFlowScope scope(inCycle);
            return m_func.MoveGentle(yName, cmdY, speedY, ct);
        });

        if (!base::WhenAll(std::move(moves)))
            return Halt(Loc::F("Auto.PlaceMoveFail", nozzle));

        {
            PlaceMoveRecord rec;
            rec.Nozzle = nozzle;
            rec.CmdX = cmdX;
            rec.CmdY = cmdY;
            rec.ATarget = aTarget;
            rec.ActualX = AxisPos(m_motion, AxisId::X);
            rec.ActualY = AxisPos(m_motion, yName);
            rec.ActualA = AxisPos(m_motion, aName);
            std::lock_guard<std::mutex> l(m_placedMutex);
            m_lastPlaceMove = rec;
        }

        if (!m_func.MoveZDown(zName, model->AssyTray->ZPickFor(zName), model->AssyTray->ZSlowFor(zName, m_sys()->AssyZSlowDown), ct))
            return Halt(Loc::F("Auto.ZDownPlaceFail", zName));

        m_releaseStarted = true;
        m_func.SetVacuum(nozzle, false);
        m_func.SetBlow(nozzle, true);

        // Đặt vào tray lắp ráp: thổi đủ BlowTimeMs rồi tắt, KHÔNG chờ cảm biến chân không báo nhả (Tuấn 2026-10-06).
        base::Delay(m_sys()->BlowTimeMs, ct);
        m_func.SetBlow(nozzle, false);

        // Tray lắp ráp: mặc định nâng cũng qua điểm giảm tốc — nhấc chậm cho lò xo trên tool nhả dần, không bật vào
        // tray (Tuấn 2026-09-30). Tắt được ở Setting (2026-10-07).
        if (!m_func.MoveZUp(zName, SafeZ(), model->AssyTray->ZSlowFor(zName, m_sys()->AssyZSlowUp), ct))
            return Halt(Loc::F("Auto.ZUpAfterPlaceFail", zName));

        return true;
    }

    /// Trả sản phẩm về đúng ô nguồn rồi đánh dấu ô đó NG. Dùng khi align fail hoặc lượng bù vượt ngưỡng — hai trường hợp
    /// mà sản phẩm còn nguyên nhưng máy không dám đặt nó.
    bool Routine_Auto::ReturnToSource(int nozzle, std::stop_token ct)
    {
        auto run = RunData();
        NozzleState& nz = Nozzle(nozzle);
        int index = nz.SourceCellIndex;

        if (index < 0 || !m_inputIn)
        {
            // Không có chỗ trả, phải người lấy hàng ra (Tuấn chốt 2026-09-26):
            // - tray đầu vào chưa vào máy — nằm ở vị trí chờ chỗ người thao tác, Y1 không được chạy tới;
            // - không rõ ô nguồn — sản phẩm của tray đầu vào CŨ (tray mới đầy hàng, không đặt ngược lại được), hoặc
            //   phần mềm vừa đồng bộ tay hút theo cảm biến (mở lại app lúc tay còn hút sản phẩm).
            // Phát dạng SỰ KIỆN: Raise trơn để ConditionActive = true mãi mãi, mà 5031 là mức Error nên RESET không
            // gỡ được và SafeToMove khoá cả máy tới khi mở lại app (rà soát 2026-09-13).
            std::string why = !m_inputIn ? Loc::T("Auto.InputTrayNotIn") : Loc::T("Auto.SourceUnknown");
            AlarmManager::RaiseEvent(AlarmCode::CannotReturnProduct, "Auto", Loc::F("Auto.NozzleWhy", nozzle, why));
            RequireNozzleClear(Loc::F("Auto.CannotReturnReason", nozzle, why));
            return Halt(Loc::F("Auto.CannotReturnHalt", nozzle, why));
        }

        // Ô nguồn có thể nằm ở Tray 1 hay Tray 2 — toạ độ và cao độ theo đúng tray đó.
        int trayNo, row, col;
        auto layout = Model()->InputCell(*run->InputTray, index, trayNo, row, col);

        std::string zName = nozzle == 1 ? AxisId::Z1 : AxisId::Z2;
        auto cell = layout->CellPos(zName, row, col);

        if (!m_func.MoveToCell(layout->AxisRow, cell.Y, cell.X, ct))
            return Halt(Loc::T("Auto.ReturnMoveFail"));
        if (!m_func.MoveZDown(zName, layout->ZPickFor(zName), layout->ZSlowFor(zName), ct))
            return Halt(Loc::T("Auto.ReturnZDownFail"));

        m_func.SetVacuum(nozzle, false);
        m_func.SetBlow(nozzle, true);
        bool released = WaitVacuum(nozzle, false, ct);
        base::Delay(m_sys()->BlowTimeMs, ct);
        m_func.SetBlow(nozzle, false);

        if (!m_func.MoveZUp(zName, SafeZ(), layout->ZSlowFor(zName), ct))
            return Halt(Loc::T("Auto.ReturnZUpFail"));

        if (!released)
        {
            // Cảm biến vẫn báo còn hàng sau khi thổi — xử lý như Place, và dừng TRƯỚC khi
            // ghi sổ: ghi tay hút thành trống trong lúc sản phẩm còn dính trên đầu hút là để lần
            // hút sau hạ một tay "trống" đang cầm hàng xuống ô có sản phẩm.
            AlarmManager::RaiseEvent(AlarmCode::PlaceFailed, "Auto",
                Loc::F("Auto.StillHoldingAfterReturn", nozzle));
            RequireNozzleClear(Loc::F("Auto.ReturnReleaseFailReason", nozzle));
            return Halt(Loc::F("Auto.ReturnReleaseFailHalt", nozzle));
        }

        run->InputTray->Set(index, ECellState::Ng);
        nz.HasProduct = false;
        nz.SourceCellIndex = -1;
        nz.Aligned = false;
        {
            std::lock_guard<std::recursive_mutex> l(m_cacheMutex);
            m_held[nozzle].reset();
        }
        Save();
        return true;
    }

    // ───────────────────────── S40 / S50 ─────────────────────────

    bool Routine_Auto::S40_Index()
    {
        auto run = RunData();
        m_cycleSw.Stop();
        double elapsed = m_cycleSw.ElapsedSeconds();
        if (elapsed > 0.01)
            run->LastCycleTimeSec = elapsed;
        run->RecordCycle(elapsed, run->CountTotal - m_cycleStartTotal);

        Save();
        run->AutoState = EAutoState::S50_TrayCheck;
        return true;
    }

    /// <summary>
    /// Hoàn thành chu trình (Tuấn 2026-10-28): MỘT trong hai tray xong — tray đầu vào hết hàng (cả sổ Tray 1 + Tray 2),
    /// hoặc tray lắp ráp hết ô trống — là xong cả lượt. Ô còn việc của tray kia thành Skipped (trang Main thấy ô nào
    /// chưa làm), CẢ HAI bàn ra vị trí chờ, máy báo "Cycle complete" và chờ Start; Start nạp hai tray như tray mới.
    /// Trước đó chỉ tray hết mới ra ngoài, tray kia làm tiếp với tray thay vào.
    /// </summary>
    bool Routine_Auto::S50_TrayCheck(std::stop_token ct)
    {
        auto run = RunData();
        bool inputDone = NextLoadedCell(*run->InputTray) < 0;
        bool assyDone = NextFreeCell(*run->AssyTray) < 0;

        // Hết hàng đầu vào mà tay còn cầm hàng, tray lắp ráp còn chỗ: đặt nốt đã rồi mới kết thúc lượt.
        bool holding = run->Nozzle1->HasProduct || run->Nozzle2->HasProduct;
        if (inputDone && !assyDone && holding)
        {
            run->AutoState = EAutoState::S20_Align1;
            return true;
        }

        if (!inputDone && !assyDone)
        {
            run->AutoState = EAutoState::S00_Ready;
            return true;
        }

        // ── Hoàn thành chu trình ──
        std::string reason = inputDone && assyDone ? Loc::T("Auto.DoneInputEmpty") + ", " + Loc::T("Auto.DoneAssyFull")
                           : inputDone ? Loc::T("Auto.DoneInputEmpty") : Loc::T("Auto.DoneAssyFull");
        int leftInput = FinishTray(*run->InputTray, ECellState::Loaded, "input tray");
        int leftAssy = FinishTray(*run->AssyTray, ECellState::Empty, "assy tray");

        // Tray lắp ráp xong (đầy, hoặc kết thúc cùng lượt): chốt thời gian thực tế của tray (Tuấn 2026-10-08) — chia
        // cho số ô đặt OK.
        if (run->AssyTrayStartedAt)
        {
            int ok = 0;
            std::vector<ECellState> cells = run->AssyTray->Cells;
            for (ECellState c : cells) if (c == ECellState::Ok) ok++;
            run->FinishAssyTray(std::chrono::system_clock::now(), ok);
            Log(Loc::Format("Assy tray done: {0} products in {1} (wall clock, incl. waits/stops) — avg {2:0.00} s/product",
                    { ok, FormatHmsTenths(std::floor(run->AssyTrayDoneSec * 10) / 10), run->AssyTrayDonePerProductSec() }),
                LogLevel::Info);
        }
        Save();

        // Hết ô lắp ráp mà tay còn cầm hàng: giữ nguyên sản phẩm trên tay (Tuấn chốt 2026-09-13) — lượt sau soi lại
        // và đặt lên tray lắp ráp mới. Chỉ là thông báo, không alarm (Tuấn 2026-09-30).
        std::string holdNote = holding
            ? " — nozzle still holding product; after tray change the new tray is scanned and the product re-aligned before place"
            : "";

        // Cả hai bàn ra vị trí chờ (StandbyPos trục hàng), chạy song song — từ lúc đó coi như hai tray đã ra khỏi máy.
        if (!MoveTraysToStandby(true, true, ct))
            return Halt(Loc::F("Auto.TrayToStandbyFail", Loc::T("Auto.TrayBoth")));
        m_inputIn = false;
        m_assyIn = false;
        {
            std::lock_guard<std::mutex> l(m_waitingMutex);
            m_cycleDone = Loc::F("Auto.CycleDone", reason);
        }
        Log(Loc::Format("Cycle complete ({0}): both trays to standby — {1} input cells and {2} assy cells left undone (skipped)",
                { reason, leftInput, leftAssy }) + holdNote,
            LogLevel::Info);

        // Luôn chạy lại từ S00, kể cả khi tay còn cầm hàng (Tuấn chốt 2026-09-13): S00 bỏ tư thế
        // đo cũ nên sản phẩm đang cầm được soi lại ở CAM bottom-up, S05 quét tray lắp ráp mới
        // trước khi đặt, và ReleaseOrphanCells giữ nguyên ô nguồn của sản phẩm đang cầm.
        run->AutoState = EAutoState::S00_Ready;
        return true;
    }

    /// Kết thúc tray khi xong lượt: ô còn việc (open: tray đầu vào còn hàng, tray lắp ráp còn trống) — kể cả ô không
    /// được chọn (Tuấn 2026-10-16) — thành Skipped, để sổ hết thật và tray đặt vào sau là tray mới; rồi xoá lựa chọn, vì
    /// lựa chọn chỉ cho tray đang làm. Ô Processing (nguồn của hàng còn trên tay) giữ nguyên.
    int Routine_Auto::FinishTray(TrayState& tray, ECellState open, const std::string& what)
    {
        bool hadSelection = tray.HasSelection();
        int skipped = 0;
        for (int i = 0; i < tray.CellCount(); i++)
            if (tray.Get(i) == open) { tray.Set(i, ECellState::Skipped); skipped++; }
        tray.ClearSelection();
        if (skipped > 0 || hadSelection)
            Log(what + " done — " + std::to_string(skipped) + " cells left undone marked skipped" +
                (hadSelection ? " (cell selection cleared)" : ""), LogLevel::Info);
        return skipped;
    }

    /// Operator vừa đặt tray mới: tray đầu vào có mặt (inputMask, 0 = không đổi) đầy hàng, tray lắp ráp trống hết. Ô đã
    /// chọn giữ nguyên — chọn lúc chờ thay tray là chọn cho tray này.
    void Routine_Auto::ReloadTrays(int inputMask, bool assy)
    {
        auto run = RunData();
        if (inputMask != 0)
        {
            run->LoadInputTrays(inputMask);
            // Ô nguồn của sản phẩm đang cầm thuộc tray đầu vào CŨ — giữ số ô đó là lát nữa đánh
            // dấu nhầm một ô còn hàng của tray mới. Bỏ đi: sản phẩm thành "không rõ ô nguồn".
            if (run->Nozzle1->HasProduct) run->Nozzle1->SourceCellIndex = -1;
            if (run->Nozzle2->HasProduct) run->Nozzle2->SourceCellIndex = -1;
        }
        if (assy)
        {
            run->AssyTray->ResetAll(ECellState::Empty);
            // Tray lắp ráp VẬT LÝ vừa đổi — số đo quét trước của tray cũ không còn đúng cho
            // tray mới dù trùng chỉ số ô, phải bỏ hết để S05_ScanAssy quét lại từ đầu.
            InvalidateAssyScan();
            m_restartCycle = true;
            // Tray mới bắt đầu đếm từ lúc vào máy (chu trình đang chạy, nhận tray ở đây).
            run->RestartAssyTrayClock(std::chrono::system_clock::now());
        }
    }

    /// Thông báo chờ nạp tray, theo tray còn thiếu — "hết/đầy" khi sổ tray đó đã dùng xong, không thì "chờ".
    std::string Routine_Auto::TrayWaitText()
    {
        {
            std::lock_guard<std::mutex> l(m_waitingMutex);
            if (!m_cycleDone.empty()) return m_cycleDone;   // vừa xong lượt, chưa nạp tray nào
        }
        auto run = RunData();
        std::string input = NextLoadedCell(*run->InputTray) < 0 ? Loc::T("Auto.WaitInputEmpty")
                                                                : Loc::T("Auto.WaitInput");
        std::string assy = NextFreeCell(*run->AssyTray) < 0 ? Loc::T("Auto.WaitAssyFull")
                                                            : Loc::T("Auto.WaitAssy");
        return !m_inputIn && !m_assyIn ? input + "; " + assy : m_inputIn ? assy : input;
    }

    /// Bước cần tray lắp ráp mà tray chưa vào: về S05 chờ nạp tray, quét xong quay lại đúng bước này.
    bool Routine_Auto::NeedAssyTray()
    {
        auto run = RunData();
        m_afterScan = run->AutoState;
        run->AutoState = EAutoState::S05_ScanAssy;
        return true;
    }

    /// Chờ operator nạp tray: trạng thái CHỜ THAO TÁC với thông báo theo tray còn thiếu, cho tới khi done(), vẫn xét
    /// STOP. Tray đã được nạp sẵn thì đi tiếp ngay, không nháy sang CHỜ THAO TÁC. Trả false khi bị dừng — chạy lại thì
    /// vào đúng bước này và chờ tiếp.
    bool Routine_Auto::WaitTray(const std::function<bool()>& done, std::stop_token ct)
    {
        TakeTrayLoads();
        if (done()) return true;

        std::string message = TrayWaitText();
        {
            std::lock_guard<std::mutex> l(m_waitingMutex);
            m_waiting = message;
        }
        m_status.SetState(EMcState::WaitOperator);
        m_status.SetMessage(message);
        Log("Waiting for tray: " + message, LogLevel::Info);
        m_cycleSw.Stop();               // chờ người thao tác không tính vào cycle time
        {
            Finally clearWaiting([this] {
                std::lock_guard<std::mutex> l(m_waitingMutex);
                m_waiting.clear();
            });
            while (true)
            {
                if (ct.stop_requested()) return Stopped(Loc::T("Auto.CancelledWaitingTray"));
                if (PauseRequested) { PauseRequested = false; return Stopped(Loc::T("Auto.StopWhileWaitingTray")); }
                base::Delay(50, ct);
                TakeTrayLoads();
                if (done()) break;
            }
        }

        m_cycleSw.Start();
        m_status.SetState(EMcState::Running);
        m_status.SetMessage(Loc::T("Auto.Running"));
        return true;
    }

    /// Nhận tray operator vừa nạp (LoadTrays) — trên luồng chu trình, ở đầu mỗi bước và trong lúc chờ tray. Tray hết
    /// theo sổ (đầu vào không còn ô có hàng, lắp ráp không còn ô trống) thì là tray mới: làm lại sổ. Tray còn dở thì giữ
    /// sổ — cùng tray đưa vào lại sau khi về gốc.
    void Routine_Auto::TakeTrayLoads()
    {
        int inputMask = m_loadInput.exchange(0);
        bool input = inputMask != 0;
        bool assy = m_loadAssy.exchange(0) == 1;
        if (!input && !assy) return;
        {
            std::lock_guard<std::mutex> l(m_waitingMutex);
            m_cycleDone.clear();   // lượt mới bắt đầu nạp tray
        }

        // Hai tray đầu vào là MỘT sổ: chỉ làm lại khi cả sổ đã hết. Còn dở một tray (về gốc giữa chừng) thì giữ nguyên
        // — tray đã xong nằm lại trên bàn mà tính là tray mới thì máy sẽ đi hút ô rỗng.
        auto run = RunData();
        bool newInput = input && NextLoadedCell(*run->InputTray) < 0;
        bool newAssy = assy && NextFreeCell(*run->AssyTray) < 0;
        ReloadTrays(newInput ? inputMask : 0, newAssy);
        if (input) m_inputIn = true;
        if (assy) m_assyIn = true;
        Save();

        if (input)
            Log(std::string("Input tray received (Start)") +
                (newInput ? " — new tray: " + InputTraysText(inputMask) : std::string(" — tray partly done, keeping records")),
                LogLevel::Info);
        if (assy)
            Log(std::string("Assy tray received (Start)") + (newAssy ? " — new tray" : " — tray partly done, keeping records"),
                LogLevel::Info);
    }

    /// Tray đầu vào vừa nạp còn nằm ở vị trí chờ: Z lên an toàn, Y1 đưa tray vào tới hàng hút kế tiếp — "Start thì tray
    /// đầu vào đi vào" (Tuấn 2026-09-26). Chưa nạp hoặc đã ở trong máy thì không chạy gì.
    bool Routine_Auto::MoveInputTrayIn(std::stop_token ct)
    {
        if (!m_inputIn) return true;
        auto model = Model();
        std::string axis = model->InputTray->AxisRow;
        if (std::fabs(AxisPos(m_motion, axis) - m_sys()->GetAxis(axis)->StandbyPos) > TraySensorWindowMm) return true;
        auto run = RunData();
        int index = NextLoadedCell(*run->InputTray);
        if (index < 0) return true;

        int trayNo, row, col;
        auto layout = model->InputCell(*run->InputTray, index, trayNo, row, col);
        if (!m_func.EnsureZSafe(ct)) return false;
        return m_func.Move(axis, layout->CellPos(AxisId::Z1, row, col).Y, MoveSpeed(axis), ct);
    }

    /// Z lên an toàn, rồi trục hàng của tray cần thay về vị trí chờ (StandbyPos) — hai tray chạy song song.
    bool Routine_Auto::MoveTraysToStandby(bool input, bool assy, std::stop_token ct)
    {
        if (!m_func.EnsureZSafe(ct)) return false;
        auto model = Model();
        bool inCycle = McFunction::InCycleFlow();
        std::vector<std::function<bool()>> moves;
        std::string inputAxis = model->InputTray->AxisRow;
        std::string assyAxis = model->AssyTray->AxisRow;
        if (input)
            moves.push_back([this, inCycle, &inputAxis, &ct] {
                McFunction::CycleFlowScope scope(inCycle);
                return MoveToStandby(inputAxis, ct);
            });
        if (assy)
            moves.push_back([this, inCycle, &assyAxis, &ct] {
                McFunction::CycleFlowScope scope(inCycle);
                return MoveToStandby(assyAxis, ct);
            });
        // TODO(port): MoveToStandby đọc m_sys()->GetAxis trong từng luồng (như C# gọi _sys().GetAxis trong
        // MoveToStandbyAsync trước await đầu tiên — tức trên luồng gọi). GetAxis chỉ chèn khi thiếu trục, mà trục
        // hàng luôn có sẵn trong system.json, nên đọc song song an toàn trong thực tế.
        return base::WhenAll(std::move(moves));
    }

    bool Routine_Auto::MoveToStandby(const std::string& axis, std::stop_token ct)
    {
        auto cfg = m_sys()->GetAxis(axis);
        return m_func.Move(axis, cfg->StandbyPos, cfg->MoveSpeed, ct);
    }

    bool Routine_Auto::TrayIn(bool input) const
    {
        return input ? m_inputIn.load() || m_loadInput.load() != 0
                     : m_assyIn.load() || m_loadAssy.load() == 1;
    }

    std::string Routine_Auto::WaitingMessage() const
    {
        std::lock_guard<std::mutex> l(m_waitingMutex);
        return m_waiting;
    }

    std::string Routine_Auto::LoadTrays(std::string& loaded, std::string& notSeen)
    {
        loaded.clear();
        notSeen.clear();
        auto model = Model();
        if (!model) return Loc::T("Auto.NoModelLoaded");

        std::vector<std::string> missing;
        int mask = 0;
        if (!TrayIn(true))
        {
            std::string why;
            mask = InputTraysSeen(why);
            if (!why.empty()) return why;
            if (mask == 0) missing.push_back(Loc::T("Auto.InputTrayNotSeen"));
        }
        bool assy = false;
        if (!TrayIn(false))
        {
            assy = !TrayMissing(model->AssyTray->AxisRow, m_func.HasTrayAssy());
            if (!assy) missing.push_back(Loc::T("Auto.AssyTrayNotSeen"));
        }

        if (mask != 0) m_loadInput.exchange(mask);
        if (assy) m_loadAssy.exchange(1);
        loaded = mask != 0 && assy ? Loc::T("Mc.BothTrays")
               : mask != 0 ? Loc::T("Mc.InputTray")
               : assy ? Loc::T("Mc.AssyTray") : std::string();
        for (size_t i = 0; i < missing.size(); i++)
        {
            if (i > 0) notSeen += "; ";
            notSeen += missing[i];
        }
        return {};
    }

    /// Các tray đầu vào operator vừa đặt — bit 0 Tray 1, bit 1 Tray 2; 0 khi không thấy tray nào, 0 kèm lý do khi từ
    /// chối. Bàn Y1 đang ở vị trí chờ thì theo hai cảm biến: tray nào có thì chạy (Tuấn chốt 2026-10-15). Thấy Tray 2 mà
    /// Tray 2 chưa teach thì từ chối — hút theo lưới chưa teach là đâm đầu hút xuống chỗ không biết. Bàn không ở vị trí
    /// chờ thì cảm biến không đo được (TrayMissingAt): tin lời operator như trước khi có Tray 2 — Tray 1, thêm Tray 2 nếu
    /// đã teach.
    int Routine_Auto::InputTraysSeen(std::string& why)
    {
        why.clear();
        auto model = Model();
        if (!AtStandby(model->InputTray->AxisRow)) return model->UsableInputTrays();

        bool t1 = m_func.HasTrayIn(0), t2 = m_func.HasTrayIn(1);
        if (t2 && !model->InputTray2->IsTaught()) { why = Loc::T("Auto.InputTray2NotTaught"); return 0; }
        return (t1 ? 1 : 0) | (t2 ? 2 : 0);
    }

    /// Trục hàng đang đứng ở vị trí chờ — chỗ duy nhất cảm biến tray đọc được, xem TrayMissingAt.
    bool Routine_Auto::AtStandby(const std::string& rowAxis)
    {
        auto axis = m_motion[rowAxis];
        return axis && std::fabs(axis->CurrentPos() - m_sys()->GetAxis(rowAxis)->StandbyPos) <= TraySensorWindowMm;
    }

    /// "Tray 1 + Tray 2" — cho log.
    std::string Routine_Auto::InputTraysText(int mask)
    {
        return mask == 3 ? "Tray 1 + Tray 2" : mask == 2 ? "Tray 2" : "Tray 1";
    }

    /// Init / Về gốc đưa mọi trục về vị trí chờ — cả hai tray coi như ra khỏi máy, phải bấm Start lại. Số đo vision của
    /// hàng đang làm dở (quét ô ở S05, lệnh đặt đã tính cho hàng trên tay) bỏ luôn, và lần chạy kế tiếp bắt đầu lại từ
    /// S00: S05 chụp lại hàng đó từ ô trống đầu tiên, hàng trên tay soi lại ở CAM bottom-up rồi mới đặt (Tuấn
    /// 2026-10-16 — tray đã ra ngoài, không tin số đo cũ). Chỉ gọi lúc chu trình không chạy.
    void Routine_Auto::TraysOut()
    {
        m_loadInput.exchange(0);
        m_loadAssy.exchange(0);
        m_inputIn = false;
        m_assyIn = false;
        {
            std::lock_guard<std::mutex> l(m_waitingMutex);
            m_cycleDone.clear();
        }
        ClearVisionCaches("Init/origin, trays out to standby — next run re-captures the current row from the start");
        m_restartCycle = true;
    }

    void Routine_Auto::LoadBothTrays()
    {
        auto model = Model();
        if (!m_inputIn) m_loadInput.exchange(model ? model->UsableInputTrays() : 1);
        if (!m_assyIn) m_loadAssy.exchange(1);
    }

    // ───────────────────────── tiện ích ─────────────────────────

    NozzleState& Routine_Auto::Nozzle(int i)
    {
        auto run = RunData();
        return i == 1 ? *run->Nozzle1 : *run->Nozzle2;
    }

    EAutoState Routine_Auto::AlignStepOf(int nozzle) { return nozzle == 1 ? EAutoState::S21_Align2 : EAutoState::S20_Align1; }

    // Z dương là chiều ĐI XUỐNG, "an toàn" là vị trí NHỎ (Tuấn xác nhận 2026-09-04) — xem MotionConnect::ZSafeOk.
    // Dừng đúng Z_SAFE — chốt chặn đã nới MotionConnect::ZSafeMargin (Tuấn chốt 2026-09-22).
    double Routine_Auto::SafeZ() { return m_motion.ZSafe(); }
    double Routine_Auto::MoveSpeed(const std::string& axis) { return m_sys()->GetAxis(axis)->MoveSpeed; }
    // Chỉ còn dùng cho chiều NÂNG Z lên — mọi lần HẠ Z xuống tray đi qua McFunction::MoveZDown để tôn trọng điểm giảm
    // tốc đã teach.
    double Routine_Auto::ZSpeed(const std::string& axis) { return m_sys()->GetAxis(axis)->MoveSpeed; }

    /// Chờ lực hút lên đủ sau khi cảm biến đã báo hút được, rồi mới cho nâng Z (SystemData::VacuumSettleMs). Hút hụt thì
    /// không chờ — đằng nào cũng đi thử lại.
    void Routine_Auto::SettleVacuum(bool gripped, std::stop_token ct)
    {
        int ms = m_sys()->VacuumSettleMs;
        if (gripped && ms > 0) base::Delay(ms, ct);
    }

    /// Chờ cảm biến chân không đạt trạng thái mong muốn, timeout thì trả false.
    bool Routine_Auto::WaitVacuum(int nozzle, bool want, std::stop_token ct)
    {
        base::Stopwatch sw;
        int limit = m_sys()->VacuumTimeoutMs;

        while (sw.ElapsedMilliseconds() < limit)
        {
            if (ct.stop_requested()) return false;
            if (m_func.HasProduct(nozzle) == want) return true;
            base::Delay(10, ct);
        }
        return m_func.HasProduct(nozzle) == want;
    }

    /// Gọi một Flow qua VisionConnect (ghi biến, chạy, đọc trong một lần giữ khoá; quá giờ do nó canh — một đồng hồ duy
    /// nhất). Đèn CAM bật trước khi gọi Flow có camera, chờ đèn sáng đủ (SystemData::LampMsFor), rồi mới kích Flow (Tuấn
    /// yêu cầu 2026-09-04, chờ đèn thêm 2026-09-23): bottom-up là đèn CAM1, top-down là đèn CAM2, Flow chỉ tính không
    /// bật đèn.
    ///
    /// Không tắt đèn ở đây. Đèn sáng suốt cả nhóm bước và chỉ tắt khi chu trình rời khỏi nhóm — xem TurnCamLampOn và
    /// UpdateCamLamp.
    VmCallResult Routine_Auto::CallVm(const VmCallRequest& req, std::stop_token ct)
    {
        auto vision = m_vision();
        if (!vision || !vision->IsConnected())
        {
            // Sự kiện (mức Error): không có vòng quét nào gỡ 4000, Raise trơn thì RESET không bao giờ xoá được.
            AlarmManager::RaiseEvent(AlarmCode::VisionDisconnected, "Vision");
            return VmCallResult::Fail(Loc::T("Auto.VisionNotConnected"));
        }

        int lampBit = req.Camera == EVmCamera::BottomUp ? IoMap::DoLampCam1
                    : req.Camera == EVmCamera::TopDown ? IoMap::DoLampCam2 : -1;
        TurnCamLampOn(lampBit, ct);

        // C# còn có nhánh "CallFlowAsync trả null → Fail(VmCycle.NoResult)"; bản C++ trả giá trị nên không có null.
        VmCallResult r = vision->CallFlow(req, ct);
        if (r.Ok) AlarmManager::ClearCondition(AlarmCode::VisionTriggerTimeout);
        return r;
    }

    /// Đèn CAM mà một bước cần sáng. Chỗ DUY NHẤT khai "nhóm bước nào dùng đèn nào" — thêm một bước có soi ảnh thì khai
    /// ở đây là xong, không phải đi sửa chỗ bật/tắt.
    int Routine_Auto::LampFor(EAutoState state)
    {
        switch (state)
        {
        case EAutoState::S05_ScanAssy:
            return IoMap::DoLampCam2;
        case EAutoState::S20_Align1:
        case EAutoState::S21_Align2:
            return IoMap::DoLampCam1;
        default:
            return -1;
        }
    }

    /// Bật đèn CAM, không chờ. Bước soi gọi hàm này TRƯỚC khi chạy trục tới vị trí chụp (Tuấn đề xuất 2026-10-23), để
    /// đèn lên sáng trong lúc trục đang chạy và ổn định — tới lúc kích Flow thì WaitLamp thường không còn phải chờ gì.
    ///
    /// Đèn sáng suốt cả nhóm bước, không nháy từng lần kích (Tuấn yêu cầu 2026-09-25). Trước đây mỗi lần gọi Flow đều
    /// bật rồi tắt ngay, nên hai lần soi CAM dưới của một nhịp thành hai nháy và một lượt quét tray lắp ráp thành 40
    /// nháy. Ngoài chuyện nhìn rối và tuổi thọ đèn, mỗi nháy còn phải trả lại khoảng chờ đèn lên đủ sáng.
    ///
    /// Đổi sang đèn khác thì tắt đèn cũ trước: hai đèn cùng sáng là ánh sáng của camera này lọt vào khung hình camera kia.
    void Routine_Auto::LightCamLamp(int lampBit)
    {
        if (lampBit < 0 || lampBit == m_lampOn) return;   // Flow chỉ tính, hoặc đèn đang sáng sẵn
        auto io = m_func.Io();
        if (m_lampOn >= 0 && io) io->SetDo(m_lampOn, false);
        if (io) io->SetDo(lampBit, true);
        m_lampOn = lampBit;
        m_lampSince.Restart();
    }

    /// Bật đèn CAM cần cho lần soi này (nếu bước soi chưa bật sẵn) rồi chờ đèn sáng đủ.
    void Routine_Auto::TurnCamLampOn(int lampBit, std::stop_token ct)
    {
        LightCamLamp(lampBit);
        WaitLamp(lampBit, ct);
    }

    /// Tắt đèn CAM khi chu trình đã rời khỏi nhóm bước dùng nó. Gọi sau MỖI bước, ngay trước checkpoint kế tiếp: bước
    /// nào cũng có thể nhảy sang nhánh khác (soi lại, quét lại, hết ô, NG), nên không chỗ nào khác biết chắc nhóm đã hết.
    void Routine_Auto::UpdateCamLamp()
    {
        if (m_lampOn >= 0 && m_lampOn != LampFor(RunData()->AutoState)) TurnCamLampOff();
    }

    void Routine_Auto::TurnCamLampOff()
    {
        if (m_lampOn < 0) return;
        if (auto io = m_func.Io()) io->SetDo(m_lampOn, false);
        m_lampOn = -1;
    }

    /// Chờ đèn CAM sáng đủ trước khi kích Flow — mỗi đèn một số (SystemData::BottomLampMs cho đèn CAM1,
    /// SystemData::TopLampMs cho đèn CAM2), tính từ lúc BẬT đèn: đèn bật sẵn từ trước khi chạy trục thì chỉ chờ phần
    /// còn thiếu, thường là không chờ. Flow chỉ tính — không dùng camera, không bật đèn nào — thì không chờ giây nào.
    void Routine_Auto::WaitLamp(int lampBit, std::stop_token ct)
    {
        if (lampBit < 0) return;
        int ms = lampBit == IoMap::DoLampCam2 ? m_sys()->TopLampMs : m_sys()->BottomLampMs;
        long long left = ms - m_lampSince.ElapsedMilliseconds();
        if (left > 0) base::Delay(static_cast<int>(left), ct);
    }

    VmCameraRead Routine_Auto::LocateTopVm(std::stop_token ct)
    {
        auto sys = m_sys();
        const VisionOutputNames& names = sys->VisionOutputs;
        m_func.WaitSettle(EVmCamera::TopDown, ct);
        int rid = VmRunIds::Next();
        auto g = VmCycle::AxisGlobals(m_motion, names, rid);
        VmCallRequest req = VmCycle::LocateRequest(names, g);
        VmCallResult r = CallVm(req, ct);
        return VmCycle::ReadLocate(r, g, names);
    }

    /// Nhánh C/E/R: dừng, GIỮ sản phẩm trên tay (chưa nhả), trả ô đích đang nhận về trống, và cho lần chạy sau bắt đầu
    /// lại từ S00 — quét lại, soi lại (Tuấn chốt 2026-09-13; D10 chọn giữ hàng). Phát alarm dạng sự kiện mức Error: bắt
    /// RESET có ý thức, nhưng RESET gỡ được.
    bool Routine_Auto::VmHalt(int nozzle, int index, const std::string& why, int alarm)
    {
        auto run = RunData();
        if (index >= 0 && run->AssyTray->Get(index) == ECellState::Processing)
            run->AssyTray->Set(index, ECellState::Empty);
        m_guardSkipStreak = 0;
        if (nozzle >= 1 && nozzle <= 2) m_remeasure[nozzle] = 0;

        AlarmManager::RaiseEvent(alarm, "Vision", why);
        bool holding = run->Nozzle1->HasProduct || run->Nozzle2->HasProduct;
        m_restartCycle = true;
        return Halt(why + " — " + Loc::T(holding ? "Auto.VmHaltHolding" : "Auto.VmHaltRestart"));
    }

    bool Routine_Auto::StopOnVisionNg() const { return m_status.StopOnVisionNg(); }

    /// Chế độ dừng khi Vision NG (Tuấn 2026-10-18 — để xem vì sao vision NG): dừng NGAY tại chỗ NG, TRƯỚC mọi xử lý NG
    /// (trả hàng về ô nguồn, đánh dấu ô NG, đếm NG) và trước khi trục nào chạy tiếp — tay vẫn cầm hàng, camera đứng ở vị
    /// trí chụp, khung CAM còn ảnh của lần NG. Không alarm, không RESET: máy về TẠM DỪNG, bước giữ nguyên. Start lần sau
    /// chạy lại đúng bước đó, tức soi lại (sửa xong Flow là thử lại ngay được); tắt chế độ rồi Start thì nếu vẫn NG, NG
    /// được xử lý như thường. Áp cho bốn chỗ NG của vision: S05 LocateTop không thấy chi tiết, S2x AlignBottom không thấy
    /// sản phẩm, bù vượt ngưỡng (nhánh N), đích lệch lưới (nhánh G). Lỗi hợp đồng (VmHalt) vốn đã dừng.
    bool Routine_Auto::VisionNgStop(const std::string& detail)
    {
        m_guardSkipStreak = 0;
        return Halt(Loc::F("Auto.VisionNgStop", detail));
    }

    /// Ô đầu vào kế tiếp còn hàng.
    ///
    /// Quét từ đầu tray, và KHÔNG nhận ô đang Processing. Hai điểm này đều quan trọng. Ô Processing là ô đã có tay hút
    /// nhận — nhận nó lần nữa thì tay #2 sẽ lao tới đúng ô mà tay #1 vừa lấy hàng, và vì cảm biến chân không bám theo van
    /// nên máy vẫn "thành công" rồi đặt hai lần cùng một sản phẩm tưởng tượng. Quét từ đầu thay vì từ con trỏ thì không
    /// có trạng thái con trỏ nào để hỏng: ô đã dùng mang nhãn khác Loaded nên tự khắc bị bỏ qua.
    ///
    /// Operator có chọn ô (TrayState::Selected) thì chỉ xét ô đã chọn — hết ô đã chọn là hết tray. Mọi chỗ chọn ô của chu
    /// trình đều qua TrayState::InScope như vậy.
    int Routine_Auto::NextLoadedCell(const TrayState& tray)
    {
        for (int i = 0; i < tray.CellCount(); i++)
            if (tray.Get(i) == ECellState::Loaded && tray.InScope(i)) return i;
        return -1;
    }

    /// Ô tray đầu vào giao cho tay này.
    ///
    /// Tay #1 hút TRƯỚC, nên nó là chỗ duy nhất trong nhịp còn nhìn thấy cả HAI ô sắp dùng — nó chọn theo hình học
    /// (Nozzle1TakesFirstPickCell). Tới lượt tay #2 thì ô tay #1 vừa nhận đã mang nhãn Processing, nên nó cứ lấy ô Loaded
    /// đầu tiên là đúng ô còn lại của cặp — không có đường nào cho hai tay chọn lệch nhau.
    ///
    /// Không ghép cặp khi chỉ còn một ô, hoặc khi tay #2 đang cầm hàng (chạy tiếp sau khi dừng) nên nhịp này nó không
    /// hút: lúc ấy không có cặp nào để cân, cứ ô ghé trước mà lấy.
    int Routine_Auto::AssignPickCell(int nozzle)
    {
        auto run = RunData();
        auto tray = run->InputTray;

        int first = -1, second = -1;
        for (int i = 0; i < tray->CellCount(); i++)
        {
            if (tray->Get(i) != ECellState::Loaded || !tray->InScope(i)) continue;
            if (first < 0) first = i;
            else { second = i; break; }
        }

        if (nozzle != 1 || first < 0 || second < 0 || run->Nozzle2->HasProduct || !m_sys()->NozzleEnabled(2)) return first;

        // Cặp ô có thể vắt từ Tray 1 sang Tray 2 — mỗi ô lấy theo lưới của tray chứa nó.
        auto model = Model();
        int t1, r1, c1, t2, r2, c2;
        auto g1 = model->InputCell(*tray, first, t1, r1, c1);
        auto g2 = model->InputCell(*tray, second, t2, r2, c2);
        return Nozzle1TakesFirstPickCell(
                   g1->CellPos(AxisId::Z1, r1, c1).X, g1->CellPos(AxisId::Z2, r1, c1).X,
                   g2->CellPos(AxisId::Z1, r2, c2).X, g2->CellPos(AxisId::Z2, r2, c2).X,
                   model->InputTray->HalfColPitch())
            ? first : second;
    }

    /// Ô lắp ráp ĐẶT ĐƯỢC: còn trống VÀ đã soi thấy chi tiết đích (có bản ghi trong m_assyScan). Ô chưa soi không được
    /// tính — bật SystemData::ScanAssyByRow thì các hàng sau chưa soi, mà soi xong có thể thành NG vì không thấy chi
    /// tiết; đếm cả chúng là hút quá số chỗ đặt rồi ôm hàng chờ đổi tray (Tuấn nhắc 2026-09-23).
    ///
    /// Tắt tuỳ chọn thì cổng hút đã đòi quét HẾT tray trước khi đếm, nên ô trống nào cũng có bản ghi — hai cách đếm ra
    /// cùng một số, luật "còn đúng một ô thì chỉ tay #1 hút" không đổi.
    int Routine_Auto::CountFreeCells(const TrayState& tray)
    {
        std::lock_guard<std::recursive_mutex> l(m_cacheMutex);
        int n = 0;
        for (int i = 0; i < tray.CellCount(); i++)
            if (tray.Get(i) == ECellState::Empty && tray.InScope(i) && m_assyScan.find(i) != m_assyScan.end()) n++;
        return n;
    }

    /// Ô lắp ráp kế tiếp còn trống — cùng quy tắc với NextLoadedCell, kể cả chỉ xét ô đã chọn.
    int Routine_Auto::NextFreeCell(const TrayState& tray)
    {
        for (int i = 0; i < tray.CellCount(); i++)
            if (tray.Get(i) == ECellState::Empty && tray.InScope(i)) return i;
        return -1;
    }

    /// Trả các ô Processing không có tay hút nào nhận về trạng thái ban đầu.
    ///
    /// Ô mang nhãn Processing nghĩa là "đã có tay hút nhận ô này". Sau khi tắt app giữa chừng hoặc dừng bất thường, nhãn
    /// đó còn nằm trên đĩa trong khi tay hút thì trống — không dọn thì ô đó bị bỏ qua vĩnh viễn và mỗi lần dừng máy lại
    /// mất thêm vài ô.
    void Routine_Auto::ReleaseOrphanCells()
    {
        auto run = RunData();
        int c1 = run->Nozzle1->HasProduct ? run->Nozzle1->SourceCellIndex : -1;
        int c2 = run->Nozzle2->HasProduct ? run->Nozzle2->SourceCellIndex : -1;

        Release(*run->InputTray, ECellState::Loaded, c1, c2);
        Release(*run->AssyTray, ECellState::Empty, -1, -1);
    }

    void Routine_Auto::Release(TrayState& tray, ECellState back, int keep1, int keep2)
    {
        for (int i = 0; i < tray.CellCount(); i++)
        {
            if (tray.Get(i) != ECellState::Processing) continue;
            if (i == keep1 || i == keep2) continue;
            tray.Set(i, back);
        }
    }

    void Routine_Auto::CountNg(const std::string& what)
    {
        auto run = RunData();
        run->CountNg++;
        run->CountTotal++;
        run->ConsecutiveNg++;
        run->InputCountNg++;          // mọi NG của sản phẩm đều là hàng lấy từ tray đầu vào
        Log("NG: " + what + " (consecutive " + std::to_string(run->ConsecutiveNg) + ")", LogLevel::Warning);
        Save();
    }

    /// Chốt chặn để máy không âm thầm làm hỏng cả tray: NG lẻ tẻ thì chạy tiếp, nhưng NG liên tiếp quá ngưỡng gần như
    /// luôn là hỏng cơ hoặc sai teach chứ không phải xui (§5.4).
    bool Routine_Auto::CheckConsecutiveNg()
    {
        auto run = RunData();
        int limit = m_sys()->MaxConsecutiveNg;
        if (run->ConsecutiveNg < limit) return true;

        // Sự kiện, không phải điều kiện tiếp diễn: 5010 mức Error, Raise trơn thì RESET không gỡ được
        // và HasError khoá cả máy tới khi mở lại App.
        AlarmManager::RaiseEvent(AlarmCode::ConsecutiveNg, "Auto",
            Loc::F("Auto.ConsecutiveNgDetail", run->ConsecutiveNg, limit));
        return Halt(Loc::F("Auto.ConsecutiveNgHalt", run->ConsecutiveNg));
    }

    /// Đánh dấu sự cố với hàng trên tay hút (Tuấn chốt 2026-09-13, an toàn) — chu trình dừng ở ranh giới bước kế tiếp.
    /// Lần bấm chạy sau McControl gỡ, ghi sổ theo cảm biến và chạy lại từ S00; tay còn hàng thì chạy tiếp, bỏ bước hút
    /// (Tuấn đổi 2026-09-26 — trước đó bắt lấy hàng ra).
    void Routine_Auto::RequireNozzleClear(const std::string& reason)
    {
        auto run = RunData();
        run->NozzleClearRequired = true;
        run->NozzleClearReason = reason;
        Save();
        Log("Remove product from nozzle required: " + reason, LogLevel::Warning);
    }

    void Routine_Auto::Save() { McDataManager::SaveRunningData(); }

    /// Dừng vì lỗi: ghi log, ghi tiến độ, để McControl chuyển trạng thái.
    bool Routine_Auto::Halt(const std::string& reason)
    {
        std::string msg = Loc::F("Auto.HaltAtStep", StepName(RunData()->AutoState), reason);
        m_status.SetMessage(msg);
        Log(msg, LogLevel::Error);
        Save();
        return false;
    }

    bool Routine_Auto::Stopped(const std::string& reason)
    {
        std::string msg = Loc::F("Auto.StoppedAtStep", StepName(RunData()->AutoState), reason);
        m_status.SetMessage(msg);
        Log(msg, LogLevel::Info);
        Save();
        return false;
    }
}
