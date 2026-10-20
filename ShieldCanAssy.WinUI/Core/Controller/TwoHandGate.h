#pragma once
#include "Base/Async.h"

namespace sca
{
    /// <summary>
    /// Cổng an toàn 2 tay kiểu ISO 13851 IIIC (chống gian lận / anti-tie-down) — PLAN.md §3.2
    /// cập nhật 2026-08-20 (yêu cầu 4 nút Start vật lý của Tuấn: Cặp B).
    ///
    /// <para><b>MÁY KHÔNG CÒN DÙNG từ 2026-09-26</b>: hai nút cụm B tách thành Start 1 (tray đầu vào) và
    /// Start 2 (tray lắp ráp), mỗi nút bấm riêng (Tuấn chốt) — xem <c>McControl.HandleButtons</c>. Giữ lại lớp
    /// này (và bài self-test của nó) để quay về được nếu cần.</para>
    ///
    /// <para><see cref="Update"/> chỉ trả về <c>true</c> ĐÚNG MỘT LẦN khi cả hai nút cùng
    /// chuyển từ nhả sang bấm trong cùng một cửa sổ thời gian ngắn (<c>m_windowMs</c>).
    /// Giữ sẵn 1 nút rồi bấm/thả nút kia liên tục KHÔNG BAO GIỜ kích hoạt được: ngay khi phát
    /// hiện lệch pha (một nút lên mà nút kia chưa lên kịp trong cửa sổ), gate tự khoá
    /// (<c>Disarmed</c>) và chỉ mở khoá lại khi CẢ HAI nút đã về 0 cùng lúc — không có đường nào
    /// dán băng dính giữ 1 nút rồi một tay bấm nút còn lại để vô hiệu hoá yêu cầu 2 tay.</para>
    ///
    /// <para>Thuần logic, không phụ thuộc IO thật — kiểm chứng được bằng bài tự kiểm giống
    /// <c>VmCycle::CheckPlace</c>. Không an toàn đa luồng: chỉ một vòng quét gọi.</para>
    /// </summary>
    class TwoHandGate
    {
    public:
        explicit TwoHandGate(double windowMs) : m_windowMs(windowMs) {}

        /// <summary>Gọi mỗi vòng quét với MỨC hiện tại của 2 nút (không phải sườn). Trả về true
        /// đúng một lần tại thời điểm phát hiện bấm đồng thời hợp lệ.</summary>
        bool Update(bool btn1, bool btn2)
        {
            switch (m_state)
            {
                case State::Armed:
                    if (btn1 && btn2)
                    {
                        // Cả hai lên cùng một vòng quét — hợp lệ ngay, không cần đợi cửa sổ.
                        m_state = State::Disarmed;
                        return true;
                    }
                    if (btn1 || btn2)
                    {
                        m_state = State::WaitingSecond;
                        m_sw.Restart();
                    }
                    return false;

                case State::WaitingSecond:
                    if (!btn1 && !btn2)
                    {
                        // Nhả nút vừa bấm trước khi nút kia kịp lên: không tính là gian lận, cho
                        // phép thử lại ngay — đây chỉ là một lần bấm hụt bình thường.
                        m_state = State::Armed;
                        return false;
                    }
                    if (btn1 && btn2)
                    {
                        bool ok = ElapsedMs() <= m_windowMs;
                        m_state = State::Disarmed;
                        return ok;
                    }
                    if (ElapsedMs() > m_windowMs)
                        m_state = State::Disarmed;   // quá cửa sổ mà nút kia chưa lên — khoá, chờ nhả hết
                    return false;

                default: // Disarmed
                    if (!btn1 && !btn2) m_state = State::Armed;
                    return false;
            }
        }

    private:
        enum class State { Armed, WaitingSecond, Disarmed };

        double ElapsedMs() const { return m_sw.ElapsedSeconds() * 1000.0; }

        double m_windowMs;
        State m_state{ State::Armed };
        base::Stopwatch m_sw;
    };
}
