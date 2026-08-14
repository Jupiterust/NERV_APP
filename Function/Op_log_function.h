#ifndef __OP_LOG_FUNCTION_H_
#define __OP_LOG_FUNCTION_H_

#include "bsp_sys.h"

/* ============================================================================
 *  操作日志：记录"什么时间、哪个协议、哪个方向、发了什么报文"
 *
 *  【为什么要有这个】
 *  初赛赛题 4.5.7(2) 对未知帧类型的要求原文是"丢弃该帧，【记录日志】，返回错误
 *  应答"，命令字表里 0x0604 查询操作日志 / 0x0605 清除操作日志又都标着"预留、
 *  初赛不考察"—— 也就是说这两条命令的存在是明确的，只是格式决赛现场才给。
 *  决赛样题里"能够按照串口交互报文要求进行数据交互"同理。
 *
 *  【6 个环形缓冲区】
 *  三种协议 × 收发两个方向，各一个独立的环：
 *
 *                    RX（上位机 -> 本机）      TX（本机 -> 上位机）
 *      自定义协议     OPLOG_PROTO_CUSTOM        同左
 *      Modbus RTU     OPLOG_PROTO_RTU           同左
 *      Modbus ASCII   OPLOG_PROTO_ASCII         同左
 *
 *  存满之后覆盖最旧的一条，永远留着最近 OPLOG_DEPTH 条。
 *
 *  【跨协议的先后顺序靠 seq，不靠时间戳】
 *  6 个环各存各的，光看时间戳没法还原"先收到 RTU 还是先收到自定义帧"——
 *  RTC 只有秒级分辨率，一秒内能收发好几帧。所以每条记录都带一个全局递增的
 *  seq（6 个环共用一个计数器），要按时间顺序看就按 seq 排序，
 *  oplog_get_merged() / oplog_dump_all() 已经做好了。
 *
 *  【只在 RAM 里，掉电就没】
 *  赛题 2.4 要求持久化的是【告警记录】，不含操作日志，而操作日志的写入频率是
 *  每收发一帧一次，落 flash 会把扇区写穿（param/告警区都没有磨损均衡）。
 *  真要留档，往 TF 卡上追加更合适 —— bsp_sdio_file_append_text() 现成的。
 *
 *  【不是中断安全的】
 *  所有记录点都在主循环里（Protocol_Route 及其下游、Protocol_SendFrame、
 *  告警上报），没有中断上下文，所以没加临界区。要在 ISR 里调 oplog_add()，
 *  得先关中断保护 s_seq 和写指针。
 * ========================================================================== */


/* ==================== 【1】现场可调的部分 ==================== */

/* 每个环存几条。6 个环 × OPLOG_DEPTH 条 × sizeof(oplog_entry_t) 就是总 RAM 开销，
 * 默认 6 × 8 × 80 ≈ 3.8KB（本机 192KB RAM，放得下，要加深直接改）。 */
#define OPLOG_DEPTH             8

/* 每条最多存报文的前多少个字节，超出的部分丢掉但 raw_len 里记的是真实长度，
 * 所以查询时看得出来"这条被截断了"。
 * 参考长度：Modbus RTU 读保持寄存器请求 8 字节、应答 7+2N；
 *           Modbus ASCII 是 RTU 的两倍多；
 *           自定义协议一帧最短 26 个 ASCII 字符，带 12 字节负载的自动上报 50 个。 */
#define OPLOG_DATA_MAX          64

/* 校验失败的帧要不要也记下来。
 *   1（默认）记，BAD 标记会出现在查询结果里 —— 赛题 4.5.7(2) 要的就是这个
 *   0        只记校验通过的，总线噪声多的时候不会把环刷掉 */
#define OPLOG_RECORD_BAD        1

/* 显示时区偏移，小时。存进去的是 UTC 秒（赛题 4.5.1 规定时间戳是 UTC），
 * 显示时加这个偏移，和 Log_recording_function.c 里告警记录的 +8 保持一致。 */
#define OPLOG_TZ_OFFSET_HOUR    8

/* 报文正文怎么显示：
 *   0（默认）自动 —— RTU 是二进制，转成十六进制；自定义协议和 Modbus ASCII
 *            本来就是可打印字符，直接原样显示（"A5B60001..."、":010300..."）
 *   1        一律转十六进制，不管哪种协议 */
#define OPLOG_FORCE_HEX         0

/* 格式化一条记录需要多大的缓冲区，传给 oplog_format() 的 buf 至少要这么大 */
#define OPLOG_LINE_MAX          (OPLOG_DATA_MAX * 2 + 64)

/* -------- 落盘到 TF 卡（决赛可选功能）--------
 * 总开关。0 = 落盘相关的运行时开销（RAM 队列 + 主循环里的落盘逻辑）全部编译掉，
 * oplog_sd_poll() 退化成空函数，行为和不加这个功能之前完全一致。
 * 下面几项只有这个开关为 1 时才真正生效，为了 #define 位置固定不额外套 #if。 */
#define OPLOG_SD_ENABLE            1

/* 落盘文件名。ffconf.h 里 _USE_LFN=1，长文件名可以用，但必须是纯 ASCII
 *（中文名会被 FR_INVALID_NAME 拒绝，见 ffconf.h 的 Project note）。 */
#define OPLOG_SD_FILENAME          "OPLOG.TXT"

/* 待落盘队列深度，独立于上面 6 个 RAM 环，不占用 OPLOG_DEPTH 的名额，也不会被
 * 那 6 个环的覆盖行为连累。存在的意义：主循环被 flash 擦除阻塞期间（比如 0x0603
 * 清告警记录，最长约 400ms）产生的记录要能扛住不丢。按 Modbus 主站 10ms 一帧的
 * 极限估算，400ms 内最多约 40 条，这里打了余量给 64。
 * 每条 sizeof(oplog_entry_t) 随 OPLOG_DATA_MAX 变，默认配置下约 80 字节，
 * 64 条约 5KB（本机 192KB RAM，放得下）。 */
#define OPLOG_SD_QUEUE_DEPTH       64

/* 队列里攒够几条触发一次落盘——减少开关文件次数，每次 open+write+close 都有
 * 固定开销，不是攒得越少越及时就越好。 */
#define OPLOG_SD_FLUSH_COUNT       8

/* 即使没攒够 FLUSH_COUNT，队列里有记录且超过这么多秒没落盘也强制落一次，
 * 避免总线空闲时已入队的几条记录一直堆在 RAM 里、迟迟不落卡。 */
#define OPLOG_SD_FLUSH_INTERVAL_S  5

/* 文件第一次真正落盘前是否先写一行表头，方便直接用 Excel/记事本打开核对列。 */
#define OPLOG_SD_WRITE_HEADER      1
#define OPLOG_SD_HEADER_TEXT       "time | dir | proto | rawlen | data\r\n"


/* ==================== 【2】类型定义 ==================== */

typedef enum {
    OPLOG_PROTO_CUSTOM = 0,     /* 自定义协议（A5B6 开头的 ASCII 十六进制帧） */
    OPLOG_PROTO_RTU    = 1,     /* Modbus RTU（二进制）                        */
    OPLOG_PROTO_ASCII  = 2,     /* Modbus ASCII（':' 开头的十六进制文本）      */
    OPLOG_PROTO_MAX    = 3
} oplog_proto_t;

typedef enum {
    OPLOG_DIR_RX  = 0,          /* 上位机 -> 本机 */
    OPLOG_DIR_TX  = 1,          /* 本机 -> 上位机 */
    OPLOG_DIR_MAX = 2
} oplog_dir_t;

typedef struct {
    uint32_t seq;                       /* 全局序号，6 个环共用，越大越新，0 = 空记录 */
    uint32_t timestamp;                 /* UTC 秒，取自 bsp_rtc_get_unix_timestamp() */
    uint16_t raw_len;                   /* 报文真实长度（可能大于 len）               */
    uint8_t  len;                       /* 实际存下来的字节数，≤ OPLOG_DATA_MAX       */
    uint8_t  ok;                        /* 1 = 校验通过/正常发出，0 = 校验失败        */
    uint8_t  proto;                     /* oplog_proto_t，合并排序后还认得出是哪种    */
    uint8_t  dir;                       /* oplog_dir_t                                */
    uint8_t  data[OPLOG_DATA_MAX];      /* 报文正文（原始上线字节）                   */
} oplog_entry_t;


/* ==================== 【3】记录 ==================== */

/* 清空 6 个环并把 seq 归零。静态变量本来就是 0，不调也能用，
 * 想在开机时明确复位一次就在 main() 里调一下。 */
void oplog_init(void);

/* 记一条。data/len 是原始上线字节，len 超过 OPLOG_DATA_MAX 会截断（raw_len 记真实值）。
 * ok = 1 表示校验通过 / 正常发出，0 表示 CRC、LRC、帧长之类没过。
 * len == 0 或 data == NULL 直接忽略，不会记一条空的。
 *
 * 记录点已经接好的有 8 处，不用自己加：
 *   RX  Protocol_ParseChar()       自定义协议整帧（带 CRC 判定结果）
 *       Modbus_ProcessRTUFrame()   RTU 整帧（带 CRC 判定结果）
 *       Modbus_ProcessASCIIFrame() ASCII 整帧（带 LRC 判定结果）
 *   TX  Protocol_SendFrame()       自定义协议所有封帧应答（含心跳、自动上报）
 *       Modbus_ProcessRTUFrame()   RTU 应答
 *       Modbus_ProcessASCIIFrame() ASCII 应答
 *       Function.c                 睡眠唤醒字符串
 *       Log_recording_function.c   告警主动上报字符串
 * 赛题 2.2 说的"不封帧的纯字符串回复"（告警、唤醒）统一归到自定义协议这一类。 */
void oplog_add(oplog_proto_t proto, oplog_dir_t dir,
               const uint8_t *data, uint16_t len, uint8_t ok);


/* ==================== 【4】查询 ==================== */

/* 某个环当前存了几条（0 ~ OPLOG_DEPTH） */
uint16_t oplog_get_count(oplog_proto_t proto, oplog_dir_t dir);

/* 某个环累计记过几条（含已经被覆盖掉的），用来看有没有丢记录 */
uint32_t oplog_get_total(oplog_proto_t proto, oplog_dir_t dir);

/* 取某个环里的第 index 条，【index 0 = 最新一条】，越大越旧。
 * 越界或该条为空返回 NULL。和告警查询 0x0602 的倒序一致。 */
const oplog_entry_t* oplog_get(oplog_proto_t proto, oplog_dir_t dir, uint16_t index);

/* 6 个环合并成一条时间流，取第 index 条，【index 0 = 全局最新】。
 * 现场如果要求"按时间顺序列出最近 N 条操作日志"，直接循环这个就行：
 *
 *      char line[OPLOG_LINE_MAX];
 *      for (uint16_t i = 0; i < 10; i++) {
 *          const oplog_entry_t *e = oplog_get_merged(i);
 *          if (e == NULL) break;
 *          oplog_format(e, line, sizeof(line));
 *          bsp_rs485_send_data((const uint8_t *)line, strlen(line));
 *      }
 */
const oplog_entry_t* oplog_get_merged(uint16_t index);

/* 全部 6 个环一共存了几条，配合 oplog_get_merged() 用 */
uint16_t oplog_get_merged_count(void);


/* ==================== 【5】清除 ==================== */

void oplog_clear(oplog_proto_t proto, oplog_dir_t dir);   /* 清一个环 */
void oplog_clear_all(void);                               /* 6 个环全清，seq 也归零 */


/* ==================== 【6】格式化 / 调试打印 ==================== */

/* 把一条记录格式化成一行文本，末尾不带换行。buf 至少 OPLOG_LINE_MAX 字节。
 * 返回写进去的字符数，出错返回 -1。格式：
 *
 *      2026-01-01 12:00:00 | RX | CUSTOM | 26 | A5B6000101010002...
 *      2026-01-01 12:00:01 | TX | RTU    | 7  | 01 03 04 41 AC B8 52
 *      2026-01-01 12:00:02 | RX | RTU    | 8  | BAD 01 03 00 00 00 02 C4 0A
 *
 *      时间 | 方向 | 协议 | 报文真实长度 | 正文
 *      校验没过的会在正文前多一个 "BAD "，截断过的正文末尾是 "..."
 *
 * 分隔符用 " | " 是照着告警记录（赛题 4.5.6）的格式来的，现场要改格式就改
 * 这一个函数，查询侧不用动。 */
int oplog_format(const oplog_entry_t *e, char *buf, uint32_t buf_size);

/* 往调试串口打印，现场排查用。printf 很慢，别放进主循环轮询 */
void oplog_dump(oplog_proto_t proto, oplog_dir_t dir);   /* 打一个环 */
void oplog_dump_all(void);                               /* 6 个环按时间顺序合并打印 */

/* "CUSTOM" / "RTU" / "ASCII"，越界返回 "?" */
const char* oplog_proto_name(oplog_proto_t proto);


/* ==================== 【7】落盘到 TF 卡 ==================== */

/* 主循环里调用，实际做文件 I/O 的地方 —— 放在 Protocol_Route() 那个 if 块之后，
 * 保证这一帧的应答已经交给串口中断发送了，这时候再动卡不会拖慢应答。
 * 队列空、SD 未就绪、或还没攒够触发条件时立即返回，不阻塞。
 * OPLOG_SD_ENABLE=0 时这是个空函数，main.c 不用加 #if 也能调。 */
void oplog_sd_poll(void);

/* 因为落盘队列满而被丢弃的记录数，"日志文件里少了几条"排查时看这个。
 * OPLOG_SD_ENABLE=0 时恒返回 0。 */
uint32_t oplog_sd_get_dropped(void);

#endif
