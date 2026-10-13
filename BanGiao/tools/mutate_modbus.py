# -*- coding: utf-8 -*-
"""Gài từng lỗi vào ServoService.cs, build riêng ServoControl, chạy TestServoModbusWrites qua
modbustest.exe. Mỗi lỗi phải làm ít nhất một mục LỖI — không thì bài kiểm đạt rỗng.
Luôn trả file gốc về chỗ cũ, kể cả khi lỗi giữa chừng."""
import io, os, re, shutil, subprocess, sys

SRC = r"C:\Users\Tuan\OneDrive\Tuan\Tuan\Projects\ServoControl\Services\ServoService.cs"
PROJ = r"C:\Users\Tuan\OneDrive\Tuan\Tuan\Projects\ServoControl\ServoControl.csproj"
DLL = r"C:\Users\Tuan\OneDrive\Tuan\Tuan\Projects\ServoControl\bin\Debug\net48\ServoControl.dll"
HERE = os.path.dirname(os.path.abspath(__file__))
EXE = os.path.join(HERE, "modbustest", "bin", "Debug", "net48", "ModbusTest.exe")
BACKUP = os.path.join(HERE, "ServoService.cs.before-mutate")

MUTATIONS = [
    ("Bộ nhớ chỉ xem đã có khoá, không so giá trị",
     "if (_held.TryGetValue(key, out ushort held) && held == value) return false;",
     "if (_held.ContainsKey(key)) return false;"),
    ("Về gốc không quên những gì driver đang giữ",
     "                ForgetHeld(unitId);\n                WriteUnlessHeldUnsafe(unitId, A6SFRegisters.BLOCK_NUMBER, (ushort)config.HomeBlock);",
     "                WriteUnlessHeldUnsafe(unitId, A6SFRegisters.BLOCK_NUMBER, (ushort)config.HomeBlock);"),
    ("MoveAbsolute lỗi mà không quên bộ nhớ",
     "                ForgetHeld(unitId);\n                Log($\"[Axis {unitId}] MoveAbsolute error: {ex.Message}\");",
     "                Log($\"[Axis {unitId}] MoveAbsolute error: {ex.Message}\");"),
    ("Có AbsoluteRampRpm mà vẫn nhân ramp theo tốc độ từng lệnh",
     "ushort rampRpm = config.AbsoluteRampRpm > 0 ? config.AbsoluteRampRpm : velocityRpm;",
     "ushort rampRpm = velocityRpm;"),
    ("Vị trí tách lại hai lần ghi 0x06",
     "                _client.WriteMultipleRegisters(unitId, A6SFRegisters.GetBlockDataAddress(config.AbsoluteBlock),\n                    new[] { (short)position, (short)(position >> 16) });",
     "                _client.WriteSingleRegister(unitId, A6SFRegisters.GetBlockDataAddress(config.AbsoluteBlock), (short)position);\n                _client.WriteSingleRegister(unitId, (ushort)(A6SFRegisters.GetBlockDataAddress(config.AbsoluteBlock) + 1), (short)(position >> 16));"),
    ("Jog+ ghi thẳng, không qua bộ nhớ (đè ô acc/dec mà MoveAbsolute không biết)",
     "                WriteUnlessHeldUnsafe(unitId, A6SFRegisters.GetBlockAccelAddress(config.AccelIndex), config.JogAccelMs);\n                WriteUnlessHeldUnsafe(unitId, A6SFRegisters.GetBlockDecelAddress(config.DecelIndex), config.JogAccelMs);\n                WriteUnlessHeldUnsafe(unitId, A6SFRegisters.GetBlockVelocityAddress(config.VelocityIndex), velocityRpm);\n                WriteUnlessHeldUnsafe(unitId, A6SFRegisters.BLOCK_NUMBER, (ushort)config.JogPositiveBlock);",
     "                _client.WriteSingleRegister(unitId, A6SFRegisters.GetBlockAccelAddress(config.AccelIndex), config.JogAccelMs);\n                _client.WriteSingleRegister(unitId, A6SFRegisters.GetBlockDecelAddress(config.DecelIndex), config.JogAccelMs);\n                _client.WriteSingleRegister(unitId, A6SFRegisters.GetBlockVelocityAddress(config.VelocityIndex), velocityRpm);\n                _client.WriteSingleRegister(unitId, A6SFRegisters.BLOCK_NUMBER, (ushort)config.JogPositiveBlock);"),
]


def build():
    r = subprocess.run(["dotnet", "build", PROJ, "-f", "net48", "-c", "Debug", "-nologo", "-clp:ErrorsOnly"],
                       capture_output=True, text=True, encoding="utf-8", errors="replace")
    return r.returncode == 0, r.stdout[-1500:]


WPF_BIN = r"C:\Users\Tuan\OneDrive\Tuan\2026\Actro\SO1C86-SHIELD CAN ASSY MC\ShieldCANAssyMC\ShieldCanAssy.Wpf\bin\Debug\net48"
COPY = os.path.join(HERE, "wpfbin-mutate")


def fresh_copy():
    """Bản sao phẳng của bin sản phẩm (chỉ file ở cấp đầu, bỏ myLibs của VisionMaster)."""
    if os.path.isdir(COPY):
        shutil.rmtree(COPY)
    os.makedirs(COPY)
    for f in os.listdir(WPF_BIN):
        p = os.path.join(WPF_BIN, f)
        if os.path.isfile(p):
            shutil.copy2(p, COPY)


def run():
    shutil.copy2(DLL, os.path.join(COPY, "ServoControl.dll"))
    env = dict(os.environ, WPF_BIN=COPY)
    r = subprocess.run([EXE], capture_output=True, text=True, encoding="utf-8", errors="replace", env=env, timeout=120)
    fails = [l.strip() for l in r.stdout.splitlines() if "[LỖI ]" in l]
    tail = [l for l in r.stdout.splitlines() if l.startswith("===")]
    return fails, (tail[-1] if tail else r.stdout[-300:] + r.stderr[-300:])


orig = io.open(SRC, encoding="utf-8-sig").read()
has_bom = io.open(SRC, "rb").read(3) == b"\xef\xbb\xbf"
nl = "\r\n" if "\r\n" in orig else "\n"
shutil.copyfile(SRC, BACKUP)
text = orig.replace("\r\n", "\n")
killed = 0
fresh_copy()
fails0, tail0 = run()   # bản gốc: phải đạt hết, không thì số "bị bắt" bên dưới vô nghĩa
print("Bản gốc trên bản sao bin: %s  %s" % (tail0, "" if not fails0 else "!! CÓ MỤC LỖI: " + "; ".join(fails0)))
try:
    for name, old, new in MUTATIONS:
        n = text.count(old)
        if n != 1:
            print("!! %s: mẫu xuất hiện %d lần — bỏ qua" % (name, n))
            continue
        mutated = text.replace(old, new).replace("\n", nl)
        io.open(SRC, "w", encoding="utf-8-sig" if has_bom else "utf-8", newline="").write(mutated)
        ok, log = build()
        if not ok:
            print("!! %s: build hỏng\n%s" % (name, log))
            continue
        fails, tail = run()
        status = "BỊ BẮT" if fails else "LỌT — bài kiểm đạt rỗng!"
        killed += 1 if fails else 0
        print("\n## %s → %s  (%s)" % (name, status, tail))
        for f in fails:
            print("   " + f[:230])
finally:
    shutil.copyfile(BACKUP, SRC)
    ok, log = build()
    print("\nĐã trả file gốc; build lại ServoControl: %s" % ("OK" if ok else "HỎNG\n" + log))
print("\n=== %d/%d lỗi gài bị bắt" % (killed, len(MUTATIONS)))
