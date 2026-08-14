#ifndef __LOG_RECORDING_FUNCTION_H_
#define __LOG_RECORDING_FUNCTION_H_

#include "Headfile.h"


// GD25Q40E 总容量 512KB (0x000000 ~ 0x07FFFF)。
// 分配最后 4 个扇区 (16KB) 专门用于存储日志
// 0x07C000 = 0x080000 - 0x4000

#define LOG_START_ADDR      0x07C000          // 日志存放起始物理地址
#define LOG_SECTOR_COUNT    4                 // 占用 4 个扇区
#define LOG_MAX_SIZE        (LOG_SECTOR_COUNT * BSP_FLASH_SECTOR_SIZE) // 16KB 总容量
#define LOG_END_ADDR        (LOG_START_ADDR + LOG_MAX_SIZE)

// 固定 64 字节（Flash 一页 256 字节，刚好存 4 条，杜绝跨页错误）
typedef struct {
    uint32_t timestamp;      // 4字节:Unix时间戳 (0xFFFFFFFF表示此位置为空)
    uint16_t error_code;     // 2字节:错误代码 (比如 0x0001 代表超阈值)
    uint16_t reserved;       // 2字节:保留位，用于4字节对齐
    char     message[56];    // 56字节:"通道|阈值|实际值" (必须以'\0'结尾)
} bsp_error_log_struct;

#define LOG_ENTRY_SIZE      sizeof(bsp_error_log_struct)
#define LOG_MAX_ENTRIES     (LOG_MAX_SIZE / LOG_ENTRY_SIZE) // 最多可存 256 条日志


void bsp_log_init(void);                                                // 开机初始化日志系统
void bsp_log_clear_all(void);                                           // 清空所有日志
uint32_t bsp_log_get_count(void);                                       // 获取当前日志总数

// 记录告警数据到底层 Flash
void bsp_log_record_alarm(const char* time_str, const char* channel, float threshold, float actual_value);


void bsp_log_query_handler(void);
void bsp_log_active_report_and_record(const char* channel, float threshold, float actual_value);
void bsp_log_record_only(const char* channel, float threshold, float actual_value);






#endif /* __BSP_LOG_H */



