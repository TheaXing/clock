#include "FreeRTOS.h"
#define taskSCHEDULER_RUNNING 2
TickType_t xTaskGetTickCount(void);
TickType_t xTaskGetTickCountFromISR(void);
void vTaskDelay(TickType_t ticks);
BaseType_t xTaskGetSchedulerState(void);
void vTaskSuspendAll(void);
BaseType_t xTaskResumeAll(void);
