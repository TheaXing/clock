#include "buzzer.h"
#include "main.h"
#if !CLOCK_BUZZER_ACTIVE
#include "tim.h"
static bool pwm_enabled;
#endif

static void pin_output_low(void)
{
    GPIO_InitTypeDef config = {0};
    /* Preload ODR before changing the mode; the NPN is HIGH-on. */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_7, GPIO_PIN_RESET);
    config.Pin = GPIO_PIN_7;
    config.Mode = GPIO_MODE_OUTPUT_PP;
    config.Pull = GPIO_NOPULL;
    config.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &config);
}

void buzzer_init(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();
    pin_output_low();
#if !CLOCK_BUZZER_ACTIVE
    MX_TIM3_Init();
    pwm_enabled = false;
#endif
    buzzer_set(false);
}

void buzzer_set(bool enabled)
{
#if CLOCK_BUZZER_ACTIVE
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_7, enabled ? GPIO_PIN_SET : GPIO_PIN_RESET);
#else
    if (enabled)
    {
        if (pwm_enabled)
        {
            return;
        }
        GPIO_InitTypeDef config = {0};
        config.Pin = GPIO_PIN_7;
        config.Mode = GPIO_MODE_AF_PP;
        config.Pull = GPIO_NOPULL;
        config.Speed = GPIO_SPEED_FREQ_LOW;
        config.Alternate = GPIO_AF2_TIM3;
        HAL_GPIO_Init(GPIOC, &config);
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 15384U);
        if (HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2) != HAL_OK)
        {
            pin_output_low();
            Error_Handler();
        }
        pwm_enabled = true;
    }
    else
    {
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 0U);
        (void)HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_2);
        pwm_enabled = false;
        /* Do not depend on the timer's disabled-channel idle level. */
        pin_output_low();
    }
#endif
}
