#include "bsp_oled.h"
#include "bsp_oledfont.h"
#include "bsp_oledpic.h"
#include "systick.h"

uint8_t initcmd1[] = {
	0xAE,		//display off
	0xD5, 0x80, //Set Display Clock Divide Ratio/Oscillator Frequency
	0xA8, 0x1F, //set multiplex Ratio
	0xD3, 0x00, //display offset
	0x40,		//set display start line
	0x8d, 0x14, //set charge pump
	0xa1,		//set segment remap
	0xc8,		//set com output scan direction
	0xda, 0x00, //set com pins hardware configuration
	0x81, 0x80, //set contrast control
	0xd9, 0x1f, //set pre-charge period
	0xdb, 0x40, //set vcom deselect level
	0xa4,		//Set Entire Display On/Off
	0xaf,		//set display on
};







/**
 * OLED writes commands and data functions
 * OLED writes commands, data functions, and changes the contents of these two functions if you want to migrate them to another development board
 * For example: I use the i2c2 interface, then you only need to change &hi2c1 to &hi2c2.
**/


void OLED_Write_cmd(uint8_t cmd)
{
	I2C_SoftWrite_Byte(0x00,cmd);
}


void OLED_Write_data(uint8_t data)
{
	I2C_SoftWrite_Byte(0x40,data);
}

/**
 * @brief	Image display function
 * @param x0  Image display start position x-axis
 * @param y0  Image display start position y-axis
 * @param x1  Image display end position x-axis 1 - 127
 * @param y1  Image display end position x-axis 1 - 4
 * @param BMP Image display pointer address
 * @note	The image needs to be converted to an array and passed into this function
*/
void OLED_ShowPic(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t BMP[])
{
	uint16_t i = 0;
	uint8_t x, y;
	for (y = y0; y < y1; y++)
	{
		OLED_Set_Position(x0, y);
		for (x = x0; x < x1; x++)
		{
			OLED_Write_data(BMP[i++]);
		}
	}
}

/**
 * @brief	Display a 16*16 pixel Chinese character
 * @param x  position x-axis  0 - 127
 * @param y  position y-axis  0 - 3
 * @param no  The order of the Chinese characters in the hzk[] array
 * @note	The Chinese character library is in the Hzk array in the oledfont.h file, 
 * You need to convert Chinese characters into arrays
*/
void OLED_ShowHanzi(uint8_t x, uint8_t y, uint8_t no)
{
	uint8_t t, adder = 0;
	OLED_Set_Position(x, y);
	for (t = 0; t < 16; t++)
	{
		OLED_Write_data(Hzk[2 * no][t]);
		adder += 1;
	}
	OLED_Set_Position(x, y + 1);
	for (t = 0; t < 16; t++)
	{
		OLED_Write_data(Hzk[2 * no + 1][t]);
		adder += 1;
	}
}

/**
 * @brief	Display a 32*32 pixel Chinese character .all screen display
 * @param x  position x-axis  0 - 127
 * @param y  position y-axis  0
 * @param n  The order of the Chinese characters in the Hzb[] array
 * @note	
*/
void OLED_ShowHzbig(uint8_t x, uint8_t y, uint8_t n)
{
	uint8_t t, adder = 0;
	OLED_Set_Position(x, y);
	for (t = 0; t < 32; t++)
	{
		OLED_Write_data(Hzb[4 * n][t]);
		adder += 1;
	}
	OLED_Set_Position(x, y + 1);
	for (t = 0; t < 32; t++)
	{
		OLED_Write_data(Hzb[4 * n + 1][t]);
		adder += 1;
	}

	OLED_Set_Position(x, y + 2);
	for (t = 0; t < 32; t++)
	{
		OLED_Write_data(Hzb[4 * n + 2][t]);
		adder += 1;
	}
	OLED_Set_Position(x, y + 3);
	for (t = 0; t < 32; t++)
	{
		OLED_Write_data(Hzb[4 * n + 3][t]);
		adder += 1;
	}
}

/**
 * @brief	Display a float 
 * @param x  position x-axis  0 - 127
 * @param y  position y-axis  0
 * @param num  The order of the Chinese characters in the Hzb[] array
 * @param accuracy Preserve decimal places
 * @param fontsize 8/16
 * @note	
*/
void OLED_ShowFloat(uint8_t x, uint8_t y, float num, uint8_t accuracy, uint8_t fontsize)
{
	uint8_t i = 0;
	uint8_t j = 0;
	uint8_t t = 0;
	uint8_t temp = 0;
	uint16_t numel = 0;
	uint32_t integer = 0;
	float decimals = 0;

	//Is a negative number?
	if (num < 0)
	{
		OLED_ShowChar(x, y, '-', fontsize);
		num = 0 - num;
		i++;
	}

	integer = (uint32_t)num;
	decimals = num - integer;

	//Integer part
	if (integer)
	{
		numel = integer;

		while (numel)
		{
			numel /= 10;
			j++;
		}
		i += (j - 1);
		for (temp = 0; temp < j; temp++)
		{
			OLED_ShowChar(x + 8 * (i - temp), y, integer % 10 + '0', fontsize); // 显示整数部分
			integer /= 10;
		}
	}
	else
	{
		OLED_ShowChar(x + 8 * i, y, temp + '0', fontsize);
	}
	i++;
	//Decimal part
	if (accuracy)
	{
		OLED_ShowChar(x + 8 * i, y, '.', fontsize);

		i++;
		for (t = 0; t < accuracy; t++)
		{
			decimals *= 10;
			temp = (uint8_t)decimals;
			OLED_ShowChar(x + 8 * (i + t), y, temp + '0', fontsize);
			decimals -= temp;
		}
	}
}

/**
 * @brief	OLED pow function
 * @param m - base
 * @param n - exponent
 * @return result
*/
static uint32_t OLED_Pow(uint8_t a, uint8_t n)
{
	uint32_t result = 1;
	while (n--)
	{
		result *= a;
	}
	return result;
}

/**
 * @brief	Display a uint32 Interger
 * @param x  position x-axis  0 - 127
 * @param y  position y-axis  0 - 3
 * @param num  Displayed integers
 * @param length Number of integer digits
 * @note	
*/
void OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t length, uint8_t fontsize)
{

	uint8_t t, temp;
	uint8_t enshow = 0;
	for (t = 0; t < length; t++)
	{
		temp = (num / OLED_Pow(10, length - t - 1)) % 10;
		if (enshow == 0 && t < (length - 1))
		{
			if (temp == 0)
			{
				OLED_ShowChar(x + (fontsize / 2) * t, y, ' ', fontsize);
				continue;
			}
			else
				enshow = 1;
		}
		OLED_ShowChar(x + (fontsize / 2) * t, y, temp + '0', fontsize);
	}
}


/**
 * @brief  显示ASCII字符串
 * @param x  起点的X坐标 (0-127)
 * @param y  起点的页坐标 (0-3)
 * @param ch 字符串指针
 * @param fontsize 字号 (8 或 16)
**/
void OLED_ShowStr(uint8_t x, uint8_t y, char *ch, uint8_t fontsize)
{
    uint8_t j = 0;
    uint8_t line_step = (fontsize == 16) ? 2 : 1; // 16字号占2页，8字号占1页

    while (ch[j] != '\0')
    {
        // 边界检查
        if (x > 120) 
        {
            x = 0;
            y += line_step;
        }

        // 2. 9边界检查：如果 Y 超过了屏幕范围 (0.91寸最大Page为3)
        if (y > 3) 
        {
            break; // 或者选择 x=0; y=0; 覆盖开头，视需求而定
        }

        OLED_ShowChar(x, y, ch[j], fontsize);

        // 3. 更新坐标：16字号通常宽8，8字号通常宽6或8，这里以8为例
        x += 8; 
        j++;
    }
}

/**
 * @brief	Displays ASCII characters
 * @param x  Character position on the X-axis  range：0 - 127
 * @param y  Character position on the Y-axis  range：0 - 3 
 * @param no  character
 * @param fontsize You can choose from three fonts 8/16
**/
void OLED_ShowChar(uint8_t x, uint8_t y, uint8_t ch, uint8_t fontsize)
{
	uint8_t c = 0, i = 0;
	c = ch - ' ';

	if (x > 127) //beyond the right boundary
	{
		x = 0;
		y++;
	}

	if (fontsize == 16)
	{
		OLED_Set_Position(x, y);
		for (i = 0; i < 8; i++)
		{
			OLED_Write_data(F8X16[c * 16 + i]);
		}
		OLED_Set_Position(x, y + 1);
		for (i = 0; i < 8; i++)
		{
			OLED_Write_data(F8X16[c * 16 + i + 8]);
		}
	}
	else
	{
		OLED_Set_Position(x, y);
		for (i = 0; i < 6; i++)
		{
			OLED_Write_data(F6X8[c][i]);
		}
	}
}


/**
 * OLED fill function, after using the function 0.91 inch oled screen into full white
**/
void OLED_Allfill(void)
{
	uint8_t i, j;
	for (i = 0; i < 4; i++)
	{
		OLED_Write_cmd(0xb0 + i);
		OLED_Write_cmd(0x00);
		OLED_Write_cmd(0x10);
		for (j = 0; j < 128; j++)
		{
			OLED_Write_data(0xFF);
		}
	}
}

/**
 * @brief Set coordinates
 * @param x: X position, range 0 - 127  Because our OLED screen resolution is 128*32, so the horizontal is 128 pixels
 * @param y: Y position, range 0 - 3    Because the vertical pixels are positioned in pages, each page has 8 pixels, so there are 4 pages
**/
void OLED_Set_Position(uint8_t x, uint8_t y)
{
	OLED_Write_cmd(0xb0 + y);
	OLED_Write_cmd(((x & 0xf0) >> 4) | 0x10);
	OLED_Write_cmd((x & 0x0f) | 0x00);
}
/**
 * Clear Screen Function
 * Fill each row and column with 0
**/
void OLED_Clear(void)
{
	uint8_t i, n;
	for (i = 0; i < 4; i++)
	{
		OLED_Write_cmd(0xb0 + i);
		OLED_Write_cmd(0x00);
		OLED_Write_cmd(0x10);
		for (n = 0; n < 128; n++)
		{
			OLED_Write_data(0);
		}
	}
}
/**
 * Turn screen display on and off
**/
void OLED_Display_On(void)
{
	OLED_Write_cmd(0x8D);
	OLED_Write_cmd(0x14);
	OLED_Write_cmd(0xAF);
}
void OLED_Display_Off(void)
{
	OLED_Write_cmd(0x8D);
	OLED_Write_cmd(0x10);
	OLED_Write_cmd(0xAF);
}


/**
 * @brief  类似printf的格式化字符串OLED显示函数
 * @param  x        起点的X坐标 (0-127)
 * @param  y        起点的页坐标 (0-3)
 * @param  fontsize 字号 (8 或 16)
 * @param  format   格式化字符串 (如 "Temp: %.2f C")
 * @param  ...      可变参数
 */
void OLED_Printf(uint8_t x, uint8_t y, uint8_t fontsize, const char *format, ...)
{
    char buffer[64]; // 定义字符缓冲区（64字节对于0.91寸屏幕一屏的字符足够了）
    va_list args;    // 定义可变参数列表

    //初始化可变参数列表
    va_start(args, format);

    //将格式化字符串和参数安全地打印到 buffer 中，防止数组越界
    vsnprintf(buffer, sizeof(buffer), format, args);

    
    va_end(args);
    OLED_ShowStr(x, y, buffer, fontsize);
}






// 初始化
void bsp_oledHardware_init(void)
{
	/* 时钟初始化 */
	rcu_periph_clock_enable(OLED_SCL_RCU);
	rcu_periph_clock_enable(OLED_SDA_RCU);
	rcu_periph_clock_enable(OLED_HardwareRCU);
	
	/* 复用配置 */
	gpio_af_set(OLED_SCL_PORT,GPIO_AF_4,OLED_SCL_PIN);
	gpio_af_set(OLED_SDA_PORT,GPIO_AF_4,OLED_SDA_PIN);
	
	/* 配置GPIO模式 */
	gpio_mode_set(OLED_SCL_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, OLED_SCL_PIN);//SCL
	gpio_output_options_set(OLED_SCL_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_MAX, OLED_SCL_PIN);
	
	gpio_mode_set(OLED_SDA_PORT,GPIO_MODE_AF,GPIO_PUPD_PULLUP,OLED_SDA_PIN);//SDA
	gpio_output_options_set(OLED_SDA_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_MAX, OLED_SDA_PIN);
	
	/* 配置I2C参数 */
	i2c_clock_config(OLED_HardwareRCU, 100000, I2C_DTCY_2);
	i2c_mode_addr_config(OLED_HardwareRCU,I2C_I2CMODE_ENABLE,I2C_ADDFORMAT_7BITS,Host_Address);
	i2c_enable(OLED_HardwareRCU);
	/* 空应答 */
	i2c_ack_config(OLED_HardwareRCU, I2C_ACK_ENABLE);
	delay_1ms(500);
	uint8_t i;
	for (i = 0; i < sizeof(initcmd1); i++)
	{
		OLED_Write_cmd(initcmd1[i]); //display off
	}
	OLED_Clear();
	OLED_Set_Position(0, 0);
}


void bsp_oledSoftware_Init(void)
{
	 // 使能 GPIO 时钟
    rcu_periph_clock_enable(OLED_SCL_RCU);
    rcu_periph_clock_enable(OLED_SDA_RCU);

    // 配置 SCL 和 SDA 为 开漏输出
    gpio_mode_set(OLED_SCL_PORT,GPIO_MODE_OUTPUT,GPIO_PUPD_NONE,OLED_SCL_PIN);
	gpio_output_options_set(OLED_SCL_PORT,GPIO_OTYPE_OD,GPIO_OSPEED_MAX,OLED_SCL_PIN);
	
    gpio_mode_set(OLED_SDA_PORT,GPIO_MODE_OUTPUT,GPIO_PUPD_NONE,OLED_SDA_PIN);
	gpio_output_options_set(OLED_SDA_PORT,GPIO_OTYPE_OD,GPIO_OSPEED_MAX,OLED_SDA_PIN);

    //初始状态释放总线（保持高电平）
    SCL_H;
    SDA_H;
	delay_1ms(500);
	uint8_t i;
	for (i = 0; i < sizeof(initcmd1); i++)
	{
		OLED_Write_cmd(initcmd1[i]); //display off
	}
	OLED_Clear();
	OLED_Set_Position(0, 0);
}










// IIC使用的函数
static void i2c_delay(void) {
    uint32_t i = 100; 
    while(i--);
}
	

void i2c_start(void) 
{
    SDA_H;
    SCL_H;
    i2c_delay();
    SDA_L; // SCL高电平时，SDA产生下降沿
    i2c_delay();
    SCL_L; // 钳住总线，准备收发数据
}


void i2c_stop(void) 
{
    SDA_L;
    SCL_H; // SCL高电平时，SDA产生上升沿
    i2c_delay();
    SDA_H;
    i2c_delay();
}

uint8_t i2c_wait_ack(void) 
{
    uint8_t timeout = 0;
    SDA_H; // 释放SDA，准备接收ACK
    i2c_delay();
    SCL_H;
    i2c_delay();
    
    while(SDA_READ) {
        timeout++;
        if(timeout > 250) {
            i2c_stop();
            return 1; // 接收ACK失败
        }
    }
    SCL_L;
    return 0; // 接收ACK成功
}

void i2c_write_byte(uint8_t data) 
{
    for(uint8_t i = 0; i < 8; i++) {
        if(data & 0x80) SDA_H;
        else SDA_L;
        
        data <<= 1;
        i2c_delay();
        SCL_H;
        i2c_delay();
        SCL_L;
    }
}

uint8_t i2c_read_byte(uint8_t ack) 
{
    uint8_t receive = 0;
    SDA_H; // 切换为读取状态（开漏模式下直接拉高即可读取外部电平）
    
    for(uint8_t i = 0; i < 8; i++) {
        i2c_delay();
        SCL_H;
        receive <<= 1;
        if(SDA_READ) receive++;
        i2c_delay();
        SCL_L;
    }
    
    // 发送应答或非应答
    if(ack) SDA_L;
    else SDA_H;
    i2c_delay();
    SCL_H;
    i2c_delay();
    SCL_L;
    
    return receive;
}


// 在 oled.c 中，把原来的硬件 I2C 写入函数替换为：
void I2C_SoftWrite_Byte(uint8_t reg, uint8_t data)
{
    i2c_start();                 // 产生起始信号
    
    i2c_write_byte(OLED_ADDR);   // 发送设备地址(0x78)
    i2c_wait_ack();              // 等待应答
    
    i2c_write_byte(reg);         // 发送控制字节(0x00或0x40)
    i2c_wait_ack();              // 等待应答
    
    i2c_write_byte(data);        // 发送数据字节
    i2c_wait_ack();              // 等待应答
    
    i2c_stop();                  // 产生停止信号
}










