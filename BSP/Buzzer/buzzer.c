#include "buzzer.h"
#include "main.h"
#if !CLOCK_BUZZER_ACTIVE
#include "tim.h"
static bool pwm_enabled;
#endif

/**
 * 功能：先预置低电平再将蜂鸣器引脚配置为推挽输出。
 * 参数：无。
 * 返回：无。
 */
static void pin_output_low(void) {
    GPIO_InitTypeDef config = {0};
    /* 切换模式前先预置输出数据寄存器；NPN 三极管为高电平导通。 */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_7, GPIO_PIN_RESET);
    config.Pin = GPIO_PIN_7;
    config.Mode = GPIO_MODE_OUTPUT_PP;
    config.Pull = GPIO_NOPULL;
    config.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &config);
}

/**
 * 功能：初始化蜂鸣器 GPIO 和可选 PWM，保持蜂鸣器关闭。
 * 参数：无。
 * 返回：无。
 */
void buzzer_init(void) {
    __HAL_RCC_GPIOC_CLK_ENABLE();
    pin_output_low();
#if !CLOCK_BUZZER_ACTIVE
    MX_TIM3_Init();
    pwm_enabled = false;
#endif
    buzzer_set(false);
}

/**
 * 功能：根据有源或无源配置控制蜂鸣器电平或 PWM 输出。
 * 参数：enabled，是否启用蜂鸣器。
 * 返回：无。
 */
void buzzer_set(bool enabled) {
#if CLOCK_BUZZER_ACTIVE
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_7, enabled ? GPIO_PIN_SET : GPIO_PIN_RESET);
#else
    if (enabled) {
        if (pwm_enabled) {
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
        if (HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2) != HAL_OK) {
            pin_output_low();
            Error_Handler();
        }
        pwm_enabled = true;
    } else {
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 0U);
        (void)HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_2);
        pwm_enabled = false;
        /* 关闭时主动拉低引脚，避免依赖定时器禁用通道的空闲电平。 */
        pin_output_low();
    }
#endif
}
