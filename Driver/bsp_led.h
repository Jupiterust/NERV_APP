#ifndef __BSP_LED_H
#define __BSP_LED_H

#include "bsp_sys.h"
// LED 引脚时钟和端口
#define BSP_LED_PORT_RCU RCU_GPIOD
#define BSP_LED_PORT GPIOD
#define BSP_LED_PIN1 GPIO_PIN_10
#define BSP_LED_PIN2 GPIO_PIN_11
#define BSP_LED_PIN3 GPIO_PIN_12
#define BSP_LED_PIN4 GPIO_PIN_13
#define BSP_LED_PIN5 GPIO_PIN_14
#define BSP_LED_PIN6 GPIO_PIN_15
// led on off function
#define BSP_LED1_ON()   gpio_bit_set(BSP_LED_PORT,   BSP_LED_PIN1)
#define BSP_LED1_OFF()  gpio_bit_reset(BSP_LED_PORT, BSP_LED_PIN1)
#define BSP_LED2_ON()   gpio_bit_set(BSP_LED_PORT,   BSP_LED_PIN2)
#define BSP_LED2_OFF()  gpio_bit_reset(BSP_LED_PORT, BSP_LED_PIN2)
#define BSP_LED3_ON()   gpio_bit_set(BSP_LED_PORT,   BSP_LED_PIN3)
#define BSP_LED3_OFF()  gpio_bit_reset(BSP_LED_PORT, BSP_LED_PIN3)
#define BSP_LED4_ON()   gpio_bit_set(BSP_LED_PORT,   BSP_LED_PIN4)
#define BSP_LED4_OFF()  gpio_bit_reset(BSP_LED_PORT, BSP_LED_PIN4)
#define BSP_LED5_ON()   gpio_bit_set(BSP_LED_PORT,   BSP_LED_PIN5)
#define BSP_LED5_OFF()  gpio_bit_reset(BSP_LED_PORT, BSP_LED_PIN5)
#define BSP_LED6_ON()   gpio_bit_set(BSP_LED_PORT,   BSP_LED_PIN6)
#define BSP_LED6_OFF()  gpio_bit_reset(BSP_LED_PORT, BSP_LED_PIN6)

#define BSP_LED1_TOGGLE()  gpio_bit_write(BSP_LED_PORT, BSP_LED_PIN1, \
                            (bit_status)(1 - gpio_input_bit_get(BSP_LED_PORT, BSP_LED_PIN1)))
#define BSP_LED2_TOGGLE()  gpio_bit_write(BSP_LED_PORT, BSP_LED_PIN2, \
                            (bit_status)(1 - gpio_input_bit_get(BSP_LED_PORT, BSP_LED_PIN2)))
#define BSP_LED3_TOGGLE()  gpio_bit_write(BSP_LED_PORT, BSP_LED_PIN3, \
                            (bit_status)(1 - gpio_input_bit_get(BSP_LED_PORT, BSP_LED_PIN3)))
#define BSP_LED4_TOGGLE()  gpio_bit_write(BSP_LED_PORT, BSP_LED_PIN4, \
                            (bit_status)(1 - gpio_input_bit_get(BSP_LED_PORT, BSP_LED_PIN4)))   
#define BSP_LED5_TOGGLE()  gpio_bit_write(BSP_LED_PORT, BSP_LED_PIN5, \
                            (bit_status)(1 - gpio_input_bit_get(BSP_LED_PORT, BSP_LED_PIN5)))
#define BSP_LED6_TOGGLE()  gpio_bit_write(BSP_LED_PORT, BSP_LED_PIN6, \
                            (bit_status)(1 - gpio_input_bit_get(BSP_LED_PORT, BSP_LED_PIN6)))

void bsp_led_init(void);

void bsp_led_examine(void);

void bsp_led_examine_toggle(void);

#endif