#include "bsp_GD30AD3344.h"


/*==============================================================================
 * 调试打印宏
 *============================================================================*/
#if (AD3344_DEBUG_EN != 0)
    #define AD_DBG(...)         printf(__VA_ARGS__)
#else
    #define AD_DBG(...)         ((void)0)
#endif

#if (AD3344_DEBUG_EN != 0) && (AD3344_DEBUG_READ != 0)
    #define AD_DBG_READ(...)    printf(__VA_ARGS__)
#else
    #define AD_DBG_READ(...)    ((void)0)
#endif

/* 工作模式名，只给调试打印用 */
#if (AD3344_CONV_MODE == AD3344_MODE_CONTINUOUS)
    #define AD3344_MODE_NAME    "continuous"
#else
    #define AD3344_MODE_NAME    "single-shot"
#endif


/*==============================================================================
 * 内部状态
 *============================================================================*/

/* SPI 总线占用标志。前台阻塞读取期间置 1，后台 10ms 扫描中断看到 1 就整拍跳过。
 * 于是只有两种时序：中断跑完前台才开始，或中断直接跳过，都不会抢总线 */
static volatile uint8_t s_bus_busy = 0;

/* 后台扫描缓存 */
static volatile int16_t s_ch_raw[AD3344_CH_NUM]   = {0};
static volatile float   s_ch_volt[AD3344_CH_NUM]  = {0.0f};
static volatile uint8_t s_ch_valid[AD3344_CH_NUM] = {0};

/* 扫描状态机：
 *   s_scan_armed  芯片上正在转换的通道，即下一拍读回来的数据属于谁。
 *                 -1 = 流水线空（刚开机或刚被前台打断），这一拍的数据要丢掉
 *   s_scan_slot   在扫描通道表里的位置，用来轮转 */
static volatile int8_t   s_scan_armed = -1;
static volatile uint8_t  s_scan_slot  = 0;
static volatile uint32_t s_scan_round = 0;   /* 完成的扫描轮数，调试打印抽稀用 */
static volatile uint8_t  s_scan_print_req = 0;

/* 32 位周期下回读到的配置寄存器值 */
static volatile uint16_t s_cfg_readback = 0;

#if (AD3344_DEBUG_EN != 0) && (AD3344_CAL_MODE != 0) && (AD3344_TIM9_SCAN_ENABLE != 0)
/* 【1.11】标定模式的采样累加器。
 * 平均在原始码而不是电压上做：整数加法在中断里开销可忽略，也避免 32 次浮点累加
 * 的舍入误差，攒够一批再一次性换算。int32 累加器：单码最大 32768，乘 4096 才 2^27。
 * 中断里只累加 + 置标志，printf 留给主循环 ad3344_debug_poll()。 */
static volatile int32_t  s_cal_acc     = 0;   /* 当前这一批的累加和   */
static volatile uint16_t s_cal_cnt     = 0;   /* 当前这一批已攒样本数 */
static volatile int32_t  s_cal_sum_out = 0;   /* 攒满后交给主循环的快照 */
static volatile uint16_t s_cal_n_out   = 0;
static volatile uint8_t  s_cal_req     = 0;   /* 1 = 有一批数据等着打印 */
#endif

#if (AD3344_TIM9_SCAN_ENABLE != 0)
/* 把 AD3344_SCAN_CH_MASK 展开成通道号数组，中断里就不用每次解位掩码 */
static const uint8_t s_scan_tab[] = {
#if (AD3344_SCAN_CH_MASK & AD3344_CH0_BIT)
    AD3344_CH0,
#endif
#if (AD3344_SCAN_CH_MASK & AD3344_CH1_BIT)
    AD3344_CH1,
#endif
#if (AD3344_SCAN_CH_MASK & AD3344_CH2_BIT)
    AD3344_CH2,
#endif
#if (AD3344_SCAN_CH_MASK & AD3344_CH3_BIT)
    AD3344_CH3,
#endif
    0   /* 掩码为空时保证数组非空能编译过，这种配置会先被【6】区的 #error 拦下 */
};
#define AD3344_SCAN_TAB_LEN     (sizeof(s_scan_tab) / sizeof(s_scan_tab[0]) - 1)
#endif  /* AD3344_TIM9_SCAN_ENABLE */


/*==============================================================================
 * 软件延时
 *
 * 循环体每轮约 8 个时钟，168MHz 下 20 轮 ≈ 1us。volatile 防止被优化掉。
 * 估算值，不是精确时基。只用于等转换完成这类带大余量的等待，SPI 时序靠硬件保证。
 *============================================================================*/
void delay_us(uint32_t t)
{
    __IO uint32_t i;
    while (t--) {
        i = 20;
        while (i--) {
            __NOP();
        }
    }
}


/*==============================================================================
 * SPI 底层
 *============================================================================*/

/* SPI + GPIO 初始化。CS 用推挽输出软件控制（软件 NSS），SCK/MISO/MOSI 走 AF5。
 * 帧长 16 位，模式 1（CPOL=0 / CPHA=1），MSB 先行，见手册 8.1 节 */
static void ad3344_spi_hw_init(void)
{
    spi_parameter_struct spi_init_struct;

    rcu_periph_clock_enable(AD3344_CS_RCU);
    rcu_periph_clock_enable(AD3344_SCK_RCU);
    rcu_periph_clock_enable(AD3344_MISO_RCU);
    rcu_periph_clock_enable(AD3344_MOSI_RCU);
    rcu_periph_clock_enable(AD3344_SPI_RCU);

    /* CS 默认拉高。手册 7.5.2：CS 拉高会复位 SPI 接口，空闲保持高才是正确状态 */
    gpio_mode_set(AD3344_CS_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, AD3344_CS_PIN);
    gpio_output_options_set(AD3344_CS_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, AD3344_CS_PIN);
    SPI_SET_CS();

    gpio_af_set(AD3344_SCK_PORT,  AD3344_SCK_AF,  AD3344_SCK_PIN);
    gpio_af_set(AD3344_MISO_PORT, AD3344_MISO_AF, AD3344_MISO_PIN);
    gpio_af_set(AD3344_MOSI_PORT, AD3344_MOSI_AF, AD3344_MOSI_PIN);

    gpio_mode_set(AD3344_SCK_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, AD3344_SCK_PIN);
    gpio_output_options_set(AD3344_SCK_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, AD3344_SCK_PIN);

    /* MISO 接芯片 DOUT/DRDY，芯片侧已有 400kΩ 弱上拉（PULL_UP_EN 默认开），
     * 不再叠加 MCU 内部上拉 */
    gpio_mode_set(AD3344_MISO_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, AD3344_MISO_PIN);
    gpio_output_options_set(AD3344_MISO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, AD3344_MISO_PIN);

    gpio_mode_set(AD3344_MOSI_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, AD3344_MOSI_PIN);
    gpio_output_options_set(AD3344_MOSI_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, AD3344_MOSI_PIN);

    spi_i2s_deinit(AD3344_SPI);
    spi_struct_para_init(&spi_init_struct);

    spi_init_struct.trans_mode           = SPI_TRANSMODE_FULLDUPLEX;
    spi_init_struct.device_mode          = SPI_MASTER;
    spi_init_struct.frame_size           = SPI_FRAMESIZE_16BIT;
    spi_init_struct.clock_polarity_phase = SPI_CK_PL_LOW_PH_2EDGE;  /* 模式1 */
    spi_init_struct.nss                  = SPI_NSS_SOFT;
    spi_init_struct.prescale             = AD3344_SPI_PSC;
    spi_init_struct.endian               = SPI_ENDIAN_MSB;
    spi_init(AD3344_SPI, &spi_init_struct);

    spi_enable(AD3344_SPI);
}


/* 一帧 16 位收发，阻塞到收完。1.3MHz 下约 12us。 */
static uint16_t ad3344_spi_txrx16(uint16_t tx)
{
    while (RESET == spi_i2s_flag_get(AD3344_SPI, SPI_FLAG_TBE));
    spi_i2s_data_transmit(AD3344_SPI, tx);
    while (RESET == spi_i2s_flag_get(AD3344_SPI, SPI_FLAG_RBNE));
    return spi_i2s_data_receive(AD3344_SPI);
}


/*----------------------------------------------------------------------------
 * 一个完整的数据传输周期，整个驱动唯一和芯片打交道的函数
 *
 * din 送进去的 16 位是新配置字（NOP=01 时生效），返回值同时是芯片里上一次已完成
 * 的转换结果（手册 7.5.7）。一次传输 = 读上一个 + 启动下一个就来自这一点。
 *
 * AD3344_TRANS_32BIT=1 时多跑一个 16 位周期回读配置寄存器（手册 7.5.8：32 位周期
 * 的后两个字节回读前两个字节写进去的配置）。
 *
 * CS 每次必须完整地低→传输→高。手册 7.5.9：16 位周期结束后拉高 CS 会复位 SPI
 * 接口，下次拉低才是干净的新周期。留着 CS 不拉高会让下次传输落在一个已开始的
 * 周期中间。
 *--------------------------------------------------------------------------*/
static uint16_t ad3344_xfer(uint16_t din)
{
    uint16_t conv;

    SPI_CLR_CS();
    delay_us(1);                    /* tCSSC 最小 100ns，给到 1us 足够 */

    conv = ad3344_spi_txrx16(din);

#if (AD3344_TRANS_32BIT != 0)
    /* 后半个 32 位周期。不改配置就送 NOP=00，回读出来的是当前配置寄存器 */
    s_cfg_readback = ad3344_spi_txrx16(AD3344_DIN_READ_ONLY);
#endif

    delay_us(1);                    /* tSCCS 最小 100ns */
    SPI_SET_CS();
    delay_us(1);                    /* tCSH 最小 200ns */

    return conv;
}


/*==============================================================================
 * 配置字与换算
 *============================================================================*/

/* 取通道的 PGA 位。用 switch 而非数组，让【1.3】那四个宏参与编译期常量折叠，
 * 改宏立刻生效，不会有别处的副本没跟上 */
static uint16_t ad3344_pga_bits(uint8_t ch)
{
    switch (ch) {
        case AD3344_CH0: return AD3344_PGA_CH0;
        case AD3344_CH1: return AD3344_PGA_CH1;
        case AD3344_CH2: return AD3344_PGA_CH2;
        case AD3344_CH3: return AD3344_PGA_CH3;
        default:         return AD3344_PGA_SEL;
    }
}

/* PGA 位 → 满量程电压。手册表 3 */
static float ad3344_pga_fsr(uint16_t pga_bits)
{
    switch (pga_bits & AD3344_REG_CONFIG_PGA_MASK) {
        case AD3344_REG_CONFIG_PGA_6_144V: return 6.144f;
        case AD3344_REG_CONFIG_PGA_4_096V: return 4.096f;
        case AD3344_REG_CONFIG_PGA_2_048V: return 2.048f;
        case AD3344_REG_CONFIG_PGA_1_024V: return 1.024f;
        case AD3344_REG_CONFIG_PGA_0_512V: return 0.512f;
        case AD3344_REG_CONFIG_PGA_0_256V: return 0.256f;
        default:                           return 0.064f;
    }
}

/* 单端通道的 MUX 位 */
static uint16_t ad3344_mux_bits(uint8_t ch)
{
    switch (ch) {
        case AD3344_CH0: return AD3344_REG_CONFIG_MUX_SINGLE_0;
        case AD3344_CH1: return AD3344_REG_CONFIG_MUX_SINGLE_1;
        case AD3344_CH2: return AD3344_REG_CONFIG_MUX_SINGLE_2;
        default:         return AD3344_REG_CONFIG_MUX_SINGLE_3;
    }
}

/*----------------------------------------------------------------------------
 * 组一个配置字
 *   ch         : 单端通道 0~3
 *   start_conv : 1 = 带上 OS=1，写进去就启动一次单次转换
 *
 * NOP 恒为 01，否则芯片把整个字当无效数据丢掉（手册表 8，bit2:1）。
 * MODE 跟随【1.1】的 AD3344_CONV_MODE。连续模式下芯片一直在转，OS 无意义，不带。
 *--------------------------------------------------------------------------*/
static uint16_t ad3344_make_cfg(uint8_t ch, uint8_t start_conv)
{
    uint16_t cfg = 0;

    cfg |= ad3344_mux_bits(ch);
    cfg |= ad3344_pga_bits(ch);
    cfg |= AD3344_DR_SEL;
    cfg |= AD3344_REG_CONFIG_PULL_UP_EN;
    cfg |= AD3344_REG_CONFIG_NOP_VALID;

#if (AD3344_CONV_MODE == AD3344_MODE_CONTINUOUS)
    cfg |= AD3344_REG_CONFIG_MODE_CONTIN;
    (void)start_conv;               /* 连续模式下芯片自己转，OS 无意义 */
#else
    cfg |= AD3344_REG_CONFIG_MODE_SINGLE;
    if (start_conv) {
        cfg |= AD3344_REG_CONFIG_OS_SINGLE;
    }
#endif

    return cfg;
}

/* 后台扫描专用配置字：不管【1.1】怎么设，扫描固定用单次 + OS=1。
 * 连续模式的转换节奏和 10ms 定时器不同步，见头文件【1.2】 */
static uint16_t ad3344_make_scan_cfg(uint8_t ch)
{
    uint16_t cfg = 0;

    cfg |= ad3344_mux_bits(ch);
    cfg |= ad3344_pga_bits(ch);
    cfg |= AD3344_DR_SEL;
    cfg |= AD3344_REG_CONFIG_MODE_SINGLE;
    cfg |= AD3344_REG_CONFIG_OS_SINGLE;
    cfg |= AD3344_REG_CONFIG_PULL_UP_EN;
    cfg |= AD3344_REG_CONFIG_NOP_VALID;

    return cfg;
}

/*----------------------------------------------------------------------------
 * 原始码 → 电压
 *
 * 手册 7.5.6：16 位二进制补码，+FS→0x7FFF，−FS→0x8000。
 * 直接强转 int16_t 借用 C 的补码语义。0x8000 单独拦一下，避免 -32768/32768 这个
 * 不对称点算出超量程的值。
 *
 * FSR 由该通道的 PGA 宏现算，不经全局变量，改量程宏不会漏掉某个读取路径。
 *--------------------------------------------------------------------------*/
static float ad3344_raw_to_volt(int16_t raw, uint8_t ch)
{
    float fsr = ad3344_pga_fsr(ad3344_pga_bits(ch));

    if (raw == (int16_t)0x8000) {
        return -fsr;
    }
    return (fsr * (float)raw) / 32768.0f;
}


/*==============================================================================
 * 工程量换算 —— 引脚电压 → 实际物理量
 * 系数全部来自头文件【1.8】，这里不出现任何裸数字
 *============================================================================*/

/* CH0：采样电阻上的电压 → 电流（理想公式，不含标定修正）。
 * I = U / R，再乘单位系数（默认 1000 → 毫安）。
 * 91Ω 时：0.364V → 4.00mA，1.820V → 20.00mA
 * 标定取数看的就是这一份输出，K/B 的求法见头文件【1.8】 */
float ad3344_volt_to_current_raw(float volt)
{
    return (volt / AD3344_CH0_SENSE_R) * AD3344_CH0_I_UNIT_K;
}

/* CH0：修正后的电流，全驱动唯一对外的电压→电流换算点。
 * 在理想公式外套一条 I = K × I + B 的直线（系数见【1.8】），一并修掉采样电阻阻值
 * 偏差、回流共地压降（增益项）和 ADC 失调、前级漏电流（偏置项）。
 * 修正放在这里而不是 ad3344_dispatch_to_struct()：i0_current、Modbus 点表、断线
 * 判据、调试打印都经过这个函数，不会出现结构体是修正值而断线判据用原始值。 */
float ad3344_volt_to_current(float volt)
{
#if (AD3344_I0_CAL_ENABLE != 0)
    return AD3344_I0_CAL_K * ad3344_volt_to_current_raw(volt) + AD3344_I0_CAL_B;
#else
    return ad3344_volt_to_current_raw(volt);
#endif
}

/* CH1 / CH2：分压后的电压 → 实际输入电压。
 * ratio 是硬件分压比（实际电压乘它才是 ADC 看到的电压），所以这里是除。
 * 0.18 时 1.80V → 10.00V。ratio 为 0 返回原值，避免配错宏时除零 */
float ad3344_volt_to_real(float volt, float ratio)
{
    if (ratio == 0.0f) {
        return volt;
    }
    return volt / ratio;
}

/* CH0 上 PT100 的电压 → 温度。两点标定，系数见【1.7】。PT100 不考察，此路保留 */
float ad3344_volt_to_pt100_temp(float volt)
{
    return AD3344_PT100_K * volt + AD3344_PT100_B;
}


/*==============================================================================
 * 4~20mA 断线检测（CH0）：带回差 + 消抖的状态机，参数见头文件【1.9】
 *
 * I 为本次采到的电流：
 *
 *          I < BROKEN_TH 连续 BROKEN_COUNT 次
 *      ┌──────────────────────────────────────────┐
 *      │                                          ▼
 *  ┌────────┐                                ┌────────┐
 *  │  正常  │                                │  断线  │
 *  └────────┘                                └────────┘
 *      ▲                                          │
 *      └──────────────────────────────────────────┘
 *          I > CLEAR_TH 连续 CLEAR_COUNT 次
 *
 * 两个阈值之间（3.5 ~ 3.8mA）是回差带，落进去不改状态，防止信号停在阈值上时标志
 * 来回翻。计数要求连续 N 次，中间有一次不满足就清零重来。
 *============================================================================*/

#if (AD3344_BROKEN_DET_ENABLE != 0)

/* 锁存的断线状态。ISR 和前台都会改，但不会同时跑（前台持总线时 ISR 整拍跳过），
 * 不需要临界区，volatile 保证可见性即可 */
static volatile uint8_t s_broken_state = AD3344_BROKEN_INIT_STATE;
/* 消抖计数器。当前状态决定它在数偏低还是数正常，一个计数器够用 */
static volatile uint8_t s_broken_cnt   = 0;

#if (AD3344_DEBUG_EN != 0) && (AD3344_BROKEN_DEBUG != 0)
/* 状态翻转事件：中断里只记一笔，主循环 ad3344_debug_poll() 里才打印 */
static volatile uint8_t s_broken_evt      = 0;   /* 1 = 有一次翻转还没打印 */
static volatile uint8_t s_broken_evt_new  = 0;   /* 翻转后的新状态 */
static volatile float   s_broken_evt_cur  = 0.0f;/* 触发翻转时的电流值 */
#endif


/* 纯判据：单看一个电流值算不算断线，不含状态 */
uint8_t ad3344_current_is_broken(float current)
{
    return (current < AD3344_BROKEN_TH) ? 1 : 0;
}


/*----------------------------------------------------------------------------
 * 喂一个新样本给状态机。
 * 后台扫描采 CH0、前台读 CH0、单次断线检查都走这里，消抖逻辑只有这一份。
 *--------------------------------------------------------------------------*/
static void ad3344_broken_update(float current)
{
    uint8_t hit;

    if (s_broken_state == 0) {
        /* 当前认为正常，数连续偏低的次数 */
        hit = (current < AD3344_BROKEN_TH) ? 1 : 0;
    } else {
        /* 当前认为断线，数连续正常的次数。回差体现在这里比的是 CLEAR_TH */
        hit = (current > AD3344_BROKEN_CLEAR_TH) ? 1 : 0;
    }

    if (!hit) {
        s_broken_cnt = 0;       /* 连击断了，重新数 */
        return;
    }

    s_broken_cnt++;

    if ((s_broken_state == 0 && s_broken_cnt >= AD3344_BROKEN_COUNT) ||
        (s_broken_state != 0 && s_broken_cnt >= AD3344_BROKEN_CLEAR_COUNT)) {
        s_broken_state = (uint8_t)(s_broken_state ? 0 : 1);
        s_broken_cnt   = 0;

#if (AD3344_DEBUG_EN != 0) && (AD3344_BROKEN_DEBUG != 0)
        s_broken_evt     = 1;
        s_broken_evt_new = s_broken_state;
        s_broken_evt_cur = current;
#endif
    }
}


uint8_t ad3344_is_broken(void)
{
    return s_broken_state;
}


void ad3344_broken_reset(void)
{
    s_broken_state = AD3344_BROKEN_INIT_STATE;
    s_broken_cnt   = 0;
#if (AD3344_SCAN_WRITE_BACK != 0)
    Data_class_structure.i0_broken = AD3344_BROKEN_INIT_STATE;
#endif
}

#else   /* AD3344_BROKEN_DET_ENABLE == 0 */

/* 功能关掉时留同名空实现，上层调用处不用加 #if */
uint8_t ad3344_current_is_broken(float current) { (void)current; return 0; }
uint8_t ad3344_is_broken(void)                  { return 0; }
void    ad3344_broken_reset(void)               { }
static void ad3344_broken_update(float current) { (void)current; }

#endif  /* AD3344_BROKEN_DET_ENABLE */


/*==============================================================================
 * 采到的电压往 Data_class_structure 里放。改通道对应关系只改这一个函数。
 *
 * 前台阻塞读取（ad3344_read_chX）和后台 10ms 扫描都调它，两条路径写出的结构体
 * 内容一致，不会出现改了扫描忘了改阻塞读。
 *
 * 当前接线（详见头文件【1.8】）：
 *   CH0 → i0_current      电压 / 91Ω × 1000 = 电流 mA
 *         i0_broken       断线状态，由【1.9】的回差+消抖状态机给出
 *         ch2_current_temp  同一电压按 PT100 老标定算的温度，遗留链路，
 *                           和电流换算互不影响，各写各的字段
 *   CH1 → v0_voltage      电压 / 0.18 = 实际 0~10V
 *   CH2 → v1_voltage      电压 / 0.18 = 实际 0~10V
 *   CH3 → 未接，不写
 *============================================================================*/
static void ad3344_dispatch_to_struct(uint8_t ch, float volt)
{
    float eng;

    switch (ch) {
        case AD3344_CH0:
            /* --- 4~20mA 电流 --- */
            eng = ad3344_volt_to_current(volt);

            /* 每采到一次 CH0 就喂一次状态机，放在写 i0_broken 之前，写回去的才是
             * 本次更新后的状态。不受 AD3344_SCAN_WRITE_BACK 影响：上层就算自己搬
             * 数据，消抖也必须连续跑，漏喂样本会让连续 N 次失去意义 */
            ad3344_broken_update(eng);

#if (AD3344_SCAN_WRITE_BACK != 0)
            Data_class_structure.i0_current = eng;
            Data_class_structure.i0_broken  = ad3344_is_broken();
            /* PT100 温度，遗留链路 */
            Data_class_structure.ch2_current_temp = ad3344_volt_to_pt100_temp(volt);
#endif
            break;

#if (AD3344_SCAN_WRITE_BACK != 0)
        case AD3344_CH1:
            Data_class_structure.v0_voltage = ad3344_volt_to_real(volt, AD3344_CH1_DIV_RATIO);
            break;

        case AD3344_CH2:
            Data_class_structure.v1_voltage = ad3344_volt_to_real(volt, AD3344_CH2_DIV_RATIO);
            break;
#endif

        default:
            break;      /* CH3 没接，不写任何字段 */
    }
}


/*----------------------------------------------------------------------------
 * 单次读取路径：阻塞采一次 CH0 立刻判断断线。
 *
 * 返回的是这一个样本的即时判据，不等消抖 —— 上位机点名要答案的场合等不起
 * N × 30ms，也没有 N 个样本可用。
 * 这个样本照样喂给状态机（ad3344_read_channel → dispatch → broken_update），
 * ad3344_is_broken() 的锁存状态不会因此落后。
 *
 * 返回 1 = 本次电流低于 AD3344_BROKEN_TH，判断线；0 = 正常
 * current 非 NULL 时带出本次电流值（默认 mA），省得再采一次
 *--------------------------------------------------------------------------*/
uint8_t ad3344_broken_check_now(float *current)
{
    float volt;
    float cur;
    uint8_t broken;

    volt = ad3344_read_ch0();               /* 阻塞采一次，内部已喂状态机 */
    cur  = ad3344_volt_to_current(volt);
    broken = ad3344_current_is_broken(cur);

    if (current != NULL) {
        *current = cur;
    }

    AD_DBG_READ("[AD3344] broken check: %.3f mA -> %s (latched=%u)\r\n",
                cur, broken ? "BROKEN" : "ok", (unsigned)ad3344_is_broken());

    return broken;
}


/*==============================================================================
 * 厂商模式寄存器(MMR)，手册未记载，见头文件【1.10】
 *============================================================================*/

/* 两个开关有一个开着就要写 MMR（外部基准也靠写 MMR 打开）*/
#if (AD3344_MMR_INIT_ENABLE != 0) || (AD3344_EXT_REF_ENABLE != 0)
/* 写 MMR：写钥匙 → 寄存器号 → 数据，三个 16 位帧夹在同一个 CS 低电平里 */
static void ad3344_mmr_write(uint16_t reg, uint16_t val)
{
    SPI_CLR_CS();
    delay_us(1);
    ad3344_spi_txrx16(AD3344_MMR_WRITE_KEY);
    ad3344_spi_txrx16(reg);
    ad3344_spi_txrx16(val);
    delay_us(1);
    SPI_SET_CS();
    delay_us(1);
}
#endif

#if (AD3344_EXT_REF_ENABLE != 0)
/* 读 MMR：读钥匙 → 寄存器号 → 空帧取回数据 */
static uint16_t ad3344_mmr_read(uint16_t reg)
{
    uint16_t val;

    SPI_CLR_CS();
    delay_us(1);
    ad3344_spi_txrx16(AD3344_MMR_READ_KEY);
    ad3344_spi_txrx16(reg);
    val = ad3344_spi_txrx16(0x0000);
    delay_us(1);
    SPI_SET_CS();
    delay_us(1);

    return val;
}

/* 使能 AIN3 作外部基准：读回 REF 寄存器，把使能位或上去再写回 */
static void ad3344_ext_ref_enable(void)
{
    uint16_t v = ad3344_mmr_read(AD3344_MMR_REG_REF);
    ad3344_mmr_write(AD3344_MMR_REG_REF, v | AD3344_MMR_REF_EXT_BIT);
    AD_DBG("[AD3344] ext ref on, mmr[0x14] 0x%04X -> 0x%04X\r\n",
           v, (uint16_t)(v | AD3344_MMR_REF_EXT_BIT));
}
#endif


/*==============================================================================
 * 总线占用
 *============================================================================*/

/* 前台占住总线。占住之后后台扫描会整拍跳过，不会插进来抢 SPI。 */
static void ad3344_bus_take(void)
{
    s_bus_busy = 1;
}

/* 释放总线，同时作废扫描流水线：前台刚改过芯片 MUX，s_scan_armed 记的归属已经
 * 不成立，置 -1 让扫描下一拍丢一次数据重新对齐 */
static void ad3344_bus_give(void)
{
    s_scan_armed = -1;
    s_bus_busy   = 0;
}


/*==============================================================================
 *【4.2】前台阻塞读取
 *============================================================================*/

/*----------------------------------------------------------------------------
 * 读一个通道，返回电压(V)。调用者负责先占好总线。
 *
 * 单次模式：
 *   1) 写 cfg(ch, OS=1) 启动本路单次转换，这一帧读回来的是上次的陈旧数据，丢掉
 *   2) 等 1/DR + 余量
 *   3) 送 0x0000（NOP=00，不改配置）把结果移出来
 *
 * 连续模式：
 *   1) 写 cfg(ch) 切 MUX
 *   2) 丢掉 AD3344_CONT_DISCARD 次结果。手册 7.4.2.2：写新配置时正在进行的转换
 *      仍用旧配置完成，所以紧接着读到的是上一个通道的值
 *   3) 再读一次，这次才是本通道的
 *--------------------------------------------------------------------------*/
static float ad3344_read_locked(uint8_t ch)
{
    int16_t raw;

#if (AD3344_CONV_MODE == AD3344_MODE_CONTINUOUS)
    uint8_t i;

    ad3344_xfer(ad3344_make_cfg(ch, 0));            /* 切 MUX */

    for (i = 0; i < AD3344_CONT_DISCARD; i++) {     /* 丢掉用旧配置转出来的几次 */
        delay_us(AD3344_WAIT_US);
        (void)ad3344_xfer(AD3344_DIN_READ_ONLY);
    }

    delay_us(AD3344_WAIT_US);
    raw = (int16_t)ad3344_xfer(AD3344_DIN_READ_ONLY);
#else
    ad3344_xfer(ad3344_make_cfg(ch, 1));            /* OS=1，启动单次转换 */
    delay_us(AD3344_WAIT_US);                       /* 等它转完 */
    raw = (int16_t)ad3344_xfer(AD3344_DIN_READ_ONLY);
#endif

    s_ch_raw[ch]   = raw;
    s_ch_volt[ch]  = ad3344_raw_to_volt(raw, ch);
    s_ch_valid[ch] = 1;

    /* 前台读也走同一个映射点，结构体内容和后台扫描一致 */
    ad3344_dispatch_to_struct(ch, s_ch_volt[ch]);

    return s_ch_volt[ch];
}


float ad3344_read_channel(uint8_t ch)
{
    float v;

    if (ch >= AD3344_CH_NUM) {
        AD_DBG("[AD3344] read_channel: bad channel %u\r\n", (unsigned)ch);
        return 0.0f;
    }

    ad3344_bus_take();
    v = ad3344_read_locked(ch);
    ad3344_bus_give();

    AD_DBG_READ("[AD3344] CH%u raw=0x%04X(%6d) V=%.4f FSR=%.3f\r\n",
                (unsigned)ch, (unsigned)(uint16_t)s_ch_raw[ch], (int)s_ch_raw[ch],
                v, ad3344_pga_fsr(ad3344_pga_bits(ch)));

    return v;
}

float ad3344_read_ch0(void) { return ad3344_read_channel(AD3344_CH0); }
float ad3344_read_ch1(void) { return ad3344_read_channel(AD3344_CH1); }
float ad3344_read_ch2(void) { return ad3344_read_channel(AD3344_CH2); }
float ad3344_read_ch3(void) { return ad3344_read_channel(AD3344_CH3); }


/*----------------------------------------------------------------------------
 * 一次读回 CH0 / CH1 / CH2
 * 三路连着采，整段只占一次总线，比分三次调少两次占用/释放和两次流水线作废。
 * 任一指针可传 NULL。
 *--------------------------------------------------------------------------*/
void ad3344_read_ch012(float *v0, float *v1, float *v2)
{
    float r0, r1, r2;

    ad3344_bus_take();
    r0 = ad3344_read_locked(AD3344_CH0);
    r1 = ad3344_read_locked(AD3344_CH1);
    r2 = ad3344_read_locked(AD3344_CH2);
    ad3344_bus_give();

    if (v0 != NULL) *v0 = r0;
    if (v1 != NULL) *v1 = r1;
    if (v2 != NULL) *v2 = r2;

    AD_DBG_READ("[AD3344] trio: CH0 %.4fV=%.3fmA | CH1 %.4fV=%.3fV | CH2 %.4fV=%.3fV\r\n",
                r0, ad3344_volt_to_current(r0),
                r1, ad3344_volt_to_real(r1, AD3344_CH1_DIV_RATIO),
                r2, ad3344_volt_to_real(r2, AD3344_CH2_DIV_RATIO));
}


/*==============================================================================
 *【4.3】TIM9 10ms 后台扫描
 *============================================================================*/

#if (AD3344_TIM9_SCAN_ENABLE != 0)

/*----------------------------------------------------------------------------
 * TIMER9 中断里 10ms 调一次。
 *
 * 一拍只做一次 16 位 SPI 传输（约 12us，加 CS 前后延时不到 20us），这一帧同时：
 *     DOUT 移出来的 = 上一拍给 s_scan_armed 那个通道启动的转换结果
 *     DIN  送进去的 = 下一个通道的配置 + OS=1，当场启动它的转换
 * 转换时间（500SPS 下 2ms）藏在两拍之间的 10ms 空档里，中断里不需要 delay。
 *
 * s_scan_armed == -1 表示流水线空（刚开机或刚被前台阻塞读取打断），这一拍读回来
 * 的数据来路不明，丢掉，只负责启动下一个通道。
 *--------------------------------------------------------------------------*/
void ad3344_tim9_10ms_isr(void)
{
    uint8_t  next_ch;
    int8_t   done_ch;
    int16_t  raw;
    uint16_t rx;

    /* 前台正在用总线，这一拍整个跳过。前台释放时会把 s_scan_armed 置回 -1，
     * 恢复后自动重新对齐 */
    if (s_bus_busy) {
        return;
    }

    next_ch = s_scan_tab[s_scan_slot];
    done_ch = s_scan_armed;

    /* 一帧搞定：读走上一个，启动下一个 */
    rx = ad3344_xfer(ad3344_make_scan_cfg(next_ch));

    if (done_ch >= 0) {
        raw = (int16_t)rx;
        s_ch_raw[done_ch]   = raw;
        s_ch_volt[done_ch]  = ad3344_raw_to_volt(raw, (uint8_t)done_ch);
        s_ch_valid[done_ch] = 1;
        ad3344_dispatch_to_struct((uint8_t)done_ch, s_ch_volt[done_ch]);

#if (AD3344_DEBUG_EN != 0) && (AD3344_CAL_MODE != 0)
        /* 【1.11】标定模式：攒 CH0 的原始码。s_cal_req 还没被主循环清掉时先不攒，
         * 否则会覆盖掉还没打印的那批快照 */
        if (done_ch == AD3344_CH0 && !s_cal_req) {
            s_cal_acc += raw;
            s_cal_cnt++;
            if (s_cal_cnt >= AD3344_CAL_AVG_N) {
                s_cal_sum_out = s_cal_acc;
                s_cal_n_out   = s_cal_cnt;
                s_cal_acc     = 0;
                s_cal_cnt     = 0;
                s_cal_req     = 1;      /* 交给 ad3344_debug_poll() 打印 */
            }
        }
#endif
    }

    s_scan_armed = (int8_t)next_ch;

    /* 轮到通道表下一格，转回第 0 格算完成一轮 */
    s_scan_slot++;
    if (s_scan_slot >= AD3344_SCAN_TAB_LEN) {
        s_scan_slot = 0;
        s_scan_round++;

#if (AD3344_DEBUG_EN != 0) && (AD3344_DEBUG_SCAN != 0)
        if ((s_scan_round % AD3344_DEBUG_SCAN_DECIM) == 0) {
    #if (AD3344_DEBUG_SCAN == 2)
            /* 中断里直接打印：一行几十字节在 115200 下要好几毫秒，会撑爆 10ms
             * 的中断并顶掉后面的定时任务，仅临时排障用 */
            printf("[AD3344][ISR] CH0=%.4f CH1=%.4f CH2=%.4f CH3=%.4f\r\n",
                   s_ch_volt[0], s_ch_volt[1], s_ch_volt[2], s_ch_volt[3]);
    #else
            s_scan_print_req = 1;   /* 交给主循环的 ad3344_debug_poll() 去打 */
    #endif
        }
#endif
    }
}

#else   /* AD3344_TIM9_SCAN_ENABLE == 0 */

/* 扫描关掉时留个空壳，中断里那句调用可以原样留着 */
void ad3344_tim9_10ms_isr(void)
{
}

#endif  /* AD3344_TIM9_SCAN_ENABLE */


/*----------------------------------------------------------------------------
 * 主循环里调。中断只置标志，printf 在这里做，不占中断时间。
 *--------------------------------------------------------------------------*/
void ad3344_debug_poll(void)
{
#if (AD3344_DEBUG_EN != 0) && (AD3344_CAL_MODE != 0) && (AD3344_TIM9_SCAN_ENABLE != 0)
    /* 【1.11】电流通道标定打印。平均在原始码上做完，这里才一次性换算，
     * 所以 raw 那一列可以直接拿去算 K/B */
    if (s_cal_req) {
        int32_t  sum = s_cal_sum_out;
        uint16_t n   = s_cal_n_out;
        float    code, volt;
        int16_t  code_i;

        s_cal_req = 0;                  /* 先清标志，中断可以继续攒下一批 */

        code = (n != 0) ? ((float)sum / (float)n) : 0.0f;

        /* 不走 ad3344_raw_to_volt()：它入参是 int16_t，会截掉平均值的小数部分，
         * 丢掉多采 32 次换来的分辨率。这里用浮点平均码按同样公式换算，
         * FSR 仍从 CH0 的 PGA 宏现取，和主路径一致 */
        volt   = (ad3344_pga_fsr(ad3344_pga_bits(AD3344_CH0)) * code) / 32768.0f;
        code_i = (int16_t)((code >= 0.0f) ? (code + 0.5f) : (code - 0.5f));

        printf("[AD3344][cal] n=%u code=0x%04X(%.1f) V=%.5f raw=%.3fmA cal=%.3fmA\r\n",
               (unsigned)n,
               (unsigned)(uint16_t)code_i, code,
               volt,
               ad3344_volt_to_current_raw(volt),
               ad3344_volt_to_current(volt));
    }
#endif

#if (AD3344_DEBUG_EN != 0) && (AD3344_TIM9_SCAN_ENABLE != 0) && (AD3344_DEBUG_SCAN == 1)
    if (s_scan_print_req) {
        s_scan_print_req = 0;
        /* 电压和工程量都打，便于区分是接线还是系数的问题 */
        printf("[AD3344][scan#%lu] CH0 %.4fV=%.3fmA%s | CH1 %.4fV=%.3fV | CH2 %.4fV=%.3fV | PT100 %.2fC\r\n",
               (unsigned long)s_scan_round,
               s_ch_volt[0], ad3344_volt_to_current(s_ch_volt[0]),
               ad3344_is_broken() ? "(BROKEN)" : "",
               s_ch_volt[1], ad3344_volt_to_real(s_ch_volt[1], AD3344_CH1_DIV_RATIO),
               s_ch_volt[2], ad3344_volt_to_real(s_ch_volt[2], AD3344_CH2_DIV_RATIO),
               ad3344_volt_to_pt100_temp(s_ch_volt[0]));
    }
#endif

#if (AD3344_DEBUG_EN != 0) && (AD3344_BROKEN_DET_ENABLE != 0) && (AD3344_BROKEN_DEBUG != 0)
    /* 断线状态翻转事件。事件级打印，状态没变时一句都不打 */
    if (s_broken_evt) {
        s_broken_evt = 0;
        printf("[AD3344] 4-20mA loop %s at %.3f mA (th %.2f/%.2f, debounce %u/%u)\r\n",
               s_broken_evt_new ? "BROKEN" : "restored",
               s_broken_evt_cur,
               (float)AD3344_BROKEN_TH, (float)AD3344_BROKEN_CLEAR_TH,
               (unsigned)AD3344_BROKEN_COUNT, (unsigned)AD3344_BROKEN_CLEAR_COUNT);
    }
#endif
}


/*==============================================================================
 *【4.4】缓存读取与杂项
 *============================================================================*/

float ad3344_get_ch_volt(uint8_t ch)
{
    if (ch >= AD3344_CH_NUM) {
        return 0.0f;
    }
    return s_ch_volt[ch];
}

int16_t ad3344_get_ch_raw(uint8_t ch)
{
    if (ch >= AD3344_CH_NUM) {
        return 0;
    }
    return s_ch_raw[ch];
}

uint8_t ad3344_ch_is_valid(uint8_t ch)
{
    if (ch >= AD3344_CH_NUM) {
        return 0;
    }
    return s_ch_valid[ch];
}

/*----------------------------------------------------------------------------
 * 取某通道的电压：有缓存用缓存，没有就现采一次。
 * 下面四个工程量接口都基于它，开不开后台扫描对调用者透明。
 *--------------------------------------------------------------------------*/
static float ad3344_volt_cached_or_read(uint8_t ch)
{
    if (s_ch_valid[ch]) {
        return s_ch_volt[ch];
    }
    return ad3344_read_channel(ch);
}

/* CH0 电流，默认 mA。断线状态用 ad3344_is_broken() 取 */
float ad3344_get_current(void)
{
    return ad3344_volt_to_current(ad3344_volt_cached_or_read(AD3344_CH0));
}

/* CH1 还原后的实际电压 V */
float ad3344_get_v0(void)
{
    return ad3344_volt_to_real(ad3344_volt_cached_or_read(AD3344_CH1), AD3344_CH1_DIV_RATIO);
}

/* CH2 还原后的实际电压 V */
float ad3344_get_v1(void)
{
    return ad3344_volt_to_real(ad3344_volt_cached_or_read(AD3344_CH2), AD3344_CH2_DIV_RATIO);
}

/* CH0 的 PT100 温度，遗留链路 */
float ad3344_get_pt100_temp(void)
{
    return ad3344_volt_to_pt100_temp(ad3344_volt_cached_or_read(AD3344_CH0));
}


void ad3344_reset(void)
{
    ad3344_bus_take();
    ad3344_xfer(AD3344_CONFIG_DEFAULT);
    ad3344_bus_give();
    AD_DBG("[AD3344] reset -> cfg 0x%04X\r\n", (unsigned)AD3344_CONFIG_DEFAULT);
}


void ad3344_stop_conversion(void)
{
    /* MODE 位改成单次且不带 OS，芯片做完手上这次就进掉电，不再自动开新的 */
    uint16_t cfg = ad3344_mux_bits(s_scan_armed >= 0 ? (uint8_t)s_scan_armed : AD3344_CH0)
                 | AD3344_PGA_SEL
                 | AD3344_DR_SEL
                 | AD3344_REG_CONFIG_MODE_SINGLE
                 | AD3344_REG_CONFIG_PULL_UP_EN
                 | AD3344_REG_CONFIG_NOP_VALID;

    ad3344_bus_take();
    ad3344_xfer(cfg);
    ad3344_bus_give();
    AD_DBG("[AD3344] stop conversion, cfg 0x%04X\r\n", cfg);
}


uint16_t ad3344_last_cfg_readback(void)
{
    return s_cfg_readback;
}


/*==============================================================================
 *【4.1】初始化
 *============================================================================*/

void bsp_ad3344_init(void)
{
    uint8_t i;

    ad3344_bus_take();              /* 初始化全程独占总线 */

    ad3344_spi_hw_init();

    /* 手册 9.1：VDD 稳定后要等约 50us 走完上电复位流程才能通信 */
    delay_us(200);

#if (AD3344_MMR_INIT_ENABLE != 0)
    ad3344_mmr_write(AD3344_MMR_REG_CAL, AD3344_MMR_CAL_VALUE);
#endif

#if (AD3344_EXT_REF_ENABLE != 0)
    ad3344_ext_ref_enable();
#endif

    /* 写一次配置字定下 DR / PGA / MODE。单次模式下这一帧不带 OS，
     * 芯片停在掉电状态等后面触发 */
    ad3344_xfer(ad3344_make_cfg(AD3344_CH0, 0));
    delay_us(AD3344_WAIT_US);

    for (i = 0; i < AD3344_CH_NUM; i++) {
        s_ch_raw[i]   = 0;
        s_ch_volt[i]  = 0.0f;
        s_ch_valid[i] = 0;
    }
    s_scan_armed     = -1;
    s_scan_slot      = 0;
    s_scan_round     = 0;
    s_scan_print_req = 0;

    /* 断线状态机清到初始状态。要排在下面自检读 CH0 之前，否则自检那个样本会被清掉 */
    ad3344_broken_reset();

    ad3344_bus_give();

    /* 不要把 #if 写进 printf 的实参列表：宏展开过程中出现预处理指令是未定义行为，
     * armcc 不保证能过，用字符串常量绕开。
     * DR 打成转换耗时而不是 SPS：6.25SPS 这种小数按整数除法会打成 6 */
    AD_DBG("[AD3344] init ok | mode=%s | conv %luus | FSR ch0/1/2/3 = %.3f/%.3f/%.3f/%.3f V\r\n",
           AD3344_MODE_NAME,
           (unsigned long)AD3344_CONV_TIME_US,
           ad3344_pga_fsr(AD3344_PGA_CH0), ad3344_pga_fsr(AD3344_PGA_CH1),
           ad3344_pga_fsr(AD3344_PGA_CH2), ad3344_pga_fsr(AD3344_PGA_CH3));

    AD_DBG("[AD3344] scale: I=V/%.1fohm*%.0f | V0=V/%.3f | V1=V/%.3f\r\n",
           AD3344_CH0_SENSE_R, AD3344_CH0_I_UNIT_K,
           AD3344_CH1_DIV_RATIO, AD3344_CH2_DIV_RATIO);

#if (AD3344_I0_CAL_ENABLE != 0)
    /* 打出当前烧进去的标定系数，多块板子时用来确认手上这块刷的是哪一版 */
    AD_DBG("[AD3344] i0 cal ON: I = %.6f * I_raw + %.4f\r\n",
           (float)AD3344_I0_CAL_K, (float)AD3344_I0_CAL_B);

    /* K=0 会让电流恒等于 B，现象是读数卡住不动，不容易往系数上想。
     * #if 不认浮点常量，只能放在运行时。先落到局部变量再比，
     * 免得 armcc 刷"条件恒为假"的警告 */
    {
        float cal_k = AD3344_I0_CAL_K;
        if (cal_k == 0.0f) {
            AD_DBG("[AD3344] *** I0 CAL K IS ZERO: current will be stuck at B ***\r\n");
        }
    }
#else
    AD_DBG("[AD3344] i0 cal off, raw I = V/R only\r\n");
#endif

#if (AD3344_CAL_MODE != 0)
    AD_DBG("[AD3344] *** CAL MODE ON *** averaging %u ch0 samples per line;"
           " use the 'raw' column for K/B, set AD3344_CAL_MODE=0 when done\r\n",
           (unsigned)AD3344_CAL_AVG_N);
#endif

#if (AD3344_BROKEN_DET_ENABLE != 0)
    AD_DBG("[AD3344] broken det on: <%.2f -> broken, >%.2f -> ok, debounce %u/%u samples\r\n",
           (float)AD3344_BROKEN_TH, (float)AD3344_BROKEN_CLEAR_TH,
           (unsigned)AD3344_BROKEN_COUNT, (unsigned)AD3344_BROKEN_CLEAR_COUNT);

    /* 回差方向的运行时校验。#if 只认整型常量表达式，这条检查只能放在运行时。
     * 配反了状态机会在两个阈值之间反复横跳。同样先落到局部变量再比 */
    {
        float th_lo = AD3344_BROKEN_TH;
        float th_hi = AD3344_BROKEN_CLEAR_TH;
        if (th_hi <= th_lo) {
            AD_DBG("[AD3344] *** BROKEN TH MISCONFIGURED: CLEAR_TH(%.2f) must be > TH(%.2f) ***\r\n",
                   th_hi, th_lo);
        }
    }
#else
    AD_DBG("[AD3344] broken det off, i0_broken stays 0\r\n");
#endif

#if (AD3344_TIM9_SCAN_ENABLE != 0)
    AD_DBG("[AD3344] tim9 scan on, %u ch, each refreshed every %ums\r\n",
           (unsigned)AD3344_SCAN_TAB_LEN,
           (unsigned)(AD3344_SCAN_TAB_LEN * (AD3344_SCAN_TICK_US / 1000)));
#else
    AD_DBG("[AD3344] tim9 scan off, use ad3344_read_chX() to poll\r\n");
#endif

    /* 上电四个通道各读一次：接线错了能立刻从串口看出来，同时后台扫描第一轮转完
     * 之前（最多 40ms）Data_class_structure 里就有真值，不会有一段全 0 的窗口。
     * 四次阻塞读取合计约 10ms，只在开机时发生一次。ad3344_read_channel() 内部走
     * ad3344_dispatch_to_struct()，跑完 i0_current / v0_voltage / v1_voltage /
     * ch2_current_temp 都填好了 */
    AD_DBG("[AD3344] self test (volt -> engineering):\r\n");
    for (i = 0; i < AD3344_CH_NUM; i++) {
        (void)ad3344_read_channel(i);
    }
    AD_DBG("           CH0 %.4fV -> %.3f mA %s\r\n",
           s_ch_volt[AD3344_CH0], ad3344_volt_to_current(s_ch_volt[AD3344_CH0]),
           ad3344_is_broken() ? "[BROKEN]" : "");
    AD_DBG("           CH1 %.4fV -> %.3f V  (/%.3f)\r\n",
           s_ch_volt[AD3344_CH1],
           ad3344_volt_to_real(s_ch_volt[AD3344_CH1], AD3344_CH1_DIV_RATIO),
           AD3344_CH1_DIV_RATIO);
    AD_DBG("           CH2 %.4fV -> %.3f V  (/%.3f)\r\n",
           s_ch_volt[AD3344_CH2],
           ad3344_volt_to_real(s_ch_volt[AD3344_CH2], AD3344_CH2_DIV_RATIO),
           AD3344_CH2_DIV_RATIO);
    AD_DBG("           CH3 %.4fV  (not connected)\r\n", s_ch_volt[AD3344_CH3]);
    AD_DBG("           PT100 %.2f C  (legacy, not graded)\r\n",
           ad3344_volt_to_pt100_temp(s_ch_volt[AD3344_CH0]));

    /* 自检做完把流水线清干净，交给后台扫描 */
    s_scan_armed = -1;
    s_scan_slot  = 0;
}
