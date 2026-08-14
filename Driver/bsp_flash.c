#include "bsp_flash.h"

void bsp_flash_spi_init(void){
    spi_parameter_struct bsp_flash_spi_init_struct;
    // enable peripheral clock
    rcu_periph_clock_enable(BSP_FLASH_SPI_RCU);

    rcu_periph_clock_enable(BSP_FLASH_SPI_GPIO_RCU);

    rcu_periph_clock_enable(BSP_FLASH_SPI_CS_RCU);
    
    // 配置SPI引脚
    gpio_af_set(BSP_FLASH_SPI_PORT, GPIO_AF_5,
         BSP_FLASH_SPI_SCK_PIN|BSP_FLASH_SPI_MISO_PIN|BSP_FLASH_SPI_MOSI_PIN);
    gpio_mode_set(BSP_FLASH_SPI_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, 
        BSP_FLASH_SPI_SCK_PIN|BSP_FLASH_SPI_MISO_PIN|BSP_FLASH_SPI_MOSI_PIN);
    // 设置电平输出
    gpio_output_options_set(BSP_FLASH_SPI_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_25MHZ, 
        BSP_FLASH_SPI_SCK_PIN|BSP_FLASH_SPI_MISO_PIN|BSP_FLASH_SPI_MOSI_PIN);
    // 配置CS引脚
    gpio_mode_set(BSP_FLASH_SPI_CS_PORT, 
        GPIO_MODE_OUTPUT,
        GPIO_PUPD_NONE,
        BSP_FLASH_SPI_CS_PIN);
    gpio_output_options_set(BSP_FLASH_SPI_CS_PORT, 
        GPIO_OTYPE_PP, 
        GPIO_OSPEED_50MHZ, 
        BSP_FLASH_SPI_CS_PIN);

    BSP_FLASH_SPI_CS_HIGH(); // 默认CS引脚拉高
    
    // SPI参数配置
    bsp_flash_spi_init_struct.trans_mode           = SPI_TRANSMODE_FULLDUPLEX;
    bsp_flash_spi_init_struct.device_mode          = SPI_MASTER;;
    bsp_flash_spi_init_struct.frame_size           = SPI_FRAMESIZE_8BIT;;
    bsp_flash_spi_init_struct.clock_polarity_phase = SPI_CK_PL_LOW_PH_1EDGE;
    bsp_flash_spi_init_struct.nss                  = SPI_NSS_SOFT;
    bsp_flash_spi_init_struct.prescale             = SPI_PSC_8 ;
    bsp_flash_spi_init_struct.endian               = SPI_ENDIAN_MSB;;
    spi_init(BSP_FLASH_SPI, &bsp_flash_spi_init_struct);

    /* enable SPI1 */
    spi_enable(BSP_FLASH_SPI);

    BSP_FLASH_SPI_CS_LOW();
    bsp_flash_spi_send_byte(0xAB);   // Release Power-Down
    BSP_FLASH_SPI_CS_HIGH();
    delay_1ms(1);
    
    /* ★ 再软件复位 Flash，让它回到干净状态 */
    BSP_FLASH_SPI_CS_LOW();
    bsp_flash_spi_send_byte(0x66);   // Enable Reset
    BSP_FLASH_SPI_CS_HIGH();
    
    BSP_FLASH_SPI_CS_LOW();
    bsp_flash_spi_send_byte(0x99);   // Reset
    BSP_FLASH_SPI_CS_HIGH();
    delay_1ms(1);                

}

/* SPI读写一个字节 */
u8 bsp_flash_spi_send_byte(u8 byte){
    /* 死循环等待发送缓冲区空 */
    while(RESET == spi_i2s_flag_get(BSP_FLASH_SPI, SPI_FLAG_TBE));
    /* 发送字节 */
    spi_i2s_data_transmit(BSP_FLASH_SPI, byte);
    /* 等待数据发送完成 */
    while(RESET == spi_i2s_flag_get(BSP_FLASH_SPI, SPI_FLAG_RBNE));
    /* 返回接收到的数据 */
    return spi_i2s_data_receive(BSP_FLASH_SPI);
}

/* SPI读取一个字节 */
u8 bsp_flash_spi_read_byte(void){
    return bsp_flash_spi_send_byte(BSP_FLASH_SPI_TRIGGER_BYTE);
}

void bsp_flash_spi_write_enable(void){
    BSP_FLASH_SPI_CS_LOW();
    bsp_flash_spi_send_byte(BSP_FLASH_SPI_WRITE_ENABLE_CMD); // 写使能指令
    BSP_FLASH_SPI_CS_HIGH();
}

void bsp_flash_spi_wait_for_write_end(void){
    uint8_t flash_status;
    BSP_FLASH_SPI_CS_LOW();
    bsp_flash_spi_send_byte(BSP_FLASH_SPI_READ_STATUS_CMD); // 发送读取状态寄存器指令
    do{
        flash_status = bsp_flash_spi_send_byte(BSP_FLASH_SPI_TRIGGER_BYTE); // 发送读取状态寄存器指令
    }while((flash_status & BSP_FLASH_SPI_WIP_FLAG) == SET); // 等待写入完成，直到状态寄存器的最低位为0
    BSP_FLASH_SPI_CS_HIGH();
}

void bsp_flash_spi_start_read_in_sequence(u32 address){
    BSP_FLASH_SPI_CS_LOW();
    bsp_flash_spi_send_byte(BSP_FLASH_SPI_READ_CMD); // 发送读取数据指令
    bsp_flash_spi_send_byte((address >> 16) & 0xFF); // 发送地址的高8位
    bsp_flash_spi_send_byte((address >>  8) & 0xFF);  // 发送地址的中8位
    bsp_flash_spi_send_byte( address & 0xFF );         // 发送地址的低8位
}

u32 bsp_flash_spi_read_id(void){
    u32 flash_id;
    BSP_FLASH_SPI_CS_LOW();
    bsp_flash_spi_send_byte(BSP_FLASH_SPI_READ_ID_CMD); // 发送读取ID指令
    flash_id  = bsp_flash_spi_send_byte(BSP_FLASH_SPI_TRIGGER_BYTE); // 读取设备ID的高8位
    flash_id <<= 8;
    flash_id |= bsp_flash_spi_send_byte(BSP_FLASH_SPI_TRIGGER_BYTE); // 读取设备ID的中8位
    flash_id <<= 8;
    flash_id |= bsp_flash_spi_send_byte(BSP_FLASH_SPI_TRIGGER_BYTE); // 读取设备ID的低8位
    BSP_FLASH_SPI_CS_HIGH();
    return flash_id;
}

// 1个扇区(Sector) = 16个页(Page)，1个页 = 256 字节

void bsp_flash_page_write(u32 address, const u8* data, u16 length){
    bsp_flash_spi_write_enable(); // 发送写使能指令
    BSP_FLASH_SPI_CS_LOW();
    bsp_flash_spi_send_byte(BSP_FLASH_SPI_WRITE_CMD); // 发送写入数据指令
    bsp_flash_spi_send_byte((address >> 16) & 0xFF);    // 发送地址的高8位
    bsp_flash_spi_send_byte((address >>  8) & 0xFF);    // 发送地址的中8位
    bsp_flash_spi_send_byte( address & 0xFF );          // 发送地址的低8位
    for(u16 i = 0; i < length; i++){
        bsp_flash_spi_send_byte(data[i]); // 写入数据
    }
    BSP_FLASH_SPI_CS_HIGH();
    bsp_flash_spi_wait_for_write_end(); // 等待写入完成
}

void bsp_flash_buffer_write(u32 address, const u8* data, u16 length){
    u8 page_offset = address % BSP_FLASH_PAGE_SIZE;     // 计算地址在页内的偏移
    u8 page_remain = BSP_FLASH_PAGE_SIZE - page_offset; // 计算当前页剩余空间
    u8 page_num = length / BSP_FLASH_PAGE_SIZE;         // 计算需要写入的完整页数
    u8 last_page_remain = length % BSP_FLASH_PAGE_SIZE; // 计算最后一页剩余数据长度

    if(page_offset == 0){ // 地址正好对齐到页起始
        if(page_num > 0){
            for(u8 i = 0; i < page_num; i++){
                bsp_flash_page_write(address, data, BSP_FLASH_PAGE_SIZE);
                address += BSP_FLASH_PAGE_SIZE;
                data += BSP_FLASH_PAGE_SIZE;    
            }
        }
        if(last_page_remain > 0){
            bsp_flash_page_write(address, data, last_page_remain);
        }
    } 
    
    else { // 地址未对齐到页起始
        if(page_remain >= length){ // 数据长度小于当前页剩余空间，直接写入
            bsp_flash_page_write(address, data, length);
        } 
        else { // 数据长度超过当前页剩余空间，先写入当前页剩余空间，再写入后续完整页和最后一页
            bsp_flash_page_write(address, data, page_remain); // 写入当前页剩余空间

            data += page_remain;
            address += page_remain;
            length -= page_remain;

            page_num = length / BSP_FLASH_PAGE_SIZE;         // 重新计算需要写入的完整页数
            last_page_remain = length % BSP_FLASH_PAGE_SIZE; // 重新计算最后一页剩余数据长度

            for(u8 i = 0; i < page_num; i++){
                bsp_flash_page_write(address, data, BSP_FLASH_PAGE_SIZE);
                address += BSP_FLASH_PAGE_SIZE;
                data += BSP_FLASH_PAGE_SIZE;
            }
            if(last_page_remain > 0){
                bsp_flash_page_write(address, data, last_page_remain);
            }
        }
    }
}

void bsp_flash_bulk_erase(void){
    bsp_flash_spi_write_enable(); // 发送写使能指令
    BSP_FLASH_SPI_CS_LOW();
    bsp_flash_spi_send_byte(BSP_FLASH_SPI_BULK_ERASE_CMD); // 发送扇区擦除指令
    BSP_FLASH_SPI_CS_HIGH();
    bsp_flash_spi_wait_for_write_end(); // 等待擦除完成
}

void bsp_flash_sector_erase(u32 address){
    bsp_flash_spi_write_enable(); // 发送写使能指令
    BSP_FLASH_SPI_CS_LOW();
    bsp_flash_spi_send_byte(BSP_FLASH_SPI_SECTOR_ERASE_CMD);    // 发送扇区擦除指令
    bsp_flash_spi_send_byte((address >> 16) & 0xFF);            // 发送地址的高8位
    bsp_flash_spi_send_byte((address >>  8) & 0xFF);            // 发送地址的中8位
    bsp_flash_spi_send_byte( address & 0xFF );                  // 发送地址的低8位
    BSP_FLASH_SPI_CS_HIGH();
    bsp_flash_spi_wait_for_write_end(); // 等待擦除完成
}

void bsp_flash_buffer_read(u32 address, u8* data, u16 length){
    bsp_flash_spi_start_read_in_sequence(address); // 发送读取数据指令和地址
    for(u16 i = 0; i < length; i++){
        data[i] = bsp_flash_spi_read_byte(); // 读取数据
    }
    BSP_FLASH_SPI_CS_HIGH();
}

void bsp_flash_buffer_erase(u32 address, u32 length){
    u8 sector_former_buffer[BSP_FLASH_SECTOR_SIZE];    // 用于保存当前扇区数据前半段的缓冲区
    u8 sector_later_buffer[BSP_FLASH_SECTOR_SIZE];   // 用于保存当前扇区数据后半段的缓冲区
    u32 sector_offset = address % BSP_FLASH_SECTOR_SIZE;        // 计算地址在扇区内的偏移
    u32 sector_remain = BSP_FLASH_SECTOR_SIZE - sector_offset;  // 计算当前扇区剩余空间
    u32 sector_num = length / BSP_FLASH_SECTOR_SIZE;            // 计算需要擦除的完整扇区数
    u32 last_sector_remain = length % BSP_FLASH_SECTOR_SIZE;    // 计算最后一个扇区剩余数据长度

    if(sector_offset == 0){ // 地址正好对齐到扇区起始
        if(sector_num > 0){
            for(u32 i = 0; i < sector_num; i++){
                bsp_flash_sector_erase(address);
                address += BSP_FLASH_SECTOR_SIZE;
            }
        }
        if(last_sector_remain > 0){
            bsp_flash_buffer_read(address + last_sector_remain, sector_later_buffer, BSP_FLASH_SECTOR_SIZE - last_sector_remain); // 读取当前扇区的后半段数据到缓冲区
            bsp_flash_sector_erase(address);
            bsp_flash_buffer_write(address + last_sector_remain, sector_later_buffer, BSP_FLASH_SECTOR_SIZE - last_sector_remain); // 将缓冲区数据写回到扇区剩余部分
        }
    } 
    
    else { // 地址未对齐到扇区起始
        if(sector_remain >= length && sector_num == 0){ 
            bsp_flash_buffer_read(address - sector_offset, sector_former_buffer, sector_offset); // 读取当前扇区的前半段数据到缓冲区
            bsp_flash_buffer_read(address + length, sector_later_buffer, BSP_FLASH_SECTOR_SIZE - sector_offset - length); // 读取当前扇区的后半段数据到缓冲区
            bsp_flash_sector_erase(address - sector_offset); // 擦除当前扇区

            bsp_flash_buffer_write(address - sector_offset, sector_former_buffer, sector_offset); // 将缓冲区数据写回到扇区前半段
            bsp_flash_buffer_write(address + length, sector_later_buffer, BSP_FLASH_SECTOR_SIZE - sector_offset - length); // 将缓冲区数据写回到扇区后半段
        } 
        else { 
            bsp_flash_buffer_read(address - sector_offset, sector_former_buffer, sector_offset); // 读取当前扇区的前半段数据到缓冲区
            bsp_flash_sector_erase(address - sector_offset); // 擦除当前扇区
            bsp_flash_buffer_write(address - sector_offset, sector_former_buffer, sector_offset); // 将缓冲区数据写回到扇区前半段

            address += sector_remain;
            length -= sector_remain;

            sector_num = length / BSP_FLASH_SECTOR_SIZE;            // 重新计算需要擦除的完整扇区数
            last_sector_remain = length % BSP_FLASH_SECTOR_SIZE;    // 重新计算最后一个扇区剩余数据长度

            for(u32 i = 0; i < sector_num; i++){
                bsp_flash_sector_erase(address);
                address += BSP_FLASH_SECTOR_SIZE;
            }
            if(last_sector_remain > 0){
                bsp_flash_buffer_read(address + last_sector_remain, sector_later_buffer, BSP_FLASH_SECTOR_SIZE - last_sector_remain); // 读取当前扇区的后半段数据到缓冲区
                bsp_flash_sector_erase(address);
                bsp_flash_buffer_write(address + last_sector_remain, sector_later_buffer, BSP_FLASH_SECTOR_SIZE - last_sector_remain); // 将缓冲区数据写回到扇区剩余部分
            }
        }
    }
}
