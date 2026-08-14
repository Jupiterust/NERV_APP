# 提示词：把操作日志（收发报文）落到 TF 卡

> 直接复制下面 `---` 之间的全部内容发给 Claude Code。

---

## 任务

在现有操作日志模块的基础上，增加「把每一帧收发的报文追加写入 TF 卡」的能力。
自定义协议、Modbus RTU、Modbus ASCII 三种协议的**接收帧和应答帧都要记**。

要求：不影响现有任何功能，不影响应答时延，没插卡时行为与现在完全一致。

## 已确认的工程事实（这些我已经查过了，不要重复调研，直接用）

**捕获机制已经全部就位，不需要新加钩子。** 8 个记录点已经接好，全部汇聚到
`Function/Op_log_function.c` 的 `oplog_add()` 这一个函数：

| 方向 | 位置 |
|---|---|
| RX 自定义协议 | `Protocol/Custom_Protocol/General_Protocol.c:451` |
| RX Modbus RTU | `Protocol/Protocol_Router.c:259` |
| RX Modbus ASCII | `Protocol/Protocol_Router.c:311` |
| TX 自定义协议 | `Protocol/Custom_Protocol/General_Protocol.c:519` |
| TX Modbus RTU | `Protocol/Protocol_Router.c:278` |
| TX Modbus ASCII | `Protocol/Protocol_Router.c:337` |
| TX 唤醒字符串 | `Function/Function.c:435` |
| TX 告警上报串 | `Function/Log_recording_function.c:151` |

其它现成的东西：

- `oplog_format(const oplog_entry_t*, char *buf, uint32_t size)` 已经是唯一的格式化出口，
  输出 `时间 | 方向 | 协议 | 报文真实长度 | 正文`，校验失败自动加 `BAD ` 前缀，
  截断自动加 `...`。**直接复用，不要另写一套格式化。**
- `bsp_sdio_file_append(const char* name, const void* data, uint32_t len)`
  已存在（`Driver/bsp_sdio.h:108`），显式长度、`FA_OPEN_ALWAYS`，不会截断也不会覆盖。
- `bsp_sdio_is_ready()` 是 TF 卡可用性的唯一真相源。
- FatFs 配置 `_USE_LFN = 1`、`_CODE_PAGE = 437`（`FatFt/ffconf.h:60,93`）：
  长文件名可用，但**文件名必须是纯 ASCII**，中文名会被 `FR_INVALID_NAME` 拒绝。
- 主循环在 `User/main.c` 的 `while(1)` 里，已有 `fs_record_poll()` 这个先例可以照抄位置。
- `oplog_entry_t` 定义在 `Function/Op_log_function.h`【2】节，
  含 `seq / timestamp / raw_len / len / ok / proto / dir / data[OPLOG_DATA_MAX]`。

## 硬约束（违反任何一条都算没做完）

1. **`oplog_add()` 内部绝对不能碰 TF 卡。**
   三个协议的 TX 记录点都在 `bsp_rs485_send_data()` **之前**（见上表 519 / 278 / 337 行），
   在那里做 `f_write` 会把应答时延从当前的 ~2.1 ms 抬到几十~几百 ms，
   直接打穿 Modbus 主站超时，并拖慢每一条命令。
   `oplog_add()` 里只允许做一次 memcpy 入队，不允许有任何阻塞调用。

2. **实际落盘只能在主循环做。** 新增 `oplog_sd_poll()`，插在 `User/main.c` 的
   `Protocol_Route()` 调用**之后**——保证应答已经交给串口中断发出去了，才动卡。

3. **一次 flush 只开关一次文件。** 多条记录先拼进一个 RAM 缓冲区，
   然后一次 `bsp_sdio_file_append()`。不要一条记录一次 open/write/close。

4. **待落盘队列必须独立于现有 6 个 RAM 环**，不要复用环里的条目做"待写标记"。
   环深只有 `OPLOG_DEPTH 8`，主循环被 flash 扇区擦除阻塞 400 ms 时
   （`0x0603` 清告警要擦 4 个扇区）条目会被覆盖，导致日志丢失。
   队列满了就丢最旧的，并计数，提供一个查询丢弃数的接口便于排查。

5. **无卡降级必须是静默的。** `bsp_sdio_is_ready()` 为假时整条落盘链路跳过，
   RAM 环的行为与现在**逐字节一致**，不打印、不回错误帧、不影响任何命令。

6. **加主开关 `#define OPLOG_SD_ENABLE`**，置 0 时编译回今天的行为
   （落盘相关代码全部 `#if` 掉，包括队列占用的 RAM）。

7. **不要新建 .c 文件，直接扩 `Function/Op_log_function.c` / `.h`。**
   本工程新建 .c 必须同时登记到 `Project/test.uvprojx`（对应 `<GroupName>` 下的
   `<File>` 条目）和 `Project/.eide/eide.yml`，漏一个就 Keil 和 VS Code 两套构建发散。
   扩现有文件可以完全避开这个坑。

8. **所有可调项放 `Op_log_function.h` 的【1】节，`.c` 里不留任何字面量。**
   沿用本工程 `modbus_data_map.h` / `file_service.h` 的"现场 5 分钟可改"风格，
   每个 `#define` 上面写清楚改它会发生什么。建议至少包含：
   文件名、攒够几条落一次、最长多少秒强制落一次、队列深度、
   单文件大小上限及超限后的处理、是否写表头、表头内容。

9. **源文件写 UTF-8 无 BOM。** 本工程注释是中文且承载主要设计依据，
   编码写错会永久损坏（已经发生过两次，不可恢复）。

10. **不要动 `Data_class_structure`**（`Function/Function.c:3` 是位置初始化的，
    中间插字段会静默错位，只能追加）。本任务不需要动它。

11. **不要占用定时器。** TIM5/6/7/9/12 五个全部已分配
    （按键 / RS485 T3.5 判帧 / 定时上报 / GD30AD3344 扫描 / 与 TIM7 共向量）。
    需要节流就用 `bsp_rtc_get_unix_timestamp()` 或计数，照抄 `fs_record_poll()` 的做法。

12. **不要重新启用任何被排除的 FreeModbus 文件**
    （`mb.c` / `mbrtu.c` / `mbascii.c` / `port/portserial.c` / `porttimer.c` / `portevent.c`
    在 `test.uvprojx` 里 `IncludeInBuild=0`，重新启用会造成 `USART1_IRQHandler` 重复定义）。

13. **不要改 `oplog_format()` 的输出格式。** 预留命令 `0x0604` 查询操作日志将来要复用它，
    落盘和串口查询必须是同一套格式。

## 建议实现路径

1. `Op_log_function.h`【1】节追加落盘相关 `#define`，文件末尾追加公开函数声明。
2. `Op_log_function.c` 加一个静态待写队列（`oplog_entry_t` 数组 + 头尾指针 + 丢弃计数）。
3. `oplog_add()` 现有逻辑保持不动，函数末尾追加
   `#if OPLOG_SD_ENABLE` → 入队（纯 memcpy）→ `#endif`。
4. 新增 `void oplog_sd_poll(void)`：
   - 队列空 / SD 未就绪 → 立即 return
   - 攒够 N 条，或距上次落盘超过 T 秒 → 循环调 `oplog_format()` 拼进行缓冲区
   - 一次 `bsp_sdio_file_append()` 写出去，成功才出队
5. `User/main.c` 的 `while(1)` 里，`Protocol_Route()` 那个 `if` 块之后加一行 `oplog_sd_poll();`
6. 首次写文件时按开关决定要不要先写一行表头。
7. 改完把改动点和新增的 `#define` 列一个清单给我。

## 验收标准

1. `OPLOG_SD_ENABLE` 置 0 时，行为与改动前完全一致（RAM 环、时延、应答一字不差）。
2. 不插卡上电，所有原有命令正常应答，查询类命令时延仍在 ~2.1 ms 量级。
3. 插卡后发 `A5B60001010101000215ABB6A5`（重启命令，赛题原文给的帧），
   卡上日志文件出现 RX 和 TX 两行，格式与 `oplog_dump_all()` 的输出一致。
4. Modbus RTU 连续轮询 10 条，10 条应答一条不少，日志 20 行一条不丢。
5. 写卡过程中不影响告警上报和定时上报。
6. Keil (`Project/test.uvprojx`) 编译零 error、无新增 warning。

## 明确不要做的事

- 不要在 8 个记录点各加一次 SD 写（只改 `oplog_add()` 一处）
- 不要为这个功能新建 .c 文件
- 不要把落盘做成同步阻塞
- 不要改 `oplog_format()` 的格式
- 不要新占定时器
- 不要改动 `Driver/BootConfig/BootConfig.h`（跨仓库契约，Bootloader 那边要同步）

---
