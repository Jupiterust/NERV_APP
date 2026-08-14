#include "bsp_led.h"

void bsp_led_init(void){
	// 使能GPIO时钟
    rcu_periph_clock_enable(BSP_LED_PORT_RCU);   
	// 推挽输出，速度50MHz
    gpio_mode_set(BSP_LED_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE,
         BSP_LED_PIN1 | BSP_LED_PIN2 | BSP_LED_PIN3 | BSP_LED_PIN4 | BSP_LED_PIN5 | BSP_LED_PIN6);   
    gpio_output_options_set(BSP_LED_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ,
         BSP_LED_PIN1 | BSP_LED_PIN2 | BSP_LED_PIN3 | BSP_LED_PIN4 | BSP_LED_PIN5 | BSP_LED_PIN6); 
}

// 快速测试现象
void bsp_led_examine(void){
	BSP_LED1_ON();
	delay_1ms(100);
	BSP_LED2_ON();
	delay_1ms(100);
	BSP_LED3_ON();
	delay_1ms(100);
	BSP_LED4_ON();
	delay_1ms(100);
	BSP_LED5_ON();
	delay_1ms(100);
	BSP_LED6_ON();
	delay_1ms(100);
}

void bsp_led_examine_toggle(void){
	BSP_LED1_TOGGLE();
	BSP_LED2_TOGGLE();
	BSP_LED3_TOGGLE();
	BSP_LED4_TOGGLE();
	BSP_LED5_TOGGLE();
	BSP_LED6_TOGGLE();
	delay_1ms(100);
}