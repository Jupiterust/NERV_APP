#include "bsp_rtc.h"

#define RTC_CLOCK_SOURCE_LXTAL		
#define BKP_VALUE    0x32F0

rtc_parameter_struct   rtc_initpara;
rtc_alarm_struct  rtc_alarm;
__IO uint32_t prescaler_a = 0, prescaler_s = 0;
uint32_t RTCSRC_FLAG = 0;



/*!
    \brief      main function
*/
void RTC_Init(void)
{
    rcu_periph_clock_enable(RCU_PMU);
    /* 使能 the access of the RTC registers */
    pmu_backup_write_enable();
	
    rtc_pre_config();
    /* get RTC clock entry selection */
    RTCSRC_FLAG = GET_BITS(RCU_BDCTL, 8, 9);

    if((BKP_VALUE != RTC_BKP0) || (0x00 == RTCSRC_FLAG)){
        rtc_setup();
    }else{
        // detect the reset source
        if (RESET != rcu_flag_get(RCU_FLAG_PORRST)){
            printf("power on reset occurred....\n\r");
        }else if (RESET != rcu_flag_get(RCU_FLAG_EPRST)){
            printf("external reset occurred....\n\r");
        }
        printf("no need to configure RTC....\n\r");

        rtc_show_time();
    }
    rcu_all_reset_flag_clear();
}

/*!
    \brief      RTC configuration function
*/
void rtc_pre_config(void)
{
    rcu_osci_on(RCU_LXTAL);
    rcu_osci_stab_wait(RCU_LXTAL);
    rcu_rtc_clock_config(RCU_RTCSRC_LXTAL);
    prescaler_s = 0xFF;
    prescaler_a = 0x7F;


    rcu_periph_clock_enable(RCU_RTC);
    rtc_register_sync_wait();
}

/*!
    \brief      use hyperterminal to setup RTC time and alarm
*/
void rtc_setup(void)
{
	uint32_t tmp_year = 0xFF, tmp_month = 0xFF, tmp_day = 0xFF;
    uint32_t tmp_hh = 0xFF, tmp_mm = 0xFF, tmp_ss = 0xFF;

    rtc_initpara.factor_asyn = prescaler_a;
    rtc_initpara.factor_syn = prescaler_s;
    rtc_initpara.year = 0x16;       //16
    rtc_initpara.day_of_week = RTC_SATURDAY;
    rtc_initpara.month = RTC_APR;
    rtc_initpara.date = 0x30;       //20
    rtc_initpara.display_format = RTC_24HOUR;
    rtc_initpara.am_pm = RTC_AM;

    /* current time input */
    printf("=======Configure RTC Time========\n\r");
	 printf("  please set the last two digits of current year:\n\r");
    while(tmp_year == 0xFF) {
        tmp_year = usart_input_threshold(99);
        rtc_initpara.year = tmp_year;
    }
    printf("  20%0.2x\n\r", tmp_year);

    printf("  please input month:\n\r");
    while(tmp_month == 0xFF) {
        tmp_month = usart_input_threshold(12);
        rtc_initpara.month = tmp_month;
    }
    printf("  %0.2x\n\r", tmp_month);

    printf("  please input day:\n\r");
    while(tmp_day == 0xFF) {
        tmp_day = usart_input_threshold(31);
        rtc_initpara.date = tmp_day;
    }
    printf("  %0.2x\n\r", tmp_day);
		
	
    printf("  please input hour:\n\r");
    while (0xFF == tmp_hh){
        tmp_hh = usart_input_threshold(23);
        rtc_initpara.hour = tmp_hh;
    }
    printf("  %0.2x\n\r", tmp_hh);

    printf("  please input minute:\n\r");
    while (0xFF == tmp_mm){
        tmp_mm = usart_input_threshold(59);
        rtc_initpara.minute = tmp_mm;
    }
    printf("  %0.2x\n\r", tmp_mm);

    printf("  please input second:\n\r");
    while (0xFF == tmp_ss){
        tmp_ss = usart_input_threshold(59);
        rtc_initpara.second = tmp_ss;
    }
    printf("  %0.2x\n\r", tmp_ss);

    /* RTC current time configuration */
    if(ERROR == rtc_init(&rtc_initpara)){
        printf("\n\r** RTC time configuration failed! **\n\r");
    }else{
        printf("\n\r** RTC time configuration success! **\n\r");
        rtc_show_time();
        RTC_BKP0 = BKP_VALUE;
    }
}

/*!
    \brief      display the current time
*/
void rtc_show_time(void)
{
    rtc_current_time_get(&rtc_initpara);
    printf("\r\nCurrent time: 20%0.2x-%0.2x-%0.2x", \
           rtc_initpara.year, rtc_initpara.month, rtc_initpara.date);
    printf(" : %0.2x:%0.2x:%0.2x \r\n", \
           rtc_initpara.hour, rtc_initpara.minute, rtc_initpara.second);
}

/*!
    \brief      display the alram value
    \param[in]  none
    \param[out] none
    \retval     none
*/
void rtc_show_alarm(void)
{
    rtc_alarm_get(RTC_ALARM0,&rtc_alarm);
    printf("The alarm: %0.2x:%0.2x:%0.2x \n\r", rtc_alarm.alarm_hour, rtc_alarm.alarm_minute,\
           rtc_alarm.alarm_second);
}

/*!
    \brief      get the input character string and check if it is valid
    \param[in]  none
    \param[out] none
    \retval     input value in BCD mode
*/
uint8_t usart_input_threshold(uint32_t value)
{
    uint32_t index = 0;
    uint32_t tmp[2] = {0, 0};

    while (index < 2){
        while (RESET == usart_flag_get(USART0, USART_FLAG_RBNE));
        tmp[index++] = usart_data_receive(USART0);
        if ((tmp[index - 1] < 0x30) || (tmp[index - 1] > 0x39)){
            printf("\n\r please input a valid number between 0 and 9 \n\r");
            index--;
        }
    }

    index = (tmp[1] - 0x30) + ((tmp[0] - 0x30) * 10);
    if (index > value){
        printf("\n\r please input a valid number between 0 and %d \n\r", value);
        return 0xFF;
    }

    index = (tmp[1] - 0x30) + ((tmp[0] - 0x30) <<4);
    return index;
}

volatile  rtc_parameter_struct bsp_rtc_init_para;
char *bsp_rtc_weeks[8] = {"ERR", "MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"};



void bsp_rtc_rcu_config(void){
    rcu_osci_on(RCU_LXTAL);
    rcu_osci_stab_wait(RCU_LXTAL);
    rcu_rtc_clock_config(RCU_RTCSRC_LXTAL);

    rcu_periph_clock_enable(RCU_RTC);
    rtc_register_sync_wait();
}

void bsp_rtc_show_time(void){
    rtc_current_time_get(&bsp_rtc_init_para);
    #if 0
    printf("Current Time: 20%0.2x-%0.2x-%0.2x  %0.2x:%0.2x:%0.2x \r\n", 
            bsp_rtc_init_para.year, bsp_rtc_init_para.month, bsp_rtc_init_para.date,
            bsp_rtc_init_para.hour, bsp_rtc_init_para.minute, bsp_rtc_init_para.second);
    #endif
}

void bsp_rtc_init(void){
    /* enable PMU clock */
    rcu_periph_clock_enable(BSP_RTC_RCU_PMU);
    /* enable the access of the RTC registers */
    pmu_backup_write_enable();
     if(RTC_BKP0 == BKP_VALUE){ 
        bsp_rtc_rcu_config();
#if 1		 
        printf("RTC is already running.\n\r");
#endif
        bsp_rtc_show_time();
        return;     
    }
    bsp_rtc_rcu_config();
    bsp_rtc_init_para.factor_asyn = BSP_PRESCALER_A;
    bsp_rtc_init_para.factor_syn  = BSP_PRESCALER_S;
    
    bsp_rtc_init_para.year = 0x26;       //26
    bsp_rtc_init_para.day_of_week = RTC_WEDSDAY;
    bsp_rtc_init_para.month = RTC_JUN;
    bsp_rtc_init_para.date = 0x03;       //30
    bsp_rtc_init_para.display_format = RTC_12HOUR;
    bsp_rtc_init_para.am_pm = RTC_PM;   
    bsp_rtc_init_para.hour = 0x06;
    bsp_rtc_init_para.minute = 0x29;
    bsp_rtc_init_para.second = 0x29;

    if(ERROR == rtc_init(&bsp_rtc_init_para)){
        printf("\n\r** RTC time configuration failed! **\n\r");
    }else{
        printf("\n\r** RTC time configuration success! **\n\r");
        bsp_rtc_show_time();
        RTC_BKP0 = BKP_VALUE; //
    }
}



uint8_t bsp_rtc_set_time(bsp_rtc_time_set_para_struct* bsp_rtc_set_para){
	bsp_rtc_init_para.year = bsp_rtc_set_para->year;       //26
    bsp_rtc_init_para.day_of_week = bsp_rtc_set_para->day_of_week;
    bsp_rtc_init_para.month = bsp_rtc_set_para->month;
    bsp_rtc_init_para.date = bsp_rtc_set_para->date;
    bsp_rtc_init_para.display_format = bsp_rtc_set_para->display_format;
    bsp_rtc_init_para.am_pm = RTC_PM;   
    bsp_rtc_init_para.hour = bsp_rtc_set_para->hour;
    bsp_rtc_init_para.minute = bsp_rtc_set_para->minute;
    bsp_rtc_init_para.second = bsp_rtc_set_para->second;
	
    if (ERROR == rtc_init(&bsp_rtc_init_para)) {
        printf("** RTC set time failed! **\r\n");
        return ERROR;
    }

    RTC_BKP0 = BKP_VALUE;   
    return SUCCESS;
}

// BCD convert DEC
#define BCD2DEC(x)  (((x) >> 4) * 10 + ((x) & 0x0F))

static const uint16_t month_days[15] = {
    0, 0, 0, 0, 31, 61, 92, 122, 153, 184, 214, 245, 275, 306, 337
};


uint32_t bsp_rtc_get_unix_timestamp(void){
    rtc_current_time_get(&bsp_rtc_init_para);

    uint16_t year  = 2000 + BCD2DEC(bsp_rtc_init_para.year);
    uint8_t  month = BCD2DEC(bsp_rtc_init_para.month);
    uint8_t  day   = BCD2DEC(bsp_rtc_init_para.date);
    uint8_t  hour  = BCD2DEC(bsp_rtc_init_para.hour);
    uint8_t  min   = BCD2DEC(bsp_rtc_init_para.minute);
    uint8_t  sec   = BCD2DEC(bsp_rtc_init_para.second);

    /* 处理12小时制 PM/AM */
    if(bsp_rtc_init_para.display_format == RTC_12HOUR){
        if(bsp_rtc_init_para.am_pm == RTC_PM && hour != 12)
            hour += 12;
        if(bsp_rtc_init_para.am_pm == RTC_AM && hour == 12)
            hour = 0;
    }

    /* 把Jan/Feb归入上一年，从3月起算方便闰年处理 */
    if(month < 3){
        month += 12;
        year  -= 1;
    }

    uint32_t days = 365UL * year
                  + year / 4
                  - year / 100
                  + year / 400
                  + month_days[month]
                  + day - 1
                  - 719468UL;  /* 对齐到Unix纪元 1970-01-01 */

    uint32_t timestamp = days   * 86400UL
                       + (uint32_t)hour * 3600UL
                       + (uint32_t)min  * 60UL
                       + sec;

    /* RTC存的是北京时间UTC+8，转为标准UTC，按需开启 */
    // timestamp -= 8 * 3600UL;

    return timestamp;
}





// 																干干添加的部分											//

// 十进制转 BCD
#define DEC2BCD(x)  ((((x) / 10) << 4) | ((x) % 10))

// 检查是否为闰年
#define IS_LEAP_YEAR(year) ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))

static const uint8_t month_table[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};


/**
 * @brief  设置时间为指定的 Unix 时间戳
 */
uint8_t bsp_rtc_set_unix_timestamp(uint32_t timestamp, int8_t timezone) {
    uint32_t days, seconds;
    uint16_t year = 1970;
    uint8_t month, month_day;
    
    // 处理时区
    timestamp += (int32_t)timezone * 3600;

    days = timestamp / 86400;      // 总天数
    seconds = timestamp % 86400;   // 当天剩余秒数

    // 计算小时、分钟、秒 
    uint8_t hour = seconds / 3600;
    uint8_t minute = (seconds % 3600) / 60;
    uint8_t second = seconds % 60;

    // 计算星期几
    uint8_t day_of_week = (days + 4) % 7; 
    if(day_of_week == 0) day_of_week = 7; 

    // 计算年份
    while (1) {
        uint16_t days_in_year = IS_LEAP_YEAR(year) ? 366 : 365;
        if (days < days_in_year) break;
        days -= days_in_year;
        year++;
    }

    // 计算月份和日期
    month = 0;
    while (1) {
        uint8_t days_in_month = month_table[month];
        if (month == 1 && IS_LEAP_YEAR(year)) days_in_month = 29; // 闰二月
        if (days < days_in_month) break;
        days -= days_in_month;
        month++;
    }
    month += 1;    // 月份从1开始
    month_day = days + 1; // 日期从1开始

    // 填充 RTC 结构体 (转换为 BCD 格式)
    bsp_rtc_init_para.year           = DEC2BCD(year % 100);
    bsp_rtc_init_para.month          = DEC2BCD(month);
    bsp_rtc_init_para.date           = DEC2BCD(month_day);
    bsp_rtc_init_para.day_of_week    = day_of_week;
    bsp_rtc_init_para.hour           = DEC2BCD(hour);
    bsp_rtc_init_para.minute         = DEC2BCD(minute);
    bsp_rtc_init_para.second         = DEC2BCD(second);
    bsp_rtc_init_para.display_format = RTC_24HOUR; // 建议强制使用24小时制简化
    bsp_rtc_init_para.am_pm          = RTC_AM;

    // 写入硬件
    if (ERROR == rtc_init(&bsp_rtc_init_para)) {
        printf("RTC set timestamp failed!\r\n");
        return ERROR;
    }

    RTC_BKP0 = BKP_VALUE; // 标记 RTC 已配置
    return SUCCESS;
}





/**
 * @brief  配置 RTC 闹钟，使其在 10 秒后触发
 */
void bsp_rtc_set_alarm_10s(void)
{
    rtc_parameter_struct current_time;

    g_rtc_alarm_fired = 0;  // 重新装表，上一次的记录作废

    // 获取当前时间 */
    rtc_current_time_get(&current_time);

    // 计算 10 秒后的秒数 
    uint8_t current_sec_dec = BCD2DEC(current_time.second);
    uint8_t target_sec_dec  = (current_sec_dec + 10) % 60;
    
    // 关闭闹钟才能修改配置 
    rtc_alarm_disable(RTC_ALARM0);

    // 配置闹钟结构体 
    rtc_alarm.alarm_mask = RTC_ALARM_DATE_MASK | RTC_ALARM_HOUR_MASK | RTC_ALARM_MINUTE_MASK;
    rtc_alarm.weekday_or_date = RTC_ALARM_DATE_SELECTED;
    rtc_alarm.alarm_day = 0x01;  // 被掩码忽略，填什么无所谓
    rtc_alarm.am_pm = RTC_AM;    // 被掩码忽略
    rtc_alarm.alarm_hour = 0x00; // 被掩码忽略
    rtc_alarm.alarm_minute = 0x00;// 被掩码忽略
    rtc_alarm.alarm_second = DEC2BCD(target_sec_dec); // 只精确匹配这个秒数

    rtc_alarm_config(RTC_ALARM0, &rtc_alarm);

    // 清除相关的中断标志位 
    rtc_flag_clear(RTC_FLAG_ALRM0);
    exti_interrupt_flag_clear(EXTI_17); // RTC 闹钟内部连接在 EXTI 17

    // 使能闹钟和闹钟中断 
    rtc_interrupt_enable(RTC_INT_ALARM0);
    rtc_alarm_enable(RTC_ALARM0);

    // 配置唤醒所需的 EXTI 线 (极其重要，深度睡眠唤醒全靠它) 
    exti_init(EXTI_17, EXTI_INTERRUPT, EXTI_TRIG_RISING);
    
    //使能 NVIC 中的 RTC 闹钟中断 
    nvic_irq_enable(RTC_Alarm_IRQn, 0, 0); // 优先级 0,0 (根据你的工程调整)
}

/* 闹钟真的响过一次就置 1。装闹钟前由 bsp_rtc_set_alarm_10s() 清零。
 * 这是判断"RTC + 闹钟 + EXTI17 + NVIC"整条链路通不通的唯一直接证据：
 * 睡不醒的时候先看它有没有被置起来，能把问题一刀切成 RTC 侧还是 PMU 侧。*/
volatile uint8_t g_rtc_alarm_fired = 0;

// RTC 闹钟中断服务函数
void RTC_Alarm_IRQHandler(void)
{
    if (RESET != exti_interrupt_flag_get(EXTI_17)) {
        exti_interrupt_flag_clear(EXTI_17);
        //检查 RTC 内部闹钟 0 标志位
        if (RESET != rtc_flag_get(RTC_FLAG_ALRM0)) {

            // 清除 RTC 闹钟标志位
            rtc_flag_clear(RTC_FLAG_ALRM0);

            // 唤醒后，关闭闹钟
            rtc_alarm_disable(RTC_ALARM0);

            g_rtc_alarm_fired = 1;
        }
    }
}


