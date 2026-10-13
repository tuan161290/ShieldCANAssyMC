# -*- coding: utf-8 -*-
"""Đọc system.json trên máy (CHỈ ĐỌC) và tính thanh ghi ramp: cách hiện tại (nhân theo tốc độ
từng lệnh) và cách mới (độ dốc cố định theo mức Auto cao)."""
import io, json

d = json.load(io.open(r"\\1.2.1.218\Users\Public\App\_RunData\Config\system.json", encoding="utf-8-sig"))


def rpm(cfg, v):
    r = int(round(abs(v) / cfg["UnitPerRev"] * 60.0))
    mx = cfg.get("MaxRpm") or 3000
    return max(1, min(r, mx))


def clamp(x):
    if x > 10000:
        return 10000
    r = int(x)
    return 50 if r == 0 else r


print("%-4s %-9s %6s %6s %6s %5s %5s" % ("Truc", "Kind", "UperR", "Cao", "Thap", "Acc", "Dec"))
for a in d["Axes"]:
    print("%-4s %-9s %6s %6s %6s %5s %5s" % (a["Name"], a["Kind"], a["UnitPerRev"], a["MoveSpeed"],
                                            a.get("MoveSpeedLow"), a["AccelTimeMs"], a["DecelTimeMs"]))
print()
print("%-4s %-10s %6s | %-24s | %-24s" % ("Truc", "lenh", "rpm", "HIEN TAI reg -> ramp", "MOI reg -> ramp"))
for a in d["Axes"]:
    if a["Kind"] not in (0, "Panasonic"):
        continue
    hi = rpm(a, a["MoveSpeed"])
    new_reg = clamp(a["AccelTimeMs"] * 3000.0 / hi)
    speeds = [("Auto cao", a["MoveSpeed"])]
    low = a.get("MoveSpeedLow") or a["MoveSpeed"] * 0.2
    if a["Name"].startswith("Z"):
        speeds.append(("Auto thap", low))
    speeds.append(("Jog cham", a["JogSpeedSlow"]))
    for name, v in speeds:
        r = rpm(a, v)
        cur_reg = clamp(a["AccelTimeMs"] * 3000.0 / r)
        print("%-4s %-10s %6d | %6d -> %6.0f ms       | %6d -> %6.0f ms" % (
            a["Name"], name, r, cur_reg, cur_reg * r / 3000.0, new_reg, new_reg * r / 3000.0))

