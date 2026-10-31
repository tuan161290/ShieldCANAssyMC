#pragma once

namespace sca
{
    /// <summary>
    /// Lấy tray ra ở vị trí chờ = Clear Tray cho tray đó (Tuấn 2026-10-30). Một cảm biến "có tray" — vòng quét McControl
    /// gọi <see cref="Update"/> mỗi nhịp.
    ///
    /// <para>Cảm biến gắn cố định ở vị trí chờ (xem Routine_Auto::TrayMissingAt): chỉ khi trục hàng ĐỨNG YÊN ở đó thì tín
    /// hiệu mới có nghĩa. Trục rời vị trí chờ thì bỏ theo dõi; về tới nơi thì lấy trạng thái lúc đó làm mốc, không coi là
    /// sườn — tray chạy ra khỏi cảm biến không phải là bị lấy ra. Chỉ sườn xuống (có → không) đứng vững
    /// <see cref="DebounceMs"/> mới tính: tay chạm rung tray, hay cảm biến chập chờn lúc đặt tray, không xoá oan. Đặt tray
    /// vào không làm gì — Start nhận tray như cũ.</para>
    /// </summary>
    class TrayRemovalWatch
    {
    public:
        static constexpr long long DebounceMs = 500;

        /// <summary>true đúng một lần khi tray vừa được lấy ra. <paramref name="atStandby"/>: trục hàng đã về gốc, đứng yên
        /// ở vị trí chờ, I/O đang kết nối.</summary>
        bool Update(bool atStandby, bool present, long long nowMs)
        {
            if (!atStandby)
            {
                m_armed = false;
                return false;
            }
            if (!m_armed)
            {
                m_armed = true;
                m_present = present;
                m_changedAt = -1;
                return false;
            }
            if (present == m_present)
            {
                m_changedAt = -1;
                return false;
            }
            if (m_changedAt < 0) m_changedAt = nowMs;
            if (nowMs - m_changedAt < DebounceMs) return false;
            m_present = present;
            m_changedAt = -1;
            return !present;
        }

    private:
        bool m_armed{ false };
        bool m_present{ false };
        long long m_changedAt{ -1 };   // lúc cảm biến bắt đầu khác mốc; -1 = đang khớp mốc
    };
}
