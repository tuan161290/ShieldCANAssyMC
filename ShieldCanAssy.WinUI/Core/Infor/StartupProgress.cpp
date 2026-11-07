#include "Infor/StartupProgress.h"

#include <chrono>
#include <mutex>

namespace sca
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        struct Row
        {
            StartupStep Step;
            Clock::time_point Started{};
        };

        std::mutex g_mutex;
        std::vector<Row> g_rows;

        Row* Find(std::string_view key)
        {
            for (Row& row : g_rows)
                if (row.Step.Key == key) return &row;
            return nullptr;
        }

        double SecondsSince(Clock::time_point start)
        {
            return std::chrono::duration<double>(Clock::now() - start).count();
        }

        void Finish(Row& row, bool ok)
        {
            if (row.Step.State == EStartupState::Running) row.Step.Seconds = SecondsSince(row.Started);
            row.Step.State = ok ? EStartupState::Ok : EStartupState::Failed;
        }
    }

    void StartupProgress::Reset(std::vector<StartupStep> steps)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_rows.clear();
        for (StartupStep& step : steps) g_rows.push_back({ std::move(step) });
    }

    void StartupProgress::Begin(std::string_view key)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        Row* row = Find(key);
        if (row == nullptr) return;
        row->Step.State = EStartupState::Running;
        row->Started = Clock::now();
    }

    void StartupProgress::End(std::string_view key, bool ok, std::string_view detail)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        Row* row = Find(key);
        if (row == nullptr) return;
        Finish(*row, ok);
        if (!detail.empty()) row->Step.Detail = std::string(detail);
    }

    void StartupProgress::FailUnfinished(std::string_view reason)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (Row& row : g_rows)
        {
            if (row.Step.Key == CamViews) continue;
            if (row.Step.State != EStartupState::Pending && row.Step.State != EStartupState::Running) continue;
            Finish(row, false);
            row.Step.Detail = std::string(reason);
        }
    }

    std::vector<StartupStep> StartupProgress::Snapshot()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        std::vector<StartupStep> steps;
        steps.reserve(g_rows.size());
        for (const Row& row : g_rows)
        {
            steps.push_back(row.Step);
            if (row.Step.State == EStartupState::Running) steps.back().Seconds = SecondsSince(row.Started);
        }
        return steps;
    }

    StartupSummary StartupProgress::Summarize(const std::vector<StartupStep>& steps)
    {
        StartupSummary summary;
        int total = 0, done = 0;
        bool allEnded = !steps.empty();
        for (const StartupStep& step : steps)
        {
            total += step.Weight;
            const bool ended = step.State == EStartupState::Ok || step.State == EStartupState::Failed;
            if (ended) done += step.Weight;
            else allEnded = false;
            if (step.State == EStartupState::Failed) ++summary.Failed;
        }
        summary.AllEnded = allEnded;
        summary.Percent = total > 0 ? done * 100 / total : 0;
        return summary;
    }
}
