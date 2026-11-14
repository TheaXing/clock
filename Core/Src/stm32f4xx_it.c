/* USER CODE BEGIN Header */
/**
 * @file stm32f4xx_it.c
 * @brief 实现中断与异常处理函数。
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
#include "stm32f4xx_it.h"
/* 内部头文件 */
/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* 内部类型定义 */
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* 内部常量定义 */
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* 内部宏 */
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* 内部变量 */
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* 内部函数声明 */
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* 用户内部代码 */
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* 外部变量 */
extern TIM_HandleTypeDef htim7;
extern UART_HandleTypeDef huart2;

/* USER CODE BEGIN EV */

/* USER CODE END EV */

/* Cortex-M4 处理器中断与异常处理函数 */

/**
 * @brief 功能：响应不可屏蔽中断并停留在异常状态。
 * 参数：无。
 * @retval 返回：无，函数持续等待。
 */
void NMI_Handler(void) {
    /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

    /* USER CODE END NonMaskableInt_IRQn 0 */
    /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
    while (1) {
    }
    /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
 * @brief 功能：响应硬故障异常并停留在异常状态。
 * 参数：无。
 * @retval 返回：无，函数持续等待。
 */
void HardFault_Handler(void) {
    /* USER CODE BEGIN HardFault_IRQn 0 */

    /* USER CODE END HardFault_IRQn 0 */
    while (1) {
        /* USER CODE BEGIN W1_HardFault_IRQn 0 */
        /* USER CODE END W1_HardFault_IRQn 0 */
    }
}

/**
 * @brief 功能：响应内存管理故障并停留在异常状态。
 * 参数：无。
 * @retval 返回：无，函数持续等待。
 */
void MemManage_Handler(void) {
    /* USER CODE BEGIN MemoryManagement_IRQn 0 */

    /* USER CODE END MemoryManagement_IRQn 0 */
    while (1) {
        /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
        /* USER CODE END W1_MemoryManagement_IRQn 0 */
    }
}

/**
 * @brief 功能：响应指令预取或数据访问总线故障并停留在异常状态。
 * 参数：无。
 * @retval 返回：无，函数持续等待。
 */
void BusFault_Handler(void) {
    /* USER CODE BEGIN BusFault_IRQn 0 */

    /* USER CODE END BusFault_IRQn 0 */
    while (1) {
        /* USER CODE BEGIN W1_BusFault_IRQn 0 */
        /* USER CODE END W1_BusFault_IRQn 0 */
    }
}

/**
 * @brief 功能：响应未定义指令或非法状态异常并停留在异常状态。
 * 参数：无。
 * @retval 返回：无，函数持续等待。
 */
void UsageFault_Handler(void) {
    /* USER CODE BEGIN UsageFault_IRQn 0 */

    /* USER CODE END UsageFault_IRQn 0 */
    while (1) {
        /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
        /* USER CODE END W1_UsageFault_IRQn 0 */
    }
}

/**
 * @brief 功能：提供调试监视异常处理入口，当前不执行额外处理。
 * 参数：无。
 * @retval 返回：无
 */
void DebugMon_Handler(void) {
    /* USER CODE BEGIN DebugMonitor_IRQn 0 */

    /* USER CODE END DebugMonitor_IRQn 0 */
    /* USER CODE BEGIN DebugMonitor_IRQn 1 */

    /* USER CODE END DebugMonitor_IRQn 1 */
}

/* STM32F4xx 外设中断处理函数 */
/* 在此添加已使用外设的中断处理函数。 */
/* 可用的外设中断处理函数名称 */
/* 请参见启动文件 startup_stm32f4xx.s。 */

/**
 * @brief 功能：将 TIM7 全局中断交由 HAL 定时器处理流程分发。
 * 参数：无。
 * @retval 返回：无
 */
void TIM7_IRQHandler(void) {
    /* USER CODE BEGIN TIM7_IRQn 0 */

    /* USER CODE END TIM7_IRQn 0 */
    HAL_TIM_IRQHandler(&htim7);
    /* USER CODE BEGIN TIM7_IRQn 1 */

    /* USER CODE END TIM7_IRQn 1 */
}

/** USART2 全局中断；ESP 接收与错误回调由 esp_at.c 实现。 */
/**
 * @brief 功能：分发 USART2 全局中断，ESP 接收和错误回调由 esp_at.c 实现。
 * 参数：无。
 * @retval 返回：无
 */
void USART2_IRQHandler(void) {
    /* USER CODE BEGIN USART2_IRQn 0 */
    /* USER CODE END USART2_IRQn 0 */
    HAL_UART_IRQHandler(&huart2);
    /* USER CODE BEGIN USART2_IRQn 1 */
    /* USER CODE END USART2_IRQn 1 */
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
