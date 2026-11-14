#ifndef CLOCK_ALARM_TYPES_H
#define CLOCK_ALARM_TYPES_H
#include <stdbool.h>
#include <stdint.h>
#define ALARM_COUNT 8U
#define ALARM_DEFAULT_SNOOZE_SECONDS 300U
#define ALARM_DEFAULT_RING_SECONDS 60U

/* repeat_mask 的位 0 为周一、位 6 为周日；0 表示在指定年月日单次触发。 */
/** 保存一个闹钟的日期、时刻、重复规则和提醒时长。 */
typedef struct {
    uint16_t year;           // 年份
    uint16_t ring_seconds;   // 单次响铃时长，单位为秒
    uint16_t snooze_seconds; // 贪睡等待时长，单位为秒
    uint8_t month;           // 月份，取值为 1～12
    uint8_t day;             // 当月日期，从 1 开始
    uint8_t hour;            // 小时，取值为 0～23
    uint8_t minute;          // 分钟，取值为 0～59
    uint8_t repeat_mask;     // 重复星期位掩码，位 0 为周一、位 6 为周日，0 表示指定日期单次触发
    uint8_t enabled;         // 启用标志，取值为 0 或 1
} alarm_entry_t;

/** 保存全部八个闹钟的完整配置。 */
typedef struct {
    alarm_entry_t entries[ALARM_COUNT]; // 按编号排列的全部闹钟条目
} alarm_config_t;
void alarm_config_defaults(alarm_config_t *config);
bool alarm_entry_valid(const alarm_entry_t *entry);
bool alarm_config_valid(const alarm_config_t *config);
#endif
