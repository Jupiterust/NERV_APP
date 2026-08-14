/*
 * FreeModbus Libary: BARE Port
 * Copyright (C) 2006 Christian Walter <wolti@sil.at>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * File: $Id$
 */

#ifndef _PORT_H
#define _PORT_H

#include <assert.h>
#include <inttypes.h>

#define	INLINE                      inline
#define PR_BEGIN_EXTERN_C           extern "C" {
#define	PR_END_EXTERN_C             }

#define ENTER_CRITICAL_SECTION( )   
#define EXIT_CRITICAL_SECTION( )    

typedef uint8_t BOOL;

typedef unsigned char UCHAR;
typedef char CHAR;

typedef uint16_t USHORT;
typedef int16_t SHORT;

typedef uint32_t ULONG;
typedef int32_t LONG;

#ifndef TRUE
#define TRUE            1
#endif

#ifndef FALSE
#define FALSE           0
#endif


/* 4 个寄存器缓冲区的容量。实际数值集中在 Function/modbus_data_map.h 的【3】里，
 * 比赛现场只改那一个文件，这里只是把名字接过来给 vendor 代码继续用旧名字。 */
#include "modbus_data_map.h"

#define REG_INPUT_SIZE   MB_IREG_COUNT
#define REG_HOLD_SIZE    MB_HREG_COUNT
#define REG_COILS_SIZE   MB_COIL_COUNT
#define REG_DISC_SIZE    MB_DISC_COUNT

typedef enum
{
	REG_INPUT = 0,
	REG_HOLD = 1,
	REG_COILS = 2,
	REG_DISC = 3,
}REG_TYPE;


//! 回调函数的函数指针类型（vendor 遗留，当前工程未使用）
typedef void (*FuncPtr)(uint16_t* , uint16_t* , uint16_t , uint8_t);

void Set_Register(REG_TYPE reg_type , uint16_t address , uint16_t* buf , uint16_t len);

#endif
