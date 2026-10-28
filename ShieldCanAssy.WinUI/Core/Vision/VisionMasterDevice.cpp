#include "Vision/VisionMasterDevice.h"

#include "Base/Async.h"
#include "Base/Text.h"
#include "Communication/VisionConnect.h"
#include "Lang/Loc.h"
#include "Utility/Log/LogWorker.h"
#include "Vision/VmImageConvert.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <format>
#include <map>
#include <mutex>
#include <set>

#include <windows.h>

// Header của SDK dùng bảng mã tiếng Trung (C4819) và tự `using namespace std` ở phạm vi toàn cục — chỉ được
// nằm trong file .cpp này, không bao giờ lọt ra header công khai.
#pragma warning(push)
#pragma warning(disable: 4819 4996 4251 4275)
#include "IVmSolution.h"
#include "IVmProcedure.h"
#include "IVmGlobalVariable.h"
#pragma warning(push)
#pragma warning(disable : 4091)   // MVD_AlgoParamDefine.h của SDK: typedef enum không đặt tên biến
#include "IVmNPointCalib.h"
#include "IVmTranslationCalib.h"
#pragma warning(pop)
#include "VMException.h"
#include "iMVS-6000PlatformSDKC.h"
#pragma warning(pop)

// LƯU Ý BUILD: mọi hàm xuất của SDK khai `extern "C"` nhưng vẫn ném CVmException. Phải biên dịch với /EHs
// (không phải /EHsc), nếu không trình biên dịch bỏ luôn các khối catch dưới đây và tiến trình chết với
// 0xC0000409 (bàn giao VM-WinUI3 §7).

namespace sca
{
    namespace vm = VisionMasterSDK;

    namespace
    {
        using Solution = vm::VmSolution::IVmSolution;
        using Procedure = vm::VmProcedure::IVmProcedure;
        using ProcResults = vm::VmProcedure::IMVSProcedureResults;
        using GlobalTool = vm::GlobalVariableModule::GlobalVariableModuleTool;

        constexpr int CodeProcedureNotFound = static_cast<int>(0xE0000316);   // IMVS_EC_MODULE_PROCEDURE_NOT_FOUND
        constexpr int CodeModuleNotFound = static_cast<int>(0xE0000302);      // IMVS_EC_MODULE_NOT_FOUND
        constexpr int CodeInvalidHandle = static_cast<int>(0xE0000006);       // IMVS_EC_INVALID_HANDLE

        /// Tên lớp module của SDK (strModuleName trong ModuleInfoList) — MODU_NAME_NPOINTCALIBMODU,
        /// MODU_NAME_TRANSLATIONCALIBMODU, MODU_NAME_GROUPMODULE của iMVS-6000PlatformSDKDefine.h. Dò loại module theo
        /// tên lớp, KHÔNG theo tên hiển thị người dựng solution đặt (bản .NET dò bằng kiểu .NET: `as IMVSNPointCalibModuTool`).
        constexpr const char* ModuNPointCalib = "IMVSNPointCalibModu";
        constexpr const char* ModuTranslationCalib = "TranslationCalibModu";
        /// Kiểu của module Group trong VisionMaster — <c>GetAllModuleList</c> trả về nó xen giữa danh sách, rồi mới
        /// tới các module nằm trong nó.
        constexpr const char* GroupModuleType = "IMVSGroup";

        /// Chờ callback "Flow xong" tối đa bấy nhiêu sau khi Run() (lệnh chặn) đã trả về. Đây KHÔNG phải canh
        /// giờ trigger (việc của VisionConnect) — chỉ là chặn trên cho bước "đợi SDK đẩy xong kết quả".
        constexpr auto RunEndGrace = std::chrono::milliseconds(1000);

        /// Hai hàm cho UI chỉ thử khoá SDK chừng này rồi bỏ.
        constexpr auto UiLockWait = std::chrono::milliseconds(30);

        /// DisposeResource() đã chạy — SDK không dùng lại được trong tiến trình này nữa.
        std::atomic<bool> g_sdkDisposed{ false };

        std::string Hex(int code) { return std::format("0x{:08X}", static_cast<unsigned>(code)); }

        std::string Describe(const std::string& what, const vm::CVmException& e)
        {
            const char* d = e.GetDescription();
            std::string text = (d && *d) ? d : "VisionMaster SDK error";
            return what + ": " + text + " (" + Hex(e.GetErrorCode()) + ")";
        }

        /// Bọc MỘT lời gọi SDK: CVmException → VisionDeviceError kèm mã lỗi; exception lạ → VisionDeviceError.
        template <class F>
        auto Sdk(const std::string& what, F&& f) -> decltype(f())
        {
            try { return f(); }
            catch (const vm::CVmException& e) { throw VisionDeviceError(Describe(what, e), e.GetErrorCode()); }
            catch (const std::exception&) { throw; }
            catch (...) { throw VisionDeviceError(what + ": unknown VisionMaster SDK exception", -2); }
        }

        template <std::size_t N>
        std::string Str(const char (&a)[N]) { return std::string(a, strnlen(a, N)); }

        std::string StripPercent(const std::string& name)
        {
            return name.size() > 1 && name.front() == '%' && name.back() == '%' ? name.substr(1, name.size() - 2) : name;
        }

        bool EnsureSdkDll()
        {
            static const bool ok = GetModuleHandleW(L"iMVS-6000PlatformSDK.dll") != nullptr
                                   || LoadLibraryW(L"iMVS-6000PlatformSDK.dll") != nullptr;
            return ok;
        }

        void RequireSdkDll()
        {
            if (!EnsureSdkDll())
                throw VisionDeviceError("can't load iMVS-6000PlatformSDK.dll — VisionMaster 4.4 not installed "
                                        "or PATH lacks Applications\\PublicFile\\x64");
        }

        /// VmIoType của SDK C++ (IVmDynamicIoDefine.h) → mã IMVS_MODULE_BASE_DATA_TYPE mà bản .NET trả và
        /// VmOutCheck hiểu (13 = int, 14 = float, 15 = chuỗi, 1 = ảnh, 0 = không xác định). Enum .NET đó
        /// không có trong header C++ nên phải đổi tay (TypeCodeOf của bản C#).
        int TypeCodeOf(vm::VmIoType t)
        {
            switch (t)
            {
            case vm::IO_TYPE_INT: return VmOutCheck::TypeInt;
            case vm::IO_TYPE_FLOAT: return VmOutCheck::TypeFloat;
            case vm::IO_TYPE_STRING: return 15;
            case vm::IO_TYPE_IMAGE: return 1;
            default: return 0;
            }
        }

        struct RunSignal
        {
            std::mutex mutex;
            std::condition_variable_any cv;
            bool done{ false };
            bool filter{ false };
            unsigned processId{ 0 };
            bool everSeen{ false };
        };

        /// Callback của SDK — đến từ luồng của SDK. Chỉ đặt cờ, không gọi lại SDK, không để lọt exception.
        /// Khuôn lấy từ Samples\VC\...\GetResultControl\GetResultControlDlg.cpp (CallBackModuRes) và
        /// VmWinUiProbe\VmSdk.cpp: nInfoType = WORK_STATE, nWorkStatus = 0 nghĩa là Flow đã chạy xong.
        int __stdcall OnSdkCallback(OUT ::OutputPlatformInfo* const info, IN void* const user)
        {
            try
            {
                auto* sig = static_cast<RunSignal*>(user);
                if (!sig || !info || !info->pData) return IMVS_EC_OK;
                if (info->nInfoType != IMVS_ENUM_CTRLC_OUTPUT_PLATFORM_INFO_WORK_STATE) return IMVS_EC_OK;
                auto* ws = static_cast<IMVS_PF_MODULE_WORK_STAUS*>(info->pData);
                if (ws->nWorkStatus != 0) return IMVS_EC_OK;
                {
                    std::lock_guard<std::mutex> lock(sig->mutex);
                    sig->everSeen = true;
                    if (!sig->filter || ws->nProcessID == sig->processId) sig->done = true;
                }
                sig->cv.notify_all();
            }
            catch (...) {}
            return IMVS_EC_OK;
        }

        /// <c>CVmToolBase::EnableResultCallback()</c> (VmToolBase.h — hàm DUY NHẤT của SDK C++ bật lại callback kết quả
        /// cho một module sau <c>DisableModulesCallback</c>; DLL không xuất hàm C nào tương đương). <paramref name="tool"/>
        /// là void* lấy từ <c>IVmSolution::operator[]("Flow.Module")</c>: mẫu GetResultControlDlg.cpp ép thẳng con trỏ đó
        /// sang lớp công cụ có kiểu (<c>ImageSourceModuleTool*</c>, <c>IMVSCircleFindModuTool*</c>), và mọi lớp công cụ của
        /// module trong Flow (168 lớp trong Includes, kể cả IMVSGroupTool) thừa kế ĐƠN <c>CVmToolBase</c>, nên ép sang
        /// lớp gốc là cùng địa chỉ. Các lớp KHÔNG thừa kế nó (DataQueue, GlobalCamera, CommManager, LightControl,
        /// GlobalVariable) là module toàn cục, không nằm trong Flow. Phòng xa module lạ (module tự viết) trả đối tượng
        /// khác kiểu: vi phạm truy cập được chặn bằng SEH ở đây (hàm riêng — __try không ở chung hàm có đối tượng cần
        /// huỷ); CVmException vẫn lọt ra cho Sdk() đổi thành VisionDeviceError. Trả false khi bị chặn.
        bool CallEnableResultCallback(void* tool)
        {
            __try
            {
                static_cast<vm::CVmToolBase*>(tool)->EnableResultCallback();
                return true;
            }
            __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
            {
                return false;
            }
        }

        int ProbeGlobalListCpp(vm::GlobalVariableModule::GlobalVariableModuleTool* tool, int& count)
        {
            try
            {
                auto* all = tool->GetAllGlobalVar();
                if (!all) return 0;
                count = all->nCount;
                return 1;
            }
            catch (...) { return 0; }
        }

        /// Đọc thử (CHỈ ĐỌC) danh sách biến của một ứng viên module Global Variable: 1 = đọc được, count = số biến;
        /// 0 = SDK báo lỗi; -1 = vi phạm truy cập (con trỏ không phải module Global Variable). SEH như
        /// CallEnableResultCallback.
        int ProbeGlobalList(vm::GlobalVariableModule::GlobalVariableModuleTool* tool, int& count)
        {
            __try
            {
                return ProbeGlobalListCpp(tool, count);
            }
            __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
            {
                return -1;
            }
        }

        /// Đọc một output số, chấp nhận CẢ HAI kiểu mà VisionMaster dùng: thử GetOutputFloat trước, không có thì
        /// GetOutputInt.
        ///
        /// Vì sao cần cả hai: nhiều kết quả tự nhiên của VisionMaster là Int — Module Status (0 = NG, 1 = OK), Match Num,
        /// CalibPointNum — và giao diện khai output của Flow lọc danh sách Subscribe theo kiểu: để ô Type là float thì
        /// những nguồn Int đó biến mất khỏi danh sách, không bind được. Trước đây App chỉ gọi GetOutputFloat, nên cách
        /// duy nhất là chèn thêm một module Variable Calculation chuyển Int→float vào TỪNG Flow — thừa việc, và quên một
        /// chỗ là hỏng theo kiểu khó đoán nhất: output sai kiểu làm GetOutputFloat trả rỗng, giống hệt như output không
        /// tồn tại, nên App báo "không tìm thấy" trong khi VisionMaster khớp mẫu hoàn toàn bình thường. Tuấn dính đúng
        /// lỗi này 2026-09-12 khi khai Found ← Module Status (kiểu Int).
        ///
        /// Chỉ lấy phần tử [0] của mỗi output, nên module định vị trong solution phải đặt "Max Number to Find = 1";
        /// nhiều kết quả thì App lặng lẽ dùng cái đầu tiên.
        /// SDK: IMVSProcedureResults::GetOutputFloat / GetOutputInt (IVmProcedure.h) — chưa chạy được ở máy dev.
        bool TryReadNumber(ProcResults* res, const std::string& name, float& value)
        {
            value = 0.0f;
            if (!res) return false;
            try
            {
                vm::FloatDataArray f = res->GetOutputFloat(name.c_str());
                if (f.nValueNum >= 1 && f.pFloatVal != nullptr) { value = f.pFloatVal[0]; return true; }
            }
            catch (...) { /* output không phải kiểu float — thử kiểu int bên dưới */ }
            try
            {
                vm::IntDataArray n = res->GetOutputInt(name.c_str());
                if (n.nValueNum >= 1 && n.pIntVal != nullptr) { value = static_cast<float>(n.pIntVal[0]); return true; }
            }
            catch (...) { /* không đọc được kiểu nào — coi như thiếu output */ }
            return false;
        }

        /// Ảnh chỉ để hiển thị, không phải số dùng để tính lượng bù — lỗi thì im lặng trả nullptr.
        /// SDK: IMVSProcedureResults::GetOutputImageV2 → IoImage.stImage (mẫu GetResultControlDlg.cpp dòng
        /// "GetOutputImageV2("ImageData")") — chưa chạy được ở máy dev.
        std::shared_ptr<const ImageData> TryReadImage(ProcResults* res, const std::string& name)
        {
            if (!res) return nullptr;
            try
            {
                vm::IoImage io = res->GetOutputImageV2(name.c_str());
                const vm::ImageBaseData& b = io.stImage;
                return VmImageToImageData(b.ImageData, b.DataLen, b.Width, b.Height, static_cast<unsigned>(b.Pixelformat));
            }
            catch (...) { return nullptr; }
        }

        /// float.Parse / int.Parse(InvariantCulture) — sai định dạng thì ném (như bản C#).
        template <class T>
        T ParseInvariant(const std::string& text, const char* kind)
        {
            T v{};
            const char* b = text.data();
            const char* e = b + text.size();
            while (b < e && (*b == ' ' || *b == '+')) ++b;
            while (e > b && e[-1] == ' ') --e;
            auto r = std::from_chars(b, e, v);
            if (r.ec != std::errc() || r.ptr != e)
                throw std::invalid_argument("value \"" + text + "\" is not a valid " + kind + " number");
            return v;
        }
    }

    struct VisionMasterDevice::Impl
    {
        std::function<std::shared_ptr<SystemData>()> sys;

        /// Xếp hàng mọi lần chạm SDK (xem chú thích luồng ở header). Re-entrant như lock của C#.
        std::recursive_timed_mutex sdk;
        Solution* sol{ nullptr };
        std::atomic<bool> connected{ false };
        std::atomic<bool> loaded{ false };
        std::shared_ptr<RunSignal> signal = std::make_shared<RunSignal>();
        int runsWithoutCallback{ 0 };
        /// Giữ đối tượng tham số của module đang hiển thị sống tới lần RenderSource kế tiếp.
        std::unique_ptr<vm::CModuParamsBase> renderParams;
        /// Module Global Variable đã dò (GetGlobalVarModule) cho lượt nạp solution gvGen; loadGen tăng mỗi LoadSolution.
        GlobalTool* gvTool{ nullptr };
        unsigned gvGen{ 0 };
        unsigned loadGen{ 0 };

        mutable std::mutex cacheMutex;
        /// Đường dẫn tuyệt đối (UTF-8) của file .solw đang nạp — rỗng khi chưa nạp. (cacheMutex)
        std::string solutionFile;
        std::vector<std::string> flowNames;
        std::shared_ptr<const ImageData> top, bottom1, bottom2;
        std::vector<std::string> lastCleared;

        VisionOutputNames Names() const
        {
            std::shared_ptr<SystemData> s;
            try { if (sys) s = sys(); } catch (...) {}
            return s ? s->VisionOutputs : VisionOutputNames{};
        }

        void SetTop(std::shared_ptr<const ImageData> img) { std::lock_guard<std::mutex> l(cacheMutex); top = std::move(img); }

        /// Cất ảnh CAM bottom-up vào đúng tay: Flow của tay #2 thì tay #2, còn lại tay #1 (SetBottomUpImage của bản C#).
        void SetBottomUp(const VisionOutputNames& names, const std::string& flow, std::shared_ptr<const ImageData> img)
        {
            std::lock_guard<std::mutex> l(cacheMutex);
            if (VisionConnect::BottomUpToolOf(names, flow) == 2) bottom2 = std::move(img);
            else bottom1 = std::move(img);
        }

        // ───────────────────────── Flow ─────────────────────────

        /// Tên → (có trong solution, ID). SDK: IVmSolution::GetAllProcedureList (IVmSolution.h). Trả false khi
        /// SDK không đưa được danh sách.
        bool ListFlows(std::vector<std::pair<std::string, unsigned>>& out)
        {
            out.clear();
            try
            {
                ::ProcessInfoList* list = sol->GetAllProcedureList();
                if (!list) return false;
                for (unsigned i = 0; i < list->nNum && i < std::size(list->astProcessInfo); ++i)
                    out.emplace_back(Str(list->astProcessInfo[i].strProcessName), list->astProcessInfo[i].nProcessID);
                return true;
            }
            catch (...) { return false; }
        }

        void RefreshFlowNames()
        {
            std::vector<std::pair<std::string, unsigned>> flows;
            if (!ListFlows(flows)) return;
            std::vector<std::string> names;
            for (auto& f : flows) names.push_back(f.first);
            std::lock_guard<std::mutex> l(cacheMutex);
            flowNames = std::move(names);
        }

        /// Lấy Flow theo tên (GetProcedure của bản C#: không có thì ném "Vm.DevFlowNotFound"). operator[] của SDK trả
        /// void* cho CẢ Flow lẫn module ("Flow.Module") nên phải đối chiếu với danh sách Flow trước khi ép kiểu — bản
        /// .NET có `as VmProcedure` làm việc đó.
        /// SDK: IVmSolution::operator[] (mẫu GetResultControlDlg.cpp: (IVmProcedure*)(*m_pVmSol)["Flow1"]).
        Procedure* GetProcedure(const std::string& flow, bool& hasId, unsigned& id)
        {
            auto missing = [&flow](int code = -1) { return VisionDeviceError(Loc::F("Vm.DevFlowNotFound", flow), code); };
            hasId = false;
            id = 0;
            if (flow.empty()) throw missing();

            std::vector<std::pair<std::string, unsigned>> flows;
            if (ListFlows(flows))
            {
                for (auto& f : flows)
                    if (f.first == flow) { hasId = true; id = f.second; break; }
                if (!hasId) throw missing();
            }
            else if (flow.find('.') != std::string::npos) throw missing();   // không kiểm được thì không ép kiểu một đường dẫn module

            void* p = nullptr;
            try { p = (*sol)[flow.c_str()]; }
            catch (const vm::CVmException& e)
            {
                if (e.GetErrorCode() == CodeProcedureNotFound || e.GetErrorCode() == CodeModuleNotFound) throw missing(e.GetErrorCode());
                throw VisionDeviceError(Describe("find Flow \"" + flow + "\"", e), e.GetErrorCode());
            }
            catch (...) { throw VisionDeviceError("find Flow \"" + flow + "\": unknown VisionMaster SDK exception", -2); }
            if (!p) throw missing();
            return static_cast<Procedure*>(p);
        }

        /// Chạy Flow MỘT lần (chạy hai lần — một để lấy số, một để lấy ảnh — là kích camera thật chụp hai lần cho một
        /// lần trigger logic, tốn thời gian và có thể đọc phải hai khung hình khác nhau nếu vật vừa dịch) rồi trả đối
        /// tượng kết quả của đúng lần chạy đó.
        ///
        /// <c>EnableResultCallback()</c> khi cần ảnh — không gọi thì GetOutputImageV2 không có dữ liệu, dù
        /// output số vẫn đọc được (mẫu GetResultControl). <c>Run()</c> mặc định bIsWait = true nên CHẶN tới
        /// khi Flow xong (IVmProcedure.h) — như VmProcedure.Run() bản .NET; VisionConnect canh giờ bên ngoài.
        /// Mẫu VC của hãng còn chờ callback WORK_STATE rồi mới đọc kết quả, nên sau Run() ta chờ thêm callback
        /// đó theo kiểu sự kiện, nghe token huỷ, tối đa RunEndGrace; nếu một lần chạy trước đã hết khoảng chờ
        /// mà callback chưa từng tới (cấu hình SDK không phát) thì thôi không chờ nữa.
        ProcResults* RunFlow(Procedure* proc, bool hasId, unsigned id, bool wantImage, const std::stop_token& ct)
        {
            base::ThrowIfCancelled(ct);
            if (wantImage) Sdk("enable Flow result callback", [&] { proc->EnableResultCallback(); });

            {
                std::lock_guard<std::mutex> lock(signal->mutex);
                signal->done = false;
                signal->filter = hasId;
                signal->processId = id;
            }

            Sdk("run Flow", [&] { proc->Run(); });

            {
                std::unique_lock<std::mutex> lock(signal->mutex);
                bool skip = !signal->everSeen && runsWithoutCallback > 0;
                if (!signal->done && !skip)
                    signal->cv.wait_for(lock, ct, RunEndGrace, [this] { return signal->done; });
                if (!signal->done) ++runsWithoutCallback;
            }
            base::ThrowIfCancelled(ct);

            return Sdk("get Flow result", [&] { return proc->GetResult(); });
        }

        // ───────────────────────── biến toàn cục của solution ─────────────────────────

        static VisionDeviceError NoGlobalModule(int code, const std::string& hint)
        {
            return VisionDeviceError(hint.empty() ? Loc::T("Vm.DevNoGvModule") : Loc::F("Vm.DevNoGvModuleNamed", hint), code);
        }

        /// <summary>
        /// Module Global Variable của solution đang nạp — dò như GetGlobalVarModule của bản C#:
        /// 1. tên khai ở Setting (<c>VisionOutputs.GlobalVarModule</c>) qua operator[];
        /// 2. module có ID 13000 (IMVS_PF_MODULE_GLOBAL_VAR) trong IVmSolution::GetAllModuleList, lấy theo tên hiển thị —
        ///    tên này đổi theo ngôn ngữ cài VisionMaster nên không hardcode được;
        /// 3. GetGlobalVariableToolInstance().
        /// Trước 2026-10-18 chỉ có bước 3: trên máy lắp ráp (solution sản xuất) con trỏ nó trả về khác null nhưng ghi
        /// biến báo IMVS_EC_NULL_PTR ("write global variable A1_deg … 0xE000000A") trong khi bản WPF ghi bình thường.
        /// Mỗi ứng viên được ĐỌC THỬ danh sách biến (ProbeGlobalList, có SEH) — lấy ứng viên đầu tiên đọc được; không
        /// ứng viên nào đọc được thì dùng ứng viên đầu tiên khác null (như trước). Kết quả dò ghi log một lần mỗi lượt
        /// nạp solution.
        /// </summary>
        GlobalTool* GetGlobalVarModule(const std::string& hint)
        {
            if (gvTool && gvGen == loadGen) return gvTool;

            struct Candidate { std::string via; GlobalTool* tool; };
            std::vector<Candidate> candidates;
            auto byName = [&](const std::string& name) -> GlobalTool* {
                if (!sol || name.empty()) return nullptr;
                void* p = nullptr;
                try { p = (*sol)[name.c_str()]; } catch (...) { p = nullptr; }
                return static_cast<GlobalTool*>(p);
            };

            if (!hint.empty())
                if (GlobalTool* t = byName(hint)) candidates.push_back({ "Setting name '" + hint + "'", t });

            std::vector<std::string> gvModules;
            if (sol)
            {
                ::ModuleInfoList* list = nullptr;
                try { list = sol->GetAllModuleList(); } catch (...) { list = nullptr; }
                for (unsigned i = 0; list != nullptr && i < list->nNum && i < std::size(list->astModuleInfo); ++i)
                {
                    if (list->astModuleInfo[i].nModuleID != IMVS_PF_MODULE_GLOBAL_VAR) continue;
                    std::string display = Str(list->astModuleInfo[i].strDisplayName);
                    gvModules.push_back(display + " [" + Str(list->astModuleInfo[i].strModuleName) + "]");
                    if (GlobalTool* t = byName(display)) candidates.push_back({ "module list '" + display + "' (ID 13000)", t });
                }
            }

            GlobalTool* instance = nullptr;
            try { instance = ::GetGlobalVariableToolInstance(); } catch (...) { instance = nullptr; }
            if (instance) candidates.push_back({ "GetGlobalVariableToolInstance", instance });

            std::vector<std::string> report;
            GlobalTool* chosen = nullptr;
            std::string chosenVia;
            for (const Candidate& c : candidates)
            {
                int count = -1;
                int probe = ProbeGlobalList(c.tool, count);
                report.push_back(c.via + (probe == 1 ? ": list OK, " + std::to_string(count) + " vars"
                                          : probe == 0 ? ": list read failed" : ": access violation"));
                if (!chosen && probe == 1) { chosen = c.tool; chosenVia = c.via; }
            }
            if (!chosen && !candidates.empty()) { chosen = candidates.front().tool; chosenVia = candidates.front().via + " (unverified)"; }

            LogWorker::Instance().RecordLog(
                "VisionMaster Global Variable module: " + (chosen ? "using " + chosenVia : std::string("none found")) +
                    " | ID-13000 modules: " + (gvModules.empty() ? std::string("none") : base::Join(gvModules, ", ")) +
                    " | probes: " + (report.empty() ? std::string("no candidate") : base::Join(report, "; ")),
                chosen ? LogLevel::Info : LogLevel::Warning, "Vision");

            if (!chosen) throw NoGlobalModule(-1, hint);
            gvTool = chosen;
            gvGen = loadGen;
            return gvTool;
        }

        /// Tên → kiểu (float/int/string) của mọi biến toàn cục đang khai, tên NGUYÊN như SDK trả. Không đọc được thì
        /// NÉM LỖI — dùng để quyết định tạo biến nên không được coi "không đọc được" là "chưa có biến nào"
        /// (GetGlobalVarTypes của bản C#).
        /// SDK: GlobalVariableModuleTool::GetAllGlobalVar → GlobalVarInfoList (IVmGlobalVariable.h).
        std::map<std::string, std::string> GetGlobalVarTypes(const std::string& hint)
        {
            GlobalTool* tool = GetGlobalVarModule(hint);
            vm::GlobalVariableModule::GlobalVarInfoList* all = nullptr;
            try { all = tool->GetAllGlobalVar(); }
            catch (const vm::CVmException& e)
            {
                if (e.GetErrorCode() == CodeModuleNotFound) throw NoGlobalModule(e.GetErrorCode(), hint);
                throw VisionDeviceError(Describe("read global variable list", e), e.GetErrorCode());
            }
            catch (...) { throw VisionDeviceError("read global variable list: unknown VisionMaster SDK exception", -2); }
            if (!all) throw VisionDeviceError(Loc::T("Vm.DevGvListNull"));

            std::map<std::string, std::string> d;
            for (int i = 0; i < all->nCount && i < static_cast<int>(std::size(all->stGlobalValue)); ++i)
            {
                std::string name = Str(all->stGlobalValue[i].strValueName);
                if (!name.empty()) d[name] = Str(all->stGlobalValue[i].strValueType);
            }
            return d;
        }

        /// Ghi một biến theo đúng kiểu đang khai: <c>int</c> thì SetVarInt (làm tròn), còn lại SetVarFloat. Cờ
        /// <c>IsTriggerLocate</c> khai int cho Module rẽ nhánh (Tuấn 2026-10-08); các biến khác đều float.
        /// SDK: CVariable::SetVarInt / SetVarFloat (Variable.h) — nhận số nhị phân, không qua chuỗi.
        void WriteVar(GlobalTool* tool, const std::string& name, double value, const std::string& type, const std::string& hint)
        {
            // GetGlobalVariableToolInstance trả con trỏ khác null cả khi solution KHÔNG có module Global Variable
            // (đo ở máy dev Slit); khi đó lời ghi ném IMVS_EC_MODULE_NOT_FOUND — đổi về đúng câu của bản C#.
            try
            {
                if (base::EqualsIgnoreCase(type, "int"))
                {
                    int n = static_cast<int>(std::nearbyint(value));   // Math.Round: làm tròn nửa về số chẵn
                    tool->SetVarInt(name.c_str(), &n, 1);
                }
                else
                {
                    float f = static_cast<float>(value);
                    tool->SetVarFloat(name.c_str(), &f, 1);
                }
            }
            catch (const vm::CVmException& e)
            {
                if (e.GetErrorCode() == CodeModuleNotFound) throw NoGlobalModule(e.GetErrorCode(), hint);
                throw VisionDeviceError(Describe("write global variable \"" + name + "\"", e), e.GetErrorCode());
            }
            catch (...) { throw VisionDeviceError("write global variable \"" + name + "\": unknown VisionMaster SDK exception", -2); }
        }

        /// <summary>
        /// Ghi vị trí trục thật vào <b>biến toàn cục</b> của solution (Tuấn chốt 2026-09-12) — đường nạp điểm hiệu chuẩn.
        ///
        /// <para>Đây là cách nạp toạ độ vật lý cho module hiệu chuẩn: module đặt <i>Calibration Points Input = By
        /// Coordinate</i> rồi subscribe <i>Physical Coordinate X/Y</i> và <i>World Rotate Angle</i> vào các biến toàn
        /// cục này; App ghi giá trị ngay trước khi kích Flow. Chính Hikrobot dùng khuôn đó trong tài liệu "Calibration
        /// and Locating Theory" tr.54 (các biến <c>BottomX/BottomY/BottomR</c> kiểu float).</para>
        ///
        /// <para>Hơn hẳn cách khai input riêng cho từng Flow ở chỗ: một chỗ duy nhất giữ trạng thái trục của máy, Flow
        /// nào cần trục nào thì subscribe trục đó — không phải khai lại bộ input cho mỗi Flow hiệu chuẩn, và không Flow
        /// nào có thể "quên" nhận toạ độ.</para>
        ///
        /// <para>Tên biến chưa có trong solution thì ném lỗi kèm danh sách biến đang có — thà dừng còn hơn nạp một điểm
        /// hiệu chuẩn với toạ độ cũ mà không ai biết. Không đọc được danh sách thì bỏ qua phép đối chiếu tên thay vì chặn
        /// cả phép hiệu chuẩn (và ghi float như trước) — GetGlobalVarTypeMap của bản C#.</para>
        /// </summary>
        void SetGlobalFloats(const std::map<std::string, double>& vars, const std::string& hint)
        {
            if (vars.empty()) return;
            GlobalTool* tool = GetGlobalVarModule(hint);

            // Bản C# ném "không có module Global Variable" ngay khi không tìm thấy module; còn đọc danh sách lỗi vì
            // lý do khác thì bỏ qua đối chiếu tên. SDK C++ chỉ lộ "không có module" qua mã lỗi của lời đọc danh sách
            // (đo ở máy dev Slit 2026-10-06: GetAllGlobalVar ném 0xE0000302, còn SetVarFloat ném 0xE000000A).
            std::map<std::string, std::string> declared;
            try
            {
                for (auto& kv : GetGlobalVarTypes(hint)) declared[StripPercent(kv.first)] = kv.second;
            }
            catch (const VisionDeviceError& e)
            {
                if (e.Code == CodeModuleNotFound) throw;
                declared.clear();
            }
            catch (...) { declared.clear(); }

            for (auto& kv : vars)
            {
                if (kv.first.empty()) continue;
                auto it = declared.find(kv.first);
                if (!declared.empty() && it == declared.end())
                {
                    std::vector<std::string> have;
                    for (auto& d : declared) have.push_back(d.first);
                    throw VisionDeviceError(Loc::F("Vm.DevVarNotDeclaredList", kv.first, base::Join(have, ", ")));
                }
                WriteVar(tool, kv.first, kv.second, it == declared.end() ? std::string() : it->second, hint);
            }
        }

        /// Ghi biến toàn cục cho chu trình mới — NGHIÊM: tên trống, không đọc được danh sách biến, tên chưa
        /// khai, hay giá trị không hữu hạn đều ném lỗi và KHÔNG ghi biến nào. Khác <see cref="SetGlobalFloats"/>
        /// (đường nạp điểm hiệu chuẩn) vốn bỏ qua đối chiếu khi không đọc được danh sách.
        void SetGlobalFloatsStrict(const std::map<std::string, double>& vars, const std::string& hint)
        {
            if (vars.empty()) return;
            GlobalTool* tool = GetGlobalVarModule(hint);

            std::map<std::string, std::string> declared;
            for (auto& kv : GetGlobalVarTypes(hint)) declared[StripPercent(kv.first)] = kv.second;

            for (auto& kv : vars)
            {
                if (kv.first.empty()) throw VisionDeviceError(Loc::T("Vm.DevVarNameEmpty"));
                if (declared.find(kv.first) == declared.end()) throw VisionDeviceError(Loc::F("Vm.DevVarNotDeclared", kv.first));
                if (!std::isfinite(kv.second)) throw VisionDeviceError(Loc::F("Vm.DevValueNotFinite", kv.first));
            }
            for (auto& kv : vars) WriteVar(tool, kv.first, kv.second, declared[kv.first], hint);
        }

        /// SDK: IVmSolution::SaveSolution (IVmSolution.h) — lưu vào chính file .solw đang nạp.
        void Save() { Sdk("save solution", [&] { sol->SaveSolution(); }); }

        /// Module tầng ngoài lấy bằng "Flow.Module", module trong Group bằng "Flow.Group.Module" ("Flow.Module" trả null)
        /// — dò trên SDK .NET thật ngày 19/09 (GetFlowModules của bản C#). Lấy hỏng thì nullptr, như TryGetModule.
        template <class Getter>
        static auto TryGetTool(Getter&& get, const std::string& flow, const std::string& group, const std::string& name) -> decltype(get(""))
        {
            decltype(get("")) tool = nullptr;
            try { tool = get((flow + "." + name).c_str()); } catch (...) { tool = nullptr; }
            if (tool == nullptr && !group.empty())
            {
                try { tool = get((flow + "." + group + "." + name).c_str()); } catch (...) { tool = nullptr; }
            }
            return tool;
        }

        void DisconnectLocked()
        {
            if (sol && !g_sdkDisposed.load())
            {
                try { sol->UnRegisterCallBack(); } catch (...) {}
                if (loaded.load())
                {
                    // SDK: IMVS_PF_CloseSolution(handle) (iMVS-6000PlatformSDKC.h) với handle từ
                    // IVmSolution::GetSolutionHandle — tương đương CloseSolution() bản .NET. Chưa chạy được ở máy dev.
                    try { if (void* h = sol->GetSolutionHandle()) ::IMVS_PF_CloseSolution(h); } catch (...) {}
                }
                // SDK: DestroySolutionInstance (IVmSolution.h). Đo ở máy dev: tạo → huỷ → tạo lại 3 vòng đều được.
                try { ::DestroySolutionInstance(sol); } catch (...) {}
            }
            sol = nullptr;
            renderParams.reset();
            gvTool = nullptr;
            loaded = false;
            connected = false;
            std::lock_guard<std::mutex> l(cacheMutex);
            flowNames.clear();
            solutionFile.clear();
        }
    };

    VisionMasterDevice::VisionMasterDevice(std::function<std::shared_ptr<SystemData>()> sys) : m(std::make_unique<Impl>())
    {
        m->sys = std::move(sys);
    }

    /// Huỷ instance solution nếu còn (không gọi DisposeResource — hãng dặn tránh gọi nó trong destructor).
    VisionMasterDevice::~VisionMasterDevice()
    {
        try { Disconnect(); } catch (...) {}
    }

    std::shared_ptr<IVisionDevice> VisionMasterDevice::Create(std::function<std::shared_ptr<SystemData>()> sys)
    {
        return std::make_shared<VisionMasterDevice>(std::move(sys));
    }

    bool VisionMasterDevice::IsConnected() const { return m->connected.load(); }
    bool VisionMasterDevice::IsSolutionLoaded() const { return m->loaded.load(); }

    bool VisionMasterDevice::Connect()
    {
        std::lock_guard<std::recursive_timed_mutex> lock(m->sdk);
        if (m->sol) { m->connected = true; return true; }

        RequireSdkDll();
        if (g_sdkDisposed.load())
            throw VisionDeviceError("VisionMaster SDK already released (DisposeSdk) — restart the program to reconnect", CodeInvalidHandle);

        // SDK: GetSolutionExistedInstance / CreateSolutionInstance (IVmSolution.h; mẫu GetResultControlDlg.cpp).
        // Instance còn sống thì dùng lại: tạo lần hai làm RegisterCallBack ném lỗi và SDK sập ở lời gọi kế (đo ở máy dev).
        Solution* sol = Sdk("get solution instance", [] { return ::GetSolutionExistedInstance(); });
        if (!sol) sol = Sdk("create solution instance (CreateSolutionInstance)", [] { return ::CreateSolutionInstance(); });
        if (!sol) throw VisionDeviceError("CreateSolutionInstance returned null");
        m->sol = sol;

        // App dùng chuỗi UTF-8 khắp nơi (đường dẫn có dấu, tên Flow) — header: "Should be called after
        // CreateSolutionInstance". Không đặt được thì vẫn chạy với tên ASCII.
        try { ::SetGlobalEncoding(IMVS_ENCODING_UTF8); } catch (...) {}

        try
        {
            Sdk("register callback", [&] { sol->RegisterCallBack(OnSdkCallback, m->signal.get()); });
            // Tắt callback kết quả của mọi module để đỡ tốn tài nguyên — chỉ bật cho Flow cần ảnh.
            Sdk("disable module callbacks", [&] { sol->DisableModulesCallback(); });
        }
        catch (...)
        {
            m->DisconnectLocked();
            throw;
        }
        m->connected = true;
        return true;
    }

    void VisionMasterDevice::Disconnect()
    {
        std::lock_guard<std::recursive_timed_mutex> lock(m->sdk);
        m->DisconnectLocked();
    }

    void VisionMasterDevice::DisposeSdk()
    {
        if (g_sdkDisposed.exchange(true)) return;
        if (GetModuleHandleW(L"iMVS-6000PlatformSDK.dll") == nullptr) return;   // SDK chưa từng được nạp
        try { ::DisposeResource(); } catch (...) {}
    }

    bool VisionMasterDevice::LoadSolution(const std::string& solutionPath)
    {
        std::lock_guard<std::recursive_timed_mutex> lock(m->sdk);
        if (!m->sol) throw VisionDeviceError("VisionMaster SDK not connected (call Connect first)");

        // SDK: LoadSolution(path, password) (IVmSolution.h; mẫu GetResultControlDlg.cpp, VmSdk.cpp). Solution là
        // singleton toàn tiến trình nên chỉ một solution được nạp tại một thời điểm (mẫu SolutionControlCS_WPF).
        Solution* sol = Sdk("load solution", [&] { return ::LoadSolution(solutionPath.c_str(), ""); });
        if (!sol) throw VisionDeviceError("load solution: SDK returned null solution");
        m->sol = sol;
        m->renderParams.reset();     // solution mới thì module cũ không còn hiệu lực
        m->gvTool = nullptr;
        ++m->loadGen;
        m->runsWithoutCallback = 0;
        { std::lock_guard<std::mutex> l(m->signal->mutex); m->signal->everSeen = false; }

        // Sau khi nạp xong, tắt callback kết quả của mọi module để đỡ tốn tài nguyên — ta chỉ cần Run() rồi đọc output
        // của Flow, không cần callback (mẫu GetResultControl làm y hệt ngay sau LoadSolution).
        try { sol->DisableModulesCallback(); } catch (...) {}
        m->RefreshFlowNames();
        {
            // Đường dẫn tuyệt đối theo thư mục làm việc lúc nạp — như SDK hiểu đường dẫn tương đối.
            std::error_code ec;
            std::filesystem::path abs = std::filesystem::absolute(base::U8Path(solutionPath), ec);
            std::lock_guard<std::mutex> l(m->cacheMutex);
            m->solutionFile = ec ? solutionPath : base::PathToU8(abs.lexically_normal());
        }
        m->loaded = true;
        return true;
    }

    std::string VisionMasterDevice::SolutionFile() const
    {
        std::lock_guard<std::mutex> l(m->cacheMutex);
        return m->solutionFile;
    }

    VisionPose VisionMasterDevice::TriggerCalib(EVisionProcedure procedure, const AxisSnapshot& axes, std::stop_token ct)
    {
        VisionOutputNames names = m->Names();
        std::string flow = VisionConnect::FlowNameOf(names, procedure);

        std::lock_guard<std::recursive_timed_mutex> lock(m->sdk);
        if (!m->sol || !m->loaded) return VisionPose::NotFound(Loc::T("Vm.NoSolution"));

        // Ghi trước, chạy sau, trong cùng một lượt: vị trí trục + IsTriggerLocate = 0 (nhánh hiệu chuẩn của Flow soi).
        m->SetGlobalFloats(VisionConnect::CalibVars(names, axes), names.GlobalVarModule);

        // RunAndGetFloatsWithImage(flow, OutImage, OutFound) của bản C#: chạy MỘT lần, đọc cả số lẫn ảnh.
        bool hasId = false;
        unsigned id = 0;
        Procedure* proc = m->GetProcedure(flow, hasId, id);
        bool wantImage = !names.OutImage.empty();
        ProcResults* res = m->RunFlow(proc, hasId, id, wantImage, ct);

        float found = 0.0f;
        if (!TryReadNumber(res, names.OutFound, found))
            throw VisionDeviceError(Loc::F("Vm.DevMissingOutputVar", flow, names.OutFound));
        // Ảnh không bắt buộc: chưa khai output ảnh (hoặc lấy ảnh lỗi) thì ảnh là nullptr nhưng KHÔNG ném lỗi — đo tư
        // thế để đặt hàng không phụ thuộc vào việc có lấy được ảnh hiển thị hay không.
        std::shared_ptr<const ImageData> image = wantImage ? TryReadImage(res, names.OutImage) : nullptr;

        // Hiệu chuẩn tâm xoay là CAM bottom-up, hiệu chuẩn top-down là CAM top-down.
        if (VisionConnect::IsBottomUp(procedure)) m->SetBottomUp(names, flow, image);
        else m->SetTop(image);

        // Flow nạp điểm chỉ cần báo thấy hay không — module hiệu chuẩn tự giữ điểm vừa nạp.
        if (found > 0.5f) { VisionPose p; p.Found = true; return p; }
        return VisionPose::NotFound(Loc::T("Vm.SolutionNotFound"));
    }

    std::string VisionMasterDevice::ClearCalibPoints(EVisionProcedure procedure)
    {
        VisionOutputNames names = m->Names();
        std::string flow = VisionConnect::FlowNameOf(names, procedure);

        std::lock_guard<std::recursive_timed_mutex> lock(m->sdk);
        if (!m->sol || !m->loaded) return Loc::T("Vm.NoSolution");

        bool hasId = false;
        unsigned id = 0;
        Procedure* proc = m->GetProcedure(flow, hasId, id);

        // Mọi module bên trong Flow — KỂ CẢ module nằm trong Group (IVmProcedure::GetAllModuleList; GetProcedureModuleList
        // chỉ trả tầng ngoài — dò trên SDK .NET 19/09). Không duyệt được thì coi như không có module nào.
        ::ModuleInfoList* list = nullptr;
        try { list = proc->GetAllModuleList(); } catch (...) { list = nullptr; }

        std::vector<std::string> cleared;
        std::string group;
        for (unsigned i = 0; list != nullptr && i < list->nNum && i < std::size(list->astModuleInfo); ++i)
        {
            std::string display = Str(list->astModuleInfo[i].strDisplayName);
            if (display.empty()) continue;
            std::string type = Str(list->astModuleInfo[i].strModuleName);
            if (type == GroupModuleType) { group = display; continue; }

            // Module lấy bằng hàm xuất có kiểu của SDK (IVmNPointCalib.h, IVmTranslationCalib.h); DoClearPoint trả mã lỗi,
            // 0 là xoá được (bản .NET: DoClearPoint tự bắt VmException rồi trả mã lỗi — đọc IL của DLL ngày 09/10).
            int rc = 0;
            if (type == ModuNPointCalib)
            {
                auto* t = Impl::TryGetTool([](const char* p) { return ::GetNPointCalibToolInstance(p); }, flow, group, display);
                if (t == nullptr) continue;   // lấy không được thì bỏ qua như TryGetModule bản C#
                auto* params = Sdk("get N-point Calibration params", [&] { return t->GetParamObj(); });
                if (params == nullptr) throw VisionDeviceError("Flow \"" + flow + "\" module \"" + display + "\": no parameter object");
                rc = Sdk("Clear Point", [&] { return params->DoClearPoint(); });
            }
            else if (type == ModuTranslationCalib)
            {
                auto* t = Impl::TryGetTool([](const char* p) { return ::GetTranslationCalibToolInstance(p); }, flow, group, display);
                if (t == nullptr) continue;
                auto* params = Sdk("get Translation Calibration params", [&] { return t->GetParamObj(); });
                if (params == nullptr) throw VisionDeviceError("Flow \"" + flow + "\" module \"" + display + "\": no parameter object");
                rc = Sdk("Clear Point", [&] { return params->DoClearPoint(); });
            }
            else continue;

            if (rc != 0) throw VisionDeviceError(Loc::F("Vm.DevClearPointFail", display, flow, rc), rc);
            cleared.push_back(display);
        }

        if (cleared.empty()) throw VisionDeviceError(Loc::F("Vm.DevNoCalibModule", flow));
        std::lock_guard<std::mutex> l(m->cacheMutex);
        m->lastCleared = std::move(cleared);
        return {};
    }

    std::vector<std::string> VisionMasterDevice::LastClearedCalibModules() const
    {
        std::lock_guard<std::mutex> l(m->cacheMutex);
        return m->lastCleared;
    }

    VmCallResult VisionMasterDevice::CallFlow(const VmCallRequest& request, std::stop_token ct)
    {
        if (request.Flow.empty()) return VmCallResult::Fail(Loc::T("Vm.FlowNameMissing"));
        VisionOutputNames names = m->Names();

        std::lock_guard<std::recursive_timed_mutex> lock(m->sdk);
        if (!m->sol || !m->loaded) return VmCallResult::Fail(Loc::T("Vm.NoSolution"));

        // Ghi biến, chạy, đọc ngay — cùng một lượt.
        m->SetGlobalFloatsStrict(request.Globals, names.GlobalVarModule);

        // RunAndRead của bản C#.
        bool hasId = false;
        unsigned id = 0;
        Procedure* proc = m->GetProcedure(request.Flow, hasId, id);
        bool wantImage = request.Camera != EVmCamera::None && !names.OutImage.empty();
        ProcResults* res = m->RunFlow(proc, hasId, id, wantImage, ct);

        VmCallResult result;
        for (auto& name : request.Outputs)
        {
            float v = 0.0f;
            if (!TryReadNumber(res, name, v)) throw VisionDeviceError(Loc::F("Vm.DevMissingOutputParam", request.Flow, name));
            result.Values[name] = v;
        }
        // Thiếu output tuỳ chọn thì chỉ ghi nhận — tư thế khớp mẫu thường rỗng khi không thấy vật.
        for (auto& name : request.OptionalOutputs)
        {
            float v = 0.0f;
            if (TryReadNumber(res, name, v)) result.Values[name] = v;
            else result.MissingOptional.push_back(name);
        }

        std::shared_ptr<const ImageData> image = wantImage ? TryReadImage(res, names.OutImage) : nullptr;
        if (request.Camera == EVmCamera::BottomUp) m->SetBottomUp(names, request.Flow, image);
        else if (request.Camera == EVmCamera::TopDown) m->SetTop(image);

        result.Ok = true;
        return result;
    }

    VmFlowOutputs VisionMasterDevice::ReadFlowOutputs(const std::string& flow)
    {
        std::lock_guard<std::recursive_timed_mutex> lock(m->sdk);
        if (!m->sol || !m->loaded) return VmFlowOutputs::Fail(flow, Loc::T("Vm.NoSolution"));

        bool hasId = false;
        unsigned id = 0;
        Procedure* proc = m->GetProcedure(flow, hasId, id);

        VmFlowOutputs r;
        r.Flow = flow;

        // Danh sách output Flow đang khai — đúng những tên hiện trong tab Output Params. KHÔNG gọi Run(): đây là phần
        // khai báo của Flow, không phải kết quả một lần chạy.
        // SDK: IVmProcedure::GetResult()->GetAllOutputNameInfo() → IoNameInfoArray (IVmProcedure.h,
        // IVmDynamicIoDefine.h). Bản .NET ghi nhận 19/09: nguồn này trả 0 với Flow chưa chạy lần nào — chưa phân biệt
        // được là do Flow đó không khai output hay do chưa chạy. Nguồn thứ hai của bản .NET (VmModule.Outputs của chính
        // Flow, có mặt kể cả khi Flow chưa chạy) KHÔNG có trong SDK C++.
        std::set<std::string> seen;
        ProcResults* res = Sdk("get Flow result", [&] { return proc->GetResult(); });
        if (res)
        {
            vm::IoNameInfoArray outs = Sdk("read Flow Output Params", [&] { return res->GetAllOutputNameInfo(); });
            for (int i = 0; outs.pstIo != nullptr && i < outs.nNameNum; ++i)
            {
                std::string name = VmOutCheck::CleanIoName(Str(outs.pstIo[i].strIoName));
                if (!name.empty() && seen.insert(name).second) r.AddDeclared(name, TypeCodeOf(outs.pstIo[i].nType));
            }
        }

        // Số Input Params còn sót (hợp đồng bỏ Input Params từ 2026-09-14 — còn sót là rác bản cũ, không phải lỗi).
        // SDK: IVmProcedure::GetParamObj()->GetAllInputNameInfo() (IVmProcedure.h).
        try
        {
            auto* prm = proc->GetParamObj();
            r.InputParamCount = prm ? prm->GetAllInputNameInfo().nNameNum : -1;
        }
        catch (...) { r.InputParamCount = -1; }

        // Modules để RỖNG có chủ ý (FillModules của bản C#): SDK C++ không có đường nào đọc hai tab Input/Output Params
        // của một module bất kỳ kèm ô Subscribe (VmModule.Inputs/Outputs, VmIO.SubscriptionInfo chỉ có ở SDK .NET). Rỗng
        // thì phần kiểm Script nói thẳng "không duyệt được module" thay vì báo thiếu/chưa bind sạch.
        return r;
    }

    GlobalVarSyncReport VisionMasterDevice::SyncGlobalVariables(const std::vector<GlobalVarSpec>& specs, bool apply)
    {
        /// Kho biến toàn cục của solution thật (VmGlobalVarStore của bản C#).
        struct Store final : IGlobalVarStore
        {
            Impl* m{ nullptr };
            std::string hint;

            std::map<std::string, std::string> ReadAll() override { return m->GetGlobalVarTypes(hint); }

            /// Bản .NET tạo biến bằng VM.ExtensionMethod.GlobalVariableHelper.AddVar — hàm nội bộ của cửa sổ Global
            /// Variable, không có trong tài liệu SDK. Đọc IL (2026-10-18): nó gọi Apps.XmlParser VariableManager.AddVar,
            /// ghi một struct nhị phân (tên/kiểu/giá trị/GUID, trường UTF-8 cố định 256 byte) vào tham số kiểu byte
            /// "AddVar" của module Global Variable (ID 13000) qua VmModule.Params, rồi thêm output động qua
            /// VmModule.Outputs. SDK C++ không có cả hai: GlobalVariableModuleTool chỉ đọc/ghi giá trị, IMVS_PF_SetParamValue
            /// chỉ nhận chuỗi, và không có hàm thêm IO động. Vì vậy bản C++ chỉ KIỂM được, không tạo được — báo rõ để
            /// người dùng tạo tay.
            void Add(const std::string& name, const std::string& type, const std::string&) override
            {
                throw VisionDeviceError("VisionMaster C++ SDK can't create global variables — create \"" + name +
                                        "\" (type " + type + ") in VisionMaster's Global Variable window, then save the solution");
            }

            /// Ghi qua hàm ghi theo kiểu (số nhị phân, không qua chuỗi số theo culture Windows) — SetGlobalVarValue của
            /// bản C#. SDK: CVariable::SetVarFloat / SetVarInt / SetVarString (Variable.h).
            void SetValue(const std::string& name, const std::string& type, const std::string& invariantValue) override
            {
                GlobalTool* tool = m->GetGlobalVarModule(hint);
                if (type == "float")
                {
                    float f = ParseInvariant<float>(invariantValue, "float");
                    Sdk("write global variable", [&] { tool->SetVarFloat(name.c_str(), &f, 1); });
                }
                else if (type == "int")
                {
                    int n = ParseInvariant<int>(invariantValue, "int");
                    Sdk("write global variable", [&] { tool->SetVarInt(name.c_str(), &n, 1); });
                }
                else
                {
                    auto data = std::make_unique<vm::InputStringData>();
                    std::memset(data.get(), 0, sizeof(vm::InputStringData));
                    std::memcpy(data->strValue, invariantValue.data(), std::min(invariantValue.size(), sizeof(data->strValue) - 1));
                    Sdk("write global variable", [&] { tool->SetVarString(name.c_str(), data.get(), 1); });
                }
            }

            std::string Save() override { m->Save(); return {}; }
        };

        VisionOutputNames names = m->Names();
        std::lock_guard<std::recursive_timed_mutex> lock(m->sdk);
        if (!m->sol || !m->loaded) return GlobalVarSyncReport::Fail(Loc::T("Vm.NoVisionSolution"));
        Store store;
        store.m = m.get();
        store.hint = names.GlobalVarModule;
        return GlobalVarSync::Run(&store, specs, apply);
    }

    std::string VisionMasterDevice::SaveSolution()
    {
        std::lock_guard<std::recursive_timed_mutex> lock(m->sdk);
        if (!m->sol || !m->loaded) return Loc::T("Vm.NoSolution");
        try { m->Save(); return {}; }
        catch (const std::exception& ex) { std::string w = ex.what(); return w.empty() ? std::string("save solution failed") : w; }
    }

    std::shared_ptr<const ImageData> VisionMasterDevice::LastTopDownImage() const
    {
        std::lock_guard<std::mutex> l(m->cacheMutex);
        return m->top;
    }

    std::shared_ptr<const ImageData> VisionMasterDevice::LastBottomUpImageTool1() const
    {
        std::lock_guard<std::mutex> l(m->cacheMutex);
        return m->bottom1;
    }

    std::shared_ptr<const ImageData> VisionMasterDevice::LastBottomUpImageTool2() const
    {
        std::lock_guard<std::mutex> l(m->cacheMutex);
        return m->bottom2;
    }

    void* VisionMasterDevice::RenderSource(const std::string& path)
    {
        std::unique_lock<std::recursive_timed_mutex> lock(m->sdk, UiLockWait);
        if (!lock.owns_lock() || !m->sol || !m->loaded || path.empty()) return nullptr;

        try
        {
            std::size_t dot = path.find('.');
            std::string flow = path.substr(0, dot);
            bool hasId = false;
            unsigned id = 0;
            Procedure* proc = m->GetProcedure(flow, hasId, id);

            // Cả Flow — SDK: IVmProcedure::GetControlInfo() (IVmProcedure.h). Chưa chạy được ở máy dev.
            if (dot == std::string::npos) return Sdk("get Flow ControlInfo", [&] { return proc->GetControlInfo(); });

            // Một module: tra ID theo tên hiển thị trong IVmProcedure::GetAllModuleList() (trả cả module nằm
            // trong Group), rồi dựng CModuParamsBase(moduleID) và gọi GetControlInfo() của nó
            // (VmModuleParamBase.h) — đúng hàm mà mẫu VC gọi qua lớp tham số riêng của từng module
            // (pCirFindParam->GetControlInfo(); các lớp đó thừa kế CModuParamsBase và không định nghĩa lại hàm này).
            // Đã chạy ở máy dev khi KHÔNG có solution: trả con trỏ khác null trỏ vào vùng tĩnh của DLL.
            std::string rest = path.substr(dot + 1);
            std::size_t lastDot = rest.rfind('.');
            std::string leaf = lastDot == std::string::npos ? rest : rest.substr(lastDot + 1);

            ::ModuleInfoList* list = Sdk("read Flow module list", [&] { return proc->GetAllModuleList(); });
            if (!list) return nullptr;
            for (unsigned i = 0; i < list->nNum && i < std::size(list->astModuleInfo); ++i)
            {
                std::string name = Str(list->astModuleInfo[i].strDisplayName);
                if (name.empty() || (name != rest && name != leaf)) continue;
                unsigned moduleId = list->astModuleInfo[i].nModuleID;
                m->renderParams = Sdk("create module params", [&] { return std::make_unique<vm::CModuParamsBase>(moduleId); });
                return Sdk("get module ControlInfo", [&] { return m->renderParams->GetControlInfo(); });
            }
            return nullptr;
        }
        catch (...) { return nullptr; }
    }

    bool VisionMasterDevice::EnableResultCallbacks(const std::string& path, std::string& error)
    {
        error.clear();
        std::unique_lock<std::recursive_timed_mutex> lock(m->sdk, UiLockWait);
        if (!lock.owns_lock() || !m->sol || !m->loaded || path.empty()) return false;

        std::vector<std::string> failed;
        // Một module: lấy đối tượng công cụ bằng operator[] — "Flow.Module", không có thì "Flow.<Group>.Module"
        // (như TryGetTool). Lỗi của từng module không dừng các module khác, gom vào câu báo (bản C# chỉ log cảnh báo).
        auto enableOne = [&](const std::string& flow, const std::string& group, const std::string& name) {
            void* tool = nullptr;
            try { tool = Impl::TryGetTool([this](const char* p) { return (*m->sol)[p]; }, flow, group, name); }
            catch (...) { tool = nullptr; }
            try
            {
                if (tool == nullptr || !Sdk("enable module result callback", [&] { return CallEnableResultCallback(tool); }))
                    failed.push_back(name);
            }
            catch (const std::exception& ex) { failed.push_back(name + " (" + ex.what() + ")"); }
        };

        try
        {
            std::size_t dot = path.find('.');
            std::string flow = path.substr(0, dot);
            bool hasId = false;
            unsigned id = 0;
            Procedure* proc = m->GetProcedure(flow, hasId, id);

            ::ModuleInfoList* list = nullptr;
            try { list = proc->GetAllModuleList(); } catch (...) { list = nullptr; }

            if (dot == std::string::npos)
            {
                // Cả Flow: Display của Flow lấy ảnh và hình từ các module con, nên bật hết (cả module trong Group), rồi Flow.
                std::string group;
                for (unsigned i = 0; list != nullptr && i < list->nNum && i < std::size(list->astModuleInfo); ++i)
                {
                    std::string display = Str(list->astModuleInfo[i].strDisplayName);
                    if (display.empty()) continue;
                    bool isGroup = Str(list->astModuleInfo[i].strModuleName) == GroupModuleType;
                    enableOne(flow, isGroup ? std::string() : group, display);
                    if (isGroup) group = display;
                }
                Sdk("enable Flow result callback", [&] { proc->EnableResultCallback(); });
            }
            else
            {
                // Một module — đúng cách dò của RenderSource (tên đầy đủ sau Flow, hoặc tên lá).
                std::string rest = path.substr(dot + 1);
                std::size_t lastDot = rest.rfind('.');
                std::string leaf = lastDot == std::string::npos ? rest : rest.substr(lastDot + 1);
                void* direct = nullptr;
                try { direct = (*m->sol)[path.c_str()]; } catch (...) { direct = nullptr; }
                if (direct != nullptr)
                {
                    if (!Sdk("enable module result callback", [&] { return CallEnableResultCallback(direct); }))
                        failed.push_back(rest);
                }
                else
                {
                    std::string group;
                    bool seen = false;
                    for (unsigned i = 0; list != nullptr && i < list->nNum && i < std::size(list->astModuleInfo); ++i)
                    {
                        std::string display = Str(list->astModuleInfo[i].strDisplayName);
                        if (display.empty()) continue;
                        bool isGroup = Str(list->astModuleInfo[i].strModuleName) == GroupModuleType;
                        if (display == rest || display == leaf)
                        {
                            enableOne(flow, isGroup ? std::string() : group, display);
                            seen = true;
                            break;
                        }
                        if (isGroup) group = display;
                    }
                    if (!seen) failed.push_back(rest);
                }
            }
        }
        catch (const std::exception& ex) { error = ex.what(); return true; }
        catch (...) { error = "unknown VisionMaster SDK exception"; return true; }

        if (!failed.empty())
        {
            error = "no result callback for";
            for (std::size_t i = 0; i < failed.size(); ++i) error += (i == 0 ? " " : ", ") + failed[i];
        }
        return true;
    }

    std::vector<std::string> VisionMasterDevice::FlowNames()
    {
        {
            std::unique_lock<std::recursive_timed_mutex> lock(m->sdk, UiLockWait);
            if (lock.owns_lock())
            {
                if (!m->sol || !m->loaded) return {};
                m->RefreshFlowNames();
            }
        }
        std::lock_guard<std::mutex> l(m->cacheMutex);
        return m->flowNames;
    }

    std::string VisionMasterDevice::SdkVersion()
    {
        if (!EnsureSdkDll()) return {};
        try
        {
            const char* v = ::IMVS_PF_GetBuildVersion();   // iMVS-6000PlatformSDKC.h
            return v ? std::string(v) : std::string();
        }
        catch (...) { return {}; }
    }
}
