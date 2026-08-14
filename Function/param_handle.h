#ifndef __PARAM_HANDLE_H
#define __PARAM_HANDLE_H

#include "Headfile.h"

#define BSP_FLASH_PARAM_ADDR  0x000000  // 外部 Flash 参数区起始地址（第一个扇区）

/* 参数区有效性标记。空白 flash 读出来全是 0xFF，被清零的 flash 读出来全是 0，
 * 两者都对不上这个魔术字，param_load() 就会写入一整套出厂默认值。
 * magic 放在结构体最后：尾部追加字段不会改变前面各字段的偏移，老版本写下的数据
 * 依然能按原偏移读出来，只是 magic 对不上而触发一次出厂初始化。 */
#define BSP_FLASH_PARAM_MAGIC     0x43494D43UL   // "CIMC"

/* 出厂默认值，依据初赛赛题：
 *  - 波特率：注意事项 5「嵌入式系统出厂默认必须使用 19200，否则无法正常通讯」
 *  - 设备 ID：3.1「有效范围 0001-FFFE」，取最小合法地址
 *  - 变比  ：1.0 即上报原始采样值（2.3「返回的通道数据必须是原始采样值 × 当前变比」）
 *  - 阈值  ：赛题未规定默认值，取 ADC 满量程 3.3V，保证上电不会立刻触发告警 */
#define PARAM_DEFAULT_DEVICE_ID   0x0001U
#define PARAM_DEFAULT_BAUDRATE    19200U
#define PARAM_DEFAULT_RATIO       1.0f
#define PARAM_DEFAULT_THRESHOLD   3.3f

/* 设备地址边界，见协议 4.5.1(5)：0x0000 未分配禁止使用，0xFFFF 是广播地址 */
#define PARAM_DEVICE_ID_MIN       0x0001U
#define PARAM_DEVICE_ID_MAX       0xFFFEU

/* Modbus 从站地址。规范里 1~247 是有效从站地址，0 是广播，248~255 保留。
 * 和自定义协议的设备 ID 是两套独立身份，互不影响。 */
#define PARAM_DEFAULT_MB_ADDR     0x01U
#define PARAM_MB_ADDR_MIN         1U
#define PARAM_MB_ADDR_MAX         247U

typedef enum{
	ch0_ratio_index = 0,
	ch1_ratio_index = 1,
	ch0_threshold_index = 2,
	ch1_threshold_index = 3,
	
}log_object_index_enum;

/* 赛题 2.4 要求重启后仍有效的参数一共 5 项：CH0/CH1 变比、CH0/CH1 阈值、设备 ID、
 * 波特率、告警记录。前四类放在这个结构体里，告警记录另有独立的 flash 区域，
 * 由 Log_recording_function.c 管理。上报间隔、告警模式不在赛题的持久化清单里，
 * 所以刻意不落盘。 */
typedef struct {
    float    val_f[5];    // [0]CH0变比 [1]CH1变比 [2]CH0阈值 [3]CH1阈值
    uint16_t val_u16[5];  // [0]设备 ID
    uint32_t val_u32[5];  // [0]波特率
    uint8_t  val_u8[5];   // [0]Modbus 从站地址
    uint32_t magic;       // 必须留在最后，见上面 BSP_FLASH_PARAM_MAGIC 的说明
} bsp_flash_param_t;


void param_load(void);
void param_restore_defaults(void);

// float 参数读写
float param_get_float(log_object_index_enum index);
void  param_set_float(log_object_index_enum index, float value);

// 设备 ID 读写
uint16_t param_get_id(void);
void     param_set_id(uint16_t id);

// 波特率读写
uint32_t param_get_baud(void);
void     param_set_baud(uint32_t baud);

// Modbus 从站地址读写
uint8_t param_get_mb_addr(void);
void    param_set_mb_addr(uint8_t addr);

#endif
