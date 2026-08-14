#ifndef __APP_UPGRADE_H
#define __APP_UPGRADE_H

#include "Headfile.h"
//  1.移植  2.串口修改

void app_nvic_correction(void);
void app_upgrade_start(void);

uint32_t get_app_version(void);
void set_app_version(uint32_t version);

void set_app_id_version(uint16_t id);
uint16_t get_app_id_version(void);

#define app_update_receive_start 	0x01

//void set_app_updateFlag(uint8_t flag);

//uint8_t get_app_updateFlag(void);


//typedef enum{
//	ch0_ratio_index = 0,
//	ch1_ratio_index = 1,
//	ch0_threshold_index = 2,
//	ch1_threshold_index = 3,
//	
//}log_object_index_enum;

//void log_store_float(log_object_index_enum temp_index,float input);
//float log_get_float(log_object_index_enum temp_index);

//void log_id_store(uint16_t id);
//uint16_t log_id_get(void);

#endif

