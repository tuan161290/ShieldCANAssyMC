using ShieldCanAssy.Wpf.Mvvm;

namespace ShieldCanAssy.Wpf.Models
{
    /// <summary>Một đèn DI hoặc DO trên bảng I/O của trang Manual.</summary>
    public class IoLampVm : ObservableObject
    {
        public int Bit { get; set; }
        public string Name { get; set; }
        public bool IsInput { get; set; }

        private bool _on;
        public bool On { get => _on; set => SetProperty(ref _on, value); }

        // Ezi-IO đánh số kênh theo hex (0x00–0x1F) trong tool cấu hình và tài liệu của hãng —
        // hiển thị theo đúng định dạng đó để đối chiếu trực tiếp, không phải quy đổi trong đầu.
        public string Caption => $"{(IsInput ? "DI" : "DO")} 0x{Bit:X2}  {Name}";
    }
}
