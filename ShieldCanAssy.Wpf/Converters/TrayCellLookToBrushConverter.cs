using System;
using System.Globalization;
using System.Windows.Data;
using System.Windows.Media;
using ShieldCanAssy.Wpf.Models;

namespace ShieldCanAssy.Wpf.Converters
{
    /// <summary>
    /// <see cref="ETrayCellLook"/> → Brush cho ô tray trang Main. Không có mã màu nào ở đây: từng brush gán trong App.xaml
    /// từ Themes/Palette.xaml (TrayCell*Brush), như <see cref="BoolToBrushConverter"/>. Converter một đầu vào, không
    /// MultiBinding/DataTrigger — WinUI 3 cũng có IValueConverter nên port sang giữ nguyên khuôn này (Tuấn 2026-10-08).
    /// </summary>
    public class TrayCellLookToBrushConverter : IValueConverter
    {
        public Brush Empty { get; set; }
        public Brush Scanned { get; set; }
        public Brush Loaded { get; set; }
        public Brush Processing { get; set; }
        public Brush Ok { get; set; }
        public Brush Ng { get; set; }
        public Brush Skipped { get; set; }

        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            switch (value is ETrayCellLook look ? look : ETrayCellLook.Empty)
            {
                case ETrayCellLook.Scanned:    return Scanned;
                case ETrayCellLook.Loaded:     return Loaded;
                case ETrayCellLook.Processing: return Processing;
                case ETrayCellLook.Ok:         return Ok;
                case ETrayCellLook.Ng:         return Ng;
                case ETrayCellLook.Skipped:    return Skipped;
                default:                       return Empty;
            }
        }

        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
            => throw new NotSupportedException();
    }
}
