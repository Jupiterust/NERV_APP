/*-----------------------------------------------------------------------*/
/* Low level disk I/O module skeleton for FatFs     (C)ChaN, 2007        */
/*-----------------------------------------------------------------------*/
/* This is a stub disk I/O module that acts as front end of the existing */
/* disk I/O modules and attach it to FatFs module with common interface. */
/*-----------------------------------------------------------------------*/

#include "diskio.h"
#include "sdcard.h"
#include "bsp_rtc.h"     /* get_fattime() 要拿 RTC 的当前时间给文件盖时间戳 */
#include <stdio.h>

/*-----------------------------------------------------------------------*/
/* Correspondence between physical drive number and physical drive.      */
/*-----------------------------------------------------------------------*/
#define BLOCKSIZE   512
#define BUSMODE_4BIT
#define DMA_MODE

/* RTC 寄存器是 BCD 码，get_fattime() 里要用。
   这里自己定义一份而不是用 Function.h 的 BCD2DEC：FatFs 是移植层，
   不应该反过来依赖 Function 层的头文件。 */
#define FAT_BCD2DEC(v)  ((uint32_t)((((v) >> 4) & 0x0F) * 10 + ((v) & 0x0F)))

/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
    BYTE drv                /* Physical drive nmuber (0..) */
)
{
    sd_error_enum status;
    sd_card_info_struct sd_cardinfo;
    uint32_t cardstate = 0;
    if(0 == drv){
        status = sd_init();
        if(SD_OK == status){
            status = sd_card_information_get(&sd_cardinfo);
        }else{
            return STA_NOINIT;
        }
        if(SD_OK == status){
            status = sd_card_select_deselect(sd_cardinfo.card_rca);
        }else{
            return STA_NOINIT;
        }
        status = sd_cardstatus_get(&cardstate);
        if(cardstate & 0x02000000){
            /* 卡被锁定(CARD_IS_LOCKED)。原来这里是死循环 while(1)，
               一旦插了锁定的卡就会把整个 main() 卡死在初始化阶段，
               改成报错返回，让上层自己决定怎么办 */
            return STA_NOINIT | STA_PROTECT;
        }
        if(SD_OK == status){
#ifdef BUSMODE_4BIT
            status = sd_bus_mode_config(SDIO_BUSMODE_4BIT);
#else
            status = sd_bus_mode_config( SDIO_BUSMODE_1BIT );
#endif /* BUSMODE_4BIT */
        }else{
            return STA_NOINIT;
        }
        if(SD_OK == status){
#ifdef DMA_MODE
            status = sd_transfer_mode_config( SD_DMA_MODE );
#else
            status = sd_transfer_mode_config( SD_POLLING_MODE );
#endif /* DMA_MODE */
        }else{
            return STA_NOINIT;
        }
        if(SD_OK == status){
            return 0;
        }else{
            return STA_NOINIT;
        }
    }else{
        return STA_NOINIT;
    }
}



/*-----------------------------------------------------------------------*/
/* Return Disk Status                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
    BYTE drv         /* Physical drive nmuber (0..) */
)
{
    if(0 == drv){
        return RES_OK;
    }


    return STA_NOINIT;
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
    BYTE drv,          /* Physical drive nmuber (0..) */
    BYTE *buff,        /* Data buffer to store read data */
    DWORD sector,      /* Sector address (LBA) */
    BYTE count         /* Number of sectors to read (1..255) */
)
{
    sd_error_enum status = SD_ERROR;
    if(NULL == buff){
        return RES_PARERR;
    }
    if(!count){
        return RES_PARERR;
    }

    if(0 == drv){
        if(1 == count){
            status = sd_block_read((uint32_t *)(&buff[0]), (uint32_t)(sector<<9), BLOCKSIZE);
        }else{
            status = sd_multiblocks_read((uint32_t *)(&buff[0]), (uint32_t)(sector<<9), BLOCKSIZE, (uint32_t)count);
        }
    }
    if(SD_OK == status){
        return RES_OK;
    }
    return RES_ERROR;
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/
/* The FatFs module will issue multiple sector transfer request
/  (count > 1) to the disk I/O layer. The disk function should process
/  the multiple sector transfer properly Do. not translate it into
/  multiple single sector transfers to the media, or the data read/write
/  performance may be drasticaly decreased. */

#if _READONLY == 0
DRESULT disk_write (
    BYTE drv,            /* Physical drive nmuber (0..) */
    const BYTE *buff,    /* Data to be written */
    DWORD sector,        /* Sector address (LBA) */
    BYTE count           /* Number of sectors to write (1..255) */
)
{
    sd_error_enum status = SD_ERROR;
    if(NULL == buff){
        return RES_PARERR;
    }
    if(!count){
        return RES_PARERR;
    }

    if(0 == drv){
        if(1 == count){
            status = sd_block_write((uint32_t *)buff, sector<<9, BLOCKSIZE);
        }else{
            status = sd_multiblocks_write((uint32_t *)buff, sector<<9, BLOCKSIZE, (uint32_t)count);
        }
    }
    if(SD_OK == status){
        return RES_OK;
    }
    return RES_ERROR;
}
#endif /* _READONLY */



/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (BYTE pdrv, BYTE cmd, void *buff)
{
    DRESULT res = RES_ERROR;
    
    if (pdrv != 0) return RES_PARERR; // 假设0是SD卡

    switch (cmd) {
        case CTRL_SYNC:
            res = RES_OK;
            break;

        case GET_SECTOR_COUNT:
            // 【极其重要】FatFs 格式化(f_mkfs)时必须知道SD卡总共有多少扇区！
            //
            // 这里原来用的是 sd_card_information_get() 的 card_capacity 字段，那是错的：
            // 该字段单位是【字节】(sdcard.c:1796 -> (c_size+1)*512*1024)，
            // 一张8GB卡就是 8006926336，已经超过 uint32_t 上限 4294967295，直接回绕，
            // 于是 f_mkfs 会按一个远小于实际的容量去建文件系统。
            //
            // sd_card_capacity_get() 返回的单位是【KB】，2TB以内都不会溢出。
            // 1KB = 2个512字节扇区，所以乘2。
            *(DWORD*)buff = (DWORD)sd_card_capacity_get() * 2;
            res = RES_OK;
            break;

        case GET_BLOCK_SIZE:
            // 擦除块的大小，SD卡通常返回 1 即可，如果是Flash通常是 4096/512 = 8
            *(DWORD*)buff = 1;
            res = RES_OK;
            break;

        default:
            res = RES_PARERR;
            break;
    }
    
    return res;
}
 
/*-----------------------------------------------------------------------*/
/* Get current time                                                      */
/*-----------------------------------------------------------------------*/ 
/* FatFs 通过这个回调取"现在几点"，用来给文件盖创建时间和修改时间。
 * ff.c 里 f_open(创建)、f_write 后的 f_close、f_mkdir 都会调它。
 *
 * 原来固定 return 0 —— 0 解包出来是 1980-00-00 00:00:00，月和日都是 0，
 * 是个非法日期。这就是"TF 卡里的文件没有修改时间"的原因：电脑上看文件属性
 * 是空白或者 1980/0/0。
 *
 * 返回值是 FAT 规定的【打包格式】，不是 Unix 时间戳：
 *   bit31:25  年，从 1980 起算 (0..127)      bit24:21  月 (1..12)
 *   bit20:16  日 (1..31)                      bit15:11  时 (0..23)
 *   bit10:5   分 (0..59)                      bit4:0    秒/2 (0..29，所以只有 2 秒精度)
 *
 * 两个坑：
 * 1. RTC 寄存器里存的是 BCD 码，必须先转十进制。不转的话 0x13 会被当成 19，
 *    月份、日期全错。
 * 2. 这个回调可能在 bsp_rtc_init() 之前就被调用 —— main.c 里 TF 卡的操作
 *    排在 RTC 初始化【前面】。所以下面对明显不合法的日期做了兜底，宁可盖一个
 *    固定的合法日期，也不要把非法值写进目录项。 */
DWORD get_fattime(void)
{
    uint32_t year, month, day, hour, minute, second;

    bsp_rtc_show_time();          /* 刷新 bsp_rtc_init_para */

    /* BCD -> 十进制。年份是 00~99，代表 20xx */
    year   = FAT_BCD2DEC(bsp_rtc_init_para.year);
    month  = FAT_BCD2DEC(bsp_rtc_init_para.month);
    day    = FAT_BCD2DEC(bsp_rtc_init_para.date);
    hour   = FAT_BCD2DEC(bsp_rtc_init_para.hour);
    minute = FAT_BCD2DEC(bsp_rtc_init_para.minute);
    second = FAT_BCD2DEC(bsp_rtc_init_para.second);

    /* RTC 还没走起来时给一个固定的合法日期，别让 FAT 目录项里出现 0 月 0 日 */
    if (month < 1 || month > 12 || day < 1 || day > 31 ||
        year > 99 || hour > 23 || minute > 59 || second > 59) {
        year = 26; month = 1; day = 1;
        hour = 0; minute = 0; second = 0;
    }

    return ((DWORD)(year + 2000 - 1980) << 25)   /* 20xx 换算成"距 1980 多少年" */
         | ((DWORD)month  << 21)
         | ((DWORD)day    << 16)
         | ((DWORD)hour   << 11)
         | ((DWORD)minute << 5)
         | ((DWORD)(second / 2));
}
