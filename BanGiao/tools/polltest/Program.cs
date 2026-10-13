// Đo độ trễ đầu-cuối của một lệnh chạy Panasonic với vòng đọc THẬT của ServoService:
// MoveAbsolute rồi chờ tới nơi đúng kiểu PanasonicAxis.WaitMotionAsync (hỏi GetAxisState mỗi Task.Delay(10)).
// Driver giả trả lời Modbus RTU, mỗi giao dịch tốn TxMs (dây + trễ bộ chuyển USB), trục chạy MoveMs sau cạnh lên STB.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;
using FluentModbus;
using ServoControl.Models;
using ServoControl.Services;

static class Program
{
    const string WpfBin = @"C:\Users\Tuan\OneDrive\Tuan\2026\Actro\SO1C86-SHIELD CAN ASSY MC\ShieldCANAssyMC\ShieldCanAssy.Wpf\bin\Debug\net48";

    static int Main(string[] args)
    {
        AppDomain.CurrentDomain.AssemblyResolve += (s, e) =>
        {
            string p = Path.Combine(WpfBin, new AssemblyName(e.Name).Name + ".dll");
            return File.Exists(p) ? Assembly.LoadFrom(p) : null;
        };
        return Run(args);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    static int Run(string[] args)
    {
        int moves = args.Length > 0 ? int.Parse(args[0]) : 120;
        Console.WriteLine("moves/config={0}, MoveMs=120, pause 0-100 ms (spin, pha ngau nhien)", moves);
        Console.WriteLine("{0,-6} {1,-9} {2,10} {3,10} {4,10} {5,12} {6,10} {7,8}",
            "Tx ms", "interval", "chu ky doc", "cho khoa", "biet toi", "tong tre TB", "p95", "STB");
        var txs = args.Length > 1 ? new[] { double.Parse(args[1]) } : new[] { 4.0, 18.0 };
        var ivs = args.Length > 2 ? new[] { int.Parse(args[2]) } : new[] { 50, 10 };
        foreach (double tx in txs)
            foreach (int interval in ivs)
            {
                var r = Measure(interval, tx, moves).GetAwaiter().GetResult();
                Console.WriteLine("{0,-6} {1,-9} {2,10:0.0} {3,10:0.0} {4,10:0.0} {5,12:0.0} {6,10:0.0} {7,8}",
                    tx, interval, r.PollPeriod, r.LockWait, r.Detect, r.Overhead, r.OverheadP95, r.Stb);
            }
        return 0;
    }

    sealed class Result { public double PollPeriod, LockWait, Detect, Overhead, OverheadP95; public string Stb; }

    static async Task<Result> Measure(int intervalMs, double txMs, int moves)
    {
        var drive = new SlowDrive { TxMs = txMs, MoveMs = 120 };
        var svc = new ServoService();
        var cfg = new ServoConfig { MonitorIntervalMs = intervalMs, PositionTolerance = 50 };
        cfg.Axes.Add(new ServoAxisConfig
        {
            UnitId = 1, AbsoluteVelocityIndex = 0, AbsoluteRampRpm = 1800, AbsoluteAccelMs = 300, AbsoluteDecelMs = 300
        });
        if (!svc.Connect(cfg, drive)) throw new Exception("Connect failed");
        svc.StartMonitoring();
        await Task.Delay(300).ConfigureAwait(false);

        var rng = new Random(12345);
        var lockWait = new List<double>(); var detect = new List<double>(); var overhead = new List<double>();
        int target = 0, warm = 5, stbBefore = 0;
        double pollT0 = 0; int polls0 = 0;
        for (int i = 0; i < moves + warm; i++)
        {
            if (i == warm) { pollT0 = drive.Now; polls0 = drive.Polls; stbBefore = drive.StbCount; }
            target = target == 100000 ? 0 : 100000;

            double until = drive.Now + rng.NextDouble() * 100;
            while (drive.Now < until) Thread.SpinWait(50);

            drive.FirstWriteAt = double.NaN;
            int stb = drive.StbCount;
            double tCall = drive.Now;
            await svc.MoveAbsolute(1, target, 300).ConfigureAwait(false);
            if (drive.StbCount != stb + 1) throw new Exception("move " + i + ": no STB");

            while (true)
            {
                var st = svc.GetAxisState(1);
                if (st.INP && !st.BUSY && Math.Abs(st.ActualPosition - target) <= 50) break;
                if (drive.Now - tCall > 3000) throw new Exception("move " + i + ": never arrived");
                await Task.Delay(10).ConfigureAwait(false);
            }
            double tDone = drive.Now;
            if (i < warm) continue;
            lockWait.Add(drive.FirstWriteAt - tCall);
            detect.Add(tDone - (drive.StbAt + drive.MoveMs));
            overhead.Add(tDone - tCall - drive.MoveMs);
        }
        double period = (drive.Now - pollT0) / Math.Max(1, drive.Polls - polls0);
        int stbs = drive.StbCount - stbBefore;
        svc.StopMonitoring();
        await Task.Delay(200).ConfigureAwait(false);
        svc.Disconnect();

        overhead.Sort();
        return new Result
        {
            PollPeriod = period,
            LockWait = lockWait.Average(),
            Detect = detect.Average(),
            Overhead = overhead.Average(),
            OverheadP95 = overhead[(int)(0.95 * (overhead.Count - 1))],
            Stb = stbs + "/" + moves
        };
    }
}

/// <summary>Driver A6SF giả có thời gian: mỗi giao dịch tốn TxMs, trục chạy MoveMs kể từ cạnh lên STB.</summary>
sealed class SlowDrive : IModbusRtuSerialPort
{
    public double TxMs, MoveMs;
    readonly Stopwatch _clock = Stopwatch.StartNew();
    public double Now { get { return _clock.Elapsed.TotalMilliseconds; } }

    public double FirstWriteAt = double.NaN;
    public double StbAt = double.NegativeInfinity;
    public int StbCount, Polls;

    int _from = 50000, _to = 50000, _pending = 50000;
    bool _stb;
    byte[] _reply = new byte[0];

    public string PortName { get { return "SLOW-FAKE"; } }
    public bool IsOpen { get; private set; }
    public void Open() { IsOpen = true; }
    public void Close() { IsOpen = false; }

    public void Write(byte[] b, int off, int count)
    {
        double t0 = Now;
        byte unit = b[off], fc = b[off + 1];
        ushort addr = (ushort)(b[off + 2] << 8 | b[off + 3]);
        int qty = b[off + 4] << 8 | b[off + 5];
        bool moving = t0 < StbAt + MoveMs;

        switch (fc)
        {
            case 0x01:
            {
                int bits = 0;
                if (addr == 0x00A0) { bits = 0x01 | (moving ? 0 : 0x04); Polls++; }   // S_RDY, ALM, INP
                else if (addr == 0x0140) bits = (moving ? 0x01 : 0) | 0x02;           // BUSY, HOME_CMP
                else if (addr == 0x0020) bits = 0x01;                                  // SRV-ON
                _reply = Frame(unit, fc, 1, (byte)bits);
                break;
            }
            case 0x03:
            {
                var body = new List<byte> { unit, fc, (byte)(2 * qty) };
                if (addr == 0x600F)
                {
                    int p = moving ? _from : _to;   // low word first, each word big-endian
                    body.AddRange(new[] { (byte)(p >> 8), (byte)p, (byte)(p >> 24), (byte)(p >> 16) });
                }
                else body.AddRange(new byte[2 * qty]);
                _reply = Frame(body.ToArray());
                break;
            }
            case 0x05:
            {
                if (double.IsNaN(FirstWriteAt)) FirstWriteAt = t0;
                bool on = b[off + 4] == 0xFF;
                if (addr == 0x0120 && on && !_stb)
                {
                    _from = _to; _to = _pending; StbAt = t0; StbCount++;
                }
                if (addr == 0x0120) _stb = on;
                _reply = Frame(unit, fc, b[off + 2], b[off + 3], b[off + 4], b[off + 5]);
                break;
            }
            case 0x06:
                if (double.IsNaN(FirstWriteAt)) FirstWriteAt = t0;
                _reply = Frame(unit, fc, b[off + 2], b[off + 3], b[off + 4], b[off + 5]);
                break;
            case 0x10:
            {
                if (double.IsNaN(FirstWriteAt)) FirstWriteAt = t0;
                if (qty == 2)
                {
                    int lo = b[off + 7] << 8 | b[off + 8], hi = b[off + 9] << 8 | b[off + 10];
                    _pending = lo | hi << 16;
                }
                _reply = Frame(unit, fc, b[off + 2], b[off + 3], b[off + 4], b[off + 5]);
                break;
            }
            default:
                _reply = Frame(unit, (byte)(fc | 0x80), 0x01);
                break;
        }

        while (Now < t0 + TxMs) Thread.SpinWait(20);
    }

    public int Read(byte[] buffer, int offset, int count)
    {
        int n = Math.Min(count, _reply.Length);
        Array.Copy(_reply, 0, buffer, offset, n);
        _reply = new byte[0];
        return n;
    }

    public Task<int> ReadAsync(byte[] buffer, int offset, int count, CancellationToken token)
    {
        return Task.FromResult(Read(buffer, offset, count));
    }

    public Task WriteAsync(byte[] buffer, int offset, int count, CancellationToken token)
    {
        Write(buffer, offset, count);
        return Task.FromResult(0);
    }

    static byte[] Frame(params byte[] body)
    {
        var f = new byte[body.Length + 2];
        Array.Copy(body, f, body.Length);
        ushort crc = 0xFFFF;
        for (int i = 0; i < body.Length; i++)
        {
            crc ^= f[i];
            for (int k = 0; k < 8; k++)
                crc = (crc & 1) != 0 ? (ushort)((crc >> 1) ^ 0xA001) : (ushort)(crc >> 1);
        }
        f[body.Length] = (byte)crc;
        f[body.Length + 1] = (byte)(crc >> 8);
        return f;
    }
}
