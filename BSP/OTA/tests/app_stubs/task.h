#ifndef OTA_APP_TEST_TASK_H
#define OTA_APP_TEST_TASK_H
#include "FreeRTOS.h"
typedef void (*TaskFunction_t)(void *);
typedef void *TaskHandle_t;
BaseType_t xTaskCreate(TaskFunction_t, const char *, uint16_t, void *, UBaseType_t, TaskHandle_t *);
void vTaskDelete(TaskHandle_t);
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t);
void vTaskDelayUntil(TickType_t *, TickType_t);
int xTaskGetSchedulerState(void);
void vTaskSuspendAll(void);
BaseType_t xTaskResumeAll(void);
#endif
