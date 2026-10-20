#pragma once
#include "Infor/Enums.h"

#include <string>

namespace sca
{
    /// <summary>
    /// Nội dung tĩnh của một mã alarm, nạp từ Config\alarms.json. Tên JSON viết thường tường minh (level, vi, en,
    /// cause, causeEn, remedy, remedyEn) để lần ghi lại (khi hoà bản mẫu) không đổi kiểu chữ của cả file.
    ///
    /// <para><c>level</c> ghi ra dạng chuỗi ("Error") chứ không phải số: file này tồn tại để người sửa được bằng
    /// Notepad, mà "level": 2 thì không ai đoán ra là gì. Đọc vào vẫn nhận cả hai kiểu.</para>
    /// </summary>
    struct AlarmDefinition
    {
        EAlarmLevel Level{ EAlarmLevel::Error };
        std::string Vi;
        std::string En;
        std::string Cause;
        /// <summary>Nguyên nhân bằng tiếng Anh (2026-10-09).</summary>
        std::string CauseEn;
        std::string Remedy;
        /// <summary>Cách xử lý bằng tiếng Anh (2026-10-09).</summary>
        std::string RemedyEn;
    };

    /// <summary>
    /// Tra cứu nội dung alarm. Nạp từ <c>%Root%\Config\alarms.json</c>; nếu chưa có thì lấy bản
    /// mẫu cạnh exe và chép sang Config để người dùng sửa được lời văn mà không cần build lại.
    ///
    /// <para><b>Ai sở hữu cái gì.</b> Lời văn (<c>vi</c>, <c>en</c>, <c>cause</c>, <c>causeEn</c>, <c>remedy</c>,
    /// <c>remedyEn</c>) thuộc về người dùng — sửa thoải mái, phần mềm không bao giờ ghi đè; chỉ ô tiếng Anh còn TRỐNG
    /// mới được điền từ bản mẫu (file cũ có trước khi App có tiếng Anh). Còn <c>level</c> quyết
    /// định máy có dừng hay không, nên nó thuộc về phần mềm và luôn được lấy lại theo bản mẫu.
    /// Nếu mức nằm trong tay file cấu hình thì một lần sửa nhầm trong Notepad là đủ hạ một lỗi
    /// Fatal xuống Warning và máy chạy tiếp qua sự cố.</para>
    /// </summary>
    class AlarmCatalog
    {
    public:
        AlarmCatalog() = delete;

        static bool Loaded();
        static void Load();

        /// <summary>
        /// Tra định nghĩa của một mã. Mã trục (2000–2699) rơi về mẫu <c>axisFaults</c> nếu không
        /// có bản ghi riêng — cho phép khai riêng vài mã đặc biệt mà vẫn dùng mẫu cho phần còn lại
        /// ({AXIS} được thay bằng tên trục). Không tra được thì trả về định nghĩa "chưa định nghĩa" chứ không
        /// trả rỗng: một mã thiếu trong bảng vẫn phải hiện lên màn hình, không được nuốt mất.
        /// </summary>
        static AlarmDefinition Get(int code);
    };
}
