#ifndef __BSP_DEBUG_H
#define __BSP_DEBUG_H

#include "bsp_sys.h"


#define BSP_DUBUG_GPIO_RCU      RCU_GPIOA
#define BSP_DEBUG_UART_RCU      RCU_USART0
#define BSP_DEBUG_UART          USART0

#define BSP_DUBUG_GPIO_PORT     GPIOA 
#define BSP_DEBUG_GPIO_TX       GPIO_PIN_9
#define BSP_DEBUG_GPIO_RX       GPIO_PIN_10

#define BSP_DEBUG_UART_IRQn         USART0_IRQn
#define BSP_DEBUG_UART_IRQHandler   USART0_IRQHandler

#define BSP_DEBUG_RX_BUFFER_LENGTH 256

// func
void bsp_debug_init(void);
void bsp_debug_senddata(uint16_t *buf,uint16_t len);
void bsp_debug_irq_enable(void);
// variables
extern volatile uint8_t bsp_debug_recv_data_buffer[BSP_DEBUG_RX_BUFFER_LENGTH];
extern volatile uint32_t bsp_debug_usart_buf_index;
extern volatile bool bbsp_debug_receive_finished_flag;
#endif


