/* The real Core rtc.c controls the hardware fake; only HAL/register boundaries vary. */
#include <assert.h>
#include <stdio.h>
#include "rtc.h"

RTC_TypeDef test_rtc_instance;
uint32_t test_clock_source;
bool test_rtc_enabled;
static int osc_calls, peripheral_calls, init_calls, writes, fatal_errors;
static HAL_StatusTypeDef oscillator_status = HAL_OK, synchronize_status = HAL_OK;
void HAL_PWR_EnableBkUpAccess(void) { }
void Error_Handler(void) { ++fatal_errors; }
HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef *osc)
{
    assert(osc->OscillatorType == RCC_OSCILLATORTYPE_LSE && osc->LSEState == RCC_LSE_ON);
    assert(osc->PLL.PLLState == RCC_PLL_NONE);
    ++osc_calls; return oscillator_status;
}
HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *config)
{
    /* A switch away from a populated source would erase the backup domain. */
    assert(test_clock_source == 0 || test_clock_source == RCC_RTCCLKSOURCE_LSE);
    assert(config->PeriphClockSelection == RCC_PERIPHCLK_RTC);
    assert(config->RTCClockSelection == RCC_RTCCLKSOURCE_LSE);
    test_clock_source = config->RTCClockSelection;
    ++peripheral_calls; return HAL_OK;
}
HAL_StatusTypeDef HAL_RTC_Init(RTC_HandleTypeDef *rtc)
{
    assert(test_rtc_enabled && rtc->Instance == RTC);
    assert(rtc->Init.AsynchPrediv == 127 && rtc->Init.SynchPrediv == 255);
    ++init_calls; test_rtc_instance.ISR = 1; return HAL_OK;
}
HAL_StatusTypeDef HAL_RTC_WaitForSynchro(RTC_HandleTypeDef *rtc)
{ assert(rtc == &hrtc); return synchronize_status; }
HAL_StatusTypeDef HAL_RTC_SetTime(RTC_HandleTypeDef *rtc, RTC_TimeTypeDef *time, uint32_t fmt)
{ assert(rtc == &hrtc && fmt == RTC_FORMAT_BIN && time->Hours == 0); ++writes; return HAL_OK; }
HAL_StatusTypeDef HAL_RTC_SetDate(RTC_HandleTypeDef *rtc, RTC_DateTypeDef *date, uint32_t fmt)
{
    assert(rtc == &hrtc && fmt == RTC_FORMAT_BIN);
    assert(date->Year == 0 && date->Month == 1 && date->Date == 1 && date->WeekDay == 6);
    ++writes; return HAL_OK;
}
int main(void)
{
    test_clock_source = RCC_RTCCLKSOURCE_LSE;
    test_rtc_instance.ISR = 1; /* Existing calendar across MCU reset. */
    MX_RTC_Init();
    assert(MX_RTC_IsReady() && init_calls == 1 && writes == 0 && fatal_errors == 0);
    MX_RTC_Init();
    assert(MX_RTC_IsReady() && init_calls == 2 && writes == 0);

    test_clock_source = 0; test_rtc_instance.ISR = 0; /* New backup domain. */
    MX_RTC_Init();
    assert(MX_RTC_IsReady() && writes == 2);
    MX_RTC_Init();
    assert(MX_RTC_IsReady() && writes == 2);

    oscillator_status = HAL_TIMEOUT; /* Missing or failed 32.768 kHz crystal. */
    int previous_init_calls = init_calls;
    MX_RTC_Init();
    assert(!MX_RTC_IsReady() && init_calls == previous_init_calls && fatal_errors == 0);

    oscillator_status = HAL_OK;
    synchronize_status = HAL_TIMEOUT;
    MX_RTC_Init();
    assert(!MX_RTC_IsReady() && fatal_errors == 0 && writes == 2);

    synchronize_status = HAL_OK;
    test_clock_source = 2; /* Existing other RTC source must be left intact. */
    int previous_osc_calls = osc_calls, previous_peripheral_calls = peripheral_calls;
    MX_RTC_Init();
    assert(!MX_RTC_IsReady() && fatal_errors == 0 && test_clock_source == 2);
    assert(osc_calls == previous_osc_calls && peripheral_calls == previous_peripheral_calls);
    puts("Core RTC retained-calendar, first-powerup, LSE timeout, sync timeout, and source protection tests passed");
    return 0;
}
