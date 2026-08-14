#include "Headfile.h"


static uint8_t last_oled_state = 0xFF;
int a = 0;

int main(void) 
{
	SystemInit();
	app_nvic_correction();
	nvic_priority_group_set(NVIC_PRIGROUP_PRE1_SUB3);
    bsp_debug_init();
	bsp_oledSoftware_Init();
	OLED_Printf(0,0,16,"2026467365");
	OLED_Printf(0,2,16,"IDLE");
    bsp_flash_spi_init();
	// 加载内部flash的参数
	param_load();

	printf("flash_id = 0x%X\r\n", bsp_flash_spi_read_id());
	bsp_log_init();		// 扫描出断电前的写指针，接着往后写

	
	// 从机地址
#if (MB_FORCE_SLAVE_ADDR != 0)
	Modbus_slave_addr = MB_FORCE_SLAVE_ADDR;
#else
	Modbus_slave_addr = param_get_mb_addr();
#endif
	// Flash 参数在 RAM 里的镜像
	MY_DEVICE_ID      = param_get_id();
	Data_class_structure.ch0_ratio     = param_get_float(ch0_ratio_index);
	Data_class_structure.ch1_ratio     = param_get_float(ch1_ratio_index);
	Data_class_structure.ch0_threshold = param_get_float(ch0_threshold_index);
	Data_class_structure.ch1_threshold = param_get_float(ch1_threshold_index);

    Wake_Up_Key_Init();

	// TF卡 + FatFs，失败不影响后续功能，只是文件操作不可用
	// TF卡 + FatFs，失败不影响后续功能，只是文件操作不可用
	bool sd_ready = bsp_sdio_Init();
	printf("sdio init %s\r\n", sd_ready ? "ok" : "failed");
	bsp_sdio_test(sd_ready);
	// uint32_t total_kb,free_kb;
	// bsp_sdio_get_space(&total_kb,&free_kb);
	// printf("sdio total space = %u KB\r\n", total_kb);
	// printf("sdio free space = %u KB\r\n", free_kb);
	// bsp_sdio_mkdir("C");
	// bsp_sdio_file_write_text("A/love.txt","Hello World");
	// bsp_sdio_file_append_text("A/love.txt","Hello Again");



	BSP_RS485_BAUDRATE = param_get_baud();
	bsp_rs485_init();
	
	bsp_rtc_init();
	bsp_adc_dma_init(); 		//	使用 dma
//	bsp_adc_port_init();		// 	未使用dma 

	bsp_dac_init();
	bsp_dac_set_raw(4095);	//默认设置为4095
	
	// 外部 ADC：SPI + 芯片配置 + 四通道自检。量程/速率/扫描开关见 bsp_GD30AD3344.h【1】
	bsp_ad3344_init();

	bsp_key_init(); // 使用了TIM5
    // modbus协议使用了TIM6
	bsp_tim7_init();// 使用了TIM7
	bsp_tim9_init();// 使用了TIM9

	// 初始化完成，发一帧心跳
	Protocol_SendFrame(MY_DEVICE_ID, TYPE_HEARTBEAT, CMD_SPEC_HEARTBEAT, NULL, 0);

	bsp_debug_irq_enable();

	delay_1ms(100);

	// 打印最终生效的参数值，便于排查
	printf("id=0x%04X baud=%u ratio=%.2f/%.2f thres=%.2f/%.2f\r\n",
	       (unsigned)MY_DEVICE_ID, (unsigned)BSP_RS485_BAUDRATE,
	       Data_class_structure.ch0_ratio, Data_class_structure.ch1_ratio,
	       Data_class_structure.ch0_threshold, Data_class_structure.ch1_threshold);
	
	while (1) 
	{
		// 收到一帧就按 Protocol_mode 分发：自定义协议 / Modbus RTU / ASCII / 自动嗅探
		if (g_rs485_rx_flag == 1)
        {
            Protocol_Route(rx_real_buffer, rx_real_len);
            g_rs485_rx_flag = 0;
			rx_real_len = 0;
        }

		// 定时上报。两个判断都不能少：
		// 1. Modbus 主站在轮询时必须闭嘴，从站主动发帧会和主站轮询在半双工 485 上撞车。
		//    标志位照常清掉，跳过的只是这一次上报，主站切回自定义协议后自动恢复。
		//    开关见 modbus_data_map.h【2】。
		// 2. g_report_flag 只表示 TIM7 更新中断来过一次，不等于用户开了自动上报。
		//    bsp_tim7_set_timeout() 改间隔靠软件更新事件重载 ARR，那个事件同样触发中断，
		//    少了这层判断会凭空发出一帧 0x0302，而 OLED / MB_IREG_AUTO_FLAG 仍显示未上报。
		if (g_report_flag == 1) {
			g_report_flag = 0;
			if (Data_class_structure.Regular_reporting_Flag == 1 &&
			    !Protocol_ModbusMasterActive()) {
				report_auto_data_task();
			}
		}
		
		// 报警阈值判断 + 记日志
		report_police_function(Data_class_structure.alarm_mode);

		// TF 卡数据记录（决赛"采集+存储"）。用 RTC 秒判间隔，没开记录时立即返回，
		// 开关在 file_service.h：FS_REC_FILE / FS_REC_INTERVAL_S / FS_CSV_ROW_FMT
		fs_record_poll();

		// 外部 ADC 调试打印。printf 一行要好几毫秒，TIM9 中断里只置标志，打印放这里。
		// 开关 AD3344_DEBUG_SCAN 关掉时编译成空函数
		ad3344_debug_poll();

		// 睡眠排障档（bsp_wkp.c 的 SLEEP_DEPTH==0）打一行 "RTC ALARM FIRED"，
		// 其它档位编译成空函数
		bsp_wkp_alarm_test_poll();

		// 上报状态变化时刷新 OLED
		uint8_t cur_state = Data_class_structure.Regular_reporting_Flag;
		if (cur_state != last_oled_state) {
			last_oled_state = cur_state;
			if(cur_state == 1){
				OLED_Printf(0, 2, 16, "AutoSample " );
				BSP_LED2_ON();
			}
			else {
				OLED_Printf(0, 2, 16, "IDLE       " );
				BSP_LED2_OFF();
			}
		}

    }
}






