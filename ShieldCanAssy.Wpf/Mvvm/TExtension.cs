using System;
using System.Windows.Data;
using System.Windows.Markup;
using ShieldCanAssy.Core.Lang;

namespace ShieldCanAssy.Wpf.Mvvm
{
    /// <summary>
    /// <c>{l:T Khoá}</c> — chữ hiển thị theo ngôn ngữ đang chọn (<see cref="Loc"/>), với
    /// <c>xmlns:l="clr-namespace:ShieldCanAssy.Wpf.Mvvm"</c>. Thực chất là một binding một chiều vào
    /// <c>Loc.Instance[Khoá]</c>, nên đổi ngôn ngữ thì nhãn tự đổi, và dùng được cả trong Setter của Style lẫn trong
    /// DataTemplate.
    /// </summary>
    [MarkupExtensionReturnType(typeof(object))]
    public class TExtension : MarkupExtension
    {
        public TExtension() { }

        public TExtension(string key) { Key = key; }

        [ConstructorArgument("key")]
        public string Key { get; set; }

        public override object ProvideValue(IServiceProvider serviceProvider)
        {
            var b = new Binding("[" + Key + "]") { Source = Loc.Instance, Mode = BindingMode.OneWay };
            return b.ProvideValue(serviceProvider);
        }
    }
}
