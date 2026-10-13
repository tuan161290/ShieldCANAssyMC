"""Chạy --selftest với SimulationMode bật tạm, rồi trả system.json về đúng từng byte.

Tuấn đã đồng ý cách này (2026-09-12). Không chạy nếu app đang mở — không đụng config lúc đó.
"""
import filecmp, os, re, shutil, subprocess, sys, time

SP = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(SP, '..', '..'))   # BanGiao\tools → gốc dự án
EXE = os.path.join(ROOT, r'ShieldCanAssy.Wpf\bin\Debug\net48\ShieldCanAssy.Wpf.exe')
# Cùng luật với AppPaths.Init: D:\ShieldCanAssy nếu máy có ổ D:, không thì _RunData cạnh exe.
# Máy lắp ráp 1.2.1.218 không có ổ D: nên bản build thử dùng _RunData riêng, không đụng App sản xuất.
DATA = r'D:\ShieldCanAssy' if os.path.isdir('D:\\') else os.path.join(os.path.dirname(EXE), '_RunData')
CFG = os.path.join(DATA, 'Config', 'system.json')
REPORT = os.path.join(DATA, 'selftest.txt')
BAK = os.path.join(SP, 'system.json.bak-selftest')


def out(s):
    sys.stdout.buffer.write((s + '\n').encode('utf-8'))
    sys.stdout.flush()


def app_running():
    r = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq ShieldCanAssy.Wpf.exe', '/NH'],
                       capture_output=True, text=True, errors='replace')
    return 'ShieldCanAssy.Wpf.exe' in r.stdout


if app_running():
    out('ABORT: ShieldCanAssy.Wpf.exe đang chạy — không đụng system.json')
    sys.exit(2)

out('exe built: ' + time.strftime('%m/%d/%Y %H:%M:%S', time.localtime(os.path.getmtime(EXE))))
report_before = os.path.getmtime(REPORT) if os.path.exists(REPORT) else 0

# Máy dev KHÔNG có động cơ hay thiết bị ngoại vi — system.json ở đó nằm sẵn SimulationMode: true và
# phải giữ nguyên thế (Tuấn 21/09). Chỉ lật false→true khi file đang là false (máy khác), và lúc nào
# cũng trả file về đúng từng byte như trước khi chạy.
on = re.compile(rb'"SimulationMode"\s*:\s*true')
off = re.compile(rb'("SimulationMode"\s*:\s*)false')
out('data: ' + DATA)
fresh = not os.path.exists(CFG)
raw = b'' if fresh else open(CFG, 'rb').read()
if not fresh and len(on.findall(raw)) + len(off.findall(raw)) != 1:
    out('ABORT: system.json không có đúng một khoá SimulationMode — không đụng gì')
    sys.exit(3)

if not fresh:
    shutil.copyfile(CFG, BAK)
try:
    if fresh:
        out('Chưa có system.json — App tự tạo bản mặc định (SimulationMode = true)')
    elif off.search(raw):
        open(CFG, 'wb').write(off.sub(rb'\1true', raw))
        out('SimulationMode: false → true tạm thời')
    else:
        out('SimulationMode đã là true — chạy nguyên trạng')

    t0 = time.time()
    p = subprocess.Popen([EXE, '--selftest'], cwd=os.path.dirname(EXE))
    code = p.wait(timeout=1500)
    while app_running() and time.time() - t0 < 1500:
        time.sleep(2)
    out('exit code: %s   elapsed: %.1f s' % (code, time.time() - t0))
finally:
    if not fresh:
        shutil.copyfile(BAK, CFG)
        out('system.json restored exactly: %s' % filecmp.cmp(BAK, CFG, shallow=False))

if not os.path.exists(REPORT) or os.path.getmtime(REPORT) <= report_before:
    out('REPORT NOT UPDATED')
    sys.exit(4)

lines = open(REPORT, encoding='utf-8-sig', errors='replace').read().splitlines()
out('report written: ' + time.strftime('%m/%d/%Y %H:%M:%S', time.localtime(os.path.getmtime(REPORT))) +
    '   lines: %d' % len(lines))
for l in lines:
    if 'KẾT QUẢ' in l:
        out(l)
out('--- không đạt ---')
for l in lines:
    s = l.strip()
    if s.startswith('[') and not s.startswith('[ĐẠT') and not s.startswith('[BẮT ĐẦU'):
        out(l)
out('--- TestAssyTrayFull ---')
on = False
for l in lines:
    if 'Tray lắp ráp hết chỗ' in l:
        on = True
    elif on and l.strip().startswith('──'):
        break
    if on:
        out(l)
