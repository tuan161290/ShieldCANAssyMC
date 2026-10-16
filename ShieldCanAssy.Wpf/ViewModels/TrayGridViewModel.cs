using System;
using System.Collections.ObjectModel;
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
        /// <param name="slots">Số chỗ đặt tray của card — tray đầu vào 2 (bàn đặt hai tray từ 2026-10-15), tray lắp ráp 1.
        /// Mỗi chỗ một khối lưới trong <see cref="Groups"/>, xếp chồng Tray 1 trên Tray 2 (Tuấn 2026-10-16, thay nút Select
        /// xem từng tray). Tray 2 chưa teach vẫn có khối, lưới trống mờ kèm chữ "chưa teach".</param>
        /// <param name="toggleCell">Bấm một ô (chỉ số ô trong sổ) lúc AUTO — chọn/bỏ chọn, máy chỉ làm ô đã chọn (Tuấn
        /// 2026-10-16). Null = lưới chỉ để xem.</param>
        /// <param name="clearSelection">Nút "Bỏ chọn" — máy lại làm cả tray.</param>
        public TrayGridViewModel(string titleKey, string resetKey, ICommand resetCommand, int slots = 1,
                                 Action<int> toggleCell = null, Action clearSelection = null)
        {
            _titleKey = titleKey;
            _resetKey = resetKey;
            _slots = Math.Max(1, slots);
            ResetCommand = resetCommand;
            _cellClick = new RelayCommand<TrayCellVm>(cell =>
            {
                if (cell == null || toggleCell == null || !CanSelect) return;
                toggleCell(cell.Index);
                if (_lastTray != null) Sync(_lastTray, _lastScanned);
            });
            ClearSelectionCommand = new RelayCommand(() =>
            {
                clearSelection?.Invoke();
                if (_lastTray != null) Sync(_lastTray, _lastScanned);
            });
            // Hai lưới tray sống suốt đời App — đăng ký sự kiện tĩnh không rò rỉ.
            Loc.LanguageChanged += () =>
            {
                OnPropertyChanged(nameof(Title));
                OnPropertyChanged(nameof(ResetText));
                OnPropertyChanged(nameof(ClearSelectionText));
                if (_lastTray != null) Sync(_lastTray, _lastScanned);
            };
        }

        private readonly ICommand _cellClick;

        private bool _canSelect;
        /// <summary>Bấm ô để chọn được — chỉ ở AUTO (Tuấn 2026-10-16); MainViewModel đặt theo chế độ mỗi nhịp làm mới.</summary>
        public bool CanSelect { get => _canSelect; set => SetProperty(ref _canSelect, value); }

        private int _selectedCount;
        /// <summary>Số ô operator đã chọn — 0 là máy làm cả tray.</summary>
        public int SelectedCount
        {
            get => _selectedCount;
            private set
            {
                if (!SetProperty(ref _selectedCount, value)) return;
                OnPropertyChanged(nameof(HasSelection));
                OnPropertyChanged(nameof(ClearSelectionText));
            }
        }
        public bool HasSelection => SelectedCount > 0;
        public string ClearSelectionText => Loc.F("Tray.ClearSelection", SelectedCount);
        public ICommand ClearSelectionCommand { get; }

        private readonly string _titleKey, _resetKey;
        private readonly int _slots;
        private TrayState _lastTray;
        private int[] _lastScanned;

        public string Title => Loc.T(_titleKey);

        /// <summary>Nút nạp lại nằm chung card với lưới (Tuấn 2026-10-07).</summary>
        public string ResetText => Loc.T(_resetKey);
        public ICommand ResetCommand { get; }

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

        /// <summary>Một khối lưới mỗi chỗ đặt tray, Tray 1 trước.</summary>
        public ObservableCollection<TrayGroupVm> Groups { get; } = new ObservableCollection<TrayGroupVm>();

        private string _summary;
        public string Summary { get => _summary; private set => SetProperty(ref _summary, value); }

        // Lưới đang dựng theo kích thước nào, thứ tự quét nào — đổi một trong các thứ đó là phải dựng lại.
        private int _builtRows = -1, _builtCols = -1;
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

            if (_builtRows != rows || _builtCols != tray.Cols || _builtOrder != tray.ScanOrder || Groups.Count != _slots)
                Rebuild(tray, rows);

            // Ô thứ i trong danh sách nằm ở VỊ TRÍ HÌNH HỌC thứ i, còn dữ liệu của nó nằm ở
            // tray.Cells[cell.Index] — hai thứ tự đó khác nhau khi tray quét zigzag. Khối của tray chưa có trong sổ (Tray 2
            // chưa teach) có chỉ số nằm ngoài sổ: ô để trống.
            var selected = tray.Selected;       // đọc một lần — list không bao giờ bị sửa tại chỗ
            bool anySelected = selected != null && selected.Count > 0;
            SelectedCount = anySelected ? selected.Count : 0;
            foreach (var g in Groups)
            {
                foreach (var cell in g.Cells)
                {
                    if (cell.Index < 0 || cell.Index >= tray.Cells.Count) continue;
                    cell.State = tray.Cells[cell.Index];
                    cell.IsCurrent = cell.Index == tray.CurrentIndex;
                    cell.IsScanned = scanned != null && System.Array.IndexOf(scanned, cell.Index) >= 0;
                    cell.IsSelected = anySelected && selected.Contains(cell.Index);
                    cell.IsOutOfScope = anySelected && !cell.IsSelected;
                }
                g.IsTaught = g.Tray < trays;
                g.IsActive = g.Tray == active;
                if (g.HasCaption)
                    g.Caption = g.IsTaught ? Loc.F("Tray.N", g.Tray + 1) : Loc.F("Tray.SummaryOfTray", g.Tray + 1, Loc.T("Tray.NotTaught"));
            }

            if (_slots == 1)
                Summary = tray.CurrentIndex >= 0
                    ? Loc.F("Tray.SummaryCurrent", tray.Rows, tray.Cols, tray.CurrentIndex + 1, tray.CellCount)
                    : Loc.F("Tray.SummaryDone", rows, tray.Cols);
            else
                // Tiêu đề card: ô hiện tại tính trong tray đang chạy — tray chứa ô hiện tại.
                Summary = active >= 0
                    ? Loc.F("Tray.SummaryOfTray", active + 1,
                            Loc.F("Tray.SummaryCurrent", rows, tray.Cols, tray.CurrentIndex - active * perTray + 1, perTray))
                    : Loc.F("Tray.SummaryDone", rows, tray.Cols);
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
        /// <para>Sổ nhiều tray: khối thứ t lấy chỉ số ô theo hàng TRONG SỔ (<c>t·rows + r</c>) để thứ tự zigzag nối tiếp
        /// giữa các tray đúng như máy ghé; nhãn ô theo hàng trong tray.</para>
        /// </summary>
        private void Rebuild(TrayState tray, int rows)
        {
            Groups.Clear();
            _builtRows = rows;
            _builtCols = tray.Cols;
            _builtOrder = tray.ScanOrder;

            for (int t = 0; t < _slots; t++)
            {
                var g = new TrayGroupVm(t, rows, tray.Cols, _slots > 1);
                int r0 = t * rows;
                for (int r = 0; r < rows; r++)
                    for (int c = 0; c < tray.Cols; c++)
                        g.Cells.Add(new TrayCellVm { Index = tray.IndexOf(r0 + r, c), Row = r, Col = c, ClickCommand = _cellClick });
                Groups.Add(g);
            }
        }
    }

    /// <summary>Lưới của một tray trong card — card tray đầu vào có hai khối, Tray 1 trên Tray 2 (Tuấn 2026-10-16).</summary>
    public class TrayGroupVm : ObservableObject
    {
        public TrayGroupVm(int tray, int rows, int cols, bool hasCaption)
        {
            Tray = tray;
            Rows = rows;
            Cols = cols;
            HasCaption = hasCaption;
        }

        /// <summary>Chỗ đặt tray, 0 = Tray 1.</summary>
        public int Tray { get; }
        public int Rows { get; }
        public int Cols { get; }

        /// <summary>Có dòng tên tray trên lưới — chỉ card nhiều chỗ đặt tray.</summary>
        public bool HasCaption { get; }

        private string _caption;
        public string Caption { get => _caption; set => SetProperty(ref _caption, value); }

        /// <summary>Tray có trong sổ — Tray 2 chỉ khi đã teach; chưa teach thì lưới vẽ mờ.</summary>
        private bool _isTaught = true;
        public bool IsTaught { get => _isTaught; set => SetProperty(ref _isTaught, value); }

        /// <summary>Tray chứa ô hiện tại — tên tray in đậm.</summary>
        private bool _isActive;
        public bool IsActive { get => _isActive; set => SetProperty(ref _isActive, value); }

        public ObservableCollection<TrayCellVm> Cells { get; } = new ObservableCollection<TrayCellVm>();
    }
}
