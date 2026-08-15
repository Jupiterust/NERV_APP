#include "bootloader.h"
// CONFIG STRUCT SIZE
#define CONFIG_SIZE 1024*4
// CONFIG APP SIZE
#define CONFIG_APP_SIZE 1024*20

/* APP_DOWNLOAD_ADDR / BOOT_CONFIG_ADDR 原来这里各自重复定义了一份、且和真正
 * 生效的值不一样（0x8073000 vs bootloader.h 的 0x08051000，0x0800C000 vs
 * BootConfig.h 的 0x08010000），本文件从没被编译过所以没暴露出来。已删掉这两行
 * 局部重复定义——本文件已经 #include "bootloader.h"，它自己定义了正确的
 * APP_DOWNLOAD_ADDR，又 #include 了 BootConfig.h 带来正确的 BOOT_CONFIG_ADDR，
 * 两个名字沿用下面的代码不用改。地址真正的唯一来源见 Driver/BootConfig/BootConfig.h。 */

typedef struct __attribute__((packed)) Parameter_SUM
{
	BootParam_t BootParam;
	BootParam_t BootParam_Reserved;
	UpdateLog_t UpdateLog;
	UserConfig_t UserConfig;
	CalibData_t CalibData;
}Parameter_t;

static Parameter_t my_param_sum = { 0 };

static uint8_t config_buf[CONFIG_APP_SIZE] = { 0 };

typedef void (*pFunction)(void);

pFunction jump2app;



static void Analysis_ConfigForAddr(void)
{
	for (uint16_t i = 0; i < 1024 * 4; i++)
	{
		config_buf[i] = internal_flash_read_Char(BOOT_CONFIG_ADDR + i);
	}
}

uint32_t crc32_calc(uint8_t* data , uint32_t len)
{
	uint32_t crc = 0xFFFFFFFF;
	uint32_t i , j;

	for (i = 0; i < len; i++)
	{
		crc ^= data[i];
		for (j = 0; j < 8; j++)
		{
			if (crc & 1)
				crc = (crc >> 1) ^ 0xEDB88320;
			else
				crc >>= 1;
		}
	}

	return crc ^ 0xFFFFFFFF;
}

void mcu_software_reset(void)
{
	/* set FAULTMASK */
	__set_FAULTMASK(1);
	NVIC_SystemReset();
}


void iap_load_app(uint32_t appxaddr)
{
	if (((*(__IO uint32_t*)appxaddr) & 0x2FFE0000) == 0x20000000)//check if it is legal
	{
		/* 关闭全局中断 */
		__disable_irq();

		/* 关闭 SysTick */
		SysTick->CTRL = 0;
		SysTick->LOAD = 0;
		SysTick->VAL = 0;

		/* 清除所有 NVIC 中断使能和挂起标志 */
		for (uint32_t i = 0; i < 8; i++)
		{
			NVIC->ICER[i] = 0xFFFFFFFF;
			NVIC->ICPR[i] = 0xFFFFFFFF;
		}

		__DSB();
		__ISB();


		SCB->VTOR = appxaddr;


		__set_MSP(*(__IO uint32_t*)appxaddr);


		jump2app = (pFunction)(*(__IO uint32_t*)(appxaddr + 4));
		jump2app();

		printf("jump to app fail\r\n");
		while (1);
	}
}

static bool Download_Transport(uint32_t DownLoad_Addr)
{
	printf("BootLoader : appSize:%d\r\n" , my_param_sum.BootParam.appSize);
	if (my_param_sum.BootParam.appSize == 0)
	{
		return false;
	}
	//!App固件总大小为 3 * 4 + 64 + 128 + 128 = 332KB
	//!先擦除3页(3 * 4 = 12kb)
	for (uint8_t i = 0; i < 3; i++)
	{
		internal_flash_erase(my_param_sum.BootParam.appStartAddr + i * 4 * 1024);
	}
	// printf("qqq\r\n");
	//!擦除4
	// internal_flash_earse_sector(0x8010000);
	// delay_1ms(500);
	// printf("aaa\r\n");
	//!擦除5
	// internal_flash_earse_sector(0x8020000);
	// delay_1ms(1000);
	// printf("eee\r\n");
	//!擦除6
	// internal_flash_earse_sector(0x8040000);
	// delay_1ms(1000);
	// printf("zzz\r\n");
	// //!擦除完成后，需要把下载区的数据拷贝到应用程序运行的App的地址
	memset(config_buf , 0 , sizeof(config_buf));

	for (uint16_t i = 0; i < my_param_sum.BootParam.appSize; i++)
	{
		config_buf[i] = internal_flash_read_Char(APP_DOWNLOAD_ADDR + i);
	}
	internal_flash_write_str_Char(my_param_sum.BootParam.appStartAddr , config_buf , my_param_sum.BootParam.appSize);

	uint32_t app_crc32 = crc32_calc(config_buf , my_param_sum.BootParam.appSize);

	printf("BootLoader : appCRC32:0x%08x , app_crc32:0x%08x\r\n" , my_param_sum.BootParam.appCRC32 , app_crc32);
	if (app_crc32 == my_param_sum.BootParam.appCRC32)
	{
		printf("app crc32 check pass\r\n");
		return true;
	}
	else
	{
		printf("app crc32 check fail\r\n");
		return false;
	}
}

bool Backup_App(void)
{
	uint32_t crc32 = crc32_calc(((uint8_t*)0x800D000) , 76 * 1024);
	printf("BootLoader : backupCRC32:0x%08x , crc32:0x%08x\r\n" , my_param_sum.BootParam.backupCRC32 , crc32);
	if (crc32 != my_param_sum.BootParam.backupCRC32)
	{
		printf("BootLoader : Backup_App crc32 check fail\r\n");
		return false;
	}

	for (uint8_t i = 0; i < 32; i++)
	{
		internal_flash_erase(0x8020000 + i * 1024 * 4);
		delay_1ms(30);
	}

	printf("BootLoader : Backup_App erase done\r\n");

	internal_flash_write_str_Char(0x8020000 , (uint8_t*)0x800D000 , 76 * 1024);

	uint32_t backupCRC32_1 = crc32_calc(((uint8_t*)0x8020000) , 76 * 1024);
	if (backupCRC32_1 != my_param_sum.BootParam.backupCRC32)
	{
		printf("ZZZZBootLoader : Backup_App crc32 check fail\r\n");
		return false;
	}
	else
	{
		printf("ZZZZBootLoader : Backup_App crc32 check pass\r\n");
	}

	//!更新备份的版本号
	for (uint16_t i = 0; i < CONFIG_SIZE; i++)
	{
		config_buf[i] = internal_flash_read_Char(BOOT_CONFIG_ADDR + i);
	}
	memcpy(&my_param_sum , config_buf , sizeof(Parameter_t));
	//!更新版本号
	printf("BootLoader : Backup_App version:0x%08x -> 0x%08x \r\n" , my_param_sum.BootParam_Reserved.appVersion , my_param_sum.BootParam.appVersion);
	my_param_sum.BootParam_Reserved.appVersion = my_param_sum.BootParam.appVersion;
	memcpy(config_buf , &my_param_sum , sizeof(Parameter_t));
	//!写入
	internal_flash_erase(BOOT_CONFIG_ADDR);
	internal_flash_write_str_Char(BOOT_CONFIG_ADDR , config_buf , sizeof(Parameter_t));
	printf("BootLoader : Backup_App version : 0x%08x\r\n" , my_param_sum.BootParam_Reserved.appVersion);
	return true;
}

void jump_to_app(void)
{
	// It's better to disable IRQs before jump
	if ((my_param_sum.BootParam.appEntryAddr & 0xFF000000) == 0x8000000)		//judge if the app code is legal
	{
		iap_load_app(my_param_sum.BootParam.appStartAddr);//run FLASH APP
	}
	else
	{

		for (uint16_t i = 0; i < CONFIG_SIZE; i++)
		{
			config_buf[i] = internal_flash_read_Char(BOOT_CONFIG_ADDR + i);
		}
		memcpy(&my_param_sum , config_buf , sizeof(Parameter_t));

		my_param_sum.BootParam.bootFailCount++;
		printf("start false count : %d \r\n" , my_param_sum.BootParam.bootFailCount);
		memcpy(config_buf , &my_param_sum , sizeof(Parameter_t));
		internal_flash_erase(BOOT_CONFIG_ADDR);
		internal_flash_write_str_Char(BOOT_CONFIG_ADDR , config_buf , CONFIG_SIZE);

		mcu_software_reset();
		while (1);
	}

}


void bootloader_start(void){
	
	printf("BootLoader : start compare config\r\n");
	Analysis_ConfigForAddr();
	memcpy(&my_param_sum , config_buf , sizeof(Parameter_t));

	delay_1ms(100);


	printf("BootLoader : appVersion:0x%08x\r\n" , my_param_sum.BootParam.appVersion);
	printf("BootLoader : appVersion_Reserved:0x%08x\r\n" , my_param_sum.BootParam_Reserved.appVersion);

	if (my_param_sum.BootParam.appVersion != my_param_sum.BootParam_Reserved.appVersion)
	{
		//!发现App版本发生变化
		bool Backup_Result = Backup_App();
		if (Backup_Result == false)
		{
			printf("BootLoader : Backup_App failed\r\n");
		}
		else
		{
			printf("BootLoader : Backup_App success\r\n");
		}
	}
	else
	{
		printf("BootLoader : appVersion is same\r\n");
	}


	my_param_sum.BootParam.appStartAddr = 0x800D000;
	my_param_sum.BootParam.appStackAddr = *(__IO uint32_t*)(my_param_sum.BootParam.appStartAddr + 0);	//栈顶地址
	my_param_sum.BootParam.appEntryAddr = *(__IO uint32_t*)(my_param_sum.BootParam.appStartAddr + 4);	// 应用程序入口地址，应用程序起始地址加4字节(需要手动指定中断向量表的位置)


	//!用来测试App升级失败的开关  0--关   1--开
#if Switch_App_Update_Fail_Test

	my_param_sum.BootParam.updateStatus = 0x01;
	my_param_sum.BootParam.updateFlag = 0x5A;

	//故意写一个错误的地址，模拟跳转失败
	my_param_sum.BootParam.appStartAddr = 0x810D000;
	my_param_sum.BootParam.appStackAddr = *(__IO uint32_t*)(my_param_sum.BootParam.appStartAddr + 0);	//栈顶地址
	my_param_sum.BootParam.appEntryAddr = *(__IO uint32_t*)(my_param_sum.BootParam.appStartAddr + 4);	// 应用程序入口地址，应用程序起始地址加4字节(需要手动指定中断向量表的位置)
	printf("bootFailCount:%d\r\n" , my_param_sum.BootParam.bootFailCount);

#else	
	//A分区应用程序起始地址已经写死了，同样情况下B分区应用程序起始地址也写死了，如果需要修改A分区应用程序起始地址，只需要修改这里的地址即可
	my_param_sum.BootParam.appStartAddr = 0x800D000;
	my_param_sum.BootParam.appStackAddr = *(__IO uint32_t*)(my_param_sum.BootParam.appStartAddr + 0);	//栈顶地址
	my_param_sum.BootParam.appEntryAddr = *(__IO uint32_t*)(my_param_sum.BootParam.appStartAddr + 4);	// 应用程序入口地址，应用程序起始地址加4字节(需要手动指定中断向量表的位置)

#endif

	if (my_param_sum.BootParam.bootFailCount == 0xFFFF)
	{
		my_param_sum.BootParam.bootFailCount = 0;
	}

	if (my_param_sum.BootParam.bootFailCount >= 5)
	{
		printf("jump false , 版本回滚\r\n");

		for (uint16_t i = 0; i < CONFIG_SIZE; i++)
		{
			config_buf[i] = internal_flash_read_Char(BOOT_CONFIG_ADDR + i);
		}
		memcpy(&my_param_sum , config_buf , sizeof(Parameter_t));

		my_param_sum.BootParam.bootFailCount = 0;
		my_param_sum.BootParam.appVersion = my_param_sum.BootParam_Reserved.appVersion;
		my_param_sum.BootParam.updateStatus = 0x00;
		my_param_sum.BootParam.updateFlag = 0x00;	
		//!更新一页
		memcpy(config_buf , &my_param_sum , sizeof(Parameter_t));
		internal_flash_erase(BOOT_CONFIG_ADDR);
		internal_flash_write_str_Char(BOOT_CONFIG_ADDR , config_buf , CONFIG_SIZE);


		my_param_sum.BootParam.appStartAddr = 0x800D000;
		my_param_sum.BootParam.appStackAddr = *(__IO uint32_t*)(my_param_sum.BootParam.appStartAddr + 0);	//栈顶地址
		my_param_sum.BootParam.appEntryAddr = *(__IO uint32_t*)(my_param_sum.BootParam.appStartAddr + 4);	// 应用程序入口地址，应用程序起始地址加4字节(需要手动指定中断向量表的位置)


		//将备份区的数据搬到App区
		//!擦除旧版本
		for (uint8_t i = 0;i < 19; i++)
		{
			internal_flash_erase(0x800D000 + i * 4 * 1024);
		}
		internal_flash_write_str_Char(0x800D000 , (uint8_t*)0x8020000 , 76 * 1024);
	}


	printf("BootLoader : appStackAddr:0x%08x\r\n" , my_param_sum.BootParam.appStackAddr);
	printf("BootLoader : appEntryAddr:0x%08x\r\n" , my_param_sum.BootParam.appEntryAddr);
	printf("BootLoader : appStartAddr:0x%08x\r\n" , my_param_sum.BootParam.appStartAddr);

	printf("BootLoader : appVersion:0x%08x\r\n" , my_param_sum.BootParam.appVersion);
	printf("BootLoader : updateStatus:0x%02x\r\n" , my_param_sum.BootParam.updateStatus);
	printf("BootLoader : updateFlag:0x%02x\r\n" , my_param_sum.BootParam.updateFlag);
	printf("BootLoader : magicWord:0x%08x\r\n" , my_param_sum.BootParam.magicWord);
	delay_1ms(1000);


	if (my_param_sum.BootParam.magicWord == 0xffffffff){
		while(1){
			BSP_LED1_TOGGLE();
			delay_1ms(100);
			
		}
			
	}
	if (my_param_sum.BootParam.magicWord != 0x5AA5C33C)
	{
		printf("BootLoader : param magic is false\r\n");
		goto BootJump;
	}

	//!此时需要把下载的数据地址拷贝到应用程序运行的App的地址
	if (my_param_sum.BootParam.updateStatus == 0x01 && my_param_sum.BootParam.updateFlag == 0x5A)
	{
		printf("BootLoader : app is updating, need to copy param to app addr\r\n");

		bool Download_Transport_Result = Download_Transport(APP_DOWNLOAD_ADDR);

#if Switch_App_Update_Fail_Test
		Download_Transport_Result = FALSE;
#endif

		for (uint32_t i = 0; i < CONFIG_SIZE; i++)
		{
			config_buf[i] = internal_flash_read_Char(BOOT_CONFIG_ADDR + i);
		}
		memcpy(&my_param_sum , config_buf , sizeof(Parameter_t));

		if (Download_Transport_Result == true)
		{
			my_param_sum.BootParam.appStartAddr = 0x800D000;
			my_param_sum.BootParam.appStackAddr = *(__IO uint32_t*)(my_param_sum.BootParam.appStartAddr + 0);	//栈顶地址
			my_param_sum.BootParam.appEntryAddr = *(__IO uint32_t*)(my_param_sum.BootParam.appStartAddr + 4);	// 应用程序入口地址，应用程序起始地址加4字节(需要手动指定中断向量表的位置)
			my_param_sum.BootParam.updateStatus = 0x00;
			my_param_sum.BootParam.updateFlag = 0x00;
			my_param_sum.BootParam.updateCount++;
			// my_param_sum.BootParam.appVersion++;

			printf("BootLoader : app update success\r\n");
		}
		else
		{
			printf("BootLoader : app update fail\r\n");
			my_param_sum.BootParam.resetCount++;
			my_param_sum.BootParam.bootFailCount++;
		}
		//!更新一页
		memcpy(config_buf , &my_param_sum , sizeof(Parameter_t));
		internal_flash_erase(BOOT_CONFIG_ADDR);
		internal_flash_write_str_Char(BOOT_CONFIG_ADDR , config_buf , CONFIG_SIZE);
		mcu_software_reset();
	}
	else
	{
	BootJump:
		jump_to_app();
	}

	while (1)
	{

	}
}

