#ifndef WIFI_APP_TEST_FREERTOS_H
#define WIFI_APP_TEST_FREERTOS_H
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
#define pdPASS 1
#define pdMS_TO_TICKS(value) ((TickType_t)(value))
#define configASSERT(value) assert(value)
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
size_t xPortGetFreeHeapSize(void);
size_t xPortGetMinimumEverFreeHeapSize(void);
#endif
