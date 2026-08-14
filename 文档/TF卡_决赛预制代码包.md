# TF 卡 · 决赛预制代码包

> 生成于 2026-08-13。**目标：现场拿到题目后，只改 `file_service.h` 里的 `#define`，不动 `.c`。**
> 所有代码都对着当前工作树的真实接口写的（`bsp_sdio.h`、`General_Protocol.h`、`Function.h` 逐个核对过）。
> 相关：`决赛_待办与风险清单.md`（全局问题）、`初赛_功能测试报文清单.md`（初赛报文表）。

---

## 一、题目里关于 TF 卡的原文（全部，就这么多）

**初赛赛题**：完全没有提到 TF 卡。

**决赛样题**（3 页，全文只有 3 处沾边）：

| 位置 | 原文 |
|---|---|
| 一、比赛要求 | 「➢ 能实现 **TF 卡的文件读写功能**。」 |
| 嵌入式程序开发 | 「➢ 能够同时完成多通道数据的**采集、存储**等工作。」 |
| 嵌入式程序开发 | 「➢ 能够按照**串口交互报文要求**进行数据交互。」 |

**没有给的东西**：没有命令字、没有载荷格式、没有文件名、没有目录结构、没有文件格式、没有精度或条数要求。样题明说「现场会给出额外的程序设计要求」「程序设计环节为独立脱机完成」。

**由此得出的两条设计结论**（整个代码包围绕这两条）：

1. **能考的只有"写得进、读得出"**，而且验证多半要通过 RS485 —— 因为初赛就是上位机全自动评分，决赛没理由退回人工。所以必然是「下发写 → 下发读 → 比对」，或「开始记录 → 等 N 秒 → 读回来核对」。
2. **格式全是现场给的**，所以命令字、载荷布局、文件名、CSV 列序**必须全部是 `#define`**，`.c` 里不留任何字面量。

**另外两条容易漏的事实**：
- 决赛现场提供物品清单里**没有 TF 卡**（只有 24VDC/USB 供电、1 路 4~20mA 源、2 路 0~10V 源、RS485 线缆）。**卡要自己带，且要带备份。**
- 现场**禁止任何网络连接**。见第三节 P0-3。

---

## 二、现场 5 分钟改指南 ⭐拿到题先看这页

题目发下来以后，按这个顺序改 `Function/file_service.h`，`.c` 一行都不用动：

| 现场题目说什么 | 改哪个 `#define` |
|---|---|
| 「文件操作命令字用 0x08xx」 | `FS_CMD_CLASS` 改成 `0x08`，整类命令一起平移 |
| 「读文件命令字是 0x0712」 | 单独改 `FS_CMD_READ` |
| 「数据文件名为 LOG.TXT」 | `FS_REC_FILE` |
| 「每 2 秒记录一次」 | `FS_REC_INTERVAL_S` |
| 「CSV 列序为 时间,电流,电压1,电压2」 | `FS_CSV_HEADER` + `FS_CSV_ROW_FMT` + `FS_CSV_ROW_ARGS` 三个一起改 |
| 「不要表头」 | `FS_CSV_WRITE_HEADER` 改 0 |
| 「分片长度 128 字节」 | `FS_CHUNK_MAX` |
| 「文件名字段定长 12 字节」 | `FS_NAME_FIXED_LEN` 改 12（0 = 长度前缀模式） |
| 「错误用 EEEE 应答」 | `FS_ERR_USE_EEEE` 改 1 |
| 「偏移量用 2 字节」 | `FS_OFFSET_BYTES` 改 2 |
| 「成功要回 FF」 | `FS_ACK_STYLE` 改 1 |

**如果现场给的载荷布局和预设的完全不同**：只改 `file_service.c` 里 `【B】命令处理` 那一段的解包代码，`【A】文件服务` 那一段（真正干活的）不用动。两段之间是干净的函数调用边界。

---

## 三、前置补丁（必须先打，否则文件功能一上就死）

### P0-1 ｜协议层缓冲区 —— 不打这个补丁，文件读写第一帧就跑飞

**原因**：赛题的 `报文长度` 是 1 字节，合法 payload 上限 255。文件读写是唯一会把 payload 顶到上限的功能，而现在四处缓冲区都不够。详见 `决赛_待办与风险清单.md` 的 F-1。

`Protocol/Custom_Protocol/General_Protocol.c`，改 4 处：

```c
/* 第 23 行：加 out_size 参数，超限返回 -1 */
static int HexStrToBytes(const char* hex_str, uint16_t str_len,
                         uint8_t* out_bytes, uint16_t out_size)
{
    if (str_len % 2 != 0) return -1;
    if (str_len / 2 > out_size) return -1;      /* ← 新增：越界当帧长错误处理 */
    for (uint16_t i = 0; i < str_len / 2; i++) {
        uint8_t high = CharToHex(hex_str[i*2]);
        uint8_t low  = CharToHex(hex_str[i*2+1]);
        if (high == 0xFF || low == 0xFF) return -1;
        out_bytes[i] = (high << 4) | low;
    }
    return str_len / 2;
}

/* 第 412 行 */
uint8_t raw_frame[288];                                       /* 原 [256] */
int raw_len = HexStrToBytes(&rx_buffer[start_idx], ascii_frame_len,
                            raw_frame, sizeof(raw_frame));    /* ← 补第 4 个参数 */

/* 第 457 行 */
uint8_t raw_buf[288];                                         /* 原 [256] */

/* 第 487 行 */
char ascii_tx_buf[600];                                       /* 原 [512] */
```

> 288 = 13 + 255 再取整；600 = 288 × 2 再留余量。`BytesToHexStr()` 不写 `'\0'`，长度靠 `ascii_len` 单独算，所以不用额外留终止符位。

**验收**：发一帧 payload=255 的合法命令，设备正常应答不复位；发一帧 1000 字符的伪帧，设备回错误应答不复位。

---

### P0-2 ｜驱动层补两个函数：二进制追加 + 定位写

**原因**：`bsp_sdio.h:101` 只有 `bsp_sdio_file_append_text(const char*)`，靠 `'\0'` 结尾算长度 —— 二进制数据里出现 `0x00` 就截断。分片写必须要显式长度的版本。

`Driver/bsp_sdio.h`，在第 106 行 `bsp_sdio_file_read` 后面加两行声明：

```c
/* 二进制追加写：长度显式给出，内容可以含 0x00。
 * append_text 靠 strlen 算长度，二进制数据遇到 0x00 会被截断，所以分片写用这个。 */
bool bsp_sdio_file_append(const char* filename, const void* data, uint32_t len);

/* 定位写：从 offset 处开始覆盖 len 个字节，文件不存在就新建。
 * offset 超过当前文件大小时，FatFs 会把中间用 0 填上（稀疏写）。
 * 上位机乱序下发分片时用这个；顺序下发用 append 更快。 */
bool bsp_sdio_file_write_at(const char* filename, const void* data,
                            uint32_t len, uint32_t offset);
```

`Driver/bsp_sdio.c`，追加实现（放在 `bsp_sdio_file_write` 附近）：

```c
bool bsp_sdio_file_append(const char* filename, const void* data, uint32_t len)
{
    FIL      f;
    UINT     written = 0;
    char     path[SD_PATH_MAX];
    FRESULT  res;

    if (!bsp_sdio_is_ready() || filename == NULL) return false;
    if (data == NULL || len == 0)                 return false;

    /* 长名反查；解析不到就按原样试（新建文件时卡上还没有这个名字） */
    if (!bsp_sdio_resolve_path(filename, path, sizeof(path))) {
        strncpy(path, filename, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    }

    res = f_open(&f, path, FA_WRITE | FA_OPEN_ALWAYS);
    if (res != FR_OK) { printf("[SD] append open %s fail %d\r\n", path, res); return false; }

    res = f_lseek(&f, f_size(&f));          /* 定位到文件尾 */
    if (res == FR_OK) res = f_write(&f, data, len, &written);
    f_close(&f);

    return (res == FR_OK && written == len);
}

bool bsp_sdio_file_write_at(const char* filename, const void* data,
                            uint32_t len, uint32_t offset)
{
    FIL      f;
    UINT     written = 0;
    char     path[SD_PATH_MAX];
    FRESULT  res;

    if (!bsp_sdio_is_ready() || filename == NULL) return false;
    if (data == NULL || len == 0)                 return false;

    if (!bsp_sdio_resolve_path(filename, path, sizeof(path))) {
        strncpy(path, filename, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    }

    res = f_open(&f, path, FA_WRITE | FA_OPEN_ALWAYS);
    if (res != FR_OK) { printf("[SD] write_at open %s fail %d\r\n", path, res); return false; }

    res = f_lseek(&f, offset);
    if (res == FR_OK) res = f_write(&f, data, len, &written);
    f_close(&f);

    return (res == FR_OK && written == len);
}
```

> FatFs R0.09 有 `f_lseek` 和 `f_size`（`f_size` 是宏，`ff.h` 里定义）。`_FS_READONLY = 0`，写是开着的。

---

### P0-3 ｜长文件名（LFN）—— ⚠️只能赛前做，现场没网

`FatFt/ffconf.h:93` 是 `_USE_LFN 0`，**写入只认 8.3 短名**。要开长名需要 `option/ccsbcs.c`，**这个文件不在本仓库**，而决赛现场禁网。

**两条路，建议都做：**

**路线 A（赛前，有网时做）**：
1. 弄到 FatFs R0.09 的 `option/ccsbcs.c`，加进 Keil 工程和 EIDE 工程；
2. `ffconf.h:93` → `#define _USE_LFN 1`；
3. `ffconf.h:60` → `#define _CODE_PAGE 437`（或 `936`，但 936 要 `cc936.c`，一百多 KB）；
4. 确认 RAM 还够（LFN 占 `(_MAX_LFN+1)*2 = 512` 字节 BSS）；
5. 真卡上验证 `f_open("Sample_Data_2026.csv", FA_WRITE|FA_CREATE_ALWAYS)` 成功。

**路线 B（兜底，本代码包已含）**：`fs_fix_name()` 把任意名字自动转成合法 8.3 短名。即使 LFN 开不了，现场给的长文件名也不会让功能整个失败 —— 只是卡上的实际文件名会被截短。`FS_NAME_AUTO_FIX` 置 0 可关掉。

---

## 四、`Function/file_service.h` —— 现场只改这个文件

```c
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
 *  改动指南见 文档/TF卡_决赛预制代码包.md 第二节。
 * ========================================================================== */


/* ==================== 【1】命令字 ==================== */

/* 功能模块高字节。赛题已占用 0x01~0x06，文件类猜 0x07。
 * 现场说"文件命令在 0x08xx"就只改这一行，整类一起平移。 */
#define FS_CMD_CLASS            0x07
#define FS_CMD(low)             ((uint16_t)(((FS_CMD_CLASS) << 8) | (low)))

#define FS_CMD_STATUS           FS_CMD(0x01)   /* 卡状态 / 总容量 / 剩余容量 */
#define FS_CMD_LIST             FS_CMD(0x02)   /* 列目录，一次一条           */
#define FS_CMD_READ             FS_CMD(0x03)   /* 分片读（offset + len）     */
#define FS_CMD_WRITE            FS_CMD(0x04)   /* 分片写（覆盖 / 追加）       */
#define FS_CMD_DELETE           FS_CMD(0x05)   /* 删除文件                   */
#define FS_CMD_INFO             FS_CMD(0x06)   /* 文件是否存在 + 大小         */
#define FS_CMD_REC_START        FS_CMD(0x07)   /* 开始把采样写进 CSV          */
#define FS_CMD_REC_STOP         FS_CMD(0x08)   /* 停止记录                   */
#define FS_CMD_READ_LINE        FS_CMD(0x09)   /* 按行读，带续传 offset       */


/* ==================== 【2】载荷布局 ==================== */

/* 文件名字段的编码方式：
 *   0 = 长度前缀模式 [名长(1)][名(N)]        ← 默认，无歧义
 *   N = 定长 N 字节，不足补 '\0'
 * 现场说"文件名固定 12 字节"就把这里改成 12。 */
#define FS_NAME_FIXED_LEN       0

/* 偏移量字段占几个字节（大端）。文件小于 64KB 时现场可能只给 2 字节。 */
#define FS_OFFSET_BYTES         4

/* 单帧最多带多少字节文件数据。
 * 上限受 报文长度 字段（1 字节 = 255）和 P0-1 补丁后的缓冲区限制，
 * 留余量取 200。现场说"分片 128"就改这里。 */
#define FS_CHUNK_MAX            200

/* 文件名缓冲长度，跟 bsp_sdio.h 的 SD_PATH_MAX 对齐 */
#define FS_NAME_MAX             SD_PATH_MAX

/* 列目录一次最多返回几条（FS_CMD_LIST 是按序号一次取一条，这个是内部扫描上限） */
#define FS_LIST_MAX             64


/* ==================== 【3】应答风格 ==================== */

/* 成功应答带什么：
 *   0 = 只带该命令自己的数据（读文件带数据，写文件带写入长度）
 *   1 = 所有"动作类"命令统一先回 1 字节 0xFF（和赛题其他命令的 OK 一致） */
#define FS_ACK_STYLE            1

/* 错误应答用哪个命令字：
 *   0 = 用原命令字（便于上位机对号入座）
 *   1 = 统一用 CMD_SPEC_ERROR_REPLY (0xEEEE) */
#define FS_ERR_USE_EEEE         0

/* 错误应答是否带 1 字节错误码。现场若要求"错误帧内容为空"就改 0。 */
#define FS_ERR_WITH_CODE        1

/* 错误码。现场给了别的编号就改这里。 */
#define FS_OK                   0x00
#define FS_ERR_NO_CARD          0x01   /* 卡没就绪 / 没插卡        */
#define FS_ERR_NO_FILE          0x02   /* 文件不存在               */
#define FS_ERR_NAME             0x03   /* 文件名非法 / 空          */
#define FS_ERR_PARAM            0x04   /* 载荷长度或字段不合法      */
#define FS_ERR_IO               0x05   /* 读写卡出错               */
#define FS_ERR_FULL             0x06   /* 空间不足                 */
#define FS_ERR_EOF              0x07   /* 已到文件尾               */


/* ==================== 【4】数据记录（采集→存储） ==================== */

/* 记录文件名。_USE_LFN=0 时必须是 8.3 短名。 */
#define FS_REC_FILE             "DATA.CSV"

/* 记录间隔，单位秒。用 RTC 秒计时，不占用定时器（5 个定时器已全部有主）。 */
#define FS_REC_INTERVAL_S       1

/* 是否在新建文件时写一行表头 */
#define FS_CSV_WRITE_HEADER     1

/* 表头。列序改了这里和下面两个宏要一起改。 */
#define FS_CSV_HEADER           "Time,CH0,CH1,I0_mA,V0_V,V1_V,Broken\r\n"

/* 数据行格式串 + 参数表。这两个宏必须配对修改。
 * 现场说"只要电流和两路电压"就把两个宏一起删到只剩三列。 */
#define FS_CSV_ROW_FMT          "%s,%.3f,%.3f,%.3f,%.3f,%.3f,%d\r\n"
#define FS_CSV_ROW_ARGS         ts, \
                                Data_class_structure.ch0_current_val, \
                                Data_class_structure.ch1_current_val, \
                                Data_class_structure.i0_current,      \
                                Data_class_structure.v0_voltage,      \
                                Data_class_structure.v1_voltage,      \
                                (int)Data_class_structure.i0_broken

/* 单行 CSV 的最大长度 */
#define FS_CSV_LINE_MAX         128

/* 时区补偿，单位小时。和 Log_recording_function.c 的告警时间戳保持一致（+8）。 */
#define FS_TZ_HOUR              8

/* 记录期间是否允许自动上报同时进行。
 * 0 = 开始记录时自动把定时上报停掉（半双工总线上两个东西抢发会互相打断）
 * 1 = 各管各的 */
#define FS_REC_COEXIST_REPORT   0


/* ==================== 【5】文件名兜底 ==================== */

/* 1 = 自动把任意文件名转成合法 8.3 短名（大写、非法字符换 '_'、主名截 8 位、
 *     扩展名截 3 位）。LFN 没开时靠这个兜住现场给的长文件名。
 * 0 = 原样传给 FatFs，名字不合法就直接失败。
 * 如果按 P0-3 路线 A 把 LFN 打开了，这里可以改 0。 */
#define FS_NAME_AUTO_FIX        1


/* ==================== 【6】对外接口 ==================== */

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
bool     fs_record_last_ok(void);       /* 最近一次写卡是否成功，给 Modbus 用 */

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
```

---

## 五、`Function/file_service.c`

```c
#include "Headfile.h"
#include "file_service.h"

/* ============================================================================
 *  TF 卡文件服务 —— 实现
 *
 *  分两段：
 *    【A】文件服务   协议无关，只跟 bsp_sdio 打交道，Modbus 那边也能直接调
 *    【B】命令处理   只负责解包/组包，业务全部转给【A】
 *  现场若载荷格式和预设不同，只改【B】。
 * ========================================================================== */

/* ==================== 内部状态 ==================== */

static uint8_t  s_rec_on     = 0;    /* 正在记录            */
static uint8_t  s_rec_ok     = 0;    /* 最近一次写卡是否成功 */
static uint32_t s_rec_last   = 0;    /* 上次落盘的 RTC 秒   */


/* ==================== 【A】文件服务 ==================== */

/* 把任意文件名转成 FatFs（_USE_LFN=0）能接受的 8.3 短名。
 * 支持一级目录："Log_Data/sample_2026.csv" -> "LOG_DATA/SAMPLE_2.CSV"
 * 每级主名截 8 字符、扩展名截 3 字符，小写转大写，非法字符换 '_'。 */
bool fs_fix_name(const char* in, char* out, uint32_t out_size)
{
    uint32_t oi = 0;
    uint32_t seg_chars;                 /* 本段主名已写入几个字符 */
    uint8_t  in_ext;                    /* 本段是否已进入扩展名   */
    uint32_t ext_chars;

    if (in == NULL || out == NULL || out_size < 2) return false;
    if (in[0] == '\0') return false;

#if (FS_NAME_AUTO_FIX == 0)
    strncpy(out, in, out_size - 1);
    out[out_size - 1] = '\0';
    return true;
#else
    seg_chars = 0; in_ext = 0; ext_chars = 0;

    for (uint32_t i = 0; in[i] != '\0'; i++) {
        char c = in[i];

        if (c == '/' || c == '\\') {            /* 目录分隔，开新段 */
            if (oi == 0 || out[oi - 1] == '/') continue;   /* 吃掉重复斜杠 */
            if (oi + 1 >= out_size) break;
            out[oi++] = '/';
            seg_chars = 0; in_ext = 0; ext_chars = 0;
            continue;
        }

        if (c == '.') {
            if (in_ext) continue;               /* 只认第一个点 */
            if (oi + 1 >= out_size) break;
            out[oi++] = '.';
            in_ext = 1;
            continue;
        }

        /* 长度限制：主名 8，扩展名 3 */
        if (!in_ext && seg_chars >= 8) continue;
        if ( in_ext && ext_chars >= 3) continue;

        /* 大写 + 非法字符替换。FAT 短名允许的符号很少，稳妥起见只放行字母数字 */
        if      (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        else if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) c = '_';

        if (oi + 1 >= out_size) break;
        out[oi++] = c;
        if (in_ext) ext_chars++; else seg_chars++;
    }

    out[oi] = '\0';
    return (oi > 0);
#endif
}

/* 卡状态 + 容量。不关心的传 NULL */
uint8_t fs_status(uint32_t* total_kb, uint32_t* free_kb)
{
    if (!bsp_sdio_is_ready()) return FS_ERR_NO_CARD;
    /* 注意：空闲簇没缓存时会整表扫 FAT，32GB 卡几百毫秒，别放进轮询 */
    if (!bsp_sdio_get_space(total_kb, free_kb)) return FS_ERR_IO;
    return FS_OK;
}

uint8_t fs_info(const char* name, uint32_t* size)
{
    char path[FS_NAME_MAX];

    if (!bsp_sdio_is_ready())                     return FS_ERR_NO_CARD;
    if (!fs_fix_name(name, path, sizeof(path)))   return FS_ERR_NAME;
    if (!bsp_sdio_file_exists(path))              return FS_ERR_NO_FILE;

    if (size) *size = bsp_sdio_file_get_size(path);
    return FS_OK;
}

/* 分片读。want 是想读的字节数，got 回填实际读到的（到文件尾会少于 want） */
uint8_t fs_read(const char* name, uint32_t offset, uint8_t want,
                uint8_t* out, uint8_t* got)
{
    char     path[FS_NAME_MAX];
    uint32_t fsize;
    int      n;

    if (got) *got = 0;
    if (out == NULL || want == 0)                 return FS_ERR_PARAM;
    if (want > FS_CHUNK_MAX)                      return FS_ERR_PARAM;
    if (!bsp_sdio_is_ready())                     return FS_ERR_NO_CARD;
    if (!fs_fix_name(name, path, sizeof(path)))   return FS_ERR_NAME;
    if (!bsp_sdio_file_exists(path))              return FS_ERR_NO_FILE;

    fsize = bsp_sdio_file_get_size(path);
    if (offset >= fsize)                          return FS_ERR_EOF;

    n = bsp_sdio_file_read(path, out, want, offset);
    if (n < 0)                                    return FS_ERR_IO;

    if (got) *got = (uint8_t)n;
    return FS_OK;
}

/* 分片写。append=0 表示按 offset 定位覆盖写，append=1 表示追加到文件尾。
 * 上位机顺序下发分片时用 append 更快（不用每片 lseek）。 */
uint8_t fs_write(const char* name, uint32_t offset, const uint8_t* data,
                 uint8_t len, uint8_t append)
{
    char     path[FS_NAME_MAX];
    uint32_t freek = 0;
    bool     ok;

    if (data == NULL || len == 0)                 return FS_ERR_PARAM;
    if (len > FS_CHUNK_MAX)                       return FS_ERR_PARAM;
    if (!bsp_sdio_is_ready())                     return FS_ERR_NO_CARD;
    if (!fs_fix_name(name, path, sizeof(path)))   return FS_ERR_NAME;

    /* 空间检查：剩余不足 1KB 就别写了，免得写出半截文件 */
    if (bsp_sdio_get_space(NULL, &freek) && freek < 1) return FS_ERR_FULL;

    if (append) ok = bsp_sdio_file_append(path, data, len);
    else        ok = bsp_sdio_file_write_at(path, data, len, offset);

    return ok ? FS_OK : FS_ERR_IO;
}

uint8_t fs_delete(const char* name)
{
    char path[FS_NAME_MAX];

    if (!bsp_sdio_is_ready())                     return FS_ERR_NO_CARD;
    if (!fs_fix_name(name, path, sizeof(path)))   return FS_ERR_NAME;
    if (!bsp_sdio_file_exists(path))              return FS_ERR_NO_FILE;

    return bsp_sdio_file_delete(path) ? FS_OK : FS_ERR_IO;
}


/* ==================== 数据记录（采集 → 存储） ==================== */

/* 当前时间字符串，格式和告警记录保持一致。
 * 用 bsp_rtc_show_time() 是为了跟 Log_recording_function.c 走同一条路；
 * 它内部会 printf 一行时间，记录频率高时想省这几毫秒，改成直接
 * rtc_current_time_get((rtc_parameter_struct*)&bsp_rtc_init_para) 即可。 */
static void fs_time_string(char* out, uint32_t size)
{
    int year, month, day, hour, min, sec;

    bsp_rtc_show_time();

    year  = BCD2DEC(bsp_rtc_init_para.year);
    month = BCD2DEC(bsp_rtc_init_para.month);
    day   = BCD2DEC(bsp_rtc_init_para.date);
    hour  = BCD2DEC(bsp_rtc_init_para.hour) + FS_TZ_HOUR;
    min   = BCD2DEC(bsp_rtc_init_para.minute);
    sec   = BCD2DEC(bsp_rtc_init_para.second);
    if (hour >= 24) hour -= 24;

    snprintf(out, size, "%04d-%02d-%02d %02d:%02d:%02d",
             2000 + year, month, day, hour, min, sec);
}

/* 写一行采样进 CSV */
static void fs_append_sample(void)
{
    char ts[24];
    char line[FS_CSV_LINE_MAX];

    fs_time_string(ts, sizeof(ts));
    snprintf(line, sizeof(line), FS_CSV_ROW_FMT, FS_CSV_ROW_ARGS);

    s_rec_ok = bsp_sdio_file_append_text(FS_REC_FILE, line) ? 1 : 0;
}

void fs_record_start(void)
{
    if (!bsp_sdio_is_ready()) { s_rec_ok = 0; return; }

#if (FS_CSV_WRITE_HEADER != 0)
    /* 文件不存在时先写表头。已存在就直接续写，不清空 —— 现场重复下发
       开始记录不应该把之前的数据抹掉。要每次重来就在这里加 bsp_sdio_clear() */
    if (!bsp_sdio_file_exists(FS_REC_FILE)) {
        bsp_sdio_file_write_text(FS_REC_FILE, FS_CSV_HEADER);
    }
#endif

#if (FS_REC_COEXIST_REPORT == 0)
    /* 半双工总线上，定时上报和记录同时跑没有意义（记录不占总线），
       但赛题 H-02 规定自动上报期间不响应其他命令，会让"停止记录"也发不进来。
       所以默认开始记录时先把自动上报停掉。 */
    Data_class_structure.Regular_reporting_Flag = 0;
#endif

    s_rec_last = bsp_rtc_get_unix_timestamp();
    s_rec_on   = 1;

    fs_append_sample();          /* 立刻记一条，便于现场确认功能生效 */
}

void fs_record_stop(void)   { s_rec_on = 0; }
bool fs_record_active(void) { return s_rec_on ? true : false; }
bool fs_record_last_ok(void){ return s_rec_ok ? true : false; }

/* 放主循环。用 RTC 秒计时，不占定时器（TIM5/6/7/9/12 已全部有主）。 */
void fs_record_poll(void)
{
    uint32_t now;

    if (!s_rec_on) return;

    now = bsp_rtc_get_unix_timestamp();
    if (now < s_rec_last) { s_rec_last = now; return; }    /* 对过时/回绕 */
    if ((now - s_rec_last) < FS_REC_INTERVAL_S) return;

    s_rec_last = now;
    fs_append_sample();
}


/* ==================== 【B】命令处理 ==================== */

/* 统一的错误应答 */
static void fs_reply_err(uint16_t cmd, uint8_t code)
{
#if (FS_ERR_USE_EEEE != 0)
    uint16_t c = CMD_SPEC_ERROR_REPLY;
#else
    uint16_t c = cmd;
#endif

#if (FS_ERR_WITH_CODE != 0)
    Protocol_SendFrame(MY_DEVICE_ID, TYPE_ERROR, c, &code, 1);
#else
    (void)code;
    Protocol_SendFrame(MY_DEVICE_ID, TYPE_ERROR, c, NULL, 0);
#endif
}

/* 动作类命令的成功应答 */
static void fs_reply_ok(uint16_t cmd)
{
#if (FS_ACK_STYLE != 0)
    uint8_t ack = 0xFF;
    Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, cmd, &ack, 1);
#else
    Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, cmd, NULL, 0);
#endif
}

/* 大端取值 */
static uint32_t fs_get_be(const uint8_t* p, uint8_t bytes)
{
    uint32_t v = 0;
    for (uint8_t i = 0; i < bytes; i++) v = (v << 8) | p[i];
    return v;
}

static void fs_put_be(uint8_t* p, uint32_t v, uint8_t bytes)
{
    for (uint8_t i = 0; i < bytes; i++) p[i] = (uint8_t)(v >> (8 * (bytes - 1 - i)));
}

/* 从载荷里取出文件名，返回消耗了几个字节；0 表示失败。
 * FS_NAME_FIXED_LEN = 0 时是 [名长(1)][名(N)]，否则是定长字段。 */
static uint8_t fs_parse_name(const uint8_t* p, uint8_t len, char* out, uint32_t out_size)
{
#if (FS_NAME_FIXED_LEN == 0)
    uint8_t n;
    if (len < 1) return 0;
    n = p[0];
    if (n == 0 || len < (uint8_t)(1 + n) || n >= out_size) return 0;
    memcpy(out, &p[1], n);
    out[n] = '\0';
    return (uint8_t)(1 + n);
#else
    uint32_t n = FS_NAME_FIXED_LEN;
    if (len < n || n >= out_size) return 0;
    memcpy(out, p, n);
    out[n] = '\0';
    /* 定长字段用 '\0' 或空格补位，两种都去掉 */
    for (uint32_t i = 0; i < n; i++) if (out[i] == ' ') out[i] = '\0';
    return (uint8_t)n;
#endif
}


/* 0x0701 查卡状态 —— 载荷：无
 * 应答：[就绪(1)][总KB(4)][剩余KB(4)] */
void CMD_FILE_STATUS_FUNCTION(void)
{
    uint8_t  buf[9];
    uint32_t total = 0, freek = 0;
    uint8_t  rc = fs_status(&total, &freek);

    if (rc == FS_ERR_NO_CARD) {
        buf[0] = 0;
        fs_put_be(&buf[1], 0, 4);
        fs_put_be(&buf[5], 0, 4);
        Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, FS_CMD_STATUS, buf, 9);
        return;
    }
    if (rc != FS_OK) { fs_reply_err(FS_CMD_STATUS, rc); return; }

    buf[0] = 1;
    fs_put_be(&buf[1], total, 4);
    fs_put_be(&buf[5], freek, 4);
    Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, FS_CMD_STATUS, buf, 9);
}

/* 0x0702 列目录 —— 载荷：[序号(1)]，从 0 开始
 * 应答：[总条数(1)][是目录(1)][大小(4)][名(N)] */
void CMD_FILE_LIST_FUNCTION(uint8_t* p, uint8_t len)
{
    static sd_entry_t list[FS_LIST_MAX];
    uint8_t  buf[6 + 13];
    uint8_t  idx, nlen;
    int      n;

    if (len < 1)              { fs_reply_err(FS_CMD_LIST, FS_ERR_PARAM);   return; }
    if (!bsp_sdio_is_ready()) { fs_reply_err(FS_CMD_LIST, FS_ERR_NO_CARD); return; }

    idx = p[0];
    n = bsp_sdio_list_to_buf(NULL, list, FS_LIST_MAX);
    if (n < 0)                { fs_reply_err(FS_CMD_LIST, FS_ERR_IO);      return; }
    if (idx >= n || idx >= FS_LIST_MAX) {
        fs_reply_err(FS_CMD_LIST, FS_ERR_EOF); return;
    }

    buf[0] = (uint8_t)((n > 255) ? 255 : n);
    buf[1] = list[idx].is_dir ? 1 : 0;
    fs_put_be(&buf[2], list[idx].size, 4);

    nlen = (uint8_t)strlen(list[idx].name);
    memcpy(&buf[6], list[idx].name, nlen);

    Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, FS_CMD_LIST, buf, (uint8_t)(6 + nlen));
}

/* 0x0703 分片读 —— 载荷：[名长(1)][名(N)][offset(FS_OFFSET_BYTES)][len(1)]
 * 应答：[实际长度(1)][数据(N)] */
void CMD_FILE_READ_FUNCTION(uint8_t* p, uint8_t len)
{
    char     name[FS_NAME_MAX];
    uint8_t  buf[1 + FS_CHUNK_MAX];
    uint8_t  used, want, got = 0, rc;
    uint32_t off;

    used = fs_parse_name(p, len, name, sizeof(name));
    if (used == 0) { fs_reply_err(FS_CMD_READ, FS_ERR_NAME); return; }
    if (len < (uint8_t)(used + FS_OFFSET_BYTES + 1)) {
        fs_reply_err(FS_CMD_READ, FS_ERR_PARAM); return;
    }

    off  = fs_get_be(&p[used], FS_OFFSET_BYTES);
    want = p[used + FS_OFFSET_BYTES];
    if (want > FS_CHUNK_MAX) want = FS_CHUNK_MAX;

    rc = fs_read(name, off, want, &buf[1], &got);
    if (rc != FS_OK) { fs_reply_err(FS_CMD_READ, rc); return; }

    buf[0] = got;
    Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, FS_CMD_READ, buf, (uint8_t)(1 + got));
}

/* 0x0704 分片写 —— 载荷：[名长(1)][名(N)][模式(1)][offset(FS_OFFSET_BYTES)][数据(N)]
 * 模式 0 = 按 offset 覆盖写，1 = 追加（offset 字段仍要占位，内容忽略）
 * 应答：[写入长度(1)] 或 FF */
void CMD_FILE_WRITE_FUNCTION(uint8_t* p, uint8_t len)
{
    char     name[FS_NAME_MAX];
    uint8_t  used, mode, dlen, rc;
    uint32_t off;

    used = fs_parse_name(p, len, name, sizeof(name));
    if (used == 0) { fs_reply_err(FS_CMD_WRITE, FS_ERR_NAME); return; }
    if (len < (uint8_t)(used + 1 + FS_OFFSET_BYTES)) {
        fs_reply_err(FS_CMD_WRITE, FS_ERR_PARAM); return;
    }

    mode = p[used];
    off  = fs_get_be(&p[used + 1], FS_OFFSET_BYTES);
    dlen = (uint8_t)(len - used - 1 - FS_OFFSET_BYTES);
    if (dlen == 0) { fs_reply_err(FS_CMD_WRITE, FS_ERR_PARAM); return; }

    rc = fs_write(name, off, &p[used + 1 + FS_OFFSET_BYTES], dlen, mode ? 1 : 0);
    if (rc != FS_OK) { fs_reply_err(FS_CMD_WRITE, rc); return; }

#if (FS_ACK_STYLE != 0)
    fs_reply_ok(FS_CMD_WRITE);
#else
    Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, FS_CMD_WRITE, &dlen, 1);
#endif
}

/* 0x0705 删除 —— 载荷：[名长(1)][名(N)] */
void CMD_FILE_DELETE_FUNCTION(uint8_t* p, uint8_t len)
{
    char    name[FS_NAME_MAX];
    uint8_t used, rc;

    used = fs_parse_name(p, len, name, sizeof(name));
    if (used == 0) { fs_reply_err(FS_CMD_DELETE, FS_ERR_NAME); return; }

    rc = fs_delete(name);
    if (rc != FS_OK) { fs_reply_err(FS_CMD_DELETE, rc); return; }
    fs_reply_ok(FS_CMD_DELETE);
}

/* 0x0706 文件信息 —— 载荷：[名长(1)][名(N)]
 * 应答：[存在(1)][大小(4)] */
void CMD_FILE_INFO_FUNCTION(uint8_t* p, uint8_t len)
{
    char     name[FS_NAME_MAX];
    uint8_t  buf[5];
    uint8_t  used, rc;
    uint32_t size = 0;

    used = fs_parse_name(p, len, name, sizeof(name));
    if (used == 0) { fs_reply_err(FS_CMD_INFO, FS_ERR_NAME); return; }

    rc = fs_info(name, &size);
    if (rc == FS_ERR_NO_FILE) {
        buf[0] = 0;
        fs_put_be(&buf[1], 0, 4);
        Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, FS_CMD_INFO, buf, 5);
        return;
    }
    if (rc != FS_OK) { fs_reply_err(FS_CMD_INFO, rc); return; }

    buf[0] = 1;
    fs_put_be(&buf[1], size, 4);
    Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, FS_CMD_INFO, buf, 5);
}

/* 0x0707 / 0x0708 开始 / 停止记录 —— 载荷：无 */
void CMD_FILE_REC_START_FUNCTION(void)
{
    if (!bsp_sdio_is_ready()) { fs_reply_err(FS_CMD_REC_START, FS_ERR_NO_CARD); return; }
    fs_record_start();
    fs_reply_ok(FS_CMD_REC_START);
}

void CMD_FILE_REC_STOP_FUNCTION(void)
{
    fs_record_stop();
    fs_reply_ok(FS_CMD_REC_STOP);
}

/* 0x0709 按行读 —— 载荷：[名长(1)][名(N)][offset(FS_OFFSET_BYTES)]
 * 应答：[下一行offset(FS_OFFSET_BYTES)][行长(1)][行内容(N)]
 * 把整个 CSV 一行行发给上位机时用这个，比按字节分片省事（不会把一行切两半）。 */
void CMD_FILE_READ_LINE_FUNCTION(uint8_t* p, uint8_t len)
{
    char     name[FS_NAME_MAX];
    char     path[FS_NAME_MAX];
    char     line[FS_CSV_LINE_MAX];
    uint8_t  buf[FS_OFFSET_BYTES + 1 + FS_CSV_LINE_MAX];
    uint8_t  used, llen;
    uint32_t off;
    int      n;

    used = fs_parse_name(p, len, name, sizeof(name));
    if (used == 0) { fs_reply_err(FS_CMD_READ_LINE, FS_ERR_NAME); return; }
    if (len < (uint8_t)(used + FS_OFFSET_BYTES)) {
        fs_reply_err(FS_CMD_READ_LINE, FS_ERR_PARAM); return;
    }
    if (!bsp_sdio_is_ready()) { fs_reply_err(FS_CMD_READ_LINE, FS_ERR_NO_CARD); return; }
    if (!fs_fix_name(name, path, sizeof(path))) {
        fs_reply_err(FS_CMD_READ_LINE, FS_ERR_NAME); return;
    }

    off = fs_get_be(&p[used], FS_OFFSET_BYTES);

    n = bsp_sdio_file_read_line_at(path, &off, line, sizeof(line));
    if (n == SD_LINE_EOF) { fs_reply_err(FS_CMD_READ_LINE, FS_ERR_EOF); return; }
    if (n <  0)           { fs_reply_err(FS_CMD_READ_LINE, FS_ERR_IO);  return; }

    llen = (uint8_t)((n > FS_CHUNK_MAX) ? FS_CHUNK_MAX : n);

    fs_put_be(buf, off, FS_OFFSET_BYTES);       /* off 已被改成下一行起点 */
    buf[FS_OFFSET_BYTES] = llen;
    memcpy(&buf[FS_OFFSET_BYTES + 1], line, llen);

    Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, FS_CMD_READ_LINE,
                       buf, (uint8_t)(FS_OFFSET_BYTES + 1 + llen));
}
```

---

## 六、接入现有工程（4 处小改动）

### 1) `Headfile/Headfile.h` — 加一行

```c
#include "modbus_data_map.h"
#include "file_service.h"          /* ← 新增 */
```

### 2) `Protocol/Custom_Protocol/General_Protocol.c` — switch 里加 9 个 case

放在 `case CMD_LOG_CLEAR:` 之后、`case CMD_SPEC_DISCOVER:` 之前：

```c
/* ===== TF 卡文件类（决赛新增）=====
 * 命令字和载荷格式全在 Function/file_service.h，现场只改那个文件。
 * 这里只做分发，处理函数在 Function/file_service.c —— 和赛题 2.1 的
 * 分层要求一致：Protocol 层只管组帧/分发，业务逻辑在 Function 层。 */
case FS_CMD_STATUS:     CMD_FILE_STATUS_FUNCTION();                  break;
case FS_CMD_LIST:       CMD_FILE_LIST_FUNCTION(payload, payload_len);break;
case FS_CMD_READ:       CMD_FILE_READ_FUNCTION(payload, payload_len);break;
case FS_CMD_WRITE:      CMD_FILE_WRITE_FUNCTION(payload, payload_len);break;
case FS_CMD_DELETE:     CMD_FILE_DELETE_FUNCTION(payload, payload_len);break;
case FS_CMD_INFO:       CMD_FILE_INFO_FUNCTION(payload, payload_len);break;
case FS_CMD_REC_START:  CMD_FILE_REC_START_FUNCTION();               break;
case FS_CMD_REC_STOP:   CMD_FILE_REC_STOP_FUNCTION();                break;
case FS_CMD_READ_LINE:  CMD_FILE_READ_LINE_FUNCTION(payload, payload_len);break;
```

> `payload` 和 `payload_len` 是 `Protocol_HandleFrame()` 开头（第 90/92 行）已有的局部变量，直接用。

### 3) `User/main.c` — 主循环加一行

在 `report_police_function(alarm_mode);` 附近加：

```c
fs_record_poll();       /* TF 卡数据记录，内部自己判间隔，没开记录时立即返回 */
```

### 4) Keil / EIDE 工程加文件

- Keil：`Project/test.uvprojx` 的 Function 分组里加 `file_service.c`
- EIDE：`Project/.eide/eide.yml` 同步

---

## 七、测试报文（CRC 已算，可直接粘串口工具）

设备 ID `0001`，协议版本 `02`。**锚点自检**：`A5B60001010101000215ABB6A5` 是赛题文档给的重启帧，和这里用同一个算法算出来一致，说明下表可信。

| 编号 | 测试项 | 下发报文 |
|---|---|---|
| T-01 | 查卡状态 / 容量 | `A5B6000101070100029DABB6A5` |
| T-02 | 列根目录第 0 条 | `A5B600010107020102003B8DB6A5` |
| T-03 | 列根目录第 1 条 | `A5B60001010702010201FB4CB6A5` |
| T-04 | 查 `DATA.CSV` 信息 | `A5B60001010706090208444154412E4353567BDEB6A5` |
| T-05 | 读 `DATA.CSV` off=0 len=64 | `A5B600010107030E0208444154412E435356000000004064E0B6A5` |
| T-06 | 读 `DATA.CSV` off=64 len=64 | `A5B600010107030E0208444154412E4353560000004040A4D1B6A5` |
| T-07 | 覆盖写 `TEST.TXT` = "Hello" | `A5B60001010704130208544553542E545854000000000048656C6C6F3231B6A5` |
| T-08 | 追加写 `TEST.TXT` = "World" | `A5B60001010704130208544553542E5458540100000000576F726C642CD7B6A5` |
| T-09 | 删除 `TEST.TXT` | `A5B60001010705090208544553542E54585432A4B6A5` |
| T-10 | 开始记录 | `A5B6000101070700029C4BB6A5` |
| T-11 | 停止记录 | `A5B6000101070800029F7BB6A5` |
| T-12 | 按行读 `DATA.CSV` off=0 | `A5B600010107090D0208444154412E43535600000000C60BB6A5` |
| T-13 | 读不存在的文件（应回错误码 02） | `A5B600010107060902084E4F50452E5458547577B6A5` |
| T-14 | 空文件名（应回错误码 03） | `A5B600010107060102000B8CB6A5` |
| T-15 | 读 `TEST.TXT` off=0 len=64（验 T-07/08） | `A5B600010107030E0208544553542E545854000000004002DFB6A5` |

> 全部按 `FS_NAME_FIXED_LEN=0`（长度前缀）+ `FS_OFFSET_BYTES=4` 生成。改了这两个宏，用下面的脚本重新算。

**建议测试顺序**：T-01（确认卡在）→ T-10（开始记录）→ 等 10 秒 → T-11（停止）→ T-04（看文件大小 > 0）→ T-12 反复调用直到 EOF（把 CSV 读回来）→ T-13/T-14（异常）→ 拔卡插电脑核对 CSV。

**改报文的脚本**（存成 `crc_tool_file.py`）：

```python
# -*- coding: utf-8 -*-
import struct

def crc16_modbus(b):
    crc = 0xFFFF
    for x in b:
        crc ^= x
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc

def frame(dev_id, ftype, cmd, payload=b''):
    body = struct.pack('>HHBHBB', 0xA5B6, dev_id, ftype, cmd, len(payload), 0x02) + payload
    return (body + struct.pack('>HH', crc16_modbus(body), 0xB6A5)).hex().upper()

def name(s):
    """长度前缀的文件名字段 [名长(1)][名(N)]；FS_NAME_FIXED_LEN 改了这里也要改"""
    b = s.encode('ascii')
    return bytes([len(b)]) + b

OFF = 4          # 跟 FS_OFFSET_BYTES 保持一致
def off(v):
    return v.to_bytes(OFF, 'big')

D = 0x0001
# 读：[名][offset][len]
print('读 DATA.CSV off=0 len=200 :', frame(D,0x01,0x0703, name("DATA.CSV")+off(0)+bytes([200])))
# 写：[名][模式][offset][数据]   模式 0=覆盖 1=追加
print('覆盖写 TEST.TXT=Hello    :', frame(D,0x01,0x0704, name("TEST.TXT")+bytes([0])+off(0)+b'Hello'))
print('追加写 TEST.TXT=World    :', frame(D,0x01,0x0704, name("TEST.TXT")+bytes([1])+off(0)+b'World'))
# 按行读：[名][offset]
print('按行读 DATA.CSV off=0    :', frame(D,0x01,0x0709, name("DATA.CSV")+off(0)))
```

---

## 八、现场应变剧本

拿到题目后对号入座。**左边是题目可能怎么写，右边是动哪里。**

| 现场要求 | 怎么应对 |
|---|---|
| **命令字整类不同**（比如 0x08xx） | 改 `FS_CMD_CLASS` 一行 |
| **只给一两条命令**（比如只要"读文件"） | 别删代码，把用不上的 case 从 `General_Protocol.c` 注释掉即可，留着不碍事 |
| **载荷里文件名定长** | `FS_NAME_FIXED_LEN` 改成长度 |
| **偏移用 2 字节 / 3 字节** | `FS_OFFSET_BYTES` |
| **一次要读整个文件，不分片** | 文件必然小于 200 字节才可能，直接用 `FS_CMD_READ` off=0 len=200；更大的只能分片，跟评委说明 |
| **要求返回文件行数** | `fs_info()` 里加一个循环调 `bsp_sdio_file_read_line_at()` 数行，接口已现成 |
| **数据格式不是 CSV，是定长二进制记录** | 改 `fs_append_sample()`：把 `snprintf` 换成填结构体 + `bsp_sdio_file_append()`（P0-2 补的那个二进制版），其余不动 |
| **要求按时间/序号分文件** | `FS_REC_FILE` 改成运行时拼名字：`snprintf(fname,...,"D%02d%02d.CSV",month,day)`，注意 8.3 限制 |
| **要求掉电后接着写**（断点续存） | 已经是这个行为 —— `fs_record_start()` 不清空已有文件，`bsp_sdio_file_append_text` 每次都定位到文件尾 |
| **要求记录条数上限 / 环形覆盖** | 在 `fs_append_sample()` 开头加 `if (bsp_sdio_file_get_size(FS_REC_FILE) > 上限) bsp_sdio_clear(FS_REC_FILE);` |
| **不插卡时的行为** | 已处理：所有命令先查 `bsp_sdio_is_ready()`，回 `FS_ERR_NO_CARD`(01)。`main.c` 里 SD 初始化失败本来就是非致命的 |
| **要求 Modbus 也能读写文件** | `fs_*()` 那一层是协议无关的，在 `modbus_data_map.c` 的 `Modbus_ExecPendingActions()` 里直接调；点表里已有 `MB_COIL_LOG_TO_SD` 可以复用 |
| **要求 TF 卡存告警记录** | 在 `Log_recording_function.c` 的 `bsp_log_record_alarm()` 末尾加一句 `bsp_sdio_file_append_text("ALARM.CSV", line);` |

---

## 九、赛前自检清单

代码写完，用真卡逐条过一遍：

- [ ] P0-1 缓冲区补丁打完，发 payload=255 的帧不复位
- [ ] P0-2 两个新驱动函数编译通过
- [ ] P0-3 LFN 决策已定（开了 / 或确认走 `fs_fix_name` 兜底）
- [ ] `file_service.c` 已加进 Keil **和** EIDE 两个工程
- [ ] T-01 能查到卡容量，数值和电脑上看的一致
- [ ] T-10 → 等 10 秒 → T-11，`DATA.CSV` 里有约 10 行，时间戳每秒递增
- [ ] 拔卡插电脑，`DATA.CSV` 能用 Excel 直接打开，列对齐、时间正确（不是 1980 年）
- [ ] T-12 反复调用能把整个 CSV 读回来，最后回 EOF(07)
- [ ] T-07 → T-08 → T-15，读回来的内容是 `HelloWorld`
- [ ] 不插卡时下发 T-01/T-05/T-10，都回错误码 01，设备不死
- [ ] 记录期间下发其他命令，设备正常响应（记录不占总线）
- [ ] **实测一次 `fs_append_sample()` 耗时**，超过 100ms 就改成文件常开 + 定期 `f_sync`
- [ ] 三张不同品牌/容量的卡各测一遍
- [ ] 三张卡都提前格式化成 FAT32

---

## 十、已知取舍

写下来免得现场当成 bug 去查：

1. **记录用 RTC 秒计时，最小间隔 1 秒**。五个定时器（TIM5/6/7/9/12）全部有主，不新增定时器是刻意的。要更快的记录频率，得挂到 TIM7 的 `g_report_flag` 上。
2. **`fs_append_sample()` 每次都 open/write/close**。安全（掉电最多丢一行）但慢。现场如果要求高频记录，改成文件常开。
3. **`bsp_sdio_get_space()` 在空闲簇没缓存时会整表扫 FAT**（32GB 卡几百毫秒）。所以 `fs_write()` 里的空间检查每次都调它是有代价的 —— 现场如果写入很频繁，把这个检查改成每 N 次查一次。
4. **`fs_time_string()` 调 `bsp_rtc_show_time()`，它内部会 printf 一行**。跟告警记录走的是同一条路，保持一致；嫌慢就换成直接调 `rtc_current_time_get()`。
5. **`FS_CMD_LIST` 只列根目录**，不下潜子目录。`bsp_sdio_list_to_buf()` 支持传目录名，要列子目录把载荷加一个目录名字段即可。
6. **`fs_fix_name()` 只放行字母数字**，其余符号一律换 `_`。FAT 短名其实允许 `$%'-_@~` 等，但现场不值得为这个冒险。
