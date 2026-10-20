#include "Communication/AxisConfig.h"

#include "Data/DataJson.h"

namespace sca::datajson
{
    using jsonx::Get;

    // Thứ tự khoá = thứ tự property của AxisConfig.cs (Newtonsoft ghi theo thứ tự khai báo).
    json ToJson(const AxisConfig& c)
    {
        json j = json::object();
        if (!c.Name.empty()) j["Name"] = c.Name;   // null → không ghi (NullValueHandling.Ignore)
        j["Kind"] = (int)c.Kind;
        j["Unit"] = c.Unit;
        j["ComPort"] = c.ComPort;
        j["BaudRate"] = c.BaudRate;
        j["UnitId"] = c.UnitId;
        j["HomeBlock"] = c.HomeBlock;
        j["AbsoluteBlock"] = c.AbsoluteBlock;
        j["JogPositiveBlock"] = c.JogPositiveBlock;
        j["JogNegativeBlock"] = c.JogNegativeBlock;
        j["AbsoluteVelocityIndex"] = c.AbsoluteVelocityIndex;
        j["AccelTimeMs"] = c.AccelTimeMs;
        j["DecelTimeMs"] = c.DecelTimeMs;
        j["PositionTolerancePulse"] = c.PositionTolerancePulse;
        j["SkipMoveTolerancePulse"] = c.SkipMoveTolerancePulse;
        j["IpAddress"] = c.IpAddress;
        j["AxisIndex"] = c.AxisIndex;
        j["BoardId"] = c.BoardId;
        j["PulsePerUnit"] = Num(c.PulsePerUnit);
        j["UnitPerRev"] = Num(c.UnitPerRev);
        j["MaxRpm"] = c.MaxRpm;
        j["SoftLimitPos"] = Num(c.SoftLimitPos);
        j["SoftLimitNeg"] = Num(c.SoftLimitNeg);
        j["JogSpeedSlow"] = Num(c.JogSpeedSlow);
        j["JogSpeedFast"] = Num(c.JogSpeedFast);
        j["MoveSpeed"] = Num(c.MoveSpeed);
        j["MoveSpeedMid"] = Num(c.MoveSpeedMid());   // giá trị ĐÃ SUY RA — Newtonsoft đọc getter
        j["MoveSpeedLow"] = Num(c.MoveSpeedLow());
        j["HomeDir"] = c.HomeDir;
        j["HomeUseZPhase"] = c.HomeUseZPhase;
        j["HomeTimeoutMs"] = c.HomeTimeoutMs;
        j["MoveTimeoutMs"] = c.MoveTimeoutMs;
        j["HomeOffset"] = Num(c.HomeOffset);
        j["StandbyPos"] = Num(c.StandbyPos);
        j["WrapDegrees"] = c.WrapDegrees;
        return j;
    }

    void FromJson(const json& j, AxisConfig& c)
    {
        Get(j, "Name", c.Name);
        GetEnum(j, "Kind", c.Kind);
        Get(j, "Unit", c.Unit);
        Get(j, "ComPort", c.ComPort);
        Get(j, "BaudRate", c.BaudRate);
        Get(j, "UnitId", c.UnitId);
        Get(j, "HomeBlock", c.HomeBlock);
        Get(j, "AbsoluteBlock", c.AbsoluteBlock);
        Get(j, "JogPositiveBlock", c.JogPositiveBlock);
        Get(j, "JogNegativeBlock", c.JogNegativeBlock);
        Get(j, "AbsoluteVelocityIndex", c.AbsoluteVelocityIndex);
        Get(j, "AccelTimeMs", c.AccelTimeMs);
        Get(j, "DecelTimeMs", c.DecelTimeMs);
        Get(j, "PositionTolerancePulse", c.PositionTolerancePulse);
        Get(j, "SkipMoveTolerancePulse", c.SkipMoveTolerancePulse);
        Get(j, "IpAddress", c.IpAddress);
        Get(j, "AxisIndex", c.AxisIndex);
        Get(j, "BoardId", c.BoardId);
        GetNum(j, "PulsePerUnit", c.PulsePerUnit);
        GetNum(j, "UnitPerRev", c.UnitPerRev);
        Get(j, "MaxRpm", c.MaxRpm);
        GetNum(j, "SoftLimitPos", c.SoftLimitPos);
        GetNum(j, "SoftLimitNeg", c.SoftLimitNeg);
        GetNum(j, "JogSpeedSlow", c.JogSpeedSlow);
        GetNum(j, "JogSpeedFast", c.JogSpeedFast);
        GetNum(j, "MoveSpeed", c.MoveSpeed);
        // Setter của C# lưu thẳng số đọc được (kể cả 0 = "chưa đặt", getter tự suy lại 50%/20%).
        if (auto it = j.find("MoveSpeedMid"); it != j.end() && it->is_number())
            c.SetMoveSpeedMid(it->get<double>());
        if (auto it = j.find("MoveSpeedLow"); it != j.end() && it->is_number())
            c.SetMoveSpeedLow(it->get<double>());
        Get(j, "HomeDir", c.HomeDir);
        Get(j, "HomeUseZPhase", c.HomeUseZPhase);
        Get(j, "HomeTimeoutMs", c.HomeTimeoutMs);
        Get(j, "MoveTimeoutMs", c.MoveTimeoutMs);
        GetNum(j, "HomeOffset", c.HomeOffset);
        GetNum(j, "StandbyPos", c.StandbyPos);
        Get(j, "WrapDegrees", c.WrapDegrees);
    }
}
