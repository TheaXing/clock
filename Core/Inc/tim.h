/* TIM3 专用于 PC7 上的无源蜂鸣器，TIM7 继续作为 HAL 时基。 */
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
