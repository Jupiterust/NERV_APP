#ifndef __BSP_ADC_H
#define __BSP_ADC_H

#include "bsp_sys.h"

#define ADC_DMA_BUF_SIZE        200     // 2通道 x 100次采样
#define ADC_CHANNEL_NUM         2       // 通道数量
#define ADC_SAMPLE_COUNT        100     // 每通道采样次数

extern uint16_t adc_dma_buffer[ADC_DMA_BUF_SIZE];
extern float ch0_ratio;
extern float ch1_ratio;
extern float ch0_threshold;
extern float ch1_threshold;
	


//BSP ADC all define 
#define BSP_ADC                 ADC0

// 模拟输入 PC0,PC1 以及它的时钟和端口 宏定义
#define BSP_ADC_GPIO_RCU        RCU_GPIOC
#define BSP_ADC_PORT            GPIOC
#define BSP_ADC_GPIO_PIN0       GPIO_PIN_0
#define BSP_ADC_CHANNEL0        ADC_CHANNEL_10


#define BSP_ADC_GPIO_PIN1       GPIO_PIN_1
#define BSP_ADC_CHANNEL1        ADC_CHANNEL_11

// ADC
#define BSP_ADC_RCU             RCU_ADC0

// DMA 
#define BSP_ADC_DMA_RCU         RCU_DMA1
#define BSP_ADC_DMA             DMA1

// all function
void bsp_adc_port_init(void);
void bsp_adc_init(void);
void bsp_adc_dma_init(void);
uint16_t bsp_adc_get_channel0(void);   // 获取CH10(PC0)均值
uint16_t bsp_adc_get_channel1(void);   // 获取CH11(PC1)均值
uint16_t bsp_adc_read_channel(uint8_t channel);


#endif