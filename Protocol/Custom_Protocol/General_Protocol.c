#include "General_Protocol.h"

// 接收缓冲区：Hex 转 ASCII 体积翻倍，二进制最大约 270 字节，
// 转 ASCII 约 540 字节，这里取 1024。
#define RX_BUF_SIZE 1024 
char rx_buffer[RX_BUF_SIZE];
uint16_t rx_index = 0;
//设备ID号
uint16_t MY_DEVICE_ID = 0x0001;


/* ================= 转码工具函数 ================= */

// ASCII 字符转十六进制数值 ('A' -> 10)
static uint8_t CharToHex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0xFF; // 非法字符
}

// 字符串转二进制数组 (返回成功转换的字节数)
// out_size 是 out_bytes 的容量：报文长度字段是 1 字节，合法 payload 最大 255，
// 整帧最大 13+255=268 字节；超过容量一律当帧长错误处理，绝不越界写。
static int HexStrToBytes(const char* hex_str, uint16_t str_len, uint8_t* out_bytes, uint16_t out_size) {
    if (str_len % 2 != 0) return -1; // 必须是偶数个字符
    if (str_len / 2 > out_size) return -1; // 超出目标缓冲区，按帧长错误处理
    for (uint16_t i = 0; i < str_len / 2; i++) {
        uint8_t high = CharToHex(hex_str[i*2]);
        uint8_t low  = CharToHex(hex_str[i*2+1]);
        if (high == 0xFF || low == 0xFF) return -1; // 包含非 Hex 字符
        out_bytes[i] = (high << 4) | low;
    }
    return str_len / 2;
}

// 二进制数组转 ASCII 字符串
static void BytesToHexStr(const uint8_t* bytes, uint16_t len, char* out_str) {
    const char hex_chars[] = "0123456789ABCDEF"; // 统一转为大写
    for (uint16_t i = 0; i < len; i++) {
        out_str[i*2]     = hex_chars[(bytes[i] >> 4) & 0x0F];
        out_str[i*2+1]   = hex_chars[bytes[i] & 0x0F];
    }
}
/**
 * @brief float 转 IEEE 754 大端序字节数组
 */
void float_to_big_endian_bytes(float val, uint8_t *bytes) {
    union {
        float f;
        uint8_t b[4];
    } data;

    data.f = val;

    // 小端 MCU 需倒序
    bytes[0] = data.b[3];
    bytes[1] = data.b[2];
    bytes[2] = data.b[1];
    bytes[3] = data.b[0];
}

// u8 转两个 ASCII 字符
void byte_to_hex_ascii(uint8_t byte, char *out) {
    const char hex_table[] = "0123456789ABCDEF";
    out[0] = hex_table[byte >> 4];     // 高 4 位
    out[1] = hex_table[byte & 0x0F];   // 低 4 位
}
/* 			 帧处理 			 */

// CRC16-Modbus 校验
uint16_t crc16_modbus(uint8_t *data, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc >>= 1; crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

// 解析纯二进制数据帧
void Protocol_HandleFrame(uint8_t* raw_data, uint16_t raw_len) {
    // 提取字段 (大端序)
    uint16_t device_id  = MAKE_UINT16(raw_data[2], raw_data[3]);
    uint8_t  frame_type = raw_data[4];
    uint16_t cmd_word   = MAKE_UINT16(raw_data[5], raw_data[6]);
    uint8_t  payload_len= raw_data[7];
    uint8_t  version    = raw_data[8];
    uint8_t* payload    = (payload_len > 0) ? &raw_data[9] : NULL;

    // 校验协议版本和目标设备ID
    if (version != PROTOCOL_VERSION) return;
    if (device_id != MY_DEVICE_ID && device_id != BROADCAST_ID) return;

    // payload_len 与实际帧长是否自洽：固定部分9 + payload_len + CRC2 + 帧尾2 = 13 + payload_len
    if (raw_len != 13 + payload_len) {
        // 帧长不匹配，丢弃并回错误应答
        Protocol_SendFrame(device_id, TYPE_ERROR, CMD_SPEC_ERROR_REPLY, NULL, 0);
        return;
    }

    // 校验帧类型合法性
    if (frame_type != TYPE_CMD_SEND && frame_type != TYPE_ACK &&
        frame_type != TYPE_HEARTBEAT && frame_type != TYPE_ERROR) {
        // TODO: 日志记录
        Protocol_SendFrame(device_id, TYPE_ERROR, CMD_SPEC_ERROR_REPLY, NULL, 0);
        return;
    }
	// 自动上报期间普通命令不应答（赛题 H-02：除停止命令外的应答一律扣分）。
	// 放行名单由 Function.h 的 AUTO_REPORT_ALLOW_SET_INTVL 控制：
	//   0   只放行 0x0303 停止
	//   1/2 额外放行 0x0261 改上报间隔，是否回应答由 Function.c 的处理函数决定
	if (Data_class_structure.Regular_reporting_Flag == 1) {
        if (cmd_word != CMD_CTRL_AUTO_REPORT_STOP
#if (AUTO_REPORT_ALLOW_SET_INTVL != 0)
            && cmd_word != CMD_DATA_SET_REPORT_INTVL
#endif
           ) {
            return;
        }
    }
    // 业务命令分发
    switch(cmd_word) {
		//系统管理类
        case CMD_SYS_REBOOT:				//设备重启
            if (frame_type == TYPE_CMD_SEND) {
                CMD_SYS_REBOOT_FUNCTION();
            }
            break;
		
		
		case CMD_SYS_FACTORY_RESET:         // 恢复出厂设置
			if (frame_type == TYPE_CMD_SEND) {
				// 无内容字段，收到就执行，动作全在 Function.c 里
				CMD_SYS_FACTORY_RESET_FUNCTION();
			}
			break;
		
		case CMD_SYS_READ_INFO:             // 查询设备信息
			if (frame_type == TYPE_CMD_SEND) {
				// 无内容字段，应答内容和顺序全在 Function.c 里改
				CMD_SYS_READ_INFO_FUNCTION();
			}
			break;
		
		case CMD_SYS_READ_VER:				// 查询固件版本
			if (frame_type == TYPE_CMD_SEND) {
				CMD_SYS_READ_VER_FUNCTION();
            }
			break;
		
		case CMD_SYS_SET_TIME:				// 设置设备时间
			if (frame_type == TYPE_CMD_SEND) {
				// 检查数据长度是否足够
				if (payload_len >= 4 && payload != NULL) 
				{
					CMD_SYS_SET_TIME_FUNCTION(payload);
				}
			}
			break;
		
		case CMD_SYS_READ_TIME:             // 查询设备时间
			if(frame_type == TYPE_CMD_SEND)
			{
				CMD_SYS_READ_TIME_FUNCTION();
			}	
			break;
			
		case CMD_SYS_SET_ID:// 设置设备 ID
			if (frame_type == TYPE_CMD_SEND) {
				if (payload_len >= 2 && payload != NULL) {
					CMD_SYS_SET_ID_FUNCTION(payload);
				}
			}
			break;
		
		case CMD_SYS_SET_BAUDRATE:// 设置波特率
			if(frame_type == TYPE_CMD_SEND){
				if (payload_len >= 1 && payload != NULL) {
					CMD_SYS_SET_BAUDRATE_FUNCTION(payload);
				}
			}
			break;
		
		case CMD_SYS_READ_ID:// 查询设备 ID
			if (frame_type == TYPE_CMD_SEND) {
				CMD_SYS_READ_ID_FUNCTION();
			}
			break;
		
		case CMD_SYS_READ_BAUDRATE:// 查询波特率
			CMD_SYS_READ_BAUDRATE_FUNCTION();
			break;
		
		//数据类
        case CMD_DATA_READ_CH0:// 查询 CH0 数据（滑动变阻器）
            if (frame_type == TYPE_CMD_SEND) {
				CMD_DATA_READ_CH0_FUNCTION();
            }
            break;

        case CMD_DATA_READ_CH1:// 查询 CH1 数据（DAC 回读）
			if (frame_type == TYPE_CMD_SEND) {
				CMD_DATA_READ_CH1_FUNCTION();
			}
			break;
		
		case CMD_DATA_READ_PT100:// 查询特定通道数据（外部ADC PT100）
			if (frame_type == TYPE_CMD_SEND) {
				CMD_DATA_READ_PT100_FUNCTION();
			}
			break;
		
		case CMD_DATA_SET_CH0_RATIO:// 设置 CH0 变比
			if (frame_type == TYPE_CMD_SEND) {
				if (payload_len >= 4 && payload != NULL) {
					CMD_DATA_SET_CH0_RATIO_FUNCTION(payload);
				}
			}
			break;
		
		case CMD_DATA_SET_CH1_RATIO:// 设置 CH1 变比
			if (frame_type == TYPE_CMD_SEND) {
				// 检查数据长度是否足够
				if (payload_len >= 4 && payload != NULL) {
					CMD_DATA_SET_CH1_RATIO_FUNCTION(payload);
				}
			}
			break;
		
		case CMD_DATA_SET_REPORT_INTVL:// 设置数据上报时间间隔
			if (frame_type == TYPE_CMD_SEND) {
				CMD_DATA_SET_REPORT_INTVL_FUNCTION(payload);
			}
			break;
		
		// 控制类
		case CMD_CTRL_SET_DAC_VOLT:// 设置 DAC 输出电压
			if (frame_type == TYPE_CMD_SEND) {
				if (payload_len >= 2 && payload != NULL) {
					CMD_CTRL_SET_DAC_VOLT_FUNCTION(payload);
				}
			}
			break;
		
		case CMD_CTRL_AUTO_REPORT_START:// 定时自动上报数据开始
			if (frame_type == TYPE_CMD_SEND) {
				CMD_CTRL_AUTO_REPORT_START_FUNCTION();
			}
			break;
		
		case CMD_CTRL_AUTO_REPORT_STOP:// 定时自动上报数据停止
			if (frame_type == TYPE_CMD_SEND) {
				CMD_CTRL_AUTO_REPORT_STOP_FUNCTION();
			}
			break;
		
		case CMD_CTRL_ENTER_SLEEP:// 进入睡眠模式
			if (frame_type == TYPE_CMD_SEND) {
				CMD_CTRL_ENTER_SLEEP_FUNCTION();
			}
			break;
		
		//参数配置类
		case CMD_CFG_READ_THRES_ALL:// 读取阈值参数 (批量读取CH0, CH1)
			if (frame_type == TYPE_CMD_SEND) {
				CMD_CFG_READ_THRES_ALL_FUNCTION();
			}
			break;
		
		case CMD_CFG_READ_THRES_CH0:// 读取 CH0 阈值参数
			if (frame_type == TYPE_CMD_SEND) {
				CMD_CFG_READ_THRES_CH0_FUNCTION();
			}
			break;
		
		case CMD_CFG_READ_THRES_CH1:// 读取 CH1 阈值参数
			if (frame_type == TYPE_CMD_SEND) {
				CMD_CFG_READ_THRES_CH1_FUNCTION();
			}
			break;
		
		case CMD_CFG_READ_THRES_CH2:// 读取 CH2 阈值参数
			CMD_CFG_READ_THRES_CH2_FUNCTION();// 没用,留下接口函数
			break;
		
		case CMD_CFG_WRITE_THRES_CH0:// 写入 CH0 阈值参数
			if (frame_type == TYPE_CMD_SEND) {
				if (payload_len >= 4 && payload != NULL) {
					CMD_CFG_WRITE_THRES_CH0_FUNCTION(payload);
				}
			}
			break;
		
		case CMD_CFG_WRITE_THRES_CH1:// 写入 CH1 阈值参数
			if (frame_type == TYPE_CMD_SEND) {
				if (payload_len >= 4 && payload != NULL) {
					CMD_CFG_WRITE_THRES_CH1_FUNCTION(payload);
				}
			}
			break;
		
		case CMD_CFG_WRITE_THRES_CH2:// 写入 CH2 阈值参数
			if (frame_type == TYPE_CMD_SEND) {
				if (payload_len >= 4 && payload != NULL) {
					CMD_CFG_WRITE_THRES_CH2_FUNCTION(payload);//没用,留下接口函数
				}
			}
			break;
		
		//系统升级类
		case CMD_OTA_REQUEST:// 升级请求
			if (frame_type == TYPE_CMD_SEND) {
				CMD_OTA_REQUEST_FUNCTION();
			}
			break;
		
		case CMD_OTA_READY_DATA:// 准备传输固件数据包
			if (frame_type == TYPE_CMD_SEND) {
				CMD_OTA_READY_DATA_FUNCTION();
			}
			break;
		
		case CMD_OTA_EXECUTE:// 执行升级流程
			if (frame_type == TYPE_CMD_SEND) {
				CMD_OTA_EXECUTE_FUNCTION();
			}
			break;
		
		// 告警与日志类
		case CMD_ALARM_SET_AUTO_REPORT:// 是否主动上报告警
			if (frame_type == TYPE_CMD_SEND) {
				CMD_ALARM_SET_AUTO_REPORT_FUNCTION(payload);
			}
			break;
		
		case CMD_ALARM_READ_RECORD:// 查询告警记录
			if (frame_type == TYPE_CMD_SEND) {
				CMD_ALARM_READ_RECORD_FUNCTION();
			}
			break;
		
		case CMD_ALARM_CLEAR:// 清除告警
			if (frame_type == TYPE_CMD_SEND) {
				CMD_ALARM_CLEAR_FUNCTION();
			}
			break;
		
		// 0x0604 / 0x0605 赛题标为"预留、初赛不考察"，也没给内容字段格式。
		// 命令字合法（不同于 K-03 那种非法命令字）但未实现，所以回一帧错误应答：
		// 静默丢弃在测评员看来和死机没区别。
		// 要实现就把下面这行换成 CMD_LOG_xxx_FUNCTION()，和别的命令一样。
		case CMD_LOG_READ:        // 查询操作日志
			Protocol_SendFrame(device_id, TYPE_ERROR, CMD_SPEC_ERROR_REPLY, NULL, 0);
			break;

		case CMD_LOG_CLEAR:       // 清除操作日志
			Protocol_SendFrame(device_id, TYPE_ERROR, CMD_SPEC_ERROR_REPLY, NULL, 0);
			break;

		/* ===== TF 卡文件类（决赛新增）=====
		 * 命令字和载荷格式全在 Function/file_service.h，现场只改那一个文件。
		 * 这里只做分发，处理函数在 Function/file_service.c —— 和赛题 2.1 的分层
		 * 要求一致：Protocol 层只管组帧 / 分发，业务逻辑在 Function 层。
		 * 刻意不校验 frame_type：现场用哪个帧类型下发文件命令是未知的，宁可放宽。
		 * 现场只考其中一两条命令时，把用不上的 case 注释掉即可，留着也不碍事。 */
		case FS_CMD_STATUS:     CMD_FILE_STATUS_FUNCTION();                     break;
		case FS_CMD_LIST:       CMD_FILE_LIST_FUNCTION(payload, payload_len);   break;
		case FS_CMD_READ:       CMD_FILE_READ_FUNCTION(payload, payload_len);   break;
		case FS_CMD_WRITE:      CMD_FILE_WRITE_FUNCTION(payload, payload_len);  break;
		case FS_CMD_DELETE:     CMD_FILE_DELETE_FUNCTION(payload, payload_len); break;
		case FS_CMD_INFO:       CMD_FILE_INFO_FUNCTION(payload, payload_len);   break;
		case FS_CMD_REC_START:  CMD_FILE_REC_START_FUNCTION();                  break;
		case FS_CMD_REC_STOP:   CMD_FILE_REC_STOP_FUNCTION();                   break;
		case FS_CMD_READ_LINE:  CMD_FILE_READ_LINE_FUNCTION(payload, payload_len); break;

		// 特殊帧类型
		case CMD_SPEC_DISCOVER: // 上位机广播寻找设备 (0xFFFF)
            // 特殊命令表要求帧类型必须是 0x05
            if (frame_type == TYPE_HEARTBEAT) {
                CMD_SPEC_DISCOVER_FUNCTION();
            }
            break;
		
        default:
            // 未知命令字，发送错误应答
            Protocol_SendFrame(device_id, TYPE_ERROR, CMD_SPEC_ERROR_REPLY, NULL, 0);
            break;
    }
}



/* ================= 串口接收中断接口 ================= */

void Protocol_ParseChar(char c) {
    // 入缓冲区
    if (rx_index < RX_BUF_SIZE) {
        rx_buffer[rx_index++] = c;
    } else {
        rx_index = 0; // 溢出保护
        return; 
    }

    // 查找帧尾 "B6A5"
    if (rx_index >= 4) {
        if (rx_buffer[rx_index-4] == 'B' && rx_buffer[rx_index-3] == '6' &&
            rx_buffer[rx_index-2] == 'A' && rx_buffer[rx_index-1] == '5') {
            
            // 找到帧尾，往前找帧头 "A5B6"
            int16_t start_idx = -1;
            for (int16_t i = 0; i <= rx_index - 26; i++) { // 最小ASCII帧长26
                if (rx_buffer[i] == 'A' && rx_buffer[i+1] == '5' &&
                    rx_buffer[i+2] == 'B' && rx_buffer[i+3] == '6') {
                    start_idx = i;
                    break;
                }
            }

            if (start_idx >= 0) {
                // 截取 A5B6...B6A5 之间的 ASCII 字符串
                uint16_t ascii_frame_len = rx_index - start_idx;
                
                // ASCII 转二进制数组
                // 288 = 13(固定部分+CRC+帧尾) + 255(payload 上限) 取整，文件读写会顶到这个上限
                uint8_t raw_frame[288];
                int raw_len = HexStrToBytes(&rx_buffer[start_idx], ascii_frame_len,
                                            raw_frame, sizeof(raw_frame));

                // 先把校验结论算出来，再决定怎么回。分成两步是为了操作日志：
                // 收到的这一帧必须在【回复之前】记下来，否则合并排序时会出现
                // "先回了应答才收到请求"
                uint16_t calc_crc = 0, recv_crc = 0;
                uint8_t  frame_ok = 0;

                if (raw_len >= 13) {
                    // CRC 范围：除最后 4 字节(CRC+帧尾)外的全部数据
                    calc_crc = crc16_modbus(raw_frame, raw_len - 4);
                    recv_crc = MAKE_UINT16(raw_frame[raw_len - 4], raw_frame[raw_len - 3]);
                    frame_ok = (calc_crc == recv_crc) ? 1 : 0;
                }

                // 操作日志：记上线的 ASCII 原文，和上位机发出去的字符一模一样。
                // 转码失败 / 帧长不够 / CRC 不对都记，标记成 BAD
                oplog_add(OPLOG_PROTO_CUSTOM, OPLOG_DIR_RX,
                          (const uint8_t *)&rx_buffer[start_idx], ascii_frame_len, frame_ok);

                // 校验长度与 CRC
                if (raw_len < 0) {
                    // 转码失败（奇数长度/非法字符），按帧长不匹配处理
                    Protocol_SendFrame(MY_DEVICE_ID, TYPE_ERROR, CMD_SPEC_ERROR_REPLY, NULL, 0);
                } else if (raw_len < 13) {
                    // 帧长不匹配
                    Protocol_SendFrame(MY_DEVICE_ID, TYPE_ERROR, CMD_SPEC_ERROR_REPLY, NULL, 0);
                } else if (!frame_ok) {
                    // CRC 失败
                    Protocol_SendFrame(MY_DEVICE_ID, TYPE_ERROR, CMD_SPEC_ERROR_REPLY, NULL, 0);
                } else {
                    // 校验通过，进入处理
                    Protocol_HandleFrame(raw_frame, (uint16_t)raw_len);
                }
            }
            
            // 一帧处理完毕，清空接收状态
            rx_index = 0; 
        }
    }
}

/* ================= 组装并发送接口 ================= */
void Protocol_SendFrame(uint16_t target_id, uint8_t frame_type, uint16_t cmd, uint8_t* payload, uint8_t payload_len) {
    // 288 = 9(帧头~版本) + 255(payload 上限) + 2(CRC) + 2(帧尾) 取整
    uint8_t raw_buf[288];
    uint16_t idx = 0;

    // 组装二进制帧
    raw_buf[idx++] = 0xA5;                 // 起始标志 H
    raw_buf[idx++] = 0xB6;                 // 起始标志 L
    raw_buf[idx++] = HI_UINT16(target_id); // 设备ID
    raw_buf[idx++] = LO_UINT16(target_id);
    raw_buf[idx++] = frame_type;           // 帧类型
    raw_buf[idx++] = HI_UINT16(cmd);       // 命令字
    raw_buf[idx++] = LO_UINT16(cmd);
    raw_buf[idx++] = payload_len;          // 报文长度
    raw_buf[idx++] = PROTOCOL_VERSION;     // 协议版本

    // payload
    if (payload_len > 0 && payload != NULL) {
        memcpy(&raw_buf[idx], payload, payload_len);
        idx += payload_len;
    }

    // CRC16 (范围 0 ~ idx-1)
    uint16_t crc = crc16_modbus(raw_buf, idx);
    
    // 追加 CRC 和帧尾
    raw_buf[idx++] = HI_UINT16(crc);
    raw_buf[idx++] = LO_UINT16(crc);
    raw_buf[idx++] = 0xB6;
    raw_buf[idx++] = 0xA5;

    // 整帧转 ASCII 字符串。BytesToHexStr 不写 '\0'，长度靠 ascii_len 单独算，
    // 所以不用留终止符位；600 = 288 × 2 再留余量
    char ascii_tx_buf[600];
    BytesToHexStr(raw_buf, idx, ascii_tx_buf);

    // ASCII 长度 = 字节数 * 2
    uint16_t ascii_len = idx * 2;

    /* 操作日志：自定义协议的封帧应答全都从这里出去（普通应答、错误帧、心跳、
       定时自动上报），记一处就够。不封帧的纯字符串回复（告警、睡眠唤醒）
       不走这里，在各自的发送点单独记 */
    oplog_add(OPLOG_PROTO_CUSTOM, OPLOG_DIR_TX, (const uint8_t*)ascii_tx_buf, ascii_len, 1);

    // 底层驱动发送
    bsp_rs485_send_data((uint8_t*)ascii_tx_buf, ascii_len);
}



