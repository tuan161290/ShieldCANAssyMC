// Kiểm thử tầng hạ tầng / dữ liệu của ShieldCanAssy: AppPaths, Loc::Format, IoMap/AlarmCode/AxisId/Enums,
// AxisAngle/AxisScale, SystemData (mặc định, migrate, JSON dạng C#), TrayLayout/TrayState, ModelData, RunningData,
// McDataManager, AlarmCatalog/AlarmManager, LogWorker, UiThread, SingleInstanceGuard, UserAccess.
//
// Port các phép kiểm mức dữ liệu của ShieldCanAssy.Wpf\SelfTest.cs (TestHardwareAssumptions, TestResetRules,
// TestEventAlarmsAlwaysResettable, TestTrayGridZigzag, TestTrayGridMath, TestModelManagement, TestAxisScale,
// TestRotaryWrap phần số học/migrate, TestZDecelPoint phần dữ liệu, TestSettleBeforeCapture phần dữ liệu,
// TestTwoInputTrays phần tính, TestAutoCycle phần cycle time, TestAssyTrayFull phần đồng hồ tray) cộng các phép kiểm
// tương thích JSON với file Newtonsoft của bản WPF.
//
// Mọi dữ liệu ghi vào một thư mục tạm mới — KHÔNG đụng D:\ShieldCanAssy hay _RunData của bản WPF. Các mẫu JSON dưới
// đây dựng theo tên property và giá trị mặc định của lớp C#, số liệu bịa.
#include "TestKit.h"

#include "Base/Async.h"
#include "Base/Text.h"
#include "Communication/AxisAngle.h"
#include "Communication/AxisScale.h"
#include "Communication/VmContract.h"
#include "Data/McDataManager.h"
#include "Fastech/FastechBoardId.h"
#include "Infor/AlarmCatalog.h"
#include "Infor/AlarmManager.h"
#include "Infor/IoMap.h"
#include "Infor/StartupProgress.h"
#include "Lang/Loc.h"
#include "SysConfig/AppPaths.h"
#include "SysConfig/AppUpdate.h"
#include "SysConfig/UserAccess.h"
#include "Utility/Log/LogWorker.h"
#include "Utility/SingleInstanceGuard.h"
#include "Utility/UiThread.h"
#include "nlohmann/json.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <thread>

using namespace sca;
namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

namespace
{
    /// <summary>Near() của SelfTest.cs: lệch dưới 0.02.</summary>
    bool Near(double a, double b) { return std::fabs(a - b) < 0.02; }

    std::string RawFile(const fs::path& p)
    {
        std::ifstream in(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    json ParseText(const std::string& text)
    {
        std::string t = text;
        if (base::StartsWith(t, "\xEF\xBB\xBF")) t.erase(0, 3);
        return json::parse(t);
    }

    std::vector<std::string> Keys(const json& j)
    {
        std::vector<std::string> k;
        if (!j.is_object()) return k;
        for (auto it = j.begin(); it != j.end(); ++it) k.push_back(it.key());
        return k;
    }

    /// <summary>Khoá và THỨ TỰ khoá phải đúng như Newtonsoft ghi từ lớp C# (thứ tự khai báo property).</summary>
    void CheckKeys(const json& j, const std::vector<std::string>& expect, const char* what)
    {
        std::vector<std::string> got = Keys(j);
        CHECK_MSG(got == expect, std::string(what) + ": got [" + base::Join(got, ",") + "]");
    }

    std::vector<int> Sorted(std::vector<int> v)
    {
        std::sort(v.begin(), v.end());
        return v;
    }

    // ── Khoá JSON của bản C# (Newtonsoft, NullValueHandling.Ignore) ──
    // Ngoại lệ chỉ bản WinUI: "ScanAssyByPair" (2026-10-28) — bản WPF đọc file bỏ qua khoá lạ.

    const std::vector<std::string> kSystemKeys = {
        "MachineName", "MachineCode", "SimulationMode", "Language", "ZSafe", "HasAbsoluteEncoder", "RequireHomeAfterEmg",
        "HasLightCurtain", "HasAirSensor", "HomeRotateSequential", "ControlTickMs", "WatchdogTimeoutMs",
        "WatchdogStartupGraceMs", "OnlyNozzle", "PickRetryCount", "PickRetryDeltaZ", "MaxConsecutiveNg", "ScanAssyByRow",
        "ScanAssyByPair", "VacuumTimeoutMs", "VacuumSettleMs", "BlowTimeMs", "InputZSlowDown", "InputZSlowUp", "AssyZSlowDown", "AssyZSlowUp",
        "MaxCompensationAngleDeg", "MaxCompensationDistance", "PlaceTargetGuardMm", "ACapTolDeg", "MaxGuardSkipStreak",
        "AlignXTolMm", "IoDiIpAddress", "IoDoIpAddress", "VisionSolutionPath", "VisionTriggerTimeoutMs", "VisionSettleMs",
        "TopSettleMs", "LampSettleMs", "TopLampMs", "VisionOutputs", "CalibValidDays", "TopDownCalibStepXMm",
        "TopDownCalibStepYMm", "TopDownCalibCenterX", "TopDownCalibCenterY", "TopDownCalibCenterSet",
        "CalibMaxResidualThresholdMm", "CalibShotsPerPoint", "TopDownVerifyStepMm", "AlignVerifyAngleDeg",
        "AlignVerifyAngleTolDeg", "AlignVerifyMinEccMm", "AlignVerifyMinTurnDeg", "AlignCalibXOffsetsMm",
        "AlignCalibAnglesDeg", "Axes" };

    const std::vector<std::string> kAxisKeys = {
        "Name", "Kind", "Unit", "ComPort", "BaudRate", "UnitId", "HomeBlock", "AbsoluteBlock", "JogPositiveBlock",
        "JogNegativeBlock", "AbsoluteVelocityIndex", "AccelTimeMs", "DecelTimeMs", "PositionTolerancePulse",
        "SkipMoveTolerancePulse", "IpAddress", "AxisIndex", "BoardId", "PulsePerUnit", "UnitPerRev", "MaxRpm", "SoftLimitPos",
        "SoftLimitNeg", "JogSpeedSlow", "JogSpeedFast", "MoveSpeed", "MoveSpeedMid", "MoveSpeedLow", "HomeDir",
        "HomeUseZPhase", "HomeTimeoutMs", "MoveTimeoutMs", "HomeOffset", "StandbyPos", "WrapDegrees" };

    const std::vector<std::string> kVisionOutputKeys = {
        "FlowLocateTop", "FlowAlignBottomTool1", "FlowAlignBottomTool2", "OutFound", "OutImage", "RenderTool1", "RenderTool2",
        "RenderTopDown", "GlobalVarModule", "GvAxisX", "GvAxisY2", "GvAxisA1", "GvAxisA2" };

    const std::vector<std::string> kModelKeysNoDescription = {
        "Name", "ModifiedAt", "InputTray", "AssyTray", "InputTray2", "InputTraysInUse", "UsableInputTrays", "Calib",
        "AlignPosX_Nozzle1", "AlignPosX_Nozzle2", "PickAngleA1", "PickAngleA2", "HandEyePick_Nozzle1", "HandEyePick_Nozzle2",
        "HandEyeCamPos" };

    /// <summary>Model đã có Z_SAFE và solution riêng (Tuấn 2026-11-07) — chỉ bản C++ ghi hai khoá này, bản WPF bỏ qua.</summary>
    const std::vector<std::string> kModelKeysWithMachine = {
        "Name", "ModifiedAt", "InputTray", "AssyTray", "InputTray2", "InputTraysInUse", "UsableInputTrays", "Calib", "ZSafe",
        "VisionSolutionPath", "AlignPosX_Nozzle1", "AlignPosX_Nozzle2", "PickAngleA1", "PickAngleA2", "HandEyePick_Nozzle1",
        "HandEyePick_Nozzle2", "HandEyeCamPos" };

    const std::vector<std::string> kTrayLayoutKeys = {
        "Name", "Rows", "Cols", "AxisRow", "AxisCol", "GeoZ1", "GeoZ2", "VerifyThreshold", "Z_PickZ1", "Z_PickZ2", "Z_SlowZ1",
        "Z_SlowZ2", "Z_Safe", "ScanOrder", "CamTopDownAnchorX", "CamTopDownAnchorY", "CamTopDownAnchorTaught", "CellCount",
        "HalfColPitch", "IsTaught" };

    const std::vector<std::string> kGeoKeysWithOffsets = {
        "P00", "P0C", "PR0", "PRC_Verify", "Offsets", "PlaceDxAll", "PlaceDyAll", "PlaceDaAll" };
    const std::vector<std::string> kGeoKeysNoOffsets = {
        "P00", "P0C", "PR0", "PRC_Verify", "PlaceDxAll", "PlaceDyAll", "PlaceDaAll" };

    const std::vector<std::string> kRunKeys = {
        "RecentModelName", "SavedAt", "AutoState", "UnsafePause", "NozzleClearRequired", "NozzleClearReason", "InputTray",
        "AssyTray", "Nozzle1", "Nozzle2", "CountOk", "CountNg", "CountTotal", "ConsecutiveNg", "LastCycleTimeSec",
        "InputCountOk", "InputCountNg", "AssyCountOk", "AssyCountNg", "LastProductTimeSec", "RecentProductTimes",
        "AvgProductTimeSec", "AssyTrayStartedAt", "AssyTrayDone", "AssyTrayDoneSec", "AssyTrayDoneProducts",
        "AssyTrayDonePerProductSec", "LastAssyTrayTimeSec", "LastAssyTrayProducts", "LastAssyTrayPerProductSec",
        "NgRatePercent" };

    const std::vector<std::string> kTrayStateKeys = {
        "Name", "Rows", "Cols", "Cells", "CurrentIndex", "ScanOrder", "CellCount", "Trays", "RowsPerTray", "CellsPerTray",
        "Selected", "HasSelection" };

    const std::vector<std::string> kNozzleKeys = { "Index", "HasProduct", "SourceCellIndex", "MeasuredAngle", "Aligned" };

    // ── Mẫu JSON dạng C# (Newtonsoft Formatting.Indented), số liệu bịa ──

    const char* kSystemJsonCs = R"JSON({
  "MachineName": "Shield CAN Assy MC",
  "MachineCode": "SO1C86-SCAM",
  "SimulationMode": true,
  "Language": "en",
  "ZSafe": 25.0,
  "HasAbsoluteEncoder": false,
  "RequireHomeAfterEmg": false,
  "HasLightCurtain": true,
  "HasAirSensor": false,
  "HomeRotateSequential": false,
  "ControlTickMs": 40,
  "WatchdogTimeoutMs": 4000,
  "WatchdogStartupGraceMs": 6000,
  "OnlyNozzle": 2,
  "PickRetryCount": 3,
  "PickRetryDeltaZ": 0.25,
  "MaxConsecutiveNg": 4,
  "ScanAssyByRow": true,
  "VacuumTimeoutMs": 333,
  "VacuumSettleMs": 111,
  "BlowTimeMs": 90,
  "InputZSlowDown": true,
  "InputZSlowUp": false,
  "AssyZSlowDown": false,
  "AssyZSlowUp": false,
  "MaxCompensationAngleDeg": 21.5,
  "MaxCompensationDistance": 2.5,
  "PlaceTargetGuardMm": 1.25,
  "ACapTolDeg": 0.4,
  "MaxGuardSkipStreak": 3,
  "AlignXTolMm": 0.06,
  "IoDiIpAddress": "192.168.0.20",
  "IoDoIpAddress": "192.168.0.21",
  "VisionSolutionPath": "C:\\Jobs\\Test.solw",
  "VisionTriggerTimeoutMs": 2500,
  "VisionSettleMs": 150,
  "TopSettleMs": 175,
  "LampSettleMs": 80,
  "TopLampMs": 60,
  "VisionOutputs": {
    "FlowLocateTop": "LocateTop",
    "FlowAlignBottomTool1": "T1",
    "FlowAlignBottomTool2": "T2",
    "OutFound": "Found",
    "OutImage": "ImageData",
    "RenderTool1": "T1",
    "RenderTool2": "T2.Module",
    "RenderTopDown": "",
    "GlobalVarModule": "",
    "GvAxisX": "X",
    "GvAxisY2": "MyY2",
    "GvAxisA1": "A1",
    "GvAxisA2": "A2_deg",
    "FlowTopDownCalib": "TopDownCalib"
  },
  "CalibValidDays": 60,
  "TopDownCalibStepXMm": 2.0,
  "TopDownCalibStepYMm": 3.0,
  "TopDownCalibCenterX": 300.125,
  "TopDownCalibCenterY": 50.5,
  "TopDownCalibCenterSet": true,
  "CalibMaxResidualThresholdMm": 0.04,
  "CalibShotsPerPoint": 3,
  "TopDownVerifyStepMm": 3.0,
  "AlignVerifyAngleDeg": 5.0,
  "AlignVerifyAngleTolDeg": 1.0,
  "AlignVerifyMinEccMm": 0.2,
  "AlignVerifyMinTurnDeg": 2.0,
  "AlignCalibXOffsetsMm": "-4, -3, -2, -1, 0",
  "AlignCalibAnglesDeg": "0, 5, 10",
  "Axes": [
    {
      "Name": "Z1",
      "Kind": 0,
      "Unit": "mm",
      "ComPort": "COM16",
      "BaudRate": 57600,
      "UnitId": 2,
      "HomeBlock": 0,
      "AbsoluteBlock": 1,
      "JogPositiveBlock": 2,
      "JogNegativeBlock": 3,
      "AbsoluteVelocityIndex": 0,
      "AccelTimeMs": 100,
      "DecelTimeMs": 120,
      "PositionTolerancePulse": 50,
      "SkipMoveTolerancePulse": 5,
      "IpAddress": "",
      "AxisIndex": 0,
      "BoardId": 0,
      "PulsePerUnit": 1000.0,
      "UnitPerRev": 10.0,
      "MaxRpm": 3000,
      "SoftLimitPos": 95.0,
      "SoftLimitNeg": -10.0,
      "JogSpeedSlow": 1.0,
      "JogSpeedFast": 10.0,
      "MoveSpeed": 1200.0,
      "MoveSpeedMid": 50.0,
      "MoveSpeedLow": 10.0,
      "HomeDir": -1,
      "HomeUseZPhase": true,
      "HomeTimeoutMs": 60000,
      "MoveTimeoutMs": 30000,
      "HomeOffset": 0.0,
      "StandbyPos": 0.0,
      "WrapDegrees": false,
      "SomeFutureField": 7
    },
    {
      "Name": "X",
      "Kind": 0,
      "Unit": "mm",
      "ComPort": "COM13",
      "MoveSpeed": 300.0,
      "MoveSpeedMid": 0.0,
      "SoftLimitPos": 680.0,
      "SoftLimitNeg": -20.0,
      "StandbyPos": 291.5
    },
    {
      "Name": "A2",
      "Kind": "Fastech",
      "Unit": "deg",
      "ComPort": "",
      "IpAddress": "192.168.0.3",
      "BoardId": 0,
      "PulsePerUnit": 100.0,
      "UnitPerRev": 180.0,
      "SoftLimitPos": 360.0,
      "SoftLimitNeg": 0.0,
      "HomeOffset": -114.5,
      "WrapDegrees": true
    },
    { "Name": "BOGUS", "Kind": 0 },
    null
  ],
  "UnknownFutureKey": { "a": 1 }
})JSON";

    const char* kModelJsonCs = R"JSON({
  "Name": "CS",
  "ModifiedAt": "2026-10-16T10:20:58.5696711+07:00",
  "InputTray": {
    "Name": "InputTray",
    "Rows": 2,
    "Cols": 3,
    "AxisRow": "Y1",
    "AxisCol": "X",
    "GeoZ1": {
      "P00": { "X": 100.5, "Y": -9.25, "Taught": true },
      "P0C": { "X": 140.5, "Y": -9.25, "Taught": true },
      "PR0": { "X": 100.5, "Y": 14.75, "Taught": true },
      "PRC_Verify": { "X": 0.0, "Y": 0.0, "Taught": false },
      "Offsets": [
        [ null, { "Dx": 0.125, "Dy": -0.25, "PlaceDx": 0.0, "PlaceDy": 0.0, "PlaceDa": 0.0, "HasTeachOffset": true, "HasPlaceTrim": false, "IsZero": false }, null ],
        [ null, null, null ]
      ],
      "PlaceDxAll": 0.0,
      "PlaceDyAll": 0.05,
      "PlaceDaAll": 0.0
    },
    "GeoZ2": {
      "P00": { "X": 30.5, "Y": -9.0, "Taught": true },
      "P0C": { "X": 70.5, "Y": -9.0, "Taught": true },
      "PR0": { "X": 30.5, "Y": 15.0, "Taught": true },
      "PRC_Verify": { "X": 0.0, "Y": 0.0, "Taught": false },
      "PlaceDxAll": 0.0,
      "PlaceDyAll": 0.0,
      "PlaceDaAll": 0.0
    },
    "VerifyThreshold": 0.5,
    "Z_PickZ1": 51.25,
    "Z_PickZ2": 51.5,
    "Z_SlowZ1": 47.5,
    "Z_SlowZ2": 47.375,
    "Z_Safe": 50.0,
    "ScanOrder": 2,
    "CamTopDownAnchorX": 0.0,
    "CamTopDownAnchorY": 0.0,
    "CamTopDownAnchorTaught": false,
    "CellCount": 6,
    "HalfColPitch": 10.0,
    "IsTaught": true
  },
  "AssyTray": {
    "Name": "AssyTray",
    "Rows": 4,
    "Cols": 1,
    "AxisRow": "Y2",
    "AxisCol": "X",
    "GeoZ1": {
      "P00": { "X": 490.5, "Y": 60.25, "Taught": true },
      "P0C": { "X": 0.0, "Y": 0.0, "Taught": false },
      "PR0": { "X": 491.0, "Y": 200.75, "Taught": true },
      "PRC_Verify": { "X": 0.0, "Y": 0.0, "Taught": false },
      "PlaceDxAll": 0.0,
      "PlaceDyAll": 0.0,
      "PlaceDaAll": 0.0
    },
    "GeoZ2": {
      "P00": { "X": 420.5, "Y": 60.25, "Taught": true },
      "P0C": { "X": 0.0, "Y": 0.0, "Taught": false },
      "PR0": { "X": 421.0, "Y": 200.75, "Taught": true },
      "PRC_Verify": { "X": 0.0, "Y": 0.0, "Taught": false },
      "PlaceDxAll": 0.0,
      "PlaceDyAll": 0.0,
      "PlaceDaAll": 0.0
    },
    "VerifyThreshold": 0.5,
    "Z_PickZ1": 46.0,
    "Z_PickZ2": 45.0,
    "Z_SlowZ1": 39.5,
    "Z_SlowZ2": 39.0,
    "Z_Safe": 50.0,
    "ScanOrder": "Serpentine",
    "CamTopDownAnchorX": 330.125,
    "CamTopDownAnchorY": 55.5,
    "CamTopDownAnchorTaught": true,
    "CellCount": 4,
    "HalfColPitch": "Infinity",
    "IsTaught": true
  },
  "InputTraysInUse": 1,
  "UsableInputTrays": 1,
  "Calib": {
    "HandEye1Dx": 490.125,
    "HandEye1Dy": 58.5,
    "HandEye2Dx": 420.25,
    "HandEye2Dy": 58.75,
    "HandEye1MeasuredAt": "2026-10-10T11:12:46.6085469+07:00",
    "HandEye2MeasuredAt": "0001-01-01T00:00:00",
    "HandEye1AngleRef": 227.875,
    "HandEye1YFixed": true,
    "HandEye2YFixed": false,
    "HandEye1TopX": -0.001,
    "HandEye1TopY": 0.0025,
    "HandEye1TopA": 47.125,
    "AlignAngleSign1": 1,
    "AlignAngleSign2": 0,
    "AlignVerifiedAt1": "2026-10-10T11:00:20.5878924+07:00",
    "AlignVerifiedAt2": "0001-01-01T00:00:00",
    "AlignVerifiedPosX1": 362.5,
    "AlignVerifiedPosX2": 0.0,
    "AlignVerifyResidual1": 0.0024,
    "AlignVerifyResidual2": 0.0,
    "TopDownVerifiedAt": "2026-09-19T11:58:04.8305149+07:00",
    "IsTopDownVerified": true,
    "IsUsable": false
  },
  "AlignPosX_Nozzle1": 362.5,
  "AlignPosX_Nozzle2": 291.25,
  "PickAngleA1": 0.40000000000000036,
  "PickAngleA2": 0.0,
  "HandEyePick_Nozzle1": { "X": 492.125, "Y": 59.125, "Taught": true },
  "HandEyePick_Nozzle2": { "X": 420.875, "Y": 59.25, "Taught": true },
  "HandEyeCamPos": { "X": 330.125, "Y": 55.5, "Taught": true },
  "FutureKey": [ 1, 2, 3 ]
})JSON";

    const char* kRunningJsonCs = R"JSON({
  "RecentModelName": "CS",
  "SavedAt": "2026-10-17T17:03:07.6152971+07:00",
  "AutoState": 5,
  "UnsafePause": true,
  "NozzleClearRequired": true,
  "NozzleClearReason": "test reason",
  "InputTray": {
    "Name": "InputTray",
    "Rows": 4,
    "Cols": 2,
    "Cells": [ 1, 1, 2, 3, 0, 0, 4, 5 ],
    "CurrentIndex": 3,
    "ScanOrder": 2,
    "CellCount": 8,
    "Trays": 2,
    "RowsPerTray": 2,
    "CellsPerTray": 4,
    "Selected": [ 1, 6 ],
    "HasSelection": true
  },
  "AssyTray": {
    "Name": "AssyTray",
    "Rows": 2,
    "Cols": 3,
    "Cells": [ 2, 2, 0, 0, 0, 5 ],
    "CurrentIndex": 0,
    "ScanOrder": 0,
    "CellCount": 6,
    "Trays": 1,
    "RowsPerTray": 2,
    "CellsPerTray": 6,
    "Selected": [],
    "HasSelection": false
  },
  "Nozzle1": {
    "Index": 1,
    "HasProduct": true,
    "SourceCellIndex": 3,
    "MeasuredAngle": -179.54692077636719,
    "Aligned": true
  },
  "Nozzle2": {
    "Index": 2,
    "HasProduct": false,
    "SourceCellIndex": -1,
    "MeasuredAngle": 0.0,
    "Aligned": false
  },
  "CountOk": 15,
  "CountNg": 1,
  "CountTotal": 16,
  "ConsecutiveNg": 0,
  "LastCycleTimeSec": 4.8980866999999995,
  "InputCountOk": 15,
  "InputCountNg": 1,
  "AssyCountOk": 15,
  "AssyCountNg": 6,
  "LastProductTimeSec": 4.8980866999999995,
  "RecentProductTimes": [ 4.15341815, 3.9803672999999997 ],
  "AvgProductTimeSec": 4.06689272,
  "AssyTrayStartedAt": "2026-10-17T16:58:00.5+07:00",
  "AssyTrayDone": true,
  "AssyTrayDoneSec": 9.929030599999999,
  "AssyTrayDoneProducts": 2,
  "AssyTrayDonePerProductSec": 4.9645152999999995,
  "LastAssyTrayTimeSec": 10.5009569,
  "LastAssyTrayProducts": 2,
  "LastAssyTrayPerProductSec": 5.25047845,
  "NgRatePercent": 6.25
})JSON";

    // ───────────────────────── Loc ─────────────────────────

    void TestLoc()
    {
        CHECK(Loc::Format("{0:0.000}", { 1.23456 }) == "1.235");
        CHECK(Loc::Format("{0:0.000}", { 2.0 }) == "2.000");
        CHECK(Loc::Format("{0:+0.0;-0.0}", { 1.25 }) == "+1.3");
        CHECK(Loc::Format("{0:+0.0;-0.0}", { -2.34 }) == "-2.3");
        // Âm nhưng làm tròn về 0 theo đoạn âm → .NET định dạng bằng đoạn DƯƠNG: "+0.0", không phải "-0.0".
        CHECK_MSG(Loc::Format("{0:+0.0;-0.0}", { -0.04 }) == "+0.0", Loc::Format("{0:+0.0;-0.0}", { -0.04 }));
        CHECK(Loc::Format("{0:0.###}", { 1.5 }) == "1.5");
        CHECK(Loc::Format("{0:0.###}", { 2.0 }) == "2");
        CHECK(Loc::Format("{0:0.###}", { 0.12345 }) == "0.123");
        CHECK(Loc::Format("{0:X}", { 255 }) == "FF");
        CHECK(Loc::Format("{0:X4}", { 255 }) == "00FF");
        CHECK(Loc::Format("{0,5}", { 42 }) == "   42");
        CHECK(Loc::Format("{0,-5}|", { 42 }) == "42   |");
        CHECK(Loc::Format("{{{0}}}", { 7 }) == "{7}");
        // Thiếu khoá → chính tên khoá; số chỗ {n} vượt số đối số → trả nguyên mẫu, không ném (như Loc.F của C#).
        CHECK(Loc::T("No.Such.Key") == "No.Such.Key" && !Loc::Has("No.Such.Key"));
        CHECK(Loc::Format("A {1} B", { 1 }) == "A {1} B");
        CHECK(Loc::F("Data.ModelNotFound", "x.json") == "Not found: x.json");
        CHECK(Loc::F("Data.JigPickNotTaught", 2) == "nozzle #2 jig pick pos. not taught (Teaching > Jig)");
    }

    // ───────────────────────── Hằng số: IoMap, AlarmCode, AxisId, Enums ─────────────────────────

    void TestConstants()
    {
        CHECK(IoMap::EmgOk == 0 && IoMap::BtnStop == 4 && IoMap::AirOk == 7 && IoMap::Vacuum1 == 8 && IoMap::Vacuum2 == 9);
        CHECK(IoMap::TrayIn2Present == 10 && IoMap::TrayAssyPresent == 11 && IoMap::TrayIn1Present == 12 && IoMap::LightCurtainOk == 13);
        CHECK(IoMap::BtnArmA1 == 14 && IoMap::BtnArmA2 == 15 && IoMap::BtnGoB1 == 16 && IoMap::BtnGoB2 == 17);
        CHECK(IoMap::StartButtons.size() == 4 && IoMap::StartButtons[0] == 14 && IoMap::StartButtons[3] == 17);
        CHECK(IoMap::DoVacuum1 == 0 && IoMap::DoVacuum2 == 1 && IoMap::DoBlow1 == 2 && IoMap::DoBlow2 == 3 && IoMap::DoLampRed == 4
              && IoMap::DoLampYellow == 5 && IoMap::DoLampGreen == 6 && IoMap::DoBuzzer == 7 && IoMap::DoLampStartBtn == 8);
        CHECK(IoMap::DoLampResetBtn == 9 && IoMap::DoLampStartBtnB == 10 && IoMap::DoLampCam1 == 11 && IoMap::DoLampCam2 == 12
              && IoMap::DoBrakeReleaseZ1 == 13 && IoMap::DoBrakeReleaseZ2 == 14 && IoMap::DoLampStartBtnB2 == 15);
        auto di = IoMap::DiNames();
        auto dout = IoMap::DoNames();
        CHECK(di.size() == 32 && dout.size() == 32 && IoMap::DiCount == 32 && IoMap::DoCount == 32);
        CHECK(di[IoMap::EmgOk] == "EMG OK (NC)" && di[1] == "(spare)" && di[IoMap::TrayIn1Present] == "Input tray 1 present (inner)");
        CHECK(dout[IoMap::DoBrakeReleaseZ2] == "Z2 brake off" && dout[IoMap::DoLampResetBtn] == "STOP lamp" && dout[31] == "(spare)");
        // Mọi khoá nhãn đều có chữ trong bảng (thiếu thì màn hình hiện tên khoá).
        for (const char* k : IoMap::DiNameKeys()) CHECK_MSG(Loc::Has(k), k);
        for (const char* k : IoMap::DoNameKeys()) CHECK_MSG(Loc::Has(k), k);

        CHECK(AxisId::All.size() == 7 && std::string(AxisId::All[0]) == "X" && std::string(AxisId::All[3]) == "Z1"
              && std::string(AxisId::All[6]) == "A2");
        CHECK(AxisId::IsValid("Z1") && !AxisId::IsValid("Z") && !AxisId::IsValid("A3") && AxisId::IndexOf("A2") == 6);
        CHECK(AxisId::ZAxes.size() == 2 && AxisId::PlanarAxes.size() == 3 && AxisId::RotateAxes.size() == 2);

        // 2105 = Z1 timeout về gốc (PLAN.md §7.2).
        CHECK(AlarmCode::ForAxis(AxisId::Z1, AxisFault::HomeTimeout) == 2105);
        CHECK(AlarmCode::ForAxis(AxisId::X, AxisFault::Disconnected) == 2000 && AlarmCode::ForAxis(AxisId::A2, AxisFault::FollowingError) == 2608);
        CHECK(AlarmCode::ForAxis("Q", AxisFault::HomeTimeout) == AlarmCode::UndefinedAlarm);
        std::string ax; AxisFault af;
        CHECK(AlarmCode::TryParseAxis(2105, ax, af) && ax == "Z1" && af == AxisFault::HomeTimeout);
        CHECK(AlarmCode::TryParseAxis(2304, ax, af) && ax == "Y1" && af == AxisFault::HardLimit);
        CHECK(!AlarmCode::TryParseAxis(2700, ax, af) && ax.empty() && !AlarmCode::TryParseAxis(1999, ax, af));

        CHECK((int)EAutoState::S05_ScanAssy == 5 && (int)EAutoState::S33_Place2 == 33 && (int)EAutoState::S50_TrayCheck == 50);
        CHECK(std::string(ToString(EAutoState::S40_Index)) == "S40_Index" && std::string(ToString(EMcState::Held)) == "Held");
        CHECK((int)EMcState::Emg == 11 && (int)ECellState::Skipped == 5 && (int)EScanOrder::Serpentine == 2 && (int)EAxisKind::Fastech == 1);
        CHECK((int)EOriginState::Aborted == 99 && std::string(ToString(EAlarmLevel::Fatal)) == "Fatal");
    }

    // ───────────────────────── AxisAngle / AxisScale ─────────────────────────

    void TestAxisMath()
    {
        // ── Số học góc (TestRotaryWrap) ──
        CHECK(Near(AxisAngle::Wrap(-10), 350));
        CHECK(Near(AxisAngle::Wrap(370), 10));
        CHECK(AxisAngle::Wrap(360) == 0.0 && AxisAngle::Wrap(720) == 0.0);
        CHECK(AxisAngle::Wrap(-1e-7) == 0.0);   // không thành 359.9999999
        CHECK(AxisAngle::Wrap(std::nan("")) == 0.0 && AxisAngle::Wrap(std::numeric_limits<double>::infinity()) == 0.0);
        CHECK(Near(AxisAngle::ShortestDelta(350, 10), 20));
        CHECK(Near(AxisAngle::ShortestDelta(10, 350), -20));
        CHECK(Near(AxisAngle::ShortestDelta(0, 180), 180));   // đúng nửa vòng thì đi chiều DƯƠNG
        CHECK(Near(AxisAngle::Distance(359, 1), 2));

        // ── AxisScale (TestAxisScale) — vít me 10 mm, 1000 xung/mm ──
        AxisConfig cfg;
        cfg.Name = "TEST"; cfg.Unit = "mm"; cfg.PulsePerUnit = 1000; cfg.UnitPerRev = 10; cfg.MaxRpm = 3000;
        AxisScale s(cfg);
        CHECK(s.ToPulse(20) == 20000 && s.ToPulse(-20) == -20000 && Near(s.ToUnit(20000), 20));
        CHECK(Near(s.ToUnit(s.ToPulse(37.123)), 37.123));
        CHECK_MSG(s.ToPulse(12.345) == 12345, std::to_string(s.ToPulse(12.345)));   // ví dụ đã chốt
        CHECK(s.ToPulse(0.0006) == 1 && s.ToPulse(-0.0006) == -1);                   // làm tròn, không cắt cụt
        CHECK(s.ToPulse(1e12) == INT_MAX && s.ToPulse(-1e12) == INT_MIN);            // cắt ở biên, không tràn
        CHECK(s.ToRpm(100) == 600 && s.ToRpm(-100) == 600 && s.ToRpm(10000) == 3000 && s.ToRpm(0.0001) == 1);
        CHECK(Near(s.FromRpm(600), 100));
        CHECK(s.ToPulsePerSec(100) == 100000u && s.ToPulsePerSec(0) == 1u);
        CHECK(s.PulsePerRev() == 360000);

        // Trục xoay: 18000 xung/vòng ĐỘNG CƠ qua hộp số 1/2 → 36000 xung cho một vòng đầu hút.
        AxisConfig rot;
        rot.Name = "A1"; rot.Unit = "deg"; rot.WrapDegrees = true;
        rot.PulsePerUnit = SystemData::RotaryPulsePerDeg; rot.UnitPerRev = SystemData::RotaryDegPerMotorRev; rot.MaxRpm = 3000;
        AxisScale r(rot);
        CHECK(Near(r.PulsePerUnit(), 36000.0 / 360.0) && r.ToPulse(360) == 36000 && r.ToPulse(90) == 9000);
        CHECK(r.PulsePerRev() == 36000);
        CHECK(r.ToPulsePerSec(180) == 18000u && r.ToRpm(180) == 60);

        // Cấu hình hỏng (chia cho 0) không được làm sập vòng điều khiển.
        AxisConfig badCfg;
        badCfg.PulsePerUnit = 0; badCfg.UnitPerRev = 0;
        AxisScale bad(badCfg);
        CHECK(bad.ToPulse(5) == 5 && Near(bad.ToUnit(5), 5));

        // Giữ shared_ptr tới cấu hình: sửa cấu hình thì phép quy đổi theo ngay.
        auto shared = std::make_shared<AxisConfig>();
        AxisScale live(shared);
        shared->PulsePerUnit = 2000;
        CHECK(live.ToPulse(1) == 2000);
        bool threw = false;
        try { AxisScale nul(std::shared_ptr<const AxisConfig>{}); }
        catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw);

        // Mức Auto vừa/thấp suy ra 50%/20% mức cao khi chưa đặt (TestZDecelPoint).
        AxisConfig sp;
        sp.MoveSpeed = 100;
        CHECK(Near(sp.MoveSpeedMid(), 50) && Near(sp.MoveSpeedLow(), 20));
        sp.SetMoveSpeedLow(12);
        CHECK(Near(sp.MoveSpeedLow(), 12));
    }

    // ───────────────────────── SystemData ─────────────────────────

    void TestSystemDataDefaults()
    {
        auto sys = SystemData::CreateDefault();

        // Ràng buộc phần cứng đã chốt (TestHardwareAssumptions).
        CHECK(!sys->HasAbsoluteEncoder);
        sys->SetRequireHomeAfterEmg(false);
        CHECK(sys->RequireHomeAfterEmg());   // không encoder tuyệt đối thì không tắt được
        sys->HasAbsoluteEncoder = true;
        CHECK(!sys->RequireHomeAfterEmg());
        sys->HasAbsoluteEncoder = false;
        sys->SetRequireHomeAfterEmg(true);
        CHECK(!sys->HomeRotateSequential && !sys->HasLightCurtain && !sys->HasAirSensor);
        for (const char* name : AxisId::ZAxes)
        {
            auto cfg = sys->GetAxis(name);
            CHECK_MSG(cfg->HomeDir == -1 && cfg->HomeUseZPhase, name);
        }

        CHECK(sys->MachineName == "Shield CAN Assy MC" && sys->MachineCode == "SO1C86-SCAM" && sys->SimulationMode && sys->Language == "vi");
        CHECK(sys->ZSafe == 50.0 && sys->ControlTickMs == 20 && sys->WatchdogTimeoutMs == 500 && sys->WatchdogStartupGraceMs == 5000);
        CHECK(sys->PickRetryCount == 2 && sys->PickRetryDeltaZ == 0.3 && sys->MaxConsecutiveNg == 5 && !sys->ScanAssyByRow);
        CHECK(!sys->ScanAssyByPair && sys->ScanAssy() == SystemData::EScanAssy::Tray);
        CHECK(sys->VacuumTimeoutMs == 800 && sys->VacuumSettleMs == 0 && sys->BlowTimeMs == 120);
        // Z qua điểm giảm tốc: mặc định đúng nhịp 30/09 (đầu vào hạ một mạch, nâng chậm; lắp ráp chậm cả hai chiều).
        CHECK(!sys->InputZSlowDown && sys->InputZSlowUp && sys->AssyZSlowDown && sys->AssyZSlowUp);
        CHECK(sys->IoDiIpAddress == "192.168.0.10" && sys->IoDoIpAddress == "192.168.0.11" && sys->VisionSolutionPath == "AlignBottom.solw");
        CHECK(sys->BottomSettleMs == 200 && sys->TopSettleMs == 200 && sys->BottomLampMs == 100 && sys->TopLampMs == 100);
        CHECK(sys->VisionOutputs.FlowLocateTop == "LocateTop" && sys->VisionOutputs.FlowAlignBottom(1) == "AlignBottom_Tool1"
              && sys->VisionOutputs.FlowAlignBottom(2) == "AlignBottom_Tool2" && sys->VisionOutputs.GvAxisY2 == "Y2_mm");
        CHECK(VisionOutputNames::RenderPath("T1", "  ") == "T1" && VisionOutputNames::RenderPath("T1", " T2.Mod ") == "T2.Mod");
        CHECK(sys->AlignCalibXOffsetsMm == "-4, -2, 2, 4, 0" && sys->AlignCalibXOffsets().size() == 5 && sys->AlignCalibXOffsets().back() == 0);
        CHECK(sys->AlignCalibAngles().size() == 6 && sys->AlignCalibAngles()[5] == 300);
        CHECK(sys->CalibValidDays == 90 && sys->CalibShotsPerPoint == 5 && sys->TopDownVerifyStepMm == 5.0 && !sys->TopDownCalibCenterSet);

        // Tay hút dùng trong Auto (OnlyNozzle).
        CHECK(sys->NozzleEnabled(1) && sys->NozzleEnabled(2));
        sys->OnlyNozzle = 1;
        CHECK(sys->NozzleEnabled(1) && !sys->NozzleEnabled(2));
        sys->OnlyNozzle = 7;   // số lạ = cả hai
        CHECK(sys->NozzleEnabled(1) && sys->NozzleEnabled(2));

        // Mỗi camera một số chờ ổn định / chờ đèn; Flow chỉ tính thì không chờ (TestSettleBeforeCapture).
        SystemData cam;
        cam.BottomSettleMs = 11; cam.TopSettleMs = 22; cam.BottomLampMs = 33; cam.TopLampMs = 44;
        CHECK(cam.SettleMsFor(EVmCamera::BottomUp) == 11 && cam.SettleMsFor(EVmCamera::TopDown) == 22 && cam.SettleMsFor(EVmCamera::None) == 0);
        CHECK(cam.LampMsFor(EVmCamera::BottomUp) == 33 && cam.LampMsFor(EVmCamera::TopDown) == 44 && cam.LampMsFor(EVmCamera::None) == 0);
        std::string camJson = cam.ToJsonText();
        auto camBack = SystemData::FromJsonText(R"({"VisionSettleMs":150,"LampSettleMs":80})");
        CHECK(camBack && camBack->BottomSettleMs == 150 && camBack->BottomLampMs == 80 && camBack->TopSettleMs == 200);
        CHECK(camJson.find("\"TopLampMs\": 44") != std::string::npos && camJson.find("\"VisionSettleMs\": 11") != std::string::npos
              && camJson.find("BottomSettleMs") == std::string::npos && camJson.find("\"LampSettleMs\": 33") != std::string::npos);

        // ── Trục mặc định ──
        CHECK(sys->Axes.size() == 7);
        for (size_t i = 0; i < sys->Axes.size() && i < AxisId::All.size(); ++i)
            CHECK(sys->Axes[i]->Name == AxisId::All[i]);
        for (const char* name : { AxisId::X, AxisId::Z1, AxisId::Z2, AxisId::Y1, AxisId::Y2 })
        {
            auto a = sys->GetAxis(name);
            CHECK_MSG(a->Kind == EAxisKind::Panasonic && a->PulsePerUnit == 1000.0 && a->Unit == "mm", name);
            // Số hiệu block là quy ước đã chốt của ServoControl: Block0=ORG, Block1=MoveAbs, Block2/3=Jog+/−.
            CHECK_MSG(a->HomeBlock == 0 && a->AbsoluteBlock == 1 && a->JogPositiveBlock == 2 && a->JogNegativeBlock == 3, name);
            CHECK_MSG(a->BaudRate == 115200 && a->UnitId == 1 && a->AccelTimeMs == 250 && a->PositionTolerancePulse == 10
                      && a->SkipMoveTolerancePulse == 5 && !a->WrapDegrees, name);
        }
        auto x = sys->GetAxis(AxisId::X);
        CHECK(x->ComPort == "COM3" && x->UnitPerRev == 27.0 && x->SoftLimitPos == 600 && x->MoveSpeed == 300 && x->MoveSpeedMid() == 150
              && Near(x->MoveSpeedLow(), 60) && x->StandbyPos == 100 && x->HomeDir == -1);
        CHECK(sys->GetAxis(AxisId::Z1)->ComPort == "COM4" && sys->GetAxis(AxisId::Z2)->ComPort == "COM5"
              && sys->GetAxis(AxisId::Z2)->StandbyPos == 10 && sys->GetAxis(AxisId::Z2)->SoftLimitPos == 120);
        CHECK(sys->GetAxis(AxisId::Y1)->ComPort == "COM6" && sys->GetAxis(AxisId::Y2)->ComPort == "COM7" && sys->GetAxis(AxisId::Y2)->MoveSpeed == 250);
        for (const char* name : { AxisId::A1, AxisId::A2 })
        {
            auto a = sys->GetAxis(name);
            CHECK_MSG(a->Kind == EAxisKind::Fastech && a->Unit == "deg" && a->WrapDegrees && a->SoftLimitNeg == 0 && a->SoftLimitPos == 360, name);
            CHECK_MSG(Near(a->PulsePerUnit, 36000.0 / 360.0) && Near(a->UnitPerRev, 180) && a->HomeDir == 1, name);
            CHECK_MSG(a->InSoftLimit(370) && a->InSoftLimit(-5), name);   // trục quay tự do: không góc nào bị chặn
        }
        CHECK(sys->GetAxis(AxisId::A1)->BoardId == 0 && sys->GetAxis(AxisId::A2)->BoardId == 1);
        CHECK(sys->GetAxis(AxisId::A1)->IpAddress == "192.168.0.2" && sys->GetAxis(AxisId::A2)->IpAddress == "192.168.0.3");
        CHECK(!x->InSoftLimit(x->SoftLimitPos + 1) && x->InSoftLimit(x->SoftLimitPos));
        // GetAxis tên chưa có: tạo cấu hình mặc định và THÊM vào danh sách (như bản C#).
        auto extra = sys->GetAxis("Q9");
        CHECK(extra && extra->Name == "Q9" && sys->Axes.size() == 8 && sys->GetAxis("Q9") == extra);
        CHECK(SystemData::RotaryPulsePerDeg == 100.0 && SystemData::RotaryDegPerMotorRev == 180.0);
    }

    void TestSystemDataMigrations()
    {
        // ── ParseNumberList ──
        auto l = SystemData::ParseNumberList("\xE2\x88\x92" "3;1.5  ,,x, 2e1\t7,+4,.5,5.,1,000");
        CHECK_MSG((l == std::vector<double>{ -3, 1.5, 20, 7, 4, 0.5, 5, 1, 0 }), std::to_string(l.size()));
        CHECK(SystemData::ParseNumberList("").empty() && SystemData::ParseNumberList("   ").empty());
        CHECK(SystemData::ParseNumberList("abc, 1e, -, 0x10, inf").empty());

        // ── Tự sửa system.json viết trước 2026-09-12 (TestRotaryWrap) ──
        auto old = SystemData::CreateDefault();
        for (const char* name : AxisId::RotateAxes)
        {
            auto a = old->GetAxis(name);
            a->WrapDegrees = false;
            a->PulsePerUnit = 10000.0 / 360.0;   // con số cũ: quên mất hộp số 1/2
            a->UnitPerRev = 360;
            a->SoftLimitNeg = -360;
            a->HomeOffset = 137.5;               // số ĐO ĐƯỢC — không được đụng vào
        }
        auto oldA1 = old->GetAxis(AxisId::A1);
        auto migrated = old->MigrateRotaryAxes();
        CHECK_MSG(migrated.size() == 2, base::Join(migrated, ","));
        CHECK(Near(oldA1->PulsePerUnit, 100) && oldA1->WrapDegrees && Near(oldA1->SoftLimitNeg, 0) && Near(oldA1->UnitPerRev, 180));
        CHECK(Near(oldA1->HomeOffset, 137.5));
        CHECK(old->MigrateRotaryAxes().empty());   // chạy lần hai không sửa gì thêm

        // ── File ĐÃ migrate một lần nhưng tỉ lệ xung đã lỗi thời (bẫy của lần đổi động cơ 2026-09-12) ──
        auto stale = SystemData::CreateDefault();
        for (const char* name : AxisId::RotateAxes)
        {
            auto a = stale->GetAxis(name);
            a->PulsePerUnit = 20000.0 / 360.0;
            a->HomeOffset = 42.25;
            a->StandbyPos = -10;   // khi sửa, vị trí chờ được quấn về [0, 360)
        }
        auto fixedAxes = stale->MigrateRotaryAxes();
        CHECK(fixedAxes.size() == 2 && Near(stale->GetAxis(AxisId::A1)->PulsePerUnit, SystemData::RotaryPulsePerDeg));
        CHECK(Near(stale->GetAxis(AxisId::A1)->HomeOffset, 42.25) && Near(stale->GetAxis(AxisId::A2)->StandbyPos, 350));
        CHECK(SystemData::CreateDefault()->MigrateRotaryAxes().empty());

        // ── iBdID trùng nhau: file cũ không có BoardId nên A2 đọc ra 0, trùng đúng ô của A1 ──
        auto sameBoard = SystemData::CreateDefault();
        sameBoard->GetAxis(AxisId::A2)->BoardId = sameBoard->GetAxis(AxisId::A1)->BoardId;
        auto boardFixed = sameBoard->MigrateRotaryAxes();
        CHECK((boardFixed == std::vector<std::string>{ "A2" }));
        CHECK(sameBoard->GetAxis(AxisId::A1)->BoardId == FastechBoardId::AxisA1 && sameBoard->GetAxis(AxisId::A2)->BoardId == FastechBoardId::AxisA2
              && FastechBoardId::AxisA1 == 0 && FastechBoardId::AxisA2 == 1);
        // iBdID của trục xoay không đụng tới hai card I/O (10 và 11).
        CHECK(FastechBoardId::AxisA1 != FastechBoardId::Io32Di && FastechBoardId::AxisA2 != FastechBoardId::Io32Do);
        // Trục Panasonic hay đơn vị khác "deg" thì không đụng.
        auto notRotary = SystemData::CreateDefault();
        notRotary->GetAxis(AxisId::X)->WrapDegrees = false;
        notRotary->GetAxis(AxisId::A1)->Unit = "DEG";   // so không phân biệt hoa thường → vẫn là trục xoay
        notRotary->GetAxis(AxisId::A1)->PulsePerUnit = 1;
        CHECK((notRotary->MigrateRotaryAxes() == std::vector<std::string>{ "A1" }));

        // ── Tên biến trục mang đơn vị (2026-09-15) ──
        VisionOutputNames v;
        v.GvAxisX = "X"; v.GvAxisY2 = "Y2"; v.GvAxisA1 = "Mine"; v.GvAxisA2 = "A2";
        auto renamed = v.MigrateAxisVarUnits();
        CHECK((renamed == std::vector<std::string>{ "X → X_mm", "Y2 → Y2_mm", "A2 → A2_deg" }));
        CHECK(v.GvAxisX == "X_mm" && v.GvAxisY2 == "Y2_mm" && v.GvAxisA1 == "Mine" && v.GvAxisA2 == "A2_deg");
        CHECK(v.MigrateAxisVarUnits().empty());
    }

    void TestSystemDataJson()
    {
        // null / rỗng → nullptr; JSON hỏng hoặc gốc không phải object → ném (như JsonConvert.DeserializeObject).
        CHECK(!SystemData::FromJsonText("") && !SystemData::FromJsonText("  null "));
        bool threw = false;
        try { SystemData::FromJsonText("[1, 2]"); } catch (const std::exception&) { threw = true; }
        CHECK(threw);

        // ── Chưa có system.json → tạo bản mặc định ──
        std::error_code ec;
        fs::remove(AppPaths::SystemFile(), ec);
        McDataManager::LoadSystemData();
        CHECK(fs::exists(AppPaths::SystemFile()) && McDataManager::SysData()->Axes.size() == 7);
        std::string raw = RawFile(AppPaths::SystemFile());
        CHECK(base::StartsWith(raw, "\xEF\xBB\xBF{\r\n  \"MachineName\": \"Shield CAN Assy MC\",\r\n"));   // BOM + CRLF như WriteAtomic C#
        json j = ParseText(raw);
        CheckKeys(j, kSystemKeys, "system.json keys");
        CheckKeys(j["VisionOutputs"], kVisionOutputKeys, "VisionOutputs keys");
        CHECK(j["Axes"].is_array() && j["Axes"].size() == 7);
        for (const auto& a : j["Axes"]) CheckKeys(a, kAxisKeys, "axis keys");
        CHECK(j["ZSafe"].is_number_float() && j["ControlTickMs"].is_number_integer() && j["Axes"][5]["Kind"] == 1
              && j["Axes"][0]["MoveSpeedMid"] == 150.0 && j["RequireHomeAfterEmg"] == true);
        CHECK(raw.find("\"ZSafe\": 50.0,") != std::string::npos);   // số thực nguyên ghi "50.0" như Newtonsoft

        // ── Sửa → lưu → nạp lại: so toàn bộ chuỗi JSON (mọi trường) ──
        auto sys = McDataManager::SysData();
        sys->MachineName = "Máy thử";
        sys->SimulationMode = true;
        sys->Language = "en";
        sys->ZSafe = 12.5;
        sys->OnlyNozzle = 2;
        sys->ScanAssyByRow = true;
        sys->TopDownCalibCenterSet = true;
        sys->TopDownCalibCenterX = 300.125;
        sys->AlignCalibAnglesDeg = "0, 30";
        sys->VisionOutputs.RenderTool2 = "T2.Module";
        sys->VisionOutputs.FlowAlignBottomTool2 = "AB2";
        sys->GetAxis(AxisId::Y2)->SetMoveSpeedLow(33.0);
        sys->GetAxis(AxisId::Y2)->SkipMoveTolerancePulse = 3;
        sys->GetAxis(AxisId::A1)->HomeOffset = -14.25;
        std::string before = sys->ToJsonText();
        CHECK(McDataManager::SaveSystemData());
        McDataManager::LoadSystemData();
        auto re = McDataManager::SysData();
        CHECK(re != sys && re->ToJsonText() == before);
        CHECK(re->MachineName == "Máy thử" && re->Language == "en" && re->OnlyNozzle == 2 && re->GetAxis(AxisId::Y2)->MoveSpeedLow() == 33.0
              && re->GetAxis(AxisId::A1)->HomeOffset == -14.25 && re->VisionOutputs.FlowAlignBottom(2) == "AB2");
        CHECK(re->ScanAssy() == SystemData::EScanAssy::Row);

        // Quét từng cặp (chỉ bản WinUI): khoá riêng, thắng ScanAssyByRow; đặt chế độ thì hai cờ không bao giờ cùng bật.
        re->SetScanAssy(SystemData::EScanAssy::Pair);
        CHECK(re->ScanAssyByPair && !re->ScanAssyByRow);
        CHECK(McDataManager::SaveSystemData());
        McDataManager::LoadSystemData();
        CHECK(McDataManager::SysData()->ScanAssy() == SystemData::EScanAssy::Pair);
        CHECK(RawFile(AppPaths::SystemFile()).find("\"ScanAssyByPair\": true") != std::string::npos);
        McDataManager::SysData()->ScanAssyByRow = true;   // file sửa tay: cả hai cờ bật → theo cặp
        CHECK(McDataManager::SysData()->ScanAssy() == SystemData::EScanAssy::Pair);
        McDataManager::SysData()->SetScanAssy(SystemData::EScanAssy::Tray);
        CHECK(!McDataManager::SysData()->ScanAssyByRow && !McDataManager::SysData()->ScanAssyByPair);

        // ── system.json dạng C#: trục sai thứ tự, thiếu trục, trục lạ, phần tử null, khoá lạ, khoá cũ, enum dạng tên ──
        base::WriteAllText(AppPaths::SystemFile(), std::string("\xEF\xBB\xBF") + kSystemJsonCs);
        AlarmManager::ForceClearAll();
        McDataManager::LoadSystemData();
        auto cs = McDataManager::SysData();
        CHECK(!AlarmManager::IsActive(AlarmCode::ConfigInvalid));
        CHECK(cs->Axes.size() == 7);
        for (size_t i = 0; i < cs->Axes.size() && i < AxisId::All.size(); ++i)
            CHECK_MSG(cs->Axes[i]->Name == AxisId::All[i], cs->Axes[i]->Name);
        auto z1 = cs->GetAxis(AxisId::Z1);
        CHECK(z1->ComPort == "COM16" && z1->BaudRate == 57600 && z1->UnitId == 2 && z1->DecelTimeMs == 120 && z1->PositionTolerancePulse == 50
              && z1->MoveSpeed == 1200 && z1->MoveSpeedMid() == 50 && z1->MoveSpeedLow() == 10 && z1->SoftLimitNeg == -10);
        auto cx = cs->GetAxis(AxisId::X);
        // MoveSpeedMid = 0 trong file → "chưa đặt" → suy ra 50% của mức cao; khoá thiếu giữ mặc định của AxisConfig.
        CHECK(cx->ComPort == "COM13" && cx->MoveSpeedMid() == 150 && Near(cx->MoveSpeedLow(), 60) && cx->StandbyPos == 291.5
              && cx->UnitPerRev == 10.0 && cx->HomeDir == 1 && cx->Kind == EAxisKind::Panasonic);
        auto a2 = cs->GetAxis(AxisId::A2);
        CHECK(a2->Kind == EAxisKind::Fastech && a2->BoardId == 1 && a2->HomeOffset == -114.5 && a2->WrapDegrees && a2->ComPort.empty());
        CHECK(cs->GetAxis(AxisId::Y1)->ComPort == "COM6" && cs->GetAxis(AxisId::A1)->BoardId == 0);   // trục thiếu → mặc định
        CHECK(cs->ZSafe == 25.0 && cs->Language == "en" && cs->HasLightCurtain && cs->ControlTickMs == 40 && cs->OnlyNozzle == 2
              && !cs->NozzleEnabled(1) && cs->PickRetryDeltaZ == 0.25 && cs->VacuumSettleMs == 111 && cs->InputZSlowDown && !cs->AssyZSlowUp);
        CHECK(cs->RequireHomeAfterEmg());   // file ghi false, nhưng không có encoder tuyệt đối
        CHECK(cs->BottomSettleMs == 150 && cs->TopSettleMs == 175 && cs->BottomLampMs == 80 && cs->TopLampMs == 60);
        CHECK(cs->VisionOutputs.GvAxisX == "X_mm" && cs->VisionOutputs.GvAxisY2 == "MyY2" && cs->VisionOutputs.GvAxisA1 == "A1_deg"
              && cs->VisionOutputs.GvAxisA2 == "A2_deg" && cs->VisionOutputs.RenderTool2 == "T2.Module");
        CHECK(cs->VisionSolutionPath == "C:\\Jobs\\Test.solw" && cs->TopDownCalibCenterX == 300.125 && cs->AlignCalibAngles().size() == 3);
        // Có migrate → file được ghi lại ngay, đúng dạng C#, bỏ khoá lạ/trục lạ.
        std::string rewritten = RawFile(AppPaths::SystemFile());
        json rj = ParseText(rewritten);
        CheckKeys(rj, kSystemKeys, "rewritten system.json keys");
        CHECK(rj["Axes"].size() == 7 && rj["Axes"][6]["Name"] == "A2" && rj["Axes"][6]["BoardId"] == 1 && rj["VisionOutputs"]["GvAxisX"] == "X_mm");
        CHECK(rewritten.find("BOGUS") == std::string::npos && rewritten.find("UnknownFutureKey") == std::string::npos
              && rewritten.find("FlowTopDownCalib") == std::string::npos && rewritten.find("SomeFutureField") == std::string::npos);
        CHECK(rewritten.find("\"VisionSettleMs\": 150") != std::string::npos && rewritten.find("\"LampSettleMs\": 80") != std::string::npos);

        // ── JSON hỏng → bản mặc định + alarm 1000 ──
        base::WriteAllText(AppPaths::SystemFile(), "{ \"MachineName\": ");
        McDataManager::LoadSystemData();
        CHECK(AlarmManager::IsActive(AlarmCode::ConfigInvalid) && McDataManager::SysData()->MachineName == "Shield CAN Assy MC");
        AlarmManager::ForceClearAll();
        CHECK(McDataManager::SaveSystemData());
    }

    // ───────────────────────── TrayLayout / TrayState ─────────────────────────

    void TestTrayGridZigzag()
    {
        // IndexOf phải là nghịch đảo ĐÚNG của RowCol ở mọi ô — nếu không, lưới vẽ ra sẽ gán nhầm trạng thái của ô này
        // sang chỗ của ô khác (TestTrayGridZigzag).
        for (EScanOrder order : { EScanOrder::RowMajor, EScanOrder::Serpentine })
        {
            TrayState tray;
            tray.ScanOrder = order;
            tray.Resize(5, 8);
            tray.ResetAll(ECellState::Empty);
            bool inverse = true, onto = true;
            std::vector<bool> hit((size_t)tray.CellCount(), false);
            for (int i = 0; i < tray.CellCount(); i++)
            {
                int r, c;
                tray.RowCol(i, r, c);
                int back = tray.IndexOf(r, c);
                if (back != i) inverse = false;
                if (back >= 0 && back < (int)hit.size()) hit[(size_t)back] = true;
            }
            for (bool h : hit) if (!h) onto = false;
            CHECK_MSG(inverse && onto, ToString(order));
        }
        TrayState zig;
        zig.ScanOrder = EScanOrder::Serpentine;
        zig.Resize(5, 8);
        int r1, c1;
        zig.RowCol(zig.Cols, r1, c1);
        CHECK(r1 == 1 && c1 == zig.Cols - 1);   // ô ghé ngay sau hàng đầu nằm ở cột CUỐI của hàng hai
        CHECK(TrayState::VisitIndex(1, 0, 8, EScanOrder::Serpentine) == 15 && TrayState::VisitIndex(1, 0, 8, EScanOrder::RowMajor) == 8
              && TrayState::VisitIndex(0, 0, 0, EScanOrder::RowMajor) == -1);
        int r0 = -1, c0 = -1;
        TrayState empty;
        empty.RowCol(5, r0, c0);
        CHECK(r0 == 0 && c0 == 0);

        // ── Create / Resize / Advance / ResetAll ──
        auto t = TrayState::Create("T", 2, 3);
        CHECK(t->Name == "T" && t->Rows == 2 && t->Cols == 3 && t->Cells.size() == 6 && t->CurrentIndex == 0 && t->Get(0) == ECellState::Empty);
        t->Set(1, ECellState::Ok);
        t->Set(5, ECellState::Ng);
        t->Set(99, ECellState::Ok);   // ngoài dải: bỏ qua
        CHECK(t->Get(99) == ECellState::Empty && t->Get(-1) == ECellState::Empty);
        t->CurrentIndex = 5;
        t->Resize(3, 2);              // giữ phần lưới trùng nhau: ô (0,1) còn Ok, ô (1,2) cũ bị cắt
        CHECK(t->Cells.size() == 6 && t->Get(1) == ECellState::Ok && t->Get(5) == ECellState::Empty && t->CurrentIndex == 5);
        t->Resize(1, 2);
        CHECK(t->CurrentIndex == 0 && t->Get(1) == ECellState::Ok);
        t->Resize(0, 0);
        CHECK(t->CurrentIndex == -1 && t->Cells.empty());
        t->Resize(1, 3);
        t->ResetAll();
        CHECK(t->Get(2) == ECellState::Loaded && t->CurrentIndex == 0);
        CHECK(t->Advance() && t->Advance() && !t->Advance() && t->CurrentIndex == -1 && !t->Advance());

        // ── Chọn ô (2026-10-16) ──
        auto s = TrayState::Create("S", 2, 5);
        CHECK(!s->HasSelection() && s->InScope(3) && !s->IsSelected(3));
        s->ToggleSelected(3);
        s->ToggleSelected(6);
        s->ToggleSelected(4);
        s->ToggleSelected(4);
        CHECK(s->HasSelection() && (Sorted(s->Selected()) == std::vector<int>{ 3, 6 }) && s->IsSelected(6) && s->InScope(3) && !s->InScope(4));
        TrayState copy = *s;          // bản sao mang theo lựa chọn
        CHECK((Sorted(copy.Selected()) == std::vector<int>{ 3, 6 }));
        s->Resize(2, 5);              // cùng cỡ: giữ lựa chọn
        CHECK(s->HasSelection());
        s->Resize(3, 5);              // đổi cỡ: chỉ số ô không còn trỏ đúng ô cũ — bỏ lựa chọn
        CHECK(!s->HasSelection() && copy.HasSelection());
        copy.ClearSelection();
        CHECK(!copy.HasSelection() && copy.InScope(0));
        copy.SetSelected({ 1 });
        CHECK(copy.IsSelected(1) && !copy.InScope(2));

        // RowsPerTray / CellsPerTray khi sổ nối hai tray.
        auto two = TrayState::Create("In", 4, 5);
        two->Trays = 2;
        CHECK(two->RowsPerTray() == 2 && two->CellsPerTray() == 10 && two->CellCount() == 20);
    }

    void TestTrayGridMath()
    {
        const char* Z1 = AxisId::Z1;
        const char* Z2 = AxisId::Z2;

        TrayLayout t;
        t.Name = "Test"; t.SetRows(4); t.SetCols(5);
        t.GeoZ1.P00 = TeachPoint(10, 20);
        t.GeoZ1.P0C = TeachPoint(50, 20);   // 4 bước cột → u = (10, 0)
        t.GeoZ1.PR0 = TeachPoint(10, 50);   // 3 bước hàng → v = (0, 10)
        auto p11 = t.CellPos(Z1, 1, 1);
        CHECK(Near(p11.X, 20) && Near(p11.Y, 30));
        auto pLast = t.CellPos(Z1, 3, 4);
        CHECK(Near(pLast.X, 50) && Near(pLast.Y, 50));

        // Tray đặt xoay: u và v là vector 2 chiều nên phải hấp thụ được góc nghiêng.
        TrayLayout r;
        r.SetRows(3); r.SetCols(3);
        r.GeoZ1.P00 = TeachPoint(0, 0);
        r.GeoZ1.P0C = TeachPoint(20, 2);
        r.GeoZ1.PR0 = TeachPoint(-2, 20);
        auto rc = r.CellPos(Z1, 1, 1);
        CHECK(Near(rc.X, 9) && Near(rc.Y, 11));

        // Điểm kiểm chứng bắt lỗi teach nhầm thứ tự P0C / PR0.
        double e1 = -1, e2 = -1;
        t.GeoZ1.PRC_Verify = TeachPoint(50, 50);
        CHECK(t.VerifyGrid(Z1, e1) && Near(e1, 0));
        t.GeoZ1.PRC_Verify = TeachPoint(50, 53);
        CHECK(!t.VerifyGrid(Z1, e2) && Near(e2, 3));
        t.GeoZ1.PRC_Verify = TeachPoint();   // chưa teach → luôn đạt
        CHECK(t.VerifyGrid(Z1, e1) && e1 == 0);

        // Lưới 1 hàng / 1 cột không được chia cho 0.
        TrayLayout one;
        one.SetRows(1); one.SetCols(1);
        one.GeoZ1.P00 = TeachPoint(7, 8);
        auto op = one.CellPos(Z1, 0, 0);
        CHECK(Near(op.X, 7) && Near(op.Y, 8) && std::isinf(one.HalfColPitch()));

        // Kích thước kẹp [1, 50] như setter của bản C#.
        TrayLayout clamp;
        clamp.SetRows(0); clamp.SetCols(99);
        CHECK(clamp.Rows() == 1 && clamp.Cols() == 50);
        clamp.Resize(-3, 51);
        CHECK(clamp.Rows() == 1 && clamp.Cols() == 50 && clamp.GeoZ1.Offsets.size() == 1 && clamp.GeoZ1.Offsets[0].size() == 50);

        // Đổi kích thước phải giữ lại offset của phần lưới trùng nhau.
        t.SetOffset(Z1, 0, 0, 0.11, 0.22);
        t.SetOffset(Z1, 2, 3, 0.33, 0.44);
        t.Resize(6, 7);
        auto keep0 = t.GetOffset(Z1, 0, 0);
        auto keep1 = t.GetOffset(Z1, 2, 3);
        CHECK(keep0 && Near(keep0->Dx, 0.11) && keep1 && Near(keep1->Dy, 0.44) && t.Rows() == 6 && t.Cols() == 7);
        CHECK(!t.GetOffset(Z1, 5, 6) && !t.GetOffset(Z1, 9, 9) && !t.GetOffset(Z2, 0, 0));

        // Bù đặt nằm chung ô nhớ với offset teach nhưng KHÁC nghĩa — ghi cái này không được xoá cái kia, và bù đặt
        // tuyệt đối không được chui vào toạ độ lưới.
        t.SetPlaceTrim(Z1, 0, 0, 0.05, -0.07, 0.9);
        auto mix = t.GetOffset(Z1, 0, 0);
        CHECK(mix && Near(mix->Dx, 0.11) && Near(mix->Dy, 0.22) && Near(mix->PlaceDx, 0.05) && Near(mix->PlaceDa, 0.9));
        t.SetOffset(Z1, 0, 0, 0.11, 0.22);
        mix = t.GetOffset(Z1, 0, 0);
        CHECK(mix && Near(mix->PlaceDy, -0.07));   // teach lại offset KHÔNG xoá bù đặt
        auto trimRaw = t.CellPosRaw(Z1, 0, 0);
        auto trimPos = t.CellPos(Z1, 0, 0);
        CHECK(Near(trimPos.X - trimRaw.X, 0.11) && Near(trimPos.Y - trimRaw.Y, 0.22));
        t.Resize(4, 5);
        mix = t.GetOffset(Z1, 0, 0);
        CHECK(mix && Near(mix->PlaceDx, 0.05) && Near(mix->PlaceDa, 0.9));   // đổi kích thước giữ cả bù đặt
        t.SetPlaceTrim(Z1, 0, 0, 0, 0, 0);
        mix = t.GetOffset(Z1, 0, 0);
        CHECK(mix && Near(mix->Dx, 0.11) && !mix->HasPlaceTrim() && mix->HasTeachOffset() && !mix->IsZero());

        // Bù chung: cộng cho mọi ô của đúng Tool, dồn với bù riêng của ô, không lây sang Tool kia.
        t.SetPlaceTrim(Z1, 0, 0, 0.05, -0.07, 0.9);
        t.SetPlaceTrimAll(Z1, 0.2, 0.1, -1.0);
        auto sum00 = t.PlaceTrim(Z1, 0, 0);
        CHECK(Near(sum00.PlaceDx, 0.25) && Near(sum00.PlaceDy, 0.03) && Near(sum00.PlaceDa, -0.1) && !sum00.HasTeachOffset());
        auto sum23 = t.PlaceTrim(Z1, 2, 3);
        CHECK(Near(sum23.PlaceDx, 0.2) && Near(sum23.PlaceDy, 0.1) && Near(sum23.PlaceDa, -1.0));
        CHECK(!t.PlaceTrim(Z2, 0, 0).HasPlaceTrim());

        // Ô ngoài lưới khi ghi offset → ném như IndexOutOfRangeException của bản C#.
        bool threw = false;
        try { t.SetOffset(Z1, 10, 0, 1, 1); } catch (const std::out_of_range&) { threw = true; }
        CHECK(threw);

        // CellPosRaw — phần "lý thuyết" không cộng offset (trang Teaching dùng để tính Set Offset).
        TrayLayout g;
        g.SetRows(3); g.SetCols(3);
        g.GeoZ1.P00 = TeachPoint(0, 0);
        g.GeoZ1.P0C = TeachPoint(20, 0);
        g.GeoZ1.PR0 = TeachPoint(0, 20);
        g.SetOffset(Z1, 1, 1, 2.0, -3.0);
        auto raw11 = g.CellPosRaw(Z1, 1, 1);
        auto full11 = g.CellPos(Z1, 1, 1);
        CHECK(Near(raw11.X, 10) && Near(raw11.Y, 10) && Near(full11.X, 12) && Near(full11.Y, 7));
        CHECK(Near(g.HalfColPitch(), 5));

        // Mỗi Tool có lưới riêng, độc lập hoàn toàn.
        TrayLayout dual;
        dual.SetRows(2); dual.SetCols(2);
        dual.GeoZ1.P00 = TeachPoint(0, 0); dual.GeoZ1.P0C = TeachPoint(10, 0); dual.GeoZ1.PR0 = TeachPoint(0, 10);
        dual.GeoZ2.P00 = TeachPoint(100, 100); dual.GeoZ2.P0C = TeachPoint(110, 100); dual.GeoZ2.PR0 = TeachPoint(100, 110);
        auto z1cell = dual.CellPos(Z1, 1, 1);
        auto z2cell = dual.CellPos(Z2, 1, 1);
        CHECK(Near(z1cell.X, 10) && Near(z1cell.Y, 10) && Near(z2cell.X, 110) && Near(z2cell.Y, 110));
        CHECK(dual.IsTaughtFor(Z1) && dual.IsTaughtFor(Z2) && dual.IsTaught() && !TrayLayout().IsTaughtFor(Z1));

        // CamPosFor: mốc ô (1,1) + độ lệch lưới Tool 2 (CamGridZAxis).
        dual.CamTopDownAnchorX = 300; dual.CamTopDownAnchorY = 50;
        auto cam = dual.CamPosFor(1, 1);
        CHECK(Near(cam.X, 310) && Near(cam.Y, 60) && std::string(TrayLayout::CamGridZAxis) == "Z2");

        // Điểm giảm tốc (TestZDecelPoint): hợp lệ khi 0 < Z_Slow < Z_Pick; tắt chặng → 0.
        TrayLayout zs;
        zs.Z_PickZ1 = 90; zs.Z_SlowZ1 = 85; zs.Z_PickZ2 = 91; zs.Z_SlowZ2 = 95;
        CHECK(zs.ZSlowValid(Z1) && !zs.ZSlowValid(Z2) && zs.ZSlowFor(Z1) == 85 && zs.ZSlowFor(Z1, false) == 0 && zs.ZPickFor(Z2) == 91);
        zs.Z_SlowZ1 = 0;
        CHECK(!zs.ZSlowValid(Z1));

        // TeachPoint: phép trừ / Scaled.
        TeachPoint d = TeachPoint(5, 7) - TeachPoint(1, 2);
        CHECK(d.X == 4 && d.Y == 5 && d.Taught && d.Scaled(2).X == 8 && !TeachPoint().Taught);

        // Mặc định của hai tray.
        auto in = TrayLayout::CreateInputDefault();
        auto as = TrayLayout::CreateAssyDefault();
        CHECK(in->Name == "InputTray" && in->Rows() == 4 && in->Cols() == 5 && in->AxisRow == "Y1" && in->AxisCol == "X" && in->Z_Safe == 50.0);
        CHECK(as->Name == "AssyTray" && as->Rows() == 5 && as->Cols() == 6 && as->AxisRow == "Y2" && as->VerifyThreshold == 0.5);
        CHECK(TrayLayout::CreateInputDefault("InputTray2")->Name == "InputTray2");
    }

    // ───────────────────────── ModelData ─────────────────────────

    void TestModelData()
    {
        const char* Z1 = AxisId::Z1;
        const char* Z2 = AxisId::Z2;

        // ── Model mô phỏng: Tool1/Tool2 dùng lưới Input Tray độc lập ──
        auto sim = ModelData::CreateSimulation();
        auto z1Pick = sim->InputTray->CellPos(Z1, 0, 0);
        auto z2Pick = sim->InputTray->CellPos(Z2, 0, 0);
        CHECK(!Near(z1Pick.X, z2Pick.X) && !Near(z1Pick.Y, z2Pick.Y));
        CHECK(sim->Name == "SIM" && sim->InputTray->Rows() == 2 && sim->InputTray->Cols() == 5 && sim->AssyTray->Rows() == 3
              && sim->AssyTray->Cols() == 4 && sim->AssyTray->ScanOrder == EScanOrder::Serpentine);
        CHECK(sim->InputTray->ZSlowValid(Z1) && sim->InputTray->ZSlowValid(Z2) && sim->Calib.IsUsable() && !sim->Calib.IsExpired(90));
        CHECK(sim->HandEyeJigProblem(1).empty() && sim->HandEyeJigProblem(2).empty() && sim->PickAngle(2) == 356.5 && sim->AlignPosX(2) == 270);
        CHECK(Near(sim->AssyTray->CamTopDownAnchorX, 390) && Near(sim->AssyTray->CamTopDownAnchorY, 55) && sim->AssyTray->CamTopDownAnchorTaught);
        CHECK(Near(sim->Calib.HandEyeDx(2), -70) && Near(sim->Calib.HandEyeDy(2), 5.4) && sim->Calib.AlignAngleSign(2) == 1);

        // ── Model mới: ba điểm cơ cấu kẹp CHƯA teach; câu nói đúng điểm còn thiếu (TestModelManagement) ──
        ModelData model;
        CHECK(model.Name == "DEFAULT" && model.Description.empty() && model.InputTray && model.AssyTray && model.InputTray2);
        CHECK(!model.HandEyePick_Nozzle1.Taught && !model.HandEyePick_Nozzle2.Taught && !model.HandEyeCamPos.Taught);
        CHECK_MSG(model.HandEyeJigProblem(1).find("jig pick") != std::string::npos && !model.HandEyeJigProblem(2).empty(), model.HandEyeJigProblem(1));
        model.HandEyePick_Nozzle1 = TeachPoint(123.456, 78.901);
        model.HandEyePick_Nozzle2 = TeachPoint(61.234, 79.012);
        CHECK(model.HandEyeJigProblem(1) == Loc::T("Data.JigCamNotTaught"));
        model.HandEyeCamPos = TeachPoint(150.987, 90.123);
        CHECK(model.HandEyeJigProblem(1).empty() && model.HandEyeJigProblem(2).empty() && model.HandEyePick(2).X == 61.234);

        // ── Tray 2 theo Tray 1 (NormalizeInputTrays) ──
        ModelData two;
        auto t1 = two.InputTray;
        t1->Resize(3, 6);
        t1->ScanOrder = EScanOrder::Serpentine;
        t1->Z_PickZ1 = 51; t1->Z_PickZ2 = 52; t1->Z_SlowZ1 = 47; t1->Z_SlowZ2 = 48; t1->Z_Safe = 45; t1->VerifyThreshold = 0.3;
        CHECK(two.InputTraysInUse() == 1 && two.UsableInputTrays() == 1 && two.InputLayout(0) == t1 && two.InputLayout(1) == two.InputTray2);
        two.NormalizeInputTrays();
        auto t2 = two.InputTray2;
        CHECK(t2->Rows() == 3 && t2->Cols() == 6 && t2->ScanOrder == EScanOrder::Serpentine && t2->AxisRow == t1->AxisRow
              && t2->Z_PickZ1 == 51 && t2->Z_PickZ2 == 52 && t2->Z_SlowZ1 == 47 && t2->Z_Safe == 45 && t2->VerifyThreshold == 0.3);
        CHECK(t2->GeoZ1.Offsets.size() == 3 && t2->GeoZ1.Offsets[0].size() == 6);
        // Tray 2 đã teach (P00) thì KHÔNG lấy cao độ của Tray 1 nữa.
        t2->GeoZ1.P00 = TeachPoint(1, 2);
        t2->Z_PickZ1 = 0; t2->Z_PickZ2 = 0;
        t1->Z_PickZ1 = 60;
        two.NormalizeInputTrays();
        CHECK(t2->Z_PickZ1 == 0);
        two.InputTray2 = nullptr;
        two.NormalizeInputTrays();
        CHECK(two.InputTray2 && two.InputTray2->Name == "InputTray2" && two.InputTray2->Rows() == 3);

        // ── Hai tray đầu vào (TestTwoInputTrays phần tính): sổ nối Tray 2 sau Tray 1 ──
        auto m = ModelData::CreateSimulation();
        McDataManager::UseModel(m, false);
        auto run = McDataManager::RunData();
        auto in1 = m->InputTray;
        CHECK(m->InputTraysInUse() == 1 && run->InputTray->Trays == 1 && run->InputTray->Rows == in1->Rows() && run->InputTray->Cols == in1->Cols());
        CHECK(run->AssyTray->Rows == 3 && run->AssyTray->Cols == 4 && run->AssyTray->ScanOrder == EScanOrder::Serpentine && !McDataManager::ModelLoaded());
        auto in2 = m->InputTray2;
        CHECK(in2->Rows() == in1->Rows() && in2->Cols() == in1->Cols() && in2->ScanOrder == in1->ScanOrder && in2->AxisRow == in1->AxisRow
              && in2->Z_PickZ1 == in1->Z_PickZ1 && in2->Z_PickZ2 == in1->Z_PickZ2 && in2->Z_SlowZ1 == in1->Z_SlowZ1);
        const double shiftY = 60;
        auto shifted = [&](const TeachPoint& p) { return TeachPoint(p.X, p.Y + shiftY); };
        in2->GeoZ1.P00 = shifted(in1->GeoZ1.P00); in2->GeoZ1.P0C = shifted(in1->GeoZ1.P0C); in2->GeoZ1.PR0 = shifted(in1->GeoZ1.PR0);
        in2->GeoZ2.P00 = shifted(in1->GeoZ2.P00); in2->GeoZ2.P0C = shifted(in1->GeoZ2.P0C); in2->GeoZ2.PR0 = shifted(in1->GeoZ2.PR0);
        in2->Z_PickZ1 = in1->Z_PickZ1 + 0.5;
        McDataManager::SyncRunTrays();
        int n = run->InputTray->CellsPerTray();
        CHECK(m->InputTraysInUse() == 2 && m->UsableInputTrays() == 3 && run->InputTray->Trays == 2 && run->InputTray->Rows == 2 * in1->Rows()
              && n == in1->CellCount());
        int ta, ra, ca, tb, rb, cb;
        auto ga = m->InputCell(*run->InputTray, n - 1, ta, ra, ca);
        auto gb = m->InputCell(*run->InputTray, n, tb, rb, cb);
        CHECK(ta == 0 && ga == in1 && ra == in1->Rows() - 1 && tb == 1 && gb == in2 && rb == 0
              && std::fabs(gb->CellPos(Z1, rb, cb).Y - in1->CellPos(Z1, 0, cb).Y - shiftY) < 1e-9);

        // Operator đặt tray đầu vào mới: tray có mặt thành có hàng, tray vắng thành trống (LoadInputTrays).
        run->LoadInputTrays(2);
        int loaded = 0;
        for (int i = 0; i < run->InputTray->CellCount(); i++) if (run->InputTray->Get(i) == ECellState::Loaded) loaded++;
        CHECK(loaded == n && run->InputTray->Get(0) == ECellState::Empty && run->InputTray->Get(n) == ECellState::Loaded
              && run->InputTray->CurrentIndex == n);
        CHECK(!run->InputTrayNeeded(0) && run->InputTrayNeeded(1));
        run->InputTray->Set(2, ECellState::Processing);   // hàng đang trên tay có ô nguồn ở Tray 1
        CHECK(run->InputTrayNeeded(0));
        run->LoadInputTrays(0);
        CHECK(run->InputTray->CurrentIndex == -1 && !run->InputTrayNeeded(0) && !run->InputTrayNeeded(1));
        run->LoadInputTrays(3);
        CHECK(run->InputTray->CurrentIndex == 0 && run->InputTrayNeeded(0) && run->InputTrayNeeded(1));
        // Có chọn ô: chỉ ô đã chọn tính là "máy sẽ ghé".
        run->InputTray->SetSelected({ n + 1 });
        CHECK(!run->InputTrayNeeded(0) && run->InputTrayNeeded(1));
        run->InputTray->ClearSelection();

        // Lấy Tray 1 ra ở vị trí chờ (Tuấn 2026-10-30): ô Tray 1 trống, hàng trên tay từ Tray 1 mất ô nguồn, Tray 2 giữ nguyên;
        // Start thấy Tray 1 (tray mới) thì nạp lại — tray đã làm xong thì không.
        run->LoadInputTrays(3);
        run->InputTray->Set(1, ECellState::Ok);
        run->InputTray->Set(2, ECellState::Processing);
        run->InputTray->Set(n, ECellState::Ok);
        run->Nozzle1->SourceCellIndex = 2;
        run->Nozzle2->SourceCellIndex = n + 1;
        run->InputTray->CurrentIndex = 2;
        run->ClearInputTray(0);
        bool tray1Empty = true;
        for (int i = 0; i < n; i++) if (run->InputTray->Get(i) != ECellState::Empty) tray1Empty = false;
        CHECK(tray1Empty && run->InputTray->Get(n) == ECellState::Ok && run->InputTray->Get(n + 1) == ECellState::Loaded);
        CHECK(run->Nozzle1->SourceCellIndex == -1 && run->Nozzle2->SourceCellIndex == n + 1 && run->InputTray->CurrentIndex == n + 1);
        CHECK(!run->InputTrayNeeded(0) && run->InputTrayNeeded(1));
        CHECK(run->RefillEmptyInputTrays(2) == 0 && run->InputTray->Get(n) == ECellState::Ok);   // Tray 2 còn dở: giữ sổ
        CHECK(run->RefillEmptyInputTrays(3) == 1 && run->InputTray->Get(0) == ECellState::Loaded &&
              run->InputTray->Get(n - 1) == ECellState::Loaded && run->InputTray->Get(n) == ECellState::Ok);
        for (int i = n; i < 2 * n; i++) run->InputTray->Set(i, ECellState::Ok);
        CHECK(run->RefillEmptyInputTrays(3) == 0);   // Tray 2 làm xong (OK) nằm lại trên bàn: không thành tray mới
        run->Nozzle2->SourceCellIndex = -1;
        run->LoadInputTrays(3);

        // ── VisionCalib ──
        VisionCalib c;
        CHECK(!c.IsHandEyeMeasured(1) && !c.IsHandEyeOutdated(1) && !c.IsAlignVerified(1) && !c.IsUsable() && !c.IsTopDownVerified());
        auto now = std::chrono::system_clock::now();
        c.HandEye1MeasuredAt = now;            // bản ghi cũ: đã đo nhưng chưa có mốc góc
        CHECK(c.IsHandEyeOutdated(1) && !c.IsHandEyeMeasured(1));
        c.SetHandEye(1, 1.5, 2.5, 10.0, now);
        CHECK(c.IsHandEyeMeasured(1) && !c.IsHandEyeOutdated(1) && c.HandEye1YFixed && c.HandEyeAngleRef(1) == 10.0 && c.HandEyeAngleRef(2) == 0);
        c.SetHandEye(2, -1, -2, 20.0, now);
        c.SetAlignVerified(1, 1, 300, 0.001, now);
        CHECK(!c.IsUsable());
        c.SetAlignVerified(2, -1, 270, 0.002, now - std::chrono::hours(24 * 100));
        CHECK(c.IsUsable() && c.AlignAngleSign(2) == -1 && c.AlignVerifiedPosX(2) == 270);
        CHECK(c.IsExpired(90) && !c.IsExpired(120));   // quá hạn tính từ lần xác minh CŨ nhất
        double hx, hy, ha;
        CHECK(!c.TryGetHandEyeTarget(1, hx, hy, ha) && hx == 0 && !c.TryGetHandEyeMasterAxis(2, hx, hy));
        c.SetHandEyeTarget(2, 1, 2, 3, 4, 5);
        CHECK(c.TryGetHandEyeTarget(2, hx, hy, ha) && hx == 1 && hy == 2 && ha == 3 && c.TryGetHandEyeMasterAxis(2, hx, hy) && hx == 4 && hy == 5);
        c.TopDownVerifiedAt = now;
        CHECK(c.IsTopDownVerified());
    }

    void TestModelJson()
    {
        CHECK(!ModelData::FromJsonText("") && !ModelData::FromJsonText("null"));

        auto m = ModelData::FromJsonText(kModelJsonCs);
        CHECK(m != nullptr);
        if (!m) return;
        const char* Z1 = AxisId::Z1;
        CHECK(m->Name == "CS" && m->Description.empty() && m->InputTray->Rows() == 2 && m->InputTray->Cols() == 3
              && m->InputTray->ScanOrder == EScanOrder::Serpentine && m->AssyTray->ScanOrder == EScanOrder::Serpentine);
        auto off = m->InputTray->GetOffset(Z1, 0, 1);
        CHECK(off && off->Dx == 0.125 && off->Dy == -0.25 && !m->InputTray->GetOffset(Z1, 0, 0) && !m->InputTray->GetOffset(Z1, 1, 2));
        CHECK(m->InputTray->GeoZ2.Offsets.empty() && Near(m->InputTray->CellPos(Z1, 0, 1).X, 120.625) && m->InputTray->GeoZ1.PlaceDyAll == 0.05);
        CHECK(m->AssyTray->Cols() == 1 && m->AssyTray->IsTaught() && m->AssyTray->CamTopDownAnchorX == 330.125 && std::isinf(m->AssyTray->HalfColPitch()));
        // Thiếu khoá InputTray2 (model lưu trước khi có Tray 2) → giữ mặc định, chưa teach.
        CHECK(m->InputTray2 && m->InputTray2->Name == "InputTray2" && !m->InputTray2->IsTaught() && m->InputTraysInUse() == 1);
        CHECK(m->Calib.HandEye1AngleRef && *m->Calib.HandEye1AngleRef == 227.875 && !m->Calib.HandEye2AngleRef && m->Calib.HandEye1YFixed);
        CHECK(m->Calib.HandEye1MeasuredAt > TimePoint{} && m->Calib.HandEye2MeasuredAt == TimePoint{} && m->Calib.IsHandEyeMeasured(1)
              && !m->Calib.IsHandEyeMeasured(2) && m->Calib.IsAlignVerified(1) && !m->Calib.IsAlignVerified(2) && !m->Calib.IsUsable());
        CHECK(m->Calib.IsTopDownVerified() && m->Calib.AlignVerifyResidual1 == 0.0024);
        double hx, hy, ha;
        CHECK(m->Calib.TryGetHandEyeTarget(1, hx, hy, ha) && ha == 47.125 && !m->Calib.TryGetHandEyeTarget(2, hx, hy, ha)
              && !m->Calib.TryGetHandEyeMasterAxis(1, hx, hy));
        CHECK(m->PickAngleA1 == 0.40000000000000036 && m->AlignPosX(2) == 291.25 && m->HandEyePick(1).Taught && m->HandEyeCamPos.X == 330.125);
        std::string iso = ToIsoString(m->Calib.HandEye1MeasuredAt);
        TimePoint back{};
        CHECK(TryParseDateTime(iso, back) && back == m->Calib.HandEye1MeasuredAt);   // 100 ns giữ nguyên qua ISO

        // Ghi lại: khoá và thứ tự khoá như Newtonsoft; null bị bỏ; phần tử null trong mảng Offsets vẫn ghi null.
        std::string text = m->ToJsonText();
        json j = ParseText(text);
        CheckKeys(j, kModelKeysNoDescription, "model keys");
        CheckKeys(j["InputTray"], kTrayLayoutKeys, "InputTray keys");
        CheckKeys(j["InputTray"]["GeoZ1"], kGeoKeysWithOffsets, "GeoZ1 keys");
        CheckKeys(j["InputTray"]["GeoZ2"], kGeoKeysNoOffsets, "GeoZ2 keys");
        CheckKeys(j["InputTray"]["GeoZ1"]["P00"], { "X", "Y", "Taught" }, "TeachPoint keys");
        CHECK(j["InputTray"]["GeoZ1"]["Offsets"].size() == 2 && j["InputTray"]["GeoZ1"]["Offsets"][0][0].is_null()
              && j["InputTray"]["GeoZ1"]["Offsets"][0][1]["Dx"] == 0.125 && j["InputTray"]["GeoZ1"]["Offsets"][0][1]["HasTeachOffset"] == true);
        CheckKeys(j["InputTray"]["GeoZ1"]["Offsets"][0][1], { "Dx", "Dy", "PlaceDx", "PlaceDy", "PlaceDa", "HasTeachOffset", "HasPlaceTrim", "IsZero" },
                  "CellOffset keys");
        CHECK(j["AssyTray"]["HalfColPitch"] == "Infinity" && j["AssyTray"]["ScanOrder"] == 2 && j["InputTray"]["CellCount"] == 6
              && j["InputTray"]["HalfColPitch"] == 10.0 && j["InputTray"]["IsTaught"] == true);
        CHECK(j["InputTray2"]["Rows"] == 4 && j["InputTraysInUse"] == 1 && j["UsableInputTrays"] == 1);
        CheckKeys(j["Calib"], { "HandEye1Dx", "HandEye1Dy", "HandEye2Dx", "HandEye2Dy", "HandEye1MeasuredAt", "HandEye2MeasuredAt",
                                "HandEye1AngleRef", "HandEye1YFixed", "HandEye2YFixed", "HandEye1TopX", "HandEye1TopY", "HandEye1TopA",
                                "AlignAngleSign1", "AlignAngleSign2", "AlignVerifiedAt1", "AlignVerifiedAt2", "AlignVerifiedPosX1",
                                "AlignVerifiedPosX2", "AlignVerifyResidual1", "AlignVerifyResidual2", "TopDownVerifiedAt",
                                "IsTopDownVerified", "IsUsable" },
                  "Calib keys");
        CHECK(j["Calib"]["HandEye2MeasuredAt"] == "0001-01-01T00:00:00" && j["Calib"]["IsUsable"] == false && j["Calib"]["IsTopDownVerified"] == true);
        CHECK(text.find("FutureKey") == std::string::npos && text.find("\r\n") != std::string::npos);
        auto again = ModelData::FromJsonText(text);
        CHECK(again && again->ToJsonText() == text);
        m->Description = "mô tả";
        CHECK(ParseText(m->ToJsonText())["Description"] == "mô tả");

        // Z_SAFE và solution riêng của model: model C# chưa có → nullopt / rỗng, không ghi khoá; có thì ghi sau Calib.
        CHECK(!m->ZSafe && m->VisionSolutionPath.empty());
        m->Description.clear();
        m->ZSafe = 32.5;
        m->VisionSolutionPath = "D:\\Job\\Model A.solw";
        json mj = ParseText(m->ToJsonText());
        CheckKeys(mj, kModelKeysWithMachine, "model keys with Z_SAFE / solution");
        auto back2 = ModelData::FromJsonText(m->ToJsonText());
        CHECK(back2 && back2->ZSafe && *back2->ZSafe == 32.5 && back2->VisionSolutionPath == "D:\\Job\\Model A.solw");
        mj["ZSafe"] = nullptr;
        CHECK(!ModelData::FromJsonText(mj.dump())->ZSafe);
    }

    void TestModelManagement()
    {
        // ── Model mặc định khi thư mục Model trống ──
        McDataManager::LoadRecentModel();
        auto def = McDataManager::Model();
        CHECK(McDataManager::ModelLoaded() && def->Name == "DEFAULT" && McDataManager::RunData()->RecentModelName == "DEFAULT");
        CHECK((McDataManager::ListModels() == std::vector<std::string>{ "DEFAULT" }));
        auto run = McDataManager::RunData();
        CHECK(run->InputTray->Rows == 4 && run->InputTray->Cols == 5 && run->InputTray->Trays == 1 && run->AssyTray->Rows == 5
              && run->AssyTray->Cols == 6);
        json dj = ParseText(RawFile(AppPaths::ModelDir() / "DEFAULT.json"));
        CheckKeys(dj, kModelKeysWithMachine, "DEFAULT.json keys");
        CHECK(!dj["InputTray"]["GeoZ1"].contains("Offsets"));   // chưa có offset nào → khoá bị bỏ (null)
        // Model mới nhận Z_SAFE và solution máy đang dùng (system.json) và ghi luôn vào file.
        auto sys = McDataManager::SysData();
        const double sysZ = sys->ZSafe;
        const std::string sysPath = sys->VisionSolutionPath;
        CHECK(def->ZSafe && *def->ZSafe == sysZ && def->VisionSolutionPath == sysPath && dj["ZSafe"] == sysZ
              && dj["VisionSolutionPath"] == sysPath);

        // ── Tạo / sao chép / nạp / xoá (TestModelManagement) ──
        auto savedModel = McDataManager::Model();
        bool savedLoaded = McDataManager::ModelLoaded();
        std::string savedRecent = run->RecentModelName;
        const std::string nameA = "__SELFTEST_A__", nameB = "__SELFTEST_B__";

        auto model = std::make_shared<ModelData>();
        model->Name = nameA;
        model->Description = "model tạm của --selftest";
        model->HandEyePick_Nozzle1 = TeachPoint(123.456, 78.901);
        model->HandEyePick_Nozzle2 = TeachPoint(61.234, 79.012);
        model->HandEyeCamPos = TeachPoint(150.987, 90.123);
        model->AssyTray->SetOffset(AxisId::Z2, 1, 2, 0.5, -0.5);
        McDataManager::UseModel(model, true);
        CHECK(McDataManager::SaveModel(nameA));
        auto listed = McDataManager::ListModels();
        CHECK(std::find(listed.begin(), listed.end(), nameA) != listed.end());
        CHECK(McDataManager::CopyModel(nameA, nameB));
        CHECK(McDataManager::LoadModel(nameB));
        auto re = McDataManager::Model();
        CHECK(re->Name == nameB && re->Description == model->Description);
        CHECK(re->HandEyePick_Nozzle1.Taught && re->HandEyePick_Nozzle2.Taught && re->HandEyeCamPos.Taught
              && Near(re->HandEyePick_Nozzle1.X, 123.456) && Near(re->HandEyePick_Nozzle1.Y, 78.901) && Near(re->HandEyePick_Nozzle2.X, 61.234)
              && Near(re->HandEyeCamPos.X, 150.987) && Near(re->HandEyeCamPos.Y, 90.123));
        CHECK(!Near(re->HandEyePick_Nozzle1.X, re->HandEyePick_Nozzle2.X));
        auto reOff = re->AssyTray->GetOffset(AxisId::Z2, 1, 2);
        CHECK(reOff && reOff->Dx == 0.5 && reOff->Dy == -0.5 && !re->AssyTray->GetOffset(AxisId::Z2, 0, 0));

        // ── Z_SAFE và solution theo model (Tuấn 2026-11-07) ──
        // Nạp model có giá trị riêng: máy (SystemData) theo model.
        re->ZSafe = 33.25;
        re->VisionSolutionPath = "B.solw";
        CHECK(McDataManager::SaveModel());
        McDataManager::UseModel(savedModel, savedLoaded);
        CHECK(sys->ZSafe == sysZ && sys->VisionSolutionPath == sysPath);
        CHECK(McDataManager::LoadModel(nameB));
        CHECK(sys->ZSafe == 33.25 && sys->VisionSolutionPath == "B.solw");
        // Trang Setting lưu: model, máy và CHỈ hai khoá đó trên đĩa — chỗ sửa dở chưa lưu của Teaching không lọt vào file.
        McDataManager::Model()->Description = "unsaved teach edit";
        CHECK(McDataManager::SaveModelMachineValues(41.5, "B2.solw"));
        CHECK(*McDataManager::Model()->ZSafe == 41.5 && sys->ZSafe == 41.5 && sys->VisionSolutionPath == "B2.solw");
        json bj = ParseText(RawFile(AppPaths::ModelDir() / (nameB + ".json")));
        CHECK(bj["ZSafe"] == 41.5 && bj["VisionSolutionPath"] == "B2.solw" && bj["Description"] == model->Description);
        // Model lưu trước khi có hai khoá: lần nạp đầu nhận giá trị máy đang dùng rồi ghi vào file.
        json oldJson = bj;
        oldJson.erase("ZSafe");
        oldJson.erase("VisionSolutionPath");
        oldJson["Name"] = nameA;
        base::WriteAllText(AppPaths::ModelDir() / (nameA + "_OLD.json"), oldJson.dump(2));
        sys->ZSafe = 27.0;
        sys->VisionSolutionPath = "Machine.solw";
        CHECK(McDataManager::LoadModel(nameA + "_OLD"));
        json oj = ParseText(RawFile(AppPaths::ModelDir() / (nameA + "_OLD.json")));
        CHECK(*McDataManager::Model()->ZSafe == 27.0 && McDataManager::Model()->VisionSolutionPath == "Machine.solw"
              && oj["ZSafe"] == 27.0 && oj["VisionSolutionPath"] == "Machine.solw");
        CHECK(McDataManager::DeleteModel(nameA + "_OLD"));
        McDataManager::LoadModel(nameB);
        CHECK(McDataManager::DeleteModel(nameA));
        listed = McDataManager::ListModels();
        CHECK(std::find(listed.begin(), listed.end(), nameA) == listed.end() && std::find(listed.begin(), listed.end(), nameB) != listed.end());
        CHECK(McDataManager::DeleteModel(nameB) && McDataManager::ListModels().size() == 1);
        CHECK(McDataManager::Model()->Name == nameB);   // xoá trên đĩa không đụng model đang nạp

        // Nạp model không có trên đĩa → alarm 6001 (sự kiện), model đang dùng giữ nguyên.
        AlarmManager::ForceClearAll();
        CHECK(!McDataManager::LoadModel("__NO_SUCH__") && AlarmManager::IsActive(AlarmCode::ModelFileCorrupt)
              && !AlarmManager::Get(AlarmCode::ModelFileCorrupt)->ConditionActive && McDataManager::Model()->Name == nameB);
        AlarmManager::ForceClearAll();

        McDataManager::UseModel(savedModel, savedLoaded);
        McDataManager::RunData()->RecentModelName = savedRecent;
        CHECK(McDataManager::RunData()->RecentModelName == savedRecent && McDataManager::Model() == savedModel);
    }

    // ───────────────────────── RunningData ─────────────────────────

    /// <summary>Danh sách dòng của dashboard khởi động: tính phần trăm theo trọng số, lỗi, khoá lạ.</summary>
    void TestStartupProgress()
    {
        auto find = [](std::string_view key) {
            for (const StartupStep& step : StartupProgress::Snapshot())
                if (step.Key == key) return step;
            return StartupStep{};
        };

        StartupProgress::Reset({ { "G", "A", "addr", 10 }, { "G", "B", "", 30 }, { "G", StartupProgress::CamViews, "", 60 } });
        auto summary = StartupProgress::Summarize(StartupProgress::Snapshot());
        CHECK(summary.Percent == 0 && summary.Failed == 0 && !summary.AllEnded);

        // Khoá lạ bỏ qua: đường kết nối còn gọi vào đây sau khởi động và trong kiểm thử.
        StartupProgress::Begin("Unknown");
        StartupProgress::End("Unknown", false, "x");
        CHECK(StartupProgress::Summarize(StartupProgress::Snapshot()).Failed == 0);

        StartupProgress::Begin("A");
        CHECK(find("A").State == EStartupState::Running);
        CHECK(StartupProgress::Summarize(StartupProgress::Snapshot()).Percent == 0);
        StartupProgress::End("A", true);
        CHECK(find("A").State == EStartupState::Ok && find("A").Detail == "addr");   // detail rỗng thì giữ chữ cũ
        CHECK(StartupProgress::Summarize(StartupProgress::Snapshot()).Percent == 10);

        StartupProgress::End("B", false, "port busy");
        summary = StartupProgress::Summarize(StartupProgress::Snapshot());
        CHECK(find("B").State == EStartupState::Failed && find("B").Detail == "port busy");
        CHECK(summary.Percent == 40 && summary.Failed == 1 && !summary.AllEnded);

        StartupProgress::End(StartupProgress::CamViews, true, "3 of 3 bound");
        summary = StartupProgress::Summarize(StartupProgress::Snapshot());
        CHECK(summary.Percent == 100 && summary.AllEnded && summary.Failed == 1);

        // Init ném exception: dòng của Core thành lỗi để dashboard khỏi chờ; dòng CAM views giữ nguyên, UI vẫn tự báo.
        StartupProgress::Reset({ { "G", "A" }, { "G", "B" }, { "G", StartupProgress::CamViews } });
        StartupProgress::End("A", true);
        StartupProgress::Begin("B");
        StartupProgress::FailUnfinished("boom");
        CHECK(find("A").State == EStartupState::Ok);
        CHECK(find("B").State == EStartupState::Failed && find("B").Detail == "boom");
        CHECK(find(StartupProgress::CamViews).State == EStartupState::Pending);

        CHECK(!StartupProgress::Summarize({}).AllEnded);
        StartupProgress::Reset({});
    }

    void TestRunningData()
    {
        // ── Cycle time trung bình CycleAvgWindow sản phẩm (TestAutoCycle) ──
        const int win = RunningData::CycleAvgWindow;
        CHECK(win == 40);
        RunningData ct;
        for (int i = 0; i < win / 2 + 1; i++) ct.RecordCycle(20, 2);
        ct.RecordCycle(30, 1);
        ct.RecordCycle(5, 0);   // lượt không ra sản phẩm nào: bỏ qua
        ct.RecordCycle(0.005, 1);
        CHECK((int)ct.RecentProductTimes.size() == win && std::fabs(ct.LastProductTimeSec - 30) < 1e-9
              && std::fabs(ct.AvgProductTimeSec - ((win - 1) * 10 + 30) / (double)win) < 1e-9);
        // Bộ đếm theo tray: nạp lại tray không xoá, chỉ Clear sản lượng mới xoá.
        ct.InputCountOk = 5; ct.InputCountNg = 1; ct.AssyCountOk = 5; ct.AssyCountNg = 2;
        ct.CountOk = 9; ct.CountNg = 1; ct.CountTotal = 10; ct.LastCycleTimeSec = 3;
        CHECK(Near(ct.NgRatePercent(), 10));
        ct.InputTray->ResetAll(ECellState::Loaded);
        ct.AssyTray->ResetAll(ECellState::Empty);
        CHECK(ct.InputCountOk == 5 && ct.InputCountNg == 1 && ct.AssyCountOk == 5 && ct.AssyCountNg == 2);

        // ── Đồng hồ tray lắp ráp (Tuấn 2026-10-08 / 2026-10-17) ──
        auto t0 = std::chrono::system_clock::now();
        ct.StartAssyTrayClock(t0);
        ct.StartAssyTrayClock(t0 + std::chrono::seconds(3));   // đã có mốc → không đổi
        CHECK(ct.AssyTrayStartedAt && *ct.AssyTrayStartedAt == t0 && !ct.AssyTrayDone);
        ct.FinishAssyTray(t0 + std::chrono::seconds(10), 4);
        CHECK(ct.AssyTrayDone && Near(ct.AssyTrayDoneSec, 10) && ct.AssyTrayDoneProducts == 4 && !ct.AssyTrayStartedAt
              && Near(ct.AssyTrayDonePerProductSec(), 2.5) && ct.LastAssyTrayTimeSec == 0);
        ct.FinishAssyTray(t0 + std::chrono::seconds(20), 9);   // chưa có mốc → bỏ qua
        CHECK(ct.AssyTrayDoneProducts == 4);
        ct.RestartAssyTrayClock(t0 + std::chrono::seconds(30));   // tray mới: số chốt chuyển xuống Last
        CHECK(!ct.AssyTrayDone && ct.AssyTrayDoneSec == 0 && Near(ct.LastAssyTrayTimeSec, 10) && ct.LastAssyTrayProducts == 4
              && Near(ct.LastAssyTrayPerProductSec(), 2.5) && ct.AssyTrayStartedAt && *ct.AssyTrayStartedAt == t0 + std::chrono::seconds(30));
        ct.FinishAssyTray(t0 + std::chrono::seconds(25), 2);   // đồng hồ lùi → 0, không âm
        CHECK(ct.AssyTrayDoneSec == 0 && ct.AssyTrayDone);
        ct.StartAssyTrayClock(t0 + std::chrono::seconds(40));
        ct.ResetCounters();   // không đụng mốc tray đang làm
        CHECK(ct.RecentProductTimes.empty() && ct.AvgProductTimeSec == 0 && ct.LastProductTimeSec == 0 && ct.LastCycleTimeSec == 0);
        CHECK(ct.InputCountOk == 0 && ct.InputCountNg == 0 && ct.AssyCountOk == 0 && ct.AssyCountNg == 0 && ct.CountTotal == 0 && ct.NgRatePercent() == 0);
        CHECK(ct.LastAssyTrayTimeSec == 0 && ct.LastAssyTrayProducts == 0 && !ct.AssyTrayDone && ct.AssyTrayStartedAt.has_value());

        // ── ResetCycleProgress: sổ tray / tay hút làm lại từ đầu, bộ đếm và NozzleClearRequired giữ ──
        RunningData rp;
        rp.AutoState = EAutoState::S31_Place1;
        rp.UnsafePause = true;
        rp.NozzleClearRequired = true;
        rp.CountOk = 3;
        rp.InputTray->Resize(4, 2);
        rp.InputTray->Trays = 2;
        rp.InputTray->ResetAll();
        rp.InputTray->ScanOrder = EScanOrder::Serpentine;
        rp.InputTray->ToggleSelected(1);
        rp.Nozzle1->HasProduct = true;
        rp.AssyTrayStartedAt = std::chrono::system_clock::now();
        auto oldIn = rp.InputTray;
        auto oldN1 = rp.Nozzle1;
        rp.ResetCycleProgress();
        CHECK(rp.AutoState == EAutoState::S00_Ready && !rp.UnsafePause && rp.NozzleClearRequired && rp.CountOk == 3 && !rp.AssyTrayStartedAt);
        CHECK(rp.InputTray != oldIn && rp.InputTray->Rows == 4 && rp.InputTray->Cols == 2 && rp.InputTray->Trays == 2
              && rp.InputTray->Get(0) == ECellState::Empty && !rp.InputTray->HasSelection() && rp.InputTray->ScanOrder == EScanOrder::RowMajor);
        CHECK(rp.Nozzle1 != oldN1 && !rp.Nozzle1->HasProduct && rp.Nozzle1->Index == 1 && rp.Nozzle2->Index == 2 && rp.Nozzle2->SourceCellIndex == -1);
        CHECK(oldIn->Get(0) == ECellState::Loaded && oldN1->HasProduct);   // nơi đang giữ bản cũ không bị đổi dưới chân

        // ── running.json dạng C# ──
        CHECK(!RunningData::FromJsonText("") && !RunningData::FromJsonText("null"));
        auto r = RunningData::FromJsonText(kRunningJsonCs);
        CHECK(r != nullptr);
        if (!r) return;
        CHECK(r->RecentModelName == "CS" && r->SavedAt > TimePoint{} && r->AutoState == EAutoState::S05_ScanAssy && r->UnsafePause
              && r->NozzleClearRequired && r->NozzleClearReason == "test reason");
        CHECK(r->InputTray->Rows == 4 && r->InputTray->Cols == 2 && r->InputTray->Cells.size() == 8 && r->InputTray->Get(3) == ECellState::Ng
              && r->InputTray->Get(7) == ECellState::Skipped && r->InputTray->CurrentIndex == 3 && r->InputTray->Trays == 2
              && r->InputTray->ScanOrder == EScanOrder::Serpentine && (Sorted(r->InputTray->Selected()) == std::vector<int>{ 1, 6 }));
        CHECK(r->AssyTray->Get(5) == ECellState::Skipped && !r->AssyTray->HasSelection());
        CHECK(r->Nozzle1->HasProduct && r->Nozzle1->SourceCellIndex == 3 && r->Nozzle1->MeasuredAngle == -179.54692077636719 && r->Nozzle1->Aligned
              && !r->Nozzle2->HasProduct);
        CHECK(r->CountOk == 15 && r->CountNg == 1 && r->CountTotal == 16 && r->AssyCountNg == 6 && r->LastCycleTimeSec == 4.8980866999999995
              && r->RecentProductTimes.size() == 2 && r->RecentProductTimes[1] == 3.9803672999999997);
        CHECK(r->AssyTrayStartedAt && r->AssyTrayDone && r->AssyTrayDoneProducts == 2 && r->LastAssyTrayTimeSec == 10.5009569
              && Near(r->NgRatePercent(), 6.25));

        std::string text = r->ToJsonText();
        json j = ParseText(text);
        CheckKeys(j, kRunKeys, "running.json keys");
        CheckKeys(j["InputTray"], kTrayStateKeys, "TrayState keys");
        CheckKeys(j["Nozzle1"], kNozzleKeys, "NozzleState keys");
        CHECK(j["AutoState"] == 5 && j["InputTray"]["Cells"][3] == 3 && j["InputTray"]["RowsPerTray"] == 2 && j["InputTray"]["HasSelection"] == true
              && j["NgRatePercent"] == 6.25);
        auto again = RunningData::FromJsonText(text);
        CHECK(again && again->ToJsonText() == text);
        // null bỏ khoá: RecentModelName / NozzleClearReason rỗng, AssyTrayStartedAt chưa có.
        RunningData blank;
        json bj = ParseText(blank.ToJsonText());
        CHECK(!bj.contains("RecentModelName") && !bj.contains("NozzleClearReason") && !bj.contains("AssyTrayStartedAt")
              && bj["SavedAt"] == "0001-01-01T00:00:00" && bj["InputTray"]["Rows"] == 4 && bj["AssyTray"]["Cols"] == 6);

        // ── Nạp running.json: tiến độ chu trình luôn làm lại từ đầu, bộ đếm và model gần nhất giữ ──
        base::WriteAllText(AppPaths::RunningFile(), std::string("\xEF\xBB\xBF") + kRunningJsonCs);
        McDataManager::LoadRunningData();
        auto run = McDataManager::RunData();
        CHECK(run->CountOk == 15 && run->RecentModelName == "CS" && run->AutoState == EAutoState::S00_Ready && !run->UnsafePause
              && run->NozzleClearRequired && run->AssyTrayDone && !run->AssyTrayStartedAt);
        CHECK(run->InputTray->Rows == 4 && run->InputTray->Cols == 2 && run->InputTray->Trays == 2 && run->InputTray->Get(0) == ECellState::Empty
              && !run->InputTray->HasSelection() && !run->Nozzle1->HasProduct);
        run->RecordCycle(10.0, 4);
        CHECK(McDataManager::SaveRunningData());
        auto savedAt = run->SavedAt;
        std::string raw = RawFile(AppPaths::RunningFile());
        CHECK(base::StartsWith(raw, "\xEF\xBB\xBF{\r\n"));
        McDataManager::LoadRunningData();
        auto re = McDataManager::RunData();
        CHECK(re != run && re->RecentProductTimes.size() == 6 && Near(re->LastProductTimeSec, 2.5) && re->SavedAt == savedAt && re->CountOk == 15);

        // running.json hỏng → bản mới, không alarm (chỉ log).
        base::WriteAllText(AppPaths::RunningFile(), "{ broken");
        McDataManager::LoadRunningData();
        CHECK(McDataManager::RunData()->CountOk == 0 && McDataManager::RunData()->RecentModelName.empty());
    }

    // ───────────────────────── Alarm ─────────────────────────

    /// <summary>Cập nhật qua GitHub (Tuấn 2026-11-07): so số phiên bản, đọc trang releases/latest, SHA-256 của file tải về.
    /// Phần mạng (FetchLatest / Download) không kiểm ở đây.</summary>
    void TestAppUpdate(const fs::path& root)
    {
        CHECK(AppUpdate::CompareVersions("2026.11.8", "2026.11.7") > 0);
        CHECK(AppUpdate::CompareVersions("v2026.11.7", "2026.11.7.0") == 0);
        CHECK(AppUpdate::CompareVersions("2026.11.7.2", "2026.11.7") > 0);
        CHECK(AppUpdate::CompareVersions("2026.11.30", "2026.12.1") < 0);   // so số, không so chữ
        CHECK(AppUpdate::CompareVersions("2027.1.1", "2026.12.31") > 0);
        std::string current = AppUpdate::CurrentVersion();
        CHECK_MSG(!current.empty() && AppUpdate::CompareVersions(current, "2026.11.7") >= 0, current);

        // GitHub có trường digest: lấy luôn SHA-256 ở đó.
        const char* withDigest = R"({
          "tag_name": "v2026.11.8", "name": "ShieldCanAssy 2026.11.8", "body": "Fix scan\r\nFaster origin",
          "published_at": "2026-11-08T03:04:05Z",
          "assets": [
            { "name": "ShieldCanAssy-2026.11.8.zip.sha256", "size": 90,
              "browser_download_url": "https://github.com/x/y/releases/download/v2026.11.8/ShieldCanAssy-2026.11.8.zip.sha256" },
            { "name": "ShieldCanAssy-2026.11.8.zip", "size": 41234567, "digest": "sha256:ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef0123456789",
              "browser_download_url": "https://github.com/x/y/releases/download/v2026.11.8/ShieldCanAssy-2026.11.8.zip" } ] })";
        ReleaseInfo r;
        std::string error;
        CHECK_MSG(AppUpdate::ParseLatestRelease(withDigest, r, error), error);
        CHECK(r.Tag == "v2026.11.8" && r.Version == "2026.11.8" && r.AssetName == "ShieldCanAssy-2026.11.8.zip" && r.AssetSize == 41234567);
        CHECK(r.Sha256 == "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789" && base::EndsWith(r.ShaUrl, ".zip.sha256"));
        CHECK(r.PublishedAt.substr(0, 10) == "2026-11-08" && base::Contains(r.Notes, "Faster origin"));

        // Không có digest: đọc file .sha256 đi kèm.
        json noDigest = json::parse(withDigest);
        noDigest["assets"][1].erase("digest");
        CHECK_MSG(AppUpdate::ParseLatestRelease(noDigest.dump(), r, error) && r.Sha256.empty() && !r.ShaUrl.empty(), error);
        // Không có gì để kiểm SHA-256 → từ chối, không tải file không kiểm được.
        json noCheck = noDigest;
        noCheck["assets"].erase(0);
        CHECK(!AppUpdate::ParseLatestRelease(noCheck.dump(), r, error) && base::Contains(error, "SHA-256"));
        // Release không có zip của App, JSON hỏng.
        CHECK(!AppUpdate::ParseLatestRelease(R"({"tag_name":"v1","assets":[{"name":"notes.txt"}]})", r, error));
        CHECK(!AppUpdate::ParseLatestRelease("{ not json", r, error) && !AppUpdate::ParseLatestRelease("", r, error));

        // SHA-256: mẫu chuẩn FIPS 180-2 ("abc") và file rỗng.
        fs::path abc = root / "sha-abc.bin";
        base::WriteAllText(abc, "abc");
        CHECK(AppUpdate::Sha256File(abc) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        fs::path empty = root / "sha-empty.bin";
        base::WriteAllText(empty, "");
        CHECK(AppUpdate::Sha256File(empty) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        CHECK(AppUpdate::Sha256File(root / "no-such-file.bin").empty());
    }

    void TestAlarmCatalog(const fs::path& root)
    {
        // Bản mẫu thật của repo (C:\PGM\ShieldCANAssyMC\Config\alarms.json) đặt cạnh "exe" giả trong thư mục tạm.
        fs::path realAlarms = fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "Config" / "alarms.json";
        fs::path exeDir = root / "exe";
        fs::create_directories(exeDir / "Config");
        std::string tplText;
        CHECK_MSG(base::ReadAllText(realAlarms, tplText), base::PathToU8(realAlarms));
        base::WriteAllText(exeDir / "Config" / "alarms.json", tplText);
        AppPaths::SetExeDirForTests(exeDir);
        std::error_code ec;
        fs::remove(AppPaths::AlarmsFile(), ec);
        AlarmCatalog::Load();
        std::string copied;
        CHECK(AlarmCatalog::Loaded() && base::ReadAllText(AppPaths::AlarmsFile(), copied) && copied == tplText);
        CHECK(base::StartsWith(RawFile(AppPaths::AlarmsFile()), "\xEF\xBB\xBF"));   // bản trên máy LUÔN có BOM
        for (int code : { 1000, 1001, 1002, 1003, 1099, 1100, 1103, 1105, 3000, 3001, 4000, 4001, 4002, 4003, 4004, 4005, 4006,
                          4010, 4011, 4090, 5001, 5002, 5003, 5004, 5005, 5010, 5020, 5030, 5031, 6000, 6001, 6002 })
        {
            auto d = AlarmCatalog::Get(code);
            CHECK_MSG(d.Vi.find("chưa định nghĩa:") == std::string::npos && !d.Remedy.empty() && !d.En.empty() && !d.CauseEn.empty()
                      && !d.RemedyEn.empty(), std::to_string(code));
        }
        CHECK(AlarmCatalog::Get(1100).Vi == "DỪNG KHẨN CẤP (EMG)" && AlarmCatalog::Get(1100).En == "EMG stop" && AlarmCatalog::Get(1100).Level == EAlarmLevel::Fatal);
        // Mã 2105 tra ra đúng trục và loại lỗi (TestStartupState) — mẫu axisFaults, {AXIS} thay bằng tên trục.
        auto z1home = AlarmCatalog::Get(2105);
        CHECK(z1home.Vi == "Trục Z1: về gốc thất bại" && z1home.En == "Axis Z1: home failed" && z1home.Level == EAlarmLevel::Error
              && z1home.RemedyEn.find("Z1") != std::string::npos);
        CHECK(AlarmCatalog::Get(2304).Level == EAlarmLevel::Warning && AlarmCatalog::Get(2304).En == "Axis Y1: hard limit");
        CHECK(AlarmCatalog::Get(7777).Vi == "Alarm chưa định nghĩa: 7777" && AlarmCatalog::Get(7777).En == "Undefined alarm: 7777"
              && AlarmCatalog::Get(7777).Level == EAlarmLevel::Error && !AlarmCatalog::Get(7777).CauseEn.empty());
        CHECK(AlarmCatalog::Get(2700).En == "Undefined alarm: 2700");   // ngoài khối trục 2000–2699

        // File của người dùng: lời văn giữ, level lấy theo phần mềm, ô tiếng Anh TRỐNG được điền, mã thiếu được bổ sung.
        base::WriteAllText(AppPaths::AlarmsFile(),
            "{ \"codes\": { \"1100\": { \"level\": 1, \"vi\": \"EMG của tôi\", \"en\": \"\", \"cause\": \"c\", \"remedy\": \"r\", \"remedyEn\": \"my remedy\" },"
            " \"9\": { \"level\": \"info\", \"vi\": \"riêng\" } } }");
        AlarmCatalog::Load();
        auto mine = AlarmCatalog::Get(1100);
        CHECK(mine.Vi == "EMG của tôi" && mine.Level == EAlarmLevel::Fatal && mine.En == "EMG stop" && mine.Cause == "c" && mine.RemedyEn == "my remedy"
              && !mine.CauseEn.empty());
        CHECK(AlarmCatalog::Get(9).Vi == "riêng" && AlarmCatalog::Get(9).Level == EAlarmLevel::Info && AlarmCatalog::Get(6002).Level == EAlarmLevel::Warning
              && AlarmCatalog::Get(2000).En == "Axis X: comm lost");
        base::ReadAllText(AppPaths::AlarmsFile(), copied);
        json aj = json::parse(copied);
        CheckKeys(aj, { "_comment", "axisFaults", "codes" }, "alarms.json keys");
        CheckKeys(aj["codes"]["1100"], { "level", "vi", "en", "cause", "causeEn", "remedy", "remedyEn" }, "AlarmDefinition keys");
        CHECK(aj["codes"]["1100"]["level"] == "Fatal" && aj["_comment"].is_null() && copied.find("\"9\"") < copied.find("\"1000\"")
              && aj["codes"]["9"]["cause"].is_null());

        // Thiếu file và không có bản mẫu → không nạp được, Get vẫn trả định nghĩa "chưa định nghĩa".
        fs::path noTpl = root / "exe_none";
        fs::create_directories(noTpl);
        AppPaths::SetExeDirForTests(noTpl);
        fs::remove(AppPaths::AlarmsFile(), ec);
        AlarmCatalog::Load();
        CHECK(!AlarmCatalog::Loaded() && AlarmCatalog::Get(1100).En == "Undefined alarm: 1100");

        // Nạp lại bản mẫu cho các phần sau.
        AppPaths::SetExeDirForTests(exeDir);
        AlarmCatalog::Load();
        CHECK(AlarmCatalog::Loaded() && AlarmCatalog::Get(1103).En == "Air pressure low");
    }

    void TestAlarmManager()
    {
        // ── Phân cấp alarm và lỗi gốc (TestResetRules) ──
        AlarmManager::ForceClearAll();
        AlarmManager::Raise(AlarmCode::ForAxis(AxisId::X, AxisFault::Disconnected), AxisId::X);
        base::Delay(20);
        AlarmManager::Raise(AlarmCode::ForAxis(AxisId::X, AxisFault::MoveTimeout), AxisId::X);
        base::Delay(20);
        AlarmManager::Raise(AlarmCode::ProductDropped, "Auto");
        auto root = AlarmManager::RootCause();
        CHECK(root && root->Code == 2000 && root->IsRootCause);
        int rootCount = 0;
        for (int c : { 2000, 2002, 5002 })
            if (auto a = AlarmManager::Get(c); a && a->IsRootCause) rootCount++;
        CHECK(rootCount == 1);
        CHECK(AlarmManager::Get(2000)->Level == EAlarmLevel::Fatal && AlarmManager::Get(5002)->Level == EAlarmLevel::Error && AlarmManager::HasFatal());

        // Hết nguyên nhân thì alarm mức nào cũng nằm lại chờ RESET, kể cả Warning (Tuấn chốt 2026-09-26).
        AlarmManager::Raise(AlarmCode::TrayEmpty, "Auto");
        CHECK(AlarmManager::Get(5020)->Level == EAlarmLevel::Warning && AlarmManager::HasWarning());
        auto pending = AlarmManager::PendingReset();
        CHECK(!pending || pending->Code != AlarmCode::TrayEmpty);
        AlarmManager::ClearCondition(AlarmCode::TrayEmpty);
        CHECK(AlarmManager::IsActive(AlarmCode::TrayEmpty) && AlarmManager::PendingReset());
        AlarmManager::ClearCondition(2000);
        auto stillStuck = AlarmManager::Reset();
        CHECK(!AlarmManager::IsActive(2000) && AlarmManager::IsActive(2002) && !AlarmManager::IsActive(AlarmCode::TrayEmpty));
        CHECK(stillStuck.size() == 2);   // 2002 và 5002 còn nguyên nhân
        AlarmManager::ForceClearAll();
        CHECK(AlarmManager::ActiveCount() == 0 && !AlarmManager::HasError());

        // ── Alarm sự kiện một lần không được kẹt RESET vĩnh viễn (TestEventAlarmsAlwaysResettable) ──
        int code = AlarmCode::ForAxis(AxisId::Z1, AxisFault::HomeTimeout);
        AlarmData ev = AlarmManager::RaiseAxisEvent(AxisId::Z1, AxisFault::HomeTimeout, "test");
        CHECK(AlarmManager::IsActive(code) && AlarmManager::Get(code)->Level == EAlarmLevel::Error && !AlarmManager::Get(code)->ConditionActive
              && !ev.ConditionActive && ev.Source == "Z1");
        CHECK(AlarmManager::HasError());
        auto stuck = AlarmManager::Reset();
        CHECK(!AlarmManager::IsActive(code) && stuck.empty());

        // ── Raise lặp, Detail, Version, sự kiện, lịch sử, Dismiss, nguồn suy từ mã, chữ hiển thị ──
        AlarmManager::ForceClearAll();
        int raisedEvents = 0;
        int token = AlarmManager::AlarmRaised().Subscribe([&](const AlarmData&) { ++raisedEvents; });
        size_t hist0 = AlarmManager::History().size();
        uint64_t v0 = AlarmManager::Version();
        AlarmData a = AlarmManager::Raise(AlarmCode::AirPressureLow);
        CHECK(a.Source == "Safety" && a.Level == EAlarmLevel::Error && a.IsRootCause && a.ConditionActive && AlarmManager::Version() > v0);
        CHECK(a.MessageVi == "Áp suất khí thấp" && a.Message() == "Air pressure low" && a.Display() == "[1103] Air pressure low"
              && !a.CauseEn.empty() && a.RemedyText() == a.RemedyEn && a.CauseText() == a.CauseEn);
        AlarmData b = AlarmManager::RaiseAxis(AxisId::Y1, AxisFault::HardLimit, "POT");
        CHECK(b.Code == 2304 && b.Source == "Y1" && !b.IsRootCause && b.Display() == "[2304] Axis Y1: hard limit — POT");
        uint64_t v1 = AlarmManager::Version();
        AlarmManager::Raise(AlarmCode::AirPressureLow);   // raise lại: không nhân bản, không đổi gì
        CHECK(raisedEvents == 2 && AlarmManager::ActiveCount() == 2 && AlarmManager::History().size() == hist0 + 2 && AlarmManager::Version() == v1);
        AlarmManager::Raise(AlarmCode::AirPressureLow, {}, "0.3 MPa");
        CHECK(AlarmManager::Get(1103)->Detail == "0.3 MPa" && AlarmManager::Version() > v1);
        CHECK(AlarmManager::HasError() && !AlarmManager::HasFatal() && AlarmManager::HasWarning() && AlarmManager::RootCause()->Code == 1103);
        CHECK(!AlarmManager::PendingReset() && AlarmManager::Reset().size() == 2 && AlarmManager::ActiveCount() == 2);
        AlarmManager::ClearCondition(1103);
        CHECK(AlarmManager::IsActive(1103) && AlarmManager::PendingReset()->Code == 1103);
        auto st = AlarmManager::Reset();
        CHECK(st.size() == 1 && st[0].Code == 2304 && !AlarmManager::IsActive(1103));
        auto hist = AlarmManager::History();
        CHECK(hist.size() >= 2 && hist[hist.size() - 2].Code == 1103 && hist[hist.size() - 2].ClearedAt.has_value());
        AlarmManager::Dismiss(2304);   // Warning máy tự chạy tiếp được: gỡ không chờ RESET, vẫn còn trong lịch sử
        CHECK(AlarmManager::ActiveCount() == 0 && !AlarmManager::Get(2304) && AlarmManager::History().back().ClearedAt.has_value());
        CHECK(AlarmManager::Raise(3000).Source == "IO" && AlarmManager::Raise(4004).Source == "Vision" && AlarmManager::Raise(5001).Source == "Auto"
              && AlarmManager::Raise(6000).Source == "Data" && AlarmManager::Raise(1002).Source == "System" && AlarmManager::Raise(2600).Source == "A2");
        AlarmManager::AlarmRaised().Unsubscribe(token);
        AlarmManager::ForceClearAll();
        AlarmManager::SetMaxHistory(3);
        for (int c : { 4001, 4002, 4003, 4005 }) AlarmManager::Raise(c);
        CHECK(AlarmManager::History().size() == 3 && AlarmManager::History().front().Code == 4002 && AlarmManager::MaxHistory() == 3);
        AlarmManager::SetMaxHistory(200);
        AlarmManager::ForceClearAll();

        // ── alarm.csv: dòng ghi MessageVi như bản C# ──
        base::WaitBackgroundIdle(3000);
        AlarmManager::FlushCsv();
        std::string csv;
        fs::path day = AppPaths::LogDir() / base::NowString("yyyyMMdd");
        CHECK(base::ReadAllText(day / "alarm.csv", csv) && base::StartsWith(csv, "RaisedAt,ClearedAt,Code,Level,Source,RootCause,Message,Detail\r\n"));
        CHECK(csv.find(",1103,Error,Safety,ROOT,Áp suất khí thấp,\r\n") != std::string::npos
              && csv.find(",2304,Warning,Y1,,Trục Y1: chạm hard limit (POT/NOT),POT\r\n") != std::string::npos);
    }

    // ───────────────────────── LogWorker / UiThread / SingleInstanceGuard ─────────────────────────

    void TestLogAndThreads()
    {
        uint64_t lv = LogWorker::Instance().Version();
        LogWorker::Instance().RecordLog("bỏ qua", LogLevel::Debug);
        LogWorker::Instance().RecordLog("Dòng\tthử\r\ntiếng Việt", LogLevel::Warning, "Test");
        LogWorker::Instance().Flush();
        auto list = LogWorker::Instance().LogList();
        CHECK(LogWorker::Instance().Version() == lv + 1 && !list.empty() && list[0].Message == "Dòng\tthử\r\ntiếng Việt" && list[0].Source == "Test");
        fs::path day = AppPaths::LogDir() / base::NowString("yyyyMMdd");
        std::string raw = RawFile(day / "app.log");
        std::string expect = FormatLocal(list[0].UpdateTime, "yyyy-MM-dd HH:mm:ss.fff") + "\tWARNING\tTest\tDòng thử  tiếng Việt\r\n";
        CHECK(base::StartsWith(raw, "\xEF\xBB\xBF" "20") && base::EndsWith(raw, expect) && raw.find("bỏ qua") == std::string::npos);
        // Log của tầng dữ liệu / alarm bằng tiếng Anh ngắn gọn (PORTING.md §3).
        CHECK(raw.find("\tINFO\tData\tsystem.json loaded\r\n") != std::string::npos);
        CHECK(raw.find("\tERROR\tSafety\tALARM 1103 [Error] Air pressure low  (root cause)\r\n") != std::string::npos);
        CHECK(raw.find("\tINFO\tAlarm\tAlarm table loaded: ") != std::string::npos && raw.find("\tINFO\tData\tModel saved: DEFAULT\r\n") != std::string::npos);
        auto dayLines = LogWorker::ReadDay(AppPaths::Root(), std::chrono::system_clock::now());
        CHECK(!dayLines.empty() && dayLines.back().Level == LogLevel::Warning && dayLines.back().Message == "Dòng thử  tiếng Việt");
        CHECK(!dayLines.empty() && std::chrono::abs(dayLines.back().UpdateTime - list[0].UpdateTime) < std::chrono::milliseconds(2));

        int ran = 0;
        UiThread::Run([&] { ++ran; });
        UiThread::Post([&] { ++ran; });
        std::vector<std::function<void()>> posted;
        UiThread::SetDispatcher([&](std::function<void()> fn) { posted.push_back(std::move(fn)); });
        UiThread::Run([&] { ++ran; });
        UiThread::Post([&] { ++ran; });
        std::thread([&] { UiThread::Run([&] { ++ran; }); }).join();
        CHECK(ran == 3 && posted.size() == 2 && UiThread::IsUiThread());
        UiThread::SetDispatcher(nullptr);

        std::string mutexName = "Local\\sca_infra_test_" + base::NowString("HHmmssfff");
        CHECK(SingleInstanceGuard::TryLock(mutexName));
        SingleInstanceGuard::Release();
    }

    /// <summary>Đăng nhập theo cấp (Tuấn 2026-10-21): users.json mặc định, sai/đúng mật khẩu, tự đăng xuất, đổi mật khẩu
    /// và thời gian tự đăng xuất (chỉ Engineer), nạp lại từ file, file hỏng.</summary>
    void TestUserAccess()
    {
        using namespace std::chrono_literals;
        fs::path file = AppPaths::UsersFile();
        std::error_code ec;
        fs::remove(file, ec);

        // Chưa có file: tạo với mật khẩu mặc định, chỉ lưu dạng băm.
        UserAccess::Load();
        CHECK(fs::exists(file));
        CHECK(UserAccess::Level() == EUserLevel::Operator && !UserAccess::IsAtLeast(EUserLevel::Engineer));
        CHECK(UserAccess::IsDefaultPassword(EUserLevel::Engineer) && UserAccess::PasswordChangedAt(EUserLevel::Engineer).empty());
        CHECK(UserAccess::IdleLogoutMin() == UserAccess::DefaultIdleLogoutMin);
        std::string raw = RawFile(file);
        CHECK_MSG(raw.find("\"1234\"") == std::string::npos && raw.find("\"Hash\"") != std::string::npos, raw);

        // Sai mật khẩu: giữ Operator. Đúng: lên Engineer, Version tăng.
        uint64_t v0 = UserAccess::Version();
        CHECK(!UserAccess::Login("123"));
        CHECK(!UserAccess::Login(""));
        CHECK(UserAccess::Level() == EUserLevel::Operator && UserAccess::Version() == v0);
        CHECK(UserAccess::Login(UserAccess::DefaultEngineerPassword));
        CHECK(UserAccess::IsAtLeast(EUserLevel::Engineer) && UserAccess::Version() > v0);

        // Tự đăng xuất đúng ngưỡng.
        CHECK(!UserAccess::LogoutIfIdle(9min + 59s) && UserAccess::Level() == EUserLevel::Engineer);
        CHECK(UserAccess::LogoutIfIdle(10min) && UserAccess::Level() == EUserLevel::Operator);
        CHECK(!UserAccess::LogoutIfIdle(60min));   // đã là Operator

        // Cấp Master: cùng ô đăng nhập, lên Master; sai thì giữ cấp; không có trong users.json; không đặt làm mật khẩu
        // Engineer được; cũng tự đăng xuất. Chữ thật không nằm trong bài kiểm (Tuấn 2026-11-02): chỉ kiểm bản băm thật đọc
        // được, phần còn lại dùng mật khẩu giả. Chữ giao diện không nhắc gì tới cấp này, kể cả câu từ chối.
        CHECK(UserAccess::MasterSecretValidForTests());
        const std::string fakeMaster = "not-the-real-one";
        UserAccess::SetMasterPasswordForTests(fakeMaster);
        CHECK(UserAccess::Login(fakeMaster) && UserAccess::Level() == EUserLevel::Master);
        CHECK(UserAccess::IsAtLeast(EUserLevel::Engineer) && std::string(ToString(EUserLevel::Master)) == "Master");
        CHECK(!UserAccess::Login(fakeMaster.substr(1)) && UserAccess::Level() == EUserLevel::Master);
        std::string refused = UserAccess::ChangePassword(EUserLevel::Engineer, fakeMaster);
        CHECK(!refused.empty() && UserAccess::IsDefaultPassword(EUserLevel::Engineer));
        CHECK(RawFile(file).find(fakeMaster) == std::string::npos);
        CHECK(UserAccess::LogoutIfIdle(10min) && UserAccess::Level() == EUserLevel::Operator);
        for (const std::string& text : { refused, Loc::F("User.LoginHint", 10), Loc::T("User.LoginTip"), Loc::T("User.VisionEditLockedTip") })
            CHECK_MSG(!base::Contains(base::ToLower(text), "master"), text);

        // Thời gian tự đăng xuất: chỉ Engineer, trong khoảng cho phép.
        CHECK(!UserAccess::SetIdleLogoutMin(5).empty() && UserAccess::IdleLogoutMin() == 10);
        CHECK(UserAccess::Login("1234"));
        CHECK(!UserAccess::SetIdleLogoutMin(0).empty() && !UserAccess::SetIdleLogoutMin(121).empty());
        CHECK(UserAccess::SetIdleLogoutMin(5).empty() && UserAccess::IdleLogoutMin() == 5);
        CHECK(UserAccess::LogoutIfIdle(5min) && UserAccess::Level() == EUserLevel::Operator);

        // Đổi mật khẩu: chỉ Engineer, tối thiểu 4 ký tự; mật khẩu cũ hết dùng được.
        CHECK(!UserAccess::ChangePassword(EUserLevel::Engineer, "abcd").empty());
        CHECK(UserAccess::Login("1234"));
        CHECK(!UserAccess::ChangePassword(EUserLevel::Engineer, "abc").empty());
        CHECK(!UserAccess::ChangePassword(EUserLevel::Operator, "abcd").empty());
        CHECK(UserAccess::ChangePassword(EUserLevel::Engineer, "Ab c9").empty());
        CHECK(!UserAccess::IsDefaultPassword(EUserLevel::Engineer) && !UserAccess::PasswordChangedAt(EUserLevel::Engineer).empty());
        UserAccess::Logout("test");
        CHECK(UserAccess::Level() == EUserLevel::Operator);
        CHECK(!UserAccess::Login("1234"));
        CHECK(UserAccess::Login("Ab c9"));

        // Nạp lại: về Operator, mật khẩu mới và thời gian tự đăng xuất còn nguyên.
        UserAccess::Load();
        CHECK(UserAccess::Level() == EUserLevel::Operator && UserAccess::IdleLogoutMin() == 5);
        CHECK(!UserAccess::IsDefaultPassword(EUserLevel::Engineer));
        CHECK(!UserAccess::Login("1234") && UserAccess::Login("Ab c9"));

        // Đặt lại đúng mật khẩu mặc định thì lại là "mặc định".
        CHECK(UserAccess::ChangePassword(EUserLevel::Engineer, "1234").empty() && UserAccess::IsDefaultPassword(EUserLevel::Engineer));

        // File hỏng: đổi tên .bad, về mật khẩu mặc định, 10 phút.
        UserAccess::Logout({});
        CHECK(base::WriteAllText(file, "{ \"Passwords\": { \"Engineer\": { \"Salt\": \"zz\" } "));
        UserAccess::Load();
        fs::path bad = file;
        bad += ".bad";
        CHECK(fs::exists(bad) && fs::exists(file));
        CHECK(UserAccess::IdleLogoutMin() == 10 && UserAccess::IsDefaultPassword(EUserLevel::Engineer));
        CHECK(UserAccess::Login("1234") && UserAccess::Level() == EUserLevel::Engineer);
        CHECK(UserAccess::Login(fakeMaster) && UserAccess::Level() == EUserLevel::Master);   // không phụ thuộc users.json
        UserAccess::Logout({});
        UserAccess::SetMasterPasswordForTests({});
        CHECK(!UserAccess::Login(fakeMaster));
    }
}

int RunInfraTests()
{
    testkit::Group g("Infra");

    fs::path root = fs::temp_directory_path() / ("sca_infra_" + base::NowString("yyyyMMdd_HHmmssfff"));
    AppPaths::Init(base::PathToU8(root));
    CHECK(AppPaths::Root() == root && !AppPaths::UsingFallbackRoot());
    CHECK(fs::is_directory(AppPaths::ImageNgDir()) && fs::is_directory(AppPaths::RuntimeDir()) && fs::is_directory(AppPaths::ModelDir()));
    CHECK(AppPaths::SystemFile() == root / "Config" / "system.json" && AppPaths::RunningFile() == root / "Runtime" / "running.json"
          && AppPaths::AlarmsFile() == root / "Config" / "alarms.json");
    CHECK(std::string(AppPaths::DefaultRoot) == "D:\\ShieldCanAssy");
    LogWorker::Instance().Init(AppPaths::Root());

    TestLoc();
    TestConstants();
    TestAxisMath();
    TestSystemDataDefaults();
    TestSystemDataMigrations();
    TestSystemDataJson();
    TestTrayGridZigzag();
    TestTrayGridMath();
    TestModelData();
    TestModelJson();
    TestModelManagement();
    TestRunningData();
    TestStartupProgress();
    TestAppUpdate(root);
    TestAlarmCatalog(root);
    TestAlarmManager();
    TestLogAndThreads();
    TestUserAccess();

    AppPaths::SetExeDirForTests({});
    LogWorker::Instance().Shutdown();
    std::error_code ec;
    fs::remove_all(root, ec);
    return g.Result();
}
