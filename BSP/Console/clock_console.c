#include "clock_console.h"
#include "usart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>

static SemaphoreHandle_t console_mutex;
static SemaphoreHandle_t stream_mutex;

struct _reent;

/* Newlib 通过 _sbrk 使用独立堆，暂停调度以串行化分配。SuspendAll/ResumeAll 支持嵌套，包括浮点格式化触发的分配。与 FreeRTOS 堆一致，禁止在中断上下文分配 Newlib 内存。 */
/**
 * 功能：通过可嵌套暂停调度串行化 Newlib 堆访问，并拒绝中断上下文。
 * 参数：reent，Newlib 重入状态指针，此实现不使用。
 * 返回：无。
 */
void __malloc_lock(struct _reent *reent) {
    (void)reent;
    configASSERT(__get_IPSR() == 0U);
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        vTaskSuspendAll();
    }
}

/**
 * 功能：在 Newlib 堆访问完成后恢复对应层级的任务调度。
 * 参数：reent，Newlib 重入状态指针，此实现不使用。
 * 返回：无。
 */
void __malloc_unlock(struct _reent *reent) {
    (void)reent;
    configASSERT(__get_IPSR() == 0U);
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        (void)xTaskResumeAll();
    }
}

/**
 * 功能：创建串口发送与标准输出流的互斥锁，已创建时保持原对象。
 * 参数：无。
 * 返回：无。
 */
void clock_console_init(void) {
    if (console_mutex == NULL) {
        console_mutex = xSemaphoreCreateMutex();
    }
    if (stream_mutex == NULL) {
        stream_mutex = xSemaphoreCreateMutex();
    }
}

/**
 * 功能：在流互斥保护下格式化并刷新日志，拒绝无效调用上下文。
 * 参数：format，格式化输出字符串；...，与格式串对应的可变参数。
 * 返回：输出字符数，参数、锁、格式化或刷新失败时返回 -1。
 */
int clock_log(const char *format, ...) {
    int scheduler_state = xTaskGetSchedulerState();
    if (format == NULL || __get_IPSR() != 0U || scheduler_state == taskSCHEDULER_SUSPENDED ||
        console_mutex == NULL || stream_mutex == NULL) {
        errno = EIO;
        return -1;
    }

    bool task_context = scheduler_state == taskSCHEDULER_RUNNING;
    if (task_context && xSemaphoreTake(stream_mutex, portMAX_DELAY) != pdTRUE) {
        errno = EIO;
        return -1;
    }
    /* 即使各任务拥有独立 _reent，Newlib 的标准输出流仍然共享；_write 中的串口互斥锁无法保护格式化过程和流缓冲区。 */
    va_list arguments;
    va_start(arguments, format);
    int result = vprintf(format, arguments);
    va_end(arguments);
    if (fflush(stdout) != 0) {
        result = -1;
    }
    if (task_context) {
        (void)xSemaphoreGive(stream_mutex);
    }
    return result;
}

/**
 * 功能：将普通文本作为日志行输出，避免把文本解释为格式串。
 * 参数：text，待处理文本。
 * 返回：输出字符数，失败时返回 -1。
 */
int clock_log_line(const char *text) {
    if (text == NULL) {
        errno = EIO;
        return -1;
    }
    return clock_log("%s\n", text);
}

/* 使用强符号覆盖 CubeMX 在 syscalls.c 中提供的 Newlib 弱符号 _write。 */
/**
 * 功能：覆盖 Newlib 写接口，在串口锁保护下按有限长度分块发送。
 * 参数：file，文件描述符，此实现不区分描述符；buffer，待发送字符缓冲区；length，传输或处理的字节数。
 * 返回：已发送字节数；零长度返回 0，未发送任何字节且出错返回 -1。
 */
int _write(int file, char *buffer, int length) {
    (void)file;
    if (length == 0) {
        return 0;
    }
    int scheduler_state = xTaskGetSchedulerState();
    if (buffer == NULL || length < 0 || __get_IPSR() != 0U ||
        scheduler_state == taskSCHEDULER_SUSPENDED) {
        /* 禁止在中断中获取实时操作系统互斥锁或轮询串口硬件。 */
        errno = EIO;
        return -1;
    }

    bool task_context = scheduler_state == taskSCHEDULER_RUNNING;
    if (task_context &&
        (console_mutex == NULL || xSemaphoreTake(console_mutex, portMAX_DELAY) != pdTRUE)) {
        errno = EIO;
        return -1;
    }

    int written = 0;
    while (written < length) {
        /* 115200 波特率下 256 字节约需 23 毫秒；每次轮询均设置有限超时。 */
        int remaining = length - written;
        uint16_t chunk = (uint16_t)(remaining > 256 ? 256 : remaining);
        if (HAL_UART_Transmit(&huart1, (uint8_t *)buffer + written, chunk, 100U) != HAL_OK) {
            errno = EIO;
            break;
        }
        written += chunk;
    }
    if (task_context) {
        (void)xSemaphoreGive(console_mutex);
    }
    return written == 0 ? -1 : written;
}

/**
 * 功能：通过日志底层写接口输出单个字符。
 * 参数：character，待输出字符。
 * 返回：成功返回输入字符，失败返回 -1。
 */
int __io_putchar(int character) {
    char byte = (char)character;
    return _write(1, &byte, 1) == 1 ? character : -1;
}
