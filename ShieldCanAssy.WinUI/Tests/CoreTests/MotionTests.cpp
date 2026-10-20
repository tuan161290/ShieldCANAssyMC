// Kiểm thử tầng thiết bị chuyển động / I/O: AxisScale, AxisAngle, FakeAxis, FakeIo, MotionConnect (mô phỏng), chọn lớp
// trục thật theo AxisConfig::Kind (chỉ dựng đối tượng, KHÔNG kết nối), luật chờ lệnh chạy của PanasonicAxis, và
// PanasonicAxis + servo::ServoService chạy trọn trên ổ A6SF giả (servo::FakeA6sfPort) — bản C++ của các bài
// TestAxisScale / TestServoModbusWrites / TestMoveCompletionRule / TestMoveIncrement / TestRotaryWrap / TestZSafeInterlock
// trong SelfTest.cs.
//
// AN TOÀN: máy chạy test là máy lắp ráp thật. Không bài nào mở cổng COM, nạp EziMOTIONPlusE.dll hay gọi FAS_*:
// FastechAxis/IoConnect chỉ được dựng và hỏi trạng thái khi CHƯA kết nối (mọi lệnh trả về sớm trước khi chạm thư
// viện), PanasonicAxis chỉ nối qua cổng giả. Mọi dữ liệu ghi vào một thư mục tạm mới — không đụng D:\ShieldCanAssy.
#include "TestKit.h"

#include "Base/Async.h"
#include "Base/Text.h"
#include "Communication/AxisAngle.h"
#include "Communication/AxisScale.h"
#include "Communication/FastechAxis.h"
#include "Communication/IoConnect.h"
#include "Communication/MotionConnect.h"
#include "Communication/PanasonicAxis.h"
#include "Communication/Sim/FakeAxis.h"
#include "Communication/Sim/FakeIo.h"
#include "Data/McDataManager.h"
#include "Data/SystemData.h"
#include "Fastech/EziMOTIONPlusELib.h"
#include "Fastech/FastechBoardId.h"
#include "Infor/AlarmCatalog.h"
#include "Infor/AlarmManager.h"
#include "Infor/AxisId.h"
#include "Infor/IoMap.h"
#include "ServoControl/FakeA6sfPort.h"
#include "ServoControl/ServoService.h"
#include "SysConfig/AppPaths.h"
#include "Utility/Log/LogWorker.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

using namespace sca;
namespace fs = std::filesystem;

namespace
{
    /// <summary>Vòng quét giả: gọi fn mỗi 5 ms trên luồng riêng (FakeAxis chỉ chạy khi có người gọi Refresh).</summary>
    struct Ticker
    {
        std::jthread thread;
        explicit Ticker(std::function<void()> fn)
            : thread([fn = std::move(fn)](std::stop_token st) { while (!st.stop_requested()) { fn(); base::Delay(5); } }) {}
    };

    int Code(const char* axis, AxisFault f) { return AlarmCode::ForAxis(axis, f); }

    /// <summary>SelfTest.Near: |a − b| &lt; 0.02.</summary>
    bool Near(double a, double b) { return std::fabs(a - b) < 0.02; }

    /// <summary>SelfTest.WaitUntil: chờ điều kiện tới hết hạn, trả kết quả cuối.</summary>
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

    /// <summary>SystemData::CreateDefault() trả giá trị hay shared_ptr đều dùng được.</summary>
    SystemData& Deref(SystemData& s) { return s; }
    SystemData& Deref(const std::shared_ptr<SystemData>& p) { return *p; }
    std::shared_ptr<SystemData> Shared(const std::shared_ptr<SystemData>& p) { return p; }

    std::shared_ptr<AxisConfig> MakeCfg(const char* name)
    {
        auto c = std::make_shared<AxisConfig>();
        c->Name = name; c->SoftLimitNeg = -1; c->SoftLimitPos = 600; c->JogSpeedSlow = 20; c->StandbyPos = 10;
        return c;
    }

    std::string DetailOf(int code)
    {
        auto a = AlarmManager::Get(code);
        return a ? a->Detail : std::string("(not active)");
    }

    // ───────────────────────── AxisScale / AxisAngle ─────────────────────────

    /// <summary>SelfTest.TestAxisScale — quy đổi mm ⇄ xung, mm/s ⇄ rpm.</summary>
    void AxisScaleTests()
    {
        // Vít me bước 10 mm, encoder 10000 xung/vòng → 1000 xung/mm.
        AxisConfig cfg;
        cfg.Name = "TEST"; cfg.Unit = "mm"; cfg.PulsePerUnit = 1000; cfg.UnitPerRev = 10; cfg.MaxRpm = 3000;
        AxisScale s(cfg);

        CHECK(s.ToPulse(20) == 20000);
        CHECK(s.ToPulse(-20) == -20000);
        CHECK(Near(s.ToUnit(20000), 20));
        CHECK(Near(s.ToUnit(s.ToPulse(37.123)), 37.123));

        // Đúng ví dụ đã chốt: gõ 12.345 mm ở trang Manual, số NGUYÊN gửi xuống Modbus phải là 12345 xung.
        CHECK_MSG(s.ToPulse(12.345) == 12345, std::to_string(s.ToPulse(12.345)));

        // Làm tròn phải ra số gần nhất, không cắt cụt: cắt cụt tích luỹ thành sai vị trí.
        CHECK(s.ToPulse(0.0006) == 1);
        CHECK(s.ToPulse(-0.0006) == -1);

        // Tràn int biến lệnh đi xa thành lệnh đi ngược hết cỡ — phải cắt ở biên.
        CHECK(s.ToPulse(1e12) == std::numeric_limits<int>::max() && s.ToPulse(-1e12) == std::numeric_limits<int>::min());

        // 100 mm/s với bước 10 mm/vòng = 10 vòng/s = 600 rpm.
        CHECK(s.ToRpm(100) == 600);
        CHECK(s.ToRpm(-100) == 600);
        CHECK(s.ToRpm(10000) == 3000);
        // Kẹp sàn ở 1: rpm = 0 bị driver hiểu là đứng yên, lệnh treo tới khi timeout.
        CHECK(s.ToRpm(0.0001) == 1);
        CHECK(Near(s.FromRpm(600), 100));

        // Fastech nhận xung/giây chứ không phải rpm.
        CHECK(s.ToPulsePerSec(100) == 100000u);
        CHECK(s.ToPulsePerSec(0) == 1u);

        // Trục xoay: 18000 xung/vòng ĐỘNG CƠ qua hộp số 1/2 → 36000 xung cho một vòng đầu hút.
        AxisConfig rot;
        rot.Name = "A1"; rot.Unit = "deg"; rot.WrapDegrees = true;
        rot.PulsePerUnit = SystemData::RotaryPulsePerDeg; rot.UnitPerRev = SystemData::RotaryDegPerMotorRev; rot.MaxRpm = 3000;
        AxisScale r(rot);
        CHECK(Near(r.PulsePerUnit(), 36000.0 / 360.0));
        CHECK(r.ToPulse(360) == 36000);
        CHECK(r.ToPulse(90) == 9000);
        // PulsePerRev phải TRÒN: mọi phép chọn vòng gần nhất cộng trừ bội số của nó.
        CHECK(r.PulsePerRev() == 36000);
        // 180°/s đầu hút = 1 vòng động cơ/giây = 18000 xung/s = 60 rpm.
        CHECK(r.ToPulsePerSec(180) == 18000u);
        CHECK(r.ToRpm(180) == 60);

        // Cấu hình hỏng (chia cho 0) không được làm sập vòng điều khiển.
        AxisConfig badCfg;
        badCfg.Name = "BAD"; badCfg.PulsePerUnit = 0; badCfg.UnitPerRev = 0;
        AxisScale bad(badCfg);
        CHECK(bad.ToPulse(5) == 5 && Near(bad.ToUnit(5), 5));

        // Giữ shared_ptr tới cấu hình như tham chiếu C#: sửa ở trang Setting thì phép quy đổi theo ngay.
        auto live = std::make_shared<AxisConfig>(cfg);
        AxisScale ls(live);
        live->PulsePerUnit = 2000;
        CHECK(ls.ToPulse(1) == 2000);

        // PulsePerUnit là HẰNG SỐ ĐÃ CHỐT (electronic gear trong driver) — default trong SystemData phải khớp.
        auto def = SystemData::CreateDefault();
        SystemData& sys = Deref(def);
        for (const char* name : { AxisId::X, AxisId::Z1, AxisId::Z2, AxisId::Y1, AxisId::Y2 })
        {
            auto a = sys.GetAxis(name);
            CHECK_MSG(Near(a->PulsePerUnit, 1000.0), name);
            // Số hiệu block là quy ước đã chốt của ServoAxisConfig (Block0=ORG, 1=MoveAbs, 2/3=Jog+/−).
            CHECK_MSG(a->HomeBlock == 0 && a->AbsoluteBlock == 1 && a->JogPositiveBlock == 2 && a->JogNegativeBlock == 3, name);
            CHECK_MSG(a->Kind == EAxisKind::Panasonic, name);
        }
        for (const char* name : { AxisId::A1, AxisId::A2 })
        {
            auto a = sys.GetAxis(name);
            CHECK_MSG(Near(a->PulsePerUnit, 36000.0 / 360.0) && a->Kind == EAxisKind::Fastech && a->WrapDegrees, name);
            CHECK_MSG(Near(a->UnitPerRev, 180) && a->InSoftLimit(370) && a->InSoftLimit(-5), name);
        }
        CHECK(sys.GetAxis(AxisId::A1)->BoardId == FastechBoardId::AxisA1 && sys.GetAxis(AxisId::A2)->BoardId == FastechBoardId::AxisA2);
        CHECK(FastechBoardId::AxisA1 != FastechBoardId::Io32Di && FastechBoardId::AxisA2 != FastechBoardId::Io32Do
              && FastechBoardId::AxisA1 != FastechBoardId::AxisA2 && FastechBoardId::Io32Di != FastechBoardId::Io32Do);
        CHECK(!sys.GetAxis(AxisId::X)->InSoftLimit(sys.GetAxis(AxisId::X)->SoftLimitPos + 1));
    }

    /// <summary>Phần số học góc của SelfTest.TestRotaryWrap.</summary>
    void AxisAngleTests()
    {
        CHECK(Near(AxisAngle::Wrap(-10), 350));
        CHECK(Near(AxisAngle::Wrap(370), 10));
        CHECK(Near(AxisAngle::Wrap(360), 0));
        CHECK(Near(AxisAngle::Wrap(-1e-7), 0));      // không thành 359.9999999
        CHECK(Near(AxisAngle::Wrap(720), 0));
        CHECK(Near(AxisAngle::Wrap(std::numeric_limits<double>::quiet_NaN()), 0));
        // Chỗ dễ sai nhất của trục quay: đi từ 350° tới 10° là +20°, không phải −340°.
        CHECK(Near(AxisAngle::ShortestDelta(350, 10), 20));
        CHECK(Near(AxisAngle::ShortestDelta(10, 350), -20));
        CHECK(Near(AxisAngle::ShortestDelta(0, 180), 180));   // đúng nửa vòng thì đi chiều DƯƠNG
        CHECK(Near(AxisAngle::Distance(359, 1), 2));
    }

    // ───────────────────────── FakeAxis ─────────────────────────

    void FakeAxisTests()
    {
        AlarmManager::ForceClearAll();
        auto cfg = MakeCfg("X");
        auto ax = std::make_shared<FakeAxis>(cfg);
        Ticker tick([ax] { ax->Refresh(); });
        std::stop_source never;

        CHECK(ax->Name() == "X" && ax->Config() == cfg && !ax->IsConnected() && !ax->SetServoOn(true));
        CHECK_NEAR(ax->CurrentPos(), 10.0, 1e-9);
        CHECK(ax->Connect() && ax->IsConnected() && !ax->ServoOn());
        CHECK(!ax->Home(never.get_token()));                       // servo chưa bật
        CHECK(ax->SetServoOn(true) && ax->ServoOn());
        CHECK(!ax->MoveAbs(50, 100, never.get_token()) && AlarmManager::IsActive(Code("X", AxisFault::NotHomed)));
        CHECK(AlarmManager::Get(Code("X", AxisFault::NotHomed)).value_or(AlarmData{}).ConditionActive == false);   // sự kiện

        FakeAxis::SetSpeedFactor(20);
        CHECK(ax->Home(never.get_token()) && ax->OrgOk() && !ax->Busy());
        CHECK_NEAR(ax->CurrentPos(), 0.0, 1e-9);
        CHECK(ax->MoveAbs(50, 300, never.get_token()) && !ax->Busy());
        CHECK_NEAR(ax->CurrentPos(), 50.0, 0.01);
        CHECK(ax->MoveInc(5, 300, never.get_token()));
        CHECK_NEAR(ax->CurrentPos(), 55.0, 0.02);
        CHECK(ax->GentleMoves() == 0 && ax->MoveAbs(40, 77, never.get_token(), true) && ax->GentleMoves() == 1 && ax->LastGentleSpeed() == 77);

        // Soft limit: đích ngoài hành trình bị từ chối, trục không nhúc nhích; chữ chi tiết qua Loc ("Axis.SoftLimitOutSim").
        CHECK(!ax->MoveAbs(700, 300, never.get_token()) && AlarmManager::IsActive(Code("X", AxisFault::SoftLimit)));
        CHECK_MSG(DetailOf(Code("X", AxisFault::SoftLimit)) == "Tgt 700.000mm outside [-1, 600]", DetailOf(Code("X", AxisFault::SoftLimit)));
        CHECK(!ax->MoveInc(-100, 300, never.get_token()));
        CHECK_NEAR(ax->CurrentPos(), 40.0, 0.02);

        // Jog + Stop, Jog(0).
        FakeAxis::SetSpeedFactor(1);
        ax->Jog(+1, 50);
        base::Delay(120);
        CHECK(ax->Busy() && ax->CurrentPos() > 41.0);
        ax->Stop();
        double p1 = ax->CurrentPos();
        base::Delay(60);
        CHECK(!ax->Busy() && ax->CurrentPos() == p1);
        ax->Jog(-1, 50);
        base::Delay(60);
        ax->Jog(0, 0);
        CHECK(!ax->Busy() && ax->CurrentPos() < p1);

        // Huỷ giữa chừng qua stop_token: trả false (huỷ thấy ở đầu vòng chờ — FakeAxis không ném).
        {
            std::stop_source cts;
            auto fut = base::RunAsync([&] { return ax->MoveAbs(500, 50, cts.get_token()); });
            base::Delay(100);
            CHECK(ax->Busy());
            base::Stopwatch sw;
            cts.request_stop();
            CHECK(!fut.get() && sw.ElapsedMilliseconds() < 1000 && !ax->Busy() && ax->CurrentPos() < 100);
        }

        // Timeout: SimStall chạy chậm 20 lần, MoveTimeoutMs ngắn.
        cfg->MoveTimeoutMs = 150;
        ax->SetSimStall(true);
        CHECK(!ax->MoveAbs(300, 50, never.get_token()) && AlarmManager::IsActive(Code("X", AxisFault::MoveTimeout)) && !ax->Busy());
        CHECK_MSG(DetailOf(Code("X", AxisFault::MoveTimeout)).find("short of Tgt") != std::string::npos, DetailOf(Code("X", AxisFault::MoveTimeout)));
        ax->SetSimStall(false);
        cfg->MoveTimeoutMs = 30000;
        cfg->HomeTimeoutMs = 100;
        ax->SetSimStall(true);
        CHECK(!ax->Home(never.get_token()) && AlarmManager::IsActive(Code("X", AxisFault::HomeTimeout)) && !ax->OrgOk());
        CHECK_MSG(DetailOf(Code("X", AxisFault::HomeTimeout)) == "> 100 ms", DetailOf(Code("X", AxisFault::HomeTimeout)));
        ax->SetSimStall(false);
        cfg->HomeTimeoutMs = 60000;
        FakeAxis::SetSpeedFactor(20);
        CHECK(ax->Home(never.get_token()) && ax->OrgOk());

        // Bơm alarm driver giữa lúc chạy rồi reset.
        FakeAxis::SetSpeedFactor(1);
        {
            auto fut = base::RunAsync([&] { return ax->MoveAbs(400, 50, never.get_token()); });
            base::Delay(60);
            ax->SetSimDriverAlarm(true);
            CHECK(!fut.get() && ax->Alarm() && ax->ErrorCode() == 0x1601 && AlarmManager::IsActive(Code("X", AxisFault::DriverAlarm)));
            CHECK(DetailOf(Code("X", AxisFault::DriverAlarm)) == "0x1601");
            CHECK(!ax->MoveAbs(10, 50, never.get_token()));
            ax->Jog(+1, 50);
            CHECK(!ax->Busy());
            CHECK(ax->ResetAlarm() && !ax->Alarm() && ax->ErrorCode() == 0);
            CHECK(AlarmManager::Get(Code("X", AxisFault::DriverAlarm)).value_or(AlarmData{}).ConditionActive == false);
        }

        // Cữ cứng suy từ vị trí; mất kết nối giả; Disconnect làm mất gốc.
        CHECK(!ax->FLS() && !ax->RLS());
        ax->SetSimDisconnected(true);
        CHECK(!ax->IsConnected() && !ax->SetServoOn(false));
        ax->SetSimDisconnected(false);
        CHECK(ax->SetServoOn(false) && !ax->ServoOn() && ax->OrgOk());   // tắt servo không mất gốc
        ax->Disconnect();
        CHECK(!ax->IsConnected() && !ax->ServoOn() && !ax->OrgOk());

        // Vị trí chờ trùng gốc → đặt lệch 15 theo chiều ngược HomeDir, kẹp trong soft limit.
        auto c0 = MakeCfg("Y1"); c0->StandbyPos = 0; c0->HomeDir = -1;
        CHECK_NEAR(FakeAxis(c0).CurrentPos(), 15.0, 1e-9);
        auto c1 = MakeCfg("Y2"); c1->StandbyPos = 0;                      // HomeDir mặc định +1 → −15, kẹp về −1
        CHECK_NEAR(FakeAxis(c1).CurrentPos(), -1.0, 1e-9);

        // Trục quay quấn vòng: Move Abs đi đường ngắn, Move Inc nhích đúng delta.
        auto rc = MakeCfg("A1"); rc->Unit = "deg"; rc->WrapDegrees = true; rc->SoftLimitNeg = 0; rc->SoftLimitPos = 360; rc->StandbyPos = 0;
        auto rot = std::make_shared<FakeAxis>(rc);
        CHECK_NEAR(rot->CurrentPos(), 345.0, 1e-9);                      // −15 chưa kẹp (trục quay), hiển thị quấn
        Ticker tick2([rot] { rot->Refresh(); });
        FakeAxis::SetSpeedFactor(20);
        CHECK(rot->Connect() && rot->SetServoOn(true) && rot->Home(never.get_token()));
        CHECK_NEAR(rot->CurrentPos(), 0.0, 1e-9);
        FakeAxis::SetSpeedFactor(1);
        base::Stopwatch sw;
        CHECK(rot->MoveAbs(350, 100, never.get_token()));          // 0° → 350° = −10°, không phải +350°
        CHECK(sw.ElapsedMilliseconds() < 1500);
        CHECK(AxisAngle::Distance(rot->CurrentPos(), 350.0) < 0.01 && rot->CurrentPos() >= 0 && rot->CurrentPos() < 360);
        sw.Restart();
        CHECK(rot->MoveAbs(10, 100, never.get_token()) && sw.ElapsedMilliseconds() < 1500);   // 350° → 10° = +20°
        CHECK(AxisAngle::Distance(rot->CurrentPos(), 10.0) < 0.01);
        FakeAxis::SetSpeedFactor(50);
        CHECK(rot->MoveInc(200, 100, never.get_token()));           // nhích đúng +200°, không đổi thành −160°
        CHECK(AxisAngle::Distance(rot->CurrentPos(), 210.0) < 0.02);
        CHECK(rot->MoveAbs(370, 100, never.get_token()) && AxisAngle::Distance(rot->CurrentPos(), 10.0) < 0.01);   // không có soft limit
        CHECK(!rot->FLS() && !rot->RLS());
        FakeAxis::SetSpeedFactor(1);
        AlarmManager::ForceClearAll();
    }

    // ───────────────────────── FakeIo ─────────────────────────

    void FakeIoTests()
    {
        FakeIo io;
        int calls = 0, lastBit = -1;
        bool sensorAtHook = false;
        io.AfterSetDo = [&](int bit, bool on)
        {
            ++calls; lastBit = bit;
            if (bit == IoMap::DoVacuum1) sensorAtHook = io.GetDi(IoMap::Vacuum1) == on;   // cảm biến đã đổi TRƯỚC khi gọi móc
        };
        CHECK(!io.IsConnected() && io.Connect() && io.IsConnected());

        // Tín hiệu an toàn NC mặc định bình thường; tray đầu vào 1 + tray lắp ráp có, tray đầu vào 2 vắng.
        CHECK(io.GetDi(IoMap::EmgOk) && io.GetDi(IoMap::LightCurtainOk) && io.GetDi(IoMap::AirOk));
        CHECK(io.GetDi(IoMap::TrayIn1Present) && io.GetDi(IoMap::TrayAssyPresent) && !io.GetDi(IoMap::TrayIn2Present));
        CHECK(!io.GetDi(IoMap::BtnStop) && !io.GetDi(IoMap::BtnArmA1) && !io.GetDi(IoMap::Vacuum1));
        CHECK(!io.GetDi(-1) && !io.GetDi(IoMap::DiCount) && !io.GetDo(-1) && !io.GetDo(IoMap::DoCount));

        io.SetDo(IoMap::DoBuzzer, true); io.SetDo(IoMap::DoBuzzer, true); io.SetDo(99, true);
        CHECK(io.GetDo(IoMap::DoBuzzer) && calls == 1 && lastBit == IoMap::DoBuzzer);
        io.SetDo(IoMap::DoBuzzer, false);
        CHECK(!io.GetDo(IoMap::DoBuzzer) && calls == 2);

        // Cảm biến chân không bám theo van hút; PickSuccessRate = 0 → hút hụt.
        io.SetDo(IoMap::DoVacuum1, true);
        CHECK(io.GetDi(IoMap::Vacuum1) && sensorAtHook && calls == 3);
        CHECK(io.PickSuccessRate() == 1.0);
        io.SetPickSuccessRate(0);
        io.SetDo(IoMap::DoVacuum2, true);
        CHECK(io.GetDo(IoMap::DoVacuum2) && !io.GetDi(IoMap::Vacuum2));
        io.SetPickSuccessRate(1.0);
        io.SetDo(IoMap::DoVacuum1, false);
        CHECK(!io.GetDi(IoMap::Vacuum1) && sensorAtHook);
        io.AfterSetDo = nullptr;
        int before = calls;
        io.SetDo(IoMap::DoBlow1, true);
        CHECK(calls == before && !io.AfterSetDo);

        // DI giữ mức: sườn chỉ tồn tại tới vòng quét kế tiếp.
        const int spare = 20;
        io.SimSet(spare, true);
        CHECK(io.GetDi(spare) && io.RisingEdge(spare));
        io.Refresh();
        CHECK(io.GetDi(spare) && !io.RisingEdge(spare) && !io.FallingEdge(spare));
        io.SimSet(spare, false);
        CHECK(io.FallingEdge(spare));
        io.Refresh();
        CHECK(!io.FallingEdge(spare));
        io.SimToggle(IoMap::EmgOk);
        CHECK(!io.GetDi(IoMap::EmgOk));
        io.SimToggle(IoMap::EmgOk);
        CHECK(io.GetDi(IoMap::EmgOk));
        io.SimSet(-1, true); io.SimToggle(IoMap::DiCount);                  // ngoài dải: bỏ qua

        // Nút nhấn-nhả (Stop, bốn nút Start): lên 1 đúng MỘT vòng quét rồi tự nhả.
        for (int btn : { IoMap::BtnStop, IoMap::BtnArmA1, IoMap::BtnArmA2, IoMap::BtnGoB1, IoMap::BtnGoB2 })
        {
            io.SimPress(btn);
            CHECK(!io.GetDi(btn));
            io.Refresh();
            CHECK(io.GetDi(btn) && io.RisingEdge(btn));
            io.Refresh();
            CHECK(!io.GetDi(btn) && io.FallingEdge(btn) && !io.RisingEdge(btn));
            io.Refresh();
            CHECK(!io.FallingEdge(btn));
        }
        // Bit không phải nút: SimPress đặt lên rồi GIỮ (chỉ năm nút trên tự nhả).
        io.SimPress(spare);
        io.Refresh();
        io.Refresh();
        CHECK(io.GetDi(spare));
        io.Disconnect();
        CHECK(!io.IsConnected());
    }

    // ───────────────────────── MotionConnect (mô phỏng) ─────────────────────────

    void MotionSimTests()
    {
        AlarmManager::ForceClearAll();
        auto sys = McDataManager::SysData();
        sys->SimulationMode = true;
        MotionConnect motion;
        CHECK(!motion.AllConnected() && !motion.AnyAlarm() && motion.AxisList().empty() && !motion.ZSafeOk());
        motion.Init(*sys);
        Ticker tick([&] { motion.Refresh(); });
        std::stop_source never;

        CHECK(motion.Simulation() && motion.AxisList().size() == 7 && motion.Axes().size() == 7);
        CHECK(motion.Axis("Q") == nullptr && motion["Z1"] == motion.AxisList()[3] && motion.Axis("Z1")->Config() == sys->GetAxis("Z1"));
        for (size_t i = 0; i < AxisId::All.size(); ++i)
            CHECK_MSG(motion.AxisList()[i]->Name() == AxisId::All[i], AxisId::All[i]);
        for (const auto& a : motion.AxisList()) CHECK_MSG(std::dynamic_pointer_cast<FakeAxis>(a) != nullptr, a->Name());
        CHECK(!motion.AllConnected() && motion.ConnectAll() && motion.AllConnected() && !motion.AllServoOn());
        for (auto& a : motion.AxisList()) CHECK(a->SetServoOn(true));
        CHECK(motion.AllServoOn() && !motion.AllHomed() && !motion.AnyAlarm() && !motion.AnyBusy());
        CHECK(motion.ZSafe() == sys->ZSafe && motion.ZSafeLimit() == sys->ZSafe + MotionConnect::ZSafeMargin);

        // ── Chốt chặn Z_SAFE (SelfTest.TestZSafeInterlock ở tầng MotionConnect) ──
        // Z dương là chiều ĐI XUỐNG, "an toàn" là vị trí NHỎ. Đặt Z_SAFE ngay trên chỗ Z đang đứng rồi hạ cả hai.
        auto z1 = motion["Z1"];
        auto z2 = motion["Z2"];
        motion.SetZSafe(std::max(z1->CurrentPos(), z2->CurrentPos()) + 5.0);
        CHECK(motion.ZSafeOk());
        z1->Jog(1, 200); z2->Jog(1, 200);
        WaitUntil([&] { return z1->CurrentPos() > motion.ZSafeLimit() + 1 && z2->CurrentPos() > motion.ZSafeLimit() + 1; }, 4000);
        z1->Jog(0, 0); z2->Jog(0, 0);
        CHECK_MSG(!motion.ZSafeOk(), std::format("Z1={:.1f} Z2={:.1f}", z1->CurrentPos(), z2->CurrentPos()));
        // Rút một trục thôi — vẫn phải chặn, vì điều kiện là CẢ HAI.
        z1->Jog(-1, 200);
        WaitUntil([&] { return z1->CurrentPos() < motion.ZSafe() - 1; }, 4000);
        z1->Jog(0, 0);
        CHECK(!motion.ZSafeOk());
        z2->Jog(-1, 200);
        WaitUntil([&] { return motion.ZSafeOk(); }, 4000);
        z2->Jog(0, 0);
        CHECK(motion.ZSafeOk());
        // Ngưỡng nới ZSafeMargin = 1 mm trên Z_SAFE: dừng lệch quanh Z_SAFE không làm chốt chặn báo sai.
        motion.SetZSafe(z1->CurrentPos() - 0.5);
        CHECK(motion.ZSafeOk() == (z2->CurrentPos() <= motion.ZSafeLimit()));
        motion.SetZSafe(z1->CurrentPos() - 1.5);
        CHECK(!motion.ZSafeOk());
        motion.SetZSafe(sys->ZSafe);

        // ── Home qua MotionConnect: IsHoming bật trong lúc chạy, tắt khi xong ──
        {
            auto fut = base::RunAsync([&] { return motion.Home(z1, never.get_token()); });
            base::Delay(80);
            CHECK(motion.IsHoming("Z1") && !motion.IsHoming("X") && motion.AnyBusy());
            CHECK(fut.get() && !motion.IsHoming("Z1") && z1->OrgOk());
        }

        // ── Y1/Y2 về gốc chạm Limit− là bình thường: không báo HardLimit trong lúc về gốc; X thì vẫn báo ──
        {
            auto y1 = motion["Y1"];
            auto x = motion["X"];
            double y1Neg = y1->Config()->SoftLimitNeg, xNeg = x->Config()->SoftLimitNeg;
            // Đẩy soft limit âm lên trên vị trí hiện tại → FakeAxis báo RLS (cữ cứng sau soft limit 5 mm).
            y1->Config()->SoftLimitNeg = 1000;
            x->Config()->SoftLimitNeg = 1000;
            CHECK(WaitUntil([&] { return AlarmManager::IsActive(Code("Y1", AxisFault::HardLimit)) && AlarmManager::IsActive(Code("X", AxisFault::HardLimit)); }, 500));
            CHECK(DetailOf(Code("Y1", AxisFault::HardLimit)) == "NOT");
            std::stop_source cts;
            auto fy = base::RunAsync([&] { return motion.Home(y1, cts.get_token()); });
            auto fx = base::RunAsync([&] { return motion.Home(x, cts.get_token()); });
            CHECK(WaitUntil([&] { return motion.IsHoming("Y1") && motion.IsHoming("X"); }, 500));
            base::Delay(60);
            CHECK(y1->RLS() && x->RLS());
            CHECK(AlarmManager::Get(Code("Y1", AxisFault::HardLimit)).value_or(AlarmData{}).ConditionActive == false);
            CHECK(AlarmManager::Get(Code("X", AxisFault::HardLimit)).value_or(AlarmData{}).ConditionActive == true);
            cts.request_stop();
            CHECK(!fy.get() && !fx.get() && !y1->OrgOk() && !x->OrgOk());
            y1->Config()->SoftLimitNeg = y1Neg;
            x->Config()->SoftLimitNeg = xNeg;
            base::Delay(40);
            AlarmManager::ForceClearAll();
        }

        FakeAxis::SetSpeedFactor(50);
        for (auto& a : motion.AxisList())
            if (a->Name() != "Z1") CHECK_MSG(motion.Home(a, never.get_token()), a->Name());
        CHECK(motion.AllHomed() && !motion.AnyBusy());
        FakeAxis::SetSpeedFactor(1);

        // ── Inching (SelfTest.TestMoveIncrement ở tầng trục) ──
        {
            auto x = motion["X"];
            double start = x->CurrentPos();
            double speed = x->Config()->MoveSpeed;
            CHECK(x->MoveInc(5.0, speed, never.get_token()));
            CHECK_MSG(Near(x->CurrentPos(), start + 5.0), std::format("start={:.3f} now={:.3f}", start, x->CurrentPos()));
            CHECK(x->MoveInc(-5.0, speed, never.get_token()));
            CHECK(Near(x->CurrentPos(), start));
            // Soft limit của Inching kiểm theo đích TUYỆT ĐỐI = vị trí hiện tại + delta, không theo độ lớn delta.
            double overshoot = x->Config()->SoftLimitPos + 50 - x->CurrentPos();
            CHECK(!x->MoveInc(overshoot, speed, never.get_token()) && Near(x->CurrentPos(), start));
            CHECK(AlarmManager::IsActive(Code("X", AxisFault::SoftLimit)));
            AlarmManager::ForceClearAll();
        }

        // ── Trục quay A1 (SelfTest.TestRotaryWrap — hành vi thật của trục) ──
        {
            auto a1 = motion["A1"];
            double speed = a1->Config()->MoveSpeed;
            CHECK_MSG(Near(a1->CurrentPos(), AxisAngle::Wrap(a1->Config()->HomeOffset)), std::to_string(a1->CurrentPos()));
            CHECK(a1->MoveAbs(350, speed, never.get_token()) && AxisAngle::Distance(a1->CurrentPos(), 350) < 0.05);
            base::Stopwatch sw;
            CHECK(a1->MoveAbs(10, speed, never.get_token()) && AxisAngle::Distance(a1->CurrentPos(), 10) < 0.05);
            double expectedShortMs = 20.0 / speed * 1000.0;
            CHECK_MSG(sw.ElapsedMilliseconds() < expectedShortMs * 4 + 500, std::to_string(sw.ElapsedMilliseconds()) + " ms");
            CHECK(a1->MoveAbs(370, speed, never.get_token()) && AxisAngle::Distance(a1->CurrentPos(), 10) < 0.05);
            CHECK(a1->CurrentPos() >= 0 && a1->CurrentPos() < 360);
            CHECK(a1->MoveAbs(-30, speed, never.get_token()) && AxisAngle::Distance(a1->CurrentPos(), 330) < 0.05);
            AlarmManager::ForceClearAll();
        }

        motion["X"]->Jog(+1, 20);
        motion["A2"]->Jog(-1, 20);
        CHECK(motion.AnyBusy());
        motion.StopAll();
        CHECK(!motion.AnyBusy());

        // Alarm driver: điều kiện tiếp diễn — vòng quét Raise kèm mã, hết thì ClearCondition.
        auto y1f = std::dynamic_pointer_cast<FakeAxis>(motion["Y1"]);
        y1f->SetSimDriverAlarm(true);
        CHECK(WaitUntil([&] { return motion.AnyAlarm() && AlarmManager::IsActive(Code("Y1", AxisFault::DriverAlarm)); }, 500));
        CHECK(DetailOf(Code("Y1", AxisFault::DriverAlarm)) == "0x1601");
        CHECK(motion["Y1"]->ResetAlarm() && !motion.AnyAlarm());

        // Mất kết nối giả → +00; nối lại → hết nguyên nhân (chờ RESET).
        auto xf = std::dynamic_pointer_cast<FakeAxis>(motion["X"]);
        xf->SetSimDisconnected(true);
        CHECK(WaitUntil([&] { return AlarmManager::IsActive(Code("X", AxisFault::Disconnected)); }, 500) && !motion.AllConnected());
        xf->SetSimDisconnected(false);
        CHECK(WaitUntil([&] { return AlarmManager::Get(Code("X", AxisFault::Disconnected)).value_or(AlarmData{}).ConditionActive == false; }, 500));
        AlarmManager::Reset();
        CHECK(!AlarmManager::IsActive(Code("X", AxisFault::Disconnected)));

        motion.DisconnectAll();
        base::Delay(40);
        CHECK(!motion.AllConnected() && !motion.AllHomed() && AlarmManager::IsActive(Code("X", AxisFault::Disconnected)));
        AlarmManager::ForceClearAll();
    }

    // ───────────────────────── Lớp thật: chỉ dựng, KHÔNG kết nối ─────────────────────────

    /// <summary>
    /// SimulationMode = false → FastechAxis cho trục Kind = Fastech, PanasonicAxis cho phần còn lại; không bao giờ rơi
    /// về FakeAxis. Chưa Connect thì mọi lệnh trả về sớm, trước khi chạm thư viện Fastech hay cổng COM — bài này dựa
    /// đúng vào điều đó, và tuyệt đối không gọi Connect/ConnectAll/Disconnect/DisconnectAll hay IoConnect::Refresh.
    /// </summary>
    void HardwareSelectionTests()
    {
        AlarmManager::ForceClearAll();
        auto def = SystemData::CreateDefault();
        SystemData& hwSys = Deref(def);
        hwSys.SimulationMode = false;
        std::stop_source never;

        MotionConnect hw;
        hw.Init(hwSys);
        CHECK(!hw.Simulation() && hw.AxisList().size() == 7);
        for (const auto& a : hw.AxisList())
        {
            bool fastech = a->Config()->Kind == EAxisKind::Fastech;
            CHECK_MSG(std::dynamic_pointer_cast<FakeAxis>(a) == nullptr, a->Name());
            CHECK_MSG(fastech ? std::dynamic_pointer_cast<FastechAxis>(a) != nullptr
                              : std::dynamic_pointer_cast<PanasonicAxis>(a) != nullptr, a->Name());
            CHECK_MSG(!a->IsConnected() && !a->ServoOn() && !a->OrgOk() && !a->Busy() && !a->Alarm(), a->Name());
            // Chưa kết nối: mọi lệnh trả false ngay, không gửi gì.
            CHECK_MSG(!a->SetServoOn(true) && !a->Home(never.get_token()) && !a->MoveAbs(1, 1, never.get_token())
                      && !a->MoveInc(1, 1, never.get_token()) && !a->ResetAlarm(), a->Name());
            a->Jog(+1, 10);
            a->Jog(0, 0);
            a->Stop();
        }
        CHECK(std::dynamic_pointer_cast<FastechAxis>(hw["A1"]) != nullptr && std::dynamic_pointer_cast<FastechAxis>(hw["A2"]) != nullptr);
        CHECK(std::dynamic_pointer_cast<PanasonicAxis>(hw["X"]) != nullptr && std::dynamic_pointer_cast<PanasonicAxis>(hw["Z2"]) != nullptr);

        // Vòng quét trên trục chưa mở: không đọc gì xuống driver, chỉ báo mất kết nối (+00) cho cả 7 trục.
        hw.Refresh();
        for (const char* name : AxisId::All)
            CHECK_MSG(AlarmManager::IsActive(Code(name, AxisFault::Disconnected)), name);
        CHECK(!hw.AllConnected() && !hw.AnyBusy() && !hw.AnyAlarm());
        hw.StopAll();
        auto a1 = std::dynamic_pointer_cast<FastechAxis>(hw["A1"]);
        CHECK(a1 && a1->RawStatus() == 0u && a1->CurrentPos() == 0.0);

        // IoConnect chưa nối: chỉ đụng bộ đệm.
        IoConnect io(Shared(def));
        CHECK(!io.IsConnected() && !io.GetDi(IoMap::EmgOk) && io.RawDi() == 0u && io.RawDo() == 0u);
        io.SetDo(IoMap::DoLampGreen, true);
        io.SetDo(IoMap::DoCount, true);                               // ngoài dải: bỏ qua
        CHECK(io.RawDo() == (1u << IoMap::DoLampGreen) && !io.GetDo(IoMap::DoLampGreen));   // chưa ghi xuống module
        io.SetDo(IoMap::DoLampGreen, false);
        CHECK(io.RawDo() == 0u && !io.RisingEdge(IoMap::BtnStop) && !io.FallingEdge(IoMap::BtnStop));

        // Không bài nào ở đây được phép nạp EziMOTIONPlusE.dll.
        CHECK(!EziMOTIONPlusELib::IsLoaded());
        AlarmManager::ForceClearAll();
    }

    // ───────────────────────── PanasonicAxis ─────────────────────────

    /// <summary>SelfTest.TestMoveCompletionRule — luật xong / chờ / gửi lại / bỏ của một lệnh chạy servo Panasonic.</summary>
    void MoveCompletionRuleTests()
    {
        using W = PanasonicAxis::EMoveWait;
        auto j = &PanasonicAxis::JudgeMove;
        const int max = PanasonicAxis::MaxMoveResends;

        CHECK(j(true, false, 5, true, 0, true) == W::Done);      // vào dung sai, hết BUSY là xong — không chờ INP
        CHECK(j(true, true, 5, true, 0, true) == W::Wait);       // vào dung sai mà còn BUSY thì chờ
        CHECK(j(false, true, 10, false, 0, true) == W::Wait);    // đang BUSY thì chờ, không gửi lại
        CHECK(j(false, false, 1, false, 0, true) == W::Wait);    // chưa đủ hai lượt đọc sau lúc gửi
        CHECK(j(false, false, 5, true, 0, true) == W::Wait);     // đã nhúc nhích (đang về đích) thì chờ
        CHECK(j(false, false, 2, false, 0, true) == W::Resend);  // hai lượt đọc mới: không BUSY, chưa tới, chưa nhúc nhích
        CHECK(j(false, false, 2, false, max - 1, true) == W::Resend && j(false, false, 2, false, max, true) == W::Lost);
        // 15/10: thư viện nay gửi cả lệnh 0.01 mm — nằm gọn trong dung sai "đã tới" 0.05 mm.
        CHECK(j(true, false, 0, false, 0, true) == W::Wait && j(true, false, 1, false, 0, true) == W::Wait);
        CHECK(j(true, false, 2, false, 0, true) == W::Done);
        CHECK(j(true, false, 0, false, 0, false) == W::Done);    // thư viện không gửi (đã ở đích): xong ngay

        // "Đã tới" so bằng XUNG; ngưỡng bỏ qua của thư viện không bao giờ vượt dung sai "đã tới".
        AxisConfig wide; wide.PositionTolerancePulse = 50; wide.SkipMoveTolerancePulse = 80;
        AxisConfig tight; tight.PositionTolerancePulse = 3; tight.SkipMoveTolerancePulse = 5;
        AxisConfig norm; norm.PositionTolerancePulse = 50;          // SkipMoveTolerancePulse mặc định 5
        CHECK(PanasonicAxis::LibrarySkipTolerance(wide) == 50 && PanasonicAxis::LibrarySkipTolerance(tight) == 3);
        CHECK(PanasonicAxis::LibrarySkipTolerance(norm) == 5);
        AxisConfig neg; neg.SkipMoveTolerancePulse = -7;
        CHECK(PanasonicAxis::LibrarySkipTolerance(neg) == 0);
        AxisScale scale(norm);
        CHECK(PanasonicAxis::WithinPulses(scale.ToPulse(46.470), scale.ToPulse(46.520), 50)
              && std::fabs(46.470 - 46.520) > scale.ToUnit(50));   // phép so mm cũ ra 0.0500000004 > 0.05
        CHECK(!PanasonicAxis::WithinPulses(46470, 46521, 50) && PanasonicAxis::WithinPulses(46470, 46469, 0)
              && !PanasonicAxis::WithinPulses(46470, 46472, 0));    // dung sai ≤ 0 vẫn tính là 1 xung
    }

    using Regs = std::map<uint16_t, uint16_t>;
    using A6 = servo::A6SFRegisters;

    uint16_t At(const Regs& s, uint16_t a)
    {
        auto it = s.find(a);
        return it != s.end() ? it->second : (uint16_t)0;
    }

    /// <summary>Vị trí 32 bit của một block: word thấp trước (ServoService ghi một lần 0x10 hai word).</summary>
    int PosOf(const Regs& s, uint16_t addr) { return (int)((uint32_t)At(s, addr) | (uint32_t)At(s, (uint16_t)(addr + 1)) << 16); }

    std::string Show(const std::vector<servo::FakeA6sfPort::Tx>& txs)
    {
        std::string s = std::to_string(txs.size()) + ":";
        for (const auto& t : txs) s += " · " + t.ToString();
        return s;
    }

    /// <summary>
    /// SelfTest.TestServoModbusWrites — lệnh chạy chỉ gửi xuống driver thứ driver chưa có (Tuấn 2026-09-25: ghi đi ghi lại
    /// các thanh ghi làm tăng độ trễ). Mỗi mục kiểm cả hai phía: SỐ giao dịch trước STB (đã bớt ghi chưa) và bảng thanh
    /// ghi driver chụp ở cạnh STB (bớt ghi rồi driver có còn chạy đúng tốc độ, ramp, block, đích không). Chạy thẳng
    /// servo::ServoService trên servo::FakeA6sfPort, không có luồng monitor (như bản C#).
    /// </summary>
    void PanasonicServiceTests()
    {
        auto port = std::make_shared<servo::FakeA6sfPort>();
        servo::ServoService service;
        std::string lastLog;
        service.OnLog += [&](const std::string& s) { lastLog = s; };

        // Đúng bảng block máy này: block ABS đọc ô tốc độ 0, Jog đọc ô 1, cả hai chung ô acc/dec 0.
        // Ramp 300 ms ở 1800 rpm = Z1 đang cài trên máy (Auto cao 300 mm/s, 10 mm/vòng).
        auto ax = std::make_shared<servo::ServoAxisConfig>();
        ax->Name = "Z1"; ax->UnitId = 1; ax->AbsoluteVelocityIndex = 0; ax->VelocityIndex = 1;
        ax->AbsoluteAccelMs = 300; ax->AbsoluteDecelMs = 300; ax->AbsoluteRampRpm = 1800;
        auto cfg = std::make_shared<servo::ServoConfig>();
        cfg->Axes.push_back(ax);
        CHECK(service.Connect(cfg, port));
        auto st = service.GetAxisState(1);
        CHECK(st != nullptr);
        if (!st) return;
        st->SON = true;                 // CanMove
        st->ActualPosition = -999999;   // xa mọi đích — MoveAbsolute không thoát sớm vì "đã ở đó"

        const uint16_t Vel0 = 0x4600, Vel1 = 0x4601, Acc0 = 0x4610, Dec0 = 0x4620, Pos = 0x4806, Blk = 0x4414;
        CHECK(A6::GetBlockVelocityAddress(0) == Vel0 && A6::GetBlockAccelAddress(0) == Acc0 && A6::GetBlockDecelAddress(0) == Dec0
              && A6::GetBlockDataAddress(1) == Pos && A6::BLOCK_NUMBER == Blk && servo::FakeA6sfPort::Stb == A6::STB);

        // Chạy một lệnh (chặn) rồi trả về các giao dịch TRƯỚC khi STB bật.
        auto run = [&](const std::function<void()>& cmd)
        {
            size_t from = port->TransactionCount();
            cmd();
            std::vector<servo::FakeA6sfPort::Tx> before;
            for (const auto& t : port->TransactionsSince(from))
            {
                if (t.Fc == 0x05 && t.Address == servo::FakeA6sfPort::Stb) break;
                before.push_back(t);
            }
            return before;
        };
        auto last = [&] { return port->LastStart(); };
        auto count = [](const std::vector<servo::FakeA6sfPort::Tx>& txs, const std::function<bool(const servo::FakeA6sfPort::Tx&)>& pred)
        {
            return (int)std::count_if(txs.begin(), txs.end(), pred);
        };

        // Z hạ hai chặng rồi nâng hai chặng: cao → thấp → thấp → cao, đúng nhịp máy chạy.
        auto tx = run([&] { service.MoveAbsolute(1, 44770, 1800); });
        auto s0 = last();
        CHECK_MSG(port->StartCount() == 1 && At(s0, Blk) == 1 && At(s0, Vel0) == 1800 && At(s0, Acc0) == 500
                  && At(s0, Dec0) == 500 && PosOf(s0, Pos) == 44770, lastLog);
        CHECK_MSG(tx.size() == 5, Show(tx));                                   // tốc độ, acc, dec, vị trí, số block
        CHECK_MSG(count(tx, [&](const auto& t) { return t.Fc == 0x10 && t.Address == Pos && t.Values.size() == 2; }) == 1
                  && count(tx, [&](const auto& t) { return t.Fc == 0x06 && (t.Address == Pos || t.Address == Pos + 1); }) == 0, Show(tx));

        tx = run([&] { service.MoveAbsolute(1, 55150, 36); });
        auto s1 = last();
        // Chặng chậm 36 rpm: ramp GIỮ 500 — cùng gia tốc chặng nhanh, không nhân lại theo tốc độ từng lệnh.
        CHECK_MSG(At(s1, Vel0) == 36 && At(s1, Acc0) == 500 && At(s1, Dec0) == 500 && At(s1, Blk) == 1 && PosOf(s1, Pos) == 55150, lastLog);
        CHECK_MSG(tx.size() == 2 && count(tx, [&](const auto& t) { return t.Address == Vel0; }) == 1
                  && count(tx, [&](const auto& t) { return t.Address == Pos; }) == 1, Show(tx));

        tx = run([&] { service.MoveAbsolute(1, 44770, 36); });
        CHECK_MSG(tx.size() == 1 && tx[0].Fc == 0x10, Show(tx));               // cùng tốc độ: chỉ còn vị trí
        CHECK(At(last(), Blk) == 1 && At(last(), Vel0) == 36 && PosOf(last(), Pos) == 44770);

        // Jog ghi ramp của nó vào CHÍNH ô acc/dec 0 mà block ABS đọc.
        run([&] { service.JogPositive(1, 60); });
        auto sj = last();
        CHECK_MSG(At(sj, Blk) == 2 && At(sj, Acc0) == 100 && At(sj, Dec0) == 100 && At(sj, Vel1) == 60, lastLog);
        service.DecelerateStop(1);
        tx = run([&] { service.MoveAbsolute(1, 20000, 36); });
        auto s2 = last();
        // Sau Jog, lệnh ABS ghi lại ramp ABS và số block — không chạy nhầm ramp của Jog…
        CHECK_MSG(At(s2, Acc0) == 500 && At(s2, Dec0) == 500 && At(s2, Blk) == 1 && At(s2, Vel0) == 36 && PosOf(s2, Pos) == 20000, Show(tx));
        // …mà không ghi lại ô tốc độ ABS Jog không đụng tới — 4 giao dịch.
        CHECK_MSG(tx.size() == 4 && count(tx, [&](const auto& t) { return t.Address == Vel0; }) == 0, Show(tx));

        // Driver bật lại nguồn: bảng block trong RAM về 0, App phải về gốc lại.
        port->PowerCycle();
        run([&] { service.StartHome(1); });
        CHECK(At(last(), Blk) == 0);
        tx = run([&] { service.MoveAbsolute(1, 30000, 36); });
        auto s3 = last();
        CHECK_MSG(At(s3, Vel0) == 36 && At(s3, Acc0) == 500 && At(s3, Dec0) == 500 && At(s3, Blk) == 1 && PosOf(s3, Pos) == 30000, Show(tx));

        size_t starts = port->StartCount();
        port->FailNext = 1;
        run([&] { service.MoveAbsolute(1, 40000, 1800); });
        // Driver trả lỗi giữa lệnh: không kích STB, có báo lỗi.
        CHECK_MSG(port->StartCount() == starts && lastLog.find("MoveAbsolute error") != std::string::npos, lastLog);
        tx = run([&] { service.MoveAbsolute(1, 40000, 1800); });
        auto s4 = last();
        // Lệnh sau lỗi ghi lại đủ 5 thứ và chạy đúng — không tin những gì tưởng driver đang giữ.
        CHECK_MSG(tx.size() == 5 && At(s4, Vel0) == 1800 && At(s4, Acc0) == 500 && At(s4, Blk) == 1 && PosOf(s4, Pos) == 40000, Show(tx));

        run([&] { service.MoveAbsolute(1, -1234, 1800); });
        CHECK_MSG(PosOf(last(), Pos) == -1234, std::to_string(PosOf(last(), Pos)));   // vị trí âm nguyên vẹn qua hai word

        // Dự án khác dùng chung ServoControl để AbsoluteRampRpm = 0: thanh ghi ramp phải y như trước.
        ax->AbsoluteRampRpm = 0;
        run([&] { service.MoveAbsolute(1, 10000, 1800); });
        CHECK(At(last(), Acc0) == 500);
        tx = run([&] { service.MoveAbsolute(1, 11000, 36); });
        CHECK_MSG(At(last(), Acc0) == 10000 && At(last(), Dec0) == 10000, Show(tx));   // 300×3000/36 chặn ở 10000
        tx = run([&] { service.MoveAbsolute(1, 12000, 36); });
        CHECK_MSG(tx.size() == 1, Show(tx));

        // 15/10: Y1 đứng 46.470, tay #1 cần 46.520 — lệch ĐÚNG 50 xung = PositionTolerance. Nay thư viện chỉ bỏ qua trong
        // SkipMoveTolerance (App đặt 5 xung) — Tuấn: "nhận lệnh chính xác 0.01".
        st->ActualPosition = 46470;
        auto sends = [&](int p) { return service.MoveAbsolute(1, p, 36); };
        CHECK_MSG(!sends(46520) && sends(46521), lastLog);   // SkipMoveTolerance chưa đặt: vẫn bỏ qua trong 50 xung như cũ

        AxisConfig axCfg;
        axCfg.PulsePerUnit = 1000.0; axCfg.UnitPerRev = 10.0; axCfg.PositionTolerancePulse = 50;
        AxisScale scale(axCfg);
        cfg->SkipMoveTolerance = PanasonicAxis::LibrarySkipTolerance(axCfg);
        CHECK_MSG(cfg->SkipMoveTolerance == 5, std::to_string(cfg->SkipMoveTolerance));

        size_t startsBefore = port->StartCount();
        bool sentY1 = sends(scale.ToPulse(46.520));
        CHECK_MSG(sentY1 && port->StartCount() == startsBefore + 1 && PosOf(last(), Pos) == 46520, lastLog);
        CHECK(sends(scale.ToPulse(46.480)) && PosOf(last(), Pos) == 46480 && sends(scale.ToPulse(46.460)) && PosOf(last(), Pos) == 46460);
        CHECK(sends(46476));                                  // 0.006 mm (6 xung) vẫn xuống driver
        CHECK(!sends(46475) && !sends(46465) && !sends(46470));   // trong 5 xung là đã ở đích — không gửi gì

        // Thư viện bỏ lệnh ⇒ App phải thấy đã tới; ngược lại App sẽ gửi lại lệnh mà thư viện cứ bỏ → 2302 như 15/10.
        bool agree = true;
        std::string why;
        for (int d : { 0, 1, 4, 5, 6, 10, 49, 50, 51, -5, -6, -50, -51 })
        {
            bool skipped = !sends(46470 + d);
            bool there = PanasonicAxis::WithinPulses(46470, 46470 + d, axCfg.PositionTolerancePulse);
            if (skipped && !there) { agree = false; why += " Δ" + std::to_string(d); }
        }
        CHECK_MSG(agree, why);

        service.Dispose();
    }

    /// <summary>
    /// Ổ A6SF giả "có cơ" cho kiểm thử PanasonicAxis trọn đường: servo::FakeA6sfPort chỉ giữ coil/thanh ghi và trả lời
    /// Modbus; luồng này đóng vai phần cơ của driver — theo các giao dịch ghi (SRV-ON, STB, S-STOP, A-CLR) mà chạy block
    /// ABS / ORG / JOG đã chụp ở cạnh STB, rồi cập nhật coil trạng thái (S-RDY, ALM, BUSY, HOME-CMP, SRV-ON, NOT, POT) và
    /// vị trí 0x600F như driver thật. Mỗi nhịp 2 ms.
    /// </summary>
    class SimA6sfDrive
    {
    public:
        SimA6sfDrive(std::shared_ptr<servo::FakeA6sfPort> port, int startPulse)
            : m_port(std::move(port)), m_pos(startPulse)
        {
            Publish();
            m_thread = std::jthread([this](std::stop_token st) { while (!st.stop_requested()) { Tick(); base::Delay(2); } });
        }
        ~SimA6sfDrive() { m_thread.request_stop(); if (m_thread.joinable()) m_thread.join(); }
        SimA6sfDrive(const SimA6sfDrive&) = delete;
        SimA6sfDrive& operator=(const SimA6sfDrive&) = delete;

        /// <summary>Xung mỗi nhịp 2 ms (500 = 250 mm/s ở 1000 xung/mm).</summary>
        std::atomic<int> StepPerTick{ 500 };

        /// <summary>Số lệnh ABS kế tiếp driver "không nhận" dù STB đã kích (mất khung Modbus — 30/09 09:42).</summary>
        std::atomic<int> IgnoreAbs{ 0 };

        /// <summary>Vị trí cảm biến gốc (xung, hệ toạ độ cũ) — tới đó driver đặt lại gốc = 0 và bật HOME-CMP.</summary>
        std::atomic<int> HomeSensorPulse{ -3000 };

        void SetAlarm(uint16_t code) { std::lock_guard<std::mutex> l(m_mutex); m_alarm = code; if (code) m_mode = Mode::Idle; }
        void SetLimits(bool pot, bool neg) { std::lock_guard<std::mutex> l(m_mutex); m_pot = pot; m_not = neg; if (pot || neg) m_mode = Mode::Idle; }
        int Position() const { std::lock_guard<std::mutex> l(m_mutex); return m_pos; }
        bool Moving() const { std::lock_guard<std::mutex> l(m_mutex); return m_mode != Mode::Idle; }
        int AbsStarts() const { return m_absStarts.load(); }

    private:
        enum class Mode { Idle, Abs, Home, JogPos, JogNeg };

        void Tick()
        {
            std::vector<servo::FakeA6sfPort::Tx> txs;
            std::vector<Regs> starts;
            {
                std::lock_guard<std::mutex> l(m_port->Mutex);
                for (size_t i = m_txSeen; i < m_port->Transactions.size(); ++i) txs.push_back(m_port->Transactions[i]);
                m_txSeen = m_port->Transactions.size();
                for (size_t i = m_startSeen; i < m_port->Starts.size(); ++i) starts.push_back(m_port->Starts[i]);
                m_startSeen = m_port->Starts.size();
            }

            {
                std::lock_guard<std::mutex> l(m_mutex);
                size_t si = 0;
                for (const auto& t : txs)
                {
                    if (t.Fc != 0x05 || t.Values.empty()) continue;
                    bool on = t.Values[0] != 0;
                    if (t.Address == A6::STB)
                    {
                        // Cảnh STB lên — FakeA6sfPort chụp bảng thanh ghi đúng lúc này (Starts cùng thứ tự).
                        if (on && !m_stbWas && si < starts.size()) StartBlock(starts[si++]);
                        m_stbWas = on;
                    }
                    else if ((t.Address == A6::S_STOP || t.Address == A6::H_STOP) && on) m_mode = Mode::Idle;
                    else if (t.Address == A6::ALARM_CLEAR && on) m_alarm = 0;
                    else if (t.Address == A6::SON_ADDR) { m_son = on; if (!on) m_mode = Mode::Idle; }
                }

                int step = std::max(1, StepPerTick.load());
                switch (m_mode)
                {
                    case Mode::Abs:
                        if (std::abs(m_target - m_pos) <= step) { m_pos = m_target; m_mode = Mode::Idle; }
                        else m_pos += m_target > m_pos ? step : -step;
                        break;
                    case Mode::Home:
                    {
                        int sensor = HomeSensorPulse.load();
                        if (std::abs(sensor - m_pos) <= step) { m_pos = 0; m_orgCmp = true; m_mode = Mode::Idle; }
                        else m_pos += sensor > m_pos ? step : -step;
                        break;
                    }
                    case Mode::JogPos: m_pos += step; break;
                    case Mode::JogNeg: m_pos -= step; break;
                    case Mode::Idle: break;
                }
            }
            Publish();
        }

        /// <summary>Gọi khi giữ m_mutex.</summary>
        void StartBlock(const Regs& snap)
        {
            if (!m_son || m_alarm != 0) return;   // driver chưa sẵn sàng: bỏ qua STB
            int block = At(snap, A6::BLOCK_NUMBER);
            if (block == 1)
            {
                if (IgnoreAbs.load() > 0) { --IgnoreAbs; return; }
                m_target = PosOf(snap, A6::GetBlockDataAddress(1));
                m_mode = Mode::Abs;
                ++m_absStarts;
            }
            else if (block == 0) { m_orgCmp = false; m_mode = Mode::Home; }
            else if (block == 2) m_mode = Mode::JogPos;
            else if (block == 3) m_mode = Mode::JogNeg;
        }

        void Publish()
        {
            bool son, busy, org, pot, neg;
            uint16_t alarm;
            int pos;
            {
                std::lock_guard<std::mutex> l(m_mutex);
                son = m_son; busy = m_mode != Mode::Idle; org = m_orgCmp; pot = m_pot; neg = m_not; alarm = m_alarm; pos = m_pos;
            }
            std::lock_guard<std::mutex> l(m_port->Mutex);
            m_port->Coils[A6::SON_MON] = son;
            m_port->Coils[A6::NOT_MON] = neg;
            m_port->Coils[A6::POT_MON] = pot;
            m_port->Coils[A6::S_READY] = son && alarm == 0;
            m_port->Coils[A6::ALARM_OUTPUT] = alarm != 0;
            m_port->Coils[A6::INP] = !busy;
            m_port->Coils[A6::BUSY] = busy;
            m_port->Coils[A6::HOME_CMP] = org;
            m_port->Registers[A6::POSITION_ACTUAL] = (uint16_t)((uint32_t)pos & 0xFFFF);
            m_port->Registers[(uint16_t)(A6::POSITION_ACTUAL + 1)] = (uint16_t)((uint32_t)pos >> 16);
            m_port->Registers[A6::ERROR_CODE] = alarm;
        }

        std::shared_ptr<servo::FakeA6sfPort> m_port;
        mutable std::mutex m_mutex;          // mọi trường dưới đây
        Mode m_mode = Mode::Idle;
        int m_pos = 0;
        int m_target = 0;
        bool m_son = false;
        bool m_orgCmp = false;
        bool m_pot = false;
        bool m_not = false;
        bool m_stbWas = false;
        uint16_t m_alarm = 0;
        size_t m_txSeen = 0;                 // chỉ luồng Tick
        size_t m_startSeen = 0;
        std::atomic<int> m_absStarts{ 0 };
        std::jthread m_thread;               // khai cuối: dừng trước khi các trường trên bị huỷ
    };

    /// <summary>
    /// PanasonicAxis + servo::ServoService (luồng monitor thật) trên ổ A6SF giả — Connect (quyền Modbus, cấu hình
    /// thư viện), Refresh, servo, về gốc, Move Abs (ramp/gentle/Setting sửa sống), Inching, soft limit, gửi lại khi driver
    /// không nhận lệnh, Jog/Stop, huỷ, alarm driver, cữ cứng, timeout về gốc, mất/khôi phục kết nối, Disconnect.
    /// </summary>
    void PanasonicAxisTests()
    {
        AlarmManager::ForceClearAll();
        auto cfg = std::make_shared<AxisConfig>();
        cfg->Name = "Y1"; cfg->Kind = EAxisKind::Panasonic; cfg->ComPort = "COM-FAKE"; cfg->UnitId = 1;
        cfg->SoftLimitNeg = -1; cfg->SoftLimitPos = 400; cfg->MoveSpeed = 250; cfg->AccelTimeMs = 300; cfg->DecelTimeMs = 300;
        cfg->PositionTolerancePulse = 10; cfg->SkipMoveTolerancePulse = 5; cfg->MoveTimeoutMs = 5000; cfg->HomeTimeoutMs = 5000;
        AxisScale scale(cfg);
        auto code = [](AxisFault f) { return Code("Y1", f); };

        auto port = std::make_shared<servo::FakeA6sfPort>();
        SimA6sfDrive drive(port, 20000);                                   // trục đứng ở 20 mm lúc bật nguồn
        auto ax = std::make_shared<PanasonicAxis>(cfg, port);
        std::stop_source never;

        CHECK(!ax->IsConnected() && !ax->SetServoOn(true) && !ax->MoveAbs(10, 10, never.get_token()));
        ax->Refresh();                                                     // chưa mở: không làm gì
        CHECK(!ax->IsConnected());

        Ticker tick([ax] { ax->Refresh(); });
        CHECK(ax->Connect() && ax->IsConnected());
        CHECK_MSG(port->Reg(A6::MODBUS_RIGHTS) == A6::MODBUS_RIGHTS_ACQUIRE, std::to_string(port->Reg(A6::MODBUS_RIGHTS)));
        auto svc = ax->Service();
        auto sc = svc->Config();
        CHECK(sc && sc->ComPort == "COM-FAKE" && sc->Parity == servo::Parity::Even && sc->StopBits == servo::StopBits::One
              && sc->MonitorIntervalMs == 30 && sc->ResponseTimeoutMs == 100 && sc->PositionTolerance == 10 && sc->SkipMoveTolerance == 5);
        CHECK(sc && sc->Axes.size() == 1 && sc->Axes[0]->UnitId == 1 && sc->Axes[0]->AbsoluteVelocityIndex == 0
              && sc->Axes[0]->AbsoluteBlock == 1 && sc->Axes[0]->HomeBlock == 0 && sc->Axes[0]->AbsoluteAccelMs == 300
              && sc->Axes[0]->AbsoluteRampRpm == scale.ToRpm(250));
        CHECK_MSG(WaitUntil([&] { return Near(ax->CurrentPos(), 20.0); }, 1500), std::to_string(ax->CurrentPos()));
        CHECK(!ax->ServoOn() && !ax->OrgOk() && !ax->Alarm() && !ax->Busy());

        // ── Servo ON: chờ driver báo SRV-ON + S-RDY ──
        CHECK(ax->SetServoOn(true));
        CHECK(WaitUntil([&] { return ax->ServoOn(); }, 1000));

        // ── Về gốc: chờ HOME-CMP; vị trí lấy ngay sau HOME-CMP (driver đã đặt lại gốc), không đợi Refresh ──
        CHECK(ax->Home(never.get_token()) && ax->OrgOk());
        CHECK_MSG(Near(ax->CurrentPos(), 0.0), std::to_string(ax->CurrentPos()));
        CHECK(At(port->LastStart(), A6::BLOCK_NUMBER) == 0);

        // ── Move Abs: block ABS, ô tốc độ 0, ramp cố định theo mức Auto cao (300 ms @ 1500 rpm → 600) ──
        size_t s0 = port->StartCount();
        CHECK(ax->MoveAbs(50.0, 250, never.get_token()));
        CHECK_MSG(Near(ax->CurrentPos(), 50.0) && drive.Position() == 50000, std::to_string(ax->CurrentPos()));
        Regs s = port->LastStart();
        CHECK_MSG(port->StartCount() == s0 + 1 && At(s, A6::BLOCK_NUMBER) == 1 && At(s, A6::GetBlockVelocityAddress(0)) == 1500
                  && At(s, A6::GetBlockAccelAddress(0)) == 600 && At(s, A6::GetBlockDecelAddress(0)) == 600
                  && PosOf(s, A6::GetBlockDataAddress(1)) == 50000, std::to_string(At(s, A6::GetBlockAccelAddress(0))));
        // Lệnh chậm thường: cùng gia tốc (ramp 600); lệnh "gentle": ramp theo chính tốc độ lệnh (3000/150 × 300 = 6000).
        CHECK(ax->MoveAbs(52.0, 25, never.get_token()));
        CHECK(At(port->LastStart(), A6::GetBlockVelocityAddress(0)) == 150 && At(port->LastStart(), A6::GetBlockAccelAddress(0)) == 600);
        CHECK(ax->MoveAbs(54.0, 25, never.get_token(), true));
        CHECK(At(port->LastStart(), A6::GetBlockAccelAddress(0)) == 6000 && At(port->LastStart(), A6::GetBlockDecelAddress(0)) == 6000);
        // Sửa ở trang Setting có tác dụng ngay lệnh kế; số ≤ 0 rơi về 250 ms chứ không thành tăng tốc tức thời.
        cfg->AccelTimeMs = 600;
        CHECK(ax->MoveAbs(56.0, 250, never.get_token()) && At(port->LastStart(), A6::GetBlockAccelAddress(0)) == 1200);
        cfg->AccelTimeMs = 0;
        CHECK(ax->MoveAbs(58.0, 250, never.get_token()) && At(port->LastStart(), A6::GetBlockAccelAddress(0)) == 500);
        cfg->AccelTimeMs = 300;

        // ── Inching = Move Abs tới vị trí hiện tại + delta; 0.02 mm vẫn xuống driver, ≤ 5 xung thì thư viện bỏ (đã tới) ──
        double p = ax->CurrentPos();
        s0 = port->StartCount();
        CHECK(ax->MoveInc(0.02, 50, never.get_token()) && port->StartCount() == s0 + 1);
        CHECK_MSG(PosOf(port->LastStart(), A6::GetBlockDataAddress(1)) == scale.ToPulse(p + 0.02), std::to_string(PosOf(port->LastStart(), A6::GetBlockDataAddress(1))));
        s0 = port->StartCount();
        base::Stopwatch swSkip;
        CHECK(ax->MoveInc(0.003, 50, never.get_token()) && port->StartCount() == s0 && swSkip.ElapsedMilliseconds() < 500);

        // ── Soft limit: kiểm đích tuyệt đối, không gửi gì ──
        s0 = port->StartCount();
        CHECK(!ax->MoveAbs(700, 100, never.get_token()) && AlarmManager::IsActive(code(AxisFault::SoftLimit)));
        CHECK_MSG(DetailOf(code(AxisFault::SoftLimit)) == "Tgt 700.000mm outside [-1.000, 400.000]", DetailOf(code(AxisFault::SoftLimit)));
        CHECK(!ax->MoveInc(1000, 100, never.get_token()) && port->StartCount() == s0);

        // ── Driver không nhận lệnh một lần → App gửi lại và tới nơi; không nhận mãi → báo sau 3 lần gửi lại ──
        drive.IgnoreAbs = 1;
        s0 = port->StartCount();
        CHECK(ax->MoveAbs(60.0, 250, never.get_token()) && Near(ax->CurrentPos(), 60.0));
        CHECK_MSG(port->StartCount() == s0 + 2, std::to_string(port->StartCount() - s0));
        drive.IgnoreAbs = 100;
        s0 = port->StartCount();
        CHECK(!ax->MoveAbs(70.0, 250, never.get_token()) && AlarmManager::IsActive(code(AxisFault::MoveTimeout)));
        CHECK_MSG(DetailOf(code(AxisFault::MoveTimeout)).find("after 3 resends") != std::string::npos, DetailOf(code(AxisFault::MoveTimeout)));
        CHECK_MSG(port->StartCount() == s0 + 1 + PanasonicAxis::MaxMoveResends, std::to_string(port->StartCount() - s0));
        drive.IgnoreAbs = 0;
        AlarmManager::ForceClearAll();

        // ── Jog / Stop: bắn nền, không chặn luồng gọi ──
        p = ax->CurrentPos();
        base::Stopwatch swJog;
        ax->Jog(+1, 50);
        CHECK(swJog.ElapsedMilliseconds() < 50);
        CHECK(WaitUntil([&] { return ax->CurrentPos() > p + 1.0; }, 1500));
        ax->Jog(0, 0);
        CHECK(WaitUntil([&] { return !drive.Moving() && !ax->Busy(); }, 1500));
        double pj = ax->CurrentPos();
        base::Delay(100);
        CHECK(Near(ax->CurrentPos(), pj));
        ax->Jog(-1, 50);
        CHECK(WaitUntil([&] { return ax->CurrentPos() < pj - 1.0; }, 1500));
        ax->Stop();
        CHECK(WaitUntil([&] { return !drive.Moving() && !ax->Busy(); }, 1500));

        // ── Huỷ giữa lúc chạy: trả false (huỷ thấy ở đầu vòng chờ, trục được lệnh dừng) hoặc ném OperationCanceled
        //    (huỷ đúng lúc đang chờ 10 ms — y như C# ném OperationCanceledException) ──
        drive.StepPerTick = 20;                                             // 10 mm/s
        {
            std::stop_source cts;
            auto fut = base::RunAsync([&]
            {
                try { return ax->MoveAbs(300.0, 10, cts.get_token()) ? 1 : 0; }
                catch (const base::OperationCanceled&) { return 2; }
            });
            CHECK(WaitUntil([&] { return drive.Moving(); }, 1000));
            cts.request_stop();
            int r = fut.get();
            CHECK_MSG(r == 0 || r == 2, std::to_string(r));
            ax->Stop();
            CHECK(WaitUntil([&] { return !drive.Moving(); }, 1500) && ax->CurrentPos() < 290);
        }

        // ── Alarm driver giữa lúc chạy: +01 kèm mã, mất gốc; RESET xoá alarm, vẫn phải về gốc lại ──
        {
            auto fut = base::RunAsync([&] { return ax->MoveAbs(250.0, 10, never.get_token()); });
            CHECK(WaitUntil([&] { return drive.Moving(); }, 1000));
            drive.SetAlarm(0x1601);
            // Một lượt poll của thư viện đọc ALM (bước 1) TRƯỚC thanh ghi mã lỗi (bước 5) — WaitMotion có thể thấy ALM
            // khi mã lỗi còn là số cũ (cũng vậy ở bản C#). Mã đúng được kiểm ngay dưới qua ErrorCode().
            CHECK_MSG(!fut.get() && AlarmManager::IsActive(code(AxisFault::DriverAlarm))
                      && base::StartsWith(DetailOf(code(AxisFault::DriverAlarm)), "0x"), DetailOf(code(AxisFault::DriverAlarm)));
            CHECK(WaitUntil([&] { return ax->Alarm() && ax->ErrorCode() == 0x1601 && !ax->OrgOk(); }, 1000));
            CHECK(ax->ResetAlarm() && !ax->OrgOk());
            CHECK(WaitUntil([&] { return !ax->Alarm() && ax->ErrorCode() == 0; }, 1000));
            AlarmManager::ForceClearAll();
        }
        drive.StepPerTick = 500;
        CHECK(ax->Home(never.get_token()) && ax->OrgOk());

        // ── Tắt servo không mất gốc; chạy khi servo tắt: thư viện không gửi, App gửi lại rồi báo mất lệnh ──
        CHECK(ax->SetServoOn(false));
        CHECK(WaitUntil([&] { return !ax->ServoOn(); }, 1000) && ax->OrgOk());
        CHECK(!ax->MoveAbs(30.0, 250, never.get_token()) && AlarmManager::IsActive(code(AxisFault::MoveTimeout)));
        CHECK(ax->SetServoOn(true) && ax->OrgOk());
        AlarmManager::ForceClearAll();

        // ── Cữ cứng lúc chạy: +04 "POT"; Refresh báo FLS ──
        drive.StepPerTick = 20;
        {
            auto fut = base::RunAsync([&] { return ax->MoveAbs(200.0, 10, never.get_token()); });
            CHECK(WaitUntil([&] { return drive.Moving(); }, 1000));
            drive.SetLimits(true, false);
            CHECK(!fut.get() && AlarmManager::IsActive(code(AxisFault::HardLimit)) && DetailOf(code(AxisFault::HardLimit)) == "POT");
            CHECK(WaitUntil([&] { return ax->FLS() && !ax->RLS(); }, 1000));
            drive.SetLimits(false, false);
            CHECK(WaitUntil([&] { return !ax->FLS(); }, 1000));
            AlarmManager::ForceClearAll();
        }

        // ── Về gốc quá giờ: dừng trục, +05 "> N ms, no ORG_CMP", mất gốc ──
        drive.StepPerTick = 1;
        cfg->HomeTimeoutMs = 300;
        CHECK(!ax->Home(never.get_token()) && !ax->OrgOk() && AlarmManager::IsActive(code(AxisFault::HomeTimeout)));
        CHECK_MSG(DetailOf(code(AxisFault::HomeTimeout)) == "> 300 ms, no ORG_CMP", DetailOf(code(AxisFault::HomeTimeout)));
        CHECK(WaitUntil([&] { return !drive.Moving(); }, 1000));
        cfg->HomeTimeoutMs = 5000;
        drive.StepPerTick = 500;
        AlarmManager::ForceClearAll();

        // ── Mất kết nối chớp nhoáng ≠ mất kết nối: rơi gói liên tục → hạ cờ sau đủ FailLimit vòng; hết rơi → tự nối lại ──
        port->DropNext = 1000000;
        CHECK(WaitUntil([&] { return !ax->IsConnected(); }, 3000));
        port->DropNext = 0;
        CHECK(WaitUntil([&] { return ax->IsConnected(); }, 3000));

        // ── Disconnect: trả quyền Modbus, dừng monitor, đóng; mất gốc ──
        ax->Disconnect();
        CHECK(!ax->IsConnected() && !ax->OrgOk() && !svc->IsConnected());
        CHECK_MSG(port->Reg(A6::MODBUS_RIGHTS) == A6::MODBUS_RIGHTS_RELEASE, std::to_string(port->Reg(A6::MODBUS_RIGHTS)));
        base::Delay(30);
        CHECK(!ax->IsConnected());                                          // Refresh sau Disconnect không dựng lại cờ
        base::WaitBackgroundIdle(2000);
        AlarmManager::ForceClearAll();
    }
}

int RunMotionTests()
{
    testkit::Group g("Motion");

    fs::path root = fs::temp_directory_path() / ("sca_motion_" + base::NowString("yyyyMMdd_HHmmssfff"));
    AppPaths::Init(base::PathToU8(root));
    LogWorker::Instance().Init(AppPaths::Root());
    fs::path realAlarms = fs::path(__FILE__).parent_path().parent_path().parent_path() / "Config" / "alarms.json";
    if (!fs::exists(realAlarms)) realAlarms = fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "Config" / "alarms.json";
    fs::path exeDir = root / "exe";
    fs::create_directories(exeDir / "Config");
    std::string tplText;
    CHECK_MSG(base::ReadAllText(realAlarms, tplText), base::PathToU8(realAlarms));
    base::WriteAllText(exeDir / "Config" / "alarms.json", tplText);
    AppPaths::SetExeDirForTests(exeDir);
    AlarmCatalog::Load();
    McDataManager::LoadSystemData();
    CHECK(McDataManager::SysData()->Axes.size() == 7 && AppPaths::Root() == root);
    // Thư mục tạm mới nên system.json là bản mặc định — chế độ mô phỏng. Không bài nào được chạy khi khác đi.
    CHECK(McDataManager::SysData()->SimulationMode);
    if (!McDataManager::SysData()->SimulationMode) return g.Result();

    AxisScaleTests();
    AxisAngleTests();
    FakeAxisTests();
    FakeIoTests();
    MotionSimTests();
    HardwareSelectionTests();
    MoveCompletionRuleTests();
    PanasonicServiceTests();
    PanasonicAxisTests();

    LogWorker::Instance().Flush();
    return g.Result();
}
