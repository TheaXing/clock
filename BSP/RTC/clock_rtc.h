#ifndef CLOCK_RTC_ADAPTER_H
#define CLOCK_RTC_ADAPTER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t weekday; /* Monday = 1, Sunday = 7, as in the STM32 RTC. */
} rtc_date_time_t;

/* Task-context operations. rtc_init preserves an existing backup-domain date. */
void rtc_init(void);
void rtc_set_time(const rtc_date_time_t *date_time);
/* Writes an all-zero result if the RTC is unavailable or a read fails. */
void rtc_get_time(rtc_date_time_t *date_time);
bool clock_rtc_ready(void);

#ifdef __cplusplus
}
#endif
#endif
