namespace ShieldCanAssy.Lib.Fastech
{
    /// <summary>
    /// Phân bổ <c>iBdID</c> cho mọi module Fastech trong process.
    ///
    /// <para><c>iBdID</c> là <b>định danh nội bộ của thư viện</b>, không phải địa chỉ IP và
    /// không đọc được từ thiết bị — nó là con số mình tự chọn khi gọi <c>FAS_Connect</c>, và
    /// mọi lệnh sau đó chỉ mang theo con số này. Trùng số giữa hai module thì lần
    /// <c>FAS_Connect</c> sau đè lên trước, và triệu chứng là <b>lệnh gửi cho trục A2 chạy ra
    /// card I/O</b> — một lỗi rất khó lần ra vì cả hai module đều báo "kết nối thành công".</para>
    ///
    /// <para>Vì vậy bảng này là nguồn duy nhất; không nơi nào được viết số trực tiếp.</para>
    /// </summary>
    public static class FastechBoardId
    {
        /// <summary>Trục xoay A1 — Ezi-SERVO Plus-E.</summary>
        public const int AxisA1 = 0;

        /// <summary>Trục xoay A2 — Ezi-SERVO Plus-E.</summary>
        public const int AxisA2 = 1;

        /// <summary>Card vào Ezi-IO Ethernet 32Di.</summary>
        public const int Io32Di = 10;

        /// <summary>Card ra Ezi-IO Ethernet 32Do.</summary>
        public const int Io32Do = 11;
    }
}
