#ifndef __BSP_KEY_H_
#define __BSP_KEY_H_

#include "bsp_sys.h" 

#define BSP_KEY1_2_GPIO_CLK             RCU_GPIOB
#define BSP_KEY1_2_GPIO_PORT            GPIOB
#define BSP_KEY1_GPIO_PIN               GPIO_PIN_0
#define BSP_KEY2_GPIO_PIN               GPIO_PIN_1

#define BSP_KEY3_4_5_6_GPIO_CLK         RCU_GPIOE
#define BSP_KEY3_4_5_6_GPIO_PORT        GPIOE
#define BSP_KEY3_GPIO_PIN               GPIO_PIN_3
#define BSP_KEY4_GPIO_PIN               GPIO_PIN_4
#define BSP_KEY5_GPIO_PIN               GPIO_PIN_5
#define BSP_KEY6_GPIO_PIN               GPIO_PIN_6

#define BSP_KEY_TIM_RCU                 RCU_TIMER5
#define BSP_KEY_TIM                     TIMER5
#define BSP_KEY_TIM_IRQn                TIMER5_DAC_IRQn
#define BSP_KEY_TIM_IRQHandler          TIMER5_DAC_IRQHandler // 根据芯片实际中断向量表修改

#define BSP_KEYS_LENGTH                 6

/* 时间阈值配置 (假设 bsp_key_scan 每 10ms 调用一次) */
#define BSP_KEY_DEBOUNCE_TICKS          2    // 消抖时间 20ms
#define BSP_KEY_SHORT_TICKS             5    // 短按判定最少时间 50ms
#define BSP_KEY_LONG_TICKS              150  // 长按判定时间 1.5s
#define BSP_KEY_DOUBLE_TICKS            45   // 双击间隔时间最大 300ms

// 按键输出事件枚举
typedef enum {
    KEY_EVENT_NONE = 0,     // 无事件
	
    KEY_EVENT_SHORT,        // 短按
    KEY_EVENT_LONG,         // 长按
    KEY_EVENT_DOUBLE        // 双击
} KeyEvent_t;

// 按键内部状态机枚举
typedef enum {
    KS_IDLE = 0,
    KS_DEBOUNCE,
    KS_PRESS,
    KS_WAIT_RELEASE
} KeyState_t;

// 按键控制结构体
typedef struct {
    KeyState_t state;           // 当前状态机状态
    uint32_t count;             // 按下计数器
    uint32_t double_click_cnt;  // 双击等待计数器
    KeyEvent_t event;           // 触发的事件
} BSP_KEY_STRUCT;

void bsp_key_init(void);
void bsp_key_scan(void);
KeyEvent_t bsp_key_get_event(uint8_t key_idx);
void bsp_key_test(void);

void bsp_key_pins_exit_init(void);
extern volatile uint16_t cnt[6];
#endif /* __BSP_KEY_H_ */