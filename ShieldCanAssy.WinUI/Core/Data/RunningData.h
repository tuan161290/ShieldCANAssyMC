#pragma once
#include "Infor/Enums.h"
#include "Utility/DateTimeText.h"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace sca
{
    /// <summary>
    /// Tiến độ và trạng thái ô của một tray.
    ///
    /// <para><b>Đa luồng.</b> <see cref="Selected"/> đọc/ghi qua hàm có khoá riêng (bản C# thay cả danh sách mỗi lần
    /// đổi để luồng chu trình đọc không cần khoá — tham chiếu .NET đổi nguyên tử, std::vector thì không). Các trường còn
    /// lại là dữ liệu trơn như bản C#: <see cref="Resize"/> chỉ được gọi khi kích thước thật sự đổi
    /// (<c>McDataManager::SyncRunTrays</c>), không trong lúc luồng chu trình đang ghi ô.</para>
    /// </summary>
    struct TrayState
    {
        std::string Name;
        int Rows = 0;
        int Cols = 0;

        /// <summary>Trạng thái từng ô, đánh chỉ số [row * Cols + col].</summary>
        std::vector<ECellState> Cells;

        /// <summary>Con trỏ ô đang xử lý. −1 = đã hết tray.</summary>
        int CurrentIndex = 0;

        EScanOrder ScanOrder = EScanOrder::RowMajor;

        int CellCount() const { return Rows * Cols; }

        /// <summary>
        /// Số tray nối nhau theo hàng trong sổ này: sổ tray đầu vào là 2 khi Tray 2 đã teach (bàn đặt hai tray từ
        /// 2026-10-15), còn lại 1. Mỗi tray chiếm <see cref="RowsPerTray"/> hàng liền nhau — Tray 1 trước — nên máy ghé
        /// hết Tray 1 rồi mới sang Tray 2. <c>McDataManager::SyncRunTrays</c> đặt số này theo model.
        /// </summary>
        int Trays = 1;
        int RowsPerTray() const { return Trays > 1 ? Rows / Trays : Rows; }
        int CellsPerTray() const { return RowsPerTray() * Cols; }

        /// <summary>
        /// Ô operator chọn ở lưới trang Main lúc AUTO, theo chỉ số ô (thứ tự ghé) — có ô nào thì chu trình Auto chỉ làm
        /// trên các ô đó: tray đầu vào chỉ hút ô đã chọn, tray lắp ráp chỉ soi và đặt vào ô đã chọn; làm hết thì coi như
        /// xong tray (Tuấn 2026-10-16). Trống = cả tray. Chỉ cho tray đang làm: máy xong tray thì xoá
        /// (<c>Routine_Auto.CloseSelection</c>), mở lại App cũng mất. Nạp tray mới KHÔNG xoá — chọn trước lúc chờ thay
        /// tray là cho tray sắp đặt vào. Trả BẢN SAO.
        /// </summary>
        std::vector<int> Selected() const;
        void SetSelected(std::vector<int> selected);

        bool HasSelection() const;

        bool IsSelected(int index) const;

        /// <summary>Chu trình được làm ô này: chưa chọn ô nào thì mọi ô, có chọn thì chỉ ô đã chọn.</summary>
        bool InScope(int index) const;

        void ToggleSelected(int index);

        void ClearSelection();

        void Resize(int rows, int cols);

        ECellState Get(int index) const
        {
            return index >= 0 && index < (int)Cells.size() ? Cells[(size_t)index] : ECellState::Empty;
        }

        void Set(int index, ECellState state)
        {
            if (index >= 0 && index < (int)Cells.size()) Cells[(size_t)index] = state;
        }

        void RowCol(int index, int& row, int& col) const;

        /// <summary>
        /// Nghịch đảo của <see cref="RowCol"/>: từ vị trí HÌNH HỌC (hàng, cột) trên tray ra chỉ số ô theo thứ tự
        /// máy ghé. Cần cho mọi chỗ vẽ hình cái tray — hình phải theo vị trí thật của ô, còn thứ tự ghé chỉ là
        /// đường đi của máy giữa các ô đó.
        /// </summary>
        int IndexOf(int row, int col) const { return VisitIndex(row, col, Cols, ScanOrder); }

        /// <summary>
        /// Thứ tự ghé của ô ở (hàng, cột) — bản tĩnh để trang Teaching dùng được trên <c>TrayLayout</c> (chưa có
        /// <see cref="TrayState"/>). Chỉ có MỘT phép tính này trong cả App, đặt cạnh <see cref="RowCol"/> để hai
        /// chiều không thể lệch nhau.
        /// </summary>
        static int VisitIndex(int row, int col, int cols, EScanOrder order);

        void ResetAll(ECellState state = ECellState::Loaded);

        /// <summary>Tiến tới ô kế. Trả về false khi hết tray.</summary>
        bool Advance();

        static std::shared_ptr<TrayState> Create(const std::string& name, int rows, int cols);

        TrayState() = default;
        TrayState(const TrayState& other);
        TrayState& operator=(const TrayState& other);

    private:
        mutable std::mutex m_selMutex;   // m_selected
        std::vector<int> m_selected;
    };

    /// <summary>Trạng thái một tay hút — hạt nhân của kiểm tra toàn vẹn khi chạy lại (§3.7).</summary>
    struct NozzleState
    {
        int Index = 0;
        bool HasProduct = false;

        /// <summary>Ô nguồn của sản phẩm đang giữ, để trả về đúng chỗ khi Align fail.</summary>
        int SourceCellIndex = -1;

        double MeasuredAngle = 0;
        bool Aligned = false;
    };

    /// <summary>
    /// Tiến độ chạy máy — ghi xuống Runtime\running.json sau mỗi ô hoàn tất và ngay khi vào
    /// EMG/ALARM. Vẫn ghi để phục vụ log/điều tra sự cố, nhưng KHÔNG dùng để khôi phục chu
    /// trình qua một lần khởi động lại ứng dụng — <c>McDataManager::LoadRunningData</c>
    /// gọi <see cref="ResetCycleProgress"/> ngay sau khi nạp, buộc mỗi lần mở app phải Init lại
    /// toàn bộ máy (về gốc, tray trống) thay vì chạy tiếp từ ô dở dang của phiên trước (quyết
    /// định của Tuấn — ưu tiên trạng thái máy luôn tường minh với người vận hành hơn là tiện lợi
    /// resume, đổi lấy việc không còn nguy cơ tiếp tục nhầm một chu trình mà máy đã bị đụng vào
    /// tay trong lúc tắt). Trong CÙNG một phiên chạy (không tắt app), STOP rồi START vẫn tiếp
    /// tục đúng bước như cũ — cơ chế checkpoint của <c>Routine_Auto</c> không đổi.
    /// Bộ đếm sản xuất (CountOk/CountNg/CountTotal) và <see cref="RecentModelName"/> KHÔNG bị
    /// reset — đó là số liệu báo cáo/tiện ích, không phải trạng thái chu trình cần init lại.
    ///
    /// <para>InputTray/AssyTray/Nozzle1/Nozzle2 là shared_ptr (tham chiếu như bản C#, không bao giờ null):
    /// ResetCycleProgress thay đối tượng mới, nơi đang giữ bản cũ không bị đổi dưới chân.</para>
    /// </summary>
    struct RunningData
    {
        /// <summary>Rỗng đứng vai null — không ghi khoá ra JSON.</summary>
        std::string RecentModelName;
        TimePoint SavedAt{};

        EAutoState AutoState = EAutoState::S00_Ready;

        /// <summary>Dừng giữa hành trình chứ không phải tại checkpoint — buộc nâng Z trước khi tiếp.</summary>
        bool UnsafePause = false;

        /// <summary>
        /// Sự cố với hàng trên tay hút (Tuấn chốt 2026-09-13, an toàn): chu trình dừng một lần.
        /// Bật khi bước đặt dừng giữa chừng SAU KHI đã nhả hút, khi trả về ô nguồn không nhả được,
        /// hoặc khi phải trả về ô nguồn mà không rõ ô nguồn. Lần bấm chạy kế tiếp McControl gỡ cờ và
        /// ghi sổ theo cảm biến — tay còn hàng thì chạy tiếp, bỏ bước hút (Tuấn đổi 2026-09-26). KHÔNG xoá ở
        /// <see cref="ResetCycleProgress"/> — mở lại app không có nghĩa là đã lấy hàng ra.
        /// </summary>
        bool NozzleClearRequired = false;

        /// <summary>Lý do của <see cref="NozzleClearRequired"/>, hiện cho người vận hành. Rỗng = null.</summary>
        std::string NozzleClearReason;

        /// <summary>Sổ tray đầu vào — Tray 1, nối thêm Tray 2 khi đã teach (<see cref="TrayState::Trays"/>). Kích thước do
        /// <c>McDataManager::SyncRunTrays</c> đặt theo model; ô → tray nào, hàng/cột nào: <c>ModelData::InputCell</c>.</summary>
        std::shared_ptr<TrayState> InputTray = TrayState::Create("InputTray", 4, 5);
        std::shared_ptr<TrayState> AssyTray = TrayState::Create("AssyTray", 5, 6);

        /// <summary>
        /// Operator vừa đặt tray đầu vào mới: ô của tray có mặt (<paramref name="mask"/> — bit 0 Tray 1, bit 1 Tray 2) thành
        /// có hàng, ô của tray vắng thành trống để máy không ghé (Tuấn 2026-10-15: tray nào có thì chạy).
        /// </summary>
        void LoadInputTrays(int mask);

        /// <summary>Tray đầu vào thứ <paramref name="tray"/> (0-based) còn ô máy sẽ ghé (có hàng, trong số ô đã chọn nếu có
        /// chọn) hoặc ô nguồn của hàng đang trên tay — vắng tray này thì máy không làm tiếp được. Tray đã xong hết hoặc
        /// không nạp thì vắng cũng không sao.</summary>
        bool InputTrayNeeded(int tray) const;

        std::shared_ptr<NozzleState> Nozzle1 = MakeNozzle(1);
        std::shared_ptr<NozzleState> Nozzle2 = MakeNozzle(2);

        // ── Bộ đếm sản xuất ──
        int CountOk = 0;
        int CountNg = 0;
        int CountTotal = 0;
        int ConsecutiveNg = 0;
        double LastCycleTimeSec = 0;

        // ── Bộ đếm theo tray, hiện trong card tray trang Main (Tuấn 2026-10-07) — nạp lại tray KHÔNG xoá, chỉ Clear
        //    sản lượng (ResetCounters) mới xoá. Tray đầu vào: OK = sản phẩm đặt xong, NG = sản phẩm tính NG. Tray lắp
        //    ráp: OK = ô đặt xong, NG = ô bị bỏ qua (không thấy chi tiết đích, điểm đặt lệch lưới). ──
        int InputCountOk = 0;
        int InputCountNg = 0;
        int AssyCountOk = 0;
        int AssyCountNg = 0;

        // ── Cycle time một sản phẩm (Tuấn chốt 2026-09-28): thời gian một lượt S00→S40 chia cho số sản phẩm của lượt
        //    (thường 2) — không tính lúc dừng và lúc chờ tray, xem Routine_Auto. ──

        /// <summary>Số sản phẩm gần nhất lấy trung bình — bằng một tray lắp ráp 40 sản phẩm (Tuấn 2026-10-06, trước là 25).</summary>
        static constexpr int CycleAvgWindow = 40;

        /// <summary>Cycle time của sản phẩm vừa xong (s/sp).</summary>
        double LastProductTimeSec = 0;

        /// <summary>Cycle time từng sản phẩm, tối đa <see cref="CycleAvgWindow"/> cái gần nhất.</summary>
        std::vector<double> RecentProductTimes;

        /// <summary>Trung bình của <see cref="RecentProductTimes"/> (s/sp) — tính sẵn lúc ghi, giao diện chỉ đọc số.</summary>
        double AvgProductTimeSec = 0;

        /// <summary>Ghi một lượt chạy xong <paramref name="sec"/> giây ra <paramref name="products"/> sản phẩm.</summary>
        void RecordCycle(double sec, int products);

        // ── Thời gian thực tế một tray lắp ráp (Tuấn 2026-10-08): đồng hồ GIỜ THỰC từ lúc máy chạy với tray đó tới lúc tray
        //    hết ô trống — KHÁC cycle time ở trên, tính cả lúc chờ tray đầu vào và lúc STOP, để biết thật sự mất bao lâu
        //    mới xong một tray. Mở lại App thì sổ tray làm lại từ đầu (ResetCycleProgress) nên đồng hồ cũng bỏ. ──

        /// <summary>Lúc bắt đầu tray lắp ráp đang làm; nullopt = không đếm (chưa chạy với tray này, hoặc tray đã xong,
        /// chờ thay).</summary>
        std::optional<TimePoint> AssyTrayStartedAt;

        /// <summary>
        /// Tray lắp ráp đang làm đã xong, đồng hồ đã dừng, số chốt nằm ở <see cref="AssyTrayDoneSec"/> /
        /// <see cref="AssyTrayDoneProducts"/> — hàng Cycle Time / Tack Time trang Main giữ số đó. Bấm Start với tray mới
        /// (<see cref="RestartAssyTrayClock"/>) thì số chốt mới chuyển xuống hàng Last và đếm lại từ 0 (Tuấn 2026-10-17).
        /// Trước đó tray xong là chép ngay vào Last nên hai hàng hiện cùng một số.
        /// </summary>
        bool AssyTrayDone = false;

        /// <summary>Thời gian thực tế của tray lắp ráp vừa xong mà chưa chuyển xuống Last (s) — xem <see cref="AssyTrayDone"/>.</summary>
        double AssyTrayDoneSec = 0;

        /// <summary>Số ô đặt OK của tray lắp ráp vừa xong mà chưa chuyển xuống Last.</summary>
        int AssyTrayDoneProducts = 0;

        /// <summary>Thời gian thực tế trung bình một sản phẩm của tray vừa xong = thời gian tray ÷ số ô đặt OK.</summary>
        double AssyTrayDonePerProductSec() const { return AssyTrayDoneProducts > 0 ? AssyTrayDoneSec / AssyTrayDoneProducts : 0; }

        /// <summary>Thời gian thực tế của tray lắp ráp trước (s) — chuyển xuống đây lúc tray kế tiếp bắt đầu.</summary>
        double LastAssyTrayTimeSec = 0;

        /// <summary>Số ô đặt OK của tray lắp ráp trước.</summary>
        int LastAssyTrayProducts = 0;

        /// <summary>Thời gian thực tế trung bình một sản phẩm của tray trước = thời gian tray ÷ số ô đặt OK.</summary>
        double LastAssyTrayPerProductSec() const { return LastAssyTrayProducts > 0 ? LastAssyTrayTimeSec / LastAssyTrayProducts : 0; }

        /// <summary>Bắt đầu đếm nếu tray đang làm chưa có mốc — gọi khi máy bắt đầu chạy.</summary>
        void StartAssyTrayClock(TimePoint now);

        /// <summary>Tray lắp ráp mới bắt đầu: số chốt của tray vừa xong (nếu có) chuyển xuống Last, đếm lại từ
        /// <paramref name="now"/>.</summary>
        void RestartAssyTrayClock(TimePoint now);

        /// <summary>Tray lắp ráp vừa hết ô trống: dừng đồng hồ, chốt thời gian thực tế và số sản phẩm vào số của tray đang
        /// làm (<see cref="AssyTrayDone"/>) — chưa đụng Last. Chưa có mốc thì bỏ qua.</summary>
        void FinishAssyTray(TimePoint now, int products);

        double NgRatePercent() const { return CountTotal > 0 ? CountNg * 100.0 / CountTotal : 0; }

        /// <summary>Clear sản lượng. Không đụng mốc tray đang làm — tray đó vẫn đang chạy, chỉ xoá số của tray trước.</summary>
        void ResetCounters();

        /// <summary>
        /// Xoá sạch tiến độ chu trình dở dang — gọi một lần ngay sau khi nạp running.json lúc
        /// khởi động app, để mỗi lần mở app đều bắt đầu từ S00 với tray trống và tay hút rỗng,
        /// bất kể phiên trước dừng ở đâu. KHÔNG đụng tới bộ đếm/RecentModelName/SavedAt — những
        /// trường đó không phải "tiến độ chu trình".
        /// </summary>
        void ResetCycleProgress();

        /// <summary>JSON dạng Newtonsoft (property chỉ-đọc như CellCount, NgRatePercent cũng ghi ra; null bỏ khoá).</summary>
        std::string ToJsonText() const;
        /// <summary>nullptr khi nội dung là null/rỗng; ném khi JSON hỏng. Thiếu khoá giữ mặc định.</summary>
        static std::shared_ptr<RunningData> FromJsonText(const std::string& text);

    private:
        static std::shared_ptr<NozzleState> MakeNozzle(int index)
        {
            auto n = std::make_shared<NozzleState>();
            n->Index = index;
            return n;
        }
    };
}
