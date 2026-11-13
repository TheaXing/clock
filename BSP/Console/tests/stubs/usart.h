#ifndef TEST_CONSOLE_USART_H
#define TEST_CONSOLE_USART_H
#include <stdint.h>
typedef struct { int unused; } UART_HandleTypeDef;
typedef enum { HAL_OK = 0, HAL_ERROR = 1 } HAL_StatusTypeDef;
extern UART_HandleTypeDef huart1;
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *, uint8_t *, uint16_t, uint32_t);
uint32_t __get_IPSR(void);
#endif
