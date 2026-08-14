#ifndef __BSP_FLASH_H
#define __BSP_FLASH_H

#include "bsp_sys.h"
// BSP_FLASH_SPI引脚和相关参数宏定义
#define BSP_FLASH_SPI_RCU       RCU_SPI0
#define BSP_FLASH_SPI_GPIO_RCU  RCU_GPIOB
#define BSP_FLASH_SPI_PORT      GPIOB
#define BSP_FLASH_SPI_CS_RCU   RCU_GPIOA
#define BSP_FLASH_SPI            SPI0
// SPI0_CLK(PB3), SPI0_MISO(PB4), SPI0_MOSI(PB5)
#define BSP_FLASH_SPI_SCK_PIN   GPIO_PIN_3
#define BSP_FLASH_SPI_MISO_PIN  GPIO_PIN_4
#define BSP_FLASH_SPI_MOSI_PIN  GPIO_PIN_5
// SPI0_CS(PB6) 
#define BSP_FLASH_SPI_CS_PORT  GPIOA
#define BSP_FLASH_SPI_CS_PIN   GPIO_PIN_15

#define  BSP_FLASH_SPI_CS_LOW()            gpio_bit_reset(BSP_FLASH_SPI_CS_PORT, BSP_FLASH_SPI_CS_PIN)
#define  BSP_FLASH_SPI_CS_HIGH()           gpio_bit_set(BSP_FLASH_SPI_CS_PORT, BSP_FLASH_SPI_CS_PIN)

#define BSP_FLASH_SPI_TRIGGER_BYTE      0xA0
#define BSP_FLASH_SPI_WRITE_ENABLE_CMD  0x06
#define BSP_FLASH_SPI_WRITE_CMD         0x02
#define BSP_FLASH_SPI_READ_CMD          0x03
#define BSP_FLASH_SPI_READ_ID_CMD       0x9F
#define BSP_FLASH_SPI_READ_STATUS_CMD   0x05
#define BSP_FLASH_SPI_BULK_ERASE_CMD    0xC7
#define BSP_FLASH_SPI_SECTOR_ERASE_CMD  0x20

#define BSP_FLASH_SPI_WIP_FLAG          0x01
// BSP Flash容量和相关参数
#define BSP_FLASH_PAGE_SIZE 256


#define BSP_FLASH_SECTOR_SIZE (BSP_FLASH_PAGE_SIZE * 16)


u8 bsp_flash_spi_send_byte(u8 byte);
u8 bsp_flash_spi_read_byte(void);
u32 bsp_flash_spi_read_id(void);
void bsp_flash_spi_init(void);
u32 bsp_flash_spi_read_id(void);

void bsp_flash_buffer_write(u32 address, const u8* data, u16 length);
void bsp_flash_sector_erase(u32 address);

void bsp_flash_buffer_read(u32 address, u8* data, u16 length);
#endif