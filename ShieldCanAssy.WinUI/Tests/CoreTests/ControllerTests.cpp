// Kiểm thử McControl chạy THẬT trong mô phỏng (FakeAxis/FakeIo/FakeVision) — bản C++ của các kịch bản trong
// ShieldCanAssy.Wpf\SelfTest.cs đi qua McControl: khởi động/tắt, về gốc, alarm sự kiện, EMG/rèm sáng/khí, STOP huỷ chuỗi
// tay, đèn/còi, chọn chế độ, nút tray/chọn ô, chạy rà, và các chu trình Auto (TestAutoCycle, TestAssyPreScan,
// TestResumeIntegrity, TestTrayChangeover, TestTwoInputTrays, TestCellSelection, TestAssyTrayFull, TestVmAutoCycle,
// TestVmFaults, TestVmHalts, TestVmGate).
//
// Khác bản C#: mỗi kịch bản chạy trên một thư mục dữ liệu tạm MỚI và một lần McControl::Init/Shutdown riêng (bản C# chạy
// cả loạt trên một lần Init, bài sau dựa vào trạng thái bài trước). Phần kiểm giao diện WPF (TrayGridViewModel, màu ô,
// VisionReadout card) không port ở đây. Chữ hiển thị là tiếng Anh nên các phép "Message chứa ..." so theo chữ tiếng Anh
// của cùng khoá Loc.
//
// AN TOÀN: máy chạy test là máy lắp ráp thật. Mọi kịch bản kiểm SimulationMode = true TRƯỚC khi Init (thư mục tạm mới nên
// system.json là bản mặc định — mô phỏng); khác đi thì bỏ, không Init. Không mở cổng COM, không nạp EziMOTIONPlusE.dll,
// không chạm VisionMaster (McControl chọn FakeVision khi mô phỏng).
#include "TestKit.h"

#include "Base/Async.h"
#include "Base/Text.h"
#include "Communication/AxisAngle.h"
#include "Communication/GlobalVarSync.h"
#include "Communication/Sim/FakeAxis.h"
#include "Communication/Sim/FakeIo.h"
#include "Communication/Sim/FakeVision.h"
#include "Communication/VmContract.h"
#include "Controller/McControl.h"
#include "Controller/Routine_Auto.h"
#include "Controller/Routine_DryCycle.h"
#include "Controller/VmCycle.h"
#include "Data/McDataManager.h"
#include "Infor/AlarmCatalog.h"
#include "Infor/AlarmManager.h"
#include "Infor/AxisId.h"
#include "Infor/IoMap.h"
#include "SysConfig/AppPaths.h"
#include "Utility/Log/LogWorker.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <format>
#include <functional>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

using namespace sca;
namespace fs = std::filesystem;

namespace
{
    // ───────────────────────── tiện ích ─────────────────────────

    bool WaitUntil(const std::function<bool()>& cond, int timeoutMs)
    {
        base::Stopwatch sw;
        while (sw.ElapsedMilliseconds() < timeoutMs)
        {
            if (cond()) return true;
            base::Delay(10);
        }
        return cond();
    }

    void Sleep(int ms) { base::Delay(ms); }

    /// <summary>SelfTest.Near: |a − b| &lt; 0.02.</summary>
    bool Near(double a, double b) { return std::fabs(a - b) < 0.02; }

    /// <summary>Chứa chuỗi con, không phân biệt hoa thường.</summary>
    bool Has(const std::string& text, std::string_view part) { return base::Contains(base::ToLower(text), base::ToLower(part)); }

    McStatus& St() { return McControl::Status(); }
    EMcState State() { return St().State(); }
    std::string Msg() { return St().Message(); }
    std::string VmState() { return St().StateText() + " — " + St().Message(); }
    bool WaitingOp() { return State() == EMcState::WaitOperator; }

    std::shared_ptr<FakeIo> Io() { return std::dynamic_pointer_cast<FakeIo>(McControl::Io()); }
    std::shared_ptr<FakeVision> Fv() { return std::dynamic_pointer_cast<FakeVision>(McControl::Vision()); }
    std::shared_ptr<McFunction> Func() { return McControl::Func(); }
    std::shared_ptr<RunningData> Run() { return McDataManager::RunData(); }
    std::shared_ptr<ModelData> Model() { return McDataManager::Model(); }
    std::shared_ptr<SystemData> Sys() { return McDataManager::SysData(); }
    std::shared_ptr<IAxis> Axis(std::string_view name) { return McControl::Motion().Axis(name); }

    int CountCells(const TrayState& t, ECellState s) { return (int)std::count(t.Cells.begin(), t.Cells.end(), s); }
    bool AllCells(const TrayState& t, ECellState s) { return CountCells(t, s) == t.CellCount(); }
    bool NoCell(const TrayState& t, ECellState s) { return CountCells(t, s) == 0; }
    std::vector<int> CellsIn(const TrayState& t, ECellState s)
    {
        std::vector<int> v;
        for (int i = 0; i < t.CellCount(); i++) if (t.Get(i) == s) v.push_back(i);
        return v;
    }
    std::string TrayText(const TrayState& t)
    {
        std::string s;
        for (int i = 0; i < t.CellCount(); i++) s += ToString(t.Get(i))[0];
        return s;
    }
    std::string ActiveCodes()
    {
        std::string s;
        for (const auto& a : AlarmManager::ActiveAlarms()) s += std::to_string(a.Code) + " " + a.Detail + "; ";
        return s;
    }
    std::string PendingText() { auto a = AlarmManager::PendingReset(); return a ? std::to_string(a->Code) : std::string("-"); }
    std::string VmAlarm(int code)
    {
        auto a = AlarmManager::Get(code);
        return a ? std::to_string(a->Code) + " — " + a->Detail : "(no alarm " + std::to_string(code) + ")";
    }

    bool AtStandby(const std::string& axis)
    {
        return std::fabs(Axis(axis)->CurrentPos() - Sys()->GetAxis(axis)->StandbyPos) < 0.05;
    }
    std::string PosText(const std::string& axis)
    {
        return std::format("{} {:.2f} / standby {:.2f}", axis, Axis(axis)->CurrentPos(), Sys()->GetAxis(axis)->StandbyPos);
    }
    std::vector<double> AxisPositions()
    {
        std::vector<double> v;
        for (const auto& a : McControl::Motion().AxisList()) v.push_back(a->CurrentPos());
        return v;
    }
    bool SamePositions(const std::vector<double>& before)
    {
        auto now = AxisPositions();
        for (size_t i = 0; i < now.size() && i < before.size(); i++)
            if (std::fabs(now[i] - before[i]) > 1e-6) return false;
        return true;
    }

    /// <summary>Gỡ móc AfterSetDo khi ra khỏi phạm vi — móc giữ tham chiếu tới biến cục bộ của bài.</summary>
    struct HookGuard
    {
        ~HookGuard()
        {
            if (auto io = Io()) io->AfterSetDo = nullptr;
            if (auto fv = Fv()) fv->SetAfterFlow(nullptr);
        }
    };

    // ───────────────────────── dựng / gỡ McControl trên thư mục tạm ─────────────────────────

    fs::path RealAlarmsFile()
    {
        fs::path p = fs::path(__FILE__).parent_path().parent_path().parent_path() / "Config" / "alarms.json";
        if (!fs::exists(p)) p = fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "Config" / "alarms.json";
        return p;
    }

    /// <summary>Một kịch bản = một thư mục dữ liệu tạm mới + một lần McControl::Init (mô phỏng) ... Shutdown.</summary>
    struct Fixture
    {
        bool ok{ false };

        explicit Fixture(const char* name)
        {
            std::printf("  - %s\n", name);
            fs::path root = fs::temp_directory_path() / (std::string("sca_ctrl_") + name + "_" + base::NowString("yyyyMMdd_HHmmssfff"));
            AppPaths::Init(base::PathToU8(root));
            LogWorker::Instance().Init(AppPaths::Root());
            fs::path exeDir = root / "exe";
            fs::create_directories(exeDir / "Config");
            std::string tpl;
            if (base::ReadAllText(RealAlarmsFile(), tpl)) base::WriteAllText(exeDir / "Config" / "alarms.json", tpl);
            AppPaths::SetExeDirForTests(exeDir);
            AlarmCatalog::Load();
            AlarmManager::ForceClearAll();

            McDataManager::LoadSystemData();
            McDataManager::LoadRunningData();
            McDataManager::LoadRecentModel();

            auto sys = Sys();
            // AN TOÀN: chỉ chạy khi chắc chắn mô phỏng.
            CHECK_MSG(sys->SimulationMode && AppPaths::Root() == root, "SimulationMode must be true on a fresh data root");
            if (!sys->SimulationMode) return;

            // Như SelfTest.Run: bỏ hết khoảng chờ đèn/ổn định/chân không/thổi, trục giả chạy nhanh nhất.
            sys->BottomLampMs = sys->TopLampMs = 0;
            sys->BottomSettleMs = sys->TopSettleMs = 0;
            sys->VacuumSettleMs = 0;
            sys->BlowTimeMs = 0;
            sys->VacuumTimeoutMs = 50;
            FakeAxis::SetSpeedFactor(1000);

            McControl::Init();
            if (auto fv = Fv()) fv->SetProcessTimeMs(0);
            WaitUntil([] { return McControl::Motion().AllConnected() && State() == EMcState::NotHomed; }, 2000);
            ok = McControl::IsInitialized() && Fv() != nullptr && Io() != nullptr;
            CHECK_MSG(ok, "McControl::Init in simulation (FakeIo + FakeVision)");
        }

        ~Fixture()
        {
            if (McControl::IsInitialized()) McControl::Shutdown();
            FakeAxis::SetSpeedFactor(1);
            base::WaitBackgroundIdle(3000);
            AlarmManager::ForceClearAll();
            LogWorker::Instance().Flush();
        }
    };

    /// <summary>Nghi thức tay trước Về gốc: SVO on mọi trục, "Mở phanh" Z1/Z2, chờ qua cửa sổ nhả phanh.</summary>
    void ServoAndBrakes()
    {
        for (const auto& a : McControl::Motion().AxisList()) a->SetServoOn(true);
        Func()->RequestBrakeRelease(AxisId::Z1);
        Func()->RequestBrakeRelease(AxisId::Z2);
        std::string why;
        WaitUntil([&] { return Func()->CanMove(AxisId::Z1, why) && Func()->CanMove(AxisId::Z2, why); }, 1500);
    }

    bool Origin()
    {
        McControl::RequestOrigin();
        WaitUntil([] { return State() == EMcState::Homing; }, 2000);
        return WaitUntil([] { return State() != EMcState::Homing; }, 40000) && St().OrgOk();
    }

    bool InitToStandby()
    {
        McControl::RequestInit();
        WaitUntil([] { return State() == EMcState::Homing; }, 2000);
        return WaitUntil([] { return State() != EMcState::Homing; }, 60000);
    }

    void ClearNozzles()
    {
        auto run = Run();
        run->Nozzle1->HasProduct = run->Nozzle2->HasProduct = false;
        run->Nozzle1->SourceCellIndex = run->Nozzle2->SourceCellIndex = -1;
        run->Nozzle1->Aligned = run->Nozzle2->Aligned = false;
        Func()->SetVacuum(1, false);
        Func()->SetVacuum(2, false);
    }

    /// <summary>Sổ hai tray + tay trống + S00.</summary>
    void Prep(ECellState inputCells, ECellState assyCells)
    {
        auto run = Run();
        run->InputTray->ResetAll(inputCells);
        run->AssyTray->ResetAll(assyCells);
        McControl::InvalidateAssyScan();
        ClearNozzles();
        run->AutoState = EAutoState::S00_Ready;
        Sleep(50);
    }

    void StopRoutine()
    {
        EMcState s = State();
        if (s != EMcState::Running && s != EMcState::Pausing && s != EMcState::WaitOperator && s != EMcState::Held) return;
        if (s == EMcState::Held) McControl::RequestSetMode(EMcMode::Manual);
        McControl::RequestPause();
        WaitUntil([] { EMcState x = State(); return x != EMcState::Running && x != EMcState::Pausing && x != EMcState::WaitOperator; }, 60000);
    }

    /// <summary>SelfTest.VmPrepareRun: về gốc, AUTO, không alarm, tray đầu vào đầy / tray lắp ráp trống, tay trống, S00.</summary>
    void PrepareRun()
    {
        StopRoutine();
        AlarmManager::ForceClearAll();
        McControl::RequestReset();
        WaitUntil([] { return State() != EMcState::Alarm && !AlarmManager::HasError(); }, 3000);
        if (!St().OrgOk()) { ServoAndBrakes(); Origin(); }
        McControl::RequestSetMode(EMcMode::Auto);
        WaitUntil([] { return St().Mode() == EMcMode::Auto; }, 2000);

        auto run = Run();
        run->ResetCounters();
        run->NozzleClearRequired = false;
        run->NozzleClearReason.clear();
        run->UnsafePause = false;
        Prep(ECellState::Loaded, ECellState::Empty);
        Sleep(50);
    }

    bool Start()
    {
        McControl::RequestStart();
        return WaitUntil([] { return State() == EMcState::Running; }, 5000);
    }

    bool ResetAndStart()
    {
        McControl::RequestReset();
        WaitUntil([] { return State() != EMcState::Alarm && !AlarmManager::HasError(); }, 3000);
        Sleep(150);
        return Start();
    }

    void ResetAndArm()
    {
        McControl::RequestReset();
        WaitUntil([] { return !AlarmManager::PendingReset(); }, 2000);
        McControl::RequestArm();
        WaitUntil([] { return State() == EMcState::Armed; }, 3000);
    }

    bool PauseToStop() { McControl::RequestPause(); return WaitUntil([] { return State() == EMcState::Paused; }, 30000); }

    void WaitHalted()
    {
        WaitUntil([] { EMcState s = State(); return s == EMcState::Alarm || s == EMcState::Paused || s == EMcState::Idle; }, 15000);
    }

    bool Holding()
    {
        auto run = Run();
        return (run->Nozzle1->HasProduct && Func()->HasProduct(1)) || (run->Nozzle2->HasProduct && Func()->HasProduct(2));
    }

    /// <summary>SelfTest.VmSetup: model mô phỏng mới tinh, xoá mọi lỗi bơm vào FakeVision, dựng lại kho biến, đóng dấu hiệu chuẩn.</summary>
    std::shared_ptr<FakeVision> VmSetup()
    {
        McDataManager::UseModel(ModelData::CreateSimulation());
        McControl::InvalidateVisionCache();
        auto fv = Fv();
        if (!fv) return nullptr;
        fv->SetSimulateOptics(false);
        fv->SetBottomAngleSign(1);
        fv->SetMarkA(0);
        fv->SetForcedObjPose(std::nullopt);
        fv->SetForcePlaceStatusTimes(0);
        fv->SetForceDAngleTimes(0);
        fv->SetTargetShiftTimes(0);
        fv->SetCorruptEchoOnce(false);
        fv->SetAfterFlow(nullptr);
        fv->SetFoundRate(1.0);
        fv->SetBottomFoundRate(1.0);
        fv->SeedVmSolution(VisionGlobalVarCatalog::Build(Sys().get()));
        fv->StampCalibrated(&Model()->Calib);
        return fv;
    }

    // ═════════════════════════ khởi động / an toàn ═════════════════════════

    /// <summary>SelfTest.TestStartupState + Init/Shutdown + công cụ vision của McControl.</summary>
    void TestStartupShutdown()
    {
        Fixture f("StartupShutdown");
        if (!f.ok) return;

        CHECK(McControl::IsInitialized());
        CHECK(McControl::Motion().AxisList().size() == 7);
        CHECK(McControl::Motion().AllConnected());
        CHECK(McControl::Io()->IsConnected());
        CHECK_MSG(State() == EMcState::NotHomed, VmState());
        CHECK(!St().OrgOk());
        CHECK(St().Simulation());
        CHECK_MSG(Fv() != nullptr, "simulation must use FakeVision, never VisionMaster");
        CHECK(AlarmCatalog::Loaded());

        // Model mặc định chưa teach → nạp model giả SIM (InstallSimulationModel), dùng được và đã đóng dấu hiệu chuẩn.
        auto m = Model();
        CHECK_MSG(m && m->InputTray->IsTaught() && m->AssyTray->IsTaught() && m->Calib.IsUsable(), m ? m->Name : "(null)");
        CHECK_MSG(St().ModelName() == m->Name, St().ModelName());
        CHECK_MSG(AllCells(*Run()->InputTray, ECellState::Loaded) && AllCells(*Run()->AssyTray, ECellState::Empty),
                  TrayText(*Run()->InputTray) + " / " + TrayText(*Run()->AssyTray));

        // Nạp solution chạy nền — VisionLoadCount lên 1, VisionConnected bật.
        CHECK_MSG(WaitUntil([] { return McControl::VisionLoadCount() >= 1; }, 3000), std::to_string(McControl::VisionLoadCount()));
        CHECK(WaitUntil([] { return St().VisionConnected(); }, 1000));
        CHECK(!AlarmManager::IsActive(AlarmCode::VisionLoadFailed));

        // Watchdog không báo oan khi vòng quét chạy bình thường (lần quét đầu sau 1 s).
        Sleep(1500);
        CHECK_MSG(!AlarmManager::IsActive(AlarmCode::ControlWatchdog), ActiveCodes());

        // Kiểm biến toàn cục: kho biến giả đã dựng theo danh mục → không thiếu biến nào.
        auto gv = McControl::SyncVisionGlobalVariables(false);
        CHECK_MSG(gv.Error.empty() && gv.Count(EGlobalVarState::Missing) == 0 && gv.Count(EGlobalVarState::WrongType) == 0, gv.ToText());
        // Đối chiếu Output của các Flow với hợp đồng: bản khai giả khớp hợp đồng.
        auto oc = McControl::CheckVisionFlowOutputs();
        CHECK_MSG(oc.Clean(), oc.ToText());

        // Nạp lại solution: không bận → nhận, số lần nạp tăng.
        int loads = McControl::VisionLoadCount();
        CHECK(McControl::ReloadVisionSolution().empty());
        CHECK(WaitUntil([&] { return McControl::VisionLoadCount() > loads; }, 3000));

        // Init lần hai lúc đang chạy: không làm gì (phải Shutdown trước).
        auto ioBefore = McControl::Io();
        McControl::Init();
        CHECK(McControl::Io() == ioBefore);

        McControl::Shutdown();
        CHECK(!McControl::IsInitialized());
        CHECK(!McControl::RoutineBusy());
        CHECK(!ioBefore->IsConnected());
        CHECK(!McControl::Vision()->IsConnected());
    }

    /// <summary>SelfTest.TestOriginSequence.</summary>
    void TestOriginSequence()
    {
        Fixture f("OriginSequence");
        if (!f.ok) return;
        auto z1 = Axis(AxisId::Z1), z2 = Axis(AxisId::Z2);

        // Chưa qua nghi thức thao tác tay thì Về gốc bị PreCheck chặn, không tự bật servo/nhả phanh hộ operator.
        z1->SetServoOn(false);
        z2->SetServoOn(false);
        WaitUntil([] { return !Func()->BrakeReady(AxisId::Z1) && !Func()->BrakeReady(AxisId::Z2); }, 500);
        McControl::RequestOrigin();
        bool blocked = WaitUntil([] { return Has(Msg(), "brake") && State() == EMcState::NotHomed; }, 2000);
        CHECK_MSG(blocked && !St().OrgOk(), VmState());

        // Popup Origin trang Main (Tuấn 2026-10-21): lúc này CHỈ thiếu phanh nên OriginBlockReason rỗng; xác nhận thì
        // ServoOnAndReleaseBrakes làm hộ nghi thức tay (huỷ giữa chừng trả lý do "cancelled"), rồi về gốc như thường.
        CHECK_MSG(!McControl::OriginBrakesReady() && McControl::OriginBlockReason().empty(), McControl::OriginBlockReason());
        {
            std::stop_source cancelled;
            cancelled.request_stop();
            std::string why = McControl::ServoOnAndReleaseBrakes(cancelled.get_token());
            CHECK_MSG(Has(why, "cancelled"), why);
        }
        z1->SetServoOn(false);
        WaitUntil([] { return !Func()->BrakeReady(AxisId::Z1); }, 500);
        std::string prep = McControl::ServoOnAndReleaseBrakes(McControl::ManualToken());
        CHECK_MSG(prep.empty() && McControl::OriginBrakesReady() && McControl::Motion().AllServoOn(), prep);

        bool ok = Origin();
        CHECK_MSG(ok, std::string("last step ") + ToString(St().OriginStep()) + " — " + Msg());
        CHECK(McControl::Motion().AllHomed());
        CHECK_MSG(State() == EMcState::Idle, VmState());
        CHECK(McControl::Motion().ZSafeOk());
        CHECK_MSG(!AlarmManager::HasError(), ActiveCodes());
        CHECK_MSG(Has(Msg(), "Origin done"), Msg());
        CHECK(!Run()->UnsafePause);

        // Về gốc đưa hai tray ra khỏi máy: chạy lại phải bấm Start nạp lại (TraysOut).
        auto autoR = McControl::AutoRoutine();
        CHECK(!autoR->TrayIn(true) && !autoR->TrayIn(false));

        // Tay hút còn hàng và đang có yêu cầu lấy hàng ra → vẫn về gốc được; Về gốc không đụng tới yêu cầu đó.
        auto run = Run();
        Func()->SetVacuum(1, true);
        WaitUntil([] { return Func()->HasProduct(1); }, 500);
        run->NozzleClearRequired = true;
        run->NozzleClearReason = "selftest";
        bool ok2 = Origin();
        CHECK_MSG(ok2 && Func()->HasProduct(1), VmState());
        CHECK(run->NozzleClearRequired);
        Func()->SetVacuum(1, false);
        run->NozzleClearRequired = false;
        run->NozzleClearReason.clear();

        // STOP lúc đang về gốc: huỷ, không bao giờ để trạng thái dở dang là "đã về gốc".
        FakeAxis::SetSpeedFactor(1);
        McControl::RequestOrigin();
        WaitUntil([] { return State() == EMcState::Homing; }, 2000);
        Sleep(100);
        McControl::RequestStop();
        bool cancelled = WaitUntil([] { return State() != EMcState::Homing; }, 10000);
        FakeAxis::SetSpeedFactor(1000);
        CHECK_MSG(cancelled && State() == EMcState::NotHomed, VmState());
        CHECK(!McControl::RoutineBusy());
    }

    /// <summary>SelfTest.TestEventAlarmsAlwaysResettable.</summary>
    void TestEventAlarmsResettable()
    {
        Fixture f("EventAlarms");
        if (!f.ok) return;
        CHECK(WaitUntil([] { return McControl::Safety()->SafeToMove(); }, 2000));
        CHECK_MSG(AlarmManager::ActiveCount() == 0, ActiveCodes());

        int code = AlarmCode::ForAxis(AxisId::Z1, AxisFault::HomeTimeout);
        AlarmManager::RaiseAxisEvent(AxisId::Z1, AxisFault::HomeTimeout, "test");
        CHECK(AlarmManager::IsActive(code));
        auto a = AlarmManager::Get(code);
        CHECK(a && a->Level == EAlarmLevel::Error && !a->ConditionActive);
        CHECK(WaitUntil([] { return !McControl::Safety()->SafeToMove(); }, 500));
        CHECK(WaitUntil([] { return State() == EMcState::Alarm; }, 1000));

        McControl::RequestReset();
        CHECK(WaitUntil([&] { return !AlarmManager::IsActive(code); }, 2000));
        CHECK(WaitUntil([] { return McControl::Safety()->SafeToMove(); }, 1000));
        CHECK_MSG(WaitUntil([] { return State() == EMcState::NotHomed; }, 1000), VmState());
        CHECK_MSG(Has(Msg(), "RESET done"), Msg());

        // Bấm RESET lúc còn alarm chưa hết nguyên nhân: nói rõ vì sao không xoá được.
        AlarmManager::Raise(AlarmCode::RoutineCrashed, "SelfTest");
        WaitUntil([] { return State() == EMcState::Alarm; }, 1000);
        McControl::RequestReset();
        CHECK_MSG(WaitUntil([] { return Has(Msg(), "not cleared"); }, 1000), Msg());
        CHECK(State() == EMcState::Alarm);
        AlarmManager::ClearCondition(AlarmCode::RoutineCrashed);
        McControl::RequestReset();
        CHECK(WaitUntil([] { return State() == EMcState::NotHomed; }, 1000));
    }

    /// <summary>SelfTest.TestEmergencyStop + TestVacuumHoldsThroughEmg.</summary>
    void TestEmergencyStop()
    {
        Fixture f("EmergencyStop");
        if (!f.ok) return;
        auto io = Io();

        io->SimSet(IoMap::EmgOk, false);
        CHECK(WaitUntil([] { return State() == EMcState::Emg; }, 2000));
        CHECK(AlarmManager::IsActive(AlarmCode::EmergencyStop));
        CHECK(!McControl::Safety()->SafeToMove());
        CHECK(McControl::Safety()->AbortToken().stop_requested());
        CHECK_MSG(WaitUntil([] { return St().TowerLamp() == ETowerLamp::RedBlink; }, 500), ToString(St().TowerLamp()));

        auto stuck = AlarmManager::Reset();
        CHECK(std::any_of(stuck.begin(), stuck.end(), [](const AlarmData& a) { return a.Code == AlarmCode::EmergencyStop; }));

        io->SimSet(IoMap::EmgOk, true);
        WaitUntil([] { auto a = AlarmManager::Get(AlarmCode::EmergencyStop); return a && !a->ConditionActive; }, 2000);
        CHECK(AlarmManager::IsActive(AlarmCode::EmergencyStop));
        CHECK_MSG(State() == EMcState::Emg, VmState());   // không tự ra khỏi EMG khi nhả nút

        McControl::RequestReset();
        CHECK(WaitUntil([] { return !AlarmManager::IsActive(AlarmCode::EmergencyStop); }, 2000));
        CHECK_MSG(WaitUntil([] { return State() == EMcState::Idle || State() == EMcState::NotHomed; }, 1000), VmState());

        // Van hút giữ nguyên khi EMG (§3.1 quy tắc 3, §11 #11).
        Func()->SetVacuum(1, true);
        Sleep(50);
        CHECK(Func()->HasProduct(1));
        io->SimSet(IoMap::EmgOk, false);
        WaitUntil([] { return State() == EMcState::Emg; }, 2000);
        CHECK(io->GetDo(IoMap::DoVacuum1));
        CHECK(Func()->HasProduct(1));
        io->SimSet(IoMap::EmgOk, true);
        McControl::RequestReset();
        WaitUntil([] { return !AlarmManager::IsActive(AlarmCode::EmergencyStop); }, 2000);
        Func()->SetVacuum(1, false);
        Sleep(50);
        CHECK(!Func()->HasProduct(1));
    }

    /// <summary>SelfTest.TestLightCurtain + cảm biến khí.</summary>
    void TestLightCurtainAndAir()
    {
        Fixture f("CurtainAir");
        if (!f.ok) return;
        auto io = Io();
        auto sys = Sys();

        sys->HasLightCurtain = true;
        CHECK_MSG(WaitUntil([] { return McControl::Safety()->SafeToMove(); }, 2000), McControl::Safety()->BlockReason());
        io->SimSet(IoMap::LightCurtainOk, false);
        WaitUntil([] { return !McControl::Safety()->SafeToMove(); }, 2000);
        CHECK_MSG(AlarmManager::ActiveCount() == 0, ActiveCodes());
        CHECK(!McControl::Safety()->SafeToMove());
        CHECK_MSG(Has(McControl::Safety()->BlockReason(), "curtain"), McControl::Safety()->BlockReason());
        CHECK(!St().LightCurtainOk());

        // Rèm bị cắt chặn cả lệnh về gốc thợ chủ động bấm.
        McControl::RequestOrigin();
        Sleep(200);
        CHECK_MSG(State() != EMcState::Homing && Has(Msg(), "Cannot Origin"), VmState());

        io->SimSet(IoMap::LightCurtainOk, true);
        CHECK(WaitUntil([] { return McControl::Safety()->SafeToMove(); }, 2000));

        // Tắt HasLightCurtain: cắt DI rèm không ảnh hưởng gì.
        sys->HasLightCurtain = false;
        io->SimSet(IoMap::LightCurtainOk, false);
        Sleep(100);
        CHECK(McControl::Safety()->SafeToMove());
        CHECK(!AlarmManager::IsActive(AlarmCode::LightCurtainBroken));
        io->SimSet(IoMap::LightCurtainOk, true);

        // Cảm biến khí: bật cờ rồi mất khí → alarm 1103, chặn chuyển động; có khí lại → hết nguyên nhân, RESET gỡ được.
        sys->HasAirSensor = true;
        io->SimSet(IoMap::AirOk, false);
        CHECK(WaitUntil([] { return AlarmManager::IsActive(AlarmCode::AirPressureLow); }, 1000));
        CHECK(WaitUntil([] { return !McControl::Safety()->SafeToMove(); }, 500));
        CHECK(!St().AirOk());
        io->SimSet(IoMap::AirOk, true);
        WaitUntil([] { auto a = AlarmManager::Get(AlarmCode::AirPressureLow); return !a || !a->ConditionActive; }, 1000);
        McControl::RequestReset();
        CHECK(WaitUntil([] { return !AlarmManager::IsActive(AlarmCode::AirPressureLow); }, 2000));
        CHECK(WaitUntil([] { return McControl::Safety()->SafeToMove(); }, 1000));
        sys->HasAirSensor = false;
    }

    /// <summary>SelfTest.TestStopCancelsManualSequences.</summary>
    void TestStopCancelsManualSequences()
    {
        Fixture f("StopManualToken");
        if (!f.ok) return;
        std::stop_token token = McControl::ManualToken();
        CHECK(!token.stop_requested());
        CHECK(McControl::ManualToken() == token);   // chưa huỷ thì lấy lại đúng token đó
        std::stop_token safetyBefore = McControl::Safety()->AbortToken();
        McControl::RequestStop();
        CHECK(WaitUntil([&] { return token.stop_requested(); }, 2000));
        CHECK(!safetyBefore.stop_requested());
        CHECK(!McControl::ManualToken().stop_requested());

        // Token Safety bị huỷ (EMG) thì chuỗi tay cũng bị huỷ; sau RESET lấy được token mới.
        std::stop_token t2 = McControl::ManualToken();
        Io()->SimSet(IoMap::EmgOk, false);
        CHECK(WaitUntil([&] { return t2.stop_requested(); }, 2000));
        Io()->SimSet(IoMap::EmgOk, true);
        McControl::RequestReset();
        WaitUntil([] { return !AlarmManager::IsActive(AlarmCode::EmergencyStop); }, 2000);
        // DoReset xoá alarm (AlarmManager::Reset) RỒI mới RenewToken — đọc ngay giữa hai bước thì token Safety còn là
        // cái đã huỷ. Chờ thay vì đọc một lần (test chập chờn 2026-10-18).
        CHECK(WaitUntil([] { return !McControl::ManualToken().stop_requested(); }, 2000));
    }

    /// <summary>Đèn tháp, đèn nút, còi và tắt còi — McControl.UpdateOutputs/ComputeLamp.</summary>
    void TestLampsAndBuzzer()
    {
        Fixture f("LampsBuzzer");
        if (!f.ok) return;
        auto io = Io();

        CHECK_MSG(WaitUntil([] { return St().TowerLamp() == ETowerLamp::Yellow; }, 500), ToString(St().TowerLamp()));
        CHECK(WaitUntil([&] { return io->GetDo(IoMap::DoLampResetBtn); }, 500));      // STOP sáng lúc đứng
        CHECK(!io->GetDo(IoMap::DoLampStartBtn) && !io->GetDo(IoMap::DoLampStartBtnB) && !io->GetDo(IoMap::DoLampStartBtnB2));
        CHECK(!io->GetDo(IoMap::DoBuzzer) && !St().Buzzer());

        // Lỗi Error: đèn đỏ, còi kêu; tắt còi thì im mà vẫn ở LỖI; RESET xong cờ tắt còi tự về false.
        AlarmManager::RaiseEvent(AlarmCode::RoutineCrashed, "SelfTest");
        CHECK(WaitUntil([] { return State() == EMcState::Alarm; }, 1000));
        CHECK(WaitUntil([] { return St().TowerLamp() == ETowerLamp::Red; }, 500));
        CHECK(WaitUntil([&] { return io->GetDo(IoMap::DoLampRed) && io->GetDo(IoMap::DoBuzzer) && St().Buzzer(); }, 500));
        McControl::RequestMuteBuzzer();
        CHECK(WaitUntil([&] { return !io->GetDo(IoMap::DoBuzzer) && St().BuzzerMuted(); }, 500));
        CHECK(State() == EMcState::Alarm && AlarmManager::HasError());
        McControl::RequestReset();
        CHECK(WaitUntil([] { return State() == EMcState::NotHomed && !St().BuzzerMuted(); }, 1000));
        CHECK(!io->GetDo(IoMap::DoBuzzer));

        // Alarm mới sau khi đã tắt còi lần trước: lại kêu.
        AlarmManager::RaiseEvent(AlarmCode::RoutineCrashed, "SelfTest");
        CHECK(WaitUntil([&] { return io->GetDo(IoMap::DoBuzzer); }, 1000));
        McControl::RequestReset();
        WaitUntil([] { return State() == EMcState::NotHomed; }, 1000);

        // Về gốc: tháp vàng nháy; xong: vàng. Armed: xanh nháy, đèn Start TẮT suốt (không nháy), STOP sáng.
        ServoAndBrakes();
        CHECK(Origin());
        CHECK(WaitUntil([] { return St().TowerLamp() == ETowerLamp::Yellow; }, 500));
        McControl::RequestSetMode(EMcMode::Auto);
        CHECK(WaitUntil([] { return State() == EMcState::Armed; }, 2000));
        CHECK(WaitUntil([] { return St().TowerLamp() == ETowerLamp::GreenBlink; }, 500));
        int startOn = 0, stopOff = 0;
        base::Stopwatch sw;
        while (sw.ElapsedMilliseconds() < 1200)
        {
            if (io->GetDo(IoMap::DoLampStartBtn) || io->GetDo(IoMap::DoLampStartBtnB) || io->GetDo(IoMap::DoLampStartBtnB2)) startOn++;
            if (!io->GetDo(IoMap::DoLampResetBtn)) stopOff++;
            Sleep(10);
        }
        CHECK_MSG(startOn == 0 && stopOff == 0, std::format("start lamps on {}x, STOP lamp off {}x in 1.2 s", startOn, stopOff));
    }

    /// <summary>Chọn chế độ, AutoModeRefused, START mềm, Init, chọn tay hút — McControl.UpdateMode/DoArm/DoGo/DoInit.</summary>
    void TestModeSwitch()
    {
        Fixture f("ModeSwitch");
        if (!f.ok) return;
        auto io = Io();

        std::mutex m;
        std::string refused;
        int token = McControl::AutoModeRefused().Subscribe([&](std::string s) { std::lock_guard<std::mutex> l(m); refused = s; });
        McControl::RequestSetMode(EMcMode::Auto);
        bool got = WaitUntil([&] { std::lock_guard<std::mutex> l(m); return !refused.empty(); }, 2000);
        Sleep(100);
        {
            std::lock_guard<std::mutex> l(m);
            CHECK_MSG(got && Has(refused, "AUTO refused") && Has(refused, "Origin"), refused);
        }
        CHECK(St().Mode() == EMcMode::Manual && State() == EMcState::NotHomed);

        // Ở MANUAL: START mềm báo chọn AUTO; nút Start cứng không vào sẵn sàng; START cũ (self-test) báo vặn AUTO.
        McControl::RequestArm();
        CHECK_MSG(WaitUntil([] { return Has(Msg(), "Select AUTO"); }, 1000), Msg());
        ServoAndBrakes();
        CHECK(Origin());
        St().SetMessage({});
        io->SimPress(IoMap::BtnGoB2);
        Sleep(200);
        CHECK_MSG(State() != EMcState::Armed && State() != EMcState::Running && Has(Msg(), "AUTO"), VmState());
        McControl::RequestStart();
        CHECK_MSG(WaitUntil([] { return Has(Msg(), "Key to AUTO"); }, 1000), Msg());

        // Đã về gốc: chọn AUTO là vào luôn Armed; về MANUAL thì huỷ Armed về Idle.
        McControl::RequestSetMode(EMcMode::Auto);
        CHECK_MSG(WaitUntil([] { return St().Mode() == EMcMode::Auto && State() == EMcState::Armed; }, 2000), VmState());
        CHECK_MSG(Has(Msg(), "Ready"), Msg());
        McControl::RequestSetMode(EMcMode::Manual);
        CHECK_MSG(WaitUntil([] { return St().Mode() == EMcMode::Manual && State() == EMcState::Idle; }, 2000), VmState());
        {
            std::lock_guard<std::mutex> l(m);
            refused.clear();
        }

        // START mềm ở AUTO sau khi rời Armed: vào lại Armed.
        McControl::RequestSetMode(EMcMode::Auto);
        WaitUntil([] { return State() == EMcState::Armed; }, 2000);
        McControl::RequestStop();   // Armed + STOP: không phải trạng thái chạy — dừng trục, máy ở nguyên
        Sleep(100);
        CHECK_MSG(State() == EMcState::Armed, VmState());

        // Init: đưa mọi trục về vị trí chờ, không về gốc lại.
        McControl::RequestSetMode(EMcMode::Manual);
        WaitUntil([] { return State() == EMcState::Idle; }, 2000);
        Run()->UnsafePause = true;
        bool inited = InitToStandby();
        bool atStandby = true;
        for (const auto& a : McControl::Motion().AxisList())
        {
            auto cfg = a->Config();
            double d = cfg->WrapDegrees ? AxisAngle::Distance(a->CurrentPos(), cfg->StandbyPos) : std::fabs(a->CurrentPos() - cfg->StandbyPos);
            if (d >= 0.02) atStandby = false;
        }
        CHECK_MSG(inited && atStandby && St().OrgOk() && !Run()->UnsafePause && State() == EMcState::Idle, VmState());
        CHECK_MSG(Has(Msg(), "Init done"), Msg());

        // Chọn tay hút: lúc dừng thì nhận và lưu; số lạ thì về cả hai tay.
        McControl::SetOnlyNozzle(2);
        CHECK_MSG(Sys()->OnlyNozzle == 2 && Has(Msg(), "Nozzle #2 only"), Msg());
        McControl::SetOnlyNozzle(7);
        CHECK_MSG(Sys()->OnlyNozzle == 0 && Has(Msg(), "Both nozzles"), Msg());

        McControl::AutoModeRefused().Unsubscribe(token);
    }

    /// <summary>Nút tray trang Main, chọn ô (luật), đồng bộ tay hút theo cảm biến — McControl.ReloadInputTray/ResetAssyTray/
    /// ToggleCellSelection/ClearCellSelection/SyncNozzleStateFromSensor.</summary>
    void TestTrayButtons()
    {
        Fixture f("TrayButtons");
        if (!f.ok) return;
        auto run = Run();

        run->InputTray->ResetAll(ECellState::Ng);
        McControl::ReloadInputTray();
        CHECK_MSG(AllCells(*run->InputTray, ECellState::Loaded), TrayText(*run->InputTray));

        run->AssyTray->ResetAll(ECellState::Ok);
        run->AssyTrayStartedAt = std::chrono::system_clock::now();
        St().SetMessage({});
        McControl::ResetAssyTray();
        CHECK(AllCells(*run->AssyTray, ECellState::Empty) && !run->AssyTrayStartedAt.has_value());
        CHECK(Msg().empty());
        Func()->SetVacuum(1, true);
        WaitUntil([] { return Func()->HasProduct(1); }, 500);
        McControl::ResetAssyTray();
        CHECK_MSG(Has(Msg(), "product on nozzle"), Msg());
        CHECK(!run->NozzleClearRequired);

        // Đồng bộ sổ tay hút theo cảm biến (máy đứng yên).
        run->Nozzle1->HasProduct = false;
        run->Nozzle2->HasProduct = true;
        run->Nozzle2->SourceCellIndex = 4;
        McControl::SyncNozzleStateFromSensor();
        CHECK(run->Nozzle1->HasProduct && !run->Nozzle2->HasProduct && run->Nozzle2->SourceCellIndex == -1);
        CHECK_MSG(Has(Msg(), "synced"), Msg());
        Func()->SetVacuum(1, false);
        WaitUntil([] { return !Func()->HasProduct(1); }, 500);
        McControl::SyncNozzleStateFromSensor();
        CHECK(!run->Nozzle1->HasProduct);

        // Chọn ô: ở MANUAL không chọn được.
        McControl::ClearCellSelection(true);
        McControl::ClearCellSelection(false);
        McControl::ToggleCellSelection(true, 0);
        CHECK(!run->InputTray->HasSelection());

        ServoAndBrakes();
        CHECK(Origin());
        McControl::RequestSetMode(EMcMode::Auto);
        CHECK(WaitUntil([] { return St().Mode() == EMcMode::Auto; }, 2000));

        // Tray còn việc: ô đã xong không chọn được, báo rõ.
        run->InputTray->ResetAll(ECellState::Loaded);
        run->AssyTray->ResetAll(ECellState::Empty);
        run->InputTray->Set(0, ECellState::Ok);
        run->AssyTray->Set(0, ECellState::Ok);
        St().SetMessage({});
        McControl::ToggleCellSelection(true, 0);
        std::string inputRefused = Msg();
        McControl::ToggleCellSelection(false, 0);
        CHECK_MSG(!run->InputTray->HasSelection() && !run->AssyTray->HasSelection() &&
                  Has(inputRefused, "no part") && Has(Msg(), "not empty"), inputRefused + " | " + Msg());
        // Chỉ số ngoài tray: bỏ qua.
        McControl::ToggleCellSelection(true, -1);
        McControl::ToggleCellSelection(true, run->InputTray->CellCount());
        CHECK(!run->InputTray->HasSelection());

        // Chọn, bấm lại là bỏ, Bỏ chọn xoá hết.
        McControl::ToggleCellSelection(true, 3);
        McControl::ToggleCellSelection(true, 6);
        McControl::ToggleCellSelection(false, 4);
        McControl::ClearCellSelection(false);
        McControl::ToggleCellSelection(false, 2);
        McControl::ToggleCellSelection(false, 9);
        McControl::ToggleCellSelection(false, 5);
        McControl::ToggleCellSelection(false, 5);
        auto si = run->InputTray->Selected(); std::sort(si.begin(), si.end());
        auto sa = run->AssyTray->Selected(); std::sort(sa.begin(), sa.end());
        CHECK(si == std::vector<int>({ 3, 6 }) && sa == std::vector<int>({ 2, 9 }));

        // Tray đã hết (không còn ô có hàng/đang làm): ô nào cũng chọn được — chọn trước cho tray sắp đặt vào.
        McControl::ClearCellSelection(true);
        run->InputTray->ResetAll(ECellState::Ok);
        McControl::ToggleCellSelection(true, 0);
        CHECK(run->InputTray->IsSelected(0));
        McControl::ClearCellSelection(true);
        McControl::ClearCellSelection(false);
        CHECK(!run->InputTray->HasSelection() && !run->AssyTray->HasSelection());
        McControl::RequestSetMode(EMcMode::Manual);
    }

    // ═════════════════════════ chạy rà ═════════════════════════

    /// <summary>SelfTest.TestDryRun.</summary>
    void TestDryRun()
    {
        // ── Chia ô cho hai tay đúng luật của Auto, thuần tính toán ──
        {
            auto sim = ModelData::CreateSimulation();
            auto inRun = TrayState::Create("in", sim->InputTray->Rows(), sim->InputTray->Cols());
            inRun->ScanOrder = EScanOrder::Serpentine;
            auto asRun = TrayState::Create("assy", sim->AssyTray->Rows(), sim->AssyTray->Cols());
            asRun->ScanOrder = EScanOrder::Serpentine;
            auto xOf = [](const TrayState& t, const TrayLayout& l, int i)
            {
                int r, c;
                t.RowCol(i, r, c);
                return l.CellPos(AxisId::Z1, r, c).X;
            };

            int pi1 = Routine_DryCycle::PickCellFor(*sim, *inRun, 6, 1), pi2 = Routine_DryCycle::PickCellFor(*sim, *inRun, 6, 2);
            CHECK_MSG(pi1 != pi2 && xOf(*inRun, *sim->InputTray, pi1) < xOf(*inRun, *sim->InputTray, pi2),
                      std::format("Tool 1 cell {}, Tool 2 cell {}", pi1, pi2));
            int pa1 = Routine_DryCycle::PlaceCellFor(*sim, *asRun, 4, 1), pa2 = Routine_DryCycle::PlaceCellFor(*sim, *asRun, 4, 2);
            CHECK_MSG(pa1 != pa2 && xOf(*asRun, *sim->AssyTray, pa1) < xOf(*asRun, *sim->AssyTray, pa2),
                      std::format("Tool 1 cell {}, Tool 2 cell {}", pa1, pa2));

            // Cặp vắt hàng CÙNG CỘT của zigzag, tray hơi nghiêng: Tool 1 lấy ô ghé trước — Y chỉ đi tới.
            auto skew = ModelData::CreateSimulation();
            auto p1 = skew->InputTray->GeoZ1.PR0; auto p2 = skew->InputTray->GeoZ2.PR0;
            skew->InputTray->GeoZ1.PR0 = TeachPoint(p1.X - 0.36, p1.Y);
            skew->InputTray->GeoZ2.PR0 = TeachPoint(p2.X - 0.08, p2.Y);
            bool noTie = Routine_Auto::Nozzle1TakesFirstPickCell(
                skew->InputTray->CellPos(AxisId::Z1, 0, 4).X, skew->InputTray->CellPos(AxisId::Z2, 0, 4).X,
                skew->InputTray->CellPos(AxisId::Z1, 1, 4).X, skew->InputTray->CellPos(AxisId::Z2, 1, 4).X);
            int ps1 = Routine_DryCycle::PickCellFor(*skew, *inRun, 4, 1), ps2 = Routine_DryCycle::PickCellFor(*skew, *inRun, 4, 2);
            CHECK_MSG(!noTie && ps1 == 4 && ps2 == 5, std::format("Tool 1 cell {}, Tool 2 cell {}", ps1, ps2));
            CHECK(Routine_Auto::Nozzle1TakesFirstCell(400.03, 400.0, skew->AssyTray->HalfColPitch()) &&
                  !Routine_Auto::Nozzle1TakesFirstCell(400.03, 400.0) &&
                  !Routine_Auto::Nozzle1TakesFirstCell(422.0, 400.0, skew->AssyTray->HalfColPitch()));
        }

        Fixture f("DryRun");
        if (!f.ok) return;
        HookGuard hooks;
        auto io = Io();
        auto run = Run();
        auto fv = Fv();
        auto dry = McControl::DryRoutine();

        ServoAndBrakes();
        CHECK(Origin());
        McControl::RequestSetMode(EMcMode::Auto);
        WaitUntil([] { return St().Mode() == EMcMode::Auto; }, 2000);

        // Tay còn hàng thì không bật được.
        Func()->SetVacuum(1, true);
        WaitUntil([] { return Func()->HasProduct(1); }, 2000);
        McControl::SetDryRun(true);
        CHECK_MSG(!St().DryRun() && Has(Msg(), "remove before Dry run"), Msg());
        Func()->SetVacuum(1, false);
        WaitUntil([] { return !Func()->HasProduct(1); }, 2000);
        McControl::SyncNozzleStateFromSensor();

        // Nút gạt chỉ CHỌN chế độ, không chạy máy.
        McControl::SetDryRun(true);
        Sleep(300);
        CHECK_MSG(St().DryRun() && State() != EMcState::Running, St().StateText());
        CHECK(dry->Strokes() == 0 && dry->Laps() == 0 && dry->Sweeps() == 0);

        // Đi ĐÚNG đường của người vận hành: vào Armed rồi bấm nút Start cứng (RequestGo).
        auto armAndGo = []
        {
            ResetAndArm();
            McControl::RequestGo();
        };

        int ok0 = run->CountOk, ng0 = run->CountNg;
        int flow0 = fv->VmCallCount();
        auto cells0 = run->AssyTray->Cells;
        std::atomic<bool> airTouched{ false };
        io->AfterSetDo = [&](int bit, bool on)
        {
            if (!on) return;
            if (bit == IoMap::DoVacuum1 || bit == IoMap::DoVacuum2 || bit == IoMap::DoBlow1 || bit == IoMap::DoBlow2) airTouched = true;
        };

        armAndGo();
        bool running = WaitUntil([] { return State() == EMcState::Running; }, 3000);
        CHECK_MSG(running && WaitUntil([&] { return dry->Elapsed().count() > 0; }, 3000), Msg());
        CHECK_MSG(Has(Msg(), "Dry run"), Msg());

        double maxA1 = 0, maxA2 = 0;
        bool ran = WaitUntil([&]
        {
            maxA1 = std::max(maxA1, std::fabs(dry->LastPlaceAngle(1)));
            maxA2 = std::max(maxA2, std::fabs(dry->LastPlaceAngle(2)));
            return dry->Strokes() >= 4;
        }, 120000);
        CHECK_MSG(ran, std::format("{} strokes — {}", dry->Strokes(), Msg()));
        // Đèn Start sáng khi đang chạy rà (Running).
        CHECK(WaitUntil([&] { return io->GetDo(IoMap::DoLampStartBtn) || State() != EMcState::Running; }, 500));

        bool stopped = PauseToStop();
        io->AfterSetDo = nullptr;
        CHECK_MSG(stopped, St().StateText());
        // Routine_DryCycle::Paused đặt trạng thái TRƯỚC rồi mới đặt câu báo — chờ câu báo (test chập chờn 2026-10-18).
        CHECK_MSG(WaitUntil([] { return Has(Msg(), "Dry run stopped"); }, 2000), Msg());
        CHECK(McControl::Motion().ZSafeOk());
        CHECK_MSG(dry->Sweeps() >= 1, std::to_string(dry->Sweeps()));
        CHECK_MSG(Sys()->ScanAssy() != SystemData::EScanAssy::Tray || dry->Sweeps() == 1,
                  std::format("{} sweeps after {} strokes", dry->Sweeps(), dry->Strokes()));
        CHECK(!airTouched);
        CHECK_MSG(fv->VmCallCount() == flow0, std::format("{} -> {}", flow0, fv->VmCallCount()));
        CHECK(run->CountOk == ok0 && run->CountNg == ng0);
        CHECK(run->AssyTray->Cells == cells0);
        double maxComp = std::fabs(Sys()->MaxCompensationAngleDeg);
        CHECK_MSG(maxA1 > 0.5 && maxA2 > 0.5, std::format("A1 {:.2f}, A2 {:.2f}", maxA1, maxA2));
        double span = maxComp / 3.0;
        CHECK_MSG(std::fabs(maxA1 - span) <= 0.05 && std::fabs(maxA2 - span) <= 0.05,
                  std::format("A1 {:.2f}, A2 {:.2f}, expected {:.2f}", maxA1, maxA2, span));
        CHECK(std::max(maxA1, maxA2) < maxComp - 0.05);

        // Chạy tiếp sau STOP: đếm cộng dồn, không về 0.
        int strokesBefore = dry->Strokes();
        armAndGo();
        WaitUntil([] { return State() == EMcState::Running; }, 3000);
        McControl::SetDryRun(false);
        CHECK_MSG(St().DryRun() && Has(Msg(), "stop before changing Dry run"), Msg());
        // STOP khi chạy rà: tạm dừng (Held), Start chạy tiếp.
        McControl::RequestStop();
        CHECK_MSG(WaitUntil([] { return State() == EMcState::Held; }, 3000), VmState());
        McControl::RequestGo();
        // 3 s thay vì 1 s: vòng quét có lúc chậm khi ghi running.json (Defender quét file) — chập chờn 2026-10-18.
        CHECK_MSG(WaitUntil([] { return State() == EMcState::Running && Has(Msg(), "Dry run"); }, 3000), VmState());
        CHECK(WaitUntil([&] { return dry->Strokes() > strokesBefore; }, 60000));
        PauseToStop();

        McControl::SetDryRun(false);
        CHECK(!St().DryRun());
        // Bật lại thì đếm về vạch xuất phát.
        McControl::SetDryRun(true);
        CHECK(dry->Strokes() == 0 && dry->Sweeps() == 0);
        McControl::SetDryRun(false);
    }

    // ═════════════════════════ chu trình Auto ═════════════════════════

    /// <summary>SelfTest.TestAutoCycle (bỏ phần kiểm giao diện WPF).</summary>
    void TestAutoCycle()
    {
        Fixture f("AutoCycle");
        if (!f.ok) return;
        HookGuard hooks;
        auto io = Io();
        auto run = Run();
        auto model = Model();

        ServoAndBrakes();
        CHECK(Origin());
        CHECK_MSG(model->InputTray->IsTaught() && model->AssyTray->IsTaught() && model->Calib.IsUsable(), model->Name);

        run->ResetCounters();
        Prep(ECellState::Loaded, ECellState::Empty);

        // START khi còn ở MANUAL: từ chối.
        McControl::RequestStart();
        Sleep(200);
        CHECK_MSG(State() != EMcState::Running, Msg());
        St().SetMessage({});
        io->SimPress(IoMap::BtnGoB2);
        Sleep(200);
        CHECK_MSG(State() != EMcState::Running && State() != EMcState::Armed && Has(Msg(), "AUTO"), VmState());

        McControl::RequestSetMode(EMcMode::Auto);
        WaitUntil([] { return St().Mode() == EMcMode::Auto; }, 2000);
        CHECK_MSG(WaitUntil([] { return State() == EMcState::Armed; }, 1000), VmState());

        // Đọc góc A ngay lúc van hút mở — mọi lần hút phải ở đúng GÓC HÚT đã teach của tay đó.
        std::mutex mx;
        int pickCount = 0;
        double worstPickA = 0, worstReleaseA = 0;
        io->AfterSetDo = [&](int bit, bool on)
        {
            int nz = bit == IoMap::DoVacuum1 ? 1 : bit == IoMap::DoVacuum2 ? 2 : 0;
            if (nz == 0) return;
            double a = Axis(nz == 1 ? AxisId::A1 : AxisId::A2)->CurrentPos();
            std::lock_guard<std::mutex> l(mx);
            if (on) { pickCount++; worstPickA = std::max(worstPickA, AxisAngle::Distance(a, model->PickAngle(nz))); }
            else worstReleaseA = std::max(worstReleaseA, AxisAngle::Distance(a, 0));
        };

        McControl::RequestStart();
        CHECK_MSG(WaitUntil([] { return State() == EMcState::Running; }, 3000), Msg());
        CHECK(WaitUntil([&] { return io->GetDo(IoMap::DoLampStartBtn) && io->GetDo(IoMap::DoLampStartBtnB) && io->GetDo(IoMap::DoLampStartBtnB2); }, 500));
        CHECK(WaitUntil([&] { return !io->GetDo(IoMap::DoLampResetBtn); }, 500));
        CHECK(WaitUntil([] { return St().TowerLamp() == ETowerLamp::Green; }, 500));
        CHECK(WaitUntil([&] { return io->GetDo(IoMap::DoLampCam1) || io->GetDo(IoMap::DoLampCam2); }, 5000));
        CHECK(McControl::RoutineBusy());

        bool made = WaitUntil([&] { return run->CountOk >= 2; }, 120000);
        CHECK_MSG(made, std::format("OK={} NG={} step={} — {}", run->CountOk, run->CountNg, ToString(run->AutoState), Msg()));
        CHECK_MSG(!AlarmManager::HasError(), ActiveCodes());
        CHECK(CountCells(*run->AssyTray, ECellState::Ok) >= 2);
        CHECK(CountCells(*run->InputTray, ECellState::Ok) >= 2);
        CHECK(run->InputCountOk >= 2 && run->AssyCountOk >= 2);
        CHECK(!McControl::AutoRoutine()->AssyScannedCells().empty());
        // CountOk tăng ở bước đặt; thời gian chu kỳ chỉ ghi ở S40_Index sau đó — chờ, không đọc ngay (test chập chờn 2026-10-18).
        WaitUntil([&] { return run->LastCycleTimeSec > 0 && run->AvgProductTimeSec > 0; }, 10000);
        CHECK_MSG(run->LastCycleTimeSec > 0, std::to_string(run->LastCycleTimeSec));
        CHECK(run->LastProductTimeSec > 0 && run->AvgProductTimeSec > 0 && run->LastProductTimeSec <= run->LastCycleTimeSec);
        CHECK(McControl::CurrentCycleSec() >= 0);
        {
            std::lock_guard<std::mutex> l(mx);
            CHECK_MSG(pickCount > 0 && worstPickA < 0.05, std::format("{} picks, worst {:.3f}°", pickCount, worstPickA));
            CHECK_MSG(worstReleaseA > 1.0, std::format("max release angle {:.3f}°", worstReleaseA));
        }
        io->AfterSetDo = nullptr;

        // STOP (bit RUN): mọi trục dừng ngay, chu trình đứng nguyên; Start chạy tiếp đúng chỗ dở.
        McControl::RequestStop();
        CHECK_MSG(WaitUntil([] { return State() == EMcState::Held; }, 1000), St().StateText());
        CHECK(!Func()->RunBit());
        Sleep(300);
        auto heldPos = AxisPositions();
        int okHeld = run->CountOk;
        EAutoState stepHeld = run->AutoState;
        Sleep(800);
        CHECK_MSG(SamePositions(heldPos) && !McControl::Motion().AnyBusy() && run->CountOk == okHeld && run->AutoState == stepHeld &&
                  State() == EMcState::Held, std::format("step {} -> {}", ToString(stepHeld), ToString(run->AutoState)));
        CHECK(WaitUntil([&] { return io->GetDo(IoMap::DoLampResetBtn) && St().TowerLamp() == ETowerLamp::Yellow; }, 500));
        CHECK(!io->GetDo(IoMap::DoLampStartBtn));
        // Đang tạm dừng: Init bị từ chối, không đổi được chạy rà / tay hút.
        McControl::RequestInit();
        CHECK_MSG(WaitUntil([] { return Has(Msg(), "MANUAL (abort cycle) before Init"); }, 1000), Msg());
        McControl::SetOnlyNozzle(1);
        CHECK(Sys()->OnlyNozzle == 0);
        io->SimPress(IoMap::BtnArmA1);
        CHECK_MSG(WaitUntil([] { return State() == EMcState::Running; }, 1000), Msg());
        // McControl đặt Running TRƯỚC rồi mới bật bit RUN (cùng một lượt quét) — chờ, không đọc ngay (chập chờn 2026-10-28).
        CHECK(WaitUntil([] { return Func()->RunBit(); }, 500));
        CHECK_MSG(WaitUntil([&] { return run->CountOk > okHeld; }, 120000) && !AlarmManager::HasError(),
                  std::format("OK {} -> {} — {}", okHeld, run->CountOk, Msg()));

        // Rèm sáng bị cắt lúc đang chạy: tạm dừng như STOP, không alarm; rèm còn cắt thì Start không chạy tiếp.
        auto sys = Sys();
        sys->HasLightCurtain = true;
        io->SimSet(IoMap::LightCurtainOk, false);
        CHECK_MSG(WaitUntil([] { return State() == EMcState::Held; }, 1000) && !AlarmManager::HasError() && AlarmManager::ActiveCount() == 0,
                  VmState());
        CHECK_MSG(Has(Msg(), "Curtain"), Msg());
        Sleep(300);
        auto curtainPos = AxisPositions();
        int okCurtain = run->CountOk;
        EAutoState stepCurtain = run->AutoState;
        Sleep(500);
        CHECK(SamePositions(curtainPos) && !McControl::Motion().AnyBusy() && run->CountOk == okCurtain && run->AutoState == stepCurtain);
        io->SimPress(IoMap::BtnArmA2);
        Sleep(300);
        CHECK_MSG(State() == EMcState::Held && Has(Msg(), "Cannot resume"), Msg());
        io->SimSet(IoMap::LightCurtainOk, true);
        Sleep(200);
        CHECK_MSG(State() == EMcState::Held && run->AutoState == stepCurtain, St().StateText());
        io->SimPress(IoMap::BtnGoB2);
        CHECK_MSG(WaitUntil([] { return State() == EMcState::Running; }, 1000) &&
                  WaitUntil([&] { return run->AutoState != stepCurtain || run->CountOk > okCurtain; }, 30000) && !AlarmManager::HasError(),
                  VmState());
        sys->HasLightCurtain = false;

        // Dừng ở checkpoint: chạy nốt bước đang làm rồi thoát, không dừng giữa chừng.
        McControl::RequestPause();
        CHECK(WaitUntil([] { return State() == EMcState::Pausing || State() == EMcState::Paused; }, 2000));
        CHECK_MSG(WaitUntil([] { return State() == EMcState::Paused; }, 30000), Msg());
        CHECK(!run->UnsafePause);
        CHECK(WaitUntil([&] { return !io->GetDo(IoMap::DoLampStartBtn) && !io->GetDo(IoMap::DoLampStartBtnB) && !io->GetDo(IoMap::DoLampStartBtnB2); }, 500));
        CHECK(WaitUntil([&] { return io->GetDo(IoMap::DoLampResetBtn); }, 500));
        CHECK(WaitUntil([] { return St().TowerLamp() == ETowerLamp::Yellow; }, 500));
        CHECK(run->SavedAt > TimePoint{});
        CHECK(!McControl::RoutineBusy());

        // Tray đã vào vùng làm việc mà cảm biến tray OFF — máy KHÔNG dừng oan.
        std::string rowIn = model->InputTray->AxisRow, rowAssy = model->AssyTray->AxisRow;
        auto cfgIn = sys->GetAxis(rowIn), cfgAssy = sys->GetAxis(rowAssy);
        double sbIn = cfgIn->StandbyPos, sbAssy = cfgAssy->StandbyPos;
        cfgIn->StandbyPos = Axis(rowIn)->CurrentPos() + 50;
        cfgAssy->StandbyPos = Axis(rowAssy)->CurrentPos() + 50;
        io->SimSet(IoMap::TrayIn1Present, false);
        io->SimSet(IoMap::TrayAssyPresent, false);
        int okBefore = run->CountOk;
        McControl::RequestStart();
        CHECK_MSG(WaitUntil([] { return State() == EMcState::Running; }, 3000), Msg());
        CHECK_MSG(State() == EMcState::Running && !Has(Msg(), "no input tray"), Msg());
        CHECK(WaitUntil([&] { return run->CountOk > okBefore; }, 120000));
        cfgIn->StandbyPos = sbIn;
        cfgAssy->StandbyPos = sbAssy;
        io->SimSet(IoMap::TrayIn1Present, true);
        io->SimSet(IoMap::TrayAssyPresent, true);
        PauseToStop();
        CHECK(!io->GetDo(IoMap::DoLampCam1) && !io->GetDo(IoMap::DoLampCam2));

        // Hút hụt lần đầu rồi hút được ở lần thử lại: 5001 báo trong lúc hút lại, xong bước hút tự rời danh sách.
        std::atomic<int> vacOns{ 0 };
        std::atomic<bool> shownWhileRetrying{ false };
        io->SetPickSuccessRate(0);
        io->AfterSetDo = [&](int bit, bool on)
        {
            if (!on || (bit != IoMap::DoVacuum1 && bit != IoMap::DoVacuum2)) return;
            int n = ++vacOns;
            if (n == 1) io->SetPickSuccessRate(1.0);
            else if (n == 2) shownWhileRetrying = AlarmManager::IsActive(AlarmCode::PickFailed);
        };
        McControl::RequestStart();
        WaitUntil([] { return State() == EMcState::Running; }, 3000);
        bool retried = WaitUntil([&] { return vacOns >= 2; }, 60000);
        PauseToStop();
        io->AfterSetDo = nullptr;
        io->SetPickSuccessRate(1.0);
        CHECK_MSG(retried && shownWhileRetrying, std::format("{} vacuum opens — {}", vacOns.load(), Msg()));
        CHECK_MSG(!AlarmManager::IsActive(AlarmCode::PickFailed) && !AlarmManager::PendingReset(), PendingText());

        // Warning làm máy dừng vẫn phải RESET rồi mới chạy tiếp.
        AlarmManager::RaiseEvent(AlarmCode::PlaceFailed, "SelfTest");
        McControl::RequestArm();
        CHECK_MSG(WaitUntil([] { return Has(Msg(), "press RESET"); }, 2000) && State() == EMcState::Paused, Msg());
        St().SetMessage({});
        io->SimPress(IoMap::BtnGoB1);
        CHECK_MSG(WaitUntil([] { return Has(Msg(), "press RESET"); }, 2000) && State() == EMcState::Paused, VmState());
        McControl::RequestReset();
        CHECK(WaitUntil([] { return !AlarmManager::IsActive(AlarmCode::PlaceFailed); }, 2000));
        CHECK_MSG(!AlarmManager::PendingReset(), PendingText());

        // Tạm dừng rồi chuyển MANUAL: bỏ chu trình — về ĐÃ DỪNG, UnsafePause, bit RUN bật lại.
        McControl::RequestStart();
        WaitUntil([] { return State() == EMcState::Running; }, 3000);
        Sleep(50);
        McControl::RequestStop();
        WaitUntil([] { return State() == EMcState::Held; }, 1000);
        McControl::RequestSetMode(EMcMode::Manual);
        CHECK_MSG(WaitUntil([] { return State() == EMcState::Paused; }, 3000) && run->UnsafePause && Func()->RunBit(), VmState());
        // TODO(port): bản C# cũng vậy — AbandonHeld đặt "Paused cycle aborted" rồi RequestAbort làm luồng chu trình thoát
        // và Routine_Auto::Stopped ghi đè "Cycle stopped at <bước>: cancelled" nếu nó tới sau. Giữ hành vi; test nhận cả hai.
        CHECK_MSG(Has(Msg(), "Paused cycle aborted") || Has(Msg(), "Cycle stopped at"), Msg());
        CHECK(WaitUntil([] { return !McControl::RoutineBusy(); }, 5000));
        Sleep(300);

        // Bỏ giữa hành trình thì phải Init trước: chọn AUTO bị từ chối, START cũng không chạy.
        ClearNozzles();
        std::mutex m;
        std::string refused;
        int token = McControl::AutoModeRefused().Subscribe([&](std::string s) { std::lock_guard<std::mutex> l(m); refused = s; });
        St().SetMessage({});
        McControl::RequestSetMode(EMcMode::Auto);
        bool gotRefused = WaitUntil([&] { std::lock_guard<std::mutex> l(m); return !refused.empty(); }, 2000);
        McControl::AutoModeRefused().Unsubscribe(token);
        Sleep(100);
        {
            std::lock_guard<std::mutex> l(m);
            CHECK_MSG(gotRefused && Has(refused, "Init") && St().Mode() == EMcMode::Manual && State() != EMcState::Armed, refused);
        }
        McControl::RequestStart();
        Sleep(200);
        CHECK_MSG(State() != EMcState::Running, Msg());
        bool inited = InitToStandby();
        CHECK_MSG(inited && McControl::Motion().ZSafeOk() && St().OrgOk() && !run->UnsafePause, VmState());
        McControl::RequestSetMode(EMcMode::Auto);
        CHECK_MSG(WaitUntil([] { return St().Mode() == EMcMode::Auto && State() == EMcState::Armed; }, 2000), VmState());
    }

    /// <summary>SelfTest.TestAssyPreScan — S05 bớt trigger, quét từng hàng, Init giữa hàng quét lại, chỉ một tay hút.</summary>
    void TestAssyPreScan()
    {
        Fixture f("AssyPreScan");
        if (!f.ok) return;
        HookGuard hooks;
        auto run = Run();
        auto fv = Fv();
        auto io = Io();
        PrepareRun();

        CHECK_MSG(Start(), Msg());
        CHECK_MSG(WaitUntil([&] { return run->CountOk >= 2; }, 120000), std::format("OK={} — {}", run->CountOk, Msg()));
        int locateAt2 = fv->LocateTopCount();
        CHECK(WaitUntil([&] { return run->CountOk >= 3; }, 120000));
        int locateAt3 = fv->LocateTopCount();
        CHECK_MSG(locateAt3 == locateAt2, std::format("LocateTop at OK=2: {}, at OK=3: {}", locateAt2, locateAt3));
        PauseToStop();

        // X chỗ đứng chụp của mỗi lần LocateTop, theo thứ tự — luồng chu trình ghi, luồng test lấy ra.
        auto sys = Sys();
        std::mutex topMx;
        std::vector<double> topX;
        const std::string flowTop = sys->VisionOutputs.FlowLocateTop, gvX = sys->VisionOutputs.GvAxisX;
        fv->SetAfterFlow([&](const std::string& flow)
        {
            if (flow != flowTop) return;
            std::lock_guard<std::mutex> l(topMx);
            topX.push_back(fv->GetGlobal(gvX));
        });
        auto takeTopX = [&] { std::lock_guard<std::mutex> l(topMx); auto v = topX; topX.clear(); return v; };
        auto joinX = [](const std::vector<double>& v) { std::string s; for (double x : v) s += std::format("{:.1f} ", x); return s; };

        // Quét TỪNG HÀNG: đặt vài sản phẩm đầu chỉ tốn cỡ một hàng trigger LocateTop.
        sys->ScanAssyByRow = true;
        run->ResetCounters();
        Prep(ECellState::Loaded, ECellState::Empty);
        takeTopX();
        int locateBeforeRow = fv->LocateTopCount();
        McControl::RequestStart();
        bool placedRow = WaitUntil([&] { return run->CountOk >= 2; }, 120000);
        int locatedRow = fv->LocateTopCount() - locateBeforeRow;
        int cols = run->AssyTray->Cols, cells = run->AssyTray->CellCount();
        CHECK_MSG(placedRow && locatedRow >= cols && locatedRow <= 2 * cols && locatedRow < cells,
                  std::format("LocateTop +{}; row {} cells, tray {} cells, OK={}", locatedRow, cols, cells, run->CountOk));
        // Trong hàng, ô X lớn chụp trước (Tuấn 2026-10-28): hàng đầu (thứ tự đi ô X tăng) chụp từ X lớn xuống X nhỏ.
        auto rowX = takeTopX();
        bool rowDesc = (int)rowX.size() >= cols;
        for (int k = 1; rowDesc && k < cols; k++) rowDesc = rowX[(size_t)k] < rowX[(size_t)k - 1];
        CHECK_MSG(rowDesc, "row capture X: " + joinX(rowX));
        PauseToStop();

        // Init giữa hàng rồi Start lại: bỏ số quét của hàng đang làm, chụp lại từ ô trống đầu tiên.
        size_t scannedBeforeInit = McControl::AutoRoutine()->AssyScannedCells().size();
        bool initDone = InitToStandby();
        CHECK_MSG(initDone && scannedBeforeInit > 0 && McControl::AutoRoutine()->AssyScannedCells().empty(),
                  std::format("before Init {} scanned, after {} — {}", scannedBeforeInit, McControl::AutoRoutine()->AssyScannedCells().size(), Msg()));
        auto& assy = *run->AssyTray;
        int firstFree = -1;
        for (int i = 0; i < assy.CellCount(); i++) if (assy.Get(i) == ECellState::Empty) { firstFree = i; break; }
        int freeRow = -1, unusedCol;
        if (firstFree >= 0) assy.RowCol(firstFree, freeRow, unusedCol);
        int rowFree = 0;
        for (int i = 0; i < assy.CellCount(); i++)
        {
            int r, c;
            assy.RowCol(i, r, c);
            if (r == freeRow && assy.Get(i) == ECellState::Empty) rowFree++;
        }
        int okBeforeRescan = run->CountOk;
        int locateBeforeRescan = fv->LocateTopCount();
        McControl::RequestStart();
        bool placedAfterInit = WaitUntil([&] { return run->CountOk > okBeforeRescan; }, 120000);
        int rescanned = fv->LocateTopCount() - locateBeforeRescan;
        auto rescan = McControl::AutoRoutine()->AssyScannedCells();
        CHECK_MSG(placedAfterInit && firstFree >= 0 && !rescan.empty() && rescan[0] == firstFree &&
                  rescanned >= rowFree && rescanned <= rowFree + cols,
                  std::format("first free {} (row {}, {} free), first scanned {}, LocateTop +{}, OK={} — {}",
                              firstFree, freeRow, rowFree, rescan.empty() ? -1 : rescan[0], rescanned, run->CountOk, Msg()));
        PauseToStop();
        sys->ScanAssyByRow = false;

        // Quét TỪNG CẶP (Tuấn 2026-10-28): mỗi vòng chỉ soi đúng hai ô sắp đặt rồi mới hút. Lần hút đầu tiên tới sau đúng
        // 2 lần LocateTop; ở mọi lần hút, số ô đã soi mà chưa đặt không quá 2 (cả tray thì ~cả tray, từng hàng ~một hàng).
        sys->SetScanAssy(SystemData::EScanAssy::Pair);
        run->ResetCounters();
        Prep(ECellState::Loaded, ECellState::Empty);
        takeTopX();
        int locateBeforePair = fv->LocateTopCount();
        std::atomic<int> pairPicks{ 0 }, firstPickLocates{ -1 }, maxAhead{ -1 };
        io->AfterSetDo = [&](int bit, bool on)
        {
            if (!on || (bit != IoMap::DoVacuum1 && bit != IoMap::DoVacuum2)) return;
            int located = fv->LocateTopCount() - locateBeforePair;
            if (pairPicks++ == 0) firstPickLocates = located;
            maxAhead = std::max(maxAhead.load(), located - run->CountOk);   // luồng chu trình: CountOk cùng luồng
        };
        McControl::RequestStart();
        bool placedPairs = WaitUntil([&] { return run->CountOk >= 6; }, 120000);
        PauseToStop();
        io->AfterSetDo = nullptr;
        CHECK_MSG(placedPairs && firstPickLocates == 2 && maxAhead == 2 && pairPicks >= 6,
                  std::format("OK={}, picks {}, LocateTop before 1st pick {}, max scanned-not-placed {} — {}", run->CountOk,
                              pairPicks.load(), firstPickLocates.load(), maxAhead.load(), Msg()));
        // Mỗi cặp chụp ô X lớn trước — kể cả cặp ở hàng có thứ tự đi ô X tăng (tray giả lập 4 cột: cặp không vắt hàng).
        auto pairX = takeTopX();
        bool pairDesc = pairX.size() >= 6;
        for (size_t k = 0; pairDesc && k + 1 < 6; k += 2) pairDesc = pairX[k] > pairX[k + 1];
        CHECK_MSG(pairDesc, "pair capture X: " + joinX(pairX));
        sys->SetScanAssy(SystemData::EScanAssy::Tray);

        // Chỉ một tay hút: chọn chỉ tay #2 — tay #1 không mở van hút lần nào mà máy vẫn đặt ra sản phẩm.
        McControl::SetOnlyNozzle(2);
        CHECK_MSG(sys->OnlyNozzle == 2, Msg());
        run->ResetCounters();
        Prep(ECellState::Loaded, ECellState::Empty);
        std::atomic<int> vac1{ 0 }, vac2{ 0 };
        io->AfterSetDo = [&](int bit, bool on)
        {
            if (!on) return;
            if (bit == IoMap::DoVacuum1) vac1++;
            else if (bit == IoMap::DoVacuum2) vac2++;
        };
        McControl::RequestStart();
        WaitUntil([] { return State() == EMcState::Running; }, 3000);
        McControl::SetOnlyNozzle(0);
        CHECK_MSG(sys->OnlyNozzle == 2 && Has(Msg(), "stop before changing nozzles"), Msg());
        bool placedOne = WaitUntil([&] { return run->CountOk >= 2; }, 120000);
        PauseToStop();
        io->AfterSetDo = nullptr;
        CHECK_MSG(placedOne && vac1 == 0 && vac2 >= 2, std::format("OK={}, vac #1 {}x, #2 {}x — {}", run->CountOk, vac1.load(), vac2.load(), Msg()));
        McControl::SetOnlyNozzle(0);
        sys->OnlyNozzle = 0;
    }

    /// <summary>SelfTest.TestResumeIntegrity — cảm biến chân không là nguồn sự thật, cả hai chiều, không alarm.</summary>
    void TestResumeIntegrity()
    {
        Fixture f("ResumeIntegrity");
        if (!f.ok) return;
        auto run = Run();
        PrepareRun();
        CHECK(Start());
        WaitUntil([&] { return run->CountOk >= 1; }, 60000);
        PauseToStop();

        // Sổ ghi có hàng mà cảm biến báo trống → START ghi sổ theo cảm biến rồi chạy luôn.
        run->InputTray->Set(0, ECellState::Processing);
        run->Nozzle1->HasProduct = true;
        run->Nozzle1->SourceCellIndex = 0;
        Func()->SetVacuum(1, false);
        Sleep(50);
        CHECK(run->Nozzle1->HasProduct && !Func()->HasProduct(1));
        McControl::RequestStart();
        bool startedEmpty = WaitUntil([] { return State() == EMcState::Running; }, 3000);
        CHECK_MSG(startedEmpty && !AlarmManager::IsActive(AlarmCode::ResumeIntegrity) && !AlarmManager::PendingReset(), VmState());
        CHECK_MSG(run->InputTray->Get(0) == ECellState::Empty && run->Nozzle1->SourceCellIndex != 0,
                  std::format("cell 0 {}, nozzle #1 source {}", ToString(run->InputTray->Get(0)), run->Nozzle1->SourceCellIndex));
        PauseToStop();

        // Tay CÒN hàng mà sổ ghi trống — nhận hàng và chạy tiếp, coi như đã qua bước hút.
        Func()->SetVacuum(1, true);
        run->Nozzle1->HasProduct = false;
        run->Nozzle1->SourceCellIndex = -1;
        Sleep(50);
        McControl::RequestStart();
        bool startedHeld = WaitUntil([] { return State() == EMcState::Running; }, 3000);
        CHECK_MSG(startedHeld && !AlarmManager::IsActive(AlarmCode::ResumeIntegrity) && run->Nozzle1->HasProduct, VmState());
        PauseToStop();
        ClearNozzles();
    }

    /// <summary>SelfTest.TestTrayChangeover — nạp tray bằng nút Start, hết tray, đổi tray, đèn lúc chờ, lỗi lúc chờ.</summary>
    void TestTrayChangeover()
    {
        Fixture f("TrayChangeover");
        if (!f.ok) return;
        HookGuard hooks;
        auto run = Run();
        auto io = Io();
        auto fv = Fv();
        auto model = Model();
        std::string rowIn = model->InputTray->AxisRow, rowAssy = model->AssyTray->AxisRow;

        std::atomic<int> vacOn{ 0 };
        io->AfterSetDo = [&](int bit, bool on) { if (on && (bit == IoMap::DoVacuum1 || bit == IoMap::DoVacuum2)) vacOn++; };
        const int lampDo[] = { IoMap::DoLampStartBtn, IoMap::DoLampStartBtnB, IoMap::DoLampStartBtnB2 };
        auto lampOnCount = [&](int ms)
        {
            int on = 0;
            base::Stopwatch sw;
            while (sw.ElapsedMilliseconds() < ms)
            {
                for (int b : lampDo) if (io->GetDo(b)) { on++; break; }
                Sleep(10);
            }
            return on;
        };

        PrepareRun();

        // ── Hết tray đầu vào giữa chừng: hoàn thành chu trình — CẢ HAI tray ra vị trí chờ, ô trống còn lại của tray lắp
        //    ráp thành Skipped, chờ Start (Tuấn 2026-10-28) ──
        Prep(ECellState::Ng, ECellState::Empty);
        run->InputTray->CurrentIndex = 0;
        run->InputTray->Set(0, ECellState::Loaded);
        McControl::RequestStart();
        WaitUntil([] { return State() == EMcState::Running; }, 3000);
        CHECK_MSG(WaitUntil(WaitingOp, 120000), VmState());
        CHECK_MSG(!AlarmManager::IsActive(AlarmCode::TrayEmpty) && !AlarmManager::PendingReset(), PendingText());
        CHECK_MSG(AtStandby(rowIn) && AtStandby(rowAssy), PosText(rowIn) + ", " + PosText(rowAssy));
        CHECK_MSG(Has(Msg(), "Cycle complete") && Has(Msg(), "input tray empty") && Has(Msg(), "Start"), Msg());
        CHECK_MSG(CountCells(*run->AssyTray, ECellState::Empty) == 0 &&
                  CountCells(*run->AssyTray, ECellState::Skipped) == run->AssyTray->CellCount() - 1,
                  TrayText(*run->AssyTray));

        // STOP lúc chờ tray: chu trình thoát khỏi vòng chờ, máy về DỪNG. Hai tray đều ra nên chờ ở bước quét S05.
        McControl::RequestStop();
        CHECK_MSG(WaitUntil([] { return State() == EMcState::Paused; }, 5000), VmState());
        CHECK_MSG(run->AutoState == EAutoState::S05_ScanAssy && CountCells(*run->InputTray, ECellState::Loaded) == 0,
                  std::string(ToString(run->AutoState)));

        // Lỗi lúc tray đang chờ đổi: vào LỖI, RESET xong START mềm vào lại được sẵn sàng.
        AlarmManager::RaiseEvent(AlarmCode::RoutineCrashed, "SelfTest");
        bool inAlarm = WaitUntil([] { return State() == EMcState::Alarm; }, 2000);
        ResetAndArm();
        CHECK_MSG(inAlarm && State() == EMcState::Armed, VmState());
        CHECK_MSG(Has(Msg(), "press Start"), Msg());
        int armedLampOn = lampOnCount(1200);
        CHECK_MSG(armedLampOn == 0 && State() == EMcState::Armed, std::format("start lamps on {}x — {}", armedLampOn, VmState()));
        CHECK(WaitUntil([] { return St().TowerLamp() == ETowerLamp::GreenBlink; }, 500));

        // Start lúc sẵn sàng mà chưa thấy tray đầu vào: chu trình chạy, hai tay không hút, chờ tray đầu vào.
        io->SimSet(IoMap::TrayIn1Present, false);
        vacOn = 0;
        io->SimPress(IoMap::BtnArmA1);
        CHECK_MSG(WaitUntil([] { return State() != EMcState::Armed; }, 3000), VmState());
        CHECK_MSG(WaitUntil([] { return WaitingOp() && Has(Msg(), "input tray"); }, 30000) && vacOn == 0 && AtStandby(rowIn),
                  VmState() + " — vac " + std::to_string(vacOn.load()) + ", " + PosText(rowIn));

        // Chờ tray: đèn ba nút Start tắt, không nháy; đèn STOP sáng IM (Tuấn 2026-10-19); tháp vàng nháy.
        // DO đổi ở vòng quét kế tiếp sau khi vào CHỜ THAO TÁC (trước đó đang chạy, STOP tắt) — chờ sáng rồi mới đếm.
        bool stopLit = WaitUntil([&] { return io->GetDo(IoMap::DoLampResetBtn); }, 500);
        CHECK_MSG(stopLit, VmState());
        int lampStopOff = 0;
        base::Stopwatch swLamp;
        while (swLamp.ElapsedMilliseconds() < 600) { if (!io->GetDo(IoMap::DoLampResetBtn)) lampStopOff++; Sleep(10); }
        int waitLampOn = lampOnCount(600);
        CHECK_MSG(WaitingOp() && waitLampOn == 0 && lampStopOff == 0, std::format("start {}x, STOP off {}x — {}", waitLampOn, lampStopOff, VmState()));
        CHECK(St().TowerLamp() == ETowerLamp::YellowBlink);

        // Lỗi NGAY LÚC chu trình đang chờ tray: chu trình bị bỏ, RESET xong START mềm vào lại được sẵn sàng.
        AlarmManager::RaiseEvent(AlarmCode::RoutineCrashed, "SelfTest");
        bool alarmWhileWaiting = WaitUntil([] { return State() == EMcState::Alarm; }, 2000);
        Sleep(300);
        ResetAndArm();
        CHECK_MSG(alarmWhileWaiting && State() == EMcState::Armed, VmState());
        io->SimPress(IoMap::BtnGoB2);
        CHECK_MSG(WaitUntil([] { return WaitingOp() && Has(Msg(), "input tray"); }, 30000), VmState());

        // STOP huỷ chu trình chạy ngầm khi máy đang DỪNG.
        St().SetState(EMcState::Idle);
        McControl::RequestStop();
        Sleep(300);
        McControl::RequestArm();
        CHECK_MSG(WaitUntil([] { return State() == EMcState::Armed; }, 3000), VmState());
        io->SimPress(IoMap::BtnGoB2);
        WaitUntil([] { return WaitingOp() && Has(Msg(), "input tray"); }, 30000);

        // Đang chờ mà cảm biến chưa thấy tray đầu vào: Start báo chưa thấy, vẫn chờ.
        St().SetMessage({});
        io->SimPress(IoMap::BtnArmA2);
        Sleep(300);
        CHECK_MSG(WaitingOp() && Has(Msg(), "No input tray at standby"), VmState());
        io->SimSet(IoMap::TrayIn1Present, true);
        io->SimPress(IoMap::BtnGoB1);
        CHECK_MSG(WaitUntil([&] { return vacOn > 0; }, 30000), VmState());
        CHECK(CountCells(*run->InputTray, ECellState::Loaded) >= run->InputTray->CellCount() - 2);
        CHECK_MSG(!AlarmManager::IsActive(AlarmCode::TrayEmpty) && !AlarmManager::PendingReset(), PendingText());
        PauseToStop();

        // ── TH1: về gốc, chỉ có tray đầu vào ──
        CHECK(Origin());
        CHECK_MSG(AtStandby(rowIn) && AtStandby(rowAssy), PosText(rowIn) + ", " + PosText(rowAssy));
        Prep(ECellState::Loaded, ECellState::Empty);
        McControl::RequestReset();
        WaitUntil([] { return !AlarmManager::PendingReset(); }, 2000);
        EMcState stateBefore = State();
        io->SimSet(IoMap::TrayAssyPresent, false);
        int locate1 = fv->LocateTopCount();
        int ok1 = run->CountOk;
        vacOn = 0;
        io->SimPress(IoMap::BtnGoB1);
        bool th1Wait = WaitUntil([] { return WaitingOp() && Has(Msg(), "assy tray"); }, 30000);
        CHECK_MSG((stateBefore == EMcState::Idle || stateBefore == EMcState::Paused) && th1Wait,
                  std::string(ToString(stateBefore)) + " -> " + VmState());
        CHECK_MSG(th1Wait && !AtStandby(rowIn), VmState() + " — " + PosText(rowIn));
        CHECK_MSG(fv->LocateTopCount() == locate1 && vacOn == 0 && AtStandby(rowAssy),
                  std::format("LocateTop +{}, vac {}", fv->LocateTopCount() - locate1, vacOn.load()) + ", " + PosText(rowAssy));
        io->SimSet(IoMap::TrayAssyPresent, true);
        io->SimPress(IoMap::BtnArmA1);
        CHECK_MSG(WaitUntil([&] { return run->CountOk > ok1; }, 120000), VmState());
        PauseToStop();

        // ── TH2: về gốc, chỉ có tray lắp ráp ──
        CHECK(Origin());
        Prep(ECellState::Loaded, ECellState::Empty);
        ResetAndArm();
        io->SimSet(IoMap::TrayIn1Present, false);
        int locate2 = fv->LocateTopCount();
        int align2 = fv->AlignBottomCount();
        int ok2 = run->CountOk;
        vacOn = 0;
        io->SimPress(IoMap::BtnArmA2);
        bool th2Wait = WaitUntil([] { return WaitingOp() && Has(Msg(), "input tray"); }, 60000);
        int scanned2 = fv->LocateTopCount() - locate2;
        int cols = run->AssyTray->Cols;
        CHECK_MSG(th2Wait && scanned2 == cols, std::format("LocateTop +{}, row {} — {}", scanned2, cols, VmState()));
        CHECK_MSG(vacOn == 0 && fv->AlignBottomCount() == align2 && AtStandby(rowIn), PosText(rowIn));
        io->SimSet(IoMap::TrayIn1Present, true);
        io->SimPress(IoMap::BtnGoB2);
        CHECK_MSG(WaitUntil([&] { return run->CountOk > ok2; }, 120000), VmState());
        PauseToStop();

        // ── Tay còn hàng, chưa có tray đầu vào: soi và đặt nốt chỉ với tray lắp ráp ──
        CHECK(Origin());
        Prep(ECellState::Loaded, ECellState::Empty);
        run->InputTray->Set(0, ECellState::Processing);
        Func()->SetVacuum(1, true);
        run->Nozzle1->HasProduct = true;
        run->Nozzle1->SourceCellIndex = 0;
        ResetAndArm();
        io->SimSet(IoMap::TrayIn1Present, false);
        int ok3 = run->CountOk;
        vacOn = 0;
        io->SimPress(IoMap::BtnGoB2);
        CHECK_MSG(WaitUntil([&] { return run->CountOk > ok3; }, 60000) && vacOn == 0 && AtStandby(rowIn), VmState());
        CHECK_MSG(WaitUntil([] { return WaitingOp() && Has(Msg(), "input tray"); }, 30000), VmState());
        PauseToStop();

        // Soi hỏng lúc chưa có tray đầu vào: không chạy Y1 tới tray ở vị trí chờ — báo 5031, lấy hàng ra.
        Func()->SetVacuum(1, true);
        run->Nozzle1->HasProduct = true;
        run->Nozzle1->SourceCellIndex = 1;
        run->InputTray->Set(1, ECellState::Processing);
        run->AutoState = EAutoState::S00_Ready;
        Sleep(50);
        fv->SetBottomFoundRate(0.0);
        ResetAndArm();
        io->SimPress(IoMap::BtnGoB2);
        bool raised5031 = WaitUntil([] { return AlarmManager::IsActive(AlarmCode::CannotReturnProduct); }, 60000);
        fv->SetBottomFoundRate(1.0);
        CHECK_MSG(raised5031 && Has(Msg(), "remove product"), VmState() + " — " + VmAlarm(AlarmCode::CannotReturnProduct));
        CHECK_MSG(AtStandby(rowIn) && run->Nozzle1->HasProduct && Func()->HasProduct(1), PosText(rowIn));
        CHECK(run->NozzleClearRequired);
        Func()->SetVacuum(1, false);
        McControl::RequestReset();
        CHECK_MSG(WaitUntil([] { return !AlarmManager::IsActive(AlarmCode::CannotReturnProduct) && !AlarmManager::IsActive(AlarmCode::VisionNotFound); }, 3000),
                  VmState());
        run->Nozzle1->HasProduct = false;
        run->Nozzle1->SourceCellIndex = -1;
        run->NozzleClearRequired = false;
        run->NozzleClearReason.clear();
        io->SimSet(IoMap::TrayIn1Present, true);

        // ── Tray lắp ráp đầy: hoàn thành chu trình, cả hai tray ra vị trí chờ, hàng còn lại thành Skipped; Start nạp lại
        //    hai tray như tray mới ──
        Prep(ECellState::Loaded, ECellState::Ok);
        McControl::RequestStart();
        CHECK_MSG(WaitUntil(WaitingOp, 60000), VmState());
        CHECK_MSG(AtStandby(rowAssy) && AtStandby(rowIn), PosText(rowAssy) + ", " + PosText(rowIn));
        CHECK_MSG(Has(Msg(), "Cycle complete") && Has(Msg(), "assy tray full") && !Has(Msg(), "input tray empty"), Msg());
        CHECK_MSG(CountCells(*run->InputTray, ECellState::Skipped) == run->InputTray->CellCount(), TrayText(*run->InputTray));
        McControl::RequestReset();
        WaitUntil([] { return !AlarmManager::PendingReset(); }, 2000);
        io->SimSet(IoMap::TrayAssyPresent, false);
        St().SetMessage({});
        // Start khi mới thay tray đầu vào: nạp tray đầu vào (tray mới, đủ hàng), chạy vào máy, chờ tray lắp ráp.
        io->SimPress(IoMap::BtnGoB1);
        CHECK_MSG(WaitUntil([] { return WaitingOp() && Has(Msg(), "Assy tray full"); }, 30000) && !Has(Msg(), "Cycle complete"),
                  VmState());
        CHECK_MSG(CountCells(*run->InputTray, ECellState::Loaded) == run->InputTray->CellCount() && !AtStandby(rowIn),
                  TrayText(*run->InputTray) + " — " + PosText(rowIn));
        io->SimSet(IoMap::TrayAssyPresent, true);
        io->SimPress(IoMap::BtnArmA1);
        CHECK_MSG(WaitUntil([] { return State() == EMcState::Running; }, 5000), VmState());
        CHECK(CountCells(*run->AssyTray, ECellState::Ok) <= 2);
        CHECK_MSG(CountCells(*run->InputTray, ECellState::Skipped) == 0, TrayText(*run->InputTray));   // tray đầu vào mới
        PauseToStop();
        io->SimSet(IoMap::TrayIn1Present, true);
        io->SimSet(IoMap::TrayAssyPresent, true);
        McControl::RequestSetMode(EMcMode::Manual);
    }

    /// <summary>SelfTest.TestTwoInputTrays (bỏ phần lưới trang Main).</summary>
    void TestTwoInputTrays()
    {
        Fixture f("TwoInputTrays");
        if (!f.ok) return;
        HookGuard hooks;
        auto run = Run();
        auto io = Io();
        auto model = Model();
        auto t1 = model->InputTray;
        auto t2 = model->InputTray2;
        std::string rowIn = t1->AxisRow;
        auto countIn = [&](int from, int to)
        {
            int k = 0;
            for (int i = from; i < to && i < run->InputTray->CellCount(); i++) if (run->InputTray->Get(i) == ECellState::Loaded) k++;
            return k;
        };

        CHECK_MSG(model->InputTraysInUse() == 1 && run->InputTray->Trays == 1 && run->InputTray->Rows == t1->Rows(),
                  std::format("{} trays, run {}x{}", model->InputTraysInUse(), run->InputTray->Rows, run->InputTray->Cols));
        CHECK(t2->Rows() == t1->Rows() && t2->Cols() == t1->Cols() && t2->ScanOrder == t1->ScanOrder && t2->AxisRow == t1->AxisRow &&
              t2->Z_PickZ1 == t1->Z_PickZ1 && t2->Z_PickZ2 == t1->Z_PickZ2 && t2->Z_SlowZ1 == t1->Z_SlowZ1);

        const double shiftY = 60;
        auto shifted = [&](const TeachPoint& p) { return TeachPoint(p.X, p.Y + shiftY); };
        double tray2FromY = t1->GeoZ1.PR0.Y + shiftY / 2;

        std::mutex mx;
        std::vector<double> pickY;
        std::vector<std::string> zBad;
        std::atomic<bool> tray2PickBeforeTray1Done{ false };
        std::atomic<int> n{ 0 };
        io->AfterSetDo = [&](int bit, bool on)
        {
            if (!on || (bit != IoMap::DoVacuum1 && bit != IoMap::DoVacuum2)) return;
            std::string zAxis = bit == IoMap::DoVacuum1 ? AxisId::Z1 : AxisId::Z2;
            double y = Axis(rowIn)->CurrentPos();
            double z = Axis(zAxis)->CurrentPos();
            bool onTray2 = y > tray2FromY;
            auto layout = onTray2 ? model->InputTray2 : t1;
            {
                std::lock_guard<std::mutex> l(mx);
                pickY.push_back(y);
                if (std::fabs(z - layout->ZPickFor(zAxis)) > 0.05)
                    zBad.push_back(std::format("{} {:.2f} on tray {} (Z pick {})", zAxis, z, onTray2 ? 2 : 1, layout->ZPickFor(zAxis)));
            }
            if (onTray2 && countIn(0, n) > 0) tray2PickBeforeTray1Done = true;
        };

        auto waitForInput = [&]
        {
            Origin();
            Prep(ECellState::Ng, ECellState::Empty);
            ResetAndArm();
            io->SimSet(IoMap::TrayIn1Present, false);
            io->SimSet(IoMap::TrayIn2Present, false);
            io->SimPress(IoMap::BtnGoB2);
            WaitUntil([] { return WaitingOp() && Has(Msg(), "input tray"); }, 60000);
            io->SimSet(IoMap::TrayIn1Present, true);
        };

        PrepareRun();

        // ── Tray 2 chưa teach mà cảm biến Tray 2 thấy tray: không nhận Start ──
        waitForInput();
        io->SimSet(IoMap::TrayIn2Present, true);
        io->SimPress(IoMap::BtnGoB1);
        Sleep(300);
        CHECK_MSG(WaitingOp() && Has(Msg(), "Input tray 2 not taught"), VmState());

        // ── Teach Tray 2: lưới Tray 1 dời Y1, cao độ hút riêng ──
        t2->GeoZ1.P00 = shifted(t1->GeoZ1.P00); t2->GeoZ1.P0C = shifted(t1->GeoZ1.P0C); t2->GeoZ1.PR0 = shifted(t1->GeoZ1.PR0);
        t2->GeoZ2.P00 = shifted(t1->GeoZ2.P00); t2->GeoZ2.P0C = shifted(t1->GeoZ2.P0C); t2->GeoZ2.PR0 = shifted(t1->GeoZ2.PR0);
        t2->Z_PickZ1 = t1->Z_PickZ1 + 0.5;
        t2->Z_PickZ2 = t1->Z_PickZ2 + 0.5;
        McDataManager::SyncRunTrays();
        n = run->InputTray->CellsPerTray();
        CHECK_MSG(model->InputTraysInUse() == 2 && run->InputTray->Trays == 2 && run->InputTray->Rows == 2 * t1->Rows() && n == t1->CellCount(),
                  std::format("run {}x{}, {} trays, {} cells/tray", run->InputTray->Rows, run->InputTray->Cols, run->InputTray->Trays, n.load()));
        int ta, ra, ca, tb, rb, cb;
        auto ga = model->InputCell(*run->InputTray, n - 1, ta, ra, ca);
        auto gb = model->InputCell(*run->InputTray, n, tb, rb, cb);
        CHECK(ta == 0 && ga == t1 && ra == t1->Rows() - 1 && tb == 1 && gb == t2 && rb == 0 &&
              std::fabs(gb->CellPos(AxisId::Z1, rb, cb).Y - t1->CellPos(AxisId::Z1, 0, cb).Y - shiftY) < 1e-9);

        // ── Chỉ Tray 2 có: Start nạp mỗi Tray 2, hút theo lưới Tray 2 ──
        io->SimSet(IoMap::TrayIn1Present, false);
        { std::lock_guard<std::mutex> l(mx); pickY.clear(); }
        int okB = run->CountOk;
        io->SimPress(IoMap::BtnArmA1);
        bool tray1Empty = true;
        bool gotB = WaitUntil([&] { return run->CountOk > okB; }, 60000);
        for (int i = 0; i < n; i++) if (run->InputTray->Get(i) != ECellState::Empty) tray1Empty = false;
        CHECK_MSG(gotB && countIn(0, n) == 0 && tray1Empty, VmState());
        bool doneB = WaitUntil([] { return WaitingOp() && Has(Msg(), "input tray"); }, 120000);
        double minY2 = std::numeric_limits<double>::max(), maxY2 = std::numeric_limits<double>::lowest();
        for (int r = 0; r < t2->Rows(); r++)
            for (int c = 0; c < t2->Cols(); c++)
                for (const char* z : { AxisId::Z1, AxisId::Z2 })
                {
                    double y = t2->CellPos(z, r, c).Y;
                    minY2 = std::min(minY2, y);
                    maxY2 = std::max(maxY2, y);
                }
        std::vector<double> ysB;
        { std::lock_guard<std::mutex> l(mx); ysB = pickY; }
        bool allOnTray2 = std::all_of(ysB.begin(), ysB.end(), [&](double y) { return y >= minY2 - 0.01 && y <= maxY2 + 0.01; });
        bool tray2Done = true;
        for (int i = n; i < run->InputTray->CellCount(); i++)
            if (run->InputTray->Get(i) != ECellState::Ok && run->InputTray->Get(i) != ECellState::Ng) tray2Done = false;
        CHECK_MSG(doneB && (int)ysB.size() >= n && allOnTray2 && tray2Done, std::format("{} picks — {}", ysB.size(), VmState()));
        PauseToStop();

        // ── Cả hai tray: Tray 1 trước, hết Tray 1 sang Tray 2 mà không ra vị trí chờ ──
        waitForInput();
        io->SimSet(IoMap::TrayIn2Present, true);
        { std::lock_guard<std::mutex> l(mx); pickY.clear(); zBad.clear(); }
        tray2PickBeforeTray1Done = false;
        io->SimPress(IoMap::BtnGoB1);
        CHECK_MSG(WaitUntil([&] { return countIn(n, 2 * n) == n && !WaitingOp(); }, 5000),
                  std::format("tray 1 {}, tray 2 {} loaded", countIn(0, n), countIn(n, 2 * n)));
        bool sawWait = false;
        bool reachedTray2 = WaitUntil([&]
        {
            if (WaitingOp()) sawWait = true;
            std::lock_guard<std::mutex> l(mx);
            return std::any_of(pickY.begin(), pickY.end(), [&](double y) { return y >= minY2 - 0.01; });
        }, 180000);
        CHECK_MSG(reachedTray2 && !tray2PickBeforeTray1Done && !sawWait && countIn(0, n) == 0,
                  std::format("reached {}, early {}, waited {} — {}", reachedTray2, tray2PickBeforeTray1Done.load(), sawWait, VmState()));
        PauseToStop();
        {
            std::lock_guard<std::mutex> l(mx);
            CHECK_MSG(zBad.empty(), zBad.empty() ? std::string() : zBad[0]);
        }

        io->AfterSetDo = nullptr;
        io->SimSet(IoMap::TrayIn1Present, true);
        io->SimSet(IoMap::TrayIn2Present, false);
        model->InputTray2 = TrayLayout::CreateInputDefault("InputTray2");
        McDataManager::SyncRunTrays();
        McControl::RequestSetMode(EMcMode::Manual);
    }

    /// <summary>SelfTest.TestCellSelection (bỏ phần lưới trang Main).</summary>
    void TestCellSelection()
    {
        Fixture f("CellSelection");
        if (!f.ok) return;
        auto run = Run();
        auto io = Io();
        auto fv = Fv();

        PrepareRun();
        CHECK(Origin());
        Prep(ECellState::Loaded, ECellState::Empty);
        McControl::RequestReset();
        WaitUntil([] { return !AlarmManager::PendingReset(); }, 2000);

        const std::vector<int> pickSel = { 3, 6 }, placeSel = { 2, 9 };
        for (int i : pickSel) McControl::ToggleCellSelection(true, i);
        for (int i : placeSel) McControl::ToggleCellSelection(false, i);
        int locate0 = fv->LocateTopCount();
        int ok0 = run->CountOk;
        io->SimPress(IoMap::BtnGoB1);
        bool done = WaitUntil([&] { return WaitingOp() && !run->InputTray->HasSelection() && !run->AssyTray->HasSelection(); }, 120000);
        CHECK_MSG(done && Has(Msg(), "Input tray empty") && Has(Msg(), "Assy tray full"), VmState());
        CHECK_MSG(CellsIn(*run->InputTray, ECellState::Ok) == pickSel &&
                  CountCells(*run->InputTray, ECellState::Skipped) == run->InputTray->CellCount() - (int)pickSel.size(),
                  TrayText(*run->InputTray));
        CHECK_MSG(CellsIn(*run->AssyTray, ECellState::Ok) == placeSel &&
                  CountCells(*run->AssyTray, ECellState::Skipped) == run->AssyTray->CellCount() - (int)placeSel.size() &&
                  run->CountOk - ok0 == (int)placeSel.size(),
                  TrayText(*run->AssyTray) + std::format(", OK +{}", run->CountOk - ok0));
        CHECK_MSG(fv->LocateTopCount() - locate0 == (int)placeSel.size(), std::format("LocateTop +{}", fv->LocateTopCount() - locate0));
        CHECK(AtStandby(Model()->InputTray->AxisRow) && AtStandby(Model()->AssyTray->AxisRow));

        // Tray đã hết, chờ thay: chọn trước được cả ô đã xong; nạp tray mới vẫn giữ ô chọn trước.
        McControl::ToggleCellSelection(true, 0);
        McControl::ToggleCellSelection(false, 5);
        CHECK_MSG(run->InputTray->IsSelected(0) && run->AssyTray->IsSelected(5), Msg());
        io->SimPress(IoMap::BtnArmA1);
        bool done2 = WaitUntil([&] { return WaitingOp() && !run->InputTray->HasSelection() && !run->AssyTray->HasSelection() &&
                                            run->AssyTray->Get(5) == ECellState::Ok; }, 120000);
        CHECK_MSG(done2 && CellsIn(*run->InputTray, ECellState::Ok) == std::vector<int>({ 0 }) &&
                  CellsIn(*run->AssyTray, ECellState::Ok) == std::vector<int>({ 5 }),
                  TrayText(*run->InputTray) + " / " + TrayText(*run->AssyTray) + " — " + VmState());
        PauseToStop();
        McControl::ClearCellSelection(true);
        McControl::ClearCellSelection(false);
        McControl::RequestSetMode(EMcMode::Manual);
    }

    /// <summary>SelfTest.TestAssyTrayFull — còn một ô, đang cầm hàng mà hết ô, không thấy chi tiết đích → NG, không rõ ô
    /// nguồn, nạp lại tray lúc cầm hàng, quét từng hàng; đồng hồ thực tế tray (Done → Last).</summary>
    void TestAssyTrayFull()
    {
        Fixture f("AssyTrayFull");
        if (!f.ok) return;
        auto run = Run();
        auto fv = Fv();
        PrepareRun();

        // ── TH1: tray lắp ráp còn đúng một ô trống, cả hai tay rảnh ──
        run->ResetCounters();
        Prep(ECellState::Loaded, ECellState::Ok);
        run->AssyTray->Set(run->AssyTray->CellCount() - 1, ECellState::Empty);
        McControl::InvalidateAssyScan();
        run->AssyTrayStartedAt.reset();
        int alignBefore1 = fv->AlignBottomCount();
        Sleep(50);
        McControl::RequestStart();
        WaitUntil([] { return State() == EMcState::Running; }, 3000);
        CHECK(WaitUntil([&] { return run->AssyTrayStartedAt.has_value() || run->AssyTrayDone; }, 3000));
        bool waiting = WaitUntil(WaitingOp, 120000);
        CHECK_MSG(waiting, VmState());
        CHECK_MSG(run->AssyTrayDone && run->AssyTrayDoneSec > 0 && run->AssyTrayDoneProducts == run->AssyTray->CellCount() &&
                  !run->AssyTrayStartedAt.has_value() &&
                  std::fabs(run->AssyTrayDonePerProductSec() - run->AssyTrayDoneSec / run->AssyTray->CellCount()) < 1e-9,
                  std::format("{:.2f} s / {} products", run->AssyTrayDoneSec, run->AssyTrayDoneProducts));
        CHECK_MSG(run->LastAssyTrayTimeSec == 0 && run->LastAssyTrayProducts == 0,
                  std::format("Last {:.2f} s / {}", run->LastAssyTrayTimeSec, run->LastAssyTrayProducts));
        double doneSec = run->AssyTrayDoneSec;
        int doneProducts = run->AssyTrayDoneProducts;
        CHECK_MSG(run->CountOk == 1, std::format("OK={} NG={}", run->CountOk, run->CountNg));
        int aligned1 = fv->AlignBottomCount() - alignBefore1;
        CHECK_MSG(aligned1 == 1, std::format("AlignBottom +{}", aligned1));
        CHECK(!run->Nozzle1->HasProduct && !run->Nozzle2->HasProduct && !Func()->HasProduct(1) && !Func()->HasProduct(2));
        // Hoàn thành chu trình: hàng còn lại trên tray đầu vào thành Skipped, cả hai tray ra vị trí chờ (Tuấn 2026-10-28).
        CHECK(CountCells(*run->InputTray, ECellState::Ok) == 1 && CountCells(*run->InputTray, ECellState::Skipped) == run->InputTray->CellCount() - 1);
        CHECK_MSG(AtStandby(Model()->InputTray->AxisRow) && AtStandby(Model()->AssyTray->AxisRow),
                  PosText(Model()->InputTray->AxisRow) + ", " + PosText(Model()->AssyTray->AxisRow));
        CHECK_MSG(WaitUntil([] { return Has(Msg(), "assy tray"); }, 1000), Msg());

        // Start với tray lắp ráp mới: số tray vừa xong chuyển xuống Last, đồng hồ đếm lại.
        McControl::RequestStart();
        WaitUntil([] { return State() == EMcState::Running; }, 5000);
        CHECK_MSG(WaitUntil([&] { return run->AssyTrayStartedAt.has_value(); }, 3000) && !run->AssyTrayDone &&
                  run->LastAssyTrayTimeSec == doneSec && run->LastAssyTrayProducts == doneProducts,
                  std::format("Last {:.2f} s / {}, done {:.2f} s / {}", run->LastAssyTrayTimeSec, run->LastAssyTrayProducts, doneSec, doneProducts));
        McControl::RequestPause();
        WaitUntil([] { return State() == EMcState::Paused; }, 60000);

        // ── TH2: tay #1 đang cầm hàng mà tray lắp ráp không còn ô nào ──
        run->InputTray->ResetAll(ECellState::Loaded);
        run->InputTray->Set(0, ECellState::Processing);
        run->AssyTray->ResetAll(ECellState::Ok);
        McControl::InvalidateAssyScan();
        Func()->SetVacuum(2, false);
        run->Nozzle2->HasProduct = false;
        run->Nozzle2->SourceCellIndex = -1;
        Func()->SetVacuum(1, true);
        run->Nozzle1->HasProduct = true;
        run->Nozzle1->SourceCellIndex = 0;
        run->AutoState = EAutoState::S30_Locate1;
        int ngBefore = run->CountNg;
        Sleep(50);
        McControl::RequestStart();
        WaitUntil([] { return State() == EMcState::Running; }, 3000);
        CHECK_MSG(WaitUntil(WaitingOp, 120000), VmState());
        CHECK(!AlarmManager::IsActive(AlarmCode::TrayEmpty) && !AlarmManager::HasError());
        CHECK(run->Nozzle1->HasProduct && Func()->HasProduct(1));
        CHECK(run->InputTray->Get(0) == ECellState::Processing && run->CountNg == ngBefore);
        int okBefore2 = run->CountOk;
        int locateBefore2 = fv->LocateTopCount();
        int alignBefore2 = fv->AlignBottomCount();
        McControl::RequestStart();
        bool realigned2 = WaitUntil([&] { return fv->AlignBottomCount() > alignBefore2; }, 60000);
        int locatedAtAlign2 = fv->LocateTopCount() - locateBefore2;
        CHECK_MSG(realigned2 && locatedAtAlign2 == run->AssyTray->CellCount(),
                  std::format("LocateTop +{} / {}", locatedAtAlign2, run->AssyTray->CellCount()));
        CHECK(WaitUntil([&] { return run->InputTray->Get(0) == ECellState::Ok; }, 60000) && run->CountOk > okBefore2);
        McControl::RequestPause();
        WaitUntil([] { return State() == EMcState::Paused; }, 60000);

        // ── TH3: nạp lại tray lắp ráp lúc dừng ở S10, ô trống duy nhất không có chi tiết đích → ô NG ──
        int last = run->AssyTray->CellCount() - 1;
        Prep(ECellState::Loaded, ECellState::Ok);
        run->AssyTray->Set(last, ECellState::Empty);
        McControl::InvalidateAssyScan();
        run->AutoState = EAutoState::S10_Pick1;
        int ngBefore3 = run->CountNg;
        int locateBefore3 = fv->LocateTopCount();
        int alignBefore3 = fv->AlignBottomCount();
        Sleep(50);
        fv->SetFoundRate(0.0);
        McControl::RequestStart();
        WaitUntil([] { return State() == EMcState::Running; }, 3000);
        bool waiting3 = WaitUntil(WaitingOp, 120000);
        fv->SetFoundRate(1.0);
        int located3 = fv->LocateTopCount() - locateBefore3;
        int aligned3 = fv->AlignBottomCount() - alignBefore3;
        CHECK_MSG(located3 == 1, std::format("LocateTop +{}", located3));
        CHECK_MSG(run->AssyTray->Get(last) == ECellState::Ng, ToString(run->AssyTray->Get(last)));
        CHECK_MSG(waiting3 && aligned3 == 0 && !run->Nozzle1->HasProduct && !run->Nozzle2->HasProduct && Has(Msg(), "assy tray"),
                  std::format("AlignBottom +{} — ", aligned3) + VmState());
        CHECK(AllCells(*run->InputTray, ECellState::Skipped) && run->CountNg == ngBefore3);   // xong lượt: hàng còn lại Skipped
        McControl::RequestStart();
        WaitUntil([] { return State() == EMcState::Running; }, 5000);
        McControl::RequestPause();
        WaitUntil([] { return State() == EMcState::Paused; }, 60000);

        // ── TH4: tay #1 cầm hàng không rõ ô nguồn và soi hỏng → 5031 sự kiện, bắt lấy hàng ra ──
        Prep(ECellState::Loaded, ECellState::Empty);
        Func()->SetVacuum(1, true);
        run->Nozzle1->HasProduct = true;
        run->Nozzle1->SourceCellIndex = -1;
        run->AutoState = EAutoState::S21_Align2;
        Sleep(50);
        fv->SetBottomFoundRate(0.0);
        McControl::RequestStart();
        WaitUntil([] { return State() == EMcState::Running; }, 3000);
        bool raised4 = WaitUntil([] { return AlarmManager::IsActive(AlarmCode::CannotReturnProduct); }, 60000);
        fv->SetBottomFoundRate(1.0);
        CHECK_MSG(raised4 && Has(Msg(), "remove product"), Msg());
        auto a5031 = AlarmManager::Get(AlarmCode::CannotReturnProduct);
        CHECK(a5031 && !a5031->ConditionActive);
        CHECK(run->Nozzle1->HasProduct && Func()->HasProduct(1));
        CHECK(AllCells(*run->InputTray, ECellState::Loaded));
        CHECK(run->NozzleClearRequired);
        auto a4004 = AlarmManager::Get(AlarmCode::VisionNotFound);
        CHECK(!a4004 || !a4004->ConditionActive);
        WaitUntil([] { return State() == EMcState::Alarm; }, 3000);
        McControl::RequestReset();
        CHECK_MSG(WaitUntil([] { return !AlarmManager::IsActive(AlarmCode::CannotReturnProduct) && !AlarmManager::IsActive(AlarmCode::VisionNotFound); }, 3000), Msg());
        CHECK(WaitUntil([] { return !AlarmManager::HasError(); }, 3000));

        // Tay vẫn còn hàng: bấm chạy là gỡ yêu cầu, nhận hàng và chạy tiếp.
        Sleep(300);
        McControl::RequestStart();
        bool started4 = WaitUntil([] { return State() == EMcState::Running; }, 1500);
        CHECK_MSG(started4 && !run->NozzleClearRequired && run->Nozzle1->HasProduct, VmState());
        McControl::RequestPause();
        WaitUntil([] { return State() == EMcState::Paused; }, 60000);

        // Người vận hành đã lấy hàng ra sau sự cố: gỡ yêu cầu, ghi tay #1 trống theo cảm biến.
        Func()->SetVacuum(1, false);
        run->Nozzle1->HasProduct = true;
        run->Nozzle1->SourceCellIndex = -1;
        run->NozzleClearRequired = true;
        run->NozzleClearReason = "selftest — product removed by hand";
        Sleep(300);
        McControl::RequestStart();
        CHECK_MSG(WaitUntil([] { return State() == EMcState::Running; }, 5000), Msg());
        CHECK(!run->NozzleClearRequired && !run->Nozzle1->HasProduct);
        McControl::RequestPause();
        WaitUntil([] { return State() == EMcState::Paused; }, 60000);

        // ── TH5: nạp lại tray lắp ráp lúc tay #1 còn cầm hàng (dừng ở S11): quét tray mới, soi lại, đặt ──
        run->InputTray->ResetAll(ECellState::Loaded);
        run->InputTray->Set(0, ECellState::Processing);
        Func()->SetVacuum(2, false);
        run->Nozzle2->HasProduct = false;
        run->Nozzle2->SourceCellIndex = -1;
        Func()->SetVacuum(1, true);
        run->Nozzle1->HasProduct = true;
        run->Nozzle1->SourceCellIndex = 0;
        run->AutoState = EAutoState::S11_Pick2;
        Sleep(50);
        McControl::ResetAssyTray();
        CHECK_MSG(!run->NozzleClearRequired, run->NozzleClearReason);
        CHECK_MSG(Has(Msg(), "Assy tray reloaded"), Msg());
        int okBefore5 = run->CountOk;
        int locateBefore5 = fv->LocateTopCount();
        int alignBefore5 = fv->AlignBottomCount();
        McControl::RequestStart();
        CHECK_MSG(WaitUntil([] { return State() == EMcState::Running; }, 5000), Msg());
        bool realigned5 = WaitUntil([&] { return fv->AlignBottomCount() > alignBefore5; }, 60000);
        int locatedAtAlign5 = fv->LocateTopCount() - locateBefore5;
        CHECK_MSG(realigned5 && locatedAtAlign5 == run->AssyTray->CellCount(),
                  std::format("LocateTop +{} / {}", locatedAtAlign5, run->AssyTray->CellCount()));
        CHECK(WaitUntil([&] { return run->InputTray->Get(0) == ECellState::Ok; }, 60000) && run->CountOk > okBefore5);
        McControl::RequestPause();
        WaitUntil([] { return State() == EMcState::Paused; }, 60000);

        // ── TH cuối: quét TỪNG HÀNG, tray lắp ráp còn đúng một ô — vẫn chỉ tay #1 hút ──
        auto sys = Sys();
        sys->ScanAssyByRow = true;
        run->ResetCounters();
        Prep(ECellState::Loaded, ECellState::Ok);
        run->AssyTray->Set(run->AssyTray->CellCount() - 1, ECellState::Empty);
        McControl::InvalidateAssyScan();
        int alignBeforeRow = fv->AlignBottomCount();
        Sleep(50);
        McControl::RequestStart();
        WaitUntil([] { return State() == EMcState::Running; }, 5000);
        bool waitingRow = WaitUntil(WaitingOp, 120000);
        int alignedRow = fv->AlignBottomCount() - alignBeforeRow;
        CHECK_MSG(waitingRow && run->CountOk == 1 && alignedRow == 1,
                  std::format("{} — OK={} NG={}, AlignBottom +{}", St().StateText(), run->CountOk, run->CountNg, alignedRow));
        CHECK(!run->Nozzle1->HasProduct && !run->Nozzle2->HasProduct);
        sys->ScanAssyByRow = false;
        McControl::RequestPause();
        if (!WaitUntil([] { return State() == EMcState::Paused; }, 15000))
        {
            McControl::RequestStart();
            WaitUntil([] { return State() == EMcState::Running; }, 5000);
            McControl::RequestPause();
            WaitUntil([] { return State() == EMcState::Paused; }, 60000);
        }
        McControl::RequestSetMode(EMcMode::Manual);
    }

    // ═════════════════════════ App chấp hành, VisionMaster tính bù ═════════════════════════

    /// <summary>SelfTest.TestVmAutoCycle (bỏ phần VisionReadout).</summary>
    void TestVmAutoCycle()
    {
        Fixture f("VmAutoCycle");
        if (!f.ok) return;
        HookGuard hooks;
        auto fv = VmSetup();
        if (!fv) { CHECK(fv != nullptr); return; }
        auto run = Run();
        PrepareRun();

        const double allDx = 0.20, allDy = -0.15, trimDx = 0.30, trimDy = -0.25;
        auto assy = Model()->AssyTray;
        assy->SetPlaceTrimAll(AxisId::Z1, allDx, allDy, 0);
        assy->SetPlaceTrimAll(AxisId::Z2, allDx, allDy, 0);
        for (int r = 0; r < assy->Rows(); r++)
            for (int c = 0; c < assy->Cols(); c++)
            {
                assy->SetPlaceTrim(AxisId::Z1, r, c, trimDx - allDx, trimDy - allDy, 0);
                assy->SetPlaceTrim(AxisId::Z2, r, c, trimDx - allDx, trimDy - allDy, 0);
            }

        // Đèn CAM sáng suốt cả nhóm bước, không nháy từng lần kích Flow — đếm SƯỜN LÊN. Đèn CAM1 bật TRƯỚC khi X chạy
        // tới vị trí soi (2026-10-23): ở sườn lên, X còn ở tray đầu vào (X ≤ 210), xa cả hai vị trí soi (270, 300).
        auto io = Io();
        auto xAxis = Axis(AxisId::X);
        const double align1 = Model()->AlignPosX_Nozzle1, align2 = Model()->AlignPosX_Nozzle2;
        std::mutex mx;
        int lampOn1 = 0, lampOn2 = 0, lamp1AtAlign = 0;
        bool cam1 = false, cam2 = false, bothLampsOn = false;
        io->AfterSetDo = [&](int bit, bool on)
        {
            std::lock_guard<std::mutex> l(mx);
            if (bit == IoMap::DoLampCam1 && on && !cam1)
            {
                double x = xAxis->CurrentPos();
                if (std::abs(x - align1) < 1.0 || std::abs(x - align2) < 1.0) lamp1AtAlign++;
            }
            if (bit == IoMap::DoLampCam1) { if (on && !cam1) lampOn1++; cam1 = on; }
            if (bit == IoMap::DoLampCam2) { if (on && !cam2) lampOn2++; cam2 = on; }
            if (cam1 && cam2) bothLampsOn = true;
        };
        int vm0 = fv->VmCallCount();
        int comp0 = fv->PlaceComputeCount();
        std::string rowAxis = assy->AxisRow;
        auto yAssy = std::dynamic_pointer_cast<FakeAxis>(Axis(rowAxis));
        int gentle0 = yAssy->GentleMoves();

        CHECK_MSG(Start(), VmState());
        bool made = WaitUntil([&] { return run->CountOk >= 4; }, 180000);
        CHECK_MSG(made, std::format("OK={} NG={} step={} — {}", run->CountOk, run->CountNg, ToString(run->AutoState), Msg()));
        CHECK_MSG(!AlarmManager::HasError(), ActiveCodes());
        CHECK_MSG(fv->PlaceComputeCount() - comp0 >= 4, std::format("compute +{}", fv->PlaceComputeCount() - comp0));
        CHECK(run->CountNg == 0 && NoCell(*run->AssyTray, ECellState::Skipped));
        io->AfterSetDo = nullptr;
        int vmCalls = fv->VmCallCount() - vm0;
        int nhip = (run->CountOk + 1) / 2;
        {
            std::lock_guard<std::mutex> l(mx);
            CHECK_MSG(lampOn1 > 0 && lampOn2 > 0 && lampOn1 + lampOn2 < vmCalls, std::format("CAM1 {}x, CAM2 {}x / {} flows", lampOn1, lampOn2, vmCalls));
            CHECK_MSG(lampOn1 <= nhip, std::format("CAM1 {}x for {} strokes", lampOn1, nhip));
            CHECK_MSG(lamp1AtAlign == 0, std::format("CAM1 lit only after X reached align pos {}/{}x", lamp1AtAlign, lampOn1));
            CHECK(!bothLampsOn);
        }

        auto mv = McControl::AutoRoutine()->LastPlaceMove();
        CHECK_MSG(mv && Near(mv->ActualX, mv->CmdX) && Near(mv->ActualY, mv->CmdY) && AxisAngle::Distance(mv->ActualA, mv->ATarget) < 0.05,
                  mv ? std::format("X {:.3f}/{:.3f} Y {:.3f}/{:.3f} A {:.3f}/{:.3f}", mv->ActualX, mv->CmdX, mv->ActualY, mv->CmdY, mv->ActualA, mv->ATarget)
                     : std::string("no place yet"));
        double yLow = Sys()->GetAxis(rowAxis)->MoveSpeedLow();
        CHECK_MSG(yAssy->GentleMoves() - gentle0 >= 4 && Near(yAssy->LastGentleSpeed(), yLow),
                  std::format("{} gentle moves, speed {:.3f}/{:.3f}", yAssy->GentleMoves() - gentle0, yAssy->LastGentleSpeed(), yLow));
        auto sent = fv->LastPlaceGlobals();
        auto calib = Model()->Calib;
        CHECK(sent && sent->count(VmVars::HasTarget) && sent->at(VmVars::HasTarget) == 1 && sent->count(VmVars::CellX) &&
              (Near(sent->at(VmVars::HandEyeDx), calib.HandEyeDx(1)) || Near(sent->at(VmVars::HandEyeDx), calib.HandEyeDx(2))));
        std::vector<std::optional<PlacedTopRecord>> placed;
        for (int i = 0; i < run->AssyTray->CellCount(); i++)
            if (run->AssyTray->Get(i) == ECellState::Ok) placed.push_back(McControl::AutoRoutine()->GetPlacedTop(i));
        CHECK_MSG(placed.size() >= 4 && std::all_of(placed.begin(), placed.end(), [](const auto& p) { return p.has_value(); }),
                  std::to_string(placed.size()));
        bool trimOk = sent && std::any_of(placed.begin(), placed.end(), [&](const auto& p)
        {
            return p && Near(sent->at(VmVars::CellX) - trimDx, p->X) && Near(VmCycle::BottomYSign * sent->at(VmVars::CellY) - trimDy, p->Y);
        });
        CHECK(trimOk);
        StopRoutine();
    }

    /// <summary>SelfTest.TestVmFaults — nhánh N (góc bù vượt ngưỡng → NG) và G (lệch lưới → ô NG, 5005 Warning).</summary>
    void TestVmFaults()
    {
        Fixture f("VmFaults");
        if (!f.ok) return;
        auto fv = VmSetup();
        if (!fv) { CHECK(fv != nullptr); return; }
        auto run = Run();

        PrepareRun();
        fv->SetForceDAngleDeg(20);
        fv->SetForceDAngleTimes(1);
        CHECK_MSG(Start(), VmState());
        bool gotNg = WaitUntil([&] { return run->CountNg >= 1; }, 120000);
        CHECK_MSG(gotNg && CountCells(*run->InputTray, ECellState::Ng) == 1, std::format("NG={}", run->CountNg));
        CHECK_MSG(WaitUntil([&] { return run->CountOk >= 1; }, 120000) && !AlarmManager::HasError(), VmState());
        StopRoutine();

        PrepareRun();
        std::mutex mx;
        std::optional<AlarmData> guardAlarm;
        int tok = AlarmManager::AlarmRaised().Subscribe([&](AlarmData a)
        {
            if (a.Code == AlarmCode::PlaceTargetGuard) { std::lock_guard<std::mutex> l(mx); guardAlarm = a; }
        });
        fv->SetTargetShiftMm(3.0);
        fv->SetTargetShiftTimes(1);
        CHECK_MSG(Start(), VmState());
        bool placedSkip = WaitUntil([&] { return run->CountOk >= 1; }, 120000);
        int skipped = CountCells(*run->AssyTray, ECellState::Ng);
        CHECK_MSG(placedSkip && skipped == 1 && run->CountNg == 0, std::format("ng cells {}, OK={}, NG={}", skipped, run->CountOk, run->CountNg));
        AlarmManager::AlarmRaised().Unsubscribe(tok);
        {
            std::lock_guard<std::mutex> l(mx);
            CHECK_MSG(guardAlarm && guardAlarm->Level == EAlarmLevel::Warning && !AlarmManager::HasError(),
                      guardAlarm ? guardAlarm->Detail : std::string("no 5005"));
        }
        StopRoutine();
    }

    /// <summary>SelfTest.TestVmHalts — dừng vì lỗi hợp đồng: giữ hàng, RESET rồi chạy lại từ S00.</summary>
    void TestVmHalts()
    {
        Fixture f("VmHalts");
        if (!f.ok) return;
        auto fv = VmSetup();
        if (!fv) { CHECK(fv != nullptr); return; }
        auto run = Run();
        auto names = Sys()->VisionOutputs;

        // Lệch lưới hai ô liên tiếp → 4010.
        PrepareRun();
        fv->SetTargetShiftMm(3.0);
        fv->SetTargetShiftTimes(2);
        CHECK_MSG(Start(), VmState());
        bool halted = WaitUntil([] { return AlarmManager::IsActive(AlarmCode::VisionContract); }, 120000);
        WaitHalted();
        CHECK_MSG(halted, VmAlarm(AlarmCode::VisionContract));
        CHECK(Holding() && NoCell(*run->AssyTray, ECellState::Processing) && !run->NozzleClearRequired);
        int ok0 = run->CountOk, align0 = fv->AlignBottomCount();
        CHECK_MSG(ResetAndStart(), VmState());
        CHECK(WaitUntil([&] { return run->CountOk > ok0; }, 120000) && fv->AlignBottomCount() > align0);
        StopRoutine();

        // Echo sai.
        PrepareRun();
        fv->SetCorruptEchoOnce(true);
        CHECK_MSG(Start(), VmState());
        bool haltedEcho = WaitUntil([] { return AlarmManager::IsActive(AlarmCode::VisionContract); }, 120000);
        WaitHalted();
        CHECK_MSG(haltedEcho && Has(VmAlarm(AlarmCode::VisionContract), "EchoRunId"), VmAlarm(AlarmCode::VisionContract));
        CHECK(Holding() && NoCell(*run->AssyTray, ECellState::Processing) && run->CountNg == 0);
        int ok1 = run->CountOk;
        CHECK_MSG(ResetAndStart(), VmState());
        CHECK(WaitUntil([&] { return run->CountOk > ok1; }, 120000));
        StopRoutine();

        // Solution thiếu biến CellX_mm.
        PrepareRun();
        auto vars = fv->GlobalVarsSnapshot();
        auto savedCell = vars[VmVars::CellX];
        fv->RemoveGlobalVar(VmVars::CellX);
        CHECK_MSG(Start(), VmState());
        bool haltedIn = WaitUntil([] { return AlarmManager::IsActive(AlarmCode::VisionContract); }, 120000);
        WaitHalted();
        CHECK_MSG(haltedIn && Has(VmAlarm(AlarmCode::VisionContract), VmVars::CellX) && Holding(), VmAlarm(AlarmCode::VisionContract));
        fv->PutGlobalVar(VmVars::CellX, savedCell.Type, savedCell.Value);
        CHECK_MSG(ResetAndStart(), VmState());
        StopRoutine();

        // Solution thiếu biến trục A2.
        PrepareRun();
        std::string a2 = names.GvAxisA2;
        auto savedA2 = fv->GlobalVarsSnapshot()[a2];
        fv->RemoveGlobalVar(a2);
        CHECK_MSG(Start(), VmState());
        bool haltedVar = WaitUntil([] { return AlarmManager::IsActive(AlarmCode::VisionContract); }, 120000);
        WaitHalted();
        CHECK_MSG(haltedVar && Has(VmAlarm(AlarmCode::VisionContract), "\"" + a2 + "\""), VmAlarm(AlarmCode::VisionContract));
        fv->PutGlobalVar(a2, savedA2.Type, savedA2.Value);
        CHECK_MSG(ResetAndStart(), VmState());
        StopRoutine();
    }

    /// <summary>SelfTest.TestVmGate — cổng hiệu chuẩn khi chạy tiếp giữa chừng, trục A xoay sau lúc chụp (nhánh R).</summary>
    void TestVmGate()
    {
        Fixture f("VmGate");
        if (!f.ok) return;
        HookGuard hooks;
        auto fv = VmSetup();
        if (!fv) { CHECK(fv != nullptr); return; }
        auto run = Run();
        auto names = Sys()->VisionOutputs;
        auto model = Model();
        double posX0 = model->AlignPosX_Nozzle1;

        PrepareRun();
        run->InputTray->Set(0, ECellState::Processing);
        Func()->SetVacuum(1, true);
        run->Nozzle1->HasProduct = true;
        run->Nozzle1->SourceCellIndex = 0;
        run->AutoState = EAutoState::S30_Locate1;
        model->AlignPosX_Nozzle1 = posX0 + 0.2;
        double x0 = Axis(AxisId::X)->CurrentPos();
        int calls0 = fv->VmCallCount();
        Sleep(100);
        McControl::RequestStart();
        bool blocked = WaitUntil([] { return AlarmManager::IsActive(AlarmCode::VisionCalibNotReady); }, 15000);
        WaitHalted();
        CHECK_MSG(blocked && Has(Msg(), "nozzle #1 insp pos."), VmState());
        CHECK_MSG(Near(Axis(AxisId::X)->CurrentPos(), x0), std::format("X {:.3f} -> {:.3f}", x0, Axis(AxisId::X)->CurrentPos()));
        CHECK_MSG(fv->VmCallCount() == calls0, std::format("Flow +{}", fv->VmCallCount() - calls0));
        model->AlignPosX_Nozzle1 = posX0;
        CHECK_MSG(ResetAndStart(), VmState());
        CHECK_MSG(WaitUntil([&] { return run->InputTray->Get(0) == ECellState::Ok && run->CountOk >= 1; }, 120000), VmState());
        StopRoutine();

        // Trục A xoay 2° ngay sau lúc chụp bottom-up tay #1 → soi lại đúng một lần rồi đặt, không dừng.
        PrepareRun();
        fv->ClearFlowHistory();
        std::atomic<bool> moved{ false };
        std::string flowTool1 = names.FlowAlignBottomTool1;
        fv->SetAfterFlow([&, flowTool1](const std::string& flow)
        {
            if (moved || flow != flowTool1) return;
            moved = true;
            auto a1 = Axis(AxisId::A1);
            Func()->Move(AxisId::A1, AxisAngle::Wrap(a1->CurrentPos() + 2.0), Sys()->GetAxis(AxisId::A1)->MoveSpeed, std::stop_token());
        });
        CHECK_MSG(Start(), VmState());
        bool placedA = WaitUntil([&] { return run->CountOk >= 2; }, 180000);
        fv->SetAfterFlow(nullptr);
        CHECK_MSG(moved && placedA && !AlarmManager::HasError(), VmState());
        auto hist = fv->FlowHistorySnapshot();
        auto iA = std::find(hist.begin(), hist.end(), names.FlowAlignBottomTool1);
        int between = -1;
        if (iA != hist.end())
        {
            auto iNext = std::find(iA + 1, hist.end(), names.FlowAlignBottomTool2);
            between = (int)std::count(iA, iNext, names.FlowAlignBottomTool1);
        }
        CHECK_MSG(between == 2, std::format("AlignBottom_Tool1 before next Tool2: {}", between));
        model->AlignPosX_Nozzle1 = posX0;
        StopRoutine();
        McControl::RequestSetMode(EMcMode::Manual);
    }

    /// <summary>Chế độ dừng khi Vision NG (Tuấn 2026-10-18, chỉ có ở bản C++): dừng ngay tại chỗ NG, chưa xử lý NG — giữ
    /// hàng, không đếm NG, không đánh dấu ô, không alarm; Start = soi lại đúng bước đó; tắt chế độ thì NG như cũ.</summary>
    void TestVisionNgStop()
    {
        Fixture f("VisionNgStop");
        if (!f.ok) return;
        auto fv = VmSetup();
        if (!fv) { CHECK(fv != nullptr); return; }
        auto run = Run();
        auto paused = [] { return State() == EMcState::Paused; };

        // ── AlignBottom không thấy sản phẩm → dừng ở bước soi, tay còn hàng ──
        PrepareRun();
        McControl::SetStopOnVisionNg(true);
        CHECK(St().StopOnVisionNg() && Has(Msg(), "Stop on Vision NG on"));
        fv->SetBottomFoundRate(0.0);
        int align0 = fv->AlignBottomCount();
        CHECK_MSG(Start(), VmState());
        bool stopped = WaitUntil(paused, 120000);
        EAutoState at = run->AutoState;
        CHECK_MSG(stopped && Has(Msg(), "VISION NG") && fv->AlignBottomCount() == align0 + 1, VmState());
        CHECK_MSG(at == EAutoState::S20_Align1 || at == EAutoState::S21_Align2, std::format("step {}", (int)at));
        CHECK_MSG(Holding() && run->CountNg == 0 && NoCell(*run->InputTray, ECellState::Ng) && !AlarmManager::HasError()
                      && !AlarmManager::IsActive(AlarmCode::VisionNotFound), VmState());

        // Start khi vẫn NG: soi lại đúng bước đó rồi lại dừng, vẫn chưa xử lý NG.
        int align1 = fv->AlignBottomCount();
        CHECK_MSG(Start(), VmState());
        CHECK_MSG(WaitUntil([&] { return paused() && fv->AlignBottomCount() > align1; }, 60000), VmState());
        CHECK_MSG(run->AutoState == at && Holding() && run->CountNg == 0, VmState());

        // Vision hết NG: Start chạy tiếp và đặt bình thường.
        fv->SetBottomFoundRate(1.0);
        int ok0 = run->CountOk;
        CHECK_MSG(Start(), VmState());
        CHECK_MSG(WaitUntil([&] { return run->CountOk > ok0; }, 120000) && run->CountNg == 0 && !AlarmManager::HasError(), VmState());
        StopRoutine();

        // ── LocateTop không thấy chi tiết đích → dừng ở S05, ô lắp ráp chưa thành NG ──
        PrepareRun();
        fv->SetFoundRate(0.0);
        int locate0 = fv->LocateTopCount();
        CHECK_MSG(Start(), VmState());
        bool stoppedTop = WaitUntil(paused, 120000);
        fv->SetFoundRate(1.0);
        CHECK_MSG(stoppedTop && run->AutoState == EAutoState::S05_ScanAssy && fv->LocateTopCount() == locate0 + 1, VmState());
        CHECK_MSG(NoCell(*run->AssyTray, ECellState::Ng) && run->AssyCountNg == 0 && !Holding(), VmState());
        StopRoutine();

        // ── Tắt chế độ: NG xử lý như trước (trả hàng, đếm NG), không dừng ──
        McControl::SetStopOnVisionNg(false);
        CHECK(!St().StopOnVisionNg());
        PrepareRun();
        fv->SetBottomFoundRate(0.0);
        CHECK_MSG(Start(), VmState());
        bool counted = WaitUntil([&] { return run->CountNg >= 1; }, 120000);
        fv->SetBottomFoundRate(1.0);
        CHECK_MSG(counted && CountCells(*run->InputTray, ECellState::Ng) >= 1, VmState());
        StopRoutine();
        McControl::RequestSetMode(EMcMode::Manual);
    }
}

int RunControllerTests()
{
    testkit::Group g("Controller");

    TestStartupShutdown();
    TestOriginSequence();
    TestEventAlarmsResettable();
    TestEmergencyStop();
    TestLightCurtainAndAir();
    TestStopCancelsManualSequences();
    TestLampsAndBuzzer();
    TestModeSwitch();
    TestTrayButtons();
    TestDryRun();

    // Chu trình Auto — cần Routine_Auto (port song song).
    TestAutoCycle();
    TestAssyPreScan();
    TestResumeIntegrity();
    TestTrayChangeover();
    TestTwoInputTrays();
    TestCellSelection();
    TestAssyTrayFull();
    TestVmAutoCycle();
    TestVmFaults();
    TestVmHalts();
    TestVmGate();
    TestVisionNgStop();

    LogWorker::Instance().Flush();
    return g.Result();
}
