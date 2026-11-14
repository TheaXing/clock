#ifndef CLOCK_ALARM_SERVICE_H
#define CLOCK_ALARM_SERVICE_H
#include <stdbool.h>
#include "alarm_types.h"
#include "clock_rtc.h"

/* 快照副本保持一致，并独立于所有者任务的可变状态。 */
/** 向其他任务提供独立于所有者可变状态的一致闹钟快照。 */
typedef struct {
    alarm_config_t config;  // 完整闹钟配置
    rtc_date_time_t now;    // 当前日历时间
    uint32_t revision;      // 已验证的 EEPROM 日志代数
    uint32_t state_version; // 本次启动内用于乐观并发控制的状态版本
    uint8_t ringing_mask;   // 正在响铃的闹钟位掩码
    uint8_t snoozed_mask;   // 正在贪睡等待的闹钟位掩码
    bool time_valid;        // 当前日历时间是否可信
    bool saving;            // 是否存在正在保存的任务
    bool storage_ok;        // 最近持久化操作是否成功
    bool test_pending;      // 是否存在待触发的临时测试提醒
    bool test_ringing;      // 临时测试提醒是否正在响铃
    bool local_editing;     // 物理闹钟或时钟编辑器是否持有草稿
} alarm_service_snapshot_t;

typedef enum {
    ALARM_REMOTE_ACCEPTED,
    ALARM_REMOTE_BUSY,
    ALARM_REMOTE_CONFLICT,
    ALARM_REMOTE_INVALID,
    ALARM_REMOTE_UNAVAILABLE
} alarm_remote_submit_t;
typedef enum {
    ALARM_REMOTE_UNKNOWN,
    ALARM_REMOTE_PENDING,
    ALARM_REMOTE_SAVED,
    ALARM_REMOTE_FAILED
} alarm_remote_result_t;

typedef enum {
    ALARM_MAINTENANCE_NONE,
    ALARM_MAINTENANCE_PENDING,
    ALARM_MAINTENANCE_ACTIVE,
    ALARM_MAINTENANCE_REJECTED
} alarm_maintenance_state_t;

/* 网络任务只预留准入请求；所有者处理当前时间、按键和排队更新后原子确认。
 * ACTIVE 期间冻结新的保存、编辑、响铃及贪睡动作，失败释放后恢复正常服务。 */
bool alarm_service_maintenance_request(void);
alarm_maintenance_state_t alarm_service_maintenance_state(void);
void alarm_service_maintenance_release(void);

/* 仅在任务上下文中调用且不阻塞；全部服务任务可用之前返回 false。
 * state_version 在启动时初始化，并在每次持久化配置应用时推进；网络调用者必须
 * 同时校验独立的启动或会话令牌。 */
bool alarm_service_snapshot(alarm_service_snapshot_t *snapshot);
/* 提交基于当前快照的完整条目；存储层仅合并相对该快照修改的字段，以保留启动
 * 读取失败后尚未知晓的持久化数据。接受仅表示排队，并不表示保存完成；仅 ACCEPTED
 * 写入 request_id。所有者会复查版本和本地编辑、保存状态，抢先发生的物理输入
 * 可能使已接受请求最终以 FAILED 完成。 */
alarm_remote_submit_t alarm_service_remote_submit(uint8_t index, const alarm_entry_t *entry,
                                                  uint32_t expected_version, uint32_t *request_id);
/* 显式补丁的 fields_mask 必须为 1～0x01ff；位 0～8 依次选择 enabled、hour、
 * minute、repeat_mask、year、month、day、ring_seconds、snooze_seconds。
 * 选中字段即使等于内存快照也会应用；省略字段来自新读取的持久化配置。entry 是与
 * 快照合并的完整有效条目，整个持久化合并结果会在保存前校验，以保留启动数据不可用
 * 或提交未获确认时的显式意图。排队、版本、编辑器、请求编号和结果语义同完整提交接口。 */
alarm_remote_submit_t alarm_service_remote_submit_patch(uint8_t index, const alarm_entry_t *entry,
                                                        uint16_t fields_mask,
                                                        uint32_t expected_version,
                                                        uint32_t *request_id);
/* 保留当前待处理编号和最近八个完成结果；更旧或未知编号返回 UNKNOWN。
 * 编号仅在本次启动内有效，网络调用者还须校验会话令牌；SAVED 要求存储已经验证。 */
alarm_remote_result_t alarm_service_remote_result(uint32_t request_id);

/* 在 RTC、界面及 I2C1 就绪后、网络工作启动前由初始化任务调用。 */
void alarm_service_init(void);
/* 常规时钟和天气生产者在完整界面更新期间持有此锁；返回 false 表示闹钟菜单或响铃视图当前占屏。 */
bool alarm_service_display_begin(void);
void alarm_service_display_end(void);
#endif
