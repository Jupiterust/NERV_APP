#ifndef __BSP_GD30AD3344_H
#define __BSP_GD30AD3344_H
#include "bsp_sys.h"

/*==============================================================================
 * GD30AD3344 16 位 Δ-Σ ADC 驱动
 *   4 路单端 / 2 路差分输入，内置基准 + 振荡器 + PGA，SPI 接口
 *   数据手册：文档/GD30AD3344_CN.pdf（Rev 1.1）
 *
 * ---- 芯片关键事实（来自数据手册，改代码前先看）------------------------------
 *  1. 只有两个寄存器：转换寄存器（只读，16 位二进制补码）和配置寄存器
 *     （读写，复位值 0x058B），没有寄存器地址的概念。
 *  2. SPI 模式 1（CPOL=0 / CPHA=1），SCLK 空闲低，上升沿移出、下降沿锁存。
 *     手册 8.1 节写死了这个模式。
 *  3. 一次 16 位传输是边写边读：DIN 送进去的是新配置字，同时 DOUT 移出来的是
 *     上一次已转换完的结果。启动下次转换和读走上次结果是同一个 SPI 动作。
 *     后台扫描的零等待就建立在这一条上。
 *  4. 配置字里 NOP[2:1] 等于 01 才会更新配置寄存器。纯读取发 0x0000 即可
 *     （NOP=00，芯片不改配置）。
 *  5. 单次模式（MODE=1，上电默认）：写 OS=1 启动一次转换，约 25us 上电，
 *     转换耗时 1/DR，完成后自动掉电，转换期间再写 OS=1 无效。
 *     连续模式（MODE=0）：芯片一直转，随时读到的都是最新值。
 *  6. CS 拉高会复位 SPI 接口，SCLK 连续低于 28ms 也会。每次传输必须完整地
 *     CS低 → 16/32 个 SCLK → CS高，不能把 CS 一直按住。
 *  7. 单端输入不输出负码（负码只在差分下出现），只用到 ±FSR 的正半边。
 *
 * DRDY/EXTI 那条路径已删除（原实现在中断里做长延时 SPI 读，且无人调用），
 * 引脚宏保留在【2】区，以后要做 DRDY 触发可直接用。
 *============================================================================*/


/*==============================================================================
 *【1】赛场可调区 —— 需要改行为的话只动这一块
 *============================================================================*/

/*--- 1.1 采样方式：单次转换 / 连续转换 -----------------------------------------
 * AD3344_MODE_SINGLE     单次（MODE=1）。每读一次写一次 OS=1 触发转换，完成后
 *                        自动掉电。功耗低、采样时刻受控，代价是每次读要等一个
 *                        转换周期。
 * AD3344_MODE_CONTINUOUS 连续（MODE=0）。芯片不停地转，读的时候直接拿最新值。
 *                        功耗高，且切换通道后要丢掉 AD3344_CONT_DISCARD 次结果
 *                        （手册 7.4.2.2：写新配置时，正在进行的转换仍用旧配置完成）。
 *
 * 只影响【4.2】的前台阻塞读取。10ms 后台扫描【4.3】固定用单次触发：连续模式下
 * 芯片的转换节奏和 10ms 定时器不同步，读回来会是重复值或跨了半次的旧值。 */
#define AD3344_MODE_SINGLE          0
#define AD3344_MODE_CONTINUOUS      1

#define AD3344_CONV_MODE            AD3344_MODE_SINGLE      /* ← 改这里切换 */

/* 连续模式下切换通道后丢弃几次结果。芯片单周期稳定，丢 1 次够，2 次是余量。
 * 单次模式下此宏无效。 */
#define AD3344_CONT_DISCARD         2


/*--- 1.2 TIM9 10ms 后台自动扫描 -------------------------------------------------
 * 打开后 TIMER9 中断（10ms，bsp_general_timer.c）里跑一次 ad3344_tim9_10ms_isr()：
 * 一次 16 位 SPI 同时完成"读走上个通道的结果 + 启动下个通道的转换"，无 delay。
 *
 * 每 10ms 采一个通道，按 AD3344_SCAN_CH_MASK 轮流：
 *   只扫 CH0        → CH0 每 10ms 更新
 *   扫 CH0/CH1/CH2  → 每通道 30ms 更新（默认）
 * 结果存驱动内部缓存，ad3344_get_ch_volt() 取，不阻塞。 */
#define AD3344_TIM9_SCAN_ENABLE     1       /* 1=开启后台扫描  0=关闭 */

/* 参与扫描的通道，按位或 */
#define AD3344_CH0_BIT              0x01
#define AD3344_CH1_BIT              0x02
#define AD3344_CH2_BIT              0x04
#define AD3344_CH3_BIT              0x08
#define AD3344_SCAN_CH_MASK         (AD3344_CH0_BIT | AD3344_CH1_BIT | AD3344_CH2_BIT)

/* 采样结果是否自动写回 Data_class_structure。
 * 1 = 驱动写；0 = 只存内部缓存，上层用 ad3344_get_xxx() 取 */
#define AD3344_SCAN_WRITE_BACK      1


/*--- 1.3 量程（PGA / FSR）------------------------------------------------------
 * 可选 6.144 / 4.096 / 2.048 / 1.024 / 0.512 / 0.256 / 0.064 V。
 * 量程只决定分辨率和削顶点，不影响换算出来的电压值：LSB = FSR / 32768，
 * 2.048V 档 62.5uV，4.096V 档 125uV。超量程的输入被削到 0x7FFF。
 *
 * VDD=3.3V，选 4.096V/6.144V 档实际也只能测到 VDD。 */
#define AD3344_PGA_SEL              AD3344_REG_CONFIG_PGA_2_048V    /* ← 全局默认量程 */

/* 单通道量程覆盖，默认全部跟随 AD3344_PGA_SEL。
 * 2.048V 量程下各路余量（换算关系见【1.8】）：
 *   CH0  4~20mA × 91Ω  →  0.364 ~ 1.820V   削顶点相当于 22.5mA
 *   CH1  0~10V × 0.18  →  0     ~ 1.800V   削顶点相当于 11.38V
 *   CH2  同 CH1
 *
 * PT100（也走 CH0）的标定是在 4.096V 量程下测的，2.048V 量程约 64℃ 就削顶
 * （(2.048+263.33)/159.668 ≈ 63.7℃）。PT100 不考察，量程按 4~20mA 的需要选
 * 2.048V；要量高温再改回 4_096V。 */
#define AD3344_PGA_CH0              AD3344_PGA_SEL      /* 4~20mA 采样电阻电压（兼 PT100 遗留）*/
#define AD3344_PGA_CH1              AD3344_PGA_SEL      /* 0~10V 分压后电压 0 */
#define AD3344_PGA_CH2              AD3344_PGA_SEL      /* 0~10V 分压后电压 1 */
#define AD3344_PGA_CH3              AD3344_PGA_SEL      /* 未使用 */


/*--- 1.4 数据速率 DR ------------------------------------------------------------
 * 可选 6.25 / 12.5 / 25 / 50 / 100 / 250 / 500 / 1000 SPS。
 * 转换时间等于 1/DR（单周期稳定，无额外建立时间）：
 *      6.25→160ms  12.5→80ms  25→40ms  50→20ms
 *      100→10ms    250→4ms    500→2ms   1000→1ms
 * 速率越低噪声越小。开了 10ms 后台扫描时转换时间必须小于 10ms，DR 不能低于
 * 100SPS，低于了【6】区编译期报错。 */
#define AD3344_DR_SEL               AD3344_REG_CONFIG_DR_500SPS

/* 等待转换完成时在理论转换时间外多给的余量（us），已含单次模式约 25us 的上电时间 */
#define AD3344_CONV_MARGIN_US       500


/*--- 1.5 传输周期长度 -----------------------------------------------------------
 * 16 位：只读转换结果（手册 7.5.9）。
 * 32 位：转换结果 + 配置寄存器回读（手册 7.5.8），排查配置有没有写进芯片时用，
 *        回读值由 ad3344_last_cfg_readback() 取。 */
#define AD3344_TRANS_32BIT          0       /* 0=16位周期  1=32位周期(带配置回读) */


/*--- 1.6 调试打印 ---------------------------------------------------------------
 * printf 走 bsp_debug 的串口，一条几十字节的语句在 115200 下要好几毫秒。 */
#define AD3344_DEBUG_EN             1       /* 总开关：0 = printf 一句都不编译 */

/* 前台阻塞读取（ad3344_read_chX）每次打印一行，调通道接线时打开 */
#define AD3344_DEBUG_READ           1

/* 后台 10ms 扫描的打印。
 *   0 = 不打印
 *   1 = 主循环里打印。中断只置标志，ad3344_debug_poll() 才 printf，不撑爆中断。
 *   2 = 直接在中断里 printf。10ms 的中断里干几 ms 串口会严重超时，仅临时排障用。 */
#define AD3344_DEBUG_SCAN           0

/* 扫描打印抽稀：每 N 轮扫描打一行。3 通道一轮 30ms，33 轮约 1 秒一行 */
#define AD3344_DEBUG_SCAN_DECIM     33


/*--- 1.7 PT100 温度标定（CH0）---------------------------------------------------
 * 温度 = AD3344_PT100_K * 电压 + AD3344_PT100_B
 * 系数由两点实测反推：0℃ → 1.9470V，130℃ → 2.5241V
 *      K = 130 / (2.5241 - 1.9470) = 159.668
 *      B = -1.9470 * 159.668       = -263.33
 * 换了采样电阻/激励电流/分压比就重测两点，代进上式改这两行。 */
#define AD3344_PT100_K              159.668f
#define AD3344_PT100_B              (-263.33f)


/*--- 1.8 通道工程量换算 ---------------------------------------------------------
 * 采到的是 ADC 引脚电压，前级电路（采样电阻 / 分压电阻）决定它对应多少物理量。
 * 换板子或换采样电阻，这里的系数就要全改。
 * 换算结果由驱动写进 Data_class_structure，映射点只有一处：
 * bsp_GD30AD3344.c 的 ad3344_dispatch_to_struct()。
 *
 * 当前接线：
 *   CH0  4~20mA 电流环，串 91Ω 采样电阻  →  i0_current (mA) + i0_broken
 *        同一路电压另按老标定算一份 PT100 温度 → ch2_current_temp，
 *        PT100 不考察，此路保留，不影响电流换算
 *   CH1  0~10V 输入，硬件分压 0.18 倍     →  v0_voltage (V)
 *   CH2  0~10V 输入，硬件分压 0.18 倍     →  v1_voltage (V)
 *   CH3  未接 */

/* ---- CH0：电流环 ----
 * 电流 = 采样电阻电压 / 阻值。91Ω 时 4mA → 0.364V，20mA → 1.820V。
 * AD3344_CH0_I_UNIT_K 决定 i0_current 的单位。改成 A 的话，Function.h 的字段注释、
 * modbus_data_map.h 的 MB_IREG_I0_CURRENT / MB_SCALE_I0、下面的断线阈值都要跟着改。 */
#define AD3344_CH0_SENSE_R          91.0f       /* 采样电阻，单位 Ω */
#define AD3344_CH0_I_UNIT_K         1000.0f     /* 1000=输出mA  1=输出A */

//线性公式
#define AD3344_I0_CAL_ENABLE        1           /* 0 = 不修正，等价于 K=1.0 B=0.0 */
#define AD3344_I0_CAL_K             1.0031f     /* 增益系数，1.0 = 不改变增益 */
#define AD3344_I0_CAL_B             0.5459f     /* 偏置，单位跟着 AD3344_CH0_I_UNIT_K */

/* 断线检测的阈值等参数见【1.9】 */

/* ---- CH1 / CH2：分压后的 0~10V ----
 * 分开写，硬件上两路分压电阻不一样时可单独调 */
#define AD3344_CH1_DIV_RATIO        0.18f
#define AD3344_CH2_DIV_RATIO        0.18f

/*--- 1.9 4~20mA 断线检测（CH0）---------------------------------------------------
 * 4~20mA 回路里 0 是非法值：正常最小也有 4mA，变送器掉线、线缆断、端子松，
 * 采样电阻上就没电流。所以判断线 = 判电流明显低于 4mA。
 *
 * 直接写 if (I < 3.5) 有两个问题：
 *   1) 信号停在阈值附近时噪声让它反复穿越，断线标志跟着 30ms 一翻，上位机看到
 *      的是闪烁告警。→ 回差：低于 BROKEN_TH 判断线，高于 CLEAR_TH 才判恢复，
 *      中间区段不改状态。
 *   2) 单次干扰或采样抖动就误报。→ 消抖：连续 N 次满足条件才翻转。
 *
 * 两条判断路径共用这套参数：
 *   a) 后台 10ms 扫描每采到一次 CH0 就喂一次状态机，结果锁存在
 *      Data_class_structure.i0_broken，ad3344_is_broken() 读。常态走这条。
 *   b) ad3344_broken_check_now() 阻塞采一次 CH0 立刻给结论，不走消抖（只有一个
 *      样本）。这个样本同样喂给状态机，两条路的状态不会打架。
 *
 * 消抖时间 = 连续 N 次 × CH0 刷新周期。扫三路时 CH0 是 30ms 一次，N=5 约 150ms
 * 确认断线。要灵敏就减小 N，要抗干扰就加大 N。 */

#define AD3344_BROKEN_DET_ENABLE    1       /* 总开关。0 = 整个功能不编译，i0_broken 恒 0 */

/* 判定阈值，单位跟随 AD3344_CH0_I_UNIT_K（默认 mA）。
 * 必须 CLEAR_TH > BROKEN_TH，差值即回差带宽。
 * 3.5 / 3.8 = 正常下限 4mA 之下留 0.5mA 余量，回差 0.3mA。 */
#define AD3344_BROKEN_TH            3.5f    /* 低于它 → 倾向判断线 */
#define AD3344_BROKEN_CLEAR_TH      3.8f    /* 高于它 → 倾向判恢复 */

/* 消抖次数，连续多少次满足条件才翻转。填 1 = 不消抖 */
#define AD3344_BROKEN_COUNT         5       /* 连续 N 次偏低 → 置断线   */
#define AD3344_BROKEN_CLEAR_COUNT   5       /* 连续 N 次正常 → 解除断线 */

/* 上电初始状态。0 = 先当正常，采够 N 次低电流才报断线，开机不会误报；
 * 1 = 先当断线，采够 N 次正常才解除 */
#define AD3344_BROKEN_INIT_STATE    0

/* 状态翻转时打一行日志。事件级打印，很稀疏。机制同扫描打印：
 * 中断置标志，主循环 ad3344_debug_poll() 里 printf */
#define AD3344_BROKEN_DEBUG         0


/*--- 1.10 上电时的厂商模式寄存器(MMR)初始化 --------------------------------------
 * 0x8100/0x8106 这套 MMR 读写钥匙和 0x12/0x14 号寄存器在 GD30AD3344_CN.pdf 里
 * 没有记载（手册只有转换寄存器和配置寄存器）。这是原厂例程里的魔数，实测在本板
 * 工作正常，原样保留。含义无从查证，做成开关，怀疑它捣乱时可以关掉对比。 */
#define AD3344_MMR_INIT_ENABLE      1       /* 上电写 MMR 0x12 = 0xACCA */
#define AD3344_EXT_REF_ENABLE       0       /* 用 AIN3 做外部基准（会占掉 CH3）*/


/*--- 1.11 电流通道标定模式 -------------------------------------------------------
 * 为【1.8】那两个系数取数用。打开后，后台扫描每采够 AD3344_CAL_AVG_N 个 CH0
 * 样本就在主循环打一行：
 *
 *   [AD3344][cal] n=32 code=0x0E95(3733.4) V=0.45524 raw=5.003mA cal=5.003mA
 *
 *   n     本行平均的样本数
 *   code  平均原始码（十六进制是四舍五入值，括号里是真实平均）。故障分层看这列：
 *         恒 0x0000 / 0xFFFF = SPI 或接线不通，恒 0x7FFF = 超量程削顶，
 *         数值合理但电流不对 = 换算系数问题
 *   V     平均引脚电压
 *   raw   未修正电流 I = V / R × 1000，算 K/B 用这列
 *   cal   套上当前 K/B 后的电流，标完拿它和标准表对比验收
 *
 * 平均在原始码上做（32 样本约 1 秒），避免把一次采样噪声固化进系数。
 *
 * 标定流程：
 *   1) 确认 AD3344_I0_CAL_K = 1.0f、AD3344_I0_CAL_B = 0.0f，本宏置 1，编译烧录
 *   2) 电流源依次给 4 / 8 / 12 / 16 / 20mA，每点稳定几秒，记下 raw 列
 *   3) 按【1.8】用 4mA 和 20mA 两点算 K、B，填回【1.8】重新编译
 *   4) 再走一遍五个点，看 cal 列确认误差收敛
 *   5) 关回 0，省掉串口开销
 *
 * 需要 AD3344_DEBUG_EN=1 且 AD3344_TIM9_SCAN_ENABLE=1，【6】区有检查。 */
#define AD3344_CAL_MODE             0       /* 1 = 打开标定打印（标完记得关回 0）*/
#define AD3344_CAL_AVG_N            32      /* 每多少个 CH0 样本平均并打印一行 */


/*==============================================================================
 *【2】硬件引脚映射 —— 换板子改这里
 *============================================================================*/

/* SPI 外设 */
#define AD3344_SPI                   SPI3
#define AD3344_SPI_RCU               RCU_SPI3
/* SPI 分频。SPI3 挂在 84MHz 总线上，64 分频约 1.3MHz，SCLK 周期约 762ns，
 * 大于手册要求的 250ns 最小周期 */
#define AD3344_SPI_PSC               SPI_PSC_64

/* 片选 CS */
#define AD3344_CS_RCU                RCU_GPIOE
#define AD3344_CS_PORT               GPIOE
#define AD3344_CS_PIN                GPIO_PIN_10

/* 时钟 SCK */
#define AD3344_SCK_RCU               RCU_GPIOE
#define AD3344_SCK_PORT              GPIOE
#define AD3344_SCK_PIN               GPIO_PIN_12
#define AD3344_SCK_AF                GPIO_AF_5

/* 数据输入 MISO（芯片侧是 DOUT/DRDY 复用脚）*/
#define AD3344_MISO_RCU              RCU_GPIOE
#define AD3344_MISO_PORT             GPIOE
#define AD3344_MISO_PIN              GPIO_PIN_13
#define AD3344_MISO_AF               GPIO_AF_5

/* 数据输出 MOSI（芯片侧 DIN）*/
#define AD3344_MOSI_RCU              RCU_GPIOE
#define AD3344_MOSI_PORT             GPIOE
#define AD3344_MOSI_PIN              GPIO_PIN_14
#define AD3344_MOSI_AF               GPIO_AF_5

/* DRDY 外部中断，当前未用，留给以后做 DRDY 触发 */
#define AD3344_EXTI_PORT_SRC         EXTI_SOURCE_GPIOE
#define AD3344_EXTI_PIN_SRC          EXTI_SOURCE_PIN13
#define AD3344_EXTI_LINE             EXTI_13
#define AD3344_EXTI_IRQn             EXTI10_15_IRQn

/* CS 控制，直接写 BOP/BC 寄存器，比 gpio_bit_write() 快一个数量级 */
#define SPI_SET_CS()  (GPIO_BOP(AD3344_CS_PORT) = AD3344_CS_PIN)
#define SPI_CLR_CS()  (GPIO_BC(AD3344_CS_PORT)  = AD3344_CS_PIN)


/*==============================================================================
 *【3】寄存器定义 —— 照抄数据手册 7.6 节，不要改
 *============================================================================*/

/* 通道编号，供 API 使用 */
#define AD3344_CH0                  0
#define AD3344_CH1                  1
#define AD3344_CH2                  2
#define AD3344_CH3                  3
#define AD3344_CH_NUM               4

/* 输入形式 */
#define AD3344_DUAL_END             0       /* 差分 */
#define AD3344_SINGLE_END           1       /* 单端（本板用这个）*/

/* 复位值 */
#define AD3344_CONVERSION_DEFAULT   ((uint16_t)0x0000)
#define AD3344_CONFIG_DEFAULT       ((uint16_t)0x058B)

/* ---- 配置寄存器位域（手册表 7 / 表 8）----
 * | 15 | 14:12    | 11:9     |  8   | 7:5    |  4  |    3      |  2:1   |  0  |
 * | OS | MUX[2:0] | PGA[2:0] | MODE | DR[2:0]| Rsv | PULL_UP_EN| NOP[1:0]| Rsv |
 */

/* bit15 OS：单次转换启动位。写 1 = 在掉电状态下启动一次转换；转换中写无效；读回恒为 0 */
#define AD3344_REG_CONFIG_OS_MASK           (0x8000)
#define AD3344_REG_CONFIG_OS_SINGLE         (0x8000)
#define AD3344_REG_CONFIG_OS_NONE           (0x0000)

/* bit14:12 MUX：输入通道选择 */
#define AD3344_REG_CONFIG_MUX_MASK          (0x7000)
#define AD3344_REG_CONFIG_MUX_DIFF_0_1      (0x0000)    /* 差分 AINP=AIN0 AINN=AIN1（复位值）*/
#define AD3344_REG_CONFIG_MUX_DIFF_0_3      (0x1000)    /* 差分 AINP=AIN0 AINN=AIN3 */
#define AD3344_REG_CONFIG_MUX_DIFF_1_3      (0x2000)    /* 差分 AINP=AIN1 AINN=AIN3 */
#define AD3344_REG_CONFIG_MUX_DIFF_2_3      (0x3000)    /* 差分 AINP=AIN2 AINN=AIN3 */
#define AD3344_REG_CONFIG_MUX_SINGLE_0      (0x4000)    /* 单端 AIN0 对 GND */
#define AD3344_REG_CONFIG_MUX_SINGLE_1      (0x5000)    /* 单端 AIN1 对 GND */
#define AD3344_REG_CONFIG_MUX_SINGLE_2      (0x6000)    /* 单端 AIN2 对 GND */
#define AD3344_REG_CONFIG_MUX_SINGLE_3      (0x7000)    /* 单端 AIN3 对 GND */

/* bit11:9 PGA：满量程范围 FSR。手册表 3 给了对应的 LSB 大小 */
#define AD3344_REG_CONFIG_PGA_MASK          (0x0E00)
#define AD3344_REG_CONFIG_PGA_6_144V        (0x0000)    /* LSB 187.5uV */
#define AD3344_REG_CONFIG_PGA_4_096V        (0x0200)    /* LSB 125uV   */
#define AD3344_REG_CONFIG_PGA_2_048V        (0x0400)    /* LSB 62.5uV （复位值）*/
#define AD3344_REG_CONFIG_PGA_1_024V        (0x0600)    /* LSB 31.25uV */
#define AD3344_REG_CONFIG_PGA_0_512V        (0x0800)    /* LSB 15.625uV*/
#define AD3344_REG_CONFIG_PGA_0_256V        (0x0A00)    /* LSB 7.8125uV*/
#define AD3344_REG_CONFIG_PGA_0_064V        (0x0C00)    /* LSB 1.9531uV*/

/* bit8 MODE：工作模式 */
#define AD3344_REG_CONFIG_MODE_MASK         (0x0100)
#define AD3344_REG_CONFIG_MODE_CONTIN       (0x0000)    /* 连续转换 */
#define AD3344_REG_CONFIG_MODE_SINGLE       (0x0100)    /* 掉电 + 单次（复位值）*/

/* bit7:5 DR：数据速率 */
#define AD3344_REG_CONFIG_DR_MASK           (0x00E0)
#define AD3344_REG_CONFIG_DR_6_25SPS        (0x0000)
#define AD3344_REG_CONFIG_DR_12_5SPS        (0x0020)
#define AD3344_REG_CONFIG_DR_25SPS          (0x0040)
#define AD3344_REG_CONFIG_DR_50SPS          (0x0060)
#define AD3344_REG_CONFIG_DR_100SPS         (0x0080)    /* 复位值 */
#define AD3344_REG_CONFIG_DR_250SPS         (0x00A0)
#define AD3344_REG_CONFIG_DR_500SPS         (0x00C0)
#define AD3344_REG_CONFIG_DR_1000SPS        (0x00E0)

/* bit3 PULL_UP_EN：DOUT/DRDY 脚上的 400kΩ 弱上拉，仅 CS 为高时起作用 */
#define AD3344_REG_CONFIG_PULL_UP_MASK      (0x0008)
#define AD3344_REG_CONFIG_PULL_UP_DIS       (0x0000)
#define AD3344_REG_CONFIG_PULL_UP_EN        (0x0008)    /* 复位值 */

/* bit2:1 NOP：只有写 01 芯片才会真的更新配置寄存器，其余值一律当作"这次不改配置" */
#define AD3344_REG_CONFIG_NOP_MASK          (0x0006)
#define AD3344_REG_CONFIG_NOP_INVALID       (0x0000)
#define AD3344_REG_CONFIG_NOP_VALID         (0x0002)    /* 复位值 */

/* bit0 Reserved：写什么都无效，读回恒为 1 */
#define AD3344_CONFIG_RESERVED_MASK         (0x0001)

/* 纯读取用的 DIN 内容：NOP=00 → 芯片不会更新配置，只把结果移出来 */
#define AD3344_DIN_READ_ONLY                (0x0000)

/* 厂商模式寄存器(MMR)钥匙，手册未记载，见【1.10】 */
#define AD3344_MMR_WRITE_KEY                (0x8100)
#define AD3344_MMR_READ_KEY                 (0x8106)
#define AD3344_MMR_REG_CAL                  (0x0012)
#define AD3344_MMR_REG_REF                  (0x0014)
#define AD3344_MMR_CAL_VALUE                (0xACCA)
#define AD3344_MMR_REF_EXT_BIT              (0x0040)


/*==============================================================================
 *【4】对外 API
 *============================================================================*/
#ifdef __cplusplus
extern "C" {
#endif

/*--- 4.1 初始化 ---------------------------------------------------------------*/
/* SPI + GPIO + 芯片配置。开了 AD3344_TIM9_SCAN_ENABLE 时，bsp_tim9_init() 要排在它之后 */
void  bsp_ad3344_init(void);

/*--- 4.2 前台阻塞读取（单通道）-------------------------------------------------
 * 返回引脚电压（V），不是工程量。工程量已在读取时按【1.8】算好写进
 * Data_class_structure，要用直接读结构体或用 4.4 的 ad3344_get_xxx()。
 *
 * 单次模式耗时约 = 转换时间 + 余量（500SPS 约 2.5ms）；
 * 连续模式约 = (AD3344_CONT_DISCARD + 1) 个转换周期。
 * 阻塞，不要在中断里调。调用期间占住 SPI 总线，后台扫描自动让路。 */
float ad3344_read_ch0(void);
float ad3344_read_ch1(void);
float ad3344_read_ch2(void);
float ad3344_read_ch3(void);

/* 通道号版本，ch 取 AD3344_CH0..AD3344_CH3，越界返回 0.0f */
float ad3344_read_channel(uint8_t ch);

/*--- 4.2b 一次读回 CH0/CH1/CH2 三个通道 -----------------------------------------
 * 三个通道连着采，共用一次总线占用，省掉两次抢总线的开销。
 * 任一指针传 NULL 表示不需要该通道的值。耗时约单通道的 3 倍（500SPS 约 7.5ms）。 */
void  ad3344_read_ch012(float *v0, float *v1, float *v2);

/*--- 4.3 后台 10ms 扫描 --------------------------------------------------------*/
/* 放在 TIMER9 中断（bsp_general_timer.c）里，10ms 一次。内部只做一次 16 位 SPI
 * 传输（约 12us），不阻塞。AD3344_TIM9_SCAN_ENABLE=0 时是空函数，可照样留在中断里 */
void  ad3344_tim9_10ms_isr(void);

/* 取后台扫描缓存的最新电压（V），不访问 SPI */
float ad3344_get_ch_volt(uint8_t ch);
/* 取后台扫描缓存的最新原始码（二进制补码）*/
int16_t ad3344_get_ch_raw(uint8_t ch);
/* 该通道是否已被扫描填过至少一次 */
uint8_t ad3344_ch_is_valid(uint8_t ch);

/* 主循环里调，AD3344_DEBUG_SCAN==1 时负责打印，其余情况是空函数 */
void  ad3344_debug_poll(void);

/*--- 4.4 工程量换算 -------------------------------------------------------------
 * 纯计算，不碰 SPI。系数见【1.8】 */
/* CH0 采样电阻电压 → 电流（默认 mA，见 AD3344_CH0_I_UNIT_K），已套过【1.8】的
 * 标定直线。全驱动唯一的电压→电流换算点，i0_current / Modbus 点表 / 断线判据 /
 * 调试打印拿到的都是它的输出 */
float ad3344_volt_to_current(float volt);

/* 同上但不做标定修正，纯理想公式 I = V / R × 单位系数。
 * 标定取数时用，算 K/B 要的就是这个未修正值 */
float ad3344_volt_to_current_raw(float volt);
/* CH1/CH2 分压后电压 → 实际输入电压（V）。ratio 传 AD3344_CHx_DIV_RATIO */
float ad3344_volt_to_real(float volt, float ratio);
/* CH0 上 PT100 的电压 → 温度（℃），系数见【1.7】。PT100 不考察，此路保留 */
float ad3344_volt_to_pt100_temp(float volt);

/*--- 4.4b 直接取工程量 -----------------------------------------------------------
 * 取驱动内部缓存换算出来的值。
 *   开了后台扫描 → 缓存由 TIM9 每 30ms 刷新，是新鲜值，不阻塞
 *   没开后台扫描 → 是最后一次读取时的值。bsp_ad3344_init() 的上电自检读过四路，
 *                  所以不会返回 0，但也不会自己更新，要新数据先调 ad3344_read_chX()
 * 同样的值也在 Data_class_structure 的 i0_current / v0_voltage / v1_voltage 里。 */
float ad3344_get_current(void);     /* CH0 电流，默认 mA */
float ad3344_get_v0(void);          /* CH1 还原后的实际电压 V */
float ad3344_get_v1(void);          /* CH2 还原后的实际电压 V */
float ad3344_get_pt100_temp(void);  /* CH0 温度 ℃（遗留） */

/*--- 4.5 4~20mA 断线检测 ---------------------------------------------------------
 * 阈值、回差、消抖次数见【1.9】。AD3344_BROKEN_DET_ENABLE=0 时下面几个函数仍然
 * 存在、可调，只是永远返回未断线，上层代码不用跟着加 #if。 */

/* 路径 a：取消抖后锁存的断线状态，1=断线 0=正常。状态由后台 10ms 扫描（以及任何
 * 一次前台读 CH0）维护，本函数只读变量，不碰 SPI。
 * 同一个值也在 Data_class_structure.i0_broken，Modbus 点表取的是那个 */
uint8_t ad3344_is_broken(void);

/* 路径 b：阻塞采一次 CH0 立刻给结论，1=断线 0=正常，判据是这一个样本的电流
 * < AD3344_BROKEN_TH，不走消抖。
 * current 非 NULL 时把这次的电流值（默认 mA）带出去，免得再采一次。
 * 这个样本同样喂给消抖状态机，不会让 ad3344_is_broken() 的锁存状态脱节。
 * 耗时 = 一次阻塞读取（500SPS 约 2.5ms），别在中断里调 */
uint8_t ad3344_broken_check_now(float *current);

/* 纯判据，不采样：按【1.9】的阈值判断给定电流算不算断线 */
uint8_t ad3344_current_is_broken(float current);

/* 消抖状态机清回 AD3344_BROKEN_INIT_STATE 并清零计数。
 * 换接线、变送器重新上电之后调一下，免得旧状态留着 */
void    ad3344_broken_reset(void);

/* 软复位：把配置寄存器写回上电默认值 0x058B */
void  ad3344_reset(void);
/* 停止连续转换：把 MODE 位切回单次，芯片做完当前这次后进掉电 */
void  ad3344_stop_conversion(void);

/* AD3344_TRANS_32BIT=1 时取最近一次 32 位周期回读到的配置寄存器值，
 * 16 位周期下恒为 0。排查配置字有没有写进芯片用 */
uint16_t ad3344_last_cfg_readback(void);

/* 微秒级软件延时。基于 NOP 循环估算，不是精确时基 */
void  delay_us(uint32_t t);

#ifdef __cplusplus
}
#endif


/*==============================================================================
 *【5】由【1】区推导出来的量，不用改
 *============================================================================*/

/* 转换时间（微秒）= 1/DR */
#if   (AD3344_DR_SEL == AD3344_REG_CONFIG_DR_6_25SPS)
    #define AD3344_CONV_TIME_US     160000
#elif (AD3344_DR_SEL == AD3344_REG_CONFIG_DR_12_5SPS)
    #define AD3344_CONV_TIME_US     80000
#elif (AD3344_DR_SEL == AD3344_REG_CONFIG_DR_25SPS)
    #define AD3344_CONV_TIME_US     40000
#elif (AD3344_DR_SEL == AD3344_REG_CONFIG_DR_50SPS)
    #define AD3344_CONV_TIME_US     20000
#elif (AD3344_DR_SEL == AD3344_REG_CONFIG_DR_100SPS)
    #define AD3344_CONV_TIME_US     10000
#elif (AD3344_DR_SEL == AD3344_REG_CONFIG_DR_250SPS)
    #define AD3344_CONV_TIME_US     4000
#elif (AD3344_DR_SEL == AD3344_REG_CONFIG_DR_500SPS)
    #define AD3344_CONV_TIME_US     2000
#elif (AD3344_DR_SEL == AD3344_REG_CONFIG_DR_1000SPS)
    #define AD3344_CONV_TIME_US     1000
#else
    /* AD3344_DR_SEL 只能取 AD3344_REG_CONFIG_DR_xxx 里的一个 */
    #error "AD3344: invalid AD3344_DR_SEL, use one of the AD3344_REG_CONFIG_DR_xxx macros"
#endif

/* 一次阻塞读取实际要等的时间 */
#define AD3344_WAIT_US              (AD3344_CONV_TIME_US + AD3344_CONV_MARGIN_US)

/* 定时器扫描周期（微秒），跟着 TIMER9 的 10ms 走 */
#define AD3344_SCAN_TICK_US         10000


/*==============================================================================
 *【6】编译期检查 —— 参数配错在编译期就报错，不用等烧进板子
 *
 * 报错文字用 ASCII：armcc 把源文件里的字节原样吐进 Keil 的 Build Output，
 * 那个窗口按系统本地代码页（中文 Windows 是 GBK）渲染，UTF-8 中文进去是乱码。
 * 中文解释写在每条检查上面。
 *============================================================================*/

/* 开了 10ms 扫描，转换必须在一个 tick 内做完，否则读回来的是上上次的旧值。
 * 触发 → AD3344_DR_SEL 调到 250SPS 或更高（100SPS 转换正好 10ms，卡满不安全）*/
#if (AD3344_TIM9_SCAN_ENABLE != 0) && (AD3344_WAIT_US > AD3344_SCAN_TICK_US)
    #error "AD3344: conversion slower than the 10ms scan tick -- raise AD3344_DR_SEL to 250SPS or faster"
#endif

/* 扫描通道掩码不能为空。
 * 触发 → AD3344_SCAN_CH_MASK 至少选一个通道，或关掉 AD3344_TIM9_SCAN_ENABLE */
#if (AD3344_TIM9_SCAN_ENABLE != 0) && ((AD3344_SCAN_CH_MASK & 0x0F) == 0)
    #error "AD3344: TIM9 scan enabled but AD3344_SCAN_CH_MASK selects no channel"
#endif

/* AIN3 做了外部基准就不能再当采样通道。
 * 触发 → 从 AD3344_SCAN_CH_MASK 去掉 AD3344_CH3_BIT，或关掉 AD3344_EXT_REF_ENABLE */
#if (AD3344_EXT_REF_ENABLE != 0) && ((AD3344_SCAN_CH_MASK & AD3344_CH3_BIT) != 0)
    #error "AD3344: AIN3 is used as external reference, remove AD3344_CH3_BIT from AD3344_SCAN_CH_MASK"
#endif

/* 消抖次数必须在 1~255：填 0 状态机永远翻不了，填 >255 内部 uint8_t 计数器会绕回，
 * 同样翻不了。填 1 = 不消抖 */
#if (AD3344_BROKEN_DET_ENABLE != 0) && \
    ((AD3344_BROKEN_COUNT < 1) || (AD3344_BROKEN_COUNT > 255) || \
     (AD3344_BROKEN_CLEAR_COUNT < 1) || (AD3344_BROKEN_CLEAR_COUNT > 255))
    #error "AD3344: AD3344_BROKEN_COUNT / AD3344_BROKEN_CLEAR_COUNT must be in 1..255"
#endif

/* 回差方向必须是 AD3344_BROKEN_CLEAR_TH > AD3344_BROKEN_TH，反了状态机会在两个
 * 阈值间来回抖，相等则等于没回差。
 * 这条没法用 #if 检查：#if 的表达式只能是整型常量表达式，预处理器不认识 3.5f。
 * 改在 bsp_ad3344_init() 里运行时校验，配反了串口打一行
 * "BROKEN THRESHOLD MISCONFIGURED"。 */

/* 开了断线检测，CH0 必须被采到，否则状态机收不到样本。扫描表里没有 CH0 时只能靠
 * ad3344_broken_check_now() 手动喂，多半是漏配。
 * 确实只想用手动单次判断，就关掉 AD3344_TIM9_SCAN_ENABLE。 */
#if (AD3344_BROKEN_DET_ENABLE != 0) && (AD3344_TIM9_SCAN_ENABLE != 0) && \
    ((AD3344_SCAN_CH_MASK & AD3344_CH0_BIT) == 0)
    #error "AD3344: broken-wire detection needs CH0 sampled -- add AD3344_CH0_BIT to AD3344_SCAN_CH_MASK"
#endif

/* 标定打印的样本由后台扫描收集，扫描关掉就一行都不会打。
 * 触发 → 打开 AD3344_TIM9_SCAN_ENABLE，或关掉 AD3344_CAL_MODE */
#if (AD3344_CAL_MODE != 0) && (AD3344_TIM9_SCAN_ENABLE == 0)
    #error "AD3344: AD3344_CAL_MODE needs AD3344_TIM9_SCAN_ENABLE=1 to collect samples"
#endif

/* 标定打印也受 AD3344_DEBUG_EN 管，关着同样一行都没有 */
#if (AD3344_CAL_MODE != 0) && (AD3344_DEBUG_EN == 0)
    #error "AD3344: AD3344_CAL_MODE needs AD3344_DEBUG_EN=1 to print anything"
#endif

/* 平均样本数：填 0 会除零；上限 4096 是 int32 累加器的余量
 * （单个原始码最大 32768，32768 × 4096 = 2^27）*/
#if (AD3344_CAL_MODE != 0) && ((AD3344_CAL_AVG_N < 1) || (AD3344_CAL_AVG_N > 4096))
    #error "AD3344: AD3344_CAL_AVG_N must be in 1..4096"
#endif

/* AD3344_I0_CAL_K 不能为 0，否则电流恒等于 B。同回差那条，#if 不认浮点常量，
 * 在 bsp_ad3344_init() 里运行时检查 */

#endif /* __BSP_GD30AD3344_H */
