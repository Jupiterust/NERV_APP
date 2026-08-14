#include "Function.h"
//
DeviceDataParams_t Data_class_structure = {1.0f , 1.0f ,3.3f , 3.3f , 3.3f , 0x01 , 0x02 ,       0 , 0 ,0 ,0 ,0 , 0 ,0};
volatile uint8_t g_log_busy = 0;  // flash 忙标志


void report_auto_data_task(void)
{
    uint8_t ack_payload[12] = {0};
    uint32_t current_ts = bsp_rtc_get_unix_timestamp();

    ack_payload[0] = (uint8_t)(current_ts >> 24);
    ack_payload[1] = (uint8_t)(current_ts >> 16);
    ack_payload[2] = (uint8_t)(current_ts >> 8);
    ack_payload[3] = (uint8_t)(current_ts & 0xFF);
    float_to_big_endian_bytes(Data_class_structure.ch0_current_val, &ack_payload[4]);
    float_to_big_endian_bytes(Data_class_structure.ch1_current_val, &ack_payload[8]);
    Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, CMD_CTRL_AUTO_REPORT_START, ack_payload, 12);
}



void mcu_restart(void)
{
    __disable_irq();

    // 确保所有内存访问在复位指令前完成
    __DSB();

    NVIC_SystemReset();

    // 走到这里说明 NVIC_SystemReset 没生效，改用看门狗复位
    rcu_osci_on(RCU_IRC32K);
    while(SUCCESS != rcu_osci_stab_wait(RCU_IRC32K));

    fwdgt_config(10, FWDGT_PSC_DIV4);
    fwdgt_enable();

    // 空转等看门狗复位
    while (1)
    {
        __NOP();
    }
}

void CMD_SYS_REBOOT_FUNCTION()
{
	uint8_t ack = 0xFF; // FF为恢复OK
    Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, CMD_SYS_REBOOT, &ack, 1);
	// 等应答真的发出去再复位
	uint16_t timeout = 0xFFFF;
    while (RESET == usart_flag_get(BSP_RS485_USART, USART_FLAG_TC) && timeout--);
	mcu_restart();
}

/* ================= 恢复出厂设置 0x0102 =================
 * 全部动作集中在这个函数里，加减项就改对应的那一步。
 * 流程：写 flash 默认值 → 清告警记录(可关) → 回应答 → 软复位。
 *
 * 最后的复位不能省。变比/阈值/设备 ID/波特率在 RAM 里还有一份镜像
 *（Data_class_structure、MY_DEVICE_ID、BSP_RS485_BAUDRATE、Modbus_slave_addr），
 * 只写 flash 不重启的话这一轮跑的还是旧值。main() 开机那段本来就是把 flash 值灌进
 * 所有 RAM 镜像的完整流程，复位一次全部到位，还顺带把 DAC 拉回 1000、停掉 TIM7 定时
 * 上报、OLED 回 IDLE、Protocol_mode 回默认。ch2_threshold 没存进 flash，只能靠结构体
 * 初值恢复，同样依赖复位。
 *
 * 出厂默认值在 param_handle.h 的 PARAM_DEFAULT_xxx，不在这里改：
 *   变比 1.0 / 阈值 3.3 / 设备 ID 0x0001 / 波特率 19200 / Modbus 从站地址 1
 * ======================================================== */

/* 出厂复位是否连告警记录一起清空：1 = 清，0 = 保留。
 * 清空要擦 4 个扇区，阻塞约 400ms，上位机的应答超时要留够 1 秒。
 * 只清告警记录不动参数的话用 0x0603，不必改这里。 */
#define FACTORY_RESET_CLEAR_ALARM_LOG   1

void CMD_SYS_FACTORY_RESET_FUNCTION()
{
	uint8_t ack = 0xFF;          // FF为恢复OK
	uint16_t timeout = 0xFFFF;

	// 1. 外部 flash 参数块整体写回默认值（内部是一次扇区擦除 + 回写）
	param_restore_defaults();

	// 2. 清空告警记录，不想清就把上面的开关改成 0
#if (FACTORY_RESET_CLEAR_ALARM_LOG == 1)
	g_log_busy = 1;
	bsp_log_clear_all();
	g_log_busy = 0;
#endif

	// 3. 回应答。必须用旧设备 ID 和旧波特率发：上位机是按旧身份发来的命令，新身份下次
	//    开机才生效。放在这个位置意味着收到应答就是真的擦完了；上位机超时给得太短的话，
	//    把这两行整体挪到第 1 步前面。
	Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, CMD_SYS_FACTORY_RESET, &ack, 1);
	while (RESET == usart_flag_get(BSP_RS485_USART, USART_FLAG_TC) && timeout--);

	// 4. 软复位，让 main() 把默认值重新加载进所有 RAM 镜像
	mcu_restart();
}
	
/* ================= 查询设备信息 0x0103 =================
 * 赛题标为"预留、初赛不考察"，没规定内容字段格式，下面这套布局是自定的。
 * 官方给出格式后只改这一个函数，组帧 / CRC / 发送都不用动。
 *
 * len 是流水累加的，不写死下标，所以：
 *   换顺序 = 把某个【第 N 块】整块上下挪
 *   删字段 = 整块删掉
 *   加字段 = 照抄任意一块接着往 send_buf 里塞
 *
 * 当前布局共 23 字节，多字节一律大端（和协议里其它命令一致）：
 *   [0..1]    设备 ID        2 字节
 *   [2]       协议版本       1 字节
 *   [3..6]    硬件版本       4 字节
 *   [7..10]   App 固件版本   4 字节
 *   [11..22]  序列号        12 字节 ASCII
 * ======================================================== */

/* 硬件版本 / 序列号这类身份信息写死在这里，不读内部 flash 的参数块：那块由兄弟
 * Bootloader 工程负责写，App 不保证它被初始化过，没写过时读出来是 0xFF；而且
 * Parameter_t 只定义在 bootloader.c 里，Function.c 看不见。要改就改下面两行。 */
#define INFO_HW_VERSION     0x00010000UL    // 硬件版本 主.次.补丁.构建 = 0.1.0.0
#define INFO_SERIAL_NUMBER  "202601301528"  // 序列号，比 INFO_SERIAL_LEN 短就补 0x00

#define INFO_SERIAL_LEN     12   // 序列号在报文里固定占几个字节
#define INFO_BUF_SIZE       64   // 内容字段缓冲区，当前用了 23 字节。
                                 // 上限 243，报文长度字段只有 1 字节

void CMD_SYS_READ_INFO_FUNCTION()
{
	uint8_t send_buf[INFO_BUF_SIZE];
	uint8_t len = 0;
	uint32_t hw_ver  = INFO_HW_VERSION;
	uint32_t app_ver = get_app_version();
	const char *sn   = INFO_SERIAL_NUMBER;
	uint8_t sn_len   = (uint8_t)strlen(INFO_SERIAL_NUMBER);
	uint8_t i;

	memset(send_buf, 0, sizeof(send_buf));

	// 【第 1 块】设备 ID  2 字节 大端
	send_buf[len++] = (uint8_t)(MY_DEVICE_ID >> 8);
	send_buf[len++] = (uint8_t)(MY_DEVICE_ID & 0xFF);

	// 【第 2 块】协议版本  1 字节，和帧头里的版本字段同值
	send_buf[len++] = PROTOCOL_VERSION;

	// 【第 3 块】硬件版本  4 字节 大端
	send_buf[len++] = (uint8_t)(hw_ver >> 24);
	send_buf[len++] = (uint8_t)(hw_ver >> 16);
	send_buf[len++] = (uint8_t)(hw_ver >> 8);
	send_buf[len++] = (uint8_t)(hw_ver & 0xFF);

	// 【第 4 块】App 固件版本  4 字节 大端，和 0x0104 返回同值
	send_buf[len++] = (uint8_t)(app_ver >> 24);
	send_buf[len++] = (uint8_t)(app_ver >> 16);
	send_buf[len++] = (uint8_t)(app_ver >> 8);
	send_buf[len++] = (uint8_t)(app_ver & 0xFF);

	// 【第 5 块】序列号  INFO_SERIAL_LEN 字节 ASCII，不足补 0x00
	for (i = 0; i < INFO_SERIAL_LEN; i++) {
		send_buf[len++] = (i < sn_len) ? (uint8_t)sn[i] : 0x00;
	}

	Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, CMD_SYS_READ_INFO, send_buf, len);
}




void CMD_SYS_READ_VER_FUNCTION()
{
	uint32_t app_ver = get_app_version();
    uint8_t send_buf[4];
	send_buf[0] = (uint8_t)(app_ver >> 24);
	send_buf[1] = (uint8_t)(app_ver >> 16);
	send_buf[2] = (uint8_t)(app_ver >> 8);
	send_buf[3] = (uint8_t)(app_ver & 0xFF);
    Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, CMD_SYS_READ_VER, send_buf, 4);
}

void CMD_SYS_SET_TIME_FUNCTION(uint8_t* DATA_ARRAY)
{
	uint32_t received_ts;
	
	received_ts = ((uint32_t)DATA_ARRAY[0] << 24) | ((uint32_t)DATA_ARRAY[1] << 16) | ((uint32_t)DATA_ARRAY[2] << 8)  | ((uint32_t)DATA_ARRAY[3]);
	if(bsp_rtc_set_unix_timestamp(received_ts, 0) == SUCCESS) 
	{
		uint8_t ack = 0xFF; // FF为恢复OK
    	Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, CMD_SYS_SET_TIME, &ack, 1); 
    }
}

void CMD_SYS_READ_TIME_FUNCTION()
{
	uint32_t current_ts = bsp_rtc_get_unix_timestamp();
	uint8_t send_buf[4];
	
	// 转换为大端序发送 
	send_buf[0] = (uint8_t)(current_ts >> 24);
	send_buf[1] = (uint8_t)(current_ts >> 16);
	send_buf[2] = (uint8_t)(current_ts >> 8);
	send_buf[3] = (uint8_t)(current_ts & 0xFF);
	
	Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, CMD_SYS_READ_TIME, send_buf, 4);
}

void CMD_SYS_SET_ID_FUNCTION(uint8_t* DATA_ARRAY)
{
	uint16_t new_id = ((uint16_t)DATA_ARRAY[0] << 8) | ((uint16_t)DATA_ARRAY[1]);
            
	// 广播地址和 0 不能当设备 ID
	if (new_id != BROADCAST_ID && new_id != 0x0000) {
					
					
	MY_DEVICE_ID = new_id;
	param_set_id(MY_DEVICE_ID );
	uint8_t ack = 0xFF; 
	Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, CMD_SYS_SET_ID, &ack, 1);
				
	}
}

void CMD_SYS_SET_BAUDRATE_FUNCTION(uint8_t* DATA_ARRAY)
{
	uint8_t ack = 0xFF;
	// 先回应答再切波特率，否则应答会用新波特率发出去
	Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, CMD_SYS_SET_BAUDRATE, &ack, 1);
	switch (DATA_ARRAY[0])
	{
		case 0x11:
			BSP_RS485_BAUDRATE = 4800;
			break;
		case 0x12:
			BSP_RS485_BAUDRATE = 9600;
			break;
		case 0x13:
			BSP_RS485_BAUDRATE = 19200;
			break;
		case 0x14:
			BSP_RS485_BAUDRATE = 115200;
			break;
		default:
			// 不认识的编码一律回默认波特率
			BSP_RS485_BAUDRATE = 19200;
			break;
	}
	
	param_set_baud(BSP_RS485_BAUDRATE);
	
	bsp_rs485_set_baudrate(BSP_RS485_BAUDRATE);
}
	
void CMD_SYS_READ_ID_FUNCTION()
{
	uint8_t send_buf[2];
	send_buf[0] = (uint8_t)(MY_DEVICE_ID >> 8);
	send_buf[1] = (uint8_t)(MY_DEVICE_ID & 0xFF);
	Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, CMD_SYS_READ_ID, send_buf, 2);
}	

void CMD_SYS_READ_BAUDRATE_FUNCTION()
{
	uint8_t temp;
	switch(BSP_RS485_BAUDRATE)
	{
		case 4800:
			temp = 0X11;
			break; 
		case 9600:
			temp = 0X12;
			break;
		case 19200:
			temp = 0X13;
			break;
		case 115200:
			temp = 0X14;
			break;
		default :
			break;
	}
	Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, CMD_SYS_READ_BAUDRATE, &temp, 1);
}

void CMD_DATA_READ_CH0_FUNCTION()
{
	uint8_t ack_payload[4];
    float_to_big_endian_bytes(Data_class_structure.ch0_current_val, ack_payload);
    Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, CMD_DATA_READ_CH0, ack_payload, 4);
}

void CMD_DATA_READ_CH1_FUNCTION()
{	
	uint8_t ack_payload[4];
	float_to_big_endian_bytes(Data_class_structure.ch1_current_val, ack_payload);
	Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, CMD_DATA_READ_CH1, ack_payload, 4);
}

void CMD_DATA_READ_PT100_FUNCTION()
{
	// 开了 TIM9 后台扫描时，i0_current / v0_voltage / v1_voltage / ch2_current_temp
	// 由 ad3344_tim9_10ms_isr() 每 30ms 刷一遍，不用再阻塞采样，只有关掉扫描才走这条路。
	// 换算和写结构体都在驱动的 ad3344_dispatch_to_struct() 里，这里只触发采样。
#if (AD3344_TIM9_SCAN_ENABLE == 0)
	ad3344_read_ch012(NULL, NULL, NULL);            // 一次读回 CH0/CH1/CH2 并回填结构体
#endif
	uint8_t ack_payload[4];
	float_to_big_endian_bytes(Data_class_structure.ch2_current_temp, ack_payload);
	Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, CMD_DATA_READ_PT100, ack_payload, 4);

	// 三路读数调试打印
	printf("i0: %.3fmA, v0: %.3fV, v1: %.3fV, i0_broken: %d\r\n", Data_class_structure.i0_current, 
		Data_class_structure.v0_voltage, Data_class_structure.v1_voltage , Data_class_structure.i0_broken);
}

void CMD_DATA_SET_CH0_RATIO_FUNCTION(uint8_t* DATA_ARRAY)
{
	float new_ratio;
	uint8_t temp[4];
	temp[0] = DATA_ARRAY[3]; // 调换字节序
	temp[1] = DATA_ARRAY[2];
	temp[2] = DATA_ARRAY[1];
	temp[3] = DATA_ARRAY[0];
	memcpy(&new_ratio, temp, 4); 
	
	Data_class_structure.ch0_ratio = new_ratio;
	param_set_float(ch0_ratio_index, Data_class_structure.ch0_ratio);
	uint8_t ack = 0xFF; 
	Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, CMD_DATA_SET_CH0_RATIO, &ack, 1);
}

void CMD_DATA_SET_CH1_RATIO_FUNCTION(uint8_t* DATA_ARRAY)
{
	float new_ratio;
	uint8_t temp[4];
	temp[0] = DATA_ARRAY[3]; // 调换字节序
	temp[1] = DATA_ARRAY[2];
	temp[2] = DATA_ARRAY[1];
	temp[3] = DATA_ARRAY[0];
	memcpy(&new_ratio, temp, 4); 
	Data_class_structure.ch1_ratio = new_ratio;
	
	param_set_float(ch1_ratio_index, Data_class_structure.ch1_ratio);

	uint8_t ack = 0xFF; 
    Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, CMD_DATA_SET_CH1_RATIO, &ack, 1);
}

void CMD_DATA_SET_REPORT_INTVL_FUNCTION(uint8_t* DATA_ARRAY)
{
	Data_class_structure.report_interval_code = DATA_ARRAY[0];
	switch(Data_class_structure.report_interval_code)
	{
		case 1:
			bsp_tim7_set_timeout(1);
			break;
		case 2:
			bsp_tim7_set_timeout(3);
			break;
		case 3:
			bsp_tim7_set_timeout(5);
    		break;
    	default : // 默认为1s定时
    		bsp_tim7_set_timeout(1);
    		break;
	}

#if (AUTO_REPORT_ALLOW_SET_INTVL == 2)
	/* 档位 2：上报进行中只改间隔不回应答，总线上不多出一帧，避开赛题 H-02 的
	   "上报期间额外应答扣分"。没在上报时照常应答。
	   档位说明见 Function.h 顶部的 AUTO_REPORT_ALLOW_SET_INTVL。 */
	if (Data_class_structure.Regular_reporting_Flag == 1) {
		return;
	}
#endif

    uint8_t ack = 0xFF;
    Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, CMD_DATA_SET_REPORT_INTVL, &ack, 1);
}
		
void CMD_CTRL_SET_DAC_VOLT_FUNCTION(uint8_t* DATA_ARRAY)
{
	// 协议下发的是 0~4095 的 12 位 DAC 原始码值(0x0000~0x0FFF)，不是毫伏，
	// 所以只能用 bsp_dac_set_raw()。bsp_dac_set_voltage() 的入参是毫伏，会先按
	// 3300mV 限幅再乘一次 4095/3300，中间码值会偏大 1.24 倍，3300 以上全部饱和。
	uint16_t dac_code = 0;
    dac_code = ((uint16_t)DATA_ARRAY[0] << 8) | ((uint16_t)DATA_ARRAY[1]);

    bsp_dac_set_raw(dac_code);
    uint8_t ack = 0xFF; 
    Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, CMD_CTRL_SET_DAC_VOLT, &ack, 1);
}	
		
void CMD_CTRL_AUTO_REPORT_START_FUNCTION()
{
	timer_counter_value_config(TIMER7, 0);
    timer_enable(TIMER7);
	Data_class_structure.Regular_reporting_Flag = 1;
	// 应答里带一份当前数据，后续由 TIM7 周期上报
	uint8_t ack_payload[12] = {0};
	uint32_t current_ts = bsp_rtc_get_unix_timestamp();
		
	ack_payload[0] = (uint8_t)(current_ts >> 24);	
	ack_payload[1] = (uint8_t)(current_ts >> 16);	
	ack_payload[2] = (uint8_t)(current_ts >> 8);	
	ack_payload[3] = (uint8_t)(current_ts & 0xFF);	
	
	float_to_big_endian_bytes(Data_class_structure.ch0_current_val, &ack_payload[4]);	
	float_to_big_endian_bytes(Data_class_structure.ch1_current_val, &ack_payload[8]);	
	Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, CMD_CTRL_AUTO_REPORT_START, ack_payload, 12);	
}	
		
void CMD_CTRL_AUTO_REPORT_STOP_FUNCTION()
{
    timer_disable(TIMER7);
	Data_class_structure.Regular_reporting_Flag = 0;
    uint8_t ack = 0xFF; 
    Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, CMD_CTRL_AUTO_REPORT_STOP, &ack, 1);
}	
		
/* 赛题 2.2：唤醒后回一个裸 ASCII 串，不封帧 */
#define SLEEP_WAKEUP_TEXT   "instrument wakeup"

void CMD_CTRL_ENTER_SLEEP_FUNCTION()
{
	uint8_t ack = 0xFF;
	Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, CMD_CTRL_ENTER_SLEEP, &ack, 1);

	/* 不要在这里轮询 USART_FLAG_TC 等 ACK 发完：发送是异步的，TC 置位时 TC 中断还没跑，
	 * DE 仍停在发送态。等待和唤醒后的 485 恢复都在 bsp_deepsleep_config() 里做，
	 * Modbus 那条睡眠路径（modbus_data_map.c 的 MB_CMD_SLEEP）走的是同一个函数 */
	bsp_deepsleep_config();

	// 醒来后回复，此时 485 已被 bsp_deepsleep_config() 重新初始化好
	// 操作日志：赛题 2.2 把这种不封帧的字符串回复算作自定义协议的一部分，归到 CUSTOM
	oplog_add(OPLOG_PROTO_CUSTOM, OPLOG_DIR_TX,
	          (const uint8_t *)SLEEP_WAKEUP_TEXT, (uint16_t)strlen(SLEEP_WAKEUP_TEXT), 1);
	bsp_rs485_send_data((const u8 *)SLEEP_WAKEUP_TEXT, strlen(SLEEP_WAKEUP_TEXT));
}
		
void CMD_CFG_READ_THRES_ALL_FUNCTION()
{
	uint8_t ack_payload[8] = {0};
	float_to_big_endian_bytes(Data_class_structure.ch0_threshold, ack_payload);
	float_to_big_endian_bytes(Data_class_structure.ch1_threshold, &ack_payload[4]);
	Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, CMD_CFG_READ_THRES_ALL, ack_payload, 8);
}
		
void CMD_CFG_READ_THRES_CH0_FUNCTION()
{
	uint8_t ack_payload[4] = {0};
	float_to_big_endian_bytes(Data_class_structure.ch0_threshold, ack_payload);
	Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, CMD_CFG_READ_THRES_CH0, ack_payload, 4);
}
		
void CMD_CFG_READ_THRES_CH1_FUNCTION()
{
	uint8_t ack_payload[4] = {0};
    float_to_big_endian_bytes(Data_class_structure.ch1_threshold, ack_payload);
    Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, CMD_CFG_READ_THRES_CH1, ack_payload, 4);

}	
		
void CMD_CFG_READ_THRES_CH2_FUNCTION() // 未使用,留下接口
{
	
	
	
	
	
}	
		
void CMD_CFG_WRITE_THRES_CH0_FUNCTION(uint8_t* DATA_ARRAY)
{
	float new_threshold;
	uint8_t temp[4];
	temp[0] = DATA_ARRAY[3]; // 调换字节
	temp[1] = DATA_ARRAY[2];
	temp[2] = DATA_ARRAY[1];
	temp[3] = DATA_ARRAY[0];
	memcpy(&new_threshold, temp, 4); 
    
    Data_class_structure.ch0_threshold = new_threshold;
	param_set_float(ch0_threshold_index, Data_class_structure.ch0_threshold);
    uint8_t ack = 0xFF; 
    Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, CMD_CFG_WRITE_THRES_CH0, &ack, 1);
}	
		
void CMD_CFG_WRITE_THRES_CH1_FUNCTION(uint8_t* DATA_ARRAY)
{
	float new_threshold;
	uint8_t temp[4];
	temp[0] = DATA_ARRAY[3]; // 调换字节序
	temp[1] = DATA_ARRAY[2];
	temp[2] = DATA_ARRAY[1];
	temp[3] = DATA_ARRAY[0];
	memcpy(&new_threshold, temp, 4); 
	
	Data_class_structure.ch1_threshold = new_threshold;
	param_set_float(ch1_threshold_index, Data_class_structure.ch1_threshold);
	uint8_t ack = 0xFF; 
	Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, CMD_CFG_WRITE_THRES_CH1, &ack, 1);
}
		
void CMD_CFG_WRITE_THRES_CH2_FUNCTION(uint8_t* DATA_ARRAY) // 未使用留下接口
{
	
	
	
	
	
}
		
void CMD_OTA_REQUEST_FUNCTION()
{
	uint8_t ack = 0xFF; 
    Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, CMD_OTA_REQUEST, &ack, 1);
	
	// 往内部 flash 参数块写设备 ID 和升级标志，复位后由 Bootloader 接手
	set_app_id_version(MY_DEVICE_ID);
	set_app_updateFlag(app_update_receive_start);
    uint16_t timeout = 0xFFFF;
    while (RESET == usart_flag_get(BSP_RS485_USART, USART_FLAG_TC) && timeout--);
    mcu_restart();
}	
		
void CMD_OTA_READY_DATA_FUNCTION()
{
	if(SUCCESS)
	{
		uint8_t ack = 0xFF; 
		Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, CMD_OTA_READY_DATA, &ack, 1);
	}
	else {
		Protocol_SendFrame(MY_DEVICE_ID, TYPE_ERROR, CMD_OTA_READY_DATA, NULL, 0);
	}
}	
		
void CMD_OTA_EXECUTE_FUNCTION()
{



}	
		
void CMD_ALARM_SET_AUTO_REPORT_FUNCTION(uint8_t* DATA_ARRAY)
{	
	switch(DATA_ARRAY[0])
	{
		case 0x01:
			Data_class_structure.alarm_mode = 1;    // 主动上报
			break;
		case 0x02:
			Data_class_structure.alarm_mode = 2;    // 只记录不上报
			break;
		default :
			break;
	}
	uint8_t ack = 0xFF; 
	Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, CMD_ALARM_SET_AUTO_REPORT, &ack, 1);
}

void CMD_ALARM_READ_RECORD_FUNCTION()
{
	g_log_busy = 1;
	bsp_log_query_handler();
	g_log_busy = 0 ;
}

void CMD_ALARM_CLEAR_FUNCTION()
{
	g_log_busy = 1;
	bsp_log_clear_all();
	g_log_busy = 0;
	uint8_t ack = 0xFF; 
	Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, CMD_ALARM_CLEAR, &ack, 1);
}

void CMD_LOG_READ_FUNCTION()
{



}

void CMD_LOG_CLEAR_FUNCTION()
{



}

void CMD_SPEC_DISCOVER_FUNCTION()
{
	uint8_t send_buf[2];
	send_buf[0] = (uint8_t)(MY_DEVICE_ID >> 8);
	send_buf[1] = (uint8_t)(MY_DEVICE_ID & 0xFF);

	// 发现广播的应答格式是心跳帧，不是 ID 应答
	Protocol_SendFrame(MY_DEVICE_ID, TYPE_HEARTBEAT, CMD_SPEC_HEARTBEAT, NULL, 0);
}




// 记录每个通道上次是否处于告警态，用于边沿判定
static uint8_t ch0_alarm_active = 0;
static uint8_t ch1_alarm_active = 0;

void report_police_function(uint8_t alarm_mode)
{
	if (g_log_busy) return;  // flash 正被清除/查询占用，本次跳过告警写入
    // CH0：只在超限的上升沿记一次
    if (Data_class_structure.ch0_current_val > Data_class_structure.ch0_threshold) {
        if (ch0_alarm_active == 0) {      
            ch0_alarm_active = 1;
            if (alarm_mode == 1)
                bsp_log_active_report_and_record("CH0", ch0_threshold, Data_class_structure.ch0_current_val);
            else if (alarm_mode == 2)
                bsp_log_record_only("CH0", ch0_threshold, Data_class_structure.ch0_current_val);
        }
        // 持续超限期间不重复记录
    } else {
        ch0_alarm_active = 0;  // 回到正常，下次超限算新事件
    }

    // CH1
    if (Data_class_structure.ch1_current_val > Data_class_structure.ch1_threshold) {
        if (ch1_alarm_active == 0) {
            ch1_alarm_active = 1;
            if (alarm_mode == 1)
                bsp_log_active_report_and_record("CH1", ch1_threshold, Data_class_structure.ch1_current_val);
            else if (alarm_mode == 2)
                bsp_log_record_only("CH1", ch1_threshold, Data_class_structure.ch1_current_val);
        }
    } else {
        ch1_alarm_active = 0;
    }
}


		