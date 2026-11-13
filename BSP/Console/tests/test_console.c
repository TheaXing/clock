#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "usart.h"
#include "semphr.h"
#include "clock_console.h"

int _write(int, char *, int);
struct _reent;
void __malloc_lock(struct _reent *);
void __malloc_unlock(struct _reent *);
UART_HandleTypeDef huart1;
typedef struct { int depth, takes; } fake_mutex_t;
static fake_mutex_t uart_mutex, stream_mutex;
static int scheduler_running, in_isr, calls, suspend_depth, mutex_creations;
static int format_calls, fail_formatter, fail_creation = -1;
static HAL_StatusTypeDef tx_status;
static char output[1024];
static size_t output_length;
int xTaskGetSchedulerState(void) { return scheduler_running ? (suspend_depth ? 1 : 2) : 0; }
void vTaskSuspendAll(void) { ++suspend_depth; }
int xTaskResumeAll(void) { assert(suspend_depth > 0); --suspend_depth; return 1; }
SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    int index = mutex_creations++;
    assert(index < 2);
    if (index == fail_creation) { return NULL; }
    return index == 0 ? &uart_mutex : &stream_mutex;
}
int xSemaphoreTake(SemaphoreHandle_t lock, TickType_t timeout)
{
    fake_mutex_t *mutex = lock;
    assert(lock == &uart_mutex || lock == &stream_mutex);
    assert(!in_isr && !suspend_depth && timeout == portMAX_DELAY && mutex->depth++ == 0);
    ++mutex->takes;
    return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t lock)
{
    fake_mutex_t *mutex = lock;
    assert(lock == &uart_mutex || lock == &stream_mutex);
    assert(--mutex->depth == 0);
    return pdTRUE;
}
uint32_t __get_IPSR(void) { return (uint32_t)in_isr; }
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *uart, uint8_t *bytes, uint16_t length, uint32_t timeout)
{
    assert(!in_isr && !suspend_depth && uart == &huart1 && timeout != 0 && timeout < 1000);
    assert(uart_mutex.depth == (scheduler_running ? 1 : 0));
    ++calls;
    if (tx_status != HAL_OK) { return tx_status; }
    assert(output_length + length <= sizeof output);
    memcpy(output + output_length, bytes, length); output_length += length;
    return HAL_OK;
}
/* The test-only vprintf macro verifies the lock before formatting begins and
 * between output fragments. UART locks are independently nested within it.
 */
int clock_test_vprintf(const char *format, va_list arguments)
{
    assert(!in_isr && !suspend_depth);
    assert(stream_mutex.depth == (scheduler_running ? 1 : 0));
    assert(uart_mutex.depth == 0);
    ++format_calls;
    if (fail_formatter) { return -1; }
    char formatted[128];
    int length = vsnprintf(formatted, sizeof formatted, format, arguments);
    assert(length > 1 && length < (int)sizeof formatted);
    int first = length / 2;
    if (_write(1, formatted, first) != first) { return -1; }
    assert(stream_mutex.depth == (scheduler_running ? 1 : 0) && uart_mutex.depth == 0);
    if (_write(1, formatted + first, length - first) != length - first) { return -1; }
    assert(stream_mutex.depth == (scheduler_running ? 1 : 0) && uart_mutex.depth == 0);
    return length;
}

int main(int argc, char **argv)
{
    if (argc > 1) { fail_creation = strcmp(argv[1], "fail-first") == 0 ? 0 : 1; }
    clock_console_init();
    assert(mutex_creations == 2); /* UART locking alone leaves stdio formatting unprotected. */
    if (fail_creation >= 0)
    {
        assert(clock_log("allocation failure") == -1);
        scheduler_running = 1;
        assert(clock_log_line("allocation failure") == -1);
        assert(format_calls == 0 && calls == 0 && uart_mutex.takes == 0 && stream_mutex.takes == 0);
        puts("Console rejects logging when either mutex allocation fails");
        return 0;
    }
    clock_console_init();
    assert(mutex_creations == 2);
    __malloc_lock(NULL); __malloc_unlock(NULL);
    assert(suspend_depth == 0);
    assert(clock_log("early %d", 42) == 8);
    assert(stream_mutex.takes == 0 && uart_mutex.takes == 0);
    assert(output_length == 8 && memcmp(output, "early 42", 8) == 0);
    scheduler_running = 1;
    __malloc_lock(NULL); __malloc_lock(NULL);
    assert(suspend_depth == 2);
    __malloc_unlock(NULL); __malloc_unlock(NULL);
    assert(suspend_depth == 0);
    assert(clock_log(" task %d %.1f", 7, 2.5) == 11);
    assert(stream_mutex.takes == 1 && uart_mutex.takes == 2);
    assert(stream_mutex.depth == 0 && uart_mutex.depth == 0);
    assert(output_length == 19 && memcmp(output, "early 42 task 7 2.5", 19) == 0);
    assert(clock_log_line("100% complete") == 14);
    assert(output_length == 33 && memcmp(output + 19, "100% complete\n", 14) == 0);
    int previous_calls = calls, previous_formats = format_calls;
    int previous_stream_takes = stream_mutex.takes, previous_uart_takes = uart_mutex.takes;
    in_isr = 1;
    assert(clock_log("ISR") == -1 && clock_log_line("ISR") == -1 && _write(1, "ISR", 3) == -1);
    in_isr = 0;
    vTaskSuspendAll();
    assert(clock_log("suspended") == -1 && clock_log_line("suspended") == -1);
    assert(_write(1, "suspended", 9) == -1);
    (void)xTaskResumeAll();
    assert(clock_log(NULL) == -1 && clock_log_line(NULL) == -1);
    assert(calls == previous_calls && format_calls == previous_formats);
    assert(stream_mutex.takes == previous_stream_takes && uart_mutex.takes == previous_uart_takes);
    fail_formatter = 1;
    assert(clock_log("format failure") == -1);
    assert(stream_mutex.depth == 0 && uart_mutex.depth == 0);
    fail_formatter = 0;
    tx_status = HAL_ERROR;
    assert(clock_log("UART failure") == -1);
    assert(stream_mutex.depth == 0 && uart_mutex.depth == 0 && errno == EIO);
    assert(_write(1, NULL, 0) == 0);
    puts("Console whole-format locking, UART locking, pre-scheduler, context rejection, and error tests passed");
    return 0;
}
