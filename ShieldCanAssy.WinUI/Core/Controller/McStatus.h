#pragma once
#include <atomic>
#include <mutex>
#include <optional>
#include <string>

#include "Infor/Enums.h"

namespace sca
{
    /// <summary>
    /// Trạng thái máy hiện thời — UI đọc theo chu kỳ (poll), không có thông báo đổi giá trị (bản C# bind thẳng qua
    /// Bindable). Chỉ McControl, SafetyMonitor và các routine được ghi; mọi nơi khác chỉ đọc. Mọi getter an toàn đa
    /// luồng: số là atomic, chuỗi trả BẢN SAO dưới khoá.
    ///
    /// <para>Chữ trạng thái/bước/tên model chưa nạp dựng lúc đọc (Loc::T) — bản C++ chỉ có tiếng Anh nên không cần
    /// báo đổi ngôn ngữ như bản C#.</para>
    /// </summary>
    class McStatus
    {
    public:
        McStatus() = default;
        McStatus(const McStatus&) = delete;
        McStatus& operator=(const McStatus&) = delete;

#define SCA_STATUS_PROP(T, Name, init) \
        T Name() const { return m_##Name.load(); } \
        void Set##Name(T v) { m_##Name.store(v); } \
    private: std::atomic<T> m_##Name{ init }; public:

        SCA_STATUS_PROP(EMcState, State, EMcState::Init)
        SCA_STATUS_PROP(EMcMode, Mode, EMcMode::Manual)

        /// <summary>
        /// Máy đang ở chế độ <b>chạy rà</b> — bấm START sẽ chạy <c>Routine_DryCycle</c> (không sản phẩm, chỉ chạy trục)
        /// thay cho chu trình thật. Đặt qua <c>McControl::SetDryRun</c>.
        ///
        /// <para><b>Cố ý KHÔNG lưu xuống đĩa.</b> Mở lại app là về false. Một cờ đổi hẳn việc máy làm mà sống sót qua lần
        /// tắt máy thì sớm muộn sẽ có ca sản xuất chạy rà cả buổi mà không ai biết.</para>
        /// </summary>
        SCA_STATUS_PROP(bool, DryRun, false)

        /// <summary>
        /// Chế độ <b>dừng khi Vision NG</b> (Tuấn 2026-10-18, để tìm vì sao vision NG): chu trình Auto dừng NGAY tại chỗ NG —
        /// trước mọi xử lý NG và trước khi trục nào chạy tiếp — để xem ảnh và kết quả trong VisionMaster. Đặt qua
        /// <c>McControl::SetStopOnVisionNg</c>. Như <see cref="DryRun"/>, cố ý KHÔNG lưu xuống đĩa: mở lại app là tắt.
        /// </summary>
        SCA_STATUS_PROP(bool, StopOnVisionNg, false)

        SCA_STATUS_PROP(EOriginState, OriginStep, EOriginState::O0_PreCheck)
        SCA_STATUS_PROP(bool, OrgOk, false)
        SCA_STATUS_PROP(EAutoState, AutoStep, EAutoState::S00_Ready)

        /// <summary>Lượng bù của sản phẩm vừa đặt — để nhìn ra vision đang trôi trước khi thành NG.</summary>
        SCA_STATUS_PROP(double, LastDeltaTheta, 0.0)
        SCA_STATUS_PROP(double, LastPlaceError, 0.0)

        // ── An toàn ──
        SCA_STATUS_PROP(bool, EmgOk, false)
        SCA_STATUS_PROP(bool, LightCurtainOk, true)
        SCA_STATUS_PROP(bool, AirOk, false)
        SCA_STATUS_PROP(bool, SafetyChainOk, false)

        // ── Kết nối ──
        SCA_STATUS_PROP(bool, MotionConnected, false)
        SCA_STATUS_PROP(bool, IoConnected, false)
        SCA_STATUS_PROP(bool, VisionConnected, false)
        SCA_STATUS_PROP(bool, Simulation, false)

        // ── Hiển thị ──
        SCA_STATUS_PROP(ETowerLamp, TowerLamp, ETowerLamp::Off)
        SCA_STATUS_PROP(bool, Buzzer, false)
        SCA_STATUS_PROP(bool, BuzzerMuted, false)
#undef SCA_STATUS_PROP

        /// <summary>Tên model; chưa nạp (nullopt — C# null) thì trả chữ "(not loaded)" (Status.ModelNotLoaded).</summary>
        std::string ModelName() const;
        void SetModelName(std::optional<std::string> v)
        {
            std::lock_guard<std::mutex> l(m_textMutex);
            m_modelName = std::move(v);
        }

        /// <summary>Dòng nhắc hiện dưới nút bấm — nói rõ vì sao lệnh vừa rồi không thực hiện được.</summary>
        std::string Message() const { std::lock_guard<std::mutex> l(m_textMutex); return m_message; }
        void SetMessage(std::string v) { std::lock_guard<std::mutex> l(m_textMutex); m_message = std::move(v); }

        /// <summary><c>Status.Message += suffix</c> của bản C# — đọc-sửa-ghi trong MỘT lần giữ khoá.</summary>
        void AppendMessage(const std::string& suffix) { std::lock_guard<std::mutex> l(m_textMutex); m_message += suffix; }

        std::string AutoStepText() const;
        std::string StateText() const;

    private:
        mutable std::mutex m_textMutex;
        std::optional<std::string> m_modelName;
        std::string m_message;
    };
}
