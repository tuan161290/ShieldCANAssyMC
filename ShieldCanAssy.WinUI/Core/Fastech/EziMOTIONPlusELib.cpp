#include "Fastech/EziMOTIONPlusELib.h"

#include "Base/Text.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <atomic>
#include <format>
#include <memory>
#include <mutex>

// Kiểu ABI: header gốc của Fastech (FAS_EziMOTIONPlusE.h, Include_x64) dùng BYTE/WORD/DWORD/long/BOOL/unsigned long.
// Trên Windows x64 long = unsigned long = 32 bit, BOOL = int — nên con trỏ hàm dưới đây khai bằng kiểu cố định độ rộng
// (cùng kiểu với wrapper) mà vẫn đúng ABI từng bit.
static_assert(sizeof(long) == 4 && sizeof(unsigned long) == 4, "long của Win64 phải 32 bit");
static_assert(sizeof(BOOL) == sizeof(int32_t) && sizeof(DWORD) == sizeof(uint32_t) && sizeof(WORD) == sizeof(uint16_t));
static_assert(sizeof(wchar_t) == 2, "LPCWSTR = UTF-16");
static_assert(sizeof(float) == 4);

// Danh sách 151 entry point — đúng thứ tự và đủ như các [DllImport] của EziMOTIONPlusELib.cs. Kiểu tham số theo header
// gốc (ABI thật); chỗ C# khai khác header được ghi chú. CallingConvention: [DllImport] mặc định Winapi = __stdcall
// (WINAPI của header gốc) — trên x64 chỉ có một quy ước gọi, WINAPI giữ cho đúng nghĩa. CharSet mặc định Ansi:
// StringBuilder → LPSTR (FAS_GetSlaveInfo/FAS_GetMotorInfo); hai hàm log khai rõ LPWStr.
#define SCA_EZI_API_LIST(X)                                                                                              \
    X(FAS_Connect, int32_t, (uint8_t, uint8_t, uint8_t, uint8_t, int32_t))              /* BOOL */                       \
    X(FAS_ConnectTCP, int32_t, (uint8_t, uint8_t, uint8_t, uint8_t, int32_t))           /* BOOL */                       \
    X(FAS_IsBdIDExist, int32_t, (int32_t, uint8_t*, uint8_t*, uint8_t*, uint8_t*))      /* BOOL */                       \
    X(FAS_IsIPAddressExist, int32_t, (uint8_t, uint8_t, uint8_t, uint8_t, int32_t*))    /* BOOL */                       \
    X(FAS_Reconnect, int32_t, (int32_t))                                                /* BOOL */                       \
    X(FAS_SetAutoReconnect, void, (int32_t))                                            /* C# khai int */                \
    X(FAS_Close, void, (int32_t))                                                                                        \
    X(FAS_IsSlaveExist, int32_t, (int32_t))                                             /* BOOL */                       \
    X(FAS_EnableLog, void, (int32_t))                                                                                    \
    X(FAS_SetLogLevel, void, (int32_t))                                                 /* enum LOG_LEVEL */             \
    X(FAS_SetLogPath, int32_t, (const wchar_t*))                                        /* BOOL, LPCWSTR */              \
    X(FAS_PrintCustomLog, void, (int32_t, int32_t, const wchar_t*))                     /* C# khai int; LPCTSTR = W */   \
    X(FAS_GetEthernetAddr, int32_t, (int32_t, uint32_t*, uint32_t*, uint32_t*))                                          \
    X(FAS_SetEthernetAddr, int32_t, (int32_t, uint32_t, uint32_t, uint32_t))                                             \
    X(FAS_GetMACAddress, int32_t, (int32_t, uint64_t*))                                                                  \
    X(FAS_GetSlaveInfo, int32_t, (int32_t, uint8_t*, char*, int32_t))                                                    \
    X(FAS_GetMotorInfo, int32_t, (int32_t, uint8_t*, char*, int32_t))                                                    \
    X(FAS_GetSlaveInfoEx, int32_t, (int32_t, uint8_t*))                                 /* DRIVE_INFO* */                \
    X(FAS_SaveAllParameters, int32_t, (int32_t))                                                                         \
    X(FAS_SetParameter, int32_t, (int32_t, uint8_t, int32_t))                                                            \
    X(FAS_GetParameter, int32_t, (int32_t, uint8_t, int32_t*))                                                           \
    X(FAS_GetROMParameter, int32_t, (int32_t, uint8_t, int32_t*))                                                        \
    X(FAS_SetIOInput, int32_t, (int32_t, uint32_t, uint32_t))                                                            \
    X(FAS_GetIOInput, int32_t, (int32_t, uint32_t*))                                                                     \
    X(FAS_SetIOOutput, int32_t, (int32_t, uint32_t, uint32_t))                                                           \
    X(FAS_GetIOOutput, int32_t, (int32_t, uint32_t*))                                                                    \
    X(FAS_GetIOAssignMap, int32_t, (int32_t, uint8_t, uint32_t*, uint8_t*))                                              \
    X(FAS_SetIOAssignMap, int32_t, (int32_t, uint8_t, uint32_t, uint8_t))                                                \
    X(FAS_IOAssignMapReadROM, int32_t, (int32_t))                                                                        \
    X(FAS_ServoEnable, int32_t, (int32_t, int32_t))                                                                      \
    X(FAS_ServoAlarmReset, int32_t, (int32_t))                                                                           \
    X(FAS_StepAlarmReset, int32_t, (int32_t, int32_t))                                                                   \
    X(FAS_BrakeSet, int32_t, (int32_t, int32_t, int32_t*))                                                               \
    X(FAS_GetAxisStatus, int32_t, (int32_t, uint32_t*))                                                                  \
    X(FAS_GetIOAxisStatus, int32_t, (int32_t, uint32_t*, uint32_t*, uint32_t*))                                          \
    X(FAS_GetMotionStatus, int32_t, (int32_t, int32_t*, int32_t*, int32_t*, int32_t*, uint16_t*))                        \
    X(FAS_GetAllStatus, int32_t, (int32_t, uint32_t*, uint32_t*, uint32_t*, int32_t*, int32_t*, int32_t*, int32_t*, uint16_t*)) \
    X(FAS_GetAllStatusEx, int32_t, (int32_t, uint8_t*, int32_t*))                                                        \
    X(FAS_SetCommandPos, int32_t, (int32_t, int32_t))                                                                    \
    X(FAS_SetActualPos, int32_t, (int32_t, int32_t))                                                                     \
    X(FAS_ClearPosition, int32_t, (int32_t))                                                                             \
    X(FAS_GetCommandPos, int32_t, (int32_t, int32_t*))                                                                   \
    X(FAS_GetActualPos, int32_t, (int32_t, int32_t*))                                                                    \
    X(FAS_GetPosError, int32_t, (int32_t, int32_t*))                                                                     \
    X(FAS_GetActualVel, int32_t, (int32_t, int32_t*))                                                                    \
    X(FAS_GetAlarmType, int32_t, (int32_t, uint8_t*))                                                                    \
    X(FAS_GetWarningType, int32_t, (int32_t, uint8_t*))                                                                  \
    X(FAS_MoveStop, int32_t, (int32_t))                                                                                  \
    X(FAS_EmergencyStop, int32_t, (int32_t))                                                                             \
    X(FAS_MovePause, int32_t, (int32_t, int32_t))                                                                        \
    X(FAS_MoveOriginSingleAxis, int32_t, (int32_t))                                                                      \
    X(FAS_MoveSingleAxisAbsPos, int32_t, (int32_t, int32_t, uint32_t))                                                   \
    X(FAS_MoveSingleAxisIncPos, int32_t, (int32_t, int32_t, uint32_t))                                                   \
    X(FAS_MoveToLimit, int32_t, (int32_t, uint32_t, int32_t))                                                            \
    X(FAS_MoveVelocity, int32_t, (int32_t, uint32_t, int32_t))                                                           \
    X(FAS_PositionAbsOverride, int32_t, (int32_t, int32_t))                                                              \
    X(FAS_PositionIncOverride, int32_t, (int32_t, int32_t))                                                              \
    X(FAS_VelocityOverride, int32_t, (int32_t, uint32_t))                                                                \
    X(FAS_MoveLinearAbsPos, int32_t, (uint8_t, int32_t*, int32_t*, uint32_t, uint16_t))                                  \
    X(FAS_MoveLinearIncPos, int32_t, (uint8_t, int32_t*, int32_t*, uint32_t, uint16_t))                                  \
    X(FAS_MoveLinearAbsPos2, int32_t, (uint8_t, int32_t*, int32_t*, uint32_t, uint16_t))                                 \
    X(FAS_MoveLinearIncPos2, int32_t, (uint8_t, int32_t*, int32_t*, uint32_t, uint16_t))                                 \
    X(FAS_MoveCircleAbsPos1, int32_t, (uint8_t, int32_t*, int32_t*, int32_t*, int32_t, uint32_t, uint16_t, int32_t))     \
    X(FAS_MoveCircleIncPos1, int32_t, (uint8_t, int32_t*, int32_t*, int32_t*, int32_t, uint32_t, uint16_t, int32_t))     \
    X(FAS_MoveCircleAbsPos2, int32_t, (uint8_t, int32_t*, int32_t*, uint32_t, int32_t, uint32_t, uint16_t, int32_t))     \
    X(FAS_MoveCircleIncPos2, int32_t, (uint8_t, int32_t*, int32_t*, uint32_t, int32_t, uint32_t, uint16_t, int32_t))     \
    X(FAS_MoveCircleAbsPos3, int32_t, (uint8_t, int32_t*, int32_t*, uint32_t, int32_t, uint32_t, uint16_t, int32_t))     \
    X(FAS_MoveCircleIncPos3, int32_t, (uint8_t, int32_t*, int32_t*, uint32_t, int32_t, uint32_t, uint16_t, int32_t))     \
    X(FAS_TriggerOutput_RunA, int32_t, (int32_t, int32_t, int32_t, uint32_t, uint32_t))                                  \
    X(FAS_TriggerOutput_Status, int32_t, (int32_t, uint8_t*))                                                            \
    X(FAS_SetTriggerOutputEx, int32_t, (int32_t, uint8_t, uint8_t, uint16_t, uint8_t, int32_t*))                         \
    X(FAS_GetTriggerOutputEx, int32_t, (int32_t, uint8_t, uint8_t*, uint16_t*, uint8_t*, int32_t*))                      \
    X(FAS_MovePush, int32_t, (int32_t, uint32_t, uint32_t, int32_t, uint16_t, uint16_t, uint16_t, uint32_t, int32_t, uint16_t)) \
    X(FAS_GetPushStatus, int32_t, (int32_t, uint8_t*))                                                                   \
    X(FAS_MoveSingleAxisAbsPosEx, int32_t, (int32_t, int32_t, uint32_t, uint8_t*))      /* MOTION_OPTION_EX* */          \
    X(FAS_MoveSingleAxisIncPosEx, int32_t, (int32_t, int32_t, uint32_t, uint8_t*))      /* MOTION_OPTION_EX* */          \
    X(FAS_MoveVelocityEx, int32_t, (int32_t, uint32_t, int32_t, uint8_t*))              /* VELOCITY_OPTION_EX* */        \
    X(FAS_PosTableReadItem, int32_t, (int32_t, uint16_t, uint8_t*))                     /* LPITEM_NODE */                \
    X(FAS_PosTableWriteItem, int32_t, (int32_t, uint16_t, uint8_t*))                    /* LPITEM_NODE */                \
    X(FAS_PosTableWriteROM, int32_t, (int32_t))                                                                          \
    X(FAS_PosTableReadROM, int32_t, (int32_t))                                                                           \
    X(FAS_PosTableRunItem, int32_t, (int32_t, uint16_t))                                                                 \
    X(FAS_PosTableReadOneItem, int32_t, (int32_t, uint16_t, uint16_t, int32_t*))                                         \
    X(FAS_PosTableWriteOneItem, int32_t, (int32_t, uint16_t, uint16_t, int32_t))                                         \
    X(FAS_PosTableSingleRunItem, int32_t, (int32_t, int32_t, uint16_t))                                                  \
    X(FAS_GapControlEnable, int32_t, (int32_t, uint16_t, int32_t, int32_t, int32_t, int32_t))                            \
    X(FAS_GapControlDisable, int32_t, (int32_t))                                                                         \
    X(FAS_IsGapControlEnable, int32_t, (int32_t, int32_t*, uint16_t*))                                                   \
    X(FAS_GapControlGetADCValue, int32_t, (int32_t, int32_t*))                                                           \
    X(FAS_GapOneResultMonitor, int32_t, (int32_t, uint8_t*, int32_t*, int32_t*, int32_t*, int32_t*, int32_t*, int32_t*)) \
    X(FAS_GetAlarmLogs, int32_t, (int32_t, uint8_t*))                                   /* ALARM_LOG* */                 \
    X(FAS_ResetAlarmLogs, int32_t, (int32_t))                                                                            \
    X(FAS_GetWarningLogs, int32_t, (int32_t, uint8_t*))                                 /* ALARM_LOG* */                 \
    X(FAS_ResetWarningLogs, int32_t, (int32_t))                                                                          \
    X(FAS_GetInput, int32_t, (int32_t, uint32_t*, uint32_t*))                                                            \
    X(FAS_ClearLatch, int32_t, (int32_t, uint32_t))                                                                      \
    X(FAS_GetLatchCount, int32_t, (int32_t, uint8_t, uint32_t*))                        /* C# khai uint iInputNo */      \
    X(FAS_GetLatchCountAll, int32_t, (int32_t, uint32_t*))                                                               \
    X(FAS_GetLatchCountAll32, int32_t, (int32_t, uint32_t*))                                                             \
    X(FAS_ClearLatchCount, int32_t, (int32_t, uint32_t))                                                                 \
    X(FAS_GetOutput, int32_t, (int32_t, uint32_t*, uint32_t*))                                                           \
    X(FAS_SetOutput, int32_t, (int32_t, uint32_t, uint32_t))                                                             \
    X(FAS_SetTrigger, int32_t, (int32_t, uint8_t, uint8_t*))                            /* TRIGGER_INFO* */              \
    X(FAS_SetRunStop, int32_t, (int32_t, uint32_t, uint32_t))                                                            \
    X(FAS_GetTriggerCount, int32_t, (int32_t, uint8_t, uint32_t*))                      /* C# khai uint uOutputNo */     \
    X(FAS_GetIOLevel, int32_t, (int32_t, uint32_t*))                                                                     \
    X(FAS_SetIOLevel, int32_t, (int32_t, uint32_t))                                                                      \
    X(FAS_LoadIOLevel, int32_t, (int32_t))                                                                               \
    X(FAS_SaveIOLevel, int32_t, (int32_t))                                                                               \
    X(FAS_GetInputFilter, int32_t, (int32_t, uint16_t*))                                                                 \
    X(FAS_SetInputFilter, int32_t, (int32_t, uint16_t))                                                                  \
    X(FAS_GetIODirection, int32_t, (int32_t, uint32_t*))                                                                 \
    X(FAS_SetIODirection, int32_t, (int32_t, uint32_t))                                                                  \
    X(FAS_SetADConfig, int32_t, (int32_t, uint8_t, int32_t, int32_t, int32_t*))                                          \
    X(FAS_GetADConfig, int32_t, (int32_t, uint8_t, int32_t, int32_t*))                                                   \
    X(FAS_LoadADConfig, int32_t, (int32_t))                                                                              \
    X(FAS_SaveADConfig, int32_t, (int32_t))                                                                              \
    X(FAS_ReadADValue, int32_t, (int32_t, uint8_t, int16_t*))                                                            \
    X(FAS_ReadADAllValue, int32_t, (int32_t, uint8_t, uint8_t*))                        /* AD_BUFFER* */                 \
    X(FAS_GetAllADResult, int32_t, (int32_t, uint8_t*))                                 /* AD_RESULT* */                 \
    X(FAS_GetADResult, int32_t, (int32_t, uint8_t, float*))                                                              \
    X(FAS_SetADRange, int32_t, (int32_t, uint8_t, int32_t))                                                              \
    X(FAS_SetDACConfig, int32_t, (int32_t, uint8_t, int32_t, int32_t, int32_t*))                                         \
    X(FAS_GetDACConfig, int32_t, (int32_t, uint8_t, int32_t, int32_t*))                                                  \
    X(FAS_LoadDACConfig, int32_t, (int32_t))                                                                             \
    X(FAS_SaveDACConfig, int32_t, (int32_t))                                                                             \
    X(FAS_SetDACValue, int32_t, (int32_t, uint8_t, int32_t, int32_t))                                                    \
    X(FAS_GetDACValue, int32_t, (int32_t, uint8_t, int32_t*, int32_t*))                                                  \
    X(FAS_CounterCommand, int32_t, (int32_t, uint8_t, int32_t, int32_t))                                                 \
    X(FAS_GetCounterValue, int32_t, (int32_t, uint8_t, int32_t, int32_t*))                                               \
    X(FAS_GetCounterStatus, int32_t, (int32_t, uint32_t*))                                                               \
    X(FAS_SetCounterTrigger, int32_t, (int32_t, uint8_t, int32_t, int32_t, uint32_t, uint32_t, uint32_t))                \
    X(FAS_SetCounterConfig, int32_t, (int32_t, uint8_t, int32_t, int32_t, int32_t*))                                     \
    X(FAS_GetCounterConfig, int32_t, (int32_t, uint8_t, int32_t, int32_t*))                                              \
    X(FAS_LoadCounterConfig, int32_t, (int32_t))                                                                         \
    X(FAS_SaveCounterConfig, int32_t, (int32_t))                                                                         \
    X(FAS_SPEEDGetInputStatus, int32_t, (int32_t, uint32_t*))                                                            \
    X(FAS_SPEEDGetOutputStatus, int32_t, (int32_t, uint32_t*))                                                           \
    X(FAS_SPEEDGetNETStatus, int32_t, (int32_t, uint32_t*))                                                              \
    X(FAS_SPEEDSetOutput, int32_t, (int32_t, uint32_t, uint32_t))                                                        \
    X(FAS_SPEEDGetCommandSpeed, int32_t, (int32_t, int32_t*))                                                            \
    X(FAS_SPEEDGetFeedbackSpeed, int32_t, (int32_t, int32_t*))                                                           \
    X(FAS_SPEEDGetGearSpeed, int32_t, (int32_t, float*))                                                                 \
    X(FAS_SPEEDGetConveyorSpeed, int32_t, (int32_t, float*))                                                             \
    X(FAS_SPEEDGetOperationNo, int32_t, (int32_t, uint8_t*))                                                             \
    X(FAS_SPEEDMoveVelocity, int32_t, (int32_t, uint16_t, int32_t, uint16_t, uint16_t, uint16_t))                        \
    X(FAS_SPEEDSetOperationData, int32_t, (int32_t, uint8_t, uint16_t, uint16_t, uint16_t, uint16_t))                    \
    X(FAS_SPEEDGetOperationData, int32_t, (int32_t, uint8_t, uint16_t*, uint16_t*, uint16_t*, uint16_t*))                \
    X(FAS_SPEEDRunOperationData, int32_t, (int32_t, uint8_t, int32_t))                                                   \
    X(FAS_SPEEDLoadOperationData, int32_t, (int32_t))                                                                    \
    X(FAS_SPEEDSaveOperationData, int32_t, (int32_t))

namespace sca
{
    namespace
    {
        // ───────────────────────── bảng con trỏ hàm ─────────────────────────

        struct Api
        {
#define SCA_EZI_MEMBER(name, ret, params) ret (WINAPI* name) params = nullptr;
            SCA_EZI_API_LIST(SCA_EZI_MEMBER)
#undef SCA_EZI_MEMBER
        };

        /// Bảng đang dùng — wrapper chỉ đọc con trỏ này (acquire), Load/Unload ghi dưới khoá (release).
        std::atomic<const Api*> g_api{ nullptr };

        struct LoaderState
        {
            std::mutex mtx;
            HMODULE module = nullptr;
            std::unique_ptr<Api> api;
            std::filesystem::path path;
            std::vector<std::string> missing;
            std::string error;
            DWORD win32Error = 0;
        };

        LoaderState& State()
        {
            static LoaderState s;
            return s;
        }

        constexpr int32_t kFail = EziMOTIONPlusELib::NotLoadedCode;   // FMM_NOT_OPEN — hàm trả mã FMM_*
        constexpr int32_t kFalse = 0;                                 // FALSE — hàm native trả BOOL

        /// Gọi một entry point; DLL chưa nạp hoặc thiếu entry point thì trả `fail`, không gọi gì.
        template <class Fn, class R, class... A>
        R Call(Fn Api::* fn, R fail, A... args)
        {
            const Api* api = g_api.load(std::memory_order_acquire);
            if (api == nullptr || api->*fn == nullptr) return fail;
            return static_cast<R>((api->*fn)(args...));
        }

        /// Như Call cho hàm native void; trả true nếu đã gọi được.
        template <class Fn, class... A>
        bool CallVoid(Fn Api::* fn, A... args)
        {
            const Api* api = g_api.load(std::memory_order_acquire);
            if (api == nullptr || api->*fn == nullptr) return false;
            (api->*fn)(args...);
            return true;
        }

        // ───────────────────────── tiện ích Win32 ─────────────────────────

        std::filesystem::path ModuleFile(HMODULE module)
        {
            std::wstring buf(MAX_PATH, L'\0');
            for (;;)
            {
                DWORD n = GetModuleFileNameW(module, buf.data(), (DWORD)buf.size());
                if (n == 0) return {};
                if (n < buf.size())
                {
                    buf.resize(n);
                    return std::filesystem::path(buf);
                }
                if (buf.size() >= 65536) return {};
                buf.resize(buf.size() * 2);
            }
        }

        std::string Win32Message(DWORD code)
        {
            wchar_t buf[512] = {};
            DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
            DWORD n = FormatMessageW(flags, nullptr, code, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), buf, 512, nullptr);
            if (n == 0) n = FormatMessageW(flags, nullptr, code, 0, buf, 512, nullptr);
            std::wstring w(buf, n);
            while (!w.empty() && (w.back() == L'\r' || w.back() == L'\n' || w.back() == L' ' || w.back() == L'.')) w.pop_back();
            return w.empty() ? std::string("unknown error") : base::Narrow(w);
        }

        std::string DescribeWin32(DWORD code)
        {
            return std::format("Win32 error {} ({})", code, Win32Message(code));
        }

        /// LoadLibraryExW không bật hộp thoại lỗi hệ thống (thiếu DLL phụ thuộc, ổ đĩa…) — app chạy không người trông.
        HMODULE TryLoad(const std::wstring& file, DWORD flags, DWORD& err)
        {
            DWORD oldMode = 0;
            BOOL modeSet = SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &oldMode);
            HMODULE h = LoadLibraryExW(file.c_str(), nullptr, flags);
            err = h ? 0 : GetLastError();
            if (modeSet) SetThreadErrorMode(oldMode, nullptr);
            return h;
        }

        /// Resolve đủ 151 entry point rồi công bố bảng. Gọi dưới khoá State().mtx.
        void BindLocked(LoaderState& s, HMODULE h)
        {
            auto api = std::make_unique<Api>();
            std::vector<std::string> missing;
#define SCA_EZI_RESOLVE(name, ret, params)                                                  \
            api->name = reinterpret_cast<decltype(api->name)>(GetProcAddress(h, #name));    \
            if (api->name == nullptr) missing.emplace_back(#name);
            SCA_EZI_API_LIST(SCA_EZI_RESOLVE)
#undef SCA_EZI_RESOLVE

            s.module = h;
            s.path = ModuleFile(h);
            s.missing = std::move(missing);
            s.error.clear();
            s.win32Error = 0;
            s.api = std::move(api);
            g_api.store(s.api.get(), std::memory_order_release);
        }

        void FailLocked(LoaderState& s, std::string error, DWORD win32Error, std::string& out)
        {
            s.error = std::move(error);
            s.win32Error = win32Error;
            out = s.error;
        }

        /// Chuỗi char ANSI (code page của máy) → UTF-8. Encoding.Default của .NET Framework / StringBuilder ANSI = CP_ACP.
        std::string AnsiToUtf8(const char* text, size_t length)
        {
            if (length == 0) return {};
            int n = MultiByteToWideChar(CP_ACP, 0, text, (int)length, nullptr, 0);
            if (n <= 0) return {};
            std::wstring w((size_t)n, L'\0');
            MultiByteToWideChar(CP_ACP, 0, text, (int)length, w.data(), n);
            return base::Narrow(w);
        }

        /// Chuỗi C trong buffer cố định: dừng ở '\0' đầu tiên, tối đa `capacity` byte.
        std::string AnsiField(const uint8_t* field, size_t capacity)
        {
            size_t i = 0;
            while (i < capacity && field[i] != 0) i++;
            return AnsiToUtf8(reinterpret_cast<const char*>(field), i);
        }

        /// (short)f của C# (unchecked) trên .NET Framework x64: cắt float về int32 (cvttss2si) rồi lấy 16 bit thấp;
        /// NaN hoặc ngoài khoảng int32 → 0x80000000 → 0.
        int16_t FloatToInt16LikeNetFx(float f)
        {
            int32_t i32 = (f >= -2147483648.0f && f < 2147483648.0f) ? static_cast<int32_t>(f) : INT32_MIN;
            return static_cast<int16_t>(i32);
        }
    }

    // ═════════════════════════════ IPAddress ═════════════════════════════

    std::string IPAddress::ToString() const
    {
        return std::format("{}.{}.{}.{}", (int)Bytes[0], (int)Bytes[1], (int)Bytes[2], (int)Bytes[3]);
    }

    bool IPAddress::TryParse(std::string_view ipString, IPAddress& address)
    {
        // Chép từ IPv4AddressHelper.ParseNonCanonical (.NET Framework 4.8) — IPAddress.TryParse gọi nó cho chuỗi không
        // có ':' rồi đòi phần đã đọc phải hết chuỗi.
        address = IPAddress{};
        if (ipString.find(':') != std::string_view::npos) return false;   // .NET: nhánh IPv6 — xem TODO(port) ở header

        constexpr uint64_t MaxIPv4Value = 0xFFFFFFFFull;
        uint64_t parts[4] = {};
        uint64_t currentValue = 0;
        bool atLeastOneChar = false;
        int dotCount = 0;
        size_t current = 0;
        const size_t end = ipString.size();

        for (; current < end; current++)
        {
            char ch = ipString[current];
            currentValue = 0;

            uint64_t numberBase = 10;
            if (ch == '0')
            {
                numberBase = 8;
                current++;
                atLeastOneChar = true;
                if (current < end)
                {
                    ch = ipString[current];
                    if (ch == 'x' || ch == 'X')
                    {
                        numberBase = 16;
                        current++;
                        atLeastOneChar = false;
                    }
                }
            }

            for (; current < end; current++)
            {
                ch = ipString[current];
                uint64_t digitValue;
                if ((numberBase == 10 || numberBase == 16) && '0' <= ch && ch <= '9') digitValue = (uint64_t)(ch - '0');
                else if (numberBase == 8 && '0' <= ch && ch <= '7') digitValue = (uint64_t)(ch - '0');
                else if (numberBase == 16 && 'a' <= ch && ch <= 'f') digitValue = (uint64_t)(ch + 10 - 'a');
                else if (numberBase == 16 && 'A' <= ch && ch <= 'F') digitValue = (uint64_t)(ch + 10 - 'A');
                else break;

                currentValue = currentValue * numberBase + digitValue;
                if (currentValue > MaxIPv4Value) return false;
                atLeastOneChar = true;
            }

            if (current < end && ipString[current] == '.')
            {
                // Tối đa 3 dấu chấm, không có phần rỗng ("1..1"), chỉ phần cuối được lớn hơn 255.
                if (dotCount >= 3 || !atLeastOneChar || currentValue > 0xFF) return false;
                parts[dotCount] = currentValue;
                dotCount++;
                atLeastOneChar = false;
                continue;
            }
            break;
        }

        if (!atLeastOneChar) return false;   // phần cuối rỗng: "1.1.1."
        if (current < end) return false;     // ký tự lạ / ký tự kết thúc ('/', '\\', '?', '#'): TryParse đòi hết chuỗi

        parts[dotCount] = currentValue;
        uint64_t v = 0;
        switch (dotCount)
        {
        case 0: v = parts[0]; break;
        case 1:
            if (parts[1] > 0xFFFFFF) return false;
            v = (parts[0] << 24) | (parts[1] & 0xFFFFFF);
            break;
        case 2:
            if (parts[2] > 0xFFFF) return false;
            v = (parts[0] << 24) | ((parts[1] & 0xFF) << 16) | (parts[2] & 0xFFFF);
            break;
        case 3:
            if (parts[3] > 0xFF) return false;
            v = (parts[0] << 24) | ((parts[1] & 0xFF) << 16) | ((parts[2] & 0xFF) << 8) | (parts[3] & 0xFF);
            break;
        default: return false;
        }

        address = IPAddress((uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v);
        return true;
    }

    // ═════════════════════════════ Lớp đệm ═════════════════════════════

    std::string EziMOTIONPlusELib::DRIVE_INFO::sVersion() const
    {
        return AnsiField(buffer.data() + 8, 30);
    }

    std::string EziMOTIONPlusELib::DRIVE_INFO::sMotorInfo() const
    {
        return AnsiField(buffer.data() + 42, 20);
    }

    void EziMOTIONPlusELib::AD_RESULT::copyfrom(std::span<const uint8_t> data)
    {
        std::array<uint8_t, BUFF_SIZE> array{};
        fastech_detail::CopyIn(array.data(), array.size(), data);
        int count = MAX_CHANNEL_COUNT < 0 ? 0 : (MAX_CHANNEL_COUNT > 16 ? 16 : MAX_CHANNEL_COUNT);
        for (int i = 0; i < count; i++)
        {
            size_t at = (size_t)i * 7;
            range[i] = (char16_t)array[at];
            rawdata[i] = fastech_detail::Get<int16_t>(array.data(), at + 1);
            // TODO(port): bản C# viết `converted[i] = (short)BitConverter.ToSingle(...)` — cắt mất phần thập phân của giá
            // trị đã quy đổi (bug của bản dựng lại). Giữ nguyên hành vi; máy này không dùng card AD.
            converted[i] = (float)FloatToInt16LikeNetFx(fastech_detail::Get<float>(array.data(), at + 3));
        }
    }

    // ═════════════════════════════ Nạp DLL ═════════════════════════════

    bool EziMOTIONPlusELib::Load(std::string& error)
    {
        LoaderState& s = State();
        std::lock_guard lock(s.mtx);
        if (s.module != nullptr)
        {
            error.clear();
            return true;
        }

        const std::wstring dllName = base::Widen(DllFileName);

        // 1) Cạnh exe — đúng chỗ bản C# đặt DLL (thư mục Lib\ chép ra cạnh exe khi build).
        std::filesystem::path exeDir = ModuleFile(nullptr).parent_path();
        std::filesystem::path local = exeDir / dllName;
        DWORD localErr = 0;
        HMODULE h = nullptr;
        bool localExists = false;
        if (!exeDir.empty())
        {
            std::error_code ec;
            localExists = std::filesystem::is_regular_file(local, ec);
            h = TryLoad(local.wstring(), LOAD_WITH_ALTERED_SEARCH_PATH, localErr);
        }

        // 2) Đường tìm DLL mặc định của Windows.
        DWORD searchErr = 0;
        if (h == nullptr) h = TryLoad(dllName, 0, searchErr);

        if (h == nullptr)
        {
            DWORD code = localExists ? localErr : searchErr;
            std::string text = std::format("{} {}: {} -> {}; default DLL search path -> {}",
                                           DllFileName, localExists ? "cannot be loaded" : "not found",
                                           base::PathToU8(local), DescribeWin32(localErr), DescribeWin32(searchErr));
            FailLocked(s, std::move(text), code, error);
            return false;
        }

        BindLocked(s, h);
        error.clear();
        return true;
    }

    bool EziMOTIONPlusELib::Load(const std::filesystem::path& dllPath, std::string& error)
    {
        LoaderState& s = State();
        std::lock_guard lock(s.mtx);
        if (s.module != nullptr)
        {
            error.clear();
            return true;
        }

        // LOAD_WITH_ALTERED_SEARCH_PATH cần đường dẫn tuyệt đối (DLL phụ thuộc tìm từ thư mục của chính nó).
        std::error_code ec;
        std::filesystem::path full = std::filesystem::absolute(dllPath, ec);
        if (ec) full = dllPath;

        DWORD err = 0;
        HMODULE h = TryLoad(full.wstring(), LOAD_WITH_ALTERED_SEARCH_PATH, err);
        if (h == nullptr)
        {
            FailLocked(s, std::format("{} cannot be loaded: {} -> {}", DllFileName, base::PathToU8(full), DescribeWin32(err)), err, error);
            return false;
        }

        BindLocked(s, h);
        error.clear();
        return true;
    }

    bool EziMOTIONPlusELib::IsLoaded()
    {
        return g_api.load(std::memory_order_acquire) != nullptr;
    }

    std::string EziMOTIONPlusELib::LoadError()
    {
        LoaderState& s = State();
        std::lock_guard lock(s.mtx);
        return s.error;
    }

    uint32_t EziMOTIONPlusELib::LoadWin32Error()
    {
        LoaderState& s = State();
        std::lock_guard lock(s.mtx);
        return s.win32Error;
    }

    std::filesystem::path EziMOTIONPlusELib::LoadedPath()
    {
        LoaderState& s = State();
        std::lock_guard lock(s.mtx);
        return s.path;
    }

    const std::vector<std::string_view>& EziMOTIONPlusELib::SymbolNames()
    {
        static const std::vector<std::string_view> names = {
#define SCA_EZI_NAME(name, ret, params) std::string_view(#name),
            SCA_EZI_API_LIST(SCA_EZI_NAME)
#undef SCA_EZI_NAME
        };
        return names;
    }

    bool EziMOTIONPlusELib::HasSymbol(std::string_view name)
    {
        const auto& names = SymbolNames();
        bool known = false;
        for (std::string_view n : names)
            if (n == name) { known = true; break; }
        if (!known) return false;

        LoaderState& s = State();
        std::lock_guard lock(s.mtx);
        if (s.module == nullptr) return false;
        for (const std::string& m : s.missing)
            if (m == name) return false;
        return true;
    }

    std::vector<std::string> EziMOTIONPlusELib::MissingSymbols()
    {
        LoaderState& s = State();
        std::lock_guard lock(s.mtx);
        return s.missing;
    }

    void EziMOTIONPlusELib::Unload()
    {
        LoaderState& s = State();
        std::lock_guard lock(s.mtx);
        g_api.store(nullptr, std::memory_order_release);
        if (s.module != nullptr) FreeLibrary(s.module);
        s.module = nullptr;
        s.api.reset();
        s.path.clear();
        s.missing.clear();
        s.error.clear();
        s.win32Error = 0;
    }

    // ═════════════════════════════ Connection Functions ═════════════════════════════

    int32_t EziMOTIONPlusELib::FAS_Connect(uint8_t sb1, uint8_t sb2, uint8_t sb3, uint8_t sb4, int32_t iBdID)
    {
        return Call(&Api::FAS_Connect, kFalse, sb1, sb2, sb3, sb4, iBdID);
    }

    bool EziMOTIONPlusELib::FAS_Connect(const IPAddress& ipaddr, int32_t iBdID)
    {
        int32_t num = 0;
        std::array<uint8_t, 4> addressBytes = ipaddr.GetAddressBytes();
        num = FAS_Connect(addressBytes[0], addressBytes[1], addressBytes[2], addressBytes[3], iBdID);
        return num != 0;
    }

    int32_t EziMOTIONPlusELib::FAS_ConnectTCP(uint8_t sb1, uint8_t sb2, uint8_t sb3, uint8_t sb4, int32_t iBdID)
    {
        return Call(&Api::FAS_ConnectTCP, kFalse, sb1, sb2, sb3, sb4, iBdID);
    }

    bool EziMOTIONPlusELib::FAS_ConnectTCP(const IPAddress& ipaddr, int32_t iBdID)
    {
        int32_t num = 0;
        std::array<uint8_t, 4> addressBytes = ipaddr.GetAddressBytes();
        num = FAS_ConnectTCP(addressBytes[0], addressBytes[1], addressBytes[2], addressBytes[3], iBdID);
        return num != 0;
    }

    int32_t EziMOTIONPlusELib::FAS_IsBdIDExist(int32_t iBdID, uint8_t& sb1, uint8_t& sb2, uint8_t& sb3, uint8_t& sb4)
    {
        return Call(&Api::FAS_IsBdIDExist, kFalse, iBdID, &sb1, &sb2, &sb3, &sb4);
    }

    bool EziMOTIONPlusELib::FAS_IsBdIDExist(int32_t iBdID, IPAddress& ipaddr)
    {
        int32_t num = 0;
        uint8_t sb = 0, sb2 = 0, sb3 = 0, sb4 = 0;
        num = FAS_IsBdIDExist(iBdID, sb, sb2, sb3, sb4);
        ipaddr = IPAddress(sb, sb2, sb3, sb4);
        return num != 0;
    }

    int32_t EziMOTIONPlusELib::FAS_IsIPAddressExist(uint8_t sb1, uint8_t sb2, uint8_t sb3, uint8_t sb4, int32_t& iBdID)
    {
        return Call(&Api::FAS_IsIPAddressExist, kFalse, sb1, sb2, sb3, sb4, &iBdID);
    }

    bool EziMOTIONPlusELib::FAS_IsIPAddressExist(const IPAddress& ipaddr, int32_t& iBdID)
    {
        int32_t num = 0;
        std::array<uint8_t, 4> addressBytes = ipaddr.GetAddressBytes();
        num = FAS_IsIPAddressExist(addressBytes[0], addressBytes[1], addressBytes[2], addressBytes[3], iBdID);
        return num != 0;
    }

    int32_t EziMOTIONPlusELib::FAS_Reconnect(int32_t iBdID)
    {
        return Call(&Api::FAS_Reconnect, kFalse, iBdID);
    }

    int32_t EziMOTIONPlusELib::FAS_SetAutoReconnect(int32_t bSET)
    {
        return CallVoid(&Api::FAS_SetAutoReconnect, bSET) ? 1 : 0;
    }

    void EziMOTIONPlusELib::FAS_Close(int32_t iBdID)
    {
        CallVoid(&Api::FAS_Close, iBdID);
    }

    int32_t EziMOTIONPlusELib::FAS_IsSlaveExist(int32_t iBdID)
    {
        return Call(&Api::FAS_IsSlaveExist, kFalse, iBdID);
    }

    // ═════════════════════════════ Log Functions ═════════════════════════════

    void EziMOTIONPlusELib::FAS_EnableLog(int32_t bEnable)
    {
        CallVoid(&Api::FAS_EnableLog, bEnable);
    }

    void EziMOTIONPlusELib::FAS_SetLogLevel(int32_t level)
    {
        CallVoid(&Api::FAS_SetLogLevel, level);
    }

    void EziMOTIONPlusELib::FAS_SetLogLevel(LOG_LEVEL level)
    {
        switch (level)
        {
        case LOG_LEVEL::LOG_LEVEL_COMM:
            FAS_SetLogLevel(0);
            break;
        case LOG_LEVEL::LOG_LEVEL_PARAM:
            FAS_SetLogLevel(1);
            break;
        case LOG_LEVEL::LOG_LEVEL_MOTION:
            FAS_SetLogLevel(2);
            break;
        default:
            FAS_SetLogLevel(3);
            break;
        }
    }

    int32_t EziMOTIONPlusELib::FAS_SetLogPath(const wchar_t* lpPath)
    {
        return Call(&Api::FAS_SetLogPath, kFalse, lpPath);
    }

    int32_t EziMOTIONPlusELib::FAS_SetLogPath(const std::string& lpPath)
    {
        std::wstring w = base::Widen(lpPath);
        return FAS_SetLogPath(w.c_str());
    }

    int32_t EziMOTIONPlusELib::FAS_PrintCustomLog(int32_t iBdID, int32_t level, const wchar_t* lpszMsg)
    {
        return CallVoid(&Api::FAS_PrintCustomLog, iBdID, level, lpszMsg) ? 1 : 0;
    }

    int32_t EziMOTIONPlusELib::FAS_PrintCustomLog(int32_t iBdID, LOG_LEVEL level, const std::string& lpszMsg)
    {
        std::wstring w = base::Widen(lpszMsg);
        switch (level)
        {
        case LOG_LEVEL::LOG_LEVEL_COMM: return FAS_PrintCustomLog(iBdID, 0, w.c_str());
        case LOG_LEVEL::LOG_LEVEL_PARAM: return FAS_PrintCustomLog(iBdID, 1, w.c_str());
        case LOG_LEVEL::LOG_LEVEL_MOTION: return FAS_PrintCustomLog(iBdID, 2, w.c_str());
        default: return FAS_PrintCustomLog(iBdID, 3, w.c_str());
        }
    }

    // ═════════════════════════════ Ethernet Address Functions ═════════════════════════════

    int32_t EziMOTIONPlusELib::FAS_GetEthernetAddr(int32_t iBdID, uint32_t& gateway, uint32_t& subnet, uint32_t& ip)
    {
        return Call(&Api::FAS_GetEthernetAddr, kFail, iBdID, &gateway, &subnet, &ip);
    }

    int32_t EziMOTIONPlusELib::FAS_SetEthernetAddr(int32_t iBdID, uint32_t gateway, uint32_t subnet, uint32_t ip)
    {
        return Call(&Api::FAS_SetEthernetAddr, kFail, iBdID, gateway, subnet, ip);
    }

    int32_t EziMOTIONPlusELib::FAS_GetMACAddress(int32_t iBdID, uint64_t& MACAddress)
    {
        return Call(&Api::FAS_GetMACAddress, kFail, iBdID, &MACAddress);
    }

    // ═════════════════════════════ Info Functions ═════════════════════════════

    int32_t EziMOTIONPlusELib::FAS_GetSlaveInfo(int32_t iBdID, uint8_t& pType, char* lpBuff, int32_t nBuffSize)
    {
        return Call(&Api::FAS_GetSlaveInfo, kFail, iBdID, &pType, lpBuff, nBuffSize);
    }

    int32_t EziMOTIONPlusELib::FAS_GetSlaveInfo(int32_t iBdID, uint8_t& pType, std::string& version)
    {
        int32_t num = 0;
        uint8_t pType2 = 0;
        char stringBuilder[257] = {};   // StringBuilder(256) + chỗ cho '\0' chặn cuối
        num = FAS_GetSlaveInfo(iBdID, pType2, stringBuilder, 256);
        if (num == 0)
        {
            pType = pType2;
            version = AnsiField(reinterpret_cast<const uint8_t*>(stringBuilder), 256);
        }
        return num;
    }

    int32_t EziMOTIONPlusELib::FAS_GetMotorInfo(int32_t iBdID, uint8_t& pType, char* lpBuff, int32_t nBuffSize)
    {
        return Call(&Api::FAS_GetMotorInfo, kFail, iBdID, &pType, lpBuff, nBuffSize);
    }

    int32_t EziMOTIONPlusELib::FAS_GetMotorInfo(int32_t iBdID, uint8_t& pType, std::string& motor)
    {
        int32_t num = 0;
        uint8_t pType2 = 0;
        char stringBuilder[257] = {};
        num = FAS_GetMotorInfo(iBdID, pType2, stringBuilder, 256);
        if (num == 0)
        {
            pType = pType2;
            motor = AnsiField(reinterpret_cast<const uint8_t*>(stringBuilder), 256);
        }
        return num;
    }

    int32_t EziMOTIONPlusELib::FAS_GetSlaveInfoEx(int32_t iBdID, uint8_t* lpDriveInfo)
    {
        return Call(&Api::FAS_GetSlaveInfoEx, kFail, iBdID, lpDriveInfo);
    }

    int32_t EziMOTIONPlusELib::FAS_GetSlaveInfoEx(int32_t iBdID, DRIVE_INFO& DriveInfo)
    {
        int32_t num = 0;
        std::array<uint8_t, DRIVE_INFO::BUFF_SIZE> array{};
        num = FAS_GetSlaveInfoEx(iBdID, array.data());
        if (num == 0)
            DriveInfo.copy(array);
        return num;
    }

    // ═════════════════════════════ Parameter Functions ═════════════════════════════

    int32_t EziMOTIONPlusELib::FAS_SaveAllParameters(int32_t iBdID)
    {
        return Call(&Api::FAS_SaveAllParameters, kFail, iBdID);
    }

    int32_t EziMOTIONPlusELib::FAS_SetParameter(int32_t iBdID, uint8_t iParamNo, int32_t lParamValue)
    {
        return Call(&Api::FAS_SetParameter, kFail, iBdID, iParamNo, lParamValue);
    }

    int32_t EziMOTIONPlusELib::FAS_GetParameter(int32_t iBdID, uint8_t iParamNo, int32_t& lParamValue)
    {
        return Call(&Api::FAS_GetParameter, kFail, iBdID, iParamNo, &lParamValue);
    }

    int32_t EziMOTIONPlusELib::FAS_GetROMParameter(int32_t iBdID, uint8_t iParamNo, int32_t& lRomParam)
    {
        return Call(&Api::FAS_GetROMParameter, kFail, iBdID, iParamNo, &lRomParam);
    }

    // ═════════════════════════════ IO Functions (driver) ═════════════════════════════

    int32_t EziMOTIONPlusELib::FAS_SetIOInput(int32_t iBdID, uint32_t dwIOSETMask, uint32_t dwIOCLRMask)
    {
        return Call(&Api::FAS_SetIOInput, kFail, iBdID, dwIOSETMask, dwIOCLRMask);
    }

    int32_t EziMOTIONPlusELib::FAS_GetIOInput(int32_t iBdID, uint32_t& dwIOInput)
    {
        return Call(&Api::FAS_GetIOInput, kFail, iBdID, &dwIOInput);
    }

    int32_t EziMOTIONPlusELib::FAS_SetIOOutput(int32_t iBdID, uint32_t dwIOSETMask, uint32_t dwIOCLRMask)
    {
        return Call(&Api::FAS_SetIOOutput, kFail, iBdID, dwIOSETMask, dwIOCLRMask);
    }

    int32_t EziMOTIONPlusELib::FAS_GetIOOutput(int32_t iBdID, uint32_t& dwIOOutput)
    {
        return Call(&Api::FAS_GetIOOutput, kFail, iBdID, &dwIOOutput);
    }

    int32_t EziMOTIONPlusELib::FAS_GetIOAssignMap(int32_t iBdID, uint8_t iIOPinNo, uint32_t& dwIOLogicMask, uint8_t& bLevel)
    {
        return Call(&Api::FAS_GetIOAssignMap, kFail, iBdID, iIOPinNo, &dwIOLogicMask, &bLevel);
    }

    int32_t EziMOTIONPlusELib::FAS_SetIOAssignMap(int32_t iBdID, uint8_t iIOPinNo, uint32_t dwIOLogicMask, uint8_t bLevel)
    {
        return Call(&Api::FAS_SetIOAssignMap, kFail, iBdID, iIOPinNo, dwIOLogicMask, bLevel);
    }

    int32_t EziMOTIONPlusELib::FAS_IOAssignMapReadROM(int32_t iBdID)
    {
        return Call(&Api::FAS_IOAssignMapReadROM, kFail, iBdID);
    }

    // ═════════════════════════════ Servo Driver Control Functions ═════════════════════════════

    int32_t EziMOTIONPlusELib::FAS_ServoEnable(int32_t iBdID, int32_t bOnOff)
    {
        return Call(&Api::FAS_ServoEnable, kFail, iBdID, bOnOff);
    }

    int32_t EziMOTIONPlusELib::FAS_ServoAlarmReset(int32_t iBdID)
    {
        return Call(&Api::FAS_ServoAlarmReset, kFail, iBdID);
    }

    int32_t EziMOTIONPlusELib::FAS_StepAlarmReset(int32_t iBdID, int32_t bReset)
    {
        return Call(&Api::FAS_StepAlarmReset, kFail, iBdID, bReset);
    }

    int32_t EziMOTIONPlusELib::FAS_BrakeSet(int32_t iBdID, int32_t bSet, int32_t& nResult)
    {
        return Call(&Api::FAS_BrakeSet, kFail, iBdID, bSet, &nResult);
    }

    // ═════════════════════════════ Read Status and Position ═════════════════════════════

    int32_t EziMOTIONPlusELib::FAS_GetAxisStatus(int32_t iBdID, uint32_t& dwAxisStatus)
    {
        return Call(&Api::FAS_GetAxisStatus, kFail, iBdID, &dwAxisStatus);
    }

    int32_t EziMOTIONPlusELib::FAS_GetIOAxisStatus(int32_t iBdID, uint32_t& dwInStatus, uint32_t& dwOutStatus, uint32_t& dwAxisStatus)
    {
        return Call(&Api::FAS_GetIOAxisStatus, kFail, iBdID, &dwInStatus, &dwOutStatus, &dwAxisStatus);
    }

    int32_t EziMOTIONPlusELib::FAS_GetMotionStatus(int32_t iBdID, int32_t& lCmdPos, int32_t& lActPos, int32_t& lPosErr, int32_t& lActVel, uint16_t& wPosItemNo)
    {
        return Call(&Api::FAS_GetMotionStatus, kFail, iBdID, &lCmdPos, &lActPos, &lPosErr, &lActVel, &wPosItemNo);
    }

    int32_t EziMOTIONPlusELib::FAS_GetAllStatus(int32_t iBdID, uint32_t& dwInStatus, uint32_t& dwOutStatus, uint32_t& dwAxisStatus,
                                                int32_t& lCmdPos, int32_t& lActPos, int32_t& lPosErr, int32_t& lActVel, uint16_t& wPosItemNo)
    {
        return Call(&Api::FAS_GetAllStatus, kFail, iBdID, &dwInStatus, &dwOutStatus, &dwAxisStatus, &lCmdPos, &lActPos, &lPosErr, &lActVel, &wPosItemNo);
    }

    int32_t EziMOTIONPlusELib::FAS_GetAllStatusEx(int32_t iBdID, uint8_t* pTypes, int32_t* pDatas)
    {
        return Call(&Api::FAS_GetAllStatusEx, kFail, iBdID, pTypes, pDatas);
    }

    int32_t EziMOTIONPlusELib::FAS_SetCommandPos(int32_t iBdID, int32_t lCmdPos)
    {
        return Call(&Api::FAS_SetCommandPos, kFail, iBdID, lCmdPos);
    }

    int32_t EziMOTIONPlusELib::FAS_SetActualPos(int32_t iBdID, int32_t lActPos)
    {
        return Call(&Api::FAS_SetActualPos, kFail, iBdID, lActPos);
    }

    int32_t EziMOTIONPlusELib::FAS_ClearPosition(int32_t iBdID)
    {
        return Call(&Api::FAS_ClearPosition, kFail, iBdID);
    }

    int32_t EziMOTIONPlusELib::FAS_GetCommandPos(int32_t iBdID, int32_t& lCmdPos)
    {
        return Call(&Api::FAS_GetCommandPos, kFail, iBdID, &lCmdPos);
    }

    int32_t EziMOTIONPlusELib::FAS_GetActualPos(int32_t iBdID, int32_t& lActPos)
    {
        return Call(&Api::FAS_GetActualPos, kFail, iBdID, &lActPos);
    }

    int32_t EziMOTIONPlusELib::FAS_GetPosError(int32_t iBdID, int32_t& lPosErr)
    {
        return Call(&Api::FAS_GetPosError, kFail, iBdID, &lPosErr);
    }

    int32_t EziMOTIONPlusELib::FAS_GetActualVel(int32_t iBdID, int32_t& lActVel)
    {
        return Call(&Api::FAS_GetActualVel, kFail, iBdID, &lActVel);
    }

    int32_t EziMOTIONPlusELib::FAS_GetAlarmType(int32_t iBdID, uint8_t& nAlarmType)
    {
        return Call(&Api::FAS_GetAlarmType, kFail, iBdID, &nAlarmType);
    }

    int32_t EziMOTIONPlusELib::FAS_GetWarningType(int32_t iBdID, uint8_t& nWarningType)
    {
        return Call(&Api::FAS_GetWarningType, kFail, iBdID, &nWarningType);
    }

    // ═════════════════════════════ Motion Functions ═════════════════════════════

    int32_t EziMOTIONPlusELib::FAS_MoveStop(int32_t iBdID)
    {
        return Call(&Api::FAS_MoveStop, kFail, iBdID);
    }

    int32_t EziMOTIONPlusELib::FAS_EmergencyStop(int32_t iBdID)
    {
        return Call(&Api::FAS_EmergencyStop, kFail, iBdID);
    }

    int32_t EziMOTIONPlusELib::FAS_MovePause(int32_t iBdID, int32_t bPause)
    {
        return Call(&Api::FAS_MovePause, kFail, iBdID, bPause);
    }

    int32_t EziMOTIONPlusELib::FAS_MoveOriginSingleAxis(int32_t iBdID)
    {
        return Call(&Api::FAS_MoveOriginSingleAxis, kFail, iBdID);
    }

    int32_t EziMOTIONPlusELib::FAS_MoveSingleAxisAbsPos(int32_t iBdID, int32_t lAbsPos, uint32_t lVelocity)
    {
        return Call(&Api::FAS_MoveSingleAxisAbsPos, kFail, iBdID, lAbsPos, lVelocity);
    }

    int32_t EziMOTIONPlusELib::FAS_MoveSingleAxisIncPos(int32_t iBdID, int32_t lIncPos, uint32_t lVelocity)
    {
        return Call(&Api::FAS_MoveSingleAxisIncPos, kFail, iBdID, lIncPos, lVelocity);
    }

    int32_t EziMOTIONPlusELib::FAS_MoveToLimit(int32_t iBdID, uint32_t lVelocity, int32_t iLimitDir)
    {
        return Call(&Api::FAS_MoveToLimit, kFail, iBdID, lVelocity, iLimitDir);
    }

    int32_t EziMOTIONPlusELib::FAS_MoveVelocity(int32_t iBdID, uint32_t lVelocity, int32_t iVelDir)
    {
        return Call(&Api::FAS_MoveVelocity, kFail, iBdID, lVelocity, iVelDir);
    }

    int32_t EziMOTIONPlusELib::FAS_PositionAbsOverride(int32_t iBdID, int32_t lOverridePos)
    {
        return Call(&Api::FAS_PositionAbsOverride, kFail, iBdID, lOverridePos);
    }

    int32_t EziMOTIONPlusELib::FAS_PositionIncOverride(int32_t iBdID, int32_t lOverridePos)
    {
        return Call(&Api::FAS_PositionIncOverride, kFail, iBdID, lOverridePos);
    }

    int32_t EziMOTIONPlusELib::FAS_VelocityOverride(int32_t iBdID, uint32_t lVelocity)
    {
        return Call(&Api::FAS_VelocityOverride, kFail, iBdID, lVelocity);
    }

    int32_t EziMOTIONPlusELib::FAS_MoveLinearAbsPos(uint8_t nNoOfBds, int32_t* iBdID, int32_t* lplAbsPos, uint32_t lFeedrate, uint16_t wAccelTime)
    {
        return Call(&Api::FAS_MoveLinearAbsPos, kFail, nNoOfBds, iBdID, lplAbsPos, lFeedrate, wAccelTime);
    }

    int32_t EziMOTIONPlusELib::FAS_MoveLinearIncPos(uint8_t nNoOfBds, int32_t* iBdID, int32_t* lplIncPos, uint32_t lFeedrate, uint16_t wAccelTime)
    {
        return Call(&Api::FAS_MoveLinearIncPos, kFail, nNoOfBds, iBdID, lplIncPos, lFeedrate, wAccelTime);
    }

    int32_t EziMOTIONPlusELib::FAS_MoveLinearAbsPos2(uint8_t nNoOfBds, int32_t* iBdID, int32_t* lplAbsPos, uint32_t lFeedrate, uint16_t wAccelTime)
    {
        return Call(&Api::FAS_MoveLinearAbsPos2, kFail, nNoOfBds, iBdID, lplAbsPos, lFeedrate, wAccelTime);
    }

    int32_t EziMOTIONPlusELib::FAS_MoveLinearIncPos2(uint8_t nNoOfBds, int32_t* iBdID, int32_t* lplIncPos, uint32_t lFeedrate, uint16_t wAccelTime)
    {
        return Call(&Api::FAS_MoveLinearIncPos2, kFail, nNoOfBds, iBdID, lplIncPos, lFeedrate, wAccelTime);
    }

    int32_t EziMOTIONPlusELib::FAS_MoveCircleAbsPos1(uint8_t nNoOfBds, int32_t* iBdID, int32_t* lplCirEndAbs, int32_t* lplCirCenterAbs, int32_t iDirection, uint32_t lFeedrate, uint16_t wAccelTime, int32_t bSCurve)
    {
        return Call(&Api::FAS_MoveCircleAbsPos1, kFail, nNoOfBds, iBdID, lplCirEndAbs, lplCirCenterAbs, iDirection, lFeedrate, wAccelTime, bSCurve);
    }

    int32_t EziMOTIONPlusELib::FAS_MoveCircleIncPos1(uint8_t nNoOfBds, int32_t* iBdID, int32_t* lplCirEndInc, int32_t* lplCirCenterInc, int32_t iDirection, uint32_t lFeedrate, uint16_t wAccelTime, int32_t bSCurve)
    {
        return Call(&Api::FAS_MoveCircleIncPos1, kFail, nNoOfBds, iBdID, lplCirEndInc, lplCirCenterInc, iDirection, lFeedrate, wAccelTime, bSCurve);
    }

    int32_t EziMOTIONPlusELib::FAS_MoveCircleAbsPos2(uint8_t nNoOfBds, int32_t* iBdID, int32_t* lplCirEndAbs, uint32_t lRadius, int32_t iDirection, uint32_t lFeedrate, uint16_t wAccelTime, int32_t bSCurve)
    {
        return Call(&Api::FAS_MoveCircleAbsPos2, kFail, nNoOfBds, iBdID, lplCirEndAbs, lRadius, iDirection, lFeedrate, wAccelTime, bSCurve);
    }

    int32_t EziMOTIONPlusELib::FAS_MoveCircleIncPos2(uint8_t nNoOfBds, int32_t* iBdID, int32_t* lplCirEndInc, uint32_t lRadius, int32_t iDirection, uint32_t lFeedrate, uint16_t wAccelTime, int32_t bSCurve)
    {
        return Call(&Api::FAS_MoveCircleIncPos2, kFail, nNoOfBds, iBdID, lplCirEndInc, lRadius, iDirection, lFeedrate, wAccelTime, bSCurve);
    }

    int32_t EziMOTIONPlusELib::FAS_MoveCircleAbsPos3(uint8_t nNoOfBds, int32_t* iBdID, int32_t* lplCirCenterAbs, uint32_t nAngle, int32_t iDirecion, uint32_t lFeedrate, uint16_t wAccelTime, int32_t bSCurve)
    {
        return Call(&Api::FAS_MoveCircleAbsPos3, kFail, nNoOfBds, iBdID, lplCirCenterAbs, nAngle, iDirecion, lFeedrate, wAccelTime, bSCurve);
    }

    int32_t EziMOTIONPlusELib::FAS_MoveCircleIncPos3(uint8_t nNoOfBds, int32_t* iBdID, int32_t* lplCirCenterInc, uint32_t nAngle, int32_t iDirecion, uint32_t lFeedrate, uint16_t wAccelTime, int32_t bSCurve)
    {
        return Call(&Api::FAS_MoveCircleIncPos3, kFail, nNoOfBds, iBdID, lplCirCenterInc, nAngle, iDirecion, lFeedrate, wAccelTime, bSCurve);
    }

    int32_t EziMOTIONPlusELib::FAS_TriggerOutput_RunA(int32_t iBdID, int32_t bStartTrigger, int32_t lStartPos, uint32_t dwPeriod, uint32_t dwPulseTime)
    {
        return Call(&Api::FAS_TriggerOutput_RunA, kFail, iBdID, bStartTrigger, lStartPos, dwPeriod, dwPulseTime);
    }

    int32_t EziMOTIONPlusELib::FAS_TriggerOutput_Status(int32_t iBdID, uint8_t& bTriggerStatus)
    {
        return Call(&Api::FAS_TriggerOutput_Status, kFail, iBdID, &bTriggerStatus);
    }

    int32_t EziMOTIONPlusELib::FAS_SetTriggerOutputEx(int32_t iBdID, uint8_t nOutputNo, uint8_t bRun, uint16_t wOnTime, uint8_t nTriggerCount, int32_t* arrTriggerPosition)
    {
        return Call(&Api::FAS_SetTriggerOutputEx, kFail, iBdID, nOutputNo, bRun, wOnTime, nTriggerCount, arrTriggerPosition);
    }

    int32_t EziMOTIONPlusELib::FAS_GetTriggerOutputEx(int32_t iBdID, uint8_t nOutputNo, uint8_t& bRun, uint16_t& wOnTime, uint8_t& nTriggerCount, int32_t* arrTriggerPosition)
    {
        return Call(&Api::FAS_GetTriggerOutputEx, kFail, iBdID, nOutputNo, &bRun, &wOnTime, &nTriggerCount, arrTriggerPosition);
    }

    int32_t EziMOTIONPlusELib::FAS_MovePush(int32_t iBdID, uint32_t dwStartSpd, uint32_t dwMoveSpd, int32_t lPosition, uint16_t wAccel, uint16_t wDecel,
                                            uint16_t wPushRate, uint32_t dwPushSpd, int32_t lEndPosition, uint16_t wPushMode)
    {
        return Call(&Api::FAS_MovePush, kFail, iBdID, dwStartSpd, dwMoveSpd, lPosition, wAccel, wDecel, wPushRate, dwPushSpd, lEndPosition, wPushMode);
    }

    int32_t EziMOTIONPlusELib::FAS_GetPushStatus(int32_t iBdID, uint8_t& nPushStatus)
    {
        return Call(&Api::FAS_GetPushStatus, kFail, iBdID, &nPushStatus);
    }

    // ═════════════════════════════ Ex-Motion Functions ═════════════════════════════

    int32_t EziMOTIONPlusELib::FAS_MoveSingleAxisAbsPosEx(int32_t iBdID, int32_t lAbsPos, uint32_t lVelocity, uint8_t* lpExOption)
    {
        return Call(&Api::FAS_MoveSingleAxisAbsPosEx, kFail, iBdID, lAbsPos, lVelocity, lpExOption);
    }

    int32_t EziMOTIONPlusELib::FAS_MoveSingleAxisAbsPosEx(int32_t iBdID, int32_t lAbsPos, uint32_t lVelocity, const MOTION_OPTION_EX& ExOption)
    {
        std::array<uint8_t, MOTION_OPTION_EX::BUFF_SIZE> array{};
        ExOption.copyto(array);
        return FAS_MoveSingleAxisAbsPosEx(iBdID, lAbsPos, lVelocity, array.data());
    }

    int32_t EziMOTIONPlusELib::FAS_MoveSingleAxisIncPosEx(int32_t iBdID, int32_t lIncPos, uint32_t lVelocity, uint8_t* lpExOption)
    {
        return Call(&Api::FAS_MoveSingleAxisIncPosEx, kFail, iBdID, lIncPos, lVelocity, lpExOption);
    }

    int32_t EziMOTIONPlusELib::FAS_MoveSingleAxisIncPosEx(int32_t iBdID, int32_t lAbsPos, uint32_t lVelocity, const MOTION_OPTION_EX& ExOption)
    {
        std::array<uint8_t, MOTION_OPTION_EX::BUFF_SIZE> array{};
        ExOption.copyto(array);
        return FAS_MoveSingleAxisIncPosEx(iBdID, lAbsPos, lVelocity, array.data());
    }

    int32_t EziMOTIONPlusELib::FAS_MoveVelocityEx(int32_t iBdID, uint32_t lVelocity, int32_t iVelDir, uint8_t* lpExOption)
    {
        return Call(&Api::FAS_MoveVelocityEx, kFail, iBdID, lVelocity, iVelDir, lpExOption);
    }

    int32_t EziMOTIONPlusELib::FAS_MoveVelocityEx(int32_t iBdID, uint32_t lVelocity, int32_t iVelDir, const VELOCITY_OPTION_EX& ExOption)
    {
        std::array<uint8_t, VELOCITY_OPTION_EX::BUFF_SIZE> array{};
        ExOption.copyto(array);
        return FAS_MoveVelocityEx(iBdID, lVelocity, iVelDir, array.data());
    }

    // ═════════════════════════════ Position Table Functions ═════════════════════════════

    int32_t EziMOTIONPlusELib::FAS_PosTableReadItem(int32_t iBdID, uint16_t wItemNo, uint8_t* lpItem)
    {
        return Call(&Api::FAS_PosTableReadItem, kFail, iBdID, wItemNo, lpItem);
    }

    int32_t EziMOTIONPlusELib::FAS_PosTableReadItem(int32_t iBdID, uint16_t wItemNo, ITEM_NODE& Item)
    {
        int32_t num = 0;
        std::array<uint8_t, ITEM_NODE::BUFF_SIZE> array{};
        num = FAS_PosTableReadItem(iBdID, wItemNo, array.data());
        if (num == 0)
            Item.copyfrom(array);
        return num;
    }

    int32_t EziMOTIONPlusELib::FAS_PosTableWriteItem(int32_t iBdID, uint16_t wItemNo, uint8_t* lpItem)
    {
        return Call(&Api::FAS_PosTableWriteItem, kFail, iBdID, wItemNo, lpItem);
    }

    int32_t EziMOTIONPlusELib::FAS_PosTableWriteItem(int32_t iBdID, uint16_t wItemNo, const ITEM_NODE& Item)
    {
        std::array<uint8_t, ITEM_NODE::BUFF_SIZE> array{};
        Item.copyto(array);
        return FAS_PosTableWriteItem(iBdID, wItemNo, array.data());
    }

    int32_t EziMOTIONPlusELib::FAS_PosTableWriteROM(int32_t iBdID)
    {
        return Call(&Api::FAS_PosTableWriteROM, kFail, iBdID);
    }

    int32_t EziMOTIONPlusELib::FAS_PosTableReadROM(int32_t iBdID)
    {
        return Call(&Api::FAS_PosTableReadROM, kFail, iBdID);
    }

    int32_t EziMOTIONPlusELib::FAS_PosTableRunItem(int32_t iBdID, uint16_t wItemNo)
    {
        return Call(&Api::FAS_PosTableRunItem, kFail, iBdID, wItemNo);
    }

    int32_t EziMOTIONPlusELib::FAS_PosTableReadOneItem(int32_t iBdID, uint16_t wItemNo, uint16_t wOffset, int32_t& lPosItemVal)
    {
        return Call(&Api::FAS_PosTableReadOneItem, kFail, iBdID, wItemNo, wOffset, &lPosItemVal);
    }

    int32_t EziMOTIONPlusELib::FAS_PosTableWriteOneItem(int32_t iBdID, uint16_t wItemNo, uint16_t wOffset, int32_t lPosItemVal)
    {
        return Call(&Api::FAS_PosTableWriteOneItem, kFail, iBdID, wItemNo, wOffset, lPosItemVal);
    }

    int32_t EziMOTIONPlusELib::FAS_PosTableSingleRunItem(int32_t iBdID, int32_t bNextMove, uint16_t wItemNo)
    {
        return Call(&Api::FAS_PosTableSingleRunItem, kFail, iBdID, bNextMove, wItemNo);
    }

    // ═════════════════════════════ Gap Control Functions ═════════════════════════════

    int32_t EziMOTIONPlusELib::FAS_GapControlEnable(int32_t iBdID, uint16_t wItemNo, int32_t lGapCompSpeed, int32_t lGapAccTime, int32_t lGapDecTime, int32_t lGapStartSpeed)
    {
        return Call(&Api::FAS_GapControlEnable, kFail, iBdID, wItemNo, lGapCompSpeed, lGapAccTime, lGapDecTime, lGapStartSpeed);
    }

    int32_t EziMOTIONPlusELib::FAS_GapControlDisable(int32_t iBdID)
    {
        return Call(&Api::FAS_GapControlDisable, kFail, iBdID);
    }

    int32_t EziMOTIONPlusELib::FAS_IsGapControlEnable(int32_t iBdID, int32_t& bIsEnable, uint16_t& wCurrentItemNo)
    {
        return Call(&Api::FAS_IsGapControlEnable, kFail, iBdID, &bIsEnable, &wCurrentItemNo);
    }

    int32_t EziMOTIONPlusELib::FAS_GapControlGetADCValue(int32_t iBdID, int32_t& lADCValue)
    {
        return Call(&Api::FAS_GapControlGetADCValue, kFail, iBdID, &lADCValue);
    }

    int32_t EziMOTIONPlusELib::FAS_GapOneResultMonitor(int32_t iBdID, uint8_t& bUpdated, int32_t& iIndex, int32_t& lGapValue, int32_t& lCmdPos,
                                                       int32_t& lActPos, int32_t& lCompValue, int32_t& lReserved)
    {
        return Call(&Api::FAS_GapOneResultMonitor, kFail, iBdID, &bUpdated, &iIndex, &lGapValue, &lCmdPos, &lActPos, &lCompValue, &lReserved);
    }

    // ═════════════════════════════ Alarm Type History Functions ═════════════════════════════

    int32_t EziMOTIONPlusELib::FAS_GetAlarmLogs(int32_t iBdID, uint8_t* pAlarmLog)
    {
        return Call(&Api::FAS_GetAlarmLogs, kFail, iBdID, pAlarmLog);
    }

    int32_t EziMOTIONPlusELib::FAS_GetAlarmLogs(int32_t iBdID, ALARM_LOG& AlarmLog)
    {
        int32_t num = 0;
        std::array<uint8_t, ALARM_LOG::BUFF_SIZE> array{};
        num = FAS_GetAlarmLogs(iBdID, array.data());
        if (num == 0)
            AlarmLog.copy(array);
        return num;
    }

    int32_t EziMOTIONPlusELib::FAS_ResetAlarmLogs(int32_t iBdID)
    {
        return Call(&Api::FAS_ResetAlarmLogs, kFail, iBdID);
    }

    int32_t EziMOTIONPlusELib::FAS_GetWarningLogs(int32_t iBdID, uint8_t* pWarningLog)
    {
        return Call(&Api::FAS_GetWarningLogs, kFail, iBdID, pWarningLog);
    }

    int32_t EziMOTIONPlusELib::FAS_GetWarningLogs(int32_t iBdID, ALARM_LOG& WarningLog)
    {
        int32_t num = 0;
        std::array<uint8_t, ALARM_LOG::BUFF_SIZE> array{};
        num = FAS_GetWarningLogs(iBdID, array.data());
        if (num == 0)
            WarningLog.copy(array);
        return num;
    }

    int32_t EziMOTIONPlusELib::FAS_ResetWarningLogs(int32_t iBdID)
    {
        return Call(&Api::FAS_ResetWarningLogs, kFail, iBdID);
    }

    // ═════════════════════════════ I/O Module Functions (Ezi-IO) ═════════════════════════════

    int32_t EziMOTIONPlusELib::FAS_GetInput(int32_t iBdID, uint32_t& uInput, uint32_t& uLatch)
    {
        return Call(&Api::FAS_GetInput, kFail, iBdID, &uInput, &uLatch);
    }

    int32_t EziMOTIONPlusELib::FAS_ClearLatch(int32_t iBdID, uint32_t uLatchMask)
    {
        return Call(&Api::FAS_ClearLatch, kFail, iBdID, uLatchMask);
    }

    int32_t EziMOTIONPlusELib::FAS_GetLatchCount(int32_t iBdID, uint32_t iInputNo, uint32_t& uCount)
    {
        return Call(&Api::FAS_GetLatchCount, kFail, iBdID, static_cast<uint8_t>(iInputNo), &uCount);
    }

    int32_t EziMOTIONPlusELib::FAS_GetLatchCountAll(int32_t iBdID, uint32_t* puAllCount)
    {
        return Call(&Api::FAS_GetLatchCountAll, kFail, iBdID, puAllCount);
    }

    int32_t EziMOTIONPlusELib::FAS_GetLatchCountAll32(int32_t iBdID, uint32_t* puAllCount)
    {
        return Call(&Api::FAS_GetLatchCountAll32, kFail, iBdID, puAllCount);
    }

    int32_t EziMOTIONPlusELib::FAS_ClearLatchCount(int32_t iBdID, uint32_t uInputMask)
    {
        return Call(&Api::FAS_ClearLatchCount, kFail, iBdID, uInputMask);
    }

    int32_t EziMOTIONPlusELib::FAS_GetOutput(int32_t iBdID, uint32_t& uOutput, uint32_t& uStatus)
    {
        return Call(&Api::FAS_GetOutput, kFail, iBdID, &uOutput, &uStatus);
    }

    int32_t EziMOTIONPlusELib::FAS_SetOutput(int32_t iBdID, uint32_t uSetMask, uint32_t uClearMask)
    {
        return Call(&Api::FAS_SetOutput, kFail, iBdID, uSetMask, uClearMask);
    }

    int32_t EziMOTIONPlusELib::FAS_SetTrigger(int32_t iBdID, uint8_t uOutputNo, uint8_t* pTrigger)
    {
        return Call(&Api::FAS_SetTrigger, kFail, iBdID, uOutputNo, pTrigger);
    }

    int32_t EziMOTIONPlusELib::FAS_SetTrigger(int32_t iBdID, uint8_t uOutputNo, TRIGGER_INFO& trigger)
    {
        int32_t num = 0;
        num = FAS_SetTrigger(iBdID, uOutputNo, trigger.ByteArray().data());
        return num;
    }

    int32_t EziMOTIONPlusELib::FAS_SetRunStop(int32_t iBdID, uint32_t uRunMask, uint32_t uStopMask)
    {
        return Call(&Api::FAS_SetRunStop, kFail, iBdID, uRunMask, uStopMask);
    }

    int32_t EziMOTIONPlusELib::FAS_GetTriggerCount(int32_t iBdID, uint32_t uOutputNo, uint32_t& uCount)
    {
        return Call(&Api::FAS_GetTriggerCount, kFail, iBdID, static_cast<uint8_t>(uOutputNo), &uCount);
    }

    int32_t EziMOTIONPlusELib::FAS_GetIOLevel(int32_t iBdID, uint32_t& uIOLevel)
    {
        return Call(&Api::FAS_GetIOLevel, kFail, iBdID, &uIOLevel);
    }

    int32_t EziMOTIONPlusELib::FAS_SetIOLevel(int32_t iBdID, uint32_t uIOLevel)
    {
        return Call(&Api::FAS_SetIOLevel, kFail, iBdID, uIOLevel);
    }

    int32_t EziMOTIONPlusELib::FAS_LoadIOLevel(int32_t iBdID)
    {
        return Call(&Api::FAS_LoadIOLevel, kFail, iBdID);
    }

    int32_t EziMOTIONPlusELib::FAS_SaveIOLevel(int32_t iBdID)
    {
        return Call(&Api::FAS_SaveIOLevel, kFail, iBdID);
    }

    int32_t EziMOTIONPlusELib::FAS_GetInputFilter(int32_t iBdID, uint16_t& filter)
    {
        return Call(&Api::FAS_GetInputFilter, kFail, iBdID, &filter);
    }

    int32_t EziMOTIONPlusELib::FAS_SetInputFilter(int32_t iBdID, uint16_t filter)
    {
        return Call(&Api::FAS_SetInputFilter, kFail, iBdID, filter);
    }

    int32_t EziMOTIONPlusELib::FAS_GetIODirection(int32_t iBdID, uint32_t& direction)
    {
        return Call(&Api::FAS_GetIODirection, kFail, iBdID, &direction);
    }

    int32_t EziMOTIONPlusELib::FAS_SetIODirection(int32_t iBdID, uint32_t direction)
    {
        return Call(&Api::FAS_SetIODirection, kFail, iBdID, direction);
    }

    // ═════════════════════════════ Ezi-IO AD Functions ═════════════════════════════

    int32_t EziMOTIONPlusELib::FAS_SetADConfig(int32_t iBdID, uint8_t channel, int32_t type, int32_t value, int32_t& recv)
    {
        return Call(&Api::FAS_SetADConfig, kFail, iBdID, channel, type, value, &recv);
    }

    int32_t EziMOTIONPlusELib::FAS_GetADConfig(int32_t iBdID, uint8_t channel, int32_t type, int32_t& value)
    {
        return Call(&Api::FAS_GetADConfig, kFail, iBdID, channel, type, &value);
    }

    int32_t EziMOTIONPlusELib::FAS_LoadADConfig(int32_t iBdID)
    {
        return Call(&Api::FAS_LoadADConfig, kFail, iBdID);
    }

    int32_t EziMOTIONPlusELib::FAS_SaveADConfig(int32_t iBdID)
    {
        return Call(&Api::FAS_SaveADConfig, kFail, iBdID);
    }

    int32_t EziMOTIONPlusELib::FAS_ReadADValue(int32_t iBdID, uint8_t channel, int16_t& advalue)
    {
        return Call(&Api::FAS_ReadADValue, kFail, iBdID, channel, &advalue);
    }

    int32_t EziMOTIONPlusELib::FAS_ReadADAllValue(int32_t iBdID, uint8_t offset, uint8_t* adbuffer)
    {
        return Call(&Api::FAS_ReadADAllValue, kFail, iBdID, offset, adbuffer);
    }

    int32_t EziMOTIONPlusELib::FAS_ReadADAllValue(int32_t iBdID, uint8_t offset, AD_BUFFER& adbuffer)
    {
        std::array<uint8_t, 16> array{};
        int32_t num = 0;
        num = FAS_ReadADAllValue(iBdID, offset, array.data());
        adbuffer = AD_BUFFER(array);
        return num;
    }

    int32_t EziMOTIONPlusELib::FAS_GetAllADResult(int32_t iBdID, uint8_t* result)
    {
        return Call(&Api::FAS_GetAllADResult, kFail, iBdID, result);
    }

    int32_t EziMOTIONPlusELib::FAS_GetAllADResult(int32_t iBdID, AD_RESULT& result)
    {
        std::array<uint8_t, AD_RESULT::BUFF_SIZE> array{};
        int32_t num = 0;
        num = FAS_GetAllADResult(iBdID, array.data());
        result = AD_RESULT(array);
        return num;
    }

    int32_t EziMOTIONPlusELib::FAS_GetADResult(int32_t iBdID, uint8_t channel, float& adresult)
    {
        return Call(&Api::FAS_GetADResult, kFail, iBdID, channel, &adresult);
    }

    int32_t EziMOTIONPlusELib::FAS_SetADRange(int32_t iBdID, uint8_t channel, int32_t range)
    {
        return Call(&Api::FAS_SetADRange, kFail, iBdID, channel, range);
    }

    int32_t EziMOTIONPlusELib::FAS_SetADRange(int32_t iBdID, uint8_t channel, AD_RANGE range)
    {
        return FAS_SetADRange(iBdID, channel, static_cast<int32_t>(range));
    }

    // ═════════════════════════════ Ezi-IO DA Functions ═════════════════════════════

    int32_t EziMOTIONPlusELib::FAS_SetDACConfig(int32_t iBdID, uint8_t channel, DAC_CONFIG type, int32_t data, int32_t& recv)
    {
        return Call(&Api::FAS_SetDACConfig, kFail, iBdID, channel, static_cast<int32_t>(type), data, &recv);
    }

    int32_t EziMOTIONPlusELib::FAS_GetDACConfig(int32_t iBdID, uint8_t channel, DAC_CONFIG type, int32_t& data)
    {
        return Call(&Api::FAS_GetDACConfig, kFail, iBdID, channel, static_cast<int32_t>(type), &data);
    }

    int32_t EziMOTIONPlusELib::FAS_LoadDACConfig(int32_t iBdID)
    {
        return Call(&Api::FAS_LoadDACConfig, kFail, iBdID);
    }

    int32_t EziMOTIONPlusELib::FAS_SaveDACConfig(int32_t iBdID)
    {
        return Call(&Api::FAS_SaveDACConfig, kFail, iBdID);
    }

    int32_t EziMOTIONPlusELib::FAS_SetDACValue(int32_t iBdID, uint8_t channel, int32_t bEnable, int32_t value)
    {
        return Call(&Api::FAS_SetDACValue, kFail, iBdID, channel, bEnable, value);
    }

    int32_t EziMOTIONPlusELib::FAS_GetDACValue(int32_t iBdID, uint8_t channel, int32_t& bEnable, int32_t& value)
    {
        return Call(&Api::FAS_GetDACValue, kFail, iBdID, channel, &bEnable, &value);
    }

    // ═════════════════════════════ Ezi-IO Counter Functions ═════════════════════════════

    int32_t EziMOTIONPlusELib::FAS_CounterCommand(int32_t iBdID, uint8_t channel, COUNTER_CMD cmd, int32_t data)
    {
        return Call(&Api::FAS_CounterCommand, kFail, iBdID, channel, static_cast<int32_t>(cmd), data);
    }

    int32_t EziMOTIONPlusELib::FAS_GetCounterValue(int32_t iBdID, uint8_t channel, COUNTER_VALUE type, int32_t& value)
    {
        return Call(&Api::FAS_GetCounterValue, kFail, iBdID, channel, static_cast<int32_t>(type), &value);
    }

    int32_t EziMOTIONPlusELib::FAS_GetCounterStatus(int32_t iBdID, uint32_t& dwStatus)
    {
        return Call(&Api::FAS_GetCounterStatus, kFail, iBdID, &dwStatus);
    }

    int32_t EziMOTIONPlusELib::FAS_SetCounterTrigger(int32_t iBdID, uint8_t channel, int32_t bStartTrigger, int32_t lStartPos, uint32_t dwPeriod,
                                                     uint32_t dwPulseTime, uint32_t dwTriggerCnt)
    {
        return Call(&Api::FAS_SetCounterTrigger, kFail, iBdID, channel, bStartTrigger, lStartPos, dwPeriod, dwPulseTime, dwTriggerCnt);
    }

    int32_t EziMOTIONPlusELib::FAS_SetCounterTrigger(int32_t iBdID, uint8_t channel, bool bStartTrigger, int32_t lStartPos, uint32_t dwPeriod,
                                                     uint32_t dwPulseTime, uint32_t dwTriggerCnt)
    {
        return FAS_SetCounterTrigger(iBdID, channel, bStartTrigger ? 1 : 0, lStartPos, dwPeriod, dwPulseTime, dwTriggerCnt);
    }

    int32_t EziMOTIONPlusELib::FAS_SetCounterConfig(int32_t iBdID, uint8_t channel, COUNTER_CONFIG type, int32_t data, int32_t& recv)
    {
        return Call(&Api::FAS_SetCounterConfig, kFail, iBdID, channel, static_cast<int32_t>(type), data, &recv);
    }

    int32_t EziMOTIONPlusELib::FAS_GetCounterConfig(int32_t iBdID, uint8_t channel, COUNTER_CONFIG type, int32_t& data)
    {
        return Call(&Api::FAS_GetCounterConfig, kFail, iBdID, channel, static_cast<int32_t>(type), &data);
    }

    int32_t EziMOTIONPlusELib::FAS_LoadCounterConfig(int32_t iBdID)
    {
        return Call(&Api::FAS_LoadCounterConfig, kFail, iBdID);
    }

    int32_t EziMOTIONPlusELib::FAS_SaveCounterConfig(int32_t iBdID)
    {
        return Call(&Api::FAS_SaveCounterConfig, kFail, iBdID);
    }

    // ═════════════════════════════ Ezi-SPEED Functions ═════════════════════════════

    int32_t EziMOTIONPlusELib::FAS_SPEEDGetInputStatus(int32_t iBdID, uint32_t& dwDirectInput)
    {
        return Call(&Api::FAS_SPEEDGetInputStatus, kFail, iBdID, &dwDirectInput);
    }

    int32_t EziMOTIONPlusELib::FAS_SPEEDGetOutputStatus(int32_t iBdID, uint32_t& dwDirectOutput)
    {
        return Call(&Api::FAS_SPEEDGetOutputStatus, kFail, iBdID, &dwDirectOutput);
    }

    int32_t EziMOTIONPlusELib::FAS_SPEEDGetNETStatus(int32_t iBdID, uint32_t& dwNETStatus)
    {
        return Call(&Api::FAS_SPEEDGetNETStatus, kFail, iBdID, &dwNETStatus);
    }

    int32_t EziMOTIONPlusELib::FAS_SPEEDSetOutput(int32_t iBdID, uint32_t dwIOSETMask, uint32_t dwIOCLRMask)
    {
        return Call(&Api::FAS_SPEEDSetOutput, kFail, iBdID, dwIOSETMask, dwIOCLRMask);
    }

    int32_t EziMOTIONPlusELib::FAS_SPEEDGetCommandSpeed(int32_t iBdID, int32_t& nSpeed)
    {
        return Call(&Api::FAS_SPEEDGetCommandSpeed, kFail, iBdID, &nSpeed);
    }

    int32_t EziMOTIONPlusELib::FAS_SPEEDGetFeedbackSpeed(int32_t iBdID, int32_t& nSpeed)
    {
        return Call(&Api::FAS_SPEEDGetFeedbackSpeed, kFail, iBdID, &nSpeed);
    }

    int32_t EziMOTIONPlusELib::FAS_SPEEDGetGearSpeed(int32_t iBdID, float& fSpeed)
    {
        return Call(&Api::FAS_SPEEDGetGearSpeed, kFail, iBdID, &fSpeed);
    }

    int32_t EziMOTIONPlusELib::FAS_SPEEDGetConveyorSpeed(int32_t iBdID, float& fSpeed)
    {
        return Call(&Api::FAS_SPEEDGetConveyorSpeed, kFail, iBdID, &fSpeed);
    }

    int32_t EziMOTIONPlusELib::FAS_SPEEDGetOperationNo(int32_t iBdID, uint8_t& nOpNo)
    {
        return Call(&Api::FAS_SPEEDGetOperationNo, kFail, iBdID, &nOpNo);
    }

    int32_t EziMOTIONPlusELib::FAS_SPEEDMoveVelocity(int32_t iBdID, uint16_t uSpeed, int32_t bForward, uint16_t uAccTime, uint16_t uDecTime, uint16_t uTorqueLimit)
    {
        return Call(&Api::FAS_SPEEDMoveVelocity, kFail, iBdID, uSpeed, bForward, uAccTime, uDecTime, uTorqueLimit);
    }

    int32_t EziMOTIONPlusELib::FAS_SPEEDSetOperationData(int32_t iBdID, uint8_t nOpNo, uint16_t uSpeed, uint16_t uAccTime, uint16_t uDecTime, uint16_t uTorqueLimit)
    {
        return Call(&Api::FAS_SPEEDSetOperationData, kFail, iBdID, nOpNo, uSpeed, uAccTime, uDecTime, uTorqueLimit);
    }

    int32_t EziMOTIONPlusELib::FAS_SPEEDGetOperationData(int32_t iBdID, uint8_t nOpNo, uint16_t& uSpeed, uint16_t& uAccTime, uint16_t& uDecTime, uint16_t& uTorqueLimit)
    {
        return Call(&Api::FAS_SPEEDGetOperationData, kFail, iBdID, nOpNo, &uSpeed, &uAccTime, &uDecTime, &uTorqueLimit);
    }

    int32_t EziMOTIONPlusELib::FAS_SPEEDRunOperationData(int32_t iBdID, uint8_t nOpNo, int32_t bForward)
    {
        return Call(&Api::FAS_SPEEDRunOperationData, kFail, iBdID, nOpNo, bForward);
    }

    int32_t EziMOTIONPlusELib::FAS_SPEEDLoadOperationData(int32_t iBdID)
    {
        return Call(&Api::FAS_SPEEDLoadOperationData, kFail, iBdID);
    }

    int32_t EziMOTIONPlusELib::FAS_SPEEDSaveOperationData(int32_t iBdID)
    {
        return Call(&Api::FAS_SPEEDSaveOperationData, kFail, iBdID);
    }
}

#undef SCA_EZI_API_LIST
