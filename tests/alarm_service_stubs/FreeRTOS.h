#ifndef TEST_ALARM_FREERTOS_H
#define TEST_ALARM_FREERTOS_H
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
#define pdPASS 1
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(x) ((TickType_t)(x))
#define portMAX_DELAY UINT32_MAX
#define configTICK_RATE_HZ 1000
/* Simulate deferred higher-priority task wakeups while interrupts are masked. */
void alarm_test_enter_critical(void);
void alarm_test_exit_critical(void);
#define taskENTER_CRITICAL() alarm_test_enter_critical()
#define taskEXIT_CRITICAL() alarm_test_exit_critical()
#endif
