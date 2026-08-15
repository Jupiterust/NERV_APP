#include "app_upgrade.h"

/* 地址来源统一在 Driver/BootConfig/BootConfig.h，现场改地址只改那一处，
 * 这里的宏名字不变，跟着走就行，不用改这两行。 */
#define DOWNLOAD_ADDR FLASH_APP_DOWNLOAD_BASE

#define CONFIG_SIZE 1024*4

#define PARAM_ADDR BOOT_CONFIG_ADDR


typedef struct __attribute__((packed)) Parameter_SUM
{
	BootParam_t BootParam;
	BootParam_t BootParam_Reserved;
	UpdateLog_t UpdateLog;
	UserConfig_t UserConfig;
	CalibData_t CalibData;

}Parameter_t;

static Parameter_t my_param_sum = { 0 };

static uint8_t config_buf[CONFIG_SIZE] = { 0 };
static uint32_t config_crc32 = 0;

void app_nvic_correction(void){
	/* 关键修改1: 先关闭所有中断 */
	__disable_irq();

	/* 关键修改2: 重定位中断向量表到App起始地址 */
	SCB->VTOR = FLASH_APP_BASE;   // App 起始地址，见 BootConfig.h

	/* 关键修改3: 清除所有可能残留的中断挂起标志 */
	for (uint32_t i = 0; i < 8; i++)
	{
		NVIC->ICPR[i] = 0xFFFFFFFF;
	}

	/* 确保操作完成 */
	__DSB();
	__ISB();

	/* 时钟配置 */
	systick_config();
	bsp_led_init();

	/* 关键修改4: 在所有外设初始化完成后才使能中断 */
	__enable_irq();
}



void app_upgrade_start(void){
	if(!app_upgrade_usart_recv_flag)
		return;
	while(1){
			if (app_upgrade_usart_recv_flag)
		{
			/* 关键修改5: 立即关闭中断，防止重复触发 */
			// __disable_irq();
			app_upgrade_usart_recv_flag = 0;

			uint32_t magic_value = 0;

			uint32_t tmp = app_upgrade_usart_tmp_buf[0];
			app_upgrade_usart_tmp_buf[0] = app_upgrade_usart_tmp_buf[1];
			app_upgrade_usart_tmp_buf[1] = tmp;

			tmp = app_upgrade_usart_tmp_buf[2];
			app_upgrade_usart_tmp_buf[2] = app_upgrade_usart_tmp_buf[3];
			app_upgrade_usart_tmp_buf[3] = tmp;

			//!0 2交换  1 3 交换
			tmp = app_upgrade_usart_tmp_buf[0];
			app_upgrade_usart_tmp_buf[0] = app_upgrade_usart_tmp_buf[2];
			app_upgrade_usart_tmp_buf[2] = tmp;

			tmp = app_upgrade_usart_tmp_buf[1];
			app_upgrade_usart_tmp_buf[1] = app_upgrade_usart_tmp_buf[3];
			app_upgrade_usart_tmp_buf[3] = tmp;

			tmp = app_upgrade_usart_tmp_buf[4];
			app_upgrade_usart_tmp_buf[4] = app_upgrade_usart_tmp_buf[5];
			app_upgrade_usart_tmp_buf[5] = tmp;

			tmp = app_upgrade_usart_tmp_buf[6];
			app_upgrade_usart_tmp_buf[6] = app_upgrade_usart_tmp_buf[7];
			app_upgrade_usart_tmp_buf[7] = tmp;

			tmp = app_upgrade_usart_tmp_buf[4];
			app_upgrade_usart_tmp_buf[4] = app_upgrade_usart_tmp_buf[6];
			app_upgrade_usart_tmp_buf[6] = tmp;

			tmp = app_upgrade_usart_tmp_buf[5];
			app_upgrade_usart_tmp_buf[5] = app_upgrade_usart_tmp_buf[7];
			app_upgrade_usart_tmp_buf[7] = tmp;

			memcpy(&magic_value , app_upgrade_usart_tmp_buf + 0 , 4);
			uint32_t appVersion = 0;
			memcpy(&appVersion , app_upgrade_usart_tmp_buf + 4 , 4);
			if (magic_value != 0x5AA5C33C)
			{
				printf("magic_value : 0x%08X\r\n" , magic_value);
				printf("update magic_value error!\r\n");
				printf("please check magic_value!\r\n");
				app_upgrade_usart_tmp_buf_len = 0;
				usart_interrupt_enable(APPUPGRADE_USART, USART_INT_IDLE);
				usart_interrupt_enable(APPUPGRADE_USART, USART_INT_RBNE);
				continue;
			}
			// my_param_sum.BootParam.appVersion = appVersion;
			printf("magic_value : 0x%08X\r\n" , magic_value);
			printf("appVersion : 0x%08X\r\n" , appVersion);

			app_upgrade_usart_tmp_buf_len -= 8;

			config_crc32 = crc32_calc(app_upgrade_usart_tmp_buf + 8 , app_upgrade_usart_tmp_buf_len);
			uint32_t app_crc32 = crc32_calc((uint8_t*)0x8011000 , 76 * 1024);

			printf("Received %d bytes, CRC32: 0x%08X , app_crc32 : 0x%08X\r\n" , app_upgrade_usart_tmp_buf_len , config_crc32 , app_crc32);

			/* Flash擦除操作 */
			for (uint8_t i = 0; i < 13; i++)
			{
				internal_flash_erase(DOWNLOAD_ADDR + i * 4 * 1024);
			}

			/* Flash写入操作 */
			internal_flash_write_str_Char(DOWNLOAD_ADDR , app_upgrade_usart_tmp_buf + 8 , app_upgrade_usart_tmp_buf_len);

			//!读出原来参数然后在次基础上进行修改
			for (uint16_t i = 0; i < CONFIG_SIZE; i++)
			{
				config_buf[i] = internal_flash_read_Char(PARAM_ADDR + i);
			}

			memcpy(&my_param_sum , config_buf , sizeof(Parameter_t));

			/* 更新备份区参数 */
			//!此时 备份区参数跟 应用区参数差一个版本的
			my_param_sum.BootParam_Reserved = my_param_sum.BootParam;

			// my_param_sum.BootParam.backupCRC32 = crc32_calc(tmp_str , sizeof(BootParam_t));
			my_param_sum.BootParam.appStartAddr = 0x8011000;
			my_param_sum.BootParam.appStackAddr = *(__IO uint32_t*)(my_param_sum.BootParam.appStartAddr + 0);	//栈顶地址
			my_param_sum.BootParam.appEntryAddr = *(__IO uint32_t*)(my_param_sum.BootParam.appStartAddr + 4);	// 应用程序入口地址在应用程序起始地址后4字节(需要手动指定中断向量表的位置)
			my_param_sum.BootParam.magicWord = 0x5AA5C33C;
			my_param_sum.BootParam.appVersion = appVersion;
			my_param_sum.BootParam.backupCRC32 = app_crc32;
			// printf("zzz my_param_sum.BootParam.appVersion : 0x%08X\r\n" , my_param_sum.BootParam.appVersion);
			printf("my_param_sum.BootParam.backupCRC32 : 0x%08X\r\n" , my_param_sum.BootParam.backupCRC32);
			delay_1ms(1000);


			my_param_sum.BootParam.updateFlag = 0x5A;
			my_param_sum.BootParam.updateStatus = 0x01;
			my_param_sum.BootParam.appSize = app_upgrade_usart_tmp_buf_len;
			my_param_sum.BootParam.appCRC32 = config_crc32;

			/* 写入参数到Flash */
			internal_flash_erase(PARAM_ADDR);
			memcpy(config_buf , &my_param_sum , sizeof(Parameter_t));
			internal_flash_write_str_Char(PARAM_ADDR , config_buf , sizeof(Parameter_t));

			app_upgrade_usart_tmp_buf_len = 0;

			// LED2_ON()
			/* 进行软复位 */
			mcu_software_reset();
		}

		printf("Hello World!\r\n");
		BSP_LED1_ON();
		delay_1ms(1000);
	}
}
uint32_t get_app_version(void)
{
    // 由于Flash在STM32/GD32中是内存映射的，可以直接通过指针读取
    const Parameter_t *flash_param = (const Parameter_t *)PARAM_ADDR;
    return flash_param->BootParam.appVersion;
}

/**
 * @brief 修改并保存新的应用版本号到Flash
 * @param version 新的版本号
 */
void set_app_version(uint32_t version)
{
    memcpy(&my_param_sum, (const void *)PARAM_ADDR, sizeof(Parameter_t));
	my_param_sum.BootParam.appVersion = version;

    internal_flash_erase(PARAM_ADDR);

    internal_flash_write_str_Char(PARAM_ADDR, (uint8_t *)&my_param_sum, sizeof(Parameter_t));
}

void set_app_id_version(uint16_t id){
	 memcpy(&my_param_sum, (const void *)PARAM_ADDR, sizeof(Parameter_t));
	my_param_sum.UserConfig.user_data[0] = (uint8_t)( id & 0xff);
	my_param_sum.UserConfig.user_data[1] = (uint8_t)((id >> 8)& 0xff);
	
	internal_flash_erase(PARAM_ADDR);

	internal_flash_write_str_Char(PARAM_ADDR, (uint8_t *)&my_param_sum, sizeof(Parameter_t));
}

uint16_t get_app_id_version(void){
	    // 由于Flash在STM32/GD32中是内存映射的，可以直接通过指针读取
    Parameter_t flash_param ={0};
	 memcpy(&flash_param, (const void *)PARAM_ADDR, sizeof(Parameter_t));
    return flash_param.UserConfig.user_data[0] | (flash_param.UserConfig.user_data[1] << 8);
}


void set_app_updateFlag(uint8_t flag){
	memcpy(&my_param_sum, (const void *)PARAM_ADDR, sizeof(Parameter_t));
	my_param_sum.BootParam.updateFlag = flag;
	
	internal_flash_erase(PARAM_ADDR);

	internal_flash_write_str_Char(PARAM_ADDR, (uint8_t *)&my_param_sum, sizeof(Parameter_t));
}

uint8_t get_app_updateFlag(void){
	const Parameter_t *flash_param = (const Parameter_t *)PARAM_ADDR;
    return flash_param->BootParam.updateFlag;
}



