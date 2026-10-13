using System;
using System.ComponentModel;
using System.Globalization;
using System.Resources;

namespace ShieldCanAssy.Core.Lang
{
    public enum ELanguage { Vi, En }

    /// <summary>
    /// Chữ hiển thị theo ngôn ngữ đang chọn — tiếng Việt hoặc tiếng Anh, đổi ngay khi App đang chạy (Tuấn 2026-10-09).
    ///
    /// <para><b>Chuỗi nằm ở hai file .resx</b> trong thư mục này: <c>StringsVi.resx</c> (bản gốc) và <c>StringsEn.resx</c>,
    /// cùng một bộ khoá. Hai bảng nhúng thẳng vào ShieldCanAssy.Core.dll — không có thư mục <c>en\</c> nào phải chép kèm
    /// khi triển khai. Cùng định dạng với .resw của WinUI 3 nên port sang đó chỉ cần đổi tên file.</para>
    ///
    /// <para>XAML dùng <c>{l:T Khoá}</c> (ShieldCanAssy.Wpf.Mvvm.TExtension) — binding vào indexer của
    /// <see cref="Instance"/>, nên đổi ngôn ngữ là mọi nhãn tự đổi theo. Code dùng <see cref="T"/> / <see cref="F"/> ngay
    /// lúc dựng câu; câu đã dựng xong (thông báo, trạng thái) giữ ngôn ngữ lúc dựng. Log (app.log) không đi qua đây —
    /// luôn tiếng Việt.</para>
    ///
    /// <para>Thiếu bản tiếng Anh thì hiện bản tiếng Việt; thiếu cả khoá thì hiện chính tên khoá, để nhìn màn hình là
    /// thấy chỗ quên.</para>
    /// </summary>
    public sealed class Loc : INotifyPropertyChanged
    {
        private static readonly ResourceManager Vi =
            new ResourceManager("ShieldCanAssy.Core.Lang.StringsVi", typeof(Loc).Assembly);
        private static readonly ResourceManager En =
            new ResourceManager("ShieldCanAssy.Core.Lang.StringsEn", typeof(Loc).Assembly);

        public static Loc Instance { get; } = new Loc();

        private Loc() { }

        public static ELanguage Language { get; private set; } = ELanguage.Vi;

        /// <summary>Báo cho ViewModel dựng lại những chữ nó tự ghép (tiêu đề, nhãn có số) — chạy trên luồng gọi
        /// <see cref="SetLanguage"/>, tức luồng giao diện.</summary>
        public static event Action LanguageChanged;

        public event PropertyChangedEventHandler PropertyChanged;

        /// <summary>Mã lưu trong system.json: "vi" / "en".</summary>
        public static string Code(ELanguage lang) => lang == ELanguage.En ? "en" : "vi";

        public static ELanguage Parse(string code) =>
            string.Equals(code, "en", StringComparison.OrdinalIgnoreCase) ? ELanguage.En : ELanguage.Vi;

        public static void SetLanguage(ELanguage lang)
        {
            if (lang == Language) return;
            Language = lang;
            // "Item[]" là tên WPF dùng cho mọi binding vào indexer — một lần báo là mọi nhãn {l:T …} đọc lại.
            Instance.PropertyChanged?.Invoke(Instance, new PropertyChangedEventArgs("Item[]"));
            LanguageChanged?.Invoke();
        }

        public string this[string key] => T(key);

        /// <summary>Chữ của <paramref name="key"/> theo ngôn ngữ đang chọn.</summary>
        public static string T(string key)
        {
            if (string.IsNullOrEmpty(key)) return "";
            string s = null;
            try
            {
                if (Language == ELanguage.En) s = En.GetString(key, CultureInfo.InvariantCulture);
                if (string.IsNullOrEmpty(s)) s = Vi.GetString(key, CultureInfo.InvariantCulture);
            }
            catch (MissingManifestResourceException) { }
            return s ?? key;
        }

        /// <summary><see cref="string.Format(string, object[])"/> trên chữ của <paramref name="key"/> — giữ đúng cách
        /// định dạng số của code cũ (theo culture của máy). Bản dịch sai số chỗ {n} thì trả nguyên mẫu, không ném lỗi.</summary>
        public static string F(string key, params object[] args)
        {
            string fmt = T(key);
            try { return string.Format(fmt, args); }
            catch (FormatException) { return fmt; }
        }
    }
}
