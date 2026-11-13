#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "clock_rtc.h"
#include "rtc.h"
#include "semphr.h"

RTC_HandleTypeDef hrtc;
static bool hardware_ready = true;
static bool fail_read;
static int read_phase, writes;
static RTC_TimeTypeDef stored_time = {23, 59, 58, 0, 0, 0};
static RTC_DateTypeDef stored_date = {4, 2, 29, 24};
static int lock_depth;
int xTaskGetSchedulerState(void) { return 2; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &hrtc; }
int xSemaphoreTake(SemaphoreHandle_t lock, TickType_t timeout)
{ (void)timeout; assert(lock == &hrtc && lock_depth++ == 0); return pdTRUE; }
int xSemaphoreGive(SemaphoreHandle_t lock)
{ assert(lock == &hrtc && --lock_depth == 0); return pdTRUE; }
void MX_RTC_Init(void) { }
bool MX_RTC_IsReady(void) { return hardware_ready; }
HAL_StatusTypeDef HAL_RTC_GetTime(RTC_HandleTypeDef *rtc, RTC_TimeTypeDef *out, uint32_t fmt)
{ assert(rtc == &hrtc && fmt == RTC_FORMAT_BIN && read_phase++ == 0); *out = stored_time; return fail_read ? HAL_ERROR : HAL_OK; }
HAL_StatusTypeDef HAL_RTC_GetDate(RTC_HandleTypeDef *rtc, RTC_DateTypeDef *out, uint32_t fmt)
{ assert(rtc == &hrtc && fmt == RTC_FORMAT_BIN && read_phase == 1); read_phase = 0; *out = stored_date; return HAL_OK; }
HAL_StatusTypeDef HAL_RTC_SetTime(RTC_HandleTypeDef *rtc, RTC_TimeTypeDef *in, uint32_t fmt)
{ assert(rtc == &hrtc && fmt == RTC_FORMAT_BIN); stored_time = *in; ++writes; return HAL_OK; }
HAL_StatusTypeDef HAL_RTC_SetDate(RTC_HandleTypeDef *rtc, RTC_DateTypeDef *in, uint32_t fmt)
{ assert(rtc == &hrtc && fmt == RTC_FORMAT_BIN); stored_date = *in; ++writes; return HAL_OK; }

int main(void)
{
    rtc_date_time_t out;
    rtc_init();
    assert(clock_rtc_ready());
    assert(writes == 0); /* Reboot initialization must preserve the calendar. */
    rtc_get_time(&out);
    assert(out.year == 2024 && out.month == 2 && out.day == 29 && out.weekday == 4);
    assert(out.hour == 23 && out.minute == 59 && out.second == 58);

    rtc_date_time_t invalid = {2025, 2, 29, 1, 2, 3, 6};
    rtc_set_time(&invalid);
    assert(writes == 0); /* The HAL's field range asserts do not validate dates. */
    invalid.year = 1999;
    rtc_set_time(&invalid);
    assert(writes == 0);
    rtc_date_time_t valid = {2028, 2, 29, 12, 34, 56, 2};
    rtc_set_time(&valid);
    assert(writes == 2);
    rtc_get_time(&out);
    assert(out.year == 2028 && out.month == 2 && out.day == 29);
    assert(out.hour == 12 && out.minute == 34 && out.second == 56 && out.weekday == 2);

    fail_read = true;
    memset(&out, 0xAA, sizeof out);
    rtc_get_time(&out);
    assert(out.year == 0 && out.month == 0 && out.day == 0 && out.hour == 0);
    assert(read_phase == 0); /* Unlock the shadow registers even on a failed time read. */
    hardware_ready = false;
    rtc_init();
    assert(!clock_rtc_ready());
    memset(&out, 0xAA, sizeof out);
    rtc_get_time(&out);
    assert(out.year == 0 && out.month == 0 && out.second == 0);
    puts("RTC retention, calendar validation, read order, and unavailable-time tests passed");
    return 0;
}
