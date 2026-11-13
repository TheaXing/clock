#ifndef TEST_ALARM_QUEUE_H
#define TEST_ALARM_QUEUE_H
#include "FreeRTOS.h"
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(UBaseType_t count, UBaseType_t size);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait);
BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *item);
void vQueueDelete(QueueHandle_t queue);
#endif
