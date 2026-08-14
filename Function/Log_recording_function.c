#include "Log_recording_function.h"
#include <string.h>
#include <stdio.h>



// 内存中的写指针，记录下一条日志该写在Flash的哪个物理地址
static uint32_t current_write_addr = LOG_START_ADDR;

/* ========================================================== */
/*                      内部辅助函数部分                      */
/* ========================================================== */
/**
 * @brief 内部写入基础函数
 */
static uint8_t bsp_log_write_base(uint16_t err_code, const char* msg)
{
    if (current_write_addr >= LOG_END_ADDR) {
        // 如果 256 条存满了，自动擦除从头开始循环覆盖
        bsp_log_clear_all(); 
    }

    bsp_error_log_struct new_log;
    memset(&new_log, 0xFF, sizeof(bsp_error_log_struct)); 
    
    new_log.timestamp = bsp_rtc_get_unix_timestamp(); // 获取 RTC 时间
    new_log.error_code = err_code;
    
    // 安全拷贝字符串到结构体
    strncpy(new_log.message, msg, sizeof(new_log.message) - 1);
    new_log.message[sizeof(new_log.message) - 1] = '\0'; 

    // 调用底层 SPI Flash 写入
    bsp_flash_buffer_write(current_write_addr, (uint8_t*)&new_log, LOG_ENTRY_SIZE);
    
    current_write_addr += LOG_ENTRY_SIZE; // 指针移动到下一个空位
    return SUCCESS;
}

/**
 * @brief 按索引读取底层 Flash 中的日志
 */
static uint8_t bsp_log_read_base(uint32_t index, bsp_error_log_struct* out_log)
{
    uint32_t total_count = bsp_log_get_count();
    
    if (index >= total_count || total_count == 0 || out_log == NULL) {
        return ERROR; 
    }

    uint32_t target_addr = LOG_START_ADDR + (index * LOG_ENTRY_SIZE);
    bsp_flash_buffer_read(target_addr, (uint8_t*)out_log, LOG_ENTRY_SIZE);
    
    return SUCCESS;
}


/* ========================================================== */
/*                      对外接口函数部分                      */
/* ========================================================== */

/**
 * @brief 开机初始化：寻找断电前的写入位置
 */
void bsp_log_init(void)
{
    bsp_error_log_struct temp_log;
    
    // 从头开始扫描，找到时间戳为 0xFFFFFFFF 的地方
    for (current_write_addr = LOG_START_ADDR; current_write_addr < LOG_END_ADDR; current_write_addr += LOG_ENTRY_SIZE) 
    {
        bsp_flash_buffer_read(current_write_addr, (uint8_t*)&temp_log, LOG_ENTRY_SIZE);
        if (temp_log.timestamp == 0xFFFFFFFF) {
            break; 
        }
    }
}

/**
 * @brief 清空整个日志区域
 */
void bsp_log_clear_all(void)
{
    for (uint32_t addr = LOG_START_ADDR; addr < LOG_END_ADDR; addr += BSP_FLASH_SECTOR_SIZE) 
    {
        bsp_flash_sector_erase(addr);
    }
    current_write_addr = LOG_START_ADDR;
}

/**
 * @brief 获取当前日志总量
 */
uint32_t bsp_log_get_count(void)
{
    return (current_write_addr - LOG_START_ADDR) / LOG_ENTRY_SIZE;
}

/**
 * @brief 内部存储：记录时间与超阈值告警到 Flash 
 */
void bsp_log_record_alarm(const char* time_str, const char* channel, float threshold, float actual_value)
{
    char msg_buf[80]; 
    
    // 按照格式化存储数据部分："时间 | 通道 | 阈值 | 实际值"
    snprintf(msg_buf, sizeof(msg_buf), "%s | %s | %.2f | %.2f", 
             time_str, channel, threshold, actual_value);
    
    // 写入 Flash (定义 0x0001 代表超限告警)
    bsp_log_write_base(0x0001, msg_buf);
}

/**
 * @brief 主动上报模式下的单次触发报警并存储
 *        效果：存入 Flash 的同时，立即通过 RS485 按照规定格式发出
 */
void bsp_log_active_report_and_record(const char* channel, float threshold, float actual_value)
{
    //获取当前时间
    bsp_rtc_show_time();

    // BCD 转 十进制
    int year  = BCD2DEC(bsp_rtc_init_para.year);
    int month = BCD2DEC(bsp_rtc_init_para.month);
    int day   = BCD2DEC(bsp_rtc_init_para.date);
    int hour  = BCD2DEC(bsp_rtc_init_para.hour) + 8; // 
    int min   = BCD2DEC(bsp_rtc_init_para.minute);
    int sec   = BCD2DEC(bsp_rtc_init_para.second);

    // 处理跨天溢出
    if(hour >= 24) hour -= 24;

    //将时间数字组合成字符串
    char time_str[24];
    snprintf(time_str, sizeof(time_str), "%04d-%02d-%02d %02d:%02d:%02d", 
             2000 + year, month, day, hour , min, sec);

    //存入 Flash 中 
    bsp_log_record_alarm(time_str, channel, threshold, actual_value);

    // 组装最终报文 
    char report_buf[128];
    snprintf(report_buf, sizeof(report_buf), "%s|%s|%.2f|%.2f\r\n", 
             time_str, channel, threshold, actual_value);

    //直接通过 RS485
    // 操作日志：告警主动上报是设备自己发起的报文，赛题 2.2 归在"不封帧的字符串回复"
    // 这一类，记进自定义协议的 TX 环。0x0602 查询告警的应答不记 —— 那是一次发十条，
    // 会把只有 OPLOG_DEPTH 条的环整个刷掉
    oplog_add(OPLOG_PROTO_CUSTOM, OPLOG_DIR_TX,
              (const uint8_t *)report_buf, (uint16_t)strlen(report_buf), 1);
    bsp_rs485_send_data((const uint8_t *)report_buf, (uint16_t)strlen(report_buf));
}

void bsp_log_query_handler(void)
{
	printf("query: count=%lu\r\n", bsp_log_get_count());
    uint32_t total_count = bsp_log_get_count();
    bsp_error_log_struct temp_log;
    
    if (total_count == 0) {
        bsp_rs485_send_data((const u8 *)"empty", 5);
        return;
    }

    int num_to_read = (total_count > 10) ? 10 : total_count;
    
    for (int i = 0; i < num_to_read; i++) 
    {
        uint32_t target_index = total_count - 1 - i;
        
        if (bsp_log_read_base(target_index, &temp_log) == SUCCESS) 
        {
            // 发送记录正文
            bsp_rs485_send_data((const u8 *)temp_log.message, strlen(temp_log.message));
            
            // 每条记录发完后，发一个换行符
            const char* separator = "\r\n"; 
            bsp_rs485_send_data((const u8 *)separator, 2);
        }
    }
}

/**
 * @brief 仅记录告警到 Flash，不通过 RS485 发送
 */
void bsp_log_record_only(const char* channel, float threshold, float actual_value)
{
    // 获取当前时间
    bsp_rtc_show_time();
    int year  = BCD2DEC(bsp_rtc_init_para.year);
    int month = BCD2DEC(bsp_rtc_init_para.month);
    int day   = BCD2DEC(bsp_rtc_init_para.date);
    int hour  = BCD2DEC(bsp_rtc_init_para.hour) + 8; // 处理时区偏移
    int min   = BCD2DEC(bsp_rtc_init_para.minute);
    int sec   = BCD2DEC(bsp_rtc_init_para.second);
    if(hour >= 24) hour -= 24;

    // 格式化时间字符串 (严格注意日期和时间之间的空格)
    char time_str[24];
    snprintf(time_str, sizeof(time_str), "%04d-%02d-%02d %02d:%02d:%02d", 
             2000 + year, month, day, hour, min, sec);

    // 构造要存入 Flash 的消息 (严格按照图片要求的格式: 时间|通道|阈值|实际值)
    char msg_buf[80];
    snprintf(msg_buf, sizeof(msg_buf), "%s|%s|%.2f|%.2f", 
             time_str, channel, threshold, actual_value);

    // 存入 Flash
    bsp_log_write_base(0x0001, msg_buf);
}



