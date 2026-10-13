using System;
using System.Collections.Generic;
using ShieldCanAssy.Core.Infor;

namespace ShieldCanAssy.Core.Data
{
    /// <summary>Tiến độ và trạng thái ô của một tray.</summary>
    public class TrayState
    {
        public string Name { get; set; }
        public int Rows { get; set; }
        public int Cols { get; set; }

        /// <summary>Trạng thái từng ô, đánh chỉ số [row * Cols + col].</summary>
        public List<ECellState> Cells { get; set; } = new List<ECellState>();

        /// <summary>Con trỏ ô đang xử lý. −1 = đã hết tray.</summary>
        public int CurrentIndex { get; set; }

        public EScanOrder ScanOrder { get; set; } = EScanOrder.RowMajor;

        public int CellCount => Rows * Cols;

        public void Resize(int rows, int cols)
        {
            // Giữ lại trạng thái phần lưới trùng nhau — cùng lý do với TrayLayout.Resize.
            var old = Cells;
            int oldCols = Cols;
            int oldRows = Rows;

            var next = new List<ECellState>(rows * cols);
            for (int r = 0; r < rows; r++)
            {
                for (int c = 0; c < cols; c++)
                {
                    bool inOld = r < oldRows && c < oldCols && old != null && (r * oldCols + c) < old.Count;
                    next.Add(inOld ? old[r * oldCols + c] : ECellState.Empty);
                }
            }

            Rows = rows;
            Cols = cols;
            Cells = next;
            if (CurrentIndex >= CellCount) CurrentIndex = CellCount > 0 ? 0 : -1;
        }

        public ECellState Get(int index)
            => index >= 0 && index < Cells.Count ? Cells[index] : ECellState.Empty;

        public void Set(int index, ECellState state)
        {
            if (index >= 0 && index < Cells.Count) Cells[index] = state;
        }

        public void RowCol(int index, out int row, out int col)
        {
            if (Cols <= 0) { row = 0; col = 0; return; }
            row = index / Cols;
            col = index % Cols;

            // Serpentine: hàng lẻ quét ngược để không phải chạy không tải về đầu hàng.
            if (ScanOrder == EScanOrder.Serpentine && (row % 2) == 1)
                col = Cols - 1 - col;
        }

        /// <summary>
        /// Nghịch đảo của <see cref="RowCol"/>: từ vị trí HÌNH HỌC (hàng, cột) trên tray ra chỉ số ô theo thứ tự
        /// máy ghé. Cần cho mọi chỗ vẽ hình cái tray — hình phải theo vị trí thật của ô, còn thứ tự ghé chỉ là
        /// đường đi của máy giữa các ô đó.
        /// </summary>
        public int IndexOf(int row, int col) { return VisitIndex(row, col, Cols, ScanOrder); }

        /// <summary>
        /// Thứ tự ghé của ô ở (hàng, cột) — bản tĩnh để trang Teaching dùng được trên <c>TrayLayout</c> (chưa có
        /// <see cref="TrayState"/>). Chỉ có MỘT phép tính này trong cả App, đặt cạnh <see cref="RowCol"/> để hai
        /// chiều không thể lệch nhau.
        /// </summary>
        public static int VisitIndex(int row, int col, int cols, EScanOrder order)
        {
            if (cols <= 0) return -1;
            int c = col;
            if (order == EScanOrder.Serpentine && (row % 2) == 1) c = cols - 1 - col;
            return row * cols + c;
        }

        public void ResetAll(ECellState state = ECellState.Loaded)
        {
            for (int i = 0; i < Cells.Count; i++) Cells[i] = state;
            CurrentIndex = Cells.Count > 0 ? 0 : -1;
        }

        /// <summary>Tiến tới ô kế. Trả về false khi hết tray.</summary>
        public bool Advance()
        {
            if (CurrentIndex < 0) return false;
            CurrentIndex++;
            if (CurrentIndex >= CellCount) { CurrentIndex = -1; return false; }
            return true;
        }

        public static TrayState Create(string name, int rows, int cols)
        {
            var t = new TrayState { Name = name, Rows = 0, Cols = 0 };
            t.Resize(rows, cols);
            t.CurrentIndex = 0;
            return t;
        }
    }

    /// <summary>Trạng thái một tay hút — hạt nhân của kiểm tra toàn vẹn khi chạy lại (§3.7).</summary>
    public class NozzleState
    {
        public int Index { get; set; }
        public bool HasProduct { get; set; }

        /// <summary>Ô nguồn của sản phẩm đang giữ, để trả về đúng chỗ khi Align fail.</summary>
        public int SourceCellIndex { get; set; } = -1;

        public double MeasuredAngle { get; set; }
        public bool Aligned { get; set; }
    }

    /// <summary>
    /// Tiến độ chạy máy — ghi xuống Runtime\running.json sau mỗi ô hoàn tất và ngay khi vào
    /// EMG/ALARM. Vẫn ghi để phục vụ log/điều tra sự cố, nhưng KHÔNG dùng để khôi phục chu
    /// trình qua một lần khởi động lại ứng dụng — <see cref="McDataManager.LoadRunningData"/>
    /// gọi <see cref="ResetCycleProgress"/> ngay sau khi nạp, buộc mỗi lần mở app phải Init lại
    /// toàn bộ máy (về gốc, tray trống) thay vì chạy tiếp từ ô dở dang của phiên trước (quyết
    /// định của Tuấn — ưu tiên trạng thái máy luôn tường minh với người vận hành hơn là tiện lợi
    /// resume, đổi lấy việc không còn nguy cơ tiếp tục nhầm một chu trình mà máy đã bị đụng vào
    /// tay trong lúc tắt). Trong CÙNG một phiên chạy (không tắt app), STOP rồi START vẫn tiếp
    /// tục đúng bước như cũ — cơ chế checkpoint của <c>Routine_Auto</c> không đổi.
    /// Bộ đếm sản xuất (CountOk/CountNg/CountTotal) và <see cref="RecentModelName"/> KHÔNG bị
    /// reset — đó là số liệu báo cáo/tiện ích, không phải trạng thái chu trình cần init lại.
    /// </summary>
    public class RunningData
    {
        public string RecentModelName { get; set; }
        public DateTime SavedAt { get; set; }

        public EAutoState AutoState { get; set; } = EAutoState.S00_Ready;

        /// <summary>Dừng giữa hành trình chứ không phải tại checkpoint — buộc nâng Z trước khi tiếp.</summary>
        public bool UnsafePause { get; set; }

        /// <summary>
        /// Sự cố với hàng trên tay hút (Tuấn chốt 2026-09-13, an toàn): chu trình dừng một lần.
        /// Bật khi bước đặt dừng giữa chừng SAU KHI đã nhả hút, khi trả về ô nguồn không nhả được,
        /// hoặc khi phải trả về ô nguồn mà không rõ ô nguồn. Lần bấm chạy kế tiếp McControl gỡ cờ và
        /// ghi sổ theo cảm biến — tay còn hàng thì chạy tiếp, bỏ bước hút (Tuấn đổi 2026-09-26). KHÔNG xoá ở
        /// <see cref="ResetCycleProgress"/> — mở lại app không có nghĩa là đã lấy hàng ra.
        /// </summary>
        public bool NozzleClearRequired { get; set; }

        /// <summary>Lý do của <see cref="NozzleClearRequired"/>, hiện cho người vận hành.</summary>
        public string NozzleClearReason { get; set; }

        public TrayState InputTray { get; set; } = TrayState.Create("InputTray", 4, 5);
        public TrayState AssyTray { get; set; } = TrayState.Create("AssyTray", 5, 6);

        public NozzleState Nozzle1 { get; set; } = new NozzleState { Index = 1 };
        public NozzleState Nozzle2 { get; set; } = new NozzleState { Index = 2 };

        // ── Bộ đếm sản xuất ──
        public int CountOk { get; set; }
        public int CountNg { get; set; }
        public int CountTotal { get; set; }
        public int ConsecutiveNg { get; set; }
        public double LastCycleTimeSec { get; set; }

        // ── Bộ đếm theo tray, hiện trong card tray trang Main (Tuấn 2026-10-07) — nạp lại tray KHÔNG xoá, chỉ Clear
        //    sản lượng (ResetCounters) mới xoá. Tray đầu vào: OK = sản phẩm đặt xong, NG = sản phẩm tính NG. Tray lắp
        //    ráp: OK = ô đặt xong, NG = ô bị bỏ qua (không thấy chi tiết đích, điểm đặt lệch lưới). ──
        public int InputCountOk { get; set; }
        public int InputCountNg { get; set; }
        public int AssyCountOk { get; set; }
        public int AssyCountNg { get; set; }

        // ── Cycle time một sản phẩm (Tuấn chốt 2026-09-28): thời gian một lượt S00→S40 chia cho số sản phẩm của lượt
        //    (thường 2) — không tính lúc dừng và lúc chờ tray, xem Routine_Auto. ──

        /// <summary>Số sản phẩm gần nhất lấy trung bình — bằng một tray lắp ráp 40 sản phẩm (Tuấn 2026-10-06, trước là 25).</summary>
        public const int CycleAvgWindow = 40;

        /// <summary>Cycle time của sản phẩm vừa xong (s/sp).</summary>
        public double LastProductTimeSec { get; set; }

        /// <summary>Cycle time từng sản phẩm, tối đa <see cref="CycleAvgWindow"/> cái gần nhất.</summary>
        public List<double> RecentProductTimes { get; set; } = new List<double>();

        /// <summary>Trung bình của <see cref="RecentProductTimes"/> (s/sp) — tính sẵn lúc ghi, giao diện chỉ đọc số.</summary>
        public double AvgProductTimeSec { get; set; }

        /// <summary>Ghi một lượt chạy xong <paramref name="sec"/> giây ra <paramref name="products"/> sản phẩm.</summary>
        public void RecordCycle(double sec, int products)
        {
            if (sec <= 0.01 || products <= 0) return;
            LastProductTimeSec = sec / products;
            // Danh sách mới thay cho danh sách cũ — luồng khác đang ghi running.json không bị đổi dưới chân.
            var list = new List<double>(RecentProductTimes);
            for (int i = 0; i < products; i++) list.Add(LastProductTimeSec);
            if (list.Count > CycleAvgWindow) list.RemoveRange(0, list.Count - CycleAvgWindow);
            double sum = 0;
            foreach (double t in list) sum += t;
            RecentProductTimes = list;
            AvgProductTimeSec = sum / list.Count;
        }

        // ── Thời gian thực tế một tray lắp ráp (Tuấn 2026-10-08): đồng hồ GIỜ THỰC từ lúc máy chạy với tray đó tới lúc tray
        //    hết ô trống — KHÁC cycle time ở trên, tính cả lúc chờ tray đầu vào và lúc STOP, để biết thật sự mất bao lâu
        //    mới xong một tray. Mở lại App thì sổ tray làm lại từ đầu (ResetCycleProgress) nên đồng hồ cũng bỏ. ──

        /// <summary>Lúc bắt đầu tray lắp ráp đang làm; null = chưa chạy với tray này (hoặc tray đã đầy, chờ thay).</summary>
        public DateTime? AssyTrayStartedAt { get; set; }

        /// <summary>Thời gian thực tế của tray lắp ráp vừa xong (s).</summary>
        public double LastAssyTrayTimeSec { get; set; }

        /// <summary>Số ô đặt OK của tray lắp ráp vừa xong.</summary>
        public int LastAssyTrayProducts { get; set; }

        /// <summary>Thời gian thực tế trung bình một sản phẩm của tray vừa xong = thời gian tray ÷ số ô đặt OK.</summary>
        public double LastAssyTrayPerProductSec => LastAssyTrayProducts > 0 ? LastAssyTrayTimeSec / LastAssyTrayProducts : 0;

        /// <summary>Bắt đầu đếm nếu tray đang làm chưa có mốc — gọi khi máy bắt đầu chạy.</summary>
        public void StartAssyTrayClock(DateTime now)
        {
            if (AssyTrayStartedAt == null) AssyTrayStartedAt = now;
        }

        /// <summary>Tray lắp ráp vừa hết ô trống: chốt thời gian thực tế và số sản phẩm, bỏ mốc. Chưa có mốc thì bỏ qua.</summary>
        public void FinishAssyTray(DateTime now, int products)
        {
            if (AssyTrayStartedAt == null) return;
            LastAssyTrayTimeSec = Math.Max(0, (now - AssyTrayStartedAt.Value).TotalSeconds);
            LastAssyTrayProducts = products;
            AssyTrayStartedAt = null;
        }

        public double NgRatePercent => CountTotal > 0 ? CountNg * 100.0 / CountTotal : 0;

        /// <summary>Clear sản lượng. Không đụng mốc tray đang làm — tray đó vẫn đang chạy, chỉ xoá số của tray trước.</summary>
        public void ResetCounters()
        {
            CountOk = CountNg = CountTotal = ConsecutiveNg = 0;
            InputCountOk = InputCountNg = AssyCountOk = AssyCountNg = 0;
            LastCycleTimeSec = LastProductTimeSec = AvgProductTimeSec = 0;
            RecentProductTimes = new List<double>();
            LastAssyTrayTimeSec = 0;
            LastAssyTrayProducts = 0;
        }

        /// <summary>
        /// Xoá sạch tiến độ chu trình dở dang — gọi một lần ngay sau khi nạp running.json lúc
        /// khởi động app, để mỗi lần mở app đều bắt đầu từ S00 với tray trống và tay hút rỗng,
        /// bất kể phiên trước dừng ở đâu. KHÔNG đụng tới bộ đếm/RecentModelName/SavedAt — những
        /// trường đó không phải "tiến độ chu trình".
        /// </summary>
        public void ResetCycleProgress()
        {
            AutoState = EAutoState.S00_Ready;
            UnsafePause = false;
            // Rows/Cols/ScanOrder đúng của model chỉ có sau LoadModel/LoadRecentModel (chạy
            // ngay sau lệnh gọi này ở App.xaml.cs) — Resize ở đó tự khớp lại kích thước lưới.
            InputTray = TrayState.Create(InputTray.Name, InputTray.Rows, InputTray.Cols);
            AssyTray = TrayState.Create(AssyTray.Name, AssyTray.Rows, AssyTray.Cols);
            Nozzle1 = new NozzleState { Index = 1 };
            Nozzle2 = new NozzleState { Index = 2 };
            AssyTrayStartedAt = null;      // sổ tray lắp ráp làm lại từ đầu thì đồng hồ tray cũng vậy
        }
    }
}
