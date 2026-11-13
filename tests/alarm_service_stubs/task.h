#ifndef TEST_ALARM_TASK_H
#define TEST_ALARM_TASK_H
#include "FreeRTOS.h"
typedef void (*TaskFunction_t)(void *);
typedef void *TaskHandle_t;
BaseType_t xTaskCreate(TaskFunction_t function, const char *name, uint16_t depth,
                       void *parameter, UBaseType_t priority, TaskHandle_t *handle);
void vTaskDelete(TaskHandle_t handle);
TickType_t xTaskGetTickCount(void);
void vTaskDelayUntil(TickType_t *previous, TickType_t increment);
#endif
