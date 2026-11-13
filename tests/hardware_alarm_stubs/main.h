#ifndef ALARM_TEST_MAIN_H
#define ALARM_TEST_MAIN_H
#include <stdint.h>
typedef struct { uint32_t id; } GPIO_TypeDef;
extern GPIO_TypeDef test_gpio_a, test_gpio_c;
#define GPIOA (&test_gpio_a)
#define GPIOC (&test_gpio_c)
#define GPIO_PIN_0 1U
#define GPIO_PIN_4 16U
#define GPIO_PIN_5 32U
#define GPIO_PIN_7 128U
#define GPIO_MODE_INPUT 0U
#define GPIO_MODE_OUTPUT_PP 1U
#define GPIO_MODE_AF_PP 2U
#define GPIO_NOPULL 0U
#define GPIO_SPEED_FREQ_LOW 0U
#define GPIO_AF2_TIM3 2U
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOC_CLK_ENABLE() ((void)0)
typedef struct { uint32_t Pin, Mode, Pull, Speed, Alternate; } GPIO_InitTypeDef;
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET = 1 } GPIO_PinState;
typedef enum { HAL_OK = 0, HAL_ERROR = 1, HAL_BUSY = 2, HAL_TIMEOUT = 3 } HAL_StatusTypeDef;
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *, uint16_t);
void HAL_GPIO_WritePin(GPIO_TypeDef *, uint16_t, GPIO_PinState);
void HAL_GPIO_Init(GPIO_TypeDef *, GPIO_InitTypeDef *);
void HAL_Delay(uint32_t);
void Error_Handler(void);
#endif
