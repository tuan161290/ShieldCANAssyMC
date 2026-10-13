# -*- coding: utf-8 -*-
import cycle_model as C

base = C.run()
C.show('MOC HIEN TAI', base)
print()

print('=== A. PHAN MEM THUAN - thu tu quet / giao o ===')
for n, kw in [
    ('assy tuan tu thay vi zigzag (= truoc 24/09)', dict(serp_assy=False)),
    ('bo luat giao o hinh hoc      (= truoc 24/09)', dict(geo_assign=False)),
    ('ca hai cai tren              (= truoc 24/09)', dict(serp_assy=False, geo_assign=False)),
    ('THEM: giao o HUT theo hinh hoc', dict(geo_pick=True)),
]:
    C.show('  '+n, C.run(**kw), base)

print()
print('=== B. THAM SO TRUC - doi trong trang Setting, khong sua code ===')
for n, kw in [
    ('X MoveSpeed 59 -> 100 mm/s', dict(xspeed=100)),
    ('X MoveSpeed 59 -> 150 mm/s', dict(xspeed=150)),
    ('X MoveSpeed 59 -> 200 mm/s', dict(xspeed=200)),
    ('Z AccelTime 500 -> 250 ms',  dict(z_accel_ms=250)),
    ('Z AccelTime 500 -> 150 ms',  dict(z_accel_ms=150)),
    ('Z MoveSpeedLow 10 -> 20 mm/s', dict(zlow=20.0)),
    ('Z MoveSpeedLow 10 -> 30 mm/s', dict(zlow=30.0)),
    ('VacuumSettle 500 -> 250 ms', dict(vac_settle=0.25)),
]:
    C.show('  '+n, C.run(**kw), base)

print()
print('=== C. TEACH LAI diem giam toc Z (hien 5.9 - 10.4 mm tren cao do hut/dat) ===')
for cut in (4.0, 3.0, 2.0):
    C.show('  diem giam toc cach cao do hut/dat %.1f mm' % cut, C.run(zslow_cut=cut), base)

print()
print('=== D. SUA THIET KE: teach cao do thoat hoc tray, cho Z len/xuong SONG SONG voi X ===')
for ca in (45.0, 40.0):
    C.show('  thoat hoc tray o Z = %.0f mm' % ca, C.run(clear_at=ca), base)

print()
print('=== E. GOI GIAI PHAP ===')
packs = [
    ('G1 re nhat: chi doi tham so trang Setting\n'
     '     X 150, Z accel 250 ms, Z cham 20 mm/s, hut giu 250 ms',
     dict(xspeed=150, z_accel_ms=250, zlow=20.0, vac_settle=0.25)),
    ('G2 = G1 + teach lai diem giam toc Z (cach 3 mm) + giao o hut hinh hoc',
     dict(xspeed=150, z_accel_ms=250, zlow=20.0, vac_settle=0.25,
          zslow_cut=3.0, geo_pick=True)),
    ('G3 = G2 + cho Z chay song song voi X (thoat hoc tray Z=45)',
     dict(xspeed=150, z_accel_ms=250, zlow=20.0, vac_settle=0.25,
          zslow_cut=3.0, geo_pick=True, clear_at=45.0)),
]
for n, kw in packs:
    C.show('  '+n, C.run(**kw), base)

print()
print('=== F. NHOM THOI GIAN ===')
g1 = C.run(xspeed=150, z_accel_ms=250, zlow=20.0, vac_settle=0.25)
g3 = C.run(xspeed=150, z_accel_ms=250, zlow=20.0, vac_settle=0.25,
           zslow_cut=3.0, geo_pick=True, clear_at=45.0)

for k in sorted(base.bucket, key=lambda z: -base.bucket[z]):
    print('   %-7s hien tai %6.1f s   ->  G1 %6.1f s   ->  G3 %6.1f s'
          % (k, base.bucket[k], g1.bucket.get(k, 0), g3.bucket.get(k, 0)))
print('   %-7s hien tai %6.1f s   ->  G1 %6.1f s   ->  G3 %6.1f s'
      % ('TONG', base.t, g1.t, g3.t))
