#ifndef __BSP_RS485_H
#define __BSP_RS485_H

#include "bsp_sys.h"

// USART1 (TX:PA2) (RX:PA3) (DIR:PE8 )
#define BSP_RS485_RCU             RCU_USART1
#define BSP_RS485_GPIO_RCU        RCU_GPIOD
#define BSP_RS485_USART           USART1
#define BSP_RS485_IRQn            USART1_IRQn

#define BSP_RS485_TX_PIN          GPIO_PIN_5
#define BSP_RS485_RX_PIN          GPIO_PIN_6
#define BSP_RS485_GPIO_PORT       GPIOD

#define BSP_RS485_DIR_RCU         RCU_GPIOE
#define BSP_RS485_DIR_PORT        GPIOE
#define BSP_RS485_DIR_PIN         GPIO_PIN_8

#define BSP_RS485_DIR_TX()        gpio_bit_set(BSP_RS485_DIR_PORT, BSP_RS485_DIR_PIN)
#define BSP_RS485_DIR_RX()        gpio_bit_reset(BSP_RS485_DIR_PORT, BSP_RS485_DIR_PIN)

//#define BSP_RS485_BAUDRATE        19200U
extern uint32_t BSP_RS485_BAUDRATE;

// 缓冲区大小可以在这里修改
#define BSP_RS485_RX_BUF_SIZE     	1024
#define BSP_RS485_TX_BUF_SIZE 		1024

/* 串口帧格式。Modbus RTU 规范的默认帧格式其实是 8-E-1（偶校验），初赛要求 None，
 * 决赛现场若指定偶/奇校验，只改下面这两个数字即可，不用动 .c。
 * 注意 GD32 带校验位时字长要设成 9 位（8 数据位 + 1 校验位），所以字长由校验方式推导，
 * 不要单独去改 BSP_RS485_WORDLENGTH。
 *
 * 这里用普通整数做选择而不是直接比较 USART_PM_* ：那几个宏展开后含 (uint32_t) 强制
 * 转换（CTL0_PM(x) -> BITS(9,10) & ((uint32_t)(x) << 9)），而预处理器的 #if 表达式里
 * 不允许出现强制转换，armcc 会报 "function call is not allowed in a constant expression"。 */
#define BSP_RS485_PARITY_SEL      0   /* 0 = 无校验 8-N-1, 1 = 偶校验 8-E-1, 2 = 奇校验 8-O-1 */
#define BSP_RS485_STOPBIT_SEL     1   /* 1 = 1 个停止位, 2 = 2 个停止位 */

#if   (BSP_RS485_PARITY_SEL == 1)
  #define BSP_RS485_PARITY        USART_PM_EVEN
  #define BSP_RS485_WORDLENGTH    USART_WL_9BIT
#elif (BSP_RS485_PARITY_SEL == 2)
  #define BSP_RS485_PARITY        USART_PM_ODD
  #define BSP_RS485_WORDLENGTH    USART_WL_9BIT
#else
  #define BSP_RS485_PARITY        USART_PM_NONE
  #define BSP_RS485_WORDLENGTH    USART_WL_8BIT
#endif

#if   (BSP_RS485_STOPBIT_SEL == 2)
  #define BSP_RS485_STOPBIT       USART_STB_2BIT
#else
  #define BSP_RS485_STOPBIT       USART_STB_1BIT
#endif

extern volatile u8 bsp_rs485_send_busy;   /* 1 = 正在发送，等它归 0 表示整帧已发完 */

extern volatile u8 g_rs485_rx_flag; 
extern u8           rx_real_buffer[];    
extern volatile u16 rx_real_len;         

void bsp_rs485_init(void);
void bsp_rs485_send_data(const u8 *data, u16 length);
u16  bsp_rs485_receive_data(u8 *buffer, u16 max_length);
void bsp_rs485_process_echo(void);
void bsp_rs485_set_baudrate(uint32_t new_baudrate);

/* 阻塞等待当前帧真正发完（DE 已切回接收、RBNE 已重开、busy 已清零）。
 * 返回 1 = 发完，0 = 超时。谁要在发送后立刻停时钟/改配置，必须先调它，见 .c 里的说明 */
u8   bsp_rs485_wait_tx_done(u32 timeout_ms);
/* 强制中止在途发送并把收发状态机拉回"空闲可接收"。只在 wait_tx_done 超时后收尾用 */
void bsp_rs485_tx_abort(void);
#endif
