#ifndef OTA_APP_TEST_FREERTOS_H
#define OTA_APP_TEST_FREERTOS_H
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
#define pdPASS 1
#define pdTRUE 1
#define pdFALSE 0
#define pdFAIL 0
#define configASSERT(expression) assert(expression)
#define pdMS_TO_TICKS(x) ((TickType_t)(x))
#define portMAX_DELAY UINT32_MAX
#define configTICK_RATE_HZ 1000
#define taskSCHEDULER_RUNNING 2
#define taskSCHEDULER_NOT_STARTED 0
#define taskSCHEDULER_SUSPENDED 1
#ifdef OTA_TEST_CRITICAL_HOOK
void ota_test_enter_critical(void);
void ota_test_exit_critical(void);
#define taskENTER_CRITICAL() ota_test_enter_critical()
#define taskEXIT_CRITICAL() ota_test_exit_critical()
#else
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
#endif
void *pvPortMalloc(size_t size);
void vPortFree(void *pointer);
#endif
