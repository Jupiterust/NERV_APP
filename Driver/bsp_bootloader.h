#ifndef __BSP_BOOTLOADER_H_
#define __BSP_BOOTLOADER_H_

#include "bsp_sys.h"


// GD32F470VE 存储器映射 (注释保留，后续bootloader开发时参考)
//// Flash 
//#define BSP_BOOT_FLASH_BASE         0x08000000U
//#define BSP_BOOT_FLASH_SIZE         0x00080000U  // 512KB


//// SRAM (内置)
//#define BSP_BOOT_SRAM_BASE          0x20000000U
//#define BSP_BOOT_SRAM_SIZE          0x00030000U  // 192KB

//// SRAM2 (内置，与SRAM连续编址)
//#define BSP_BOOT_SRAM2_BASE         0x10000000U
//#define BSP_BOOT_SRAM2_SIZE         0x00010000U  // 64KB

//// 总SRAM = 192KB + 64KB = 256KB

#define BSP_BOOTLOADER_KEY_GPIO_RCU   RCU_GPIOD
#define BSP_BOOTLOADER_KEY_GPIO_PORT  GPIOD
#define BSP_BOOTLOADER_KEY_PIN        GPIO_PIN_11

#define BSP_BOOT_APP_START_ADDR        0x08010000U
 
#define MAGIC_ADDR        (*((volatile uint32_t *)0x2002FF00))
#define MAGIC_JUMP_TO_APP  0xDEADBEEF

void bsp_bootloader_key_init(void);
u8 bsp_bootloader_key_trigger(void);
void bsp_bootloader_jump_application(void);

#endif
