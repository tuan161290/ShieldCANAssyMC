#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "Communication/IIoDevice.h"
#include "Data/SystemData.h"
#include "Infor/IoMap.h"

namespace sca
{
    /// <summary>
    /// Card I/O Ezi-IO Ethernet thật: <b>hai module riêng</b>, 32Di và 32Do, mỗi cái một IP và
    /// một <c>iBdID</c> (xem <see cref="FastechBoardId"/>).
    ///
    /// <para><b>Ảnh DI chụp một lần mỗi vòng quét.</b> Toàn bộ code phía trên đọc từ ảnh đó chứ
    /// không gọi UDP từng bit. Ngoài chuyện tốc độ, điều này bảo đảm mọi kiểm tra trong cùng
    /// một vòng nhìn thấy <b>cùng một thời điểm</b> — đọc EMG và đọc cửa cách nhau vài ms có
    /// thể cho ra một tổ hợp trạng thái chưa từng tồn tại thật.</para>
    ///
    /// <para><b>DO ghi theo mặt nạ, chỉ ghi khi có thay đổi.</b> <c>FAS_SetOutput</c> nhận cặp
    /// mặt nạ set/clear nên ghi được cả 32 bit trong một gói. <b>Chưa đọc được ảnh DO module
    /// đang giữ thì không ghi gì</b> — xem <see cref="SyncOutputsFromModule"/>.</para>
    ///
    /// <para><b>Mất kết nối là điều kiện dừng khẩn</b> (PLAN.md §3.1): EMG và rèm sáng đều đọc
    /// qua card này, nên mất nó là mù hoàn toàn. Lớp này chỉ báo <see cref="IsConnected"/> =
    /// false; việc dừng máy do <c>SafetyMonitor</c> quyết định.</para>
    ///
    /// <para>Luồng: Connect/Disconnect lúc khởi động/tắt app, Refresh trên vòng quét — cả ba gọi xuống thư viện
    /// Fastech. GetDi/GetDo/SetDo/sườn/RawDi/RawDo chỉ đụng bộ đệm dưới khoá, gọi được từ mọi luồng; SetDo không ghi
    /// ngay mà để vòng quét kế (<see cref="Flush"/>) gửi cả ảnh DO một lần.</para>
    /// </summary>
    class IoConnect : public IIoDevice
    {
    public:
        explicit IoConnect(std::shared_ptr<SystemData> sys);
        IoConnect(const IoConnect&) = delete;
        IoConnect& operator=(const IoConnect&) = delete;

        bool IsConnected() const override { return m_connected.load(); }

        // ───────────────────────── kết nối ─────────────────────────

        bool Connect() override;
        void Disconnect() override;

        // ───────────────────────── vòng quét ─────────────────────────

        void Refresh() override;

        // ───────────────────────── đọc / ghi bit ─────────────────────────

        bool GetDi(int bit) override;
        bool GetDo(int bit) override;
        void SetDo(int bit, bool value) override;
        bool RisingEdge(int bit) override;
        bool FallingEdge(int bit) override;

        /// <summary>Ảnh DI thô 32 bit — hiện trên trang Manual để đối chiếu với sơ đồ điện.</summary>
        uint32_t RawDi() const;
        uint32_t RawDo() const;

    private:
        /// <summary>
        /// Lấy ảnh DO module đang giữ làm điểm xuất phát cho <c>m_doWanted</c>, để lần ghi đầu
        /// tiên chỉ đổi đúng những bit phần mềm chủ động đổi. Đọc hỏng (rơi gói UDP) thì
        /// <see cref="Refresh"/> đọc lại ở vòng sau; trong lúc đó không ghi DO nào và card bị
        /// coi là mất kết nối, giống như khi ghi hỏng.
        /// </summary>
        bool SyncOutputsFromModule(bool logFailure);

        bool Open(const std::string& ipText, int bdId, const char* what);
        bool ReadInputs();
        bool Flush();

        std::shared_ptr<SystemData> m_sys;
        mutable std::mutex m_lock;           // m_di, m_diPrev, m_do, m_doWanted, m_doWritten, m_doDirty, m_doSynced

        std::array<bool, IoMap::DiCount> m_di{};
        std::array<bool, IoMap::DiCount> m_diPrev{};
        std::array<bool, IoMap::DoCount> m_do{};

        uint32_t m_doWanted = 0;
        uint32_t m_doWritten = 0;
        bool m_doDirty = true;

        /// <summary>Đã đọc ảnh DO thật từ module chưa. Chưa đọc thì <see cref="Flush"/> không
        /// được ghi: <c>m_doWanted</c> lúc đó chỉ là số 0 mặc định, ghi xuống là tắt hết.</summary>
        bool m_doSynced = false;

        std::atomic<bool> m_diOk{ false };
        std::atomic<bool> m_doOk{ false };
        std::atomic<int> m_failStreak{ 0 };

        /// <summary>Số vòng quét lỗi liên tiếp trước khi coi là mất kết nối. Một gói UDP rơi là
        /// bình thường; ba vòng liên tiếp thì không.</summary>
        static constexpr int FailLimit = 3;

        std::atomic<bool> m_connected{ false };
    };
}
