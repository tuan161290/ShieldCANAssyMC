// Kiểm thử lớp bọc EziMOTIONPlusE.dll (Core/Fastech). TUYỆT ĐỐI KHÔNG gọi FAS_* lên DLL thật — máy này nối sẵn
// Ezi-SERVO / Ezi-IO thật qua Ethernet:
//   (1) thiếu DLL: Load từ thư mục không tồn tại → IsLoaded false, mọi wrapper trả mã hỏng, không crash;
//   (2) DLL thật <repo>\Lib\EziMOTIONPlusE.dll: CHỈ nạp + kiểm đủ 151 entry point theo tên, rồi Unload;
//   (3) EziAxisStatus, FastechBoardId, hằng số, lớp đệm byte (đối chiếu layout với header gốc của Fastech), IPAddress.
#include "TestKit.h"

#include "Base/Text.h"
#include "Fastech/EziAxisStatus.h"
#include "Fastech/EziMOTIONPlusELib.h"
#include "Fastech/FastechBoardId.h"
#include "Lang/Loc.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <limits>
#include <set>
#include <thread>
#include <vector>

using namespace sca;
namespace fs = std::filesystem;
using Ezi = EziMOTIONPlusELib;

namespace
{
    // ── Bản sao layout của header gốc Fastech (MOTION_DEFINE.h, SDK Ezi-MOTION Plus-E V6, Include_x64) ──
    // Để đối chiếu offset của các lớp đệm byte bản C# (offset viết tay trong property) với struct thật của DLL.
#pragma pack(push, 2)
    struct VendorItemNode
    {
        int32_t lPosition;
        uint32_t dwStartSpd;
        uint32_t dwMoveSpd;
        uint16_t wAccelRate;
        uint16_t wDecelRate;
        uint16_t wCommand;
        uint16_t wWaitTime;
        uint16_t wContinuous;
        uint16_t wBranch;
        uint16_t wCond_branch0;
        uint16_t wCond_branch1;
        uint16_t wCond_branch2;
        uint16_t wLoopCount;
        uint16_t wBranchAfterLoop;
        uint16_t wPTSet;
        uint16_t wLoopCountCLR;
        uint16_t bCheckInpos;
        int32_t lTriggerPos;
        uint16_t wTriggerOnTime;
        uint16_t wPushRatio;
        uint32_t dwPushSpeed;
        int32_t lPushPosition;
        uint16_t wPushMode;
    };

    struct VendorTriggerInfo
    {
        uint16_t wPeriod;
        uint16_t wReserved1;
        uint16_t wOnTime;
        uint16_t wReserved2;
        uint32_t wCount;
    };
#pragma pack(pop)

#pragma pack(push, 1)
    struct VendorDriveInfo
    {
        uint16_t nVersionNo[4];
        char sVersion[30];
        uint16_t nDriveType;
        uint16_t nMotorType;
        char sMotorInfo[20];
        uint16_t nInPinNo;
        uint16_t nOutPinNo;
        uint16_t nPTNum;
        uint16_t nFirmwareType;
    };

    struct VendorMotionOptionEx
    {
        uint32_t dwOptionFlag;
        uint16_t wCustomAccelTime;
        uint16_t wCustomDecelTime;
        uint8_t buffReserved[24];
    };

    struct VendorVelocityOptionEx
    {
        uint32_t dwOptionFlag;
        uint16_t wCustomAccDecTime;
        uint8_t buffReserved[26];
    };

    struct VendorAdChannel
    {
        char range;
        int16_t rawdata;
        float converted;
    };

    struct VendorAlarmLog
    {
        uint8_t nAlarmCount;
        uint8_t nAlarmLog[30];
    };
#pragma pack(pop)

    // Hằng OFFSET_* của bản C# = offset thật trong ITEM_NODE (union với WORD wBuffer[32] → 64 byte = BUFF_SIZE).
    static_assert(offsetof(VendorItemNode, lPosition) == Ezi::OFFSET_POSITION);
    static_assert(offsetof(VendorItemNode, dwStartSpd) == Ezi::OFFSET_LOWSPEED);
    static_assert(offsetof(VendorItemNode, dwMoveSpd) == Ezi::OFFSET_HIGHSPEED);
    static_assert(offsetof(VendorItemNode, wAccelRate) == Ezi::OFFSET_ACCELTIME);
    static_assert(offsetof(VendorItemNode, wDecelRate) == Ezi::OFFSET_DECELTIME);
    static_assert(offsetof(VendorItemNode, wCommand) == Ezi::OFFSET_COMMAND);
    static_assert(offsetof(VendorItemNode, wWaitTime) == Ezi::OFFSET_WAITTIME);
    static_assert(offsetof(VendorItemNode, wContinuous) == Ezi::OFFSET_CONTINUOUS);
    static_assert(offsetof(VendorItemNode, wBranch) == Ezi::OFFSET_JUMPTABLENO);
    static_assert(offsetof(VendorItemNode, wCond_branch0) == Ezi::OFFSET_JUMPPT0);
    static_assert(offsetof(VendorItemNode, wCond_branch1) == Ezi::OFFSET_JUMPPT1);
    static_assert(offsetof(VendorItemNode, wCond_branch2) == Ezi::OFFSET_JUMPPT2);
    static_assert(offsetof(VendorItemNode, wLoopCount) == Ezi::OFFSET_LOOPCOUNT);
    static_assert(offsetof(VendorItemNode, wBranchAfterLoop) == Ezi::OFFSET_LOOPJUMPTABLENO);
    static_assert(offsetof(VendorItemNode, wPTSet) == Ezi::OFFSET_PTSET);
    static_assert(offsetof(VendorItemNode, wLoopCountCLR) == Ezi::OFFSET_LOOPCOUNTCLEAR);
    static_assert(offsetof(VendorItemNode, bCheckInpos) == Ezi::OFFSET_CHECKINPOSITION);
    static_assert(offsetof(VendorItemNode, lTriggerPos) == Ezi::OFFSET_TRIGGERPOSITION);
    static_assert(offsetof(VendorItemNode, wTriggerOnTime) == Ezi::OFFSET_TRIGGERONTIME);
    static_assert(offsetof(VendorItemNode, wPushRatio) == Ezi::OFFSET_PUSHRATIO);
    static_assert(offsetof(VendorItemNode, dwPushSpeed) == Ezi::OFFSET_PUSHSPEED);
    static_assert(offsetof(VendorItemNode, lPushPosition) == Ezi::OFFSET_PUSHPOSITION);
    static_assert(offsetof(VendorItemNode, wPushMode) == Ezi::OFFSET_PUSHMODE);
    static_assert(sizeof(VendorItemNode) == Ezi::OFFSET_BLANK && Ezi::ITEM_NODE::BUFF_SIZE == 64);
    static_assert(sizeof(VendorDriveInfo) == Ezi::DRIVE_INFO::BUFF_SIZE);
    static_assert(offsetof(VendorDriveInfo, sVersion) == 8 && offsetof(VendorDriveInfo, nDriveType) == 38
                  && offsetof(VendorDriveInfo, sMotorInfo) == 42 && offsetof(VendorDriveInfo, nFirmwareType) == 68);
    static_assert(sizeof(VendorTriggerInfo) == Ezi::TRIGGER_INFO::BUFF_SIZE);
    static_assert(sizeof(VendorMotionOptionEx) == Ezi::MOTION_OPTION_EX::BUFF_SIZE);
    static_assert(sizeof(VendorVelocityOptionEx) == Ezi::VELOCITY_OPTION_EX::BUFF_SIZE);
    static_assert(sizeof(VendorAdChannel) == 7 && sizeof(VendorAdChannel) * 16 == Ezi::AD_RESULT::BUFF_SIZE);
    static_assert(sizeof(VendorAlarmLog) == Ezi::ALARM_LOG::BUFF_SIZE);
    static_assert(EziAxisStatus::Has(EziAxisStatus::ServoOn, EziAxisStatus::ServoOn));

    template <class T>
    std::array<uint8_t, sizeof(T)> BytesOf(const T& value)
    {
        std::array<uint8_t, sizeof(T)> b{};
        std::memcpy(b.data(), &value, sizeof(T));
        return b;
    }

    /// iBdID cho các lời gọi lúc CHƯA nạp DLL — không trùng board nào của máy (0, 1, 10, 11).
    constexpr int32_t TestBd = 99;

    /// (1) Mọi wrapper khi DLL chưa nạp: trả mã hỏng, không đụng tham số out, không crash.
    void CheckWrappersWhenNotLoaded()
    {
        constexpr int32_t NL = Ezi::NotLoadedCode;
        const IPAddress loopback(127, 0, 0, 1);

        CHECK(!Ezi::FAS_Connect(loopback, TestBd));
        CHECK(!Ezi::FAS_ConnectTCP(loopback, TestBd));
        Ezi::FAS_Close(TestBd);   // void: không làm gì
        CHECK(Ezi::FAS_IsSlaveExist(TestBd) == 0);
        CHECK(Ezi::FAS_Reconnect(TestBd) == 0);
        CHECK(Ezi::FAS_SetAutoReconnect(1) == 0);
        Ezi::FAS_EnableLog(0);
        Ezi::FAS_SetLogLevel(Ezi::LOG_LEVEL::LOG_LEVEL_ALL);
        CHECK(Ezi::FAS_SetLogPath(std::string("C:\\sca_no_such_log_dir")) == 0);
        CHECK(Ezi::FAS_SetLogPath(L"C:\\sca_no_such_log_dir") == 0);
        CHECK(Ezi::FAS_PrintCustomLog(TestBd, Ezi::LOG_LEVEL::LOG_LEVEL_COMM, "test") == 0);

        IPAddress found(1, 2, 3, 4);
        CHECK(!Ezi::FAS_IsBdIDExist(TestBd, found) && found == IPAddress(0, 0, 0, 0));
        int32_t foundBd = -5;
        CHECK(!Ezi::FAS_IsIPAddressExist(loopback, foundBd) && foundBd == -5);

        // Đúng những lệnh FastechAxis / IoConnect dùng.
        uint32_t in = 7, out = 7, ax = 7;
        int32_t cmd = 7, act = 7, perr = 7, vel = 7;
        uint16_t item = 7;
        CHECK(Ezi::FAS_GetAllStatus(TestBd, in, out, ax, cmd, act, perr, vel, item) == Ezi::FMM_NOT_OPEN);
        CHECK(in == 7 && out == 7 && ax == 7 && cmd == 7 && act == 7 && perr == 7 && vel == 7 && item == 7);
        uint32_t st = 7;
        CHECK(Ezi::FAS_GetAxisStatus(TestBd, st) == NL && st == 7);
        int32_t pos = 7;
        CHECK(Ezi::FAS_GetActualPos(TestBd, pos) == NL && pos == 7);
        CHECK(Ezi::FAS_ServoEnable(TestBd, 1) == NL);
        CHECK(Ezi::FAS_ServoAlarmReset(TestBd) == NL);
        CHECK(Ezi::FAS_MoveOriginSingleAxis(TestBd) == NL);
        CHECK(Ezi::FAS_MoveSingleAxisAbsPos(TestBd, 1000, 500u) == NL);
        CHECK(Ezi::FAS_MoveSingleAxisIncPos(TestBd, 10, 500u) == NL);
        CHECK(Ezi::FAS_MoveVelocity(TestBd, 500u, Ezi::DIR_INC) == NL);
        CHECK(Ezi::FAS_MoveStop(TestBd) == NL);
        CHECK(Ezi::FAS_EmergencyStop(TestBd) == NL);
        uint32_t di = 7, latch = 7;
        CHECK(Ezi::FAS_GetInput(TestBd, di, latch) == NL && di == 7 && latch == 7);
        uint32_t dout = 7, dstat = 7;
        CHECK(Ezi::FAS_GetOutput(TestBd, dout, dstat) == NL && dout == 7 && dstat == 7);
        CHECK(Ezi::FAS_SetOutput(TestBd, 0x1u, ~0x1u) == NL);

        // Wrapper có đệm / chuỗi: hỏng thì không chép gì ra (như bản C# khi rc != 0).
        uint8_t type = 9;
        std::string text = "keep";
        CHECK(Ezi::FAS_GetSlaveInfo(TestBd, type, text) == NL && type == 9 && text == "keep");
        CHECK(Ezi::FAS_GetMotorInfo(TestBd, type, text) == NL && type == 9 && text == "keep");
        Ezi::DRIVE_INFO drive;
        CHECK(Ezi::FAS_GetSlaveInfoEx(TestBd, drive) == NL && drive.nDriveType() == 0);
        Ezi::ITEM_NODE node;
        node.lPosition(42);
        CHECK(Ezi::FAS_PosTableReadItem(TestBd, 0, node) == NL && node.lPosition() == 42);
        CHECK(Ezi::FAS_PosTableWriteItem(TestBd, 0, node) == NL);
        Ezi::MOTION_OPTION_EX mo;
        CHECK(Ezi::FAS_MoveSingleAxisAbsPosEx(TestBd, 0, 1u, mo) == NL && Ezi::FAS_MoveSingleAxisIncPosEx(TestBd, 0, 1u, mo) == NL);
        Ezi::VELOCITY_OPTION_EX vo;
        CHECK(Ezi::FAS_MoveVelocityEx(TestBd, 1u, Ezi::DIR_DEC, vo) == NL);
        Ezi::ALARM_LOG alarms;
        CHECK(Ezi::FAS_GetAlarmLogs(TestBd, alarms) == NL && Ezi::FAS_GetWarningLogs(TestBd, alarms) == NL && alarms.nAlarmCount() == 0);
        Ezi::TRIGGER_INFO trig;
        CHECK(Ezi::FAS_SetTrigger(TestBd, 0, trig) == NL);
        Ezi::AD_BUFFER adb;
        CHECK(Ezi::FAS_ReadADAllValue(TestBd, 0, adb) == NL && adb.Channel()[0] == 0);
        Ezi::AD_RESULT adr;
        CHECK(Ezi::FAS_GetAllADResult(TestBd, adr) == NL && adr.RawData()[0] == 0);
        CHECK(Ezi::FAS_SetADRange(TestBd, 0, Ezi::AD_RANGE::ADRANGE_0_to_10) == NL);
        int32_t recv = 7;
        CHECK(Ezi::FAS_SetDACConfig(TestBd, 0, Ezi::DAC_CONFIG::DAC_RANGE, 0, recv) == NL && recv == 7);
        CHECK(Ezi::FAS_CounterCommand(TestBd, 0, Ezi::COUNTER_CMD::CNTCMD_RESET_ALL, 0) == NL);
        CHECK(Ezi::FAS_SetCounterTrigger(TestBd, 0, true, 0, 1u, 1u, 1u) == NL);
        uint32_t count = 7;
        CHECK(Ezi::FAS_GetLatchCount(TestBd, 3u, count) == NL && Ezi::FAS_GetTriggerCount(TestBd, 3u, count) == NL && count == 7);
        float speed = 1.5f;
        CHECK(Ezi::FAS_SPEEDGetGearSpeed(TestBd, speed) == NL && speed == 1.5f);
        int32_t ids[2] = { TestBd, TestBd + 1 };
        int32_t target[2] = { 0, 0 };
        CHECK(Ezi::FAS_MoveLinearAbsPos(2, ids, target, 100u, 50) == NL);
        std::array<uint8_t, Ezi::ALLSTATUSEX_ITEM_COUNT> types{};
        std::array<int32_t, Ezi::ALLSTATUSEX_ITEM_COUNT> datas{};
        CHECK(Ezi::FAS_GetAllStatusEx(TestBd, types.data(), datas.data()) == NL);
    }
}

int RunFastechBindingTests()
{
    testkit::Group g("FastechBinding");

    // ══════════ (1) Thiếu DLL ══════════
    Ezi::Unload();   // trạng thái sạch — không nhóm kiểm thử nào khác được nạp DLL Fastech
    CHECK(!Ezi::IsLoaded() && Ezi::LoadedPath().empty() && Ezi::LoadError().empty() && Ezi::MissingSymbols().empty());

    std::error_code ec;
    const fs::path nowhere = fs::temp_directory_path() / "sca_fastech_no_such_dir_9c41" / Ezi::DllFileName;
    CHECK(!fs::exists(nowhere.parent_path(), ec));
    std::string err = "x";
    CHECK(!Ezi::Load(nowhere, err));
    CHECK_MSG(!err.empty() && err == Ezi::LoadError() && err.find(Ezi::DllFileName) != std::string::npos, err);
    CHECK(Ezi::LoadWin32Error() != 0 && !Ezi::IsLoaded() && !Ezi::HasSymbol("FAS_Connect"));
    CHECK(!Ezi::Load(nowhere, err) && !Ezi::IsLoaded());   // chưa nạp được thì lần sau thử lại, vẫn hỏng

    if (!Ezi::IsLoaded())   // chặn kép: chỉ gọi wrapper khi CHẮC CHẮN chưa có DLL
        CheckWrappersWhenNotLoaded();
    CHECK(!Ezi::IsLoaded());

    {
        // Nhiều luồng cùng Load (hỏng) và gọi wrapper: không crash, không lẫn trạng thái.
        std::vector<std::thread> threads;
        std::array<int, 4> fails{};
        for (int t = 0; t < 4; ++t)
            threads.emplace_back([&, t] {
                for (int i = 0; i < 25; ++i)
                {
                    std::string e;
                    uint32_t s = 0;
                    if (!Ezi::Load(nowhere, e) && Ezi::FAS_GetAxisStatus(TestBd, s) == Ezi::NotLoadedCode) ++fails[t];
                }
            });
        for (auto& th : threads) th.join();
        CHECK(fails[0] == 25 && fails[1] == 25 && fails[2] == 25 && fails[3] == 25 && !Ezi::IsLoaded());
    }

    // ══════════ Danh sách entry point ══════════
    const auto& names = Ezi::SymbolNames();
    CHECK(names.size() == 151 && names.front() == "FAS_Connect" && names.back() == "FAS_SPEEDSaveOperationData");
    {
        std::set<std::string_view> unique(names.begin(), names.end());
        CHECK(unique.size() == names.size());
        CHECK(std::all_of(names.begin(), names.end(), [](std::string_view n) { return n.substr(0, 4) == "FAS_"; }));
        CHECK(unique.count("FAS_GetAllStatus") && unique.count("FAS_GetInput") && unique.count("FAS_SetOutput") && unique.count("FAS_GetOutput")
              && unique.count("FAS_IsSlaveExist") && unique.count("FAS_MoveSingleAxisIncPos") && unique.count("FAS_TriggerOutput_RunA"));
    }

    // ══════════ (2) DLL thật: CHỈ nạp và resolve tên — không gọi bất kỳ FAS_* nào ══════════
    const fs::path repo = fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
    const fs::path real = repo / "Lib" / Ezi::DllFileName;
    CHECK_MSG(fs::is_regular_file(real, ec), base::PathToU8(real));
    if (fs::is_regular_file(real, ec))
    {
        CHECK_MSG(Ezi::Load(real, err), err);
        CHECK(err.empty() && Ezi::IsLoaded() && Ezi::LoadError().empty() && Ezi::LoadWin32Error() == 0);
        CHECK_MSG(fs::equivalent(Ezi::LoadedPath(), real, ec), base::PathToU8(Ezi::LoadedPath()));

        std::string missingText;
        for (const std::string& m : Ezi::MissingSymbols()) missingText += m + " ";
        CHECK_MSG(missingText.empty(), "missing: " + missingText);
        int resolved = 0;
        for (std::string_view name : names)
        {
            bool ok = Ezi::HasSymbol(name);
            resolved += ok ? 1 : 0;
            CHECK_MSG(ok, std::string(name));
        }
        CHECK(resolved == 151 && !Ezi::HasSymbol("FAS_NoSuchFunction") && !Ezi::HasSymbol("FAS_ConnectEx"));

        // Đã nạp: Load nào cũng trả true ngay, không nạp lại, không đổi file.
        std::string again = "x";
        CHECK(Ezi::Load(nowhere, again) && again.empty() && Ezi::Load(again) && again.empty());
        CHECK(fs::equivalent(Ezi::LoadedPath(), real, ec));

        Ezi::Unload();
        CHECK(!Ezi::IsLoaded() && Ezi::LoadedPath().empty() && !Ezi::HasSymbol("FAS_Connect"));
    }
    CHECK(!Ezi::IsLoaded());

    // ══════════ (3a) EziAxisStatus ══════════
    using S = EziAxisStatus;
    CHECK(S::Describe(0) == Loc::T("Axis.NoStatusBits") && S::Describe(0) == "(no bits)");
    CHECK(S::Describe(S::ServoOn | S::InPosition) == "IN_POSITION SERVO_ON");
    CHECK(S::Describe(S::Motioning | S::OriginReturnOk) == "ORG_OK MOTIONING");
    CHECK(S::Describe(S::HwNegLimit) == "HW_LIMIT−" && S::Describe(S::HwPosLimit | S::SwNegLimit) == "HW_LIMIT+ SW_LIMIT−");
    CHECK(S::Describe(0x20u) == " +0x00000020");   // bit lạ: hiện ra, kể cả khi không có tên nào (khoảng trắng đầu như C#)
    CHECK(S::Describe(S::ErrorAll | 0x20u) == "ERR_ALL +0x00000020");
    {
        std::string all = S::Describe(0xFFFFFFFFu);
        CHECK_MSG(all.starts_with("ERR_ALL HW_LIMIT+ HW_LIMIT− SW_LIMIT+ SW_LIMIT− POS_OVERFLOW OVER_CURRENT")
                  && all.ends_with("ACCEL DECEL CONST MOTIONING +0x00000020")
                  && std::count(all.begin(), all.end(), ' ') == 31, all);
    }
    CHECK(S::AnyErrorMask == 0x0000BFC1u && S::BusyMask == 0xF0040000u);
    CHECK(S::Has(S::AnyErrorMask, S::SystemErr) && S::Has(S::AnyErrorMask, S::ErrorAll) && !S::Has(S::AnyErrorMask, S::InPositionErr)
          && !S::Has(S::AnyErrorMask, S::HwPosLimit) && !S::Has(S::AnyErrorMask, S::SwNegLimit));
    {
        // Như FastechAxis.Refresh giải một giá trị thô.
        uint32_t raw = S::ServoOn | S::MotionAccel | S::HwPosLimit;
        CHECK(S::Has(raw, S::ServoOn) && (raw & S::BusyMask) != 0 && (raw & S::AnyErrorMask) == 0
              && S::Has(raw, S::HwPosLimit) && !S::Has(raw, S::HwNegLimit) && !S::Has(raw, S::OriginReturnOk));
        uint32_t homed = S::ServoOn | S::OriginReturnOk | S::InPosition;
        CHECK(S::Has(homed, S::OriginReturnOk) && !S::Has(homed, S::OriginReturning) && (homed & S::BusyMask) == 0);
        uint32_t fault = S::ErrorAll | S::OverLoad;
        CHECK((int32_t)(fault & S::AnyErrorMask) == 0x401);
    }

    // ══════════ (3b) FastechBoardId, hằng số ══════════
    CHECK(FastechBoardId::AxisA1 == 0 && FastechBoardId::AxisA2 == 1 && FastechBoardId::Io32Di == 10 && FastechBoardId::Io32Do == 11);
    CHECK(TestBd != FastechBoardId::AxisA1 && TestBd != FastechBoardId::AxisA2 && TestBd != FastechBoardId::Io32Di && TestBd != FastechBoardId::Io32Do);
    CHECK(Ezi::NotLoadedCode == Ezi::FMM_NOT_OPEN && Ezi::FMM_NOT_OPEN == 1 && Ezi::FMM_OK == 0);
    CHECK(Ezi::FMC_DISCONNECTED == 5 && Ezi::FMP_FRAMETYPEERROR == 0x80 && Ezi::FMP_RUNFAIL == 0x85 && Ezi::FMP_ROMACCESS == 0x8B
          && Ezi::FMP_PACKETCRCERROR == 0xAA && Ezi::FMM_UNKNOWN_ERROR == 0xFF);
    CHECK(Ezi::DEVTYPE_EZI_SERVO2_PLUS_E_ST == 100 && Ezi::DEVTYPE_EZI_IO_PLUS_E_IN32 == 151 && Ezi::DEVTYPE_EZI_IO_PLUS_E_OUT32 == 161
          && std::string(Ezi::DEVNAME_EZI_IO_PLUS_E_OUT32) == "Ezi-IO Ethernet-OUT32");
    CHECK(Ezi::CMD_NO_COMMAND == 0xFFFF && Ezi::CMD_MAX_COUNT == 12 && Ezi::OFFSET_BLANK == 58 && Ezi::MAX_ALARM_LOG == 30
          && Ezi::STATUSEX_TYPE_ADVALUE6 == 33 && Ezi::PUSH_SPEED_MAX == 100000u);
    CHECK(std::string(Ezi::ALARM_DESCRIPTION[Ezi::ALARM_OVERCURRENT - 1]) == "Over Current"
          && std::string(Ezi::ALARM_DESCRIPTION[Ezi::ALARM_POSITIONCHANGED - 1]) == "Position Changed"
          && std::string(Ezi::ALARM_DESCRIPTION[12]).empty());
    CHECK(Ezi::CNTSTATUS_CH1_COMPARISON == 0x200u && Ezi::CNTSTATUS_CH2_ENABLED == 0x10000u && Ezi::CNTSTATUS_CH2_COMPARISON == 0x02000000u);
    CHECK((int)Ezi::AD_RANGE::ADRANGE_EX_0_to_10 == 19 && (int)Ezi::DA_RANGE::DARANGE_EX_4_to_20_C == 19
          && (int)Ezi::COUNTER_CONFIG::CFG_CP_LOGIC == 9 && (int)Ezi::LOG_LEVEL::LOG_LEVEL_ALL == 3
          && (int)Ezi::COUNTER_CMD::CNTCMD_RESET_LATCH == 6 && (int)Ezi::AD_DATA_TYPE::TYPE_AD_FILTER_OFFSET == 2);

    // ══════════ (3c) Lớp đệm byte — đối chiếu với struct gốc ══════════
    {
        Ezi::ITEM_NODE node;
        node.lPosition(-123456); node.dwStartSpd(1000); node.dwMoveSpd(50000); node.wAccelTime(100); node.wDecelTime(120);
        node.wCommand(Ezi::CMD_INC_NORMALMOTION); node.wWaitTime(5); node.wContinuous(1); node.wBranch(2);
        node.wCond_branch0(3); node.wCond_branch1(4); node.wCond_branch2(5); node.wLoopCount(6); node.wBranchAfterLoop(7);
        node.wPTSet(8); node.wLoopCountCLR(9); node.bCheckInpos(1); node.lTriggerPos(-77); node.wTriggerOnTime(10);
        node.wPushRatio(50); node.dwPushSpeed(2000); node.lPushPosition(-999); node.wPushMode(1);
        std::array<uint8_t, 64> raw{};
        node.copyto(raw);
        VendorItemNode v{};
        std::memcpy(&v, raw.data(), sizeof v);
        CHECK(v.lPosition == -123456 && v.dwStartSpd == 1000 && v.dwMoveSpd == 50000 && v.wAccelRate == 100 && v.wDecelRate == 120);
        CHECK(v.wCommand == 7 && v.wWaitTime == 5 && v.wContinuous == 1 && v.wBranch == 2 && v.wCond_branch0 == 3 && v.wCond_branch1 == 4
              && v.wCond_branch2 == 5 && v.wLoopCount == 6 && v.wBranchAfterLoop == 7 && v.wPTSet == 8 && v.wLoopCountCLR == 9);
        CHECK(v.bCheckInpos == 1 && v.lTriggerPos == -77 && v.wTriggerOnTime == 10 && v.wPushRatio == 50 && v.dwPushSpeed == 2000
              && v.lPushPosition == -999 && v.wPushMode == 1);
        CHECK(std::all_of(raw.begin() + 58, raw.end(), [](uint8_t b) { return b == 0; }));

        Ezi::ITEM_NODE back;
        back.copyfrom(raw);
        CHECK(back.lPushPosition() == -999 && back.wCommand() == 7 && back.dwMoveSpd() == 50000);
        back.Clear();
        CHECK(back.lPosition() == 0 && back.wPushMode() == 0 && back.wLoopCount() == 0 && back.wCommand() == 0xFFFF && back.wBranch() == 0xFFFF
              && back.wCond_branch0() == 0xFFFF && back.wCond_branch1() == 0xFFFF && back.wCond_branch2() == 0xFFFF
              && back.wBranchAfterLoop() == 0xFFFF && back.wLoopCountCLR() == 0xFFFF);

        std::array<uint8_t, 4> shortBuf{};   // mảng ngắn: chép min(len, BUFF_SIZE) như bản C#
        node.copyto(shortBuf);
        int32_t p = 0;
        std::memcpy(&p, shortBuf.data(), 4);
        CHECK(p == -123456);
    }
    {
        Ezi::MOTION_OPTION_EX mo;
        mo.BIT_USE_CUSTOMACCEL(true);
        CHECK(mo.dwOptionFlag() == 2);
        mo.BIT_IGNOREEXSTOP(true);
        mo.BIT_USE_CUSTOMDECEL(true);
        CHECK(mo.dwOptionFlag() == 7);
        mo.BIT_USE_CUSTOMACCEL(false);
        CHECK(mo.dwOptionFlag() == 5 && mo.BIT_IGNOREEXSTOP() && !mo.BIT_USE_CUSTOMACCEL() && mo.BIT_USE_CUSTOMDECEL());
        mo.wCustomAccelTime(300);
        mo.wCustomDecelTime(400);
        std::array<uint8_t, 32> raw{};
        mo.copyto(raw);
        VendorMotionOptionEx v{};
        std::memcpy(&v, raw.data(), sizeof v);
        CHECK(v.dwOptionFlag == 5 && v.wCustomAccelTime == 300 && v.wCustomDecelTime == 400);
        mo.Clear();
        CHECK(mo.dwOptionFlag() == 0 && mo.wCustomDecelTime() == 0);

        Ezi::VELOCITY_OPTION_EX vo;
        vo.BIT_USE_CUSTOMACCDEC(true);
        vo.wCustomAccDecTime(250);
        vo.copyto(raw);
        VendorVelocityOptionEx vv{};
        std::memcpy(&vv, raw.data(), sizeof vv);
        CHECK(vv.dwOptionFlag == 2 && vv.wCustomAccDecTime == 250 && vo.BIT_USE_CUSTOMACCDEC() && !vo.BIT_IGNOREEXSTOP());
    }
    {
        VendorAlarmLog va{};
        va.nAlarmCount = 3;
        va.nAlarmLog[0] = (uint8_t)Ezi::ALARM_OVERCURRENT;
        va.nAlarmLog[2] = (uint8_t)Ezi::ALARM_OVERLOAD;
        va.nAlarmLog[29] = (uint8_t)Ezi::ALARM_POSITIONCHANGED;
        Ezi::ALARM_LOG al;
        al.copy(BytesOf(va));
        CHECK(al.nAlarmCount() == 3 && al.nAlarmLog(0) == 1 && al.nAlarmLog(2) == 4 && al.nAlarmLog(29) == 16 && al.nAlarmLog(30) == 0);
        al.nAlarmCount(9);
        CHECK(al.nAlarmCount() == 3);   // setter của bản C# không ghi gì — giữ nguyên (TODO(port) ở header)
        al.Clear();
        CHECK(al.nAlarmCount() == 0 && al.nAlarmLog(29) == 0);
    }
    {
        VendorDriveInfo vd{};
        vd.nVersionNo[0] = 2; vd.nVersionNo[1] = 21; vd.nVersionNo[2] = 0; vd.nVersionNo[3] = 45;
        std::memcpy(vd.sVersion, "V2.21.0.45", 10);
        vd.nDriveType = 100; vd.nMotorType = 7;
        std::memcpy(vd.sMotorInfo, "EzM2-42M", 8);
        vd.nInPinNo = 12; vd.nOutPinNo = 10; vd.nPTNum = 256; vd.nFirmwareType = 3;
        Ezi::DRIVE_INFO d;
        d.copy(BytesOf(vd));
        CHECK(d.nVersionNo0() == 2 && d.nVersionNo1() == 21 && d.nVersionNo2() == 0 && d.nVersionNo3() == 45);
        CHECK_MSG(d.sVersion() == "V2.21.0.45" && d.sMotorInfo() == "EzM2-42M", d.sVersion() + " / " + d.sMotorInfo());
        CHECK(d.nDriveType() == 100 && d.nMotorType() == 7 && d.nInPinNo() == 12 && d.nOutPinNo() == 10 && d.nPTNum() == 256 && d.nFirmwareType() == 3);
        std::memset(vd.sVersion, 'A', 30);   // đầy 30 ký tự, không có '\0': đọc đủ 30, không tràn sang nDriveType
        d.copy(BytesOf(vd));
        CHECK(d.sVersion() == std::string(30, 'A') && d.nDriveType() == 100);
        d.Clear();
        CHECK(d.sVersion().empty() && d.sMotorInfo().empty() && d.nPTNum() == 0);
    }
    {
        Ezi::TRIGGER_INFO ti;
        ti.wPeriod(100); ti.wOnTime(10); ti.wCount(123456u);
        VendorTriggerInfo vt{};
        std::memcpy(&vt, ti.ByteArray().data(), sizeof vt);
        CHECK(vt.wPeriod == 100 && vt.wReserved1 == 0 && vt.wOnTime == 10 && vt.wReserved2 == 0 && vt.wCount == 123456u);
        Ezi::TRIGGER_INFO t2;
        t2.copy(ti.ByteArray());
        CHECK(t2.wCount() == 123456u && t2.wPeriod() == 100);
    }
    {
        VendorAdChannel ch[16] = {};
        for (int i = 0; i < 16; ++i)
        {
            ch[i].range = (char)i;
            ch[i].rawdata = (int16_t)(-100 * i);
            ch[i].converted = (float)i;
        }
        ch[3].converted = 2.75f;                                        // (short) cắt phần thập phân → 2
        ch[4].converted = -3.5f;                                        // → −3 (cắt về 0)
        ch[5].converted = std::numeric_limits<float>::quiet_NaN();      // .NET Framework x64 → 0
        ch[6].converted = 40000.0f;                                     // int32 40000 → 16 bit thấp = −25536
        std::array<uint8_t, 112> raw{};
        std::memcpy(raw.data(), ch, sizeof ch);
        Ezi::AD_RESULT r(raw);
        CHECK(r.MAX_CHANNEL_COUNT == 16 && r.Range()[3] == u'\x03' && r.Range()[15] == u'\x0F' && r.RawData()[3] == -300 && r.RawData()[15] == -1500);
        CHECK(r.Converted()[3] == 2.0f && r.Converted()[4] == -3.0f && r.Converted()[5] == 0.0f && r.Converted()[6] == -25536.0f
              && r.Converted()[15] == 15.0f);

        std::array<uint8_t, 16> raw16{};
        const int16_t chv[8] = { 1, -2, 3, -4, 5, -6, 7, -32768 };
        std::memcpy(raw16.data(), chv, sizeof chv);
        Ezi::AD_BUFFER b(raw16);
        CHECK(b.Channel()[0] == 1 && b.Channel()[1] == -2 && b.Channel()[7] == -32768);
        std::array<uint8_t, 15> raw15{};
        raw15.fill(1);
        Ezi::AD_BUFFER bad(raw15);   // sai độ dài → mọi kênh 0 (như bản C#)
        CHECK(bad.Channel()[0] == 0 && bad.Channel()[6] == 0);
    }

    // ══════════ (3d) IPAddress (IPAddress.TryParse của .NET Framework cho IPv4) ══════════
    {
        IPAddress ip;
        CHECK(IPAddress::TryParse("192.168.0.2", ip) && ip == IPAddress(192, 168, 0, 2) && ip.ToString() == "192.168.0.2");
        CHECK(ip.GetAddressBytes()[0] == 192 && ip.GetAddressBytes()[3] == 2);
        CHECK(IPAddress::TryParse("192.168.2", ip) && ip.ToString() == "192.168.0.2");
        CHECK(IPAddress::TryParse("192.11010050", ip) && ip.ToString() == "192.168.0.2");
        CHECK(IPAddress::TryParse("3232235522", ip) && ip.ToString() == "192.168.0.2");
        CHECK(IPAddress::TryParse("0xC0.0250.0.02", ip) && ip.ToString() == "192.168.0.2");
        CHECK(IPAddress::TryParse("0", ip) && ip.ToString() == "0.0.0.0");
        CHECK(IPAddress::TryParse("4294967295", ip) && ip.ToString() == "255.255.255.255");
        for (const char* bad : { "", "256.1.1.1", "1.2.3.256", "1.2.65536", "4294967296", "1.2.3.4.", "1..2.3", "1.2.3.4.5",
                                 " 1.2.3.4", "1.2.3.4 ", "08.1.1.1", "0x", "1.2.3.4/24", "::1", "abc", "192.168.0.x" })
            CHECK_MSG(!IPAddress::TryParse(bad, ip) && ip == IPAddress(), std::string("'") + bad + "'");
    }

    CHECK(!Ezi::IsLoaded());
    return g.Result();
}
