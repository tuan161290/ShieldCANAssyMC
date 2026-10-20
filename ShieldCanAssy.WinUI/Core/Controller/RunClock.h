#pragma once
// Thay cho System.Diagnostics.Stopwatch ở chỗ C# dùng Start/Stop/Restart/Reset/Elapsed cộng dồn (đồng hồ lượt của
// Routine_Auto, đồng hồ chạy rà của Routine_DryCycle). base::Stopwatch chỉ đo từ một mốc, không dừng/chạy tiếp được.
// An toàn đa luồng: luồng chu trình bấm giờ, luồng UI đọc theo chu kỳ.
#include <chrono>
#include <mutex>

namespace sca
{
    class RunClock
    {
    public:
        RunClock() = default;
        RunClock(const RunClock&) = delete;
        RunClock& operator=(const RunClock&) = delete;

        /// <summary>Stopwatch.Start: chạy tiếp, cộng dồn vào thời gian đã đo. Đang chạy thì không làm gì.</summary>
        void Start()
        {
            std::lock_guard<std::mutex> l(m_mutex);
            if (m_running) return;
            m_running = true;
            m_since = Clock::now();
        }

        /// <summary>Stopwatch.Stop: dừng, giữ thời gian đã đo.</summary>
        void Stop()
        {
            std::lock_guard<std::mutex> l(m_mutex);
            if (!m_running) return;
            m_acc += Clock::now() - m_since;
            m_running = false;
        }

        /// <summary>Stopwatch.Reset: dừng và về 0.</summary>
        void Reset()
        {
            std::lock_guard<std::mutex> l(m_mutex);
            m_running = false;
            m_acc = Clock::duration::zero();
        }

        /// <summary>Stopwatch.Restart: về 0 rồi chạy.</summary>
        void Restart()
        {
            std::lock_guard<std::mutex> l(m_mutex);
            m_acc = Clock::duration::zero();
            m_running = true;
            m_since = Clock::now();
        }

        bool IsRunning() const
        {
            std::lock_guard<std::mutex> l(m_mutex);
            return m_running;
        }

        /// <summary>Stopwatch.Elapsed.</summary>
        std::chrono::steady_clock::duration Elapsed() const
        {
            std::lock_guard<std::mutex> l(m_mutex);
            return m_running ? m_acc + (Clock::now() - m_since) : m_acc;
        }

        /// <summary>Elapsed.TotalSeconds.</summary>
        double ElapsedSeconds() const { return std::chrono::duration<double>(Elapsed()).count(); }

        /// <summary>ElapsedMilliseconds.</summary>
        long long ElapsedMilliseconds() const
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(Elapsed()).count();
        }

    private:
        using Clock = std::chrono::steady_clock;
        mutable std::mutex m_mutex;
        bool m_running{ false };
        Clock::time_point m_since{};
        Clock::duration m_acc{ Clock::duration::zero() };
    };
}
