/* USER CODE BEGIN Header */
/**
 * @file gpio.c
 * @brief 配置项目使用的 GPIO 引脚。
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
#include "gpio.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* 配置 GPIO */

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/** 引脚可配置为模拟、输入、输出、事件输出或外部中断模式。 */
/**
 * @brief 功能：配置液晶控制输出、按键输入及蜂鸣器初始电平。
 * 参数：无。
 * @retval 返回：无
 */
void MX_GPIO_Init(void) {

    GPIO_InitTypeDef gpio_init = {0};

    /* 开启 GPIO 端口时钟 */
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* 配置 GPIO 输出电平 */
    HAL_GPIO_WritePin(GPIOE, LCD_CS_Pin | LCD_RST_Pin | LCD_DC_Pin, GPIO_PIN_SET);

    /* 配置 GPIO 输出电平 */
    HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_RESET);

    /* 配置液晶片选、复位、数据控制与背光引脚。 */
    gpio_init.Pin = LCD_CS_Pin | LCD_RST_Pin | LCD_DC_Pin | LCD_BL_Pin;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOE, &gpio_init);

    /* USER CODE BEGIN MX_GPIO_Init_2 */
    /* PA0、PC4 和 PC5 均有外部 10 kΩ 上拉，按下时为低电平。 */
    gpio_init.Pin = KEY1_Pin;
    gpio_init.Mode = GPIO_MODE_INPUT;
    gpio_init.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(KEY1_GPIO_Port, &gpio_init);
    gpio_init.Pin = KEY2_Pin | KEY3_Pin;
    HAL_GPIO_Init(GPIOC, &gpio_init);
    /* PC7 模式由蜂鸣器驱动管理：PWM 方案响铃时使用 AF2，停止时推挽输出低电平；有源蜂鸣器方案仅使用 GPIO。 */
    HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
    /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 2 */

/* USER CODE END 2 */
