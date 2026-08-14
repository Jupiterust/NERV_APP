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

#include "port.h"

 /* ----------------------- Modbus includes ----------------------------------*/
#include "mb.h"
#include "mbport.h"

#include "bsp_rs485.h"

/* ----------------------- static functions ---------------------------------*/
static void prvvUARTTxReadyISR(void);
static void prvvUARTRxISR(void);


BOOL xMBPortSerialInit(UCHAR ucPort, ULONG ulBaudRate, UCHAR ucDataBits, eMBParity eParity)
{

	my_usart1_init();
	usart_baudrate_set(BSP_RS485_USART, ulBaudRate);
	return TRUE;
}

/* ----------------------- Start implementation -----------------------------*/
void vMBPortSerialEnable(BOOL xRxEnable, BOOL xTxEnable)
{
	if (xRxEnable == TRUE)
		usart_interrupt_enable(BSP_RS485_USART, USART_INT_RBNE);
	else
		usart_interrupt_disable(BSP_RS485_USART, USART_INT_RBNE);

	if (xTxEnable == TRUE)
	{
		my_usart_485_CS(USARTX_485_Send);    // ← 拉高DE，切换到发送模式
		usart_interrupt_enable(BSP_RS485_USART, USART_INT_TBE);
	}
	else
	{
		// 等待最后一字节发送完成

		while ((usart_flag_get(BSP_RS485_USART, USART_FLAG_TC) == RESET))
		{
			__NOP();
		};
		my_usart_485_CS(USARTX_485_Receive); // ← 拉低DE，切换到接收模式
		usart_interrupt_disable(BSP_RS485_USART, USART_INT_TBE);
	}
}

// xMBPortSerialPutByte 去掉 RS485_Send，由 vMBPortSerialEnable 统一控制
BOOL xMBPortSerialPutByte(CHAR ucByte)
{
	usart_data_transmit(BSP_RS485_USART, ucByte);
	return TRUE;
}

// xMBPortSerialGetByte 去掉 RS485_Receive，由 vMBPortSerialEnable 统一控制
BOOL xMBPortSerialGetByte(CHAR* pucByte)
{
	*pucByte = usart_data_receive(BSP_RS485_USART);
	return TRUE;
}

/* Create an interrupt handler for the transmit buffer empty interrupt
 * (or an equivalent) for your target processor. This function should then
 * call pxMBFrameCBTransmitterEmpty( ) which tells the protocol stack that
 * a new character can be sent. The protocol stack will then call
 * xMBPortSerialPutByte( ) to send the character.
 */
static void prvvUARTTxReadyISR(void)
{
	pxMBFrameCBTransmitterEmpty();
}

/* Create an interrupt handler for the receive interrupt for your target
 * processor. This function should then call pxMBFrameCBByteReceived( ). The
 * protocol stack will then call xMBPortSerialGetByte( ) to retrieve the
 * character.
 */
static void prvvUARTRxISR(void)
{
	pxMBFrameCBByteReceived();
}


void USART1_IRQHandler(void)
{
	if (usart_interrupt_flag_get(BSP_RS485_USART, USART_INT_FLAG_RBNE))
	{
		//接收中断
		prvvUARTRxISR();
	}

	if (usart_interrupt_flag_get(BSP_RS485_USART, USART_INT_FLAG_TBE))
	{
		//发送中断
		prvvUARTTxReadyISR();
	}
}


