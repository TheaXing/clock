#ifndef TEST_RENDERER_FREERTOS_H
#define TEST_RENDERER_FREERTOS_H
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
#define pdPASS 1
#define pdFAIL 0
#define portMAX_DELAY UINT32_MAX
#define configASSERT(expression) assert(expression)
#define pdMS_TO_TICKS(ms) (ms)
void *pvPortMalloc(size_t size);
void vPortFree(void *ptr);
#endif
