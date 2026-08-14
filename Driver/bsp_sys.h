#ifndef __BSP_SYS_H
#define __BSP_SYS_H




// 官方库
#include "gd32f4xx.h"
#include "gd32f4xx_libopt.h"
#include "systick.h"
#include "gd32f4xx_syscfg.h"
#include "gd32f4xx_gpio.h"
// 标准库
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
//定义一些常用的数据类型短关键字
#define  __IO volatile
#define  __O  volatile
	
// 空操作占位，用于逻辑分支中明确表示"此处有意为空"
#define pass ;
#define zero 0
#define ZERO 0

typedef int32_t  s32;
typedef int16_t s16;
typedef int8_t  s8;

typedef const int32_t sc32;
typedef const int16_t sc16;
typedef const int8_t sc8;


typedef __IO int32_t  vs32;
typedef __IO int16_t  vs16;
typedef __IO int8_t   vs8;

typedef __I int32_t vsc32;
typedef __I int16_t vsc16;
typedef __I int8_t vsc8;

typedef uint32_t  u32;
typedef uint16_t u16;
typedef uint8_t  u8;

typedef const uint32_t uc32;
typedef const uint16_t uc16;
typedef const uint8_t uc8;

typedef __IO uint32_t  vu32;
typedef __IO uint16_t vu16;
typedef __IO uint8_t  vu8;
typedef __IO int	vint;
typedef __IO long	vlong;
typedef __IO int16_t vint16_t;
typedef __IO int32_t vint32_t;
typedef __IO int64_t vint64_t;

typedef __I uint32_t vuc32;
typedef __I uint16_t vuc16;
typedef __I uint8_t vuc8;

//FatFt系统
#include "ff.h"
#include "diskio.h"
#include "sdcard.h"

// 解析库
#include "General_Protocol.h"

// 功能库
#include "Function.h"
#include "Log_recording_function.h"

// bsp库
#include "bsp_ADC.h"
#include "bsp_dac.h"
#include "bsp_debug.h"
#include "bsp_flash.h"
#include "bsp_rtc.h"
#include "bsp_led.h"
#include "bsp_rs485.h"
#include "bsp_oled.h"
#include "bsp_wkp.h"
#include "bsp_GD30AD3344.h"
#include "bsp_general_timer.h"
#include "bsp_sdio.h"
#include "bsp_key.h"


#endif