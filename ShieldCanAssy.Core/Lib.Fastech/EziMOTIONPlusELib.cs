// ═══════════════════════════════════════════════════════════════════════════════════
//  File của Fastech (bản dựng lại từ project Ama_Actro_HousingCorrosion) — GIỮ NGUYÊN,
//  không sửa. Muốn đổi hành vi thì bọc ở FastechAxis / IoConnect, để sau này thay bản
//  SDK mới chỉ cần chép đè.
//  CS0219: vài biến thừa trong bản dựng lại. Chặn riêng ở đây để phần code của mình
//  vẫn giữ được mức 0 cảnh báo, nhờ vậy cảnh báo mới xuất hiện là nhìn thấy ngay.
// ═══════════════════════════════════════════════════════════════════════════════════
#pragma warning disable CS0219

using System;
using System.Net;
using System.Runtime.InteropServices;
using System.Text;

namespace FASTECH;

public class EziMOTIONPlusELib
{
	public class ITEM_NODE
	{
		private byte[] buffer;

		public const int BUFF_SIZE = 64;

		public int lPosition
		{
			get
			{
				return BitConverter.ToInt32(buffer, 0);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 0, 4);
			}
		}

		public uint dwStartSpd
		{
			get
			{
				return BitConverter.ToUInt32(buffer, 4);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 4, 4);
			}
		}

		public uint dwMoveSpd
		{
			get
			{
				return BitConverter.ToUInt32(buffer, 8);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 8, 4);
			}
		}

		public ushort wAccelTime
		{
			get
			{
				return BitConverter.ToUInt16(buffer, 12);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 12, 2);
			}
		}

		public ushort wDecelTime
		{
			get
			{
				return BitConverter.ToUInt16(buffer, 14);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 14, 2);
			}
		}

		public ushort wCommand
		{
			get
			{
				return BitConverter.ToUInt16(buffer, 16);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 16, 2);
			}
		}

		public ushort wWaitTime
		{
			get
			{
				return BitConverter.ToUInt16(buffer, 18);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 18, 2);
			}
		}

		public ushort wContinuous
		{
			get
			{
				return BitConverter.ToUInt16(buffer, 20);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 20, 2);
			}
		}

		public ushort wBranch
		{
			get
			{
				return BitConverter.ToUInt16(buffer, 22);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 22, 2);
			}
		}

		public ushort wCond_branch0
		{
			get
			{
				return BitConverter.ToUInt16(buffer, 24);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 24, 2);
			}
		}

		public ushort wCond_branch1
		{
			get
			{
				return BitConverter.ToUInt16(buffer, 26);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 26, 2);
			}
		}

		public ushort wCond_branch2
		{
			get
			{
				return BitConverter.ToUInt16(buffer, 28);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 28, 2);
			}
		}

		public ushort wLoopCount
		{
			get
			{
				return BitConverter.ToUInt16(buffer, 30);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 30, 2);
			}
		}

		public ushort wBranchAfterLoop
		{
			get
			{
				return BitConverter.ToUInt16(buffer, 32);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 32, 2);
			}
		}

		public ushort wPTSet
		{
			get
			{
				return BitConverter.ToUInt16(buffer, 34);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 34, 2);
			}
		}

		public ushort wLoopCountCLR
		{
			get
			{
				return BitConverter.ToUInt16(buffer, 36);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 36, 2);
			}
		}

		public ushort bCheckInpos
		{
			get
			{
				return BitConverter.ToUInt16(buffer, 38);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 38, 2);
			}
		}

		public int lTriggerPos
		{
			get
			{
				return BitConverter.ToInt32(buffer, 40);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 40, 4);
			}
		}

		public ushort wTriggerOnTime
		{
			get
			{
				return BitConverter.ToUInt16(buffer, 44);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 44, 2);
			}
		}

		public ushort wPushRatio
		{
			get
			{
				return BitConverter.ToUInt16(buffer, 46);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 46, 2);
			}
		}

		public uint dwPushSpeed
		{
			get
			{
				return BitConverter.ToUInt32(buffer, 48);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 48, 4);
			}
		}

		public int lPushPosition
		{
			get
			{
				return BitConverter.ToInt32(buffer, 52);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 52, 4);
			}
		}

		public ushort wPushMode
		{
			get
			{
				return BitConverter.ToUInt16(buffer, 56);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 56, 2);
			}
		}

		public ITEM_NODE()
		{
			buffer = new byte[64];
		}

		public void copyfrom(byte[] data)
		{
			int count = ((data.GetLength(0) < 64) ? data.GetLength(0) : 64);
			Buffer.BlockCopy(data, 0, buffer, 0, count);
		}

		public void copyto(byte[] data)
		{
			int count = ((data.GetLength(0) < 64) ? data.GetLength(0) : 64);
			Buffer.BlockCopy(buffer, 0, data, 0, count);
		}

		public void Clear()
		{
			for (int i = 0; i < 64; i++)
			{
				buffer[i] = 0;
			}
			wCommand = ushort.MaxValue;
			wBranch = ushort.MaxValue;
			wCond_branch0 = ushort.MaxValue;
			wCond_branch1 = ushort.MaxValue;
			wCond_branch2 = ushort.MaxValue;
			wBranchAfterLoop = ushort.MaxValue;
			wLoopCountCLR = ushort.MaxValue;
		}
	}

	public class MOTION_OPTION_EX
	{
		private byte[] buffer;

		public const int BUFF_SIZE = 32;

		public int dwOptionFlag
		{
			get
			{
				return BitConverter.ToInt32(buffer, 0);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 0, 4);
			}
		}

		public bool BIT_IGNOREEXSTOP
		{
			get
			{
				byte b = (byte)(buffer[0] & 1);
				return b != 0;
			}
			set
			{
				byte b = (value ? ((byte)(buffer[0] | 1)) : ((byte)(buffer[0] & 0xFE)));
				buffer[0] = b;
			}
		}

		public bool BIT_USE_CUSTOMACCEL
		{
			get
			{
				byte b = (byte)(buffer[0] & 2);
				return b != 0;
			}
			set
			{
				byte b = (value ? ((byte)(buffer[0] | 2)) : ((byte)(buffer[0] & 0xFD)));
				buffer[0] = b;
			}
		}

		public bool BIT_USE_CUSTOMDECEL
		{
			get
			{
				byte b = (byte)(buffer[0] & 4);
				return b != 0;
			}
			set
			{
				byte b = (value ? ((byte)(buffer[0] | 4)) : ((byte)(buffer[0] & 0xFB)));
				buffer[0] = b;
			}
		}

		public ushort wCustomAccelTime
		{
			get
			{
				return BitConverter.ToUInt16(buffer, 4);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 4, 2);
			}
		}

		public ushort wCustomDecelTime
		{
			get
			{
				return BitConverter.ToUInt16(buffer, 6);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 6, 2);
			}
		}

		public MOTION_OPTION_EX()
		{
			buffer = new byte[32];
		}

		public void copyto(byte[] data)
		{
			int count = ((data.GetLength(0) < 32) ? data.GetLength(0) : 32);
			Buffer.BlockCopy(buffer, 0, data, 0, count);
		}

		public void Clear()
		{
			for (int i = 0; i < 32; i++)
			{
				buffer[i] = 0;
			}
		}
	}

	public class VELOCITY_OPTION_EX
	{
		private byte[] buffer;

		public const int BUFF_SIZE = 32;

		public int dwOptionFlag
		{
			get
			{
				return BitConverter.ToInt32(buffer, 0);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 0, 4);
			}
		}

		public bool BIT_IGNOREEXSTOP
		{
			get
			{
				byte b = (byte)(buffer[0] & 1);
				return b != 0;
			}
			set
			{
				byte b = (value ? ((byte)(buffer[0] | 1)) : ((byte)(buffer[0] & 0xFE)));
				buffer[0] = b;
			}
		}

		public bool BIT_USE_CUSTOMACCDEC
		{
			get
			{
				byte b = (byte)(buffer[0] & 2);
				return b != 0;
			}
			set
			{
				byte b = (value ? ((byte)(buffer[0] | 2)) : ((byte)(buffer[0] & 0xFD)));
				buffer[0] = b;
			}
		}

		public ushort wCustomAccDecTime
		{
			get
			{
				return BitConverter.ToUInt16(buffer, 4);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 4, 2);
			}
		}

		public VELOCITY_OPTION_EX()
		{
			buffer = new byte[32];
		}

		public void copyto(byte[] data)
		{
			int count = ((data.GetLength(0) < 32) ? data.GetLength(0) : 32);
			Buffer.BlockCopy(buffer, 0, data, 0, count);
		}

		public void Clear()
		{
			for (int i = 0; i < 32; i++)
			{
				buffer[i] = 0;
			}
		}
	}

	public class ALARM_LOG
	{
		private byte[] buffer;

		public const int BUFF_SIZE = 31;

		public byte nAlarmCount
		{
			get
			{
				return buffer[0];
			}
			set
			{
				buffer[0] = nAlarmCount;
			}
		}

		public ALARM_LOG()
		{
			buffer = new byte[31];
		}

		public void copy(byte[] data)
		{
			int count = ((data.GetLength(0) < 31) ? data.GetLength(0) : 31);
			Buffer.BlockCopy(data, 0, buffer, 0, count);
		}

		public void Clear()
		{
			for (int i = 0; i < 31; i++)
			{
				buffer[i] = 0;
			}
		}

		public byte nAlarmLog(uint index)
		{
			return (byte)((index < 30) ? buffer[index + 1] : 0);
		}
	}

	public class DRIVE_INFO
	{
		private byte[] buffer;

		public const int BUFF_SIZE = 70;

		public ushort nVersionNo0 => BitConverter.ToUInt16(buffer, 0);

		public ushort nVersionNo1 => BitConverter.ToUInt16(buffer, 2);

		public ushort nVersionNo2 => BitConverter.ToUInt16(buffer, 4);

		public ushort nVersionNo3 => BitConverter.ToUInt16(buffer, 6);

		public string sVersion
		{
			get
			{
				byte[] array = new byte[30];
				int i;
				for (i = 0; i < 30 && buffer[8 + i] != 0; i++)
				{
				}
				Buffer.BlockCopy(buffer, 8, array, 0, Math.Min(30, i + 1));
				return Encoding.Default.GetString(array).Replace("\0", "");
			}
		}

		public ushort nDriveType => BitConverter.ToUInt16(buffer, 38);

		public ushort nMotorType => BitConverter.ToUInt16(buffer, 40);

		public string sMotorInfo
		{
			get
			{
				byte[] array = new byte[20];
				int i;
				for (i = 0; i < 20 && buffer[42 + i] != 0; i++)
				{
				}
				Buffer.BlockCopy(buffer, 42, array, 0, Math.Min(20, i + 1));
				return Encoding.Default.GetString(array).Replace("\0", "");
			}
		}

		public ushort nInPinNo => BitConverter.ToUInt16(buffer, 62);

		public ushort nOutPinNo => BitConverter.ToUInt16(buffer, 64);

		public ushort nPTNum => BitConverter.ToUInt16(buffer, 66);

		public ushort nFirmwareType => BitConverter.ToUInt16(buffer, 68);

		public DRIVE_INFO()
		{
			buffer = new byte[70];
		}

		public void copy(byte[] data)
		{
			int count = ((data.GetLength(0) < 70) ? data.GetLength(0) : 70);
			Buffer.BlockCopy(data, 0, buffer, 0, count);
		}

		public void Clear()
		{
			for (int i = 0; i < 70; i++)
			{
				buffer[i] = 0;
			}
		}
	}

	public class TRIGGER_INFO
	{
		private byte[] buffer;

		public const int BUFF_SIZE = 12;

		public byte[] ByteArray => buffer;

		public ushort wPeriod
		{
			get
			{
				return BitConverter.ToUInt16(buffer, 0);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 0, 2);
			}
		}

		public ushort wReserved1
		{
			get
			{
				return BitConverter.ToUInt16(buffer, 2);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 2, 2);
			}
		}

		public ushort wOnTime
		{
			get
			{
				return BitConverter.ToUInt16(buffer, 4);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 4, 2);
			}
		}

		public ushort wReserved2
		{
			get
			{
				return BitConverter.ToUInt16(buffer, 6);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 6, 2);
			}
		}

		public uint wCount
		{
			get
			{
				return BitConverter.ToUInt32(buffer, 8);
			}
			set
			{
				byte[] bytes = BitConverter.GetBytes(value);
				Buffer.BlockCopy(bytes, 0, buffer, 8, 4);
			}
		}

		public TRIGGER_INFO()
		{
			buffer = new byte[12];
		}

		public void copy(byte[] data)
		{
			int count = ((data.GetLength(0) < 12) ? data.GetLength(0) : 12);
			Buffer.BlockCopy(data, 0, buffer, 0, count);
		}

		public void Clear()
		{
			for (int i = 0; i < 12; i++)
			{
				buffer[i] = 0;
			}
		}
	}

	public enum AD_RANGE
	{
		ADRANGE_10_to_10 = 0,
		ADRANGE_5_to_5 = 1,
		ADRANGE_2_5_to_2_5 = 2,
		ADRANGE_0_to_10 = 3,
		ADRANGE_0_to_20_C = 4,
		ADRANGE_EX_10_to_10 = 16,
		ADRANGE_EX_5_to_5 = 17,
		ADRANGE_EX_2_5_to_2_5 = 18,
		ADRANGE_EX_0_to_10 = 19
	}

	public class AD_RESULT
	{
		private char[] range;

		private short[] rawdata;

		private float[] converted;

		public int MAX_CHANNEL_COUNT = 16;

		public const int BUFF_SIZE = 112;

		public char[] Range => range;

		public short[] RawData => rawdata;

		public float[] Converted => converted;

		public AD_RESULT(byte[] data)
		{
			range = new char[MAX_CHANNEL_COUNT];
			rawdata = new short[MAX_CHANNEL_COUNT];
			converted = new float[MAX_CHANNEL_COUNT];
			copyfrom(data);
		}

		private void copyfrom(byte[] data)
		{
			byte[] array = new byte[112];
			int count = ((data.GetLength(0) < 112) ? data.GetLength(0) : 112);
			Buffer.BlockCopy(data, 0, array, 0, count);
			for (int i = 0; i < MAX_CHANNEL_COUNT; i++)
			{
				range[i] = (char)array[i * 7];
				rawdata[i] = BitConverter.ToInt16(array, i * 7 + 1);
				converted[i] = (short)BitConverter.ToSingle(array, i * 7 + 3);
			}
		}
	}

	public enum AD_DATA_TYPE
	{
		TYPE_AD_RANGE,
		TYPE_AD_FILTER_LENGTH,
		TYPE_AD_FILTER_OFFSET
	}

	public class AD_BUFFER
	{
		private short[] channel;

		public const int MAX_CHANNEL_COUNT = 8;

		public short[] Channel => channel;

		public AD_BUFFER(byte[] buffer)
		{
			channel = new short[8];
			if (buffer.Length == 16)
			{
				for (int i = 0; i < 8; i++)
				{
					channel[i] = BitConverter.ToInt16(buffer, i * 2);
				}
			}
		}
	}

	public enum DA_RANGE
	{
		DARANGE_0_to_5 = 0,
		DARANGE_10_to_10 = 1,
		DARANGE_0_to_10 = 2,
		DARANGE_1_to_5 = 3,
		DARANGE_0_to_20_C = 4,
		DARANGE_4_to_20_C = 5,
		DARANGE_EX_0_to_5 = 16,
		DARANGE_EX_10_to_10 = 17,
		DARANGE_EX_0_to_20_C = 18,
		DARANGE_EX_4_to_20_C = 19
	}

	public enum DAC_CONFIG
	{
		DAC_RANGE,
		DAC_CALIBRATION_HIGH,
		DAC_CALIBRATION_LOW
	}

	public enum COUNTER_CMD
	{
		CNTCMD_CH_ENABLE,
		CNTCMD_LATCHA_ENABLE,
		CNTCMD_LATCHB_ENABLE,
		CNTCMD_ZLATCH_ENABLE,
		CNTCMD_RESET_ALL,
		CNTCMD_RESET_COUNT,
		CNTCMD_RESET_LATCH
	}

	public enum COUNTER_VALUE
	{
		CNT_COUNT_VALUE,
		CNT_LATCHA_VALUE,
		CNT_LATCHB_VALUE,
		CNT_ZLATCH_VALUE
	}

	public enum COUNTER_CONFIG
	{
		CFG_INPUT_MODE,
		CFG_COUNT_DIRECTION,
		CFG_LATCHA_MODE,
		CFG_LATCHB_MODE,
		CFG_ZLATCH_MODE,
		CFG_LATCHA_LOGIC,
		CFG_LATCHB_LOGIC,
		CFG_ZLATCH_LOGIC,
		CFG_RESET_LOGIC,
		CFG_CP_LOGIC
	}

	public enum LOG_LEVEL
	{
		LOG_LEVEL_COMM,
		LOG_LEVEL_PARAM,
		LOG_LEVEL_MOTION,
		LOG_LEVEL_ALL
	}

	public const int FMM_OK = 0;

	public const int FMM_NOT_OPEN = 1;

	public const int FMM_INVALID_PORT_NUM = 2;

	public const int FMM_INVALID_SLAVE_NUM = 3;

	public const int FMC_DISCONNECTED = 5;

	public const int FMC_TIMEOUT_ERROR = 6;

	public const int FMC_CRCFAILED_ERROR = 7;

	public const int FMC_RECVPACKET_ERROR = 8;

	public const int FMM_POSTABLE_ERROR = 9;

	public const int FMP_FRAMETYPEERROR = 128;

	public const int FMP_DATAERROR = 129;

	public const int FMP_PACKETERROR = 130;

	public const int FMP_RUNFAIL = 133;

	public const int FMP_RESETFAIL = 134;

	public const int FMP_SERVOONFAIL1 = 135;

	public const int FMP_SERVOONFAIL2 = 136;

	public const int FMP_SERVOONFAIL3 = 137;

	public const int FMP_SERVOOFF_FAIL = 138;

	public const int FMP_ROMACCESS = 139;

	public const int FMP_PACKETCRCERROR = 170;

	public const int FMM_UNKNOWN_ERROR = 255;

	public const byte DEVTYPE_EZI_SERVO2_PLUS_E_ST = 100;

	public const string DEVNAME_EZI_SERVO2_PLUS_E_ST = "Ezi-SERVO II Plus-E-ST";

	public const byte DEVTYPE_S_SERVO_PLUS_E_ST = 102;

	public const string DEVNAME_S_SERVO_PLUS_E_ST = "S-SERVO Plus-E-ST";

	public const byte DEVTYPE_EZI_IO_PLUS_E_IN16 = 150;

	public const string DEVNAME_EZI_IO_PLUS_E_IN16 = "Ezi-IO Ethernet-IN16";

	public const byte DEVTYPE_EZI_IO_PLUS_E_IN32 = 151;

	public const string DEVNAME_EZI_IO_PLUS_E_IN32 = "Ezi-IO Ethernet-IN32";

	public const byte DEVTYPE_EZI_IO_PLUS_E_OUT16 = 160;

	public const string DEVNAME_EZI_IO_PLUS_E_OUT16 = "Ezi-IO Ethernet-OUT16";

	public const byte DEVTYPE_EZI_IO_PLUS_E_OUT32 = 161;

	public const string DEVNAME_EZI_IO_PLUS_E_OUT32 = "Ezi-IO Ethernet-OUT32";

	public const byte DEVTYPE_EZI_IO_PLUS_E_I8O8 = 155;

	public const string DEVNAME_EZI_IO_PLUS_E_I8O8 = "Ezi-IO Ethernet-I8O8";

	public const byte DEVTYPE_EZI_IO_PLUS_E_I16O16 = 156;

	public const string DEVNAME_EZI_IO_PLUS_E_I16O16 = "Ezi-IO Ethernet-I16O16";

	public const byte DEVTYPE_EZI_IO_PLUS_E_IO16 = 152;

	public const string DEVNAME_EZI_IO_PLUS_E_IO16 = "Ezi-IO Ethernet-IO16";

	public const int DIR_INC = 1;

	public const int DIR_DEC = 0;

	public const byte MAX_AXIS_STATUS = 32;

	public const byte ALLSTATUSEX_ITEM_COUNT = 12;

	public const byte STATUSEX_TYPE_NONE = 0;

	public const byte STATUSEX_TYPE_INPUT = 1;

	public const byte STATUSEX_TYPE_OUTPUT = 2;

	public const byte STATUSEX_TYPE_AXISSTATUS = 3;

	public const byte STATUSEX_TYPE_CMDPOS = 4;

	public const byte STATUSEX_TYPE_ACTPOS = 5;

	public const byte STATUSEX_TYPE_ACTVEL = 6;

	public const byte STATUSEX_TYPE_POSERR = 7;

	public const byte STATUSEX_TYPE_PTNO = 8;

	public const byte STATUSEX_TYPE_ALARMTYPE = 9;

	public const byte STATUSEX_TYPE_TEMPERATURE = 10;

	public const byte STATUSEX_TYPE_CURRENT = 11;

	public const byte STATUSEX_TYPE_LOAD = 12;

	public const byte STATUSEX_TYPE_PEAKLOAD = 13;

	public const byte STATUSEX_TYPE_ENCVEL = 14;

	public const byte STATUSEX_TYPE_INPUT_HIGH = 15;

	public const byte STATUSEX_TYPE_PTNO_RUNNING = 16;

	public const byte STATUSEX_TYPE_ADVALUE0 = 30;

	public const byte STATUSEX_TYPE_ADVALUE2 = 31;

	public const byte STATUSEX_TYPE_ADVALUE4 = 32;

	public const byte STATUSEX_TYPE_ADVALUE6 = 33;

	public const byte LEVEL_LOW_ACTIVE = 0;

	public const byte LEVEL_HIGH_ACTIVE = 1;

	public const byte IN_LOGIC_NONE = 0;

	public const byte OUT_LOGIC_NONE = 0;

	public const ushort MAX_LOOP_COUNT = 100;

	public const ushort MAX_WAIT_TIME = 60000;

	public const ushort PUSH_RATIO_MIN = 20;

	public const ushort PUSH_RATIO_MAX = 90;

	public const uint PUSH_SPEED_MIN = 1u;

	public const uint PUSH_SPEED_MAX = 100000u;

	public const uint PUSH_PULSECOUNT_MIN = 1u;

	public const uint PUSH_PULSECOUNT_MAX = 10000u;

	public const ushort CMD_ABS_LOWSPEED = 0;

	public const ushort CMD_ABS_HIGHSPEED = 1;

	public const ushort CMD_ABS_HIGHSPEEDDECEL = 2;

	public const ushort CMD_ABS_NORMALMOTION = 3;

	public const ushort CMD_INC_LOWSPEED = 4;

	public const ushort CMD_INC_HIGHSPEED = 5;

	public const ushort CMD_INC_HIGHSPEEDDECEL = 6;

	public const ushort CMD_INC_NORMALMOTION = 7;

	public const ushort CMD_MOVE_ORIGIN = 8;

	public const ushort CMD_COUNTERCLEAR = 9;

	public const ushort CMD_PUSH_ABSMOTION = 10;

	public const ushort CMD_STOP = 11;

	public const ushort CMD_MAX_COUNT = 12;

	public const ushort CMD_NO_COMMAND = ushort.MaxValue;

	public const ushort OFFSET_POSITION = 0;

	public const ushort OFFSET_LOWSPEED = 4;

	public const ushort OFFSET_HIGHSPEED = 8;

	public const ushort OFFSET_ACCELTIME = 12;

	public const ushort OFFSET_DECELTIME = 14;

	public const ushort OFFSET_COMMAND = 16;

	public const ushort OFFSET_WAITTIME = 18;

	public const ushort OFFSET_CONTINUOUS = 20;

	public const ushort OFFSET_JUMPTABLENO = 22;

	public const ushort OFFSET_JUMPPT0 = 24;

	public const ushort OFFSET_JUMPPT1 = 26;

	public const ushort OFFSET_JUMPPT2 = 28;

	public const ushort OFFSET_LOOPCOUNT = 30;

	public const ushort OFFSET_LOOPJUMPTABLENO = 32;

	public const ushort OFFSET_PTSET = 34;

	public const ushort OFFSET_LOOPCOUNTCLEAR = 36;

	public const ushort OFFSET_CHECKINPOSITION = 38;

	public const ushort OFFSET_TRIGGERPOSITION = 40;

	public const ushort OFFSET_TRIGGERONTIME = 44;

	public const ushort OFFSET_PUSHRATIO = 46;

	public const ushort OFFSET_PUSHSPEED = 48;

	public const ushort OFFSET_PUSHPOSITION = 52;

	public const ushort OFFSET_PUSHMODE = 56;

	public const ushort OFFSET_BLANK = 58;

	public const ushort ALARM_NONE = 0;

	public const ushort ALARM_OVERCURRENT = 1;

	public const ushort ALARM_OVERSPEED = 2;

	public const ushort ALARM_STEPOUT = 3;

	public const ushort ALARM_OVERLOAD = 4;

	public const ushort ALARM_OVERTEMPERATURE = 5;

	public const ushort ALARM_OVERBACKEMF = 6;

	public const ushort ALARM_MOTORCONNECT = 7;

	public const ushort ALARM_ENCODERCONNECT = 8;

	public const ushort ALARM_LOWMOTORPOWER = 9;

	public const ushort ALARM_INPOSITION = 10;

	public const ushort ALARM_SYSTEMHALT = 11;

	public const ushort ALARM_ROMDEVICE = 12;

	public const ushort ALARM_HIGHINPUTVOLTAGE = 14;

	public const ushort ALARM_POSITIONOVERFLOW = 15;

	public const ushort ALARM_POSITIONCHANGED = 16;

	public static string[] ALARM_DESCRIPTION = new string[16]
	{
		"Over Current", "Over Speed", "Position Tracking", "Over Load", "Over Temperature", "Over Back EMF", "No Motor Connect", "No Encoder Connect", "Low Motor Power", "Inposition Error",
		"System Halt", "ROM Device Error", "", "High Input Voltage", "Position Overflow", "Position Changed"
	};

	public const ushort MAX_ALARM_LOG = 30;

	public const uint CNTSTATUS_CH1_ENABLED = 1u;

	public const uint CNTSTATUS_CH1_LTCA_EN = 2u;

	public const uint CNTSTATUS_CH1_LTCB_EN = 4u;

	public const uint CNTSTATUS_CH1_ZLTC_EN = 8u;

	public const uint CNTSTATUS_CH1_LTCA_LATCHED = 16u;

	public const uint CNTSTATUS_CH1_LTCB_LATCHED = 32u;

	public const uint CNTSTATUS_CH1_ZLTC_LATCHED = 64u;

	public const uint CNTSTATUS_CH1_RESET = 128u;

	public const uint CNTSTATUS_CH1_TRIGGER = 256u;

	public const uint CNTSTATUS_CH1_COMPARISON = 512u;

	public const uint CNTSTATUS_CH2_ENABLED = 65536u;

	public const uint CNTSTATUS_CH2_LTCA_EN = 131072u;

	public const uint CNTSTATUS_CH2_LTCB_EN = 262144u;

	public const uint CNTSTATUS_CH2_ZLTC_EN = 524288u;

	public const uint CNTSTATUS_CH2_LTCA_LATCHED = 1048576u;

	public const uint CNTSTATUS_CH2_LTCB_LATCHED = 2097152u;

	public const uint CNTSTATUS_CH2_ZLTC_LATCHED = 4194304u;

	public const uint CNTSTATUS_CH2_RESET = 8388608u;

	public const uint CNTSTATUS_CH2_TRIGGER = 16777216u;

	public const uint CNTSTATUS_CH2_COMPARISON = 33554432u;

	[DllImport("EziMOTIONPlusE.dll")]
	protected static extern int FAS_Connect(byte sb1, byte sb2, byte sb3, byte sb4, int iBdID);

	public static bool FAS_Connect(IPAddress ipaddr, int iBdID)
	{
		int num = 0;
		byte[] addressBytes = ipaddr.GetAddressBytes();
		num = FAS_Connect(addressBytes[0], addressBytes[1], addressBytes[2], addressBytes[3], iBdID);
		return num != 0;
	}

	[DllImport("EziMOTIONPlusE.dll")]
	protected static extern int FAS_ConnectTCP(byte sb1, byte sb2, byte sb3, byte sb4, int iBdID);

	public static bool FAS_ConnectTCP(IPAddress ipaddr, int iBdID)
	{
		int num = 0;
		byte[] addressBytes = ipaddr.GetAddressBytes();
		num = FAS_ConnectTCP(addressBytes[0], addressBytes[1], addressBytes[2], addressBytes[3], iBdID);
		return num != 0;
	}

	[DllImport("EziMOTIONPlusE.dll")]
	protected static extern int FAS_IsBdIDExist(int iBdID, ref byte sb1, ref byte sb2, ref byte sb3, ref byte sb4);

	public static bool FAS_IsBdIDExist(int iBdID, ref IPAddress ipaddr)
	{
		int num = 0;
		byte sb = 0;
		byte sb2 = 0;
		byte sb3 = 0;
		byte sb4 = 0;
		num = FAS_IsBdIDExist(iBdID, ref sb, ref sb2, ref sb3, ref sb4);
		ipaddr = new IPAddress(new byte[4] { sb, sb2, sb3, sb4 });
		return num != 0;
	}

	[DllImport("EziMOTIONPlusE.dll")]
	protected static extern int FAS_IsIPAddressExist(byte sb1, byte sb2, byte sb3, byte sb4, ref int iBdID);

	public static bool FAS_IsIPAddressExist(IPAddress ipaddr, ref int iBdID)
	{
		int num = 0;
		byte[] addressBytes = ipaddr.GetAddressBytes();
		num = FAS_IsIPAddressExist(addressBytes[0], addressBytes[1], addressBytes[2], addressBytes[3], ref iBdID);
		return num != 0;
	}

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_Reconnect(int iBdID);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SetAutoReconnect(int bSET);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern void FAS_Close(int iBdID);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_IsSlaveExist(int iBdID);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern void FAS_EnableLog(int bEnable);

	[DllImport("EziMOTIONPlusE.dll")]
	protected static extern void FAS_SetLogLevel(int level);

	public static void FAS_SetLogLevel(LOG_LEVEL level)
	{
		switch (level)
		{
		case LOG_LEVEL.LOG_LEVEL_COMM:
			FAS_SetLogLevel(0);
			break;
		case LOG_LEVEL.LOG_LEVEL_PARAM:
			FAS_SetLogLevel(1);
			break;
		case LOG_LEVEL.LOG_LEVEL_MOTION:
			FAS_SetLogLevel(2);
			break;
		default:
			FAS_SetLogLevel(3);
			break;
		}
	}

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SetLogPath([MarshalAs(UnmanagedType.LPWStr)] string lpPath);

	[DllImport("EziMOTIONPlusE.dll")]
	protected static extern int FAS_PrintCustomLog(int iBdID, int level, [MarshalAs(UnmanagedType.LPWStr)] string lpszMsg);

	public static int FAS_PrintCustomLog(int iBdID, LOG_LEVEL level, string lpszMsg)
	{
		int num = 0;
		return level switch
		{
			LOG_LEVEL.LOG_LEVEL_COMM => FAS_PrintCustomLog(iBdID, 0, lpszMsg), 
			LOG_LEVEL.LOG_LEVEL_PARAM => FAS_PrintCustomLog(iBdID, 1, lpszMsg), 
			LOG_LEVEL.LOG_LEVEL_MOTION => FAS_PrintCustomLog(iBdID, 2, lpszMsg), 
			_ => FAS_PrintCustomLog(iBdID, 3, lpszMsg), 
		};
	}

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetEthernetAddr(int iBdID, ref uint gateway, ref uint subnet, ref uint ip);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SetEthernetAddr(int iBdID, uint gateway, uint subnet, uint ip);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetMACAddress(int iBdID, ref ulong MACAddress);

	[DllImport("EziMOTIONPlusE.dll")]
	protected static extern int FAS_GetSlaveInfo(int iBdID, ref byte pType, StringBuilder lpBuff, int nBuffSize);

	public static int FAS_GetSlaveInfo(int iBdID, ref byte pType, ref string version)
	{
		int num = 0;
		byte pType2 = 0;
		StringBuilder stringBuilder = new StringBuilder(256);
		num = FAS_GetSlaveInfo(iBdID, ref pType2, stringBuilder, 256);
		if (num == 0)
		{
			pType = pType2;
			version = stringBuilder.ToString();
		}
		return num;
	}

	[DllImport("EziMOTIONPlusE.dll")]
	protected static extern int FAS_GetMotorInfo(int iBdID, ref byte pType, StringBuilder lpBuff, int nBuffSize);

	public static int FAS_GetMotorInfo(int iBdID, ref byte pType, ref string motor)
	{
		int num = 0;
		byte pType2 = 0;
		StringBuilder stringBuilder = new StringBuilder(256);
		num = FAS_GetMotorInfo(iBdID, ref pType2, stringBuilder, 256);
		if (num == 0)
		{
			pType = pType2;
			motor = stringBuilder.ToString();
		}
		return num;
	}

	[DllImport("EziMOTIONPlusE.dll")]
	protected static extern int FAS_GetSlaveInfoEx(int iBdID, byte[] lpDriveInfo);

	public static int FAS_GetSlaveInfoEx(int iBdID, ref DRIVE_INFO DriveInfo)
	{
		int num = 0;
		byte[] array = new byte[70];
		num = FAS_GetSlaveInfoEx(iBdID, array);
		if (num == 0)
		{
			DriveInfo.copy(array);
		}
		return num;
	}

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SaveAllParameters(int iBdID);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SetParameter(int iBdID, byte iParamNo, int lParamValue);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetParameter(int iBdID, byte iParamNo, ref int lParamValue);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetROMParameter(int iBdID, byte iParamNo, ref int lRomParam);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SetIOInput(int iBdID, uint dwIOSETMask, uint dwIOCLRMask);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetIOInput(int iBdID, ref uint dwIOInput);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SetIOOutput(int iBdID, uint dwIOSETMask, uint dwIOCLRMask);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetIOOutput(int iBdID, ref uint dwIOOutput);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetIOAssignMap(int iBdID, byte iIOPinNo, ref uint dwIOLogicMask, ref byte bLevel);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SetIOAssignMap(int iBdID, byte iIOPinNo, uint dwIOLogicMask, byte bLevel);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_IOAssignMapReadROM(int iBdID);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_ServoEnable(int iBdID, int bOnOff);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_ServoAlarmReset(int iBdID);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_StepAlarmReset(int iBdID, int bReset);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_BrakeSet(int iBdID, int bSet, ref int nResult);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetAxisStatus(int iBdID, ref uint dwAxisStatus);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetIOAxisStatus(int iBdID, ref uint dwInStatus, ref uint dwOutStatus, ref uint dwAxisStatus);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetMotionStatus(int iBdID, ref int lCmdPos, ref int lActPos, ref int lPosErr, ref int lActVel, ref ushort wPosItemNo);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetAllStatus(int iBdID, ref uint dwInStatus, ref uint dwOutStatus, ref uint dwAxisStatus, ref int lCmdPos, ref int lActPos, ref int lPosErr, ref int lActVel, ref ushort wPosItemNo);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetAllStatusEx(int iBdID, byte[] pTypes, int[] pDatas);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SetCommandPos(int iBdID, int lCmdPos);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SetActualPos(int iBdID, int lActPos);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_ClearPosition(int iBdID);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetCommandPos(int iBdID, ref int lCmdPos);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetActualPos(int iBdID, ref int lActPos);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetPosError(int iBdID, ref int lPosErr);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetActualVel(int iBdID, ref int lActVel);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetAlarmType(int iBdID, ref byte nAlarmType);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetWarningType(int iBdID, ref byte nWarningType);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_MoveStop(int iBdID);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_EmergencyStop(int iBdID);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_MovePause(int iBdID, int bPause);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_MoveOriginSingleAxis(int iBdID);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_MoveSingleAxisAbsPos(int iBdID, int lAbsPos, uint lVelocity);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_MoveSingleAxisIncPos(int iBdID, int lIncPos, uint lVelocity);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_MoveToLimit(int iBdID, uint lVelocity, int iLimitDir);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_MoveVelocity(int iBdID, uint lVelocity, int iVelDir);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_PositionAbsOverride(int iBdID, int lOverridePos);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_PositionIncOverride(int iBdID, int lOverridePos);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_VelocityOverride(int iBdID, uint lVelocity);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_MoveLinearAbsPos(byte nNoOfBds, int[] iBdID, int[] lplAbsPos, uint lFeedrate, ushort wAccelTime);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_MoveLinearIncPos(byte nNoOfBds, int[] iBdID, int[] lplIncPos, uint lFeedrate, ushort wAccelTime);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_MoveLinearAbsPos2(byte nNoOfBds, int[] iBdID, int[] lplAbsPos, uint lFeedrate, ushort wAccelTime);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_MoveLinearIncPos2(byte nNoOfBds, int[] iBdID, int[] lplIncPos, uint lFeedrate, ushort wAccelTime);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_MoveCircleAbsPos1(byte nNoOfBds, int[] iBdID, int[] lplCirEndAbs, int[] lplCirCenterAbs, int iDirection, uint lFeedrate, ushort wAccelTime, int bSCurve);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_MoveCircleIncPos1(byte nNoOfBds, int[] iBdID, int[] lplCirEndInc, int[] lplCirCenterInc, int iDirection, uint lFeedrate, ushort wAccelTime, int bSCurve);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_MoveCircleAbsPos2(byte nNoOfBds, int[] iBdID, int[] lplCirEndAbs, uint lRadius, int iDirection, uint lFeedrate, ushort wAccelTime, int bSCurve);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_MoveCircleIncPos2(byte nNoOfBds, int[] iBdID, int[] lplCirEndInc, uint lRadius, int iDirection, uint lFeedrate, ushort wAccelTime, int bSCurve);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_MoveCircleAbsPos3(byte nNoOfBds, int[] iBdID, int[] lplCirCenterAbs, uint nAngle, int iDirecion, uint lFeedrate, ushort wAccelTime, int bSCurve);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_MoveCircleIncPos3(byte nNoOfBds, int[] iBdID, int[] lplCirCenterInc, uint nAngle, int iDirecion, uint lFeedrate, ushort wAccelTime, int bSCurve);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_TriggerOutput_RunA(int iBdID, int bStartTrigger, int lStartPos, uint dwPeriod, uint dwPulseTime);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_TriggerOutput_Status(int iBdID, ref byte bTriggerStatus);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SetTriggerOutputEx(int iBdID, byte nOutputNo, byte bRun, ushort wOnTime, byte nTriggerCount, int[] arrTriggerPosition);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetTriggerOutputEx(int iBdID, byte nOutputNo, ref byte bRun, ref ushort wOnTime, ref byte nTriggerCount, int[] arrTriggerPosition);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_MovePush(int iBdID, uint dwStartSpd, uint dwMoveSpd, int lPosition, ushort wAccel, ushort wDecel, ushort wPushRate, uint dwPushSpd, int lEndPosition, ushort wPushMode);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetPushStatus(int iBdID, ref byte nPushStatus);

	[DllImport("EziMOTIONPlusE.dll")]
	protected static extern int FAS_MoveSingleAxisAbsPosEx(int iBdID, int lAbsPos, uint lVelocity, byte[] lpExOption);

	public static int FAS_MoveSingleAxisAbsPosEx(int iBdID, int lAbsPos, uint lVelocity, MOTION_OPTION_EX ExOption)
	{
		int num = 0;
		byte[] array = new byte[32];
		ExOption.copyto(array);
		return FAS_MoveSingleAxisAbsPosEx(iBdID, lAbsPos, lVelocity, array);
	}

	[DllImport("EziMOTIONPlusE.dll")]
	protected static extern int FAS_MoveSingleAxisIncPosEx(int iBdID, int lIncPos, uint lVelocity, byte[] lpExOption);

	public static int FAS_MoveSingleAxisIncPosEx(int iBdID, int lAbsPos, uint lVelocity, MOTION_OPTION_EX ExOption)
	{
		int num = 0;
		byte[] array = new byte[32];
		ExOption.copyto(array);
		return FAS_MoveSingleAxisIncPosEx(iBdID, lAbsPos, lVelocity, array);
	}

	[DllImport("EziMOTIONPlusE.dll")]
	protected static extern int FAS_MoveVelocityEx(int iBdID, uint lVelocity, int iVelDir, byte[] lpExOption);

	public static int FAS_MoveVelocityEx(int iBdID, uint lVelocity, int iVelDir, VELOCITY_OPTION_EX ExOption)
	{
		int num = 0;
		byte[] array = new byte[32];
		ExOption.copyto(array);
		return FAS_MoveVelocityEx(iBdID, lVelocity, iVelDir, array);
	}

	[DllImport("EziMOTIONPlusE.dll")]
	protected static extern int FAS_PosTableReadItem(int iBdID, ushort wItemNo, byte[] lpItem);

	public static int FAS_PosTableReadItem(int iBdID, ushort wItemNo, ref ITEM_NODE Item)
	{
		int num = 0;
		byte[] array = new byte[64];
		num = FAS_PosTableReadItem(iBdID, wItemNo, array);
		if (num == 0)
		{
			Item.copyfrom(array);
		}
		return num;
	}

	[DllImport("EziMOTIONPlusE.dll")]
	protected static extern int FAS_PosTableWriteItem(int iBdID, ushort wItemNo, byte[] lpItem);

	public static int FAS_PosTableWriteItem(int iBdID, ushort wItemNo, ITEM_NODE Item)
	{
		int num = 0;
		byte[] array = new byte[64];
		Item.copyto(array);
		return FAS_PosTableWriteItem(iBdID, wItemNo, array);
	}

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_PosTableWriteROM(int iBdID);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_PosTableReadROM(int iBdID);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_PosTableRunItem(int iBdID, ushort wItemNo);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_PosTableReadOneItem(int iBdID, ushort wItemNo, ushort wOffset, ref int lPosItemVal);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_PosTableWriteOneItem(int iBdID, ushort wItemNo, ushort wOffset, int lPosItemVal);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_PosTableSingleRunItem(int iBdID, int bNextMove, ushort wItemNo);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GapControlEnable(int iBdID, ushort wItemNo, int lGapCompSpeed, int lGapAccTime, int lGapDecTime, int lGapStartSpeed);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GapControlDisable(int iBdID);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_IsGapControlEnable(int iBdID, ref int bIsEnable, ref ushort wCurrentItemNo);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GapControlGetADCValue(int iBdID, ref int lADCValue);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GapOneResultMonitor(int iBdID, ref byte bUpdated, ref int iIndex, ref int lGapValue, ref int lCmdPos, ref int lActPos, ref int lCompValue, ref int lReserved);

	[DllImport("EziMOTIONPlusE.dll")]
	protected static extern int FAS_GetAlarmLogs(int iBdID, byte[] pAlarmLog);

	public static int FAS_GetAlarmLogs(int iBdID, ref ALARM_LOG AlarmLog)
	{
		int num = 0;
		byte[] array = new byte[31];
		num = FAS_GetAlarmLogs(iBdID, array);
		if (num == 0)
		{
			AlarmLog.copy(array);
		}
		return num;
	}

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_ResetAlarmLogs(int iBdID);

	[DllImport("EziMOTIONPlusE.dll")]
	protected static extern int FAS_GetWarningLogs(int iBdID, byte[] pWarningLog);

	public static int FAS_GetWarningLogs(int iBdID, ref ALARM_LOG WarningLog)
	{
		int num = 0;
		byte[] array = new byte[31];
		num = FAS_GetWarningLogs(iBdID, array);
		if (num == 0)
		{
			WarningLog.copy(array);
		}
		return num;
	}

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_ResetWarningLogs(int iBdID);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetInput(int iBdID, ref uint uInput, ref uint uLatch);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_ClearLatch(int iBdID, uint uLatchMask);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetLatchCount(int iBdID, uint iInputNo, ref uint uCount);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetLatchCountAll(int iBdID, uint[] puAllCount);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetLatchCountAll32(int iBdID, uint[] puAllCount);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_ClearLatchCount(int iBdID, uint uInputMask);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetOutput(int iBdID, ref uint uOutput, ref uint uStatus);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SetOutput(int iBdID, uint uSetMask, uint uClearMask);

	[DllImport("EziMOTIONPlusE.dll")]
	protected static extern int FAS_SetTrigger(int iBdID, byte uOutputNo, byte[] pTrigger);

	public static int FAS_SetTrigger(int iBdID, byte uOutputNo, TRIGGER_INFO trigger)
	{
		int num = 0;
		num = FAS_SetTrigger(iBdID, uOutputNo, trigger.ByteArray);
		if (num == 0)
		{
		}
		return num;
	}

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SetRunStop(int iBdID, uint uRunMask, uint uStopMask);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetTriggerCount(int iBdID, uint uOutputNo, ref uint uCount);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetIOLevel(int iBdID, ref uint uIOLevel);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SetIOLevel(int iBdID, uint uIOLevel);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_LoadIOLevel(int iBdID);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SaveIOLevel(int iBdID);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetInputFilter(int iBdID, ref ushort filter);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SetInputFilter(int iBdID, ushort filter);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetIODirection(int iBdID, ref uint direction);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SetIODirection(int iBdID, uint direction);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SetADConfig(int iBdID, byte channel, int type, int value, ref int recv);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetADConfig(int iBdID, byte channel, int type, ref int value);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_LoadADConfig(int iBdID);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SaveADConfig(int iBdID);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_ReadADValue(int iBdID, byte channel, ref short advalue);

	[DllImport("EziMOTIONPlusE.dll")]
	protected static extern int FAS_ReadADAllValue(int iBdID, byte offset, byte[] adbuffer);

	public static int FAS_ReadADAllValue(int iBdID, byte offset, out AD_BUFFER adbuffer)
	{
		byte[] array = new byte[16];
		int num = 0;
		num = FAS_ReadADAllValue(iBdID, offset, array);
		adbuffer = new AD_BUFFER(array);
		return num;
	}

	[DllImport("EziMOTIONPlusE.dll")]
	protected static extern int FAS_GetAllADResult(int iBdID, byte[] result);

	public static int FAS_GetAllADResult(int iBdID, out AD_RESULT result)
	{
		byte[] array = new byte[112];
		int num = 0;
		num = FAS_GetAllADResult(iBdID, array);
		result = new AD_RESULT(array);
		return num;
	}

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetADResult(int iBdID, byte channel, ref float adresult);

	[DllImport("EziMOTIONPlusE.dll")]
	protected static extern int FAS_SetADRange(int iBdID, byte channel, int range);

	public static int FAS_SetADRange(int iBdID, byte channel, AD_RANGE range)
	{
		return FAS_SetADRange(iBdID, channel, (int)range);
	}

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SetDACConfig(int iBdID, byte channel, DAC_CONFIG type, int data, ref int recv);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetDACConfig(int iBdID, byte channel, DAC_CONFIG type, ref int data);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_LoadDACConfig(int iBdID);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SaveDACConfig(int iBdID);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SetDACValue(int iBdID, byte channel, int bEnable, int value);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetDACValue(int iBdID, byte channel, ref int bEnable, ref int value);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_CounterCommand(int iBdID, byte channel, COUNTER_CMD cmd, int data);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetCounterValue(int iBdID, byte channel, COUNTER_VALUE type, ref int value);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetCounterStatus(int iBdID, ref uint dwStatus);

	[DllImport("EziMOTIONPlusE.dll")]
	protected static extern int FAS_SetCounterTrigger(int iBdID, byte channel, int bStartTrigger, int lStartPos, uint dwPeriod, uint dwPulseTime, uint dwTriggerCnt);

	public static int FAS_SetCounterTrigger(int iBdID, byte channel, bool bStartTrigger, int lStartPos, uint dwPeriod, uint dwPulseTime, uint dwTriggerCnt)
	{
		return FAS_SetCounterTrigger(iBdID, channel, bStartTrigger ? 1 : 0, lStartPos, dwPeriod, dwPulseTime, dwTriggerCnt);
	}

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SetCounterConfig(int iBdID, byte channel, COUNTER_CONFIG type, int data, ref int recv);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_GetCounterConfig(int iBdID, byte channel, COUNTER_CONFIG type, ref int data);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_LoadCounterConfig(int iBdID);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SaveCounterConfig(int iBdID);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SPEEDGetInputStatus(int iBdID, ref uint dwDirectInput);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SPEEDGetOutputStatus(int iBdID, ref uint dwDirectOutput);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SPEEDGetNETStatus(int iBdID, ref uint dwNETStatus);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SPEEDSetOutput(int iBdID, uint dwIOSETMask, uint dwIOCLRMask);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SPEEDGetCommandSpeed(int iBdID, ref int nSpeed);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SPEEDGetFeedbackSpeed(int iBdID, ref int nSpeed);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SPEEDGetGearSpeed(int iBdID, ref float fSpeed);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SPEEDGetConveyorSpeed(int iBdID, ref float fSpeed);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SPEEDGetOperationNo(int iBdID, ref byte nOpNo);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SPEEDMoveVelocity(int iBdID, ushort uSpeed, int bForward, ushort uAccTime, ushort uDecTime, ushort uTorqueLimit);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SPEEDSetOperationData(int iBdID, byte nOpNo, ushort uSpeed, ushort uAccTime, ushort uDecTime, ushort uTorqueLimit);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SPEEDGetOperationData(int iBdID, byte nOpNo, ref ushort uSpeed, ref ushort uAccTime, ref ushort uDecTime, ref ushort uTorqueLimit);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SPEEDRunOperationData(int iBdID, byte nOpNo, int bForward);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SPEEDLoadOperationData(int iBdID);

	[DllImport("EziMOTIONPlusE.dll")]
	public static extern int FAS_SPEEDSaveOperationData(int iBdID);
}
