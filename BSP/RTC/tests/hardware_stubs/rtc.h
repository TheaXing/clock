#ifndef TEST_HARDWARE_RTC_H
#define TEST_HARDWARE_RTC_H
#include <stdint.h>
#include <stdbool.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { uint32_t ISR; } RTC_TypeDef;
extern RTC_TypeDef test_rtc_instance;
#define RTC (&test_rtc_instance)
typedef struct { uint32_t HourFormat, AsynchPrediv, SynchPrediv, OutPut, OutPutPolarity, OutPutType; } RTC_InitTypeDef;
typedef struct { RTC_TypeDef *Instance; RTC_InitTypeDef Init; } RTC_HandleTypeDef;
typedef struct { uint8_t Hours, Minutes, Seconds; } RTC_TimeTypeDef;
typedef struct { uint8_t WeekDay, Month, Date, Year; } RTC_DateTypeDef;
typedef struct { uint32_t OscillatorType, LSEState; struct { uint32_t PLLState; } PLL; } RCC_OscInitTypeDef;
typedef struct { uint32_t PeriphClockSelection, RTCClockSelection; } RCC_PeriphCLKInitTypeDef;
extern uint32_t test_clock_source;
extern bool test_rtc_enabled;
#define __HAL_RCC_PWR_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GET_RTC_SOURCE() test_clock_source
#define __HAL_RCC_RTC_ENABLE() (test_rtc_enabled = true)
#define __HAL_RCC_RTC_DISABLE() (test_rtc_enabled = false)
#define __HAL_RTC_IS_CALENDAR_INITIALIZED(handle) ((handle)->Instance->ISR != 0U)
#define RCC_RTCCLKSOURCE_LSE 1U
#define RCC_OSCILLATORTYPE_LSE 2U
#define RCC_LSE_ON 1U
#define RCC_PLL_NONE 0U
#define RCC_PERIPHCLK_RTC 1U
#define RTC_HOURFORMAT_24 0U
#define RTC_OUTPUT_DISABLE 0U
#define RTC_OUTPUT_POLARITY_HIGH 0U
#define RTC_OUTPUT_TYPE_OPENDRAIN 0U
#define RTC_MONTH_JANUARY 1U
#define RTC_WEEKDAY_SATURDAY 6U
#define RTC_FORMAT_BIN 0U
void HAL_PWR_EnableBkUpAccess(void);
HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef *);
HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *);
HAL_StatusTypeDef HAL_RTC_Init(RTC_HandleTypeDef *);
HAL_StatusTypeDef HAL_RTC_WaitForSynchro(RTC_HandleTypeDef *);
HAL_StatusTypeDef HAL_RTC_SetTime(RTC_HandleTypeDef *, RTC_TimeTypeDef *, uint32_t);
HAL_StatusTypeDef HAL_RTC_SetDate(RTC_HandleTypeDef *, RTC_DateTypeDef *, uint32_t);
void Error_Handler(void);
void MX_RTC_Init(void);
bool MX_RTC_IsReady(void);
extern RTC_HandleTypeDef hrtc;
#endif
