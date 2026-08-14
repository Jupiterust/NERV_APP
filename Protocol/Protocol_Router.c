#include "Protocol_Router.h"

/* Free_Modbus：只借用寄存器回调 + 功能码处理函数（mbfunc*.c / port.c）和 CRC16（mbcrc.c）。
 * mb.c 的事件队列、portserial.c / porttimer.c 的字节级中断 + T3.5 帧同步都不用：那一路要
 * 独占 USART1 的接收中断，和 bsp_rs485.c 冲突（USART1_IRQHandler 会重复定义），而且它依赖
 * 的 my_usart1_init / my_usart_485_CS / my_timer_init 在本仓库里没有实现。
 * 这里直接复用 bsp_rs485.c 做好的整帧捕获（rx_real_buffer / rx_real_len，即 RTU/ASCII 都
 * 需要的"帧间静默即为一帧"），一次性喂给功能码处理函数。RTU 和 ASCII 共用同一套功能码
 * 分发（Modbus_DispatchPDU），只是帧头/校验/编码不同。
 */
#include "port.h"
#include "mb.h"
#include "mbframe.h"
#include "mbproto.h"
#include "mbconfig.h"
#include "mbfunc.h"
#include "mbcrc.h"

/* Modbus 从站地址。1~247 有效，0 广播，248~255 保留，和自定义协议的设备 ID 是两套身份。
 * 开机由 main.c 从 flash 载入，运行时可改（保持寄存器 MB_HREG_MB_SLAVE_ADDR 或直接改这个
 * 变量），改完自动持久化。要在编译期钉死地址，改 modbus_data_map.h【2】的
 * MB_FORCE_SLAVE_ADDR，那样开机不再读 flash。 */
#if (MB_FORCE_SLAVE_ADDR != 0)
uint8_t Modbus_slave_addr = MB_FORCE_SLAVE_ADDR;
#else
uint8_t Modbus_slave_addr = PARAM_DEFAULT_MB_ADDR;
#endif

// 协议模式，默认值在 modbus_data_map.h【2】的 MB_DEFAULT_PROTOCOL_MODE 里改
uint8_t Protocol_mode = MB_DEFAULT_PROTOCOL_MODE;

/* 应答帧用独立缓冲拼装，不在接收缓冲区上原地做。
 * rx_real_buffer 随时可能被 TIMER6 的判帧中断改写：写保持寄存器会触发 flash 整扇区擦除，
 * 阻塞 100ms 以上，清告警更是 400ms，这期间足够新帧到达并覆盖掉正在拼的应答。
 * 顺带也把 ASCII 路径上原来那个 256 字节的栈数组挪成静态的。 */
#define MB_BIN_BUF_SIZE     256   // 二进制帧最大长度（地址 + PDU + 校验）
#define MB_ASCII_BUF_SIZE   520   // ':' + 2*255 个十六进制字符 + CRLF，留余量

static uint8_t mb_bin_buf[MB_BIN_BUF_SIZE];
static uint8_t mb_ascii_buf[MB_ASCII_BUF_SIZE];

/* 最近一帧校验通过的报文属于哪种协议，用来判断总线上有没有 Modbus 主站，
 * 进而决定能不能主动发定时上报帧（见 Protocol_ModbusMasterActive）。
 * 必须在 CRC / LRC 通过之后才置位：总线撞车产生的乱码也不以 "A5B6" 开头，
 * 校验前就置位的话一段噪声就能把自动上报永久关掉。 */
#define PROTO_SRC_NONE      0
#define PROTO_SRC_CUSTOM    1
#define PROTO_SRC_MODBUS    2
static volatile uint8_t s_last_proto = PROTO_SRC_NONE;

static void CustomProtocol_Process(uint8_t *data, uint16_t len);
static void Modbus_ProcessFrame(uint8_t *data, uint16_t len);
static void Modbus_ProcessRTUFrame(uint8_t *data, uint16_t len);
static void Modbus_ProcessASCIIFrame(uint8_t *data, uint16_t len);
static uint8_t Modbus_DispatchPDU(uint8_t *frame_buf, uint16_t *p_bin_len);
static eMBException Modbus_FuncReadExceptionStatus(UCHAR *pdu, USHORT *p_pdu_len);
static eMBException Modbus_FuncDiagnostic(UCHAR *pdu, USHORT *p_pdu_len);
static uint8_t AsciiHexCharToVal(uint8_t c);
static uint8_t Modbus_CalcLRC(const uint8_t *buf, uint16_t len);

// 按 Protocol_mode 分发：1/2/3 强制走指定协议（忽略帧内容），4 按帧内容自动嗅探。
// 自定义协议固定以 ASCII "A5B6" 开头，Modbus 的 RTU / ASCII 都凑不出这个特征，
// 4 字节足以区分；Modbus 内部再由 Modbus_ProcessFrame 分 RTU / ASCII。
void Protocol_Route(uint8_t *data, uint16_t len)
{
    switch (Protocol_mode) {
        case 1: // 强制自定义协议
            CustomProtocol_Process(data, len);
            break;
        case 2: // 强制 Modbus RTU
            Modbus_ProcessRTUFrame(data, len);
            break;
        case 3: // 强制 Modbus ASCII
            Modbus_ProcessASCIIFrame(data, len);
            break;
        case 4: // 自动分流（默认）
        default:
            if (len >= 4 && data[0] == 'A' && data[1] == '5' && data[2] == 'B' && data[3] == '6') {
                CustomProtocol_Process(data, len);
            } else {
                Modbus_ProcessFrame(data, len);
            }
            break;
    }
}

static void CustomProtocol_Process(uint8_t *data, uint16_t len)
{
    // 收到自定义协议的帧说明主站切回自定义协议了，恢复定时上报。
    // 放在解析之前，这样即使这条命令因"上报进行中只认停止命令"被忽略也算数。
    s_last_proto = PROTO_SRC_CUSTOM;

    for (uint16_t i = 0; i < len; i++) {
        Protocol_ParseChar((char)data[i]);
    }
}

/* 总线上有没有 Modbus 主站在轮询，有的话本机不能主动发帧 */
uint8_t Protocol_ModbusMasterActive(void)
{
#if (MB_SUPPRESS_AUTO_REPORT_ON_MODBUS == 0)
    return 0;                                   // 关掉了这个保护，永远允许主动发
#else
    if (Protocol_mode == 2 || Protocol_mode == 3) {
        return 1;                               // 强制 Modbus 模式，一直闭嘴
    }
    return (s_last_proto == PROTO_SRC_MODBUS) ? 1 : 0;
#endif
}

// Modbus 入口：按帧头区分 RTU（二进制）和 ASCII（':' 开头的十六进制文本）。
// RTU 的地址字节可能凑巧等于 ':'(0x3A)，所以还要看结尾是不是 CRLF，两个条件都满足
// 才按 ASCII 处理。
static void Modbus_ProcessFrame(uint8_t *data, uint16_t len)
{
    if (len >= 9 && data[0] == ':' && data[len - 2] == '\r' && data[len - 1] == '\n') {
        Modbus_ProcessASCIIFrame(data, len);
    } else {
        Modbus_ProcessRTUFrame(data, len);
    }
}

/* FC07 读取异常状态：请求 PDU 只有功能码本身，应答 = 功能码 + 1 字节状态。
 * 状态字节的含义由 Modbus_GetExceptionStatus() 定义（见 modbus_data_map.c）。 */
static eMBException Modbus_FuncReadExceptionStatus(UCHAR *pdu, USHORT *p_pdu_len)
{
    if (*p_pdu_len != 1) {
        return MB_EX_ILLEGAL_DATA_VALUE;
    }
    pdu[1]      = Modbus_GetExceptionStatus();
    *p_pdu_len  = 2;
    return MB_EX_NONE;
}

/* FC08 诊断。vendor 的 mbfuncdiag.c 只有 license 注释没有实现，这里自己写。
 * 只做两个常被测的子功能：
 *   0x0000 Return Query Data  原样回显请求数据，验证链路
 *   0x000A Clear Counters     本实现没有统计计数器，回显确认即可
 * 其余子功能返回非法功能码。请求格式：功能码(1) + 子功能(2) + 数据(2)。 */
static eMBException Modbus_FuncDiagnostic(UCHAR *pdu, USHORT *p_pdu_len)
{
    uint16_t sub;

    if (*p_pdu_len != 5) {
        return MB_EX_ILLEGAL_DATA_VALUE;
    }
    sub = (uint16_t)((uint16_t)pdu[1] << 8 | pdu[2]);

    if (sub == 0x0000 || sub == 0x000A) {
        return MB_EX_NONE;   // pdu 内容原样保留，直接当应答回去
    }
    return MB_EX_ILLEGAL_FUNCTION;
}

// 输入是一份 [地址|PDU] 的二进制帧（不含校验码），按功能码调用 Free_Modbus 的处理函数，
// 处理完把应答的 [地址|PDU] 原地写回 frame_buf 并更新 *p_bin_len。RTU/ASCII 都走这里。
// 返回 0 表示这一帧不用回（广播帧，或不是发给本机的）。
static uint8_t Modbus_DispatchPDU(uint8_t *frame_buf, uint16_t *p_bin_len)
{
    uint8_t slave_addr = frame_buf[0];
    if (slave_addr != Modbus_slave_addr && slave_addr != MB_ADDRESS_BROADCAST) return 0;

    // 分发前把设备数据刷进寄存器缓冲区，保证主站读到的是当前值而不是上一轮的旧值
    Modbus_SyncRegsFromDevice();

    UCHAR  *pdu     = &frame_buf[1];                 // PDU 从功能码开始
    USHORT  pdu_len = (USHORT)(*p_bin_len - 1);       // 去掉地址字节
    UCHAR   func_code = pdu[MB_PDU_FUNC_OFF];
    eMBException exception;

    switch (func_code) {
        case MB_FUNC_READ_COILS:            // 读线圈
            exception = eMBFuncReadCoils(pdu, &pdu_len);
            break;
        case MB_FUNC_READ_DISCRETE_INPUTS:  // 读离散输入
            exception = eMBFuncReadDiscreteInputs(pdu, &pdu_len);
            break;
        case MB_FUNC_READ_HOLDING_REGISTER: // 读保持寄存器
            exception = eMBFuncReadHoldingRegister(pdu, &pdu_len);
            break;
        case MB_FUNC_READ_INPUT_REGISTER:   // 读输入寄存器
            exception = eMBFuncReadInputRegister(pdu, &pdu_len);
            break;
        case MB_FUNC_WRITE_SINGLE_COIL:     // 写单个线圈
            exception = eMBFuncWriteCoil(pdu, &pdu_len);
            break;
        case MB_FUNC_WRITE_REGISTER:        // 写单个保持寄存器
            exception = eMBFuncWriteHoldingRegister(pdu, &pdu_len);
            break;
        case MB_FUNC_WRITE_MULTIPLE_COILS:  // 写多个线圈
            exception = eMBFuncWriteMultipleCoils(pdu, &pdu_len);
            break;
        case MB_FUNC_WRITE_MULTIPLE_REGISTERS:// 写多个保持寄存器
            exception = eMBFuncWriteMultipleHoldingRegister(pdu, &pdu_len);
            break;
        case MB_FUNC_READWRITE_MULTIPLE_REGISTERS:  // 读写多个保持寄存器
            exception = eMBFuncReadWriteMultipleHoldingRegister(pdu, &pdu_len);
            break;
        case MB_FUNC_DIAG_READ_EXCEPTION:           // 读异常状态
            exception = Modbus_FuncReadExceptionStatus(pdu, &pdu_len);
            break;
        case MB_FUNC_DIAG_DIAGNOSTIC:               // 诊断
            exception = Modbus_FuncDiagnostic(pdu, &pdu_len);
            break;
        case MB_FUNC_OTHER_REPORT_SLAVEID:          // 读从站 ID
            // 每次重设一遍，避免从站地址运行中被改过后这里还报旧值
            eMBSetSlaveID(Modbus_slave_addr, TRUE, NULL, 0);
            exception = eMBFuncReportSlaveID(pdu, &pdu_len);
            break;
        default:
            exception = MB_EX_ILLEGAL_FUNCTION;
            break;
    }

    // 写类功能码：主站刚把新值写进寄存器缓冲区，这里落到设备上（变比/阈值还要存 flash）。
    // 只跑写入类别对应的那一半：告警模式在保持寄存器和线圈里各有一份映射，两边都跑会用
    // 同步时的旧值覆盖掉刚写进来的新值。放在广播判断之前，广播写也要生效。
    if (exception == MB_EX_NONE) {
        switch (func_code) {
            // 输入
            case MB_FUNC_WRITE_REGISTER:
            case MB_FUNC_WRITE_MULTIPLE_REGISTERS:
            case MB_FUNC_READWRITE_MULTIPLE_REGISTERS:
                Modbus_ApplyHoldingRegs();
                break;
            case MB_FUNC_WRITE_SINGLE_COIL:
            case MB_FUNC_WRITE_MULTIPLE_COILS:
                Modbus_ApplyCoils();
                break;
            default:
                break;
        }
    }

    if (slave_addr == MB_ADDRESS_BROADCAST) return 0; // 广播帧不应答

    if (exception != MB_EX_NONE) {
        pdu_len = 0;
        pdu[pdu_len++] = (UCHAR)(func_code | MB_FUNC_ERROR);
        pdu[pdu_len++] = (UCHAR)exception;
    }

    *p_bin_len = (uint16_t)(1 + pdu_len); // 地址 + 应答 PDU
    return 1;
}

// RTU：地址(1) + 功能码(1) + 数据 + CRC16(2)，全二进制。先整帧搬到独立缓冲区再处理，
// 避免处理期间（flash 擦除会阻塞上百毫秒）新帧改写接收缓冲区。
static void Modbus_ProcessRTUFrame(uint8_t *data, uint16_t len)
{
    // 最短帧：地址(1) + 功能码(1) + CRC16(2)
    if (len < 4 || len > MB_BIN_BUF_SIZE) return;

    uint16_t crc_calc = usMBCRC16(data, len - 2);
    uint16_t crc_recv = (uint16_t)data[len - 2] | ((uint16_t)data[len - 1] << 8);

    // 操作日志：整帧原样记一条，含 CRC 判定结果。放在 return 之前，
    // 校验没过的帧也要留痕（赛题 4.5.7(2) 对异常帧的要求就是"记录日志"）
    oplog_add(OPLOG_PROTO_RTU, OPLOG_DIR_RX, data, len, (crc_calc == crc_recv) ? 1 : 0);

    if (crc_calc != crc_recv) return; // CRC 错误，按 Modbus 规范静默丢弃，不回复

    // CRC 通过说明总线上确实有 Modbus 主站在轮询，哪怕这一帧是发给别的从站的。
    // 从这一刻起本机不再主动发上报帧，避免和主站的轮询撞车。
    s_last_proto = PROTO_SRC_MODBUS;

    uint16_t bin_len = len - 2; // 去掉 CRC，剩 [地址|PDU]
    memcpy(mb_bin_buf, data, bin_len);

    if (!Modbus_DispatchPDU(mb_bin_buf, &bin_len)) return;
    if ((uint32_t)bin_len + 2 > MB_BIN_BUF_SIZE) return; // 应答放不下，丢弃

    // 拼应答帧：[地址不变 | 处理函数已就地写好的应答 PDU | CRC16]
    uint16_t crc_reply = usMBCRC16(mb_bin_buf, bin_len);
    mb_bin_buf[bin_len]     = (uint8_t)(crc_reply & 0xFF);
    mb_bin_buf[bin_len + 1] = (uint8_t)(crc_reply >> 8);

    oplog_add(OPLOG_PROTO_RTU, OPLOG_DIR_TX, mb_bin_buf, bin_len + 2, 1);
    bsp_rs485_send_data(mb_bin_buf, bin_len + 2);

    // 应答发出去之后才能做会打断通信的事：切波特率 / 重启 / 睡眠 / 升级请求
    Modbus_ExecPendingActions();
}

// ASCII：':' + 十六进制文本（每字节 2 个 ASCII 字符，含地址+功能码+数据+LRC）+ CRLF。
// 解码成二进制 → 校验 LRC → 分发 → 重新编码回十六进制文本发送。
// 收发都用独立静态缓冲区，理由同 RTU。
static void Modbus_ProcessASCIIFrame(uint8_t *data, uint16_t len)
{
    // 最短帧：':' + 地址(2字符) + 功能码(2字符) + LRC(2字符) + CR LF = 9
    if (len < 9) return;

    uint16_t hex_len = len - 3; // 去掉开头 ':' 和结尾 CRLF
    if (hex_len % 2 != 0) return;

    uint16_t bin_len = hex_len / 2; // 含地址 + PDU + LRC
    if (bin_len < 3 || bin_len > MB_BIN_BUF_SIZE) return; // 至少要有地址+功能码+LRC

    for (uint16_t i = 0; i < bin_len; i++) {
        uint8_t hi = AsciiHexCharToVal(data[1 + i * 2]);
        uint8_t lo = AsciiHexCharToVal(data[1 + i * 2 + 1]);
        if (hi == 0xFF || lo == 0xFF) return; // 非法字符，丢弃
        mb_bin_buf[i] = (uint8_t)((hi << 4) | lo);
    }

    uint8_t lrc_calc = Modbus_CalcLRC(mb_bin_buf, bin_len - 1);
    uint8_t lrc_recv = mb_bin_buf[bin_len - 1];

    // 操作日志：记的是上线的 ASCII 原文（':' 开头那一串），不是解码后的二进制，
    // 这样查询结果和主站发出去的字符一模一样，好对
    oplog_add(OPLOG_PROTO_ASCII, OPLOG_DIR_RX, data, len, (lrc_calc == lrc_recv) ? 1 : 0);

    if (lrc_calc != lrc_recv) return; // LRC 错误，静默丢弃，不回复

    s_last_proto = PROTO_SRC_MODBUS;  // 同 RTU：确认总线上有 Modbus 主站

    uint16_t frame_len = bin_len - 1; // 去掉 LRC，剩 [地址|PDU]
    if (!Modbus_DispatchPDU(mb_bin_buf, &frame_len)) return;
    if ((uint32_t)frame_len + 1 > MB_BIN_BUF_SIZE) return;

    mb_bin_buf[frame_len] = Modbus_CalcLRC(mb_bin_buf, frame_len); // 补上应答的 LRC
    uint16_t out_bin_len = frame_len + 1;                          // 含 LRC

    // 重新编码回 ':' + 十六进制文本 + CRLF
    uint16_t ascii_len = (uint16_t)(1 + out_bin_len * 2);
    if ((uint32_t)ascii_len + 2 > MB_ASCII_BUF_SIZE) return;

    const char hex_chars[] = "0123456789ABCDEF";
    mb_ascii_buf[0] = ':';
    for (uint16_t i = 0; i < out_bin_len; i++) {
        mb_ascii_buf[1 + i * 2]     = (uint8_t)hex_chars[(mb_bin_buf[i] >> 4) & 0x0F];
        mb_ascii_buf[1 + i * 2 + 1] = (uint8_t)hex_chars[mb_bin_buf[i] & 0x0F];
    }
    mb_ascii_buf[ascii_len]     = '\r';
    mb_ascii_buf[ascii_len + 1] = '\n';

    oplog_add(OPLOG_PROTO_ASCII, OPLOG_DIR_TX, mb_ascii_buf, ascii_len + 2, 1);
    bsp_rs485_send_data(mb_ascii_buf, ascii_len + 2);

    Modbus_ExecPendingActions();
}

static uint8_t AsciiHexCharToVal(uint8_t c)
{
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    return 0xFF;
}

// Modbus ASCII 的 LRC：所有字节求和后取补码（和取反加一）
static uint8_t Modbus_CalcLRC(const uint8_t *buf, uint16_t len)
{
    uint8_t sum = 0;
    for (uint16_t i = 0; i < len; i++) {
        sum = (uint8_t)(sum + buf[i]);
    }
    return (uint8_t)(-(int8_t)sum);
}
