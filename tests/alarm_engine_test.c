#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alarm_engine.h"

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static rtc_date_time_t date(unsigned y, unsigned m, unsigned d, unsigned h, unsigned min, unsigned sec)
{
    rtc_date_time_t value = { (uint16_t)y, (uint8_t)m, (uint8_t)d, (uint8_t)h, (uint8_t)min, (uint8_t)sec, 0 };
    return value;
}

static alarm_events_t step(alarm_engine_t *engine, rtc_date_time_t value, bool valid, uint32_t tick)
{
    return alarm_engine_step(engine, &value, valid, tick);
}

static void setup(alarm_engine_t *engine, alarm_config_t *config, uint8_t days)
{
    alarm_config_defaults(config);
    config->entries[0].enabled = 1;
    config->entries[0].repeat_mask = days;
    alarm_engine_init(engine, config);
}

static void test_calendar_and_validation(void)
{
    alarm_config_t config;
    rtc_date_time_t value;
    uint32_t seconds;
    unsigned i;
    static const struct { rtc_date_time_t value; uint32_t seconds; unsigned weekday; } fixtures[] = {
        { { 2000, 1, 1, 0, 0, 0, 0 }, 0U, 6 },
        { { 2000, 2, 29, 0, 0, 0, 7 }, 5097600U, 2 },
        { { 2026, 1, 1, 0, 0, 0, 7 }, 820540800U, 4 },
        { { 2099, 12, 31, 23, 59, 59, 0 }, 3155759999U, 4 }
    };
    alarm_config_defaults(&config);
    CHECK(alarm_config_valid(&config));
    for (i = 0; i < ALARM_COUNT; ++i) {
        CHECK(config.entries[i].enabled == 0);
        CHECK(config.entries[i].hour == 7 && config.entries[i].minute == 0);
        CHECK(config.entries[i].year == 2026 && config.entries[i].month == 1 && config.entries[i].day == 1);
        CHECK(config.entries[i].repeat_mask == 0x1f && config.entries[i].ring_seconds == 60 && config.entries[i].snooze_seconds == 300);
    }
    for (i = 0; i < sizeof fixtures / sizeof fixtures[0]; ++i) {
        CHECK(alarm_calendar_to_seconds(&fixtures[i].value, &seconds));
        CHECK(seconds == fixtures[i].seconds);
        CHECK(alarm_seconds_to_calendar(seconds, &value));
        CHECK(value.year == fixtures[i].value.year && value.month == fixtures[i].value.month && value.day == fixtures[i].value.day);
        CHECK(value.hour == fixtures[i].value.hour && value.minute == fixtures[i].value.minute && value.second == fixtures[i].value.second);
        CHECK(value.weekday == fixtures[i].weekday);
    }
    CHECK(!alarm_seconds_to_calendar(3155760000U, &value));
    value = date(2025, 2, 29, 0, 0, 0); CHECK(!alarm_calendar_to_seconds(&value, &seconds));
    value = date(2100, 1, 1, 0, 0, 0); CHECK(!alarm_calendar_to_seconds(&value, &seconds));
    value = date(1999, 12, 31, 0, 0, 0); CHECK(!alarm_calendar_to_seconds(&value, &seconds));
    value = date(2026, 0, 1, 0, 0, 0); CHECK(!alarm_calendar_to_seconds(&value, &seconds));
    value = date(2026, 4, 31, 0, 0, 0); CHECK(!alarm_calendar_to_seconds(&value, &seconds));
    value = date(2026, 1, 1, 24, 0, 0); CHECK(!alarm_calendar_to_seconds(&value, &seconds));
    value = date(2026, 1, 1, 0, 60, 0); CHECK(!alarm_calendar_to_seconds(&value, &seconds));
    value = date(2026, 1, 1, 0, 0, 60); CHECK(!alarm_calendar_to_seconds(&value, &seconds));
    CHECK(!alarm_calendar_to_seconds(NULL, &seconds)); CHECK(!alarm_calendar_to_seconds(&value, NULL));
    CHECK(!alarm_seconds_to_calendar(0, NULL)); CHECK(!alarm_config_valid(NULL)); CHECK(!alarm_entry_valid(NULL));
#define BAD_FIELD(field, value_) do { alarm_entry_t bad = config.entries[0]; bad.field = (value_); CHECK(!alarm_entry_valid(&bad)); } while (0)
    BAD_FIELD(year, 1999); BAD_FIELD(year, 2100); BAD_FIELD(month, 0); BAD_FIELD(month, 13);
    BAD_FIELD(day, 0); BAD_FIELD(day, 32); BAD_FIELD(hour, 24); BAD_FIELD(minute, 60);
    BAD_FIELD(repeat_mask, 128); BAD_FIELD(enabled, 2); BAD_FIELD(ring_seconds, 4); BAD_FIELD(ring_seconds, 601);
    BAD_FIELD(snooze_seconds, 4); BAD_FIELD(snooze_seconds, 3601);
#undef BAD_FIELD
    config.entries[0].ring_seconds = 5; config.entries[0].snooze_seconds = 5; CHECK(alarm_config_valid(&config));
    config.entries[0].ring_seconds = 600; config.entries[0].snooze_seconds = 3600; CHECK(alarm_config_valid(&config));
    config.entries[7].day = 0; CHECK(!alarm_config_valid(&config));
}

static void test_weekly_once_baseline(void)
{
    alarm_engine_t engine;
    alarm_config_t config;
    setup(&engine, &config, 0x1f);
    CHECK(step(&engine, date(2026,1,1,7,0,0), true, 0).started == 0);
    CHECK(step(&engine, date(2026,1,1,7,0,1), true, 100).started == 0);
    CHECK(step(&engine, date(2026,1,2,6,59,59), true, 200).started == 0);
    CHECK(step(&engine, date(2026,1,2,7,0,0), true, 300).started == 1);
    CHECK(step(&engine, date(2026,1,2,7,0,0), true, 400).started == 0);
    CHECK(step(&engine, date(2026,1,2,7,0,1), true, 500).started == 0);
    CHECK(alarm_engine_stop(&engine) == 1);
    CHECK(step(&engine, date(2026,1,3,6,59,59), true, 600).started == 0);
    CHECK(step(&engine, date(2026,1,3,7,0,0), true, 700).started == 0);
    setup(&engine, &config, 0);
    CHECK(step(&engine, date(2026,1,1,6,59,58), true, 0).started == 0);
    CHECK(step(&engine, date(2026,1,1,7,0,2), true, 100).started == 1);
    alarm_engine_stop(&engine);
    CHECK(step(&engine, date(2026,1,2,6,59,59), true, 200).started == 0);
    CHECK(step(&engine, date(2026,1,2,7,0,0), true, 300).started == 0);
}

static void test_leap_midnight_cross_week(void)
{
    alarm_engine_t engine;
    alarm_config_t config;
    setup(&engine, &config, 0);
    config.entries[0].year = 2028; config.entries[0].month = 2; config.entries[0].day = 29;
    config.entries[0].hour = 0; CHECK(alarm_engine_apply(&engine, &config));
    step(&engine, date(2028,2,28,23,59,59), true, 0);
    CHECK(step(&engine, date(2028,2,29,0,0,0), true, 100).started == 1);
    setup(&engine, &config, 1);
    config.entries[0].hour = 0; CHECK(alarm_engine_apply(&engine, &config));
    step(&engine, date(2026,1,4,23,59,58), true, 0);
    CHECK(step(&engine, date(2026,1,5,0,0,2), true, 100).started == 1);
    setup(&engine, &config, 0x40);
    step(&engine, date(2026,1,4,6,59,59), true, 0);
    CHECK(step(&engine, date(2026,1,4,7,0,0), true, 100).started == 1);
}

static void test_clock_changes_and_invalid_time(void)
{
    alarm_engine_t engine;
    alarm_config_t config;
    setup(&engine, &config, 0x7f);
    step(&engine, date(2026,1,1,6,59,59), false, 0);
    CHECK(step(&engine, date(2026,1,1,7,0,0), true, 100).started == 0);
    step(&engine, date(2026,1,2,6,59,59), true, 200);
    CHECK(step(&engine, date(2026,1,2,7,0,5), true, 300).started == 0); /* six second jump */
    CHECK(step(&engine, date(2026,1,2,6,59,59), true, 400).started == 0);
    CHECK(step(&engine, date(2026,1,2,7,0,0), true, 500).started == 1);
    alarm_engine_stop(&engine);
    step(&engine, date(2026,1,1,6,59,59), true, 600);
    CHECK(step(&engine, date(2026,1,1,7,0,0), true, 700).started == 1); /* older date never fired */
    step(&engine, date(2026,1,2,6,59,59), true, 800);
    CHECK(step(&engine, date(2026,1,2,7,0,0), true, 900).started == 0); /* same occurrence */
    step(&engine, date(2026,1,3,6,59,55), true, 1000);
    CHECK(step(&engine, date(2026,1,3,7,0,0), true, 1100).started == 1); /* allowed five seconds */
    alarm_engine_stop(&engine);
    step(&engine, date(2026,1,4,6,59,59), true, 1200);
    step(&engine, date(2026,2,30,7,0,0), true, 1300);
    CHECK(step(&engine, date(2026,1,4,7,0,0), true, 1400).started == 0);
    setup(&engine, &config, 0x7f);
    step(&engine, date(2019,1,1,6,59,59), true, 0);
    CHECK(step(&engine, date(2019,1,1,7,0,0), true, 100).started == 0);
    step(&engine, date(2026,1,1,6,59,59), true, 200);
    CHECK(step(&engine, date(2026,1,1,7,0,0), true, 300).started == 1);
}

static void test_future_clock_correction_preserves_real_occurrences(void)
{
    alarm_engine_t engine;
    alarm_config_t config;
    setup(&engine, &config, 0x7f);
    step(&engine, date(2035,1,1,6,59,59), true, 0);
    CHECK(step(&engine, date(2035,1,1,7,0,0), true, 100).started == 1);
    CHECK(alarm_engine_stop(&engine) == 1);
    /* Correcting a mistaken future clock must permit unseen actual dates. */
    CHECK(step(&engine, date(2026,1,1,6,59,59), true, 200).started == 0);
    CHECK(step(&engine, date(2026,1,1,7,0,0), true, 300).started == 1);
    CHECK(alarm_engine_stop(&engine) == 1);
    step(&engine, date(2026,1,2,6,59,59), true, 400);
    CHECK(step(&engine, date(2026,1,2,7,0,0), true, 500).started == 1);
    CHECK(alarm_engine_stop(&engine) == 1);
    /* All actually fired dates remain suppressed across either correction. */
    step(&engine, date(2026,1,1,6,59,59), true, 600);
    CHECK(step(&engine, date(2026,1,1,7,0,0), true, 700).started == 0);
    step(&engine, date(2035,1,1,6,59,59), true, 800);
    CHECK(step(&engine, date(2035,1,1,7,0,0), true, 900).started == 0);
}

static void test_occurrence_history_fifo_and_reset_policy(void)
{
    alarm_engine_t engine;
    alarm_config_t config;
    rtc_date_time_t before, due;
    unsigned i;
    setup(&engine, &config, 0x7f);
    for (i = 0U; i < ALARM_OCCURRENCE_HISTORY_SIZE; ++i) {
        CHECK(alarm_seconds_to_calendar(820565999U + i * 86400U, &before));
        CHECK(alarm_seconds_to_calendar(820566000U + i * 86400U, &due));
        step(&engine, before, true, i * 1000U);
        CHECK(step(&engine, due, true, i * 1000U + 100U).started == 1);
        CHECK(alarm_engine_stop(&engine) == 1);
    }
    CHECK(alarm_engine_apply(&engine, &config));
    step(&engine, date(2026,1,1,6,59,59), true, 40000U);
    CHECK(step(&engine, date(2026,1,1,7,0,0), true, 40100U).started == 0);
    /* The 33rd actual firing evicts only the oldest recorded date. */
    step(&engine, date(2026,2,2,6,59,59), true, 41000U);
    CHECK(step(&engine, date(2026,2,2,7,0,0), true, 41100U).started == 1);
    alarm_engine_stop(&engine);
    step(&engine, date(2026,1,2,6,59,59), true, 42000U);
    CHECK(step(&engine, date(2026,1,2,7,0,0), true, 42100U).started == 0);
    step(&engine, date(2026,1,1,6,59,59), true, 43000U);
    CHECK(step(&engine, date(2026,1,1,7,0,0), true, 43100U).started == 1);
    alarm_engine_stop(&engine);
    /* Editing this entry resets its exact history; reboot also clears it. */
    config.entries[0].ring_seconds = 5U;
    CHECK(alarm_engine_apply(&engine, &config));
    step(&engine, date(2026,1,2,6,59,59), true, 44000U);
    CHECK(step(&engine, date(2026,1,2,7,0,0), true, 44100U).started == 1);
    alarm_engine_init(&engine, &config);
    step(&engine, date(2026,1,2,6,59,59), true, 45000U);
    CHECK(step(&engine, date(2026,1,2,7,0,0), true, 45100U).started == 1);
}

static void test_simultaneous_duration_snooze_and_wrap(void)
{
    alarm_engine_t engine;
    alarm_config_t config;
    rtc_date_time_t before = date(2026,1,1,6,59,59), due = date(2026,1,1,7,0,0);
    unsigned i;
    alarm_config_defaults(&config);
    for (i = 0; i < ALARM_COUNT; ++i) config.entries[i].enabled = 1;
    config.entries[0].ring_seconds = 5;
    config.entries[0].snooze_seconds = 5;
    config.entries[1].snooze_seconds = 6;
    alarm_engine_init(&engine, &config);
    step(&engine, before, true, UINT32_MAX - 2000U);
    CHECK(step(&engine, due, true, UINT32_MAX - 1000U).started == 0xff);
    CHECK(step(&engine, due, false, 3998U).stopped == 0);
    CHECK(step(&engine, due, false, 3999U).stopped == 1);
    CHECK(engine.ringing_mask == 0xfe);
    CHECK(alarm_engine_snooze(&engine, 3999U) == 0xfe);
    CHECK(engine.ringing_mask == 0 && engine.snoozed_mask == 0xfe);
    CHECK(step(&engine, due, false, 9998U).started == 0);
    CHECK(step(&engine, due, false, 9999U).started == 2);
    CHECK(step(&engine, due, false, 10000U).started == 0);
    CHECK(alarm_engine_stop(&engine) == 2);
    CHECK(engine.snoozed_mask == 0 && engine.ringing_mask == 0);
    CHECK(step(&engine, due, false, 500000U).started == 0);
    setup(&engine, &config, 0x7f);
    config.entries[0].snooze_seconds = 5; config.entries[0].ring_seconds = 5;
    CHECK(alarm_engine_apply(&engine, &config));
    step(&engine, before, true, UINT32_MAX - 4000U);
    step(&engine, due, true, UINT32_MAX - 3000U);
    CHECK(alarm_engine_snooze(&engine, UINT32_MAX - 2000U) == 1);
    CHECK(step(&engine, due, false, 2998U).started == 0);
    CHECK(step(&engine, due, false, 2999U).started == 1);
    CHECK(step(&engine, due, false, 7998U).stopped == 0);
    CHECK(step(&engine, due, false, 7999U).stopped == 1);
    CHECK(alarm_engine_snooze(&engine, 8000U) == 0);
}

static void test_atomic_apply_preserves_unchanged(void)
{
    alarm_engine_t engine, saved;
    alarm_config_t config, bad;
    rtc_date_time_t before = date(2026,1,1,6,59,59), due = date(2026,1,1,7,0,0);
    setup(&engine, &config, 0x7f);
    config.entries[1].enabled = 1; config.entries[1].repeat_mask = 0x7f;
    CHECK(alarm_engine_apply(&engine, &config));
    step(&engine, before, true, 0); CHECK(step(&engine, due, true, 100).started == 3);
    saved = engine; bad = config; bad.entries[7].ring_seconds = 0;
    CHECK(!alarm_engine_apply(&engine, &bad)); CHECK(memcmp(&engine, &saved, sizeof engine) == 0);
    CHECK(alarm_engine_apply(&engine, &config)); CHECK(engine.ringing_mask == 3);
    config.entries[1].minute = 1; CHECK(alarm_engine_apply(&engine, &config));
    CHECK(engine.ringing_mask == 1 && memcmp(engine.occurrence_days[0], saved.occurrence_days[0], sizeof engine.occurrence_days[0]) == 0);
    CHECK(alarm_engine_snooze(&engine, 200) == 1);
    CHECK(alarm_engine_apply(&engine, &config)); CHECK(engine.snoozed_mask == 1);
    config.entries[0].snooze_seconds = 6; CHECK(alarm_engine_apply(&engine, &config));
    CHECK(engine.snoozed_mask == 0);
    CHECK(step(&engine, date(2026,1,1,7,0,1), true, 300).started == 0);
    step(&engine, date(2026,1,1,7,0,59), true, 400);
    CHECK(step(&engine, date(2026,1,1,7,1,0), true, 500).started == 2);
    /* Reset restores configuration, never the volatile ringing or snooze. */
    alarm_engine_init(&engine, &config);
    CHECK(engine.ringing_mask == 0 && engine.snoozed_mask == 0);
    CHECK(step(&engine, date(2026,1,1,7,1,0), true, 600).started == 0);
}

int main(void)
{
    test_calendar_and_validation();
    test_weekly_once_baseline();
    test_leap_midnight_cross_week();
    test_future_clock_correction_preserves_real_occurrences();
    test_clock_changes_and_invalid_time();
    test_occurrence_history_fifo_and_reset_policy();
    test_simultaneous_duration_snooze_and_wrap();
    test_atomic_apply_preserves_unchanged();
    printf("alarm engine: %u checks passed (%zu-byte engine state)\n", checks, sizeof(alarm_engine_t));
    return 0;
}




