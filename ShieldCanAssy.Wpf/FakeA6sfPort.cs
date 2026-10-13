using System;
using System.Collections.Generic;
using System.Globalization;
using System.Threading;
using System.Threading.Tasks;
using FluentModbus;

namespace ShieldCanAssy.Wpf
{
    /// <summary>
    /// Driver A6SF giả nói Modbus RTU ngay trong bộ nhớ — để self-test thấy từng khung mà
    /// <c>ServoControl.ServoService</c> gửi xuống, không cần cổng COM hay driver thật.
    ///
    /// <para>Giữ thanh ghi và coil như driver, ghi lại từng giao dịch, và ở mỗi cạnh lên của STB
    /// chụp lại toàn bộ thanh ghi — đúng những gì block sắp chạy sẽ đọc. Đếm giao dịch chỉ cho biết
    /// đã bớt ghi; ảnh chụp ở cạnh STB mới cho biết bớt ghi rồi driver vẫn chạy đúng lệnh.</para>
    /// </summary>
    internal sealed class FakeA6sfPort : IModbusRtuSerialPort
    {
        public const ushort Stb = 0x0120;

        /// <summary>Một giao dịch driver đã nhận: mã hàm, địa chỉ, các giá trị ghi.</summary>
        public sealed class Tx
        {
            public byte Fc;
            public ushort Address;
            public ushort[] Values;

            public override string ToString()
            {
                return string.Format(CultureInfo.InvariantCulture, "{0:X2} {1:X4}={2}",
                    Fc, Address, string.Join(",", Array.ConvertAll(Values, v => v.ToString(CultureInfo.InvariantCulture))));
            }
        }

        public readonly Dictionary<ushort, ushort> Registers = new Dictionary<ushort, ushort>();
        public readonly Dictionary<ushort, bool> Coils = new Dictionary<ushort, bool>();
        public readonly List<Tx> Transactions = new List<Tx>();

        /// <summary>Bảng thanh ghi chụp ở mỗi cạnh lên của STB, theo thứ tự.</summary>
        public readonly List<Dictionary<ushort, ushort>> Starts = new List<Dictionary<ushort, ushort>>();

        /// <summary>Trả mã lỗi Modbus cho N giao dịch kế tiếp mà không áp dụng chúng.</summary>
        public int FailNext;

        private byte[] _reply = new byte[0];

        public string PortName { get { return "FAKE-A6SF"; } }
        public bool IsOpen { get; private set; }
        public void Open() { IsOpen = true; }
        public void Close() { IsOpen = false; }

        /// <summary>Driver vừa bật lại nguồn: bảng block trong RAM về 0.</summary>
        public void PowerCycle()
        {
            Registers.Clear();
            Coils.Clear();
        }

        public ushort Reg(ushort address)
        {
            ushort v;
            return Registers.TryGetValue(address, out v) ? v : (ushort)0;
        }

        public void Write(byte[] buffer, int offset, int count)
        {
            var req = new byte[count];
            Array.Copy(buffer, offset, req, 0, count);
            if (count < 4 || Crc(req, count - 2) != (ushort)(req[count - 2] | req[count - 1] << 8))
                throw new InvalidOperationException("khung Modbus sai CRC");

            byte unit = req[0], fc = req[1];
            if (FailNext > 0)
            {
                FailNext--;
                _reply = Frame(unit, (byte)(fc | 0x80), 0x04);   // server device failure
                return;
            }

            ushort addr = (ushort)(req[2] << 8 | req[3]);
            switch (fc)
            {
                case 0x05:
                {
                    bool on = req[4] == 0xFF;
                    bool was;
                    Coils.TryGetValue(addr, out was);
                    Coils[addr] = on;
                    Transactions.Add(new Tx { Fc = fc, Address = addr, Values = new ushort[] { (ushort)(on ? 1 : 0) } });
                    if (addr == Stb && on && !was) Starts.Add(new Dictionary<ushort, ushort>(Registers));
                    _reply = Frame(unit, fc, req[2], req[3], req[4], req[5]);
                    break;
                }
                case 0x06:
                {
                    ushort v = (ushort)(req[4] << 8 | req[5]);
                    Registers[addr] = v;
                    Transactions.Add(new Tx { Fc = fc, Address = addr, Values = new[] { v } });
                    _reply = Frame(unit, fc, req[2], req[3], req[4], req[5]);
                    break;
                }
                case 0x10:
                {
                    int qty = req[4] << 8 | req[5];
                    var vals = new ushort[qty];
                    for (int i = 0; i < qty; i++)
                    {
                        vals[i] = (ushort)(req[7 + 2 * i] << 8 | req[8 + 2 * i]);
                        Registers[(ushort)(addr + i)] = vals[i];
                    }
                    Transactions.Add(new Tx { Fc = fc, Address = addr, Values = vals });
                    _reply = Frame(unit, fc, req[2], req[3], req[4], req[5]);
                    break;
                }
                default:
                    _reply = Frame(unit, (byte)(fc | 0x80), 0x01);   // illegal function
                    break;
            }
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

        private static byte[] Frame(params byte[] body)
        {
            var f = new byte[body.Length + 2];
            Array.Copy(body, f, body.Length);
            ushort crc = Crc(f, body.Length);
            f[body.Length] = (byte)crc;
            f[body.Length + 1] = (byte)(crc >> 8);
            return f;
        }

        private static ushort Crc(byte[] b, int count)
        {
            ushort crc = 0xFFFF;
            for (int i = 0; i < count; i++)
            {
                crc ^= b[i];
                for (int k = 0; k < 8; k++)
                    crc = (crc & 1) != 0 ? (ushort)((crc >> 1) ^ 0xA001) : (ushort)(crc >> 1);
            }
            return crc;
        }
    }
}
