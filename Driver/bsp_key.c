#include "bsp_key.h"
#include <stdio.h>

static BSP_KEY_STRUCT bsp_user_keys[BSP_KEYS_LENGTH] = {0}; // 初始化按键状态数组

// 硬件引脚配置关系表
static const uint32_t bsp_user_keys_port[BSP_KEYS_LENGTH][2] = {
    {BSP_KEY1_2_GPIO_PORT, BSP_KEY1_GPIO_PIN},
    {BSP_KEY1_2_GPIO_PORT, BSP_KEY2_GPIO_PIN},
    {BSP_KEY3_4_5_6_GPIO_PORT, BSP_KEY3_GPIO_PIN},
    {BSP_KEY3_4_5_6_GPIO_PORT, BSP_KEY4_GPIO_PIN},
    {BSP_KEY3_4_5_6_GPIO_PORT, BSP_KEY5_GPIO_PIN},
    {BSP_KEY3_4_5_6_GPIO_PORT, BSP_KEY6_GPIO_PIN}
};

static void bsp_key_pins_init(void)
{
    rcu_periph_clock_enable(BSP_KEY1_2_GPIO_CLK);
    rcu_periph_clock_enable(BSP_KEY3_4_5_6_GPIO_CLK);
    
    gpio_mode_set(BSP_KEY1_2_GPIO_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP,
                  BSP_KEY1_GPIO_PIN | BSP_KEY2_GPIO_PIN);
                  
    gpio_mode_set(BSP_KEY3_4_5_6_GPIO_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP,
                  BSP_KEY3_GPIO_PIN | BSP_KEY4_GPIO_PIN | BSP_KEY5_GPIO_PIN | BSP_KEY6_GPIO_PIN);
}
// tim5 中断周期为 10ms
static void bsp_key_tim_init(void)
{
    rcu_periph_clock_enable(BSP_KEY_TIM_RCU);
    
    // 注意：请根据系统主频实际计算预分频和周期，确保
    rcu_timer_clock_prescaler_config(RCU_TIMER_PSC_MUL4);
    
    timer_parameter_struct timer_initpara;
	// 
    timer_initpara.prescaler = 24 - 1;          
    timer_initpara.period = 100000 - 1;         
    timer_initpara.alignedmode = TIMER_COUNTER_EDGE;
    timer_initpara.counterdirection = TIMER_COUNTER_UP;
    timer_initpara.clockdivision = TIMER_CKDIV_DIV1;
    timer_init(BSP_KEY_TIM, &timer_initpara);
    
    nvic_irq_enable(BSP_KEY_TIM_IRQn, 0, 0);
    timer_flag_clear(BSP_KEY_TIM, TIMER_FLAG_UP);
    timer_interrupt_enable(BSP_KEY_TIM, TIMER_INT_UP);
    timer_enable(BSP_KEY_TIM);
}

void bsp_key_init(void)
{
    bsp_key_pins_init();
    bsp_key_tim_init();
}

// 双击功能未完成
void bsp_key_scan(void)
{
    for (uint8_t i = 0; i < BSP_KEYS_LENGTH; i++) {
        // 低电平表示按下
        uint8_t is_pressed = (gpio_input_bit_get(bsp_user_keys_port[i][0], bsp_user_keys_port[i][1]) == RESET);

        // 双击等待计时器递减
        if (bsp_user_keys[i].double_click_cnt > 0) {
            bsp_user_keys[i].double_click_cnt--;
            // 如果计时器减到0，说明双击超时，之前保留的单击动作在此刻触发
            if (bsp_user_keys[i].double_click_cnt == 0) {
                bsp_user_keys[i].event = KEY_EVENT_SHORT;
            }
        }

        switch (bsp_user_keys[i].state) {
            case KS_IDLE:
                if (is_pressed) {
                    bsp_user_keys[i].state = KS_DEBOUNCE;
                }
                break;

            case KS_DEBOUNCE:
                if (is_pressed) {
                    bsp_user_keys[i].state = KS_PRESS;
                    bsp_user_keys[i].count = 0;
                } else {
                    bsp_user_keys[i].state = KS_IDLE;
                }
                break;

            case KS_PRESS:
                if (is_pressed) {
                    bsp_user_keys[i].count++;
                    // 限制计数器累加，防止溢出（10ms一次，累加到比长按阈值稍大即可停止）
                    if (bsp_user_keys[i].count > BSP_KEY_LONG_TICKS + 10) {
                        bsp_user_keys[i].count = BSP_KEY_LONG_TICKS + 1;
                    }
                } else {
                    // 按键释放（松手），此时才进行时间长度判定
                    if (bsp_user_keys[i].count >= BSP_KEY_LONG_TICKS) {
                        // 按住时间达到长按阈值，输出长按事件
                        bsp_user_keys[i].event = KEY_EVENT_LONG;
                        bsp_user_keys[i].double_click_cnt = 0; // 清除可能存在的双击判定
                    } 
                    else if (bsp_user_keys[i].count >= BSP_KEY_SHORT_TICKS) {
                        // 按住时间未达长按，但达到短按判定
                        if (bsp_user_keys[i].double_click_cnt > 0) {
                            // 在双击时间窗口内再次按下并释放，判定为双击
                            bsp_user_keys[i].event = KEY_EVENT_DOUBLE;
                            bsp_user_keys[i].double_click_cnt = 0; 
                        } else {
                            // 启动双击判定倒计时
                            bsp_user_keys[i].double_click_cnt = BSP_KEY_DOUBLE_TICKS;
                        }
                    }
                    bsp_user_keys[i].state = KS_IDLE;
                }
                break;

            default:
                bsp_user_keys[i].state = KS_IDLE;
                break;
        }
    }
}
KeyEvent_t bsp_key_get_event(uint8_t key_idx)
{
    if (key_idx < 1 || key_idx > BSP_KEYS_LENGTH)
        return KEY_EVENT_NONE; 
    
    KeyEvent_t temp_event = bsp_user_keys[key_idx - 1].event;
    if (temp_event != KEY_EVENT_NONE) {
        bsp_user_keys[key_idx - 1].event = KEY_EVENT_NONE; // 清空事件标志
    }
    return temp_event;
}

void bsp_key_test(void)
{
    for (uint8_t i = 0; i < BSP_KEYS_LENGTH; i++) {
        KeyEvent_t event = bsp_key_get_event(i + 1);
        switch (event) {
            case KEY_EVENT_NONE:
                break;
            case KEY_EVENT_SHORT:
                printf("Key %d Short Pressed\n", i + 1);
                break;
            case KEY_EVENT_LONG:
                printf("Key %d Long Pressed\n", i + 1);
                break;
            case KEY_EVENT_DOUBLE:
                printf("Key %d Double Clicked\n", i + 1);
                break;
        }
    }
}

/**
 * @brief 定时器中断服务函数
 */
void BSP_KEY_TIM_IRQHandler(void)
{
    if (timer_interrupt_flag_get(BSP_KEY_TIM, TIMER_FLAG_UP) != RESET) {
        timer_interrupt_flag_clear(BSP_KEY_TIM, TIMER_FLAG_UP);
        bsp_key_scan();
    }
}

// 下面是外部中断触发
static const exti_line_enum bsp_key_exti_lines[BSP_KEYS_LENGTH] = {
    EXTI_0, EXTI_1, EXTI_3, EXTI_4, EXTI_5, EXTI_6
};

void bsp_key_exti_enable(void)
{
    // 清除可能在按键释放时产生的抖动悬挂标志，防止唤醒后误触发
    exti_interrupt_flag_clear(EXTI_0);
    exti_interrupt_flag_clear(EXTI_1);
    exti_interrupt_flag_clear(EXTI_3);
    exti_interrupt_flag_clear(EXTI_4);
    exti_interrupt_flag_clear(EXTI_5);
    exti_interrupt_flag_clear(EXTI_6);
	
    exti_interrupt_enable(EXTI_0);
    exti_interrupt_enable(EXTI_1);
    exti_interrupt_enable(EXTI_3);
    exti_interrupt_enable(EXTI_4);
    exti_interrupt_enable(EXTI_5);
    exti_interrupt_enable(EXTI_6);
}

void bsp_key_exti_disable(void)
{
    exti_interrupt_disable(EXTI_0);
    exti_interrupt_disable(EXTI_1);
    exti_interrupt_disable(EXTI_3);
    exti_interrupt_disable(EXTI_4);
    exti_interrupt_disable(EXTI_5);
    exti_interrupt_disable(EXTI_6);
}

void bsp_key_pins_exit_init(void)
{
    rcu_periph_clock_enable(BSP_KEY1_2_GPIO_CLK);
    rcu_periph_clock_enable(BSP_KEY3_4_5_6_GPIO_CLK);
    rcu_periph_clock_enable(RCU_SYSCFG);

    //上拉
    gpio_mode_set(BSP_KEY1_2_GPIO_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP,
                  BSP_KEY1_GPIO_PIN | BSP_KEY2_GPIO_PIN);
                  
    gpio_mode_set(BSP_KEY3_4_5_6_GPIO_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP,
                  BSP_KEY3_GPIO_PIN | BSP_KEY4_GPIO_PIN | BSP_KEY5_GPIO_PIN | BSP_KEY6_GPIO_PIN);

    // 配置 NVIC 
  
    nvic_irq_enable(EXTI0_IRQn, 2, 0);
    nvic_irq_enable(EXTI1_IRQn, 2, 0);
    nvic_irq_enable(EXTI3_IRQn, 2, 0);
    nvic_irq_enable(EXTI4_IRQn, 2, 0);
    nvic_irq_enable(EXTI5_9_IRQn, 2, 0); // 共享中断向量

    syscfg_exti_line_config(EXTI_SOURCE_GPIOB, EXTI_SOURCE_PIN0); // Key 1
    syscfg_exti_line_config(EXTI_SOURCE_GPIOB, EXTI_SOURCE_PIN1); // Key 2
    syscfg_exti_line_config(EXTI_SOURCE_GPIOE, EXTI_SOURCE_PIN3); // Key 3
    syscfg_exti_line_config(EXTI_SOURCE_GPIOE, EXTI_SOURCE_PIN4); // Key 4
    syscfg_exti_line_config(EXTI_SOURCE_GPIOE, EXTI_SOURCE_PIN5); // Key 5
    syscfg_exti_line_config(EXTI_SOURCE_GPIOE, EXTI_SOURCE_PIN6); // Key 6

    
    //下降沿触发 
    for (uint8_t i = 0; i < BSP_KEYS_LENGTH; i++) {
        exti_init(bsp_key_exti_lines[i], EXTI_INTERRUPT, EXTI_TRIG_FALLING);
        //6. 清除之前的标志位 
        exti_interrupt_flag_clear(bsp_key_exti_lines[i]);
    }
    
    // 初始化时先使能中断监听
    bsp_key_exti_enable();
}

volatile uint16_t cnt[6] = {0} ;

void EXTI0_IRQHandler(void)
{

    if (exti_interrupt_flag_get(EXTI_0) != RESET) {
        exti_interrupt_flag_clear(EXTI_0);
		cnt[0]++;
    }
}



void EXTI1_IRQHandler(void)
{
    if (exti_interrupt_flag_get(EXTI_1) != RESET) {
        exti_interrupt_flag_clear(EXTI_1);
		cnt[1]++;
    }
}

void EXTI3_IRQHandler(void)
{
    if (exti_interrupt_flag_get(EXTI_3) != RESET) {
        exti_interrupt_flag_clear(EXTI_3);
		cnt[2]++;
    }
}

void EXTI4_IRQHandler(void)
{
    if (exti_interrupt_flag_get(EXTI_4) != RESET) {
        exti_interrupt_flag_clear(EXTI_4);
		cnt[3]++;
    }
}

void EXTI5_9_IRQHandler(void)
{
    // Pin 5 和 Pin 6 共用此中断
    if (exti_interrupt_flag_get(EXTI_5) != RESET) {
        exti_interrupt_flag_clear(EXTI_5);
		cnt[4]++;
    }
    if (exti_interrupt_flag_get(EXTI_6) != RESET) {
        exti_interrupt_flag_clear(EXTI_6);
		cnt[5]++;
    }
}


