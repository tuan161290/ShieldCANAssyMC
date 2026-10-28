#pragma once
#include <memory>
#include <stop_token>
#include <string>

#include "Base/Event.h"
#include "Communication/GlobalVarSync.h"
#include "Communication/IIoDevice.h"
#include "Communication/IVisionDevice.h"
#include "Communication/MotionConnect.h"
#include "Communication/VmOutCheck.h"
#include "Controller/McFunction.h"
#include "Controller/McStatus.h"
#include "Controller/Routine_Auto.h"
#include "Controller/Routine_Calib.h"
#include "Controller/Routine_DryCycle.h"
#include "Controller/Routine_Origin.h"
#include "Controller/SafetyMonitor.h"
#include "Infor/Enums.h"

namespace sca
{
    /// <summary>
    /// Vòng điều khiển chính và máy trạng thái — PLAN.md §3.3, §3.4.
    ///
    /// <para>Một thread nền duy nhất quét thiết bị, chạy SafetyMonitor, xử lý nút bấm và cập nhật đèn tháp với chu kỳ
    /// <c>ControlTickMs</c>. Mọi lệnh từ UI chỉ ĐẶT YÊU CẦU (<see cref="RequestGo"/>…) rồi trả về ngay — vòng quét mới là
    /// nơi quyết định làm gì, nên không có đường nào để UI phát lệnh chuyển động vòng qua interlock.</para>
    ///
    /// <para><b>Bố cục bản C++ (cho người viết McControl.cpp).</b> Lớp chỉ có hàm static; toàn bộ trạng thái riêng nằm
    /// trong <c>struct McControl::Impl</c> định nghĩa ở McControl.cpp (theo khuôn bản Slit: một singleton trạng thái +
    /// các hàm static <c>Impl::UpdateMode/HandleButtons/DoGo/DoStop/…</c> mang đúng tên hàm private của bản C#):</para>
    /// <list type="bullet">
    /// <item><b>Sở hữu.</b> Impl giữ <c>McStatus</c> và <c>MotionConnect</c> (đối tượng tĩnh, sống suốt app), và
    /// <c>shared_ptr</c> của Io, Vision, Safety, Func, Routine_Origin/Auto/DryCycle/Calib (tạo trong <see cref="Init"/>,
    /// đọc qua accessor dưới khoá). Routine giữ THAM CHIẾU tới Motion/Func/Status — Impl phải giữ Func sống lâu hơn mọi
    /// routine dùng nó.</item>
    /// <item><b>Luồng.</b> <c>std::jthread</c> vòng quét (ưu tiên AboveNormal) và <c>std::jthread</c> watchdog (1000 ms
    /// trễ đầu, mỗi 200 ms) — watchdog RIÊNG vì vòng quét treo thì không tự phát hiện được. Về gốc / Init / Auto / chạy rà
    /// chạy trên MỘT luồng routine (mỗi lúc một cái): <see cref="RoutineBusy"/> = luồng đó còn sống
    /// (thay <c>_activeRoutine.IsCompleted</c>); <c>CycleBusy</c> thêm cờ <c>activeIsCycle</c>.</item>
    /// <item><b>Huỷ.</b> Về gốc/Init nhận <c>Safety()->AbortToken()</c>. Auto/chạy rà nhận token RIÊNG của chu trình:
    /// <c>base::LinkedStopSource(Safety()->AbortToken())</c> giữ trong shared_ptr (<c>_cycleCts</c>), để <c>EndCycle</c>
    /// huỷ được cả khi Safety đã đổi token sau RESET. Chuỗi bấm tay trang Vision dùng <see cref="ManualToken"/>.</item>
    /// <item><b>Cờ yêu cầu.</b> <c>std::atomic&lt;int&gt;</c> đọc-xoá bằng exchange (C# Interlocked.Exchange);
    /// <c>_requestedMode</c> là atomic BỀN.</item>
    /// <item><b>Routine.</b> <c>Run(ct)</c> của các routine là hàm chặn không ném; vẫn bọc try/catch như bản Slit
    /// (OperationCanceled → false; exception → AlarmCode::RoutineCrashed dạng sự kiện). Sau khi Auto/chạy rà thoát:
    /// <c>Func()->SetRunBit(true)</c> rồi sửa State như bản C#.</item>
    /// <item><b>Tắt app.</b> <see cref="Shutdown"/>: RequestAbort trước, chờ routine có giới hạn (3 s), dừng+JOIN vòng
    /// quét và watchdog, StopAll, SaveRunningData, chờ việc nền (<c>base::WaitBackgroundIdle</c>), rồi mới đóng thiết bị.</item>
    /// </list>
    /// </summary>
    class McControl
    {
    public:
        McControl() = delete;

        static McStatus& Status();
        static MotionConnect& Motion();
        /// <summary>nullptr trước lần <see cref="Init"/> đầu tiên (cả bảy con trỏ dưới đây).</summary>
        static std::shared_ptr<IIoDevice> Io();
        /// <summary>FakeVision khi mô phỏng, VisionConnect khi máy thật (như bản C#).</summary>
        static std::shared_ptr<IVisionDevice> Vision();
        static std::shared_ptr<SafetyMonitor> Safety();
        static std::shared_ptr<McFunction> Func();
        static std::shared_ptr<Routine_Calib> Calib();

        /// <summary>Chu trình Auto — trang Vision Edit đọc số quét ô đã đặt, bài tự kiểm đọc lệnh của lần đặt gần nhất.</summary>
        static std::shared_ptr<Routine_Auto> AutoRoutine();

        /// <summary>Chu trình chạy rà — trang Main đọc số nhịp và thời gian đã chạy.</summary>
        static std::shared_ptr<Routine_DryCycle> DryRoutine();

        /// <summary>Giây đã chạy của lượt Auto hiện tại — bộ đếm "cycle time hiện tại" ở trang Main.</summary>
        static double CurrentCycleSec();

        /// <summary>Bấm AUTO mà chưa đủ điều kiện vào sẵn sàng — máy ở lại MANUAL, kèm lý do để UI bật hộp thoại (Tuấn
        /// 2026-09-30). Bắn từ vòng quét (thread nền).</summary>
        static base::Event<std::string>& AutoModeRefused();

        /// <summary>
        /// Chọn tay hút dùng trong Auto — 0 cả hai, 1 chỉ tay #1, 2 chỉ tay #2 (Tuấn 2026-09-29), nút ở trang Main. Lưu vào
        /// system.json. Không đổi được lúc đang chạy; lúc dừng (Paused) thì đổi được, vòng sau theo lựa chọn mới.
        /// </summary>
        static void SetOnlyNozzle(int nozzle);

        /// <summary>
        /// Bật/tắt chế độ <b>chạy rà</b> — nút gạt ở trang Main. Chỉ CHỌN chế độ, KHÔNG chạy máy: bật xong vẫn phải chọn AUTO
        /// rồi bấm nút Start cứng. Không đổi được trong lúc máy đang chạy.
        /// </summary>
        static void SetDryRun(bool on);

        /// <summary>Bật/tắt chế độ dừng khi Vision NG (McStatus::StopOnVisionNg). Đổi được bất cứ lúc nào — chu trình đọc cờ
        /// ngay tại chỗ NG. Không lưu xuống đĩa.</summary>
        static void SetStopOnVisionNg(bool on);

        /// <summary>Bỏ mọi số đo đã cất (quét ô, tư thế trên tay) ở ranh giới bước kế tiếp — gọi sau khi teach lại
        /// lưới/mốc/vị trí soi, đổi model, hiệu chuẩn lại, về gốc hoặc nạp lại solution (thiết kế R7, R24).</summary>
        static void InvalidateVisionCache();

        /// <summary>Bỏ kết quả quét trước tray lắp ráp (S05_ScanAssy) — gọi mỗi khi UI nạp lại tray lắp ráp bằng tay.</summary>
        static void InvalidateAssyScan();

        /// <summary>Nút "Nạp lại tray đầu vào" ở trang Main: mọi ô của Tray 1 (và Tray 2 nếu đã teach) thành có hàng. Không
        /// đọc cảm biến.</summary>
        static void ReloadInputTray();

        /// <summary>Nút "Nạp lại tray lắp ráp" ở trang Main: mọi ô thành trống, bỏ kết quả quét trước của tray cũ, và cho
        /// chu trình chạy lại từ S00 ở ranh giới bước kế tiếp (Tuấn chốt 2026-09-13).</summary>
        static void ResetAssyTray();

        /// <summary>Bấm một ô ở lưới tray trang Main lúc AUTO (Tuấn 2026-10-16): chọn hoặc bỏ chọn. Gọi được lúc máy đang chạy.
        /// Không ghi running.json — lựa chọn chỉ sống trong phiên.</summary>
        static void ToggleCellSelection(bool input, int index);

        /// <summary>Nút "Bỏ chọn" của card tray trang Main — chu trình lại làm cả tray.</summary>
        static void ClearCellSelection(bool input);

        static bool IsInitialized();

        /// <summary>Dựng thiết bị, kết nối, chạy vòng quét + watchdog. CHẶN trong lúc kết nối — gọi từ luồng khởi động,
        /// không gọi lúc vòng quét đang chạy.</summary>
        static void Init();

        /// <summary>Số lần nạp solution thành công. Trang Main chỉ gắn khung ảnh CAM sau lần nạp đầu, và gắn lại khi số này
        /// đổi (nạp lại ở Setting).</summary>
        static int VisionLoadCount();

        /// <summary>Nạp lại solution theo <c>SystemData::VisionSolutionPath</c> HIỆN TẠI — gọi từ trang Setting. Không chặn
        /// (nạp trên luồng nền).</summary>
        /// <returns>Lý do từ chối, RỖNG nếu đã yêu cầu nạp (C#: null).</returns>
        static std::string ReloadVisionSolution();

        /// <summary>Kiểm tra (<paramref name="apply"/> = false) hoặc tạo các biến toàn cục mà các Flow cần trong solution
        /// đang nạp (Tuấn 2026-09-13). Tạo biến chỉ cho phép khi không có chu trình nào đang chạy. CHẶN — gọi từ luồng nền
        /// (C# SyncVisionGlobalVariablesAsync).</summary>
        static GlobalVarSyncReport SyncVisionGlobalVariables(bool apply);

        /// <summary>Đối chiếu tab Output Params của ba Flow với hợp đồng (Tuấn 2026-09-19) — đọc khai báo, KHÔNG chạy Flow
        /// nào. CHẶN — gọi từ luồng nền (C# CheckVisionFlowOutputsAsync).</summary>
        static VmOutCheckReport CheckVisionFlowOutputs();

        /// <summary>Huỷ mọi chuyển động, dừng và JOIN vòng quét, watchdog, routine đang chạy, rồi đóng thiết bị. CHẶN.</summary>
        static void Shutdown();

        // Các hàm Request* gọi được từ bất kỳ thread nào (kể cả UI); vòng quét mới thực sự xử lý.

        /// <summary>Đặt chế độ Auto/Manual — máy không có công tắc chìa vật lý, gọi từ nút chọn mode trên trang Main.</summary>
        static void RequestSetMode(EMcMode mode);

        static void RequestStop();

        /// <summary>
        /// Token cho các chuỗi chạy tay ở trang Vision — hiệu chuẩn, hand-eye, chạy kiểm từng bước (Tuấn 2026-10-08). Nối vào
        /// <c>SafetyMonitor::AbortToken</c> nên EMG/alarm vẫn huỷ được; STOP (mọi nút STOP, cả nút cứng) huỷ riêng nó. Token
        /// riêng thay vì huỷ token Safety để không kéo theo chu trình Auto đang tạm dừng. Lấy mới mỗi lần bắt đầu một chuỗi.
        /// </summary>
        static std::stop_token ManualToken();

        /// <summary>Dừng ở checkpoint — chạy nốt bước đang làm rồi thoát chu trình. Dùng khi chuyển MANUAL lúc đang chạy,
        /// và trong bài tự kiểm.</summary>
        static void RequestPause();
        static void RequestReset();
        static void RequestOrigin();

        /// <summary>Popup Origin trang Main (Tuấn 2026-10-21): lý do Về gốc sẽ bị từ chối KHÔNG kể phanh Z1/Z2 — cùng điều
        /// kiện DoOrigin + Routine_Origin PreCheck. Rỗng = về gốc được, hoặc chỉ còn thiếu Servo On + nhả phanh.</summary>
        static std::string OriginBlockReason();

        /// <summary>Z1 và Z2 đều đã nhả phanh xong (McFunction::BrakeReady) — Về gốc không cần nghi thức tay nữa.</summary>
        static bool OriginBrakesReady();

        /// <summary>
        /// Nghi thức tay trước Về gốc, làm hộ khi operator đã xác nhận trên popup Origin trang Main (Tuấn 2026-10-21) thay vì
        /// sang trang Manual: Servo On mọi trục chưa bật (như "Servo On (all)"), "Mở phanh" Z1/Z2, chờ qua cửa sổ nhả phanh.
        /// KHÔNG tự về gốc — xong thì người gọi RequestOrigin. CHẶN (tối đa ~7 s) — gọi từ luồng nền.
        /// </summary>
        /// <returns>Lý do thất bại, RỖNG khi Z1/Z2 đã sẵn sàng.</returns>
        static std::string ServoOnAndReleaseBrakes(std::stop_token ct);

        /// <summary>Nút Init cạnh Về gốc trang Main — đưa máy về vị trí chờ, không về gốc lại.</summary>
        static void RequestInit();
        static void RequestMuteBuzzer();

        /// <summary>Vào trạng thái Armed, chờ bấm Start. Chỉ nút mềm "START" trên UI gọi — CỐ Ý chỉ làm được đúng việc này
        /// (2026-08-20): không có đường nào từ màn hình cảm ứng chạy thẳng máy.</summary>
        static void RequestArm();

        /// <summary>Một trong bốn nút Start cứng (<c>IoMap::StartButtons</c>), như nhau (Tuấn chốt 2026-10-16). KHÔNG có
        /// nút mềm nào gọi hàm này.</summary>
        static void RequestGo();

        /// <summary>Đường START cũ, CHỈ còn dùng cho bài tự kiểm (mô phỏng). Đường vận hành thật phải qua
        /// <see cref="RequestGo"/>.</summary>
        static void RequestStart();

        /// <summary>Lấy cảm biến chân không làm nguồn sự thật, ghi đè trạng thái phần mềm. Chỉ cho gọi khi máy đang đứng yên.</summary>
        static void SyncNozzleStateFromSensor();

        /// <summary>Còn routine (Về gốc / Init / Auto / chạy rà) đang chạy. Gọi được từ mọi luồng. (C#: private
        /// RoutineBusy — mở ra cho trang Setting/Vision như bản Slit C++.)</summary>
        static bool RoutineBusy();

    private:
        /// <summary>Toàn bộ trạng thái riêng và các hàm private của bản C# — định nghĩa ở McControl.cpp.</summary>
        struct Impl;
    };
}
