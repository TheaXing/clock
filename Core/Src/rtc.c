/* USER CODE BEGIN Header */
/** RTC LSE initialization with bounded failure and retained backup calendar. */
/* USER CODE END Header */
#include "rtc.h"

RTC_HandleTypeDef hrtc;

/* USER CODE BEGIN 0 */
static bool rtc_hardware_ready;

static void clock_rtc_hardware_init(void)
{
  RCC_OscInitTypeDef oscillator = {0};
  RCC_PeriphCLKInitTypeDef peripheral_clock = {0};
  rtc_hardware_ready = false;

  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();
  uint32_t source = __HAL_RCC_GET_RTC_SOURCE();
  if (source != 0U && source != RCC_RTCCLKSOURCE_LSE)
  {
    /* Switching a populated backup domain would erase its calendar. */
    return;
  }

  oscillator.OscillatorType = RCC_OSCILLATORTYPE_LSE;
  oscillator.LSEState = RCC_LSE_ON;
  oscillator.PLL.PLLState = RCC_PLL_NONE;
  /* HAL has a finite LSE_STARTUP_TIMEOUT; do not halt the display if absent. */
  if (HAL_RCC_OscConfig(&oscillator) != HAL_OK)
  {
    return;
  }
  peripheral_clock.PeriphClockSelection = RCC_PERIPHCLK_RTC;
  peripheral_clock.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
  if (HAL_RCCEx_PeriphCLKConfig(&peripheral_clock) != HAL_OK)
  {
    return;
  }
  __HAL_RCC_RTC_ENABLE();

  hrtc.Instance = RTC;
  bool retained_calendar = __HAL_RTC_IS_CALENDAR_INITIALIZED(&hrtc) != 0U;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK || HAL_RTC_WaitForSynchro(&hrtc) != HAL_OK)
  {
    return;
  }
  if (!retained_calendar)
  {
    /* Safe initial calendar only on first power-up; network time replaces it. */
    RTC_TimeTypeDef time = {0};
    RTC_DateTypeDef date = {0};
    date.Year = 0;
    date.Month = RTC_MONTH_JANUARY;
    date.Date = 1;
    date.WeekDay = RTC_WEEKDAY_SATURDAY;
    if (HAL_RTC_SetTime(&hrtc, &time, RTC_FORMAT_BIN) != HAL_OK ||
        HAL_RTC_SetDate(&hrtc, &date, RTC_FORMAT_BIN) != HAL_OK)
    {
      return;
    }
  }
  rtc_hardware_ready = true;
}

bool MX_RTC_IsReady(void)
{
  return rtc_hardware_ready;
}
/* USER CODE END 0 */

void MX_RTC_Init(void)
{
  /* USER CODE BEGIN RTC_Init 0 */
  /* Keep this early return: regenerated Cube defaults must not reset the date. */
  clock_rtc_hardware_init();
  return;
  /* USER CODE END RTC_Init 0 */

  /* USER CODE BEGIN RTC_Init 1 */
  /* USER CODE END RTC_Init 1 */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */
  /* USER CODE END RTC_Init 2 */
}

void HAL_RTC_MspInit(RTC_HandleTypeDef *rtcHandle)
{
  if (rtcHandle->Instance == RTC)
  {
    /* USER CODE BEGIN RTC_MspInit 0 */
    /* USER CODE END RTC_MspInit 0 */
    __HAL_RCC_RTC_ENABLE();
    /* USER CODE BEGIN RTC_MspInit 1 */
    /* USER CODE END RTC_MspInit 1 */
  }
}

void HAL_RTC_MspDeInit(RTC_HandleTypeDef *rtcHandle)
{
  if (rtcHandle->Instance == RTC)
  {
    /* USER CODE BEGIN RTC_MspDeInit 0 */
    /* USER CODE END RTC_MspDeInit 0 */
    __HAL_RCC_RTC_DISABLE();
    /* USER CODE BEGIN RTC_MspDeInit 1 */
    rtc_hardware_ready = false;
    /* USER CODE END RTC_MspDeInit 1 */
  }
}
/* USER CODE BEGIN 1 */
/* USER CODE END 1 */
