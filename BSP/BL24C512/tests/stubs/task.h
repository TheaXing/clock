#ifndef BL24C512_TEST_TASK_H
#define BL24C512_TEST_TASK_H
#include "FreeRTOS.h"
#define taskSCHEDULER_NOT_STARTED 0
#define taskSCHEDULER_SUSPENDED 1
#define taskSCHEDULER_RUNNING 2
int xTaskGetSchedulerState(void);
void vTaskSuspendAll(void);
BaseType_t xTaskResumeAll(void);
void vTaskDelay(TickType_t ticks);
#endif
