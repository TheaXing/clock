/* USER CODE BEGIN Header */
/**
 * @file main.h
 * @brief 声明主程序接口及应用公共定义。
 ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* 防止头文件重复包含 */
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* 头文件 */
#include "stm32f4xx_hal.h"

/* 内部头文件 */
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* 对外类型 */
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* 对外常量 */
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* 对外宏 */
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* 对外函数声明 */
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* 内部常量定义 */
#define LCD_CS_Pin GPIO_PIN_2
#define LCD_CS_GPIO_Port GPIOE
#define LCD_RST_Pin GPIO_PIN_3
#define LCD_RST_GPIO_Port GPIOE
#define LCD_DC_Pin GPIO_PIN_4
#define LCD_DC_GPIO_Port GPIOE
#define LCD_BL_Pin GPIO_PIN_5
#define LCD_BL_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */
#define KEY1_Pin GPIO_PIN_0
#define KEY1_GPIO_Port GPIOA
#define KEY2_Pin GPIO_PIN_4
#define KEY2_GPIO_Port GPIOC
#define KEY3_Pin GPIO_PIN_5
#define KEY3_GPIO_Port GPIOC
#define BUZZER_Pin GPIO_PIN_7
#define BUZZER_GPIO_Port GPIOC
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* 条件结束：__MAIN_H */
