#ifndef TEST_APP_FREERTOS_H
#define TEST_APP_FREERTOS_H
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
#define pdPASS 1
#define pdFAIL 0
#define pdMS_TO_TICKS(milliseconds) ((TickType_t)(milliseconds))
#define configASSERT(condition) assert(condition)
size_t xPortGetFreeHeapSize(void);
size_t xPortGetMinimumEverFreeHeapSize(void);
#endif
