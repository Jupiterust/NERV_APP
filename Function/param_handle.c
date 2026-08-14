#include "param_handle.h"

static bsp_flash_param_t g_param;  // RAM 中的参数副本，上电由 param_load 填充

// 把当前 RAM 副本整体写回 flash，参数区 <4KB，一个扇区就够。
// 注意：每次都是"整扇区擦除 + 重写"，没有磨损均衡，调用方要避免高频写入。
static void param_save(void)
{
    g_param.magic = BSP_FLASH_PARAM_MAGIC;   // 任何一次落盘都盖上有效性标记

    bsp_flash_sector_erase(BSP_FLASH_PARAM_ADDR);
    bsp_flash_buffer_write(BSP_FLASH_PARAM_ADDR,
                           (u8 *)&g_param, sizeof(bsp_flash_param_t));
}

// 判断 float 是不是有限值：指数位全 1 就是 NaN 或 Inf。
// 用位判断而不是 <math.h> 的 isnan/isfinite —— AC5 默认按 C90 编译，那两个宏不一定可用。
static uint8_t param_float_is_finite(float v)
{
    union { float f; uint32_t u; } cv;
    cv.f = v;
    return (uint8_t)(((cv.u >> 23) & 0xFFU) != 0xFFU);
}

// 波特率白名单，对应协议 4.5.1(7) 的映射：11-4800 12-9600 13-19200 14-115200
static uint8_t param_baud_is_valid(uint32_t baud)
{
    return (uint8_t)(baud == 4800U || baud == 9600U ||
                     baud == 19200U || baud == 115200U);
}

// enum 索引转 val_f 数组下标，其他通道只留接口
static uint8_t param_index_map(log_object_index_enum index)
{
    switch (index) {
        case ch0_ratio_index:     return 0;
        case ch1_ratio_index:     return 1;
        case ch0_threshold_index: return 2;
        case ch1_threshold_index: return 3;
        default:                  return 0;
    }
}

// 写入一整套出厂默认值并落 flash
void param_restore_defaults(void)
{
    memset(&g_param, 0, sizeof(bsp_flash_param_t));

    g_param.val_f[0]   = PARAM_DEFAULT_RATIO;      // CH0 变比
    g_param.val_f[1]   = PARAM_DEFAULT_RATIO;      // CH1 变比
    g_param.val_f[2]   = PARAM_DEFAULT_THRESHOLD;  // CH0 阈值
    g_param.val_f[3]   = PARAM_DEFAULT_THRESHOLD;  // CH1 阈值
    g_param.val_u16[0] = PARAM_DEFAULT_DEVICE_ID;
    g_param.val_u32[0] = PARAM_DEFAULT_BAUDRATE;
    g_param.val_u8[0]  = PARAM_DEFAULT_MB_ADDR;

    param_save();
}

/* 上电从 flash 读参数。
 *
 * 赛题 2.4 要求变比/阈值/设备 ID/波特率重启后仍有效，所以这里除了"读出来"，还得
 * 保证"读出来的能用"：参数区没初始化过时（空白 0xFF 或整块 0），波特率会变成
 * 0xFFFFFFFF 或 0 —— usart_baudrate_set() 算出的分频直接是 0，串口当场不通；
 * 变比会变成 NaN 或 0 —— 所有通道数据变成 NaN 或恒 0，B-04/B-05 的"合法数值"过不了。
 *
 * 分两级处理：
 *   1. magic 对不上 → 整个参数区没初始化过，写入一整套出厂默认值
 *   2. magic 对得上 → 只对有硬性取值范围的字段做体检（设备 ID 范围、波特率白名单、
 *      浮点数不能是 NaN/Inf），修好之后回写一次，下次开机就是干净的
 *
 * 变比/阈值只查 NaN/Inf，不把 0 当非法值：上位机有权把变比设成 0，
 * D-01/F-01 那组测试要求重启后原样读回，这里不能自作主张改掉。 */
void param_load(void)
{
    uint8_t fixed = 0;
    uint8_t i;

    bsp_flash_buffer_read(BSP_FLASH_PARAM_ADDR,
                          (u8 *)&g_param, sizeof(bsp_flash_param_t));

    if (g_param.magic != BSP_FLASH_PARAM_MAGIC) {
        param_restore_defaults();
        return;
    }

    for (i = 0; i < 2; i++) {                 // 变比
        if (!param_float_is_finite(g_param.val_f[i])) {
            g_param.val_f[i] = PARAM_DEFAULT_RATIO;
            fixed = 1;
        }
    }
    for (i = 2; i < 4; i++) {                 // 阈值
        if (!param_float_is_finite(g_param.val_f[i])) {
            g_param.val_f[i] = PARAM_DEFAULT_THRESHOLD;
            fixed = 1;
        }
    }

    if (g_param.val_u16[0] < PARAM_DEVICE_ID_MIN ||
        g_param.val_u16[0] > PARAM_DEVICE_ID_MAX) {
        g_param.val_u16[0] = PARAM_DEFAULT_DEVICE_ID;
        fixed = 1;
    }

    if (!param_baud_is_valid(g_param.val_u32[0])) {
        g_param.val_u32[0] = PARAM_DEFAULT_BAUDRATE;
        fixed = 1;
    }

    if (g_param.val_u8[0] < PARAM_MB_ADDR_MIN || g_param.val_u8[0] > PARAM_MB_ADDR_MAX) {
        g_param.val_u8[0] = PARAM_DEFAULT_MB_ADDR;
        fixed = 1;
    }

    if (fixed) {
        param_save();
    }
}

float param_get_float(log_object_index_enum index)
{
    return g_param.val_f[param_index_map(index)];
}

void param_set_float(log_object_index_enum index, float value)
{
    g_param.val_f[param_index_map(index)] = value;
    param_save();
}

// val_u16[0] 固定存设备 ID
uint16_t param_get_id(void)
{
    return g_param.val_u16[0];
}

// 非法地址不落盘：0x0000 是保留地址，0xFFFF 是广播地址，存进去下次开机就失联了
void param_set_id(uint16_t id)
{
    if (id < PARAM_DEVICE_ID_MIN || id > PARAM_DEVICE_ID_MAX) {
        return;
    }
    g_param.val_u16[0] = id;
    param_save();
}

// val_u32[0] 固定存波特率
uint32_t param_get_baud(void)
{
    return g_param.val_u32[0];
}

// 白名单外的波特率不落盘，避免把自己写成一个开机起不来的值
void param_set_baud(uint32_t baud)
{
    if (!param_baud_is_valid(baud)) {
        return;
    }
    g_param.val_u32[0] = baud;
    param_save();
}

// val_u8[0] 固定存 Modbus 从站地址
uint8_t param_get_mb_addr(void)
{
    return g_param.val_u8[0];
}

// 0 是广播地址、248~255 是保留地址，都不能作为本机身份存进去
void param_set_mb_addr(uint8_t addr)
{
    if (addr < PARAM_MB_ADDR_MIN || addr > PARAM_MB_ADDR_MAX) {
        return;
    }
    g_param.val_u8[0] = addr;
    param_save();
}
