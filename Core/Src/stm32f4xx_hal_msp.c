/* USER CODE BEGIN Header */
/**
 * @file stm32f4xx_hal_msp.c
 * @brief 实现底层硬件初始化与反初始化。
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
/* 头文件 */
#include "main.h"
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* 内部类型定义 */
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* 内部常量定义 */
/* USER CODE BEGIN Define */

/* USER CODE END Define */

/* 内部宏 */
/* USER CODE BEGIN Macro */

/* USER CODE END Macro */

/* 内部变量 */
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* 内部函数声明 */
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* 外部函数 */
/* USER CODE BEGIN ExternalFunctions */

/* USER CODE END ExternalFunctions */

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */
/** 初始化全局底层支持。 */
/**
 * @brief 功能：初始化 HAL 全局底层支持，开启 SYSCFG 和电源接口时钟。
 * 参数：无。
 * @retval 返回：无
 */
void HAL_MspInit(void) {

    /* USER CODE BEGIN MspInit 0 */

    /* USER CODE END MspInit 0 */

    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();

    /* 初始化系统中断 */

    /* USER CODE BEGIN MspInit 1 */

    /* USER CODE END MspInit 1 */
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
