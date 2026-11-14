/* 使用真实 Core rtc.c 控制模拟硬件，仅替换 HAL 与寄存器边界。 */
#include <assert.h>
#include <stdio.h>
#include "rtc.h"

RTC_TypeDef test_rtc_instance;
uint32_t test_clock_source;
bool test_rtc_enabled;
static int osc_calls, peripheral_calls, init_calls, writes, fatal_errors;
static HAL_StatusTypeDef oscillator_status = HAL_OK, synchronize_status = HAL_OK;
/**
 * 功能：提供备份域写权限的空硬件边界桩。
 * 参数：无。
 * 返回：无。
 */
void HAL_PWR_EnableBkUpAccess(void) {
}
/**
 * 功能：累计模拟硬件初始化的致命错误次数。
 * 参数：无。
 * 返回：无。
 */
void Error_Handler(void) {
    ++fatal_errors;
}
/**
 * 功能：校验仅开启外部低速晶振且不修改锁相环。
 * 参数：osc，待校验的振荡器配置。
 * 返回：注入的晶振配置状态。
 */
HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef *osc) {
    assert(osc->OscillatorType == RCC_OSCILLATORTYPE_LSE && osc->LSEState == RCC_LSE_ON);
    assert(osc->PLL.PLLState == RCC_PLL_NONE);
    ++osc_calls;
    return oscillator_status;
}
/**
 * 功能：校验已有时钟源未被替换，并记录外部低速 RTC 时钟配置。
 * 参数：config，待校验的外设时钟配置。
 * 返回：固定返回 HAL_OK。
 */
HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *config) {
    /* 切换已配置的时钟源会清除备份域。 */
    assert(test_clock_source == 0 || test_clock_source == RCC_RTCCLKSOURCE_LSE);
    assert(config->PeriphClockSelection == RCC_PERIPHCLK_RTC);
    assert(config->RTCClockSelection == RCC_RTCCLKSOURCE_LSE);
    test_clock_source = config->RTCClockSelection;
    ++peripheral_calls;
    return HAL_OK;
}
/**
 * 功能：校验 RTC 使能与分频配置，并模拟日历已初始化状态。
 * 参数：rtc，待校验的 RTC 句柄。
 * 返回：固定返回 HAL_OK。
 */
HAL_StatusTypeDef HAL_RTC_Init(RTC_HandleTypeDef *rtc) {
    assert(test_rtc_enabled && rtc->Instance == RTC);
    assert(rtc->Init.AsynchPrediv == 127 && rtc->Init.SynchPrediv == 255);
    ++init_calls;
    test_rtc_instance.ISR = 1;
    return HAL_OK;
}
/**
 * 功能：校验 RTC 句柄并返回注入的影子寄存器同步结果。
 * 参数：rtc，待校验的 RTC 句柄。
 * 返回：注入的同步状态。
 */
HAL_StatusTypeDef HAL_RTC_WaitForSynchro(RTC_HandleTypeDef *rtc) {
    assert(rtc == &hrtc);
    return synchronize_status;
}
/**
 * 功能：校验首次初始化的零点时间并累计写入次数。
 * 参数：rtc，待校验的 RTC 句柄；time，待校验的初始 RTC 时间；fmt，要求为二进制的 RTC 数据格式。
 * 返回：固定返回 HAL_OK。
 */
HAL_StatusTypeDef HAL_RTC_SetTime(RTC_HandleTypeDef *rtc, RTC_TimeTypeDef *time, uint32_t fmt) {
    assert(rtc == &hrtc && fmt == RTC_FORMAT_BIN && time->Hours == 0);
    ++writes;
    return HAL_OK;
}
/**
 * 功能：校验首次初始化的2000 年 1 月 1 日日期并累计写入次数。
 * 参数：rtc，待校验的 RTC 句柄；date，接收解析日期时间的地址；fmt，要求为二进制的 RTC 数据格式。
 * 返回：固定返回 HAL_OK。
 */
HAL_StatusTypeDef HAL_RTC_SetDate(RTC_HandleTypeDef *rtc, RTC_DateTypeDef *date, uint32_t fmt) {
    assert(rtc == &hrtc && fmt == RTC_FORMAT_BIN);
    assert(date->Year == 0 && date->Month == 1 && date->Date == 1 && date->WeekDay == 6);
    ++writes;
    return HAL_OK;
}
/**
 * 功能：运行 RTC 日历保留、首次上电、晶振失败、同步失败及原时钟源保护测试。
 * 参数：无。
 * 返回：全部检查通过返回 0，检查失败时终止进程。
 */
int main(void) {
    test_clock_source = RCC_RTCCLKSOURCE_LSE;
    test_rtc_instance.ISR = 1; /* 模拟微控制器复位后保留的已有日历。 */
    MX_RTC_Init();
    assert(MX_RTC_IsReady() && init_calls == 1 && writes == 0 && fatal_errors == 0);
    MX_RTC_Init();
    assert(MX_RTC_IsReady() && init_calls == 2 && writes == 0);

    test_clock_source = 0;
    test_rtc_instance.ISR = 0; /* 模拟首次使用的备份域。 */
    MX_RTC_Init();
    assert(MX_RTC_IsReady() && writes == 2);
    MX_RTC_Init();
    assert(MX_RTC_IsReady() && writes == 2);

    oscillator_status = HAL_TIMEOUT; /* 模拟 32.768 kHz 晶振缺失或故障。 */
    int previous_init_calls = init_calls;
    MX_RTC_Init();
    assert(!MX_RTC_IsReady() && init_calls == previous_init_calls && fatal_errors == 0);

    oscillator_status = HAL_OK;
    synchronize_status = HAL_TIMEOUT;
    MX_RTC_Init();
    assert(!MX_RTC_IsReady() && fatal_errors == 0 && writes == 2);

    synchronize_status = HAL_OK;
    test_clock_source = 2; /* 已有其他 RTC 时钟源必须原样保留。 */
    int previous_osc_calls = osc_calls, previous_peripheral_calls = peripheral_calls;
    MX_RTC_Init();
    assert(!MX_RTC_IsReady() && fatal_errors == 0 && test_clock_source == 2);
    assert(osc_calls == previous_osc_calls && peripheral_calls == previous_peripheral_calls);
    puts(
        "Core RTC retained-calendar, first-powerup, LSE timeout, sync timeout, and source protection tests passed");
    return 0;
}
