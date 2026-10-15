using System;
using System.Collections.ObjectModel;
using System.Globalization;
using System.Windows.Input;
using ShieldCanAssy.Core.Data;
using ShieldCanAssy.Core.Infor;
using ShieldCanAssy.Core.Lang;
using ShieldCanAssy.Wpf.Models;
using ShieldCanAssy.Wpf.Mvvm;

namespace ShieldCanAssy.Wpf.ViewModels
{
    /// <summary>
    /// Lưới ô của một tray. Kích thước lấy từ <see cref="TrayState"/> lúc chạy — không có con
    /// số 4×5 hay 5×6 nào nằm trong XAML (PLAN.md §8.1). Đổi model sang tray 6×8 là lưới tự
    /// dựng lại, không phải sửa giao diện.
    /// </summary>
    public class TrayGridViewModel : ObservableObject
    {
        /// <param name="titleKey">Khoá chữ (<see cref="Loc"/>) của tiêu đề card.</param>
        /// <param name="resetKey">Khoá chữ của nút nạp lại.</param>
        /// <param name="slots">Số chỗ đặt tray mà card cho chọn xem — tray đầu vào 2 (bàn đặt hai tray từ 2026-10-15), tray
        /// lắp ráp 1. Nhiều chỗ thì card hiện MỘT tray mỗi lúc, khung lưới giữ nguyên như khi chỉ có một tray, nút Select
        /// LUÔN có — kể cả lúc Tray 2 chưa teach, khi đó xem Tray 2 là lưới trống kèm chữ "chưa teach" (Tuấn 2026-10-15).</param>
        public TrayGridViewModel(string titleKey, string resetKey, ICommand resetCommand, int slots = 1)
        {
            _titleKey = titleKey;
            _resetKey = resetKey;
            _slots = Math.Max(1, slots);
            ResetCommand = resetCommand;
            SelectTrayCommand = new RelayCommand(() =>
            {
                ShownTray = (ShownTray + 1) % _slots;
                if (_lastTray != null) Sync(_lastTray, _lastScanned);
            });
            // Hai lưới tray sống suốt đời App — đăng ký sự kiện tĩnh không rò rỉ.
            Loc.LanguageChanged += () =>
            {
                OnPropertyChanged(nameof(Title));
                OnPropertyChanged(nameof(ResetText));
                OnPropertyChanged(nameof(SelectText));
                if (_lastTray != null) Sync(_lastTray, _lastScanned);
            };
        }

        private readonly string _titleKey, _resetKey;
        private TrayState _lastTray;
        private int[] _lastScanned;

        public string Title => Loc.T(_titleKey);

        /// <summary>Nút nạp lại nằm chung card với lưới (Tuấn 2026-10-07).</summary>
        public string ResetText => Loc.T(_resetKey);
        public ICommand ResetCommand { get; }

        // ── Chọn tray đang xem (chỉ card nhiều chỗ đặt tray). Sổ (TrayState.Trays) chỉ có Tray 2 khi Tray 2 đã teach. ──

        private readonly int _slots;

        /// <summary>Card có nút Select chọn tray — tray đầu vào.</summary>
        public bool HasTraySelect => _slots > 1;

        private int _shownTray;
        /// <summary>
        /// Tray đang hiện (0 = Tray 1). Tự theo tray máy đang chạy — tray chứa ô hiện tại — mỗi khi tray đó đổi (Tray 1 xong
        /// sang Tray 2, nạp lại về Tray 1). Nút Select chuyển qua tray kia để xem, tới lần đổi tray kế tiếp thì card lại về
        /// tray đang chạy.
        /// </summary>
        public int ShownTray
        {
            get => _shownTray;
            private set { if (SetProperty(ref _shownTray, value)) OnPropertyChanged(nameof(SelectText)); }
        }

        public string SelectText => Loc.F("Tray.SelectN", ShownTray + 1);
        public ICommand SelectTrayCommand { get; }

        /// <summary>Tray chứa ô hiện tại ở lần Sync trước — để chỉ tự chuyển khi nó ĐỔI, không giành lại lựa chọn của nút
        /// Select ở mỗi nhịp làm mới.</summary>
        private int _activeTray = -1;

        // Bộ đếm của riêng tray này (Tuấn 2026-10-07) — số lấy từ RunningData.InputCountOk/AssyCountOk…, nạp lại tray
        // không xoá, chỉ Clear sản lượng mới xoá. Tổng = OK + NG. Tray đầu vào: tổng của cả hai tray.
        private int _countOk, _countNg, _countTotal;
        public int CountOk { get => _countOk; private set => SetProperty(ref _countOk, value); }
        public int CountNg { get => _countNg; private set => SetProperty(ref _countNg, value); }
        public int CountTotal { get => _countTotal; private set => SetProperty(ref _countTotal, value); }

        public void SetCounts(int ok, int ng)
        {
            CountOk = ok;
            CountNg = ng;
            CountTotal = ok + ng;
        }

        public ObservableCollection<TrayCellVm> Cells { get; } = new ObservableCollection<TrayCellVm>();

        private int _rows;
        public int Rows { get => _rows; private set => SetProperty(ref _rows, value); }

        private int _cols;
        public int Cols { get => _cols; private set => SetProperty(ref _cols, value); }

        private string _summary;
        public string Summary { get => _summary; private set => SetProperty(ref _summary, value); }

        // Lưới đang dựng cho tray nào, thứ tự quét nào — đổi một trong hai là phải dựng lại.
        private int _builtTray = -1;
        private EScanOrder _builtOrder;

        /// <param name="scanned">Chỉ số các ô đã quét trước OK (chỉ tray lắp ráp) — tô xanh nhạt nếu ô còn trống.</param>
        public void Sync(TrayState tray, int[] scanned = null)
        {
            if (tray == null) return;
            _lastTray = tray;
            _lastScanned = scanned;

            int trays = Math.Max(1, tray.Trays);       // tray có trong sổ — Tray 2 chỉ khi đã teach
            int rows = tray.RowsPerTray;                // hàng của MỘT tray
            int perTray = tray.CellsPerTray;

            int active = tray.CurrentIndex >= 0 && perTray > 0 ? Math.Min(tray.CurrentIndex / perTray, trays - 1) : -1;
            if (active >= 0 && active != _activeTray) ShownTray = active;
            _activeTray = active;

            if (Rows != rows || Cols != tray.Cols || Cells.Count != perTray ||
                _builtTray != ShownTray || _builtOrder != tray.ScanOrder)
                Rebuild(tray, rows);

            // Ô thứ i trong danh sách nằm ở VỊ TRÍ HÌNH HỌC thứ i, còn dữ liệu của nó nằm ở
            // tray.Cells[cell.Index] — hai thứ tự đó khác nhau khi tray quét zigzag. Đang xem tray chưa có trong sổ (Tray 2
            // chưa teach) thì chỉ số nằm ngoài sổ: ô để trống.
            foreach (var cell in Cells)
            {
                if (cell.Index < 0 || cell.Index >= tray.Cells.Count) continue;
                cell.State = tray.Cells[cell.Index];
                cell.IsCurrent = cell.Index == tray.CurrentIndex;
                cell.IsScanned = scanned != null && System.Array.IndexOf(scanned, cell.Index) >= 0;
            }

            if (_slots == 1)
            {
                Summary = tray.CurrentIndex >= 0
                    ? Loc.F("Tray.SummaryCurrent", tray.Rows, tray.Cols, tray.CurrentIndex + 1, tray.CellCount)
                    : Loc.F("Tray.SummaryDone", rows, tray.Cols);
                return;
            }

            string inner;
            if (ShownTray >= trays)
                inner = Loc.T("Tray.NotTaught");
            else if (active == ShownTray)
                inner = Loc.F("Tray.SummaryCurrent", rows, tray.Cols, tray.CurrentIndex - ShownTray * perTray + 1, perTray);
            else if (tray.CurrentIndex < 0)
                inner = Loc.F("Tray.SummaryDone", rows, tray.Cols);
            else
                inner = string.Format(CultureInfo.InvariantCulture, "{0} × {1}", rows, tray.Cols);
            Summary = Loc.F("Tray.SummaryOfTray", ShownTray + 1, inner);
        }

        /// <summary>
        /// Dựng lưới theo <b>vị trí hình học</b> của ô trên tray — hàng trên xuống hàng dưới, mỗi hàng trái sang
        /// phải — chứ KHÔNG theo thứ tự máy ghé ô.
        ///
        /// <para>Trước 2026-09-24 lưới dựng theo thứ tự ghé, nên tray quét zigzag bị vẽ lộn: hàng chẵn hiện ngược,
        /// ô 2-8 nằm ở đầu trái (Tuấn phát hiện). Người vận hành nhìn lưới này để đối chiếu với cái tray thật trước
        /// mặt, nên lưới phải là ẢNH CỦA CÁI TRAY, không phải ảnh của đường đi. Quét zigzag giờ nhìn ra đúng như
        /// nó là: hàng đầu đầy dần từ trái sang, hàng sau đầy dần từ phải sang.</para>
        ///
        /// <para>Sổ nhiều tray: chỉ dựng các hàng của <see cref="ShownTray"/>. Chỉ số ô lấy theo hàng TRONG SỔ
        /// (<c>ShownTray·rows + r</c>) để thứ tự zigzag nối tiếp giữa các tray đúng như máy ghé; nhãn ô theo hàng trong tray.</para>
        /// </summary>
        private void Rebuild(TrayState tray, int rows)
        {
            Cells.Clear();
            Rows = rows;
            Cols = tray.Cols;
            _builtTray = ShownTray;
            _builtOrder = tray.ScanOrder;

            int r0 = ShownTray * rows;
            for (int r = 0; r < rows; r++)
                for (int c = 0; c < tray.Cols; c++)
                    Cells.Add(new TrayCellVm { Index = tray.IndexOf(r0 + r, c), Row = r, Col = c });
        }
    }
}
