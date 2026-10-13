using System.ComponentModel;

namespace ShieldCanAssy.Core.Communication
{
    /// <summary>
    /// Trừu tượng hoá card I/O (Fastech Ezi-IO Ethernet 32Di/32Do).
    ///
    /// <para><b>Nhắc lại giới hạn ở PLAN.md §3.1:</b> đây KHÔNG phải thiết bị an toàn. Các bit
    /// EMG/cửa đọc qua đây chỉ dùng để hiển thị, ghi log và dừng chu trình — việc cắt nguồn
    /// động lực do relay an toàn cứng đảm nhiệm, độc lập hoàn toàn với phần mềm.</para>
    /// </summary>
    public interface IIoDevice : INotifyPropertyChanged
    {
        bool IsConnected { get; }

        bool Connect();
        void Disconnect();

        /// <summary>Đọc trạng thái một ngõ vào.</summary>
        bool GetDi(int bit);

        /// <summary>Trạng thái ngõ ra hiện đang đặt.</summary>
        bool GetDo(int bit);

        void SetDo(int bit, bool value);

        /// <summary>Đọc toàn bộ 32 DI về bộ đệm. Gọi từ vòng quét.</summary>
        void Refresh();

        /// <summary>Sườn lên của một DI trong vòng quét vừa rồi — dùng cho nút bấm (PLAN.md §3.2).</summary>
        bool RisingEdge(int bit);

        /// <summary>Sườn xuống — dùng để biết nút đã được nhả (điều kiện của START).</summary>
        bool FallingEdge(int bit);
    }
}
