using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace ShieldCanAssy.Utility
{
    /// <summary>
    /// Lớp nền cho các đối tượng trạng thái của Core được bind thẳng lên WPF
    /// (McStatus, AxisStatus, IoStatus...). Khác với ObservableObject bên tầng Wpf ở chỗ
    /// PropertyChanged được marshal về UI thread qua <see cref="UiThread"/> — các đối tượng
    /// này bị ghi từ thread nền (vòng quét motion/IO) chứ không chỉ từ UI.
    /// </summary>
    public class Bindable : INotifyPropertyChanged
    {
        public event PropertyChangedEventHandler PropertyChanged;

        protected virtual void OnPropertyChanged([CallerMemberName] string propertyName = null)
        {
            var handler = PropertyChanged;
            if (handler == null) return;
            UiThread.Run(() => handler(this, new PropertyChangedEventArgs(propertyName)));
        }

        protected bool SetProperty<T>(ref T field, T value, [CallerMemberName] string propertyName = null)
        {
            if (EqualityComparer<T>.Default.Equals(field, value))
                return false;
            field = value;
            OnPropertyChanged(propertyName);
            return true;
        }

        /// <summary>Bắn PropertyChanged cho property tính toán (không có backing field).</summary>
        public void Notify(string propertyName) => OnPropertyChanged(propertyName);
    }
}
