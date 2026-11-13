#include "clock_rtc.h"
#include "rtc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <stddef.h>
#include <string.h>

static SemaphoreHandle_t rtc_mutex;
static bool rtc_available;

static bool date_time_valid(const rtc_date_time_t *value)
{
    static const uint8_t month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (value == NULL || value->year < 2000U || value->year > 2099U ||
        value->month < 1U || value->month > 12U || value->day < 1U ||
        value->weekday < 1U || value->weekday > 7U || value->hour > 23U ||
        value->minute > 59U || value->second > 59U)
    {
        return false;
    }
    uint8_t days = month_days[value->month - 1U];
    if (value->month == 2U && (value->year % 4U) == 0U)
    {
        ++days; /* 2000..2099 has no Gregorian century exception. */
    }
    return value->day <= days;
}

static bool lock_rtc(void)
{
    return xTaskGetSchedulerState() != taskSCHEDULER_RUNNING ||
           (rtc_mutex != NULL && xSemaphoreTake(rtc_mutex, portMAX_DELAY) == pdTRUE);
}

static void unlock_rtc(void)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        (void)xSemaphoreGive(rtc_mutex);
    }
}

void rtc_init(void)
{
    rtc_available = false;
    if (rtc_mutex == NULL)
    {
        rtc_mutex = xSemaphoreCreateMutex();
    }
    if (rtc_mutex == NULL)
    {
        return;
    }
    MX_RTC_Init();
    rtc_available = MX_RTC_IsReady();
}

bool clock_rtc_ready(void)
{
    return rtc_available && MX_RTC_IsReady();
}

void rtc_set_time(const rtc_date_time_t *date_time)
{
    if (!date_time_valid(date_time) || !clock_rtc_ready() || !lock_rtc())
    {
        return;
    }

    RTC_TimeTypeDef time = {0};
    RTC_DateTypeDef date = {0};
    time.Hours = date_time->hour;
    time.Minutes = date_time->minute;
    time.Seconds = date_time->second;
    time.TimeFormat = RTC_HOURFORMAT12_AM;
    time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    time.StoreOperation = RTC_STOREOPERATION_RESET;
    date.Year = (uint8_t)(date_time->year - 2000U);
    date.Month = date_time->month;
    date.Date = date_time->day;
    date.WeekDay = date_time->weekday;

    if (HAL_RTC_SetTime(&hrtc, &time, RTC_FORMAT_BIN) != HAL_OK ||
        HAL_RTC_SetDate(&hrtc, &date, RTC_FORMAT_BIN) != HAL_OK)
    {
        rtc_available = false;
    }
    unlock_rtc();
}

void rtc_get_time(rtc_date_time_t *date_time)
{
    if (date_time == NULL)
    {
        return;
    }
    memset(date_time, 0, sizeof *date_time);
    if (!clock_rtc_ready() || !lock_rtc())
    {
        return;
    }

    RTC_TimeTypeDef time = {0};
    RTC_DateTypeDef date = {0};
    /* GetTime locks the shadow calendar; GetDate MUST follow to unlock it. */
    HAL_StatusTypeDef time_status = HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN);
    HAL_StatusTypeDef date_status = HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BIN);
    if (time_status == HAL_OK && date_status == HAL_OK)
    {
        rtc_date_time_t result = {
            .year = (uint16_t)(2000U + date.Year),
            .month = date.Month, .day = date.Date,
            .hour = time.Hours, .minute = time.Minutes, .second = time.Seconds,
            .weekday = date.WeekDay
        };
        if (date_time_valid(&result))
        {
            *date_time = result;
        }
    }
    unlock_rtc();
}
