#ifndef __GENERAL_PROTOCOL_H__
#define __GENERAL_PROTOCOL_H__

#include "Headfile.h"

/* ================= 协议固定值定义 ================= */
#define PROTOCOL_VERSION    0x02 // 这是设备的版本号
#define BROADCAST_ID        0xFFFF


/* ================= 帧类型定义 ================= */
#define TYPE_CMD_SEND       0x01  // 上位机 -> 设备
#define TYPE_ACK            0x02  // 设备 -> 上位机
#define TYPE_HEARTBEAT      0x05  // 心跳/双向
#define TYPE_ERROR          0xFF  // 异常报警

/* ================= 业务命令字宏定义================= */

// 1. 系统管理类 (0x01)
#define CMD_SYS_REBOOT              0x0101  // 设备重启
#define CMD_SYS_FACTORY_RESET       0x0102  // 恢复出厂设置（预留）
#define CMD_SYS_READ_INFO           0x0103  // 查询设备信息（预留）
#define CMD_SYS_READ_VER            0x0104  // 查询固件版本
#define CMD_SYS_SET_TIME            0x0105  // 设置设备时间
#define CMD_SYS_READ_TIME           0x0106  // 查询设备时间
#define CMD_SYS_SET_ID              0x01A1  // 设置设备 ID
#define CMD_SYS_SET_BAUDRATE        0x01A2  // 设置波特率
#define CMD_SYS_READ_ID             0x0111  // 查询设备 ID
#define CMD_SYS_READ_BAUDRATE       0x0112  // 查询波特率

// 2. 数据类 (0x02)
#define CMD_DATA_READ_CH0           0x0201  // 查询 CH0 数据（滑动变阻器）
#define CMD_DATA_READ_CH1           0x0202  // 查询 CH1 数据（DAC 回读）
#define CMD_DATA_READ_PT100         0x0221  // 查询特定通道数据（外部ADC PT100）
#define CMD_DATA_SET_CH0_RATIO      0x0241  // 设置 CH0 变比
#define CMD_DATA_SET_CH1_RATIO      0x0242  // 设置 CH1 变比
#define CMD_DATA_SET_REPORT_INTVL   0x0261  // 设置数据上报时间间隔

// 3. 控制类 (0x03)
#define CMD_CTRL_SET_DAC_VOLT       0x0301  // 设置 DAC 输出电压
#define CMD_CTRL_AUTO_REPORT_START  0x0302  // 定时自动上报数据开始
#define CMD_CTRL_AUTO_REPORT_STOP   0x0303  // 定时自动上报数据停止
#define CMD_CTRL_ENTER_SLEEP        0x03AA  // 进入睡眠模式

// 4. 参数配置类 (0x04)
#define CMD_CFG_READ_THRES_ALL      0x0400  // 读取阈值参数 (批量读取CH0, CH1)
#define CMD_CFG_READ_THRES_CH0      0x0401  // 读取 CH0 阈值参数
#define CMD_CFG_READ_THRES_CH1      0x0402  // 读取 CH1 阈值参数
#define CMD_CFG_READ_THRES_CH2      0x0403  // 读取 CH2 阈值参数
#define CMD_CFG_WRITE_THRES_CH0     0x0411  // 写入 CH0 阈值参数
#define CMD_CFG_WRITE_THRES_CH1     0x0412  // 写入 CH1 阈值参数
#define CMD_CFG_WRITE_THRES_CH2     0x0413  // 写入 CH2 阈值参数

// 5. 系统升级类 (0x05)
#define CMD_OTA_REQUEST             0x0501  // 升级请求
#define CMD_OTA_READY_DATA          0x0502  // 准备传输固件数据包
#define CMD_OTA_EXECUTE             0x0503  // 执行升级流程

// 6. 告警与日志类 (0x06)
#define CMD_ALARM_SET_AUTO_REPORT   0x0601  // 是否主动上报告警
#define CMD_ALARM_READ_RECORD       0x0602  // 查询告警记录
#define CMD_ALARM_CLEAR             0x0603  // 清除告警
#define CMD_LOG_READ                0x0604  // 查询操作日志（预留）
#define CMD_LOG_CLEAR               0x0605  // 清除操作日志（预留）

/* ================= 特殊命令字 ================= */
// 搭配心跳帧 (TYPE_HEARTBEAT 0x05) 使用
#define CMD_SPEC_HEARTBEAT          0x8888  // 设备心跳 / 上电通知帧 (设备 -> 上位机)
#define CMD_SPEC_DISCOVER           0xFFFF  // 上位机广播寻找设备 (上位机 -> 设备)

// 搭配错误帧 (TYPE_ERROR 0xFF) 使用
#define CMD_SPEC_ERROR_REPLY        0xEEEE  // 错误应答帧指示 (当不知原指令时使用)
// 注意：文档中提到如果是对特定命令的错误回复，命令字也可能是“原始命令字”

/* ================= 大端序辅助宏 ================= */
#define MAKE_UINT16(high, low)  ((uint16_t)(((uint16_t)(high) << 8) | (uint16_t)(low)))
#define HI_UINT16(val)          ((uint8_t)(((uint16_t)(val) >> 8) & 0xFF))
#define LO_UINT16(val)          ((uint8_t)((uint16_t)(val) & 0xFF))

// 外部声明
//设备ID号
extern uint16_t MY_DEVICE_ID;


/* ================= 函数声明 ================= */
// 串口中断调用的字符解析函数
void Protocol_ParseChar(char c);

// 核心命令处理
void Protocol_HandleFrame(uint8_t* raw_frame, uint16_t raw_len);

// 发送数据帧 (内部会自动组装二进制 -> 转ASCII -> 发送)
void Protocol_SendFrame(uint16_t target_id, uint8_t frame_type, uint16_t cmd, uint8_t* payload, uint8_t payload_len);
void float_to_big_endian_bytes(float val, uint8_t *bytes);




#endif /* __GENERAL_PROTOCOL_H__ */


