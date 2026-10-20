#pragma once
#include "Data/Model/ModelData.h"
#include "Data/RunningData.h"
#include "Data/SystemData.h"

#include <memory>
#include <string>
#include <vector>

namespace sca
{
    /// <summary>Nạp và lưu toàn bộ dữ liệu bền vững: system.json, model, running.json. Ba đối tượng
    /// hiện hành được chia sẻ qua shared_ptr (như tham chiếu C#) và không bao giờ null. File ghi dạng Newtonsoft
    /// (Formatting.Indented, NullValueHandling.Ignore, ngày ISO), UTF-8 có BOM, qua file tạm rồi thay thế.</summary>
    class McDataManager
    {
    public:
        McDataManager() = delete;

        static std::shared_ptr<SystemData> SysData();
        static std::shared_ptr<RunningData> RunData();
        static std::shared_ptr<ModelData> Model();
        static bool ModelLoaded();

        // ───────────────────────── system.json ─────────────────────────

        /// <summary>Nạp system.json: sắp lại trục theo AxisId::All (bổ sung trục thiếu, bỏ trục lạ), chạy
        /// SystemData::MigrateRotaryAxes và VisionOutputNames::MigrateAxisVarUnits (có thay đổi thì ghi lại file).
        /// Chưa có file → tạo bản mặc định. Lỗi → bản mặc định + alarm 1000.</summary>
        static void LoadSystemData();
        static bool SaveSystemData();

        // ───────────────────────── running.json ─────────────────────────

        static void LoadRunningData();
        /// <summary>Ghi tiến độ. Gọi sau mỗi ô hoàn tất và ngay khi vào EMG/ALARM — phục vụ log/điều tra
        /// sự cố và giữ bộ đếm OK/NG, KHÔNG dùng để resume chu trình: <see cref="LoadRunningData"/>
        /// luôn xoá tiến độ (AutoState/tray/tay hút) đọc được từ file này ngay sau khi nạp.</summary>
        static bool SaveRunningData();

        // ───────────────────────── model ─────────────────────────

        static std::vector<std::string> ListModels();
        static bool LoadModel(const std::string& name);

        /// <summary>
        /// Dùng một model dựng sẵn trong bộ nhớ, không đọc từ file — hiện chỉ dùng cho model mô
        /// phỏng. Đi qua đây thay vì gán thẳng <see cref="Model"/> để bước đồng bộ kích thước
        /// lưới tiến độ không bị bỏ sót: quên nó thì lưới trên trang Main và con trỏ ô sẽ lệch
        /// kích thước với model, và triệu chứng là máy chạy sang ô không tồn tại.
        /// </summary>
        static void UseModel(std::shared_ptr<ModelData> model, bool countsAsLoaded = false);

        /// <summary>
        /// Đồng bộ sổ tiến độ theo model đang dùng: kích thước lưới và thứ tự quét. Lưới ô của tray là thuộc tính của
        /// MODEL, còn TrayState (RunData) là bản sao riêng, không tự đồng bộ — nạp/dùng model và trang Teaching đổi kích
        /// thước, thứ tự quét hay vừa teach lưới tray đầu vào đều phải gọi hàm này. Sổ tray đầu vào nối các tray đang dùng
        /// theo hàng (<see cref="ModelData::InputTraysInUse"/> — Tray 2 chỉ khi đã teach, xem
        /// <see cref="ModelData::InputCell"/>); Tray 2 theo Tray 1 về kích thước (<see cref="ModelData::NormalizeInputTrays"/>).
        /// </summary>
        static void SyncRunTrays();

        /// <summary>name rỗng = dùng Model()->Name.</summary>
        static bool SaveModel(const std::string& name = {});

        /// <summary>
        /// Xoá một model khỏi đĩa. Không đụng tới <see cref="Model"/> đang nạp trong bộ nhớ dù
        /// tên trùng — gọi hàm này xong màn hình vẫn hiển thị dữ liệu cũ cho tới khi nạp model
        /// khác, tránh cảnh đang teach dở mà dữ liệu biến mất ngay dưới tay.
        /// </summary>
        static bool DeleteModel(const std::string& name);

        /// <summary>
        /// Sao chép model trên đĩa sang tên mới — đọc thẳng từ file nguồn, không qua
        /// <see cref="Model"/> đang nạp, để sao chép một model KHÔNG PHẢI model đang mở cũng
        /// đúng dữ liệu.
        /// </summary>
        static bool CopyModel(const std::string& sourceName, const std::string& newName);

        /// <summary>Nạp lại model dùng gần nhất; nếu chưa có model nào thì tạo bản mặc định.</summary>
        static void LoadRecentModel();
    };
}
