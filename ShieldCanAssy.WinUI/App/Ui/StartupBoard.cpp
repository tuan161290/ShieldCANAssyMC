#include "pch.h"
#include "Ui/StartupBoard.h"

#include <format>

#include "Lang/Loc.h"
#include "Ui/UiUtil.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using sca::EStartupState;
using sca::StartupProgress;

namespace
{
    using Clock = std::chrono::steady_clock;

    /// <summary>Đủ để mắt thấy 100% trước khi giao diện chính hiện ra.</summary>
    constexpr auto ReadyHold = std::chrono::milliseconds(500);

    /// <summary>Mở nguội trên máy lắp ráp đo được ~18 s (2026-11-02): quá 60 s là có gì đó kẹt, cho phép vào trước.</summary>
    constexpr double SlowAfterSeconds = 60;
}

namespace ui
{
    void StartupBoard::Build(Parts parts)
    {
        m_parts = std::move(parts);
        m_started = Clock::now();
        m_parts.LogHost.Child(m_log.Build());
        SetText(m_parts.Title, T("Startup.Title"));
    }

    double StartupBoard::ElapsedSeconds() const
    {
        return std::chrono::duration<double>(Clock::now() - m_started).count();
    }

    void StartupBoard::BuildRows(std::vector<sca::StartupStep> const& steps)
    {
        m_parts.Steps.Children().Clear();
        m_rows.clear();
        std::string group;
        for (const sca::StartupStep& step : steps)
        {
            if (step.Group != group)
            {
                group = step.Group;
                TextBlock header;
                header.Text(H(group));
                header.FontSize(11);
                header.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
                header.Foreground(Res(L"TextSecondaryBrush"));
                header.Margin(ThicknessHelper::FromLengths(0, m_rows.empty() ? 0 : 10, 0, 2));
                m_parts.Steps.Children().Append(header);
            }

            Row row;
            Grid grid;
            grid.MinHeight(28);
            grid.ColumnSpacing(4);
            auto addColumn = [&](GridLength width) {
                ColumnDefinition column;
                column.Width(width);
                grid.ColumnDefinitions().Append(column);
            };
            addColumn(GridLengthHelper::FromPixels(24));
            addColumn(GridLengthHelper::FromPixels(104));
            addColumn(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            addColumn(GridLengthHelper::Auto());

            row.Pending = winrt::Microsoft::UI::Xaml::Shapes::Ellipse();
            row.Pending.Width(8);
            row.Pending.Height(8);
            row.Pending.Fill(Res(L"LampOffBrush"));
            row.Pending.HorizontalAlignment(HorizontalAlignment::Center);
            row.Pending.VerticalAlignment(VerticalAlignment::Center);
            grid.Children().Append(row.Pending);

            row.Ring = ProgressRing();
            row.Ring.Width(16);
            row.Ring.Height(16);
            row.Ring.MinWidth(0);
            row.Ring.MinHeight(0);
            row.Ring.IsActive(false);
            row.Ring.Visibility(Visibility::Collapsed);
            row.Ring.HorizontalAlignment(HorizontalAlignment::Center);
            row.Ring.VerticalAlignment(VerticalAlignment::Center);
            grid.Children().Append(row.Ring);

            row.Icon = FontIcon();
            row.Icon.FontFamily(winrt::Microsoft::UI::Xaml::Media::FontFamily(L"Segoe Fluent Icons, Segoe MDL2 Assets"));
            row.Icon.FontSize(14);
            row.Icon.Visibility(Visibility::Collapsed);
            row.Icon.HorizontalAlignment(HorizontalAlignment::Center);
            row.Icon.VerticalAlignment(VerticalAlignment::Center);
            grid.Children().Append(row.Icon);

            row.Name = TextBlock();
            row.Name.Text(H(step.Key));
            row.Name.FontSize(14);
            row.Name.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
            row.Name.VerticalAlignment(VerticalAlignment::Center);
            Grid::SetColumn(row.Name, 1);
            grid.Children().Append(row.Name);

            row.Detail = TextBlock();
            row.Detail.FontSize(12);
            row.Detail.TextWrapping(TextWrapping::Wrap);
            row.Detail.Foreground(Res(L"TextSecondaryBrush"));
            row.Detail.VerticalAlignment(VerticalAlignment::Center);
            Grid::SetColumn(row.Detail, 2);
            grid.Children().Append(row.Detail);

            row.Time = TextBlock();
            row.Time.FontSize(12);
            row.Time.Foreground(Res(L"TextTertiaryBrush"));
            row.Time.VerticalAlignment(VerticalAlignment::Center);
            Grid::SetColumn(row.Time, 3);
            grid.Children().Append(row.Time);

            m_parts.Steps.Children().Append(grid);
            m_rows.push_back(std::move(row));
        }
    }

    void StartupBoard::UpdateRow(Row& row, sca::StartupStep const& step)
    {
        SetText(row.Detail, step.Detail);
        SetText(row.Time, step.Seconds > 0 ? std::format("{:.1f} s", step.Seconds) : std::string());

        if (row.Shown == (int)step.State) return;
        row.Shown = (int)step.State;
        const bool running = step.State == EStartupState::Running;
        const bool failed = step.State == EStartupState::Failed;
        Show(row.Pending, step.State == EStartupState::Pending);
        Show(row.Ring, running);
        row.Ring.IsActive(running);
        Show(row.Icon, step.State == EStartupState::Ok || failed);
        if (step.State == EStartupState::Ok)
        {
            row.Icon.Glyph(L"\xE73E");   // CheckMark
            row.Icon.Foreground(Res(L"SuccessBrush"));
        }
        else if (failed)
        {
            row.Icon.Glyph(L"\xE711");   // Cancel
            row.Icon.Foreground(Res(L"CriticalBrush"));
            row.Name.Foreground(Res(L"CriticalBrush"));
            row.Detail.Foreground(Res(L"CriticalBrush"));
        }
    }

    void StartupBoard::ShowProgress(int percent)
    {
        if (m_parts.Bar.Value() != percent) m_parts.Bar.Value(percent);
        SetText(m_parts.Percent, std::format("{}%", percent));
        const int seconds = (int)ElapsedSeconds();
        SetText(m_parts.Elapsed, std::format("{}:{:02}", seconds / 60, seconds % 60));
    }

    void StartupBoard::ShowOutcome(hstring const& title, std::string const& line, wchar_t const* barBrush, bool canContinue)
    {
        SetText(m_parts.Title, title);
        SetText(m_parts.Current, line);
        auto brush = barBrush != nullptr ? Res(barBrush) : nullptr;
        if (brush && m_parts.Bar.Foreground() != brush) m_parts.Bar.Foreground(brush);
        Show(m_parts.Continue, canContinue);
    }

    bool StartupBoard::Update(bool coreReady, CamStartupState const& cam)
    {
        // Control VisionMaster nằm ở phía App nên dòng CAM views do đây báo, không phải Core.
        if (coreReady && !m_camBegun)
        {
            m_camBegun = true;
            StartupProgress::Begin(StartupProgress::CamViews);
        }
        if (m_camBegun && !m_camEnded && cam.Settled)
        {
            m_camEnded = true;
            StartupProgress::End(StartupProgress::CamViews, cam.Ok, cam.Detail);
        }

        const std::vector<sca::StartupStep> steps = StartupProgress::Snapshot();
        if (steps.size() != m_rows.size()) BuildRows(steps);
        std::string running;
        m_solutionFailed = false;
        for (size_t i = 0; i < steps.size(); ++i)
        {
            UpdateRow(m_rows[i], steps[i]);
            if (steps[i].State == EStartupState::Running) running += (running.empty() ? "" : ", ") + steps[i].Key;
            if (steps[i].Key == StartupProgress::Solution && steps[i].State == EStartupState::Failed) m_solutionFailed = true;
        }
        m_log.Update();

        const sca::StartupSummary summary = StartupProgress::Summarize(steps);
        m_failed = summary.Failed;
        ShowProgress(summary.Percent);

        if (m_continued) return true;

        if (summary.AllEnded && summary.Failed == 0)
        {
            if (!m_ready)
            {
                m_ready = true;
                m_readyAt = Clock::now();
            }
            ShowOutcome(T("Startup.Ready"), sca::Loc::T("Startup.ReadyNote"), L"SuccessBrush", false);
            return Clock::now() - m_readyAt >= ReadyHold;
        }

        if (summary.AllEnded)
        {
            ShowOutcome(T("Startup.Failed"), sca::Loc::F("Startup.FailedNote", summary.Failed), L"CriticalBrush", true);
            return false;
        }

        if (ElapsedSeconds() >= SlowAfterSeconds)
        {
            ShowOutcome(T("Startup.Title"), sca::Loc::F(coreReady ? "Startup.SlowNote" : "Startup.WaitingNote", running),
                        L"CautionOnBrush", coreReady);
            return false;
        }

        ShowOutcome(T("Startup.Title"), running.empty() ? std::string() : sca::Loc::F("Startup.Working", running), nullptr, false);
        return false;
    }
}
