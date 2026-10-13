using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace ShieldCanAssy.Wpf.Mvvm
{
    /// <summary>
    /// Lớp nền cho ViewModel: cài đặt INotifyPropertyChanged.
    /// Thay cho việc kế thừa hành vi binding của DevExpress.
    /// </summary>
    public abstract class ObservableObject : INotifyPropertyChanged
    {
        public event PropertyChangedEventHandler PropertyChanged;

        protected void OnPropertyChanged([CallerMemberName] string propertyName = null)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        }

        /// <summary>Gán giá trị và raise PropertyChanged nếu thay đổi. Trả về true nếu đổi.</summary>
        protected bool SetProperty<T>(ref T field, T value, [CallerMemberName] string propertyName = null)
        {
            if (EqualityComparer<T>.Default.Equals(field, value))
                return false;
            field = value;
            OnPropertyChanged(propertyName);
            return true;
        }
    }
}
