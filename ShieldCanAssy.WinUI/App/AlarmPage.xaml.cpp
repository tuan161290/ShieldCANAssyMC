#include "pch.h"
#include "AlarmPage.xaml.h"
#if __has_include("AlarmPage.g.cpp")
#include "AlarmPage.g.cpp"
#endif

#include "Controller/McControl.h"
#include "Infor/AlarmManager.h"
#include "Lang/Loc.h"
#include "Ui/UiUtil.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
namespace WF = winrt::Windows::Foundation;

namespace winrt::ShieldCanAssy::implementation
{
    void AlarmPage::OnRootLoaded(WF::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_loaded) return;
        m_loaded = true;
        ui::SetText(ActiveHeaderText(), sca::Loc::T("Alarm.ActiveHeader"));
        ui::SetText(HistoryHeaderText(), sca::Loc::T("Alarm.HistoryHeader"));
        ActiveTableHost().Child(m_active.Build(ui::AlarmTableKind::Active));
        HistoryTableHost().Child(m_history.Build(ui::AlarmTableKind::History));
    }

    // AlarmManager/McStatus là tĩnh — đọc được cả trước khi McControl::Init xong.
    void AlarmPage::OnTick()
    {
        if (!m_loaded) return;

        uint64_t version = sca::AlarmManager::Version();
        if (version != m_shownVersion)
        {
            m_shownVersion = version;
            m_active.Update(sca::AlarmManager::ActiveAlarms());
            m_history.Update(sca::AlarmManager::History());
        }

        ui::SetClampedText(AlarmMessageText(), sca::McControl::Status().Message());
    }

    // Cùng đường với nút RESET trên thanh điều hướng và nút RESET vật lý: McControl quyết định xoá được alarm nào.
    void AlarmPage::OnResetClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        sca::McControl::RequestReset();
    }

    void AlarmPage::OnMuteClick(WF::IInspectable const&, RoutedEventArgs const&)
    {
        sca::McControl::RequestMuteBuzzer();
    }
}