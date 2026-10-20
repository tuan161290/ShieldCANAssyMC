#pragma once
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "Communication/IAxis.h"
#include "Data/SystemData.h"

namespace sca
{
    /// <summary>
    /// Quản 7 trục: tạo, kết nối, quét trạng thái, và cung cấp các điều kiện tổng hợp
    /// (đã về gốc chưa, Z có an toàn không) cho tầng trên.
    ///
    /// <para>Luồng: <see cref="Init"/> chỉ gọi lúc khởi động, trước khi luồng nào khác duyệt hai danh sách trục.
    /// <see cref="Refresh"/> do vòng quét McControl gọi (mỗi 20 ms); mọi hàm còn lại gọi được từ mọi luồng.</para>
    /// </summary>
    class MotionConnect
    {
    public:
        MotionConnect() = default;
        MotionConnect(const MotionConnect&) = delete;
        MotionConnect& operator=(const MotionConnect&) = delete;

        /// <summary>Chỉ thay đổi trong <see cref="Init"/> (lúc khởi động) — không gọi Init khi luồng khác
        /// còn đang duyệt hai danh sách này.</summary>
        const std::map<std::string, std::shared_ptr<IAxis>, std::less<>>& Axes() const { return m_axes; }

        /// <summary>Danh sách theo thứ tự cố định của <c>AxisId::All</c>.</summary>
        const std::vector<std::shared_ptr<IAxis>>& AxisList() const { return m_axisList; }

        bool Simulation() const { return m_simulation.load(); }

        /// <summary>Trục theo tên (C# <c>this[string name]</c>); nullptr nếu không có.</summary>
        std::shared_ptr<IAxis> Axis(std::string_view name) const;
        std::shared_ptr<IAxis> operator[](std::string_view name) const { return Axis(name); }

        void Init(SystemData& sys);

        bool ConnectAll();
        void DisconnectAll();

        /// <summary>Một vòng quét: cập nhật trạng thái mọi trục và soi các điều kiện phát alarm.</summary>
        void Refresh();

        /// <summary>
        /// Về gốc một trục qua đây (không gọi thẳng <c>axis->Home</c>) để vòng quét biết trục đó đang về gốc.
        /// </summary>
        bool Home(const std::shared_ptr<IAxis>& axis, std::stop_token ct);

        /// <summary>Trục đang về gốc qua <see cref="Home"/> (cho kiểm thử/hiển thị).</summary>
        bool IsHoming(std::string_view name) const;

        bool AllConnected() const;
        bool AllServoOn() const;
        bool AllHomed() const;
        bool AnyAlarm() const;
        bool AnyBusy() const;

        /// <summary>
        /// Chốt chặn chống va (PLAN.md §5.1): X, Y1, Y2 chỉ được chạy khi CẢ HAI trục Z ở trên
        /// cao độ an toàn. Gốc Z = 0 ở phía TRÊN hành trình, chiều dương của Z là chiều ĐI XUỐNG
        /// (Tuấn xác nhận 2026-09-04, PLAN.md §4.3) — ngược HomeDir/chiều về gốc (đi lên) của
        /// chính nó, vì "về gốc" là hành trình TÌM lại điểm 0, không phải chiều dương lúc chạy
        /// bình thường. Nên "an toàn" (đã rút lên) là vị trí NHỎ — ≤ ngưỡng, không phải ≥.
        ///
        /// <para>Ngưỡng là <see cref="ZSafeLimit"/> = Z_SAFE + <see cref="ZSafeMargin"/>, không phải Z_SAFE: mỗi lần nâng
        /// lên, Z dừng ĐÚNG tại Z_SAFE (chạy đi lại, soi CAM bottom-up), trục dừng lệch vài micromet quanh đó không được
        /// làm chốt chặn báo sai.</para>
        /// </summary>
        bool ZSafeOk() const;

        /// <summary>
        /// Dung sai của chốt chặn trên Z_SAFE (mm). Tuấn chốt 2026-09-22: Z dừng đúng Z_SAFE, chốt chặn nới 1 mm —
        /// trước đó Z dừng ở Z_SAFE − 1 mm và chốt chặn ở đúng Z_SAFE.
        /// </summary>
        static constexpr double ZSafeMargin = 1.0;

        /// <summary>Ngưỡng thật của chốt chặn: X/Y chỉ chạy khi cả Z1, Z2 ≤ giá trị này.</summary>
        double ZSafeLimit() const { return m_zSafe.load() + ZSafeMargin; }

        double ZSafe() const { return m_zSafe.load(); }
        void SetZSafe(double v) { m_zSafe = v; }

        void StopAll();

    private:
        /// <summary>
        /// Chọn lớp trục theo <c>SimulationMode</c> và <c>AxisConfig::Kind</c>.
        ///
        /// <para>Tạo được lớp thật <b>không</b> có nghĩa là kết nối được — <c>Connect()</c> vẫn
        /// có thể thất bại vì chưa cắm cáp, sai COM, sai IP, hoặc thiếu DLL. Đó là đường xử lý
        /// bình thường: trục báo mất kết nối, alarm +00 nổi lên, máy không cho chạy. Không có
        /// đường nào lặng lẽ rơi về trục mô phỏng khi <c>SimulationMode = false</c>, vì trên
        /// màn hình hai thứ đó trông giống hệt nhau và người vận hành sẽ tưởng máy đang chạy.</para>
        /// </summary>
        static std::shared_ptr<IAxis> CreateAxis(const std::shared_ptr<AxisConfig>& cfg, bool simulation);

        /// <summary>
        /// Y1/Y2 đứng chờ ở −10 mm, phía ngoài cảm biến gốc, nên về gốc (chiều âm) luôn chạm Limit− rồi driver mới quay
        /// lại tìm gốc — không phải lỗi (Tuấn 2026-09-29, log 09:03 báo 2404 rồi 0.6 s sau Y2 vẫn về gốc xong). Về gốc
        /// hỏng thật vẫn báo HomeTimeout; Limit+ và Limit− lúc không về gốc vẫn báo như cũ.
        /// </summary>
        bool IgnoreNegLimitWhileHoming(std::string_view name) const;

        std::map<std::string, std::shared_ptr<IAxis>, std::less<>> m_axes;
        std::vector<std::shared_ptr<IAxis>> m_axisList;
        std::atomic<double> m_zSafe{ 0.0 };
        std::atomic<bool> m_simulation{ false };

        /// <summary>Trục đang về gốc — xem <see cref="Home"/>.</summary>
        mutable std::mutex m_homingMutex;
        std::set<std::string, std::less<>> m_homing;
    };
}
