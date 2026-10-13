# -*- coding: utf-8 -*-
"""Mo phong chu trinh Auto tren so lieu that cua may (DEFAULT.json + system.json),
dong hoc hieu chuan tu app.log. Tinh thoi gian & quang duong X cho mot tray lap rap."""
import json, io, math

MODEL = '//1.2.1.218/Users/Public/App/_RunData/Model/DEFAULT.json'
SYS   = '//1.2.1.218/Users/Public/App/_RunData/Config/system.json'
M = json.load(io.open(MODEL, encoding='utf-8-sig'))
S = json.load(io.open(SYS,   encoding='utf-8-sig'))
AX = dict((a['Name'], a) for a in S['Axes'])

# ---------- dong hoc: hinh thang v/a, hieu chuan tu log ----------
# X : khop AccelTimeMs=300 -> a=196.7 ; kiem bang buoc 1mm (0.147s) va 2mm (0.217s)
# Y : DO DUOC ~69 mm/s, a~159  (cau hinh ghi 500 mm/s - khong dung voi thuc te)
KIN = {
    'X':     (59.0, 59.0/0.300),
    'Y1':    (69.3, 158.6),
    'Y2':    (69.3, 158.6),
    'Zfast': (300.0, 300.0/0.500),
    'Zslow': (10.0,  10.0/0.500),
    'A':     (180.0, 720.0),
}
MOVE_OVH = 0.055
LAMP     = S['LampSettleMs']/1000.0
SETTLE   = S['VisionSettleMs']/1000.0
FLOW_TOP = 0.153
FLOW_BOT = 0.235
VAC_WAIT = 0.200
VAC_SET  = S['VacuumSettleMs']/1000.0
BLOW     = S['BlowTimeMs']/1000.0

def ramp_s(accel_ms, v_mm_s, unit_per_rev=10.0):
    """Thoi gian ramp THAT, tinh qua dung thanh ghi ma ServoControl ghi xuong driver:
       reg = accel_ms * 3000/rpm, chan tren 10000 (A6SFRegisters: ms/(3000 r/min), 0-10000),
       ramp that = reg * rpm/3000. Khi bi chan thi ramp ngan hon con so dat o trang Setting."""
    rpm = v_mm_s*60.0/unit_per_rev
    reg = min(accel_ms*3000.0/rpm, 10000.0)
    return reg*rpm/3000.0/1000.0

def tmove(axis, d):
    d = abs(d)
    if d < 1e-6: return MOVE_OVH
    v, a = KIN[axis]
    if d <= v*v/a: return MOVE_OVH + 2.0*math.sqrt(d/a)
    return MOVE_OVH + v/a + d/v

class Tray(object):
    def __init__(self, node, zkey):
        g = node['Geo'+zkey]
        self.rows, self.cols = node['Rows'], node['Cols']
        p00, p0c, pr0 = g['P00'], g['P0C'], g['PR0']
        self.x0, self.y0 = p00['X'], p00['Y']
        self.dxc = (p0c['X']-p00['X'])/(self.cols-1); self.dyc = (p0c['Y']-p00['Y'])/(self.cols-1)
        self.dxr = (pr0['X']-p00['X'])/(self.rows-1); self.dyr = (pr0['Y']-p00['Y'])/(self.rows-1)
    def X(self, r, c): return self.x0 + c*self.dxc + r*self.dxr
    def Y(self, r, c): return self.y0 + c*self.dyc + r*self.dyr

IN1, IN2 = Tray(M['InputTray'],'Z1'), Tray(M['InputTray'],'Z2')
AS1, AS2 = Tray(M['AssyTray'],'Z1'),  Tray(M['AssyTray'],'Z2')
ROWS_A, COLS_A = M['AssyTray']['Rows'], M['AssyTray']['Cols']
ROWS_I, COLS_I = M['InputTray']['Rows'], M['InputTray']['Cols']
N_ASSY = ROWS_A*COLS_A
CAMX0, CAMY0 = M['AssyTray']['CamTopDownAnchorX'], M['AssyTray']['CamTopDownAnchorY']
def camX(c): return CAMX0 + c*AS1.dxc
def camY(r): return CAMY0 + r*AS1.dyr
ALIGNX = {1: M['AlignPosX_Nozzle1'], 2: M['AlignPosX_Nozzle2']}

def visit(i, rows, cols, serp):
    r, c = divmod(i, cols)
    if serp and r % 2 == 1: c = cols-1-c
    return r, c

class Sim(object):
    def __init__(self):
        self.t = 0.0; self.x = AX['X']['StandbyPos']; self.xdist = 0.0
        self.bucket = {}
    def add(self, tag, dt):
        self.t += dt; self.bucket[tag] = self.bucket.get(tag, 0.0) + dt
    def goto(self, tag, newx, ypairs=()):
        parts = [tmove('X', newx-self.x)]
        for ax, a, b in ypairs: parts.append(tmove(ax, b-a))
        self.xdist += abs(newx-self.x); self.x = newx
        self.add(tag, max(parts))
    def zcycle(self, node, z, hold, clear_at=None):
        """clear_at = cao do thoat hoc tray da teach; chang 0 <-> clear_at chay song song voi X
        nen khong tinh vao duong gang."""
        p, s_ = node['Z_Pick'+z], node['Z_Slow'+z]
        top = 0.0 if clear_at is None else clear_at
        dt  = tmove('Zfast', max(s_-top, 0.0)) + tmove('Zslow', p-s_)
        dt += tmove('Zslow', p-s_) + tmove('Zfast', max(s_-top, 0.0))
        self.add('Z', dt); self.add('khi', hold)
    def vision(self, flow): self.add('vision', LAMP + SETTLE + flow)

def run(serp_assy=True, serp_in=False, by_row=True, geo_assign=True, geo_pick=False,
        xspeed=None, zslow_cut=0.0, lamp=None, z_accel_ms=500, zlow=10.0,
        vac_settle=None, clear_at=None, order_pick=(1,2), order_align=(2,1), order_place=(1,2)):
    vx = xspeed if xspeed else 59.0
    KIN['X'] = (vx, vx/ramp_s(300.0, vx))
    global LAMP
    LAMP = 0.0 if lamp == 0 else S['LampSettleMs']/1000.0
    KIN['Zfast'] = (300.0, 300.0/ramp_s(float(z_accel_ms), 300.0))
    KIN['Zslow'] = (zlow,  zlow / ramp_s(float(z_accel_ms), zlow))
    vac = VAC_SET if vac_settle is None else vac_settle

    s = Sim()
    ycur = {'Y1': AX['Y1']['StandbyPos'], 'Y2': AX['Y2']['StandbyPos']}
    inp_i = 0; scanned_row = -1

    asy = dict(M['AssyTray']); inp = dict(M['InputTray'])
    if zslow_cut:                       # keo diem giam toc lai gan cao do dat
        for z in ('Z1','Z2'):
            asy['Z_Slow'+z] = asy['Z_Pick'+z] - zslow_cut
            inp['Z_Slow'+z] = inp['Z_Pick'+z] - zslow_cut

    def scan_cell(r, c):
        s.goto('quet', camX(c), [('Y2', ycur['Y2'], camY(r))]); ycur['Y2'] = camY(r)
        s.vision(FLOW_TOP)

    if not by_row:
        for i in range(N_ASSY):
            r, c = visit(i, ROWS_A, COLS_A, serp_assy); scan_cell(r, c)

    for k in range(N_ASSY//2):
        ra, ca = visit(2*k,   ROWS_A, COLS_A, serp_assy)
        rb, cb = visit(2*k+1, ROWS_A, COLS_A, serp_assy)
        if by_row and ra != scanned_row:
            cols = range(COLS_A) if (ra % 2 == 0 or not serp_assy) else range(COLS_A-1, -1, -1)
            for c in cols: scan_cell(ra, c)
            scanned_row = ra

        if geo_assign and AS1.X(rb, cb) < AS1.X(ra, ca): cell1, cell2 = (rb, cb), (ra, ca)
        else:                                            cell1, cell2 = (ra, ca), (rb, cb)

        pi = visit(inp_i,   ROWS_I, COLS_I, serp_in)
        pj = visit(inp_i+1, ROWS_I, COLS_I, serp_in)
        inp_i += 2
        if inp_i >= ROWS_I*COLS_I: inp_i = 0
        p1, p2 = pi, pj
        if geo_pick and min(IN1.X(*pj), IN2.X(*pi)) > min(IN1.X(*pi), IN2.X(*pj)):
            p1, p2 = pj, pi

        _pk = {1:(p1,IN1), 2:(p2,IN2)}
        for nz in order_pick:
            cell, tr = _pk[nz]
            yt = tr.Y(*cell)
            s.goto('X', tr.X(*cell), [('Y1', ycur['Y1'], yt)]); ycur['Y1'] = yt
            s.zcycle(inp, 'Z%d' % nz, VAC_WAIT + vac, clear_at)

        for nz in order_align:
            s.add('Z', tmove('Zfast', 0))
            s.goto('X', ALIGNX[nz]); s.vision(FLOW_BOT)

        _pl = {1:(cell1,AS1), 2:(cell2,AS2)}
        for nz in order_place:
            cell, tr = _pl[nz]
            yt = tr.Y(*cell)
            s.goto('X', tr.X(*cell), [('Y2', ycur['Y2'], yt)]); ycur['Y2'] = yt
            s.zcycle(asy, 'Z%d' % nz, BLOW, clear_at)
    return s

def show(name, s, ref=None):
    d = '' if ref is None else '  (%+.1f s, %+.1f%%)' % (s.t-ref.t, 100*(s.t-ref.t)/ref.t)
    print('%-52s %6.1f s = %4.2f phut | X %7.0f mm | %5.2f s/con%s'
          % (name, s.t, s.t/60.0, s.xdist, s.t/N_ASSY, d))

if __name__ == '__main__':
    base = run()
    print('=== MOT TRAY LAP RAP %dx%d = %d con (dung het 2 tray dau vao %dx%d) ==='
          % (ROWS_A, COLS_A, N_ASSY, ROWS_I, COLS_I))
    show('HIEN TAI', base)
    print()
    print('--- thoi gian theo nhom ---')
    for k in sorted(base.bucket, key=lambda z: -base.bucket[z]):
        print('   %-7s %6.1f s  (%4.1f%%)' % (k, base.bucket[k], 100*base.bucket[k]/base.t))
