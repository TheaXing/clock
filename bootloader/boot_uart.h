#ifndef BOOT_UART_H
#define BOOT_UART_H

#include "stm32f4xx_hal.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BOOT_UART_ERROR_OVERFLOW (1UL << 16)
#define BOOT_UART_ERROR_ARM (1UL << 17)
#define BOOT_UART_ERROR_TRANSFER (1UL << 18)

bool boot_uart_start(UART_HandleTypeDef *uart);
void boot_uart_stop(void);
bool boot_uart_send(const void *data, size_t length);
bool boot_uart_receive(uint8_t *out, uint32_t timeout);
uint32_t boot_uart_errors(void);

#endif
