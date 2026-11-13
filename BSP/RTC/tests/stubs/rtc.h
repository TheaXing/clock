#ifndef TEST_RTC_H
#define TEST_RTC_H
#include <stdint.h>
#include <stdbool.h>
typedef struct { int unused; } RTC_HandleTypeDef;
typedef struct { uint8_t Hours, Minutes, Seconds, TimeFormat; uint32_t DayLightSaving, StoreOperation; } RTC_TimeTypeDef;
typedef struct { uint8_t WeekDay, Month, Date, Year; } RTC_DateTypeDef;
typedef enum { HAL_OK = 0, HAL_ERROR = 1 } HAL_StatusTypeDef;
#define RTC_FORMAT_BIN 0U
#define RTC_HOURFORMAT12_AM 0U
#define RTC_DAYLIGHTSAVING_NONE 0U
#define RTC_STOREOPERATION_RESET 0U
extern RTC_HandleTypeDef hrtc;
void MX_RTC_Init(void);
bool MX_RTC_IsReady(void);
HAL_StatusTypeDef HAL_RTC_GetTime(RTC_HandleTypeDef *, RTC_TimeTypeDef *, uint32_t);
HAL_StatusTypeDef HAL_RTC_GetDate(RTC_HandleTypeDef *, RTC_DateTypeDef *, uint32_t);
HAL_StatusTypeDef HAL_RTC_SetTime(RTC_HandleTypeDef *, RTC_TimeTypeDef *, uint32_t);
HAL_StatusTypeDef HAL_RTC_SetDate(RTC_HandleTypeDef *, RTC_DateTypeDef *, uint32_t);
#endif
