#ifndef __BSP_SDIO_H_
#define __BSP_SDIO_H_

#include "bsp_sys.h"
#include "stdbool.h"
extern FIL fdst;
extern FATFS fs;
extern UINT br, bw;
//BYTE textfilebuffer[2048] = "GD32MCU FATFS TEST!\r\n";
extern BYTE buffer[128];
extern BYTE filebuffer[128];

/* 写文件时自动补建路径上的目录，用到的临时路径缓冲长度。
 * 路径比这个还长会被截断（截断后建出来的目录名不对，f_open 随后会失败），
 * _USE_LFN=1 之后单个文件名本身可以很长（FatFs 侧上限是 _MAX_LFN=255），
 * 真正的约束变成了这里的【路径总长】，所以名字取长了要把这个宏一起调大。 */
#define SD_PATH_MAX     64




// 初始化：上电时调一次，返回false表示无卡/初始化失败，之后所有文件操作都会失败
bool bsp_sdio_Init(void);

/* ==================== 长目录名 / 长文件名的读取支持 ==================== */

/* 【现状：LFN 已经打开了，这一段是兜底，不是必需品】
 *
 * ffconf.h 里 _USE_LFN 现在是 1，所以下面这两句【都】能直接用，不经过反查：
 *
 *      bsp_sdio_fw_test("Updata_file/V2.BIN");   // 长名，直接开
 *      bsp_sdio_fw_test("V2.BIN");               // 短名，照旧
 *
 * 打开 LFN 的三处改动、以及为什么不需要官方的 option/ccsbcs.c，写在
 * ffconf.h 里 _USE_LFN 下面那段 "Project note"，改之前先看那里。
 * 一句话版本：本仓库 FatFt/ 下没有 option 目录，ff_convert() / ff_wtoupper()
 * 用的是 bsp_sdio.c 末尾那份 ASCII-only 的最小实现。
 *
 * 【那为什么这三个函数还留着】
 * 一是【中文文件名仍然打不开】—— 那需要 _CODE_PAGE=936 + option/cc936.c，
 * 那张 GBK 表比本镜像剩下的 flash 还大。卡上如果有 PC 建的中文名文件，长名取
 * 不出来，但 Windows / Linux 在 FAT 上建长名时会同时写一条 8.3 短名条目做别名，
 * 规则是"主名去空格和点、大写、截到6个字符、接~1"：
 *
 *      Updata_file      ->  UPDATA~1
 *      firmware_v2.bin  ->  FIRMWA~1.BIN
 *
 * f_readdir 看得见这条短名，所以照这个规则反查还是能摸到文件。
 * 二是 _USE_LFN 万一因为 flash 吃紧被改回 0，这条路是唯一的退路。
 *
 * 也就是说：新代码直接写长路径就行，不用调这三个函数；它们是保险。 */

/* 兜底搜索时认哪种扩展名。按名字找不到时，会去找卡上第一个这种扩展名的文件 */
#define SD_FW_FILE_EXT      "BIN"

/* 兜底搜索开关：1 = 按名字找不到时，退而求其次找任意一个 .BIN；0 = 只按名字找。
 * 现场卡上只放一个升级文件时留 1 最省事（文件名叫什么都能升）；
 * 卡上同时有多个 bin、必须严格按名字认的场合改成 0 */
#define SD_FW_SEARCH_ANY_BIN    1

/* 把用户写的路径解析成 FatFs 真正能打开的 8.3 短路径。
 * path    : 可以带长目录名/长文件名，如 "Updata_file/V2.BIN"
 * out     : 解析结果，如 "UPDATA~1/V2.BIN"，可以直接拿去 f_open / bsp_sdio_file_*
 * 返回 true 表示路径上每一级都在卡上找到了
 *
 * 路径本来就是合法短路径时，只做一次 f_stat 就原样返回，不扫目录 */
bool bsp_sdio_resolve_path(const char* path, char* out, uint32_t out_size);

/* 在卡上检索升级文件，找到就把可直接 f_open 的短路径写进 out。
 * wanted : 期望的文件名或路径，传 NULL 用默认的 SD_FW_FILE_NAME
 *
 * 按四步找，命中任意一步就返回：
 *   1) 把 wanted 当路径解析（含长目录名）
 *   2) 在根目录下按文件名找
 *   3) 在根目录的每个一级子目录下按文件名找
 *   4) SD_FW_SEARCH_ANY_BIN=1 时，在根目录和各一级子目录里找第一个 .BIN
 * 只下潜一层子目录，卡上文件多也不会扫太久 */
bool bsp_sdio_fw_find(const char* wanted, char* out, uint32_t out_size);

/* 打印目录内容，现场用来看卡上到底有什么、长目录名的短名别名是什么。
 * dir 传 NULL 或 "" 表示根目录，会连带把一级子目录的内容一起列出来
 *
 * 【用法】升级文件读不到时，先来一句这个，比猜路径快：
 *
 *      bsp_sdio_list_dir(NULL);
 *
 * 输出形如：
 *      [DIR ] /
 *        <DIR>  UPDATA~1
 *        45312  APP.BIN
 *      [DIR ] /UPDATA~1
 *        45312  V2.BIN
 */
void bsp_sdio_list_dir(const char* dir);

// 文本文件读写（内容以'\0'结尾的字符串，长度靠strlen算）
bool bsp_sdio_file_write_text(const char* filename, const char* content);   // 覆盖写
bool bsp_sdio_file_append_text(const char* filename, const char* content);  // 追加写
int  bsp_sdio_file_read_text(const char* filename, char* buffer, uint32_t buffer_size);

// 二进制文件读写（长度显式给出，内容可以含0x00）
bool bsp_sdio_file_write(const char* filename, const void* data, uint32_t len);
int  bsp_sdio_file_read(const char* filename, void* buf, uint32_t size, uint32_t offset);

/* 二进制追加写：长度显式给出，内容可以含 0x00。
 * bsp_sdio_file_append_text() 靠 strlen 算长度，二进制数据遇到 0x00 就被截断，
 * 所以上位机分片下发文件时用这个，别用 append_text。 */
bool bsp_sdio_file_append(const char* filename, const void* data, uint32_t len);

/* 定位写：从 offset 处开始覆盖 len 个字节，文件不存在就新建。
 * offset 超过当前文件大小时 FatFs 会把文件扩到该位置，但中间那段的内容未定义
 * （不保证是 0），所以上位机乱序下发分片不会写错位置，但空洞别当成 0 用。
 * 顺序下发用 bsp_sdio_file_append() 更快（省一次 lseek）。 */
bool bsp_sdio_file_write_at(const char* filename, const void* data,
                            uint32_t len, uint32_t offset);

// 文件管理
bool     bsp_sdio_file_exists(const char* filename);
uint32_t bsp_sdio_file_get_size(const char* filename);
bool     bsp_sdio_file_delete(const char* filename);

/* 清空目录时最多往下递归几层，防止卡上目录结构异常时把栈递归穿了。
 * 每层约 70 字节栈（DIR + FILINFO + 局部变量），路径缓冲是各层共用的一块 */
#define SD_CLEAR_MAX_DEPTH  4

/* 清空一个文件的内容，或清空一个文件夹里的东西 —— 目标本身保留。
 *
 *      bsp_sdio_clear("TEST.TXT");        // 文件里的 "Hello" 没了，文件还在，大小 0
 *      bsp_sdio_clear("LOG");             // LOG 目录里的文件和子目录全删掉，LOG 目录还在
 *      bsp_sdio_clear("Updata_file");     // 长目录名也认，内部会反查成 UPDATA~1
 *      bsp_sdio_clear("/");               // 根目录：等于清空整张卡的内容（见下面警告）
 *
 * path 传文件还是目录不用自己区分，函数内部 f_stat 看属性自己判：
 *      文件 -> f_truncate 截成 0 字节，文件名/时间/属性都留着
 *      目录 -> 递归删掉里面所有文件和子目录，这个目录本身留着（是"清空"不是"删除"）
 * 想连目录一起删掉，清空之后再补一句 bsp_sdio_file_delete(path) 即可 —— f_unlink
 * 只能删空目录，所以顺序不能反。
 *
 * 路径可以带长目录名/长文件名（"Updata_file/DATA.TXT"），反查规则和
 * bsp_sdio_resolve_path() 一样，区别是这里最后一级允许是目录。
 *
 * 返回 true = 全部清干净了。目录里有一条没删掉（只读属性、层数超了）会返回 false，
 * 但剩下的照删不误，不会删一半就撂挑子；具体哪条失败会 printf 出 FRESULT 码。
 *
 * 【几个注意】
 * 1. path 传 "" 或 "/" 会把整张卡的内容清光，不可恢复。为了防手滑，传 NULL 是
 *    直接返回 false 而不是当根目录，但仍然只能由明确的指令触发，别放进上电流程。
 * 2. 不像 bsp_sdio_format()，本函数不动文件系统本身，几十个文件也就几十毫秒；
 *    但条目多的时候一样是阻塞的，主循环期间 RS485 收不了包。
 * 3. 正在 f_open 打开着的文件删不掉。本驱动每个函数都是 open 完立刻 close，
 *    唯一例外是升级流式读取的 s_fw_file —— 清目录前先 bsp_sdio_fw_close()。
 */
bool     bsp_sdio_clear(const char* path);

// 一键格式化：清空全卡，耗时可达几十秒且全程阻塞，只能由明确指令触发
bool bsp_sdio_format(void);

/* ==================== 容量 / 目录 / 重命名 / 建目录 / 按行读 ==================== */

/* 查询卡容量和剩余空间，单位 KB。不关心的那个传 NULL。
 *
 *      uint32_t total, freek;
 *      if (bsp_sdio_get_space(&total, &freek)) {
 *          printf("SD %luMB / %luMB\r\n", freek / 1024, total / 1024);
 *      }
 *
 * 用 KB 不用字节：32GB 卡的字节数 uint32_t 装不下。
 * 空闲簇数没缓存时（刚挂载、上次异常掉电）会整表扫一遍 FAT，32GB 卡几百毫秒，
 * 别放进主循环轮询。 */
bool bsp_sdio_get_space(uint32_t* total_kb, uint32_t* free_kb);

/* 目录里的一条。name 是 8.3 短名，长名在卡上的别名就长这样（UPDATA~1） */
typedef struct {
    char     name[13];      // 文件/目录名，'\0' 结尾（FatFs 的 8.3 名最长 12 字符）
    uint32_t size;          // 字节数，目录恒为 0
    uint16_t date;          // FAT 日期：bit15~9 = 年-1980，bit8~5 = 月，bit4~0 = 日
    uint16_t time;          // FAT 时间：bit15~11 = 时，bit10~5 = 分，bit4~0 = 秒/2
    bool     is_dir;        // true = 目录
} sd_entry_t;

/* 把目录内容填进数组。bsp_sdio_list_dir() 只往调试串口 printf，上位机拿不到，
 * 要把文件列表回给主站就用这个。
 * dir     : 目录路径，传 NULL / "" / "/" 表示根目录，可以带长目录名
 * out     : 结果数组，只填前 max_cnt 条
 * max_cnt : out 的容量，传 0 就只数个数不填（out 可以给 NULL）
 *
 * 返回目录里的条目总数，-1 表示失败。注意返回值可能【大于】max_cnt —— 那表示
 * 结果被截断了，只有前 max_cnt 条写进了 out：
 *
 *      sd_entry_t list[16];
 *      int n = bsp_sdio_list_to_buf(NULL, list, 16);
 *      if (n > 16) { ... 卡上不止16个，list里只有前16条 ... }
 *
 * "." / ".." 不计入，也不会填进去。只列本级，不下潜子目录。 */
int bsp_sdio_list_to_buf(const char* dir, sd_entry_t* out, uint32_t max_cnt);

/* 建目录，多级路径会一级一级建出来（"A/B/C" 会把 A、A/B 一起建了）。
 * 目录已存在返回 true（f_mkdir 的 FR_EXIST 按成功算）。
 * 路径结尾不要带斜杠。_USE_LFN=1，目录名长度不限，但整条路径要短于 SD_PATH_MAX，
 * 且只能用 ASCII 字符（中文名 f_mkdir 返回 FR_INVALID_NAME，本函数返回 false）。 */
bool bsp_sdio_mkdir(const char* path);

/* 重命名 / 移动。改名和挪目录是同一个操作，看新路径带不带目录：
 *
 *      bsp_sdio_rename("SAMPLE.CSV", "SAMPLE.BAK");        // 改名（日志轮转）
 *      bsp_sdio_rename("SAMPLE.CSV", "BAK/SAMPLE.CSV");    // 移进 BAK 目录
 *
 * _USE_LFN=1 之后两个参数都可以直接写长名（ASCII），new_path 不再受 8.3 限制。
 * old_path 找不到时还会走一次 8.3 反查兜底，见本文件开头那段说明。
 * 新路径上的目录不存在会自动建出来。
 * 目标名字已被占用时返回 false（FatFs 的 f_rename 不覆盖，返回 FR_EXIST）。
 * 目录也能移，FatFs 会同步更新它的 ".." 条目。正在 f_open 打开的文件不要动。 */
bool bsp_sdio_rename(const char* old_path, const char* new_path);

/* 按行读的返回码。>=0 是行长度（空行为 0） */
#define SD_LINE_ERR     (-1)    // 卡没就绪 / 文件打不开 / 读卡出错
#define SD_LINE_EOF     (-2)    // 已到文件尾，没有这一行了

/* 读第 line_no 行（从 0 开始数），行尾的 "\r\n" 会去掉，buf 可直接当字符串用。
 *
 *      char line[64];
 *      if (bsp_sdio_file_read_line("CONFIG.TXT", 0, line, sizeof(line)) >= 0) {
 *          ... 解析第一行 ...
 *      }
 *
 * 每调一次都要从文件头重新数行，逐行遍历整个文件请改用下面那个 _at 版本。
 * 行比 buf_size-1 还长时会被切成两行返回，不丢数据但也不报错。 */
int bsp_sdio_file_read_line(const char* filename, uint32_t line_no,
                            char* buf, uint32_t buf_size);

/* 按字节偏移逐行读，offset 既是入参也是出参：传进去是本行起点，读完被改成
 * 下一行的起点。适合把整个 CSV 一行行发给上位机 —— 不用每行都从头数。
 *
 *      char line[96];
 *      uint32_t off = 0;
 *      while (bsp_sdio_file_read_line_at("SAMPLE.CSV", &off, line, sizeof(line)) >= 0) {
 *          Protocol_SendFrame(..., (uint8_t*)line, strlen(line));
 *      }
 *
 * 返回 SD_LINE_EOF 就是读完了。offset 也可以自己存着，下次接着读（断点续传）。 */
int bsp_sdio_file_read_line_at(const char* filename, uint32_t* offset,
                               char* buf, uint32_t buf_size);

/* ==================== TF卡固件升级文件（读取部分） ==================== */

/* 升级镜像在卡上的默认文件名，fw_probe/fw_open/fw_test 传 NULL 时用这个。
 * 这里写短名，因为它同时也是 f_open 的直接目标；要放在子目录里、或者名字超出
 * 8.3，写 "Updata_file/V2.BIN" 这种也可以，上面那段的路径解析会兜住 */
#define SD_FW_FILE_NAME     "APP.BIN"

// 支持两种文件格式，probe 时按前4字节自动识别：
//   1) 裸bin      —— fromelf --bin 直接导出的，文件开头就是向量表
//   2) 带头部的包 —— 8字节头部(magic 4 + 版本号 4) + 裸bin，
//                    和 app_upgrade.c 里串口升级用的包格式一致
#define SD_FW_MAGIC_WORD    0x5AA5C33CU
#define SD_FW_HEADER_SIZE   8U

// 镜像合法性检查用的地址常量，必须和 App 实际链接运行的地址保持一致：
//     LR_IROM1 0x08011000 0x00020000   App 加载/运行地址，128KB
//     RW_IRAM1 0x20000000 0x00030000   RAM 起始地址，192KB
// 这两个数字跟 Driver/BootConfig/BootConfig.h 的 FLASH_APP_BASE / FLASH_APP_SIZE
// 应该永远相等——那边是"App 代码认为自己在哪"，这边是"TF卡升级镜像校验用哪个地址
// 判断合法性"，两个不一致的话，合法的镜像会被判成非法（或者反过来，非法的通过了）。
// 现场改 App 地址时这两处必须一起改，改完还要去 BootConfig.h 顶部看还有没有其它
// 要同步的地方（Keil IROM1 设置 / eide.yml 的 storageLayout）。
#define SD_FW_APP_BASE      0x08011000U
#define SD_FW_APP_MAX_SIZE  0x00020000U
#define SD_FW_RAM_BASE      0x20000000U
#define SD_FW_RAM_SIZE      0x00030000U

// 失败原因分开返回，方便通过 RS485/Modbus 回给主站，也方便现场定位
typedef enum {
    SD_FW_OK = 0,
    SD_FW_ERR_NO_CARD,      // 卡没就绪或没挂载（bsp_sdio_Init 没成功）
    SD_FW_ERR_NO_FILE,      // 卡上没有这个文件
    SD_FW_ERR_EMPTY,        // 文件是空的
    SD_FW_ERR_TOO_BIG,      // 比 App 区还大，烧不进去
    SD_FW_ERR_NOT_APP,      // 向量表不像本机App，多半是拿错了bin
    SD_FW_ERR_READ          // 读卡出错
} sd_fw_result_t;

// 卡是否已就绪（bsp_sdio_Init 的结果，省得各处自己存一份 sd_ready）
bool bsp_sdio_is_ready(void);

// 只检查不读取：确认卡上的升级文件存在、大小合理、是本机能跑的镜像
sd_fw_result_t bsp_sdio_fw_probe(const char* filename, uint32_t* out_size);

// 流式读取：open 一次，read 多次，最后必须 close。
// 文件带8字节头部时 open 会自动跳过，read 出来的一律是纯镜像数据
sd_fw_result_t bsp_sdio_fw_open(const char* filename, uint32_t* out_size);
int            bsp_sdio_fw_read(void* buf, uint32_t size);
void           bsp_sdio_fw_close(void);

// 最近一次 probe/open 的识别结果
uint32_t bsp_sdio_fw_get_offset(void);   // 镜像在文件里的起始偏移：0=裸bin，8=带头部
uint32_t bsp_sdio_fw_get_version(void);  // 头部里的版本号，裸bin时为0
const char* bsp_sdio_fw_get_path(void);  // 实际打开的短路径，传长路径时和传进去的不一样

// 错误码转字符串，给 printf/OLED 用（英文，中文在串口会乱码）
const char* bsp_sdio_fw_strerr(sd_fw_result_t res);

// 自检打印：不碰flash，只验证"卡上的镜像能不能完整读出来"，顺带打印向量表
void bsp_sdio_fw_test(const char* filename);

// 自检打印，验证完可连同实现一起删掉
void bsp_sdio_test(bool sd_ready);

#endif


