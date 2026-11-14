#include "clock_rtc.h"
#include "rtc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <stddef.h>
#include <string.h>

static SemaphoreHandle_t rtc_mutex;
static bool rtc_available;

/**
 * 功能：检查 RTC 日期时间字段、月份天数及闰年日期。
 * 参数：value，待校验的日期时间。
 * 返回：日期时间有效返回 true，否则返回 false。
 */
static bool date_time_valid(const rtc_date_time_t *value) {
    static const uint8_t month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (value == NULL || value->year < 2000U || value->year > 2099U || value->month < 1U ||
        value->month > 12U || value->day < 1U || value->weekday < 1U || value->weekday > 7U ||
        value->hour > 23U || value->minute > 59U || value->second > 59U) {
        return false;
    }
    uint8_t days = month_days[value->month - 1U];
    if (value->month == 2U && (value->year % 4U) == 0U) {
        ++days; /* 2000～2099 年内无需额外处理公历世纪年例外。 */
    }
    return value->day <= days;
}

/**
 * 功能：在调度器运行时取得 RTC 互斥锁，启动前直接允许访问。
 * 参数：无。
 * 返回：允许访问 RTC 返回 true，否则返回 false。
 */
static bool lock_rtc(void) {
    return xTaskGetSchedulerState() != taskSCHEDULER_RUNNING ||
           (rtc_mutex != NULL && xSemaphoreTake(rtc_mutex, portMAX_DELAY) == pdTRUE);
}

/**
 * 功能：在调度器运行时释放 RTC 互斥锁。
 * 参数：无。
 * 返回：无。
 */
static void unlock_rtc(void) {
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        (void)xSemaphoreGive(rtc_mutex);
    }
}

/**
 * 功能：创建 RTC 互斥锁并初始化保留已有日历的硬件。
 * 参数：无。
 * 返回：无。
 */
void rtc_init(void) {
    rtc_available = false;
    if (rtc_mutex == NULL) {
        rtc_mutex = xSemaphoreCreateMutex();
    }
    if (rtc_mutex == NULL) {
        return;
    }
    MX_RTC_Init();
    rtc_available = MX_RTC_IsReady();
}

/**
 * 功能：查询软件初始化与 RTC 硬件就绪状态。
 * 参数：无。
 * 返回：二者均就绪返回 true，否则返回 false。
 */
bool clock_rtc_ready(void) {
    return rtc_available && MX_RTC_IsReady();
}

/**
 * 功能：校验日历后互斥写入 RTC 时间与日期，HAL 失败时禁用可用状态。
 * 参数：date_time，待写入的有效日期时间。
 * 返回：无。
 */
void rtc_set_time(const rtc_date_time_t *date_time) {
    if (!date_time_valid(date_time) || !clock_rtc_ready() || !lock_rtc()) {
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
        HAL_RTC_SetDate(&hrtc, &date, RTC_FORMAT_BIN) != HAL_OK) {
        rtc_available = false;
    }
    unlock_rtc();
}

/**
 * 功能：互斥读取时间后读取日期以解锁影子日历，并校验结果。
 * 参数：date_time，接收日期时间的地址，失败时写入全零。
 * 返回：无；RTC 不可用或读取失败时输出全零。
 */
void rtc_get_time(rtc_date_time_t *date_time) {
    if (date_time == NULL) {
        return;
    }
    memset(date_time, 0, sizeof *date_time);
    if (!clock_rtc_ready() || !lock_rtc()) {
        return;
    }

    RTC_TimeTypeDef time = {0};
    RTC_DateTypeDef date = {0};
    /* GetTime 会锁定影子日历寄存器，随后必须调用 GetDate 解锁。 */
    HAL_StatusTypeDef time_status = HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN);
    HAL_StatusTypeDef date_status = HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BIN);
    if (time_status == HAL_OK && date_status == HAL_OK) {
        rtc_date_time_t result = {.year = (uint16_t)(2000U + date.Year),
                                  .month = date.Month,
                                  .day = date.Date,
                                  .hour = time.Hours,
                                  .minute = time.Minutes,
                                  .second = time.Seconds,
                                  .weekday = date.WeekDay};
        if (date_time_valid(&result)) {
            *date_time = result;
        }
    }
    unlock_rtc();
}
