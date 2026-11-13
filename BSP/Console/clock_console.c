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

/* Newlib uses its own heap through _sbrk; scheduler suspension serializes it.
 * SuspendAll/ResumeAll nest, including allocations made by float formatting.
 * Like the FreeRTOS heap, newlib allocation is forbidden in interrupt context.
 */
void __malloc_lock(struct _reent *reent)
{
    (void)reent;
    configASSERT(__get_IPSR() == 0U);
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        vTaskSuspendAll();
    }
}

void __malloc_unlock(struct _reent *reent)
{
    (void)reent;
    configASSERT(__get_IPSR() == 0U);
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        (void)xTaskResumeAll();
    }
}

void clock_console_init(void)
{
    if (console_mutex == NULL)
    {
        console_mutex = xSemaphoreCreateMutex();
    }
    if (stream_mutex == NULL)
    {
        stream_mutex = xSemaphoreCreateMutex();
    }
}

int clock_log(const char *format, ...)
{
    int scheduler_state = xTaskGetSchedulerState();
    if (format == NULL || __get_IPSR() != 0U ||
        scheduler_state == taskSCHEDULER_SUSPENDED ||
        console_mutex == NULL || stream_mutex == NULL)
    {
        errno = EIO;
        return -1;
    }

    bool task_context = scheduler_state == taskSCHEDULER_RUNNING;
    if (task_context && xSemaphoreTake(stream_mutex, portMAX_DELAY) != pdTRUE)
    {
        errno = EIO;
        return -1;
    }
    /* Newlib's stdout stream is shared even with per-task _reent structures.
     * The UART mutex in _write cannot protect formatting or stream buffers.
     */
    va_list arguments;
    va_start(arguments, format);
    int result = vprintf(format, arguments);
    va_end(arguments);
    if (fflush(stdout) != 0)
    {
        result = -1;
    }
    if (task_context)
    {
        (void)xSemaphoreGive(stream_mutex);
    }
    return result;
}

int clock_log_line(const char *text)
{
    if (text == NULL)
    {
        errno = EIO;
        return -1;
    }
    return clock_log("%s\n", text);
}

/* Strong symbol overrides CubeMX's weak newlib _write in syscalls.c. */
int _write(int file, char *buffer, int length)
{
    (void)file;
    if (length == 0)
    {
        return 0;
    }
    int scheduler_state = xTaskGetSchedulerState();
    if (buffer == NULL || length < 0 || __get_IPSR() != 0U ||
        scheduler_state == taskSCHEDULER_SUSPENDED)
    {
        /* Never take an RTOS mutex or poll UART hardware from an interrupt. */
        errno = EIO;
        return -1;
    }

    bool task_context = scheduler_state == taskSCHEDULER_RUNNING;
    if (task_context && (console_mutex == NULL ||
        xSemaphoreTake(console_mutex, portMAX_DELAY) != pdTRUE))
    {
        errno = EIO;
        return -1;
    }

    int written = 0;
    while (written < length)
    {
        /* 256 bytes take about 23 ms at 115200; each poll has a finite timeout. */
        int remaining = length - written;
        uint16_t chunk = (uint16_t)(remaining > 256 ? 256 : remaining);
        if (HAL_UART_Transmit(&huart1, (uint8_t *)buffer + written, chunk, 100U) != HAL_OK)
        {
            errno = EIO;
            break;
        }
        written += chunk;
    }
    if (task_context)
    {
        (void)xSemaphoreGive(console_mutex);
    }
    return written == 0 ? -1 : written;
}

int __io_putchar(int character)
{
    char byte = (char)character;
    return _write(1, &byte, 1) == 1 ? character : -1;
}
