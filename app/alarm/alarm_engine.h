#ifndef CLOCK_ALARM_ENGINE_H
#define CLOCK_ALARM_ENGINE_H
#include "alarm_types.h"
#include "clock_rtc.h"

/** 记录引擎一次推进产生的响铃开始和停止事件。 */
typedef struct {
    uint8_t started; // 本次开始响铃的闹钟位掩码
    uint8_t stopped; // 本次停止响铃的闹钟位掩码
} alarm_events_t;

/* 每个闹钟按触发顺序保留最近 32 个实际触发日期，采用先进先出淘汰。
 * 日期使用覆盖 2000～2099 年的精确 uint16 日期键；条目每天仅配置一个时刻，
 * 因此条目未修改时一个日期唯一对应一次触发。初始化或编辑会清空该条目的历史，
 * 应用未修改条目会保留历史。被淘汰的旧日期可能再次触发；复位后历史不保留。 */
#define ALARM_OCCURRENCE_HISTORY_SIZE 32U

/** 保存闹钟配置、去重历史及响铃和贪睡运行状态。 */
typedef struct {
    alarm_config_t config;      // 完整闹钟配置
    uint32_t last_wall_seconds; // 上次有效日历时间对应的累计秒数
    uint16_t occurrence_days[ALARM_COUNT][ALARM_OCCURRENCE_HISTORY_SIZE]; // 每个闹钟最近实际触发的日期键历史，0 表示空记录
    uint8_t occurrence_next[ALARM_COUNT];  // 各闹钟日期历史下次写入的位置
    uint32_t ring_started_ms[ALARM_COUNT]; // 各闹钟开始响铃的单调毫秒时刻
    uint32_t snooze_due_ms[ALARM_COUNT];   // 各闹钟贪睡到期的单调毫秒时刻
    uint8_t ringing_mask;                  // 正在响铃的闹钟位掩码
    uint8_t snoozed_mask;                  // 正在贪睡等待的闹钟位掩码
    bool wall_initialized;                 // 是否已记录可用于跨分钟判断的日历时间
} alarm_engine_t;

bool alarm_calendar_to_seconds(const rtc_date_time_t *date, uint32_t *seconds);
bool alarm_seconds_to_calendar(uint32_t seconds, rtc_date_time_t *date);
void alarm_engine_init(alarm_engine_t *engine, const alarm_config_t *config);
/* 应用完整且有效的配置快照，并保留未修改闹钟的触发历史。 */
bool alarm_engine_apply(alarm_engine_t *engine, const alarm_config_t *config);
alarm_events_t alarm_engine_step(alarm_engine_t *engine, const rtc_date_time_t *date,
                                 bool time_valid, uint32_t monotonic_ms);
uint8_t alarm_engine_stop(alarm_engine_t *engine);
uint8_t alarm_engine_snooze(alarm_engine_t *engine, uint32_t monotonic_ms);
#endif
