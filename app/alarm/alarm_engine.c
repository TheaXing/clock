#include "alarm_engine.h"
#include <stddef.h>
#include <string.h>

#define ALARM_SECONDS_PER_DAY 86400U
#define ALARM_SECONDS_LIMIT 3155760000U /* 2100 年 1 月 1 日，不包含该时刻。 */
#define ALARM_MAX_FORWARD_STEP 5U

/**
 * 功能：判断支持年份是否为闰年。
 * 参数：year，待检查的年份。
 * 返回：闰年返回 true，否则返回 false。
 */
static bool leap_year(uint16_t year) {
    /* 支持的年份范围不包含非闰世纪年 2100。 */
    return (year % 4U) == 0U;
}

/**
 * 功能：取得指定年月的天数，并考虑闰年二月。
 * 参数：year，年份；month，月份。
 * 返回：有效月份的天数；月份无效时返回 0。
 */
static uint8_t month_days(uint16_t year, uint8_t month) {
    static const uint8_t days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1U || month > 12U) {
        return 0U;
    }
    if (month == 2U && leap_year(year)) {
        return 29U;
    }
    return days[month - 1U];
}

/**
 * 功能：校验日期是否位于 2000～2099 年的有效日历范围。
 * 参数：year，年份；month，月份；day，日期。
 * 返回：日期有效时返回 true，否则返回 false。
 */
static bool valid_date(uint16_t year, uint8_t month, uint8_t day) {
    return year >= 2000U && year <= 2099U && day >= 1U && day <= month_days(year, month);
}

/**
 * 功能：校验单个闹钟的日期、时刻、重复规则及响铃和贪睡时长。
 * 参数：entry，待校验的闹钟配置。
 * 返回：所有字段有效时返回 true，否则返回 false。
 */
bool alarm_entry_valid(const alarm_entry_t *entry) {
    return entry != NULL && valid_date(entry->year, entry->month, entry->day) &&
           entry->hour <= 23U && entry->minute <= 59U && entry->repeat_mask <= 0x7fU &&
           entry->enabled <= 1U && entry->ring_seconds >= 5U && entry->ring_seconds <= 600U &&
           entry->snooze_seconds >= 5U && entry->snooze_seconds <= 3600U;
}

/**
 * 功能：逐项校验完整的闹钟配置。
 * 参数：config，待校验的完整配置。
 * 返回：配置及全部闹钟有效时返回 true，否则返回 false。
 */
bool alarm_config_valid(const alarm_config_t *config) {
    unsigned i;
    if (config == NULL) {
        return false;
    }
    for (i = 0; i < ALARM_COUNT; ++i) {
        if (!alarm_entry_valid(&config->entries[i])) {
            return false;
        }
    }
    return true;
}

/**
 * 功能：生成默认的工作日早晨闹钟配置，默认均不启用。
 * 参数：config，接收默认配置的缓冲区；为空时不处理。
 * 返回：无。
 */
void alarm_config_defaults(alarm_config_t *config) {
    unsigned i;
    if (config == NULL) {
        return;
    }
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

/**
 * 功能：将有效日历时间转换为自 2000 年元旦起经过的秒数。
 * 参数：date，输入日历时间；seconds，接收转换结果。
 * 返回：转换成功返回 true，参数或时间无效返回 false。
 */
bool alarm_calendar_to_seconds(const rtc_date_time_t *date, uint32_t *seconds) {
    uint32_t days = 0U;
    uint16_t year;
    uint8_t month;
    if (date == NULL || seconds == NULL || !valid_date(date->year, date->month, date->day) ||
        date->hour > 23U || date->minute > 59U || date->second > 59U) {
        return false;
    }
    for (year = 2000U; year < date->year; ++year) {
        days += leap_year(year) ? 366U : 365U;
    }
    for (month = 1U; month < date->month; ++month) {
        days += month_days(date->year, month);
    }
    days += (uint32_t)date->day - 1U;
    *seconds = days * ALARM_SECONDS_PER_DAY + (uint32_t)date->hour * 3600U +
               (uint32_t)date->minute * 60U + date->second;
    return true;
}

/**
 * 功能：将自 2000 年元旦起经过的秒数转换为日历时间和星期。
 * 参数：seconds，待转换的秒数；date，接收日历时间。
 * 返回：结果位于 2000～2099 年且输出有效时返回 true，否则返回 false。
 */
bool alarm_seconds_to_calendar(uint32_t seconds, rtc_date_time_t *date) {
    rtc_date_time_t result;
    uint32_t days, remainder;
    uint16_t year_days;
    if (date == NULL || seconds >= ALARM_SECONDS_LIMIT) {
        return false;
    }
    memset(&result, 0, sizeof result);
    days = seconds / ALARM_SECONDS_PER_DAY;
    result.weekday = (uint8_t)((days + 5U) % 7U + 1U); /* 2000 年 1 月 1 日为星期六。 */
    result.year = 2000U;
    for (;;) {
        year_days = leap_year(result.year) ? 366U : 365U;
        if (days < year_days) {
            break;
        }
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

/**
 * 功能：逐字段比较两个闹钟配置，忽略结构体填充字节。
 * 参数：left，左侧配置；right，右侧配置。
 * 返回：所有配置字段相同时返回 true，否则返回 false。
 */
static bool entries_equal(const alarm_entry_t *left, const alarm_entry_t *right) {
    /* 逐字段比较，避免本机或编译器的填充字节影响语义。 */
    return left->year == right->year && left->month == right->month && left->day == right->day &&
           left->hour == right->hour && left->minute == right->minute &&
           left->ring_seconds == right->ring_seconds &&
           left->snooze_seconds == right->snooze_seconds &&
           left->repeat_mask == right->repeat_mask && left->enabled == right->enabled;
}

/**
 * 功能：初始化闹钟引擎并载入有效配置，无效配置使用默认值。
 * 参数：engine，待初始化的引擎；config，候选配置。
 * 返回：无。
 */
void alarm_engine_init(alarm_engine_t *engine, const alarm_config_t *config) {
    if (engine == NULL) {
        return;
    }
    memset(engine, 0, sizeof *engine);
    if (alarm_config_valid(config)) {
        engine->config = *config;
    } else {
        alarm_config_defaults(&engine->config);
    }
}

/**
 * 功能：应用完整有效配置，清理已修改闹钟的运行状态并保留未修改项的触发历史。
 * 参数：engine，目标引擎；config，待应用的完整配置。
 * 返回：应用成功返回 true，参数或配置无效返回 false。
 */
bool alarm_engine_apply(alarm_engine_t *engine, const alarm_config_t *config) {
    unsigned i;
    if (engine == NULL || !alarm_config_valid(config)) {
        return false;
    }
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

/**
 * 功能：检查指定闹钟在给定日期是否已经触发。
 * 参数：engine，闹钟引擎；index，从零开始的闹钟编号；day，日期键。
 * 返回：历史中存在该日期时返回 true，否则返回 false。
 */
static bool occurrence_fired(const alarm_engine_t *engine, unsigned index, uint16_t day) {
    unsigned i;
    for (i = 0U; i < ALARM_OCCURRENCE_HISTORY_SIZE; ++i) {
        if (engine->occurrence_days[index][i] == day) {
            return true;
        }
    }
    return false;
}

/**
 * 功能：按先进先出顺序记录指定闹钟已经触发的日期。
 * 参数：engine，闹钟引擎；index，从零开始的闹钟编号；day，日期键。
 * 返回：无。
 */
static void remember_occurrence(alarm_engine_t *engine, unsigned index, uint16_t day) {
    uint8_t next = engine->occurrence_next[index];
    engine->occurrence_days[index][next] = day;
    engine->occurrence_next[index] = (uint8_t)((next + 1U) % ALARM_OCCURRENCE_HISTORY_SIZE);
}

/**
 * 功能：启动指定闹钟响铃、取消其贪睡状态并登记启动事件。
 * 参数：engine，闹钟引擎；index，从零开始的闹钟编号；monotonic_ms，单调毫秒时间；events，累积事件。
 * 返回：无。
 */
static void start_ringing(alarm_engine_t *engine, unsigned index, uint32_t monotonic_ms,
                          alarm_events_t *events) {
    uint8_t bit = (uint8_t)(1U << index);
    engine->ring_started_ms[index] = monotonic_ms;
    engine->ringing_mask |= bit;
    engine->snoozed_mask &= (uint8_t)~bit;
    events->started |= bit;
}

/**
 * 功能：推进响铃和贪睡计时，并在可信时间跨过配置分钟时触发尚未执行的闹钟。
 * 参数：engine，闹钟引擎；date，当前日历时间；time_valid，时间可信标志；monotonic_ms，单调毫秒时间。
 * 返回：本次新开始及停止响铃的闹钟位掩码。
 */
alarm_events_t alarm_engine_step(alarm_engine_t *engine, const rtc_date_time_t *date,
                                 bool time_valid, uint32_t monotonic_ms) {
    alarm_events_t events = {0U, 0U};
    uint32_t wall_seconds;
    unsigned i;
    if (engine == NULL) {
        return events;
    }

    /* 日历时间不可用时，响铃和贪睡计时仍继续。 */
    for (i = 0; i < ALARM_COUNT; ++i) {
        uint8_t bit = (uint8_t)(1U << i);
        if ((engine->ringing_mask & bit) != 0U &&
            monotonic_ms - engine->ring_started_ms[i] >=
                (uint32_t)engine->config.entries[i].ring_seconds * 1000U) {
            engine->ringing_mask &= (uint8_t)~bit;
            events.stopped |= bit;
        }
        /* 截止时间最多在一小时之后，处于回绕计数的安全半周期范围内。 */
        if ((engine->snoozed_mask & bit) != 0U &&
            monotonic_ms - engine->snooze_due_ms[i] < 0x80000000U) {
            start_ringing(engine, i, monotonic_ms, &events);
        }
    }

    if (!time_valid || date == NULL || date->year < 2020U ||
        !alarm_calendar_to_seconds(date, &wall_seconds)) {
        engine->wall_initialized = false;
        return events;
    }
    if (engine->wall_initialized && wall_seconds > engine->last_wall_seconds &&
        wall_seconds - engine->last_wall_seconds <= ALARM_MAX_FORWARD_STEP) {
        uint32_t occurrence = wall_seconds - (wall_seconds % 60U);
        uint16_t occurrence_day = (uint16_t)(wall_seconds / ALARM_SECONDS_PER_DAY + 1U);
        if (occurrence > engine->last_wall_seconds) {
            uint8_t weekday = (uint8_t)(((wall_seconds / ALARM_SECONDS_PER_DAY) + 5U) % 7U);
            for (i = 0; i < ALARM_COUNT; ++i) {
                const alarm_entry_t *entry = &engine->config.entries[i];
                bool matches;
                if (!entry->enabled || entry->hour != date->hour || entry->minute != date->minute ||
                    occurrence_fired(engine, i, occurrence_day)) {
                    continue;
                }
                if (entry->repeat_mask != 0U) {
                    matches = (entry->repeat_mask & (1U << weekday)) != 0U;
                } else {
                    matches = entry->year == date->year && entry->month == date->month &&
                              entry->day == date->day;
                }
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

/**
 * 功能：停止所有正在响铃的闹钟并取消全部贪睡等待。
 * 参数：engine，目标引擎。
 * 返回：本次停止响铃的闹钟位掩码；引擎为空时返回 0。
 */
uint8_t alarm_engine_stop(alarm_engine_t *engine) {
    uint8_t stopped;
    if (engine == NULL) {
        return 0U;
    }
    stopped = engine->ringing_mask;
    engine->ringing_mask = 0U;
    engine->snoozed_mask = 0U;
    return stopped;
}

/**
 * 功能：将所有正在响铃的闹钟转入各自的贪睡等待。
 * 参数：engine，目标引擎；monotonic_ms，当前单调毫秒时间。
 * 返回：本次转入贪睡的闹钟位掩码；引擎为空时返回 0。
 */
uint8_t alarm_engine_snooze(alarm_engine_t *engine, uint32_t monotonic_ms) {
    uint8_t stopped;
    unsigned i;
    if (engine == NULL) {
        return 0U;
    }
    stopped = engine->ringing_mask;
    for (i = 0; i < ALARM_COUNT; ++i) {
        if ((stopped & (1U << i)) != 0U) {
            engine->snooze_due_ms[i] =
                monotonic_ms + (uint32_t)engine->config.entries[i].snooze_seconds * 1000U;
        }
    }
    engine->snoozed_mask |= stopped;
    engine->ringing_mask = 0U;
    return stopped;
}
