# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

GD32F470VE (Cortex-M4) application firmware for a 2026 CIMC competition instrument. This is the
**App image** — it is not stand-alone firmware. It is built to run as the IAP payload of a sibling
**Bootloader** project (same physical device, same MCU, separate directory: `../../Bootloader/`).
The two share a flash parameter-block contract (`Driver/BootConfig/BootConfig.h`) and must be kept
in sync by hand; there is no automated check between them. If a change touches the shared boot-
config struct layout or this App image's flash addresses, check whether the sibling Bootloader
needs the matching change — it is the authority on the real, currently-flashed memory layout.

The device: two internal-ADC analog channels (CH0 resistive / CH1 DAC-loopback), an external ADC
(GD30AD3344) sampling three industrial channels (4~20mA current loop + two 0~10V inputs), a DAC
output, RS485 host communication (custom protocol *and* Modbus slave), TF card via SDIO/FatFs,
OLED status display, RTC with wake-from-sleep, and flash-backed alarm logging.

**Note on version control:** `APP/` no longer carries its own `.git` — the working tree now sits
inside an outer repository rooted at `E:/Project`, where the whole `APP/` tree is untracked.
`git log` from here shows the outer repo's history, not this firmware's. Periodic
`APP(AI神力)_N.zip` snapshots in the parent directory are the de-facto history.

## Build / flash

Keil MDK (µVision) is the reference build — `Project/test.uvprojx`. There is no CLI build, no
Makefile/CMake, and **no automated tests**; verification is by building and flashing real hardware.
A mirrored EIDE (VS Code extension) config exists under `Project/.eide` / `Project/.cmsis` using
the same AC5/armcc toolchain, exposed as VS Code tasks in `Project/.vscode/tasks.json`
(`build`, `rebuild`, `clean`, `flash`, `build and flash`). Those tasks invoke EIDE commands and
only work inside VS Code with the extension installed — they are not shell commands.

- Target device: `GD32F470VE`, IROM `0x08000000` (512KB), IRAM `0x20000000` (192KB).
- This App image is linked to load/run at **`0x08011000`**, size `0x20000` (128KB) — see
  `Project/Objects/test.sct`. It is not linked at flash origin because the low ~64KB of flash
  belongs to the sibling Bootloader and the shared param block. `Driver/bsp_sdio.h`'s
  `SD_FW_APP_BASE` / `SD_FW_APP_MAX_SIZE` / `SD_FW_RAM_*` **duplicate these numbers** for image
  validation — change the scatter file and they must change too, or valid images get rejected.
- `Function/App_upgrade/app_upgrade.c: app_nvic_correction()` re-bases `SCB->VTOR` to
  `0x08011000` and re-inits systick/LEDs before the vector table handoff — this must run first in
  `main()` (it does, as line 2), before any interrupt-driven peripheral init.

### Preprocessor defines live in the project files, not in headers

`MB_POINT_MAP_ENABLE` — the Modbus point-table master switch — is **commented out** in
`Function/modbus_data_map.h` and defined at project level instead. It is therefore listed in
**two places that must be edited together**:
- `Project/test.uvprojx` → `<Define>GD32F470,NDEBUG,MB_POINT_MAP_ENABLE</Define>`
- `Project/.eide/eide.yml` → `defineList:`

Editing only the header will not turn the point table on or off. Same applies to any future
project-level define.

**Adding a new `.c` file needs the same double edit** — register it in `Project/test.uvprojx`
(`<File>` entry inside the right `<GroupName>`: Startup / System / Libraries / Driver / Headfile /
User / Fatft / Function / Protocol) *and* in `Project/.eide/eide.yml`, or the two builds diverge —
Keil links it while the VS Code mirror throws undefined-symbol errors, or vice versa.
`Function/Op_log_function.c` is the worked example of a correctly registered pair. New headers go
into `Headfile/Headfile.h`, not into individual `.c` files.

## Source encoding — all `.c`/`.h` are UTF-8 without BOM

Comments throughout are in Chinese and carry most of the design rationale, so encoding is load-
bearing. Every `.c`/`.h` under `Driver/`, `Function/`, `Protocol/`, `User/`, `Headfile/` is valid
UTF-8 (no BOM), matching what Keil's editor and the EIDE/VS Code mirror are configured for.
**Write UTF-8 when editing** — opening a file, re-encoding it, and writing it back in another
codepage silently destroys the comments. That has already happened twice and is unrecoverable:
`Function/bootloader/bootloader.c` has 482 U+FFFD replacement characters and
`Libraries/Source/gd32f4xx_rtc.c` has 6, both present in the earliest snapshot. Only one string
*literal* in the whole tree contains non-ASCII (the corrupted `printf` in `bootloader.c`), so
encoding changes never alter runtime bytes — but they do destroy documentation.
`Project/README.txt` is a stale GBK scratch note (NVIC priority jottings) and is not
authoritative. `Project/.pack/` (vendor CMSIS pack cache) is deliberately left alone.

## Competition spec & test tooling (`文档/`)

Not code, but the authority for *why* the protocol looks the way it does — check here before
inferring intent from the implementation:

- **`1 2026年CIMC工业嵌入式系统开发 初赛 赛题.pdf`** — the qualifier spec. Section 4.3 is the
  authoritative command-word table; section 2.2 defines "some functions reply with a bare ASCII
  string, not a framed packet" (alarm query, Bootloader prompt, sleep wake-up).
- **`2026年CIMC...全国总决赛 样题.pdf`** — the finals sample paper. It contains **no protocol
  chapter at all**; it only adds three requirements over the qualifier (TF-card file I/O,
  GD30AD3344 4~20mA / 0~10V sampling, a FreeModbus slave) and states that detailed program
  requirements are handed out **at the venue**. This is why so much of this codebase is built
  around `#define`s that can be re-pointed in minutes.
- **`GD30AD3344_CN.pdf`** — external ADC datasheet (Chinese).
- **`初赛_功能测试报文清单.md`** — per-feature test table with pre-computed request/response
  frames (groups A–O, K = malformed frames, plus a "code problems found" section R-0…R-8). Update
  it whenever protocol behavior changes; several rows have previously drifted from the firmware.
- **`西门子嵌入式设计论文.docx`** — the design write-up. Not a source of truth for behavior.
- **`crc_tool_file.py`** — frame generator for the file-service test table (`frame()`, `name()`,
  `off()`, `crc16_modbus()`). Its first line prints the spec's own reboot frame
  `A5B60001010101000215ABB6A5` as an anchor: if that matches, the rest of its output is
  trustworthy. The older `crc_tool.py` (which also had `f32()`) is **gone**. Never
  hand-compute CRC16 when adding checklist rows — extend that script instead: CRC16-Modbus
  is poly `0xA001` reflected, init `0xFFFF`, appended big-endian in the custom protocol's binary
  frame but **little-endian** in Modbus RTU. Cross-check any new row against the frames already in
  the checklist before trusting it.

Reading the PDFs needs a workaround: there is no `pdftotext` and no `pip` on this machine, so text
extraction means writing a throwaway pure-Python parser (inflate the streams, apply each font's
ToUnicode CMap, handle both `(...)` literals and `<hex>` strings, treat TJ arrays as text).

**Four command words are marked `预留、初赛不考察` in the spec with no payload format given at
all**: `0x0102` factory reset, `0x0103` device info, `0x0604` operation-log query, `0x0605`
operation-log clear. None appear in the qualifier's 38-item scoring table. Anything implemented
for them is an invented layout — do not present it as spec-derived, and expect the finals to
supply a different one on the day.

## Startup / main loop (`User/main.c`)

Linear init: NVIC correction → debug UART → OLED → external SPI flash → **`param_load()`** →
`bsp_log_init()` (rescans the alarm ring for the pre-power-loss write pointer) → device ID /
Modbus slave address (`MB_FORCE_SLAVE_ADDR != 0` overrides the stored one) → RAM mirrors of ratio
& threshold floats → wake key → SDIO+FatFs (failure is non-fatal, just disables file ops) → RS485
(baud read back from params) → RTC → ADC(DMA) → DAC → external ADC (GD30AD3344 over SPI) →
key/timers → heartbeat frame → `bsp_debug_irq_enable()`.

Everything the flash holds is read into RAM here and **only here** — which is why `0x0102` factory
reset has to end in `mcu_restart()` rather than re-reading the mirrors itself.

Then `while(1)` is flag-driven cooperative polling — there is no RTOS, and every ISR only sets
flags or copies buffers:
1. `g_rs485_rx_flag` → `Protocol_Route(rx_real_buffer, rx_real_len)`, then clear flag and length.
2. `g_report_flag` (TIM7 periodic) → clear it, then run `report_auto_data_task()` **only if**
   `Regular_reporting_Flag == 1` *and* `!Protocol_ModbusMasterActive()`. Both guards matter, see
   the bus-arbitration note below.
3. `report_police_function(alarm_mode)` — alarm-threshold check + logging.
4. `ad3344_debug_poll()` / `bsp_wkp_alarm_test_poll()` — compile to empty unless their debug
   switches are on (`AD3344_DEBUG_SCAN`, `SLEEP_DEPTH==0`).
5. OLED IDLE/AutoSample indicator (edge-triggered on state change), then `bsp_key_scan()`.

### Timer allocation — five timers, all spoken for

| Timer | Owner | Purpose |
|---|---|---|
| TIM5 | `bsp_key_init()` | key scan tick |
| TIM6 | `bsp_rs485.c` | Modbus T3.5 inter-frame silence / frame delimiting |
| TIM7 | `bsp_tim7_init()` | periodic auto-report (1s/3s/5s, ARR re-loaded at runtime) |
| TIM9 | `bsp_tim9_init()` | 10ms GD30AD3344 background scan; priority above TIM7 so sampling isn't preempted by reporting |
| TIM12 | shares TIM7's IRQ vector | (`TIMER7_UP_TIMER12_IRQn`) |

Check this table before claiming a timer. `bsp_tim7_set_timeout()` changes the report period by
writing ARR and firing a software update event — **that event is indistinguishable from a real
overflow and sets `g_report_flag`**, which is precisely why guard 2 above also tests
`Regular_reporting_Flag`. Without it, changing the interval emits a spurious `0x0302` frame while
the device still reports itself as idle.

## RS485 custom protocol (`Protocol/Custom_Protocol/General_Protocol.c`)

Wire format is **ASCII hex-encoded binary**, not raw binary: `Protocol_ParseChar()` accumulates
characters looking for the literal ASCII bytes `"A5B6"` (header) … `"B6A5"` (trailer), hex-decodes
everything between them into a binary frame, then validates length and a CRC16-Modbus before
dispatch. Binary frame layout (big-endian multi-byte fields):

```
A5 B6 | device_id(2) | frame_type(1) | cmd_word(2) | payload_len(1) | version(1) | payload(N) | crc16(2) | B6 A5
```

`Protocol_HandleFrame()` switches on `cmd_word` (defined in `General_Protocol.h`, grouped by class:
`CMD_SYS_*`, `CMD_DATA_*`, `CMD_CTRL_*`, `CMD_CFG_*`, `CMD_OTA_*`, `CMD_ALARM_*`, `CMD_LOG_*`,
`CMD_SPEC_*`) and calls a `CMD_..._FUNCTION()` handler. **All handler bodies live in
`Function/Function.c`**, not in the protocol file — `General_Protocol.c`/`.h` own framing and
dispatch only, never business logic. To add a command: add the `CMD_*` word and `case` in
`General_Protocol.c`/`.h`, then implement `CMD_..._FUNCTION()` in `Function.c` and declare it in
`Function.h`.

Two behavioral quirks worth knowing before touching this file:
- While `Data_class_structure.Regular_reporting_Flag == 1` (auto-report active), all incoming
  commands except `CMD_CTRL_AUTO_REPORT_STOP` are silently ignored.
- `CMD_SPEC_DISCOVER` only dispatches when `frame_type == TYPE_HEARTBEAT`, and its handler replies
  with a `CMD_SPEC_HEARTBEAT` frame (not an ID-only reply) — that is the discovery-broadcast
  response format the host must expect.

Not every `case` is a working command, and the states are easy to confuse. Note especially that
"replies with an error" vs "replies with nothing" is decided in **different files**:

| Command | State |
|---|---|
| `0x0102` factory reset | Implemented. Writes flash defaults, optionally clears the alarm log (`FACTORY_RESET_CLEAR_ALARM_LOG` in `Function.c`), ACKs, then `mcu_restart()` — the reset is what reloads every RAM mirror, so it is not optional |
| `0x0103` device info | Implemented with an **invented** 23-byte layout. Built to be re-shaped fast: numbered blocks, a running `len` instead of hard-coded indices, identity strings as `#define`s above the function |
| `0x0604` / `0x0605` operation log | Only the *command* is missing — the log itself already runs. `CMD_LOG_READ_FUNCTION()` / `CMD_LOG_CLEAR_FUNCTION()` in `Function.c` are **empty** and the error reply is emitted by the `case` in `General_Protocol.c` instead. To implement: swap that `Protocol_SendFrame(..., TYPE_ERROR, ...)` line for the handler call, and fill the body from `Op_log_function.h`'s ready-made query API (see the operation-log section below) |
| `0x0403` / `0x0413` CH2 threshold | Dispatched, but both handler bodies are blank, so the device replies with **nothing** — see `R-4` in the test checklist |
| `0x0503` OTA execute | Empty stub; `0x0502` ACKs unconditionally (`if(SUCCESS)`) without handling any payload |

When implementing a reserved command, put the whole thing in one `CMD_..._FUNCTION()` with the
tunable parts as `#define`s directly above it, rather than spreading logic across handlers — the
finals hand out the real format on the day and it has to be re-pointed under time pressure.

## Protocol coexistence — custom protocol + Modbus RTU/ASCII (`Protocol/Protocol_Router.c`)

Both protocols share the same physical UART (USART1 / `BSP_RS485_USART`).

**Frame delimiting.** `Driver/bsp_rs485.c`'s ISR delimits frames using a **TIM6-based T3.5 silence
timer** (`RS485_T35_TIMER`, see the block comment at the top of that file): every received byte
resets TIM6, and if no new byte arrives before TIM6 reaches its reload value (Modbus-spec T3.5 —
3.5 character times, clamped to a fixed 1.75ms for baud > 19200 per spec, recomputed by
`rs485_t35_timer_apply_baud()` on `bsp_rs485_init()` / `bsp_rs485_set_baudrate()`), the update ISR
`TIMER6_IRQHandler` treats the accumulated bytes as one frame (`rx_real_buffer`/`rx_real_len`) and
sets `g_rs485_rx_flag`. TIM6 and USART1 share NVIC preemption priority 0 so their ISRs cannot
interrupt each other, avoiding a race on `rx_index`/`rx_buffer`. This replaced a version using the
USART hardware IDLE flag, which only measures ~1 character time of silence (a GD32 hardware
constant) — a sender with inter-byte jitter (e.g. a USB-RS485 adapter that batches bytes) could
split one real frame into two, each failing checksum and being silently dropped.

**Routing.** `Protocol_Route()` switches on the global `Protocol_mode`: 1 = force custom, 2 = force
Modbus RTU, 3 = force Modbus ASCII (all three ignore frame content), 4 = auto-sniff (default, from
`MB_DEFAULT_PROTOCOL_MODE`) — sniffs the first 4 bytes, where literal ASCII `"A5B6"` means custom
protocol (fed into `Protocol_ParseChar()`, a byte-level state machine, so it is unaffected by where
TIM6 draws frame boundaries); anything else goes to `Modbus_ProcessFrame()`, which sniffs a leading
`':'` + trailing CRLF to pick ASCII vs RTU framing.

**Bus arbitration.** `Protocol_Router.c` tracks `s_last_proto` — the protocol of the last frame
that *passed its checksum* (set after validation, never before: bus-collision garbage would
otherwise permanently silence auto-reporting). `Protocol_ModbusMasterActive()` reports whether a
Modbus master is polling; when it is, `main()` skips auto-report frames entirely. A Modbus slave
may only answer, and an unsolicited frame on a half-duplex bus corrupts the master's transaction.
The flag is only cleared by the next custom-protocol frame, so the device resumes reporting
automatically. `MB_SUPPRESS_AUTO_REPORT_ON_MODBUS` in `modbus_data_map.h` disables this guard.

**Why FreeModbus's own engine is not used.** `mb.c`, `mbrtu.c`, `mbascii.c`, `port/portserial.c`,
`port/porttimer.c`, `port/portevent.c` are all excluded from the build (`IncludeInBuild=0` in
`Project/test.uvprojx`). That engine expects to own USART1's RX interrupt byte-by-byte, colliding
with `bsp_rs485.c`'s ISR (both defined a `USART1_IRQHandler` — a duplicate-symbol build break) and
calls three functions never implemented in this repo (`my_usart1_init()`, `my_usart_485_CS()`,
`my_timer_init()`). Instead `Protocol_Router.c` takes the already-delimited whole frame, decodes it
itself (CRC16 for RTU, hex-text + LRC for ASCII, hand-rolled), and calls FreeModbus's function-code
handlers (`eMBFuncReadHoldingRegister`, `eMBFuncReadCoils`, … from `functions/mbfunc*.c`) and
register callbacks (`eMBRegHoldingCB` etc. from `port/port.c`) directly and synchronously via
`Modbus_DispatchPDU()` — those are pure buffer functions with no ISR/timer dependency. Before
re-enabling any excluded file, re-read this paragraph.

This app-level T3.5 timer does **not** implement T1.5 intra-frame gap detection (TIM6 is a basic
timer with no input-capture channel, and real stacks rarely implement it either) — fine for the
single-master polled bus this device targets, worth revisiting for a multi-master bus.

### Modbus point table (`Function/modbus_data_map.h` / `.c`)

`Free_Modbus/port/port.c` owns the four register buffers (`REG_INPUT_BUF`, `REG_HOLD_BUF`,
`REG_COILS_BUF`, `REG_DISC_BUF`) and nothing else: its callbacks only translate a wire address into
a buffer index and move bytes. Real device data moves in and out of those buffers via
`Function/modbus_data_map.c` — `Modbus_SyncRegsFromDevice()` before dispatch,
`Modbus_ApplyHoldingRegs()` / `Modbus_ApplyCoils()` after a write, `Modbus_ExecPendingActions()`
after the reply is sent. Business logic stays in `Function/`, per the competition's layering
requirement.

**`Function/modbus_data_map.h` is the single file meant to be edited at the competition venue, and
it is deliberately all `#define`s** — addresses, per-class address bases (whole-table shift),
buffer capacities, float/u32 word order, integer scale factors, baud-rate codes, command magic
values, FC07 status bits, forced slave address, default protocol mode. It carries its own on-site
documentation (a Modbus primer, seven "official table says X, change Y" recipes, a troubleshooting
table, the full default point table). Preserve that property: when something here becomes tunable,
add a `#define` there rather than a literal in the `.c`.

**Master switch.** `MB_POINT_MAP_ENABLE` (a *project-level* define, see the build section) gates the
whole point table. Removing it compiles out sections 【4】–【9】 of the header and the entire mapping
body of `modbus_data_map.c`, which falls through to an `#else` branch with empty stubs of the five
public functions plus a written-out template for hand-rolling a different mapping. The Modbus slave
itself — framing, routing, address match, CRC/LRC, every function code, the four register buffers —
keeps working either way; only the device-data⇄buffer layer switches off. Sections 【2】【3】
(slave address, protocol mode, address bases, buffer capacities, baud codes) stay defined in both
states because `port.c`, `port.h`, `Protocol_Router.c` and `main.c` depend on them. Keep that split:
if a consumer outside `modbus_data_map.c` needs a macro, it belongs outside the `#ifdef`.

The finals' three sample holders are deliberately *not* in this file — they are the last members of
`DeviceDataParams_t` (`i0_current`, `v0_voltage`, `v1_voltage`, `i0_broken`), so ADC drivers can
write them with no `extern` (every file gets `Function.h` via `Headfile.h`) and the master switch
cannot compile them out.

**Struct trap:** `Data_class_structure` in `Function.c:3` is initialized **positionally**
(`{1.0f, 1.0f, 3.3f, …}`), so new fields may only be **appended**. Inserting one mid-struct shifts
every later initializer silently — the members are all floats and small ints, so nothing warns at
compile time and it surfaces only as wrong runtime values. Appended fields are zero-initialized by
C, which is why the initializer is deliberately shorter than the member list.

Two mechanisms make on-site address edits safe, and both must be preserved when adding points:
- Every point has an `MB_CHK(...)` line at the top of `modbus_data_map.c` turning an out-of-range
  address into a *compile* error (negative array size), not a runtime overrun.
- `port.c`'s `mb_reg_index()` does the base subtraction in `int32_t` and rejects negatives, so a
  wire address below the configured base cannot wrap into a huge unsigned index.

Neither catches the third failure mode: **`MB_CHK` checks range, not overlap.** Every measured
quantity is published twice — as an IEEE754 float (2 registers, e.g. `MB_IREG_CH0_VALUE`) and as a
scaled integer mirror (1 register, e.g. `MB_IREG_CH0_SCALED` = value × `MB_SCALE_CH0`) — and
`Modbus_SyncRegsFromDevice()` refreshes both every dispatch. An official table wanting the integer
where the float already sits is satisfied by moving one `#define`, but if the two land on the same
index both writes happen, the later silently wins, and nothing warns at compile or run time.
**Check for collisions by hand after re-pointing addresses.**

Reads need no post-dispatch work — `Modbus_SyncRegsFromDevice()` runs *before* dispatch. The reply
is built in place: `eMBFunc*` and `Modbus_DispatchPDU()` only write the response PDU and report its
length; nothing is transmitted until `Modbus_ProcessRTUFrame()`/`ProcessASCIIFrame()` append the
checksum and call `bsp_rs485_send_data()` — the single sink every protocol's replies funnel through.

Address chain: master sends a base-0 wire address → `mbfunc*.c` adds 1 (vendor convention) →
`port.c` subtracts 1 and the class's `MB_*_ADDR_BASE` → buffer index, which is the number written in
`modbus_data_map.h`. `REG_*_SIZE` in `port.h` are aliases for the `MB_*_COUNT` macros, so buffer
sizing is also edited from the one file.

## External ADC — GD30AD3344 (`Driver/bsp_GD30AD3344.h`)

Same on-site-tunable design as the point table: section 【1】 of the header holds every knob (range,
rate, single-shot vs continuous, whether TIM9 background scan runs, debug printing) and section
【1.8】 the engineering-unit conversion. The driver returns **pin voltage** from its raw read; the
physical quantity is computed alongside using the 【1.8】 coefficients and stored into
`Data_class_structure`:

- **CH0** — 4~20mA current loop across a 91Ω sense resistor → `i0_current` (mA), plus `i0_broken`.
  Broken-wire detection (【1.9】) exploits the fact that 0mA is illegal in a 4~20mA loop: a reading
  well under 4mA means the transmitter, cable, or terminal is disconnected. It is debounced over N
  consecutive low samples so it cannot trip on a single glitch. The same voltage is *also* run
  through the legacy PT100 calibration into `ch2_current_temp` — PT100 is officially not examined;
  that path is vestigial and does not affect the current reading.
- **CH1 / CH2** — 0~10V inputs behind a 0.18 hardware divider → `v0_voltage` / `v1_voltage` (V).

PGA range only sets resolution and the clipping point, not the converted value. It is 2.048V to
suit the 4~20mA path (clips at ~22.5mA on CH0, ~11.38V on CH1/CH2).

`printf` costs milliseconds, so the TIM9 ISR only sets a flag; actual debug printing happens in
`ad3344_debug_poll()` in the main loop, and compiles to nothing when `AD3344_DEBUG_SCAN` is off.

## TF card / FatFs (`Driver/bsp_sdio.c`, `FatFt/`)

Initialized from `main()`; failure is non-fatal and only disables file operations
(`bsp_sdio_is_ready()` is the single source of truth).

**Modbus is the one path that reaches the card.** Writing coil `MB_COIL_LOG_TO_SD` (pulse-type)
makes `Modbus_ApplyCoils()` append one CSV line — timestamp, CH0, CH1, CH2 — to
`MB_SD_LOG_FILENAME` (`"SAMPLE.CSV"`, `modbus_data_map.h`【9】), and the result lands in
`s_sd_ok`, published as `MB_DISC_SD_READY` / `MB_IREG_SD_READY` / the `MB_EXST_BIT_SD_READY` bit of
FC07. Change the fields by editing that one `snprintf` (line must fit `line[96]`).
The driver API is far wider than what is wired up (`mkdir`, `rename`, `delete`,
`format`, `list_to_buf`, `get_space`, `file_read_line`/`_at` for streaming a big file back without
buffering it) — check `bsp_sdio.h` before writing anything new.

### File service (`Function/file_service.h` / `.c`) — the finals' TF-card path

Added 2026-08-13 and **never compiled or run on hardware** — build it in Keil before trusting it.
Nine custom-protocol commands `0x0701`–`0x0709` (card status, list, chunked read, chunked write,
delete, info, record start/stop, line-wise read) plus a CSV sampling recorder driven from the main
loop by `fs_record_poll()` (RTC-second paced — all five timers are already spoken for).

Same venue-tunable design as `modbus_data_map.h`: **`file_service.h` is all `#define`s and is the
only file meant to be edited at the venue** — command class/words, name-field encoding, offset
width, chunk size, ACK/error style, error codes, CSV file name / interval / header / row format,
size cap. `file_service.c` holds no literals and splits into 【A】 a protocol-agnostic
`fs_*()` service layer (Modbus can call it directly) and 【B】 the pack/unpack command handlers;
a venue payload change touches only 【B】. The top of `file_service.c` carries `FS_CHK` compile-time
range checks (same negative-array-size trick as `MB_CHK`), so a bad macro edit fails the build.

Two enabling changes went in with it: `General_Protocol.c` frame buffers grew to hold a full
255-byte payload (`raw_frame`/`raw_buf` 256→288, `ascii_tx_buf` 512→600, and `HexStrToBytes()`
took an `out_size` parameter — it silently overran before), and `bsp_sdio.c` gained
`bsp_sdio_file_append()` / `bsp_sdio_file_write_at()`, the length-explicit binary counterparts of
`bsp_sdio_file_append_text()` (which truncates at the first `0x00`). Both open with
`FA_OPEN_ALWAYS`, never `FA_CREATE_ALWAYS` — the latter would erase earlier chunks.

`TF卡_决赛预制代码包.md` (repo root) is the companion doc: what the finals paper actually says,
the 5-minute venue edit table, the per-scenario playbook, pre-computed test frames, and the
hardware checklist. `文档/crc_tool_file.py` regenerates those frames.

### Long file names are ON, and enabling them had a caller-side trap

`ffconf.h` now has `_USE_LFN = 1` and `_CODE_PAGE = 437`, so `f_open("operation_log.txt")` and
`f_open("OPLOG.TXT")` both work. Three coupled facts, all cross-referenced in a "Project note" block
in `ffconf.h` itself:

- **`_USE_LFN` must stay `1`.** `2` puts the `(_MAX_LFN+1)*2` = 512-byte work buffer on the stack,
  and `Stack_Size` is `0x800` (2KB). `3` needs a heap; `Heap_Size` is `0x400`. `1` is a static BSS
  buffer — "not reentrant", which is harmless with `_FS_REENTRANT = 0` and no RTOS.
- **`option/ccsbcs.c` was not needed.** `ff_convert()` / `ff_wtoupper()` are declared in `ff.h` but
  the distribution implements them in an `option/` directory this repo doesn't have. Minimal
  ASCII-only versions live at the **bottom of `bsp_sdio.c`** — put there so no new file has to be
  registered in both `test.uvprojx` and `.eide/eide.yml`. Code page 437 was chosen because it sets
  `_DF1S 0` (no DBCS) and its upper-case table `_EXCVT` is built into `ff.c`. `_CODE_PAGE = 1`
  (ASCII) is *not* usable — `ff.c` `#error`s on it whenever LFN is on.
- **File names may be any length but must be ASCII.** Chinese names would need `_CODE_PAGE = 936`
  plus `option/cc936.c`, whose GBK table is larger than this image's remaining flash. Degradation is
  graceful: `ff_convert()` returns 0, so creating such a name is rejected with `FR_INVALID_NAME`
  and reading a directory containing one just falls back to its 8.3 alias.

**The trap:** turning LFN on adds `lfname` / `lfsize` members to `FILINFO`, and `get_fileinfo()`
writes *through* `fno->lfname`. Every stack-local `FILINFO` therefore has to be initialized or it
writes to a garbage address. `bsp_sdio.c` defines `SD_FILINFO_INIT(fno)` for this — a macro rather
than a bare `fno.lfname = NULL` so that `_USE_LFN` can still be switched back to `0` without a
compile error. **All eight `FILINFO` sites use it; any new one must too.**

`bsp_sdio_resolve_path()` / `bsp_sdio_fw_find()` — which scan directories and reverse-map Windows'
8.3 aliases (`Updata_file` → `UPDATA~1`) — are consequently **no longer required** and are kept only
as a fallback: for Chinese-named files (still unreachable by long name) and in case LFN ever gets
switched back off. New code should just pass the long path.

`bsp_sdio_list_dir(NULL)` is the fastest way to see what is actually on a card. Firmware images are
probed for both layouts — a raw `fromelf --bin` dump, or an 8-byte header (`SD_FW_MAGIC_WORD` +
version) followed by the raw image — and validated against the App's link addresses before use.

## Two logs, and they are unrelated

Easy to conflate because the spec calls both "日志". Keep them straight:

| | Alarm log (告警记录) | Operation log (操作日志) |
|---|---|---|
| Module | `Function/Log_recording_function.c` | `Function/Op_log_function.c` |
| Records | threshold violations | every frame sent or received |
| Storage | external SPI flash ring, `0x07C000`+ | **RAM only**, lost on power-off |
| Spec | qualifier 2.4, *is* examined (`0x0602`) | 4.5.7(2) + reserved `0x0604`/`0x0605` |

### Operation log (`Function/Op_log_function.h`)

Records "when, which protocol, which direction, what bytes". Same venue-tunable style as the point
table: section 【1】 of the header holds every knob (`OPLOG_DEPTH`, `OPLOG_DATA_MAX`,
`OPLOG_RECORD_BAD`, `OPLOG_TZ_OFFSET_HOUR`, `OPLOG_FORCE_HEX`), and the whole header is
self-documenting — read it rather than the `.c`.

- **Six independent rings**: 3 protocols (custom / Modbus RTU / Modbus ASCII) × 2 directions.
  Default `6 × 8 × ~80B ≈ 3.8KB` of RAM.
- **Cross-ring ordering uses `seq`, not the timestamp** — the RTC is second-resolution and several
  frames can land in one second, so a merged view sorted by timestamp would be wrong.
  `oplog_get_merged()` / `oplog_get_merged_count()` already do it right; use them for any
  "list the last N operations" requirement. Within one ring, `oplog_get(proto, dir, 0)` is the
  **newest** entry — the same reverse ordering as alarm query `0x0602`.
- **The eight record points are already wired** (`Protocol_ParseChar`, both
  `Modbus_Process*Frame` RX+TX paths, `Protocol_SendFrame`, plus the two bare-string replies in
  `Function.c` and `Log_recording_function.c`). Adding a new reply path is the only reason to call
  `oplog_add()` yourself. Frames failing CRC/LRC are recorded with `ok = 0` and render with a
  `BAD ` prefix — that is what satisfies 4.5.7(2)'s "discard, log, reply error".
- **Not interrupt-safe by design**: every record point sits in the main loop, so there is no
  critical section around `s_seq` and the write pointers. Calling `oplog_add()` from an ISR needs
  one added first.
- Nothing calls `oplog_init()` — statics are already zero, so it exists only for an explicit
  power-on reset. `oplog_format()` is the single place the output format lives; `oplog_dump_all()`
  prints to the debug UART and is far too slow for the main loop.

### `g_log_busy` — the flash-busy interlock

Defined in `Function.c:4`, `extern`'d in `modbus_data_map.c`. Erasing the alarm-log sectors blocks
for roughly 400ms, and `report_police_function()` returns early while the flag is set so an alarm
write can't collide with an in-progress clear/query. Any new code that erases or scans the log
region must raise and lower it the same way. It is also why a Modbus master writing
`MB_COIL_CLEAR_ALARM` needs a response timeout of at least 1s.

## Persistent storage — two unrelated flash mechanisms

Don't confuse these; different physical devices, different addressing:

1. **External SPI NOR flash (GD25Q40E, 512KB, `Driver/bsp_flash.c`)** — this App's own operating
   parameters and alarm log, address space `0x000000`–`0x07FFFF`:
   - `0x000000`: `bsp_flash_param_t` (device ID, Modbus address, RS485 baud, CH0/CH1 ratio &
     threshold floats) — accessed via `Function/param_handle.c` (`param_load()` / `param_get_*` /
     `param_set_*`). The whole struct is rewritten with a sector erase on every set; there is no
     wear leveling.
   - `0x07C000`–`0x080000` (last 4 sectors / 16KB): fixed-size alarm log ring of
     `bsp_error_log_struct` entries, driven by `Function/Log_recording_function.c`. `bsp_log_init()`
     rescans for the pre-power-loss write pointer and continues from there.
2. **Internal MCU flash (`Driver/Rom/rom.c`, `internal_flash_*`)** — the shared boot/App contract at
   `0x08010000`, laid out by `BootConfig.h`'s `BootParam_t` / `UpdateLog_t` / `UserConfig_t` /
   `CalibData_t`. This is what the sibling Bootloader reads to decide what to boot; this App only
   pokes a few fields (see OTA below). Treat `BootConfig.h`'s layout as a cross-repo contract, not
   something to restructure unilaterally.

   **Visibility trap:** `BootConfig.h` declares `BootParam_t` etc., but the struct that actually maps
   the block (`Parameter_t`, wrapping all four) is defined inside `Function/bootloader/bootloader.c`,
   and `PARAM_ADDR` inside `Function/App_upgrade/app_upgrade.c` — both `.c` files, not headers. So
   `Function/` code cannot read `BootParam_t` fields directly; the only exposed accessors are the
   handful in `app_upgrade.h` (`get_app_version()`, `set_app_id_version()`, `set_app_updateFlag()`,
   …). Also, the identity fields the block reserves (`deviceID`, `productModel`, `serialNumber`,
   `hwVersion`) are written by the *Bootloader*, so this image cannot assume they are initialized —
   reading an unwritten block yields `0xFF`. Compile-time `#define`s are the safer source for
   device-identity data in App-side handlers.

## OTA / firmware upgrade — spans both repos, mostly not implemented here

`CMD_OTA_REQUEST_FUNCTION()` in `Function.c` is the only OTA piece actually wired in: it ACKs,
writes the device ID and sets `updateFlag = app_update_receive_start` into the shared internal-flash
param block via `set_app_id_version()` / `set_app_updateFlag()`, waits for the UART TC flag so the
ACK physically leaves the wire, then calls `mcu_restart()`. **That reset is the handoff** — the
sibling Bootloader boots next, reads the flag, and runs the real receive/flash/verify state machine.

`app_upgrade_start()` (the large raw-USART2 receive loop in the same file, staging firmware at
`DOWNLOAD_ADDR 0x08051000`) and the entire `Function/bootloader/bootloader.c` are **vendored copies
of the sibling Bootloader's logic and are dead code here** — neither is called from anywhere
(confirm with `grep -rn "app_upgrade_start\|bootloader_start"` before assuming otherwise). Only the
sibling Bootloader repo is authoritative for how that state machine behaves.
`CMD_OTA_READY_DATA_FUNCTION()` and `CMD_OTA_EXECUTE_FUNCTION()` exist as dispatch targets but do no
real payload handling.

## Sleep / wake (`Driver/bsp_wkp.c`)

`bsp_deepsleep_config()` is the entry point; how deep it sleeps is set by `SLEEP_DEPTH` at the top of
the file (0 = arm the RTC alarm but don't sleep, 1 = normal sleep, 2 = deep sleep — currently 1).
That ladder exists as a debugging tool: when the device won't wake, walk 0 → 1 → 2 to separate an
alarm-configuration problem from a sleep entry/exit problem. RS485 quiescing, RTC self-check, and
post-wake clock/485 restoration all happen inside. At `SLEEP_DEPTH == 0`,
`bsp_wkp_alarm_test_poll()` prints `RTC ALARM FIRED` from the main loop; otherwise it is empty.

## Directory map

- `User/` — `main.c`, interrupt vector callbacks (`gd32f4xx_it.c`), systick.
- `Driver/` — GD32 peripheral BSPs (`bsp_*.c`: RS485, ADC/DAC, GD30AD3344, OLED, RTC, SDIO, key,
  external SPI flash, wake key), plus `BootConfig/` (shared struct), `Rom/` (internal flash
  primitives), `Usart/` (the separate USART2 upgrade-receive driver used only by the dead
  `app_upgrade_start()` path).
- `Function/` — business logic: `Function.c` (all `CMD_*_FUNCTION` handlers + alarm polling),
  `modbus_data_map.c/.h` (the venue-editable point table), `param_handle.c`,
  `Log_recording_function.c` (alarm log → SPI flash), `Op_log_function.c/.h` (operation log → RAM
  rings), `file_service.c/.h` (TF-card file commands + CSV recorder, venue-editable header),
  `App_upgrade/`, `bootloader/` (the latter two mostly vendored/dead).
- `Protocol/Custom_Protocol/` — the ASCII-hex frame protocol.
- `Protocol/Protocol_Router.c` — custom-protocol / Modbus coexistence and bus arbitration.
- `Protocol/Free_Modbus/` — vendored FreeModbus; only `functions/mbfunc*.c`, `functions/mbutils.c`,
  `port/port.c`, `rtu/mbcrc.c` are built. See the coexistence section before re-enabling anything.
- `FatFt/` — FatFs R0.09 + disk I/O glue for the SD driver.
- `Libraries/` — GD32F4xx standard peripheral library (vendor code).
- `Headfile/Headfile.h` — the single aggregating include pulled in by every source file; add new
  module headers here rather than including them piecemeal. It also drags in `<stdio.h>` /
  `<string.h>` via `bsp_sys.h`, which is why handlers use `strlen`/`memset` without an include.
- `文档/` — competition spec PDFs, the test-frame checklist, `crc_tool_file.py`. The only place the
  protocol's intent is recorded. `TF卡_决赛预制代码包.md` sits at the repo root, not here.
