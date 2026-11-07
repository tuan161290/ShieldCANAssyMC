#pragma once
// Hộp mật khẩu của phân quyền theo cấp (sca::UserAccess, Tuấn 2026-10-21) — lớp phủ ModalOverlay như Dialogs nên nổi trên
// cả control VisionMaster.
#include <functional>

#include "SysConfig/UserAccess.h"

namespace ui
{
    class LoginDialog
    {
    public:
        /// <summary>Hỏi mật khẩu — một ô cho mọi cấp, mật khẩu gõ vào quyết định cấp (UserAccess::Login). Đúng → đóng hộp,
        /// onDone(true). Sai → báo ngay trong hộp để gõ lại. Cancel / Esc → onDone(false). Đang mở một hộp thì lời gọi mới
        /// bị bỏ qua.</summary>
        static void ShowLogin(std::function<void(bool)> onDone = {});

        /// <summary>Đổi mật khẩu Engineer (phải đang là Engineer): mật khẩu mới + nhập lại. Lưu ngay vào users.json.</summary>
        static void ShowChangePassword(std::function<void(bool)> onDone = {});

        /// <summary>Đóng hộp đang mở (như Cancel) — lúc tắt app.</summary>
        static void Close();
    };
}
