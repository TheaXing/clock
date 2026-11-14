#ifndef ESP_TEST_FREERTOS_H
#define ESP_TEST_FREERTOS_H
#include <stdint.h>
typedef int BaseType_t;
typedef uint32_t TickType_t;
#define pdFALSE 0
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define portTICK_PERIOD_MS 1U
#define taskENTER_CRITICAL() ((void)0)
void test_exit_critical(void);
#define taskEXIT_CRITICAL() test_exit_critical()
#define portYIELD_FROM_ISR(flag) ((void)(flag))
#endif
