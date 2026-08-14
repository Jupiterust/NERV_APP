#include "Headfile.h"

/* ============================================================================
 *  TF 卡文件服务 —— 实现
 *
 *  分两段：
 *    【A】文件服务   协议无关，只跟 bsp_sdio 打交道，Modbus 那边也能直接调
 *    【B】命令处理   只负责解包 / 组包，业务全部转给【A】
 *
 *  现场若载荷格式和预设完全不同，只改【B】那一段的解包代码，【A】不用动。
 *  两段之间是干净的函数调用边界，这是刻意留的。
 *
 *  所有可调项都在 file_service.h，本文件里不留任何字面量。
 * ========================================================================== */

/* ==================== 宏的编译期自检 ==================== */

/* file_service.h 里的宏改出范围，这里直接编译报错（数组长度为负），
 * 而不是到现场才发现。和 modbus_data_map.c 的 MB_CHK 是同一个套路。 */
#define FS_CHK(tag, cond)   typedef char fs_chk_##tag[(cond) ? 1 : -1]

FS_CHK(offset_bytes,   FS_OFFSET_BYTES >= 1 && FS_OFFSET_BYTES <= 4);
FS_CHK(chunk_max,      FS_CHUNK_MAX >= 1 && FS_CHUNK_MAX <= 254);   /* 应答要留 1 字节长度头 */
FS_CHK(name_fixed_len, FS_NAME_FIXED_LEN < FS_NAME_MAX);
FS_CHK(csv_line_max,   FS_CSV_LINE_MAX >= 32);
FS_CHK(read_line_hdr,  FS_OFFSET_BYTES + 2 <= 255);                 /* 按行读的帧头开销 */


/* ==================== 内部状态 ==================== */

static uint8_t  s_rec_on     = 0;    /* 正在记录             */
static uint8_t  s_rec_ok     = 0;    /* 最近一次写卡是否成功  */
static uint32_t s_rec_last   = 0;    /* 上次落盘的 RTC 秒     */


/* ==================== 【A】文件服务 ==================== */

/* 文件名规范化。
 * FS_NAME_AUTO_FIX = 0（默认，_USE_LFN=1 时用这个）：原样透传，只做长度保护。
 * FS_NAME_AUTO_FIX = 1（_USE_LFN 被改回 0 时的兜底）：转成合法 8.3 短名，
 *   支持多级目录 —— "Log_Data/sample_2026.csv" -> "LOG_DATA/SAMPLE_2.CSV"，
 *   每级主名截 8 字符、扩展名截 3 字符，小写转大写，非法字符换 '_'。 */
bool fs_fix_name(const char* in, char* out, uint32_t out_size)
{
    if (in == NULL || out == NULL || out_size < 2) return false;
    if (in[0] == '\0') return false;

#if (FS_NAME_AUTO_FIX == 0)
    if (strlen(in) >= out_size) return false;    /* 装不下就当非法名，别悄悄截断 */
    strcpy(out, in);
    return true;
#else
    {
        uint32_t oi        = 0;
        uint32_t seg_chars = 0;    /* 本段主名已写入几个字符 */
        uint8_t  in_ext    = 0;    /* 本段是否已进入扩展名   */
        uint32_t ext_chars = 0;
        uint32_t i;

        for (i = 0; in[i] != '\0'; i++) {
            char c = in[i];

            if (c == '/' || c == '\\') {                    /* 目录分隔，开新段 */
                if (oi == 0 || out[oi - 1] == '/') continue; /* 吃掉重复斜杠 */
                if (oi + 1 >= out_size) break;
                out[oi++] = '/';
                seg_chars = 0; in_ext = 0; ext_chars = 0;
                continue;
            }

            if (c == '.') {
                if (in_ext) continue;                       /* 只认第一个点 */
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
    }
#endif
}

/* 卡状态 + 容量，单位 KB。不关心的传 NULL。
 * 注意：空闲簇数没缓存时会整表扫 FAT（32GB 卡几百毫秒），别放进轮询。 */
uint8_t fs_status(uint32_t* total_kb, uint32_t* free_kb)
{
    if (!bsp_sdio_is_ready())                     return FS_ERR_NO_CARD;
    if (!bsp_sdio_get_space(total_kb, free_kb))   return FS_ERR_IO;
    return FS_OK;
}

/* 文件是否存在 + 大小 */
uint8_t fs_info(const char* name, uint32_t* size)
{
    char path[FS_NAME_MAX];

    if (!bsp_sdio_is_ready())                     return FS_ERR_NO_CARD;
    if (!fs_fix_name(name, path, sizeof(path)))   return FS_ERR_NAME;
    if (!bsp_sdio_file_exists(path))              return FS_ERR_NO_FILE;

    if (size) *size = bsp_sdio_file_get_size(path);
    return FS_OK;
}

/* 分片读。want 是想读的字节数，got 回填实际读到的（到文件尾会少于 want）。 */
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

/* 分片写。append = 0 按 offset 定位覆盖写，append = 1 追加到文件尾。
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

    /* 空间检查：剩余不足 1KB 就别写了，免得写出半截文件。
       第一次调用会扫一遍 FAT，之后 FatFs 有缓存，不慢。 */
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


/* ==================== 数据记录（采集 -> 存储） ==================== */

/* 当前时间字符串，格式和告警记录保持一致（Log_recording_function.c 同一条路）。
 * bsp_rtc_show_time() 内部会 printf 一行时间，记录频率高时想省这几毫秒，
 * 换成直接 rtc_current_time_get((rtc_parameter_struct*)&bsp_rtc_init_para) 即可。 */
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

/* 需要的话写表头。文件不存在才写，已存在直接续写，不清空 ——
 * 现场重复下发"开始记录"不应该把之前的数据抹掉。 */
static void fs_write_header_if_new(void)
{
#if (FS_CSV_WRITE_HEADER != 0)
    if (!bsp_sdio_file_exists(FS_REC_FILE)) {
        bsp_sdio_file_write_text(FS_REC_FILE, FS_CSV_HEADER);
    }
#endif
}

/* 写一行采样进 CSV */
static void fs_append_sample(void)
{
    char ts[24];
    char line[FS_CSV_LINE_MAX];

#if (FS_REC_MAX_BYTES > 0)
    /* 环形覆盖：超过上限就清空重记（表头重写）。要"保留旧文件"改成
       bsp_sdio_rename(FS_REC_FILE, "DATA.BAK") 再写新的。 */
    if (bsp_sdio_file_get_size(FS_REC_FILE) >= FS_REC_MAX_BYTES) {
        bsp_sdio_clear(FS_REC_FILE);
        fs_write_header_if_new();
    }
#endif

    fs_time_string(ts, sizeof(ts));
    snprintf(line, sizeof(line), FS_CSV_ROW_FMT, FS_CSV_ROW_ARGS);

    s_rec_ok = bsp_sdio_file_append_text(FS_REC_FILE, line) ? 1 : 0;
}

void fs_record_start(void)
{
    if (!bsp_sdio_is_ready()) { s_rec_ok = 0; return; }

    fs_write_header_if_new();

#if (FS_REC_COEXIST_REPORT == 0)
    /* 记录本身不占总线，但赛题 H-02 规定自动上报期间不响应其它命令，
       上报开着的话连"停止记录"都发不进来。所以开始记录时先把上报停掉。 */
    Data_class_structure.Regular_reporting_Flag = 0;
#endif

    s_rec_last = bsp_rtc_get_unix_timestamp();
    s_rec_on   = 1;

    fs_append_sample();          /* 立刻记一条，便于现场确认功能生效 */
}

void fs_record_stop(void)    { s_rec_on = 0; }
bool fs_record_active(void)  { return s_rec_on ? true : false; }
bool fs_record_last_ok(void) { return s_rec_ok ? true : false; }

/* 放主循环。用 RTC 秒计时，不占定时器（TIM5/6/7/9/12 已全部有主）。 */
void fs_record_poll(void)
{
    uint32_t now;

    if (!s_rec_on) return;

    now = bsp_rtc_get_unix_timestamp();
    if (now < s_rec_last) { s_rec_last = now; return; }    /* 对过时 / 回绕 */
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
    (void)cmd;
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

/* 大端取值 / 填值 */
static uint32_t fs_get_be(const uint8_t* p, uint8_t bytes)
{
    uint32_t v = 0;
    uint8_t  i;
    for (i = 0; i < bytes; i++) v = (v << 8) | p[i];
    return v;
}

static void fs_put_be(uint8_t* p, uint32_t v, uint8_t bytes)
{
    uint8_t i;
    for (i = 0; i < bytes; i++) p[i] = (uint8_t)(v >> (8 * (bytes - 1 - i)));
}

/* 从载荷里取出文件名，返回文件名字段一共占了几个字节；0 表示失败。
 * FS_NAME_FIXED_LEN = 0 时是 [名长(1)][名(N)]，否则是定长字段。 */
static uint8_t fs_parse_name(const uint8_t* p, uint8_t len, char* out, uint32_t out_size)
{
    if (p == NULL || len == 0) return 0;

#if (FS_NAME_FIXED_LEN == 0)
    {
        uint8_t n = p[0];
        if (n == 0 || len < (uint16_t)(1 + n) || n >= out_size) return 0;
        memcpy(out, &p[1], n);
        out[n] = '\0';
        return (uint8_t)(1 + n);
    }
#else
    {
        uint32_t n = FS_NAME_FIXED_LEN;
        uint32_t i;
        if (len < n || n >= out_size) return 0;
        memcpy(out, p, n);
        out[n] = '\0';
        /* 定长字段用 '\0' 或空格补位，两种都截掉 */
        for (i = 0; i < n; i++) if (out[i] == ' ') out[i] = '\0';
        if (out[0] == '\0') return 0;
        return (uint8_t)n;
    }
#endif
}


/* 0x0701 查卡状态 —— 载荷：无
 * 应答：[就绪(1)][总KB(4)][剩余KB(4)]
 * 没插卡时不回错误帧，回一帧 就绪=0 的正常应答，上位机好判断。 */
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
 * 应答：[总条数(1)][是目录(1)][大小(4)][名(N)]
 * 一次回一条，上位机按序号轮询到 EOF 为止。只列根目录，不下潜子目录。 */
void CMD_FILE_LIST_FUNCTION(uint8_t* p, uint8_t len)
{
    static sd_entry_t list[FS_LIST_MAX];   /* 1.5KB，放静态区，别占栈 */
    uint8_t  buf[6 + sizeof(list[0].name)];
    uint8_t  idx, nlen;
    int      n;

    if (len < 1)              { fs_reply_err(FS_CMD_LIST, FS_ERR_PARAM);   return; }
    if (!bsp_sdio_is_ready()) { fs_reply_err(FS_CMD_LIST, FS_ERR_NO_CARD); return; }

    idx = p[0];
    n = bsp_sdio_list_to_buf(NULL, list, FS_LIST_MAX);
    if (n < 0)                { fs_reply_err(FS_CMD_LIST, FS_ERR_IO);      return; }
    /* 返回值可能大于 FS_LIST_MAX（被截断），只有前 FS_LIST_MAX 条真的填进了数组 */
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
 * 应答：[实际长度(1)][数据(N)]，读到文件尾回错误码 07 */
void CMD_FILE_READ_FUNCTION(uint8_t* p, uint8_t len)
{
    char     name[FS_NAME_MAX];
    uint8_t  buf[1 + FS_CHUNK_MAX];
    uint8_t  used, want, got = 0, rc;
    uint32_t off;

    used = fs_parse_name(p, len, name, sizeof(name));
    if (used == 0) { fs_reply_err(FS_CMD_READ, FS_ERR_NAME); return; }
    if (len < (uint16_t)(used + FS_OFFSET_BYTES + 1)) {
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
 * 应答：FF（FS_ACK_STYLE=1）或 [写入长度(1)]（FS_ACK_STYLE=0） */
void CMD_FILE_WRITE_FUNCTION(uint8_t* p, uint8_t len)
{
    char     name[FS_NAME_MAX];
    uint8_t  used, mode, dlen, rc;
    uint32_t off;

    used = fs_parse_name(p, len, name, sizeof(name));
    if (used == 0) { fs_reply_err(FS_CMD_WRITE, FS_ERR_NAME); return; }
    if (len <= (uint16_t)(used + 1 + FS_OFFSET_BYTES)) {   /* 后面还得有数据 */
        fs_reply_err(FS_CMD_WRITE, FS_ERR_PARAM); return;
    }

    mode = p[used];
    off  = fs_get_be(&p[used + 1], FS_OFFSET_BYTES);
    dlen = (uint8_t)(len - used - 1 - FS_OFFSET_BYTES);

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
 * 应答：[存在(1)][大小(4)]。文件不存在不算错误，回 存在=0。 */
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
 * 把整个 CSV 一行行发给上位机时用这个，比按字节分片省事（不会把一行切两半）：
 * offset 传 0 开始，每次拿应答里的"下一行 offset"再发一帧，回错误码 07 就是读完了。 */
void CMD_FILE_READ_LINE_FUNCTION(uint8_t* p, uint8_t len)
{
    char     name[FS_NAME_MAX];
    char     path[FS_NAME_MAX];
    char     line[FS_CSV_LINE_MAX];
    uint8_t  buf[FS_OFFSET_BYTES + 1 + FS_CSV_LINE_MAX];
    uint8_t  used, llen;
    uint32_t off;
    uint32_t max_line;
    int      n;

    used = fs_parse_name(p, len, name, sizeof(name));
    if (used == 0) { fs_reply_err(FS_CMD_READ_LINE, FS_ERR_NAME); return; }
    if (len < (uint16_t)(used + FS_OFFSET_BYTES)) {
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

    /* 行长同时受两头限制：分片上限，以及"报文长度"字段减去本帧的头部 */
    max_line = FS_CHUNK_MAX;
    if (max_line > (uint32_t)(255 - FS_OFFSET_BYTES - 1)) {
        max_line = (uint32_t)(255 - FS_OFFSET_BYTES - 1);
    }
    llen = (uint8_t)(((uint32_t)n > max_line) ? max_line : (uint32_t)n);

    fs_put_be(buf, off, FS_OFFSET_BYTES);       /* off 已被改成下一行起点 */
    buf[FS_OFFSET_BYTES] = llen;
    memcpy(&buf[FS_OFFSET_BYTES + 1], line, llen);

    Protocol_SendFrame(MY_DEVICE_ID, TYPE_ACK, FS_CMD_READ_LINE,
                       buf, (uint8_t)(FS_OFFSET_BYTES + 1 + llen));
}
