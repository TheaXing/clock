#ifndef ESP_TEST_USART_H
#define ESP_TEST_USART_H
#include <stdint.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
/** 提供主机测试所需的不透明串口句柄。 */
typedef struct {
    int unused; // 占位成员，无实际外设寄存器
} UART_HandleTypeDef;
extern UART_HandleTypeDef huart2;
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *uart, uint8_t *data, uint16_t length,
                                    uint32_t timeout);
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *uart, uint8_t *data, uint16_t length);
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *uart);
#define __HAL_UART_CLEAR_OREFLAG(uart) ((void)(uart))
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart);
#endif
