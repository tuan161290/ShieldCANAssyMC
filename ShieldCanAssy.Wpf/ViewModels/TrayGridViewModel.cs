using System.Collections.ObjectModel;
using System.Windows.Input;
using ShieldCanAssy.Core.Data;
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
        public TrayGridViewModel(string titleKey, string resetKey, ICommand resetCommand)
        {
            _titleKey = titleKey;
            _resetKey = resetKey;
            ResetCommand = resetCommand;
            // Hai lưới tray sống suốt đời App — đăng ký sự kiện tĩnh không rò rỉ.
            Loc.LanguageChanged += () =>
            {
                OnPropertyChanged(nameof(Title));
                OnPropertyChanged(nameof(ResetText));
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

        // Bộ đếm của riêng tray này (Tuấn 2026-10-07) — số lấy từ RunningData.InputCountOk/AssyCountOk…, nạp lại tray
        // không xoá, chỉ Clear sản lượng mới xoá. Tổng = OK + NG.
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

        /// <param name="scanned">Chỉ số các ô đã quét trước OK (chỉ tray lắp ráp) — tô xanh nhạt nếu ô còn trống.</param>
        public void Sync(TrayState tray, int[] scanned = null)
        {
            if (tray == null) return;
            _lastTray = tray;
            _lastScanned = scanned;

            if (Rows != tray.Rows || Cols != tray.Cols || Cells.Count != tray.CellCount)
                Rebuild(tray);

            // Ô thứ i trong danh sách nằm ở VỊ TRÍ HÌNH HỌC thứ i, còn dữ liệu của nó nằm ở
            // tray.Cells[cell.Index] — hai thứ tự đó khác nhau khi tray quét zigzag.
            foreach (var cell in Cells)
            {
                if (cell.Index < 0 || cell.Index >= tray.Cells.Count) continue;
                cell.State = tray.Cells[cell.Index];
                cell.IsCurrent = cell.Index == tray.CurrentIndex;
                cell.IsScanned = scanned != null && System.Array.IndexOf(scanned, cell.Index) >= 0;
            }

            Summary = tray.CurrentIndex >= 0
                ? Loc.F("Tray.SummaryCurrent", tray.Rows, tray.Cols, tray.CurrentIndex + 1, tray.CellCount)
                : Loc.F("Tray.SummaryDone", tray.Rows, tray.Cols);
        }

        /// <summary>
        /// Dựng lưới theo <b>vị trí hình học</b> của ô trên tray — hàng trên xuống hàng dưới, mỗi hàng trái sang
        /// phải — chứ KHÔNG theo thứ tự máy ghé ô.
        ///
        /// <para>Trước 2026-09-24 lưới dựng theo thứ tự ghé, nên tray quét zigzag bị vẽ lộn: hàng chẵn hiện ngược,
        /// ô 2-8 nằm ở đầu trái (Tuấn phát hiện). Người vận hành nhìn lưới này để đối chiếu với cái tray thật trước
        /// mặt, nên lưới phải là ẢNH CỦA CÁI TRAY, không phải ảnh của đường đi. Quét zigzag giờ nhìn ra đúng như
        /// nó là: hàng đầu đầy dần từ trái sang, hàng sau đầy dần từ phải sang.</para>
        /// </summary>
        private void Rebuild(TrayState tray)
        {
            Cells.Clear();
            Rows = tray.Rows;
            Cols = tray.Cols;

            for (int r = 0; r < tray.Rows; r++)
                for (int c = 0; c < tray.Cols; c++)
                    Cells.Add(new TrayCellVm { Index = tray.IndexOf(r, c), Row = r, Col = c });
        }
    }
}
