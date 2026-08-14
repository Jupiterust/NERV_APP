#include "bsp_general_timer.h"

volatile uint8_t g_report_flag = 0;

// TIMER7 溢出周期修改函数
// 最大支持约 6s
void bsp_tim7_set_timeout(uint16_t seconds)
{
    if (seconds > 6) seconds = 6;

    uint32_t new_period = (seconds * 10000) - 1;

    /* ARR 带影子寄存器，新值必须靠一次更新事件(UPG)才能装载进去，所以下面那句
     * timer_event_software_generate() 是省不掉的。但它有个副作用：软件产生的更新事件
     * 和真正的计数溢出【完全等价】，一样会把 UIF 置起来，而 TIM7 的更新中断从
     * bsp_tim7_init() 起就一直是使能的（那里只调了 timer_disable 停计数器，
     * timer_disable 并不影响软件更新事件，计数器停着照样能触发）。
     *
     * 于是不加保护的话：改一次上报间隔 == 白白触发一次更新中断 == g_report_flag 被置 1
     * == 主循环多发一帧 0x0302 自动上报数据帧。表现就是上位机发一条 0x0261，
     * 收回来的是"OK 应答 + 一帧自动上报数据"两帧粘在一起，而且自动上报根本没开过。
     *
     * 注意顺序不能变，也不能只在事后清标志：中断优先级 (2,3) 高于主循环，
     * timer_event_software_generate() 执行的当场 CPU 就跳进 ISR 了，
     * ISR 里已经把 g_report_flag 置成 1、把 UIF 清掉，等下一行再清就来不及了。 */
    timer_interrupt_disable(TIMER7, TIMER_INT_UP);               // 先把更新中断关掉

    timer_autoreload_value_config(TIMER7, (uint16_t)new_period); // 更新 ARR
    timer_counter_value_config(TIMER7, 0);                       // CNT 清零
    timer_event_software_generate(TIMER7, TIMER_EVENT_SRC_UPG);  // 新 ARR 重新装载

    timer_flag_clear(TIMER7, TIMER_FLAG_UP);                     // 清掉这次事件留下的标志
    timer_interrupt_flag_clear(TIMER7, TIMER_INT_FLAG_UP);
    timer_interrupt_enable(TIMER7, TIMER_INT_UP);                // 再把更新中断开回去

    g_report_flag = 0;   // 保险：万一在关中断之前就已经被置位过
}

// 主频 168MHz(APB2 × 2)
// TIMER7 初始化  1s 溢出，
void bsp_tim7_init(void)
{
	//	时钟enable
    rcu_periph_clock_enable(RCU_TIMER7);
    rcu_timer_clock_prescaler_config(RCU_TIMER_PSC_MUL2);

    timer_parameter_struct timer_initpara;
    timer_deinit(TIMER7);

	
    timer_initpara.prescaler         = 16800 - 1;
    timer_initpara.period            = 10000 - 1;
    timer_initpara.alignedmode       = TIMER_COUNTER_EDGE;
    timer_initpara.counterdirection  = TIMER_COUNTER_UP;
    timer_initpara.clockdivision     = TIMER_CKDIV_DIV1;
    timer_initpara.repetitioncounter = 0;
    timer_init(TIMER7, &timer_initpara);

    nvic_irq_enable(TIMER7_UP_TIMER12_IRQn, 2, 3);
    timer_flag_clear(TIMER7, TIMER_FLAG_UP);
    timer_interrupt_enable(TIMER7, TIMER_INT_UP);

    timer_disable(TIMER7); // 默认不启动，由上层按需开启
}

// TIMER9 初始化，10ms 溢出，
// ADC 周期采样
void bsp_tim9_init(void)
{
    rcu_periph_clock_enable(RCU_TIMER9);
    rcu_timer_clock_prescaler_config(RCU_TIMER_PSC_MUL2);

    timer_parameter_struct timer_initpara;
    timer_deinit(TIMER9);

    // PSC = 168-1 → 计数步长 1us（1MHz）
    // ARR = 10000-1 → 溢出周期 10ms
    timer_initpara.prescaler         = 168 - 1;
    timer_initpara.period            = 10000 - 1;
    timer_initpara.alignedmode       = TIMER_COUNTER_EDGE;
    timer_initpara.counterdirection  = TIMER_COUNTER_UP;
    timer_initpara.clockdivision     = TIMER_CKDIV_DIV1;
    timer_initpara.repetitioncounter = 0;
    timer_init(TIMER9, &timer_initpara);

    // 优先级需高于 TIMER7，保证采样不被上报打断
    nvic_irq_enable(TIMER0_UP_TIMER9_IRQn, 2, 2);
    timer_flag_clear(TIMER9, TIMER_FLAG_UP);
    timer_interrupt_enable(TIMER9, TIMER_INT_UP);

    timer_enable(TIMER9);
}

// TIMER9 中断，10ms 
//	ADC 双通道采样、换算，以及系统指示灯1s翻转
void TIMER0_UP_TIMER9_IRQHandler(void)
{
    if (SET == timer_interrupt_flag_get(TIMER9, TIMER_INT_FLAG_UP)) {
        timer_interrupt_flag_clear(TIMER9, TIMER_INT_FLAG_UP);

        uint16_t ch0 = bsp_adc_get_channel0();
        Data_class_structure.ch0_original_val = (float)ch0 * 3.3f / 4095.0f;
        Data_class_structure.ch0_current_val  = Data_class_structure.ch0_original_val * Data_class_structure.ch0_ratio;

        uint16_t ch1 = bsp_adc_get_channel1();
        Data_class_structure.ch1_original_val = (float)ch1 * 3.3f / 4095.0f;
        Data_class_structure.ch1_current_val  = Data_class_structure.ch1_original_val * Data_class_structure.ch1_ratio;

        // 外部 ADC GD30AD3344：每拍走一次"读上一个通道 + 启动下一个通道单次转换"
        // 的 16 位 SPI 传输，约 20us，内部没有任何 delay。
        // 开关和扫描通道在 bsp_GD30AD3344.h 的【1.2】区；关掉后这里是空函数，不用删。
        ad3344_tim9_10ms_isr();

        // 100 × 10ms = 1s 翻转一次，作为系统心跳指示
        static uint8_t system_led_state_count = 0;
        system_led_state_count++;
        if (system_led_state_count >= 100) {
            system_led_state_count = 0;
            BSP_LED1_TOGGLE();
        }
    }
}

// TIMER7 中断，溢出周期1s / 3s / 5s
void TIMER7_UP_TIMER12_IRQHandler(void)
{
    if (SET == timer_interrupt_flag_get(TIMER7, TIMER_INT_FLAG_UP)) {
        timer_interrupt_flag_clear(TIMER7, TIMER_INT_FLAG_UP);
        g_report_flag = 1;
    }
}