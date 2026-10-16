using ShieldCanAssy.Core.Infor;
using ShieldCanAssy.Wpf.Mvvm;

namespace ShieldCanAssy.Wpf.Models
{
    /// <summary>
    /// Ô tray nhìn ra sao trên trang Main — gộp <see cref="ECellState"/> với cờ đã quét thành MỘT giá trị, để View chỉ cần
    /// một converter một đầu vào (<c>TrayCellLookToBrushConverter</c>, màu khai ở Themes/Palette.xaml). Không dùng
    /// MultiBinding: WinUI 3 không có, port sang đó thì giữ nguyên enum này và converter (Tuấn 2026-10-08).
    /// </summary>
    public enum ETrayCellLook
    {
        Empty,
        /// <summary>Ô tray lắp ráp còn trống nhưng đã quét trước OK (LocateTop thấy chi tiết đích).</summary>
        Scanned,
        Loaded,
        Processing,
        Ok,
        Ng,
        Skipped
    }

    /// <summary>Một ô trên lưới tray hiển thị ở trang Main.</summary>
    public class TrayCellVm : ObservableObject
    {
        public int Index { get; set; }
        public int Row { get; set; }
        public int Col { get; set; }

        public string Label => $"{Row + 1}-{Col + 1}";

        private ECellState _state = ECellState.Empty;
        public ECellState State
        {
            get => _state;
            set { if (SetProperty(ref _state, value)) OnPropertyChanged(nameof(Look)); }
        }

        private bool _isCurrent;
        public bool IsCurrent
        {
            get => _isCurrent;
            set => SetProperty(ref _isCurrent, value);
        }

        /// <summary>Ô tray lắp ráp còn trống đã quét trước OK (LocateTop thấy chi tiết đích) — tô xanh nhạt như ô có
        /// hàng của tray đầu vào (Tuấn 2026-10-07). Chỉ để hiển thị, trạng thái ô vẫn là Empty.</summary>
        private bool _isScanned;
        public bool IsScanned
        {
            get => _isScanned;
            set { if (SetProperty(ref _isScanned, value)) OnPropertyChanged(nameof(Look)); }
        }

        /// <summary>Operator đã chọn ô này lúc AUTO — chu trình chỉ làm trên ô đã chọn (Tuấn 2026-10-16). Viền đậm.</summary>
        private bool _isSelected;
        public bool IsSelected
        {
            get => _isSelected;
            set => SetProperty(ref _isSelected, value);
        }

        /// <summary>Tray có ô được chọn mà ô này không nằm trong đó — chu trình bỏ qua, vẽ mờ.</summary>
        private bool _isOutOfScope;
        public bool IsOutOfScope
        {
            get => _isOutOfScope;
            set => SetProperty(ref _isOutOfScope, value);
        }

        /// <summary>Bấm vào ô — chọn/bỏ chọn, chỉ có tác dụng ở AUTO (<c>TrayGridViewModel.CanSelect</c>).</summary>
        public System.Windows.Input.ICommand ClickCommand { get; set; }

        /// <summary>Màu ô do View quyết định từ giá trị này — ViewModel không giữ Brush (MVVM, 2026-10-08).</summary>
        public ETrayCellLook Look
        {
            get
            {
                switch (State)
                {
                    case ECellState.Loaded:     return ETrayCellLook.Loaded;
                    case ECellState.Processing: return ETrayCellLook.Processing;
                    case ECellState.Ok:         return ETrayCellLook.Ok;
                    case ECellState.Ng:         return ETrayCellLook.Ng;
                    case ECellState.Skipped:    return ETrayCellLook.Skipped;
                    default:                    return IsScanned ? ETrayCellLook.Scanned : ETrayCellLook.Empty;
                }
            }
        }
        // Viền ô vẽ trong MainView.xaml (viền elevation kiểu Fluent), ô hiện tại đánh dấu bằng quầng xanh.
    }
}
