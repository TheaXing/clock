/* TIM3 is dedicated to the PC7 passive buzzer. TIM7 remains the HAL timebase. */
#ifndef __TIM_H__
#define __TIM_H__
#ifdef __cplusplus
extern "C" {
#endif
#include "main.h"
extern TIM_HandleTypeDef htim3;
void MX_TIM3_Init(void);
void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);
#ifdef __cplusplus
}
#endif
#endif
