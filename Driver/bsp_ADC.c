#include "bsp_ADC.h"

uint16_t adc_dma_buffer[ADC_DMA_BUF_SIZE];

// 通道0,1的变比
float ch0_ratio = 1.0f;
float ch1_ratio = 1.0f;
float ch0_threshold = 1.0f;
float ch1_threshold = 1.0f;

//  GPIO及ADC时钟初始化，单次采样模式入口
void bsp_adc_port_init(void)
{
    rcu_periph_clock_enable(BSP_ADC_GPIO_RCU);
    rcu_periph_clock_enable(BSP_ADC_RCU);

    
    gpio_mode_set(BSP_ADC_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE,
                  BSP_ADC_GPIO_PIN0 | BSP_ADC_GPIO_PIN1);

    adc_clock_config(ADC_ADCCK_PCLK2_DIV8);

    bsp_adc_init();

    adc_software_trigger_enable(BSP_ADC, ADC_ROUTINE_CHANNEL);
}

//  ADC的初始化

void bsp_adc_init(void)
{
    adc_deinit();

    adc_special_function_config(BSP_ADC, ADC_CONTINUOUS_MODE, ENABLE);
    adc_data_alignment_config(BSP_ADC, ADC_DATAALIGN_RIGHT);

    /* 通道长度改为2 */
    adc_channel_length_config(BSP_ADC, ADC_ROUTINE_CHANNEL, ADC_CHANNEL_NUM);

    /* 规则组序列0：PC0 CH10 */
    adc_routine_channel_config(BSP_ADC, 0, BSP_ADC_CHANNEL0, ADC_SAMPLETIME_56);
    /* 规则组序列1：PC1 CH11 */
    adc_routine_channel_config(BSP_ADC, 1, BSP_ADC_CHANNEL1, ADC_SAMPLETIME_56);

    adc_external_trigger_source_config(BSP_ADC, ADC_ROUTINE_CHANNEL,
                                       ADC_EXTTRIG_INSERTED_T0_CH3);
    adc_external_trigger_config(BSP_ADC, ADC_ROUTINE_CHANNEL, ENABLE);

    adc_enable(BSP_ADC);
    delay_1ms(1);
    adc_calibration_enable(BSP_ADC);
}



//  ADC + DMA初始化，双通道循环采样
void bsp_adc_dma_init(void)
{
    //时钟初始化
    rcu_periph_clock_enable(BSP_ADC_GPIO_RCU);
    rcu_periph_clock_enable(BSP_ADC_RCU);
    rcu_periph_clock_enable(BSP_ADC_DMA_RCU);

    gpio_mode_set(BSP_ADC_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE,
                  BSP_ADC_GPIO_PIN0 | BSP_ADC_GPIO_PIN1);

    adc_clock_config(ADC_ADCCK_PCLK2_DIV8);

    // ADC初始化 
    adc_deinit();
    adc_special_function_config(BSP_ADC, ADC_SCAN_MODE,       ENABLE);  // ← 新增
    adc_special_function_config(BSP_ADC, ADC_CONTINUOUS_MODE, ENABLE);
    adc_data_alignment_config(BSP_ADC, ADC_DATAALIGN_RIGHT);
    adc_channel_length_config(BSP_ADC, ADC_ROUTINE_CHANNEL, ADC_CHANNEL_NUM);

    adc_routine_channel_config(BSP_ADC, 0, BSP_ADC_CHANNEL0, ADC_SAMPLETIME_56);
    adc_routine_channel_config(BSP_ADC, 1, BSP_ADC_CHANNEL1, ADC_SAMPLETIME_56);

    // 删除外部触发配置，使用软件触发 

    adc_enable(BSP_ADC);
    delay_1ms(1);
    adc_calibration_enable(BSP_ADC);

    // DMA初始化 
    dma_deinit(BSP_ADC_DMA, DMA_CH0);

    dma_multi_data_parameter_struct bsp_dma_para;
    dma_multi_data_para_struct_init(&bsp_dma_para);
    bsp_dma_para.periph_addr        = (uint32_t)(&ADC_RDATA(BSP_ADC));
    bsp_dma_para.periph_width       = DMA_PERIPH_WIDTH_16BIT;
    bsp_dma_para.periph_inc         = DMA_PERIPH_INCREASE_DISABLE;
    bsp_dma_para.memory0_addr       = (uint32_t)adc_dma_buffer;
    bsp_dma_para.memory_width       = DMA_MEMORY_WIDTH_16BIT;
    bsp_dma_para.memory_inc         = DMA_MEMORY_INCREASE_ENABLE;
    bsp_dma_para.memory_burst_width = DMA_MEMORY_BURST_SINGLE;
    bsp_dma_para.periph_burst_width = DMA_PERIPH_BURST_SINGLE;
    bsp_dma_para.critical_value     = DMA_FIFO_1_WORD;
    bsp_dma_para.circular_mode      = DMA_CIRCULAR_MODE_ENABLE;
    bsp_dma_para.direction          = DMA_PERIPH_TO_MEMORY;
    bsp_dma_para.number             = ADC_DMA_BUF_SIZE;
    bsp_dma_para.priority           = DMA_PRIORITY_HIGH;
    dma_multi_data_mode_init(BSP_ADC_DMA, DMA_CH0, &bsp_dma_para);

    dma_channel_subperipheral_select(BSP_ADC_DMA, DMA_CH0, DMA_SUBPERI0);
    dma_channel_enable(BSP_ADC_DMA, DMA_CH0);

    // 使能ADC DMA请求 
    adc_dma_mode_enable(BSP_ADC);
    adc_dma_request_after_last_enable(BSP_ADC);

    // 启动连续转换 
    adc_software_trigger_enable(BSP_ADC, ADC_ROUTINE_CHANNEL);
}

//  从DMA buffer取CH10(PC0)的100次均值
uint16_t bsp_adc_get_channel0(void)
{
    uint32_t sum = 0;
    for(uint16_t i = 0; i < ADC_DMA_BUF_SIZE; i += ADC_CHANNEL_NUM){
        sum += adc_dma_buffer[i];       // 偶数索引是CH10
    }
    return (uint16_t)(sum / ADC_SAMPLE_COUNT);
}


//  从DMA buffer取CH11(PC1)的100次均值
uint16_t bsp_adc_get_channel1(void)
{
    uint32_t sum = 0;
    for(uint16_t i = 1; i < ADC_DMA_BUF_SIZE; i += ADC_CHANNEL_NUM){
        sum += adc_dma_buffer[i];       // 奇数索引是CH11
    }
    return (uint16_t)(sum / ADC_SAMPLE_COUNT);
}


//   单次读取指定通道ADC值（轮询方式）
uint16_t bsp_adc_read_channel(uint8_t channel)
{
    // 重新配置为单通道 
    adc_channel_length_config(BSP_ADC, ADC_ROUTINE_CHANNEL, 1);
    adc_routine_channel_config(BSP_ADC, 0, channel, ADC_SAMPLETIME_56);

    // 软件触发一次转换
    adc_software_trigger_enable(BSP_ADC, ADC_ROUTINE_CHANNEL);

    // 等待转换完成 
    while(RESET == adc_flag_get(BSP_ADC, ADC_FLAG_EOC));
    adc_flag_clear(BSP_ADC, ADC_FLAG_EOC);

    return (uint16_t)ADC_RDATA(BSP_ADC);
}
