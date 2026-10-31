#include "pch.h"
#include "Ui/LoginDialog.h"

#include "Lang/Loc.h"
#include "SysConfig/UserAccess.h"
#include "Ui/Dialogs.h"
#include "Ui/UiUtil.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Input;
using winrt::Windows::System::VirtualKey;

namespace ui
{
    namespace
    {
        /// <summary>Hộp đang mở. Control giữ ở đây chứ không chụp vào lambda của chính chúng — chụp thì ô mật khẩu và
        /// handler giữ nhau, đóng hộp không giải phóng được.</summary>
        struct Current
        {
            std::vector<PasswordBox> Boxes;
            TextBlock Error{ nullptr };
            /// <summary>Nhận chữ các ô theo thứ tự; trả câu lỗi, rỗng = xong (đóng hộp).</summary>
            std::function<std::string(std::vector<std::string> const&)> Submit;
            std::function<void(bool)> Done;
        };

        Current g_current;
        ModalOverlay g_overlay;

        void Finish(bool ok)
        {
            g_overlay.Close();
            auto done = std::move(g_current.Done);
            g_current = Current{};
            if (done) done(ok);
        }

        void Submit()
        {
            if (!g_current.Submit) return;
            std::vector<std::string> values;
            for (auto const& box : g_current.Boxes) values.push_back(U8(box.Password()));
            std::string why = g_current.Submit(values);
            if (why.empty())
            {
                Finish(true);
                return;
            }
            // Sai: xoá các ô, con trỏ về ô đầu để gõ lại ngay.
            for (auto const& box : g_current.Boxes) box.Password(L"");
            if (g_current.Error) g_current.Error.Text(H(why));
            if (!g_current.Boxes.empty()) g_current.Boxes.front().Focus(FocusState::Programmatic);
        }

        Button MakeButton(wchar_t const* text, bool primary)
        {
            Button b;
            b.Content(box_value(text));
            b.Width(96);
            b.Height(40);
            b.MinHeight(40);
            if (primary) b.Style(ResStyle(L"PrimaryButton"));
            return b;
        }

        // Thẻ kiểu ContentDialog Windows 11 như Dialogs::BuildCard: nội dung nền trắng, dải nút nền lớp 0.
        void Open(std::string const& title, std::string const& hint, std::vector<std::wstring> const& placeholders,
                  wchar_t const* okText, std::function<std::string(std::vector<std::string> const&)> submit,
                  std::function<void(bool)> onDone)
        {
            if (g_overlay.IsOpen() || !ModalOverlay::Root())
            {
                if (onDone) onDone(false);
                return;
            }
            g_current = Current{};
            g_current.Submit = std::move(submit);
            g_current.Done = std::move(onDone);

            Border card;
            card.Width(400);
            card.Background(Res(L"SurfaceBrush"));
            card.BorderBrush(Res(L"CardStrokeBrush"));
            card.BorderThickness(ThicknessHelper::FromUniformLength(1));
            card.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));

            Grid layout;
            RowDefinition contentRow; contentRow.Height(GridLengthHelper::Auto());
            RowDefinition buttonRow; buttonRow.Height(GridLengthHelper::Auto());
            layout.RowDefinitions().Append(contentRow);
            layout.RowDefinitions().Append(buttonRow);

            StackPanel content;
            content.Margin(ThicknessHelper::FromLengths(24, 20, 24, 12));

            StackPanel header;
            header.Orientation(Orientation::Horizontal);
            header.Margin(ThicknessHelper::FromLengths(0, 0, 0, 8));
            FontIcon icon;
            icon.Glyph(L"\xE72E");   // Lock
            icon.FontSize(22);
            icon.Foreground(Res(L"AccentBrush"));
            icon.Margin(ThicknessHelper::FromLengths(0, 0, 10, 0));
            icon.VerticalAlignment(VerticalAlignment::Center);
            TextBlock titleText;
            titleText.Text(H(title));
            titleText.FontSize(20);
            titleText.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
            titleText.VerticalAlignment(VerticalAlignment::Center);
            header.Children().Append(icon);
            header.Children().Append(titleText);
            content.Children().Append(header);

            TextBlock hintText;
            hintText.Text(H(hint));
            hintText.FontSize(13);
            hintText.TextWrapping(TextWrapping::Wrap);
            hintText.Foreground(Res(L"TextSecondaryBrush"));
            hintText.Margin(ThicknessHelper::FromLengths(0, 0, 0, 12));
            content.Children().Append(hintText);

            for (size_t i = 0; i < placeholders.size(); ++i)
            {
                PasswordBox box;
                box.PlaceholderText(placeholders[i]);
                box.FontSize(15);
                box.MinHeight(40);
                box.Margin(ThicknessHelper::FromLengths(0, 0, 0, 8));
                // Enter: sang ô kế, ô cuối thì xác nhận. Esc: huỷ.
                box.KeyDown([i](auto&&, KeyRoutedEventArgs const& e) {
                    if (e.Key() == VirtualKey::Enter)
                    {
                        e.Handled(true);
                        if (i + 1 < g_current.Boxes.size()) g_current.Boxes[i + 1].Focus(FocusState::Programmatic);
                        else Submit();
                    }
                    else if (e.Key() == VirtualKey::Escape)
                    {
                        e.Handled(true);
                        Finish(false);
                    }
                });
                box.PasswordChanged([](auto&&, auto&&) {
                    if (g_current.Error) g_current.Error.Text(L"");
                });
                if (i == 0)
                    box.Loaded([](auto&&, auto&&) {
                        if (!g_current.Boxes.empty()) g_current.Boxes.front().Focus(FocusState::Programmatic);
                    });
                g_current.Boxes.push_back(box);
                content.Children().Append(box);
            }

            // Dòng lỗi cao cố định — câu "Wrong password" hiện ra không đẩy dải nút xuống (Tuấn 2026-10-20).
            g_current.Error = TextBlock();
            g_current.Error.Height(20);
            g_current.Error.FontSize(13);
            g_current.Error.Foreground(Res(L"CriticalBrush"));
            g_current.Error.TextTrimming(TextTrimming::CharacterEllipsis);
            content.Children().Append(g_current.Error);

            Border bar;
            bar.Background(Res(L"BackgroundBrush"));
            bar.BorderBrush(Res(L"DividerBrush"));
            bar.BorderThickness(ThicknessHelper::FromLengths(0, 1, 0, 0));
            bar.CornerRadius(CornerRadius{ 0, 0, 8, 8 });
            bar.Padding(ThicknessHelper::FromLengths(24, 16, 24, 16));
            StackPanel buttons;
            buttons.Orientation(Orientation::Horizontal);
            buttons.HorizontalAlignment(HorizontalAlignment::Right);
            buttons.Spacing(8);
            Button ok = MakeButton(okText, true);
            ok.Click([](auto&&, auto&&) { Submit(); });
            Button cancel = MakeButton(L"Cancel", false);
            cancel.Click([](auto&&, auto&&) { Finish(false); });
            buttons.Children().Append(ok);
            buttons.Children().Append(cancel);
            bar.Child(buttons);

            Grid::SetRow(content, 0);
            Grid::SetRow(bar, 1);
            layout.Children().Append(content);
            layout.Children().Append(bar);
            card.Child(layout);
            g_overlay.Open(card);
        }
    }

    void LoginDialog::ShowLogin(sca::EUserLevel need, std::function<void(bool)> onDone)
    {
        const bool master = need == sca::EUserLevel::Master;
        Open(sca::Loc::T(master ? "User.MasterTitle" : "User.LoginTitle"),
             master ? sca::Loc::T("User.MasterHint") : sca::Loc::F("User.LoginHint", sca::UserAccess::IdleLogoutMin()),
             { L"Password" }, L"Login",
             [need](std::vector<std::string> const& values) -> std::string {
                 if (!sca::UserAccess::Login(values.at(0))) return sca::Loc::T("User.WrongPassword");
                 return sca::UserAccess::IsAtLeast(need) ? std::string() : sca::Loc::T("User.MasterRequired");
             },
             std::move(onDone));
    }

    void LoginDialog::ShowChangePassword(std::function<void(bool)> onDone)
    {
        Open(sca::Loc::T("User.ChangeTitle"), sca::Loc::F("User.ChangeHint", sca::UserAccess::MinPasswordLength),
             { L"New password", L"Repeat new password" }, L"Save",
             [](std::vector<std::string> const& values) -> std::string {
                 if (values.at(0) != values.at(1)) return sca::Loc::T("User.PasswordMismatch");
                 return sca::UserAccess::ChangePassword(sca::EUserLevel::Engineer, values.at(0));
             },
             std::move(onDone));
    }

    void LoginDialog::Close()
    {
        if (g_overlay.IsOpen()) Finish(false);
    }
}
