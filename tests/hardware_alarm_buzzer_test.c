#include "buzzer.h"
#include "main.h"
#include "tim.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
GPIO_TypeDef test_gpio_a, test_gpio_c;
TIM_HandleTypeDef htim3;
static uint32_t mode, level = GPIO_PIN_SET, starts, stops, running;
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState value)
{ assert(port == GPIOC && pin == GPIO_PIN_7); level = (uint32_t)value; }
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *config)
{ assert(port == GPIOC && config->Pin == GPIO_PIN_7); mode = config->Mode; }
void MX_TIM3_Init(void) { mode = GPIO_MODE_AF_PP; }
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel)
{
    assert(timer == &htim3 && channel == TIM_CHANNEL_2 && timer->compare > 0U);
    if (running) return HAL_ERROR; /* HAL requires READY, not BUSY. */
    ++starts; running = 1U; return HAL_OK;
}
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *timer, uint32_t channel)
{ assert(timer == &htim3 && channel == TIM_CHANNEL_2); ++stops; running = 0U; return HAL_OK; }
void Error_Handler(void) { assert(0 && "unexpected buzzer HAL error"); abort(); }
int main(void)
{
    buzzer_init(); assert(mode == GPIO_MODE_OUTPUT_PP && level == GPIO_PIN_RESET);
    buzzer_set(true);
    buzzer_set(true); /* Service may repeat the current ringing state. */
#if CLOCK_BUZZER_ACTIVE
    assert(mode == GPIO_MODE_OUTPUT_PP && level == GPIO_PIN_SET && starts == 0U);
#else
    assert(mode == GPIO_MODE_AF_PP && starts == 1U);
#endif
    buzzer_set(false); assert(mode == GPIO_MODE_OUTPUT_PP && level == GPIO_PIN_RESET);
    buzzer_set(true); buzzer_set(false);
#if !CLOCK_BUZZER_ACTIVE
    assert(starts == 2U && stops >= 2U);
#endif
    puts("alarm hardware buzzer: pass"); return 0;
}
