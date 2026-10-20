#include "Communication/PanasonicAxis.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <format>

#include "Infor/AlarmManager.h"
#include "Lang/Loc.h"
#include "Utility/Log/LogWorker.h"

namespace sca
{
    namespace
    {
        /// <summary>C# <c>finally { _cmdLock.Release(); }</c> sau một lần Wait thành công.</summary>
        template <class L>
        struct ReleaseOnExit
        {
            L& lock;
            ~ReleaseOnExit() { lock.Release(); }
        };
    }

    // ───────────────────────── khoá lệnh (SemaphoreSlim) ─────────────────────────

    void PanasonicAxis::CmdLock::Wait()
    {
        std::unique_lock<std::mutex> l(m_mutex);
        m_cv.wait(l, [this] { return !m_taken; });
        m_taken = true;
    }

    void PanasonicAxis::CmdLock::Wait(std::stop_token ct)
    {
        // SemaphoreSlim.WaitAsync(ct) với token đã huỷ trả task bị huỷ ngay, kể cả khi khoá đang rảnh.
        if (ct.stop_requested()) throw base::OperationCanceled();
        std::unique_lock<std::mutex> l(m_mutex);
        if (!m_cv.wait(l, ct, [this] { return !m_taken; })) throw base::OperationCanceled();
        m_taken = true;
    }

    bool PanasonicAxis::CmdLock::Wait(int timeoutMs)
    {
        std::unique_lock<std::mutex> l(m_mutex);
        if (!m_cv.wait_for(l, std::chrono::milliseconds(timeoutMs), [this] { return !m_taken; })) return false;
        m_taken = true;
        return true;
    }

    void PanasonicAxis::CmdLock::Release()
    {
        {
            std::lock_guard<std::mutex> l(m_mutex);
            m_taken = false;
        }
        m_cv.notify_one();
    }

    // ───────────────────────── dựng ─────────────────────────

    PanasonicAxis::PanasonicAxis(std::shared_ptr<AxisConfig> cfg)
        : PanasonicAxis(std::move(cfg), nullptr)
    {
    }

    PanasonicAxis::PanasonicAxis(std::shared_ptr<AxisConfig> cfg, std::shared_ptr<servo::ISerialPort> port)
        : m_name(cfg->Name), m_cfg(cfg), m_scale(cfg), m_servo(std::make_shared<servo::ServoService>()),
          m_unitId(cfg->UnitId), m_port(std::move(port))
    {
    }

    std::shared_ptr<PanasonicAxis> PanasonicAxis::KeepAlive()
    {
        return weak_from_this().lock();
    }

    std::shared_ptr<servo::ServoAxisConfig> PanasonicAxis::AxisCfg() const
    {
        std::lock_guard<std::mutex> l(m_axisCfgMutex);
        return m_axisCfg;
    }

    bool PanasonicAxis::RunWithTimeout(std::function<void(servo::ServoService&)> work, int timeoutMs)
    {
        struct State
        {
            std::mutex m;
            std::condition_variable cv;
            bool done = false;
            std::exception_ptr error;
        };
        auto state = std::make_shared<State>();
        auto service = m_servo;   // bị bỏ lại khi hết giờ thì việc nền vẫn giữ ServoService sống tới lúc xong
        base::RunDetached([state, service, work = std::move(work)]
        {
            std::exception_ptr error;
            try { work(*service); }
            catch (...) { error = std::current_exception(); }
            {
                std::lock_guard<std::mutex> l(state->m);
                state->error = error;
                state->done = true;
            }
            state->cv.notify_all();
        });

        std::unique_lock<std::mutex> l(state->m);
        if (!state->cv.wait_for(l, std::chrono::milliseconds(timeoutMs), [&] { return state->done; })) return false;
        // Task.Wait ném lại lỗi của task đã xong (AggregateException) — giữ đúng: bên gọi bắt như C#.
        if (state->error) std::rethrow_exception(state->error);
        return true;
    }

    // ───────────────────────── kết nối ─────────────────────────

    bool PanasonicAxis::Connect()
    {
        m_opened = false;
        m_failStreak = 0;
        try
        {
            auto config = std::make_shared<servo::ServoConfig>();
            config->ComPort = m_cfg->ComPort;
            config->BaudRate = m_cfg->BaudRate;
            // A6SF đấu tay 115200/Even/1 stop bit (Tuấn chốt, 2026-09-08) — ghi tường
            // minh thay vì trông cậy mặc định của ServoConfig: StopBits mặc định của thư
            // viện là Two (chỉnh riêng cho project AutoScrews), sai với máy này.
            config->Parity = servo::Parity::Even;
            config->StopBits = servo::StopBits::One;
            // Nghỉ giữa hai lượt đọc trạng thái. App chỉ biết trục "đã tới" ở lượt đọc kế
            // tiếp sau khi trục dừng, nên khoảng nghỉ này là thời gian chết cuối mỗi lệnh chạy
            // (Tuấn yêu cầu 2026-09-25). Trước đây 50 — Task.Delay làm tròn lên bội 15.6 ms của
            // đồng hồ Windows nên thực tế ~63 ms; 10 thành ~16 ms, đặt nhỏ hơn cũng vẫn ~16 ms.
            // Mỗi trục một cổng COM nên đọc dày không lấn trục nào khác. Cái giá: lệnh mới dễ
            // gặp một lượt đọc đang dở hơn và phải chờ nó xong. 2026-10-01 Tuấn nới lên 30 (~31 ms
            // thực tế) cho đường RS485 bớt dày, đi cùng thời gian chờ phản hồi 100 ms bên dưới.
            config->MonitorIntervalMs = 30;
            // Chờ phản hồi Modbus tối đa 100 ms thay vì 1000 ms mặc định của FluentModbus (Tuấn
            // 2026-10-01). Mất một gói phản hồi thì vòng đọc giữ khoá cổng suốt thời gian chờ: trạng
            // thái trục đứng yên, lệnh mới xếp hàng sau — máy khựng ~1 s sau khi Z nâng xong. Phản
            // hồi bình thường ở 115200 về trong vài ms.
            config->ResponseTimeoutMs = 100;
            // QUAN TRỌNG: ServoService::MoveAbsolute tự kiểm tra IsAtPosition bằng CHÍNH giá trị
            // này TRƯỚC KHI ghi bất cứ thanh ghi nào (kể cả tốc độ) — nếu đang trong dung sai thì
            // return ngay, im lặng, không gửi gì xuống driver. Trước đây KHÔNG gán field này, nên
            // nó luôn ở mặc định cứng của thư viện (50 xung = 0.05mm) bất kể
            // AxisConfig::PositionTolerancePulse đặt bao nhiêu — đây là nguyên nhân thật của việc
            // Inching 0.02mm/0.05mm "không di chuyển được" — Tuấn phát hiện, 2026-09-09. Đồng bộ
            // thẳng với PositionTolerancePulse để chỉ còn MỘT chỗ cấu hình dung sai.
            // (Ban đầu tưởng đây cũng là nguyên nhân "đổi tốc độ không có tác dụng" — KHÔNG PHẢI,
            // cái đó là do ô tốc độ sai, xem AbsoluteVelocityIndex bên dưới.)
            config->PositionTolerance = m_cfg->PositionTolerancePulse;
            // Từ 2026-10-15 ngưỡng BỎ QUA lệnh tách riêng, nhỏ hơn nhiều (5 xung = 0.005 mm): system.json đặt
            // PositionTolerancePulse 50 thì lệnh 0.05 mm Y1 giữa hai lần hút cùng hàng không tới driver → 2302.
            config->SkipMoveTolerance = LibrarySkipTolerance(*m_cfg);

            auto axisCfg = std::make_shared<servo::ServoAxisConfig>();
            axisCfg->Name = m_cfg->Name;
            axisCfg->UnitId = m_unitId;
            axisCfg->HomeBlock = m_cfg->HomeBlock;
            axisCfg->AbsoluteBlock = m_cfg->AbsoluteBlock;
            axisCfg->JogPositiveBlock = m_cfg->JogPositiveBlock;
            axisCfg->JogNegativeBlock = m_cfg->JogNegativeBlock;
            // Ô tốc độ của block ABS — mặc định thư viện là 1 (ô của Jog), sai với bảng
            // block máy này nên MoveAbs không bao giờ nhận tốc độ mới. Xem
            // AxisConfig::AbsoluteVelocityIndex.
            axisCfg->AbsoluteVelocityIndex = m_cfg->AbsoluteVelocityIndex;
            // Thời gian tăng/giảm tốc thật (ms) — chỉnh được ở trang Setting. Kẹp về dải
            // thanh ghi cho phép ngay tại đây để một con số gõ nhầm (0 hoặc âm) không đi
            // xuống driver thành lệnh tăng tốc tức thời.
            axisCfg->AbsoluteAccelMs = ClampRampMs(m_cfg->AccelTimeMs);
            axisCfg->AbsoluteDecelMs = ClampRampMs(m_cfg->DecelTimeMs);
            axisCfg->AbsoluteRampRpm = m_scale.ToRpm(m_cfg->MoveSpeed);
            config->Axes.push_back(axisCfg);
            {
                std::lock_guard<std::mutex> l(m_axisCfgMutex);
                m_axisCfg = axisCfg;
            }

            // m_port null = mở AxisConfig::ComPort (Connect(config, nullptr) của thư viện).
            if (!m_servo->Connect(config, m_port))
            {
                LogWorker::Instance().RecordLog(
                    std::format("{}: cannot open {}", m_name, m_cfg->ComPort), LogLevel::Error, m_name);
                m_connected = false;
                return false;
            }

            m_servo->StartMonitoring();

            // A6SF bỏ qua MỌI lệnh điều khiển gửi qua Modbus (SON, Jog, Home, MoveAbs, Alarm
            // Clear...) tới khi driver được cấp "quyền điều khiển Modbus" — ghi 0x0055 vào
            // thanh ghi 0x4300 (SX-DSV03042, xem A6SFRegisters::MODBUS_RIGHTS). Thiếu bước này
            // thì lệnh vẫn gửi đi, Modbus ACK bình thường (không lỗi nào cả) — driver chỉ ÂM
            // THẦM không làm gì, quyền điều khiển vẫn thuộc về I/O phần cứng (CN X5) hoặc bảng
            // điều khiển tại chỗ. Đúng triệu chứng Tuấn báo: "bấm SVO ON, lệnh gửi đi rồi
            // nhưng servo không lên" (2026-09-09).
            //
            // Có hạn giờ chứ KHÔNG chờ trần: một lần đọc Modbus treo trên phần cứng thật (tuỳ
            // driver/cáp USB-RS485) sẽ khoá luôn McControl::Init() — app chạy nhưng không bao giờ
            // hiện được cửa sổ chính (Tuấn báo lỗi, 2026-09-09, bản C# sau đúng một lần deploy có
            // lời chờ trần). Không xin được quyền trong 2s thì vẫn tiếp tục kết nối — SVO ON có
            // thể chưa dùng được ngay, còn hơn app không mở lên được.
            const int unitId = m_unitId;
            if (!RunWithTimeout([unitId](servo::ServoService& s) { s.AcquireModbusRights(unitId); }, 2000))
            {
                LogWorker::Instance().RecordLog(
                    m_name + ": Modbus control rights request took > 2 s, drive may not be answering — connecting anyway; "
                             "SVO ON/Jog/Home may not work until rights are acquired (retried on next Connect())",
                    LogLevel::Warning, m_name);
            }

            m_opened = true;
            {
                std::lock_guard<std::mutex> lock(m_sinceReadMutex);
                m_sinceRead.Restart();
                m_sinceReadRunning = true;
            }
            m_connected = true;
            LogWorker::Instance().RecordLog(
                std::format("{}: connected {} @ {}, UnitId {}", m_name, m_cfg->ComPort, m_cfg->BaudRate, m_unitId),
                LogLevel::Info, m_name);
            return true;
        }
        catch (const std::exception& ex)
        {
            LogWorker::Instance().RecordLog(std::format("{}: connect error — {}", m_name, ex.what()),
                LogLevel::Error, m_name);
            m_connected = false;
            return false;
        }
    }

    void PanasonicAxis::Disconnect()
    {
        // Xin m_cmdLock trước khi đụng vào cổng — xem chú thích của CmdLock. Có hạn giờ chứ không
        // chờ vô hạn: phòng driver treo thật (không phản hồi) thì việc đóng app vẫn phải kết thúc
        // được, không đơ theo. gotLock = false (hết giờ mà chưa xin được) vẫn cứ đóng cổng — trì
        // hoãn thêm cũng không giải quyết được driver đã treo, thà đóng còn hơn treo cả app.
        bool gotLock = false;
        try { gotLock = m_cmdLock.Wait(3000); } catch (...) {}
        try
        {
            // Trả quyền điều khiển Modbus lại cho I/O phần cứng/bảng điều khiển tại chỗ trước
            // khi đóng cổng — xem AcquireModbusRights ở Connect(). Phải làm TRƯỚC
            // m_servo->Disconnect() vì cần cổng còn mở để gửi được lệnh ghi này. Có hạn giờ
            // — cùng lý do ở Connect().
            const int unitId = m_unitId;
            try { RunWithTimeout([unitId](servo::ServoService& s) { s.ReleaseModbusRights(unitId); }, 2000); } catch (...) {}
            try { m_servo->StopMonitoring(); } catch (...) {}
            try { m_servo->Disconnect(); } catch (...) {}
        }
        catch (...) {}
        if (gotLock) m_cmdLock.Release();

        m_opened = false;
        m_failStreak = 0;
        m_connected = false;
        m_softHomed = false;
        m_orgOk = false;
    }

    // ───────────────────────── đọc trạng thái ─────────────────────────

    void PanasonicAxis::Refresh()
    {
        if (!m_opened) return;
        {
            std::lock_guard<std::mutex> lock(m_sinceReadMutex);
            if (!m_connected && m_sinceReadRunning && m_sinceRead.ElapsedMilliseconds() < RetryMs) return;
            m_sinceRead.Restart();
            m_sinceReadRunning = true;
        }

        std::shared_ptr<servo::ServoAxisState> st;
        try { st = m_servo->GetAxisState(m_unitId); }
        catch (const std::exception& ex) { ReadFailed(ex.what()); return; }

        if (!st) { ReadFailed(std::format("library has no state for UnitId {}", m_unitId)); return; }
        if (!st->IsConnected) { ReadFailed("library Modbus poll is failing"); return; }

        // Số đọc ra chỉ dùng khi vòng quét của thư viện đang lành — nhánh trên đã loại bộ nhớ đệm cũ.
        ReadOk();
        bool alm = st->ALM;
        m_servoOn = st->SON.load();
        m_alarm = alm;
        m_busy = st->BUSY.load();
        m_fls = st->POT.load();
        m_rls = st->NOT.load();
        m_errorCode = (int)st->ErrorCode.load();
        m_pos = m_scale.ToUnit(st->ActualPosition.load());

        // Servo OFF không làm mất gốc (Tuấn bỏ 2026-09-26): driver vẫn đếm encoder khi tắt servo, trục có
        // tụt hay bị đẩy thì vị trí đọc về vẫn đúng. Alarm thì vẫn mất gốc — encoder error, quá tốc…
        if (alm && m_softHomed) m_softHomed = false;

        m_orgOk = m_softHomed.load() && st->ORG_CMP.load();
    }

    void PanasonicAxis::ReadFailed(const std::string& why)
    {
        int streak = ++m_failStreak;
        if (streak < FailLimit) return;
        if (m_connected)
            LogWorker::Instance().RecordLog(
                std::format("{}: connection lost after {} consecutive failed reads ({}) — still retrying every {} ms, "
                            "no app restart needed", m_name, streak, why, RetryMs),
                LogLevel::Warning, m_name);
        m_connected = false;
    }

    void PanasonicAxis::ReadOk()
    {
        if (!m_connected && m_opened)
            LogWorker::Instance().RecordLog(m_name + ": reads OK again, connection back to normal",
                LogLevel::Info, m_name);
        m_failStreak = 0;
        m_connected = true;
    }

    // ───────────────────────── lệnh ─────────────────────────

    bool PanasonicAxis::SetServoOn(bool on)
    {
        if (!IsConnected()) return false;
        try
        {
            // Qua chung m_cmdLock với Jog/Stop/Home/MoveAbs — SON/SOFF cũng là một lệnh ghi
            // Modbus trên cùng cổng, bản C# trước đây bỏ sót khoá này (xem chú thích CmdLock).
            m_cmdLock.Wait();
            {
                ReleaseOnExit<CmdLock> release{ m_cmdLock };
                if (on) m_servo->ServoOn(m_unitId);
                else m_servo->ServoOff(m_unitId);
            }

            if (!on) return true;   // không làm mất gốc — xem Refresh

            // Chờ driver báo sẵn sàng — bật servo là thao tác có thể thất bại (kẹt phanh,
            // thiếu nguồn động lực), nên phải xác nhận chứ không phát lệnh rồi đi tiếp.
            base::Stopwatch sw;
            while (sw.ElapsedMilliseconds() < 5000)
            {
                auto st = m_servo->GetAxisState(m_unitId);
                if (st && st->SON && st->S_RDY) return true;
                if (st && st->ALM) return false;
                base::Delay(20);
            }
            return false;
        }
        catch (const std::exception& ex)
        {
            LogWorker::Instance().RecordLog(std::format("{}: SetServoOn error — {}", m_name, ex.what()),
                LogLevel::Error, m_name);
            return false;
        }
    }

    bool PanasonicAxis::Home(std::stop_token ct)
    {
        if (!IsConnected()) return false;

        m_softHomed = false;
        m_orgOk = false;

        try
        {
            m_cmdLock.Wait(ct);
            ReleaseOnExit<CmdLock> release{ m_cmdLock };
            m_servo->StartHome(m_unitId);
        }
        catch (const std::exception& ex)
        {
            // C# catch (Exception) — gồm cả huỷ lúc đang chờ khoá (OperationCanceledException).
            AlarmManager::RaiseAxisEvent(m_name, AxisFault::HomeTimeout, Loc::F("Axis.HomeSendFail", ex.what()));
            return false;
        }

        base::Stopwatch sw;
        while (sw.ElapsedMilliseconds() < m_cfg->HomeTimeoutMs)
        {
            if (ct.stop_requested()) { Stop(); return false; }

            auto st = m_servo->GetAxisState(m_unitId);
            if (!st) { base::Delay(20, ct); continue; }

            if (st->ALM)
            {
                AlarmManager::RaiseAxisEvent(m_name, AxisFault::HomeTimeout,
                    Loc::F("Axis.HomeDriverAlarm", std::format("{:04X}", st->ErrorCode.load())));
                return false;
            }

            // Xác nhận bằng ORG_CMP, không phải bằng BUSY xuống 0: BUSY tắt cả khi lệnh bị
            // huỷ giữa chừng, và khi đó trục dừng ở một chỗ bất kỳ chứ không phải ở gốc.
            if (st->ORG_CMP && !st->BUSY)
            {
                // Lấy vị trí thẳng từ trạng thái thư viện (đọc ngay sau ORG_CMP), không đợi Refresh —
                // CurrentPos cũ là số trước khi driver đặt lại gốc (26/09: log Z1 −67.069 mm khi trục hiện 0).
                m_pos = m_scale.ToUnit(st->ActualPosition.load());

                m_softHomed = true;
                m_orgOk = true;
                LogWorker::Instance().RecordLog(
                    std::format("{}: origin done in {:.1f} s, pos {:.3f}{}", m_name, sw.ElapsedSeconds(), CurrentPos(), m_cfg->Unit),
                    LogLevel::Info, m_name);
                return true;
            }

            base::Delay(20, ct);   // C#: await Task.Delay(20, ct) — huỷ giữa lúc chờ thì ném OperationCanceled
        }

        Stop();
        AlarmManager::RaiseAxisEvent(m_name, AxisFault::HomeTimeout,
            Loc::F("Axis.HomeTimeoutOrg", m_cfg->HomeTimeoutMs));
        return false;
    }

    bool PanasonicAxis::MoveAbs(double pos, double speed, std::stop_token ct, bool gentle)
    {
        if (!IsConnected()) return false;

        if (!m_cfg->InSoftLimit(pos))
        {
            AlarmManager::RaiseAxisEvent(m_name, AxisFault::SoftLimit,
                Loc::F("Axis.SoftLimitOut", pos, m_cfg->Unit, m_cfg->SoftLimitNeg, m_cfg->SoftLimitPos));
            return false;
        }

        int target = m_scale.ToPulse(pos);
        uint16_t rpm = m_scale.ToRpm(speed);

        // Đọc lại accel/decel từ cấu hình sống ngay trước khi phát lệnh — trang Setting sửa
        // thẳng vào đối tượng AxisConfig này, nên sửa xong là lệnh kế tiếp dùng luôn giá trị
        // mới, không phải khởi động lại app.
        //
        // AccelTimeMs là thời gian ramp ở mức Auto CAO; lệnh chậm hơn dùng cùng gia tốc đó (Tuấn
        // 2026-09-25). Trước đây thư viện nhân lại độ dốc theo tốc độ TỪNG lệnh, nên Z hai chặng
        // (cao/thấp xen kẽ) phải ghi lại acc/dec ở mọi lệnh — mỗi lần ghi là thời gian chết trước
        // khi trục chạy. Độ dốc cố định thì thanh ghi chỉ đổi khi số ở Setting đổi.
        //
        // gentle: ramp theo tốc độ chính lệnh này — Y2 tới điểm bù lúc đặt, cho hàng trên tray lắp ráp khỏi xê
        // dịch (Tuấn 2026-09-30). Chạy chậm mà giữ gia tốc cũ thì cú giật lúc khởi động/dừng vẫn y nguyên.
        //
        // Bản C++ ghi các số này vào m_axisCfg SAU khi đã giữ m_cmdLock (trong SendMove) — thư viện đọc chúng trong
        // MoveAbsolute; cùng giá trị, cùng thời điểm (ngay trước lần gửi đầu) như bản C#.
        uint16_t highRpm = m_scale.ToRpm(m_cfg->MoveSpeed);
        RampUpdate ramp{ ClampRampMs(m_cfg->AccelTimeMs), ClampRampMs(m_cfg->DecelTimeMs),
                         gentle ? std::min(rpm, highRpm) : highRpm };

        ESend send = SendMove(target, rpm, ct, &ramp);
        if (send == ESend::Failed) return false;
        return WaitMotion(pos, target, rpm, send == ESend::Sent, ct);
    }

    PanasonicAxis::ESend PanasonicAxis::SendMove(int target, uint16_t rpm, std::stop_token ct, const RampUpdate* ramp)
    {
        try
        {
            m_cmdLock.Wait(ct);
            ReleaseOnExit<CmdLock> release{ m_cmdLock };
            if (ramp)
            {
                if (auto axisCfg = AxisCfg())
                {
                    axisCfg->AbsoluteAccelMs = ramp->AccelMs;
                    axisCfg->AbsoluteDecelMs = ramp->DecelMs;
                    axisCfg->AbsoluteRampRpm = ramp->RampRpm;
                }
            }
            bool sent = m_servo->MoveAbsolute(m_unitId, target, rpm);
            return sent ? ESend::Sent : ESend::Skipped;
        }
        catch (const base::OperationCanceled&) { throw; }   // STOP/huỷ lúc chờ khoá — không phải lỗi phát lệnh
        catch (const std::exception& ex)
        {
            AlarmManager::RaiseAxisEvent(m_name, AxisFault::MoveTimeout, Loc::F("Axis.SendFail", ex.what()));
            return ESend::Failed;
        }
    }

    int PanasonicAxis::LibrarySkipTolerance(const AxisConfig& cfg)
    {
        return std::max(0, std::min(cfg.SkipMoveTolerancePulse, cfg.PositionTolerancePulse));
    }

    int PanasonicAxis::PollNow() const
    {
        auto st = m_servo->GetAxisState(m_unitId);
        return st ? st->PollCount.load() : 0;
    }

    PanasonicAxis::EMoveWait PanasonicAxis::JudgeMove(bool there, bool busy, int pollsSinceSend, bool movedSinceSend, int resends, bool sent)
    {
        if (there && !busy && (!sent || pollsSinceSend >= 2)) return EMoveWait::Done;
        if (busy || pollsSinceSend < 2 || movedSinceSend) return EMoveWait::Wait;
        return resends < MaxMoveResends ? EMoveWait::Resend : EMoveWait::Lost;
    }

    bool PanasonicAxis::WithinPulses(int actualPulse, int targetPulse, int tolPulse)
    {
        return std::llabs((long long)actualPulse - targetPulse) <= std::max(1, tolPulse);
    }

    bool PanasonicAxis::MoveInc(double delta, double speed, std::stop_token ct)
    {
        return MoveAbs(CurrentPos() + delta, speed, ct);
    }

    uint16_t PanasonicAxis::ClampRampMs(int ms)
    {
        if (ms <= 0) return 250;
        return ms > 10000 ? (uint16_t)10000 : (uint16_t)ms;
    }

    bool PanasonicAxis::WaitMotion(double target, int targetPulse, uint16_t rpm, bool sent, std::stop_token ct)
    {
        base::Stopwatch sw;
        int tol = m_cfg->PositionTolerancePulse;
        int sentPoll = PollNow();
        auto st0 = m_servo->GetAxisState(m_unitId);
        int sentFrom = st0 ? st0->ActualPosition.load() : m_scale.ToPulse(CurrentPos());   // chỗ đứng lúc gửi
        int resends = 0;

        while (sw.ElapsedMilliseconds() < m_cfg->MoveTimeoutMs)
        {
            if (ct.stop_requested()) { Stop(); return false; }

            auto st = m_servo->GetAxisState(m_unitId);
            if (!st) { base::Delay(10, ct); continue; }

            // Đọc số lượt đọc TRƯỚC BUSY/vị trí: đủ hai lượt mới thì BUSY/vị trí đọc sau đó chắc chắn là của sau lệnh.
            // (C# đọc lại st.ActualPosition ở từng chỗ dùng; ở đây đọc một lần — cùng thứ tự, cùng một con số cho cả
            // phép "đã tới", "đã nhúc nhích" và chỗ đứng lúc gửi lại.)
            int polls = (int)((uint32_t)st->PollCount.load() - (uint32_t)sentPoll);   // unchecked
            bool busy = st->BUSY;
            int actual = st->ActualPosition;
            double now = m_scale.ToUnit(actual);

            if (st->ALM)
            {
                AlarmManager::RaiseAxis(m_name, AxisFault::DriverAlarm, std::format("0x{:04X}", st->ErrorCode.load()));
                return false;
            }
            if (st->POT || st->NOT)
            {
                AlarmManager::RaiseAxis(m_name, AxisFault::HardLimit, st->POT ? "POT" : "NOT");
                return false;
            }

            bool there = WithinPulses(actual, targetPulse, tol);
            bool moved = !WithinPulses(actual, sentFrom, tol);
            EMoveWait verdict = JudgeMove(there, busy, polls, moved, resends, sent);

            if (verdict == EMoveWait::Resend)
            {
                resends++;
                LogWorker::Instance().RecordLog(std::format(
                    "{0}: drive did not take move to {1:.3f}{2} (not BUSY, at {3:.3f}{2}) — resend #{4}",
                    m_name, target, m_cfg->Unit, now, resends), LogLevel::Warning, m_name);
                ESend send = SendMove(targetPulse, rpm, ct);
                if (send == ESend::Failed) return false;
                sent = send == ESend::Sent;
                sentPoll = PollNow();
                sentFrom = actual;
                continue;
            }
            if (verdict == EMoveWait::Lost)
            {
                AlarmManager::RaiseAxisEvent(m_name, AxisFault::MoveTimeout, Loc::F(
                    "Axis.MoveLost",
                    resends, target, m_cfg->Unit, now));
                return false;
            }

            if (verdict == EMoveWait::Done)
            {
                // Ghi luôn con số VỪA ĐỌC vào CurrentPos (Tuấn phát hiện 2026-09-19).
                //
                // CurrentPos bình thường do vòng quét Refresh() nuôi, mà vòng quét Modbus đi
                // lần lượt 7 trục nên có thể chậm hơn lệnh chạy này hàng chục ms. Ai đọc
                // encoder NGAY sau khi MoveAbs trả về — VmCycle.AxisGlobals ghi biến cho
                // VisionMaster, Routine_Calib đọc vị trí lúc hút — sẽ lấy phải số của trước
                // khi chạy, trong khi camera đã chụp ở vị trí mới. Sai lệch đó KHÔNG bị phép
                // đối chiếu echo bắt được, vì App gửi số cũ thì Flow cũng trả lại đúng số cũ.
                //
                // Con số này là số tươi nhất có thể có: chính nó vừa được dùng để kết luận
                // "đã tới nơi" ngay ở dòng trên.
                m_pos = now;
                return true;
            }

            base::Delay(10, ct);
        }

        Stop();
        AlarmManager::RaiseAxisEvent(m_name, AxisFault::MoveTimeout,
            Loc::F("Axis.MoveTimeout",
                m_cfg->MoveTimeoutMs, target, m_cfg->Unit, CurrentPos()));
        return false;
    }

    void PanasonicAxis::Jog(int dir, double speed)
    {
        if (!IsConnected()) return;
        uint16_t rpm = m_scale.ToRpm(speed);
        auto self = KeepAlive();
        base::RunDetached([this, self, dir, rpm]
        {
            try
            {
                m_cmdLock.Wait();
                ReleaseOnExit<CmdLock> release{ m_cmdLock };
                if (dir > 0) m_servo->JogPositive(m_unitId, rpm);
                else if (dir < 0) m_servo->JogNegative(m_unitId, rpm);
                else m_servo->DecelerateStop(m_unitId);
            }
            catch (const std::exception& ex)
            {
                LogWorker::Instance().RecordLog(std::format("{}: Jog error — {}", m_name, ex.what()),
                    LogLevel::Error, m_name);
            }
        });
    }

    void PanasonicAxis::Stop()
    {
        if (!IsConnected()) return;
        auto self = KeepAlive();
        base::RunDetached([this, self]
        {
            try
            {
                m_cmdLock.Wait();
                ReleaseOnExit<CmdLock> release{ m_cmdLock };
                m_servo->DecelerateStop(m_unitId);
            }
            catch (...) {}
        });
    }

    bool PanasonicAxis::ResetAlarm()
    {
        if (!IsConnected()) return false;
        try
        {
            // Qua chung m_cmdLock — cùng lý do SetServoOn (xem chú thích CmdLock).
            m_cmdLock.Wait();
            {
                ReleaseOnExit<CmdLock> release{ m_cmdLock };
                m_servo->AlarmClear(m_unitId);
            }

            // Xoá alarm của driver thường kèm mất vị trí — bắt về gốc lại (PLAN.md §4.1).
            m_softHomed = false;
            m_orgOk = false;

            base::Delay(200);
            auto st = m_servo->GetAxisState(m_unitId);
            return st && !st->ALM;
        }
        catch (const std::exception& ex)
        {
            LogWorker::Instance().RecordLog(std::format("{}: ResetAlarm error — {}", m_name, ex.what()),
                LogLevel::Error, m_name);
            return false;
        }
    }
}
