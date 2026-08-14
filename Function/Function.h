#ifndef __FUNCTION_H_
#define __FUNCTION_H_

#include "Headfile.h"

// 16进制转10进制
#define BCD2DEC(val) (((val) >> 4) * 10 + ((val) & 0x0F))


// 定时时间过滤器 
//0,上报期间不允许改间隔,不返回应答
//1,上报期间允许改间隔,并返回应答
//2,上报期间允许改间隔,但不返回应答
#define AUTO_REPORT_ALLOW_SET_INTVL   0 


// 数据类参数结构体
typedef struct {
    // 配置类 (需要存储到Flash)
    float ch0_ratio;        // CH0 变比
    float ch1_ratio;        // CH1 变比
    float ch0_threshold;    // CH0 告警阈值
    float ch1_threshold;    // CH1 告警阈值
	float ch2_threshold;    // CH2 告警阈值
    uint8_t report_interval_code; // 上报间隔代码 (01, 02, 03)
    uint8_t alarm_mode;     // 告警模式 (01-主动, 02-被动)
    
    // 实时运行数据 (仅内存保留)
	float ch0_original_val; // ch0的原始值
	float ch1_original_val; // ch1的原始值
    float ch0_current_val;  // 经变比换算后的实时值
    float ch1_current_val;  // 经变比换算后的实时值
    float ch2_current_temp; // PT100 实际温度
    uint32_t last_report_time; // 上次上报的时间戳
	uint8_t Regular_reporting_Flag; // 开关定时器标志位

    //决赛样题增加的东西 —— 三路采样，ADC 驱动直接往这里填即可。
    //（定义在 modbus_data_map.c 里，外面看不见；
    //  搬到这里之后，凡是包含 Headfile.h 的文件都能直接读写。）
    float   i0_current;     // 4~20mA 通道电流，单位 mA
    float   v0_voltage;     // 0~10V 通道 0，单位 V
    float   v1_voltage;     // 0~10V 通道 1，单位 V
    uint8_t i0_broken;      // 4~20mA 断线标志，1 = 断线
} DeviceDataParams_t;


extern DeviceDataParams_t Data_class_structure;


void mcu_restart(void);

//系统管理类
void CMD_SYS_REBOOT_FUNCTION();
void CMD_SYS_FACTORY_RESET_FUNCTION();
void CMD_SYS_READ_INFO_FUNCTION();
void CMD_SYS_READ_VER_FUNCTION();
void CMD_SYS_SET_TIME_FUNCTION(uint8_t* DATA_ARRAY);
void CMD_SYS_READ_TIME_FUNCTION();
void CMD_SYS_SET_ID_FUNCTION(uint8_t* DATA_ARRAY);
void CMD_SYS_SET_BAUDRATE_FUNCTION(uint8_t* DATA_ARRAY);
void CMD_SYS_READ_ID_FUNCTION();
void CMD_SYS_READ_BAUDRATE_FUNCTION();

//数据类
void CMD_DATA_READ_CH0_FUNCTION();
void CMD_DATA_READ_CH1_FUNCTION();
void CMD_DATA_READ_PT100_FUNCTION();
void CMD_DATA_SET_CH0_RATIO_FUNCTION(uint8_t* DATA_ARRAY);
void CMD_DATA_SET_CH1_RATIO_FUNCTION(uint8_t* DATA_ARRAY);
void CMD_DATA_SET_REPORT_INTVL_FUNCTION();

// 控制类
void CMD_CTRL_SET_DAC_VOLT_FUNCTION(uint8_t* DATA_ARRAY);
void CMD_CTRL_AUTO_REPORT_START_FUNCTION();
void CMD_CTRL_AUTO_REPORT_STOP_FUNCTION();
void CMD_CTRL_ENTER_SLEEP_FUNCTION();

//参数配置类
void CMD_CFG_READ_THRES_ALL_FUNCTION();
void CMD_CFG_READ_THRES_CH0_FUNCTION();
void CMD_CFG_READ_THRES_CH1_FUNCTION();
void CMD_CFG_READ_THRES_CH2_FUNCTION();
void CMD_CFG_WRITE_THRES_CH0_FUNCTION(uint8_t* DATA_ARRAY);
void CMD_CFG_WRITE_THRES_CH1_FUNCTION(uint8_t* DATA_ARRAY);
void CMD_CFG_WRITE_THRES_CH2_FUNCTION(uint8_t* DATA_ARRAY);

//系统升级类
void CMD_OTA_REQUEST_FUNCTION();
void CMD_OTA_READY_DATA_FUNCTION();
void CMD_OTA_EXECUTE_FUNCTION();

// 告警与日志类
void CMD_ALARM_SET_AUTO_REPORT_FUNCTION(uint8_t* DATA_ARRAY);
void CMD_ALARM_READ_RECORD_FUNCTION();
void CMD_ALARM_CLEAR_FUNCTION();
void CMD_LOG_READ_FUNCTION();
void CMD_LOG_CLEAR_FUNCTION();

// 特殊帧类型

void CMD_SPEC_DISCOVER_FUNCTION();



//
void report_police_function(uint8_t alarm_mode);
void report_auto_data_task(void);



#endif


