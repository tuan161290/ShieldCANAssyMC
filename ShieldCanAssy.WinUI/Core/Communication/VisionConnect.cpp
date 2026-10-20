#include "Communication/VisionConnect.h"

#include "Base/Async.h"
#include "Base/Text.h"
#include "Infor/AlarmCode.h"
#include "Infor/AlarmManager.h"
#include "Lang/Loc.h"
#include "Utility/Log/LogWorker.h"

#include <chrono>
#include <condition_variable>
#include <exception>
#include <optional>

namespace sca
{
    struct VisionCmdGate
    {
        /// lock (_cmdLock) của bản C# — re-entrant như Monitor.
        std::recursive_mutex cmdLock;
    };

    namespace
    {
        struct FactorySlot
        {
            std::mutex mutex;
            std::function<std::shared_ptr<IVisionDevice>()> factory;
        };

        FactorySlot& Slot()
        {
            static FactorySlot slot;
            return slot;
        }

        void LogError(const std::string& message)
        {
            LogWorker::Instance().RecordLog(message, LogLevel::Error, "Vision");
        }

        std::string What(const std::exception& ex)
        {
            std::string w = ex.what();
            return w.empty() ? "unknown error" : w;
        }

        /// <summary>
        /// <c>VmProcedure.Run()</c> là lệnh CHẶN, không nhận token huỷ — chạy trên luồng nền rồi canh thời gian
        /// (Task.Run + Task.WhenAny của bản C#). Hết giờ (hoặc bị huỷ) thì trả nullopt cho lớp gọi báo alarm 4003,
        /// nhưng luồng chạy <c>Run()</c> bên dưới KHÔNG bị huỷ thật — SDK không cho — nó tự trôi tới khi solution
        /// phản hồi. Cùng một giới hạn với timeout đọc trạng thái driver Panasonic.
        /// </summary>
        template <class T>
        std::optional<T> RunWithTimeout(const std::shared_ptr<VisionCmdGate>& gate, int timeoutMs, std::function<T()> body, const std::stop_token& ct)
        {
            struct State
            {
                std::mutex mutex;
                std::condition_variable_any cv;
                bool done{ false };
                std::optional<T> value;
                std::exception_ptr error;
                std::atomic<int> abandoned{ 0 };
            };
            auto st = std::make_shared<State>();

            base::RunDetached([gate, st, body = std::move(body)]
            {
                std::optional<T> value;
                std::exception_ptr error;
                try
                {
                    std::lock_guard<std::recursive_mutex> lock(gate->cmdLock);
                    // Lần gọi đã bị bỏ (quá giờ/huỷ) trong lúc còn xếp hàng chờ khoá — ví dụ sau một Run treo — thì
                    // không chạy muộn nữa: chạy muộn là ghi biến cũ, và Flow hiệu chuẩn nạp thêm một điểm sai.
                    if (st->abandoned.load() != 1) value = body();
                }
                catch (...) { error = std::current_exception(); }
                {
                    std::lock_guard<std::mutex> lock(st->mutex);
                    st->value = std::move(value);
                    st->error = error;
                    st->done = true;
                }
                st->cv.notify_all();
            });

            std::unique_lock<std::mutex> lock(st->mutex);
            auto finished = [&st] { return st->done; };
            bool done = timeoutMs < 0 ? st->cv.wait(lock, ct, finished)
                                      : st->cv.wait_for(lock, ct, std::chrono::milliseconds(timeoutMs), finished);
            if (done)
            {
                if (st->error) std::rethrow_exception(st->error);
                return std::move(st->value);
            }
            st->abandoned.store(1);
            return std::nullopt;
        }
    }

    VisionConnect::VisionConnect(std::shared_ptr<SystemData> sys)
        : m_sys(sys ? std::move(sys) : std::make_shared<SystemData>()), m_gate(std::make_shared<VisionCmdGate>())
    {
        // Bản C#: McControl dùng FakeVision khi SimulationMode — mô phỏng không bao giờ chạm VisionMaster (trên máy
        // sản xuất, VisionMaster có thể đang phục vụ app WPF).
        if (m_sys->SimulationMode)
        {
            m_fake = std::make_shared<FakeVision>();
            auto s = m_sys;
            m_fake->SetNames([s] { return s->VisionOutputs; });
        }
    }

    void VisionConnect::SetRealDeviceFactory(std::function<std::shared_ptr<IVisionDevice>()> factory)
    {
        std::lock_guard<std::mutex> lock(Slot().mutex);
        Slot().factory = std::move(factory);
    }

    bool VisionConnect::HasRealDeviceFactory()
    {
        std::lock_guard<std::mutex> lock(Slot().mutex);
        return static_cast<bool>(Slot().factory);
    }

    std::shared_ptr<IVisionDevice> VisionConnect::Device() const
    {
        if (m_fake) return m_fake;
        std::lock_guard<std::mutex> lock(m_deviceMutex);
        return m_device;
    }

    std::shared_ptr<IVisionDevice> VisionConnect::EnsureDevice(std::string& why)
    {
        std::lock_guard<std::mutex> lock(m_deviceMutex);
        if (m_device) return m_device;

        std::function<std::shared_ptr<IVisionDevice>()> factory;
        {
            std::lock_guard<std::mutex> slotLock(Slot().mutex);
            factory = Slot().factory;
        }
        if (!factory)
        {
            why = "no real VisionMaster device registered (VisionConnect::SetRealDeviceFactory)";
            return nullptr;
        }
        try { m_device = factory(); }
        catch (const std::exception& ex) { why = "can't create VisionMaster device — " + What(ex); return nullptr; }
        catch (...) { why = "can't create VisionMaster device — unknown error"; return nullptr; }
        if (!m_device) why = "VisionMaster device factory returned null";
        return m_device;
    }

    bool VisionConnect::IsConnected() const { return m_fake ? m_fake->IsConnected() : m_connected.load(); }

    bool VisionConnect::IsSolutionLoaded() const
    {
        auto dev = Device();
        return dev && dev->IsSolutionLoaded();
    }

    bool VisionConnect::Connect()
    {
        if (m_fake)
        {
            bool ok = m_fake->Connect();
            // Như McControl bản C# làm với FakeVision: solution giả có đủ biến của danh mục.
            m_fake->SeedVmSolution(VisionGlobalVarCatalog::Build(m_sys.get()));
            return ok;
        }

        std::string why;
        auto dev = EnsureDevice(why);
        if (!dev)
        {
            // Không có thiết bị thật thì KHÔNG lặng lẽ chạy bản giả: máy thật mà "thấy" bằng số bịa là đặt sai hàng.
            LogError("Vision: no real device — " + why);
            AlarmManager::Raise(AlarmCode::VisionLoadFailed, "Vision", why);
            m_connected = false;
            return false;
        }
        bool ok = false;
        try { ok = dev->Connect(); }
        catch (const std::exception& ex) { LogError("Vision: connect error — " + What(ex)); }
        catch (...) { LogError("Vision: connect error — unknown error"); }
        m_connected = ok;
        return ok;
    }

    void VisionConnect::Disconnect()
    {
        if (m_fake) { m_fake->Disconnect(); return; }
        auto dev = Device();
        try { if (dev) dev->Disconnect(); } catch (...) {}
        m_connected = false;
    }

    bool VisionConnect::LoadSolution(const std::string& solutionPath)
    {
        if (m_fake) return m_fake->LoadSolution(solutionPath);

        std::string why;
        auto dev = EnsureDevice(why);
        if (!dev)
        {
            LogError("Vision: solution load error — " + why);
            AlarmManager::Raise(AlarmCode::VisionLoadFailed, "Vision", why);
            return false;
        }
        try
        {
            std::lock_guard<std::recursive_mutex> lock(m_gate->cmdLock);
            if (dev->LoadSolution(solutionPath)) return true;
            LogError("Vision: solution load error — device refused " + solutionPath);
            return false;
        }
        catch (const std::exception& ex)
        {
            LogError("Vision: solution load error — " + What(ex));
            return false;
        }
        catch (...)
        {
            LogError("Vision: solution load error — unknown error");
            return false;
        }
    }

    std::string VisionConnect::FlowNameOf(const VisionOutputNames& names, EVisionProcedure procedure)
    {
        switch (procedure)
        {
        case EVisionProcedure::AlignCalibTool1: return names.FlowAlignBottomTool1;
        case EVisionProcedure::AlignCalibTool2: return names.FlowAlignBottomTool2;
        default: return names.FlowLocateTop;
        }
    }

    std::map<std::string, double> VisionConnect::AxisVars(const VisionOutputNames& names, const AxisSnapshot& axes)
    {
        std::map<std::string, double> vars;
        if (!names.GvAxisX.empty()) vars[names.GvAxisX] = axes.X;
        if (!names.GvAxisY2.empty()) vars[names.GvAxisY2] = axes.Y2;
        if (!names.GvAxisA1.empty()) vars[names.GvAxisA1] = axes.A1;
        if (!names.GvAxisA2.empty()) vars[names.GvAxisA2] = axes.A2;
        return vars;
    }

    std::map<std::string, double> VisionConnect::CalibVars(const VisionOutputNames& names, const AxisSnapshot& axes)
    {
        auto vars = AxisVars(names, axes);
        // Flow soi và nạp điểm hiệu chuẩn là một, rẽ nhánh theo cờ này (Tuấn 2026-10-08) — lần soi ghi 1 ở
        // VmCycle.LocateRequest / AlignBottomRequest.
        vars[VmVars::IsTriggerLocate] = 0;
        return vars;
    }

    bool VisionConnect::IsBottomUp(EVisionProcedure procedure)
    {
        return procedure == EVisionProcedure::AlignCalibTool1 || procedure == EVisionProcedure::AlignCalibTool2;
    }

    int VisionConnect::BottomUpToolOf(const VisionOutputNames& names, const std::string& flow)
    {
        return flow == names.FlowAlignBottomTool2 ? 2 : 1;
    }

    VisionPose VisionConnect::TriggerCalib(EVisionProcedure procedure, const AxisSnapshot& axes, std::stop_token ct)
    {
        if (m_fake) return m_fake->TriggerCalib(procedure, axes, ct);

        // (axes == null → "Vm.NoCalibAxes" của bản C# không có ở đây: AxisSnapshot là giá trị.)
        if (!IsConnected()) return VisionPose::NotFound(Loc::T("Vm.NotConnected"));
        if (!IsSolutionLoaded()) return VisionPose::NotFound(Loc::T("Vm.NoSolution"));

        auto dev = Device();
        std::string flow = FlowNameOf(m_sys->VisionOutputs, procedure);

        try
        {
            // Ghi trước, chạy sau, trong cùng một lượt giữ khoá lệnh của thiết bị (thiết bị thô ghi CalibVars rồi Run).
            auto result = RunWithTimeout<VisionPose>(m_gate, m_sys->VisionTriggerTimeoutMs,
                [dev, procedure, axes, ct] { return dev->TriggerCalib(procedure, axes, ct); }, ct);

            if (!result)
            {
                if (ct.stop_requested()) return VisionPose::NotFound(Loc::T("Vm.Cancelled"));
                AlarmManager::RaiseEvent(AlarmCode::VisionTriggerTimeout, "Vision", flow);
                return VisionPose::NotFound(Loc::T("Vm.Timeout"));
            }
            return *result;
        }
        catch (const base::OperationCanceled&) { return VisionPose::NotFound(Loc::T("Vm.Cancelled")); }
        catch (const std::exception& ex)
        {
            LogError("Vision: " + flow + " error — " + What(ex));
            return VisionPose::NotFound(What(ex));
        }
    }

    std::string VisionConnect::ClearCalibPoints(EVisionProcedure procedure)
    {
        if (m_fake) return m_fake->ClearCalibPoints(procedure);

        if (!IsConnected()) return Loc::T("Vm.NotConnected");
        if (!IsSolutionLoaded()) return Loc::T("Vm.NoSolution");

        auto dev = Device();
        std::string flow = FlowNameOf(m_sys->VisionOutputs, procedure);
        try
        {
            std::lock_guard<std::recursive_mutex> lock(m_gate->cmdLock);
            std::string err = dev->ClearCalibPoints(procedure);
            if (err.empty())
                LogWorker::Instance().RecordLog("Vision: " + flow + " — cleared old calib pts of module " +
                                                base::Join(dev->LastClearedCalibModules(), ", "), LogLevel::Info, "Vision");
            return err;
        }
        // Bản C# không ghi log ở nhánh lỗi — lý do đi thẳng về chuỗi nạp điểm, nó dừng và báo.
        catch (const std::exception& ex) { return What(ex); }
        catch (...) { return "unknown error"; }
    }

    std::vector<std::string> VisionConnect::LastClearedCalibModules() const
    {
        auto dev = Device();
        return dev ? dev->LastClearedCalibModules() : std::vector<std::string>{};
    }

    GlobalVarSyncReport VisionConnect::SyncGlobalVariables(const std::vector<GlobalVarSpec>& specs, bool apply)
    {
        if (m_fake) return m_fake->SyncGlobalVariables(specs, apply);

        if (!IsSolutionLoaded()) return GlobalVarSyncReport::Fail(Loc::T("Vm.NoVisionSolution"));
        auto dev = Device();
        try
        {
            // Không canh giờ như Run(): lưu solution có thể mất vài giây, và bỏ dở giữa chừng tệ
            // hơn chờ. Vẫn giữ khoá lệnh để không lọt lần kích Flow nào vào giữa.
            std::lock_guard<std::recursive_mutex> lock(m_gate->cmdLock);
            return dev->SyncGlobalVariables(specs, apply);
        }
        catch (const std::exception& ex) { return GlobalVarSyncReport::Fail(What(ex)); }
        catch (...) { return GlobalVarSyncReport::Fail("unknown error"); }
    }

    VmFlowOutputs VisionConnect::ReadFlowOutputs(const std::string& flow)
    {
        if (m_fake) return m_fake->ReadFlowOutputs(flow);

        if (flow.empty()) return VmFlowOutputs::Fail(flow, Loc::T("Vm.FlowNameNotSet"));
        if (!IsConnected()) return VmFlowOutputs::Fail(flow, Loc::T("Vm.NotConnected"));
        if (!IsSolutionLoaded()) return VmFlowOutputs::Fail(flow, Loc::T("Vm.NoSolution"));

        auto dev = Device();
        try
        {
            std::lock_guard<std::recursive_mutex> lock(m_gate->cmdLock);
            return dev->ReadFlowOutputs(flow);
        }
        catch (const std::exception& ex) { return VmFlowOutputs::Fail(flow, What(ex)); }
        catch (...) { return VmFlowOutputs::Fail(flow, "unknown error"); }
    }

    VmCallResult VisionConnect::CallFlow(const VmCallRequest& request, std::stop_token ct)
    {
        if (m_fake) return m_fake->CallFlow(request, ct);

        if (request.Flow.empty()) return VmCallResult::Fail(Loc::T("Vm.FlowNameMissing"));
        if (!IsConnected()) return VmCallResult::Fail(Loc::T("Vm.NotConnected"));
        if (!IsSolutionLoaded()) return VmCallResult::Fail(Loc::T("Vm.NoSolution"));

        auto dev = Device();
        try
        {
            // Ghi biến, chạy, đọc ngay — cùng một lượt giữ khoá lệnh của thiết bị.
            auto r = RunWithTimeout<VmCallResult>(m_gate, m_sys->VisionTriggerTimeoutMs,
                [dev, request, ct] { return dev->CallFlow(request, ct); }, ct);

            if (!r)
            {
                // Huỷ (EMG, rèm, STOP huỷ chu trình) giữa lúc Flow chạy không phải quá giờ — không alarm. Quá giờ thật
                // thì phát dạng SỰ KIỆN cho RESET gỡ được: Raise trơn chỉ được gỡ bởi một lần soi Auto thành công, mà
                // còn alarm Error thì Auto không chạy được — 13/09 máy kẹt LỖI, RESET 13 lần, phải tắt App.
                if (ct.stop_requested()) return VmCallResult::Fail(Loc::F("Vm.CancelledInFlow", request.Flow));
                AlarmManager::RaiseEvent(AlarmCode::VisionTriggerTimeout, "Vision", request.Flow);
                return VmCallResult::Fail(Loc::F("Vm.FlowTimeout", request.Flow), true);
            }
            return *r;
        }
        catch (const base::OperationCanceled&) { return VmCallResult::Fail(Loc::F("Vm.CancelledInFlow", request.Flow)); }
        catch (const std::exception& ex)
        {
            LogError("Vision: " + request.Flow + " error — " + What(ex));
            return VmCallResult::Fail(request.Flow + ": " + What(ex));
        }
    }

    std::string VisionConnect::SaveSolution()
    {
        if (m_fake) return m_fake->SaveSolution();

        if (!IsSolutionLoaded()) return Loc::T("Vm.NoSolution");
        auto dev = Device();
        try
        {
            std::lock_guard<std::recursive_mutex> lock(m_gate->cmdLock);
            return dev->SaveSolution();
        }
        catch (const std::exception& ex) { return What(ex); }
        catch (...) { return "unknown error"; }
    }

    std::shared_ptr<const ImageData> VisionConnect::LastTopDownImage() const
    {
        auto dev = Device();
        return dev ? dev->LastTopDownImage() : nullptr;
    }

    std::shared_ptr<const ImageData> VisionConnect::LastBottomUpImageTool1() const
    {
        auto dev = Device();
        return dev ? dev->LastBottomUpImageTool1() : nullptr;
    }

    std::shared_ptr<const ImageData> VisionConnect::LastBottomUpImageTool2() const
    {
        auto dev = Device();
        return dev ? dev->LastBottomUpImageTool2() : nullptr;
    }
}
