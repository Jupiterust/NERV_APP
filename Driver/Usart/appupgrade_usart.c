#include "appupgrade_usart.h"

void appupgrade_usart_init(void) 
{
    //! 开启对应的中断处理函数
    nvic_irq_enable(APPUPGRADE_USART_IRQ, 3, 2);

    //! 使能USART2时钟和GPIOD时钟
    rcu_periph_clock_enable(APPUPGRADE_USART_RCU);
    rcu_periph_clock_enable(APPUPGRADE_USART_PIN_RCU);

    // 对TX (PD8) 配置成为复用模式
    gpio_af_set(APPUPGRADE_USART_PORT, GPIO_AF_7, APPUPGRADE_USART_TX_Pin);
    // 设置GPIO引脚模式
    gpio_mode_set(APPUPGRADE_USART_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, APPUPGRADE_USART_TX_Pin);
    // 设置GPIO引脚参数
    gpio_output_options_set(APPUPGRADE_USART_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, APPUPGRADE_USART_TX_Pin);

    // 对RX (PD9) 配置成为复用模式
    gpio_af_set(APPUPGRADE_USART_PORT, GPIO_AF_7, APPUPGRADE_USART_RX_Pin);
    // 设置GPIO引脚模式 (接收引脚通常也可以配置为上拉 GPIO_PUPD_PULLUP，这里保持与您原始配置一致)
    gpio_mode_set(APPUPGRADE_USART_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, APPUPGRADE_USART_RX_Pin);
    // 设置GPIO引脚参数
    gpio_output_options_set(APPUPGRADE_USART_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, APPUPGRADE_USART_RX_Pin);

    // 配置USART2
    usart_deinit(APPUPGRADE_USART);
    usart_baudrate_set(APPUPGRADE_USART, 115200U);
    usart_receive_config(APPUPGRADE_USART, USART_RECEIVE_ENABLE);
    usart_transmit_config(APPUPGRADE_USART, USART_TRANSMIT_ENABLE);
    usart_enable(APPUPGRADE_USART);

    //! 启动串口中断
    usart_interrupt_enable(APPUPGRADE_USART, USART_INT_RBNE);
    usart_interrupt_enable(APPUPGRADE_USART, USART_INT_IDLE);
}



#define USART_EXAMINE 0
#if USART_EXAMINE

//! 接收数据的缓冲区
uint8_t recv_buf[128] = { 0 };
uint8_t recv_len = 0;
//! 接收数据的数组
uint8_t recv_real_buf[128] = { 0 };
uint8_t recv_real_len = 0;
//! 接收数据的标记位
uint8_t recv_flag = 0;



void appupgrade_usart_recv_buf(void)
{
    if (recv_flag)
    {
        // 将接收到的实际数据逐个发送回电脑
        for (uint8_t i = 0; i < recv_real_len; i++)
        {
            usart_data_transmit(APPUPGRADE_USART, recv_real_buf[i]);
            // 等待发送缓冲区空（TBE），避免数据覆盖
            while (RESET == usart_flag_get(APPUPGRADE_USART, USART_FLAG_TBE));
        }
        
        // 或者是发送一个可见字符如 'A' 进行测试
        // usart_data_transmit(APPUPGRADE_USART, 'A');
        // while (RESET == usart_flag_get(APPUPGRADE_USART, USART_FLAG_TBE));

        recv_flag = 0; // 清除处理标记
    }
}


void APPUPGRADE_USART_IRQHandler(void)
{
	// 如果是接受中断
    if (usart_interrupt_flag_get(APPUPGRADE_USART, USART_INT_FLAG_RBNE) != RESET)
    {
        uint8_t data = (uint8_t)usart_data_receive(APPUPGRADE_USART);
        if (recv_len < 128)
        {
            recv_buf[recv_len++] = data;
        }
        // BSP_LED2_ON(); 
    }
    // 如果是空闲中断
    if (usart_interrupt_flag_get(APPUPGRADE_USART, USART_INT_FLAG_IDLE) != RESET)
    {
        // 清除 IDLE 标志
        volatile uint32_t temp = usart_data_receive(APPUPGRADE_USART);
        (void)temp;

        BSP_LED3_ON(); 

        if (recv_len != 0)
        {
            // 诊断 4：只有在收到数据（len != 0）且产生空闲时，才点亮 LED4
            BSP_LED4_ON(); 

            memcpy(recv_real_buf, recv_buf, recv_len);
            recv_real_len = recv_len;
            recv_len = 0;
            recv_flag = 1;
        }
    }
}
#else 

uint8_t app_upgrade_usart_tmp_buf[40 * 1024] = { 0 };
uint32_t app_upgrade_usart_tmp_buf_len = 0;
uint8_t app_upgrade_usart_recv_flag = 0;


void APPUPGRADE_USART_IRQHandler(void)
{
	/* 关键修改: 添加缓冲区溢出保护 */
	if (RESET != usart_flag_get(APPUPGRADE_USART , USART_FLAG_RBNE))
	{
		if (app_upgrade_usart_tmp_buf_len < sizeof(app_upgrade_usart_tmp_buf))
		{
			app_upgrade_usart_tmp_buf[app_upgrade_usart_tmp_buf_len++] = usart_data_receive(APPUPGRADE_USART);
		}
		else
		{
			/* 缓冲区满，丢弃数据并读取寄存器清除标志 */
			(void)usart_data_receive(APPUPGRADE_USART);
		}
	}

	/* 接收完成 */
	if (RESET != usart_flag_get(APPUPGRADE_USART , USART_FLAG_IDLE) && app_upgrade_usart_tmp_buf_len > 0)
	{
		/* 清除中断标记位 */
		(void)usart_data_receive(APPUPGRADE_USART);

		/* 关键修改: 只有在未处理时才设置标志 */
		if (app_upgrade_usart_recv_flag == 0)
		{
			app_upgrade_usart_recv_flag = 1;

			/* 关键修改: 接收完成后禁用IDLE中断，防止重复触发 */
			usart_interrupt_disable(APPUPGRADE_USART , USART_INT_IDLE);
			usart_interrupt_disable(APPUPGRADE_USART , USART_INT_RBNE);
		}
	}
}


#endif

/****************************End*****************************/
