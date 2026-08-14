
#ifndef __ROM_H__
#define __ROM_H__


#include "Headfile.h"



/************************ 函数定义 ************************/
void internal_flash_erase(uint32_t addr);

void internal_flash_earse_sector(uint32_t addr);

void internal_flash_write(uint32_t addr , uint16_t data);

void internal_flash_write_word(uint32_t addr , uint32_t data);

void internal_flash_write_str(uint32_t addr , uint16_t* data , uint16_t len);

void internal_flash_write_str_Char(uint32_t addr , uint8_t* data , uint32_t len);

uint16_t internal_flash_read(uint32_t addr);

uint8_t internal_flash_read_Char(uint32_t addr);

uint32_t internal_flash_read_word(uint32_t addr);

#endif // !__ROM_H__

/****************************End*****************************/
