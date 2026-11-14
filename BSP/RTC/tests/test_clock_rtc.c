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
/**
 * 功能：模拟始终处于运行状态的调度器。
 * 参数：无。
 * 返回：固定返回运行状态值 2。
 */
int xTaskGetSchedulerState(void) {
    return 2;
}
/**
 * 功能：使用模拟 RTC 句柄作为互斥锁标识。
 * 参数：无。
 * 返回：模拟 RTC 互斥锁句柄。
 */
SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    return &hrtc;
}
/**
 * 功能：检查互斥锁身份与获取次序，并增加模拟锁深度。
 * 参数：lock，模拟互斥锁句柄；timeout，等待超时值。
 * 返回：固定返回 pdTRUE。
 */
int xSemaphoreTake(SemaphoreHandle_t lock, TickType_t timeout) {
    (void)timeout;
    assert(lock == &hrtc && lock_depth++ == 0);
    return pdTRUE;
}
/**
 * 功能：检查互斥锁身份与释放次序，并减少模拟锁深度。
 * 参数：lock，模拟互斥锁句柄。
 * 返回：固定返回 pdTRUE。
 */
int xSemaphoreGive(SemaphoreHandle_t lock) {
    assert(lock == &hrtc && --lock_depth == 0);
    return pdTRUE;
}
/**
 * 功能：提供保持模拟日历内容的空硬件初始化桩。
 * 参数：无。
 * 返回：无。
 */
void MX_RTC_Init(void) {
}
/**
 * 功能：返回测试设定的 RTC 硬件就绪标记。
 * 参数：无。
 * 返回：模拟硬件就绪时返回 true，否则返回 false。
 */
bool MX_RTC_IsReady(void) {
    return hardware_ready;
}
/**
 * 功能：校验 RTC 句柄及读取顺序，并输出已保存的模拟时间。
 * 参数：rtc，待校验的 RTC 句柄；out，接收模拟日历字段的地址；fmt，要求为二进制的 RTC 数据格式。
 * 返回：注入读取失败时返回 HAL_ERROR，否则返回 HAL_OK。
 */
HAL_StatusTypeDef HAL_RTC_GetTime(RTC_HandleTypeDef *rtc, RTC_TimeTypeDef *out, uint32_t fmt) {
    assert(rtc == &hrtc && fmt == RTC_FORMAT_BIN && read_phase++ == 0);
    *out = stored_time;
    return fail_read ? HAL_ERROR : HAL_OK;
}
/**
 * 功能：校验先时间后日期的读取顺序，输出日期并释放模拟影子锁。
 * 参数：rtc，待校验的 RTC 句柄；out，接收模拟日历字段的地址；fmt，要求为二进制的 RTC 数据格式。
 * 返回：固定返回 HAL_OK。
 */
HAL_StatusTypeDef HAL_RTC_GetDate(RTC_HandleTypeDef *rtc, RTC_DateTypeDef *out, uint32_t fmt) {
    assert(rtc == &hrtc && fmt == RTC_FORMAT_BIN && read_phase == 1);
    read_phase = 0;
    *out = stored_date;
    return HAL_OK;
}
/**
 * 功能：校验二进制格式并保存模拟 RTC 时间，累计写入次数。
 * 参数：rtc，待校验的 RTC 句柄；in，待保存的模拟日历字段；fmt，要求为二进制的 RTC 数据格式。
 * 返回：固定返回 HAL_OK。
 */
HAL_StatusTypeDef HAL_RTC_SetTime(RTC_HandleTypeDef *rtc, RTC_TimeTypeDef *in, uint32_t fmt) {
    assert(rtc == &hrtc && fmt == RTC_FORMAT_BIN);
    stored_time = *in;
    ++writes;
    return HAL_OK;
}
/**
 * 功能：校验二进制格式并保存模拟 RTC 日期，累计写入次数。
 * 参数：rtc，待校验的 RTC 句柄；in，待保存的模拟日历字段；fmt，要求为二进制的 RTC 数据格式。
 * 返回：固定返回 HAL_OK。
 */
HAL_StatusTypeDef HAL_RTC_SetDate(RTC_HandleTypeDef *rtc, RTC_DateTypeDef *in, uint32_t fmt) {
    assert(rtc == &hrtc && fmt == RTC_FORMAT_BIN);
    stored_date = *in;
    ++writes;
    return HAL_OK;
}

/**
 * 功能：运行 RTC 日历保留、日期有效性、影子寄存器读序与不可用状态测试。
 * 参数：无。
 * 返回：全部检查通过返回 0，检查失败时终止进程。
 */
int main(void) {
    rtc_date_time_t out;
    rtc_init();
    assert(clock_rtc_ready());
    assert(writes == 0); /* 重启初始化必须保留已有日历。 */
    rtc_get_time(&out);
    assert(out.year == 2024 && out.month == 2 && out.day == 29 && out.weekday == 4);
    assert(out.hour == 23 && out.minute == 59 && out.second == 58);

    rtc_date_time_t invalid = {2025, 2, 29, 1, 2, 3, 6};
    rtc_set_time(&invalid);
    assert(writes == 0); /* HAL 的字段范围断言不能校验日期有效性。 */
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
    assert(read_phase == 0); /* 读取时间失败时仍须解锁影子寄存器。 */
    hardware_ready = false;
    rtc_init();
    assert(!clock_rtc_ready());
    memset(&out, 0xAA, sizeof out);
    rtc_get_time(&out);
    assert(out.year == 0 && out.month == 0 && out.second == 0);
    puts("RTC retention, calendar validation, read order, and unavailable-time tests passed");
    return 0;
}
