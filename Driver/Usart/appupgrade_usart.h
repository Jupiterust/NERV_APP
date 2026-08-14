
#ifndef __APPUPGRADE_USART_H__
#define __APPUPGRADE_USART_H__
#include "Headfile.h"


// define 
#define APPUPGRADE_USART_PORT 		GPIOD
#define APPUPGRADE_USART	 		USART2
#define APPUPGRADE_USART_TX_Pin 	GPIO_PIN_8
#define APPUPGRADE_USART_RX_Pin 	GPIO_PIN_9
#define APPUPGRADE_USART_RCU 		RCU_USART2
#define APPUPGRADE_USART_PIN_RCU 	RCU_GPIOD
#define APPUPGRADE_USART_IRQ 		USART2_IRQn
#define APPUPGRADE_USART_IRQHandler USART2_IRQHandler


void appupgrade_usart_init(void); 

void appupgrade_usart_recv_buf(void);

extern uint8_t app_upgrade_usart_tmp_buf[40 * 1024];
extern uint32_t app_upgrade_usart_tmp_buf_len;
extern uint8_t app_upgrade_usart_recv_flag;

#endif 

/****************************End*****************************/
