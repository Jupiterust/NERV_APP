#include "bsp_bootloader.h"

void bsp_bootloader_key_init(void){
    rcu_periph_clock_enable(BSP_BOOTLOADER_KEY_GPIO_RCU);   // 使能GPIO时钟
    gpio_mode_set(BSP_BOOTLOADER_KEY_GPIO_PORT, 
        GPIO_MODE_INPUT, 
        GPIO_PUPD_PULLUP,
        BSP_BOOTLOADER_KEY_PIN); // 配置为输入模式，启用内部上拉
}

u8 bsp_bootloader_key_trigger(void){
    // 边沿检测：按键按下并释放时返回1
    static u8 last_state = 1; // 初始状态为高电平（未按下）
    u8 current_state = gpio_input_bit_get(BSP_BOOTLOADER_KEY_GPIO_PORT, BSP_BOOTLOADER_KEY_PIN);
    
    // 从高到低的下降沿
    if(last_state == 1 && current_state == RESET) {
        delay_1ms(10); // 消抖
        if(gpio_input_bit_get(BSP_BOOTLOADER_KEY_GPIO_PORT, BSP_BOOTLOADER_KEY_PIN) == RESET) {
            last_state = RESET;
            return 0; // 按下状态，等待释放
        }
    }
    
    // 从低到高的上升沿（释放）
    if(last_state == RESET && current_state == 1) {
        last_state = 1;
        return 1; // 触发一次
    }
    
    last_state = current_state;
    return 0;
}
typedef void (*app_entry_t)(void);

//void bsp_bootloader_jump_application(void)
//{
//    uint32_t *app_base  = (uint32_t *)BSP_BOOT_APP_START_ADDR;
//    uint32_t  app_sp    = app_base[0];
//    uint32_t  app_entry = app_base[1];
//    printf("app_sp = 0x%08X\r\n", app_sp);
//    printf("app_entry = 0x%08X\r\n", app_entry);
//    // 1. 检查栈指针是否在 SRAM 范围内 (0x20000000 ~ 0x20030000 + 0x10010000)
//    if ((app_sp < 0x20000000) || (app_sp >= 0x20040000)) {
//        if ((app_sp < 0x10000000) || (app_sp >= 0x10020000)) {
//            return;
//        }
//    }

//    // 2. 检查入口地址是否在 App Flash 范围内
//    if (app_entry < BSP_BOOT_APP_START_ADDR ||
//        app_entry > BSP_BOOT_APP_START_ADDR + 0x70000) {
//        return;
//    }

//    // 3. 禁用全局中断
//    __disable_irq();

//    // 4. 清除所有外设中断
//    for (int i = 0; i < 8; i++) {
//        NVIC->ICER[i] = 0xFFFFFFFF;
//        NVIC->ICPR[i] = 0xFFFFFFFF;
//    }

//    // 5. 关闭 SysTick
//    SysTick->CTRL = 0;
//    SysTick->LOAD = 0;
//    SysTick->VAL  = 0;

//    // 6. 重定向向量表到 App
//    SCB->VTOR = (uint32_t)BSP_BOOT_APP_START_ADDR;
//    __DSB();
//    __ISB();

//    // 7. 设置栈指针并跳转到 App 的 Reset_Handler
//    __set_MSP(app_sp);

//    ((app_entry_t)app_entry)();
//}

//void bsp_bootloader_jump_application(void)
//{
//    uint32_t *app_base  = (uint32_t *)BSP_BOOT_APP_START_ADDR;
//    uint32_t  app_sp    = app_base[0];
//    uint32_t  app_entry = app_base[1];
//    printf("app_sp = 0x%08X\r\n", app_sp);
//    printf("app_entry = 0x%08X\r\n", app_entry);
//    // 1. 检查栈指针是否在 SRAM 范围内 (0x20000000 ~ 0x20030000 + 0x10010000)
//    if ((app_sp < 0x20000000) || (app_sp >= 0x20040000)) {
//        if ((app_sp < 0x10000000) || (app_sp >= 0x10020000)) {
//            return;
//        }
//    }

//    __disable_irq();
//    for (int i = 0; i < 8; i++) {
//        NVIC->ICER[i] = 0xFFFFFFFF;
//        NVIC->ICPR[i] = 0xFFFFFFFF;
//    }
//    SysTick->CTRL = 0;
//    SysTick->LOAD = 0;
//    SysTick->VAL  = 0;

//    // ↓ 跳转前把时钟切回内部RC，关掉PLL和HXTAL
//    RCU_CFG0 &= ~RCU_CFG0_SCS;          // 切换到IRC16M
//    while((RCU_CFG0 & RCU_SCSS_IRC16M)); // 等待切换完成
//    RCU_CTL &= ~RCU_CTL_PLLEN;          // 关PLL
//    RCU_CTL &= ~RCU_CTL_HXTALEN;        // 关HXTAL

//    SCB->VTOR = BSP_BOOT_APP_START_ADDR;
//    __set_MSP(app_sp);
//    ((app_entry_t)app_entry)();
//}

void bsp_bootloader_jump_application(void)
{
    uint32_t *app_base  = (uint32_t *)BSP_BOOT_APP_START_ADDR;
    uint32_t  app_sp    = app_base[0];
    uint32_t  app_entry = app_base[1];

    if ((app_sp & 0x2FFE0000) != 0x20000000) return;

    __disable_irq();
    for (int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    SCB->VTOR = BSP_BOOT_APP_START_ADDR;
    __set_MSP(app_sp);
    ((app_entry_t)app_entry)();
}
