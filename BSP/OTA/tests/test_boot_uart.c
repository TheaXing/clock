#include "boot_uart.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

USART_TypeDef test_usart2;
static UART_HandleTypeDef uart;
static uint32_t primask;
static uint32_t tick;
static bool irq_enabled;
static unsigned arm_calls;
static unsigned fail_arm_at;
static bool fail_abort;
static HAL_StatusTypeDef transmit_result;
static const char *during_transmit;
static uint32_t during_transmit_error;
static uint8_t irq_byte;
static uint32_t irq_error;

/** 功能：读取测试中断屏蔽状态；参数：无；返回：屏蔽值。 */
uint32_t __get_PRIMASK(void) {
    return primask;
}
/** 功能：模拟关闭中断；参数：无；返回：无。 */
void __disable_irq(void) {
    primask = 1U;
}
/** 功能：恢复中断屏蔽；参数：屏蔽值；返回：无。 */
void __set_PRIMASK(uint32_t value) {
    primask = value;
}
/** 功能：清除模拟接收错误；参数：句柄；返回：无。 */
void test_clear_overrun(UART_HandleTypeDef *handle) {
    handle->ErrorCode = 0U;
}
/** 功能：关闭指定接收中断；参数：句柄和中断；返回：无。 */
void test_disable_interrupt(UART_HandleTypeDef *handle, uint32_t interrupt) {
    handle->interrupts &= ~interrupt;
}
/** 功能：推进模拟时间；参数：无；返回：毫秒。 */
uint32_t HAL_GetTick(void) {
    return tick++;
}
/** 功能：检查接收中断优先级；参数：中断及优先级；返回：无。 */
void HAL_NVIC_SetPriority(int irq, uint32_t priority, uint32_t subpriority) {
    assert(irq == USART2_IRQn && priority == 1U && subpriority == 0U);
}
/** 功能：开启模拟中断；参数：中断号；返回：无。 */
void HAL_NVIC_EnableIRQ(int irq) {
    assert(irq == USART2_IRQn);
    irq_enabled = true;
}
/** 功能：关闭模拟中断；参数：中断号；返回：无。 */
void HAL_NVIC_DisableIRQ(int irq) {
    assert(irq == USART2_IRQn);
    irq_enabled = false;
}
/** 功能：检查清除的是串口挂起位；参数：中断号；返回：无。 */
void HAL_NVIC_ClearPendingIRQ(int irq) {
    assert(irq == USART2_IRQn);
}
/** 功能：中止模拟接收；参数：句柄；返回：配置结果。 */
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *handle) {
    handle->buffer = NULL;
    handle->interrupts = 0U;
    return fail_abort ? HAL_ERROR : HAL_OK;
}
/** 功能：模拟单字节中断接收并忠实清除HAL错误；参数：句柄及缓冲；返回：配置结果。 */
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *handle, uint8_t *data, uint16_t size) {
    assert(size == 1U && data != NULL);
    ++arm_calls;
    if (arm_calls == fail_arm_at) {
        return HAL_ERROR;
    }
    handle->buffer = data;
    handle->interrupts = UART_IT_RXNE | UART_IT_PE | UART_IT_ERR;
    handle->ErrorCode = HAL_UART_ERROR_NONE;
    return HAL_OK;
}
/** 功能：通过实际中断入口注入异步数据；参数：字节及错误；返回：无。 */
static void inject(uint8_t byte, uint32_t error) {
    irq_byte = byte;
    irq_error = error;
    if (irq_enabled && primask == 0U) {
        USART2_IRQHandler();
    }
}
/** 功能：模拟HAL错误与RXNE同时到来时先完成接收再报告错误；参数：句柄；返回：无。 */
void HAL_UART_IRQHandler(UART_HandleTypeDef *handle) {
    handle->ErrorCode = irq_error;
    if (handle->buffer != NULL && (handle->interrupts & UART_IT_RXNE) != 0U) {
        *handle->buffer = irq_byte;
        handle->buffer = NULL;
        handle->interrupts = 0U;
        HAL_UART_RxCpltCallback(handle);
    }
    if (handle->ErrorCode != HAL_UART_ERROR_NONE) {
        HAL_UART_ErrorCallback(handle);
    }
}
/** 功能：在阻塞发送期间注入ESP异步通知；参数：句柄和数据；返回：配置结果。 */
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *handle, uint8_t *data,
                                  uint16_t size, uint32_t timeout) {
    assert(handle == &uart && data != NULL && size != 0U && timeout == 2000U);
    handle->ErrorCode = HAL_UART_ERROR_NONE;
    if (during_transmit != NULL) {
        for (const char *p = during_transmit; *p != '\0'; ++p) {
            inject((uint8_t)*p, during_transmit_error);
        }
    }
    return transmit_result;
}
/** 功能：重置夹具并开始一轮真实接收；参数：无；返回：无。 */
static void start(void) {
    boot_uart_stop();
    memset(&uart, 0, sizeof uart);
    uart.Instance = USART2;
    primask = 0U;
    tick = 0U;
    arm_calls = 0U;
    fail_arm_at = 0U;
    fail_abort = false;
    transmit_result = HAL_OK;
    during_transmit = NULL;
    during_transmit_error = 0U;
    assert(boot_uart_start(&uart));
    assert(irq_enabled && boot_uart_errors() == 0U);
}
/** 功能：验证实际输出流内容；参数：期望字节串；返回：无。 */
static void expect_text(const char *expected) {
    for (const char *p = expected; *p != '\0'; ++p) {
        uint8_t byte = 0U;
        assert(boot_uart_receive(&byte, 10U));
        assert(byte == (uint8_t)*p);
    }
}
/** 功能：复现发送缓存查询期间异步关闭并验证没有残缺粘连；参数：无；返回：无。 */
static void test_receive_during_transmit(void) {
    start();
    during_transmit = "CLOSED\r\n";
    const char command[] = "AT+CIPRECVLEN?\r\n";
    assert(boot_uart_send(command, sizeof command - 1U));
    const char response[] = "+CIPRECVLEN:0\r\n\r\nOK\r\n";
    for (size_t i = 0U; i < sizeof response - 1U; ++i) {
        inject((uint8_t)response[i], 0U);
    }
    expect_text("CLOSED\r\n+CIPRECVLEN:0\r\n\r\nOK\r\n");
    uint8_t byte;
    assert(!boot_uart_receive(&byte, 0U));
    assert(boot_uart_errors() == 0U);
}
/** 功能：多次跨环形边界保持所有二进制值和顺序；参数：无；返回：无。 */
static void test_wrap_and_binary(void) {
    start();
    for (unsigned block = 0U; block < 12U; ++block) {
        for (unsigned i = 0U; i < 700U; ++i) {
            inject((uint8_t)(i + block), 0U);
        }
        for (unsigned i = 0U; i < 700U; ++i) {
            uint8_t byte;
            assert(boot_uart_receive(&byte, 0U));
            assert(byte == (uint8_t)(i + block));
        }
    }
    assert(boot_uart_errors() == 0U);
}
/** 功能：队列溢出后整轮失败，不返回已损坏流中的任何字节；参数：无；返回：无。 */
static void test_overflow_and_restart(void) {
    start();
    for (unsigned i = 0U; i < 1025U; ++i) {
        inject((uint8_t)i, 0U);
    }
    uint8_t byte;
    assert((boot_uart_errors() & BOOT_UART_ERROR_OVERFLOW) != 0U);
    assert(!boot_uart_receive(&byte, 20U));
    assert(!boot_uart_send("AT\r\n", 4U));
    assert(boot_uart_start(&uart));
    assert(boot_uart_errors() == 0U);
    assert(!boot_uart_receive(&byte, 0U));
    inject('R', 0U);
    expect_text("R");
}
/** 功能：错误与RXNE同时到来时不得重装清除错误或交付字节；参数：无；返回：无。 */
static void test_error_and_rxne(void) {
    const uint32_t errors[] = {HAL_UART_ERROR_ORE, HAL_UART_ERROR_NE, HAL_UART_ERROR_FE, HAL_UART_ERROR_PE};
    for (size_t i = 0U; i < sizeof errors / sizeof errors[0]; ++i) {
        start();
        unsigned arms_before = arm_calls;
        inject('C', errors[i]);
        assert(arm_calls == arms_before);
        assert((boot_uart_errors() & errors[i]) != 0U);
        uint8_t byte;
        assert(!boot_uart_receive(&byte, 1U));
        assert(!boot_uart_send("AT\r\n", 4U));
    }
    start();
    during_transmit = "CLOSED\r\n";
    during_transmit_error = HAL_UART_ERROR_ORE;
    assert(!boot_uart_send("AT+CIPRECVLEN?\r\n", 15U));
    assert((boot_uart_errors() & HAL_UART_ERROR_ORE) != 0U);
}
/** 功能：启动或重装失败时拒绝不受保护的接收与发送；参数：无；返回：无。 */
static void test_arm_and_abort_failures(void) {
    start();
    fail_arm_at = arm_calls + 1U;
    assert(!boot_uart_start(&uart));
    assert((boot_uart_errors() & BOOT_UART_ERROR_ARM) != 0U);
    uint8_t byte;
    assert(!boot_uart_receive(&byte, 1U));
    assert(!boot_uart_send("AT\r\n", 4U));
    start();
    fail_arm_at = arm_calls + 1U;
    inject('C', 0U);
    assert((boot_uart_errors() & BOOT_UART_ERROR_ARM) != 0U);
    assert(!boot_uart_receive(&byte, 1U));
    start();
    fail_abort = true;
    assert(!boot_uart_start(&uart));
    assert(boot_uart_errors() != 0U);
    fail_abort = false;
}
/** 功能：超时有界、屏蔽状态恢复、停机无中断且重启无旧字节；参数：无；返回：无。 */
static void test_timeout_stop_and_mask(void) {
    start();
    uint8_t byte;
    uint32_t before = tick;
    assert(!boot_uart_receive(&byte, 5U));
    assert(tick - before >= 5U && tick - before < 10U);
    inject('A', 0U);
    primask = 1U;
    assert(boot_uart_receive(&byte, 0U) && byte == 'A');
    assert(primask == 1U);
    primask = 0U;
    inject('B', 0U);
    boot_uart_stop();
    assert(!irq_enabled && uart.buffer == NULL && uart.interrupts == 0U);
    assert(!boot_uart_receive(&byte, 0U));
    assert(!boot_uart_send("AT\r\n", 4U));
    assert(boot_uart_start(&uart));
    assert(!boot_uart_receive(&byte, 0U));
    transmit_result = HAL_TIMEOUT;
    assert(!boot_uart_send("AT\r\n", 4U));
    assert(boot_uart_errors() != 0U);
}
/** 功能：运行持续接收与故障关闭回归；参数：无；返回：成功为零。 */
int main(void) {
    test_receive_during_transmit();
    test_wrap_and_binary();
    test_overflow_and_restart();
    test_error_and_rxne();
    test_arm_and_abort_failures();
    test_timeout_stop_and_mask();
    boot_uart_stop();
    puts("Boot UART: concurrent receive, binary ring, overflow/error fail-closed, timeout and handoff passed");
    return 0;
}
