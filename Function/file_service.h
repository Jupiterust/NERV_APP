#ifndef __FILE_SERVICE_H_
#define __FILE_SERVICE_H_

#include "bsp_sys.h"

/* ============================================================================
 *  TF 卡文件服务 —— 决赛现场唯一需要编辑的文件
 *
 *  决赛样题对 TF 卡只有一句话："能实现 TF 卡的文件读写功能"，命令字、载荷格式、
 *  文件名、CSV 列序全部现场才给。所以这里把每一个可能被现场改掉的东西都做成
 *  #define，file_service.c 里不留任何字面量。
 *
 *  现场 5 分钟改动指南见 TF卡_决赛预制代码包.md 第二节。
 *  一句话版本：题目说什么，就在下面找对应的 #define 改，.c 一行都不用动。
 * ========================================================================== */


/* ==================== 【1】命令字 ==================== */

/* 功能模块高字节。赛题已占用 0x01~0x06，文件类猜 0x07。
 * 现场说"文件命令在 0x08xx"就只改这一行，整类一起平移。 */
#define FS_CMD_CLASS            0x07
#define FS_CMD(low)             ((uint16_t)(((FS_CMD_CLASS) << 8) | (low)))

#define FS_CMD_STATUS           FS_CMD(0x01)   /* 卡状态 / 总容量 / 剩余容量 */
#define FS_CMD_LIST             FS_CMD(0x02)   /* 列目录，一次一条            */
#define FS_CMD_READ             FS_CMD(0x03)   /* 分片读（offset + len）      */
#define FS_CMD_WRITE            FS_CMD(0x04)   /* 分片写（覆盖 / 追加）        */
#define FS_CMD_DELETE           FS_CMD(0x05)   /* 删除文件                    */
#define FS_CMD_INFO             FS_CMD(0x06)   /* 文件是否存在 + 大小          */
#define FS_CMD_REC_START        FS_CMD(0x07)   /* 开始把采样写进 CSV           */
#define FS_CMD_REC_STOP         FS_CMD(0x08)   /* 停止记录                    */
#define FS_CMD_READ_LINE        FS_CMD(0x09)   /* 按行读，带续传 offset        */

/* 单条命令字被现场单独指定时（比如"读文件是 0x0712"），把上面对应那行的
 * FS_CMD(0x03) 直接换成 0x0712 即可，其余命令不受影响。 */


/* ==================== 【2】载荷布局 ==================== */

/* 文件名字段的编码方式：
 *   0 = 长度前缀模式 [名长(1)][名(N)]        <- 默认，无歧义
 *   N = 定长 N 字节，不足补 '\0' 或空格
 * 现场说"文件名固定 12 字节"就把这里改成 12。 */
#define FS_NAME_FIXED_LEN       0

/* 偏移量字段占几个字节（大端）。文件小于 64KB 时现场可能只给 2 字节。
 * 取值 1~4。 */
#define FS_OFFSET_BYTES         4

/* 单帧最多带多少字节文件数据。
 * 上限受 报文长度 字段（1 字节 = 255）限制，应答里还要留 1 字节长度头，
 * 留余量取 200。现场说"分片 128"就改这里。 */
#define FS_CHUNK_MAX            200

/* 文件名缓冲长度，跟 bsp_sdio.h 的 SD_PATH_MAX 对齐 */
#define FS_NAME_MAX             SD_PATH_MAX

/* 列目录一次最多扫描几条（FS_CMD_LIST 按序号一次回一条，这个是内部扫描上限）。
 * 占 FS_LIST_MAX × sizeof(sd_entry_t) ≈ 24 字节的静态 RAM。 */
#define FS_LIST_MAX             64


/* ==================== 【3】应答风格 ==================== */

/* 动作类命令（写 / 删除 / 开始记录 / 停止记录）成功时回什么：
 *   0 = 回该命令自己的数据（写文件回写入长度，其余回空载荷）
 *   1 = 统一先回 1 字节 0xFF（和赛题其它命令的 OK 一致）   <- 默认 */
#define FS_ACK_STYLE            1

/* 错误应答用哪个命令字：
 *   0 = 用原命令字（便于上位机对号入座）   <- 默认
 *   1 = 统一用 CMD_SPEC_ERROR_REPLY (0xEEEE)，和赛题未知命令的错误帧一致 */
#define FS_ERR_USE_EEEE         0

/* 错误应答是否带 1 字节错误码。现场若要求"错误帧内容为空"就改 0。 */
#define FS_ERR_WITH_CODE        1

/* 错误码。现场给了别的编号就改这里。 */
#define FS_OK                   0x00
#define FS_ERR_NO_CARD          0x01   /* 卡没就绪 / 没插卡         */
#define FS_ERR_NO_FILE          0x02   /* 文件不存在                */
#define FS_ERR_NAME             0x03   /* 文件名非法 / 空           */
#define FS_ERR_PARAM            0x04   /* 载荷长度或字段不合法       */
#define FS_ERR_IO               0x05   /* 读写卡出错                */
#define FS_ERR_FULL             0x06   /* 空间不足                  */
#define FS_ERR_EOF              0x07   /* 已到文件尾                */


/* ==================== 【4】数据记录（采集 -> 存储） ==================== */

/* 记录文件名。_USE_LFN 已是 1，长名（ASCII）可以直接写，
 * 例如 "Sample_Data_2026.csv" 或 "LOG/SAMPLE.CSV"（目录会自动建）。 */
#define FS_REC_FILE             "DATA.CSV"

/* 记录间隔，单位秒。用 RTC 秒计时，不占定时器（TIM5/6/7/9/12 已全部有主）。
 * 最小 1 秒 —— 要更快只能挂到 TIM7 的 g_report_flag 上，见 .md 第十节。 */
#define FS_REC_INTERVAL_S       1

/* 是否在新建文件时写一行表头 */
#define FS_CSV_WRITE_HEADER     1

/* 表头。列序改了，这里和下面两个宏要一起改。 */
#define FS_CSV_HEADER           "Time,CH0,CH1,I0_mA,V0_V,V1_V,Broken\r\n"

/* 数据行格式串 + 参数表。这两个宏必须配对修改。
 * 现场说"只要时间、电流和两路电压"就把两个宏一起删到只剩四列：
 *   #define FS_CSV_ROW_FMT   "%s,%.3f,%.3f,%.3f\r\n"
 *   #define FS_CSV_ROW_ARGS  ts, Data_class_structure.i0_current, \
 *                            Data_class_structure.v0_voltage,     \
 *                            Data_class_structure.v1_voltage
 * ts 是本地变量（时间字符串），可用字段见 Function.h 的 DeviceDataParams_t。 */
#define FS_CSV_ROW_FMT          "%s,%.3f,%.3f,%.3f,%.3f,%.3f,%d\r\n"
#define FS_CSV_ROW_ARGS         ts,                                   \
                                Data_class_structure.ch0_current_val, \
                                Data_class_structure.ch1_current_val, \
                                Data_class_structure.i0_current,      \
                                Data_class_structure.v0_voltage,      \
                                Data_class_structure.v1_voltage,      \
                                (int)Data_class_structure.i0_broken

/* 单行 CSV 的最大长度（含 '\0'）。列数加多了要同步加大。 */
#define FS_CSV_LINE_MAX         128

/* 时区补偿，单位小时。和 Log_recording_function.c 的告警时间戳保持一致（+8）。 */
#define FS_TZ_HOUR              8

/* 记录文件的大小上限，单位字节。0 = 不限。
 * 现场要求"最多记 N 条 / 满了从头覆盖"时填一个数，超过就把文件清空重记
 * （表头会重写）。比如一行约 60 字节、要求最多 1000 条 -> 填 60000。 */
#define FS_REC_MAX_BYTES        0

/* 记录期间是否允许自动上报同时进行：
 *   0 = 开始记录时自动把定时上报停掉  <- 默认
 *   1 = 各管各的
 * 默认关掉的原因：赛题 H-02 规定自动上报期间除"停止上报"外不响应任何命令，
 * 上报开着时连"停止记录"都发不进来。 */
#define FS_REC_COEXIST_REPORT   0


/* ==================== 【5】文件名兜底 ==================== */

/* 1 = 自动把任意文件名转成合法 8.3 短名（大写、非法字符换 '_'、主名截 8 位、
 *     扩展名截 3 位）。
 * 0 = 原样传给 FatFs。                                   <- 默认
 *
 * 当前 ffconf.h 的 _USE_LFN 已经是 1（长名可用，ASCII 名任意长度），所以默认
 * 取 0：现场给的长文件名要一字不差地落在卡上，评委拔卡插电脑是要对名字的。
 * 只有在 _USE_LFN 被改回 0 时才需要把这里置 1 兜底 —— 那时卡上的名字会被截短，
 * 功能不至于整个失败。中文文件名两种设置都存不了（需要 _CODE_PAGE 936）。 */
#define FS_NAME_AUTO_FIX        0


/* ==================== 【6】编译期自检 ==================== */

/* 上面的宏改出范围会直接编译报错（数组长度为负），不会拖到现场才发现。
 * 检查语句放在 file_service.c 开头（和 modbus_data_map.c 的 MB_CHK 一个套路，
 * 放 .c 里是为了不让每个包含 Headfile.h 的文件都重复展开一遍）。
 * 改完宏 build 一次就知道有没有踩线。 */


/* ==================== 【7】对外接口 ==================== */

/* --- 文件服务层（协议无关，Modbus 那边也能直接调） --- */
bool     fs_fix_name(const char* in, char* out, uint32_t out_size);
uint8_t  fs_status(uint32_t* total_kb, uint32_t* free_kb);
uint8_t  fs_info(const char* name, uint32_t* size);
uint8_t  fs_read(const char* name, uint32_t offset, uint8_t want,
                 uint8_t* out, uint8_t* got);
uint8_t  fs_write(const char* name, uint32_t offset, const uint8_t* data,
                  uint8_t len, uint8_t append);
uint8_t  fs_delete(const char* name);

/* --- 数据记录 --- */
void     fs_record_start(void);
void     fs_record_stop(void);
bool     fs_record_active(void);
void     fs_record_poll(void);          /* 放主循环，内部自己判间隔 */
bool     fs_record_last_ok(void);       /* 最近一次写卡是否成功，给 Modbus / OLED 用 */

/* --- 协议命令处理（在 General_Protocol.c 的 switch 里调） --- */
void CMD_FILE_STATUS_FUNCTION(void);
void CMD_FILE_LIST_FUNCTION(uint8_t* p, uint8_t len);
void CMD_FILE_READ_FUNCTION(uint8_t* p, uint8_t len);
void CMD_FILE_WRITE_FUNCTION(uint8_t* p, uint8_t len);
void CMD_FILE_DELETE_FUNCTION(uint8_t* p, uint8_t len);
void CMD_FILE_INFO_FUNCTION(uint8_t* p, uint8_t len);
void CMD_FILE_REC_START_FUNCTION(void);
void CMD_FILE_REC_STOP_FUNCTION(void);
void CMD_FILE_READ_LINE_FUNCTION(uint8_t* p, uint8_t len);

#endif
