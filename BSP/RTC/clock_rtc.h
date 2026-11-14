#ifndef CLOCK_RTC_ADAPTER_H
#define CLOCK_RTC_ADAPTER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 保存经过校验的完整日历日期与时间。 */
typedef struct {
    uint16_t year;   // 完整年份，支持 2000～2099
    uint8_t month;   // 月份，取值为 1～12
    uint8_t day;     // 月内日期，从 1 开始
    uint8_t hour;    // 小时，取值为 0～23
    uint8_t minute;  // 分钟，取值为 0～59
    uint8_t second;  // 秒，取值为 0～59
    uint8_t weekday; // 星期编号，星期一为 1，星期日为 7
} rtc_date_time_t;

/* 操作在任务上下文执行；rtc_init 保留备份域已有日期。 */
void rtc_init(void);
void rtc_set_time(const rtc_date_time_t *date_time);
/* RTC 不可用或读取失败时输出全零结果。 */
void rtc_get_time(rtc_date_time_t *date_time);
bool clock_rtc_ready(void);

#ifdef __cplusplus
}
#endif
#endif
