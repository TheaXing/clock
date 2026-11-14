#include "boot_uart.h"

#define BOOT_UART_CAPACITY 1024U

static UART_HandleTypeDef *receiver;
static volatile uint8_t ring[BOOT_UART_CAPACITY];
static volatile uint32_t head;
static volatile uint32_t tail;
static volatile uint32_t errors;
static volatile bool running;
static uint8_t received_byte;

/** 功能：停止接收中断并保留本轮故障，禁止损坏字节流继续被解析；参数：故障位；返回：无。 */
static void fail(uint32_t reason) {
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    errors |= reason;
    running = false;
    HAL_NVIC_DisableIRQ(USART2_IRQn);
    if (receiver != NULL) {
        __HAL_UART_DISABLE_IT(receiver, UART_IT_RXNE);
        __HAL_UART_DISABLE_IT(receiver, UART_IT_PE);
        __HAL_UART_DISABLE_IT(receiver, UART_IT_ERR);
    }
    __set_PRIMASK(mask);
}

/** 功能：关闭接收中断并清除挂起状态以安全交接App；参数：无；返回：无。 */
void boot_uart_stop(void) {
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    running = false;
    HAL_NVIC_DisableIRQ(USART2_IRQn);
    if (receiver != NULL) {
        __HAL_UART_DISABLE_IT(receiver, UART_IT_RXNE);
        __HAL_UART_DISABLE_IT(receiver, UART_IT_PE);
        __HAL_UART_DISABLE_IT(receiver, UART_IT_ERR);
        (void)HAL_UART_AbortReceive(receiver);
        __HAL_UART_CLEAR_OREFLAG(receiver);
    }
    HAL_NVIC_ClearPendingIRQ(USART2_IRQn);
    receiver = NULL;
    __set_PRIMASK(mask);
}

/** 功能：为一轮恢复下载重新同步并开启持续接收；参数：USART2句柄；返回：就绪为真。 */
bool boot_uart_start(UART_HandleTypeDef *uart) {
    boot_uart_stop();
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    head = 0U;
    tail = 0U;
    errors = 0U;
    if (uart == NULL || uart->Instance != USART2) {
        errors = BOOT_UART_ERROR_ARM;
        __set_PRIMASK(mask);
        return false;
    }
    receiver = uart;
    if (HAL_UART_AbortReceive(receiver) != HAL_OK) {
        errors = BOOT_UART_ERROR_ARM;
        __set_PRIMASK(mask);
        return false;
    }
    __HAL_UART_CLEAR_OREFLAG(receiver);
    receiver->ErrorCode = HAL_UART_ERROR_NONE;
    HAL_NVIC_ClearPendingIRQ(USART2_IRQn);
    HAL_NVIC_SetPriority(USART2_IRQn, 1U, 0U);
    if (HAL_UART_Receive_IT(receiver, &received_byte, 1U) != HAL_OK) {
        errors = BOOT_UART_ERROR_ARM;
        __set_PRIMASK(mask);
        return false;
    }
    running = true;
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    __set_PRIMASK(mask);
    return true;
}

/** 功能：在接收中断保持运行时发送完整数据；参数：字节和长度；返回：本轮无故障且发送完成为真。 */
bool boot_uart_send(const void *data, size_t length) {
    if (!running || errors != 0U || data == NULL || length == 0U || length > UINT16_MAX ||
        __get_PRIMASK() != 0U) {
        return false;
    }
    if (HAL_UART_Transmit(receiver, (uint8_t *)data, (uint16_t)length, 2000U) != HAL_OK) {
        fail(BOOT_UART_ERROR_TRANSFER);
        return false;
    }
    return running && errors == 0U;
}

/** 功能：从环形队列有界读取字节并原样恢复中断屏蔽；参数：输出和毫秒期限；返回：读到有效字节为真。 */
bool boot_uart_receive(uint8_t *out, uint32_t timeout) {
    if (out == NULL) {
        return false;
    }
    uint32_t start = HAL_GetTick();
    for (;;) {
        uint32_t mask = __get_PRIMASK();
        __disable_irq();
        if (!running || errors != 0U) {
            __set_PRIMASK(mask);
            return false;
        }
        if (tail != head) {
            *out = ring[tail & (BOOT_UART_CAPACITY - 1U)];
            ++tail;
            __set_PRIMASK(mask);
            return true;
        }
        __set_PRIMASK(mask);
        if (mask != 0U || (uint32_t)(HAL_GetTick() - start) >= timeout) {
            return false;
        }
    }
}

/** 功能：读取保留至下一次启动的硬件或队列故障位；参数：无；返回：故障位。 */
uint32_t boot_uart_errors(void) {
    return errors;
}

/** 功能：保存单字节后立即重新接收，错误同时到来时禁止重装清错；参数：HAL句柄；返回：无。 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart) {
    if (uart != receiver || !running) {
        return;
    }
    if (uart->ErrorCode != HAL_UART_ERROR_NONE) {
        fail(uart->ErrorCode);
        return;
    }
    if ((uint32_t)(head - tail) >= BOOT_UART_CAPACITY) {
        fail(BOOT_UART_ERROR_OVERFLOW);
        return;
    }
    ring[head & (BOOT_UART_CAPACITY - 1U)] = received_byte;
    ++head;
    if (HAL_UART_Receive_IT(uart, &received_byte, 1U) != HAL_OK) {
        fail(BOOT_UART_ERROR_ARM);
    }
}

/** 功能：保留接收硬件故障并停止本轮传输；参数：HAL句柄；返回：无。 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart) {
    if (uart == receiver) {
        fail(uart->ErrorCode == HAL_UART_ERROR_NONE ? BOOT_UART_ERROR_TRANSFER : uart->ErrorCode);
    }
}

/** 功能：将Boot专属USART2向量分发至HAL接收器；参数：无；返回：无。 */
void USART2_IRQHandler(void) {
    if (receiver != NULL && running) {
        HAL_UART_IRQHandler(receiver);
    }
}
