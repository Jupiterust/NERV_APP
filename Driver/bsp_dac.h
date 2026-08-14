#ifndef __BSP_DAC_H
#define __BSP_DAC_H

#include "bsp_sys.h"

// BSP DAC 引脚时钟 端口 宏定义 可直接根据手册修改 
// 无需修改.c
#define BSP_DAC_GPIO_RCU        RCU_GPIOA
#define BSP_DAC_RCU             RCU_DAC
#define BSP_DAC					DAC0
#define BSP_DAC_GPIO_PORT       GPIOA
#define BSP_DAC_GPIO_PIN        GPIO_PIN_4
// 这里设置电压
#define BSP_DAC_VREF_MV         3300U

void bsp_dac_init(void);
void bsp_dac_set_voltage(uint16_t voltage_mv);
void bsp_dac_set_raw(uint16_t raw_val);

#endif 