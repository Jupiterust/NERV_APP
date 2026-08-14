#include "Headfile.h"
#include "port.h"

/* 把设备数据和 Free_Modbus 的寄存器缓冲区对起来。
 *
 * 改点表不用动这个文件：地址、字序、缩放倍数、从站地址、命令字全部集中在
 * Function/modbus_data_map.h。只有新增官方要求的字段时才回来加两行，
 * 照抄现成的任意一行即可，步骤见 .h 的【1】场景 F。
 *
 * 缓冲区本身（REG_INPUT_BUF / REG_HOLD_BUF / REG_COILS_BUF / REG_DISC_BUF）定义在
 * vendor 移植层 Free_Modbus/port/port.c，这里只做设备数据和寄存器之间的搬运。
 * 业务逻辑不下沉到 Protocol 层，符合赛题 2.1 的分层要求。
 *
 * 调用时机由 Protocol_Router.c 的 Modbus_DispatchPDU() 负责：
 *   分发前         Modbus_SyncRegsFromDevice()   保证主站读到当前值
 *   写保持寄存器后  Modbus_ApplyHoldingRegs()     参数落到设备并存 flash
 *   写线圈后       Modbus_ApplyCoils()           开关量落到设备
 *   应答发完后     Modbus_ExecPendingActions()   执行会打断通信的动作
 * 所以不需要在主循环里周期刷新，也不存在读到上一轮陈旧数据的问题。
 */

/* Modbus 的 4 个寄存器缓冲区，定义在 Free_Modbus/port/port.c。
   点表开着还是关着，它们都在，主站读写的就是这 4 个数组。 */
extern uint16_t REG_INPUT_BUF[REG_INPUT_SIZE];
extern uint16_t REG_HOLD_BUF[REG_HOLD_SIZE];
extern uint8_t  REG_COILS_BUF[REG_COILS_SIZE];
extern uint8_t  REG_DISC_BUF[REG_DISC_SIZE];

extern volatile uint8_t g_log_busy;   /* 定义在 Function.c，flash 忙标志 */

/* 决赛三路采样的容器在 Data_class_structure 里（Function.h 结构体末尾的
   i0_current / v0_voltage / v1_voltage / i0_broken），不在本文件。
   放那边的原因：和 ch0_current_val 这些实时量放在一起，业务数据统一归 Function/ 管；
   Function.h 经 Headfile.h 被每个源文件包含，ADC 驱动直接写不用补 extern；
   而且不受下面的点表总开关影响，关掉也编译得过。 */


#ifdef MB_POINT_MAP_ENABLE
/* ###########################################################################
 * ##  点表总开关 = 开，下面是现成的这套映射。                               ##
 * ##  开关在 modbus_data_map.h 最上面，注释掉那一行就切到本文件末尾的       ##
 * ##  #else 分支（空壳 + 自己写映射的模板）                                 ##
 * ########################################################################### */


/* ========================================================================
 *  编译期越界检查。新增字段时第 2 步就是往这个列表里抄一行。
 *
 *  地址填错或超出表容量会在编译期报 "size of array is negative"，名字里带
 *  mb_chk_ 的那一项就是出问题的字段，不用等下载到板子上踩内存。
 *
 *  参数：MB_CHK(标签名, 地址宏, 占几个寄存器, 表容量宏)
 *      占几个寄存器：float / u32 填 2，u16 / i16 / 线圈 / 离散输入填 1
 *      表容量宏：输入寄存器 REG_INPUT_SIZE，保持寄存器 REG_HOLD_SIZE，
 *                线圈 REG_COILS_SIZE，离散输入 REG_DISC_SIZE，
 *                实际数值在 modbus_data_map.h 的【3】里改
 * ======================================================================== */
#define MB_CHK(tag, addr, nregs, size) \
    typedef char mb_chk_##tag[((int)(addr) >= 0 && (int)((addr) + (nregs)) <= (int)(size)) ? 1 : -1]

MB_CHK(ireg_ch0_value,   MB_IREG_CH0_VALUE,      2, REG_INPUT_SIZE);
MB_CHK(ireg_ch1_value,   MB_IREG_CH1_VALUE,      2, REG_INPUT_SIZE);
MB_CHK(ireg_ch2_temp,    MB_IREG_CH2_TEMP,       2, REG_INPUT_SIZE);
MB_CHK(ireg_ch0_raw,     MB_IREG_CH0_RAW,        2, REG_INPUT_SIZE);
MB_CHK(ireg_ch1_raw,     MB_IREG_CH1_RAW,        2, REG_INPUT_SIZE);
MB_CHK(ireg_ch0_scaled,  MB_IREG_CH0_SCALED,     1, REG_INPUT_SIZE);
MB_CHK(ireg_ch1_scaled,  MB_IREG_CH1_SCALED,     1, REG_INPUT_SIZE);
MB_CHK(ireg_ch2_scaled,  MB_IREG_CH2_SCALED,     1, REG_INPUT_SIZE);
MB_CHK(ireg_timestamp,   MB_IREG_TIMESTAMP,      2, REG_INPUT_SIZE);
MB_CHK(ireg_fw_version,  MB_IREG_FW_VERSION,     2, REG_INPUT_SIZE);
MB_CHK(ireg_auto_flag,   MB_IREG_AUTO_FLAG,      1, REG_INPUT_SIZE);
MB_CHK(ireg_alarm_mode,  MB_IREG_ALARM_MODE,     1, REG_INPUT_SIZE);
MB_CHK(ireg_report_int,  MB_IREG_REPORT_INTVL,   1, REG_INPUT_SIZE);
MB_CHK(ireg_baud_code,   MB_IREG_BAUD_CODE,      1, REG_INPUT_SIZE);
MB_CHK(ireg_device_id,   MB_IREG_DEVICE_ID,      1, REG_INPUT_SIZE);
MB_CHK(ireg_mb_addr,     MB_IREG_MB_SLAVE_ADDR,  1, REG_INPUT_SIZE);
MB_CHK(ireg_proto_mode,  MB_IREG_PROTOCOL_MODE,  1, REG_INPUT_SIZE);
MB_CHK(ireg_alarm_count, MB_IREG_ALARM_COUNT,    1, REG_INPUT_SIZE);
MB_CHK(ireg_sd_ready,    MB_IREG_SD_READY,       1, REG_INPUT_SIZE);
MB_CHK(ireg_i0_current,  MB_IREG_I0_CURRENT,     2, REG_INPUT_SIZE);
MB_CHK(ireg_v0_voltage,  MB_IREG_V0_VOLTAGE,     2, REG_INPUT_SIZE);
MB_CHK(ireg_v1_voltage,  MB_IREG_V1_VOLTAGE,     2, REG_INPUT_SIZE);
MB_CHK(ireg_i0_broken,   MB_IREG_I0_BROKEN,      1, REG_INPUT_SIZE);
MB_CHK(ireg_i0_scaled,   MB_IREG_I0_SCALED,      1, REG_INPUT_SIZE);
MB_CHK(ireg_v0_scaled,   MB_IREG_V0_SCALED,      1, REG_INPUT_SIZE);
MB_CHK(ireg_v1_scaled,   MB_IREG_V1_SCALED,      1, REG_INPUT_SIZE);
MB_CHK(ireg_log_valid,   MB_IREG_LOG_VALID,      1, REG_INPUT_SIZE);
MB_CHK(ireg_log_ts,      MB_IREG_LOG_TIMESTAMP,  2, REG_INPUT_SIZE);
MB_CHK(ireg_log_text,    MB_IREG_LOG_TEXT,       MB_IREG_LOG_TEXT_REGS, REG_INPUT_SIZE);

MB_CHK(hreg_ch0_ratio,   MB_HREG_CH0_RATIO,      2, REG_HOLD_SIZE);
MB_CHK(hreg_ch1_ratio,   MB_HREG_CH1_RATIO,      2, REG_HOLD_SIZE);
MB_CHK(hreg_ch0_thres,   MB_HREG_CH0_THRES,      2, REG_HOLD_SIZE);
MB_CHK(hreg_ch1_thres,   MB_HREG_CH1_THRES,      2, REG_HOLD_SIZE);
MB_CHK(hreg_dac_code,    MB_HREG_DAC_CODE,       1, REG_HOLD_SIZE);
MB_CHK(hreg_report_int,  MB_HREG_REPORT_INTVL,   1, REG_HOLD_SIZE);
MB_CHK(hreg_alarm_mode,  MB_HREG_ALARM_MODE,     1, REG_HOLD_SIZE);
MB_CHK(hreg_device_id,   MB_HREG_DEVICE_ID,      1, REG_HOLD_SIZE);
MB_CHK(hreg_baud_code,   MB_HREG_BAUD_CODE,      1, REG_HOLD_SIZE);
MB_CHK(hreg_mb_addr,     MB_HREG_MB_SLAVE_ADDR,  1, REG_HOLD_SIZE);
MB_CHK(hreg_set_time,    MB_HREG_SET_TIME,       2, REG_HOLD_SIZE);
MB_CHK(hreg_alarm_index, MB_HREG_ALARM_INDEX,    1, REG_HOLD_SIZE);
MB_CHK(hreg_command,     MB_HREG_COMMAND,        1, REG_HOLD_SIZE);

MB_CHK(coil_auto,        MB_COIL_AUTO_REPORT,    1, REG_COILS_SIZE);
MB_CHK(coil_alarm,       MB_COIL_ALARM_ACTIVE,   1, REG_COILS_SIZE);
MB_CHK(coil_clear,       MB_COIL_CLEAR_ALARM,    1, REG_COILS_SIZE);
MB_CHK(coil_log_sd,      MB_COIL_LOG_TO_SD,      1, REG_COILS_SIZE);

MB_CHK(disc_ch0_alarm,   MB_DISC_CH0_ALARM,      1, REG_DISC_SIZE);
MB_CHK(disc_ch1_alarm,   MB_DISC_CH1_ALARM,      1, REG_DISC_SIZE);
MB_CHK(disc_auto,        MB_DISC_AUTO_REPORT,    1, REG_DISC_SIZE);
MB_CHK(disc_alarm_ne,    MB_DISC_ALARM_NOT_EMPTY,1, REG_DISC_SIZE);
MB_CHK(disc_sd_ready,    MB_DISC_SD_READY,       1, REG_DISC_SIZE);
MB_CHK(disc_i0_broken,   MB_DISC_I0_BROKEN,      1, REG_DISC_SIZE);


/* ========================================================================
 *  推迟到应答发完之后才执行的动作
 * ======================================================================== */
static uint32_t s_pending_baud   = 0;              /* 非 0 表示有待切换的波特率 */
static uint16_t s_pending_action = MB_CMD_NONE;    /* 待执行的命令寄存器动作     */
static uint8_t  s_sd_ok          = 0;              /* 最近一次 TF 卡操作是否成功 */


/* ========================================================================
 *  基础读写工具
 * ======================================================================== */

/* 多字节量的排列方式集中在下面这两个函数里，MB_FLOAT_BYTE_ORDER 一改 float 和 u32
 * 全部跟着变。两个函数互为逆运算，读写始终自洽。
 * reg_swap16 用宏不用 static 函数：模式 0/1 下它不被引用，写成函数 armcc 会报
 * "#177-D: declared but never referenced" 的告警 */
#define reg_swap16(v)   ((uint16_t)(((uint16_t)(v) << 8) | ((uint16_t)(v) >> 8)))

static void reg_write_u32(uint16_t *buf, uint16_t idx, uint32_t value)
{
    uint16_t hi = (uint16_t)(value >> 16);      /* AB */
    uint16_t lo = (uint16_t)(value & 0xFFFF);   /* CD */

#if   (MB_FLOAT_BYTE_ORDER == 1)                /* CD AB */
    buf[idx] = lo;              buf[idx + 1] = hi;
#elif (MB_FLOAT_BYTE_ORDER == 2)                /* BA DC */
    buf[idx] = reg_swap16(hi);  buf[idx + 1] = reg_swap16(lo);
#elif (MB_FLOAT_BYTE_ORDER == 3)                /* DC BA */
    buf[idx] = reg_swap16(lo);  buf[idx + 1] = reg_swap16(hi);
#else                                           /* AB CD（默认） */
    buf[idx] = hi;              buf[idx + 1] = lo;
#endif
}

static uint32_t reg_read_u32(const uint16_t *buf, uint16_t idx)
{
    uint16_t hi, lo;

#if   (MB_FLOAT_BYTE_ORDER == 1)                /* CD AB */
    lo = buf[idx];              hi = buf[idx + 1];
#elif (MB_FLOAT_BYTE_ORDER == 2)                /* BA DC */
    hi = reg_swap16(buf[idx]);  lo = reg_swap16(buf[idx + 1]);
#elif (MB_FLOAT_BYTE_ORDER == 3)                /* DC BA */
    lo = reg_swap16(buf[idx]);  hi = reg_swap16(buf[idx + 1]);
#else                                           /* AB CD（默认） */
    hi = buf[idx];              lo = buf[idx + 1];
#endif

    return ((uint32_t)hi << 16) | (uint32_t)lo;
}

static void reg_write_float(uint16_t *buf, uint16_t idx, float value)
{
    union { float f; uint32_t u; } cv;
    cv.f = value;
    reg_write_u32(buf, idx, cv.u);
}

static float reg_read_float(const uint16_t *buf, uint16_t idx)
{
    union { float f; uint32_t u; } cv;
    cv.u = reg_read_u32(buf, idx);
    return cv.f;
}

/* 缩放整数：官方点表是放大 N 倍的 16 位整数时用。
 * 有饱和处理，变比设大后不会因溢出翻成负数。 */
static void reg_write_scaled(uint16_t *buf, uint16_t idx, float value, float scale)
{
    float v = value * scale;
    if (v >  32767.0f) v =  32767.0f;
    if (v < -32768.0f) v = -32768.0f;
    buf[idx] = (uint16_t)(int16_t)v;
}

/* 按位比较而不是用 !=：空白 flash 里读出来的变比/阈值是 NaN，NaN != NaN 恒成立，
 * 用浮点比较会每轮轮询都判定值变了，反复擦写参数扇区 */
static uint8_t float_changed(float a, float b)
{
    union { float f; uint32_t u; } ua, ub;
    ua.f = a;
    ub.f = b;
    return (uint8_t)(ua.u != ub.u);
}

/* 波特率和编码的互转。编码值在 modbus_data_map.h 的【2】里改，
 * 两个函数互为逆运算，改宏时两边一起变，不用动这里。 */
static uint16_t baud_to_code(uint32_t baud)
{
    switch (baud) {
        case 4800:   return MB_BAUD_CODE_4800;
        case 9600:   return MB_BAUD_CODE_9600;
        case 19200:  return MB_BAUD_CODE_19200;
        case 115200: return MB_BAUD_CODE_115200;
        default:     return MB_BAUD_CODE_DEFAULT;
    }
}

static uint32_t code_to_baud(uint16_t code)
{
    switch (code) {
        case MB_BAUD_CODE_4800:   return 4800;
        case MB_BAUD_CODE_9600:   return 9600;
        case MB_BAUD_CODE_19200:  return 19200;
        case MB_BAUD_CODE_115200: return 115200;
        default:                  return 0;   /* 0 表示非法，调用方据此忽略 */
    }
}

/* 把告警记录文本按每寄存器 2 个 ASCII 字符铺进寄存器，高字节在前，不足补空格。
 * 主站按字符串读出来是 "时间|通道|阈值|实际值"。 */
static void reg_write_text(uint16_t *buf, uint16_t idx, uint16_t nregs, const char *s)
{
    uint16_t i;
    uint16_t n = (uint16_t)strlen(s);
    for (i = 0; i < nregs; i++) {
        uint8_t hi = (uint8_t)((i * 2)     < n ? s[i * 2]     : ' ');
        uint8_t lo = (uint8_t)((i * 2 + 1) < n ? s[i * 2 + 1] : ' ');
        buf[idx + i] = (uint16_t)(((uint16_t)hi << 8) | lo);
    }
}


/* ========================================================================
 *  设备数据 -> 寄存器
 *  每一项一行，加字段就照着抄一行，并在上面补一条 MB_CHK
 * ======================================================================== */
void Modbus_SyncRegsFromDevice(void)
{
    uint8_t  ch0_over, ch1_over;
    uint32_t log_total;
    uint16_t log_index;

    /* ---------------- 输入寄存器：实时采样值 ---------------- */
    reg_write_float(REG_INPUT_BUF, MB_IREG_CH0_VALUE, Data_class_structure.ch0_current_val);
    reg_write_float(REG_INPUT_BUF, MB_IREG_CH1_VALUE, Data_class_structure.ch1_current_val);
    /* CH2 取缓存值，不要在这里现采：ad3344_read_chX() 是阻塞的（500SPS 单次模式
       一路约 2.5ms），挂在报文分发路径上会拖慢 Modbus 应答。缓存由 TIM9 的
       ad3344_tim9_10ms_isr() 每 30ms 刷一次，开关见 bsp_GD30AD3344.h【1.2】。 */
    reg_write_float(REG_INPUT_BUF, MB_IREG_CH2_TEMP,  Data_class_structure.ch2_current_temp);
    reg_write_float(REG_INPUT_BUF, MB_IREG_CH0_RAW,   Data_class_structure.ch0_original_val);
    reg_write_float(REG_INPUT_BUF, MB_IREG_CH1_RAW,   Data_class_structure.ch1_original_val);

    /* 同一批数据的缩放整数镜像，倍数在 modbus_data_map.h 的【4】里改 */
    reg_write_scaled(REG_INPUT_BUF, MB_IREG_CH0_SCALED, Data_class_structure.ch0_current_val,  MB_SCALE_CH0);
    reg_write_scaled(REG_INPUT_BUF, MB_IREG_CH1_SCALED, Data_class_structure.ch1_current_val,  MB_SCALE_CH1);
    reg_write_scaled(REG_INPUT_BUF, MB_IREG_CH2_SCALED, Data_class_structure.ch2_current_temp, MB_SCALE_CH2);

    /* ---------------- 输入寄存器：时间与版本 ---------------- */
    reg_write_u32(REG_INPUT_BUF, MB_IREG_TIMESTAMP,  bsp_rtc_get_unix_timestamp());
    reg_write_u32(REG_INPUT_BUF, MB_IREG_FW_VERSION, get_app_version());

    /* ---------------- 输入寄存器：运行状态 ---------------- */
    log_total = bsp_log_get_count();

    REG_INPUT_BUF[MB_IREG_AUTO_FLAG]     = Data_class_structure.Regular_reporting_Flag;
    REG_INPUT_BUF[MB_IREG_ALARM_MODE]    = Data_class_structure.alarm_mode;
    REG_INPUT_BUF[MB_IREG_REPORT_INTVL]  = Data_class_structure.report_interval_code;
    REG_INPUT_BUF[MB_IREG_BAUD_CODE]     = baud_to_code(BSP_RS485_BAUDRATE);
    REG_INPUT_BUF[MB_IREG_DEVICE_ID]     = MY_DEVICE_ID;
    REG_INPUT_BUF[MB_IREG_MB_SLAVE_ADDR] = Modbus_slave_addr;
    REG_INPUT_BUF[MB_IREG_PROTOCOL_MODE] = Protocol_mode;
    REG_INPUT_BUF[MB_IREG_ALARM_COUNT]   = (uint16_t)log_total;
    REG_INPUT_BUF[MB_IREG_SD_READY]      = s_sd_ok;

    /* ---------------- 输入寄存器：决赛三路采样 ---------------- */
    reg_write_float(REG_INPUT_BUF, MB_IREG_I0_CURRENT, Data_class_structure.i0_current);
    reg_write_float(REG_INPUT_BUF, MB_IREG_V0_VOLTAGE, Data_class_structure.v0_voltage);
    reg_write_float(REG_INPUT_BUF, MB_IREG_V1_VOLTAGE, Data_class_structure.v1_voltage);
    REG_INPUT_BUF[MB_IREG_I0_BROKEN] = Data_class_structure.i0_broken;
    reg_write_scaled(REG_INPUT_BUF, MB_IREG_I0_SCALED, Data_class_structure.i0_current, MB_SCALE_I0);
    reg_write_scaled(REG_INPUT_BUF, MB_IREG_V0_SCALED, Data_class_structure.v0_voltage, MB_SCALE_V0);
    reg_write_scaled(REG_INPUT_BUF, MB_IREG_V1_SCALED, Data_class_structure.v1_voltage, MB_SCALE_V1);

    /* ---------------- 输入寄存器：告警记录读窗口 ---------------- */
    /* 序号方向由 modbus_data_map.h 的【9】MB_LOG_INDEX_NEWEST_FIRST 决定：
         1（默认）序号 0 = 最新一条，越大越旧，和自定义协议 0x0602 的倒序一致
         0        序号 0 = 最旧一条，越大越新
       日志从 LOG_START_ADDR 开始逐条往后追加，所以从旧到新第 n 条的地址是
       LOG_START_ADDR + n * LOG_ENTRY_SIZE。序号越界就把有效位置 0。 */
    log_index = REG_HOLD_BUF[MB_HREG_ALARM_INDEX];
    if (log_total > 0 && log_index < log_total) {
        bsp_error_log_struct entry;
#if (MB_LOG_INDEX_NEWEST_FIRST != 0)
        uint32_t entry_no = log_total - 1 - log_index;   /* 0 = 最新 */
#else
        uint32_t entry_no = log_index;                   /* 0 = 最旧 */
#endif
        uint32_t addr = LOG_START_ADDR + entry_no * LOG_ENTRY_SIZE;

        bsp_flash_buffer_read(addr, (uint8_t *)&entry, LOG_ENTRY_SIZE);
        entry.message[sizeof(entry.message) - 1] = '\0';

        REG_INPUT_BUF[MB_IREG_LOG_VALID] = 1;
        reg_write_u32(REG_INPUT_BUF, MB_IREG_LOG_TIMESTAMP, entry.timestamp);
        reg_write_text(REG_INPUT_BUF, MB_IREG_LOG_TEXT, MB_IREG_LOG_TEXT_REGS, entry.message);
    } else {
        REG_INPUT_BUF[MB_IREG_LOG_VALID] = 0;
        reg_write_u32(REG_INPUT_BUF, MB_IREG_LOG_TIMESTAMP, 0);
        reg_write_text(REG_INPUT_BUF, MB_IREG_LOG_TEXT, MB_IREG_LOG_TEXT_REGS, "");
    }

    /* ---------------- 保持寄存器：参数当前值 ---------------- */
    reg_write_float(REG_HOLD_BUF, MB_HREG_CH0_RATIO, Data_class_structure.ch0_ratio);
    reg_write_float(REG_HOLD_BUF, MB_HREG_CH1_RATIO, Data_class_structure.ch1_ratio);
    reg_write_float(REG_HOLD_BUF, MB_HREG_CH0_THRES, Data_class_structure.ch0_threshold);
    reg_write_float(REG_HOLD_BUF, MB_HREG_CH1_THRES, Data_class_structure.ch1_threshold);

    /* DAC 直接读硬件输出寄存器，不设影子变量，自定义协议 0x0301 改过也能同步反映 */
    REG_HOLD_BUF[MB_HREG_DAC_CODE]      = dac_output_value_get(BSP_DAC, DAC_OUT0);
    REG_HOLD_BUF[MB_HREG_REPORT_INTVL]  = Data_class_structure.report_interval_code;
    REG_HOLD_BUF[MB_HREG_ALARM_MODE]    = Data_class_structure.alarm_mode;
    REG_HOLD_BUF[MB_HREG_DEVICE_ID]     = MY_DEVICE_ID;
    REG_HOLD_BUF[MB_HREG_BAUD_CODE]     = baud_to_code(BSP_RS485_BAUDRATE);
    REG_HOLD_BUF[MB_HREG_MB_SLAVE_ADDR] = Modbus_slave_addr;
    /* 设置时间是只写语义，回读恒为 0，避免主站把上次写的时间戳又写回来一次 */
    reg_write_u32(REG_HOLD_BUF, MB_HREG_SET_TIME, 0);
    REG_HOLD_BUF[MB_HREG_COMMAND]       = MB_CMD_NONE;
    /* MB_HREG_ALARM_INDEX 由主站选定，不要覆盖 */

    /* ---------------- 线圈：开关量当前状态 ---------------- */
    REG_COILS_BUF[MB_COIL_AUTO_REPORT]  = Data_class_structure.Regular_reporting_Flag ? 1 : 0;
    REG_COILS_BUF[MB_COIL_ALARM_ACTIVE] = (Data_class_structure.alarm_mode == 1) ? 1 : 0;
    /* CLEAR_ALARM / LOG_TO_SD 是脉冲型，执行完在 Apply 里归 0，这里不覆盖 */

    /* ---------------- 离散输入：只读状态位 ---------------- */
    ch0_over = (Data_class_structure.ch0_current_val > Data_class_structure.ch0_threshold) ? 1 : 0;
    ch1_over = (Data_class_structure.ch1_current_val > Data_class_structure.ch1_threshold) ? 1 : 0;

    REG_DISC_BUF[MB_DISC_CH0_ALARM]       = ch0_over;
    REG_DISC_BUF[MB_DISC_CH1_ALARM]       = ch1_over;
    REG_DISC_BUF[MB_DISC_AUTO_REPORT]     = Data_class_structure.Regular_reporting_Flag ? 1 : 0;
    REG_DISC_BUF[MB_DISC_ALARM_NOT_EMPTY] = (log_total > 0) ? 1 : 0;
    REG_DISC_BUF[MB_DISC_SD_READY]        = s_sd_ok;
    REG_DISC_BUF[MB_DISC_I0_BROKEN]       = Data_class_structure.i0_broken;
}


/* FC07 读取异常状态：把几个关键状态压成一个字节返回。
 * 每一位对应哪个状态在 modbus_data_map.h 的【9】MB_EXST_BIT_* 里改。 */
uint8_t Modbus_GetExceptionStatus(void)
{
    uint8_t st = 0;
    if (REG_DISC_BUF[MB_DISC_CH0_ALARM])       st |= MB_EXST_BIT_CH0_ALARM;
    if (REG_DISC_BUF[MB_DISC_CH1_ALARM])       st |= MB_EXST_BIT_CH1_ALARM;
    if (REG_DISC_BUF[MB_DISC_AUTO_REPORT])     st |= MB_EXST_BIT_AUTO_REPORT;
    if (REG_DISC_BUF[MB_DISC_ALARM_NOT_EMPTY]) st |= MB_EXST_BIT_ALARM_NOT_EMPTY;
    if (REG_DISC_BUF[MB_DISC_SD_READY])        st |= MB_EXST_BIT_SD_READY;
    if (REG_DISC_BUF[MB_DISC_I0_BROKEN])       st |= MB_EXST_BIT_I0_BROKEN;
    return st;
}


/* ========================================================================
 *  保持寄存器 -> 设备（F=06 / F=16 之后调用）
 *  只在值真的变了时才动设备 / 落 flash，避免主站周期性写同一个值
 * ======================================================================== */
void Modbus_ApplyHoldingRegs(void)
{
    float    v;
    uint32_t ts;
    uint32_t baud;
    uint16_t dac_code;
    uint16_t intvl;
    uint16_t mode;
    uint16_t id;
    uint16_t addr;
    uint16_t cmd;

    /* ---------------- 变比 / 阈值 ---------------- */
    /* param_set_float() 每次都是整扇区擦除 + 重写，没有磨损均衡，必须做变化检测，
       否则主站 1s 一次写同一个值会把扇区写穿 */
    // 这里更新到Data_class_structure, 同时写入flash， 
    v = reg_read_float(REG_HOLD_BUF, MB_HREG_CH0_RATIO);
    if (float_changed(v, Data_class_structure.ch0_ratio)) {
        Data_class_structure.ch0_ratio = v;
        param_set_float(ch0_ratio_index, v);
    }

    v = reg_read_float(REG_HOLD_BUF, MB_HREG_CH1_RATIO);
    if (float_changed(v, Data_class_structure.ch1_ratio)) {
        Data_class_structure.ch1_ratio = v;
        param_set_float(ch1_ratio_index, v);
    }

    v = reg_read_float(REG_HOLD_BUF, MB_HREG_CH0_THRES);
    if (float_changed(v, Data_class_structure.ch0_threshold)) {
        Data_class_structure.ch0_threshold = v;
        param_set_float(ch0_threshold_index, v);
    }

    v = reg_read_float(REG_HOLD_BUF, MB_HREG_CH1_THRES);
    if (float_changed(v, Data_class_structure.ch1_threshold)) {
        Data_class_structure.ch1_threshold = v;
        param_set_float(ch1_threshold_index, v);
    }

    /* ---------------- DAC ---------------- */
    /* 寄存器里放 0~4095 的原始码，和自定义协议 0x0301 的下发内容同义。
       上限在 modbus_data_map.h 的【9】MB_DAC_CODE_MAX，超上限截断。 */
    dac_code = REG_HOLD_BUF[MB_HREG_DAC_CODE];
    if (dac_code > MB_DAC_CODE_MAX) {
        dac_code = MB_DAC_CODE_MAX;
    }
    if (dac_code != dac_output_value_get(BSP_DAC, DAC_OUT0)) {
        bsp_dac_set_raw(dac_code);
    }

    /* ---------------- 上报间隔 ---------------- */
    /* 默认 1=1s 2=3s 3=5s，和 CMD_DATA_SET_REPORT_INTVL_FUNCTION 一致。
       只改秒数改 modbus_data_map.h【9】的 MB_REPORT_SEC_CODE1/2/3；
       加第 4 档就把 MB_REPORT_INTVL_MAX 改成 4，并在下面的三目表达式前加一行
       (intvl == 4) ? 新秒数 : 。范围外的写入直接忽略，不报错也不改原值。 */
    intvl = REG_HOLD_BUF[MB_HREG_REPORT_INTVL];
    if (intvl >= MB_REPORT_INTVL_MIN && intvl <= MB_REPORT_INTVL_MAX &&
        intvl != Data_class_structure.report_interval_code) {
        uint16_t sec = (intvl == 3) ? MB_REPORT_SEC_CODE3
                     : (intvl == 2) ? MB_REPORT_SEC_CODE2
                                    : MB_REPORT_SEC_CODE1;
        Data_class_structure.report_interval_code = (uint8_t)intvl;
        bsp_tim7_set_timeout(sec);
    }

    /* ---------------- 告警模式 ---------------- */
    /* 线圈 MB_COIL_ALARM_ACTIVE 里还有一份等价映射，两条路都能改 */
    mode = REG_HOLD_BUF[MB_HREG_ALARM_MODE];
    if (mode == 1 || mode == 2) {
        Data_class_structure.alarm_mode = (uint8_t)mode;
    }

    /* ---------------- 自定义协议设备 ID ---------------- */
    /* 两套身份独立，改它不影响 Modbus 从站地址，本次事务的应答照发，可立即生效 */
    id = REG_HOLD_BUF[MB_HREG_DEVICE_ID];
    if (id >= PARAM_DEVICE_ID_MIN && id <= PARAM_DEVICE_ID_MAX && id != MY_DEVICE_ID) {
        MY_DEVICE_ID = id;
        param_set_id(id);
    }

    /* ---------------- Modbus 从站地址 ---------------- */
    /* 应答帧的地址字节取自请求帧，不受这里影响，本次事务正常回复，下一轮轮询
       开始用新地址。Modbus 设备改地址的惯常做法。 */
    addr = REG_HOLD_BUF[MB_HREG_MB_SLAVE_ADDR];
    if (addr >= PARAM_MB_ADDR_MIN && addr <= PARAM_MB_ADDR_MAX && addr != Modbus_slave_addr) {
        Modbus_slave_addr = (uint8_t)addr;
        param_set_mb_addr((uint8_t)addr);
    }

    /* ---------------- 波特率 ---------------- */
    /* 必须推迟：立刻切的话应答帧会用新波特率发出去，主站收不到，本次事务超时 */
    baud = code_to_baud(REG_HOLD_BUF[MB_HREG_BAUD_CODE]);
    if (baud != 0 && baud != BSP_RS485_BAUDRATE) {
        s_pending_baud = baud;
    }

    /* ---------------- 设置 RTC 时间 ---------------- */
    /* 只写语义：写非 0 就设置一次，Sync 会把回读值清成 0 */
    ts = reg_read_u32(REG_HOLD_BUF, MB_HREG_SET_TIME);
    if (ts != 0) {
        bsp_rtc_set_unix_timestamp(ts, 0);
        reg_write_u32(REG_HOLD_BUF, MB_HREG_SET_TIME, 0);
    }

    /* ---------------- 命令寄存器 ---------------- */
    /* 重启 / 睡眠 / 升级请求都会打断通信，推迟到应答发完再执行 */
    cmd = REG_HOLD_BUF[MB_HREG_COMMAND];
    if (cmd == MB_CMD_REBOOT || cmd == MB_CMD_SLEEP || cmd == MB_CMD_OTA_REQUEST) {
        s_pending_action = cmd;
    }
    REG_HOLD_BUF[MB_HREG_COMMAND] = MB_CMD_NONE;
}


/* ========================================================================
 *  线圈 -> 设备（F=05 / F=15 之后调用）
 * ======================================================================== */
void Modbus_ApplyCoils(void)
{
    uint8_t want_auto;

    /* ---------------- 自动上报开关 ---------------- */
    /* 等同自定义协议 0x0302 / 0x0303，只是不回自定义协议的应答帧 */
    want_auto = REG_COILS_BUF[MB_COIL_AUTO_REPORT] ? 1 : 0;
    if (want_auto != Data_class_structure.Regular_reporting_Flag) {
        if (want_auto) {
            timer_counter_value_config(TIMER7, 0);
            timer_enable(TIMER7);
        } else {
            timer_disable(TIMER7);
        }
        Data_class_structure.Regular_reporting_Flag = want_auto;
    }

    /* ---------------- 告警是否主动上报 ---------------- */
    /* 线圈只有 0/1，映射到协议 0x0601 的 01=主动上报 / 02=仅记录。
       F=05 只写单个线圈时其余线圈还是 Sync 时的值，赋回去是空操作。 */
    Data_class_structure.alarm_mode = REG_COILS_BUF[MB_COIL_ALARM_ACTIVE] ? 1 : 2;

    /* ---------------- 清空告警记录（脉冲型） ---------------- */
    /* bsp_log_clear_all() 要擦 4 个扇区，阻塞约 400ms，本次事务的应答会晚到，
       主站的 Response Timeout 要留够 1s。执行完线圈归 0。 */
    if (REG_COILS_BUF[MB_COIL_CLEAR_ALARM]) {
        REG_COILS_BUF[MB_COIL_CLEAR_ALARM] = 0;
        g_log_busy = 1;
        bsp_log_clear_all();
        g_log_busy = 0;
    }

    /* ---------------- 把当前采样追加到 TF 卡（脉冲型） ---------------- */
    /* 决赛要求 TF 卡文件读写，这里提供一条 CSV 落盘通路。
       文件名在 modbus_data_map.h【9】的 MB_SD_LOG_FILENAME，
       换字段改下面 snprintf 的格式串和参数，一行别超过 line[] 的 96 字节。 */
    if (REG_COILS_BUF[MB_COIL_LOG_TO_SD]) {
        char line[96];
        REG_COILS_BUF[MB_COIL_LOG_TO_SD] = 0;
        snprintf(line, sizeof(line), "%lu,%.3f,%.3f,%.2f\r\n",
                 (unsigned long)bsp_rtc_get_unix_timestamp(),
                 Data_class_structure.ch0_current_val,
                 Data_class_structure.ch1_current_val,
                 Data_class_structure.ch2_current_temp);
        s_sd_ok = bsp_sdio_file_append_text(MB_SD_LOG_FILENAME, line) ? 1 : 0;
    }
}


/* ========================================================================
 *  应答发完之后才能做的动作
 *  由 Protocol_Router.c 在 bsp_rs485_send_data() 之后调用
 * ======================================================================== */
void Modbus_ExecPendingActions(void)
{
    uint16_t act;
    uint32_t baud;

    if (s_pending_baud == 0 && s_pending_action == MB_CMD_NONE) {
        return;                       /* 常态：没有待办 */
    }

    /* bsp_rs485_send_busy 在 TC 中断里、方向切回接收之后才清零，
       等它归 0 才能确认应答帧真的发完 */
    while (bsp_rs485_send_busy) {
        ;
    }

    if (s_pending_baud != 0) {
        baud = s_pending_baud;
        s_pending_baud = 0;
        param_set_baud(baud);              /* 赛题 2.4：波特率要持久化 */
        bsp_rs485_set_baudrate(baud);      /* 内部会重算 T3.5 静默窗口 */
    }

    if (s_pending_action != MB_CMD_NONE) {
        act = s_pending_action;
        s_pending_action = MB_CMD_NONE;

        switch (act) {
            case MB_CMD_REBOOT:
                mcu_restart();
                break;

            case MB_CMD_SLEEP:
                bsp_deepsleep_config();     /* RTC 闹钟 10s 后自动唤醒 */
                /* 唤醒后按赛题 4.5.3(4) 回一个纯字符串，不封帧 */
                bsp_rs485_send_data((const uint8_t *)"instrument wakeup", 17);
                break;

            case MB_CMD_OTA_REQUEST:
                /* 同 CMD_OTA_REQUEST_FUNCTION：写内部 flash 参数块的标志位后软复位，
                   接收 / 校验 / 搬运由兄弟 Bootloader 工程完成 */
                set_app_id_version(MY_DEVICE_ID);
                set_app_updateFlag(app_update_receive_start);
                mcu_restart();
                break;

            default:
                break;
        }
    }
}


#else  /* ==== MB_POINT_MAP_ENABLE 未定义：点表关闭，下面是自己写映射的模板 ==== */

/* ###########################################################################
 * ##                     点表总开关 = 关：自己写映射                        ##
 * ###########################################################################
 *
 * 当前状态：
 *   · Modbus 从站完全正常工作：收发、判帧、地址匹配、CRC/LRC、异常码、全部功能码
 *     （FC01/02/03/04/05/06/07/08/15/16/17/23）都在
 *   · 主站读到的是下面 4 个数组的原始内容，默认全 0
 *   · 关掉的只有设备数据和数组之间的搬运
 *
 * 要做的是把代码填进下面 5 个函数。调用时机 Protocol_Router.c 已经接好，
 * 不用动 Protocol 层，也不用管中断和缓冲区。
 *
 * 4 个数组的用法（下标 = 线上地址 - 【3】里的地址基址，基址为 0 时等于线上地址）：
 *
 *   REG_INPUT_BUF[i]   uint16_t  输入寄存器  FC04 读              测量值
 *   REG_HOLD_BUF[i]    uint16_t  保持寄存器  FC03 读 / FC06、16 写 参数
 *   REG_COILS_BUF[i]   uint8_t   线圈        FC01 读 / FC05、15 写 0 或 1
 *   REG_DISC_BUF[i]    uint8_t   离散输入    FC02 读              0 或 1
 *
 *   容量分别是 REG_INPUT_SIZE / REG_HOLD_SIZE / REG_COILS_SIZE / REG_DISC_SIZE，
 *   数值在 modbus_data_map.h 的【3】里改。写之前要确认下标 < 容量：开关关掉之后
 *   没有编译期越界检查兜底，越界会直接踩内存。
 *
 * 16 位整数直接赋值。float 占 2 个寄存器，写法：
 *
 *     union { float f; uint32_t u; } cv;
 *     cv.f = 浮点值;
 *     REG_INPUT_BUF[i]     = (uint16_t)(cv.u >> 16);   // 高字在前，标准写法
 *     REG_INPUT_BUF[i + 1] = (uint16_t)(cv.u & 0xFFFF);
 *
 *   主站读出来数值离谱就是字序反了，把这两行对调。
 *
 * 各类型的现成例子在 #ifdef 那一半的 Modbus_SyncRegsFromDevice() 里。
 * ########################################################################### */

/* 时机①：每收到一帧发给本机的 Modbus 报文，分发之前调用。
 * 在这里把设备当前数据写进 REG_INPUT_BUF / REG_HOLD_BUF / REG_COILS_BUF /
 * REG_DISC_BUF，保证主站读到此刻的值而不是上一轮的旧值。
 * 不要在这里做耗时操作（阻塞式 ADC 转换、读 flash），会拖慢应答。 */
void Modbus_SyncRegsFromDevice(void)
{
    /* 例：REG_INPUT_BUF[0] = (uint16_t)(Data_class_structure.ch0_current_val * 100); */
}

/* 时机②：主站写保持寄存器（FC06 / FC16 / FC23）成功之后调用。
 * 新值此时已在 REG_HOLD_BUF 里，在这里取出来落到设备上。
 * 要存 flash 的参数先判断值是否真的变了：param_set_* 是整扇区擦写，没有磨损均衡，
 * 主站 1 秒一次写同一个值会把扇区写坏。 */
void Modbus_ApplyHoldingRegs(void)
{
    /* 例：uint16_t v = REG_HOLD_BUF[0]; if (v != 旧值) { 落到设备; 存flash; } */
}

/* 时机③：主站写线圈（FC05 / FC15）成功之后调用，用法同上，读 REG_COILS_BUF。
 * 和上面分成两个函数的原因：同一个量若在保持寄存器和线圈里各映射一份，两边都跑
 * 的话后跑的那份会用同步时的旧值覆盖掉刚写进来的新值。 */
void Modbus_ApplyCoils(void)
{
    /* 例：if (REG_COILS_BUF[0]) { 开启某功能; } */
}

/* 时机④：应答帧发出去之后调用。
 * 会打断通信的动作放这里：切波特率、软复位、进睡眠、跳 Bootloader。
 * 放在②③里做的话，应答还没发出去设备就重启了，主站会判超时。 */
void Modbus_ExecPendingActions(void)
{
    /* 例：if (待切波特率) { while (bsp_rs485_send_busy); bsp_rs485_set_baudrate(x); } */
}

/* 时机⑤：主站发 FC07（读异常状态）时调用，返回 1 个字节，每位一个状态。
 * 用不到就返回 0，主站读到 0x00，不影响别的功能。 */
uint8_t Modbus_GetExceptionStatus(void)
{
    return 0;
}

#endif /* MB_POINT_MAP_ENABLE */
