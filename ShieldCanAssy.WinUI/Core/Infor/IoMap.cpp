#include "Infor/IoMap.h"

#include "Lang/Loc.h"

namespace sca
{
    namespace
    {
        // Khoá chữ (Loc) của từng bit — chữ thật nằm ở Lang/StringsEn.inc (cùng khoá với StringsVi/StringsEn.resx).
        std::array<const char*, IoMap::DiCount> BuildDiNames()
        {
            std::array<const char*, IoMap::DiCount> n{};
            for (auto& s : n) s = "Io.Spare";
            n[IoMap::EmgOk] = "Io.DiEmgOk";
            n[IoMap::BtnStop] = "Io.DiBtnStop";
            n[IoMap::AirOk] = "Io.DiAirOk";
            n[IoMap::Vacuum1] = "Io.DiVacuum1";
            n[IoMap::Vacuum2] = "Io.DiVacuum2";
            n[IoMap::TrayIn2Present] = "Io.DiTrayIn2Present";
            n[IoMap::TrayAssyPresent] = "Io.DiTrayAssyPresent";
            n[IoMap::TrayIn1Present] = "Io.DiTrayIn1Present";
            n[IoMap::LightCurtainOk] = "Io.DiLightCurtainOk";
            n[IoMap::BtnArmA1] = "Io.DiBtnArmA1";
            n[IoMap::BtnArmA2] = "Io.DiBtnArmA2";
            n[IoMap::BtnGoB1] = "Io.DiBtnGoB1";
            n[IoMap::BtnGoB2] = "Io.DiBtnGoB2";
            return n;
        }

        std::array<const char*, IoMap::DoCount> BuildDoNames()
        {
            std::array<const char*, IoMap::DoCount> n{};
            for (auto& s : n) s = "Io.Spare";
            n[IoMap::DoVacuum1] = "Io.DoVacuum1";
            n[IoMap::DoVacuum2] = "Io.DoVacuum2";
            n[IoMap::DoBlow1] = "Io.DoBlow1";
            n[IoMap::DoBlow2] = "Io.DoBlow2";
            n[IoMap::DoLampRed] = "Io.DoLampRed";
            n[IoMap::DoLampYellow] = "Io.DoLampYellow";
            n[IoMap::DoLampGreen] = "Io.DoLampGreen";
            n[IoMap::DoBuzzer] = "Io.DoBuzzer";
            n[IoMap::DoLampStartBtn] = "Io.DoLampStartBtn";
            n[IoMap::DoLampResetBtn] = "Io.DoLampResetBtn";
            n[IoMap::DoLampStartBtnB] = "Io.DoLampStartBtnB";
            n[IoMap::DoLampStartBtnB2] = "Io.DoLampStartBtnB2";
            n[IoMap::DoLampCam1] = "Io.DoLampCam1";
            n[IoMap::DoLampCam2] = "Io.DoLampCam2";
            n[IoMap::DoBrakeReleaseZ1] = "Io.DoBrakeReleaseZ1";
            n[IoMap::DoBrakeReleaseZ2] = "Io.DoBrakeReleaseZ2";
            return n;
        }
    }

    const std::array<const char*, IoMap::DiCount>& IoMap::DiNameKeys()
    {
        static const auto keys = BuildDiNames();
        return keys;
    }

    const std::array<const char*, IoMap::DoCount>& IoMap::DoNameKeys()
    {
        static const auto keys = BuildDoNames();
        return keys;
    }

    std::vector<std::string> IoMap::DiNames()
    {
        std::vector<std::string> r;
        r.reserve(DiCount);
        for (const char* k : DiNameKeys()) r.push_back(Loc::T(k));
        return r;
    }

    std::vector<std::string> IoMap::DoNames()
    {
        std::vector<std::string> r;
        r.reserve(DoCount);
        for (const char* k : DoNameKeys()) r.push_back(Loc::T(k));
        return r;
    }
}
