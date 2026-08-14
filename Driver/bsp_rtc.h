#ifndef __BSP_RTC_H
#define __BSP_RTC_H

#define bsp_rtc_RCU         RCU_PMU 

#include "bsp_sys.h"
/*
rtc_parameter_struct   rtc_initpara;
rtc_alarm_struct  rtc_alarm;
__IO uint32_t prescaler_a = 0, prescaler_s = 0;
uint32_t RTCSRC_FLAG = 0;

*/
#define BSP_PRESCALER_A     0x7F //127
#define BSP_PRESCALER_S     0xFF //255
#define BSP_RTC_RCU_PMU     RCU_PMU //备份电源
#define BSP_RTC_RCU         RCU_RTC //
#define BSP_RTC_RCU         RCU_LXTAL

#define BSP_RTC_AM          RTC_AM 


typedef struct{
	uint8_t year;
	uint8_t month; 
	uint8_t day_of_week;   
	uint8_t date;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;	
	uint32_t display_format;                                                    
}bsp_rtc_time_set_para_struct;


extern volatile rtc_parameter_struct bsp_rtc_init_para;
extern char *bsp_rtc_weeks[8];
void RTC_Init(void);	// RTC初始化
void rtc_setup(void);	// RTC时钟设置
void rtc_show_time(void);	// RTC时间
void rtc_show_alarm(void);	// RTC闹钟
uint8_t usart_input_threshold(uint32_t value);  // 用作输入值有效校验
void rtc_pre_config(void);


void bsp_rtc_show_time(void);
void bsp_rtc_init(void);
uint8_t bsp_rtc_set_time(bsp_rtc_time_set_para_struct* bsp_rtc_set_para);
uint8_t bsp_rtc_set_unix_timestamp(uint32_t timestamp, int8_t timezone);
uint32_t bsp_rtc_get_unix_timestamp(void);
void bsp_rtc_set_alarm_10s(void);

/* 闹钟中断进过一次就置 1，由 bsp_rtc_set_alarm_10s() 清零。排查"睡不醒"时看它 */
extern volatile uint8_t g_rtc_alarm_fired;
#endif