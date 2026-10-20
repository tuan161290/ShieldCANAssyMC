#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <functional>
#include <mutex>
#include <random>
#include <set>

#include "Communication/IIoDevice.h"
#include "Infor/IoMap.h"

namespace sca
{
    /// <summary>
    /// Card I/O mô phỏng. Mặc định mọi tín hiệu an toàn ở trạng thái bình thường (NC = 1),
    /// nút bấm nhả. Trang Manual có bảng công tắc để lật từng bit và bấm nút giả lập.
    /// </summary>
    class FakeIo : public IIoDevice
    {
    public:
        /// <summary>
        /// Chỗ móc <see cref="AfterSetDo"/>: gán/đọc an toàn đa luồng (C# là một field delegate gán lại được bất cứ lúc
        /// nào — self-test gán rồi gán null trong lúc vòng quét đang chạy). Giữ cú pháp C#:
        /// <c>io->AfterSetDo = [](int bit, bool on) {...};</c> và <c>io->AfterSetDo = nullptr;</c>.
        /// </summary>
        class SetDoHook
        {
        public:
            using Fn = std::function<void(int, bool)>;
            SetDoHook() = default;
            SetDoHook(const SetDoHook&) = delete;
            SetDoHook& operator=(const SetDoHook&) = delete;
            SetDoHook& operator=(Fn fn) { std::lock_guard<std::mutex> l(m_mutex); m_fn = std::move(fn); return *this; }
            SetDoHook& operator=(std::nullptr_t) { std::lock_guard<std::mutex> l(m_mutex); m_fn = nullptr; return *this; }
            /// <summary>Bản sao để gọi NGOÀI khoá (C#: <c>var after = AfterSetDo; if (after != null) after(...)</c>).</summary>
            Fn Get() const { std::lock_guard<std::mutex> l(m_mutex); return m_fn; }
            explicit operator bool() const { std::lock_guard<std::mutex> l(m_mutex); return static_cast<bool>(m_fn); }

        private:
            mutable std::mutex m_mutex;
            Fn m_fn;
        };

        FakeIo();
        FakeIo(const FakeIo&) = delete;
        FakeIo& operator=(const FakeIo&) = delete;

        bool IsConnected() const override { return m_connected.load(); }

        bool Connect() override { m_connected = true; return true; }
        void Disconnect() override { m_connected = false; }

        /// <summary>Xác suất hút thành công — hạ xuống để kiểm thử nhánh retry / NG (§5.4).</summary>
        double PickSuccessRate() const { return m_pickSuccessRate.load(); }
        void SetPickSuccessRate(double v) { m_pickSuccessRate = v; }

        bool GetDi(int bit) override;
        bool GetDo(int bit) override;
        void SetDo(int bit, bool value) override;

        /// <summary>
        /// Gọi ngay sau khi một ngõ ra ĐỔI trạng thái. Để bài tự kiểm tra đọc được trạng thái máy đúng vào
        /// khoảnh khắc đó — vd. góc trục A ngay lúc van hút mở — thay vì canh giờ mà chụp hụt. Cùng vai trò
        /// với <c>FakeVision.AfterFlow</c>. Chạy trên luồng gọi SetDo, ngoài khoá của FakeIo.
        /// </summary>
        SetDoHook AfterSetDo;

        void Refresh() override;

        bool RisingEdge(int bit) override;
        bool FallingEdge(int bit) override;

        // ── API riêng cho bảng mô phỏng trên trang Manual ──

        /// <summary>Nhấn-nhả một nút: bit lên 1 trong đúng một vòng quét.</summary>
        void SimPress(int diBit);

        /// <summary>Lật một DI giữ mức (EMG, cửa, khí, có tray...).</summary>
        void SimToggle(int diBit);

        void SimSet(int diBit, bool value);

    private:
        /// <summary>Cảm biến chân không bám theo van hút — có xác suất hút hụt để kiểm thử retry.</summary>
        void SetVacuumSensor(int sensorBit, bool valveOn);

        std::array<bool, IoMap::DiCount> m_di{};
        std::array<bool, IoMap::DiCount> m_diPrev{};
        std::array<bool, IoMap::DoCount> m_do{};
        std::set<int> m_pendingPress;
        mutable std::mutex m_lock;           // m_di, m_diPrev, m_do, m_pendingPress, m_rng
        std::mt19937 m_rng{ 12345 };
        std::atomic<double> m_pickSuccessRate{ 1.0 };
        std::atomic<bool> m_connected{ false };
    };
}
