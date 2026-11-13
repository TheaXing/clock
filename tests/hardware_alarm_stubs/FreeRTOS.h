#ifndef ALARM_TEST_FREERTOS_H
#define ALARM_TEST_FREERTOS_H
#include <stdint.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define taskSCHEDULER_RUNNING 2
#define taskSCHEDULER_SUSPENDED 0
#define taskSCHEDULER_NOT_STARTED 1
#endif
