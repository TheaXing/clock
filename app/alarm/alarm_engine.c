#include "alarm_engine.h"
#include <stddef.h>
#include <string.h>

#define ALARM_SECONDS_PER_DAY 86400U
#define ALARM_SECONDS_LIMIT 3155760000U /* 2100-01-01, excluded. */
#define ALARM_MAX_FORWARD_STEP 5U

static bool leap_year(uint16_t year)
{
    /* The supported range excludes the non-leap century 2100. */
    return (year % 4U) == 0U;
}

static uint8_t month_days(uint16_t year, uint8_t month)
{
    static const uint8_t days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month < 1U || month > 12U) return 0U;
    if (month == 2U && leap_year(year)) return 29U;
    return days[month - 1U];
}

static bool valid_date(uint16_t year, uint8_t month, uint8_t day)
{
    return year >= 2000U && year <= 2099U && day >= 1U && day <= month_days(year, month);
}

bool alarm_entry_valid(const alarm_entry_t *entry)
{
    return entry != NULL && valid_date(entry->year, entry->month, entry->day)
        && entry->hour <= 23U && entry->minute <= 59U
        && entry->repeat_mask <= 0x7fU && entry->enabled <= 1U
        && entry->ring_seconds >= 5U && entry->ring_seconds <= 600U
        && entry->snooze_seconds >= 5U && entry->snooze_seconds <= 3600U;
}

bool alarm_config_valid(const alarm_config_t *config)
{
    unsigned i;
    if (config == NULL) return false;
    for (i = 0; i < ALARM_COUNT; ++i) {
        if (!alarm_entry_valid(&config->entries[i])) return false;
    }
    return true;
}

void alarm_config_defaults(alarm_config_t *config)
{
    unsigned i;
    if (config == NULL) return;
    memset(config, 0, sizeof *config);
    for (i = 0; i < ALARM_COUNT; ++i) {
        config->entries[i].year = 2026U;
        config->entries[i].month = 1U;
        config->entries[i].day = 1U;
        config->entries[i].hour = 7U;
        config->entries[i].repeat_mask = 0x1fU;
        config->entries[i].ring_seconds = ALARM_DEFAULT_RING_SECONDS;
        config->entries[i].snooze_seconds = ALARM_DEFAULT_SNOOZE_SECONDS;
    }
}

bool alarm_calendar_to_seconds(const rtc_date_time_t *date, uint32_t *seconds)
{
    uint32_t days = 0U;
    uint16_t year;
    uint8_t month;
    if (date == NULL || seconds == NULL || !valid_date(date->year, date->month, date->day)
        || date->hour > 23U || date->minute > 59U || date->second > 59U) return false;
    for (year = 2000U; year < date->year; ++year) days += leap_year(year) ? 366U : 365U;
    for (month = 1U; month < date->month; ++month) days += month_days(date->year, month);
    days += (uint32_t)date->day - 1U;
    *seconds = days * ALARM_SECONDS_PER_DAY + (uint32_t)date->hour * 3600U
        + (uint32_t)date->minute * 60U + date->second;
    return true;
}

bool alarm_seconds_to_calendar(uint32_t seconds, rtc_date_time_t *date)
{
    rtc_date_time_t result;
    uint32_t days, remainder;
    uint16_t year_days;
    if (date == NULL || seconds >= ALARM_SECONDS_LIMIT) return false;
    memset(&result, 0, sizeof result);
    days = seconds / ALARM_SECONDS_PER_DAY;
    result.weekday = (uint8_t)((days + 5U) % 7U + 1U); /* 2000-01-01 was Saturday. */
    result.year = 2000U;
    for (;;) {
        year_days = leap_year(result.year) ? 366U : 365U;
        if (days < year_days) break;
        days -= year_days;
        ++result.year;
    }
    result.month = 1U;
    while (days >= month_days(result.year, result.month)) {
        days -= month_days(result.year, result.month);
        ++result.month;
    }
    result.day = (uint8_t)(days + 1U);
    remainder = seconds % ALARM_SECONDS_PER_DAY;
    result.hour = (uint8_t)(remainder / 3600U);
    result.minute = (uint8_t)((remainder % 3600U) / 60U);
    result.second = (uint8_t)(remainder % 60U);
    *date = result;
    return true;
}

static bool entries_equal(const alarm_entry_t *left, const alarm_entry_t *right)
{
    /* Compare fields so native/compiler padding never changes semantics. */
    return left->year == right->year && left->month == right->month && left->day == right->day
        && left->hour == right->hour && left->minute == right->minute
        && left->ring_seconds == right->ring_seconds && left->snooze_seconds == right->snooze_seconds
        && left->repeat_mask == right->repeat_mask && left->enabled == right->enabled;
}

void alarm_engine_init(alarm_engine_t *engine, const alarm_config_t *config)
{
    if (engine == NULL) return;
    memset(engine, 0, sizeof *engine);
    if (alarm_config_valid(config)) engine->config = *config;
    else alarm_config_defaults(&engine->config);
}

bool alarm_engine_apply(alarm_engine_t *engine, const alarm_config_t *config)
{
    unsigned i;
    if (engine == NULL || !alarm_config_valid(config)) return false;
    for (i = 0; i < ALARM_COUNT; ++i) {
        if (!entries_equal(&engine->config.entries[i], &config->entries[i])) {
            uint8_t bit = (uint8_t)(1U << i);
            engine->ringing_mask &= (uint8_t)~bit;
            engine->snoozed_mask &= (uint8_t)~bit;
            memset(engine->occurrence_days[i], 0, sizeof engine->occurrence_days[i]);
            engine->occurrence_next[i] = 0U;
            engine->ring_started_ms[i] = 0U;
            engine->snooze_due_ms[i] = 0U;
        }
    }
    engine->config = *config;
    return true;
}

static bool occurrence_fired(const alarm_engine_t *engine, unsigned index, uint16_t day)
{
    unsigned i;
    for (i = 0U; i < ALARM_OCCURRENCE_HISTORY_SIZE; ++i)
        if (engine->occurrence_days[index][i] == day) return true;
    return false;
}

static void remember_occurrence(alarm_engine_t *engine, unsigned index, uint16_t day)
{
    uint8_t next = engine->occurrence_next[index];
    engine->occurrence_days[index][next] = day;
    engine->occurrence_next[index] = (uint8_t)((next + 1U) % ALARM_OCCURRENCE_HISTORY_SIZE);
}

static void start_ringing(alarm_engine_t *engine, unsigned index, uint32_t monotonic_ms, alarm_events_t *events)
{
    uint8_t bit = (uint8_t)(1U << index);
    engine->ring_started_ms[index] = monotonic_ms;
    engine->ringing_mask |= bit;
    engine->snoozed_mask &= (uint8_t)~bit;
    events->started |= bit;
}

alarm_events_t alarm_engine_step(alarm_engine_t *engine, const rtc_date_time_t *date,
                                 bool time_valid, uint32_t monotonic_ms)
{
    alarm_events_t events = { 0U, 0U };
    uint32_t wall_seconds;
    unsigned i;
    if (engine == NULL) return events;

    /* Ring and snooze timers continue even while wall-clock time is unavailable. */
    for (i = 0; i < ALARM_COUNT; ++i) {
        uint8_t bit = (uint8_t)(1U << i);
        if ((engine->ringing_mask & bit) != 0U
            && monotonic_ms - engine->ring_started_ms[i] >= (uint32_t)engine->config.entries[i].ring_seconds * 1000U) {
            engine->ringing_mask &= (uint8_t)~bit;
            events.stopped |= bit;
        }
        /* Deadlines are at most one hour away, safely inside the modular half range. */
        if ((engine->snoozed_mask & bit) != 0U
            && monotonic_ms - engine->snooze_due_ms[i] < 0x80000000U) {
            start_ringing(engine, i, monotonic_ms, &events);
        }
    }

    if (!time_valid || date == NULL || date->year < 2020U
        || !alarm_calendar_to_seconds(date, &wall_seconds)) {
        engine->wall_initialized = false;
        return events;
    }
    if (engine->wall_initialized && wall_seconds > engine->last_wall_seconds
        && wall_seconds - engine->last_wall_seconds <= ALARM_MAX_FORWARD_STEP) {
        uint32_t occurrence = wall_seconds - (wall_seconds % 60U);
        uint16_t occurrence_day = (uint16_t)(wall_seconds / ALARM_SECONDS_PER_DAY + 1U);
        if (occurrence > engine->last_wall_seconds) {
            uint8_t weekday = (uint8_t)(((wall_seconds / ALARM_SECONDS_PER_DAY) + 5U) % 7U);
            for (i = 0; i < ALARM_COUNT; ++i) {
                const alarm_entry_t *entry = &engine->config.entries[i];
                bool matches;
                if (!entry->enabled || entry->hour != date->hour || entry->minute != date->minute
                    || occurrence_fired(engine, i, occurrence_day)) continue;
                if (entry->repeat_mask != 0U) matches = (entry->repeat_mask & (1U << weekday)) != 0U;
                else matches = entry->year == date->year && entry->month == date->month && entry->day == date->day;
                if (matches) {
                    remember_occurrence(engine, i, occurrence_day);
                    start_ringing(engine, i, monotonic_ms, &events);
                }
            }
        }
    }
    engine->last_wall_seconds = wall_seconds;
    engine->wall_initialized = true;
    return events;
}

uint8_t alarm_engine_stop(alarm_engine_t *engine)
{
    uint8_t stopped;
    if (engine == NULL) return 0U;
    stopped = engine->ringing_mask;
    engine->ringing_mask = 0U;
    engine->snoozed_mask = 0U;
    return stopped;
}

uint8_t alarm_engine_snooze(alarm_engine_t *engine, uint32_t monotonic_ms)
{
    uint8_t stopped;
    unsigned i;
    if (engine == NULL) return 0U;
    stopped = engine->ringing_mask;
    for (i = 0; i < ALARM_COUNT; ++i) {
        if ((stopped & (1U << i)) != 0U) {
            engine->snooze_due_ms[i] = monotonic_ms + (uint32_t)engine->config.entries[i].snooze_seconds * 1000U;
        }
    }
    engine->snoozed_mask |= stopped;
    engine->ringing_mask = 0U;
    return stopped;
}

