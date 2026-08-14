#include "bsp_wkp.h"


void bsp_sleep_mode(void)
{
    rcu_periph_clock_enable(RCU_PMU);

    /*直接进入睡眠 */
    pmu_to_sleepmode(WFI_CMD);
    
    /*唤醒后，程序会直接从这里继续执行 */
}






/* ===== 睡眠可调参数 =====
 *
 * SLEEP_DEPTH 决定睡多深。醒不来时按 0 → 1 → 2 逐档试，能分清问题在哪一层：
 *
 *   0 = 只装闹钟不睡，排障档。装好 10s 闹钟立刻返回，主循环照常跑，所以发 0x03AA
 *       之后会立刻收到 ACK 和 "instrument wakeup"。要看的是约 10s 后有没有第三行
 *       "RTC ALARM FIRED"（RTC_Alarm_IRQHandler 置 g_rtc_alarm_fired，主循环的
 *       bsp_wkp_alarm_test_poll() 发出）：
 *         收到  → RTC + 闹钟 + EXTI17 + NVIC 整条链通，问题在睡眠的进入/退出，试 1 档
 *         收不到 → RTC 侧有问题（日历没走 / LXTAL 没起振 / 闹钟没配上），先修 RTC
 *
 *   1 = 普通睡眠。SLEEPDEEP=0，只停 CPU 时钟，外设时钟全在，任何中断都能唤醒，
 *       唤醒后不用重配时钟，不存在醒不来的情况。电流有明显下降，够满足评测要求的
 *       "可观察到电流变化"。
 *
 *   2 = 深度睡眠。电流最低，但唤醒完全依赖 RTC 闹钟经 EXTI17 触发，RTC 不对就是死机。
 *       确认 0 档能收到 "RTC ALARM FIRED" 之后再用。
 */
#define SLEEP_DEPTH            1

#define SLEEP_TX_WAIT_MS       500U  /* 进睡眠前等在途应答帧发完的上限(ms)。
                                      * 9600 波特下最长一帧也就 300ms 出头 */
#define SLEEP_DBG_WAIT_MS      50U   /* 调试串口(USART0) printf 的收尾等待上限(ms) */
#define SLEEP_RTC_PROBE_MS     20U   /* 探测 RTC 亚秒计数器是否在跳的采样间隔(ms) */
#define SLEEP_GUARD_TICKS      15000U/* SLEEP_DEPTH==1 的兜底上限，约等于毫秒。
                                      * 闹钟该 10s 到，15s 还不来就自己醒，别把板子睡死 */

void bsp_deepsleep_config(void)
{
    uint32_t dbg_wait = SLEEP_DBG_WAIT_MS * 1000U; // 当轮询次数用，只为了不死等
    uint32_t ss_probe;

    rcu_periph_clock_enable(RCU_PMU);

    /* ---- 兜底 1：LXTAL 必须起振 ----
     * 唤醒依赖 RTC 闹钟，RTC 走的是 LXTAL(32.768kHz)。晶振没焊或没起振时
     * bsp_rtc_rcu_config() 里 rcu_osci_stab_wait() 的返回值被丢掉，RTC 一格不走，
     * 闹钟永远不来，进了深睡只能按复位。宁可不睡。*/
    if (RESET == rcu_flag_get(RCU_FLAG_LXTALSTB)) {
        printf("sleep aborted: LXTAL not stable, RTC alarm would never fire\r\n");
        return;
    }

    /* ---- 兜底 2：RTC 日历必须在计数 ----
     * LXTAL 稳了不等于日历在走：rtc_init() 失败会把 RTC 留在初始化模式，日历冻结，
     * 秒数到不了闹钟设定值。RTC_SS 亚秒计数器在 factor_syn=255 时按 256Hz 递减，
     * 每约 3.9ms 变一次，隔 20ms 采两次还一样就说明 RTC 停着，不能睡。*/
    ss_probe = RTC_SS;
    delay_1ms(SLEEP_RTC_PROBE_MS);
    if (ss_probe == RTC_SS) {
        printf("sleep aborted: RTC calendar frozen (SS stuck at %u)\r\n", (unsigned)ss_probe);
        return;
    }

    /* ---- 兜底 3：等 485 那一帧真的发完 ----
     * 485 发送是中断驱动的，Protocol_SendFrame() 返回时帧还在往外发，DE 停在发送态、
     * RBNE 关着、bsp_rs485_send_busy 还是 1，这些都要等 TC 中断跑完才复位
     *（见 bsp_rs485.c 中 bsp_rs485_wait_tx_done 上方的说明）。
     * 睡眠会停时钟，带着这个状态睡过去，收发器会一直驱动总线把 485 拉死，
     * 醒来后 bsp_rs485_send_data() 也会卡在开头的 while(bsp_rs485_send_busy)。*/
    if (!bsp_rs485_wait_tx_done(SLEEP_TX_WAIT_MS)) {
        bsp_rs485_tx_abort();   // 超时也要收干净，不能带着发送态进睡眠
    }

    // 等调试串口发完。必须带超时：USART0 没初始化时 TC 恒为 0，裸 while 会死在这
    while (RESET == usart_flag_get(USART0, USART_FLAG_TC) && dbg_wait > 0U) {
        dbg_wait--;
    }

    // 设 10 秒后的闹钟，内部会清零 g_rtc_alarm_fired
    bsp_rtc_set_alarm_10s();

    pmu_flag_clear(PMU_FLAG_WAKEUP);

#if (SLEEP_DEPTH == 0)
    /* 排障档：不睡，回主循环等 bsp_wkp_alarm_test_poll() 报告闹钟响没响 */
    printf("sleep probe: alarm armed, NOT sleeping (SLEEP_DEPTH=0)\r\n");
    return;

#elif (SLEEP_DEPTH == 1)
    /* 普通睡眠：SLEEPDEEP=0，外设时钟不停，唤醒后时钟和串口配置原样保留。
     * 套一层 while 是因为 SysTick / TIM7 等中断都会把 CPU 从 WFI 叫醒，闹钟没响要继续
     * 睡回去，否则睡不满 10s。
     * SysTick 中断故意不关，拿它当兜底超时源：闹钟不来时最多空转 SLEEP_GUARD_TICKS 次
     * 也会退出，不会把板子睡死，这是深睡做不到的。代价是 CPU 每 1ms 醒一次，电流不如
     * 全静默低，但主循环、OLED 刷新、ADC 轮询都停了，电流表上仍有明显下降。*/
    {
        uint32_t guard = SLEEP_GUARD_TICKS;

        while (0U == g_rtc_alarm_fired && guard > 0U) {
            pmu_to_sleepmode(WFI_CMD);  // 任何中断都会回到这里
            guard--;                    // 唤醒源以 1ms 的 SysTick 为主，guard 约等于毫秒
        }

        if (0U == g_rtc_alarm_fired) {
            printf("sleep: woke by guard timeout, RTC alarm never fired!\r\n");
        }
    }

#else
    /* 深度睡眠：LDO 低功耗，WFI 唤醒。低驱动模式(PMU_LOWDRIVER_ENABLE)对退出时序敏感，
     * 不开，省下的电流不值得拿唤醒可靠性换 */
    pmu_to_deepsleepmode(PMU_LDO_LOWPOWER, PMU_LOWDRIVER_DISABLE, WFI_CMD);

    /* MCU 在此停机，等待 10 秒 */

    // 深睡退出时系统时钟回落到 IRC16M，必须重配回 PLL，否则串口波特率全错
    SystemInit();
    systick_config();

    /* 时钟刚换过，睡前又可能强制中止过一帧，485 整体重新初始化。
     * bsp_rs485_init() 可重复调用（内部先 usart_deinit / timer_deinit），
     * 波特率取全局 BSP_RS485_BAUDRATE，仍是参数区的值 */
    bsp_rs485_init();
#endif
}

/* SLEEP_DEPTH==0 排障档专用：主循环每圈调一次，闹钟响过就从 485 回一行裸字符串。
 * 其它档位编译成空函数 */
void bsp_wkp_alarm_test_poll(void)
{
#if (SLEEP_DEPTH == 0)
    if (g_rtc_alarm_fired) {
        g_rtc_alarm_fired = 0;
        bsp_rs485_send_data((const u8 *)"RTC ALARM FIRED", 15);
        printf("RTC ALARM FIRED\r\n");
    }
#endif
}



// 
void bsp_standby_mode(void)
{
    rcu_periph_clock_enable(RCU_PMU);

    /*清除唤醒标志位 (WUP) 和 待机状态位 (STB) */
    pmu_flag_clear(PMU_FLAG_RESET_WAKEUP);
    pmu_flag_clear(PMU_FLAG_RESET_STANDBY);

    //配置唤醒源
    pmu_wakeup_pin_enable();

    //进入待机模式
	pmu_to_standbymode();
}



/**
 * @brief  初始化按键中断
 */
void Wake_Up_Key_Init(void)
{

    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_SYSCFG);

    /* 配置 GPIO 模式*/
    gpio_mode_set(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_PULLDOWN, GPIO_PIN_0);

    /* 配置NVIC优先级 */
    nvic_irq_enable(EXTI0_IRQn, 2, 0);

    /* 连接 EXTI 线到 GPIO 引脚 */
    syscfg_exti_line_config(EXTI_SOURCE_GPIOA, EXTI_SOURCE_PIN0);

    /* 设置为中断模式，上升沿触发 */
    exti_init(EXTI_0, EXTI_INTERRUPT, EXTI_TRIG_RISING);
    
    /* 使能中断并清除之前的标志位 */
    exti_interrupt_flag_clear(EXTI_0);
    exti_interrupt_enable(EXTI_0);
}


