/* USER CODE BEGIN Header */
/**
 * @file stm32f4xx_hal_timebase_tim.c
 * @brief 使用硬件定时器提供 HAL 时基。
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
#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_tim.h"

/* 内部类型定义 */
/* 内部常量定义 */
/* 内部宏 */
/* 内部变量 */
TIM_HandleTypeDef htim7;
/* 内部函数声明 */
/* 内部函数 */

/**
 * @brief 功能：将 TIM7 配置为一毫秒 HAL 时基，并设置其更新中断优先级。
 * 参数：
 * @param tick_priority 时基中断优先级。
 * @retval 返回：返回 HAL 初始化、启动或优先级检查结果。
 * @note HAL_Init 在复位后的初始化阶段调用本函数；HAL_RCC_ClockConfig 在重新配置时钟时也会调用。
 */
HAL_StatusTypeDef HAL_InitTick(uint32_t tick_priority) {
    RCC_ClkInitTypeDef clkconfig;
    uint32_t tim_clock, apb1_prescaler = 0U;

    uint32_t prescaler_value = 0U;
    uint32_t flash_latency;

    HAL_StatusTypeDef status;

    /* 开启 TIM7 时钟 */
    __HAL_RCC_TIM7_CLK_ENABLE();

    /* 读取时钟配置 */
    HAL_RCC_GetClockConfig(&clkconfig, &flash_latency);

    /* 读取 APB1 分频系数 */
    apb1_prescaler = clkconfig.APB1CLKDivider;
    /* 计算 TIM7 时钟频率 */
    if (apb1_prescaler == RCC_HCLK_DIV1) {
        tim_clock = HAL_RCC_GetPCLK1Freq();
    } else {
        tim_clock = 2UL * HAL_RCC_GetPCLK1Freq();
    }

    /* 计算预分频系数，使 TIM7 计数器时钟为 1 MHz。 */
    prescaler_value = (uint32_t)((tim_clock / 1000000U) - 1U);

    /* 初始化 TIM7 */
    htim7.Instance = TIM7;

    /* 配置 TIM7：预分频系数为 tim_clock/1000000-1，使计数器时钟为 1 MHz；自动重装载值为 1000000/1000-1，产生 1 ms 时基；时钟分频为 0，采用向上计数。 */
    htim7.Init.Period = (1000000U / 1000U) - 1U;
    htim7.Init.Prescaler = prescaler_value;
    htim7.Init.ClockDivision = 0;
    htim7.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim7.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    status = HAL_TIM_Base_Init(&htim7);
    if (status == HAL_OK) {

        /* 以中断模式启动定时器时基。 */
        status = HAL_TIM_Base_Start_IT(&htim7);
        if (status == HAL_OK) {
            /* 开启 TIM7 全局中断 */
            HAL_NVIC_EnableIRQ(TIM7_IRQn);
            /* 检查 HAL 时基中断优先级范围。 */
            if (tick_priority < (1UL << __NVIC_PRIO_BITS)) {
                /* 配置定时器中断优先级 */
                HAL_NVIC_SetPriority(TIM7_IRQn, tick_priority, 0U);
                uwTickPrio = tick_priority;
            } else {
                status = HAL_ERROR;
            }
        }
    }

    /* 返回函数执行状态 */
    return status;
}

/**
 * @brief 功能：关闭 TIM7 更新中断，暂停 HAL 时基递增。
 * 参数：无。
 * @retval 返回：无
 */
void HAL_SuspendTick(void) {
    /* 关闭 TIM7 更新中断 */
    __HAL_TIM_DISABLE_IT(&htim7, TIM_IT_UPDATE);
}

/**
 * @brief 功能：开启 TIM7 更新中断，恢复 HAL 时基递增。
 * 参数：无。
 * @retval 返回：无
 */
void HAL_ResumeTick(void) {
    /* 开启 TIM7 更新中断 */
    __HAL_TIM_ENABLE_IT(&htim7, TIM_IT_UPDATE);
}
