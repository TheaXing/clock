/* USER CODE BEGIN Header */
/**
 * @file stm32f4xx_it.h
 * @brief 声明中断与异常处理函数。
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
#ifndef __STM32F4xx_IT_H
#define __STM32F4xx_IT_H

#ifdef __cplusplus
extern "C" {
#endif

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
void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void DebugMon_Handler(void);
void TIM7_IRQHandler(void);
void USART2_IRQHandler(void);
/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

#ifdef __cplusplus
}
#endif

#endif /* 条件结束：__STM32F4xx_IT_H */
