#ifndef __PROTOCOL_ROUTER_H__
#define __PROTOCOL_ROUTER_H__

#include "Headfile.h"

// 运行协议模式：1 = 强制自定义协议, 2 = 强制 Modbus RTU, 3 = 强制 Modbus ASCII,
// 4 = 自动分流（默认，按帧内容嗅探）
extern uint8_t Protocol_mode;

// Modbus 从站地址（规范：1~247 有效，0 广播，248~255 保留）。
// 和自定义协议的设备 ID 是两套独立身份，开机由 main.c 从 flash 载入。
extern uint8_t Modbus_slave_addr;

// 按 Protocol_mode 分发到自定义协议 / Modbus RTU / Modbus ASCII / 自动嗅探
void Protocol_Route(uint8_t *data, uint16_t len);

/* 总线上是不是有 Modbus 主站在轮询。
 * 返回 1 表示【现在不能主动往总线上发帧】—— Modbus 从站只能被动应答，
 * 主动发会和主站的轮询撞车（详见 modbus_data_map.h【2】的说明）。
 * main.c 用它来决定这一轮要不要执行自定义协议的定时自动上报。
 * 判定依据：最近收到的一帧是校验通过的 Modbus 报文，或 Protocol_mode 被强制成 2/3。 */
uint8_t Protocol_ModbusMasterActive(void);

#endif
