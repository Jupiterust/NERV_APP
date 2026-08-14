#ifndef __BSP_GENERAL_TIM_H_
#define __BSP_GENERAL_TIM_H_
#include "bsp_sys.h"

void bsp_tim7_init(void);
void bsp_tim9_init(void);
void bsp_tim7_set_timeout(uint16_t seconds);

extern volatile uint8_t g_report_flag;



#endif



