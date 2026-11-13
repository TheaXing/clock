#ifndef CLOCK_ALARM_ENGINE_H
#define CLOCK_ALARM_ENGINE_H
#include "alarm_types.h"
#include "clock_rtc.h"

typedef struct {
    uint8_t started;
    uint8_t stopped;
} alarm_events_t;

/* Keep the last 32 actually fired dates per entry, in firing order (FIFO).
 * Dates are exact uint16 day keys over supported years 2000..2099. An entry
 * has only one configured time per day, so a date uniquely identifies its
 * occurrence while that entry is unchanged. Init or any entry edit clears
 * that entry's history; applying unchanged entries preserves it. Older dates
 * may fire again after eviction. History is volatile across reset.
 */
#define ALARM_OCCURRENCE_HISTORY_SIZE 32U

typedef struct {
    alarm_config_t config;
    uint32_t last_wall_seconds;
    uint16_t occurrence_days[ALARM_COUNT][ALARM_OCCURRENCE_HISTORY_SIZE];
    uint8_t occurrence_next[ALARM_COUNT];
    uint32_t ring_started_ms[ALARM_COUNT];
    uint32_t snooze_due_ms[ALARM_COUNT];
    uint8_t ringing_mask;
    uint8_t snoozed_mask;
    bool wall_initialized;
} alarm_engine_t;

bool alarm_calendar_to_seconds(const rtc_date_time_t *date, uint32_t *seconds);
bool alarm_seconds_to_calendar(uint32_t seconds, rtc_date_time_t *date);
void alarm_engine_init(alarm_engine_t *engine, const alarm_config_t *config);
/* Apply a complete validated snapshot, keeping occurrence history of unchanged entries. */
bool alarm_engine_apply(alarm_engine_t *engine, const alarm_config_t *config);
alarm_events_t alarm_engine_step(alarm_engine_t *engine, const rtc_date_time_t *date,
                                 bool time_valid, uint32_t monotonic_ms);
uint8_t alarm_engine_stop(alarm_engine_t *engine);
uint8_t alarm_engine_snooze(alarm_engine_t *engine, uint32_t monotonic_ms);
#endif

