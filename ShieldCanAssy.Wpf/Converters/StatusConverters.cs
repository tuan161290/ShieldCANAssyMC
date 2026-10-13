using System;
using System.Globalization;
using System.Windows;
using System.Windows.Data;
using System.Windows.Media;
using ShieldCanAssy.Core.Infor;
using ShieldCanAssy.Wpf.Services;

namespace ShieldCanAssy.Wpf.Converters
{
    /// <summary>Loại hộp thoại (overlay thay MessageBox — xem DialogHost) → màu icon.</summary>
    public class DialogKindToBrushConverter : IValueConverter
    {
        public object Convert(object value, Type t, object p, CultureInfo c)
        {
            if (!(value is DialogKind kind)) return Brushes.Gray;
            switch (kind)
            {
                case DialogKind.Warn:    return new SolidColorBrush(Color.FromRgb(0x9D, 0x5D, 0x00));
                case DialogKind.Error:   return new SolidColorBrush(Color.FromRgb(0xC4, 0x2B, 0x1C));
                case DialogKind.Confirm: return new SolidColorBrush(Color.FromRgb(0x00, 0x78, 0xD4));
                default:                 return new SolidColorBrush(Color.FromRgb(0x0F, 0x7B, 0x0F));
            }
        }

        public object ConvertBack(object v, Type t, object p, CultureInfo c) => throw new NotSupportedException();
    }

    /// <summary>Loại hộp thoại → glyph hiển thị cạnh tiêu đề.</summary>
    public class DialogKindToGlyphConverter : IValueConverter
    {
        public object Convert(object value, Type t, object p, CultureInfo c)
        {
            if (!(value is DialogKind kind)) return "";
            switch (kind)
            {
                case DialogKind.Warn:    return "⚠";
                case DialogKind.Error:   return "⛔";
                case DialogKind.Confirm: return "❓";
                default:                 return "ℹ";
            }
        }

        public object ConvertBack(object v, Type t, object p, CultureInfo c) => throw new NotSupportedException();
    }
    /// <summary>Mức alarm → màu nền dòng/thẻ.</summary>
    public class AlarmLevelToBrushConverter : IValueConverter
    {
        public object Convert(object value, Type t, object p, CultureInfo c)
        {
            if (!(value is EAlarmLevel level)) return Brushes.Transparent;
            switch (level)
            {
                case EAlarmLevel.Fatal:   return new SolidColorBrush(Color.FromRgb(0xC4, 0x2B, 0x1C));
                case EAlarmLevel.Error:   return new SolidColorBrush(Color.FromRgb(0xE9, 0x49, 0x49));
                case EAlarmLevel.Warning: return new SolidColorBrush(Color.FromRgb(0x9D, 0x5D, 0x00));
                default:                  return new SolidColorBrush(Color.FromRgb(0x60, 0x60, 0x60));
            }
        }

        public object ConvertBack(object v, Type t, object p, CultureInfo c) => throw new NotSupportedException();
    }

    /// <summary>
    /// Đèn tháp (<see cref="ETowerLamp"/>) → màu accent đặc cho header MainWindow — CỐ Ý bind
    /// thẳng vào <c>Status.TowerLamp</c> (nguồn sự thật, do <c>McControl.ComputeLamp</c> tính,
    /// đã gộp cả tình huống AlarmManager.HasWarning lúc Idle) thay vì tự suy lại từ EMcState lần
    /// hai — tránh accent header và đèn tháp vật lý nói hai màu khác nhau cho cùng một trạng thái
    /// máy (PartronCamTestAuto.Wpf mắc lỗi này: converter tự switch lại EMcState, dễ lệch dần với
    /// McFunction.UpdateTowerLamp mỗi khi state machine đổi). Blink chỉ là hiệu ứng nháy trên đèn
    /// vật lý — accent header tĩnh nên gộp về đúng màu gốc (Green/Yellow/Red).
    /// </summary>
    public class TowerLampToBrushConverter : IValueConverter
    {
        public object Convert(object value, Type t, object p, CultureInfo c)
        {
            if (!(value is ETowerLamp lamp)) return new SolidColorBrush(Color.FromRgb(0x60, 0x60, 0x60));
            switch (lamp)
            {
                case ETowerLamp.Green:
                case ETowerLamp.GreenBlink:  return new SolidColorBrush(Color.FromRgb(0x38, 0x98, 0x53));
                case ETowerLamp.Red:
                case ETowerLamp.RedBlink:    return new SolidColorBrush(Color.FromRgb(0xC4, 0x2B, 0x1C));
                case ETowerLamp.Yellow:
                case ETowerLamp.YellowBlink: return new SolidColorBrush(Color.FromRgb(0x9D, 0x5D, 0x00));
                default:                     return new SolidColorBrush(Color.FromRgb(0x60, 0x60, 0x60));
            }
        }

        public object ConvertBack(object v, Type t, object p, CultureInfo c) => throw new NotSupportedException();
    }

    /// <summary>Cùng tông với <see cref="TowerLampToBrushConverter"/> nhưng chỉ là lớp TINT mờ —
    /// dùng cho nền header/badge kiểu Windows 11 (viền + chữ đậm màu, nền chỉ phủ mờ chứ không
    /// phải khối màu đặc).</summary>
    public class TowerLampToTintBrushConverter : IValueConverter
    {
        private static Brush Tint(byte r, byte g, byte b) => new SolidColorBrush(Color.FromArgb(0x1C, r, g, b));

        public object Convert(object value, Type t, object p, CultureInfo c)
        {
            if (!(value is ETowerLamp lamp)) return Tint(0x60, 0x60, 0x60);
            switch (lamp)
            {
                case ETowerLamp.Green:
                case ETowerLamp.GreenBlink:  return Tint(0x38, 0x98, 0x53);
                case ETowerLamp.Red:
                case ETowerLamp.RedBlink:    return Tint(0xC4, 0x2B, 0x1C);
                case ETowerLamp.Yellow:
                case ETowerLamp.YellowBlink: return Tint(0x9D, 0x5D, 0x00);
                default:                     return Tint(0x60, 0x60, 0x60);
            }
        }

        public object ConvertBack(object v, Type t, object p, CultureInfo c) => throw new NotSupportedException();
    }

    /// <summary>Trạng thái máy → màu banner trạng thái ở trang Main.</summary>
    public class McStateToBrushConverter : IValueConverter
    {
        public object Convert(object value, Type t, object p, CultureInfo c)
        {
            if (!(value is EMcState s)) return Brushes.Gray;
            switch (s)
            {
                case EMcState.Running:      return new SolidColorBrush(Color.FromRgb(0x38, 0x98, 0x53));
                case EMcState.Idle:         return new SolidColorBrush(Color.FromRgb(0x0F, 0x7B, 0x0F));
                case EMcState.Armed:        return new SolidColorBrush(Color.FromRgb(0x9D, 0x5D, 0x00));
                case EMcState.Paused:
                case EMcState.Held:
                case EMcState.Pausing:
                case EMcState.WaitOperator: return new SolidColorBrush(Color.FromRgb(0x9D, 0x5D, 0x00));
                case EMcState.Alarm:        return new SolidColorBrush(Color.FromRgb(0xC4, 0x2B, 0x1C));
                case EMcState.Emg:          return new SolidColorBrush(Color.FromRgb(0x8B, 0x00, 0x00));
                case EMcState.Homing:       return new SolidColorBrush(Color.FromRgb(0x00, 0x78, 0xD4));
                default:                    return new SolidColorBrush(Color.FromRgb(0x60, 0x60, 0x60));
            }
        }

        public object ConvertBack(object v, Type t, object p, CultureInfo c) => throw new NotSupportedException();
    }

    /// <summary>
    /// Đèn tháp → màu hiển thị. Nhận ConverterParameter "Red"/"Yellow"/"Green" để một
    /// converter phục vụ cả ba bóng đèn trên UI.
    /// </summary>
    public class TowerLampConverter : IValueConverter
    {
        private static readonly Brush Off = new SolidColorBrush(Color.FromRgb(0x3A, 0x3A, 0x3A));

        public object Convert(object value, Type t, object p, CultureInfo c)
        {
            if (!(value is ETowerLamp lamp) || !(p is string which)) return Off;

            switch (which)
            {
                case "Red":
                    return lamp == ETowerLamp.Red || lamp == ETowerLamp.RedBlink
                        ? new SolidColorBrush(Color.FromRgb(0xE9, 0x49, 0x49)) : Off;
                case "Yellow":
                    return lamp == ETowerLamp.Yellow || lamp == ETowerLamp.YellowBlink
                        ? new SolidColorBrush(Color.FromRgb(0xFF, 0xC8, 0x00)) : Off;
                case "Green":
                    return lamp == ETowerLamp.Green || lamp == ETowerLamp.GreenBlink
                        ? new SolidColorBrush(Color.FromRgb(0x38, 0x98, 0x53)) : Off;
                default:
                    return Off;
            }
        }

        public object ConvertBack(object v, Type t, object p, CultureInfo c) => throw new NotSupportedException();
    }

    /// <summary>bool đảo → Visibility. Dùng cho "hiện khi KHÔNG có alarm".</summary>
    public class InverseBoolToVisibilityConverter : IValueConverter
    {
        public object Convert(object value, Type t, object p, CultureInfo c)
            => (value is bool b && b) ? Visibility.Collapsed : Visibility.Visible;

        public object ConvertBack(object v, Type t, object p, CultureInfo c) => throw new NotSupportedException();
    }

    /// <summary>bool đảo → bool. Dùng cho IsEnabled="{Binding Busy, Converter=...}" (khoá nút lúc đang chạy).</summary>
    public class InverseBooleanConverter : IValueConverter
    {
        public object Convert(object value, Type t, object p, CultureInfo c) => !(value is bool b && b);
        public object ConvertBack(object value, Type t, object p, CultureInfo c) => !(value is bool b && b);
    }

    /// <summary>Số lượng &gt; 0 → hiện. Dùng cho banner alarm.</summary>
    public class CountToVisibilityConverter : IValueConverter
    {
        public object Convert(object value, Type t, object p, CultureInfo c)
            => (value is int n && n > 0) ? Visibility.Visible : Visibility.Collapsed;

        public object ConvertBack(object v, Type t, object p, CultureInfo c) => throw new NotSupportedException();
    }

    /// <summary>Chuỗi rỗng → ẩn. Dùng cho dòng nhắc Message ở trang Main.</summary>
    public class StringToVisibilityConverter : IValueConverter
    {
        public object Convert(object value, Type t, object p, CultureInfo c)
            => string.IsNullOrWhiteSpace(value as string) ? Visibility.Collapsed : Visibility.Visible;

        public object ConvertBack(object v, Type t, object p, CultureInfo c) => throw new NotSupportedException();
    }

    /// <summary>null → hiện, có giá trị → ẩn. Dùng cho chữ "(chưa có ảnh)" đè dưới khung Image
    /// khi CAM chưa chụp lần nào (chưa Trigger, hoặc đang chạy mô phỏng).</summary>
    public class NullToVisibilityConverter : IValueConverter
    {
        public object Convert(object value, Type t, object p, CultureInfo c)
            => value == null ? Visibility.Visible : Visibility.Collapsed;

        public object ConvertBack(object v, Type t, object p, CultureInfo c) => throw new NotSupportedException();
    }

    /// <summary>True nếu 2 giá trị cùng tham chiếu — dùng để tô sáng một item đang chọn trong
    /// ItemsControl (vd. lưới ô Teaching) bằng cách so item hiện tại với property "đang chọn" của
    /// ViewModel, qua MultiBinding + DataTrigger (DataTrigger đơn không so được 2 binding).</summary>
    public class ReferenceEqualsConverter : IMultiValueConverter
    {
        public object Convert(object[] values, Type t, object p, CultureInfo c)
            => values.Length == 2 && ReferenceEquals(values[0], values[1]);

        public object[] ConvertBack(object v, Type[] t, object p, CultureInfo c) => throw new NotSupportedException();
    }
}
