#include "bsp_sdio.h"


FIL fdst;
FATFS fs;
UINT br, bw;
BYTE buffer[128];
BYTE filebuffer[128];

// 卡是否挂载成功，bsp_sdio_Init() 里更新，给 bsp_sdio_is_ready() 和下面的升级文件读取用
static bool s_sd_mounted = false;


/* 每个栈上的 FILINFO 用之前必须先过这个宏，别省。
 *
 * ffconf.h 里 _USE_LFN = 1 之后，FILINFO 末尾会多出 lfname / lfsize 两个成员，
 * 而 ff.c 的 get_fileinfo() 是 if (fno->lfname && fno->lfsize) 之后【直接往
 * fno.lfname 指的地址写长文件名】。fno 是栈上的局部变量，不初始化的话 lfname
 * 就是栈里的随机值，非 0 时会往一个随机地址写内存 —— 现象是随机的数据错乱或
 * 硬件异常，且离案发现场很远，非常难查。
 *
 * 置 NULL 表示"只要 8.3 名，不要长名"，f_readdir/f_stat 照常工作，fno.fname
 * 里还是 8.3 别名 —— 正好是 sd_dir_lookup() / sd_search_tree() 那套反查逻辑
 * 要的东西，所以它们不用动。
 * 哪天要在某处拿到长名，把那一处换成 fno.lfname = 自己的缓冲区 + fno.lfsize
 * = 缓冲区长度即可，别改这个宏。
 *
 * 用宏而不是直接写 fno.lfname = NULL，是因为 _USE_LFN 关掉时 FILINFO 里根本
 * 没有 lfname 成员，直接写会编译不过。有了这层，_USE_LFN 可以随时改回 0。 */
#if _USE_LFN
    #define SD_FILINFO_INIT(f)      do { (f).lfname = NULL; (f).lfsize = 0; } while (0)
#else
    #define SD_FILINFO_INIT(f)      do { } while (0)
#endif


/* 把路径里每一级目录都建出来。FatFs 的 f_open 只建文件不建目录，写 "A/test.txt"
 * 时若 A 不存在会直接返回 FR_NO_PATH(5)，这是 FatFs 的固有行为。有了这一层，
 * bsp_sdio_file_write_text("A/B/test.txt", ...) 可以直接用。
 *
 * 做法是从左往右扫，遇到 '/' 临时换成 '\0'，拿前缀去 f_mkdir，再换回来。
 * 目录已存在时 f_mkdir 返回 FR_EXIST，属正常情况，不判返回值。
 *
 * ffconf.h 里 _USE_LFN = 1，目录名和文件名长度都不限（受 SD_PATH_MAX 约束路径
 * 总长），但只能是 ASCII 字符，中文名会返回 FR_INVALID_NAME(6)。 */
static void sd_make_parent_dirs(const char* path)
{
    char tmp[SD_PATH_MAX];
    int  i;

    if (path == NULL) return;

    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    for (i = 0; tmp[i] != '\0'; i++) {
        if (tmp[i] == '/' && i > 0) {
            tmp[i] = '\0';
            f_mkdir(tmp);          /* 已存在会返回 FR_EXIST，忽略即可 */
            tmp[i] = '/';
        }
    }
}

/* 初始化 SD 卡并挂载文件系统。
 * 返回 true = 卡就绪且已挂载，false = 无卡或初始化失败，此后所有文件操作都会失败。
 * 上电流程里调一次，排在 bsp_debug_init() 之后，失败信息才打得出来。
 * 失败不影响其它功能，不需要 return。 */
bool bsp_sdio_Init(void)
{
    s_sd_mounted = false;   // 允许重新插卡后再调一次，先把状态清掉

    nvic_irq_enable(SDIO_IRQn, 1, 0);					// 使能SDIO中断，抢占优先级1（低于RS485的0）

    // disk_initialize 才是真正上电识卡的一步，失败就没必要往下走了
    if(0 != disk_initialize(0)){
        return false;
    }

    // FatFs R0.09 的 f_mount 只登记 FATFS 对象、不访问硬件，
    // 真正读FAT表发生在第一次 f_open/f_stat 时
    s_sd_mounted = (FR_OK == f_mount(0, &fs));
    return s_sd_mounted;
}


/* 覆盖写文本文件：不存在则创建，已存在则清空后重写。
 * filename 长短不限（_USE_LFN=1），"LOG.TXT" 和 "operation_log.txt" 都行，只能 ASCII
 * content  以 '\0' 结尾的字符串，'\0' 本身不写进文件
 * 返回 true = 全部写入成功。
 * 每次调用都会冲掉原有内容，要接在后面用 bsp_sdio_file_append_text()。 */
bool bsp_sdio_file_write_text(const char* filename, const char* content)
{
    FIL file;
    UINT bw;      // 实际写入的字节数
    FRESULT res;
    
    sd_make_parent_dirs(filename);   // f_open 不建目录，路径带目录时先建出来

    // FA_CREATE_ALWAYS: 总是创建新文件，如果存在则覆盖
    // FA_WRITE: 写权限
    res = f_open(&file, filename, FA_CREATE_ALWAYS | FA_WRITE);
    if (res != FR_OK) {
        // 把 FRESULT 打出来，否则只知道"失败了"查不出原因。
        // 常见：5=FR_NO_PATH(目录不存在) 6=FR_INVALID_NAME(超出8.3) 3=FR_NOT_READY(没卡)
        printf("f_open(%s) w failed: %d\r\n", filename, res);
        return false;
    }

    // 写入数据内容 (不写入字符串末尾的 \0)
    res = f_write(&file, content, strlen(content), &bw);
    
    // 必须关闭文件，数据才会真正保存进卡里！
    f_close(&file);
    
    // 判断是否全部写入成功
    return (res == FR_OK && bw == strlen(content));

}


/* 追加写文本文件：不存在则创建，已存在则接在末尾。返回 true = 全部写入成功。
 * 每调一次都要走完 open+lseek+write+close 一整轮，高频写（每 10ms 一条）既慢又伤卡，
 * 应先在 RAM 里攒够一批再一次性写下去。 */
bool bsp_sdio_file_append_text(const char* filename, const char* content)
{
    FIL file;
    UINT bw;
    FRESULT res;

    sd_make_parent_dirs(filename);   // f_open 不建目录，路径带目录时先建出来

    // 本版本是 FatFs R0.09，没有 R0.12+ 才有的 FA_OPEN_APPEND，
    // 所以用 FA_OPEN_ALWAYS 打开后手动 lseek 到文件末尾来实现追加
    res = f_open(&file, filename, FA_OPEN_ALWAYS | FA_WRITE);
    if (res != FR_OK) {                 // 必须先判失败：open失败时file是无效句柄，不能再拿去lseek
        printf("f_open(%s) a failed: %d\r\n", filename, res);
        return false;
    }
    f_lseek(&file, f_size(&file));      // 指针移到文件末尾，实现追加
    
    res = f_write(&file, content, strlen(content), &bw);
    
    f_close(&file);
    return (res == FR_OK && bw == strlen(content));
}


/* 读文本文件到 buffer，末尾自动补 '\0'，可直接当字符串用。
 * buffer_size 是 buffer 总大小，最多读 buffer_size-1 字节，留一位给 '\0'。
 * 返回实际读到的字节数，-1 表示文件不存在或读取失败。
 * 文件比 buffer 大时只读前 buffer_size-1 字节，且不会报告截断。要完整读大文件，
 * 先用 bsp_sdio_file_get_size() 拿大小，再用 bsp_sdio_file_read() 带 offset 分段读。 */
int bsp_sdio_file_read_text(const char* filename, char* buffer, uint32_t buffer_size)
{
    FIL file;
    UINT br;      // 实际读出的字节数
    FRESULT res;
    
    // 安全检查，防止传空指针或长度为0

    if (buffer == NULL || buffer_size == 0) return -1;

    
    // FA_READ: 读权限
    // FA_OPEN_EXISTING: 文件必须存在，否则报错
    res = f_open(&file, filename, FA_READ | FA_OPEN_EXISTING);
    if (res != FR_OK) return -1; // 打开失败
    
    // 开始读取 (最大读取长度是 缓冲区大小-1，为了给结束符 \0 留位置)
    res = f_read(&file, buffer, buffer_size - 1, &br);
    
    f_close(&file);
    
    if (res == FR_OK) {
        // 安全起见，在读取到的数据末尾加上字符串结束符，防止printf打印乱码
        buffer[br] = '\0'; 
        return (int)br; // 返回实际读到的字节数
    }
    
    return -1; // 读取过程中出错
}



/* 检查文件是否存在 */
bool bsp_sdio_file_exists(const char* filename)
{
    FILINFO fno;
    SD_FILINFO_INIT(fno);
    // f_stat 用于获取文件状态，如果返回 FR_OK 说明文件存在
    if (f_stat(filename, &fno) == FR_OK) {
        return true;
    }
    return false;
}

/* 获取文件大小
 * 返回：字节数。如果文件不存在返回 0
 *
 * 【用法】日志涨到64KB就删掉重来，防止把卡写满：
 *
 *      if(bsp_sdio_file_get_size("ALARM.LOG") > 64 * 1024){
 *          bsp_sdio_file_delete("ALARM.LOG");
 *      }
 *
 * 注意：返回0有两种含义 —— 文件不存在，或者文件确实是空的。
 *       需要区分就配合 bsp_sdio_file_exists() 一起用
 */
uint32_t bsp_sdio_file_get_size(const char* filename)
{
    FILINFO fno;
    SD_FILINFO_INIT(fno);
    if (f_stat(filename, &fno) == FR_OK) {
        return fno.fsize; // 返回文件大小，单位是字节
    }
    return 0; 
}


/* 删除文件。返回 true = 成功，false = 失败或文件不存在。
 * 文件处于 f_open 打开状态时删不掉，但本文件每个函数都是 open 完立刻 close。 */
bool bsp_sdio_file_delete(const char* filename) {
    // f_unlink 用于删除文件或空目录
    if (f_unlink(filename) == FR_OK) {
        return true;
    }
    return false;
}


/* 写文件（二进制安全），覆盖模式，文件已存在会被清空重写。
 * 和 bsp_sdio_file_write_text 的区别是长度由 len 显式给出，不靠 strlen，
 * 内容里可以含 0x00，适合存结构体、采样数组等裸数据。返回 true = 全部写入成功。 */
bool bsp_sdio_file_write(const char* filename, const void* data, uint32_t len)
{
    FIL file;
    UINT bw;
    FRESULT res;

    if (filename == NULL) return false;
    if (data == NULL && len > 0) return false;

    sd_make_parent_dirs(filename);   // f_open 不建目录，路径带目录时先建出来

    res = f_open(&file, filename, FA_CREATE_ALWAYS | FA_WRITE);
    if (res != FR_OK) {
        printf("f_open(%s) wb failed: %d\r\n", filename, res);
        return false;
    }

    res = f_write(&file, data, (UINT)len, &bw);

    // 必须关闭，否则最后一个扇区还在 FIL 的缓存里没落盘
    f_close(&file);

    return (res == FR_OK && bw == len);
}


/* 追加写（二进制安全）。长度由 len 显式给出，内容里可以含 0x00。
 * 和 bsp_sdio_file_append_text 的区别就是不靠 strlen 算长度 —— 上位机分片下发
 * 文件时数据里出现 0x00 是常事，用 append_text 会被截断。
 * 本版本是 FatFs R0.09，没有 FA_OPEN_APPEND，所以打开后手动 lseek 到文件末尾。 */
bool bsp_sdio_file_append(const char* filename, const void* data, uint32_t len)
{
    FIL file;
    UINT bw = 0;
    FRESULT res;

    if (filename == NULL || data == NULL || len == 0) return false;

    sd_make_parent_dirs(filename);   // f_open 不建目录，路径带目录时先建出来

    res = f_open(&file, filename, FA_OPEN_ALWAYS | FA_WRITE);
    if (res != FR_OK) {              // open 失败时 file 是无效句柄，不能再拿去 lseek
        printf("f_open(%s) ab failed: %d\r\n", filename, res);
        return false;
    }

    res = f_lseek(&file, f_size(&file));            // 指针移到文件末尾
    if (res == FR_OK) res = f_write(&file, data, (UINT)len, &bw);

    f_close(&file);                  // 必须关闭，否则最后一个扇区还在 FIL 缓存里

    return (res == FR_OK && bw == len);
}


/* 定位写（二进制安全）：从 offset 开始覆盖 len 个字节，文件不存在就新建。
 * 用 FA_OPEN_ALWAYS 而不是 FA_CREATE_ALWAYS —— 后者会先清空文件，分片写第二片
 * 就把第一片抹掉了。offset 超过当前文件大小时 FatFs 把文件扩到该位置，中间那段
 * 内容未定义（不保证是 0），落点靠下面的 f_tell 核对。
 * 上位机乱序下发分片时用这个；顺序下发用 bsp_sdio_file_append() 更快。 */
bool bsp_sdio_file_write_at(const char* filename, const void* data,
                            uint32_t len, uint32_t offset)
{
    FIL file;
    UINT bw = 0;
    FRESULT res;

    if (filename == NULL || data == NULL || len == 0) return false;

    sd_make_parent_dirs(filename);

    res = f_open(&file, filename, FA_OPEN_ALWAYS | FA_WRITE);
    if (res != FR_OK) {
        printf("f_open(%s) r+b failed: %d\r\n", filename, res);
        return false;
    }

    res = f_lseek(&file, (DWORD)offset);
    // f_lseek 越过文件末尾时 R0.09 会扩展文件，落点仍要自己核对
    if (res == FR_OK && f_tell(&file) != offset) res = FR_INT_ERR;
    if (res == FR_OK) res = f_write(&file, data, (UINT)len, &bw);

    f_close(&file);

    return (res == FR_OK && bw == len);
}


/* 从指定偏移读文件（二进制安全）。offset 为起始字节，0 表示从头读。
 * 不像 bsp_sdio_file_read_text 那样在末尾补 '\0'，读多少给多少。
 * 返回实际读到的字节数，0 表示已到文件末尾，-1 表示失败。
 * 文件比 buffer 大时靠递增 offset 分段读，循环到返回 0 为止。 */
int bsp_sdio_file_read(const char* filename, void* buf, uint32_t size, uint32_t offset)
{
    FIL file;
    UINT br;
    FRESULT res;

    if (filename == NULL || buf == NULL || size == 0) return -1;

    res = f_open(&file, filename, FA_READ | FA_OPEN_EXISTING);
    if (res != FR_OK) return -1;

    if (offset > 0) {
        res = f_lseek(&file, (DWORD)offset);
        // f_lseek 越过文件末尾时不报错，只把指针停在末尾，要自己核对落点
        if (res != FR_OK || f_tell(&file) != offset) {
            f_close(&file);
            return -1;
        }
    }

    res = f_read(&file, buf, (UINT)size, &br);
    f_close(&file);

    return (res == FR_OK) ? (int)br : -1;
}


/* 格式化 TF 卡，FAT12/16/32 由 FatFs 按容量自动选。返回 true = 成功。
 * 1. 全卡数据清空，不可恢复。
 * 2. 慢且阻塞。f_mkfs 初始化 FAT 区是一个扇区一个扇区写的（ff.c 原话
 *    "This loop may take a time on FAT32 volume"），32GB 卡实测几十秒，期间主循环
 *    完全停摆、RS485 收不了包。所以只能由明确的用户指令触发，不能放在上电流程里。
 * 3. 必须先成功调用过 bsp_sdio_Init()：f_mkfs 要从 f_mount 登记过的 FATFS 对象里
 *    拿工作缓冲区，没登记会直接返回 FR_NOT_ENABLED。 */
bool bsp_sdio_format(void)
{
    FRESULT res;
    DWORD free_clust;
    FATFS *pfs;

    // drv=0 逻辑盘号
    // sfd=0 建 MBR 分区表(FDISK 模式)。SFD(1) 是无分区表裸格式，部分读卡器/相机不认
    // au=0  簇大小由 FatFs 按卡容量自动选
    res = f_mkfs(0, 0, 0);
    if (res != FR_OK) return false;

    // f_mkfs 内部把 fs->fs_type 清成 0，下次文件操作 chk_mounted 会自动重新挂载，
    // 不用手动 f_mount。这次 f_getfree 只是确认新文件系统能读起来
    return (FR_OK == f_getfree("0:", &free_clust, &pfs));
}


/* ==========================================================================
 *  长目录名 / 长文件名的解析与检索
 *
 *  ffconf.h 里 _USE_LFN 现在是 1，f_open("Updata_file/V2.BIN") 已经能直接开了，
 *  这一段【不再是必需品】，留着是兜底，两种情况下还用得上：
 *    1. 中文名文件 —— 那要 _CODE_PAGE=936 + option/cc936.c，GBK 表比剩余 flash 还大，
 *       做不了。但 Windows / Linux 在 FAT 上建长名时会同时写一条 8.3 短名条目做别名
 *       （"Updata_file" → "UPDATA~1"），f_readdir 看得见，照规则反查还是能摸到。
 *    2. _USE_LFN 万一因为 flash 吃紧被改回 0，这是唯一的退路。
 *  新代码直接写长路径即可，不必调这一族函数。详见 bsp_sdio.h 里的说明。
 *
 *  两层能力：
 *      bsp_sdio_resolve_path()  按用户写的路径逐级反查，拼出能打开的短路径
 *      bsp_sdio_fw_find()       反查不到时直接按文件名 / 扩展名在卡上检索
 *  升级相关的 fw_probe / fw_open / fw_test 内部都走 fw_find。
 * ========================================================================== */

/* ASCII 大写化。短名条目在卡上一律是大写存的，比较前两边都拉到大写 */
static char sd_upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}


/* 大小写无关的字符串相等判断 */
static bool sd_stricmp_eq(const char* a, const char* b)
{
    while (*a != '\0' && *b != '\0') {
        if (sd_upper(*a) != sd_upper(*b)) {
            return false;
        }
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}


/* 找扩展名的点，没有返回 NULL。取最后一个点（"v2.1.bin" 的扩展名是 "bin"），
 * 开头那个点不算（".gitignore" 整体是主名，FAT 短名生成规则如此） */
static const char* sd_find_ext(const char* name)
{
    const char* dot = NULL;
    int         i;

    for (i = 0; name[i] != '\0'; i++) {
        if (name[i] == '.' && i > 0) {
            dot = &name[i];
        }
    }
    return dot;
}


/* 按短名别名的生成规则取主名：去空格和点、大写。
 * "Updata_file" → "UPDATA_FILE"，短名本身也走这一遍 */
static void sd_sfn_base(const char* name, char* out, uint32_t out_size)
{
    const char* dot = sd_find_ext(name);
    uint32_t    n   = 0;
    int         i;

    for (i = 0; name[i] != '\0'; i++) {
        if (dot != NULL && &name[i] >= dot) {
            break;                                  /* 到扩展名了，主名结束 */
        }
        if (name[i] == ' ' || name[i] == '.') {
            continue;                               /* 空格和点在短名里会被丢掉 */
        }
        if (n + 1 >= out_size) {
            break;
        }
        out[n++] = sd_upper(name[i]);
    }
    out[n] = '\0';
}


/* 同上，取扩展名：大写，最多 3 个字符 */
static void sd_sfn_ext(const char* name, char* out, uint32_t out_size)
{
    const char* dot = sd_find_ext(name);
    uint32_t    n   = 0;
    int         i;

    if (dot != NULL) {
        for (i = 1; dot[i] != '\0' && n < 3; i++) {
            if (n + 1 >= out_size) {
                break;
            }
            out[n++] = sd_upper(dot[i]);
        }
    }
    out[n] = '\0';
}


/* 判断 f_readdir 返回的短名 sfn 是不是 want 指的那一个。两种命中方式：
 *   1) 大小写无关地完全相等，want 本来就是合法短名（"V2.BIN"）
 *   2) sfn 是 want 的短名别名，want 是长名（"Updata_file" 对 "UPDATA~1"）
 *
 * 别名规则：主名去空格和点、大写、截到 6 个字符再接 "~序号"，扩展名取前 3 字符大写。
 * 所以判断条件是扩展名一致，且 sfn 里 '~' 前那截是 want 主名的前缀。
 *
 * 局限：同目录下前 6 个字符相同的长名会生成 "~1" "~2" 多条，这里认扫到的第一条；
 * 个别系统冲突时改用带哈希的别名（如 "UP1A2B~1"），认不出来。
 * 这两种情况由 bsp_sdio_fw_find() 的扩展名兜底搜索处理。 */
static bool sd_name_match(const char* sfn, const char* want)
{
    char     sbase[16], sext[8];
    char     wbase[40], wext[8];
    uint32_t nb;

    if (sd_stricmp_eq(sfn, want)) {
        return true;                                /* 合法短名，直接命中 */
    }

    sd_sfn_base(sfn,  sbase, sizeof(sbase));
    sd_sfn_ext (sfn,  sext,  sizeof(sext));
    sd_sfn_base(want, wbase, sizeof(wbase));
    sd_sfn_ext (want, wext,  sizeof(wext));

    if (!sd_stricmp_eq(sext, wext)) {
        return false;                               /* 扩展名对不上，不可能是同一个 */
    }

    /* 短名主名形如 "UPDATA~1"，找到 '~' 的位置 */
    for (nb = 0; sbase[nb] != '\0'; nb++) {
        if (sbase[nb] == '~') {
            break;
        }
    }
    if (nb == 0 || sbase[nb] != '~') {
        return false;                               /* 没有 '~'，不是别名 */
    }
    /* '~' 后面必须跟序号。'~' 本身是合法短名字符，"A~B.TXT" 这种真短名不能被当成
     * 别名去做前缀匹配，否则一个字符就能误判 */
    if (sbase[nb + 1] < '0' || sbase[nb + 1] > '9') {
        return false;
    }

    if (strlen(wbase) < nb) {
        return false;
    }
    return (0 == strncmp(sbase, wbase, nb));        /* '~' 前那截是长名主名的前缀 */
}


/* 把目录和名字拼成一条路径，dir 为空则只有名字。放不下返回 false，不截断：
 * 截断出来的路径可能打开别的文件，比直接失败更难查 */
static bool sd_path_join(const char* dir, const char* name, char* out, uint32_t out_size)
{
    uint32_t dl = (dir == NULL) ? 0 : (uint32_t)strlen(dir);
    uint32_t nl = (uint32_t)strlen(name);

    if (out == NULL) {
        return false;
    }
    if (dl > 0) {
        if (dl + 1 + nl + 1 > out_size) {
            return false;
        }
        if (out != dir) {           /* 允许 dir 和 out 是同一块缓冲（逐级往后接路径） */
            memcpy(out, dir, dl);
        }
        out[dl] = '/';
        memcpy(out + dl + 1, name, nl + 1);
    } else {
        if (nl + 1 > out_size) {
            return false;
        }
        memcpy(out, name, nl + 1);
    }
    return true;
}


/* 在目录 dir 里逐条扫，找第一个匹配的条目，把它真正的 8.3 短名写进 out_sfn。
 * dir      : 目录路径，根目录传 ""
 * want     : 要找的名字（可以是长名），传 NULL 表示不看名字
 * ext      : 要求的扩展名（不带点，如 "BIN"），传 NULL 表示不限
 * need_dir : true 只找目录，false 只找文件
 *
 * out_sfn 至少要有 13 字节（FILINFO.fname 就是 TCHAR[13]）。
 * R0.09 没有 f_closedir，DIR 对象不占资源，扫到就直接 return */
static bool sd_dir_lookup(const char* dir, const char* want, const char* ext,
                          bool need_dir, char* out_sfn)
{
    DIR     dp;
    FILINFO fno;
    char    cur_ext[8];

    SD_FILINFO_INIT(fno);
    if (FR_OK != f_opendir(&dp, (dir == NULL) ? "" : dir)) {
        return false;
    }

    for (;;) {
        fno.fname[0] = '\0';
        if (FR_OK != f_readdir(&dp, &fno)) {
            break;
        }
        if (fno.fname[0] == '\0') {
            break;                                  /* 空名字表示已经读完 */
        }
        if (fno.fname[0] == '.') {
            continue;                               /* "." / ".." 跳过 */
        }

        /* 要目录就只看目录，要文件就只看文件 */
        if (need_dir != ((fno.fattrib & AM_DIR) != 0)) {
            continue;
        }
        if (want != NULL && !sd_name_match(fno.fname, want)) {
            continue;
        }
        if (ext != NULL) {
            sd_sfn_ext(fno.fname, cur_ext, sizeof(cur_ext));
            if (!sd_stricmp_eq(cur_ext, ext)) {
                continue;
            }
        }

        strncpy(out_sfn, fno.fname, 13);
        out_sfn[12] = '\0';
        return true;
    }
    return false;
}


/* 把用户写的路径逐级反查成能打开的短路径，如 "Updata_file/V2.BIN" → "UPDATA~1/V2.BIN"。
 * 详细说明见 bsp_sdio.h。
 * 只用于读。写文件时目标还不存在，反查必然失败，写路径直接用短名，
 * 目录由 sd_make_parent_dirs() 建。 */
/* 路径反查的实现，比 bsp_sdio_resolve_path() 多一个 dir_last：
 *      false : 最后一级必须是文件（升级文件检索用的就是这个语义）
 *      true  : 最后一级是文件或目录都行（清空/删除这类既能对文件也能对目录用的操作）
 * 分出这个参数是因为 sd_dir_lookup() 的 need_dir 只能二选一，而长目录名
 * "Updata_file" 这种，末级按文件找必然落空 */
static bool sd_resolve_ex(const char* path, char* out, uint32_t out_size, bool dir_last)
{
    char        cur[SD_PATH_MAX];
    char        comp[SD_PATH_MAX];
    char        sfn[16];
    const char* p;
    uint32_t    n;
    bool        last;

    if (path == NULL || out == NULL || out_size == 0) {
        return false;
    }
    if (!s_sd_mounted) {
        return false;
    }

    /* 先按原样试一次。本来就是合法短路径的话一次 f_stat 就够，不用扫目录 */
    if (bsp_sdio_file_exists(path)) {
        if ((uint32_t)strlen(path) + 1 > out_size) {
            return false;
        }
        if (out != path) {          /* 允许调用方拿同一块缓冲当入参和出参 */
            strcpy(out, path);
        }
        return true;
    }

    cur[0] = '\0';
    p = path;
    while (*p == '/' || *p == '\\') {
        p++;                                        /* 跳过开头的分隔符 */
    }

    while (*p != '\0') {
        /* 切出一级路径名 */
        n = 0;
        while (*p != '\0' && *p != '/' && *p != '\\') {
            if (n + 1 < sizeof(comp)) {
                comp[n++] = *p;
            }
            p++;
        }
        comp[n] = '\0';
        while (*p == '/' || *p == '\\') {
            p++;                                    /* 吃掉分隔符，顺便容忍 "a//b" */
        }
        last = (*p == '\0');

        if (n == 0) {
            continue;                               /* 连续分隔符切出来的空段 */
        }

        /* 中间几级必须是目录；最后一级默认按文件找，dir_last 时找不到再按目录找一次 */
        if (!sd_dir_lookup(cur, comp, NULL, !last, sfn)) {
            if (!last || !dir_last || !sd_dir_lookup(cur, comp, NULL, true, sfn)) {
                return false;
            }
        }
        if (!sd_path_join((cur[0] == '\0') ? NULL : cur, sfn, cur, sizeof(cur))) {
            return false;
        }
    }

    if (cur[0] == '\0') {
        return false;                               /* 路径是空的 */
    }
    if ((uint32_t)strlen(cur) + 1 > out_size) {
        return false;
    }
    strcpy(out, cur);
    return true;
}


bool bsp_sdio_resolve_path(const char* path, char* out, uint32_t out_size)
{
    return sd_resolve_ex(path, out, out_size, false);
}


/* 在根目录和各一级子目录里检索一个文件
 * want / ext 的含义同 sd_dir_lookup，两者至少给一个 */
static bool sd_search_tree(const char* want, const char* ext, char* out, uint32_t out_size)
{
    DIR     dp;
    FILINFO fno;
    char    sfn[16];

    SD_FILINFO_INIT(fno);
    /* 第一层：根目录 */
    if (sd_dir_lookup("", want, ext, false, sfn)) {
        return sd_path_join(NULL, sfn, out, out_size);
    }

    /* 第二层：根目录下的每个子目录。只下潜一层，文件多也不会扫太久，
     * 也不会递归吃栈（每层一个 DIR + FILINFO，约 60 字节） */
    if (FR_OK != f_opendir(&dp, "")) {
        return false;
    }
    for (;;) {
        fno.fname[0] = '\0';
        if (FR_OK != f_readdir(&dp, &fno) || fno.fname[0] == '\0') {
            break;
        }
        if (fno.fname[0] == '.' || !(fno.fattrib & AM_DIR)) {
            continue;
        }

        /* 这里嵌套开了第二个 DIR。FatFs 的 DIR 各自独立，每次读前都会重新
         * move_window，内层扫完不会打乱外层的遍历位置 */
        if (sd_dir_lookup(fno.fname, want, ext, false, sfn)) {
            return sd_path_join(fno.fname, sfn, out, out_size);
        }
    }
    return false;
}


/* 在卡上检索升级文件，详细说明见 bsp_sdio.h。
 * 找不到时用 bsp_sdio_list_dir(NULL) 看卡上到底有什么。 */
bool bsp_sdio_fw_find(const char* wanted, char* out, uint32_t out_size)
{
    const char* leaf;
    const char* q;

    if (out == NULL || out_size == 0) {
        return false;
    }
    if (!s_sd_mounted) {
        return false;
    }
    if (wanted == NULL) {
        wanted = SD_FW_FILE_NAME;
    }

    /* 1) 当成路径逐级反查，"Updata_file/V2.BIN" 走的是这一步 */
    if (bsp_sdio_resolve_path(wanted, out, out_size)) {
        return true;
    }

    /* 取路径最后一段当文件名，下面两步都只认这个名字，不管用户写的目录对不对 */
    leaf = wanted;
    for (q = wanted; *q != '\0'; q++) {
        if (*q == '/' || *q == '\\') {
            leaf = q + 1;
        }
    }
    if (*leaf == '\0') {
        return false;
    }

    /* 2)(3) 按文件名在根目录和各一级子目录里找，目录名写错或写漏都能救回来 */
    if (sd_search_tree(leaf, NULL, out, out_size)) {
        return true;
    }

#if SD_FW_SEARCH_ANY_BIN
    /* 4) 名字也对不上就找第一个 .BIN。卡上有多个 bin 时扫到哪个算哪个，
     * 所以调用方要把 bsp_sdio_fw_get_path() 打出来确认升的是哪一个 */
    if (sd_search_tree(NULL, SD_FW_FILE_EXT, out, out_size)) {
        return true;
    }
#endif

    return false;
}


/* 打印目录内容，详细说明见 bsp_sdio.h */
void bsp_sdio_list_dir(const char* dir)
{
    DIR     dp;
    FILINFO fno;
    char    sub[SD_PATH_MAX];
    bool    is_root;

    SD_FILINFO_INIT(fno);
    if (!s_sd_mounted) {
        printf("[SD ] no sd card\r\n");
        return;
    }
    if (dir == NULL) {
        dir = "";
    }
    is_root = (dir[0] == '\0');

    if (FR_OK != f_opendir(&dp, dir)) {
        printf("[DIR ] /%s : open failed\r\n", dir);
        return;
    }
    printf("[DIR ] /%s\r\n", dir);

    for (;;) {
        fno.fname[0] = '\0';
        if (FR_OK != f_readdir(&dp, &fno) || fno.fname[0] == '\0') {
            break;
        }
        if (fno.fname[0] == '.') {
            continue;
        }

        if (fno.fattrib & AM_DIR) {
            printf("  <DIR>  %s\r\n", fno.fname);
        } else {
            printf("  %6u %s\r\n", (unsigned int)fno.fsize, fno.fname);
        }
    }

    if (!is_root) {
        return;                                     /* 只下潜一层，避免深目录刷屏 */
    }

    /* 再把每个子目录的内容列一遍。要重新开一次 DIR，上面那个已经读到底了 */
    if (FR_OK != f_opendir(&dp, "")) {
        return;
    }
    for (;;) {
        fno.fname[0] = '\0';
        if (FR_OK != f_readdir(&dp, &fno) || fno.fname[0] == '\0') {
            break;
        }
        if (fno.fname[0] == '.' || !(fno.fattrib & AM_DIR)) {
            continue;
        }
        strncpy(sub, fno.fname, sizeof(sub) - 1);
        sub[sizeof(sub) - 1] = '\0';
        bsp_sdio_list_dir(sub);
    }
}


/* ==========================================================================
 *  清空：文件清内容、目录清内容
 * ========================================================================== */

/* 递归清空目录里的所有条目，目录本身保留。
 * path      : 目录路径，根目录传 ""。这块缓冲同时用来就地拼子路径，返回时会还原
 * path_size : 该缓冲的总大小
 * depth     : 当前层数，最外层传 0
 * 返回 true = 这个目录已经空了；有任何一条没删掉都返回 false（其余的照删不误，
 * 不会因为一条失败就半途而废）
 *
 * 【为什么可以边遍历边删】
 * f_readdir 每返回一条就已经把 index 往后挪了一格（ff.c 里 dir_read 之后紧跟
 * dir_next），所以删掉刚读出来的这条不影响下一次读；被删的条目在卡上只是首字节
 * 写成 0xE5，后面 dir_read 会自动跳过。递归进子目录期间，共享的扇区窗口 fs->win
 * 会被换掉，但 dir_read 每轮都先 move_window(dj->sect) 按扇区号取回来，父层的
 * 遍历不会错位。
 *
 * 【栈开销】每层 DIR + FILINFO + 几个局部变量约 70 字节，路径缓冲是共用的（子路径
 * 直接接在 path 后面，删完再把 '\0' 写回去），所以层数深也不会一层一个 64 字节。
 * 仍然用 SD_CLEAR_MAX_DEPTH 兜底，防止卡上目录结构异常时把栈递归穿了 */
static bool sd_clear_dir(char* path, uint32_t path_size, uint32_t depth)
{
    DIR      dp;
    FILINFO  fno;
    FRESULT  res;
    uint32_t base_len;
    bool     ok = true;

    SD_FILINFO_INIT(fno);
    if (depth > SD_CLEAR_MAX_DEPTH) {
        printf("[SD ] clear: /%s too deep\r\n", path);
        return false;
    }

    if (FR_OK != f_opendir(&dp, path)) {
        printf("[SD ] clear: opendir /%s failed\r\n", path);
        return false;
    }
    base_len = (uint32_t)strlen(path);

    for (;;) {
        fno.fname[0] = '\0';
        res = f_readdir(&dp, &fno);
        if (res != FR_OK) {
            printf("[SD ] clear: readdir /%s failed: %d\r\n", path, (int)res);
            ok = false;                             /* 读卡出错，不能算"已经空了" */
            break;
        }
        if (fno.fname[0] == '\0') {
            break;                                  /* 空名字表示读完了 */
        }
        if (fno.fname[0] == '.') {
            continue;                               /* "." / ".." 跳过 */
        }

        /* 把子路径接在 path 后面，根目录时 dir 传 NULL 就只剩名字 */
        if (!sd_path_join((base_len == 0) ? NULL : path, fno.fname, path, path_size)) {
            printf("[SD ] clear: path too long: /%s/%s\r\n", path, fno.fname);
            ok = false;
            continue;                               /* path 没被改动，接着扫下一条 */
        }

        /* 目录要先掏空才删得掉：f_unlink 对非空目录返回 FR_DENIED(7) */
        if (fno.fattrib & AM_DIR) {
            if (!sd_clear_dir(path, path_size, depth + 1)) {
                ok = false;
            }
        }

        res = f_unlink(path);
        if (res != FR_OK) {
            /* 常见：7=FR_DENIED(非空目录/只读属性) 8=FR_EXIST 9=FR_INVALID_OBJECT */
            printf("[SD ] clear: unlink /%s failed: %d\r\n", path, (int)res);
            ok = false;
        }

        path[base_len] = '\0';                      /* 还原成本层目录路径 */
    }

    path[base_len] = '\0';
    return ok;
}


/* 清空文件内容 / 清空目录内容，详细说明见 bsp_sdio.h */
bool bsp_sdio_clear(const char* path)
{
    char     real[SD_PATH_MAX];
    FILINFO  fno;
    FIL      file;
    FRESULT  res;

    SD_FILINFO_INIT(fno);
    if (!s_sd_mounted) {
        printf("[SD ] no sd card\r\n");
        return false;
    }
    if (path == NULL) {
        return false;                               /* 不接受 NULL：见头文件里根目录那段 */
    }

    /* 根目录单独放行：_FS_RPATH=0 时 f_stat("") 直接返回 FR_INVALID_NAME(6)，
     * 反查那一套用不上，但 f_opendir("") 是好使的 */
    if (path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        real[0] = '\0';
        return sd_clear_dir(real, sizeof(real), 0);
    }

    /* 允许传长目录名/长文件名，反查成能给 FatFs 用的 8.3 短路径 */
    if (!sd_resolve_ex(path, real, sizeof(real), true)) {
        printf("[SD ] clear: %s not found\r\n", path);
        return false;
    }
    if (FR_OK != f_stat(real, &fno)) {
        printf("[SD ] clear: stat %s failed\r\n", real);
        return false;
    }

    if (fno.fattrib & AM_DIR) {
        return sd_clear_dir(real, sizeof(real), 0);
    }

    /* 文件：截断成 0 字节，文件本身留着（时间、属性也留着）。
     * 用 FA_OPEN_EXISTING 而不是 FA_CREATE_ALWAYS，是为了"文件不存在"能报错，
     * 不然清一个打错字的文件名会悄悄建出一个空文件来 */
    res = f_open(&file, real, FA_OPEN_EXISTING | FA_WRITE);
    if (res != FR_OK) {
        printf("f_open(%s) clr failed: %d\r\n", real, (int)res);
        return false;
    }
    res = f_truncate(&file);        /* 刚 open 时读写指针在 0，从 0 截断就是清空 */
    f_close(&file);                 /* 必须关闭，目录项里的大小才会落盘 */

    if (res != FR_OK) {
        printf("f_truncate(%s) failed: %d\r\n", real, (int)res);
        return false;
    }
    return true;
}


/* ==========================================================================
 *  容量查询 / 目录列举 / 重命名 / 建目录 / 按行读
 *
 *  这几个的底层 FatFs 都已经编译进来了（ffconf.h 里 _FS_MINIMIZE=0、
 *  _FS_READONLY=0），本段只是套一层和上面同风格的壳：路径能带长目录名、
 *  失败打 FRESULT 码、每个函数 open 完立刻 close。
 * ========================================================================== */

/* bsp_sdio_get_space() 里换算 KB 用的是 ">>1"，也就是写死了 512 字节扇区。
 * 改了 ffconf.h 的 _MAX_SS，这里会编译报错（数组大小为负），
 * 比运行时把容量算错好查 —— 和 modbus_data_map.c 里 MB_CHK 是同一个套路 */
typedef char sd_ss_must_be_512[(_MAX_SS == 512) ? 1 : -1];

/* 查询卡容量和剩余空间，单位 KB。两个出参都可以传 NULL 表示不关心。
 *
 *      uint32_t total, freek;
 *      if (bsp_sdio_get_space(&total, &freek)) {
 *          printf("SD %luMB free / %luMB total\r\n", freek/1024, total/1024);
 *      }
 *
 * 用 KB 不用字节：32GB 卡的字节数是 3.4e10，uint32_t 装不下，KB 只有 3.3e7。
 *
 * 【耗时】FAT32 的空闲簇数平时缓存在 FATFS.free_clust 里，命中就是纯计算；
 * 缓存没建立时（刚挂载、上次是异常掉电）f_getfree 会把整张 FAT 表扫一遍，
 * 32GB 卡是几百毫秒的阻塞。所以别放进主循环轮询，查一次存起来用。 */
bool bsp_sdio_get_space(uint32_t* total_kb, uint32_t* free_kb)
{
    FATFS*   pfs = NULL;
    DWORD    free_clust = 0;
    DWORD    total_sect;
    DWORD    free_sect;

    if (total_kb != NULL) {
        *total_kb = 0;
    }
    if (free_kb != NULL) {
        *free_kb = 0;
    }
    if (!s_sd_mounted) {
        return false;
    }

    if (FR_OK != f_getfree("0:", &free_clust, &pfs) || pfs == NULL) {
        return false;
    }

    /* n_fatent 是 FAT 表项数 = 簇数 + 2（0号和1号项是保留的，不对应数据簇） */
    total_sect = (pfs->n_fatent - 2) * (DWORD)pfs->csize;
    free_sect  = free_clust * (DWORD)pfs->csize;

    if (total_kb != NULL) {
        *total_kb = (uint32_t)(total_sect >> 1);    /* 2 个 512B 扇区 = 1KB */
    }
    if (free_kb != NULL) {
        *free_kb = (uint32_t)(free_sect >> 1);
    }
    return true;
}


/* 把目录内容填进数组，给上位机/Modbus 用（bsp_sdio_list_dir 只往调试串口打，
 * 上位机拿不到）。详细说明见 bsp_sdio.h */
int bsp_sdio_list_to_buf(const char* dir, sd_entry_t* out, uint32_t max_cnt)
{
    char     real[SD_PATH_MAX];
    DIR      dp;
    FILINFO  fno;
    FRESULT  res;
    uint32_t total = 0;

    SD_FILINFO_INIT(fno);
    if (!s_sd_mounted) {
        return -1;
    }
    if (out == NULL && max_cnt > 0) {
        return -1;
    }

    /* 根目录：f_opendir("") 是好使的，反查那一套用不上 */
    if (dir == NULL || dir[0] == '\0' || (dir[0] == '/' && dir[1] == '\0')) {
        real[0] = '\0';
    } else if (!sd_resolve_ex(dir, real, sizeof(real), true)) {
        printf("[SD ] list: %s not found\r\n", dir);
        return -1;
    }

    if (FR_OK != f_opendir(&dp, real)) {
        printf("[SD ] list: opendir /%s failed\r\n", real);
        return -1;
    }

    for (;;) {
        fno.fname[0] = '\0';
        res = f_readdir(&dp, &fno);
        if (res != FR_OK) {
            printf("[SD ] list: readdir /%s failed: %d\r\n", real, (int)res);
            return -1;
        }
        if (fno.fname[0] == '\0') {
            break;                                  /* 空名字表示读完了 */
        }
        if (fno.fname[0] == '.') {
            continue;                               /* "." / ".." 跳过 */
        }

        /* 超出 max_cnt 的不再往里写，但继续数，让调用方知道被截断了 */
        if (total < max_cnt) {
            strncpy(out[total].name, fno.fname, sizeof(out[total].name) - 1);
            out[total].name[sizeof(out[total].name) - 1] = '\0';
            out[total].size   = (uint32_t)fno.fsize;
            out[total].date   = fno.fdate;
            out[total].time   = fno.ftime;
            out[total].is_dir = ((fno.fattrib & AM_DIR) != 0);
        }
        total++;
    }

    return (int)total;
}


/* 建目录，多级路径会一级一级建出来。详细说明见 bsp_sdio.h */
bool bsp_sdio_mkdir(const char* path)
{
    FRESULT res;
    uint32_t len;

    if (!s_sd_mounted || path == NULL || path[0] == '\0') {
        return false;
    }
    len = (uint32_t)strlen(path);
    if (path[len - 1] == '/' || path[len - 1] == '\\') {
        /* 全工程只有一处字符串字面量带非 ASCII（bootloader.c 里那条坏掉的），
         * 这里也保持英文：中文在串口终端上是乱码 */
        printf("[SD ] mkdir: %s has a trailing slash\r\n", path);
        return false;                               /* 带斜杠 f_mkdir 会当成空名字 */
    }

    sd_make_parent_dirs(path);                      /* "A/B/C" 先把 A、A/B 建出来 */

    res = f_mkdir(path);
    if (res == FR_OK || res == FR_EXIST) {
        return true;                                /* 已经存在也算成功 */
    }
    /* 常见：6=FR_INVALID_NAME(超出 8.3) 5=FR_NO_PATH 3=FR_NOT_READY */
    printf("f_mkdir(%s) failed: %d\r\n", path, (int)res);
    return false;
}


/* 重命名 / 移动。详细说明见 bsp_sdio.h */
bool bsp_sdio_rename(const char* old_path, const char* new_path)
{
    char    real[SD_PATH_MAX];
    FRESULT res;

    if (!s_sd_mounted || old_path == NULL || new_path == NULL) {
        return false;
    }
    if (new_path[0] == '\0') {
        return false;
    }

    /* _USE_LFN=1 之后长名 f_stat 直接就过了，sd_resolve_ex 原样返回不扫目录；
     * 只有中文名之类 LFN 覆盖不到的才会真的走一遍 8.3 反查兜底 */
    if (!sd_resolve_ex(old_path, real, sizeof(real), true)) {
        printf("[SD ] rename: %s not found\r\n", old_path);
        return false;
    }

    sd_make_parent_dirs(new_path);                  /* f_rename 不建目录，移进新目录前先建 */

    res = f_rename(real, new_path);
    if (res != FR_OK) {
        /* 常见：8=FR_EXIST(新名字已被占用，f_rename 不覆盖)
                 6=FR_INVALID_NAME(新名字超出 8.3) 5=FR_NO_PATH */
        printf("f_rename(%s -> %s) failed: %d\r\n", real, new_path, (int)res);
        return false;
    }
    return true;
}


/* 从当前读写指针处读一行，指针停到下一行开头。行尾的 "\r\n" / "\n" 不算内容，
 * 读出来的是能直接当字符串用的一行。
 * 返回行长度（空行返回 0），SD_LINE_EOF 表示已到文件尾，SD_LINE_ERR 表示读失败。
 *
 * 一次 f_read 抓一整段再回退指针，不是一个字节一个字节读的 —— 逐字节 f_read
 * 虽然有扇区缓存不会次次访问卡，但每次调用的簿记开销累起来慢一个量级。
 *
 * 行比 buf_size-1 还长时：把前面这截当一行返回，剩下的部分下次调用继续返回，
 * 也就是一条长行会被切成两行，不会丢数据但也不会报错。 */
static int sd_getline(FIL* fp, char* buf, uint32_t buf_size)
{
    DWORD    start;
    UINT     br = 0;
    uint32_t i;

    if (buf_size < 2) {
        return SD_LINE_ERR;
    }

    start = f_tell(fp);
    if (FR_OK != f_read(fp, buf, (UINT)(buf_size - 1), &br)) {
        return SD_LINE_ERR;
    }
    if (br == 0) {
        return SD_LINE_EOF;                         /* 一个字节都没读到 = 文件尾 */
    }

    for (i = 0; i < br; i++) {
        if (buf[i] == '\n') {
            break;
        }
    }

    /* 指针挪到 '\n' 的下一个字节；这一段里没有 '\n'（最后一行没换行符，
     * 或者行太长被截断）就停在读到的末尾 */
    if (FR_OK != f_lseek(fp, start + i + ((i < br) ? 1U : 0U))) {
        return SD_LINE_ERR;
    }

    if (i > 0 && buf[i - 1] == '\r') {
        i--;                                        /* CRLF 的 '\r' 也去掉 */
    }
    buf[i] = '\0';
    return (int)i;
}


/* 读第 line_no 行（从 0 开始数）。详细说明见 bsp_sdio.h */
int bsp_sdio_file_read_line(const char* filename, uint32_t line_no,
                            char* buf, uint32_t buf_size)
{
    char     real[SD_PATH_MAX];
    FIL      file;
    int      n = SD_LINE_ERR;
    uint32_t k;

    if (filename == NULL || buf == NULL || buf_size < 2) {
        return SD_LINE_ERR;
    }
    buf[0] = '\0';
    if (!s_sd_mounted) {
        return SD_LINE_ERR;
    }
    if (!sd_resolve_ex(filename, real, sizeof(real), false)) {
        return SD_LINE_ERR;
    }
    if (FR_OK != f_open(&file, real, FA_READ | FA_OPEN_EXISTING)) {
        return SD_LINE_ERR;
    }

    /* 前面的行读出来直接丢掉，只为了把指针推过去 */
    for (k = 0; ; k++) {
        n = sd_getline(&file, buf, buf_size);
        if (n < 0 || k == line_no) {
            break;                                  /* 读到目标行，或者行数不够了 */
        }
    }

    f_close(&file);
    if (n < 0) {
        buf[0] = '\0';
    }
    return n;
}


/* 按偏移逐行读，offset 是入参也是出参。详细说明见 bsp_sdio.h */
int bsp_sdio_file_read_line_at(const char* filename, uint32_t* offset,
                               char* buf, uint32_t buf_size)
{
    char real[SD_PATH_MAX];
    FIL  file;
    int  n;

    if (filename == NULL || offset == NULL || buf == NULL || buf_size < 2) {
        return SD_LINE_ERR;
    }
    buf[0] = '\0';
    if (!s_sd_mounted) {
        return SD_LINE_ERR;
    }
    if (!sd_resolve_ex(filename, real, sizeof(real), false)) {
        return SD_LINE_ERR;
    }
    if (FR_OK != f_open(&file, real, FA_READ | FA_OPEN_EXISTING)) {
        return SD_LINE_ERR;
    }

    /* f_lseek 越过文件末尾不报错，只把指针停在末尾，下面 sd_getline 会读到
     * 0 字节返回 SD_LINE_EOF，正好是"读完了"，不用单独判 */
    if (*offset > 0 && FR_OK != f_lseek(&file, (DWORD)*offset)) {
        f_close(&file);
        return SD_LINE_ERR;
    }

    n = sd_getline(&file, buf, buf_size);
    if (n >= 0) {
        *offset = (uint32_t)f_tell(&file);          /* 下一行的起点 */
    } else {
        buf[0] = '\0';
    }

    f_close(&file);
    return n;
}


/* ==========================================================================
 *  TF 卡固件升级文件，本段只负责把镜像从卡上读出来。
 *
 *  完整升级链路四步，这里只做第一步：
 *      1) 从卡上读 APP.BIN                        <- 本段代码
 *      2) 搬进内部 flash 暂存区 DOWNLOAD_ADDR(0x08051000)
 *      3) 写 BootParam 的 appSize/appCRC32/updateFlag/updateStatus
 *      4) mcu_restart()，由兄弟 Bootloader 工程把暂存区搬到 App 区
 *  第 2~4 步是业务逻辑，放 Function 层。
 *
 *  CRC32 也不在这里算：镜像搬进内部 flash 之后可以直接内存映射访问，用
 *  crc32_calc((uint8_t*)DOWNLOAD_ADDR, size)（定义在 bootloader.c）算出
 *  BootParam.appCRC32，比在这里流式算一遍省一次整盘读取。
 * ========================================================================== */

static FIL      s_fw_file;              // 流式读取用的文件句柄，只在本段内部使用
static bool     s_fw_opened  = false;   // 句柄是否有效，防止没open就read
static uint32_t s_fw_offset  = 0;       // 镜像在文件里的起始偏移：0=裸bin，8=带头部
static uint32_t s_fw_version = 0;       // 带头部时解析出的版本号
static char     s_fw_path[SD_PATH_MAX] = SD_FW_FILE_NAME;   // 实际用的短路径，见 sd_fw_locate()


/* 把调用方给的文件名换成能 f_open 的短路径，结果存进 s_fw_path 并返回。
 * 传 "Updata_file/V2.BIN" 也能读到就靠这一步：长目录名、目录写错、只想随便找个 bin，
 * 都由 bsp_sdio_fw_find() 兜。
 * 找不到时把原名原样存回去，让上层照常报 SD_FW_ERR_NO_FILE，错误码不变。 */
static const char* sd_fw_locate(const char* filename)
{
    char req[SD_PATH_MAX];

    if (filename == NULL) {
        filename = SD_FW_FILE_NAME;
    }

    /* 先把请求名抄到局部缓冲：fw_test 解析完会拿着 s_fw_path 再调 fw_probe，那时
     * filename 和 s_fw_path 是同一块内存，直接原地拷会踩到自己 */
    strncpy(req, filename, sizeof(req) - 1);
    req[sizeof(req) - 1] = '\0';

    if (bsp_sdio_fw_find(req, s_fw_path, sizeof(s_fw_path))) {
        return s_fw_path;
    }

    strcpy(s_fw_path, req);     /* 找不到就原样存回去，错误码交给上层照常报 */
    return s_fw_path;
}


// 从小端字节流里取一个32位字。bin文件和MCU都是小端，
// 但手工拼字节不依赖对齐，buf来自任意位置也不会触发非对齐访问异常
static uint32_t sd_fw_le32(const uint8_t* p)
{
    return (uint32_t)p[0]        | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}


/* 校验镜像头 8 字节的向量表，判断这个 bin 是不是本机 App。
 * vec      指向向量表，裸 bin 是文件头，带头部的包在偏移 8 处
 * img_size 镜像本体字节数，不含头部
 *
 * Cortex-M 镜像开头固定是 [0-3] 初始 MSP 栈顶、[4-7] Reset_Handler 入口。
 * 拿错 bin（Bootloader 的 bin、按老地址 0x0800D000 链接的旧 App）这两个字段一定
 * 对不上，能在擦 flash 之前拦下来。暂存区一旦擦了就没有回头路。 */
static sd_fw_result_t sd_fw_check_vector_table(const uint8_t* vec, uint32_t img_size)
{
    uint32_t msp           = sd_fw_le32(vec);
    uint32_t reset_handler = sd_fw_le32(vec + 4);

    // 栈顶必须落在RAM里。允许等于RAM末地址，因为栈是从高地址往下长的，
    // 栈顶指到RAM最后一个字节的下一个位置是正常写法
    if (msp < SD_FW_RAM_BASE || msp > (SD_FW_RAM_BASE + SD_FW_RAM_SIZE)) {
        return SD_FW_ERR_NOT_APP;
    }

    // 入口地址的bit0必须是1 —— Cortex-M只有Thumb指令集，
    // 向量表里的地址一律带Thumb位
    if ((reset_handler & 0x01U) == 0U) {
        return SD_FW_ERR_NOT_APP;
    }

    // 入口必须落在这个镜像自己的范围内（链接基址 ~ 基址+镜像大小）
    if (reset_handler < SD_FW_APP_BASE || reset_handler >= (SD_FW_APP_BASE + img_size)) {
        return SD_FW_ERR_NOT_APP;
    }

    return SD_FW_OK;
}


/* 卡是否就绪。bsp_sdio_Init() 的结果存在这里，各处不用自己传 sd_ready */
bool bsp_sdio_is_ready(void)
{
    return s_sd_mounted;
}


/* 探测升级文件：只看不读，确认卡上有一个能用的镜像。收到升级命令后先探测再动 flash。
 * filename 传 NULL 则用默认的 SD_FW_FILE_NAME("APP.BIN")
 * out_size 成功时填镜像本体字节数（不含 8 字节头部），可传 NULL
 * 返回 SD_FW_OK 表示这个文件可以拿去升级
 *
 * 按前 4 字节自动识别两种格式：裸 bin，或 8 字节头部(magic+版本) + 裸 bin。
 * 识别结果用 bsp_sdio_fw_get_offset() / get_version() 取回。
 *
 * 只校验长度和向量表，防的是拿错文件不是文件损坏，内容完整性靠 Bootloader 那边的
 * CRC32 比对兜底。 */
sd_fw_result_t bsp_sdio_fw_probe(const char* filename, uint32_t* out_size)
{
    uint8_t  head[16];      // 最多要看：8字节头部 + 8字节向量表
    uint32_t file_size;
    uint32_t img_size;
    uint32_t want;
    int      got;

    s_fw_offset  = 0;
    s_fw_version = 0;
    if (out_size != NULL) {
        *out_size = 0;
    }
    if (!s_sd_mounted) {
        return SD_FW_ERR_NO_CARD;
    }

    // 长目录名/长文件名在这里换成真正能打开的短路径，之后一律用 filename
    filename = sd_fw_locate(filename);

    if (!bsp_sdio_file_exists(filename)) {
        return SD_FW_ERR_NO_FILE;
    }

    file_size = bsp_sdio_file_get_size(filename);
    if (file_size == 0) {
        return SD_FW_ERR_EMPTY;
    }
    // 比App区还大就没必要往下走了，搬过去也会越界
    if (file_size > (SD_FW_APP_MAX_SIZE + SD_FW_HEADER_SIZE)) {
        return SD_FW_ERR_TOO_BIG;
    }

    // 文件不足16字节时按实际长度读，f_read读不满不算错，所以要自己核对返回值
    want = (file_size < sizeof(head)) ? file_size : (uint32_t)sizeof(head);
    got  = bsp_sdio_file_read(filename, head, want, 0);
    if (got < 0) {
        return SD_FW_ERR_READ;
    }
    // 连一张向量表都装不下的，肯定不是镜像
    if ((uint32_t)got < 8U) {
        return SD_FW_ERR_NOT_APP;
    }

    if (sd_fw_le32(head) == SD_FW_MAGIC_WORD) {
        // 带头部的包：magic(4) + 版本号(4) + 镜像本体
        if ((uint32_t)got < 16U) {
            return SD_FW_ERR_NOT_APP;   // 有头部却连向量表都没有
        }
        s_fw_offset  = SD_FW_HEADER_SIZE;
        s_fw_version = sd_fw_le32(head + 4);
    } else {
        // 裸bin：文件开头就是向量表
        s_fw_offset  = 0;
        s_fw_version = 0;
    }

    img_size = file_size - s_fw_offset;
    if (img_size == 0) {
        return SD_FW_ERR_EMPTY;         // 只有头部没有内容
    }
    if (img_size > SD_FW_APP_MAX_SIZE) {
        return SD_FW_ERR_TOO_BIG;
    }

    if (sd_fw_check_vector_table(head + s_fw_offset, img_size) != SD_FW_OK) {
        return SD_FW_ERR_NOT_APP;
    }

    if (out_size != NULL) {
        *out_size = img_size;
    }
    return SD_FW_OK;
}


/* 打开升级文件准备流式读取，内部先跑一遍 bsp_sdio_fw_probe 的全部检查。
 * filename 传 NULL 则用默认的 SD_FW_FILE_NAME("APP.BIN")
 * out_size 成功时填镜像本体字节数（不含头部），可传 NULL
 * 返回 SD_FW_OK 表示可以开始 bsp_sdio_fw_read()
 *
 * 文件带 8 字节头部时这里自动跳过，read 出来的一律是纯镜像数据。
 *
 * 用法是循环 bsp_sdio_fw_read() 边读边往内部 flash 暂存区搬，缓冲取 4KB 和 flash
 * 页对齐，读一页烧一页；不管成功失败都要 bsp_sdio_fw_close()，否则句柄漏了下次
 * 打不开；累计读到的字节数和 out_size 不符时绝对不能置升级标志。
 *
 * 整个读取过程是阻塞的，128KB 镜像约几百 ms，期间主循环停摆、RS485 收不了包、
 * Modbus 主站会超时。所以升级动作不能在收到帧的当场做，要挂到
 * Modbus_ExecPendingActions() 里等应答发完再执行，和 MB_CMD_REBOOT /
 * MB_CMD_OTA_REQUEST 走同一条路。 */
sd_fw_result_t bsp_sdio_fw_open(const char* filename, uint32_t* out_size)
{
    sd_fw_result_t res;
    uint32_t       img_size;

    // 上次异常退出没关文件的话在这里补上，否则 FIL 句柄一直占着
    bsp_sdio_fw_close();

    // probe 内部做过路径解析，结果在 s_fw_path 里。必须用解析后的路径 open，
    // 不能再用调用方传进来的原始 filename
    res = bsp_sdio_fw_probe(filename, &img_size);
    if (res != SD_FW_OK) {
        return res;
    }

    if (FR_OK != f_open(&s_fw_file, s_fw_path, FA_READ | FA_OPEN_EXISTING)) {
        return SD_FW_ERR_READ;
    }

    // 带头部的包要把读指针挪过头部，让调用方只看到纯镜像
    if (s_fw_offset > 0) {
        if (FR_OK != f_lseek(&s_fw_file, (DWORD)s_fw_offset)
            || f_tell(&s_fw_file) != s_fw_offset) {
            f_close(&s_fw_file);
            return SD_FW_ERR_READ;
        }
    }

    s_fw_opened = true;
    if (out_size != NULL) {
        *out_size = img_size;
    }
    return SD_FW_OK;
}


/* 从升级文件读下一块，文件指针自动前移，不用自己算 offset。
 * 返回实际读到的字节数，0 = 已到文件末尾，-1 = 出错或没先 open。
 * 每次读多少由调用方定，建议 4KB（内部 flash 一页），读一页烧一页正好对齐。 */
int bsp_sdio_fw_read(void* buf, uint32_t size)
{
    UINT read_len;

    if (!s_fw_opened || buf == NULL || size == 0) {
        return -1;
    }

    if (FR_OK != f_read(&s_fw_file, buf, (UINT)size, &read_len)) {
        return -1;
    }
    return (int)read_len;
}


/* 关闭升级文件，读完或读失败都必须调一次，重复调用安全。
 * 只读文件不 close 不会丢数据（没有写缓存要落盘），但 s_fw_file 会一直占着，
 * 下一次 bsp_sdio_fw_open() 就打不开了。 */
void bsp_sdio_fw_close(void)
{
    if (s_fw_opened) {
        f_close(&s_fw_file);
        s_fw_opened = false;
    }
}


/* 最近一次 probe/open 识别出的镜像起始偏移：0 = 裸 bin，8 = 带头部的包 */
uint32_t bsp_sdio_fw_get_offset(void)
{
    return s_fw_offset;
}


/* 最近一次 probe/open 从头部取到的版本号，裸 bin 时为 0。
 * 用于填 BootParam.appVersion，或回给主站看升的是哪一版 */
uint32_t bsp_sdio_fw_get_version(void)
{
    return s_fw_version;
}


/* 最近一次 probe/open 实际打开的短路径。传长路径（"Updata_file/V2.BIN"）时这里是
 * 解析后的 "UPDATA~1/V2.BIN"。走了 .BIN 兜底搜索时，这是唯一能看出选中了哪个文件
 * 的地方，升级前要打出来核对。 */
const char* bsp_sdio_fw_get_path(void)
{
    return s_fw_path;
}


/* 错误码转字符串，给 printf 或回主站的报文用。
 * 串口输出一律用英文，中文在串口助手里会乱码 */
const char* bsp_sdio_fw_strerr(sd_fw_result_t res)
{
    switch (res) {
        case SD_FW_OK:            return "ok";
        case SD_FW_ERR_NO_CARD:   return "no sd card";
        case SD_FW_ERR_NO_FILE:   return "file not found";
        case SD_FW_ERR_EMPTY:     return "file is empty";
        case SD_FW_ERR_TOO_BIG:   return "file too big for app region";
        case SD_FW_ERR_NOT_APP:   return "not a valid app image";
        case SD_FW_ERR_READ:      return "sd read error";
        default:                  return "unknown error";
    }
}


/* 自检打印：把卡上的升级文件检查一遍并完整读出来，只读不写，不碰 flash。
 * filename 传 NULL 用默认的 "APP.BIN"。在 main.c 的 bsp_sdio_Init() 之后调。
 *
 * 向量表无条件打印，包括校验不通过的情况：校验失败时最需要看的就是这两个数，
 * 只报一句 "not a valid app image" 定位不了原因。重点看 link base 那一行，
 * 不是 0x08011000 就说明这个 bin 按别的地址链接（老布局的 0x0800D000、
 * Bootloader 的 0x08000000），应该换 bin 而不是改校验去迁就它。 */
void bsp_sdio_fw_test(const char* filename)
{
    uint8_t        buf[512];
    uint8_t        head[16];
    uint32_t       file_size = 0;
    uint32_t       img_size  = 0;
    uint32_t       total     = 0;
    uint32_t       msp       = 0;
    uint32_t       entry     = 0;
    uint32_t       want;
    int            got;
    int            n;
    sd_fw_result_t res;

    if (filename == NULL) {
        filename = SD_FW_FILE_NAME;
    }

    if (!s_sd_mounted) {
        printf("[FW] %s\r\n", bsp_sdio_fw_strerr(SD_FW_ERR_NO_CARD));
        return;
    }

    // 长目录名/长文件名在这里解析成真正能打开的短路径，之后一律用 filename。
    // 传进来的原名先打出来，和下面的 "file" 行一对比就知道解析成了什么
    printf("[FW] want      : %s\r\n", filename);
    filename = sd_fw_locate(filename);

    if (!bsp_sdio_file_exists(filename)) {
        printf("[FW] %s : %s\r\n", filename, bsp_sdio_fw_strerr(SD_FW_ERR_NO_FILE));
        bsp_sdio_list_dir(NULL);        // 没找到就把卡上有什么列出来，省得再猜路径
        return;
    }

    file_size = bsp_sdio_file_get_size(filename);
    printf("[FW] file      : %s\r\n", filename);
    printf("[FW] size      : %u B (limit %u B)\r\n",
           (unsigned int)file_size, (unsigned int)SD_FW_APP_MAX_SIZE);

    // 先无条件把头部和向量表打出来，再报校验结论，顺序反过来就没法定位了
    want = (file_size < sizeof(head)) ? file_size : (uint32_t)sizeof(head);
    got  = bsp_sdio_file_read(filename, head, want, 0);
    if (got >= 8) {
        uint32_t off = 0;

        if (sd_fw_le32(head) == SD_FW_MAGIC_WORD && got >= 16) {
            off = SD_FW_HEADER_SIZE;
            printf("[FW] header    : magic 0x%08X  version 0x%08X\r\n",
                   (unsigned int)SD_FW_MAGIC_WORD,
                   (unsigned int)sd_fw_le32(head + 4));
        } else {
            printf("[FW] header    : none (raw bin), first word 0x%08X\r\n",
                   (unsigned int)sd_fw_le32(head));
        }

        img_size = file_size - off;
        msp      = sd_fw_le32(head + off);
        entry    = sd_fw_le32(head + off + 4);

        printf("[FW] msp       : 0x%08X  need 0x%08X-0x%08X  %s\r\n",
               (unsigned int)msp,
               (unsigned int)SD_FW_RAM_BASE,
               (unsigned int)(SD_FW_RAM_BASE + SD_FW_RAM_SIZE),
               (msp >= SD_FW_RAM_BASE && msp <= (SD_FW_RAM_BASE + SD_FW_RAM_SIZE))
                   ? "pass" : "FAIL");

        printf("[FW] entry     : 0x%08X  need 0x%08X-0x%08X  %s\r\n",
               (unsigned int)entry,
               (unsigned int)SD_FW_APP_BASE,
               (unsigned int)(SD_FW_APP_BASE + img_size),
               (entry >= SD_FW_APP_BASE && entry < (SD_FW_APP_BASE + img_size))
                   ? "pass" : "FAIL");

        printf("[FW] thumb bit : %s\r\n", (entry & 0x01U) ? "pass" : "FAIL");

        // entry 落在哪个4KB边界上，直接反推出这个bin是按什么地址链接的
        printf("[FW] link base : 0x%08X (guess)\r\n",
               (unsigned int)(entry & 0xFFFFF000U));
    } else {
        printf("[FW] head read : failed (%d)\r\n", got);
    }

    res = bsp_sdio_fw_probe(filename, &img_size);
    printf("[FW] probe     : %s\r\n", bsp_sdio_fw_strerr(res));
    if (res != SD_FW_OK) {
        return;
    }

    res = bsp_sdio_fw_open(filename, &img_size);
    if (res != SD_FW_OK) {
        printf("[FW] open      : %s\r\n", bsp_sdio_fw_strerr(res));
        return;
    }

    n = bsp_sdio_fw_read(buf, sizeof(buf));
    while (n > 0) {
        total += (uint32_t)n;
        n = bsp_sdio_fw_read(buf, sizeof(buf));
    }
    bsp_sdio_fw_close();

    if (n < 0) {
        printf("[FW] read      : failed at %u B\r\n", (unsigned int)total);
    } else if (total != img_size) {
        printf("[FW] read      : short, got %u of %u B\r\n",
               (unsigned int)total, (unsigned int)img_size);
    } else {
        printf("[FW] read      : ok, %u B all read\r\n", (unsigned int)total);
    }
}


/*****************************  TF卡功能自检 begin  *****************************
 *  验证通过后可以把从这里到 "TF卡功能自检 end" 的整段删掉，删了不影响其它功能。
 *  （bsp_sdio.h 末尾 bsp_sdio_test 的声明也一并删掉）
 *
 *  【这一段测什么】
 *  bsp_sdio.h 里对外暴露的每个函数都跑一遍，每项打印 PASS / FAIL，最后给总数。
 *  目的是换卡、改 ffconf.h、改 SD_PATH_MAX 之后能一次性确认驱动整体还好使，
 *  而不是出问题了再一个函数一个函数试。
 *
 *  【安全性】
 *  默认【不破坏卡上已有数据】：所有读写都关在 SD_TEST_DIR 这一个目录里，跑完
 *  自己删干净。唯二的例外用开关关着，默认都是 0：
 *      SD_TEST_ENABLE_FORMAT   格式化整张卡
 *      SD_TEST_ENABLE_ROOTLIST 列根目录（不破坏，只是刷屏）
 *
 *  【耗时】
 *  几十毫秒到几百毫秒，取决于卡。全程阻塞主循环，所以只在 main() 初始化阶段调，
 *  别放进 while(1)。
 *
 *  【栈】
 *  主栈只有 2KB（Startup 里 Stack_Size = 0x800），而驱动内部还要用掉几百字节
 *  （sd_clear_dir 最深递归 4 层、每层 DIR+FILINFO 约 70 字节）。所以本段所有
 *  缓冲区都是 static，别改成局部数组。
 * ============================================================================ */

/* 危险开关：置 1 会把整张卡格式化掉，不可恢复。只在确实要验证 bsp_sdio_format()
 * 时临时改成 1，测完立刻改回 0 */
#define SD_TEST_ENABLE_FORMAT       0

/* 置 1 会把根目录连同一级子目录整个列出来。不破坏数据，但卡上文件多时刷屏很久 */
#define SD_TEST_ENABLE_ROOTLIST     0

/* 测试用的目录和文件，全部关在一个目录里，跑完删掉。
 * 注意路径总长受 SD_PATH_MAX(64) 限制 */
#define SD_TEST_DIR         "SDTEST"
#define SD_TEST_SUB         "SDTEST/SUB"
#define SD_TEST_TXT         "SDTEST/TEXT.TXT"
#define SD_TEST_BIN         "SDTEST/BINARY.BIN"
#define SD_TEST_CSV         "SDTEST/LINES.CSV"
#define SD_TEST_REN         "SDTEST/RENAMED.TXT"
#define SD_TEST_MOVED       "SDTEST/SUB/MOVED.TXT"
/* 长文件名，专门验证 ffconf.h 的 _USE_LFN=1 有没有生效。
 * _USE_LFN 改回 0 的话这一项会 FAIL，其它项照过 */
#define SD_TEST_LONGNAME    "SDTEST/long_file_name_test.txt"

static uint16_t s_test_pass = 0;
static uint16_t s_test_fail = 0;

/* 记一项结果。串口输出一律用英文：源文件编码和串口助手编码对不上时中文必炸，
 * 本工程 bootloader.c 里那句中文 printf 已经是乱码的前车之鉴 */
static void sd_t_check(bool ok, const char *name)
{
    if (ok) {
        s_test_pass++;
        printf("[T ] PASS  %s\r\n", name);
    } else {
        s_test_fail++;
        printf("[T ] FAIL  %s\r\n", name);
    }
}

/* 打印卡片硬件参数 + 文件系统参数。这一段不算测试项，纯粹是现场排障时
 * "卡到底认没认出来" 的第一手信息 */
static void sd_t_dump_card_info(void)
{
    static const char *const card_type_str[] = {
        "SDSC v1.1", "SDSC v2.0", "SDHC/SDXC", "SDIO",
        "SDIO combo", "MMC", "MMC HC", "MMC HS"
    };
    sd_card_info_struct card_info;
    uint32_t cap_kb;
    DWORD    free_clust;
    FATFS   *pfs;

    /* 卡片硬件参数：从 CID/CSD 寄存器读，跟有没有格式化无关 */
    if (SD_OK == sd_card_information_get(&card_info)) {
        printf("[SD] type      : %s\r\n",
            (card_info.card_type < 8) ? card_type_str[card_info.card_type] : "unknown");
        /* CID/CSD 里的字段大多是 uint8_t/uint16_t，显式转一下避免可变参数提升的告警 */
        printf("[SD] rca       : 0x%04X\r\n", (unsigned int)card_info.card_rca);
        printf("[SD] blocksize : %u B\r\n",   (unsigned int)card_info.card_blocksize);
        printf("[SD] mid       : 0x%02X\r\n", (unsigned int)card_info.card_cid.mid);
        printf("[SD] serial    : 0x%08X\r\n", (unsigned int)card_info.card_cid.psn);
        printf("[SD] date      : %u-%02u\r\n",
            (unsigned int)(2000U + (card_info.card_cid.mdt >> 4)),
            (unsigned int)(card_info.card_cid.mdt & 0x0F));
    } else {
        printf("[SD] card info : failed\r\n");
    }

    /* 用 sd_card_capacity_get()，它返回的单位是 KB。
     * 不要用 card_info.card_capacity —— 那个是字节数，>=4GB 的卡会把 uint32_t 撑爆 */
    cap_kb = sd_card_capacity_get();
    printf("[SD] capacity  : %u KB (%u MB)\r\n",
        (unsigned int)cap_kb, (unsigned int)(cap_kb / 1024U));

    /* 文件系统参数：这一步才真正去读 FAT 表，卡没格式化会返回错误。
     * f_getfree 在 FSInfo 里空闲簇数无效时会全盘扫描 FAT 表，大卡可能要几秒 */
    if (FR_OK == f_getfree("0:", &free_clust, &pfs)) {
        printf("[SD] fs        : FAT%s  total %u KB  free %u KB\r\n",
            (pfs->fs_type == FS_FAT32) ? "32" : ((pfs->fs_type == FS_FAT16) ? "16" : "12"),
            (unsigned int)((pfs->n_fatent - 2) * pfs->csize / 2),
            (unsigned int)(free_clust * pfs->csize / 2));
    } else {
        printf("[SD] fs        : no FAT volume (need format?)\r\n");
    }
}

/* 跑完把测试目录整个删掉：先 clear 再 delete —— f_unlink 只能删空目录，
 * 顺序反了就删不掉。这本身也是对 clear + delete 组合用法的一次验证 */
static void sd_t_cleanup(void)
{
    bsp_sdio_clear(SD_TEST_DIR);
    sd_t_check(bsp_sdio_file_delete(SD_TEST_DIR), "cleanup: rmdir test dir");
    sd_t_check(!bsp_sdio_file_exists(SD_TEST_DIR), "cleanup: test dir gone");
}


void bsp_sdio_test(bool sd_ready)
{
    static char     buf[128];
    static char     line[64];
    static uint8_t  bin_w[16];
    static uint8_t  bin_r[16];
    static sd_entry_t entries[8];
    uint32_t total_kb = 0, free_kb = 0;
    uint32_t off;
    int      n, i, cnt;
    bool     ok;

    s_test_pass = 0;
    s_test_fail = 0;

    printf("\r\n========== SD self-test ==========\r\n");

    /* bsp_sdio_is_ready() 是"卡能不能用"的唯一真相，和 main 传进来的 sd_ready
     * 应该永远一致。不一致说明中间有人动过挂载状态 */
    if (!sd_ready || !bsp_sdio_is_ready()) {
        printf("[T ] SKIP  card not ready (sd_ready=%d is_ready=%d)\r\n",
               (int)sd_ready, (int)bsp_sdio_is_ready());
        printf("========== SD self-test end ==========\r\n\r\n");
        return;
    }
    sd_t_check(sd_ready == bsp_sdio_is_ready(), "is_ready matches init result");

    sd_t_dump_card_info();

    /* ---------------- 【1】容量查询 ---------------- */
    ok = bsp_sdio_get_space(&total_kb, &free_kb);
    sd_t_check(ok && total_kb > 0, "get_space");
    if (ok) {
        printf("[T ]       total=%u KB free=%u KB\r\n",
               (unsigned int)total_kb, (unsigned int)free_kb);
        /* 只传一个指针、另一个给 NULL 的用法也要能过 */
        total_kb = 0;
        sd_t_check(bsp_sdio_get_space(&total_kb, NULL) && total_kb > 0,
                   "get_space with NULL free_kb");
    }

    /* ---------------- 【2】建目录 ---------------- */
    /* 先把上次没删干净的残留清掉，否则 size 断言会因为旧内容而误判 */
    bsp_sdio_clear(SD_TEST_DIR);

    sd_t_check(bsp_sdio_mkdir(SD_TEST_DIR), "mkdir");
    sd_t_check(bsp_sdio_file_exists(SD_TEST_DIR), "mkdir: dir exists");
    /* 多级路径：SUB 的父目录已经在了，这里验证的是不会因为 FR_EXIST 就返回 false */
    sd_t_check(bsp_sdio_mkdir(SD_TEST_SUB), "mkdir nested");
    sd_t_check(bsp_sdio_mkdir(SD_TEST_DIR), "mkdir existing returns true");

    /* ---------------- 【3】文本写 / 追加 / 大小 / 读回 ---------------- */
    /* 用定长的已知内容，这样字节数可以精确断言，能抓出"追加写成了覆盖写"这类问题 */
    sd_t_check(bsp_sdio_file_write_text(SD_TEST_TXT, "AAAA\r\n"), "write_text");
    sd_t_check(bsp_sdio_file_exists(SD_TEST_TXT), "file_exists after write");
    sd_t_check(bsp_sdio_file_get_size(SD_TEST_TXT) == 6, "get_size == 6");

    sd_t_check(bsp_sdio_file_append_text(SD_TEST_TXT, "BBBB\r\n"), "append_text");
    sd_t_check(bsp_sdio_file_get_size(SD_TEST_TXT) == 12, "get_size == 12 after append");

    n = bsp_sdio_file_read_text(SD_TEST_TXT, buf, sizeof(buf));
    sd_t_check(n == 12, "read_text length");
    sd_t_check(n == 12 && memcmp(buf, "AAAA\r\nBBBB\r\n", 12) == 0, "read_text content");

    /* 覆盖写要真的把旧内容清掉，不是从头盖一段 */
    sd_t_check(bsp_sdio_file_write_text(SD_TEST_TXT, "CC\r\n"), "write_text overwrite");
    sd_t_check(bsp_sdio_file_get_size(SD_TEST_TXT) == 4, "overwrite truncates to 4");

    /* 不存在的文件：这几个都必须"干净地失败"，不能挂 */
    sd_t_check(!bsp_sdio_file_exists("SDTEST/NOPE.TXT"), "file_exists on missing");
    sd_t_check(bsp_sdio_file_get_size("SDTEST/NOPE.TXT") == 0, "get_size on missing");
    sd_t_check(bsp_sdio_file_read_text("SDTEST/NOPE.TXT", buf, sizeof(buf)) < 0,
               "read_text on missing");

    /* ---------------- 【4】长文件名（验证 _USE_LFN=1） ---------------- */
    /* 这一项单独拎出来：ffconf.h 里 _USE_LFN 改回 0 的话只有它会 FAIL */
    sd_t_check(bsp_sdio_file_write_text(SD_TEST_LONGNAME, "long name ok\r\n"),
               "LFN: write long file name");
    sd_t_check(bsp_sdio_file_exists(SD_TEST_LONGNAME), "LFN: exists");
    sd_t_check(bsp_sdio_file_get_size(SD_TEST_LONGNAME) == 14, "LFN: size");
    n = bsp_sdio_file_read_text(SD_TEST_LONGNAME, buf, sizeof(buf));
    sd_t_check(n == 14, "LFN: read back");

    /* ---------------- 【5】二进制读写 ---------------- */
    /* 内容里故意放 0x00：文本接口靠 strlen 算长度，遇 0x00 就截断了，
     * 二进制接口必须能完整写进去 */
    for (i = 0; i < 16; i++) {
        bin_w[i] = (uint8_t)(i * 16);        /* 0x00,0x10,0x20 ... 0xF0 */
    }
    sd_t_check(bsp_sdio_file_write(SD_TEST_BIN, bin_w, 16), "file_write binary");
    sd_t_check(bsp_sdio_file_get_size(SD_TEST_BIN) == 16, "binary size == 16");

    memset(bin_r, 0xFF, sizeof(bin_r));
    n = bsp_sdio_file_read(SD_TEST_BIN, bin_r, 16, 0);
    sd_t_check(n == 16, "file_read binary length");
    sd_t_check(n == 16 && memcmp(bin_w, bin_r, 16) == 0, "file_read binary content");

    /* 带偏移读：从第 8 字节开始读 8 个，应当等于原数据的后半段 */
    memset(bin_r, 0xFF, sizeof(bin_r));
    n = bsp_sdio_file_read(SD_TEST_BIN, bin_r, 8, 8);
    sd_t_check(n == 8, "file_read with offset length");
    sd_t_check(n == 8 && memcmp(bin_w + 8, bin_r, 8) == 0, "file_read with offset content");

    /* 偏移越过文件末尾要报错，而不是悄悄读 0 字节 —— 实现里专门核对了 f_tell */
    sd_t_check(bsp_sdio_file_read(SD_TEST_BIN, bin_r, 8, 999) < 0,
               "file_read offset past EOF fails");

    /* ---------------- 【6】按行读 ---------------- */
    sd_t_check(bsp_sdio_file_write_text(SD_TEST_CSV, "row0,1\r\nrow1,2\r\nrow2,3\r\n"),
               "write 3-line csv");

    n = bsp_sdio_file_read_line(SD_TEST_CSV, 0, line, sizeof(line));
    sd_t_check(n == 6 && strcmp(line, "row0,1") == 0, "read_line #0 (CRLF stripped)");

    n = bsp_sdio_file_read_line(SD_TEST_CSV, 2, line, sizeof(line));
    sd_t_check(n == 6 && strcmp(line, "row2,3") == 0, "read_line #2");

    sd_t_check(bsp_sdio_file_read_line(SD_TEST_CSV, 99, line, sizeof(line)) == SD_LINE_EOF,
               "read_line past EOF returns SD_LINE_EOF");

    /* _at 版本：逐行走一遍，offset 每次被改成下一行起点，走完应当正好 3 行 */
    off = 0;
    cnt = 0;
    while (bsp_sdio_file_read_line_at(SD_TEST_CSV, &off, line, sizeof(line)) >= 0) {
        cnt++;
        if (cnt > 10) {
            break;                  /* 防止 offset 没推进导致死循环 */
        }
    }
    sd_t_check(cnt == 3, "read_line_at walks exactly 3 lines");

    /* ---------------- 【7】重命名 / 移动 ---------------- */
    sd_t_check(bsp_sdio_rename(SD_TEST_TXT, SD_TEST_REN), "rename");
    sd_t_check(!bsp_sdio_file_exists(SD_TEST_TXT), "rename: old name gone");
    sd_t_check(bsp_sdio_file_exists(SD_TEST_REN), "rename: new name exists");

    /* 带目录的新路径 = 移动 */
    sd_t_check(bsp_sdio_rename(SD_TEST_REN, SD_TEST_MOVED), "rename into subdir (move)");
    sd_t_check(bsp_sdio_file_exists(SD_TEST_MOVED), "move: file in subdir");

    /* 目标已存在时 f_rename 不覆盖，必须返回 false */
    bsp_sdio_file_write_text(SD_TEST_TXT, "x\r\n");
    sd_t_check(!bsp_sdio_rename(SD_TEST_TXT, SD_TEST_MOVED), "rename onto existing fails");

    /* ---------------- 【8】目录列举 ---------------- */
    /* max_cnt=0 + out=NULL 只数个数，这是拿总数好分配缓冲的正规用法 */
    cnt = bsp_sdio_list_to_buf(SD_TEST_DIR, NULL, 0);
    sd_t_check(cnt > 0, "list_to_buf count-only");

    n = bsp_sdio_list_to_buf(SD_TEST_DIR, entries, 8);
    sd_t_check(n == cnt, "list_to_buf fill matches count-only");
    if (n > 0) {
        for (i = 0; i < n && i < 8; i++) {
            printf("[T ]       %-12s %s %u B\r\n",
                   entries[i].name,
                   entries[i].is_dir ? "<DIR>" : "     ",
                   (unsigned int)entries[i].size);
        }
        /* SUB 是我们自己建的，必须在列表里且被标成目录 */
        ok = false;
        for (i = 0; i < n && i < 8; i++) {
            if (entries[i].is_dir && strcmp(entries[i].name, "SUB") == 0) {
                ok = true;
            }
        }
        sd_t_check(ok, "list_to_buf marks SUB as dir");
    }

    printf("[T ] ---- list_dir output ----\r\n");
    bsp_sdio_list_dir(SD_TEST_DIR);
#if SD_TEST_ENABLE_ROOTLIST
    bsp_sdio_list_dir(NULL);            /* 根目录，开关见本段开头 */
#endif

    /* ---------------- 【9】路径解析 ---------------- */
    /* _USE_LFN=1 之后长名 f_stat 直接过，resolve 会原样返回，不扫目录 */
    ok = bsp_sdio_resolve_path(SD_TEST_LONGNAME, buf, sizeof(buf));
    sd_t_check(ok, "resolve_path on long name");
    if (ok) {
        printf("[T ]       \"%s\" -> \"%s\"\r\n", SD_TEST_LONGNAME, buf);
    }
    sd_t_check(!bsp_sdio_resolve_path("NO/SUCH/PATH.TXT", buf, sizeof(buf)),
               "resolve_path on missing fails");

    /* ---------------- 【10】清空 ---------------- */
    /* 清文件：内容没了，文件还在，大小变 0 */
    sd_t_check(bsp_sdio_clear(SD_TEST_CSV), "clear file");
    sd_t_check(bsp_sdio_file_exists(SD_TEST_CSV), "clear file: still exists");
    sd_t_check(bsp_sdio_file_get_size(SD_TEST_CSV) == 0, "clear file: size 0");

    /* 清目录：里面的东西全删，目录本身留着 */
    sd_t_check(bsp_sdio_clear(SD_TEST_SUB), "clear dir");
    sd_t_check(bsp_sdio_file_exists(SD_TEST_SUB), "clear dir: dir still exists");
    sd_t_check(bsp_sdio_list_to_buf(SD_TEST_SUB, NULL, 0) == 0, "clear dir: now empty");

    /* 传 NULL 是防手滑的保护，必须返回 false 而不是当成根目录 */
    sd_t_check(!bsp_sdio_clear(NULL), "clear(NULL) refused");

    /* ---------------- 【11】删除 ---------------- */
    sd_t_check(bsp_sdio_file_delete(SD_TEST_CSV), "file_delete");
    sd_t_check(!bsp_sdio_file_exists(SD_TEST_CSV), "file_delete: gone");
    sd_t_check(!bsp_sdio_file_delete("SDTEST/NOPE.TXT"), "file_delete on missing fails");

    /* ---------------- 【12】升级文件接口 ---------------- */
    /* 卡上没有升级文件是正常情况，所以这里不判 PASS/FAIL，只打印识别结果。
     * 真要验证升级镜像读取，用 bsp_sdio_fw_test(NULL)，它会把向量表也打出来 */
    printf("[T ] ---- firmware image probe ----\r\n");
    if (bsp_sdio_fw_find(NULL, buf, sizeof(buf))) {
        printf("[T ]       fw_find   : %s\r\n", buf);
    } else {
        printf("[T ]       fw_find   : no image on card (normal if not upgrading)\r\n");
    }
    {
        uint32_t img_size = 0;
        sd_fw_result_t res = bsp_sdio_fw_probe(NULL, &img_size);
        printf("[T ]       fw_probe  : %s\r\n", bsp_sdio_fw_strerr(res));
        if (res == SD_FW_OK) {
            printf("[T ]       size=%u B offset=%u ver=0x%08X path=%s\r\n",
                   (unsigned int)img_size,
                   (unsigned int)bsp_sdio_fw_get_offset(),
                   (unsigned int)bsp_sdio_fw_get_version(),
                   bsp_sdio_fw_get_path());
            /* 流式读一小段就关掉，确认 open/read/close 这条链是通的。
             * 注意 close 必须调，否则 s_fw_file 一直占着，后面清目录会失败 */
            if (SD_FW_OK == bsp_sdio_fw_open(NULL, &img_size)) {
                n = bsp_sdio_fw_read(buf, 64);
                bsp_sdio_fw_close();
                sd_t_check(n > 0, "fw_open/read/close");
            } else {
                sd_t_check(false, "fw_open/read/close");
            }
        }
    }

    /* ---------------- 收尾：把测试目录删干净 ---------------- */
    /* 无条件先清理，再考虑格式化。反过来写的话（格式化完就不用清理了）会让
     * sd_t_cleanup() 在开了格式化开关时变成没人调的 static 函数，armcc 会报
     * #177-D declared but never referenced */
    sd_t_cleanup();

    /* ---------------- 【13】格式化（默认关） ---------------- */
#if SD_TEST_ENABLE_FORMAT
    printf("[T ] formatting, this blocks for up to tens of seconds ...\r\n");
    sd_t_check(bsp_sdio_format(), "format");
    printf("[T ] card formatted, ALL DATA ERASED\r\n");
#else
    printf("[T ] SKIP  format (SD_TEST_ENABLE_FORMAT = 0)\r\n");
#endif

    printf("========== SD self-test: %u passed, %u failed ==========\r\n\r\n",
           (unsigned int)s_test_pass, (unsigned int)s_test_fail);
}
/******************************  TF卡功能自检 end  ******************************/


/* ============================================================================
 *  长文件名(LFN)所需的两个转换函数
 *
 *  【为什么在这里】
 *  ffconf.h 里 _USE_LFN = 1，这样 "operation_log.txt" 和 "OPLOG.TXT" 都能打开。
 *  ff.h 只声明了 ff_convert() / ff_wtoupper()，官方实现放在 option/ccsbcs.c，
 *  而本仓库的 FatFt/ 下没有 option 目录。与其去凑那个文件，不如给一份
 *  ASCII-only 的最小实现 —— 反正要支持的是 operation_log.txt 这种名字。
 *
 *  放在 bsp_sdio.c 末尾而不是新建 .c 文件，是为了不用同时改
 *  Project/test.uvprojx 和 Project/.eide/eide.yml 两个工程文件（漏一个就会
 *  出现"Keil 能连过、VS Code 报未定义符号"这种两边不一致的情况）。
 *
 *  【能做什么、不能做什么】
 *  文件名长度不限（受 SD_PATH_MAX = 64 约束路径总长），但字符只能是 ASCII。
 *  非 ASCII 字符返回 0，FatFs 的两条处理路径都是安全的：
 *    - 建文件时  ff.c create_name()   if (!w) return FR_INVALID_NAME  干净地拒绝
 *    - 读目录时  ff.c get_fileinfo()  if (!w) { i = 0; break; }       放弃长名，
 *                                     退回 8.3 名，所以 PC 上建的中文名文件
 *                                     仍然能用它的 8.3 别名访问
 *  中文文件名要 _CODE_PAGE = 936 + option/cc936.c，那张 GBK 映射表比本镜像
 *  剩下的 flash 还大，做不了。
 * ========================================================================== */

/* OEM <-> Unicode 双向转换，dir = 1 是 OEM->Unicode，0 是 Unicode->OEM。
 * ASCII 区间两个方向都是 1:1，所以不用管 dir。 */
WCHAR ff_convert (WCHAR chr, UINT dir)
{
	(void)dir;
	return (chr < 0x80) ? chr : 0;
}

/* Unicode 转大写，FatFs 用它做长文件名的大小写不敏感比较。
 * 非 ASCII 原样返回（走不到，上面 ff_convert 已经把它们挡掉了）。 */
WCHAR ff_wtoupper (WCHAR chr)
{
	return (chr >= 'a' && chr <= 'z') ? (WCHAR)(chr - 0x20) : chr;
}

