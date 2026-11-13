#include "FreeRTOS.h"
#define taskSCHEDULER_RUNNING 2
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t ticks);
BaseType_t xTaskGetSchedulerState(void);
