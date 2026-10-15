using System;
using System.Collections.Generic;
using System.Diagnostics;
using ShieldCanAssy.Core.Infor;
using ShieldCanAssy.Utility;

namespace ShieldCanAssy.Core.Communication.Sim
{
    /// <summary>
    /// Card I/O mô phỏng. Mặc định mọi tín hiệu an toàn ở trạng thái bình thường (NC = 1),
    /// nút bấm nhả. Trang Manual có bảng công tắc để lật từng bit và bấm nút giả lập.
    /// </summary>
    public class FakeIo : Bindable, IIoDevice
    {
        private readonly bool[] _di = new bool[IoMap.DiCount];
        private readonly bool[] _diPrev = new bool[IoMap.DiCount];
        private readonly bool[] _do = new bool[IoMap.DoCount];
        private readonly HashSet<int> _pendingPress = new HashSet<int>();
        private readonly object _lock = new object();

        public FakeIo()
        {
            // Tín hiệu an toàn đấu NC: 1 = bình thường.
            _di[IoMap.EmgOk] = true;
            _di[IoMap.LightCurtainOk] = true;
            _di[IoMap.AirOk] = true;
            // Tray 2 đầu vào mặc định VẮNG: model SIM không teach Tray 2, máy mô phỏng chạy một tray như trước nên thời gian
            // self-test không đổi — bài kiểm hai tray tự bật cảm biến này.
            _di[IoMap.TrayIn1Present] = true;
            _di[IoMap.TrayAssyPresent] = true;
        }

        private bool _connected;
        public bool IsConnected { get => _connected; private set => SetProperty(ref _connected, value); }

        /// <summary>Xác suất hút thành công — hạ xuống để kiểm thử nhánh retry / NG (§5.4).</summary>
        public double PickSuccessRate { get; set; } = 1.0;

        private readonly Random _rng = new Random(12345);

        public bool Connect() { IsConnected = true; return true; }
        public void Disconnect() { IsConnected = false; }

        public bool GetDi(int bit)
        {
            lock (_lock) return bit >= 0 && bit < IoMap.DiCount && _di[bit];
        }

        public bool GetDo(int bit)
        {
            lock (_lock) return bit >= 0 && bit < IoMap.DoCount && _do[bit];
        }

        public void SetDo(int bit, bool value)
        {
            if (bit < 0 || bit >= IoMap.DoCount) return;
            lock (_lock)
            {
                if (_do[bit] == value) return;
                _do[bit] = value;
            }
            OnPropertyChanged(nameof(GetDo));

            // Cảm biến chân không bám theo van hút — có xác suất hút hụt để kiểm thử retry.
            if (bit == IoMap.DoVacuum1) SetVacuumSensor(IoMap.Vacuum1, value);
            if (bit == IoMap.DoVacuum2) SetVacuumSensor(IoMap.Vacuum2, value);

            var after = AfterSetDo;
            if (after != null) after(bit, value);
        }

        /// <summary>
        /// Gọi ngay sau khi một ngõ ra ĐỔI trạng thái. Để bài tự kiểm tra đọc được trạng thái máy đúng vào
        /// khoảnh khắc đó — vd. góc trục A ngay lúc van hút mở — thay vì canh giờ mà chụp hụt. Cùng vai trò
        /// với <c>FakeVision.AfterFlow</c>.
        /// </summary>
        public Action<int, bool> AfterSetDo;

        private void SetVacuumSensor(int sensorBit, bool valveOn)
        {
            bool sensed = valveOn && _rng.NextDouble() < PickSuccessRate;
            lock (_lock) _di[sensorBit] = sensed;
        }

        public void Refresh()
        {
            lock (_lock)
            {
                Array.Copy(_di, _diPrev, IoMap.DiCount);

                // Nút bấm giả lập: giữ đúng MỘT vòng quét rồi tự nhả, để bên tiêu thụ buộc phải
                // xử lý bằng sườn lên chứ không phải bằng mức (PLAN.md §3.2).
                foreach (int bit in _pendingPress)
                    if (bit >= 0 && bit < IoMap.DiCount) _di[bit] = true;

                foreach (int bit in _pendingPress)
                    if (bit >= 0 && bit < IoMap.DiCount) _diPrev[bit] = false;

                _pendingPress.Clear();

                for (int i = 0; i < IoMap.DiCount; i++)
                {
                    if (i == IoMap.BtnStop ||
                        i == IoMap.BtnArmA1 || i == IoMap.BtnArmA2 ||
                        i == IoMap.BtnGoB1 || i == IoMap.BtnGoB2)
                    {
                        // Nút không được giữ: nếu vòng trước đã ở mức 1 thì vòng này nhả.
                        if (_diPrev[i]) _di[i] = false;
                    }
                }
            }
        }

        public bool RisingEdge(int bit)
        {
            lock (_lock) return bit >= 0 && bit < IoMap.DiCount && _di[bit] && !_diPrev[bit];
        }

        public bool FallingEdge(int bit)
        {
            lock (_lock) return bit >= 0 && bit < IoMap.DiCount && !_di[bit] && _diPrev[bit];
        }

        // ── API riêng cho bảng mô phỏng trên trang Manual ──

        /// <summary>Nhấn-nhả một nút: bit lên 1 trong đúng một vòng quét.</summary>
        public void SimPress(int diBit)
        {
            lock (_lock) _pendingPress.Add(diBit);
        }

        /// <summary>Lật một DI giữ mức (EMG, cửa, khí, có tray...).</summary>
        public void SimToggle(int diBit)
        {
            lock (_lock)
            {
                if (diBit < 0 || diBit >= IoMap.DiCount) return;
                _di[diBit] = !_di[diBit];
            }
        }

        public void SimSet(int diBit, bool value)
        {
            lock (_lock)
            {
                if (diBit < 0 || diBit >= IoMap.DiCount) return;
                _di[diBit] = value;
            }
        }
    }
}
