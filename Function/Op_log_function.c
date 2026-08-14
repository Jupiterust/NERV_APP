#include "Headfile.h"

/* 操作日志：6 个环形缓冲区（3 种协议 × 收发两个方向）。
 * 设计说明、可调项、记录点清单全部在 Op_log_function.h，这里只放实现。 */


/* ========================================================================
 *  存储
 * ======================================================================== */

/* 6 个环。静态变量自动清零，seq 为 0 的槽就是"还没用过" */
static oplog_entry_t s_ring[OPLOG_PROTO_MAX][OPLOG_DIR_MAX][OPLOG_DEPTH];

/* 每个环下一条写哪个槽（0 ~ OPLOG_DEPTH-1） */
static uint16_t s_head[OPLOG_PROTO_MAX][OPLOG_DIR_MAX];

/* 每个环累计记了多少条，含被覆盖掉的。count = min(total, OPLOG_DEPTH) */
static uint32_t s_total[OPLOG_PROTO_MAX][OPLOG_DIR_MAX];

/* 全局序号，6 个环共用。从 1 开始发，0 留给"空槽"。
 * 42 亿条之后会回绕，那时合并排序会乱一次 —— 按一秒 10 帧算要跑 13 年，不处理。 */
static uint32_t s_seq;


static uint8_t proto_dir_valid(oplog_proto_t proto, oplog_dir_t dir)
{
    return (uint8_t)((proto < OPLOG_PROTO_MAX) && (dir < OPLOG_DIR_MAX));
}


/* ========================================================================
 *  落盘到 TF 卡：一条独立于上面 6 个 RAM 环的队列
 *
 *  oplog_add() 只往这个队列里做一次 memcpy（oplog_sd_enqueue），真正的文件
 *  I/O 全部挪到主循环的 oplog_sd_poll() 里做，避免拖慢 Protocol_SendFrame()
 *  之前的应答路径。详见 Op_log_function.h【1】节和【7】节的说明。
 * ======================================================================== */
#if (OPLOG_SD_ENABLE != 0)

static oplog_entry_t s_sd_queue[OPLOG_SD_QUEUE_DEPTH];
static uint16_t       s_sd_head;            /* 下一条入队写在这个下标 */
static uint16_t       s_sd_tail;            /* 下一条出队从这个下标读 */
static uint16_t       s_sd_count;           /* 队列里当前有几条 */
static uint32_t       s_sd_dropped;         /* 队列满导致被丢弃的条数 */
static uint32_t       s_sd_last_flush_ts;   /* 上一次成功落盘的时间戳（UTC秒） */
static uint8_t         s_sd_header_written; /* 表头是否已经写过 */

/* 纯 memcpy 入队，不做任何 I/O。队列满了丢最旧的一条并计数——
 * 和上面 6 个 RAM 环"存满覆盖最旧"是同一个思路，只是这里多记一下丢了几条。 */
static void oplog_sd_enqueue(const oplog_entry_t *e)
{
    if (s_sd_count >= OPLOG_SD_QUEUE_DEPTH) {
        s_sd_tail = (uint16_t)((s_sd_tail + 1) % OPLOG_SD_QUEUE_DEPTH);
        s_sd_count--;
        s_sd_dropped++;
    }
    s_sd_queue[s_sd_head] = *e;
    s_sd_head = (uint16_t)((s_sd_head + 1) % OPLOG_SD_QUEUE_DEPTH);
    s_sd_count++;
}


uint32_t oplog_sd_get_dropped(void)
{
    return s_sd_dropped;
}


void oplog_sd_poll(void)
{
    /* 拼行缓冲区，静态的不占栈——最坏情况一次性把整条队列都拼进去，
     * 按 OPLOG_SD_QUEUE_DEPTH × OPLOG_LINE_MAX 留够空间。 */
    static char s_flush_buf[OPLOG_SD_QUEUE_DEPTH * OPLOG_LINE_MAX];
    uint32_t    now;
    uint32_t    buf_len = 0;
    uint16_t    n, i, pos;
    int         line_len;
    bool        wr_ok;

    if (s_sd_count == 0) {
        return;                                   /* 没有待落盘的记录 */
    }
    if (!bsp_sdio_is_ready()) {
        return;                                   /* 没卡，静默跳过，队列继续攒着 */
    }

    now = bsp_rtc_get_unix_timestamp();
    if (s_sd_count < OPLOG_SD_FLUSH_COUNT &&
        (uint32_t)(now - s_sd_last_flush_ts) < (uint32_t)OPLOG_SD_FLUSH_INTERVAL_S) {
        return;                                   /* 还没到触发条件 */
    }

    /* TODO(debug): 排查"没生成文件"问题用的临时打印，确认现象后可以删掉 */
    printf("[OPLOG-SD] triggered: queued=%u now=%lu last=%lu header_written=%u\r\n",
           (unsigned)s_sd_count, (unsigned long)now, (unsigned long)s_sd_last_flush_ts,
           (unsigned)s_sd_header_written);

#if (OPLOG_SD_WRITE_HEADER != 0)
    if (!s_sd_header_written) {
        wr_ok = bsp_sdio_file_append(OPLOG_SD_FILENAME, OPLOG_SD_HEADER_TEXT,
                                     (uint32_t)strlen(OPLOG_SD_HEADER_TEXT));
        printf("[OPLOG-SD] header write %s\r\n", wr_ok ? "ok" : "FAILED");
        if (wr_ok) {
            s_sd_header_written = 1;
        } else {
            return;                               /* 写失败（卡满之类），下次再试 */
        }
    }
#endif

    /* 把队列里现有的记录按入队顺序（旧到新）拼成文本，保持时间先后 */
    n   = s_sd_count;
    pos = s_sd_tail;
    for (i = 0; i < n; i++) {
        line_len = oplog_format(&s_sd_queue[pos], &s_flush_buf[buf_len],
                                (uint32_t)sizeof(s_flush_buf) - buf_len);
        if (line_len > 0) {
            buf_len += (uint32_t)line_len;
            if (buf_len + 2 < sizeof(s_flush_buf)) {
                s_flush_buf[buf_len++] = '\r';
                s_flush_buf[buf_len++] = '\n';
            }
        }
        pos = (uint16_t)((pos + 1) % OPLOG_SD_QUEUE_DEPTH);
    }

    /* TODO(debug): 同上，临时打印 */
    printf("[OPLOG-SD] formatted %u bytes from %u entries\r\n",
           (unsigned)buf_len, (unsigned)n);

    if (buf_len == 0) {
        return;
    }

    /* 一次 open+write+close，写成功才出队；失败就整批留在队列里，下次 poll 重试，
     * 如果因此被后续新记录挤满，走 oplog_sd_enqueue() 的丢最旧逻辑 */
    wr_ok = bsp_sdio_file_append(OPLOG_SD_FILENAME, s_flush_buf, buf_len);
    printf("[OPLOG-SD] data write %s\r\n", wr_ok ? "ok" : "FAILED");
    if (wr_ok) {
        s_sd_tail  = (uint16_t)((s_sd_tail + n) % OPLOG_SD_QUEUE_DEPTH);
        s_sd_count = (uint16_t)(s_sd_count - n);
        s_sd_last_flush_ts = now;
    }
}

#else /* OPLOG_SD_ENABLE == 0：空实现，main.c 不用加 #if 也能调 oplog_sd_poll() */

uint32_t oplog_sd_get_dropped(void) { return 0; }
void oplog_sd_poll(void) { }

#endif /* OPLOG_SD_ENABLE */


/* ========================================================================
 *  记录
 * ======================================================================== */

void oplog_init(void)
{
    oplog_clear_all();
}


void oplog_add(oplog_proto_t proto, oplog_dir_t dir,
               const uint8_t *data, uint16_t len, uint8_t ok)
{
    oplog_entry_t *e;
    uint16_t       n;

    if (!proto_dir_valid(proto, dir)) {
        return;
    }
    if (data == NULL || len == 0) {
        return;                                 /* 不记空条目 */
    }
#if (OPLOG_RECORD_BAD == 0)
    if (!ok) {
        return;                                 /* 只记校验通过的，见【1】 */
    }
#endif

    e = &s_ring[proto][dir][s_head[proto][dir]];
    s_head[proto][dir] = (uint16_t)((s_head[proto][dir] + 1) % OPLOG_DEPTH);
    s_total[proto][dir]++;

    n = (len > OPLOG_DATA_MAX) ? (uint16_t)OPLOG_DATA_MAX : len;

    e->seq       = ++s_seq;
    e->timestamp = bsp_rtc_get_unix_timestamp();
    e->raw_len   = len;                         /* 记真实长度，截断了也看得出来 */
    e->len       = (uint8_t)n;
    e->ok        = ok ? 1 : 0;
    e->proto     = (uint8_t)proto;
    e->dir       = (uint8_t)dir;
    memcpy(e->data, data, n);

#if (OPLOG_SD_ENABLE != 0)
    /* 纯 memcpy 入队，不做任何 I/O —— 真正的文件读写全部挪到主循环的
     * oplog_sd_poll() 里做，见 Op_log_function.h【1】节的说明。 */
    oplog_sd_enqueue(e);
#endif
}


/* ========================================================================
 *  查询
 * ======================================================================== */

uint16_t oplog_get_count(oplog_proto_t proto, oplog_dir_t dir)
{
    uint32_t total;

    if (!proto_dir_valid(proto, dir)) {
        return 0;
    }
    total = s_total[proto][dir];
    return (total > OPLOG_DEPTH) ? (uint16_t)OPLOG_DEPTH : (uint16_t)total;
}


uint32_t oplog_get_total(oplog_proto_t proto, oplog_dir_t dir)
{
    if (!proto_dir_valid(proto, dir)) {
        return 0;
    }
    return s_total[proto][dir];
}


const oplog_entry_t* oplog_get(oplog_proto_t proto, oplog_dir_t dir, uint16_t index)
{
    const oplog_entry_t *e;
    uint16_t             cnt;
    uint16_t             pos;

    if (!proto_dir_valid(proto, dir)) {
        return NULL;
    }
    cnt = oplog_get_count(proto, dir);
    if (index >= cnt) {
        return NULL;
    }

    /* head 指向下一个要写的槽，所以 head-1 是最新的一条，再往前数 index 条。
     * 加一个 OPLOG_DEPTH 再取模，避免无符号数减出大数 */
    pos = (uint16_t)((s_head[proto][dir] + OPLOG_DEPTH - 1 - index) % OPLOG_DEPTH);
    e   = &s_ring[proto][dir][pos];

    return (e->seq != 0) ? e : NULL;
}


uint16_t oplog_get_merged_count(void)
{
    uint16_t sum = 0;
    uint8_t  p, d;

    for (p = 0; p < OPLOG_PROTO_MAX; p++) {
        for (d = 0; d < OPLOG_DIR_MAX; d++) {
            sum = (uint16_t)(sum + oplog_get_count((oplog_proto_t)p, (oplog_dir_t)d));
        }
    }
    return sum;
}


/* 6 个环合并成一条时间流里的第 index 条（0 = 全局最新）。
 * 做法是找第 index+1 大的 seq：每一轮在"比上一轮选中的还小"的记录里挑最大的。
 * 一共 6 × OPLOG_DEPTH = 48 条，index 轮扫描，最坏 2000 多次比较，
 * 只在查询时跑，不在收发路径上，够用且不占 RAM（不需要排序数组）。 */
const oplog_entry_t* oplog_get_merged(uint16_t index)
{
    const oplog_entry_t *best = NULL;
    uint32_t             upper = 0xFFFFFFFFU;   /* 本轮只看 seq < upper 的 */
    uint16_t             k, i;
    uint8_t              p, d;

    if (index >= oplog_get_merged_count()) {
        return NULL;
    }

    for (k = 0; k <= index; k++) {
        best = NULL;
        for (p = 0; p < OPLOG_PROTO_MAX; p++) {
            for (d = 0; d < OPLOG_DIR_MAX; d++) {
                for (i = 0; i < OPLOG_DEPTH; i++) {
                    const oplog_entry_t *e = &s_ring[p][d][i];
                    if (e->seq == 0 || e->seq >= upper) {
                        continue;               /* 空槽，或本轮之前已经取过 */
                    }
                    if (best == NULL || e->seq > best->seq) {
                        best = e;
                    }
                }
            }
        }
        if (best == NULL) {
            return NULL;                        /* 没那么多条 */
        }
        upper = best->seq;
    }
    return best;
}


/* ========================================================================
 *  清除
 * ======================================================================== */

void oplog_clear(oplog_proto_t proto, oplog_dir_t dir)
{
    if (!proto_dir_valid(proto, dir)) {
        return;
    }
    memset(s_ring[proto][dir], 0, sizeof(s_ring[proto][dir]));
    s_head[proto][dir]  = 0;
    s_total[proto][dir] = 0;
}


void oplog_clear_all(void)
{
    memset(s_ring,  0, sizeof(s_ring));
    memset(s_head,  0, sizeof(s_head));
    memset(s_total, 0, sizeof(s_total));
    s_seq = 0;
}


/* ========================================================================
 *  格式化
 * ======================================================================== */

const char* oplog_proto_name(oplog_proto_t proto)
{
    switch (proto) {
        case OPLOG_PROTO_CUSTOM: return "CUSTOM";
        case OPLOG_PROTO_RTU:    return "RTU";
        case OPLOG_PROTO_ASCII:  return "ASCII";
        default:                 return "?";
    }
}


/* UTC 秒 -> 年月日时分秒。
 * 用的是把 3 月当年初的历法算法（闰年规则全部落到年末的 2 月，不用查表也不用
 * 特判闰年），民用历 1970 年之后都对。这里不引 <time.h> 的 gmtime：
 * armcc 的 gmtime 会拖进一大块库代码，而且要求 time_t 有效范围内的输入。 */
static void oplog_unix_to_datetime(uint32_t ts,
                                   int *year, int *mon, int *day,
                                   int *hour, int *min, int *sec)
{
    uint32_t days = ts / 86400UL;
    uint32_t rem  = ts % 86400UL;
    int32_t  z, era, doe, yoe, doy, mp;
    int32_t  yr;

    *hour = (int)(rem / 3600UL);
    rem  %= 3600UL;
    *min  = (int)(rem / 60UL);
    *sec  = (int)(rem % 60UL);

    z   = (int32_t)days + 719468;               /* 纪元挪到 0000-03-01 */
    era = ((z >= 0) ? z : (z - 146096)) / 146097;   /* 400 年一个循环 */
    doe = z - era * 146097;                     /* 循环内的第几天 0~146096 */
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    yr  = yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);  /* 以 3 月 1 日为第 0 天 */
    mp  = (5 * doy + 2) / 153;                  /* 0 = 3月 … 11 = 次年2月 */
    *day = (int)(doy - (153 * mp + 2) / 5 + 1);
    *mon = (int)(mp + ((mp < 10) ? 3 : -9));
    if (*mon <= 2) {
        yr++;                                   /* 1、2 月算下一年 */
    }
    *year = (int)yr;
}


/* 往 buf 里追加字符串，写不下就停，始终留一个位置给 '\0'。返回新的写入位置 */
static uint32_t oplog_append(char *buf, uint32_t pos, uint32_t size, const char *s)
{
    while (*s != '\0' && (pos + 1) < size) {
        buf[pos++] = *s++;
    }
    return pos;
}


int oplog_format(const oplog_entry_t *e, char *buf, uint32_t buf_size)
{
    static const char hex_chars[] = "0123456789ABCDEF";
    int      year, mon, day, hour, min, sec;
    uint32_t pos = 0;
    uint16_t i;
    int      n;
    uint8_t  as_hex;

    if (e == NULL || buf == NULL || buf_size < 32) {
        return -1;
    }

    /* 存的是 UTC，显示加时区偏移，和告警记录的 +8 对齐 */
    oplog_unix_to_datetime(e->timestamp + (uint32_t)(OPLOG_TZ_OFFSET_HOUR * 3600),
                           &year, &mon, &day, &hour, &min, &sec);

    n = snprintf(buf, buf_size, "%04d-%02d-%02d %02d:%02d:%02d | %s | %-6s | %u | ",
                 year, mon, day, hour, min, sec,
                 (e->dir == OPLOG_DIR_TX) ? "TX" : "RX",
                 oplog_proto_name((oplog_proto_t)e->proto),
                 (unsigned int)e->raw_len);
    /* snprintf 返回的是"本来要写多少"，放不下时会大于 buf_size，得夹一下 */
    if (n < 0) {
        return -1;
    }
    pos = ((uint32_t)n >= buf_size) ? (buf_size - 1) : (uint32_t)n;

    if (!e->ok) {
        pos = oplog_append(buf, pos, buf_size, "BAD ");
    }

    /* RTU 是二进制，必须转十六进制；自定义协议和 Modbus ASCII 本来就是
     * 可打印字符，原样显示更好认（"A5B60001…" / ":010300…"） */
#if (OPLOG_FORCE_HEX != 0)
    as_hex = 1;
#else
    as_hex = (e->proto == OPLOG_PROTO_RTU) ? 1 : 0;
#endif

    for (i = 0; i < e->len; i++) {
        uint8_t c = e->data[i];
        if (as_hex) {
            if (pos + 3 >= buf_size) {
                break;
            }
            buf[pos++] = hex_chars[(c >> 4) & 0x0F];
            buf[pos++] = hex_chars[c & 0x0F];
            buf[pos++] = ' ';
        } else {
            if (pos + 1 >= buf_size) {
                break;
            }
            /* 不可打印字符（换行、CRLF）用 '.' 顶替，免得把一行日志撑成好几行 */
            buf[pos++] = (char)((c >= 0x20 && c < 0x7F) ? c : '.');
        }
    }

    /* 报文比 OPLOG_DATA_MAX 长，后面还有没存下来的 */
    if (e->raw_len > e->len) {
        pos = oplog_append(buf, pos, buf_size, "...");
    }

    buf[pos] = '\0';
    return (int)pos;
}


/* ========================================================================
 *  调试打印
 * ======================================================================== */

void oplog_dump(oplog_proto_t proto, oplog_dir_t dir)
{
    char     line[OPLOG_LINE_MAX];
    uint16_t cnt;
    uint16_t i;

    if (!proto_dir_valid(proto, dir)) {
        return;
    }
    cnt = oplog_get_count(proto, dir);

    printf("[OPLOG] %s %s : %u kept / %lu total\r\n",
           oplog_proto_name(proto),
           (dir == OPLOG_DIR_TX) ? "TX" : "RX",
           (unsigned int)cnt,
           (unsigned long)oplog_get_total(proto, dir));

    for (i = 0; i < cnt; i++) {
        const oplog_entry_t *e = oplog_get(proto, dir, i);
        if (e == NULL) {
            break;
        }
        if (oplog_format(e, line, sizeof(line)) >= 0) {
            printf("  %s\r\n", line);
        }
    }
}


void oplog_dump_all(void)
{
    char     line[OPLOG_LINE_MAX];
    uint16_t cnt = oplog_get_merged_count();
    uint16_t i;

    printf("[OPLOG] merged, newest first : %u entries\r\n", (unsigned int)cnt);

    for (i = 0; i < cnt; i++) {
        const oplog_entry_t *e = oplog_get_merged(i);
        if (e == NULL) {
            break;
        }
        if (oplog_format(e, line, sizeof(line)) >= 0) {
            printf("  %s\r\n", line);
        }
    }
}
