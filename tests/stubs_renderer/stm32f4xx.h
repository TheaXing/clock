#ifndef TEST_RENDERER_STM32_H
#define TEST_RENDERER_STM32_H
#include <stdint.h>
typedef struct { int unused; } GPIO_TypeDef;
typedef struct { int unused; } SPI_HandleTypeDef;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
#define GPIOE ((GPIO_TypeDef *)1)
#define GPIO_PIN_2 0x0004U
#define GPIO_PIN_3 0x0008U
#define GPIO_PIN_4 0x0010U
#define GPIO_PIN_5 0x0020U
#define HAL_OK 0
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
int HAL_SPI_Transmit(SPI_HandleTypeDef *spi, const uint8_t *data, uint16_t length, uint32_t timeout);
#endif
