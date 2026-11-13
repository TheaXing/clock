#ifndef CLOCK_ALARM_TYPES_H
#define CLOCK_ALARM_TYPES_H
#include <stdbool.h>
#include <stdint.h>
#define ALARM_COUNT 8U
#define ALARM_DEFAULT_SNOOZE_SECONDS 300U
#define ALARM_DEFAULT_RING_SECONDS 60U

/* repeat_mask: bit0 Monday .. bit6 Sunday; zero means once on year/month/day. */
typedef struct {
    uint16_t year;
    uint16_t ring_seconds;
    uint16_t snooze_seconds;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t repeat_mask;
    uint8_t enabled;
} alarm_entry_t;

typedef struct { alarm_entry_t entries[ALARM_COUNT]; } alarm_config_t;
void alarm_config_defaults(alarm_config_t *config);
bool alarm_entry_valid(const alarm_entry_t *entry);
bool alarm_config_valid(const alarm_config_t *config);
#endif
