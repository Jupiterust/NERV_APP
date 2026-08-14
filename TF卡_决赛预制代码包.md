# TF 卡 · 决赛预制代码包

> 生成于 2026-08-13，**同日已全部落地到工作树**（不再是"待打的补丁"，是"已在的代码"）。
> **目标：现场拿到题目后，只改 `Function/file_service.h` 里的 `#define`，不动 `.c`。**
> 相关：`初赛_功能测试报文清单.md`（初赛报文表）、`文档/crc_tool_file.py`（本文报文的生成脚本）。

## 落地状态一览

| 项 | 状态 | 位置 |
|---|---|---|
| P0-1 协议缓冲区扩容 | ✅ 已改 | `Protocol/Custom_Protocol/General_Protocol.c` |
| P0-2 驱动层二进制追加 / 定位写 | ✅ 已加 | `Driver/bsp_sdio.c` + `.h` |
| P0-3 长文件名 LFN | ✅ 本来就开着 | `FatFt/ffconf.h`：`_USE_LFN 1` + `_CODE_PAGE 437` |
| 文件服务 + 9 条命令 | ✅ 已加 | `Function/file_service.h` / `.c` |
| 接入（Headfile / switch / 主循环 / 两个工程文件） | ✅ 已接 | 见第六节 |
| 真卡验证 | ⬜ **没做，必须你来做** | 见第九节自检清单 |

**没有编译过**：这台机器上没有 armcc / gcc，代码是对着真实接口逐个核对手写的，语法只做了括号平衡与编码检查。**第一件事是在 Keil 里 build 一次。**

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
- 决赛现场提供物品清单里**没有 TF 卡**（只有 24VDC/USB 供电、1 路 4~20mA 源、2 路 0~10V 源、RS485 线缆）。**卡要自己带，且要带备份，提前格式化成 FAT32。**
- 现场**禁止任何网络连接**，所以任何"临时去下载个文件"的方案都不成立（这也是 P0-3 必须赛前做完的原因，见第三节）。

---

## 二、现场 5 分钟改指南 ⭐拿到题先看这页

题目发下来以后，按这个顺序改 `Function/file_service.h`，`.c` 一行都不用动：

| 现场题目说什么 | 改哪个 `#define` |
|---|---|
| 「文件操作命令字用 0x08xx」 | `FS_CMD_CLASS` 改成 `0x08`，整类命令一起平移 |
| 「读文件命令字是 0x0712」 | 单独把 `FS_CMD_READ` 那行的 `FS_CMD(0x03)` 换成 `0x0712` |
| 「数据文件名为 LOG.TXT」 | `FS_REC_FILE` |
| 「每 2 秒记录一次」 | `FS_REC_INTERVAL_S` |
| 「CSV 列序为 时间,电流,电压1,电压2」 | `FS_CSV_HEADER` + `FS_CSV_ROW_FMT` + `FS_CSV_ROW_ARGS` 三个一起改 |
| 「不要表头」 | `FS_CSV_WRITE_HEADER` 改 0 |
| 「分片长度 128 字节」 | `FS_CHUNK_MAX` |
| 「文件名字段定长 12 字节」 | `FS_NAME_FIXED_LEN` 改 12（0 = 长度前缀模式） |
| 「错误用 EEEE 应答」 | `FS_ERR_USE_EEEE` 改 1 |
| 「偏移量用 2 字节」 | `FS_OFFSET_BYTES` 改 2 |
| 「成功要回 FF」 | `FS_ACK_STYLE` 改 1（**已经是 1**，要改成"回数据"才动它） |
| 「最多记 N 条，满了覆盖」 | `FS_REC_MAX_BYTES` 填字节数上限（0 = 不限） |

改完 **一定要 build 一次**：`file_service.c` 开头有一组编译期自检（`FS_CHK`），宏改出范围会直接编译报错（数组长度为负），不会拖到现场才发现。

**如果现场给的载荷布局和预设的完全不同**：只改 `file_service.c` 里 `【B】命令处理` 那一段的解包代码，`【A】文件服务` 那一段（真正干活的）不用动。两段之间是干净的函数调用边界。

---

## 三、前置补丁（已经打完，这里留原因）

### P0-1 ｜协议层缓冲区 —— ✅ 已改

**原因**：赛题的 `报文长度` 是 1 字节，合法 payload 上限 255。文件读写是唯一会把 payload 顶到上限的功能，而原来四处缓冲区都是 256 / 512，一帧满载就越界。

`Protocol/Custom_Protocol/General_Protocol.c` 实际改动：

- `HexStrToBytes()` 加了第 4 个参数 `out_size`，`str_len/2 > out_size` 直接返回 -1（按帧长错误处理，不越界写）；
- 接收侧 `raw_frame[256]` → `raw_frame[288]`，调用处补 `sizeof(raw_frame)`；
- `Protocol_SendFrame()` 里 `raw_buf[256]` → `[288]`，`ascii_tx_buf[512]` → `[600]`。

> 288 = 13(固定部分+CRC+帧尾) + 255(payload 上限) 取整；600 = 288 × 2 再留余量。
> `BytesToHexStr()` 不写 `'\0'`，长度靠 `ascii_len` 单独算，所以不用额外留终止符位。
> 字符级接收缓冲 `rx_buffer[1024]` 和 `rx_real_buffer[1024]` 本来就够（满帧 ASCII 是 536 字符），没动。

**验收**：见第七节 T-17（满片写，payload=213）和 T-18（payload=255，应回错误码 04 且设备不复位）。

### P0-2 ｜驱动层二进制追加 + 定位写 —— ✅ 已加

**原因**：原来只有 `bsp_sdio_file_append_text(const char*)`，靠 `'\0'` 结尾算长度 —— 二进制数据里出现 `0x00` 就截断。分片写必须要显式长度的版本。

`Driver/bsp_sdio.h` / `.c` 新增两个函数：

```c
bool bsp_sdio_file_append(const char* filename, const void* data, uint32_t len);
bool bsp_sdio_file_write_at(const char* filename, const void* data,
                            uint32_t len, uint32_t offset);
```

实现要点（和文件里其它写函数保持一致）：
- 都用 `FA_OPEN_ALWAYS | FA_WRITE`，**不是** `FA_CREATE_ALWAYS` —— 后者每次打开都清空文件，分片写第二片就把第一片抹掉了；
- 先调 `sd_make_parent_dirs()`，路径带目录时目录会自动建出来（和 `bsp_sdio_file_write` 一样）；
- `_USE_LFN` 已经是 1，所以**不需要**再走 `bsp_sdio_resolve_path()` 那套 8.3 反查，长名直接传给 FatFs；
- `write_at` 在 `f_lseek` 之后核对 `f_tell() == offset`，落点不对（卡满、扩不出去）当失败；
- FatFs R0.09 没有 `FA_OPEN_APPEND`，追加靠 `f_lseek(&f, f_size(&f))` 手动实现。

⚠️ `write_at` 的 offset 超过当前文件大小时，FatFs 会把文件扩到该位置，但**中间那段内容未定义（不保证是 0）**。乱序下发分片不会写错位置，但别把空洞当 0 用。

### P0-3 ｜长文件名（LFN）—— ✅ 本来就是开的

`FatFt/ffconf.h` 现在是 `_USE_LFN 1` + `_CODE_PAGE 437`，`ff.h` 需要的 `ff_convert()` / `ff_wtoupper()` 的最小 ASCII 实现在 **`bsp_sdio.c` 末尾**（放那儿是为了不用再往两个工程文件里注册新文件）。所以：

- **文件名可以是任意长度的 ASCII**，`f_open("Sample_Data_2026.csv", ...)` 直接好使；
- **中文文件名仍然存不了**（要 `_CODE_PAGE 936` + `option/cc936.c`，那张 GBK 表比本镜像剩下的 flash 还大）。降级是温和的：`ff_convert()` 返回 0 → 建这种名字被拒绝，返回 `FR_INVALID_NAME`；
- 因此 `file_service.h` 的 `FS_NAME_AUTO_FIX` **默认取 0**（原样透传，现场给的长名一字不差地落到卡上）。只有 `_USE_LFN` 被改回 0 时才需要置 1，靠 `fs_fix_name()` 转成 8.3 短名兜底；
- 一个残留限制：`FS_CMD_LIST` 列目录用的 `bsp_sdio_list_to_buf()` 拿的是 `fno.fname`，也就是 **8.3 别名**（`Sample_Data_2026.csv` 会显示成 `SAMPLE~1.CSV`）。读写不受影响，只是列表不好看。现场若要求列出长名，给 `FILINFO` 挂一个 `lfname` 缓冲即可（`SD_FILINFO_INIT` 那套，见 `bsp_sdio.c`）。

---

## 四、`Function/file_service.h` —— 现场只改这个文件

**代码以工作树里的文件为准**（这里不再复制全文，免得两边漂移）。文件结构：

| 节 | 内容 | 现场会不会改 |
|---|---|---|
| 【1】 | 命令字：`FS_CMD_CLASS` + 9 条命令 | ★ 大概率 |
| 【2】 | 载荷布局：`FS_NAME_FIXED_LEN` / `FS_OFFSET_BYTES` / `FS_CHUNK_MAX` / `FS_NAME_MAX` / `FS_LIST_MAX` | ★ 大概率 |
| 【3】 | 应答风格：`FS_ACK_STYLE` / `FS_ERR_USE_EEEE` / `FS_ERR_WITH_CODE` + 8 个错误码 | ☆ 可能 |
| 【4】 | 数据记录：`FS_REC_FILE` / `FS_REC_INTERVAL_S` / `FS_CSV_*` / `FS_TZ_HOUR` / `FS_REC_MAX_BYTES` / `FS_REC_COEXIST_REPORT` | ★ 大概率 |
| 【5】 | 文件名兜底：`FS_NAME_AUTO_FIX` | ☆ 只在 LFN 关掉时 |
| 【6】 | 说明：编译期自检 `FS_CHK` 在 `file_service.c` 开头，宏改出范围直接编译报错 | ✗ 别动 |
| 【7】 | 对外接口声明 | ✗ 别动 |

默认值（现在编译进去的就是这套）：命令字 `0x0701`~`0x0709`、文件名 `[名长(1)][名(N)]`、偏移 4 字节大端、分片 200 字节、成功回 `FF`、错误回原命令字 + 1 字节错误码、记录文件 `DATA.CSV` 每 1 秒一行 7 列 CSV。

---

## 五、`Function/file_service.c` —— 结构

同样以工作树的文件为准。分两段，边界是干净的函数调用：

**【A】文件服务** —— 协议无关，只跟 `bsp_sdio` 打交道，Modbus 那边可以直接调：

| 函数 | 作用 | 返回 |
|---|---|---|
| `fs_fix_name(in, out, size)` | 文件名规范化（默认透传，`FS_NAME_AUTO_FIX=1` 时转 8.3） | `bool` |
| `fs_status(&total_kb, &free_kb)` | 卡是否就绪 + 容量 | 错误码 |
| `fs_info(name, &size)` | 文件在不在 + 多大 | 错误码 |
| `fs_read(name, off, want, out, &got)` | 分片读 | 错误码 |
| `fs_write(name, off, data, len, append)` | 分片写（`append=1` 追加 / `0` 定位覆盖） | 错误码 |
| `fs_delete(name)` | 删文件 | 错误码 |
| `fs_record_start/stop/active/poll/last_ok` | 采样 → CSV 的定时记录 | — |

**【B】命令处理** —— 只解包 / 组包，业务全部转给【A】：9 个 `CMD_FILE_*_FUNCTION()`。
现场载荷格式和预设不同时，**只改这一段**。

几个刻意的行为，别当 bug：
- **`0x0701` 没插卡不回错误帧**，回一帧 `就绪=0` 的正常应答，上位机好判断；
- **`0x0706` 文件不存在也不算错误**，回 `存在=0`；要"不存在就报错"用 `0x0703` 读，它回错误码 02；
- **`fs_record_start()` 不清空已有文件**，重复下发"开始记录"是续写（断点续存本来就该这样）。要每次重来，把 `FS_REC_MAX_BYTES` 设成 1 或在 `fs_append_sample()` 里加 `bsp_sdio_clear()`；
- **开始记录会把定时上报关掉**（`FS_REC_COEXIST_REPORT = 0`）。原因是赛题 H-02 规定自动上报期间除"停止上报"外不响应任何命令，上报开着连"停止记录"都发不进来。

---

## 六、接入现有工程 —— ✅ 已接完

| # | 文件 | 改了什么 |
|---|---|---|
| 1 | `Headfile/Headfile.h` | `#include "file_service.h"`（在 `modbus_data_map.h` 之后） |
| 2 | `Protocol/Custom_Protocol/General_Protocol.c` | switch 里加了 9 个 `case FS_CMD_*`，放在 `case CMD_LOG_CLEAR` 之后、`case CMD_SPEC_DISCOVER` 之前 |
| 3 | `User/main.c` | 主循环 `report_police_function()` 之后加 `fs_record_poll();` |
| 4 | `Project/test.uvprojx` + `Project/.eide/eide.yml` | Function 分组里都加了 `file_service.c`（两边必须同步，见 CLAUDE.md） |

分发那 9 个 `case` **刻意不校验 `frame_type`** —— 现场用哪个帧类型下发文件命令是未知的，宁可放宽。现场若明确要求只认 `0x01`，照抄旁边命令的 `if (frame_type == TYPE_CMD_SEND)` 包一层即可。

---

## 七、测试报文（CRC 已算，可直接粘串口工具）

设备 ID `0001`，协议版本 `02`。**锚点自检**：`A5B60001010101000215ABB6A5` 是赛题文档给的重启帧，和这里用同一个脚本算出来一致，说明下表可信。

生成脚本已存到 **`文档/crc_tool_file.py`**（`python3 文档/crc_tool_file.py` 直接重打整张表）。改了 `FS_NAME_FIXED_LEN` / `FS_OFFSET_BYTES` / `FS_CMD_CLASS` 就改脚本顶部对应的几行重新生成。

| 编号 | 测试项 | 预期应答 | 下发报文 |
|---|---|---|---|
| T-01 | 查卡状态 / 容量 | `[1][总KB][剩余KB]` | `A5B6000101070100029DABB6A5` |
| T-02 | 列根目录第 0 条 | `[总数][是目录][大小][名]` | `A5B600010107020102003B8DB6A5` |
| T-03 | 列根目录第 1 条 | 同上 | `A5B60001010702010201FB4CB6A5` |
| T-04 | 查 `DATA.CSV` 信息 | `[存在][大小]` | `A5B60001010706090208444154412E4353567BDEB6A5` |
| T-05 | 读 `DATA.CSV` off=0 len=64 | `[实长][数据]` | `A5B600010107030E0208444154412E435356000000004064E0B6A5` |
| T-06 | 读 `DATA.CSV` off=64 len=64 | 同上 | `A5B600010107030E0208444154412E4353560000004040A4D1B6A5` |
| T-07 | 覆盖写 `TEST.TXT` = "Hello" | `FF` | `A5B60001010704130208544553542E545854000000000048656C6C6F3231B6A5` |
| T-08 | 追加写 `TEST.TXT` = "World" | `FF` | `A5B60001010704130208544553542E5458540100000000576F726C642CD7B6A5` |
| T-09 | 删除 `TEST.TXT` | `FF` | `A5B60001010705090208544553542E54585432A4B6A5` |
| T-10 | 开始记录 | `FF` | `A5B6000101070700029C4BB6A5` |
| T-11 | 停止记录 | `FF` | `A5B6000101070800029F7BB6A5` |
| T-12 | 按行读 `DATA.CSV` off=0 | `[下一行off][行长][行]` | `A5B600010107090D0208444154412E43535600000000C60BB6A5` |
| T-13 | **读**不存在的 `NOPE.TXT` | 错误码 `02` | `A5B600010107030E02084E4F50452E545854000000004094BEB6A5` |
| T-14 | 空文件名（名长=0） | 错误码 `03` | `A5B600010107060102000B8CB6A5` |
| T-15 | 读 `TEST.TXT` off=0 len=64（验 T-07/08） | `[10]HelloWorld` | `A5B600010107030E0208544553542E545854000000004002DFB6A5` |
| T-16 | **查**不存在的 `NOPE.TXT` 信息 | `[0][00000000]`（不是错误！） | `A5B600010107060902084E4F50452E5458547577B6A5` |
| T-17 | 满片写 `BIG.TXT` 200 字节（payload=213，验 P0-1） | `FF`，且设备不复位 | 见 `文档/crc_tool_file.py` 生成 |
| T-18 | payload=255 的写（数据 239 > 分片上限） | 错误码 `04`，且设备不复位 | 同上 |

> T-01~T-16 按 `FS_NAME_FIXED_LEN=0`（长度前缀）+ `FS_OFFSET_BYTES=4` 生成。

**建议测试顺序**：T-01（确认卡在）→ T-10（开始记录）→ 等 10 秒 → T-11（停止）→ T-04（看文件大小 > 0）→ T-12 反复调用直到 EOF（把 CSV 读回来）→ T-07 → T-08 → T-15（读回 `HelloWorld`）→ T-13/T-14/T-16（异常）→ T-17/T-18（缓冲区）→ 拔卡插电脑核对 CSV。

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
| **要求返回文件行数** | 在 `fs_info()` 里加一个循环调 `bsp_sdio_file_read_line_at()` 数行，接口已现成 |
| **数据格式不是 CSV，是定长二进制记录** | 改 `fs_append_sample()`：把 `snprintf` 换成填结构体 + `bsp_sdio_file_append()`（P0-2 补的那个二进制版），其余不动 |
| **要求按时间/序号分文件** | `FS_REC_FILE` 改成运行时拼名字：`char fn[24]; snprintf(fn,sizeof(fn),"D%02d%02d.CSV",month,day);`，LFN 已开，长名也行 |
| **要求掉电后接着写**（断点续存） | 已经是这个行为 —— `fs_record_start()` 不清空已有文件，追加写每次都定位到文件尾 |
| **要求记录条数上限 / 环形覆盖** | `FS_REC_MAX_BYTES` 填字节上限（一行约 60 字节 × 条数），超了自动清空重记 |
| **不插卡时的行为** | 已处理：所有命令先查 `bsp_sdio_is_ready()`，回 `FS_ERR_NO_CARD`(01)。`main.c` 里 SD 初始化失败本来就是非致命的 |
| **要求 Modbus 也能读写文件** | `fs_*()` 那一层是协议无关的，在 `modbus_data_map.c` 的 `Modbus_ExecPendingActions()` 里直接调；点表里已有 `MB_COIL_LOG_TO_SD` 可以复用 |
| **要求 TF 卡存告警记录** | 在 `Log_recording_function.c` 的告警记录函数末尾加一句 `bsp_sdio_file_append_text("ALARM.CSV", line);` |
| **要求记录周期 < 1 秒** | RTC 只有秒分辨率。挂到 TIM7 的 `g_report_flag` 上，或在 `fs_record_poll()` 里改用 systick 计数 |

---

## 九、赛前自检清单（⬜ 全是待办，这些必须在真卡上做）

- [ ] **Keil 里 build 一次，零错误零警告**（代码没在本机编译过）
- [ ] EIDE / VS Code 那边也 build 一次（两个工程文件都加了 `file_service.c`，验证没漏）
- [ ] T-17 满片写不复位；T-18 payload=255 回错误码 04 不复位（P0-1 验收）
- [ ] T-01 能查到卡容量，数值和电脑上看的一致
- [ ] T-10 → 等 10 秒 → T-11，`DATA.CSV` 里有约 10 行，时间戳每秒递增
- [ ] 拔卡插电脑，`DATA.CSV` 能用 Excel 直接打开，列对齐、时间正确（不是 1980 年）
- [ ] T-12 反复调用能把整个 CSV 读回来，最后回 EOF(07)
- [ ] T-07 → T-08 → T-15，读回来的内容是 `HelloWorld`
- [ ] 长文件名实测：写一个 `Sample_Data_2026.csv`，拔卡看电脑上名字是不是完整的
- [ ] 不插卡时下发 T-01/T-05/T-10，分别回 `就绪=0` / 错误码 01 / 错误码 01，设备不死
- [ ] 记录期间下发其它命令，设备正常响应（记录不占总线）
- [ ] **实测一次 `fs_append_sample()` 耗时**，超过 100ms 就改成文件常开 + 定期 `f_sync`
- [ ] 三张不同品牌/容量的卡各测一遍，且都提前格式化成 FAT32

---

## 十、已知取舍

写下来免得现场当成 bug 去查：

1. **记录用 RTC 秒计时，最小间隔 1 秒**。五个定时器（TIM5/6/7/9/12）全部有主，不新增定时器是刻意的。要更快的记录频率，得挂到 TIM7 的 `g_report_flag` 上。
2. **`fs_append_sample()` 每次都 open/write/close**。安全（掉电最多丢一行）但慢。现场如果要求高频记录，改成文件常开 + 定期 `f_sync`。
3. **`bsp_sdio_get_space()` 在空闲簇没缓存时会整表扫 FAT**（32GB 卡几百毫秒）。`fs_write()` 每次写都调它，第一次慢、之后走 FatFs 的缓存。现场写入很频繁又嫌慢，就把这个检查改成每 N 次查一次。
4. **`fs_time_string()` 调 `bsp_rtc_show_time()`，它内部会 printf 一行**。跟告警记录走的是同一条路，保持一致；嫌慢就换成直接 `rtc_current_time_get()`。
5. **`FS_CMD_LIST` 只列根目录**，不下潜子目录，且拿到的是 8.3 别名（见第三节 P0-3 最后一条）。`bsp_sdio_list_to_buf()` 支持传目录名，要列子目录把载荷加一个目录名字段即可。
6. **`fs_fix_name()` 在 `FS_NAME_AUTO_FIX=1` 时只放行字母数字**，其余符号一律换 `_`。FAT 短名其实允许 `$%'-_@~` 等，但现场不值得为这个冒险。默认是 0（透传），这条只在 LFN 关掉时才生效。
7. **`static sd_entry_t list[64]`** 占约 1.5KB 静态 RAM（在 `CMD_FILE_LIST_FUNCTION` 里）。RAM 有 192KB，不是问题，但改 `FS_LIST_MAX` 时心里有数。
