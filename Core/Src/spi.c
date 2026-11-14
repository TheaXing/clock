/* USER CODE BEGIN Header */
/**
 * @file spi.c
 * @brief 配置 SPI 外设。
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
#include "spi.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

SPI_HandleTypeDef hspi2;

/* 初始化 SPI2 */
/**
 * @brief 功能：以主机、八位、软件片选模式初始化液晶屏使用的 SPI2。
 * 参数：无。
 * @retval 返回：无
 */
void MX_SPI2_Init(void) {

    /* USER CODE BEGIN SPI2_Init 0 */

    /* USER CODE END SPI2_Init 0 */

    /* USER CODE BEGIN SPI2_Init 1 */

    /* USER CODE END SPI2_Init 1 */
    hspi2.Instance = SPI2;
    hspi2.Init.Mode = SPI_MODE_MASTER;
    hspi2.Init.Direction = SPI_DIRECTION_2LINES;
    hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi2.Init.NSS = SPI_NSS_SOFT;
    hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
    hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi2.Init.CRCPolynomial = 10;
    if (HAL_SPI_Init(&hspi2) != HAL_OK) {
        Error_Handler();
    }
    /* USER CODE BEGIN SPI2_Init 2 */

    /* USER CODE END SPI2_Init 2 */
}

/**
 * @brief 功能：开启 SPI2 时钟并配置 MISO、MOSI 和 SCK 复用引脚。
 * 参数：
 * @param spi_handle 待初始化的 SPI 句柄。
 * @retval 返回：无
 */
void HAL_SPI_MspInit(SPI_HandleTypeDef *spi_handle) {

    GPIO_InitTypeDef gpio_init = {0};
    if (spi_handle->Instance == SPI2) {
        /* USER CODE BEGIN SPI2_MspInit 0 */

        /* USER CODE END SPI2_MspInit 0 */
        /* 开启 SPI2 时钟 */
        __HAL_RCC_SPI2_CLK_ENABLE();

        __HAL_RCC_GPIOC_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        /** SPI2 引脚配置：PC2 为 MISO，PC3 为 MOSI，PB13 为 SCK。 */
        gpio_init.Pin = GPIO_PIN_2 | GPIO_PIN_3;
        gpio_init.Mode = GPIO_MODE_AF_PP;
        gpio_init.Pull = GPIO_NOPULL;
        gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        gpio_init.Alternate = GPIO_AF5_SPI2;
        HAL_GPIO_Init(GPIOC, &gpio_init);

        gpio_init.Pin = GPIO_PIN_13;
        gpio_init.Mode = GPIO_MODE_AF_PP;
        gpio_init.Pull = GPIO_NOPULL;
        gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        gpio_init.Alternate = GPIO_AF5_SPI2;
        HAL_GPIO_Init(GPIOB, &gpio_init);

        /* USER CODE BEGIN SPI2_MspInit 1 */

        /* USER CODE END SPI2_MspInit 1 */
    }
}

/**
 * @brief 功能：关闭 SPI2 时钟并释放其复用引脚。
 * 参数：
 * @param spi_handle 待反初始化的 SPI 句柄。
 * @retval 返回：无
 */
void HAL_SPI_MspDeInit(SPI_HandleTypeDef *spi_handle) {

    if (spi_handle->Instance == SPI2) {
        /* USER CODE BEGIN SPI2_MspDeInit 0 */

        /* USER CODE END SPI2_MspDeInit 0 */
        /* 关闭外设时钟 */
        __HAL_RCC_SPI2_CLK_DISABLE();

        /** SPI2 引脚配置：PC2 为 MISO，PC3 为 MOSI，PB13 为 SCK。 */
        HAL_GPIO_DeInit(GPIOC, GPIO_PIN_2 | GPIO_PIN_3);

        HAL_GPIO_DeInit(GPIOB, GPIO_PIN_13);

        /* USER CODE BEGIN SPI2_MspDeInit 1 */

        /* USER CODE END SPI2_MspDeInit 1 */
    }
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
