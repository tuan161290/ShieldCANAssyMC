#include "pch.h"
#include "Ui/SettingPageFields.h"

#include "Utility/Log/LogWorker.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
namespace WF = winrt::Windows::Foundation;
namespace NF = winrt::Windows::Globalization::NumberFormatting;

namespace ui::settingpage
{
    NumberToggleGroup::NumberToggleGroup() : m_loading(std::make_shared<bool>(false)) {}

    void NumberToggleGroup::SetupNumberBox(NumberBox const& box, int minFractionDigits)
    {
        // Dấu chấm thập phân cố định — máy có thể đặt vùng dùng dấu phẩy; system.json và bản WPF đều dùng dấu chấm.
        NF::DecimalFormatter formatter(single_threaded_vector<hstring>({ L"en-US" }), L"US");
        formatter.IntegerDigits(1);
        formatter.FractionDigits(minFractionDigits);
        formatter.IsGrouped(false);
        formatter.IsDecimalPointAlwaysDisplayed(false);
        box.NumberFormatter(formatter);
        box.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Hidden);
        box.AcceptsExpression(false);
        box.ValidationMode(NumberBoxValidationMode::InvalidInputOverwritten);
    }

    void NumberToggleGroup::AddNumber(NumberBox const& box, std::string label, std::function<double()> get,
                                      std::function<bool(double)> set)
    {
        auto field = std::make_shared<NumberField>();
        field->Box = box;
        field->Label = std::move(label);
        field->Get = std::move(get);
        field->Set = std::move(set);
        m_numbers.push_back(field);

        // weak_ptr: handler nằm trên chính NumberBox, giữ shared_ptr thì thành vòng tham chiếu ô ↔ field.
        std::weak_ptr<NumberField> weak = field;
        box.ValueChanged([weak, loading = m_loading](NumberBox const& sender, NumberBoxValueChangedEventArgs const& e) {
            if (*loading) return;
            auto f = weak.lock();
            if (!f) return;
            double value = e.NewValue();
            bool ok = false;
            if (!std::isnan(value))
            {
                try
                {
                    ok = f->Set(value);
                }
                catch (const std::exception& ex)
                {
                    sca::LogWorker::Instance().RecordLog("Setting: " + f->Label + " not applied — " + ex.what(),
                                                         sca::LogLevel::Warning, "UI");
                }
            }
            // Giá trị không hợp lệ (ô trống, số lẻ ở ô số nguyên, ngoài giới hạn): trả về giá trị đang dùng.
            // Hợp lệ: viết lại theo giá trị thật trong dữ liệu (vd. số nguyên đã làm tròn).
            double actual = f->Get();
            if (!ok || sender.Value() != actual)
            {
                *loading = true;
                sender.Value(actual);
                *loading = false;
            }
        });
    }

    void NumberToggleGroup::BindNumber(NumberBox const& box, std::string label, std::function<double()> get,
                                       std::function<void(double)> set)
    {
        AddNumber(box, std::move(label), std::move(get), [set = std::move(set)](double v) {
            if (!std::isfinite(v)) return false;
            set(v);
            return true;
        });
    }

    void NumberToggleGroup::BindInt(NumberBox const& box, std::string label, std::function<int()> get,
                                    std::function<void(int)> set, int min, int max)
    {
        AddNumber(box, std::move(label), [get = std::move(get)] { return (double)get(); },
            [set = std::move(set), min, max](double v) {
                // Ô int của bản WPF: chữ không phải số nguyên là lỗi binding, giá trị cũ giữ nguyên.
                if (!std::isfinite(v) || v != std::floor(v) || v < (double)min || v > (double)max) return false;
                set((int)v);
                return true;
            });
    }

    void NumberToggleGroup::BindToggle(ToggleSwitch const& toggle, std::function<bool()> get, std::function<void(bool)> set)
    {
        auto field = std::make_shared<ToggleField>();
        field->Toggle = toggle;
        field->Get = std::move(get);
        m_toggles.push_back(field);

        std::weak_ptr<ToggleField> weak = field;
        toggle.Toggled([weak, loading = m_loading, set = std::move(set)](WF::IInspectable const& sender, RoutedEventArgs const&) {
            if (*loading) return;
            auto f = weak.lock();
            if (!f) return;
            auto sw = sender.as<ToggleSwitch>();
            set(sw.IsOn());
            // Getter có thể không trả đúng giá trị vừa đặt (RequireHomeAfterEmg luôn true khi không có encoder
            // tuyệt đối) — hiện giá trị thật.
            bool actual = f->Get();
            if (sw.IsOn() != actual)
            {
                *loading = true;
                sw.IsOn(actual);
                *loading = false;
            }
        });
    }

    void NumberToggleGroup::LoadAll()
    {
        *m_loading = true;
        for (auto& f : m_numbers)
        {
            double value = f->Get();
            if (f->Box.Value() != value) f->Box.Value(value);
        }
        for (auto& t : m_toggles)
        {
            bool value = t->Get();
            if (t->Toggle.IsOn() != value) t->Toggle.IsOn(value);
        }
        *m_loading = false;
    }
}
