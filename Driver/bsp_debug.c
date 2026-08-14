#include "bsp_debug.h"


// rx buffer
volatile uint8_t bsp_debug_recv_data_buffer[BSP_DEBUG_RX_BUFFER_LENGTH] = {0};
// rx buffer index
volatile uint32_t bsp_debug_usart_buf_index = 0;
// 接收完成标志位
volatile bool bsp_debug_receive_finished_flag = false;
int fputc(int ch, FILE *f)
{
    usart_data_transmit(BSP_DEBUG_UART, (uint8_t)ch);
    while(RESET == usart_flag_get(BSP_DEBUG_UART, USART_FLAG_TBE));
    return ch;
}



void bsp_debug_init(void){
	// 使能时钟
    rcu_periph_clock_enable(BSP_DUBUG_GPIO_RCU);    
    rcu_periph_clock_enable(BSP_DEBUG_UART_RCU); 
	// 端口配置
	gpio_af_set(BSP_DUBUG_GPIO_PORT, GPIO_AF_7, BSP_DEBUG_GPIO_TX | BSP_DEBUG_GPIO_RX);				
	gpio_mode_set(BSP_DUBUG_GPIO_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, BSP_DEBUG_GPIO_TX);  	

    gpio_output_options_set(BSP_DUBUG_GPIO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, BSP_DEBUG_GPIO_TX);  

	gpio_mode_set(BSP_DUBUG_GPIO_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, BSP_DEBUG_GPIO_RX);  		
	gpio_output_options_set(BSP_DUBUG_GPIO_PORT, GPIO_MODE_ANALOG, GPIO_OSPEED_50MHZ, BSP_DEBUG_GPIO_RX);  

	// 字长8位 停止位1位 无校验 波特率115200
    usart_deinit(BSP_DEBUG_UART);    						
    usart_word_length_set(BSP_DEBUG_UART, USART_WL_8BIT);  
    usart_stop_bit_set(BSP_DEBUG_UART, USART_STB_1BIT);    
    usart_parity_config(BSP_DEBUG_UART, USART_PM_NONE);		
    usart_baudrate_set(BSP_DEBUG_UART, 115200U);     		
    usart_receive_config(BSP_DEBUG_UART, USART_RECEIVE_ENABLE);     
	usart_transmit_config(BSP_DEBUG_UART, USART_TRANSMIT_ENABLE);   
	usart_hardware_flow_rts_config(BSP_DEBUG_UART, USART_RTS_DISABLE);
    usart_hardware_flow_cts_config(BSP_DEBUG_UART, USART_CTS_DISABLE);
    usart_enable(BSP_DEBUG_UART);          							
}

//接收idle中断使能
void bsp_debug_irq_enable(void){
	nvic_irq_enable(BSP_DEBUG_UART_IRQn, 0, 0);
	usart_interrupt_enable(BSP_DEBUG_UART, USART_INT_RBNE);
    usart_interrupt_enable(BSP_DEBUG_UART, USART_INT_IDLE);
}

void bsp_debug_senddata(uint16_t *buf,uint16_t len)
 {
     uint16_t t;
     for(t=0;t<len;t++)      
     {           
         while(usart_flag_get(BSP_DEBUG_UART, USART_FLAG_TC) == RESET);  
         usart_data_transmit(BSP_DEBUG_UART,buf[t]);
     }     
     while(usart_flag_get(BSP_DEBUG_UART, USART_FLAG_TC) == RESET);          
}

void BSP_DEBUG_UART_IRQHandler(void)
{
    if(RESET != usart_interrupt_flag_get(BSP_DEBUG_UART, USART_INT_FLAG_RBNE))
	{			
		// 防越界
		if(bsp_debug_usart_buf_index > (BSP_DEBUG_RX_BUFFER_LENGTH - 1)){
			bsp_debug_usart_buf_index = 0;
		}
		 //  数据接收处理
		bsp_debug_recv_data_buffer[bsp_debug_usart_buf_index++] = usart_data_receive(BSP_DEBUG_UART);             
		usart_interrupt_flag_clear(BSP_DEBUG_UART, USART_INT_FLAG_RBNE); 	//清除接收中断标志位
		
#if 0
		BSP_LED5_TOGGLE();// 用于验证现象验真现象
#endif
    } 
	if (RESET != usart_flag_get(BSP_DEBUG_UART , USART_FLAG_IDLE) && bsp_debug_usart_buf_index > 0){
		
		volatile uint32_t tmp;
		tmp = USART_STAT0(BSP_DEBUG_UART);
        tmp = USART_DATA(BSP_DEBUG_UART);
        (void)tmp;
		
		
		if(bsp_debug_receive_finished_flag == false){
			bsp_debug_receive_finished_flag = true;
			bsp_debug_usart_buf_index = 0;
		}
	}
}
// 对接收的数据进行处理
void bsp_debug_handler(void){
	if(bsp_debug_receive_finished_flag == true){
		bsp_debug_receive_finished_flag = false;
		
	}
}
