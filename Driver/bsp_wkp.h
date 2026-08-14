#ifndef __BSP_WKP_H_
#define __BSP_WKP_H_
#include "bsp_sys.h"


void bsp_sleep_mode(void);


/* 睡眠入口。睡多深由 bsp_wkp.c 顶部的 SLEEP_DEPTH 决定（0=只装闹钟不睡/1=普通睡眠/2=深睡），
 * 进睡眠前的 485 收尾、RTC 自检，以及唤醒后的时钟/485 恢复都在里面做完 */
void bsp_deepsleep_config(void);

/* SLEEP_DEPTH==0 排障档用：主循环里调，闹钟响过就回 "RTC ALARM FIRED"。其它档位是空函数 */
void bsp_wkp_alarm_test_poll(void);



void bsp_standby_mode(void);

void Wake_Up_Key_Init(void);
#endif


