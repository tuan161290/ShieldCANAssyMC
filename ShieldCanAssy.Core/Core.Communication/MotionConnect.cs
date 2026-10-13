using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using ShieldCanAssy.Core.Communication.Sim;
using ShieldCanAssy.Core.Data;
using ShieldCanAssy.Core.Infor;
using ShieldCanAssy.Utility;
using ShieldCanAssy.Utility.Log;

namespace ShieldCanAssy.Core.Communication
{
    /// <summary>
    /// Quản 7 trục: tạo, kết nối, quét trạng thái, và cung cấp các điều kiện tổng hợp
    /// (đã về gốc chưa, Z có an toàn không) cho tầng trên.
    /// </summary>
    public class MotionConnect : Bindable
    {
        private readonly Dictionary<string, IAxis> _axes = new Dictionary<string, IAxis>(StringComparer.Ordinal);
        private double _zSafe;

        /// <summary>Trục đang về gốc — xem <see cref="HomeAsync"/>.</summary>
        private readonly ConcurrentDictionary<string, bool> _homing = new ConcurrentDictionary<string, bool>();

        public IReadOnlyDictionary<string, IAxis> Axes => _axes;

        /// <summary>Danh sách theo thứ tự cố định của <see cref="AxisId.All"/>.</summary>
        public IList<IAxis> AxisList { get; private set; } = new List<IAxis>();

        public bool Simulation { get; private set; }

        public IAxis this[string name]
        {
            get
            {
                IAxis a;
                return _axes.TryGetValue(name, out a) ? a : null;
            }
        }

        public void Init(SystemData sys)
        {
            _axes.Clear();
            Simulation = sys.SimulationMode;
            _zSafe = sys.ZSafe;

            foreach (string name in AxisId.All)
            {
                var cfg = sys.GetAxis(name);

                _axes[name] = CreateAxis(cfg, Simulation);
            }

            AxisList = AxisId.All.Select(n => _axes[n]).ToList();
        }

        /// <summary>
        /// Chọn lớp trục theo <c>SimulationMode</c> và <c>AxisConfig.Kind</c>.
        ///
        /// <para>Tạo được lớp thật <b>không</b> có nghĩa là kết nối được — <c>Connect()</c> vẫn
        /// có thể thất bại vì chưa cắm cáp, sai COM, sai IP, hoặc thiếu DLL. Đó là đường xử lý
        /// bình thường: trục báo mất kết nối, alarm +00 nổi lên, máy không cho chạy. Không có
        /// đường nào lặng lẽ rơi về trục mô phỏng khi <c>SimulationMode = false</c>, vì trên
        /// màn hình hai thứ đó trông giống hệt nhau và người vận hành sẽ tưởng máy đang chạy.</para>
        /// </summary>
        private static IAxis CreateAxis(AxisConfig cfg, bool simulation)
        {
            if (simulation) return new FakeAxis(cfg);

            switch (cfg.Kind)
            {
                case EAxisKind.Fastech:
                    return new FastechAxis(cfg);
                case EAxisKind.Panasonic:
                default:
                    return new PanasonicAxis(cfg);
            }
        }

        public bool ConnectAll()
        {
            bool all = true;
            foreach (var axis in AxisList)
            {
                bool ok = false;
                try { ok = axis.Connect(); }
                catch (Exception ex)
                {
                    LogWorker.Instance.RecordLog($"Kết nối trục {axis.Name} lỗi: {ex.Message}",
                        Utility.Log.LogLevel.Error, axis.Name);
                }

                if (!ok)
                {
                    AlarmManager.RaiseAxis(axis.Name, AxisFault.Disconnected);
                    all = false;
                }
            }
            return all;
        }

        public void DisconnectAll()
        {
            foreach (var axis in AxisList)
            {
                try { axis.Disconnect(); } catch { }
            }
        }

        /// <summary>Một vòng quét: cập nhật trạng thái mọi trục và soi các điều kiện phát alarm.</summary>
        public void Refresh()
        {
            foreach (var axis in AxisList)
            {
                try
                {
                    axis.Refresh();

                    if (!axis.IsConnected) AlarmManager.RaiseAxis(axis.Name, AxisFault.Disconnected);
                    else AlarmManager.ClearConditionAxis(axis.Name, AxisFault.Disconnected);

                    if (axis.Alarm) AlarmManager.RaiseAxis(axis.Name, AxisFault.DriverAlarm, $"0x{axis.ErrorCode:X4}");
                    else AlarmManager.ClearConditionAxis(axis.Name, AxisFault.DriverAlarm);

                    bool rls = axis.RLS && !IgnoreNegLimitWhileHoming(axis.Name);
                    if (axis.FLS || rls)
                        AlarmManager.RaiseAxis(axis.Name, AxisFault.HardLimit, axis.FLS ? "POT" : "NOT");
                    else
                        AlarmManager.ClearConditionAxis(axis.Name, AxisFault.HardLimit);
                }
                catch (Exception ex)
                {
                    LogWorker.Instance.RecordLog($"Quét trục {axis.Name} lỗi: {ex.Message}",
                        Utility.Log.LogLevel.Error, axis.Name);
                }
            }
        }

        /// <summary>
        /// Về gốc một trục qua đây (không gọi thẳng <c>axis.HomeAsync</c>) để vòng quét biết trục đó đang về gốc.
        /// </summary>
        public async Task<bool> HomeAsync(IAxis axis, CancellationToken ct)
        {
            _homing[axis.Name] = true;
            try { return await axis.HomeAsync(ct).ConfigureAwait(false); }
            finally { bool _; _homing.TryRemove(axis.Name, out _); }
        }

        /// <summary>
        /// Y1/Y2 đứng chờ ở −10 mm, phía ngoài cảm biến gốc, nên về gốc (chiều âm) luôn chạm Limit− rồi driver mới quay
        /// lại tìm gốc — không phải lỗi (Tuấn 2026-09-29, log 09:03 báo 2404 rồi 0.6 s sau Y2 vẫn về gốc xong). Về gốc
        /// hỏng thật vẫn báo HomeTimeout; Limit+ và Limit− lúc không về gốc vẫn báo như cũ.
        /// </summary>
        private bool IgnoreNegLimitWhileHoming(string name)
            => (name == AxisId.Y1 || name == AxisId.Y2) && _homing.ContainsKey(name);

        public bool AllConnected => AxisList.Count > 0 && AxisList.All(a => a.IsConnected);
        public bool AllServoOn   => AxisList.Count > 0 && AxisList.All(a => a.ServoOn);
        public bool AllHomed     => AxisList.Count > 0 && AxisList.All(a => a.OrgOk);
        public bool AnyAlarm     => AxisList.Any(a => a.Alarm);
        public bool AnyBusy      => AxisList.Any(a => a.Busy);

        /// <summary>
        /// Chốt chặn chống va (PLAN.md §5.1): X, Y1, Y2 chỉ được chạy khi CẢ HAI trục Z ở trên
        /// cao độ an toàn. Gốc Z = 0 ở phía TRÊN hành trình, chiều dương của Z là chiều ĐI XUỐNG
        /// (Tuấn xác nhận 2026-09-04, PLAN.md §4.3) — ngược HomeDir/chiều về gốc (đi lên) của
        /// chính nó, vì "về gốc" là hành trình TÌM lại điểm 0, không phải chiều dương lúc chạy
        /// bình thường. Nên "an toàn" (đã rút lên) là vị trí NHỎ — ≤ ngưỡng, không phải ≥.
        ///
        /// <para>Ngưỡng là <see cref="ZSafeLimit"/> = Z_SAFE + <see cref="ZSafeMargin"/>, không phải Z_SAFE: mỗi lần nâng
        /// lên, Z dừng ĐÚNG tại Z_SAFE (chạy đi lại, soi CAM bottom-up), trục dừng lệch vài micromet quanh đó không được
        /// làm chốt chặn báo sai.</para>
        /// </summary>
        public bool ZSafeOk
        {
            get
            {
                var z1 = this[AxisId.Z1];
                var z2 = this[AxisId.Z2];
                if (z1 == null || z2 == null) return false;
                return z1.CurrentPos <= ZSafeLimit && z2.CurrentPos <= ZSafeLimit;
            }
        }

        /// <summary>
        /// Dung sai của chốt chặn trên Z_SAFE (mm). Tuấn chốt 2026-09-22: Z dừng đúng Z_SAFE, chốt chặn nới 1 mm —
        /// trước đó Z dừng ở Z_SAFE − 1 mm và chốt chặn ở đúng Z_SAFE.
        /// </summary>
        public const double ZSafeMargin = 1.0;

        /// <summary>Ngưỡng thật của chốt chặn: X/Y chỉ chạy khi cả Z1, Z2 ≤ giá trị này.</summary>
        public double ZSafeLimit => _zSafe + ZSafeMargin;

        public double ZSafe
        {
            get => _zSafe;
            set => _zSafe = value;
        }

        public void StopAll()
        {
            foreach (var axis in AxisList)
            {
                try { axis.Stop(); } catch { }
            }
        }
    }
}
