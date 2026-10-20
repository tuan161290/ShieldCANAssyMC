#include "Controller/McControl.h"

#include <algorithm>
#include <atomic>
#include <format>
#include <mutex>
#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "Base/Async.h"
#include "Base/Text.h"
#include "Communication/IoConnect.h"
#include "Communication/Sim/FakeIo.h"
#include "Communication/Sim/FakeVision.h"
#include "Communication/VisionConnect.h"
#include "Controller/VmCycle.h"
#include "Data/McDataManager.h"
#include "Infor/AlarmManager.h"
#include "Infor/IoMap.h"
#include "Lang/Loc.h"
#include "Utility/Log/LogWorker.h"

// Bản C++ của ShieldCanAssy.Core\Core.Controller\McControl.cs. Bố cục theo bản Slit C++: một singleton trạng thái
// (State, namespace ẩn danh) + các hàm static của McControl::Impl mang đúng tên hàm private của bản C#.

namespace sca
{
    namespace
    {
        void Log(const std::string& msg, LogLevel level, const char* source = "McControl")
        {
            LogWorker::Instance().RecordLog(msg, level, source);
        }

        /// <summary><c>text.Replace(Environment.NewLine, " | ")</c>.</summary>
        std::string ReplaceNewLines(const std::string& text) { return base::Replace(text, "\r\n", " | "); }

        struct State
        {
            McStatus status;
            MotionConnect motion;

            std::mutex devMutex;   // các con trỏ dưới đây: Init() ghi, luồng khác đọc qua accessor
            std::shared_ptr<IIoDevice> io;
            std::shared_ptr<IVisionDevice> vision;
            std::shared_ptr<SafetyMonitor> safety;
            std::shared_ptr<McFunction> func;
            std::shared_ptr<Routine_Origin> origin;
            std::shared_ptr<Routine_Auto> autoR;
            std::shared_ptr<Routine_DryCycle> dry;
            std::shared_ptr<Routine_Calib> calib;

            base::Event<std::string> autoModeRefused;
            std::atomic<bool> initialized{ false };
            std::atomic<bool> running{ false };
            std::atomic<long long> lastTickMs{ 0 }, loopStartMs{ 0 };
            base::Stopwatch clock;

            // Yêu cầu từ UI / nút cứng — cờ một lần, vòng quét đọc rồi xoá.
            std::atomic<int> reqStart{ 0 }, reqStop{ 0 }, reqPause{ 0 }, reqReset{ 0 }, reqOrigin{ 0 }, reqInit{ 0 },
                reqMute{ 0 }, reqArm{ 0 }, reqGo{ 0 };

            /// Chế độ Auto/Manual đặt bằng phần mềm — máy không có công tắc chìa vật lý, chọn qua nút trên trang Main
            /// (xem RequestSetMode). Khác các req* ở trên (cờ một lần rồi tự xoá): đây là trạng thái BỀN, vòng quét chỉ
            /// đọc ra so sánh với Status.Mode.
            std::atomic<int> requestedMode{ (int)EMcMode::Manual };
            std::atomic<int> visionLoadCount{ 0 };

            // Token của chuỗi chạy tay trang Vision — xem McControl::ManualToken.
            std::mutex manualLock;
            std::shared_ptr<base::LinkedStopSource> manualCts;
            std::stop_token manualLinkedTo;

            std::mutex routineLock;
            std::shared_ptr<std::atomic<bool>> routineDone;   // null = chưa từng chạy routine nào

            /// Token riêng của chu trình Auto/chạy rà đang chạy — nối vào token Safety lúc khởi động, EndCycle huỷ được
            /// cả khi Safety đã đổi token sau RESET.
            std::shared_ptr<base::LinkedStopSource> cycleCts;
            /// Routine đang chạy là chu trình Auto/chạy rà (không phải Về gốc/Init); đã xin kết thúc nó chưa.
            bool activeIsCycle{ false };
            std::atomic<bool> cycleEnding{ false };

            // Luồng khai báo SAU CÙNG: huỷ trước mọi thứ chúng dùng.
            std::jthread routine, loop, watchdog;
        };

        State& St() { static State s; return s; }
    }

    struct McControl::Impl
    {
        // ───────────────────────── routine nền ─────────────────────────

        /// Gọi trong routineLock, lúc routine cũ đã thoát (RoutineBusy() = false).
        template <class F>
        static void StartRoutine(State& s, F body)
        {
            auto done = std::make_shared<std::atomic<bool>>(false);
            if (s.routine.joinable()) s.routine.join();
            s.routineDone = done;
            s.routine = std::jthread([done, body = std::move(body)]
            {
                try { body(); }
                catch (const std::exception& ex) { AlarmManager::RaiseEvent(AlarmCode::RoutineCrashed, "McControl", ex.what()); }
                catch (...) { AlarmManager::RaiseEvent(AlarmCode::RoutineCrashed, "McControl", "unknown error"); }
                done->store(true);
            });
        }

        static bool CycleBusy(State& s)
        {
            std::lock_guard<std::mutex> lock(s.routineLock);
            return s.activeIsCycle && s.routineDone && !s.routineDone->load();
        }

        /// Gọi trong routineLock, lúc chu trình cũ đã thoát.
        static std::stop_token NewCycleToken(State& s)
        {
            s.cycleCts = std::make_shared<base::LinkedStopSource>(s.safety->AbortToken());
            s.activeIsCycle = true;
            s.cycleEnding = false;
            return s.cycleCts->Token();
        }

        /// Chờ routine đang chạy thoát (có giới hạn) rồi JOIN luồng của nó nếu đã thoát.
        static void JoinRoutine(State& s, int waitMs)
        {
            std::shared_ptr<std::atomic<bool>> done;
            { std::lock_guard<std::mutex> lock(s.routineLock); done = s.routineDone; }
            base::Stopwatch sw;
            while (done && !done->load() && sw.ElapsedMilliseconds() < waitMs) base::Delay(5);
            std::lock_guard<std::mutex> lock(s.routineLock);
            // Routine còn treo thật (driver không phản hồi) thì tách luồng ra thay vì treo việc đóng app vô thời hạn —
            // như C# Wait(3000) rồi bỏ đi.
            if (s.routine.joinable())
            {
                if (!done || done->load()) s.routine.join();
                else s.routine.detach();
            }
        }

        // ───────────────────────── vision ─────────────────────────

        /// <summary>
        /// Nạp solution vision ngay lúc khởi tạo, chạy nền để không giữ màn hình khởi động.
        ///
        /// <para>Quên bước này thì máy vẫn chạy và mọi lần chụp đều trả "không tìm thấy đối tượng" — nghĩa là
        /// <b>toàn bộ sản phẩm thành NG</b> và trên màn hình chỉ thấy alarm vision, không thấy nguyên nhân thật. Vì vậy
        /// <c>Routine_Auto</c> còn kiểm tra lại <c>IsSolutionLoaded</c> ở S00.</para>
        /// </summary>
        static void LoadVisionSolution(State& s, const SystemData& sys)
        {
            std::string path = sys.VisionSolutionPath;
            std::shared_ptr<IVisionDevice> vision;
            { std::lock_guard<std::mutex> l(s.devMutex); vision = s.vision; }
            if (!vision) return;
            State* sp = &s;
            base::RunDetached([vision, path, sp]
            {
                try
                {
                    bool ok = vision->LoadSolution(path);
                    if (ok)
                    {
                        sp->visionLoadCount.fetch_add(1);
                        Log("Vision solution loaded: " + path, LogLevel::Info, "Vision");

                        // Thiếu dòng này là bug: Raise() ở nhánh else bên dưới không có nơi nào gọi ClearCondition tương
                        // ứng, nên một lần nạp lỗi là alarm này ConditionActive = true MÃI MÃI — RESET không bao giờ gỡ
                        // được, dù nạp lại solution qua Setting đã thành công (Tuấn phát hiện, 2026-09-08: SVO ON bị chặn
                        // toàn máy chỉ vì alarm Vision cũ không tự hết).
                        AlarmManager::ClearCondition(AlarmCode::VisionLoadFailed);
                    }
                    else
                    {
                        AlarmManager::Raise(AlarmCode::VisionLoadFailed, "Vision", path);
                    }
                }
                catch (const std::exception& ex)
                {
                    AlarmManager::Raise(AlarmCode::VisionLoadFailed, "Vision", ex.what());
                }
            });
        }

        /// <summary>
        /// Ở chế độ mô phỏng, model mặc định chưa teach và chưa hiệu chuẩn nên chuỗi Auto sẽ dừng ngay ở S00. Nạp bộ số
        /// giả để chạy và kiểm chứng được LOGIC chu trình trước khi có máy (§10). Chỉ nạp khi model hiện tại chưa dùng
        /// được, để không đè lên dữ liệu teach thật của ai đó đang mở bản mô phỏng để xem lại thông số.
        /// </summary>
        static void InstallSimulationModel(State& s)
        {
            auto m = McDataManager::Model();
            bool usable = m != nullptr && m->InputTray->IsTaught() && m->AssyTray->IsTaught() && m->Calib.IsUsable();
            if (usable) return;

            McDataManager::UseModel(ModelData::CreateSimulation());
            auto run = McDataManager::RunData();
            run->LoadInputTrays(McDataManager::Model()->UsableInputTrays());
            run->AssyTray->ResetAll(ECellState::Empty);
            McControl::InvalidateAssyScan();
            s.status.SetModelName(McDataManager::Model()->Name);

            Log("SIMULATION: fake model 'SIM' loaded — tray grids, camera calibration and rotation centers are "
                "MADE-UP numbers, for cycle testing only", LogLevel::Warning);
        }

        // ───────────────────────── vòng quét ─────────────────────────

        static void StartLoop(State& s, int tickMs)
        {
            if (s.running) return;
            s.running = true;
            s.lastTickMs = s.clock.ElapsedMilliseconds();
            s.loopStartMs = s.lastTickMs.load();

            s.loop = std::jthread([&s, tickMs](std::stop_token st)
            {
                ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
                Loop(s, tickMs, st);
            });
            s.watchdog = std::jthread([&s](std::stop_token st)
            {
                if (!base::DelayNoThrow(1000, st)) return;
                while (!st.stop_requested())
                {
                    CheckWatchdog(s);
                    if (!base::DelayNoThrow(200, st)) return;
                }
            });
        }

        static void Loop(State& s, int tickMs, std::stop_token st)
        {
            base::Stopwatch tickSw;
            while (s.running && !st.stop_requested())
            {
                tickSw.Restart();
                try
                {
                    s.motion.Refresh();
                    s.io->Refresh();
                    s.safety->Evaluate();

                    UpdateMode(s);
                    HandleButtons(s);
                    UpdateState(s);
                    EMcState state = s.status.State();
                    // Tạm dừng mà vào LỖI/EMG thì bỏ chu trình — sau RESET không còn gì để Start chạy tiếp.
                    if (!s.func->RunBit() && (state == EMcState::Alarm || state == EMcState::Emg))
                        AbandonHeld(s, Loc::T("Mc.WhyErrorPaused"));
                    // Chu trình đang chạy hay chờ tray mà vào LỖI/EMG thì cho nó thoát ở điểm dừng kế tiếp: RESET xong máy
                    // về DỪNG, chu trình không được chạy tiếp hay nằm chờ ngầm phía sau (Tuấn 30/09: A1 báo lỗi lúc chờ đổi
                    // tray, sau đó AUTO báo "Đang có chu trình chạy dở" mãi). Không huỷ token: lệnh trục đang chạy làm nốt
                    // như trước, lệnh kế bị chặn vì còn lỗi.
                    state = s.status.State();
                    if ((state == EMcState::Alarm || state == EMcState::Emg) && CycleBusy(s) && !s.cycleEnding)
                        EndCycle(s, Loc::T("Mc.WhyErrorRunning"), false);
                    UpdateOutputs(s);
                    s.func->UpdateBrakeOutputs();
                }
                catch (const std::exception& ex)
                {
                    // Sự kiện, không Raise trơn: không nơi nào gỡ nguyên nhân, một lỗi là RESET không bao giờ xoá được.
                    AlarmManager::RaiseEvent(AlarmCode::RoutineCrashed, "McControl", ex.what());
                }
                catch (...)
                {
                    AlarmManager::RaiseEvent(AlarmCode::RoutineCrashed, "McControl", "unknown error");
                }
                s.lastTickMs = s.clock.ElapsedMilliseconds();

                int rest = tickMs - (int)tickSw.ElapsedMilliseconds();
                if (!base::DelayNoThrow(rest > 0 ? rest : 1, st)) return;
            }
        }

        /// <summary>
        /// Watchdog chạy trên luồng RIÊNG, không nằm trong vòng quét — vòng quét treo thì nó không thể tự phát hiện mình
        /// đã treo (PLAN.md §3.3).
        ///
        /// <para><b>Ân hạn khởi động (<c>WatchdogStartupGraceMs</c>).</b> Ngay sau khi tạo luồng vòng quét, hệ điều hành
        /// chưa chắc lập tức cấp CPU cho nó — nạp solution Vision chạy nền cùng lúc có thể chiếm CPU đủ lâu để tick đầu
        /// tiên trễ hơn 500 ms dù vòng quét không hề treo thật. Trong cửa sổ ân hạn chỉ BỎ QUA việc RAISE alarm, không tắt
        /// hẳn watchdog — một treo THẬT trong lúc khởi động vẫn bị bắt ngay sau khi hết ân hạn.</para>
        /// </summary>
        static void CheckWatchdog(State& s)
        {
            if (!s.running) return;

            auto sys = McDataManager::SysData();
            long long now = s.clock.ElapsedMilliseconds();
            long long silent = now - s.lastTickMs.load();
            int limit = sys->WatchdogTimeoutMs;

            if (silent > limit)
            {
                bool inStartupGrace = now - s.loopStartMs.load() < sys->WatchdogStartupGraceMs;
                if (!inStartupGrace)
                    AlarmManager::Raise(AlarmCode::ControlWatchdog, "System", Loc::F("Mc.WatchdogSilent", silent, limit));
            }
            else
            {
                AlarmManager::ClearCondition(AlarmCode::ControlWatchdog);
            }
        }

        // ───────────────────────── chế độ / nút bấm ─────────────────────────

        static void UpdateMode(State& s)
        {
            McStatus& status = s.status;
            auto next = (EMcMode)s.requestedMode.load();
            if (next == status.Mode()) return;

            // Chuyển về Manual trong lúc đang chạy thì chạy nốt tới checkpoint rồi dừng (§3.4). Đang tạm dừng thì bỏ chu
            // trình: ở Manual còn jog được, Start sau đó mà phát lại lệnh dở từ chỗ đã jog tới là va. Đang chờ tray cũng
            // vậy — không thì Start sau đó chạy tiếp chu trình Auto dưới chế độ MANUAL.
            if (status.Mode() == EMcMode::Auto && (status.State() == EMcState::Running || status.State() == EMcState::WaitOperator))
                RequestPause();
            if (next == EMcMode::Manual && status.State() == EMcState::Held)
                AbandonHeld(s, Loc::T("Mc.WhyManualPaused"));

            // Armed chỉ có nghĩa dưới Mode Auto — rời Auto thì huỷ luôn bước arm dở.
            if (next == EMcMode::Manual && status.State() == EMcState::Armed)
                status.SetState(EMcState::Idle);

            // Chọn AUTO là vào luôn trạng thái sẵn sàng, không phải bấm thêm nút (Tuấn 2026-09-30). Chưa đủ điều kiện
            // thì ở lại MANUAL và báo ngay. Chu trình còn sống (vừa gạt MANUAL lúc đang chạy/chờ tray) thì chỉ đổi lại
            // chế độ như trước, không arm.
            EMcState st = status.State();
            bool cycleAlive = st == EMcState::Running || st == EMcState::Pausing || st == EMcState::WaitOperator;
            if (next == EMcMode::Auto && !cycleAlive)
            {
                std::string why = ArmBlockReason(s);
                if (!why.empty())
                {
                    s.requestedMode = (int)EMcMode::Manual;
                    std::string msg = Loc::F("Mc.AutoRefused", why);
                    status.SetMessage(msg);
                    Log(msg, LogLevel::Warning);
                    s.autoModeRefused.Raise(msg);
                    return;
                }
                status.SetMode(next);
                Log(std::string("Mode: ") + ToString(next), LogLevel::Info);
                Arm(s, "AUTO selected");
                return;
            }

            status.SetMode(next);
            Log(std::string("Mode: ") + ToString(next), LogLevel::Info);
        }

        static void HandleButtons(State& s)
        {
            // Nút cứng bắt SƯỜN LÊN, không bắt mức — nút kẹt hoặc dính tiếp điểm mà bắt theo mức sẽ khiến máy tự khởi
            // động lại liên tục (PLAN.md §3.2).
            if (s.io->RisingEdge(IoMap::BtnStop)) RequestStop();
            // Không còn nút Tắt còi cứng (Tuấn 2026-10-15) — bit đó thành cảm biến Tray 1, tắt còi bằng nút trang Main.

            // Bốn nút Start như nhau (khách hàng, Tuấn chốt 2026-10-16). Bấm cùng lúc nhiều nút vẫn chỉ là một lần Start
            // (một cờ yêu cầu). RESET không còn nút vật lý — chỉ bấm được bằng nút mềm trang Main.
            for (int bit : IoMap::StartButtons)
                if (s.io->RisingEdge(bit)) RequestGo();

            if (s.reqReset.exchange(0) == 1) DoReset(s);
            if (s.reqMute.exchange(0) == 1) s.status.SetBuzzerMuted(true);
            if (s.reqStop.exchange(0) == 1) DoStop(s);
            if (s.reqPause.exchange(0) == 1) DoPause(s);
            if (s.reqOrigin.exchange(0) == 1) DoOrigin(s);
            if (s.reqInit.exchange(0) == 1) DoInit(s);
            if (s.reqArm.exchange(0) == 1) DoArm(s);
            if (s.reqGo.exchange(0) == 1) DoGo(s);

            // reqStart/DoStart: xem RequestStart — chỉ còn bài tự kiểm dùng.
            if (s.reqStart.exchange(0) == 1) DoStart(s);
        }

        static void CancelManualSequences(State& s)
        {
            std::shared_ptr<base::LinkedStopSource> cts;
            { std::lock_guard<std::mutex> lock(s.manualLock); cts = s.manualCts; }
            if (cts) cts->Cancel();
        }

        /// <summary>
        /// Lỗi nào cũng phải bấm RESET xác nhận rồi mới cho chạy tiếp (Tuấn chốt 2026-09-26) — chặn nút START mềm và nút
        /// Start cứng (cả lúc nạp tray giữa chừng). Chỉ xét alarm đã hết nguyên nhân. Chờ đổi tray không phải alarm
        /// (Tuấn 2026-09-30). Warning mà máy tự chạy tiếp tự rời danh sách nên không chặn (AlarmManager::Dismiss).
        /// </summary>
        static bool AlarmsAcknowledged(State& s)
        {
            auto a = AlarmManager::PendingReset();
            if (!a) return true;
            s.status.SetMessage(Loc::F("Mc.UnackedAlarm", a->Code, a->Message()));
            return false;
        }

        /// <summary>Nút START mềm — vào lại sẵn sàng khi đã ở AUTO (vd. sau STOP). Chọn AUTO thì tự vào, xem UpdateMode;
        /// nút Start cứng lúc máy đứng thì vào rồi chạy luôn, xem DoGo.</summary>
        static void DoArm(State& s)
        {
            if (s.status.State() == EMcState::Armed) return;
            std::string why = ArmRefusal(s);
            if (!why.empty()) { s.status.SetMessage(why); return; }
            Arm(s, "soft START button");
        }

        /// <summary>Lý do không vào Armed được từ nút bấm (START mềm, Start cứng lúc máy đứng); rỗng = vào được.</summary>
        static std::string ArmRefusal(State& s)
        {
            if (s.status.Mode() != EMcMode::Auto) return Loc::T("Mc.SelectAutoFirst");
            return ArmBlockReason(s);
        }

        /// <summary>
        /// Lý do chưa vào được trạng thái sẵn sàng; rỗng = vào được. Chọn AUTO, nút START mềm và nút Start cứng cùng xét
        /// ở đây nên các đường không lệch nhau. Chạy dở giữa hành trình mà chưa Init thì chặn ngay từ đây (Tuấn
        /// 2026-09-30: không đủ điều kiện thì báo luôn lúc chọn AUTO), không đợi tới lúc bấm Start mới báo.
        /// </summary>
        static std::string ArmBlockReason(State& s)
        {
            EMcState st = s.status.State();
            if (st == EMcState::Held) return HeldMessage();
            if (RoutineBusy())
                return st == EMcState::Idle || st == EMcState::Paused || st == EMcState::NotHomed
                    ? Loc::T("Mc.CycleBusyStop")
                    : Loc::T("Mc.CycleBusy");
            switch (st)
            {
                case EMcState::Idle:
                case EMcState::Paused:
                    break;
                case EMcState::NotHomed:
                case EMcState::Init:
                    return Loc::T("Mc.NotHomedPressOrigin");
                case EMcState::Alarm:
                case EMcState::Emg:
                    return Loc::T("Mc.HasErrorReset");
                default:
                    return Loc::F("Mc.MachineInState", s.status.StateText());
            }
            auto a = AlarmManager::PendingReset();
            if (a) return Loc::F("Mc.UnackedAlarm", a->Code, a->Message());
            std::string block;
            if (NozzleClearPending(s, block)) return block;
            if (McDataManager::RunData()->UnsafePause)
                return Loc::T("Mc.UnsafePauseInit");
            return {};
        }

        static void Arm(State& s, const std::string& how)
        {
            s.status.SetState(EMcState::Armed);
            s.status.SetMessage(s.status.DryRun() ? Loc::T("Mc.ArmedDry") : Loc::T("Mc.Armed"));
            Log("Armed (" + how + ")", LogLevel::Info);
        }

        /// <summary>
        /// Nút Start cứng — bốn nút như nhau (khách hàng, Tuấn chốt 2026-10-16). Máy tự biết đang thiếu tray nào: nạp mọi
        /// tray chưa vào máy mà cảm biến thấy ở vị trí chờ; tray không thấy thì chu trình làm được gì với tray đã có thì
        /// làm, bước cần tray kia đứng ở CHỜ THAO TÁC tới lần bấm Start sau — xem <c>Routine_Auto::LoadTrays</c>.
        /// <list type="bullet">
        /// <item>Tạm dừng: chạy tiếp đúng chỗ dở.</item>
        /// <item>AUTO mà máy đứng (IDLE/PAUSED, vd. sau STOP): vào Armed rồi chạy luôn — một lần bấm (Tuấn 2026-10-16).</item>
        /// <item>Armed: nạp tray rồi khởi động chu trình, không thấy tray nào cũng khởi động — chu trình chờ tray. Chạy rà
        /// không dùng tray.</item>
        /// <item>Chu trình đang chạy hoặc đang chờ tray: chỉ nạp tray.</item>
        /// </list>
        /// KHÔNG có nhánh NotHomed→DoOrigin() như DoStart cũ: về gốc là chuyển động thật, phải bấm nút "Về gốc" riêng
        /// (2026-08-20, chủ ý đơn giản hoá).
        /// </summary>
        static void DoGo(State& s)
        {
            const std::string button = "Start";
            McStatus& status = s.status;

            // Tạm dừng: Start bật lại bit RUN, chu trình chạy tiếp đúng chỗ dở (Tuấn 2026-09-29).
            if (status.State() == EMcState::Held)
            {
                if (!s.safety->SafeToMove()) { status.SetMessage(Loc::F("Mc.CannotResume", s.safety->BlockReason())); return; }
                s.autoR->HoldClock(false);
                status.SetState(EMcState::Running);
                status.SetMessage(status.DryRun() ? Loc::T("Mc.DryRunning") : Loc::T("Mc.AutoRunning"));
                s.func->SetRunBit(true);
                Log(button + " — RUN bit on, resuming", LogLevel::Info);
                return;
            }

            if (RoutineBusy())
            {
                if (status.DryRun() ||
                    (status.State() != EMcState::Running && status.State() != EMcState::WaitOperator))
                { status.SetMessage(Loc::T("Mc.CycleBusy")); return; }
                if (!AlarmsAcknowledged(s)) return;
                // Nạp tray là cho chu trình chạy trục tiếp — rèm còn bị cắt thì chưa nhận (Tuấn 2026-10-01).
                if (!status.LightCurtainOk())
                { status.SetMessage(Loc::F("Mc.CurtainPressAgain", button)); return; }
                std::string loaded, notSeen;
                std::string why = s.autoR->LoadTrays(loaded, notSeen);
                if (!why.empty()) { status.SetMessage(why); return; }
                if (loaded.empty())
                {
                    // Không nạp thêm được tray nào: tray còn thiếu chưa đặt, hoặc cả hai đã ở trong máy.
                    status.SetMessage(!notSeen.empty() ? notSeen : Loc::T("Mc.TraysAllIn"));
                    return;
                }
                std::string msg = Loc::F("Mc.TrayAccepted", loaded, button);
                status.SetMessage(msg);
                Log(msg, LogLevel::Info);
                return;
            }

            // AUTO mà máy đứng (vd. sau STOP): vào sẵn sàng rồi chạy luôn, không bắt bấm hai lần (Tuấn 2026-10-16).
            if (status.State() == EMcState::Idle || status.State() == EMcState::Paused)
            {
                std::string why = ArmRefusal(s);
                if (!why.empty()) { status.SetMessage(why); return; }
                Arm(s, button);
            }

            if (status.State() != EMcState::Armed)
            {
                status.SetMessage(Loc::F("Mc.CannotRunButtonInState", button, status.StateText()));
                return;
            }

            if (!AlarmsAcknowledged(s)) return;

            if (!s.safety->SafeToMove())
            {
                status.SetMessage(Loc::F("Mc.CannotRun", s.safety->BlockReason()));
                return;
            }

            std::string block;
            if (!ResumeIntegrityOk(s, block))
            {
                status.SetMessage(block);
                Log("GO blocked: " + block, LogLevel::Warning);
                return;
            }

            // Tray hết theo sổ (còn từ lần chạy trước) thì chu trình nhận là tray mới — không chạy tới S50 rồi mới đòi đổi
            // (Tuấn gặp 2026-09-26). Không thấy tray nào vẫn khởi động: chu trình tự vào CHỜ THAO TÁC, báo thiếu tray
            // nào. Chạy rà không dùng tray.
            if (!status.DryRun())
            {
                std::string loaded, notSeen;
                std::string why = s.autoR->LoadTrays(loaded, notSeen);
                if (!why.empty()) { status.SetMessage(why); return; }
            }

            StartCycle(s);
        }

        /// <summary>
        /// Khởi động chu trình — chạy chu trình THẬT hay chu trình RÀ do <c>McStatus::DryRun</c> quyết, tại ĐÚNG MỘT CHỖ.
        /// Cả hai đường vào đều đi qua đây: nút Start (DoGo) và đường START cũ (DoStart, chỉ bài tự kiểm dùng).
        ///
        /// <para><b>Vì sao phải gom về một chỗ.</b> 23/09 phép rẽ nhánh chạy rà chỉ được đặt trong <c>DoStart</c> — mà
        /// <c>DoStart</c> thì chỉ self-test gọi. Trên máy thật, bật chạy rà rồi bấm Start vẫn chạy chu trình thật và vẫn
        /// kích Vision (Tuấn phát hiện 2026-09-24). Một chỗ quyết thì hai đường không thể lệch nhau nữa.</para>
        /// </summary>
        static void StartCycle(State& s)
        {
            if (s.status.DryRun()) StartDryRun(s); else StartAuto(s);
        }

        /// <summary>
        /// Đường START cũ, CHỈ còn dùng cho bài tự kiểm (mô phỏng qua FakeIo, không phải vận hành thật). Đường vận hành
        /// thật (DI vật lý) phải qua RequestGo.
        /// </summary>
        static void DoStart(State& s)
        {
            McStatus& status = s.status;
            // Xét TRƯỚC khi hỏi routine có đang bận không: ở trạng thái chờ thao tác thì chu trình vẫn đang chạy — nó đang
            // nằm chờ chính cái xác nhận này. Đặt sau thì nút START ở màn hình đổi tray sẽ mãi mãi báo "đang có chu trình
            // chạy dở". Không chặn theo AlarmsAcknowledged ở đây: đường này chỉ self-test đi, và các kịch bản test nối
            // nhau qua nó sau những cảnh báo cố ý gây ra.
            if (status.State() == EMcState::WaitOperator)
            {
                // Nạp mọi tray đang thiếu một lượt — tiện cho kịch bản test, không qua kiểm cảm biến tray.
                s.autoR->LoadBothTrays();
                status.SetMessage(Loc::T("Mc.ConfirmedContinue"));
                Log("Operator confirmed — continuing", LogLevel::Info);
                return;
            }

            if (RoutineBusy())
            {
                status.SetMessage(Loc::T("Mc.CycleBusy"));
                return;
            }

            switch (status.State())
            {
                case EMcState::NotHomed:
                    DoOrigin(s);
                    return;

                case EMcState::Idle:
                case EMcState::Paused:
                case EMcState::Armed:        // chọn AUTO là vào luôn Armed (2026-09-30)
                {
                    if (status.Mode() != EMcMode::Auto)
                    {
                        status.SetMessage(Loc::T("Mc.KeyToAuto"));
                        return;
                    }
                    if (!s.safety->SafeToMove())
                    {
                        status.SetMessage(Loc::F("Mc.CannotRun", s.safety->BlockReason()));
                        return;
                    }

                    std::string block;
                    if (!ResumeIntegrityOk(s, block))
                    {
                        status.SetMessage(block);
                        Log("START blocked: " + block, LogLevel::Warning);
                        return;
                    }

                    s.autoR->LoadBothTrays();
                    StartCycle(s);
                    return;
                }

                default:
                    status.SetMessage(Loc::F("Mc.CannotStartInState", status.StateText()));
                    return;
            }
        }

        /// <summary>
        /// Kiểm tra toàn vẹn trước khi chạy tiếp — PLAN.md §3.7. So trạng thái tay hút mà PHẦN MỀM ghi nhận với cái CẢM
        /// BIẾN đang báo. Lệch nhau nghĩa là trong lúc máy dừng đã có người gỡ sản phẩm ra, hoặc sản phẩm rơi, hoặc cảm
        /// biến hỏng.
        ///
        /// <para><b>Cảm biến chân không là nguồn sự thật, cả hai chiều, không alarm</b>: tay CÒN hàng mà sổ ghi trống — nhận
        /// hàng và chạy tiếp, coi như đã qua bước hút (Tuấn đổi 2026-09-26, AdoptHeldProduct); sổ ghi có hàng mà cảm biến
        /// báo TRỐNG — hàng đã được lấy ra: ghi sổ tay trống, ô nguồn thành trống, chạy tiếp (Tuấn 2026-09-30,
        /// ReleaseEmptyNozzle). Trước đó chiều này phát alarm 5030 rồi bắt RESET và START lại.</para>
        /// </summary>
        static bool ResumeIntegrityOk(State& s, std::string& reason)
        {
            reason.clear();
            if (NozzleClearPending(s, reason)) return false;

            auto run = McDataManager::RunData();
            if (s.io == nullptr || !s.io->IsConnected())
            {
                reason = Loc::T("Mc.NoVacuumSensor");
                return false;
            }

            for (int i = 1; i <= 2; i++)
            {
                auto& nz = i == 1 ? *run->Nozzle1 : *run->Nozzle2;
                bool sensor = s.func->HasProduct(i);
                if (nz.HasProduct == sensor) continue;

                if (sensor) AdoptHeldProduct(i, "record empty but sensor sees product");
                else ReleaseEmptyNozzle(i, "record holds product but sensor sees none — product was removed");
            }

            // Dừng giữa hành trình (bỏ chu trình đang tạm dừng, có thể đã thao tác tay): phải Init — hoặc Về gốc — đưa máy
            // về vị trí chờ trước (Tuấn 2026-09-29). Trước đó chỉ đòi Z ở cao độ an toàn.
            if (run->UnsafePause)
            {
                reason = Loc::T("Mc.UnsafePauseInitResume");
                return false;
            }

            return true;
        }

        /// <summary>
        /// Yêu cầu lấy sản phẩm ra khỏi tay hút (<c>RunningData::NozzleClearRequired</c>) — đặt lúc sự cố và làm chu
        /// trình dừng một lần (Tuấn chốt 2026-09-13). Tới lần bấm chạy kế tiếp thì gỡ, ghi sổ theo cảm biến: tay còn hàng
        /// — nhận hàng và chạy tiếp (Tuấn đổi 2026-09-26); tay đã trống — ô nguồn đang Processing thành Empty (trả về
        /// Loaded là S10 sẽ hút vào hốc rỗng). Chu trình chạy lại từ S00. Chỉ còn chặn (trả true kèm lý do) khi chưa đọc
        /// được cảm biến.
        /// </summary>
        static bool NozzleClearPending(State& s, std::string& reason)
        {
            reason.clear();
            auto run = McDataManager::RunData();
            if (!run->NozzleClearRequired) return false;

            if (s.io == nullptr || !s.io->IsConnected())
            {
                reason = Loc::F("Mc.NoVacuumSensorWhy", run->NozzleClearReason);
                return true;
            }

            std::string why = run->NozzleClearReason;
            for (int i = 1; i <= 2; i++)
            {
                if (s.func->HasProduct(i)) AdoptHeldProduct(i, "after incident: " + why);
                else ReleaseEmptyNozzle(i, nullptr);
            }

            Log("Product-removal request cleared (" + why + "), cycle restarts from S00", LogLevel::Info);
            run->NozzleClearRequired = false;
            run->NozzleClearReason.clear();
            run->AutoState = EAutoState::S00_Ready;
            McDataManager::SaveRunningData();
            return false;
        }

        /// <summary>
        /// Tay hút còn hàng lúc bấm chạy — nhận hàng và chạy tiếp, <b>coi như đã qua bước hút</b> (Tuấn yêu cầu
        /// 2026-09-26). Ghi sổ tay có hàng, chưa soi; chu trình chạy lại từ S00 nên tray lắp ráp được quét và hàng được soi
        /// lại ở CAM bottom-up rồi mới đặt. Ô nguồn giữ nếu ô đó vẫn đang Processing, không thì −1.
        /// </summary>
        static void AdoptHeldProduct(int nozzle, const std::string& why)
        {
            auto run = McDataManager::RunData();
            auto& nz = nozzle == 1 ? *run->Nozzle1 : *run->Nozzle2;
            int src = nz.SourceCellIndex;
            if (src < 0 || src >= run->InputTray->CellCount() || run->InputTray->Get(src) != ECellState::Processing)
                nz.SourceCellIndex = -1;
            nz.HasProduct = true;
            nz.Aligned = false;
            run->AutoState = EAutoState::S00_Ready;
            McDataManager::SaveRunningData();
            Log(std::format("Nozzle #{} holds product ({}) — continue, skip pick, re-inspect at bottom CAM then place{}",
                            nozzle, why, nz.SourceCellIndex < 0 ? "; source cell unknown" : ""),
                LogLevel::Info);
        }

        /// <summary>
        /// Tay hút trống theo cảm biến: ghi sổ tay trống, ô nguồn đang Processing thành Empty — hàng đã bị lấy đi nên hốc
        /// tray trống. Sổ đang ghi có hàng thì chu trình chạy lại từ S00 và ghi một dòng log (<paramref name="why"/>);
        /// nullptr = không ghi log (bước gỡ yêu cầu tự ghi).
        /// </summary>
        static void ReleaseEmptyNozzle(int nozzle, const char* why)
        {
            auto run = McDataManager::RunData();
            auto& nz = nozzle == 1 ? *run->Nozzle1 : *run->Nozzle2;
            bool hadProduct = nz.HasProduct;
            int src = nz.SourceCellIndex;
            if (src >= 0 && src < run->InputTray->CellCount() && run->InputTray->Get(src) == ECellState::Processing)
                run->InputTray->Set(src, ECellState::Empty);
            nz.HasProduct = false;
            nz.SourceCellIndex = -1;
            nz.Aligned = false;
            if (!hadProduct || why == nullptr) return;

            run->AutoState = EAutoState::S00_Ready;
            McDataManager::SaveRunningData();
            Log(std::format("Nozzle #{}: {} — recorded empty per sensor, continue from S00{}", nozzle, why,
                            src >= 0 ? std::format(", source cell {} set empty", src) : std::string()),
                LogLevel::Warning);
        }

        static void StartAuto(State& s)
        {
            s.safety->RenewToken();
            s.func->SetRunBit(true);
            s.autoR->PauseRequested = false;
            McDataManager::RunData()->UnsafePause = false;

            s.status.SetState(EMcState::Running);
            s.status.SetMessage(Loc::T("Mc.AutoRunning"));

            std::lock_guard<std::mutex> lock(s.routineLock);
            auto ct = NewCycleToken(s);
            auto cts = s.cycleCts;   // giữ nguồn token sống tới khi chu trình thoát
            auto autoR = s.autoR; auto func = s.func; McStatus* status = &s.status;
            StartRoutine(s, [autoR, func, status, ct, cts]
            {
                bool ok;
                try { ok = autoR->Run(ct); }
                catch (const base::OperationCanceled&) { ok = false; }
                catch (const std::exception& ex)
                {
                    AlarmManager::RaiseEvent(AlarmCode::RoutineCrashed, "Auto", ex.what());
                    ok = false;
                }
                func->SetRunBit(true);   // chu trình đã thoát — bit tắt thì Về gốc/Calib sau đó đứng chờ mãi

                // Routine tự đặt Paused khi dừng có kiểm soát. Mọi đường ra khác đều là dừng bất thường và không được để
                // máy trông như đang sẵn sàng. WaitOperator cũng phải gỡ: STOP lúc chờ đổi tray làm chu trình thoát ngay
                // trong vòng chờ, để nguyên thì Start chỉ còn "xác nhận" cho một chu trình đã thoát và máy kẹt ở đó.
                EMcState st = status->State();
                if (st == EMcState::Running || st == EMcState::Pausing || st == EMcState::WaitOperator || st == EMcState::Held)
                    status->SetState(ok ? EMcState::Idle : EMcState::Paused);

                McDataManager::SaveRunningData();
            });
        }

        /// <summary>Khởi động chu trình chạy rà — cùng khung với StartAuto nhưng KHÔNG đụng tới sổ sách của chu trình
        /// thật: không lưu tiến độ, không cộng bộ đếm, không đổi trạng thái ô.</summary>
        static void StartDryRun(State& s)
        {
            s.safety->RenewToken();
            s.func->SetRunBit(true);
            s.dry->PauseRequested = false;

            s.status.SetState(EMcState::Running);
            s.status.SetMessage(Loc::T("Mc.DryRunning"));

            std::lock_guard<std::mutex> lock(s.routineLock);
            auto ct = NewCycleToken(s);
            auto cts = s.cycleCts;
            auto dry = s.dry; auto func = s.func; McStatus* status = &s.status;
            StartRoutine(s, [dry, func, status, ct, cts]
            {
                // Chạy rà không có đích để "xong" — mọi đường ra đều là dừng, nên luôn về Paused.
                try { dry->Run(ct); }
                catch (const base::OperationCanceled&) {}
                catch (const std::exception& ex) { AlarmManager::RaiseEvent(AlarmCode::RoutineCrashed, "DryRun", ex.what()); }
                func->SetRunBit(true);
                EMcState st = status->State();
                if (st == EMcState::Running || st == EMcState::Pausing || st == EMcState::Held)
                    status->SetState(EMcState::Paused);
            });
        }

        static void DoStop(State& s)
        {
            McStatus& status = s.status;
            CancelManualSequences(s);
            switch (status.State())
            {
                case EMcState::Homing:
                    // Huỷ về gốc: OrgOk về false, không bao giờ để trạng thái dở dang là "đã về gốc".
                    s.safety->RequestAbort("operator pressed STOP while homing");
                    status.SetMessage(Loc::T("Mc.HomingCancelled"));
                    return;

                case EMcState::WaitOperator:
                    DoPause(s);
                    return;

                case EMcState::Running:
                    // Tắt bit RUN: mọi trục dừng ngay, chu trình đứng nguyên chỗ, nút Start chạy tiếp (Tuấn 2026-09-29).
                    // GIỮ servo và GIỮ vacuum. Trước đó STOP lần 1 chạy nốt tới checkpoint, lần 2 mới dừng ngay.
                    Hold(s, HeldMessage(), "STOP");
                    return;

                case EMcState::Held:
                    status.SetMessage(HeldMessage());
                    return;

                case EMcState::Pausing:
                    // Đang chạy nốt tới checkpoint (sau khi chuyển MANUAL): STOP thì dừng ngay tại chỗ nhưng GIỮ servo và
                    // GIỮ vacuum — cắt chân không lúc này là làm rơi sản phẩm vào trong máy (§3.1).
                    s.safety->RequestAbort("operator pressed STOP twice");
                    s.motion.StopAll();
                    McDataManager::RunData()->UnsafePause = true;
                    McDataManager::SaveRunningData();
                    status.SetState(EMcState::Paused);
                    status.SetMessage(Loc::T("Mc.StoppedInPlace"));
                    Log("STOP #2 — stopped in place, UnsafePause set", LogLevel::Warning);
                    return;

                default:
                    s.motion.StopAll();
                    // Chu trình còn sống mà máy không ở trạng thái chạy nào: STOP huỷ luôn (Tuấn 2026-09-30).
                    if (CycleBusy(s)) EndCycle(s, Loc::T("Mc.WhyOperatorStop"), true);
                    return;
            }
        }

        /// <summary>
        /// Kết thúc chu trình Auto/chạy rà còn sống: cờ dừng cho vòng chờ tray và mọi checkpoint. <paramref name="cancelToken"/>
        /// huỷ luôn token riêng của chu trình — dừng cả lệnh trục/Flow đang chạy (token của Safety có thể đã đổi sau RESET
        /// nên huỷ nó không tới được chu trình cũ).
        /// </summary>
        static void EndCycle(State& s, const std::string& why, bool cancelToken)
        {
            s.cycleEnding = true;
            s.autoR->PauseRequested = true;
            s.dry->PauseRequested = true;
            if (cancelToken)
            {
                std::shared_ptr<base::LinkedStopSource> cts;
                { std::lock_guard<std::mutex> lock(s.routineLock); cts = s.cycleCts; }
                if (cts) cts->Cancel();
            }
            std::string msg = Loc::F("Mc.CycleCancelled", why);
            s.status.SetMessage(msg);
            Log(msg, LogLevel::Warning);
        }

        static std::string HeldMessage() { return Loc::T("Mc.Held"); }

        /// <summary>Tạm dừng chu trình đang chạy: tắt bit RUN, trục dừng tại chỗ, nút Start chạy tiếp.</summary>
        static void Hold(State& s, const std::string& message, const std::string& why)
        {
            s.func->SetRunBit(false);
            s.autoR->HoldClock(true);
            s.status.SetState(EMcState::Held);
            s.status.SetMessage(message);
            Log(why + " — RUN bit off, axes stopped in place", LogLevel::Info);
        }

        /// <summary>
        /// Rèm sáng vừa bị cắt — SafetyMonitor gọi trên vòng quét, TRƯỚC khi SafeToMove tắt (Tuấn 2026-10-01): dừng mọi
        /// chuyển động; chu trình Auto/chạy rà tạm dừng y như STOP, rèm thông rồi bấm Start chạy tiếp đúng chỗ dở. Về
        /// gốc/Init thì huỷ như STOP lúc về gốc; đang chạy nốt tới checkpoint thì dừng ngay như STOP. Chờ tray thì không
        /// trục nào chạy — người nạp tray đi qua rèm là chuyện thường — nên để nguyên; Start lúc rèm còn cắt bị từ chối
        /// ở DoGo.
        /// </summary>
        static void HoldForCurtain(State& s)
        {
            switch (s.status.State())
            {
                case EMcState::Running:
                    Hold(s, Loc::T("Mc.CurtainHeld"), "Light curtain");
                    return;
                case EMcState::Pausing:
                case EMcState::Homing:
                    DoStop(s);
                    return;
                default:
                    return;
            }
        }

        /// <summary>
        /// Dừng ở checkpoint (RequestPause): chỉ ĐẶT YÊU CẦU, routine chạy nốt bước đang làm rồi thoát, máy về Paused
        /// (§3.4). Chờ tray thì thoát khỏi vòng chờ — chạy lại thì quay về đúng bước đang chờ.
        /// </summary>
        static void DoPause(State& s)
        {
            switch (s.status.State())
            {
                case EMcState::WaitOperator:
                    s.autoR->PauseRequested = true;
                    s.status.SetMessage(Loc::T("Mc.TrayWaitCancelled"));
                    return;

                case EMcState::Running:
                    // Đặt cho cả hai chu trình: chỉ một cái đang chạy, cái kia bỏ qua cờ này.
                    s.dry->PauseRequested = true;
                    s.autoR->PauseRequested = true;
                    s.status.SetState(EMcState::Pausing);
                    s.status.SetMessage(Loc::T("Mc.RunningToCheckpoint"));
                    Log("Stop at checkpoint", LogLevel::Info);
                    return;

                default:
                    return;
            }
        }

        /// <summary>
        /// Bỏ chu trình đang tạm dừng (chuyển MANUAL, có lỗi): huỷ như STOP lần 2 cũ. Trục đứng giữa hành trình nên đánh
        /// dấu UnsafePause — lần chạy sau đòi Init trước, rồi làm lại bước dở từ đầu.
        /// </summary>
        static void AbandonHeld(State& s, const std::string& why)
        {
            s.safety->RequestAbort(why);
            s.func->SetRunBit(true);
            McDataManager::RunData()->UnsafePause = true;
            McDataManager::SaveRunningData();
            if (s.status.State() == EMcState::Held)
            {
                s.status.SetState(EMcState::Paused);
                s.status.SetMessage(Loc::F("Mc.HeldAbandoned", why));
            }
            Log("Paused cycle abandoned — " + why, LogLevel::Warning);
        }

        static void DoReset(State& s)
        {
            s.status.SetBuzzerMuted(true);

            // Bản C# phát ResetAlarmAsync rồi không chờ; ở đây lệnh reset là hàm chặn nên đẩy sang luồng nền để vòng quét
            // không đứng chờ driver.
            for (const auto& axis : s.motion.AxisList())
                if (axis->Alarm())
                    base::RunDetached([axis] { axis->ResetAlarm(); });

            auto stuck = AlarmManager::Reset();
            s.safety->RenewToken();

            if (!stuck.empty())
            {
                // Bấm RESET mà không xoá được gì thì phải nói rõ vì sao — đây là lúc operator hay bấm loạn nhất (§7.4).
                s.status.SetMessage(Loc::F("Mc.ResetStuck", stuck[0].Message(),
                                           stuck.size() > 1 ? Loc::F("Mc.ResetStuckMore", (int)stuck.size() - 1) : std::string()));
                return;
            }

            s.status.SetMessage(Loc::T("Mc.ResetDone"));
            if (s.status.State() == EMcState::Alarm || s.status.State() == EMcState::Emg)
                s.status.SetState(s.status.OrgOk() ? EMcState::Idle : EMcState::NotHomed);
        }

        static void DoOrigin(State& s)
        {
            McStatus* status = &s.status;
            if (RoutineBusy()) { status->SetMessage(Loc::T("Mc.CycleBusy")); return; }
            if (!s.safety->SafeToMove()) { status->SetMessage(Loc::F("Mc.CannotHome", s.safety->BlockReason())); return; }

            // Không đòi lấy sản phẩm ra trước khi về gốc (Tuấn bỏ 2026-09-26).

            // Mọi trục về vị trí chờ nên hai tray ra khỏi máy — chạy lại phải bấm Start nạp lại. TraysOut bỏ luôn số quét
            // ô và tư thế trên tay: về gốc lại thì chúng không còn cùng hệ toạ độ trục (R24); chạy lại từ S00, quét lại.
            s.autoR->TraysOut();

            s.safety->RenewToken();
            status->SetState(EMcState::Homing);
            status->SetMessage(Loc::T("Mc.HomingStarted"));

            auto ct = s.safety->AbortToken();
            auto origin = s.origin; auto func = s.func;   // routine giữ tham chiếu tới Func — giữ nó sống cùng
            std::lock_guard<std::mutex> lock(s.routineLock);
            s.activeIsCycle = false;
            StartRoutine(s, [origin, func, status, ct]
            {
                bool ok;
                try { ok = origin->Run(ct); }
                catch (const base::OperationCanceled&) { ok = false; }
                if (ok) McDataManager::RunData()->UnsafePause = false;   // mọi trục đã ở vị trí chờ, như Init
                status->SetState(ok ? EMcState::Idle : EMcState::NotHomed);
                // Routine chỉ đặt Message khi thất bại — thành công cũng phải nói, không thì trên màn hình còn treo dòng
                // "Bắt đầu chuỗi về gốc" như đang dở dang.
                if (ok) status->SetMessage(Loc::T("Mc.HomingDone"));
            });
        }

        /// <summary>
        /// Init (Tuấn 2026-09-29): nâng Z rồi đưa mọi trục về vị trí chờ, không về gốc lại — dùng sau khi tạm dừng, chuyển
        /// MANUAL và thao tác tay, trước khi chạy tiếp. Y1/Y2 về vị trí chờ nên hai tray ra khỏi máy, chạy tiếp phải bấm
        /// Start nạp lại (tray còn dở thì giữ sổ, nhưng hàng đang làm được chụp lại từ đầu — xem Routine_Auto::TraysOut).
        /// Chạy dưới trạng thái Homing để STOP, đèn và chốt an toàn xử lý y như lúc về gốc.
        /// </summary>
        static void DoInit(State& s)
        {
            McStatus* status = &s.status;
            if (status->State() == EMcState::Held)
            {
                status->SetMessage(Loc::T("Mc.InitWhileHeld"));
                return;
            }
            if (RoutineBusy()) { status->SetMessage(Loc::T("Mc.CycleBusy")); return; }
            if (!status->OrgOk()) { status->SetMessage(Loc::T("Mc.InitNotHomed")); return; }
            if (!s.safety->SafeToMove()) { status->SetMessage(Loc::F("Mc.CannotInit", s.safety->BlockReason())); return; }

            s.autoR->TraysOut();
            s.safety->RenewToken();
            EMcState before = status->State();
            status->SetState(EMcState::Homing);
            status->SetMessage(Loc::T("Mc.InitStarted"));
            Log(status->Message(), LogLevel::Info);

            auto ct = s.safety->AbortToken();
            auto origin = s.origin; auto func = s.func;
            std::lock_guard<std::mutex> lock(s.routineLock);
            s.activeIsCycle = false;
            StartRoutine(s, [origin, func, status, ct, before]
            {
                bool ok;
                try { ok = origin->Init(ct); }
                catch (const base::OperationCanceled&) { ok = false; }
                if (ok)
                {
                    McDataManager::RunData()->UnsafePause = false;
                    McDataManager::SaveRunningData();
                    status->SetMessage(Loc::T("Mc.InitDone"));
                }
                if (status->State() == EMcState::Homing)
                    status->SetState(ok ? (before == EMcState::Paused ? EMcState::Paused : EMcState::Idle) : EMcState::Paused);
            });
        }

        // ───────────────────────── trạng thái & ngõ ra ─────────────────────────

        static void UpdateState(State& s)
        {
            McStatus& status = s.status;
            status.SetOrgOk(s.motion.AllHomed());
            status.SetVisionConnected(s.vision != nullptr && s.vision->IsConnected());

            // EMG và ALARM đè lên mọi trạng thái khác.
            if (AlarmManager::IsActive(AlarmCode::EmergencyStop))
            {
                if (status.State() != EMcState::Emg)
                {
                    status.SetState(EMcState::Emg);
                    McDataManager::SaveRunningData();   // chốt tiến độ ngay khi vào EMG (§3.7)
                    Log("Entered EMG state", LogLevel::Error);
                }
                return;
            }

            if (AlarmManager::HasError())
            {
                if (status.State() != EMcState::Alarm)
                {
                    status.SetState(EMcState::Alarm);
                    McDataManager::SaveRunningData();
                }
                return;
            }

            // Thoát EMG/ALARM cần bấm RESET — không tự chạy lại khi lỗi chập chờn tự hết (§7.4).
            if (status.State() == EMcState::Emg || status.State() == EMcState::Alarm) return;

            if (status.State() == EMcState::Init && s.motion.AllConnected())
                status.SetState(EMcState::NotHomed);

            if (status.State() == EMcState::Idle && !status.OrgOk())
                status.SetState(EMcState::NotHomed);
        }

        static void UpdateOutputs(State& s)
        {
            McStatus& status = s.status;
            IIoDevice& io = *s.io;
            ETowerLamp lamp = ComputeLamp(s);
            status.SetTowerLamp(lamp);

            long long ms = s.clock.ElapsedMilliseconds();
            bool blinkPhase = (ms / 250) % 2 == 0;   // 2 Hz
            bool blinkSlow = (ms / 500) % 2 == 0;    // 1 Hz

            bool red = lamp == ETowerLamp::Red || (lamp == ETowerLamp::RedBlink && blinkPhase);
            bool yellow = lamp == ETowerLamp::Yellow || (lamp == ETowerLamp::YellowBlink && blinkSlow);
            bool green = lamp == ETowerLamp::Green || (lamp == ETowerLamp::GreenBlink && blinkSlow);

            io.SetDo(IoMap::DoLampRed, red);
            io.SetDo(IoMap::DoLampYellow, yellow);
            io.SetDo(IoMap::DoLampGreen, green);

            bool wantBuzzer = AlarmManager::HasError();
            if (!wantBuzzer) status.SetBuzzerMuted(false);   // alarm mới sẽ lại kêu
            status.SetBuzzer(wantBuzzer && !status.BuzzerMuted());
            io.SetDo(IoMap::DoBuzzer, status.Buzzer());

            // "Đang chạy" = Running/Pausing, cùng ranh giới với đèn tháp xanh liền (xem ComputeLamp) — Pausing vẫn còn di
            // chuyển tới checkpoint, chưa thật sự dừng.
            EMcState st = status.State();
            bool running = st == EMcState::Running || st == EMcState::Pausing;

            // Đèn ba nút Start (chung hai nút cụm A, Start 1, Start 2) y hệt nhau — bốn nút như nhau: sáng IM khi đang
            // chạy, mọi trạng thái khác TẮT, không nhấp nháy (Tuấn 2026-10-16) — kể cả Armed, tạm dừng (Held) và chờ nạp
            // tray (CHỜ THAO TÁC). Trước đó Armed/Held nháy 1 Hz, chờ tray nháy nhanh 0.1 s gọi người nạp.
            io.SetDo(IoMap::DoLampStartBtn, running);
            io.SetDo(IoMap::DoLampStartBtnB, running);
            io.SetDo(IoMap::DoLampStartBtnB2, running);

            // Đèn nút STOP (DO09 — tên hằng số còn chữ Reset vì đèn này trước báo "cần RESET"): sáng IM mọi lúc trừ khi
            // đang chạy (Tuấn đổi 2026-09-26). Kể cả CHỜ THAO TÁC — tray lắp ráp xong chờ thay, chờ tray đầu vào: máy đứng
            // nên STOP sáng (Tuấn 2026-10-19: "đang tắt cả 2 đèn"; từ 2026-10-01 tới đó chờ tray là tắt).
            io.SetDo(IoMap::DoLampResetBtn, !running);
        }

        /// <summary>
        /// Bảng đèn tháp — PLAN.md §3.6. Tuấn chốt 2026-09-26: dừng là VÀNG, Armed là XANH NHÁY, chạy là XANH IM, lỗi là
        /// ĐỎ — xanh chỉ dành cho "đang chạy / sắp chạy".
        /// </summary>
        static ETowerLamp ComputeLamp(State& s)
        {
            switch (s.status.State())
            {
                case EMcState::Emg:          return ETowerLamp::RedBlink;
                case EMcState::Alarm:        return ETowerLamp::Red;
                case EMcState::Running:      return ETowerLamp::Green;
                case EMcState::Pausing:      return ETowerLamp::Green;
                case EMcState::Paused:       return ETowerLamp::Yellow;
                case EMcState::Held:         return ETowerLamp::Yellow;
                case EMcState::WaitOperator: return ETowerLamp::YellowBlink;
                case EMcState::Armed:        return ETowerLamp::GreenBlink;
                case EMcState::Idle:         return AlarmManager::HasWarning() ? ETowerLamp::YellowBlink : ETowerLamp::Yellow;
                case EMcState::NotHomed:     return ETowerLamp::Yellow;
                case EMcState::Homing:       return ETowerLamp::YellowBlink;
                default:                     return ETowerLamp::Off;
            }
        }
    };

    // ───────────────────────── accessor ─────────────────────────

    McStatus& McControl::Status() { return St().status; }
    MotionConnect& McControl::Motion() { return St().motion; }
    std::shared_ptr<IIoDevice> McControl::Io() { auto& s = St(); std::lock_guard<std::mutex> l(s.devMutex); return s.io; }
    std::shared_ptr<IVisionDevice> McControl::Vision() { auto& s = St(); std::lock_guard<std::mutex> l(s.devMutex); return s.vision; }
    std::shared_ptr<SafetyMonitor> McControl::Safety() { auto& s = St(); std::lock_guard<std::mutex> l(s.devMutex); return s.safety; }
    std::shared_ptr<McFunction> McControl::Func() { auto& s = St(); std::lock_guard<std::mutex> l(s.devMutex); return s.func; }
    std::shared_ptr<Routine_Calib> McControl::Calib() { auto& s = St(); std::lock_guard<std::mutex> l(s.devMutex); return s.calib; }
    std::shared_ptr<Routine_Auto> McControl::AutoRoutine() { auto& s = St(); std::lock_guard<std::mutex> l(s.devMutex); return s.autoR; }
    std::shared_ptr<Routine_DryCycle> McControl::DryRoutine() { auto& s = St(); std::lock_guard<std::mutex> l(s.devMutex); return s.dry; }
    double McControl::CurrentCycleSec() { auto a = AutoRoutine(); return a ? a->CurrentCycleSec() : 0; }
    base::Event<std::string>& McControl::AutoModeRefused() { return St().autoModeRefused; }
    bool McControl::IsInitialized() { return St().initialized.load(); }
    int McControl::VisionLoadCount() { return St().visionLoadCount.load(); }

    void McControl::RequestSetMode(EMcMode mode) { St().requestedMode = (int)mode; }
    void McControl::RequestStart() { St().reqStart.exchange(1); }
    void McControl::RequestStop() { St().reqStop.exchange(1); }
    void McControl::RequestPause() { St().reqPause.exchange(1); }
    void McControl::RequestReset() { St().reqReset.exchange(1); }
    void McControl::RequestOrigin() { St().reqOrigin.exchange(1); }
    void McControl::RequestInit() { St().reqInit.exchange(1); }
    void McControl::RequestMuteBuzzer() { St().reqMute.exchange(1); }
    void McControl::RequestArm() { St().reqArm.exchange(1); }
    void McControl::RequestGo() { St().reqGo.exchange(1); }

    bool McControl::RoutineBusy()
    {
        State& s = St();
        std::lock_guard<std::mutex> lock(s.routineLock);
        return s.routineDone && !s.routineDone->load();
    }

    std::stop_token McControl::ManualToken()
    {
        State& s = St();
        auto safety = Safety();
        std::lock_guard<std::mutex> lock(s.manualLock);
        std::stop_token safetyToken = safety ? safety->AbortToken() : std::stop_token();
        if (!s.manualCts || s.manualCts->IsCancellationRequested() || !(s.manualLinkedTo == safetyToken))
        {
            // Không giữ cái cũ cũng an toàn: token Safety chỉ đổi sau khi đã bị huỷ (SafetyMonitor::RenewToken), nên
            // nguồn cũ — nối vào nó — cũng đã bị huỷ; chuỗi đang chạy còn giữ token của nó, trạng thái huỷ không mất.
            s.manualCts = std::make_shared<base::LinkedStopSource>(safetyToken);
            s.manualLinkedTo = safetyToken;
        }
        return s.manualCts->Token();
    }

    // ───────────────────────── chọn tay / chạy rà / tray ─────────────────────────

    void McControl::SetOnlyNozzle(int nozzle)
    {
        auto sys = McDataManager::SysData();
        if (sys == nullptr || sys->OnlyNozzle == nozzle) return;

        McStatus& status = Status();
        EMcState st = status.State();
        if (st == EMcState::Running || st == EMcState::Pausing || st == EMcState::Held ||
            st == EMcState::Homing || st == EMcState::WaitOperator)
        {
            status.SetMessage(Loc::T("Mc.BusyNozzleChange"));
            return;
        }

        sys->OnlyNozzle = nozzle == 1 || nozzle == 2 ? nozzle : 0;
        McDataManager::SaveSystemData();
        status.SetMessage(sys->OnlyNozzle == 0 ? Loc::T("Mc.BothNozzles") : Loc::F("Mc.OnlyNozzle", sys->OnlyNozzle));
        Log(status.Message(), LogLevel::Warning);
    }

    void McControl::SetDryRun(bool on)
    {
        McStatus& status = Status();
        if (status.DryRun() == on) return;

        EMcState st = status.State();
        if (st == EMcState::Running || st == EMcState::Pausing || st == EMcState::Held ||
            st == EMcState::Homing || st == EMcState::WaitOperator)
        {
            status.SetMessage(Loc::T("Mc.BusyDryRunChange"));
            return;
        }

        auto func = Func();
        if (on && func && (func->HasProduct(1) || func->HasProduct(2)))
        {
            status.SetMessage(Loc::T("Mc.DryRunNozzleHasProduct"));
            return;
        }

        status.SetDryRun(on);
        if (on) { if (auto dry = DryRoutine()) dry->ResetCounters(); }
        status.SetMessage(on ? Loc::T("Mc.DryRunOn") : Loc::T("Mc.DryRunOff"));
        Log(status.Message(), LogLevel::Warning);
    }

    void McControl::SetStopOnVisionNg(bool on)
    {
        McStatus& status = Status();
        if (status.StopOnVisionNg() == on) return;
        status.SetStopOnVisionNg(on);
        status.SetMessage(on ? Loc::T("Mc.VisionNgStopOn") : Loc::T("Mc.VisionNgStopOff"));
        Log(status.Message(), LogLevel::Warning);
    }

    void McControl::InvalidateVisionCache() { if (auto a = AutoRoutine()) a->RequestVisionCacheInvalidation(); }

    void McControl::InvalidateAssyScan() { if (auto a = AutoRoutine()) a->InvalidateAssyScan(); }

    void McControl::ReloadInputTray()
    {
        McDataManager::RunData()->LoadInputTrays(McDataManager::Model()->UsableInputTrays());
        McDataManager::SaveRunningData();
    }

    void McControl::ResetAssyTray()
    {
        auto run = McDataManager::RunData();
        run->AssyTray->ResetAll(ECellState::Empty);
        // Tray mới — đồng hồ thực tế đếm lại từ lần chạy kế tiếp với tray trong máy (Routine_Auto).
        run->AssyTrayStartedAt.reset();
        InvalidateAssyScan();
        if (auto a = AutoRoutine()) a->RequestCycleRestart();

        auto func = Func();
        bool holding = run->Nozzle1->HasProduct || run->Nozzle2->HasProduct ||
                       (func != nullptr && (func->HasProduct(1) || func->HasProduct(2)));
        if (holding)
        {
            Status().SetMessage(Loc::T("Mc.AssyTrayReloadHolding"));
            Log(Status().Message(), LogLevel::Info);
        }
        McDataManager::SaveRunningData();
    }

    void McControl::ToggleCellSelection(bool input, int index)
    {
        if (Status().Mode() != EMcMode::Auto) return;
        auto run = McDataManager::RunData();
        auto tray = input ? run->InputTray : run->AssyTray;
        if (index < 0 || index >= tray->CellCount()) return;
        if (!tray->IsSelected(index))
        {
            ECellState open = input ? ECellState::Loaded : ECellState::Empty;
            const auto& cells = tray->Cells;
            bool trayDone = std::find(cells.begin(), cells.end(), open) == cells.end() &&
                            std::find(cells.begin(), cells.end(), ECellState::Processing) == cells.end();
            if (tray->Get(index) != open && !trayDone)
            {
                Status().SetMessage(Loc::T(input ? "Mc.InputCellNotSelectable" : "Mc.AssyCellNotSelectable"));
                return;
            }
        }
        tray->ToggleSelected(index);
    }

    void McControl::ClearCellSelection(bool input)
    {
        auto run = McDataManager::RunData();
        (input ? run->InputTray : run->AssyTray)->ClearSelection();
    }

    // ───────────────────────── khởi tạo ─────────────────────────

    void McControl::Init()
    {
        State& s = St();
        if (s.running) return;   // đang chạy rồi — Shutdown() trước khi Init() lại
        auto sys = McDataManager::SysData();
        auto sysFn = [] { return McDataManager::SysData(); };

        // Khởi tạo lại sau Shutdown() (bài tự kiểm): không mang yêu cầu/chế độ của phiên trước sang — như mở lại app.
        for (auto* r : { &s.reqStart, &s.reqStop, &s.reqPause, &s.reqReset, &s.reqOrigin, &s.reqInit, &s.reqMute, &s.reqArm, &s.reqGo })
            r->store(0);
        s.requestedMode = (int)EMcMode::Manual;
        s.status.SetMode(EMcMode::Manual);
        s.status.SetOrgOk(false);
        s.status.SetDryRun(false);
        s.status.SetStopOnVisionNg(false);
        s.status.SetBuzzerMuted(false);
        s.status.SetMessage({});

        auto model = McDataManager::Model();
        s.status.SetSimulation(sys->SimulationMode);
        // nullopt = "(chưa nạp)" theo ngôn ngữ, xem McStatus::ModelName.
        s.status.SetModelName(model ? std::optional<std::string>(model->Name) : std::nullopt);
        s.status.SetState(EMcState::Init);

        s.motion.Init(*sys);
        s.motion.SetZSafe(sys->ZSafe);

        std::shared_ptr<IIoDevice> io;
        if (sys->SimulationMode) io = std::make_shared<FakeIo>();
        else io = std::make_shared<IoConnect>(sys);

        // Như bản C#: mô phỏng → FakeVision (không bao giờ chạm VisionMaster), máy thật → VisionConnect.
        std::shared_ptr<IVisionDevice> vision;
        std::shared_ptr<FakeVision> fakeVision;
        if (sys->SimulationMode)
        {
            fakeVision = std::make_shared<FakeVision>();
            fakeVision->SetNames([] { return McDataManager::SysData()->VisionOutputs; });
            fakeVision->SetFlowOutputSource([](const std::string& flow)
                { return VmCycle::FakeFlowOutputs(flow, McDataManager::SysData()->VisionOutputs); });
            vision = fakeVision;
        }
        else
        {
            vision = std::make_shared<VisionConnect>(sys);
        }

        auto safety = std::make_shared<SafetyMonitor>(s.motion, s.status, sysFn);
        safety->AttachIo(io);
        safety->SetCurtainBroken([&s] { Impl::HoldForCurtain(s); });

        auto func = std::make_shared<McFunction>(s.motion, *safety, s.status, sysFn);
        func->SetIo(io);
        auto visionFn = [] { return McControl::Vision(); };
        auto origin = std::make_shared<Routine_Origin>(s.motion, *func, s.status, sysFn);
        auto autoR = std::make_shared<Routine_Auto>(s.motion, *func, s.status, sysFn, visionFn);
        auto dry = std::make_shared<Routine_DryCycle>(s.motion, *func, s.status, sysFn);
        auto calib = std::make_shared<Routine_Calib>(s.motion, *func, sysFn, visionFn);
        {
            std::lock_guard<std::mutex> lock(s.devMutex);
            // Thứ tự gỡ bản cũ (sau Shutdown): routine trước, Func sau — routine giữ tham chiếu tới Func.
            s.calib = calib; s.dry = dry; s.autoR = autoR; s.origin = origin;
            s.func = func; s.safety = safety; s.vision = vision; s.io = io;
        }
        {
            std::lock_guard<std::mutex> lock(s.routineLock);
            s.routineDone.reset(); s.cycleCts.reset(); s.activeIsCycle = false; s.cycleEnding = false;
        }
        {
            std::lock_guard<std::mutex> lock(s.manualLock);
            s.manualCts.reset(); s.manualLinkedTo = std::stop_token();
        }

        s.motion.ConnectAll();
        io->Connect();
        vision->Connect();
        Impl::LoadVisionSolution(s, *sys);

        if (sys->SimulationMode) Impl::InstallSimulationModel(s);

        // Solution giả có đủ biến, bản ghi hiệu chuẩn đánh dấu đã qua Bước 2 — chạy thử được Auto mà không cần VisionMaster.
        if (fakeVision)
        {
            auto cur = McDataManager::SysData();
            fakeVision->SeedVmSolution(VisionGlobalVarCatalog::Build(cur.get()));
            auto m = McDataManager::Model();
            if (m != nullptr && m->Calib.IsUsable())
                fakeVision->StampCalibrated(&m->Calib);
        }

        s.status.SetState(EMcState::NotHomed);
        s.initialized = true;

        Impl::StartLoop(s, sys->ControlTickMs);
        Log(sys->SimulationMode ? "Init done — SIMULATION MODE, no real hardware is driven" : "Init done", LogLevel::Info);
    }

    std::string McControl::ReloadVisionSolution()
    {
        // Nạp giữ khoá lệnh vision nhiều giây — chu trình đang chạy thì lần kích Flow kế tiếp quá giờ.
        if (RoutineBusy()) return Loc::T("Mc.ReloadSolutionBusy");
        InvalidateVisionCache();
        Impl::LoadVisionSolution(St(), *McDataManager::SysData());
        return {};
    }

    GlobalVarSyncReport McControl::SyncVisionGlobalVariables(bool apply)
    {
        auto vision = Vision();
        if (!vision || !vision->IsSolutionLoaded())
            return GlobalVarSyncReport::Fail(Loc::T("Mc.SolutionNotLoaded"));
        if (apply && RoutineBusy())
            return GlobalVarSyncReport::Fail(Loc::T("Mc.CreateVarsBusy"));

        auto specs = VisionGlobalVarCatalog::Build(McDataManager::SysData().get());
        auto report = vision->SyncGlobalVariables(specs, apply);

        bool bad = !report.Error.empty() || report.Count(EGlobalVarState::Failed) > 0 ||
                   report.Count(EGlobalVarState::WrongType) > 0 || report.Count(EGlobalVarState::InvalidName) > 0;
        Log((apply ? "Create global variables: " : "Check global variables: ") + ReplaceNewLines(report.ToText()),
            bad ? LogLevel::Warning : LogLevel::Info, "Vision");
        return report;
    }

    VmOutCheckReport McControl::CheckVisionFlowOutputs()
    {
        auto vision = Vision();
        if (!vision || !vision->IsSolutionLoaded())
            return VmOutCheckReport::Fail(Loc::T("Mc.SolutionNotLoaded"));

        VisionOutputNames names = McDataManager::SysData()->VisionOutputs;
        auto scripts = VmScriptContract::Build(names);

        VmOutCheckReport report;
        for (const auto& expect : VmCycle::ExpectedFlowOutputs(names))
        {
            VmFlowOutputs actual = vision->ReadFlowOutputs(expect.Flow);
            VmOutCheck::CompareFlow(expect, &actual, report);

            // Cùng một lần đọc dùng cho cả hai tầng — Flow và Script bên trong nó.
            for (const auto& sc : scripts)
                if (sc.Flow == expect.Flow) VmOutCheck::CompareScript(sc, &actual, report);
        }

        Log("Check Flow outputs: " + ReplaceNewLines(report.ToText()),
            report.Clean() ? LogLevel::Info : LogLevel::Warning, "Vision");
        return report;
    }

    void McControl::SyncNozzleStateFromSensor()
    {
        McStatus& status = Status();
        if (status.State() == EMcState::Running || status.State() == EMcState::Homing)
        {
            status.SetMessage(Loc::T("Mc.SyncOnlyStopped"));
            return;
        }

        auto func = Func();
        if (!func) return;
        auto run = McDataManager::RunData();
        run->Nozzle1->HasProduct = func->HasProduct(1);
        run->Nozzle2->HasProduct = func->HasProduct(2);
        if (!run->Nozzle1->HasProduct) { run->Nozzle1->SourceCellIndex = -1; run->Nozzle1->Aligned = false; }
        if (!run->Nozzle2->HasProduct) { run->Nozzle2->SourceCellIndex = -1; run->Nozzle2->Aligned = false; }

        AlarmManager::ClearCondition(AlarmCode::ResumeIntegrity);
        McDataManager::SaveRunningData();

        status.SetMessage(Loc::T("Mc.NozzleSynced"));
        Log(status.Message(), LogLevel::Warning);
    }

    void McControl::Shutdown()
    {
        State& s = St();
        auto safety = Safety();
        auto io = Io();
        auto vision = Vision();
        s.initialized = false;

        // Huỷ AbortToken TRƯỚC KHI đóng bất cứ cổng nào — mọi routine Auto/Origin VÀ mọi lệnh Jog/Move/Home thủ công từ
        // trang Manual đều observe chung Safety.AbortToken. Không huỷ trước thì một lệnh đang dở tay (đang gửi hoặc đang
        // chờ phản hồi Modbus/RS485) có thể vẫn giữ khoá gửi lệnh của trục đúng lúc Motion.DisconnectAll() đóng cổng, dễ
        // làm driver "đơ" tới khi rút/cắm lại nguồn (Tuấn lo ngại lúc đóng app, 2026-09-10). Đợi routine kết thúc CÓ GIỚI
        // HẠN — sau khi token bị huỷ, các vòng chờ vị trí thoát gần như ngay; timeout chỉ đề phòng driver treo thật.
        if (safety) safety->RequestAbort("Application closing");
        // Token riêng của chu trình có thể đã tách khỏi token Safety hiện hành (RESET đổi token) — huỷ luôn.
        {
            std::shared_ptr<base::LinkedStopSource> cts;
            { std::lock_guard<std::mutex> lock(s.routineLock); cts = s.cycleCts; }
            if (cts) cts->Cancel();
        }
        Impl::JoinRoutine(s, 3000);

        s.running = false;
        if (s.watchdog.joinable()) { s.watchdog.request_stop(); s.watchdog.join(); }
        if (s.loop.joinable()) { s.loop.request_stop(); s.loop.join(); }

        // Vòng quét có thể vừa kịp khởi động một routine mới (và cấp token mới) trước khi dừng.
        if (safety) safety->RequestAbort("Application closing");
        {
            std::shared_ptr<base::LinkedStopSource> cts;
            { std::lock_guard<std::mutex> lock(s.routineLock); cts = s.cycleCts; }
            if (cts) cts->Cancel();
        }
        Impl::JoinRoutine(s, 3000);

        s.motion.StopAll();
        McDataManager::SaveRunningData();

        // Việc nền còn dở (nạp solution vision, reset alarm trục) phải xong trước khi đóng thiết bị.
        if (!base::WaitBackgroundIdle(10000))
            Log("Shutdown: background work still running after 10 s", LogLevel::Warning);

        s.motion.DisconnectAll();
        if (io) io->Disconnect();
        if (vision) vision->Disconnect();
    }
}
