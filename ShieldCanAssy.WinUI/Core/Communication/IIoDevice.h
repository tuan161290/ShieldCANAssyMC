#pragma once

namespace sca
{
    /// <summary>
    /// Trừu tượng hoá card I/O (Fastech Ezi-IO Ethernet 32Di/32Do — <c>IoConnect</c>; <c>FakeIo</c> khi mô phỏng).
    ///
    /// <para><b>Nhắc lại giới hạn ở PLAN.md §3.1:</b> đây KHÔNG phải thiết bị an toàn. Các bit
    /// EMG/cửa đọc qua đây chỉ dùng để hiển thị, ghi log và dừng chu trình — việc cắt nguồn
    /// động lực do relay an toàn cứng đảm nhiệm, độc lập hoàn toàn với phần mềm.</para>
    ///
    /// <para>Bản C++: mọi hàm an toàn đa luồng. <see cref="Refresh"/> chỉ do vòng quét gọi; GetDi/GetDo/SetDo và
    /// sườn đọc/ghi được từ mọi luồng (chỉ đụng bộ đệm, không gọi xuống card).</para>
    /// </summary>
    class IIoDevice
    {
    public:
        virtual ~IIoDevice() = default;

        virtual bool IsConnected() const = 0;

        virtual bool Connect() = 0;
        virtual void Disconnect() = 0;

        /// <summary>Đọc trạng thái một ngõ vào.</summary>
        virtual bool GetDi(int bit) = 0;

        /// <summary>Trạng thái ngõ ra hiện đang đặt.</summary>
        virtual bool GetDo(int bit) = 0;

        virtual void SetDo(int bit, bool value) = 0;

        /// <summary>Đọc toàn bộ 32 DI về bộ đệm. Gọi từ vòng quét.</summary>
        virtual void Refresh() = 0;

        /// <summary>Sườn lên của một DI trong vòng quét vừa rồi — dùng cho nút bấm (PLAN.md §3.2).</summary>
        virtual bool RisingEdge(int bit) = 0;

        /// <summary>Sườn xuống — dùng để biết nút đã được nhả (điều kiện của START).</summary>
        virtual bool FallingEdge(int bit) = 0;
    };
}
