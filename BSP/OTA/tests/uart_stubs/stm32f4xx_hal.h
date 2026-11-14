#ifndef TEST_STM32F4XX_HAL_H
#define TEST_STM32F4XX_HAL_H
#include <stdint.h>
#include <stddef.h>

/** 模拟串口实例以检查接收器绑定。 */
typedef struct {
    uint32_t unused; // 实例占位
} USART_TypeDef;
extern USART_TypeDef test_usart2;
#define USART2 (&test_usart2)
#define USART2_IRQn 38
#define UART_IT_RXNE 1U
#define UART_IT_PE 2U
#define UART_IT_ERR 4U
#define HAL_UART_ERROR_NONE 0U
#define HAL_UART_ERROR_PE 1U
#define HAL_UART_ERROR_NE 2U
#define HAL_UART_ERROR_FE 4U
#define HAL_UART_ERROR_ORE 8U

typedef enum {HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT} HAL_StatusTypeDef;

/** 模拟HAL句柄中的接收状态。 */
typedef struct {
    USART_TypeDef *Instance; // 外设实例
    uint32_t ErrorCode; // HAL错误码
    uint8_t *buffer; // 当前单字节接收目标
    uint32_t interrupts; // 已开启中断
} UART_HandleTypeDef;

uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t value);
void test_clear_overrun(UART_HandleTypeDef *uart);
void test_disable_interrupt(UART_HandleTypeDef *uart, uint32_t interrupt);
#define __HAL_UART_CLEAR_OREFLAG(uart) test_clear_overrun(uart)
#define __HAL_UART_DISABLE_IT(uart, interrupt) test_disable_interrupt(uart, interrupt)
uint32_t HAL_GetTick(void);
void HAL_NVIC_SetPriority(int irq, uint32_t priority, uint32_t subpriority);
void HAL_NVIC_EnableIRQ(int irq);
void HAL_NVIC_DisableIRQ(int irq);
void HAL_NVIC_ClearPendingIRQ(int irq);
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *uart);
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *uart, uint8_t *data, uint16_t size);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *uart, uint8_t *data, uint16_t size, uint32_t timeout);
void HAL_UART_IRQHandler(UART_HandleTypeDef *uart);
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart);
void USART2_IRQHandler(void);
#endif
