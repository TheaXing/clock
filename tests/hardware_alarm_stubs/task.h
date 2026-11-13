#ifndef ALARM_TEST_TASK_H
#define ALARM_TEST_TASK_H
#include "FreeRTOS.h"
int xTaskGetSchedulerState(void);
void vTaskSuspendAll(void);
BaseType_t xTaskResumeAll(void);
void vTaskDelay(TickType_t);
#endif
