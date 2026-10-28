#pragma once
#include <functional>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

#include "Communication/MotionConnect.h"
#include "Controller/McFunction.h"
#include "Controller/McStatus.h"
#include "Data/SystemData.h"
#include "Infor/Enums.h"

namespace sca
{
    /// <summary>
    /// Chuỗi về gốc — PLAN.md §4.3, theo nguyên tắc "nâng lên trước, tách ra sau".
    ///
    /// <para>Hai nguy cơ va chạm duy nhất của máy là (a) đầu hút còn thấp mà X/Y chạy ngang,
    /// (b) đầu hút xoay khi còn nằm trong hốc tray. Bước O2 cộng chốt chặn O3 khử cả hai, và
    /// O3 là bước ĐỌC LẠI VỊ TRÍ THỰC chứ không phải tin vào việc lệnh O2 đã trả về thành công.</para>
    ///
    /// <para>HÀM CHẶN, chạy trên luồng routine của McControl; không ném (huỷ/lỗi đều trả false).</para>
    /// </summary>
    class Routine_Origin
    {
    public:
        Routine_Origin(MotionConnect& motion, McFunction& func, McStatus& status, std::function<std::shared_ptr<SystemData>()> sys);
        Routine_Origin(const Routine_Origin&) = delete;
        Routine_Origin& operator=(const Routine_Origin&) = delete;

        /// <summary>Cả chuỗi về gốc (C# RunAsync). Huỷ/lỗi đều trả false sau khi đã dừng trục, đặt OrgOk = false và
        /// OriginStep = Aborted.</summary>
        bool Run(std::stop_token ct);

        /// <summary>
        /// Init (Tuấn 2026-09-29): đưa máy về vị trí chờ mà KHÔNG về gốc lại — nâng Z lên cao độ an toàn trước, rồi mọi
        /// trục về StandbyPos như bước cuối của Về gốc (O6). Dùng sau khi tạm dừng, chuyển MANUAL và thao tác tay, trước
        /// khi chạy tiếp (C# InitAsync).
        /// </summary>
        bool Init(std::stop_token ct);

        /// <summary>Lý do PreCheck (O0) sẽ từ chối, rỗng = qua. <paramref name="checkBrakes"/> = false bỏ qua riêng điều
        /// kiện phanh Z1/Z2 — popup Origin ở trang Main dùng để biết có phải CHỈ thiếu nghi thức Servo On + nhả phanh.
        /// Gọi được từ mọi luồng (chỉ đọc trạng thái).</summary>
        std::string BlockReason(bool checkBrakes) const;

    private:
        // O0
        bool PreCheck();
        bool Fail(const std::string& reason);
        // O1
        bool ServoOnAll(std::stop_token ct);
        // O2 / O4 / O5
        bool HomeParallel(const std::vector<std::string>& names, std::stop_token ct);
        bool HomeSequential(const std::vector<std::string>& names, std::stop_token ct);
        // O3
        bool GateZSafe();
        // O6
        bool MoveStandby(std::stop_token ct);
        // tiện ích
        void Step(EOriginState step);
        bool Abort(const std::string& reason);

        MotionConnect& m_motion;
        McFunction& m_func;
        McStatus& m_status;
        std::function<std::shared_ptr<SystemData>()> m_sys;
    };
}
