/* USER CODE BEGIN Header */
/**
 * @file usart.c
 * @brief 配置 USART 外设。
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
#include "usart.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

/* 初始化 USART1 */

/**
 * @brief 功能：以 115200 波特、八位数据、一个停止位初始化调试串口 USART1。
 * 参数：无。
 * @retval 返回：无
 */
void MX_USART1_UART_Init(void) {

    /* USER CODE BEGIN USART1_Init 0 */

    /* USER CODE END USART1_Init 0 */

    /* USER CODE BEGIN USART1_Init 1 */

    /* USER CODE END USART1_Init 1 */
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK) {
        Error_Handler();
    }
    /* USER CODE BEGIN USART1_Init 2 */

    /* USER CODE END USART1_Init 2 */
}

/* 初始化 USART2 */
/**
 * @brief 功能：以 115200 波特、八位数据、一个停止位初始化 ESP 通信串口 USART2。
 * 参数：无。
 * @retval 返回：无
 */
void MX_USART2_UART_Init(void) {
    /* USER CODE BEGIN USART2_Init 0 */
    /* USER CODE END USART2_Init 0 */
    /* USER CODE BEGIN USART2_Init 1 */
    /* USER CODE END USART2_Init 1 */
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart2) != HAL_OK) {
        Error_Handler();
    }
    /* USER CODE BEGIN USART2_Init 2 */
    /* USER CODE END USART2_Init 2 */
}

/**
 * @brief 功能：配置 USART1 或 USART2 的时钟与复用引脚，并开启 USART2 中断。
 * 参数：
 * @param uart_handle 待初始化的串口句柄。
 * @retval 返回：无
 */
void HAL_UART_MspInit(UART_HandleTypeDef *uart_handle) {

    GPIO_InitTypeDef gpio_init = {0};
    if (uart_handle->Instance == USART1) {
        /* USER CODE BEGIN USART1_MspInit 0 */

        /* USER CODE END USART1_MspInit 0 */
        /* 开启 USART1 时钟 */
        __HAL_RCC_USART1_CLK_ENABLE();

        __HAL_RCC_GPIOA_CLK_ENABLE();
        /** USART1 引脚配置：PA9 为 TX，PA10 为 RX。 */
        gpio_init.Pin = GPIO_PIN_9 | GPIO_PIN_10;
        gpio_init.Mode = GPIO_MODE_AF_PP;
        gpio_init.Pull = GPIO_NOPULL;
        gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        gpio_init.Alternate = GPIO_AF7_USART1;
        HAL_GPIO_Init(GPIOA, &gpio_init);

        /* USER CODE BEGIN USART1_MspInit 1 */

        /* USER CODE END USART1_MspInit 1 */
    } else if (uart_handle->Instance == USART2) {
        /* USER CODE BEGIN USART2_MspInit 0 */
        /* USER CODE END USART2_MspInit 0 */
        __HAL_RCC_USART2_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        /* USART2 引脚配置：PA2 为 TX，PA3 为 RX。 */
        gpio_init.Pin = GPIO_PIN_2 | GPIO_PIN_3;
        gpio_init.Mode = GPIO_MODE_AF_PP;
        gpio_init.Pull = GPIO_NOPULL;
        gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        gpio_init.Alternate = GPIO_AF7_USART2;
        HAL_GPIO_Init(GPIOA, &gpio_init);
        HAL_NVIC_SetPriority(USART2_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(USART2_IRQn);
        /* USER CODE BEGIN USART2_MspInit 1 */
        /* USER CODE END USART2_MspInit 1 */
    }
}

/**
 * @brief 功能：关闭指定串口的时钟并释放引脚，同时关闭 USART2 中断。
 * 参数：
 * @param uart_handle 待反初始化的串口句柄。
 * @retval 返回：无
 */
void HAL_UART_MspDeInit(UART_HandleTypeDef *uart_handle) {

    if (uart_handle->Instance == USART1) {
        /* USER CODE BEGIN USART1_MspDeInit 0 */

        /* USER CODE END USART1_MspDeInit 0 */
        /* 关闭外设时钟 */
        __HAL_RCC_USART1_CLK_DISABLE();

        /** USART1 引脚配置：PA9 为 TX，PA10 为 RX。 */
        HAL_GPIO_DeInit(GPIOA, GPIO_PIN_9 | GPIO_PIN_10);

        /* USER CODE BEGIN USART1_MspDeInit 1 */

        /* USER CODE END USART1_MspDeInit 1 */
    } else if (uart_handle->Instance == USART2) {
        /* USER CODE BEGIN USART2_MspDeInit 0 */
        /* USER CODE END USART2_MspDeInit 0 */
        __HAL_RCC_USART2_CLK_DISABLE();
        HAL_GPIO_DeInit(GPIOA, GPIO_PIN_2 | GPIO_PIN_3);
        HAL_NVIC_DisableIRQ(USART2_IRQn);
        /* USER CODE BEGIN USART2_MspDeInit 1 */
        /* USER CODE END USART2_MspDeInit 1 */
    }
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
