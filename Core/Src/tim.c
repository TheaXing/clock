#include "tim.h"

TIM_HandleTypeDef htim3;

/**
 * @brief 功能：初始化 TIM3 通道二的蜂鸣器 PWM，初始占空比为零以保持静音。
 * 参数：无。
 * @retval 返回：无
 */
void MX_TIM3_Init(void) {
    TIM_MasterConfigTypeDef master = {0};
    TIM_OC_InitTypeDef output = {0};
    /* APB1 定时器时钟为 84 MHz，除以 30769 后输出频率为 2730.02 Hz。 */
    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 0;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 30768;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&htim3) != HAL_OK) {
        Error_Handler();
    }
    master.MasterOutputTrigger = TIM_TRGO_RESET;
    master.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &master) != HAL_OK) {
        Error_Handler();
    }
    output.OCMode = TIM_OCMODE_PWM1;
    output.Pulse = 0; /* 调用 buzzer_set(true) 前保持静音。 */
    output.OCPolarity = TIM_OCPOLARITY_HIGH;
    output.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim3, &output, TIM_CHANNEL_2) != HAL_OK) {
        Error_Handler();
    }
    HAL_TIM_MspPostInit(&htim3);
}

/**
 * @brief 功能：在 PWM 底层初始化回调中开启 TIM3 时钟。
 * 参数：
 * @param htim 待初始化的定时器句柄。
 * @retval 返回：无
 */
void HAL_TIM_PWM_MspInit(TIM_HandleTypeDef *htim) {
    if (htim->Instance == TIM3) {
        __HAL_RCC_TIM3_CLK_ENABLE();
    }
}

/**
 * @brief 功能：将 TIM3 通道二对应的 PC7 配置为 AF2 推挽复用输出。
 * 参数：
 * @param htim 待配置引脚的定时器句柄。
 * @retval 返回：无
 */
void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim) {
    if (htim->Instance == TIM3) {
        GPIO_InitTypeDef config = {0};
        __HAL_RCC_GPIOC_CLK_ENABLE();
        config.Pin = GPIO_PIN_7;
        config.Mode = GPIO_MODE_AF_PP;
        config.Pull = GPIO_NOPULL;
        config.Speed = GPIO_SPEED_FREQ_LOW;
        config.Alternate = GPIO_AF2_TIM3;
        HAL_GPIO_Init(GPIOC, &config);
    }
}

/**
 * @brief 功能：关闭 TIM3 时钟并释放蜂鸣器使用的 PC7 引脚。
 * 参数：
 * @param htim 待反初始化的定时器句柄。
 * @retval 返回：无
 */
void HAL_TIM_PWM_MspDeInit(TIM_HandleTypeDef *htim) {
    if (htim->Instance == TIM3) {
        __HAL_RCC_TIM3_CLK_DISABLE();
        HAL_GPIO_DeInit(GPIOC, GPIO_PIN_7);
    }
}
