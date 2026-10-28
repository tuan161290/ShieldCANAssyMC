#include "Data/SystemData.h"

#include "Base/Text.h"
#include "Communication/AxisAngle.h"
#include "Communication/VmContract.h"
#include "Data/DataJson.h"
#include "Fastech/FastechBoardId.h"
#include "Infor/AxisId.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>

namespace sca
{
    using datajson::json;
    using datajson::Num;
    using datajson::GetNum;
    using jsonx::Get;

    namespace
    {
        /// <summary>
        /// Giá trị mặc định để app chạy được ngay lần đầu. Phần lớn số ở đây (tốc độ, soft
        /// limit, vị trí chờ, gốc toạ độ/điểm 0, dấu <c>HomeDir</c> của X/Y1/Y2/A1/A2) là PHỎNG
        /// ĐOÁN, phải xác định lại trên máy thật ở P1 (PLAN.md §11 #6) — phụ thuộc vị trí lắp
        /// cảm biến gốc thật. Ba thứ đã chốt sẵn, không phải xác định lại:
        /// <list type="bullet">
        /// <item><b>Chiều dương từng trục</b> (Tuấn chốt 2026-09-07, PLAN.md §11 #6) — xem ghi
        /// chú tại từng case bên dưới (X, Y1/Y2, A1/A2) và Z1/Z2.</item>
        /// <item><c>HomeDir</c> của Z1/Z2 là -1 (về gốc đi lên, nhưng quy ước "xuống = dương" nên
        /// đi lên là chiều âm — xem ghi chú tại case Z1/Z2 bên dưới).</item>
        /// <item><c>PulsePerUnit</c> là hằng số theo thiết kế (1000 xung/mm Panasonic, 10000
        /// xung/360° Fastech) — cấu hình driver khớp nó, không đo nó.</item>
        /// </list>
        /// </summary>
        std::shared_ptr<AxisConfig> DefaultAxis(std::string_view name)
        {
            auto cfg = std::make_shared<AxisConfig>();
            cfg->Name = std::string(name);

            if (name == AxisId::X)
            {
                // ĐÃ CHỐT (Tuấn, 2026-09-07, PLAN.md §11 #6): nhìn từ phía người thao tác,
                // chiều dương của X là TRÁI → PHẢI. HomeDir = -1 dưới đây vẫn là PHỎNG ĐOÁN —
                // dấu thật phụ thuộc cảm biến gốc lắp ở đầu trái hay đầu phải, xác định ở P1.
                cfg->Kind = EAxisKind::Panasonic; cfg->ComPort = "COM3"; cfg->UnitId = 1;
                cfg->Unit = "mm"; cfg->SoftLimitNeg = -1; cfg->SoftLimitPos = 600;
                cfg->JogSpeedSlow = 30; cfg->JogSpeedFast = 60; cfg->MoveSpeed = 300; cfg->HomeDir = -1; cfg->StandbyPos = 100;
                cfg->PulsePerUnit = 1000.0;         // ĐÃ CHỐT: 1000 xung/mm, cấu hình trong driver
                // ĐÃ CHỐT (Tuấn xác nhận qua PANATERM, 2026-09-09): trục X dùng động cơ tuyến
                // tính Alpharobot ALR160-S1 + driver Panasonic A6L — KHÔNG có "vòng quay" vật
                // lý như trục vít me. Driver quy đổi tốc độ rpm dựa trên bước cực từ (magnetic
                // pole pitch), đọc trực tiếp từ Pr9.02 trên driver = 27.00mm — PHẢI dùng đúng
                // số này cho UnitPerRev, không phải mặc định 10.0 (vốn là bước vít me của các
                // trục Y/Z). Nếu để sai, AxisScale::ToRpm quy đổi lệch, khiến tốc độ thực trên
                // trục X khác hẳn giá trị mm/s cấu hình (không ảnh hưởng độ chính xác vị trí,
                // vì đó là việc của PulsePerUnit — độc lập với UnitPerRev).
                cfg->UnitPerRev = 27.0;
            }
            else if (name == AxisId::Z1 || name == AxisId::Z2)
            {
                cfg->Kind = EAxisKind::Panasonic;
                cfg->ComPort = name == AxisId::Z1 ? "COM4" : "COM5";
                cfg->UnitId = 1; cfg->Unit = "mm";
                cfg->SoftLimitNeg = -1; cfg->SoftLimitPos = 120;
                cfg->JogSpeedSlow = 15; cfg->JogSpeedFast = 30; cfg->MoveSpeed = 150;
                // ĐÃ CHỐT (Tuấn xác nhận 2026-09-04, PLAN.md §4.3): cảm biến gốc ở phía TRÊN
                // hành trình, gán toạ độ gốc = 0 — KHÔNG lệch như trục khác. Chiều dương của Z
                // là chiều ĐI XUỐNG (xuống gần bàn/khay = giá trị tăng), ngược với X/Y/A —
                // "an toàn" (rút lên) là vị trí NHỎ, xem MotionConnect.ZSafeOk (≤ Z_SAFE + 1 mm, không
                // phải ≥). Về gốc đi LÊN — dưới quy ước "xuống = dương" thì đi lên là chiều
                // ÂM, nên HomeDir = -1 (không phải +1 như trục khác).
                cfg->HomeDir = -1;
                cfg->PulsePerUnit = 1000.0;         // ĐÃ CHỐT: 1000 xung/mm, cấu hình trong driver
                // StandbyPos nhỏ hơn Z_SAFE — vẫn còn rất gần gốc, an toàn.
                cfg->HomeOffset = 0; cfg->StandbyPos = 10;
            }
            else if (name == AxisId::Y1 || name == AxisId::Y2)
            {
                // ĐÃ CHỐT (Tuấn, 2026-09-07, PLAN.md §11 #6): chiều dương của Y là từ PHÍA
                // TRONG đi RA phía người thao tác. HomeDir = -1 dưới đây vẫn là PHỎNG ĐOÁN —
                // dấu thật phụ thuộc cảm biến gốc lắp ở đầu trong hay đầu ngoài, xác định ở P1.
                cfg->Kind = EAxisKind::Panasonic;
                cfg->ComPort = name == AxisId::Y1 ? "COM6" : "COM7";
                cfg->UnitId = 1; cfg->Unit = "mm";
                cfg->SoftLimitNeg = -1; cfg->SoftLimitPos = 400;
                cfg->JogSpeedSlow = 30; cfg->JogSpeedFast = 60; cfg->MoveSpeed = 250; cfg->HomeDir = -1; cfg->StandbyPos = 20;
                cfg->PulsePerUnit = 1000.0;         // ĐÃ CHỐT: 1000 xung/mm, cấu hình trong driver
            }
            else if (name == AxisId::A1 || name == AxisId::A2)
            {
                // ĐÃ CHỐT (Tuấn, 2026-09-07, PLAN.md §11 #6): nhìn từ TRÊN XUỐNG, chiều dương
                // của A1/A2 là NGƯỢC CHIỀU KIM ĐỒNG HỒ (CCW). HomeDir = +1 dưới đây vẫn là
                // PHỎNG ĐOÁN — dấu thật phụ thuộc cảm biến gốc nằm ở góc nào, xác định ở P1.
                cfg->Kind = EAxisKind::Fastech;
                cfg->IpAddress = name == AxisId::A1 ? "192.168.0.2" : "192.168.0.3";
                cfg->BoardId = name == AxisId::A1 ? FastechBoardId::AxisA1 : FastechBoardId::AxisA2;
                cfg->AxisIndex = 0; cfg->Unit = "deg";

                // Toạ độ quấn vòng về [0, 360) — xem AxisConfig::WrapDegrees và AxisAngle.
                cfg->WrapDegrees = true;
                cfg->SoftLimitNeg = 0; cfg->SoftLimitPos = 360;   // chỉ để hiện dải hợp lệ
                cfg->JogSpeedSlow = 45; cfg->JogSpeedFast = 90; cfg->MoveSpeed = 180;

                // ĐÃ CHỐT (Tuấn, 2026-09-12, sau khi đổi động cơ): động cơ trả 18000
                // xung/vòng ĐỘNG CƠ, nối qua hộp số 1/2 nên 2 vòng động cơ mới được 1 vòng
                // đầu hút → 36000 xung cho 360° của đầu hút = 100 xung/độ chẵn, phân giải
                // 0.01°/xung.
                //
                // Con số này đã đi qua hai lần sai, cùng một kiểu: 10000/360 = 27.7778 (quên
                // hộp số), rồi 20000/360 = 55.5556 (động cơ cũ 10000 xung/vòng). Sai tỉ lệ ở
                // đây KHÔNG hiện ra ở đâu ngoài chính góc: trục vẫn chạy, vẫn báo "tới nơi",
                // chỉ là xoay sai góc. Đổi động cơ hay hộp số thì sửa hai hằng số nguồn
                // (RotaryMotorPulsePerRev / RotaryMotorRevPerOutputRev), đừng gõ số ở đây.
                cfg->PulsePerUnit = SystemData::RotaryPulsePerDeg;
                cfg->UnitPerRev = SystemData::RotaryDegPerMotorRev;

                // HomeOffset = góc máy TẠI cảm biến gốc. Để 0 nghĩa là "chưa đo" — phải đo ở
                // P1 vì cảm biến gốc không nằm đúng tư thế gắp khay input (cách đo ghi ở
                // FastechAxis.ToMachine).
                cfg->HomeDir = +1; cfg->StandbyPos = 0; cfg->HomeOffset = 0;
            }

            return cfg;
        }

        bool IsDotNetWhite(char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }

        /// <summary>double.TryParse(s, NumberStyles.Float, CultureInfo.InvariantCulture): khoảng trắng hai đầu, dấu
        /// đầu, dấu chấm thập phân, số mũ; không nhận dấu phân cách nghìn. Thêm "Infinity"/"-Infinity"/"NaN" như ký hiệu
        /// của văn hoá bất biến.</summary>
        bool TryParseInvariantDouble(std::string_view raw, double& value)
        {
            size_t b = 0, e = raw.size();
            while (b < e && IsDotNetWhite(raw[b])) ++b;
            while (e > b && IsDotNetWhite(raw[e - 1])) --e;
            std::string_view s = raw.substr(b, e - b);
            if (s.empty()) return false;

            if (s == "Infinity" || s == "+Infinity") { value = std::numeric_limits<double>::infinity(); return true; }
            if (s == "-Infinity") { value = -std::numeric_limits<double>::infinity(); return true; }
            if (s == "NaN") { value = std::numeric_limits<double>::quiet_NaN(); return true; }

            // Kiểm dạng trước khi đưa from_chars (from_chars còn nhận "inf", "nan", hex… mà .NET không nhận).
            size_t i = 0;
            bool neg = false;
            if (s[i] == '+' || s[i] == '-') { neg = s[i] == '-'; ++i; }
            size_t start = i;
            int digits = 0;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') { ++i; ++digits; }
            if (i < s.size() && s[i] == '.')
            {
                ++i;
                while (i < s.size() && s[i] >= '0' && s[i] <= '9') { ++i; ++digits; }
            }
            if (digits == 0) return false;
            if (i < s.size() && (s[i] == 'e' || s[i] == 'E'))
            {
                ++i;
                if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
                int expDigits = 0;
                while (i < s.size() && s[i] >= '0' && s[i] <= '9') { ++i; ++expDigits; }
                if (expDigits == 0) return false;
            }
            if (i != s.size()) return false;

            std::string_view body = s.substr(start);
            double v = 0;
            auto r = std::from_chars(body.data(), body.data() + body.size(), v, std::chars_format::general);
            if (r.ec == std::errc::result_out_of_range)
            {
                // .NET Framework 4.8: số quá nhỏ → 0; số quá lớn → OverflowException (TryParse trả false).
                bool tiny = body.find("e-") != std::string_view::npos || body.find("E-") != std::string_view::npos;
                if (!tiny) return false;
                v = 0.0;
            }
            else if (r.ec != std::errc() || r.ptr != body.data() + body.size()) return false;
            value = neg ? -v : v;
            return true;
        }

        std::string RenameDefault(const std::string& current, const char* oldDefault, const char* newDefault,
                                  std::vector<std::string>& changed)
        {
            if (current != oldDefault) return current;
            changed.push_back(std::string(oldDefault) + " → " + newDefault);
            return newDefault;
        }

        json ToJson(const VisionOutputNames& v)
        {
            json j = json::object();
            j["FlowLocateTop"] = v.FlowLocateTop;
            j["FlowAlignBottomTool1"] = v.FlowAlignBottomTool1;
            j["FlowAlignBottomTool2"] = v.FlowAlignBottomTool2;
            j["OutFound"] = v.OutFound;
            j["OutImage"] = v.OutImage;
            j["RenderTool1"] = v.RenderTool1;
            j["RenderTool2"] = v.RenderTool2;
            j["RenderTopDown"] = v.RenderTopDown;
            j["GlobalVarModule"] = v.GlobalVarModule;
            j["GvAxisX"] = v.GvAxisX;
            j["GvAxisY2"] = v.GvAxisY2;
            j["GvAxisA1"] = v.GvAxisA1;
            j["GvAxisA2"] = v.GvAxisA2;
            return j;
        }

        void FromJson(const json& j, VisionOutputNames& v)
        {
            // Tên Flow cũ (TopDownCalib, AlignCalib_ToolN, MeasurePlaced…) không còn trường nào — bị bỏ qua.
            Get(j, "FlowLocateTop", v.FlowLocateTop);
            Get(j, "FlowAlignBottomTool1", v.FlowAlignBottomTool1);
            Get(j, "FlowAlignBottomTool2", v.FlowAlignBottomTool2);
            Get(j, "OutFound", v.OutFound);
            Get(j, "OutImage", v.OutImage);
            Get(j, "RenderTool1", v.RenderTool1);
            Get(j, "RenderTool2", v.RenderTool2);
            Get(j, "RenderTopDown", v.RenderTopDown);
            Get(j, "GlobalVarModule", v.GlobalVarModule);
            Get(j, "GvAxisX", v.GvAxisX);
            Get(j, "GvAxisY2", v.GvAxisY2);
            Get(j, "GvAxisA1", v.GvAxisA1);
            Get(j, "GvAxisA2", v.GvAxisA2);
        }
    }

    // ───────────────────────── VisionOutputNames ─────────────────────────

    std::string VisionOutputNames::RenderPath(std::string_view flow, std::string_view path)
    {
        std::string p = base::Trim(path);
        return p.empty() ? std::string(flow) : p;
    }

    std::vector<std::string> VisionOutputNames::MigrateAxisVarUnits()
    {
        std::vector<std::string> changed;
        GvAxisX = RenameDefault(GvAxisX, "X", "X_mm", changed);
        GvAxisY2 = RenameDefault(GvAxisY2, "Y2", "Y2_mm", changed);
        GvAxisA1 = RenameDefault(GvAxisA1, "A1", "A1_deg", changed);
        GvAxisA2 = RenameDefault(GvAxisA2, "A2", "A2_deg", changed);
        return changed;
    }

    // ───────────────────────── SystemData ─────────────────────────

    int SystemData::SettleMsFor(EVmCamera cam) const
    {
        return cam == EVmCamera::TopDown ? TopSettleMs : cam == EVmCamera::BottomUp ? BottomSettleMs : 0;
    }

    int SystemData::LampMsFor(EVmCamera cam) const
    {
        return cam == EVmCamera::TopDown ? TopLampMs : cam == EVmCamera::BottomUp ? BottomLampMs : 0;
    }

    std::vector<double> SystemData::ParseNumberList(std::string_view text)
    {
        std::vector<double> list;
        if (base::IsBlank(text)) return list;

        // Dấu trừ Unicode "−" (U+2212, UTF-8 E2 88 92) → "-".
        std::string s = base::Replace(std::string(text), "\xE2\x88\x92", "-");
        std::string cur;
        auto flush = [&] {
            if (cur.empty()) return;   // StringSplitOptions.RemoveEmptyEntries
            double v = 0;
            if (TryParseInvariantDouble(cur, v)) list.push_back(v);
            cur.clear();
        };
        for (char c : s)
        {
            if (c == ',' || c == ';' || c == ' ' || c == '\t') flush();
            else cur.push_back(c);
        }
        flush();
        return list;
    }

    std::shared_ptr<AxisConfig> SystemData::GetAxis(std::string_view name)
    {
        for (auto& a : Axes)
            if (a && a->Name == name) return a;
        auto cfg = DefaultAxis(name);
        Axes.push_back(cfg);
        return cfg;
    }

    std::vector<std::string> SystemData::MigrateRotaryAxes()
    {
        std::vector<std::string> changed;
        auto addChanged = [&](const std::string& n) {
            if (std::find(changed.begin(), changed.end(), n) == changed.end()) changed.push_back(n);
        };

        for (auto& cfg : Axes)
        {
            if (!cfg) continue;
            if (cfg->Kind != EAxisKind::Fastech) continue;
            if (!base::EqualsIgnoreCase(cfg->Unit, "deg")) continue;

            // iBdID trước — nó độc lập với tỉ lệ góc, file có thể đúng cái này mà sai cái kia.
            int wantBoard = cfg->Name == AxisId::A1 ? FastechBoardId::AxisA1
                          : cfg->Name == AxisId::A2 ? FastechBoardId::AxisA2
                          : cfg->BoardId;
            if (cfg->BoardId != wantBoard)
            {
                cfg->BoardId = wantBoard;
                changed.push_back(cfg->Name);   // bản C# Add thẳng (chưa có tên nào trong danh sách ở bước này)
            }

            bool stale = !cfg->WrapDegrees
                      || std::fabs(cfg->PulsePerUnit - RotaryPulsePerDeg) > 1e-6
                      || std::fabs(cfg->UnitPerRev - RotaryDegPerMotorRev) > 1e-6;
            if (!stale) continue;

            cfg->WrapDegrees = true;
            cfg->PulsePerUnit = RotaryPulsePerDeg;
            cfg->UnitPerRev = RotaryDegPerMotorRev;
            cfg->SoftLimitNeg = 0;
            cfg->SoftLimitPos = 360;
            cfg->HomeOffset = AxisAngle::Wrap(cfg->HomeOffset);
            cfg->StandbyPos = AxisAngle::Wrap(cfg->StandbyPos);

            addChanged(cfg->Name);
        }

        return changed;
    }

    std::shared_ptr<SystemData> SystemData::CreateDefault()
    {
        auto sys = std::make_shared<SystemData>();
        for (const char* name : AxisId::All)
            sys->Axes.push_back(DefaultAxis(name));
        return sys;
    }

    std::string SystemData::ToJsonText() const
    {
        // Thứ tự khoá = thứ tự property của SystemData.cs.
        json j = json::object();
        j["MachineName"] = MachineName;
        j["MachineCode"] = MachineCode;
        j["SimulationMode"] = SimulationMode;
        j["Language"] = Language;
        j["ZSafe"] = Num(ZSafe);
        j["HasAbsoluteEncoder"] = HasAbsoluteEncoder;
        j["RequireHomeAfterEmg"] = RequireHomeAfterEmg();   // Newtonsoft ghi giá trị getter
        j["HasLightCurtain"] = HasLightCurtain;
        j["HasAirSensor"] = HasAirSensor;
        j["HomeRotateSequential"] = HomeRotateSequential;
        j["ControlTickMs"] = ControlTickMs;
        j["WatchdogTimeoutMs"] = WatchdogTimeoutMs;
        j["WatchdogStartupGraceMs"] = WatchdogStartupGraceMs;
        j["OnlyNozzle"] = OnlyNozzle;
        j["PickRetryCount"] = PickRetryCount;
        j["PickRetryDeltaZ"] = Num(PickRetryDeltaZ);
        j["MaxConsecutiveNg"] = MaxConsecutiveNg;
        j["ScanAssyByRow"] = ScanAssyByRow;
        j["ScanAssyByPair"] = ScanAssyByPair;   // chỉ bản WinUI (2026-10-28)
        j["VacuumTimeoutMs"] = VacuumTimeoutMs;
        j["VacuumSettleMs"] = VacuumSettleMs;
        j["BlowTimeMs"] = BlowTimeMs;
        j["InputZSlowDown"] = InputZSlowDown;
        j["InputZSlowUp"] = InputZSlowUp;
        j["AssyZSlowDown"] = AssyZSlowDown;
        j["AssyZSlowUp"] = AssyZSlowUp;
        j["MaxCompensationAngleDeg"] = Num(MaxCompensationAngleDeg);
        j["MaxCompensationDistance"] = Num(MaxCompensationDistance);
        j["PlaceTargetGuardMm"] = Num(PlaceTargetGuardMm);
        j["ACapTolDeg"] = Num(ACapTolDeg);
        j["MaxGuardSkipStreak"] = MaxGuardSkipStreak;
        j["AlignXTolMm"] = Num(AlignXTolMm);
        j["IoDiIpAddress"] = IoDiIpAddress;
        j["IoDoIpAddress"] = IoDoIpAddress;
        j["VisionSolutionPath"] = VisionSolutionPath;
        j["VisionTriggerTimeoutMs"] = VisionTriggerTimeoutMs;
        j["VisionSettleMs"] = BottomSettleMs;   // [JsonProperty("VisionSettleMs")]
        j["TopSettleMs"] = TopSettleMs;
        j["LampSettleMs"] = BottomLampMs;       // [JsonProperty("LampSettleMs")]
        j["TopLampMs"] = TopLampMs;
        j["VisionOutputs"] = ToJson(VisionOutputs);
        j["CalibValidDays"] = CalibValidDays;
        j["TopDownCalibStepXMm"] = Num(TopDownCalibStepXMm);
        j["TopDownCalibStepYMm"] = Num(TopDownCalibStepYMm);
        j["TopDownCalibCenterX"] = Num(TopDownCalibCenterX);
        j["TopDownCalibCenterY"] = Num(TopDownCalibCenterY);
        j["TopDownCalibCenterSet"] = TopDownCalibCenterSet;
        j["CalibMaxResidualThresholdMm"] = Num(CalibMaxResidualThresholdMm);
        j["CalibShotsPerPoint"] = CalibShotsPerPoint;
        j["TopDownVerifyStepMm"] = Num(TopDownVerifyStepMm);
        j["AlignVerifyAngleDeg"] = Num(AlignVerifyAngleDeg);
        j["AlignVerifyAngleTolDeg"] = Num(AlignVerifyAngleTolDeg);
        j["AlignVerifyMinEccMm"] = Num(AlignVerifyMinEccMm);
        j["AlignVerifyMinTurnDeg"] = Num(AlignVerifyMinTurnDeg);
        j["AlignCalibXOffsetsMm"] = AlignCalibXOffsetsMm;
        j["AlignCalibAnglesDeg"] = AlignCalibAnglesDeg;
        json axes = json::array();
        for (const auto& a : Axes)
            if (a) axes.push_back(datajson::ToJson(*a));
        j["Axes"] = std::move(axes);
        return datajson::DumpIndented(j);
    }

    std::shared_ptr<SystemData> SystemData::FromJsonText(const std::string& text)
    {
        json j;
        if (!jsonx::ParseObject(text, j)) return nullptr;

        auto s = std::make_shared<SystemData>();
        Get(j, "MachineName", s->MachineName);
        Get(j, "MachineCode", s->MachineCode);
        Get(j, "SimulationMode", s->SimulationMode);
        Get(j, "Language", s->Language);
        GetNum(j, "ZSafe", s->ZSafe);
        Get(j, "HasAbsoluteEncoder", s->HasAbsoluteEncoder);
        {
            bool requireHome = s->m_requireHomeAfterEmg;
            Get(j, "RequireHomeAfterEmg", requireHome);
            s->SetRequireHomeAfterEmg(requireHome);
        }
        Get(j, "HasLightCurtain", s->HasLightCurtain);
        Get(j, "HasAirSensor", s->HasAirSensor);
        Get(j, "HomeRotateSequential", s->HomeRotateSequential);
        Get(j, "ControlTickMs", s->ControlTickMs);
        Get(j, "WatchdogTimeoutMs", s->WatchdogTimeoutMs);
        Get(j, "WatchdogStartupGraceMs", s->WatchdogStartupGraceMs);
        Get(j, "OnlyNozzle", s->OnlyNozzle);
        Get(j, "PickRetryCount", s->PickRetryCount);
        GetNum(j, "PickRetryDeltaZ", s->PickRetryDeltaZ);
        Get(j, "MaxConsecutiveNg", s->MaxConsecutiveNg);
        Get(j, "ScanAssyByRow", s->ScanAssyByRow);
        Get(j, "ScanAssyByPair", s->ScanAssyByPair);
        Get(j, "VacuumTimeoutMs", s->VacuumTimeoutMs);
        Get(j, "VacuumSettleMs", s->VacuumSettleMs);
        Get(j, "BlowTimeMs", s->BlowTimeMs);
        Get(j, "InputZSlowDown", s->InputZSlowDown);
        Get(j, "InputZSlowUp", s->InputZSlowUp);
        Get(j, "AssyZSlowDown", s->AssyZSlowDown);
        Get(j, "AssyZSlowUp", s->AssyZSlowUp);
        GetNum(j, "MaxCompensationAngleDeg", s->MaxCompensationAngleDeg);
        GetNum(j, "MaxCompensationDistance", s->MaxCompensationDistance);
        GetNum(j, "PlaceTargetGuardMm", s->PlaceTargetGuardMm);
        GetNum(j, "ACapTolDeg", s->ACapTolDeg);
        Get(j, "MaxGuardSkipStreak", s->MaxGuardSkipStreak);
        GetNum(j, "AlignXTolMm", s->AlignXTolMm);
        Get(j, "IoDiIpAddress", s->IoDiIpAddress);
        Get(j, "IoDoIpAddress", s->IoDoIpAddress);
        Get(j, "VisionSolutionPath", s->VisionSolutionPath);
        Get(j, "VisionTriggerTimeoutMs", s->VisionTriggerTimeoutMs);
        Get(j, "VisionSettleMs", s->BottomSettleMs);
        Get(j, "TopSettleMs", s->TopSettleMs);
        Get(j, "LampSettleMs", s->BottomLampMs);
        Get(j, "TopLampMs", s->TopLampMs);
        if (auto it = j.find("VisionOutputs"); it != j.end() && it->is_object()) FromJson(*it, s->VisionOutputs);
        Get(j, "CalibValidDays", s->CalibValidDays);
        GetNum(j, "TopDownCalibStepXMm", s->TopDownCalibStepXMm);
        GetNum(j, "TopDownCalibStepYMm", s->TopDownCalibStepYMm);
        GetNum(j, "TopDownCalibCenterX", s->TopDownCalibCenterX);
        GetNum(j, "TopDownCalibCenterY", s->TopDownCalibCenterY);
        Get(j, "TopDownCalibCenterSet", s->TopDownCalibCenterSet);
        GetNum(j, "CalibMaxResidualThresholdMm", s->CalibMaxResidualThresholdMm);
        Get(j, "CalibShotsPerPoint", s->CalibShotsPerPoint);
        GetNum(j, "TopDownVerifyStepMm", s->TopDownVerifyStepMm);
        GetNum(j, "AlignVerifyAngleDeg", s->AlignVerifyAngleDeg);
        GetNum(j, "AlignVerifyAngleTolDeg", s->AlignVerifyAngleTolDeg);
        GetNum(j, "AlignVerifyMinEccMm", s->AlignVerifyMinEccMm);
        GetNum(j, "AlignVerifyMinTurnDeg", s->AlignVerifyMinTurnDeg);
        Get(j, "AlignCalibXOffsetsMm", s->AlignCalibXOffsetsMm);
        Get(j, "AlignCalibAnglesDeg", s->AlignCalibAnglesDeg);
        if (auto it = j.find("Axes"); it != j.end() && it->is_array())
        {
            for (const auto& e : *it)
            {
                if (!e.is_object()) continue;   // phần tử null — McDataManager sắp lại theo AxisId::All
                auto a = std::make_shared<AxisConfig>();
                datajson::FromJson(e, *a);
                s->Axes.push_back(a);
            }
        }
        return s;
    }
}
