#ifndef TEST_CONSOLE_FREERTOS_H
#define TEST_CONSOLE_FREERTOS_H
#include <stdint.h>
#include <assert.h>
typedef uint32_t TickType_t;
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY UINT32_MAX
#define taskSCHEDULER_RUNNING 2
#define taskSCHEDULER_SUSPENDED 1
#define taskSCHEDULER_NOT_STARTED 0
#define configASSERT(expr) assert(expr)
#endif
