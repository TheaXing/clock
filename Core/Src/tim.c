#include "tim.h"

TIM_HandleTypeDef htim3;

void MX_TIM3_Init(void)
{
    TIM_MasterConfigTypeDef master = {0};
    TIM_OC_InitTypeDef output = {0};
    /* APB1 timer clock 84 MHz / 30769 = 2730.02 Hz. */
    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 0;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 30768;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&htim3) != HAL_OK) { Error_Handler(); }
    master.MasterOutputTrigger = TIM_TRGO_RESET;
    master.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &master) != HAL_OK) { Error_Handler(); }
    output.OCMode = TIM_OCMODE_PWM1;
    output.Pulse = 0; /* Silent until buzzer_set(true). */
    output.OCPolarity = TIM_OCPOLARITY_HIGH;
    output.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim3, &output, TIM_CHANNEL_2) != HAL_OK) { Error_Handler(); }
    HAL_TIM_MspPostInit(&htim3);
}

void HAL_TIM_PWM_MspInit(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM3) { __HAL_RCC_TIM3_CLK_ENABLE(); }
}

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM3)
    {
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

void HAL_TIM_PWM_MspDeInit(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM3)
    {
        __HAL_RCC_TIM3_CLK_DISABLE();
        HAL_GPIO_DeInit(GPIOC, GPIO_PIN_7);
    }
}
