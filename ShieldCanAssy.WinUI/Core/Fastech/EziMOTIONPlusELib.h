#pragma once
// Bản C++ của Lib.Fastech\EziMOTIONPlusELib.cs (namespace FASTECH — file của Fastech, bản dựng lại từ project
// Ama_Actro_HousingCorrosion): lớp bọc thư viện EziMOTIONPlusE.dll cho Ezi-SERVO II Plus-E và Ezi-IO Ethernet.
// Bản C# ghi "GIỮ NGUYÊN, không sửa — muốn đổi hành vi thì bọc ở FastechAxis / IoConnect"; bản C++ giữ đúng tinh thần
// đó: cùng tên hàm, cùng thứ tự tham số, cùng hằng số, cùng lớp đệm byte. Mọi quyết định máy nằm ở FastechAxis/IoConnect.
//
// ── Khác bản C# ở MỘT điểm: cách nạp DLL ──
// C# dùng [DllImport]: CLR tự nạp DLL ở lần gọi đầu, thiếu DLL thì lời gọi ném DllNotFoundException và FastechAxis /
// IoConnect bắt nó để log "không tìm thấy EziMOTIONPlusE.dll cạnh exe". C++ nạp ĐỘNG (LoadLibraryExW + GetProcAddress):
//   • Consumer gọi Load(error) TRƯỚC FAS_Connect (đúng chỗ C# có try/catch DllNotFoundException). Load trả false
//     (IsLoaded() == false) ⇔ C# sẽ ném DllNotFoundException → consumer log cùng thông điệp "DLL not found".
//   • Wrapper KHÔNG BAO GIỜ tự nạp DLL. Chưa Load (hoặc Load hỏng) thì mọi FAS_* trả mã hỏng, không crash:
//       – hàm trả mã lỗi FMM_*            → NotLoadedCode (= FMM_NOT_OPEN = 1): đúng mã chính thư viện trả khi gọi
//                                            lệnh cho một iBdID chưa FAS_Connect — thiếu DLL thì không board nào
//                                            mở được. Mọi consumer coi rc != 0 là hỏng.
//       – hàm native trả BOOL (Connect, ConnectTCP, IsBdIDExist, IsIPAddressExist, Reconnect, IsSlaveExist,
//         SetLogPath) và hai hàm native void mà C# khai int (SetAutoReconnect, PrintCustomLog) → 0 (FALSE).
//       – hàm void (Close, EnableLog, SetLogLevel) → không làm gì.
//     Không tự nạp là chủ ý an toàn: kiểm thử không bao giờ vô tình nạp DLL thật rồi gửi lệnh ra phần cứng.
//   • DLL nạp được nhưng thiếu vài entry point (bản SDK khác): Load vẫn true, tên thiếu ở MissingSymbols(); wrapper
//     của hàm thiếu trả cùng mã hỏng như trên — giống P/Invoke chỉ ném EntryPointNotFoundException ở đúng hàm thiếu.
//
// Chữ ký: wrapper giữ đúng kiểu P/Invoke của C# (byte→uint8_t, int→int32_t, uint→uint32_t, ushort→uint16_t,
// ref/out → tham chiếu, mảng → con trỏ, string → std::string UTF-8 đổi sang UTF-16/ANSI ở ranh giới). Con trỏ hàm bên
// trong (.cpp) khai theo header gốc của Fastech (FAS_EziMOTIONPlusE.h, Include_x64) — WINAPI, CharSet Ansi cho
// LPSTR, LPCWSTR cho đường dẫn log. Chỗ C# khác header gốc được ghi ngay ở hàm đó.
//
// Các overload "protected" của C# (bản thô nhận byte/byte[]/StringBuilder) giữ nguyên là protected: chỉ wrapper public
// gọi tới, như bản C#.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sca
{
    /// <summary>
    /// Thay cho System.Net.IPAddress — chỉ phần mà FAS_* cần: 4 byte IPv4. TryParse theo luật của .NET Framework 4.8
    /// (IPv4AddressHelper.ParseNonCanonical): 1–4 phần, mỗi phần thập phân / 0x hex / 0 bát phân ("192.168.2" =
    /// 192.168.0.2), phải hết chuỗi, không khoảng trắng.
    /// </summary>
    struct IPAddress
    {
        std::array<uint8_t, 4> Bytes{};

        IPAddress() = default;
        IPAddress(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) : Bytes{ b0, b1, b2, b3 } {}
        explicit IPAddress(const std::array<uint8_t, 4>& bytes) : Bytes(bytes) {}

        std::array<uint8_t, 4> GetAddressBytes() const { return Bytes; }

        /// <summary>"a.b.c.d".</summary>
        std::string ToString() const;

        /// <summary>
        /// IPAddress.TryParse cho IPv4. Hỏng thì address = 0.0.0.0 và trả false.
        /// TODO(port): .NET còn nhận chuỗi IPv6 (có ':') rồi FAS_Connect lấy 4 byte đầu của địa chỉ 16 byte — vô nghĩa
        /// với Fastech; bản C++ trả false cho mọi chuỗi IPv6.
        /// </summary>
        static bool TryParse(std::string_view ipString, IPAddress& address);

        bool operator==(const IPAddress&) const = default;
    };

    namespace fastech_detail
    {
        // BitConverter của .NET trên x64 là little-endian — memcpy trên x64 cho đúng cùng byte.
        template <class T>
        inline T Get(const uint8_t* buffer, size_t offset)
        {
            T v{};
            std::memcpy(&v, buffer + offset, sizeof(T));
            return v;
        }

        template <class T>
        inline void Set(uint8_t* buffer, size_t offset, T value)
        {
            std::memcpy(buffer + offset, &value, sizeof(T));
        }

        /// <summary>Buffer.BlockCopy(data → buffer) với count = min(data.Length, BUFF_SIZE).</summary>
        inline void CopyIn(uint8_t* buffer, size_t size, std::span<const uint8_t> data)
        {
            size_t count = data.size() < size ? data.size() : size;
            if (count) std::memcpy(buffer, data.data(), count);
        }

        /// <summary>Buffer.BlockCopy(buffer → data) với count = min(data.Length, BUFF_SIZE).</summary>
        inline void CopyOut(const uint8_t* buffer, size_t size, std::span<uint8_t> data)
        {
            size_t count = data.size() < size ? data.size() : size;
            if (count) std::memcpy(data.data(), buffer, count);
        }
    }

// Property C# { get; set; } trên mảng đệm → cặp hàm cùng tên: T Name() const / void Name(T value).
#define SCA_EZI_PROP(T, Name, Offset)                                                                 \
    T Name() const { return ::sca::fastech_detail::Get<T>(buffer.data(), Offset); }                   \
    void Name(T value) { ::sca::fastech_detail::Set<T>(buffer.data(), Offset, value); }
#define SCA_EZI_GET(T, Name, Offset)                                                                  \
    T Name() const { return ::sca::fastech_detail::Get<T>(buffer.data(), Offset); }

    class EziMOTIONPlusELib
    {
    public:
        EziMOTIONPlusELib() = delete;

        // ═════════════════════════════ Lớp đệm byte (giống hệt bản C#) ═════════════════════════════
        // Bản C# không dùng StructLayout: mỗi struct native là một mảng byte BUFF_SIZE + property đọc/ghi theo offset.
        // Offset/kích thước đã đối chiếu với MOTION_DEFINE.h của Fastech (ITEM_NODE pack(2) 64 byte, MOTION/VELOCITY_
        // OPTION_EX pack(1) 32 byte, ALARM_LOG 31, DRIVE_INFO pack(1) 70, TRIGGER_INFO pack(2) 12, AD_RESULT pack(1)
        // 112, AD_BUFFER 16) — FastechBindingTests kiểm lại bằng struct packed.

        /// <summary>Một dòng Position Table (ITEM_NODE của Fastech, 64 byte).</summary>
        class ITEM_NODE
        {
        public:
            static constexpr int BUFF_SIZE = 64;

            SCA_EZI_PROP(int32_t, lPosition, 0)
            SCA_EZI_PROP(uint32_t, dwStartSpd, 4)
            SCA_EZI_PROP(uint32_t, dwMoveSpd, 8)
            SCA_EZI_PROP(uint16_t, wAccelTime, 12)      // header gốc: wAccelRate
            SCA_EZI_PROP(uint16_t, wDecelTime, 14)      // header gốc: wDecelRate
            SCA_EZI_PROP(uint16_t, wCommand, 16)
            SCA_EZI_PROP(uint16_t, wWaitTime, 18)
            SCA_EZI_PROP(uint16_t, wContinuous, 20)
            SCA_EZI_PROP(uint16_t, wBranch, 22)
            SCA_EZI_PROP(uint16_t, wCond_branch0, 24)
            SCA_EZI_PROP(uint16_t, wCond_branch1, 26)
            SCA_EZI_PROP(uint16_t, wCond_branch2, 28)
            SCA_EZI_PROP(uint16_t, wLoopCount, 30)
            SCA_EZI_PROP(uint16_t, wBranchAfterLoop, 32)
            SCA_EZI_PROP(uint16_t, wPTSet, 34)
            SCA_EZI_PROP(uint16_t, wLoopCountCLR, 36)
            SCA_EZI_PROP(uint16_t, bCheckInpos, 38)     // 0 : Check Inpos, 1 : Don't Check.
            SCA_EZI_PROP(int32_t, lTriggerPos, 40)
            SCA_EZI_PROP(uint16_t, wTriggerOnTime, 44)
            SCA_EZI_PROP(uint16_t, wPushRatio, 46)
            SCA_EZI_PROP(uint32_t, dwPushSpeed, 48)
            SCA_EZI_PROP(int32_t, lPushPosition, 52)
            SCA_EZI_PROP(uint16_t, wPushMode, 56)

            ITEM_NODE() = default;

            void copyfrom(std::span<const uint8_t> data) { fastech_detail::CopyIn(buffer.data(), buffer.size(), data); }
            void copyto(std::span<uint8_t> data) const { fastech_detail::CopyOut(buffer.data(), buffer.size(), data); }

            /// <summary>Xoá về 0, rồi đặt các ô "không có lệnh / không nhảy" = 0xFFFF (như bản C#).</summary>
            void Clear()
            {
                buffer.fill(0);
                wCommand(UINT16_MAX);
                wBranch(UINT16_MAX);
                wCond_branch0(UINT16_MAX);
                wCond_branch1(UINT16_MAX);
                wCond_branch2(UINT16_MAX);
                wBranchAfterLoop(UINT16_MAX);
                wLoopCountCLR(UINT16_MAX);
            }

        private:
            std::array<uint8_t, BUFF_SIZE> buffer{};
        };

        /// <summary>Tuỳ chọn của FAS_MoveSingleAxisAbsPosEx / IncPosEx (MOTION_OPTION_EX, 32 byte).</summary>
        class MOTION_OPTION_EX
        {
        public:
            static constexpr int BUFF_SIZE = 32;

            SCA_EZI_PROP(int32_t, dwOptionFlag, 0)

            bool BIT_IGNOREEXSTOP() const { return (uint8_t)(buffer[0] & 1) != 0; }
            void BIT_IGNOREEXSTOP(bool value) { buffer[0] = value ? (uint8_t)(buffer[0] | 1) : (uint8_t)(buffer[0] & 0xFE); }
            bool BIT_USE_CUSTOMACCEL() const { return (uint8_t)(buffer[0] & 2) != 0; }
            void BIT_USE_CUSTOMACCEL(bool value) { buffer[0] = value ? (uint8_t)(buffer[0] | 2) : (uint8_t)(buffer[0] & 0xFD); }
            bool BIT_USE_CUSTOMDECEL() const { return (uint8_t)(buffer[0] & 4) != 0; }
            void BIT_USE_CUSTOMDECEL(bool value) { buffer[0] = value ? (uint8_t)(buffer[0] | 4) : (uint8_t)(buffer[0] & 0xFB); }

            SCA_EZI_PROP(uint16_t, wCustomAccelTime, 4)
            SCA_EZI_PROP(uint16_t, wCustomDecelTime, 6)

            MOTION_OPTION_EX() = default;

            void copyto(std::span<uint8_t> data) const { fastech_detail::CopyOut(buffer.data(), buffer.size(), data); }
            void Clear() { buffer.fill(0); }

        private:
            std::array<uint8_t, BUFF_SIZE> buffer{};
        };

        /// <summary>Tuỳ chọn của FAS_MoveVelocityEx (VELOCITY_OPTION_EX, 32 byte).</summary>
        class VELOCITY_OPTION_EX
        {
        public:
            static constexpr int BUFF_SIZE = 32;

            SCA_EZI_PROP(int32_t, dwOptionFlag, 0)

            bool BIT_IGNOREEXSTOP() const { return (uint8_t)(buffer[0] & 1) != 0; }
            void BIT_IGNOREEXSTOP(bool value) { buffer[0] = value ? (uint8_t)(buffer[0] | 1) : (uint8_t)(buffer[0] & 0xFE); }
            bool BIT_USE_CUSTOMACCDEC() const { return (uint8_t)(buffer[0] & 2) != 0; }
            void BIT_USE_CUSTOMACCDEC(bool value) { buffer[0] = value ? (uint8_t)(buffer[0] | 2) : (uint8_t)(buffer[0] & 0xFD); }

            SCA_EZI_PROP(uint16_t, wCustomAccDecTime, 4)

            VELOCITY_OPTION_EX() = default;

            void copyto(std::span<uint8_t> data) const { fastech_detail::CopyOut(buffer.data(), buffer.size(), data); }
            void Clear() { buffer.fill(0); }

        private:
            std::array<uint8_t, BUFF_SIZE> buffer{};
        };

        /// <summary>Lịch sử alarm / warning của driver (ALARM_LOG: 1 byte đếm + 30 byte mã).</summary>
        class ALARM_LOG
        {
        public:
            static constexpr int BUFF_SIZE = 31;

            uint8_t nAlarmCount() const { return buffer[0]; }
            /// TODO(port): setter của bản C# là `buffer[0] = nAlarmCount;` (gán lại chính giá trị cũ) — không ghi gì.
            /// Giữ nguyên hành vi đó; không ai gọi setter này.
            void nAlarmCount(uint8_t /*value*/) { buffer[0] = nAlarmCount(); }

            ALARM_LOG() = default;

            void copy(std::span<const uint8_t> data) { fastech_detail::CopyIn(buffer.data(), buffer.size(), data); }
            void Clear() { buffer.fill(0); }

            /// <summary>Mã alarm thứ index (0…29); ngoài khoảng trả 0.</summary>
            uint8_t nAlarmLog(uint32_t index) const { return (uint8_t)((index < 30) ? buffer[index + 1] : 0); }

        private:
            std::array<uint8_t, BUFF_SIZE> buffer{};
        };

        /// <summary>Thông tin driver (DRIVE_INFO pack(1), 70 byte) — FAS_GetSlaveInfoEx.</summary>
        class DRIVE_INFO
        {
        public:
            static constexpr int BUFF_SIZE = 70;

            // Drive Version Number (Major Ver/Minor Ver/Bug fix/Build No.)
            SCA_EZI_GET(uint16_t, nVersionNo0, 0)
            SCA_EZI_GET(uint16_t, nVersionNo1, 2)
            SCA_EZI_GET(uint16_t, nVersionNo2, 4)
            SCA_EZI_GET(uint16_t, nVersionNo3, 6)

            /// <summary>Drive Version string (char[30] ở offset 8). Bản C# giải mã bằng Encoding.Default (code page ANSI
            /// của máy, .NET Framework 4.8) và bỏ mọi '\0' — bản C++ cũng đọc theo CP_ACP rồi trả UTF-8.</summary>
            std::string sVersion() const;

            SCA_EZI_GET(uint16_t, nDriveType, 38)       // Drive Model
            SCA_EZI_GET(uint16_t, nMotorType, 40)       // Motor Model

            /// <summary>Motor Info (char[20] ở offset 42), giải mã như sVersion.</summary>
            std::string sMotorInfo() const;

            SCA_EZI_GET(uint16_t, nInPinNo, 62)         // Input Pin Number
            SCA_EZI_GET(uint16_t, nOutPinNo, 64)        // Output Pin Number
            SCA_EZI_GET(uint16_t, nPTNum, 66)           // Position Table Item Number
            SCA_EZI_GET(uint16_t, nFirmwareType, 68)    // Firmware Type Information

            DRIVE_INFO() = default;

            void copy(std::span<const uint8_t> data) { fastech_detail::CopyIn(buffer.data(), buffer.size(), data); }
            void Clear() { buffer.fill(0); }

        private:
            std::array<uint8_t, BUFF_SIZE> buffer{};
        };

        /// <summary>Cấu hình trigger ngõ ra Ezi-IO (TRIGGER_INFO pack(2), 12 byte) — FAS_SetTrigger.</summary>
        class TRIGGER_INFO
        {
        public:
            static constexpr int BUFF_SIZE = 12;

            /// <summary>Chính mảng đệm (C#: `byte[] ByteArray => buffer`) — FAS_SetTrigger gửi thẳng mảng này.</summary>
            std::array<uint8_t, BUFF_SIZE>& ByteArray() { return buffer; }
            const std::array<uint8_t, BUFF_SIZE>& ByteArray() const { return buffer; }

            SCA_EZI_PROP(uint16_t, wPeriod, 0)
            SCA_EZI_PROP(uint16_t, wReserved1, 2)
            SCA_EZI_PROP(uint16_t, wOnTime, 4)
            SCA_EZI_PROP(uint16_t, wReserved2, 6)
            SCA_EZI_PROP(uint32_t, wCount, 8)

            TRIGGER_INFO() = default;

            void copy(std::span<const uint8_t> data) { fastech_detail::CopyIn(buffer.data(), buffer.size(), data); }
            void Clear() { buffer.fill(0); }

        private:
            std::array<uint8_t, BUFF_SIZE> buffer{};
        };

        enum class AD_RANGE : int32_t
        {
            ADRANGE_10_to_10 = 0,           //  -10V ~  10V [2.441mV]
            ADRANGE_5_to_5 = 1,             //   -5V ~   5V [1.22mV]
            ADRANGE_2_5_to_2_5 = 2,         // -2.5V ~ 2.5V [0.61mV]
            ADRANGE_0_to_10 = 3,            //    0V ~  10V [1.22mV]
            ADRANGE_0_to_20_C = 4,          //   0mA ~ 20mA [2.44uA]
            ADRANGE_EX_10_to_10 = 16,       // (Read Only, External Switch)  -10V ~  10V
            ADRANGE_EX_5_to_5 = 17,         // (Read Only, External Switch)   -5V ~   5V
            ADRANGE_EX_2_5_to_2_5 = 18,     // (Read Only, External Switch) -2.5V ~ 2.5V
            ADRANGE_EX_0_to_10 = 19         // (Read Only, External Switch)    0V ~  10V
        };

        /// <summary>Kết quả AD 16 kênh (AD_RESULT pack(1): mỗi kênh char range + short raw + float converted = 7 byte).</summary>
        class AD_RESULT
        {
        public:
            /// C#: trường thường (không phải const) — mảng vẫn cố định 16 phần tử như bản C# cấp phát trong constructor.
            int32_t MAX_CHANNEL_COUNT = 16;

            static constexpr int BUFF_SIZE = 112;

            const std::array<char16_t, 16>& Range() const { return range; }
            const std::array<int16_t, 16>& RawData() const { return rawdata; }
            const std::array<float, 16>& Converted() const { return converted; }

            /// <summary>Mọi kênh = 0 (để làm tham số out của FAS_GetAllADResult).</summary>
            AD_RESULT() = default;
            explicit AD_RESULT(std::span<const uint8_t> data) { copyfrom(data); }

        private:
            void copyfrom(std::span<const uint8_t> data);

            std::array<char16_t, 16> range{};
            std::array<int16_t, 16> rawdata{};
            std::array<float, 16> converted{};
        };

        enum class AD_DATA_TYPE : int32_t
        {
            TYPE_AD_RANGE,
            TYPE_AD_FILTER_LENGTH,
            TYPE_AD_FILTER_OFFSET
        };

        /// <summary>Giá trị AD thô 8 kênh (AD_BUFFER, 16 byte) — FAS_ReadADAllValue.</summary>
        class AD_BUFFER
        {
        public:
            static constexpr int MAX_CHANNEL_COUNT = 8;

            const std::array<int16_t, 8>& Channel() const { return channel; }

            AD_BUFFER() = default;
            /// <summary>Chỉ giải mã khi đúng 16 byte (như bản C#); sai độ dài thì mọi kênh = 0.</summary>
            explicit AD_BUFFER(std::span<const uint8_t> buffer)
            {
                if (buffer.size() == 16)
                    for (int i = 0; i < 8; i++)
                        channel[i] = fastech_detail::Get<int16_t>(buffer.data(), (size_t)i * 2);
            }

        private:
            std::array<int16_t, 8> channel{};
        };

        enum class DA_RANGE : int32_t
        {
            DARANGE_0_to_5 = 0,             //    0V ~   5V (0 ~ 25000)
            DARANGE_10_to_10 = 1,           //  -10V ~  10V (-25000 ~ 25000)
            DARANGE_0_to_10 = 2,            //    0V ~  10V (0 ~ 25000)
            DARANGE_1_to_5 = 3,             //    1V ~   5V (0 ~ 25000)
            DARANGE_0_to_20_C = 4,          //   0mA ~ 20mA (0 ~ 25000)
            DARANGE_4_to_20_C = 5,          //   4mA ~ 20mA (0 ~ 25000)
            DARANGE_EX_0_to_5 = 16,         // (Read Only, External Switch)
            DARANGE_EX_10_to_10 = 17,
            DARANGE_EX_0_to_20_C = 18,
            DARANGE_EX_4_to_20_C = 19
        };

        enum class DAC_CONFIG : int32_t
        {
            DAC_RANGE,
            DAC_CALIBRATION_HIGH,
            DAC_CALIBRATION_LOW
        };

        enum class COUNTER_CMD : int32_t
        {
            CNTCMD_CH_ENABLE,
            CNTCMD_LATCHA_ENABLE,
            CNTCMD_LATCHB_ENABLE,
            CNTCMD_ZLATCH_ENABLE,
            CNTCMD_RESET_ALL,
            CNTCMD_RESET_COUNT,
            CNTCMD_RESET_LATCH
        };

        enum class COUNTER_VALUE : int32_t
        {
            CNT_COUNT_VALUE,
            CNT_LATCHA_VALUE,
            CNT_LATCHB_VALUE,
            CNT_ZLATCH_VALUE
        };

        enum class COUNTER_CONFIG : int32_t
        {
            CFG_INPUT_MODE,         // Counter Input Mode (0: Quadrature, 1: 2-Pulse, 2: 1-Pulse)
            CFG_COUNT_DIRECTION,    // Count Direction (0: CW, 1: CCW)
            CFG_LATCHA_MODE,        // Latch A Mode (0: Single, 1: Continue)
            CFG_LATCHB_MODE,        // Latch B Mode (0: Single, 1: Continue)
            CFG_ZLATCH_MODE,        // Z-Phase Latch Mode (0: Single, 1: Continue, 2: Reset)
            CFG_LATCHA_LOGIC,       // Latch A Logic (0: Low-Active, 1: High-Active)
            CFG_LATCHB_LOGIC,       // Latch B Logic (0: Low-Active, 1: High-Active)
            CFG_ZLATCH_LOGIC,       // Z-Phase Latch Logic (0: Low-Active, 1: High-Active)
            CFG_RESET_LOGIC,        // (external input) Reset Logic (0: Low-Active, 1: High-Active)
            CFG_CP_LOGIC            // (external output) Comparison Output Logic (0: Low-Active, 1: High-Active)
        };

        enum class LOG_LEVEL : int32_t
        {
            LOG_LEVEL_COMM,         // Communication Log only
            LOG_LEVEL_PARAM,        // Communication Log and parameter functions
            LOG_LEVEL_MOTION,       // Communication Log and parameter, motion, I/O functions
            LOG_LEVEL_ALL           // Communication Log and all functions
        };

        // ═════════════════════════════ Mã trả về (ReturnCodes_Define.h) ═════════════════════════════
        static constexpr int32_t FMM_OK = 0;
        static constexpr int32_t FMM_NOT_OPEN = 1;
        static constexpr int32_t FMM_INVALID_PORT_NUM = 2;
        static constexpr int32_t FMM_INVALID_SLAVE_NUM = 3;
        static constexpr int32_t FMC_DISCONNECTED = 5;
        static constexpr int32_t FMC_TIMEOUT_ERROR = 6;
        static constexpr int32_t FMC_CRCFAILED_ERROR = 7;
        static constexpr int32_t FMC_RECVPACKET_ERROR = 8;      // PACKET SIZE ERROR
        static constexpr int32_t FMM_POSTABLE_ERROR = 9;
        static constexpr int32_t FMP_FRAMETYPEERROR = 128;
        static constexpr int32_t FMP_DATAERROR = 129;
        static constexpr int32_t FMP_PACKETERROR = 130;
        static constexpr int32_t FMP_RUNFAIL = 133;
        static constexpr int32_t FMP_RESETFAIL = 134;
        static constexpr int32_t FMP_SERVOONFAIL1 = 135;
        static constexpr int32_t FMP_SERVOONFAIL2 = 136;
        static constexpr int32_t FMP_SERVOONFAIL3 = 137;
        static constexpr int32_t FMP_SERVOOFF_FAIL = 138;
        static constexpr int32_t FMP_ROMACCESS = 139;
        static constexpr int32_t FMP_PACKETCRCERROR = 170;
        static constexpr int32_t FMM_UNKNOWN_ERROR = 255;

        // ═════════════════════════════ Loại thiết bị ═════════════════════════════
        static constexpr uint8_t DEVTYPE_EZI_SERVO2_PLUS_E_ST = 100;
        static constexpr const char* DEVNAME_EZI_SERVO2_PLUS_E_ST = "Ezi-SERVO II Plus-E-ST";
        static constexpr uint8_t DEVTYPE_S_SERVO_PLUS_E_ST = 102;
        static constexpr const char* DEVNAME_S_SERVO_PLUS_E_ST = "S-SERVO Plus-E-ST";
        static constexpr uint8_t DEVTYPE_EZI_IO_PLUS_E_IN16 = 150;
        static constexpr const char* DEVNAME_EZI_IO_PLUS_E_IN16 = "Ezi-IO Ethernet-IN16";
        static constexpr uint8_t DEVTYPE_EZI_IO_PLUS_E_IN32 = 151;
        static constexpr const char* DEVNAME_EZI_IO_PLUS_E_IN32 = "Ezi-IO Ethernet-IN32";
        static constexpr uint8_t DEVTYPE_EZI_IO_PLUS_E_OUT16 = 160;
        static constexpr const char* DEVNAME_EZI_IO_PLUS_E_OUT16 = "Ezi-IO Ethernet-OUT16";
        static constexpr uint8_t DEVTYPE_EZI_IO_PLUS_E_OUT32 = 161;
        static constexpr const char* DEVNAME_EZI_IO_PLUS_E_OUT32 = "Ezi-IO Ethernet-OUT32";
        static constexpr uint8_t DEVTYPE_EZI_IO_PLUS_E_I8O8 = 155;
        static constexpr const char* DEVNAME_EZI_IO_PLUS_E_I8O8 = "Ezi-IO Ethernet-I8O8";
        static constexpr uint8_t DEVTYPE_EZI_IO_PLUS_E_I16O16 = 156;
        static constexpr const char* DEVNAME_EZI_IO_PLUS_E_I16O16 = "Ezi-IO Ethernet-I16O16";
        static constexpr uint8_t DEVTYPE_EZI_IO_PLUS_E_IO16 = 152;
        static constexpr const char* DEVNAME_EZI_IO_PLUS_E_IO16 = "Ezi-IO Ethernet-IO16";

        // ═════════════════════════════ Chiều chạy, trạng thái ═════════════════════════════
        static constexpr int32_t DIR_INC = 1;
        static constexpr int32_t DIR_DEC = 0;

        static constexpr uint8_t MAX_AXIS_STATUS = 32;

        // FAS_GetAllStatusEx: pTypes[ALLSTATUSEX_ITEM_COUNT] chọn loại dữ liệu cho từng ô pDatas.
        static constexpr uint8_t ALLSTATUSEX_ITEM_COUNT = 12;
        static constexpr uint8_t STATUSEX_TYPE_NONE = 0;
        static constexpr uint8_t STATUSEX_TYPE_INPUT = 1;
        static constexpr uint8_t STATUSEX_TYPE_OUTPUT = 2;
        static constexpr uint8_t STATUSEX_TYPE_AXISSTATUS = 3;
        static constexpr uint8_t STATUSEX_TYPE_CMDPOS = 4;
        static constexpr uint8_t STATUSEX_TYPE_ACTPOS = 5;
        static constexpr uint8_t STATUSEX_TYPE_ACTVEL = 6;
        static constexpr uint8_t STATUSEX_TYPE_POSERR = 7;
        static constexpr uint8_t STATUSEX_TYPE_PTNO = 8;
        static constexpr uint8_t STATUSEX_TYPE_ALARMTYPE = 9;
        static constexpr uint8_t STATUSEX_TYPE_TEMPERATURE = 10;
        static constexpr uint8_t STATUSEX_TYPE_CURRENT = 11;
        static constexpr uint8_t STATUSEX_TYPE_LOAD = 12;
        static constexpr uint8_t STATUSEX_TYPE_PEAKLOAD = 13;
        static constexpr uint8_t STATUSEX_TYPE_ENCVEL = 14;
        static constexpr uint8_t STATUSEX_TYPE_INPUT_HIGH = 15;
        static constexpr uint8_t STATUSEX_TYPE_PTNO_RUNNING = 16;
        static constexpr uint8_t STATUSEX_TYPE_ADVALUE0 = 30;   // Ezi-IO AD: Ch0 & Ch1 AD Value
        static constexpr uint8_t STATUSEX_TYPE_ADVALUE2 = 31;   // Ezi-IO AD: Ch2 & Ch3 AD Value
        static constexpr uint8_t STATUSEX_TYPE_ADVALUE4 = 32;   // Ezi-IO AD: Ch4 & Ch5 AD Value
        static constexpr uint8_t STATUSEX_TYPE_ADVALUE6 = 33;   // Ezi-IO AD: Ch6 & Ch7 AD Value

        // ═════════════════════════════ Gán I/O ═════════════════════════════
        static constexpr uint8_t LEVEL_LOW_ACTIVE = 0;
        static constexpr uint8_t LEVEL_HIGH_ACTIVE = 1;
        static constexpr uint8_t IN_LOGIC_NONE = 0;
        static constexpr uint8_t OUT_LOGIC_NONE = 0;

        // ═════════════════════════════ Position Table ═════════════════════════════
        static constexpr uint16_t MAX_LOOP_COUNT = 100;
        static constexpr uint16_t MAX_WAIT_TIME = 60000;
        static constexpr uint16_t PUSH_RATIO_MIN = 20;
        static constexpr uint16_t PUSH_RATIO_MAX = 90;
        static constexpr uint32_t PUSH_SPEED_MIN = 1u;
        static constexpr uint32_t PUSH_SPEED_MAX = 100000u;
        static constexpr uint32_t PUSH_PULSECOUNT_MIN = 1u;
        static constexpr uint32_t PUSH_PULSECOUNT_MAX = 10000u;

        static constexpr uint16_t CMD_ABS_LOWSPEED = 0;
        static constexpr uint16_t CMD_ABS_HIGHSPEED = 1;
        static constexpr uint16_t CMD_ABS_HIGHSPEEDDECEL = 2;
        static constexpr uint16_t CMD_ABS_NORMALMOTION = 3;
        static constexpr uint16_t CMD_INC_LOWSPEED = 4;
        static constexpr uint16_t CMD_INC_HIGHSPEED = 5;
        static constexpr uint16_t CMD_INC_HIGHSPEEDDECEL = 6;
        static constexpr uint16_t CMD_INC_NORMALMOTION = 7;
        static constexpr uint16_t CMD_MOVE_ORIGIN = 8;
        static constexpr uint16_t CMD_COUNTERCLEAR = 9;
        static constexpr uint16_t CMD_PUSH_ABSMOTION = 10;
        static constexpr uint16_t CMD_STOP = 11;
        static constexpr uint16_t CMD_MAX_COUNT = 12;
        static constexpr uint16_t CMD_NO_COMMAND = UINT16_MAX;

        // Offset từng ô trong ITEM_NODE — tham số wOffset của FAS_PosTableReadOneItem / WriteOneItem.
        static constexpr uint16_t OFFSET_POSITION = 0;
        static constexpr uint16_t OFFSET_LOWSPEED = 4;
        static constexpr uint16_t OFFSET_HIGHSPEED = 8;
        static constexpr uint16_t OFFSET_ACCELTIME = 12;
        static constexpr uint16_t OFFSET_DECELTIME = 14;
        static constexpr uint16_t OFFSET_COMMAND = 16;
        static constexpr uint16_t OFFSET_WAITTIME = 18;
        static constexpr uint16_t OFFSET_CONTINUOUS = 20;
        static constexpr uint16_t OFFSET_JUMPTABLENO = 22;
        static constexpr uint16_t OFFSET_JUMPPT0 = 24;
        static constexpr uint16_t OFFSET_JUMPPT1 = 26;
        static constexpr uint16_t OFFSET_JUMPPT2 = 28;
        static constexpr uint16_t OFFSET_LOOPCOUNT = 30;
        static constexpr uint16_t OFFSET_LOOPJUMPTABLENO = 32;
        static constexpr uint16_t OFFSET_PTSET = 34;
        static constexpr uint16_t OFFSET_LOOPCOUNTCLEAR = 36;
        static constexpr uint16_t OFFSET_CHECKINPOSITION = 38;
        static constexpr uint16_t OFFSET_TRIGGERPOSITION = 40;
        static constexpr uint16_t OFFSET_TRIGGERONTIME = 44;
        static constexpr uint16_t OFFSET_PUSHRATIO = 46;
        static constexpr uint16_t OFFSET_PUSHSPEED = 48;
        static constexpr uint16_t OFFSET_PUSHPOSITION = 52;
        static constexpr uint16_t OFFSET_PUSHMODE = 56;
        static constexpr uint16_t OFFSET_BLANK = 58;

        // ═════════════════════════════ Loại alarm (FAS_GetAlarmType) ═════════════════════════════
        static constexpr uint16_t ALARM_NONE = 0;
        static constexpr uint16_t ALARM_OVERCURRENT = 1;
        static constexpr uint16_t ALARM_OVERSPEED = 2;
        static constexpr uint16_t ALARM_STEPOUT = 3;
        static constexpr uint16_t ALARM_OVERLOAD = 4;
        static constexpr uint16_t ALARM_OVERTEMPERATURE = 5;
        static constexpr uint16_t ALARM_OVERBACKEMF = 6;
        static constexpr uint16_t ALARM_MOTORCONNECT = 7;
        static constexpr uint16_t ALARM_ENCODERCONNECT = 8;
        static constexpr uint16_t ALARM_LOWMOTORPOWER = 9;
        static constexpr uint16_t ALARM_INPOSITION = 10;
        static constexpr uint16_t ALARM_SYSTEMHALT = 11;
        static constexpr uint16_t ALARM_ROMDEVICE = 12;
        static constexpr uint16_t ALARM_HIGHINPUTVOLTAGE = 14;
        static constexpr uint16_t ALARM_POSITIONOVERFLOW = 15;
        static constexpr uint16_t ALARM_POSITIONCHANGED = 16;

        /// <summary>Mô tả alarm: phần tử [alarm − 1] (bản C# bỏ ô NULL ở đầu bảng gốc; ô 12 = mã 13 dự phòng).</summary>
        static constexpr std::array<const char*, 16> ALARM_DESCRIPTION = {
            "Over Current", "Over Speed", "Position Tracking", "Over Load", "Over Temperature", "Over Back EMF",
            "No Motor Connect", "No Encoder Connect", "Low Motor Power", "Inposition Error",
            "System Halt", "ROM Device Error", "", "High Input Voltage", "Position Overflow", "Position Changed"
        };

        static constexpr uint16_t MAX_ALARM_LOG = 30;

        // ═════════════════════════════ Ezi-IO Counter: bit trạng thái ═════════════════════════════
        static constexpr uint32_t CNTSTATUS_CH1_ENABLED = 1u;
        static constexpr uint32_t CNTSTATUS_CH1_LTCA_EN = 2u;
        static constexpr uint32_t CNTSTATUS_CH1_LTCB_EN = 4u;
        static constexpr uint32_t CNTSTATUS_CH1_ZLTC_EN = 8u;
        static constexpr uint32_t CNTSTATUS_CH1_LTCA_LATCHED = 16u;
        static constexpr uint32_t CNTSTATUS_CH1_LTCB_LATCHED = 32u;
        static constexpr uint32_t CNTSTATUS_CH1_ZLTC_LATCHED = 64u;
        static constexpr uint32_t CNTSTATUS_CH1_RESET = 128u;
        static constexpr uint32_t CNTSTATUS_CH1_TRIGGER = 256u;
        static constexpr uint32_t CNTSTATUS_CH1_COMPARISON = 512u;
        static constexpr uint32_t CNTSTATUS_CH2_ENABLED = 65536u;
        static constexpr uint32_t CNTSTATUS_CH2_LTCA_EN = 131072u;
        static constexpr uint32_t CNTSTATUS_CH2_LTCB_EN = 262144u;
        static constexpr uint32_t CNTSTATUS_CH2_ZLTC_EN = 524288u;
        static constexpr uint32_t CNTSTATUS_CH2_LTCA_LATCHED = 1048576u;
        static constexpr uint32_t CNTSTATUS_CH2_LTCB_LATCHED = 2097152u;
        static constexpr uint32_t CNTSTATUS_CH2_ZLTC_LATCHED = 4194304u;
        static constexpr uint32_t CNTSTATUS_CH2_RESET = 8388608u;
        static constexpr uint32_t CNTSTATUS_CH2_TRIGGER = 16777216u;
        static constexpr uint32_t CNTSTATUS_CH2_COMPARISON = 33554432u;

        // ═════════════════════════════ Nạp DLL (chỉ có ở bản C++) ═════════════════════════════

        static constexpr const char* DllFileName = "EziMOTIONPlusE.dll";

        /// <summary>Mã mọi hàm FAS_* trả mã lỗi FMM_* trả về khi DLL chưa nạp / thiếu entry point: FMM_NOT_OPEN.</summary>
        static constexpr int32_t NotLoadedCode = FMM_NOT_OPEN;

        /// <summary>
        /// Nạp EziMOTIONPlusE.dll: trước hết <b>cạnh exe</b> (GetModuleFileNameW), sau đó đường tìm DLL mặc định của
        /// Windows; rồi GetProcAddress đủ 151 hàm. Đã nạp thì trả true ngay (gọi nhiều lần, nhiều luồng đều an toàn);
        /// chưa nạp được thì mỗi lần gọi thử lại. Trả false ⇔ bản C# sẽ ném DllNotFoundException — error / LoadError()
        /// nói rõ đã tìm ở đâu và mã lỗi Win32. Thành công thì error rỗng.
        /// </summary>
        static bool Load(std::string& error);

        /// <summary>Như Load(error) nhưng chỉ nạp đúng file dllPath (không tìm chỗ khác). Dùng cho kiểm thử.</summary>
        static bool Load(const std::filesystem::path& dllPath, std::string& error);

        static bool IsLoaded();

        /// <summary>Lỗi của lần Load hỏng gần nhất (tiếng Anh); rỗng khi đã nạp hoặc chưa thử.</summary>
        static std::string LoadError();

        /// <summary>GetLastError() của lần LoadLibraryExW hỏng cuối (126 = không tìm thấy, 193 = sai kiến trúc…); 0 nếu không có.</summary>
        static uint32_t LoadWin32Error();

        /// <summary>Đường dẫn file DLL đã nạp; rỗng nếu chưa nạp.</summary>
        static std::filesystem::path LoadedPath();

        /// <summary>Tên đủ 151 entry point mà bản C# khai [DllImport] (cũng là danh sách Load đi resolve).</summary>
        static const std::vector<std::string_view>& SymbolNames();

        /// <summary>DLL đã nạp và có entry point này.</summary>
        static bool HasSymbol(std::string_view name);

        /// <summary>Entry point không có trong DLL đã nạp (rỗng = đủ hết). Consumer nên log nếu khác rỗng.</summary>
        static std::vector<std::string> MissingSymbols();

        /// <summary>
        /// Gỡ DLL (FreeLibrary) và quay về trạng thái chưa nạp. CHỈ cho kiểm thử: không luồng nào khác được đang gọi FAS_*.
        /// Bản sản xuất không bao giờ gỡ (CLR cũng không gỡ DLL P/Invoke).
        /// </summary>
        static void Unload();

        // ═════════════════════════════ Connection Functions ═════════════════════════════

        /// <summary>Mở kết nối UDP tới board (iBdID là định danh nội bộ của thư viện — xem FastechBoardId). true = mở được socket.</summary>
        static bool FAS_Connect(const IPAddress& ipaddr, int32_t iBdID);
        /// <summary>Như FAS_Connect nhưng giao thức TCP.</summary>
        static bool FAS_ConnectTCP(const IPAddress& ipaddr, int32_t iBdID);
        static bool FAS_IsBdIDExist(int32_t iBdID, IPAddress& ipaddr);
        static bool FAS_IsIPAddressExist(const IPAddress& ipaddr, int32_t& iBdID);
        /// <summary>Native BOOL: khác 0 = thành công.</summary>
        static int32_t FAS_Reconnect(int32_t iBdID);
        /// <summary>Header gốc: void. C# khai int (giá trị trả về là rác) — bản C++ trả 1 nếu đã gọi được, 0 nếu DLL chưa nạp.</summary>
        static int32_t FAS_SetAutoReconnect(int32_t bSET);
        static void FAS_Close(int32_t iBdID);
        /// <summary>Native BOOL: khác 0 = có thiết bị trả lời ở iBdID.</summary>
        static int32_t FAS_IsSlaveExist(int32_t iBdID);

        // ═════════════════════════════ Log Functions ═════════════════════════════
        static void FAS_EnableLog(int32_t bEnable);
        static void FAS_SetLogLevel(LOG_LEVEL level);
        /// <summary>Native BOOL FAS_SetLogPath(LPCWSTR). Khác 0 = thành công.</summary>
        static int32_t FAS_SetLogPath(const wchar_t* lpPath);
        /// <summary>Như trên, đường dẫn UTF-8 (đổi sang UTF-16 ở đây).</summary>
        static int32_t FAS_SetLogPath(const std::string& lpPath);
        /// <summary>Header gốc: void (LPCTSTR — DLL build Unicode). Trả 1 nếu đã gọi được, 0 nếu DLL chưa nạp. lpszMsg UTF-8.</summary>
        static int32_t FAS_PrintCustomLog(int32_t iBdID, LOG_LEVEL level, const std::string& lpszMsg);

        // ═════════════════════════════ Ethernet Address Functions ═════════════════════════════
        static int32_t FAS_GetEthernetAddr(int32_t iBdID, uint32_t& gateway, uint32_t& subnet, uint32_t& ip);
        static int32_t FAS_SetEthernetAddr(int32_t iBdID, uint32_t gateway, uint32_t subnet, uint32_t ip);
        static int32_t FAS_GetMACAddress(int32_t iBdID, uint64_t& MACAddress);

        // ═════════════════════════════ Info Functions ═════════════════════════════
        /// <summary>Loại driver + chuỗi phiên bản (ANSI → UTF-8). Chỉ ghi pType / version khi trả FMM_OK.</summary>
        static int32_t FAS_GetSlaveInfo(int32_t iBdID, uint8_t& pType, std::string& version);
        static int32_t FAS_GetMotorInfo(int32_t iBdID, uint8_t& pType, std::string& motor);
        static int32_t FAS_GetSlaveInfoEx(int32_t iBdID, DRIVE_INFO& DriveInfo);

        // ═════════════════════════════ Parameter Functions ═════════════════════════════
        static int32_t FAS_SaveAllParameters(int32_t iBdID);
        static int32_t FAS_SetParameter(int32_t iBdID, uint8_t iParamNo, int32_t lParamValue);
        static int32_t FAS_GetParameter(int32_t iBdID, uint8_t iParamNo, int32_t& lParamValue);
        static int32_t FAS_GetROMParameter(int32_t iBdID, uint8_t iParamNo, int32_t& lRomParam);

        // ═════════════════════════════ IO Functions (driver) ═════════════════════════════
        static int32_t FAS_SetIOInput(int32_t iBdID, uint32_t dwIOSETMask, uint32_t dwIOCLRMask);
        static int32_t FAS_GetIOInput(int32_t iBdID, uint32_t& dwIOInput);
        static int32_t FAS_SetIOOutput(int32_t iBdID, uint32_t dwIOSETMask, uint32_t dwIOCLRMask);
        static int32_t FAS_GetIOOutput(int32_t iBdID, uint32_t& dwIOOutput);
        static int32_t FAS_GetIOAssignMap(int32_t iBdID, uint8_t iIOPinNo, uint32_t& dwIOLogicMask, uint8_t& bLevel);
        static int32_t FAS_SetIOAssignMap(int32_t iBdID, uint8_t iIOPinNo, uint32_t dwIOLogicMask, uint8_t bLevel);
        static int32_t FAS_IOAssignMapReadROM(int32_t iBdID);

        // ═════════════════════════════ Servo Driver Control Functions ═════════════════════════════
        static int32_t FAS_ServoEnable(int32_t iBdID, int32_t bOnOff);
        static int32_t FAS_ServoAlarmReset(int32_t iBdID);
        static int32_t FAS_StepAlarmReset(int32_t iBdID, int32_t bReset);
        static int32_t FAS_BrakeSet(int32_t iBdID, int32_t bSet, int32_t& nResult);

        // ═════════════════════════════ Read Status and Position ═════════════════════════════
        static int32_t FAS_GetAxisStatus(int32_t iBdID, uint32_t& dwAxisStatus);
        static int32_t FAS_GetIOAxisStatus(int32_t iBdID, uint32_t& dwInStatus, uint32_t& dwOutStatus, uint32_t& dwAxisStatus);
        static int32_t FAS_GetMotionStatus(int32_t iBdID, int32_t& lCmdPos, int32_t& lActPos, int32_t& lPosErr, int32_t& lActVel, uint16_t& wPosItemNo);
        static int32_t FAS_GetAllStatus(int32_t iBdID, uint32_t& dwInStatus, uint32_t& dwOutStatus, uint32_t& dwAxisStatus,
                                        int32_t& lCmdPos, int32_t& lActPos, int32_t& lPosErr, int32_t& lActVel, uint16_t& wPosItemNo);
        /// <summary>pTypes[ALLSTATUSEX_ITEM_COUNT] (STATUSEX_TYPE_*) → pDatas[ALLSTATUSEX_ITEM_COUNT].</summary>
        static int32_t FAS_GetAllStatusEx(int32_t iBdID, uint8_t* pTypes, int32_t* pDatas);
        static int32_t FAS_SetCommandPos(int32_t iBdID, int32_t lCmdPos);
        static int32_t FAS_SetActualPos(int32_t iBdID, int32_t lActPos);
        static int32_t FAS_ClearPosition(int32_t iBdID);
        static int32_t FAS_GetCommandPos(int32_t iBdID, int32_t& lCmdPos);
        static int32_t FAS_GetActualPos(int32_t iBdID, int32_t& lActPos);
        static int32_t FAS_GetPosError(int32_t iBdID, int32_t& lPosErr);
        static int32_t FAS_GetActualVel(int32_t iBdID, int32_t& lActVel);
        static int32_t FAS_GetAlarmType(int32_t iBdID, uint8_t& nAlarmType);
        static int32_t FAS_GetWarningType(int32_t iBdID, uint8_t& nWarningType);

        // ═════════════════════════════ Motion Functions ═════════════════════════════
        static int32_t FAS_MoveStop(int32_t iBdID);
        static int32_t FAS_EmergencyStop(int32_t iBdID);
        static int32_t FAS_MovePause(int32_t iBdID, int32_t bPause);
        static int32_t FAS_MoveOriginSingleAxis(int32_t iBdID);
        static int32_t FAS_MoveSingleAxisAbsPos(int32_t iBdID, int32_t lAbsPos, uint32_t lVelocity);
        static int32_t FAS_MoveSingleAxisIncPos(int32_t iBdID, int32_t lIncPos, uint32_t lVelocity);
        static int32_t FAS_MoveToLimit(int32_t iBdID, uint32_t lVelocity, int32_t iLimitDir);
        /// <summary>Chạy liên tục (jog). iVelDir: DIR_INC = 1, DIR_DEC = 0.</summary>
        static int32_t FAS_MoveVelocity(int32_t iBdID, uint32_t lVelocity, int32_t iVelDir);
        static int32_t FAS_PositionAbsOverride(int32_t iBdID, int32_t lOverridePos);
        static int32_t FAS_PositionIncOverride(int32_t iBdID, int32_t lOverridePos);
        static int32_t FAS_VelocityOverride(int32_t iBdID, uint32_t lVelocity);
        static int32_t FAS_MoveLinearAbsPos(uint8_t nNoOfBds, int32_t* iBdID, int32_t* lplAbsPos, uint32_t lFeedrate, uint16_t wAccelTime);
        static int32_t FAS_MoveLinearIncPos(uint8_t nNoOfBds, int32_t* iBdID, int32_t* lplIncPos, uint32_t lFeedrate, uint16_t wAccelTime);
        static int32_t FAS_MoveLinearAbsPos2(uint8_t nNoOfBds, int32_t* iBdID, int32_t* lplAbsPos, uint32_t lFeedrate, uint16_t wAccelTime);
        static int32_t FAS_MoveLinearIncPos2(uint8_t nNoOfBds, int32_t* iBdID, int32_t* lplIncPos, uint32_t lFeedrate, uint16_t wAccelTime);
        static int32_t FAS_MoveCircleAbsPos1(uint8_t nNoOfBds, int32_t* iBdID, int32_t* lplCirEndAbs, int32_t* lplCirCenterAbs, int32_t iDirection, uint32_t lFeedrate, uint16_t wAccelTime, int32_t bSCurve);
        static int32_t FAS_MoveCircleIncPos1(uint8_t nNoOfBds, int32_t* iBdID, int32_t* lplCirEndInc, int32_t* lplCirCenterInc, int32_t iDirection, uint32_t lFeedrate, uint16_t wAccelTime, int32_t bSCurve);
        static int32_t FAS_MoveCircleAbsPos2(uint8_t nNoOfBds, int32_t* iBdID, int32_t* lplCirEndAbs, uint32_t lRadius, int32_t iDirection, uint32_t lFeedrate, uint16_t wAccelTime, int32_t bSCurve);
        static int32_t FAS_MoveCircleIncPos2(uint8_t nNoOfBds, int32_t* iBdID, int32_t* lplCirEndInc, uint32_t lRadius, int32_t iDirection, uint32_t lFeedrate, uint16_t wAccelTime, int32_t bSCurve);
        static int32_t FAS_MoveCircleAbsPos3(uint8_t nNoOfBds, int32_t* iBdID, int32_t* lplCirCenterAbs, uint32_t nAngle, int32_t iDirecion, uint32_t lFeedrate, uint16_t wAccelTime, int32_t bSCurve);
        static int32_t FAS_MoveCircleIncPos3(uint8_t nNoOfBds, int32_t* iBdID, int32_t* lplCirCenterInc, uint32_t nAngle, int32_t iDirecion, uint32_t lFeedrate, uint16_t wAccelTime, int32_t bSCurve);
        static int32_t FAS_TriggerOutput_RunA(int32_t iBdID, int32_t bStartTrigger, int32_t lStartPos, uint32_t dwPeriod, uint32_t dwPulseTime);
        static int32_t FAS_TriggerOutput_Status(int32_t iBdID, uint8_t& bTriggerStatus);
        static int32_t FAS_SetTriggerOutputEx(int32_t iBdID, uint8_t nOutputNo, uint8_t bRun, uint16_t wOnTime, uint8_t nTriggerCount, int32_t* arrTriggerPosition);
        static int32_t FAS_GetTriggerOutputEx(int32_t iBdID, uint8_t nOutputNo, uint8_t& bRun, uint16_t& wOnTime, uint8_t& nTriggerCount, int32_t* arrTriggerPosition);
        static int32_t FAS_MovePush(int32_t iBdID, uint32_t dwStartSpd, uint32_t dwMoveSpd, int32_t lPosition, uint16_t wAccel, uint16_t wDecel,
                                    uint16_t wPushRate, uint32_t dwPushSpd, int32_t lEndPosition, uint16_t wPushMode);
        static int32_t FAS_GetPushStatus(int32_t iBdID, uint8_t& nPushStatus);

        // ═════════════════════════════ Ex-Motion Functions ═════════════════════════════
        static int32_t FAS_MoveSingleAxisAbsPosEx(int32_t iBdID, int32_t lAbsPos, uint32_t lVelocity, const MOTION_OPTION_EX& ExOption);
        /// <summary>Tên tham số lAbsPos giữ như bản C# (thực chất là quãng tương đối).</summary>
        static int32_t FAS_MoveSingleAxisIncPosEx(int32_t iBdID, int32_t lAbsPos, uint32_t lVelocity, const MOTION_OPTION_EX& ExOption);
        static int32_t FAS_MoveVelocityEx(int32_t iBdID, uint32_t lVelocity, int32_t iVelDir, const VELOCITY_OPTION_EX& ExOption);

        // ═════════════════════════════ Position Table Functions ═════════════════════════════
        /// <summary>Chỉ chép vào Item khi trả FMM_OK.</summary>
        static int32_t FAS_PosTableReadItem(int32_t iBdID, uint16_t wItemNo, ITEM_NODE& Item);
        static int32_t FAS_PosTableWriteItem(int32_t iBdID, uint16_t wItemNo, const ITEM_NODE& Item);
        static int32_t FAS_PosTableWriteROM(int32_t iBdID);
        static int32_t FAS_PosTableReadROM(int32_t iBdID);
        static int32_t FAS_PosTableRunItem(int32_t iBdID, uint16_t wItemNo);
        static int32_t FAS_PosTableReadOneItem(int32_t iBdID, uint16_t wItemNo, uint16_t wOffset, int32_t& lPosItemVal);
        static int32_t FAS_PosTableWriteOneItem(int32_t iBdID, uint16_t wItemNo, uint16_t wOffset, int32_t lPosItemVal);
        static int32_t FAS_PosTableSingleRunItem(int32_t iBdID, int32_t bNextMove, uint16_t wItemNo);

        // ═════════════════════════════ Gap Control Functions ═════════════════════════════
        static int32_t FAS_GapControlEnable(int32_t iBdID, uint16_t wItemNo, int32_t lGapCompSpeed, int32_t lGapAccTime, int32_t lGapDecTime, int32_t lGapStartSpeed);
        static int32_t FAS_GapControlDisable(int32_t iBdID);
        static int32_t FAS_IsGapControlEnable(int32_t iBdID, int32_t& bIsEnable, uint16_t& wCurrentItemNo);
        static int32_t FAS_GapControlGetADCValue(int32_t iBdID, int32_t& lADCValue);
        static int32_t FAS_GapOneResultMonitor(int32_t iBdID, uint8_t& bUpdated, int32_t& iIndex, int32_t& lGapValue, int32_t& lCmdPos,
                                               int32_t& lActPos, int32_t& lCompValue, int32_t& lReserved);

        // ═════════════════════════════ Alarm Type History Functions ═════════════════════════════
        /// <summary>Chỉ chép vào AlarmLog khi trả FMM_OK.</summary>
        static int32_t FAS_GetAlarmLogs(int32_t iBdID, ALARM_LOG& AlarmLog);
        static int32_t FAS_ResetAlarmLogs(int32_t iBdID);
        static int32_t FAS_GetWarningLogs(int32_t iBdID, ALARM_LOG& WarningLog);
        static int32_t FAS_ResetWarningLogs(int32_t iBdID);

        // ═════════════════════════════ I/O Module Functions (Ezi-IO) ═════════════════════════════
        /// <summary>Đọc 32 ngõ vào (uInput) và cờ chốt (uLatch) của card Ezi-IO.</summary>
        static int32_t FAS_GetInput(int32_t iBdID, uint32_t& uInput, uint32_t& uLatch);
        static int32_t FAS_ClearLatch(int32_t iBdID, uint32_t uLatchMask);
        /// <summary>Header gốc: iInputNo là unsigned char (C# khai uint — native chỉ đọc byte thấp; bản C++ cắt về uint8_t).</summary>
        static int32_t FAS_GetLatchCount(int32_t iBdID, uint32_t iInputNo, uint32_t& uCount);
        static int32_t FAS_GetLatchCountAll(int32_t iBdID, uint32_t* puAllCount);
        static int32_t FAS_GetLatchCountAll32(int32_t iBdID, uint32_t* puAllCount);
        static int32_t FAS_ClearLatchCount(int32_t iBdID, uint32_t uInputMask);
        /// <summary>Đọc ảnh 32 ngõ ra card đang giữ (uOutput) và trạng thái run/stop trigger (uStatus).</summary>
        static int32_t FAS_GetOutput(int32_t iBdID, uint32_t& uOutput, uint32_t& uStatus);
        /// <summary>Ghi ngõ ra theo mặt nạ: bit 1 trong uSetMask → bật, bit 1 trong uClearMask → tắt, cả 32 bit một gói.</summary>
        static int32_t FAS_SetOutput(int32_t iBdID, uint32_t uSetMask, uint32_t uClearMask);
        static int32_t FAS_SetTrigger(int32_t iBdID, uint8_t uOutputNo, TRIGGER_INFO& trigger);
        static int32_t FAS_SetRunStop(int32_t iBdID, uint32_t uRunMask, uint32_t uStopMask);
        /// <summary>Header gốc: uOutputNo là unsigned char (C# khai uint — bản C++ cắt về uint8_t như native đọc).</summary>
        static int32_t FAS_GetTriggerCount(int32_t iBdID, uint32_t uOutputNo, uint32_t& uCount);
        static int32_t FAS_GetIOLevel(int32_t iBdID, uint32_t& uIOLevel);
        static int32_t FAS_SetIOLevel(int32_t iBdID, uint32_t uIOLevel);
        static int32_t FAS_LoadIOLevel(int32_t iBdID);
        static int32_t FAS_SaveIOLevel(int32_t iBdID);
        static int32_t FAS_GetInputFilter(int32_t iBdID, uint16_t& filter);
        static int32_t FAS_SetInputFilter(int32_t iBdID, uint16_t filter);
        static int32_t FAS_GetIODirection(int32_t iBdID, uint32_t& direction);
        static int32_t FAS_SetIODirection(int32_t iBdID, uint32_t direction);

        // ═════════════════════════════ Ezi-IO AD Functions ═════════════════════════════
        /// <summary>type: AD_DATA_TYPE dưới dạng int (như bản C#).</summary>
        static int32_t FAS_SetADConfig(int32_t iBdID, uint8_t channel, int32_t type, int32_t value, int32_t& recv);
        static int32_t FAS_GetADConfig(int32_t iBdID, uint8_t channel, int32_t type, int32_t& value);
        static int32_t FAS_LoadADConfig(int32_t iBdID);
        static int32_t FAS_SaveADConfig(int32_t iBdID);
        static int32_t FAS_ReadADValue(int32_t iBdID, uint8_t channel, int16_t& advalue);
        /// <summary>adbuffer luôn được gán (kể cả khi hỏng — khi đó mọi kênh = 0), như bản C#.</summary>
        static int32_t FAS_ReadADAllValue(int32_t iBdID, uint8_t offset, AD_BUFFER& adbuffer);
        /// <summary>result luôn được gán (kể cả khi hỏng), như bản C#.</summary>
        static int32_t FAS_GetAllADResult(int32_t iBdID, AD_RESULT& result);
        static int32_t FAS_GetADResult(int32_t iBdID, uint8_t channel, float& adresult);
        static int32_t FAS_SetADRange(int32_t iBdID, uint8_t channel, AD_RANGE range);

        // ═════════════════════════════ Ezi-IO DA Functions ═════════════════════════════
        static int32_t FAS_SetDACConfig(int32_t iBdID, uint8_t channel, DAC_CONFIG type, int32_t data, int32_t& recv);
        static int32_t FAS_GetDACConfig(int32_t iBdID, uint8_t channel, DAC_CONFIG type, int32_t& data);
        static int32_t FAS_LoadDACConfig(int32_t iBdID);
        static int32_t FAS_SaveDACConfig(int32_t iBdID);
        static int32_t FAS_SetDACValue(int32_t iBdID, uint8_t channel, int32_t bEnable, int32_t value);
        static int32_t FAS_GetDACValue(int32_t iBdID, uint8_t channel, int32_t& bEnable, int32_t& value);

        // ═════════════════════════════ Ezi-IO Counter Functions ═════════════════════════════
        static int32_t FAS_CounterCommand(int32_t iBdID, uint8_t channel, COUNTER_CMD cmd, int32_t data);
        static int32_t FAS_GetCounterValue(int32_t iBdID, uint8_t channel, COUNTER_VALUE type, int32_t& value);
        static int32_t FAS_GetCounterStatus(int32_t iBdID, uint32_t& dwStatus);
        static int32_t FAS_SetCounterTrigger(int32_t iBdID, uint8_t channel, bool bStartTrigger, int32_t lStartPos, uint32_t dwPeriod,
                                             uint32_t dwPulseTime, uint32_t dwTriggerCnt);
        static int32_t FAS_SetCounterConfig(int32_t iBdID, uint8_t channel, COUNTER_CONFIG type, int32_t data, int32_t& recv);
        static int32_t FAS_GetCounterConfig(int32_t iBdID, uint8_t channel, COUNTER_CONFIG type, int32_t& data);
        static int32_t FAS_LoadCounterConfig(int32_t iBdID);
        static int32_t FAS_SaveCounterConfig(int32_t iBdID);

        // ═════════════════════════════ Ezi-SPEED Functions ═════════════════════════════
        static int32_t FAS_SPEEDGetInputStatus(int32_t iBdID, uint32_t& dwDirectInput);
        static int32_t FAS_SPEEDGetOutputStatus(int32_t iBdID, uint32_t& dwDirectOutput);
        static int32_t FAS_SPEEDGetNETStatus(int32_t iBdID, uint32_t& dwNETStatus);
        static int32_t FAS_SPEEDSetOutput(int32_t iBdID, uint32_t dwIOSETMask, uint32_t dwIOCLRMask);
        static int32_t FAS_SPEEDGetCommandSpeed(int32_t iBdID, int32_t& nSpeed);
        static int32_t FAS_SPEEDGetFeedbackSpeed(int32_t iBdID, int32_t& nSpeed);
        static int32_t FAS_SPEEDGetGearSpeed(int32_t iBdID, float& fSpeed);
        static int32_t FAS_SPEEDGetConveyorSpeed(int32_t iBdID, float& fSpeed);
        static int32_t FAS_SPEEDGetOperationNo(int32_t iBdID, uint8_t& nOpNo);
        static int32_t FAS_SPEEDMoveVelocity(int32_t iBdID, uint16_t uSpeed, int32_t bForward, uint16_t uAccTime, uint16_t uDecTime, uint16_t uTorqueLimit);
        static int32_t FAS_SPEEDSetOperationData(int32_t iBdID, uint8_t nOpNo, uint16_t uSpeed, uint16_t uAccTime, uint16_t uDecTime, uint16_t uTorqueLimit);
        static int32_t FAS_SPEEDGetOperationData(int32_t iBdID, uint8_t nOpNo, uint16_t& uSpeed, uint16_t& uAccTime, uint16_t& uDecTime, uint16_t& uTorqueLimit);
        static int32_t FAS_SPEEDRunOperationData(int32_t iBdID, uint8_t nOpNo, int32_t bForward);
        static int32_t FAS_SPEEDLoadOperationData(int32_t iBdID);
        static int32_t FAS_SPEEDSaveOperationData(int32_t iBdID);

    protected:
        // ── Overload thô (C#: protected static extern) — chỉ wrapper public ở trên gọi tới ──
        static int32_t FAS_Connect(uint8_t sb1, uint8_t sb2, uint8_t sb3, uint8_t sb4, int32_t iBdID);      // UDP Protocol
        static int32_t FAS_ConnectTCP(uint8_t sb1, uint8_t sb2, uint8_t sb3, uint8_t sb4, int32_t iBdID);   // TCP Protocol
        static int32_t FAS_IsBdIDExist(int32_t iBdID, uint8_t& sb1, uint8_t& sb2, uint8_t& sb3, uint8_t& sb4);
        static int32_t FAS_IsIPAddressExist(uint8_t sb1, uint8_t sb2, uint8_t sb3, uint8_t sb4, int32_t& iBdID);
        static void FAS_SetLogLevel(int32_t level);
        static int32_t FAS_PrintCustomLog(int32_t iBdID, int32_t level, const wchar_t* lpszMsg);
        static int32_t FAS_GetSlaveInfo(int32_t iBdID, uint8_t& pType, char* lpBuff, int32_t nBuffSize);
        static int32_t FAS_GetMotorInfo(int32_t iBdID, uint8_t& pType, char* lpBuff, int32_t nBuffSize);
        static int32_t FAS_GetSlaveInfoEx(int32_t iBdID, uint8_t* lpDriveInfo);
        static int32_t FAS_MoveSingleAxisAbsPosEx(int32_t iBdID, int32_t lAbsPos, uint32_t lVelocity, uint8_t* lpExOption);
        static int32_t FAS_MoveSingleAxisIncPosEx(int32_t iBdID, int32_t lIncPos, uint32_t lVelocity, uint8_t* lpExOption);
        static int32_t FAS_MoveVelocityEx(int32_t iBdID, uint32_t lVelocity, int32_t iVelDir, uint8_t* lpExOption);
        static int32_t FAS_PosTableReadItem(int32_t iBdID, uint16_t wItemNo, uint8_t* lpItem);
        static int32_t FAS_PosTableWriteItem(int32_t iBdID, uint16_t wItemNo, uint8_t* lpItem);
        static int32_t FAS_GetAlarmLogs(int32_t iBdID, uint8_t* pAlarmLog);
        static int32_t FAS_GetWarningLogs(int32_t iBdID, uint8_t* pWarningLog);
        static int32_t FAS_SetTrigger(int32_t iBdID, uint8_t uOutputNo, uint8_t* pTrigger);
        static int32_t FAS_ReadADAllValue(int32_t iBdID, uint8_t offset, uint8_t* adbuffer);
        static int32_t FAS_GetAllADResult(int32_t iBdID, uint8_t* result);
        static int32_t FAS_SetADRange(int32_t iBdID, uint8_t channel, int32_t range);
        static int32_t FAS_SetCounterTrigger(int32_t iBdID, uint8_t channel, int32_t bStartTrigger, int32_t lStartPos, uint32_t dwPeriod,
                                             uint32_t dwPulseTime, uint32_t dwTriggerCnt);
    };

#undef SCA_EZI_PROP
#undef SCA_EZI_GET
}
