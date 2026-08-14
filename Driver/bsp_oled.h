#ifndef __bsp_iic_H_
#define __bsp_iic_H_
#include "bsp_sys.h"

#include <stdarg.h>   // 提供 va_list, va_start, va_end 的定义
#include <stdio.h>    // 提供 vsnprintf 函数的定义

//主要变量
#define OLED_SCL_RCU				RCU_GPIOB
#define OLED_SCL_PORT				GPIOB
#define OLED_SCL_PIN    			GPIO_PIN_8

#define OLED_SDA_RCU				RCU_GPIOB
#define OLED_SDA_PORT   			GPIOB
#define OLED_SDA_PIN				GPIO_PIN_9

#define OLED_ADDR 					0x78

#define OLED_WIDTH 128
#define OLED_HEIGHT 32
//硬件变量
#define OLED_HardwareRCU			RCU_I2C0
#define Host_Address 				0x00


//软件变量
#define SCL_H    					gpio_bit_set(OLED_SCL_PORT, OLED_SCL_PIN)
#define SCL_L    					gpio_bit_reset(OLED_SCL_PORT, OLED_SCL_PIN)

#define SDA_H    					gpio_bit_set(OLED_SDA_PORT, OLED_SDA_PIN)
#define SDA_L    					gpio_bit_reset(OLED_SDA_PORT, OLED_SDA_PIN)

#define SDA_READ 					gpio_input_bit_get(OLED_SDA_PORT, OLED_SDA_PIN)


//IIC的底层函数
void i2c_start(void);
void i2c_stop(void);
void i2c_write_byte(uint8_t data);
uint8_t i2c_read_byte(uint8_t ack);
uint8_t i2c_wait_ack(void);
void I2C_SoftWrite_Byte(uint8_t reg, uint8_t data);
void I2C_HardWrite_Byte(uint8_t reg, uint8_t data);
//OLED的函数
void bsp_oledHardware_init(void);
void bsp_oledSoftware_Init(void);
void OLED_Write_cmd(uint8_t cmd);
void OLED_Write_data(uint8_t data);
void OLED_ShowPic(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t BMP[]);
void OLED_ShowHanzi(uint8_t x, uint8_t y, uint8_t no);
void OLED_ShowHzbig(uint8_t x, uint8_t y, uint8_t n);
void OLED_ShowFloat(uint8_t x, uint8_t y, float num, uint8_t accuracy, uint8_t fontsize);
void OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t length, uint8_t fontsize);
void OLED_ShowStr(uint8_t x, uint8_t y, char *ch, uint8_t fontsize);
void OLED_ShowChar(uint8_t x, uint8_t y, uint8_t ch, uint8_t fontsize);
void OLED_Allfill(void);
void OLED_Set_Position(uint8_t x, uint8_t y);
void OLED_Clear(void);
void OLED_Display_On(void);
void OLED_Display_Off(void);
void OLED_Init(void);
void OLED_Printf(uint8_t x, uint8_t y, uint8_t fontsize, const char *format, ...);
#endif
