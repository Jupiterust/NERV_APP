#include "bsp_dac.h"


void bsp_dac_init(void)
{
	// clock enable
    rcu_periph_clock_enable(BSP_DAC_GPIO_RCU);
    rcu_periph_clock_enable(BSP_DAC_RCU);
	
    gpio_mode_set(BSP_DAC_GPIO_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, BSP_DAC_GPIO_PIN);

    dac_deinit(BSP_DAC);
	// 软件触发
    dac_trigger_source_config(BSP_DAC, DAC_OUT0, DAC_TRIGGER_SOFTWARE);
    dac_trigger_enable(BSP_DAC, DAC_OUT0);
    // 禁用波形输出
    dac_wave_mode_config(BSP_DAC, DAC_OUT0, DAC_WAVE_DISABLE);
    
    dac_output_buffer_enable(BSP_DAC, DAC_OUT0);
    dac_enable(BSP_DAC, DAC_OUT0);
}

void bsp_dac_set_voltage(uint16_t voltage_mv)
{
    // 限幅
    if(voltage_mv > BSP_DAC_VREF_MV){
        voltage_mv = BSP_DAC_VREF_MV;
    }

    //  val = voltage_mv * 4095 / Vref_mv
    uint16_t dac_val = (uint16_t)((uint32_t)voltage_mv * 4095 / BSP_DAC_VREF_MV);

    dac_data_set(BSP_DAC, DAC_OUT0, DAC_ALIGN_12B_R, dac_val);
    dac_software_trigger_enable(BSP_DAC, DAC_OUT0);
}


void bsp_dac_set_raw(uint16_t raw_val)
{
    if(raw_val > 4095){
        raw_val = 4095;
    }
    dac_data_set(BSP_DAC, DAC_OUT0, DAC_ALIGN_12B_R, raw_val);
    dac_software_trigger_enable(BSP_DAC, DAC_OUT0);
}