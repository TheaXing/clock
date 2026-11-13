#ifndef ALARM_TEST_TIM_H
#define ALARM_TEST_TIM_H
#include "main.h"
typedef struct { uint32_t compare; } TIM_HandleTypeDef;
extern TIM_HandleTypeDef htim3;
#define TIM_CHANNEL_2 4U
#define __HAL_TIM_SET_COMPARE(timer, channel, value) ((timer)->compare = (value))
void MX_TIM3_Init(void);
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *, uint32_t);
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *, uint32_t);
#endif
