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

 /* ----------------------- Platform includes --------------------------------*/
#include "port.h"

/* ----------------------- Modbus includes ----------------------------------*/
#include "mb.h"
#include "mbport.h"
#include "timer.h"

/* ----------------------- static functions ---------------------------------*/
static void prvvTIMERExpiredISR(void);

/* ----------------------- Start implementation -----------------------------*/
BOOL
xMBPortTimersInit(USHORT usTim1Timerout50us)
{
	//!定时器初始化
	my_timer_init(usTim1Timerout50us * 50);
	return TRUE;
}


inline void
vMBPortTimersEnable()
{
	/* Enable the timer with the timeout passed to xMBPortTimersInit( ) */
	//!开启定时器更新中断
	timer_disable(TIMER6);
	timer_counter_value_config(TIMER6 , 0);
	timer_interrupt_flag_clear(TIMER6 , TIMER_INT_FLAG_UP); // ← 关键！先清标志
	timer_enable(TIMER6);
	timer_interrupt_enable(TIMER6 , TIMER_INT_UP);
}

inline void
vMBPortTimersDisable()
{
	/* Disable any pending timers. */
	//!关闭定时器更新中断
	timer_interrupt_disable(TIMER6 , TIMER_INT_UP);
	timer_interrupt_flag_clear(TIMER6 , TIMER_INT_FLAG_UP);
	timer_disable(TIMER6);                         // ← 添加：停止计数器
}

/* Create an ISR which is called whenever the timer has expired. This function
 * must then call pxMBPortCBTimerExpired( ) to notify the protocol stack that
 * the timer has expired.
 */
static void prvvTIMERExpiredISR(void)
{
	(void)pxMBPortCBTimerExpired();
}

void TIMER6_IRQHandler(void)
{
	if (timer_interrupt_flag_get(TIMER6 , TIMER_INT_UP) == SET)
	{
		// printf("t6 up\r\n");
		timer_interrupt_flag_clear(TIMER6 , TIMER_INT_UP);
		prvvTIMERExpiredISR();
	}
}


