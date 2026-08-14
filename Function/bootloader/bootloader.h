#ifndef __BOOTLOADER_H__
#define __BOOTLOADER_H__

#include "bsp_sys.h"
#include "BootConfig.h"

#include <stdbool.h>
#include "rom.h"

#define PARAM_AREA_ADDR      0x08010000     // 参数区
#define APP_START_ADDR       0x08011000     // App运行区起始地址
#define APP_BACKUP_ADDR      0x08031000     // App备份区起始地址
#define APP_DOWNLOAD_ADDR    0x08051000     // 固件暂存(下载)区域

#define APP_MAX_SIZE         (128 * 1024)   // APP最大允许空间: 128KB
#define APP_MAX_PAGES        32             // 128KB对应的Flash页数(假设页大小为4KB)
// ========================================================

// CONFIG STRUCT SIZE
#define CONFIG_SIZE 1024*4			// 主参数区大小 4k
// CONFIG APP SIZE
#define CONFIG_APP_SIZE 1024*32   	// 缓存数组大小 32k

void bootloader_start(void);

#endif