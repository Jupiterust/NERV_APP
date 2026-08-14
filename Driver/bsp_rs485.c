#include "bsp_rs485.h"
#include <string.h>

// 双缓冲区
static u8           rx_buffer[BSP_RS485_RX_BUF_SIZE];
static volatile u16 rx_index = 0;

u8           rx_real_buffer[BSP_RS485_RX_BUF_SIZE];
volatile u16 rx_real_len = 0;
static volatile u8  rx_complete = 0;
volatile u8  g_rs485_rx_flag = 0;

uint32_t BSP_RS485_BAUDRATE = 19200U;

/* ---- Modbus RTU T3.5 帧间静默定时器（TIMER6） ----
 * 用定时器复现协议规定的 T3.5 判帧，取代早先直接用 USART 硬件 IDLE 标志判帧尾的做法。
 * GD32 的 IDLE 标志只相当于约 1 个字符时间的静默，远短于协议的 3.5 个字符时间，字节间隔稍
 * 有抖动（例如某些 USB 转 RS485 适配器会攒批发送）就会把一帧从中间切开，前后两段各自校验
 * 失败被丢弃，现象是偶发无应答/超时。改成每收到一个字节把 TIMER6 清零重新起跑，静默满
 * T3.5 才算一帧结束，和 FreeModbus 官方移植（porttimer.c）等价。
 * 自定义协议靠 A5B6...B6A5 自带定界，不受切帧粒度影响，只是回包时延跟着 T3.5 变长
 *（115200 下多约 1.75ms）。
 * 只做 T3.5，不做 T1.5 帧内字符间隔检测：TIM6 是基本定时器没有输入捕获通道，且单主站轮询
 * 总线上 T1.5 收益有限，多数实际部署的 FreeModbus 移植也只靠 T3.5 判帧。
 */
#define RS485_T35_TIMER        TIMER6
#define RS485_T35_TIMER_RCU    RCU_TIMER6
#define RS485_T35_TIMER_IRQn   TIMER6_IRQn
#define RS485_T35_TIMER_PSC    (84U - 1U)  // TIM6 挂在 APB1(42MHz)，x2 后 84MHz，分频到 1MHz(1us/tick)

// 按波特率算 T3.5（us）：>19200 时协议规定固定取 1.75ms，否则按 3.5 个字符时间换算
// （1 字符 = 11bit，这是协议基准帧长，与实际线上配置无关）
static uint32_t rs485_calc_t35_us(uint32_t baud)
{
    if (baud > 19200U) {
        return 1750U;
    }
    return (uint32_t)((38500000UL + baud - 1U) / baud); // 38.5/baud 秒，向上取整避免偏短
}

static void rs485_t35_timer_init(void)
{
    rcu_periph_clock_enable(RS485_T35_TIMER_RCU);
    rcu_timer_clock_prescaler_config(RCU_TIMER_PSC_MUL2);

    timer_parameter_struct timer_initpara;
    timer_deinit(RS485_T35_TIMER);

    timer_initpara.prescaler         = RS485_T35_TIMER_PSC;
    timer_initpara.period            = 0xFFFF; // 实际重载值由 rs485_t35_timer_apply_baud() 按波特率设
    timer_initpara.alignedmode       = TIMER_COUNTER_EDGE;
    timer_initpara.counterdirection  = TIMER_COUNTER_UP;
    timer_initpara.clockdivision     = TIMER_CKDIV_DIV1;
    timer_initpara.repetitioncounter = 0;
    timer_init(RS485_T35_TIMER, &timer_initpara);

    // 抢占优先级和 USART1 中断（BSP_RS485_IRQn，0,0）同档，两者不能互相打断，
    // 避免与 USART1_IRQHandler 里对 rx_index/rx_buffer 的操作产生竞态
    nvic_irq_enable(RS485_T35_TIMER_IRQn, 0, 1);
    timer_flag_clear(RS485_T35_TIMER, TIMER_FLAG_UP);
    timer_interrupt_enable(RS485_T35_TIMER, TIMER_INT_UP);

    timer_disable(RS485_T35_TIMER); // 默认不启动，收到第一个字节再开表
}

// 波特率一变（初始化或 bsp_rs485_set_baudrate）就要重算 T3.5 重载值，
// 沿用旧值算出的窗口偏窄或偏宽都会重新导致误切帧
static void rs485_t35_timer_apply_baud(uint32_t baud)
{
    uint32_t t35_us = rs485_calc_t35_us(baud);
    timer_disable(RS485_T35_TIMER);
    timer_autoreload_value_config(RS485_T35_TIMER, t35_us - 1U);
    timer_counter_value_config(RS485_T35_TIMER, 0);
    timer_flag_clear(RS485_T35_TIMER, TIMER_FLAG_UP);
}

/* 发送 */
static u8           tx_buffer[BSP_RS485_TX_BUF_SIZE];
// 必须是 u16：缓冲区 1024 字节，而 Modbus ASCII 读多个寄存器的应答轻易超过 255
//（读满 64 个保持寄存器的 ASCII 应答是 267 字节），u8 会被静默截断成半截帧
static volatile u16 tx_len      = 0;
static volatile u16 tx_index    = 0;
volatile u8         bsp_rs485_send_busy = 0;

void bsp_rs485_init(void)
{
    rcu_periph_clock_enable(BSP_RS485_GPIO_RCU);
    rcu_periph_clock_enable(BSP_RS485_RCU);
    rcu_periph_clock_enable(BSP_RS485_DIR_RCU);
    // DIR: PE8 推挽输出，默认收
    gpio_mode_set(BSP_RS485_DIR_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLDOWN, BSP_RS485_DIR_PIN);
    gpio_output_options_set(BSP_RS485_DIR_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, BSP_RS485_DIR_PIN);
    BSP_RS485_DIR_RX();

    // TX 引脚配置
    gpio_af_set(BSP_RS485_GPIO_PORT, GPIO_AF_7, BSP_RS485_TX_PIN);
    gpio_mode_set(BSP_RS485_GPIO_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, BSP_RS485_TX_PIN);
    gpio_output_options_set(BSP_RS485_GPIO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, BSP_RS485_TX_PIN);

    // RX 引脚配置
    gpio_af_set(BSP_RS485_GPIO_PORT, GPIO_AF_7, BSP_RS485_RX_PIN);
    gpio_mode_set(BSP_RS485_GPIO_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, BSP_RS485_RX_PIN);

    // 串口参数配置
    usart_deinit(BSP_RS485_USART);
    usart_baudrate_set(BSP_RS485_USART, BSP_RS485_BAUDRATE);
    // 帧格式由 bsp_rs485.h 的三个宏决定，默认 8-N-1（初赛要求）。
    // 现场要 Modbus 标准的 8-E-1 只改宏，这里不动
    usart_word_length_set(BSP_RS485_USART, BSP_RS485_WORDLENGTH);
    usart_parity_config(BSP_RS485_USART, BSP_RS485_PARITY);
    usart_stop_bit_set(BSP_RS485_USART, BSP_RS485_STOPBIT);
    usart_receive_config(BSP_RS485_USART, USART_RECEIVE_ENABLE);
    usart_transmit_config(BSP_RS485_USART, USART_TRANSMIT_ENABLE);

    // 在初始化里直接开 NVIC 和接收中断，防止漏调用
    nvic_irq_enable(BSP_RS485_IRQn, 0, 0);
    usart_interrupt_enable(BSP_RS485_USART, USART_INT_RBNE);
    // 帧结束由 TIM6 的 T3.5 静默定时器判定，不用 USART 硬件 IDLE 标志，见文件顶部

    usart_enable(BSP_RS485_USART);

    rs485_t35_timer_init();
    rs485_t35_timer_apply_baud(BSP_RS485_BAUDRATE);
}

/**
 * @brief  动态修改 RS485 通讯波特率
 */
void bsp_rs485_set_baudrate(uint32_t new_baudrate)
{

    // 先等发送缓冲区空 (TBE)
    uint32_t timeout = 0xFFFF;
    while (RESET == usart_flag_get(BSP_RS485_USART, USART_FLAG_TBE) && timeout--);

    // 再等物理层发送完成 (TC)
    timeout = 0xFFFF;
    while (RESET == usart_flag_get(BSP_RS485_USART, USART_FLAG_TC) && timeout--);

    usart_disable(BSP_RS485_USART);

    BSP_RS485_BAUDRATE = new_baudrate;
    usart_baudrate_set(BSP_RS485_USART, new_baudrate);

    // 清掉可能残留的状态位
    usart_flag_clear(BSP_RS485_USART, USART_FLAG_RBNE);
    usart_flag_clear(BSP_RS485_USART, USART_FLAG_TC);

    usart_enable(BSP_RS485_USART);

    rs485_t35_timer_apply_baud(new_baudrate); // T3.5 静默窗口跟着重算
}

void bsp_rs485_send_data(const u8 *data, u16 length)
{
    if (length == 0 || length > BSP_RS485_TX_BUF_SIZE) return;

    // 等待上一帧发送完毕
    while (bsp_rs485_send_busy);
    bsp_rs485_send_busy = 1;

    BSP_RS485_DIR_TX();

    // 关接收中断，避免发送时自发自收
    usart_interrupt_disable(BSP_RS485_USART, USART_INT_RBNE);

    memcpy(tx_buffer, data, length);
    tx_len   = length;
    tx_index = 0;

    // 开 TBE 中断，发送由中断接着做
    usart_interrupt_enable(BSP_RS485_USART, USART_INT_TBE);
}

/* ---- 发送是异步的，需要"真的发完了"这个判据时用下面两个函数 ----
 * bsp_rs485_send_data() 拷完数据、拉 DE 到发送态、关 RBNE、开 TBE 中断就返回，字节由
 * USART1_IRQHandler 逐个发出，收尾（DE 切回接收、重开 RBNE、清 bsp_rs485_send_busy）
 * 在 TC 中断分支里做。所以 bsp_rs485_send_busy 归零才是这一帧彻底结束的可靠信号。
 * 不要用 while(RESET == usart_flag_get(BSP_RS485_USART, USART_FLAG_TC)) 代替：
 *   1) 刚调完 send_data、TBE 中断还没写第一个字节时，TC 还是上一帧留下的 SET，直接穿过去；
 *   2) 等到 TC 置位时 TC 中断还没执行，DE 仍在发送态、RBNE 仍关着、busy 仍是 1。
 * 带着这种状态去停时钟（深睡）或重配串口，485 收发器会被钉在发送态把总线拉死，
 * 下一次 bsp_rs485_send_data() 也会卡死在开头的 while (bsp_rs485_send_busy)。*/
u8 bsp_rs485_wait_tx_done(u32 timeout_ms)
{
    while (bsp_rs485_send_busy && timeout_ms > 0U) {
        delay_1ms(1);
        timeout_ms--;
    }
    return (u8)(bsp_rs485_send_busy == 0U);
}

void bsp_rs485_tx_abort(void)
{
    usart_interrupt_disable(BSP_RS485_USART, USART_INT_TBE);
    usart_interrupt_disable(BSP_RS485_USART, USART_INT_TC);

    tx_index = 0;
    tx_len   = 0;

    // 先放开总线再恢复接收，顺序反了会漏掉紧接着到来的第一个字节
    BSP_RS485_DIR_RX();
    usart_flag_clear(BSP_RS485_USART, USART_FLAG_TC);
    usart_interrupt_enable(BSP_RS485_USART, USART_INT_RBNE);

    bsp_rs485_send_busy = 0;
}

u16 bsp_rs485_receive_data(u8 *buffer, u16 max_length)
{
    u16 length = 0;
    if (rx_complete) {
        length = (rx_real_len < max_length) ? rx_real_len : max_length;
        memcpy(buffer, rx_real_buffer, length);
        rx_complete = 0;
        rx_real_len = 0;
    }
    return length;
}

// 收到数据后原样回显，调试用
void bsp_rs485_process_echo(void)
{
    if (rx_complete)
    {
        static u8 temp_send_buf[150];   // 静态，不占栈
        const u8 header_len = 13;

        memset(temp_send_buf, 0, sizeof(temp_send_buf));
        memcpy(temp_send_buf, "485_RECV_OK\r\n", header_len);

        // 长度够才拼上原始数据
        if (rx_real_len > 0 && rx_real_len < (sizeof(temp_send_buf) - header_len))
        {
            memcpy(&temp_send_buf[header_len], (void *)rx_real_buffer, rx_real_len);
            bsp_rs485_send_data(temp_send_buf, header_len + rx_real_len);
        }
        else
        {
            bsp_rs485_send_data(temp_send_buf, header_len);
        }

        rx_real_len = 0;
        rx_complete = 0;
    }
}

void USART1_IRQHandler(void)
{
    // 发送缓冲区空
    if (usart_interrupt_flag_get(BSP_RS485_USART, USART_INT_FLAG_TBE))
    {
        if (tx_index < tx_len) {
            usart_data_transmit(BSP_RS485_USART, tx_buffer[tx_index++]);
        } else {
            tx_index = 0;
            tx_len   = 0;
            usart_interrupt_disable(BSP_RS485_USART, USART_INT_TBE);
            usart_interrupt_enable(BSP_RS485_USART, USART_INT_TC);
        }
    }

    // 移位寄存器物理发送完毕
    if (usart_interrupt_flag_get(BSP_RS485_USART, USART_INT_FLAG_TC))
    {
        usart_interrupt_flag_clear(BSP_RS485_USART, USART_INT_FLAG_TC);
        usart_interrupt_disable(BSP_RS485_USART, USART_INT_TC);
        
        BSP_RS485_DIR_RX();

        usart_interrupt_enable(BSP_RS485_USART, USART_INT_RBNE);
        bsp_rs485_send_busy = 0;
    }

    // 接收寄存器非空
    if (RESET != usart_interrupt_flag_get(BSP_RS485_USART, USART_INT_FLAG_RBNE))
    {
        u8 val = (u8)usart_data_receive(BSP_RS485_USART);

        if (rx_index < BSP_RS485_RX_BUF_SIZE) {
            rx_buffer[rx_index++] = val;
        }

        // 每收到一个字节就把 T3.5 计时器清零重新起跑，静默满 3.5 个字符时间没有新字节
        // 才算一帧结束，交给 TIMER6_IRQHandler
        timer_disable(RS485_T35_TIMER);
        timer_counter_value_config(RS485_T35_TIMER, 0);
        timer_flag_clear(RS485_T35_TIMER, TIMER_FLAG_UP);
        timer_enable(RS485_T35_TIMER);
    }
}

// T3.5 静默超时：距上一个字节已过 3.5 个字符时间，判定一帧结束。
// 抢占优先级和 USART1 中断相同（见 rs485_t35_timer_init），互不打断，
// 不会和 USART1_IRQHandler 里对 rx_index/rx_buffer 的操作产生竞态。
void TIMER6_IRQHandler(void)
{
    if (RESET != timer_interrupt_flag_get(RS485_T35_TIMER, TIMER_INT_FLAG_UP))
    {
        timer_interrupt_flag_clear(RS485_T35_TIMER, TIMER_INT_FLAG_UP);
        timer_disable(RS485_T35_TIMER); // 停表，下一个字节到来时重新起跑

        if (rx_index > 0) {
            // 搬到 rx_real_buffer，避免后续字节覆盖还没被读走的数据
            memcpy(rx_real_buffer, rx_buffer, rx_index);
            rx_real_len = rx_index;
            rx_complete = 1;
            g_rs485_rx_flag = 1;    // 通知主循环有新帧
            rx_index = 0;
        }
    }
}
