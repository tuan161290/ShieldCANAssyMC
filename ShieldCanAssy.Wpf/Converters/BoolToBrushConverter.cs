using System;
using System.Globalization;
using System.Windows.Data;
using System.Windows.Media;

namespace ShieldCanAssy.Wpf.Converters
{
    /// <summary>
    /// Chuyển bool → Brush. Dùng cho đèn trạng thái thiết bị:
    /// true → TrueBrush, false → FalseBrush — mặc định khớp
    /// SYSTEM_VARS.CUSTOM_LIGHT_GREEN(134,198,145)/CUSTOM_LIGHT_RED(233,73,73) của bản gốc
    /// (LbMotionCardStt/LbIoCardStt/LbLaserDeviceStt/LbCamStt.BackColor trong FrmMain.UpdateStatus).
    /// </summary>
    public class BoolToBrushConverter : IValueConverter
    {
        public Brush TrueBrush { get; set; } = new SolidColorBrush(Color.FromRgb(0x86, 0xC6, 0x91));
        public Brush FalseBrush { get; set; } = new SolidColorBrush(Color.FromRgb(0xE9, 0x49, 0x49));

        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
            => (value is bool b && b) ? TrueBrush : FalseBrush;

        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
            => throw new NotSupportedException();
    }
}
