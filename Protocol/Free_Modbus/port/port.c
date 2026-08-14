#include "port.h"
#include "mb.h"
#include "mbport.h"
#include "string.h"
#include "Function.h"

/* Free_Modbus 的寄存器移植层：4 个寄存器缓冲区 + 4 个读写回调。
 *
 * ★ 比赛现场改点表不用动这个文件。
 *   地址、容量、整表平移全部在 Function/modbus_data_map.h 里改（那里全是 #define）。
 *   这里只负责一件事：把报文里的【线上地址】换算成【缓冲区下标】，然后搬字节。
 *
 * 地址换算链路（三段，搞清楚这个就不会被"差 1"坑到）：
 *     ① 主站报文里的地址          = 线上地址（Base 0，从 0 开始）
 *     ② mbfunc*.c 收到后统一 +1   = 本文件回调收到的 usAddress（vendor 的约定）
 *     ③ 本文件再 -1 -基址          = 缓冲区下标，也就是 modbus_data_map.h 里写的编号
 *   所以：缓冲区下标 = 线上地址 - 基址，基址默认 0 时两者相等。
 *
 * 缓冲区里的数据由 Function/modbus_data_map.c 负责和设备真实数据来回搬运，
 * 业务逻辑不下沉到 Protocol 层（符合赛题 2.1 对分层的要求）。
 */

/* 输入寄存器缓冲区（FC04 只读，放测量值） */
uint16_t REG_INPUT_BUF[REG_INPUT_SIZE] = { 0 };

/* 保持寄存器缓冲区（FC03/06/16 读写，放参数） */
uint16_t REG_HOLD_BUF[REG_HOLD_SIZE] = { 0 };

/* 线圈缓冲区（FC01/05/15 读写，放开关量），一个字节存一个线圈，0 或 1 */
uint8_t REG_COILS_BUF[REG_COILS_SIZE] = { 0 };

/* 离散输入缓冲区（FC02 只读，放状态位），一个字节存一个状态，0 或 1 */
uint8_t REG_DISC_BUF[REG_DISC_SIZE] = { 0 };


/**
 * @brief 线上地址 -> 缓冲区下标，顺便做范围检查
 *
 * @param usAddress  回调收到的地址（vendor 已经在线上地址基础上 +1）
 * @param base       该类寄存器的基址，改自 modbus_data_map.h 的【3】
 * @param count      本次要访问几个（用来判断尾部会不会越界）
 * @param size       该类寄存器缓冲区的容量
 * @return 合法则返回缓冲区下标（>=0）；越界返回 -1，调用方回 MB_ENOREG，
 *         主站会收到异常码 0x02（非法数据地址）。
 *
 * 用 int32_t 而不是 USHORT 做中间计算是必须的：主站要是发了一个比基址还小的
 * 地址，USHORT 相减会回绕成 65535 这种大数，反而通过了"小于容量"的检查，
 * 结果读到缓冲区外面去。用带符号数才能真的挡住。
 */
static int32_t mb_reg_index(USHORT usAddress, int32_t base, int32_t count, int32_t size)
{
    int32_t idx = (int32_t)usAddress - 1 - base;   /* -1 抵消 vendor 那一层的 +1 */

    if (idx < 0) {
        return -1;                                 /* 地址比基址还小 */
    }
    if (idx + count > size) {
        return -1;                                 /* 尾部超出缓冲区 */
    }
    return idx;
}


/**
 * @brief 直接往某个缓冲区里塞值（按缓冲区下标，不经过地址基址换算）
 *
 * 当前工程没有用到它 —— 设备数据的搬运统一走 modbus_data_map.c。
 * 保留是为了万一现场想在别处临时写几个寄存器时有个现成的入口。
 *
 * @param reg_type 寄存器类型
 * @param address  缓冲区下标（就是 modbus_data_map.h 里写的编号）
 * @param buf      数据源
 * @param len      个数
 */
void Set_Register(REG_TYPE reg_type , uint16_t address , uint16_t* buf , uint16_t len)
{
	switch (reg_type)
	{
	case REG_INPUT:
		for (uint16_t i = 0; i < len && (address + i) < REG_INPUT_SIZE; i++)
		{
			REG_INPUT_BUF[address + i] = buf[i];
		}
		break;
	case REG_HOLD:
		for (uint16_t i = 0; i < len && (address + i) < REG_HOLD_SIZE; i++)
		{
			REG_HOLD_BUF[address + i] = buf[i];
		}
		break;
	case REG_COILS:
		for (uint16_t i = 0; i < len && (address + i) < REG_COILS_SIZE; i++)
		{
			REG_COILS_BUF[address + i] = buf[i] & 0x01 ? 1 : 0;
		}
		break;
	case REG_DISC:
		for (uint16_t i = 0; i < len && (address + i) < REG_DISC_SIZE; i++)
		{
			REG_DISC_BUF[address + i] = buf[i] & 0x01 ? 1 : 0;
		}
		break;
	default:
		break;
	}
}


/**
 * @brief 线圈读写回调（对应 FC01 读 / FC05 单写 / FC15 多写）
 *
 * 线圈在报文里是按【位】打包的：一个字节装 8 个线圈，最低位是地址最小的那个。
 * 缓冲区里则是一个字节存一个线圈（0 或 1），这个函数就是在两种表示之间转换。
 *
 * @param pucRegBuffer 报文里的位数据缓冲区
 * @param usAddress    起始地址（vendor 已 +1，见本文件开头的换算链路）
 * @param usNCoils     线圈个数
 * @param eMode        MB_REG_READ 读 / MB_REG_WRITE 写
 * @return MB_ENOERR 成功；MB_ENOREG 地址越界（主站收到异常码 0x02）
 */
eMBErrorCode eMBRegCoilsCB(UCHAR* pucRegBuffer , USHORT usAddress , USHORT usNCoils , eMBRegisterMode eMode)
{
	int32_t idx = mb_reg_index(usAddress , MB_COIL_ADDR_BASE , usNCoils , REG_COILS_SIZE);
	USHORT  usRegIndex;
	USHORT  usLeft = usNCoils;      /* 还剩几个线圈没搬 */
	UCHAR   ucBits = 0;             /* 当前字节里的位序号 0~7 */
	UCHAR   ucState = 0;            /* 当前字节的内容 */

	if (idx < 0)
	{
		return MB_ENOREG;
	}
	usRegIndex = (USHORT)idx;

	if (eMode == MB_REG_WRITE)
	{
		/* 写：把报文里的位一个个拆出来，写进缓冲区 */
		while (usLeft != 0)
		{
			ucState = *pucRegBuffer++;
			ucBits = 0;
			while (usLeft != 0 && ucBits < 8)
			{
				REG_COILS_BUF[usRegIndex++] = (ucState >> ucBits) & 0x01;
				usLeft--;
				ucBits++;
			}
		}
	}
	else
	{
		/* 读：把缓冲区里的 0/1 一个个塞进报文的位里。
		   最后一个字节里多出来的位按规范补 0 —— 循环条件带着 usLeft，
		   不能为了图省事写成固定 8 位，否则最后一个字节会去读 usNCoils 之外的
		   线圈，缓冲区末尾附近的请求就会读到数组外面去。 */
		while (usLeft != 0)
		{
			ucState = 0;
			ucBits = 0;
			while (usLeft != 0 && ucBits < 8)
			{
				if (REG_COILS_BUF[usRegIndex])
				{
					ucState |= (UCHAR)(1 << ucBits);
				}
				usRegIndex++;
				usLeft--;
				ucBits++;
			}
			*pucRegBuffer++ = ucState;
		}
	}

	return MB_ENOERR;
}


/**
 * @brief 离散输入读回调（对应 FC02，只读）
 *
 * 打包方式和线圈完全一样，只是没有写这条路（规范规定离散输入只读）。
 *
 * @param pucRegBuffer 报文里的位数据缓冲区
 * @param usAddress    起始地址（vendor 已 +1）
 * @param usNDiscrete  状态位个数
 * @return MB_ENOERR 成功；MB_ENOREG 地址越界
 */
eMBErrorCode eMBRegDiscreteCB(UCHAR* pucRegBuffer , USHORT usAddress , USHORT usNDiscrete)
{
	int32_t idx = mb_reg_index(usAddress , MB_DISC_ADDR_BASE , usNDiscrete , REG_DISC_SIZE);
	USHORT  usRegIndex;
	USHORT  usLeft = usNDiscrete;
	UCHAR   ucBits = 0;
	UCHAR   ucState = 0;

	if (idx < 0)
	{
		return MB_ENOREG;
	}
	usRegIndex = (USHORT)idx;

	while (usLeft != 0)
	{
		ucState = 0;
		ucBits = 0;
		while (usLeft != 0 && ucBits < 8)
		{
			if (REG_DISC_BUF[usRegIndex])
			{
				ucState |= (UCHAR)(1 << ucBits);
			}
			usRegIndex++;
			usLeft--;
			ucBits++;
		}
		*pucRegBuffer++ = ucState;
	}

	/* vendor 的 demo 在这里每读一次就把整张表取反（模拟状态在变化），
	   真实设备当然不能这么干，主站连着读两次会得到相反的结论，已移除。 */

	return MB_ENOERR;
}


/**
 * @brief 保持寄存器读写回调（对应 FC03 读 / FC06 单写 / FC16 多写 / FC23 读写）
 *
 * 寄存器在报文里是【大端】的：高字节在前，低字节在后。这是 Modbus 规范定死的，
 * 和 float 占两个寄存器时"哪个寄存器在前"是两回事（后者见 modbus_data_map.h【4】）。
 *
 * @param pucRegBuffer 报文里的寄存器数据缓冲区
 * @param usAddress    起始地址（vendor 已 +1）
 * @param usNRegs      寄存器个数
 * @param eMode        MB_REG_READ 读 / MB_REG_WRITE 写
 * @return MB_ENOERR 成功；MB_ENOREG 地址越界
 */
eMBErrorCode eMBRegHoldingCB(UCHAR* pucRegBuffer , USHORT usAddress , USHORT usNRegs , eMBRegisterMode eMode)
{
	int32_t idx = mb_reg_index(usAddress , MB_HREG_ADDR_BASE , usNRegs , REG_HOLD_SIZE);
	USHORT  usRegIndex;

	if (idx < 0)
	{
		return MB_ENOREG;
	}
	usRegIndex = (USHORT)idx;

	if (eMode == MB_REG_WRITE)
	{
		/* 写：报文 -> 缓冲区。写完之后由 Protocol_Router.c 调
		   Modbus_ApplyHoldingRegs() 把新值落到设备上 */
		while (usNRegs > 0)
		{
			REG_HOLD_BUF[usRegIndex] = (uint16_t)((pucRegBuffer[0] << 8) | pucRegBuffer[1]);
			pucRegBuffer += 2;
			usRegIndex++;
			usNRegs--;
		}
	}
	else
	{
		/* 读：缓冲区 -> 报文。缓冲区的内容在分发前已经由
		   Modbus_SyncRegsFromDevice() 刷成设备当前值 */
		while (usNRegs > 0)
		{
			*pucRegBuffer++ = (unsigned char)(REG_HOLD_BUF[usRegIndex] >> 8);
			*pucRegBuffer++ = (unsigned char)(REG_HOLD_BUF[usRegIndex] & 0xFF);
			usRegIndex++;
			usNRegs--;
		}
	}

	return MB_ENOERR;
}


/**
 * @brief 输入寄存器读回调（对应 FC04，只读）
 *
 * @param pucRegBuffer 报文里的寄存器数据缓冲区
 * @param usAddress    起始地址（vendor 已 +1）
 * @param usNRegs      寄存器个数
 * @return MB_ENOERR 成功；MB_ENOREG 地址越界
 */
eMBErrorCode eMBRegInputCB(UCHAR* pucRegBuffer , USHORT usAddress , USHORT usNRegs)
{
	int32_t idx = mb_reg_index(usAddress , MB_IREG_ADDR_BASE , usNRegs , REG_INPUT_SIZE);
	USHORT  usRegIndex;

	if (idx < 0)
	{
		return MB_ENOREG;
	}
	usRegIndex = (USHORT)idx;

	while (usNRegs > 0)
	{
		*pucRegBuffer++ = (unsigned char)(REG_INPUT_BUF[usRegIndex] >> 8);   /* 高字节在前 */
		*pucRegBuffer++ = (unsigned char)(REG_INPUT_BUF[usRegIndex] & 0xFF);
		usRegIndex++;
		usNRegs--;
	}

	return MB_ENOERR;
}


/**
 * @brief 把 Free_Modbus 的内部错误码映射成 Modbus 规范的异常码
 *
 * 当前工程没有用到（Protocol_Router.c 直接拿功能码处理函数返回的 eMBException），
 * 保留备用。异常码的含义见 modbus_data_map.h 的【0】⑤。
 */
uint8_t mapErrorToException(eMBErrorCode error)
{
	switch (error)
	{
	case MB_ENOREG:     // 非法寄存器地址
		return 0x02;    // 非法数据地址
	case MB_EINVAL:     // 非法参数
		return 0x03;    // 非法数据值
	case MB_ENORES:     // 资源不足
		return 0x04;    // 从站设备故障
	case MB_ETIMEDOUT:  // 超时
		return 0x06;    // 从站设备忙
	case MB_EPORTERR:   // 端口错误
		return 0x04;    // 从站设备故障
	case MB_ENOERR:     // 无错误
		return 0x00;    // 无异常
	default:            // 未知错误
		return 0x04;    // 从站设备故障
	}
}
