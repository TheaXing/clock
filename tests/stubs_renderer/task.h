#ifndef TEST_RENDERER_TASK_H
#define TEST_RENDERER_TASK_H
#include <stdint.h>
#include "FreeRTOS.h"
typedef void (*TaskFunction_t)(void *);
typedef void *TaskHandle_t;
void vTaskDelay(uint32_t ticks);
BaseType_t xTaskCreate(TaskFunction_t function, const char *name, uint16_t depth,
                       void *param, UBaseType_t priority, TaskHandle_t *handle);
#endif
