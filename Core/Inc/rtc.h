/* USER CODE BEGIN Header */
/** RTC 外设配置，应用层适配接口见 clock_rtc.h。 */
/* USER CODE END Header */
#ifndef __RTC_H__
#define __RTC_H__
#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
/* USER CODE BEGIN Includes */
#include <stdbool.h>
/* USER CODE END Includes */

extern RTC_HandleTypeDef hrtc;
void MX_RTC_Init(void);

/* USER CODE BEGIN Prototypes */
bool MX_RTC_IsReady(void);
/* USER CODE END Prototypes */
#ifdef __cplusplus
}
#endif
#endif
