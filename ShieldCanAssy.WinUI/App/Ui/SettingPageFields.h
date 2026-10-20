#pragma once
// Phần riêng của trang Setting (bổ sung cho Ui/SettingPageUi.h, file dùng chung không sửa):
//   * NumberToggleGroup — NumberBox / ToggleSwitch gắn với một giá trị trong dữ liệu SỐNG (SystemData), thay
//     binding hai chiều của bản WPF (UI-SPEC §5, §9.7: "BindNumber").
//   * Hàm dựng NumberBox theo kiểu chung của trang (định dạng số en-US, không nhóm nghìn).
#include <climits>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ui::settingpage
{
    /// <summary>
    /// Nhóm NumberBox + ToggleSwitch gắn với dữ liệu sống:
    /// <list type="bullet">
    /// <item>NumberBox: GHI khi NumberBox chốt giá trị (rời ô / Enter — sự kiện ValueChanged). Chữ không đọc được
    /// thành số: NumberBox tự trả về giá trị cũ (ValidationMode InvalidInputOverwritten) — giá trị cũ giữ nguyên như
    /// lỗi binding của bản WPF. Ô số nguyên gõ số lẻ / ngoài giới hạn: cũng trả về giá trị cũ.</item>
    /// <item>ToggleSwitch: ghi ngay khi người vận hành gạt (Toggled); đặt IsOn từ code (LoadAll) không ghi ngược lại.</item>
    /// </list>
    /// </summary>
    class NumberToggleGroup
    {
    public:
        NumberToggleGroup();

        /// <summary>Định dạng hiển thị: en-US, không nhóm nghìn, tối thiểu <paramref name="minFractionDigits"/> chữ số
        /// lẻ (không cắt bớt chữ số — giá trị đọc từ system.json hiện đủ như double.ToString() của bản WPF).</summary>
        static void SetupNumberBox(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& box, int minFractionDigits = 0);

        void BindNumber(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& box, std::string label,
                        std::function<double()> get, std::function<void(double)> set);
        void BindInt(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& box, std::string label,
                     std::function<int()> get, std::function<void(int)> set, int min = INT_MIN, int max = INT_MAX);
        void BindToggle(winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch const& toggle,
                        std::function<bool()> get, std::function<void(bool)> set);

        /// <summary>Nạp lại mọi control từ dữ liệu (không ghi ngược).</summary>
        void LoadAll();

    private:
        struct NumberField
        {
            winrt::Microsoft::UI::Xaml::Controls::NumberBox Box{ nullptr };
            std::string Label;
            std::function<double()> Get;
            /// <summary>False = giá trị không hợp lệ (không ghi gì).</summary>
            std::function<bool(double)> Set;
        };
        struct ToggleField
        {
            winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch Toggle{ nullptr };
            std::function<bool()> Get;
        };

        void AddNumber(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& box, std::string label,
                       std::function<double()> get, std::function<bool(double)> set);

        std::vector<std::shared_ptr<NumberField>> m_numbers;
        std::vector<std::shared_ptr<ToggleField>> m_toggles;
        /// <summary>Đang đặt Value/IsOn từ code — ValueChanged/Toggled lúc đó không phải người vận hành sửa.</summary>
        std::shared_ptr<bool> m_loading;
    };
}
