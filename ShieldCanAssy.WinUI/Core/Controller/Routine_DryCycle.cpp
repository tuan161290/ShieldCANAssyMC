#include "Controller/Routine_DryCycle.h"

#include <cmath>
#include <format>
#include <vector>

#include "Base/Async.h"
#include "Communication/AxisAngle.h"
#include "Controller/Routine_Auto.h"
#include "Data/McDataManager.h"
#include "Infor/AlarmManager.h"
#include "Infor/AxisId.h"
#include "Lang/Loc.h"
#include "Utility/Log/LogWorker.h"

// Bản C++ của ShieldCanAssy.Core\Core.Controller\Routine_DryCycle.cs.

namespace sca
{
    Routine_DryCycle::Routine_DryCycle(MotionConnect& motion, McFunction& func, McStatus& status,
                                       std::function<std::shared_ptr<SystemData>()> sys)
        : m_motion(motion), m_func(func), m_status(status), m_sys(std::move(sys))
    {
    }

    double Routine_DryCycle::LastPlaceAngle(int nozzle) const { return m_lastPlaceAngle[(size_t)nozzle].load(); }

    void Routine_DryCycle::ResetCounters()
    {
        m_inIdx = 0;
        m_assyIdx = 0;
        m_angleStep[1] = m_angleStep[2] = 0;
        m_lastPlaceAngle[1] = 0.0;
        m_lastPlaceAngle[2] = 0.0;
        m_strokes = 0;
        m_laps = 0;
        m_sweeps = 0;
        m_scannedRow = -1;
        m_clock.Reset();
    }

    bool Routine_DryCycle::Run(std::stop_token ct)
    {
        McFunction::MarkCycleFlow();
        try
        {
            auto model = McDataManager::Model();
            if (model == nullptr) return Stopped(Loc::T("Dry.NoModel"));
            if (!model->InputTray->IsTaught() || !model->AssyTray->IsTaught())
                return Stopped(Loc::T("Dry.TraysNotTaught"));

            // Chạy rà là chu trình KHÔNG sản phẩm: còn hàng trên tay thì đầu hút sẽ dúi sản phẩm xuống hốc tray mà không
            // có bước nhả nào. Chặn ngay từ đầu, đừng để phát hiện lúc đã hạ Z.
            if (m_func.HasProduct(1) || m_func.HasProduct(2))
                return Stopped(Loc::T("Dry.NozzleHasProduct"));

            m_clock.Start();
            LogWorker::Instance().RecordLog("Dry run started (no product, axes only)", LogLevel::Info, "DryRun");

            const std::vector<std::function<bool(std::stop_token)>> legs =
            {
                [this](std::stop_token c) { return Scan(c); },
                [this](std::stop_token c) { return Pick(1, c); },
                [this](std::stop_token c) { return Pick(2, c); },
                // Tay #2 gần CAM1 hơn nên ra trước, giống thứ tự S20/S21 của chu trình thật.
                [this](std::stop_token c) { return Align(2, c); },
                [this](std::stop_token c) { return Align(1, c); },
                [this](std::stop_token c) { return Place(1, c); },
                [this](std::stop_token c) { return Place(2, c); },
            };

            while (true)
            {
                for (const auto& leg : legs)
                {
                    if (ct.stop_requested()) return Stopped(Loc::T("Mc.Cancelled"));
                    if (PauseRequested) { PauseRequested = false; return Paused(); }
                    if (!leg(ct)) return false;
                }
                Advance();
            }
        }
        catch (const base::OperationCanceled&)
        {
            return Stopped(Loc::T("Mc.Cancelled"));
        }
        catch (const std::exception& ex)
        {
            AlarmManager::RaiseEvent(AlarmCode::RoutineCrashed, "DryRun", ex.what());   // sự kiện: RESET gỡ được (30/09)
            return Stopped(Loc::F("Mc.Unexpected", ex.what()));
        }
    }

    // ───────────────────────── bốn chặng ─────────────────────────

    /// Chặng quét trước tray lắp ráp — đi đúng đường CAM top-down của S05_ScanAssy, chỉ không kích Flow nào (Tuấn hỏi
    /// 2026-09-24: chạy rà thiếu hẳn chặng này). Đây là chặng gantry chạy nhiều nhất trong cả chu trình, bỏ nó ra thì
    /// chạy rà không rà đúng quãng đường máy thật đi.
    ///
    /// Theo đúng tuỳ chọn của chu trình thật (SystemData::ScanAssyByRow): tắt thì quét cả tray MỘT LƯỢT ở đầu mỗi lượt
    /// tray; bật thì quét một hàng ngay trước khi làm hàng đó. Hai cách đi cùng một quãng đường nhưng khác THỨ TỰ, mà thứ
    /// tự mới là thứ Tuấn cần rà — tuỳ chọn ấy sinh ra chính vì nghi chạy Y2 hết chiều dài tray làm xê dịch tray.
    bool Routine_DryCycle::Scan(std::stop_token ct)
    {
        auto run = McDataManager::RunData()->AssyTray;

        if (m_sys()->ScanAssyByRow)
        {
            int row, col;
            CellOf(*run, m_assyIdx, row, col);
            if (row == m_scannedRow) return true;
            m_scannedRow = row;
            return Sweep(*run, row, ct);
        }

        m_scannedRow = -1;
        if (m_assyIdx != 0) return true;
        return Sweep(*run, -1, ct);
    }

    /// Đưa CAM top-down qua từng ô — cả tray (onlyRow < 0) hoặc một hàng. Xét yêu cầu dừng NGAY TRONG vòng quét, không
    /// đợi hết chặng: quét cả tray mất hàng chục giây, bắt operator chờ ngần ấy sau khi bấm STOP là sai. Bỏ dở giữa chừng
    /// vẫn an toàn — mỗi lần tới ô đều kết thúc với Z ở cao độ an toàn — và chặng ngoài sẽ thấy cờ dừng ở checkpoint kế.
    bool Routine_DryCycle::Sweep(const TrayState& run, int onlyRow, std::stop_token ct)
    {
        auto tray = McDataManager::Model()->AssyTray;
        m_sweeps++;

        for (int i = 0; i < run.CellCount(); i++)
        {
            if (ct.stop_requested()) return Stopped(Loc::T("Mc.Cancelled"));
            if (PauseRequested) return true;

            int row, col;
            CellOf(run, i, row, col);
            if (onlyRow >= 0 && row != onlyRow) continue;

            TeachPoint cam = tray->CamPosFor(row, col);
            if (!m_func.MoveToCell(tray->AxisRow, cam.Y, cam.X, ct))
                return Stopped(Loc::F("Dry.ScanCellFailed", row + 1, col + 1));
        }
        return true;
    }

    // ───────────────────────── ba chặng còn lại ─────────────────────────

    bool Routine_DryCycle::Pick(int nozzle, std::stop_token ct)
    {
        auto model = McDataManager::Model();
        auto run = McDataManager::RunData()->InputTray;
        const std::string zName = nozzle == 1 ? AxisId::Z1 : AxisId::Z2;

        // Sổ chỉ gồm tray đã teach (Tray 2 khi có) — mỗi ô theo lưới và cao độ của tray chứa nó.
        int index = PickCellFor(*model, *run, m_inIdx, nozzle);
        int trayNo, row, col;
        auto tray = model->InputCell(*run, index, trayNo, row, col);
        TeachPoint cell = tray->CellPos(zName, row, col);

        if (!m_func.MoveToCellForPick(nozzle, model->PickAngle(nozzle), tray->AxisRow, cell.Y, cell.X, ct))
            return Stopped(Loc::F("Dry.PickCellFailed", nozzle));
        // Cùng nhịp chu trình thật: qua điểm giảm tốc hay không theo 4 ô ở trang Setting (Tuấn 2026-10-07).
        auto sys = McDataManager::SysData();
        if (!m_func.MoveZDown(zName, tray->ZPickFor(zName), tray->ZSlowFor(zName, sys->InputZSlowDown), ct))
            return Stopped(Loc::F("Dry.ZPickDownFailed", zName));
        if (!m_func.MoveZUp(zName, m_motion.ZSafe(), tray->ZSlowFor(zName, sys->InputZSlowUp), ct))
            return Stopped(Loc::F("Dry.ZUpFailed", zName));
        return true;
    }

    bool Routine_DryCycle::Align(int nozzle, std::stop_token ct)
    {
        double x = McDataManager::Model()->AlignPosX(nozzle);
        if (!m_func.Move(AxisId::X, x, Speed(AxisId::X), ct))
            return Stopped(Loc::F("Dry.AlignPosFailed", nozzle));
        return true;
    }

    bool Routine_DryCycle::Place(int nozzle, std::stop_token ct)
    {
        auto model = McDataManager::Model();
        auto tray = model->AssyTray;
        const std::string zName = nozzle == 1 ? AxisId::Z1 : AxisId::Z2;
        const std::string aName = nozzle == 1 ? AxisId::A1 : AxisId::A2;

        int row, col;
        auto run = McDataManager::RunData()->AssyTray;
        run->RowCol(PlaceCellFor(*model, *run, m_assyIdx, nozzle), row, col);
        TeachPoint cell = tray->CellPos(zName, row, col);

        if (!m_func.EnsureZSafe(ct))
            return Stopped(Loc::T("Dry.ZSafeFailed"));

        // A chạy song song với X/Y đúng như lúc đặt thật, nên khớp quay được rà mà không tốn thêm thời gian. Mỗi việc chạy
        // trên luồng riêng nên mang theo cờ "lệnh của chu trình" (CONTROLLER-NOTES).
        double aTarget = PlaceAngle(nozzle);
        std::string rowAxis = tray->AxisRow;
        bool inCycle = McFunction::InCycleFlow();
        double aSpeed = Speed(aName), xSpeed = Speed(AxisId::X), rowSpeed = Speed(rowAxis);
        McFunction* func = &m_func;
        bool done = base::WhenAll({
            [=] { McFunction::CycleFlowScope scope(inCycle); return func->Move(aName, aTarget, aSpeed, ct); },
            [=] { McFunction::CycleFlowScope scope(inCycle); return func->Move(AxisId::X, cell.X, xSpeed, ct); },
            [=] { McFunction::CycleFlowScope scope(inCycle); return func->Move(rowAxis, cell.Y, rowSpeed, ct); },
        });
        if (!done) return Stopped(Loc::F("Dry.PlaceCellFailed", nozzle));

        // Tray lắp ráp: theo Setting như chu trình thật.
        auto sys = McDataManager::SysData();
        if (!m_func.MoveZDown(zName, tray->ZPickFor(zName), tray->ZSlowFor(zName, sys->AssyZSlowDown), ct))
            return Stopped(Loc::F("Dry.ZPlaceDownFailed", zName));
        if (!m_func.MoveZUp(zName, m_motion.ZSafe(), tray->ZSlowFor(zName, sys->AssyZSlowUp), ct))
            return Stopped(Loc::F("Dry.ZUpFailed", zName));
        return true;
    }

    // ───────────────────────── đi ô, đi góc, đếm ─────────────────────────

    /// Góc đặt của LẦN ĐẶT NÀY: chạy vòng quanh 0 → +ngưỡng → 0 → −ngưỡng, đổi sau mỗi lần đặt đúng như chu trình thật
    /// (mỗi sản phẩm có lượng bù riêng). Lượng bù chỉ quét MỘT PHẦN ngưỡng — xem DrySwingFraction.
    double Routine_DryCycle::PlaceAngle(int nozzle)
    {
        double span = std::fabs(m_sys()->MaxCompensationAngleDeg) * DrySwingFraction;
        int step = m_angleStep[(size_t)nozzle]++ & 3;
        double bu = step == 1 ? span : step == 3 ? -span : 0.0;
        m_lastPlaceAngle[(size_t)nozzle] = bu;

        // CmdA của chu trình thật = GÓC LÚC CHỤP + góc bù, mà góc lúc chụp bây giờ chính là góc hút (A về góc hút trước
        // khi hạ Z, không còn đứng ở CmdA cũ). Chạy rà phải đi theo đúng công thức ấy. Trước đây trả về góc TUYỆT ĐỐI
        // 0/±ngưỡng, nên tay #2 luôn dừng đúng 15° thay vì quanh quẩn góc hút — Tuấn phát hiện 2026-09-24: "hút thì đúng
        // góc, thả mới bị quay về 15 độ".
        return AxisAngle::Wrap(McDataManager::Model()->PickAngle(nozzle) + bu);
    }

    /// Trước 2026-10-15 chạy rà cứ giao ô ghé trước cho tay #1. Tray đầu vào quét tuần tự thì ô ghé trước cũng là ô X nhỏ
    /// hơn nên không ai thấy; Tuấn đổi tray đầu vào sang zigzag là ở hàng chạy ngược tay #1 lấy ô X lớn hơn, khác hẳn Auto.
    /// Chạy rà phải rà đúng đường Auto đi.
    int Routine_DryCycle::PickCellFor(const ModelData& model, const TrayState& run, int first, int nozzle)
    {
        int n = run.CellCount();
        if (n <= 0) return 0;
        int a = Wrap(first, n), b = Wrap(first + 1, n);
        if (a == b) return a;
        int ta, ra, ca, tb, rb, cb;
        auto ga = model.InputCell(run, a, ta, ra, ca);
        auto gb = model.InputCell(run, b, tb, rb, cb);
        bool n1First = Routine_Auto::Nozzle1TakesFirstPickCell(
            ga->CellPos(AxisId::Z1, ra, ca).X, ga->CellPos(AxisId::Z2, ra, ca).X,
            gb->CellPos(AxisId::Z1, rb, cb).X, gb->CellPos(AxisId::Z2, rb, cb).X,
            model.InputTray->HalfColPitch());
        return (nozzle == 1) == n1First ? a : b;
    }

    int Routine_DryCycle::PlaceCellFor(const ModelData& model, const TrayState& run, int first, int nozzle)
    {
        int n = run.CellCount();
        if (n <= 0) return 0;
        int a = Wrap(first, n), b = Wrap(first + 1, n);
        if (a == b) return a;
        int ra, ca, rb, cb;
        run.RowCol(a, ra, ca);
        run.RowCol(b, rb, cb);
        bool n1First = Routine_Auto::Nozzle1TakesFirstCell(
            model.AssyTray->CellPos(AxisId::Z1, ra, ca).X, model.AssyTray->CellPos(AxisId::Z1, rb, cb).X,
            model.AssyTray->HalfColPitch());
        return (nozzle == 1) == n1First ? a : b;
    }

    int Routine_DryCycle::Wrap(int index, int n) { return ((index % n) + n) % n; }

    void Routine_DryCycle::CellOf(const TrayState& tray, int index, int& row, int& col)
    {
        int n = tray.CellCount();
        if (n <= 0) { row = 0; col = 0; return; }
        tray.RowCol(((index % n) + n) % n, row, col);
    }

    void Routine_DryCycle::Advance()
    {
        m_strokes += 2;

        auto run = McDataManager::RunData();
        int assyCount = run->AssyTray->CellCount();
        m_inIdx += 2;
        m_assyIdx += 2;

        if (assyCount > 0 && m_assyIdx >= assyCount)
        {
            m_assyIdx %= assyCount;
            m_laps++;
            LogWorker::Instance().RecordLog(std::format("Dry run: tray lap {} done — {} strokes, {:.0f} min",
                                                        m_laps.load(), m_strokes.load(), m_clock.ElapsedSeconds() / 60.0),
                                            LogLevel::Info, "DryRun");
        }

        int inCount = run->InputTray->CellCount();
        if (inCount > 0) m_inIdx %= inCount;

        m_status.SetMessage(Loc::F("Dry.Progress", m_laps.load() + 1, m_strokes.load(), m_sweeps.load(),
                                   m_clock.ElapsedSeconds() / 60.0));
    }

    double Routine_DryCycle::Speed(const std::string& axis) { return m_sys()->GetAxis(axis)->MoveSpeed; }

    bool Routine_DryCycle::Paused()
    {
        m_clock.Stop();
        m_status.SetState(EMcState::Paused);
        m_status.SetMessage(Loc::F("Dry.Paused", m_strokes.load(), m_laps.load(), m_clock.ElapsedSeconds() / 60.0));
        LogWorker::Instance().RecordLog(m_status.Message(), LogLevel::Info, "DryRun");
        return true;
    }

    bool Routine_DryCycle::Stopped(const std::string& why)
    {
        m_clock.Stop();
        m_status.SetMessage(Loc::F("Dry.Stopped", why));
        LogWorker::Instance().RecordLog(m_status.Message(), LogLevel::Warning, "DryRun");
        return false;
    }
}
