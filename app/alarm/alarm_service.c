#include "alarm_service.h"
#include "alarm_engine.h"
#include "alarm_menu.h"
#include "alarm_page.h"
#include "alarm_store.h"
#include "app.h"
#include "bl24c512.h"
#include "board_keys.h"
#include "buzzer.h"
#include "clock_console.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#if defined(CLOCK_OTA_ENABLED) && CLOCK_OTA_ENABLED
#include "ota_service.h"
#endif
#include <string.h>

/* BSP 和引擎均使用可回绕的 32 位毫秒计数。 */
_Static_assert(configTICK_RATE_HZ == 1000, "Alarm service requires 1 kHz RTOS ticks");
_Static_assert(sizeof(TickType_t) == sizeof(uint32_t), "Alarm service requires 32-bit ticks");
#define REMOTE_RESULT_COUNT 8U

/** 将闹钟编辑基线和目标条目传递给持久化任务。 */
typedef struct {
    alarm_entry_t base;   // 编辑开始时的条目基线
    alarm_entry_t entry;  // 完整闹钟条目
    uint16_t fields_mask; // 显式字段位掩码，0 表示物理菜单及旧接口的差异合并
    uint8_t selected;     // 当前选中闹钟的从零开始编号
} save_job_t;

/** 将持久化结果、代数及完整配置回传给所有者任务。 */
typedef struct {
    alarm_store_result_t result; // EEPROM 读取、合并或保存结果
    uint32_t generation;         // 保存完成后的存储代数
    alarm_config_t config;       // 完整闹钟配置
} save_result_t;

/** 传递需要所有者重新校验的远程闹钟更新请求。 */
typedef struct {
    alarm_entry_t entry;       // 完整闹钟条目
    uint32_t expected_version; // 提交时要求匹配的状态版本
    uint32_t request_id;       // 本次启动内的异步请求编号
    uint16_t fields_mask;      // 显式更新字段位掩码
    uint8_t selected;          // 当前选中闹钟的从零开始编号
} remote_job_t;

/* 不向较慢的显示任务传递指向所有者可变状态的指针。 */
/** 向较慢的显示任务传递独立快照，不引用所有者的可变数据。 */
typedef struct {
    alarm_config_t config;  // 完整闹钟配置
    alarm_menu_t menu;      // 菜单状态快照
    rtc_date_time_t now;    // 当前日历时间
    uint32_t revision;      // 已验证的 EEPROM 日志代数
    uint32_t display_epoch; // 生成快照时的屏幕所有权代次
    uint8_t ringing_mask;   // 正在响铃的闹钟位掩码
    uint8_t snoozed_mask;   // 正在贪睡等待的闹钟位掩码
    bool time_valid;        // 当前日历时间是否可信
    bool saving;            // 是否存在正在保存的任务
    bool storage_ok;        // 最近持久化操作是否成功
    bool test_pending;      // 是否存在待触发的临时测试提醒
    bool test_ringing;      // 临时测试提醒是否正在响铃
    const char *message;    // 仅引用静态字符串字面量的状态说明
} view_snapshot_t;

static SemaphoreHandle_t display_mutex;
static QueueHandle_t save_queue, result_queue, view_queue, remote_queue;
static volatile bool owns_display;
static volatile uint32_t display_epoch;
static bool initialized;
static alarm_maintenance_state_t maintenance;
/* 仅在短 RTOS 临界区中与网络任务共享；网络调用者不得接触引擎或物理菜单。 */
/** 保存仅在短 RTOS 临界区内与网络任务共享的状态和请求结果。 */
static struct {
    alarm_service_snapshot_t snapshot; // 向其他任务发布的一致服务快照
    uint32_t pending_id;               // 当前预留的请求编号，0 表示无请求
    uint32_t next_id;                  // 最近分配的请求编号
    /** 保存一个已完成远程请求的编号和最终结果。 */
    struct {
        uint32_t id;                  // 本次启动内的远程请求编号
        alarm_remote_result_t result; // 已经完成的远程处理结果
    } completed[REMOTE_RESULT_COUNT]; // 最近完成请求的循环结果缓存
    uint8_t completed_next;           // 下一个完成结果写入的位置
} published;
static alarm_store_t store;
/** 保存仅由闹钟所有者任务修改的运行状态。 */
static struct {
    alarm_engine_t engine;    // 闹钟调度引擎
    alarm_menu_t menu;        // 本地菜单及编辑草稿
    rtc_date_time_t now;      // 当前日历时间
    uint32_t revision;        // 已验证的 EEPROM 日志代数
    uint32_t state_version;   // 本次启动内用于乐观并发控制的状态版本
    uint32_t save_request_id; // 当前保存关联的远程编号，0 表示物理菜单保存
    uint32_t last_engine_ms;  // 上次推进引擎的单调毫秒时刻
    uint32_t last_view_ms;    // 上次发布显示快照的单调毫秒时刻
    uint32_t test_due_ms;     // 临时测试提醒到期的单调毫秒时刻
    uint32_t test_started_ms; // 临时测试开始响铃的单调毫秒时刻
    uint32_t beep_started_ms; // 当前蜂鸣节奏开始的单调毫秒时刻
    bool time_valid;          // 当前日历时间是否可信
    bool saving;              // 是否存在正在保存的任务
    bool storage_ok;          // 最近持久化操作是否成功
    bool test_pending;        // 是否存在待触发的临时测试提醒
    bool test_ringing;        // 临时测试提醒是否正在响铃
    bool beep_active;         // 蜂鸣节奏是否活动
    bool buzzer_on;           // 最近实际输出的蜂鸣器开关状态
    bool dirty;               // 显示内容是否需要刷新
    const char *message;      // 静态界面状态说明
} owner;

/**
 * 功能：判断物理菜单是否正在编辑闹钟或本地时钟草稿。
 * 参数：无。
 * 返回：编辑器活动时返回 true，否则返回 false。
 */
static bool local_editor_active(void) {
    return owner.menu.view == ALARM_VIEW_EDIT || owner.menu.view == ALARM_VIEW_CLOCK;
}

/**
 * 功能：复制所有者状态，并在短临界区内发布一致的服务快照。
 * 参数：无。
 * 返回：无。
 */
static void publish_service_snapshot(void) {
    alarm_service_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof snapshot);
    snapshot.config = owner.engine.config;
    snapshot.now = owner.now;
    snapshot.revision = owner.revision;
    snapshot.state_version = owner.state_version;
    snapshot.ringing_mask = owner.engine.ringing_mask;
    snapshot.snoozed_mask = owner.engine.snoozed_mask;
    snapshot.time_valid = owner.time_valid;
    snapshot.saving = owner.saving;
    snapshot.storage_ok = owner.storage_ok;
    snapshot.test_pending = owner.test_pending;
    snapshot.test_ringing = owner.test_ringing;
    snapshot.local_editing = local_editor_active();
    taskENTER_CRITICAL();
    published.snapshot = snapshot;
    taskEXIT_CRITICAL();
}

/**
 * 功能：在任务上下文中无等待地读取完整一致的服务快照。
 * 参数：snapshot，接收快照的缓冲区。
 * 返回：服务任务均可用且输出有效时返回 true，否则返回 false。
 */
bool alarm_service_snapshot(alarm_service_snapshot_t *snapshot) {
    bool available;
    if (snapshot == NULL) {
        return false;
    }
    taskENTER_CRITICAL();
    available = initialized;
    if (available) {
        *snapshot = published.snapshot;
    }
    taskEXIT_CRITICAL();
    return available;
}

/**
 * 功能：校验远程闹钟更新并在短临界区内预留唯一请求、排入队列。
 * 参数：index，从零开始的闹钟编号；entry，完整有效配置；fields_mask，显式字段掩码，0 表示差异合并；expected_version，期望运行版本；request_id，仅接受时写入请求编号。
 * 返回：请求已接受、忙、版本冲突、无效或服务不可用的状态。
 */
static alarm_remote_submit_t remote_submit(uint8_t index, const alarm_entry_t *entry,
                                           uint16_t fields_mask, uint32_t expected_version,
                                           uint32_t *request_id) {
    remote_job_t job;
    alarm_remote_submit_t result;
    if (index >= ALARM_COUNT || entry == NULL || request_id == NULL) {
        return ALARM_REMOTE_INVALID;
    }
    job.entry = *entry;
    if (!alarm_entry_valid(&job.entry)) {
        return ALARM_REMOTE_INVALID;
    }
    job.selected = index;
    job.fields_mask = fields_mask;
    job.expected_version = expected_version;
    taskENTER_CRITICAL();
    if (!initialized) {
        result = ALARM_REMOTE_UNAVAILABLE;
    } else if (maintenance != ALARM_MAINTENANCE_NONE ||
               published.pending_id != 0U || published.snapshot.saving ||
               published.snapshot.local_editing) {
        result = ALARM_REMOTE_BUSY;
    } else if (expected_version != published.snapshot.state_version) {
        result = ALARM_REMOTE_CONFLICT;
    } else {
        job.request_id = published.next_id + 1U;
        if (job.request_id == 0U) {
            job.request_id = 1U;
        }
        /* 一个请求预留同时覆盖排队请求及其 EEPROM 保存任务。 */
        if (xQueueSend(remote_queue, &job, 0U) != pdTRUE) {
            result = ALARM_REMOTE_BUSY;
        } else {
            published.next_id = job.request_id;
            published.pending_id = job.request_id;
            *request_id = job.request_id;
            result = ALARM_REMOTE_ACCEPTED;
        }
    }
    taskEXIT_CRITICAL();
    return result;
}

/**
 * 功能：预留 OTA 维护请求并立即阻止新的远程更新，最终准入由所有者确认。
 * 参数：无。
 * 返回：请求成功预留返回 true，服务不可用或存在冲突返回 false。
 */
bool alarm_service_maintenance_request(void) {
    bool accepted = false;
    taskENTER_CRITICAL();
    if (initialized && maintenance == ALARM_MAINTENANCE_NONE &&
        published.pending_id == 0U && !published.snapshot.saving &&
        !published.snapshot.local_editing && published.snapshot.ringing_mask == 0U &&
        published.snapshot.snoozed_mask == 0U && !published.snapshot.test_pending &&
        !published.snapshot.test_ringing) {
        maintenance = ALARM_MAINTENANCE_PENDING;
        accepted = true;
    }
    taskEXIT_CRITICAL();
    return accepted;
}

/**
 * 功能：读取所有者发布的维护准入状态。
 * 参数：无。
 * 返回：维护请求阶段。
 */
alarm_maintenance_state_t alarm_service_maintenance_state(void) {
    alarm_maintenance_state_t result;
    taskENTER_CRITICAL();
    result = maintenance;
    taskEXIT_CRITICAL();
    return result;
}

/**
 * 功能：在 OTA 保存失败后释放维护准入，使下一次所有者循环恢复本地操作。
 * 参数：无。
 * 返回：无。
 */
void alarm_service_maintenance_release(void) {
    taskENTER_CRITICAL();
    maintenance = ALARM_MAINTENANCE_NONE;
    taskEXIT_CRITICAL();
}

/**
 * 功能：由所有者在本轮动作之后重新检查全部冲突并原子确认维护准入。
 * 参数：无。
 * 返回：无。
 */
static void collect_maintenance(void) {
    taskENTER_CRITICAL();
    if (maintenance == ALARM_MAINTENANCE_PENDING) {
        bool busy = owner.saving || local_editor_active() || published.pending_id != 0U ||
                    owner.engine.ringing_mask != 0U || owner.engine.snoozed_mask != 0U ||
                    owner.test_pending || owner.test_ringing;
        maintenance = busy ? ALARM_MAINTENANCE_REJECTED : ALARM_MAINTENANCE_ACTIVE;
    }
    taskEXIT_CRITICAL();
}

/**
 * 功能：从当前快照提交完整闹钟，由保存任务仅合并实际修改的字段。
 * 参数：index，从零开始的闹钟编号；entry，完整配置；expected_version，期望运行版本；request_id，仅接受时写入请求编号。
 * 返回：排队结果；接受表示等待处理，持久化完成须查询结果。
 */
alarm_remote_submit_t alarm_service_remote_submit(uint8_t index, const alarm_entry_t *entry,
                                                  uint32_t expected_version, uint32_t *request_id) {
    return remote_submit(index, entry, 0U, expected_version, request_id);
}

/**
 * 功能：查询当前待处理请求或最近八个已完成请求的结果。
 * 参数：request_id，本次启动期间的请求编号。
 * 返回：待处理、保存成功、失败或未知；保存成功表示已验证持久化。
 */
alarm_remote_result_t alarm_service_remote_result(uint32_t request_id) {
    alarm_remote_result_t result = ALARM_REMOTE_UNKNOWN;
    unsigned i;
    taskENTER_CRITICAL();
    if (initialized && request_id != 0U) {
        if (request_id == published.pending_id) {
            result = ALARM_REMOTE_PENDING;
        } else {
            for (i = 0; i < REMOTE_RESULT_COUNT; ++i) {
                if (request_id == published.completed[i].id) {
                    result = published.completed[i].result;
                    break;
                }
            }
        }
    }
    taskEXIT_CRITICAL();
    return result;
}

/**
 * 功能：提交显式字段补丁，在新鲜持久化配置上合并并完整校验。
 * 参数：index，从零开始的闹钟编号；entry，与快照合并的完整有效条目；fields_mask，取值 1～0x01ff 的字段掩码；expected_version，期望运行版本；request_id，仅接受时写入请求编号。
 * 返回：请求排队状态，实际保存结果需另行查询。
 */
alarm_remote_submit_t alarm_service_remote_submit_patch(uint8_t index, const alarm_entry_t *entry,
                                                        uint16_t fields_mask,
                                                        uint32_t expected_version,
                                                        uint32_t *request_id) {
    if (fields_mask == 0U || fields_mask > 0x01ffU) {
        return ALARM_REMOTE_INVALID;
    }
    return remote_submit(index, entry, fields_mask, expected_version, request_id);
}

/**
 * 功能：先发布更新后的配置快照，再登记远程请求结果并释放预留。
 * 参数：request_id，请求编号，0 表示无需登记；result，最终处理结果。
 * 返回：无。
 */
static void complete_remote(uint32_t request_id, alarm_remote_result_t result) {
    if (request_id == 0U) {
        return;
    }
    /* 调用者观察到 SAVED 时，必须已经能够读取新配置。 */
    publish_service_snapshot();
    taskENTER_CRITICAL();
    if (published.pending_id == request_id) {
        published.completed[published.completed_next].id = request_id;
        published.completed[published.completed_next].result = result;
        published.completed_next = (uint8_t)((published.completed_next + 1U) % REMOTE_RESULT_COUNT);
        published.pending_id = 0U;
    }
    taskEXIT_CRITICAL();
}

/**
 * 功能：将存储层的读取回调转接至 EEPROM 驱动。
 * 参数：context，保留上下文，当前未使用；address，起始地址；data，输出数据；length，读取字节数。
 * 返回：驱动读取成功返回 true，否则返回 false。
 */
static bool read_eeprom(void *context, uint32_t address, void *data, size_t length) {
    (void)context;
    return bl24c512_read(address, data, length);
}

/**
 * 功能：将存储层的写入回调转接至 EEPROM 驱动。
 * 参数：context，保留上下文，当前未使用；address，起始地址；data，输入数据；length，写入字节数。
 * 返回：驱动写入成功返回 true，否则返回 false。
 */
static bool write_eeprom(void *context, uint32_t address, const void *data, size_t length) {
    (void)context;
    return bl24c512_write(address, data, length);
}

/**
 * 功能：检查 RTC 已就绪且日历时间位于可信的有效年份范围。
 * 参数：date，待检查的日历时间。
 * 返回：RTC 就绪、年份不早于 2020 且日期有效时返回 true，否则返回 false。
 */
static bool trusted_time(const rtc_date_time_t *date) {
    uint32_t seconds;
    return clock_rtc_ready() && date->year >= 2020U && alarm_calendar_to_seconds(date, &seconds);
}

/**
 * 功能：取得常规时钟和天气显示所需的互斥锁，并检查屏幕归属。
 * 参数：无。
 * 返回：允许常规页面更新时返回 true；闹钟占屏或加锁失败返回 false。
 */
bool alarm_service_display_begin(void) {
    bool busy;
    if (display_mutex == NULL) {
        return true;
    }
    if (xSemaphoreTake(display_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    taskENTER_CRITICAL();
    busy = owns_display;
    taskEXIT_CRITICAL();
    if (busy) {
        (void)xSemaphoreGive(display_mutex);
        return false;
    }
    return true;
}

/**
 * 功能：释放常规页面更新持有的显示互斥锁。
 * 参数：无。
 * 返回：无。
 */
void alarm_service_display_end(void) {
    if (display_mutex != NULL) {
        (void)xSemaphoreGive(display_mutex);
    }
}

/**
 * 功能：将显式选中字段或相对草稿基线有变化的字段合并到持久化闹钟。
 * 参数：durable，待更新的持久化条目；job，包含草稿、基线与字段掩码的保存任务。
 * 返回：无。
 */
static void merge_edited_entry(alarm_entry_t *durable, const save_job_t *job) {
    /* 即使只更新选中的闹钟，基于不可用或过期启动数据的开关操作也不得覆盖用户未编辑的字段。 */
    /* 显式云端字段即使等于内存基线也表达更新意图；其他调用者继续采用原有的编辑差异语义。 */
#define MERGE_FIELD(field, bit) \
    do { \
        if (job->fields_mask != 0U ? (job->fields_mask & (1U << (bit))) != 0U \
                                  : job->entry.field != job->base.field) { \
            durable->field = job->entry.field; \
        } \
    } while (0)
    MERGE_FIELD(enabled, 0U);
    MERGE_FIELD(hour, 1U);
    MERGE_FIELD(minute, 2U);
    MERGE_FIELD(repeat_mask, 3U);
    MERGE_FIELD(year, 4U);
    MERGE_FIELD(month, 5U);
    MERGE_FIELD(day, 6U);
    MERGE_FIELD(ring_seconds, 7U);
    MERGE_FIELD(snooze_seconds, 8U);
#undef MERGE_FIELD
}

/**
 * 功能：循环读取最新持久化配置、合并用户修改并回传保存结果。
 * 参数：argument，FreeRTOS 任务参数，当前未使用。
 * 返回：无，任务持续运行。
 */
static void save_worker(void *argument) {
    save_job_t job;
    save_result_t result;
    (void)argument;
    for (;;) {
        if (xQueueReceive(save_queue, &job, portMAX_DELAY) == pdTRUE) {
            memset(&result, 0, sizeof result);
#if defined(CLOCK_OTA_ENABLED) && CLOCK_OTA_ENABLED
            if (!bl24c512_transaction_begin()) {
                result.result = ALARM_STORE_IO_ERROR;
                result.generation = store.generation;
                (void)xQueueSend(result_queue, &result, portMAX_DELAY);
                continue;
            }
#endif
            /* 启动读取失败可能使内存保留默认值或旧槽位配置。应在新近验证的持久化快照上
             * 仅合并用户编辑的条目；未读槽位可能包含更新代数。 */
            result.result = alarm_store_load_for_update(&store, &result.config);
            if (result.result == ALARM_STORE_EMPTY || result.result == ALARM_STORE_CORRUPT) {
                alarm_config_defaults(&result.config);
            }
            if (result.result == ALARM_STORE_OK || result.result == ALARM_STORE_EMPTY ||
                result.result == ALARM_STORE_CORRUPT) {
                if (job.selected < ALARM_COUNT && alarm_entry_valid(&job.entry) &&
                    job.fields_mask <= 0x01ffU) {
                    merge_edited_entry(&result.config.entries[job.selected], &job);
                    result.result = alarm_config_valid(&result.config)
                                        ? alarm_store_save(&store, &result.config)
                                        : ALARM_STORE_INVALID;
                } else {
                    result.result = ALARM_STORE_INVALID;
                }
            }
            result.generation = store.generation;
#if defined(CLOCK_OTA_ENABLED) && CLOCK_OTA_ENABLED
            bl24c512_transaction_end();
#endif
            /* 同一时刻只有一个未完成任务，因此结果队列始终有可用位置。 */
            (void)xQueueSend(result_queue, &result, portMAX_DELAY);
        }
    }
}

/**
 * 功能：循环接收独立显示快照，校验屏幕归属代次后在显示锁内绘制。
 * 参数：argument，FreeRTOS 任务参数，当前未使用。
 * 返回：无，任务持续运行。
 */
static void view_worker(void *argument) {
    view_snapshot_t snapshot;
    uint32_t last_epoch = UINT32_MAX;
    (void)argument;
    for (;;) {
        bool visible;
        uint32_t epoch;
        alarm_page_model_t model;
        if (xQueueReceive(view_queue, &snapshot, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (xSemaphoreTake(display_mutex, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        taskENTER_CRITICAL();
        visible = owns_display;
        epoch = display_epoch;
        taskEXIT_CRITICAL();
        if (!visible || snapshot.display_epoch != epoch) {
            /* 常规页面恢复屏幕所有权后，排队的菜单快照不得再次绘制。 */
            alarm_page_invalidate();
            last_epoch = UINT32_MAX;
            (void)xSemaphoreGive(display_mutex);
            continue;
        }
        if (last_epoch != epoch) {
            alarm_page_invalidate();
        }
        last_epoch = epoch;
        memset(&model, 0, sizeof model);
        model.config = &snapshot.config;
        model.menu = &snapshot.menu;
        model.now = snapshot.now;
        model.revision = snapshot.revision;
        model.ringing_mask = snapshot.ringing_mask;
        model.snoozed_mask = snapshot.snoozed_mask;
        model.time_valid = snapshot.time_valid;
        model.saving = snapshot.saving;
        model.storage_ok = snapshot.storage_ok;
        model.test_pending = snapshot.test_pending;
        model.test_ringing = snapshot.test_ringing;
        model.message = snapshot.message;
        alarm_page_render(&model);
        (void)xSemaphoreGive(display_mutex);
    }
}

/**
 * 功能：收取保存结果，仅在持久化验证成功后应用配置并完成远程请求。
 * 参数：无。
 * 返回：无。
 */
static void collect_save_result(void) {
    save_result_t result;
    if (xQueueReceive(result_queue, &result, 0U) != pdTRUE) {
        return;
    }
    if (!owner.saving) {
        return;
    }
    owner.saving = false;
    if (result.result == ALARM_STORE_OK && alarm_engine_apply(&owner.engine, &result.config)) {
        owner.storage_ok = true;
        owner.revision = result.generation;
        ++owner.state_version;
        if (owner.state_version == 0U) {
            owner.state_version = 1U;
        }
        owner.message = "Saved";
        if (owner.save_request_id == 0U) {
            alarm_menu_saved(&owner.menu, true);
        }
        complete_remote(owner.save_request_id, ALARM_REMOTE_SAVED);
        clock_log("Alarm configuration saved, generation %lu\r\n", (unsigned long)owner.revision);
    } else {
        owner.storage_ok = false;
        owner.message = "Save failed";
        if (owner.save_request_id == 0U) {
            alarm_menu_saved(&owner.menu, false);
        }
        complete_remote(owner.save_request_id, ALARM_REMOTE_FAILED);
        clock_log("Alarm save failed (%u); running configuration retained\r\n",
                  (unsigned)result.result);
    }
    owner.save_request_id = 0U;
    owner.dirty = true;
}

/**
 * 功能：校验物理菜单草稿并提交一次异步保存。
 * 参数：无。
 * 返回：无。
 */
static void request_save(void) {
    save_job_t job;
    job.fields_mask = 0U;
    job.selected = owner.menu.selected;
    job.entry = owner.menu.draft;
    if (job.selected >= ALARM_COUNT) {
        owner.message = "Save failed";
        alarm_menu_saved(&owner.menu, false);
        clock_log_line("Alarm save failed: invalid selected alarm");
        return;
    }
    job.base = owner.engine.config.entries[job.selected];
    if (!alarm_entry_valid(&job.entry) || xQueueSend(save_queue, &job, 0U) != pdTRUE) {
        owner.message = "Save failed";
        alarm_menu_saved(&owner.menu, false);
        clock_log_line("Alarm save failed: invalid configuration or busy queue");
        return;
    }
    owner.saving = true;
    owner.save_request_id = 0U;
    owner.message = "Saving...";
}

/**
 * 功能：收取远程请求，重新检查版本和物理编辑状态后提交保存。
 * 参数：无。
 * 返回：无。
 */
static void collect_remote_request(void) {
    remote_job_t request;
    save_job_t job;
    if (xQueueReceive(remote_queue, &request, 0U) != pdTRUE) {
        return;
    }
    /* 先处理按键：提交后创建的物理草稿或保存优先；此处复查版本还可消除发布与入队之间的竞争。 */
    if (owner.saving || local_editor_active() || request.expected_version != owner.state_version) {
        complete_remote(request.request_id, ALARM_REMOTE_FAILED);
        return;
    }
    job.selected = request.selected;
    job.entry = request.entry;
    job.fields_mask = request.fields_mask;
    job.base = owner.engine.config.entries[job.selected];
    if (xQueueSend(save_queue, &job, 0U) != pdTRUE) {
        complete_remote(request.request_id, ALARM_REMOTE_FAILED);
        return;
    }
    owner.saving = true;
    owner.save_request_id = request.request_id;
    owner.message = "Saving...";
    owner.dirty = true;
}

/**
 * 功能：写入本地时钟草稿并回读 RTC，确认成功后更新菜单和运行时间。
 * 参数：无。
 * 返回：无。
 */
static void set_clock(void) {
    uint32_t requested, readback;
    rtc_date_time_t date, actual;
    if (clock_rtc_ready() && owner.menu.clock_draft.year >= 2020U &&
        alarm_calendar_to_seconds(&owner.menu.clock_draft, &requested) &&
        alarm_seconds_to_calendar(requested, &date)) {
        rtc_set_time(&date);
        rtc_get_time(&actual);
        if (trusted_time(&actual) && alarm_calendar_to_seconds(&actual, &readback) &&
            ((requested >= readback && requested - readback <= 2U) ||
             (readback > requested && readback - requested <= 2U))) {
            owner.now = actual;
            owner.time_valid = true;
            owner.menu.view = ALARM_VIEW_LIST;
            owner.message = "Time set";
            clock_log_line("Alarm manual RTC time set");
            return;
        }
    }
    owner.message = "RTC unavailable";
    clock_log_line("Alarm manual RTC set failed: RTC unavailable");
}

/**
 * 功能：按确认键优先的规则处理物理按键，并执行保存、时钟和提醒动作。
 * 参数：keys，按键事件位掩码；now_ms，当前单调毫秒时间。
 * 返回：无。
 */
static void handle_input(uint32_t keys, uint32_t now_ms) {
    alarm_input_t input;
    alarm_action_t action;
    bool active;
    uint8_t stopped;
    if (keys == 0U) {
        return;
    }
    /* 同时按键时确认键优先，使停止行为确定。 */
    if (keys & BOARD_KEY1_LONG) {
        input = ALARM_INPUT_CONFIRM_LONG;
    } else if (keys & BOARD_KEY1_SHORT) {
        input = ALARM_INPUT_CONFIRM;
    } else if (keys & BOARD_KEY2_LONG) {
        input = ALARM_INPUT_PLUS_LONG;
    } else if (keys & BOARD_KEY2_SHORT) {
        input = ALARM_INPUT_PLUS;
    } else if (keys & BOARD_KEY3_LONG) {
        input = ALARM_INPUT_MINUS_LONG;
    } else if (keys & BOARD_KEY3_SHORT) {
        input = ALARM_INPUT_MINUS;
    } else {
        return;
    }
    active = owner.engine.ringing_mask != 0U || owner.engine.snoozed_mask != 0U ||
             owner.test_pending || owner.test_ringing;
    action = alarm_menu_input(&owner.menu, input, &owner.engine.config, &owner.now, active,
                              owner.saving);
    owner.dirty = true;
    switch (action) {
    case ALARM_ACTION_SAVE:
        request_save();
        break;
    case ALARM_ACTION_SET_CLOCK:
        set_clock();
        break;
    case ALARM_ACTION_STOP:
        stopped = alarm_engine_stop(&owner.engine);
        owner.test_pending = false;
        owner.test_ringing = false;
        owner.message = "Stopped";
        clock_log("Alarm stopped mask %02x; diagnostic cancelled\r\n", (unsigned)stopped);
        break;
    case ALARM_ACTION_SNOOZE:
        stopped = alarm_engine_snooze(&owner.engine, now_ms);
        if (owner.test_pending || owner.test_ringing) {
            owner.test_pending = true;
            owner.test_ringing = false;
            owner.test_due_ms = now_ms + ALARM_DEFAULT_SNOOZE_SECONDS * 1000U;
        }
        owner.message = "Snoozed";
        clock_log("Alarm snoozed mask %02x\r\n", (unsigned)stopped);
        break;
    case ALARM_ACTION_TEST:
        owner.test_pending = true;
        owner.test_due_ms = now_ms + 10000U;
        owner.message = "Test in 10 seconds";
        clock_log_line("Alarm diagnostic scheduled in 10 seconds");
        break;
    default:
        break;
    }
}

/**
 * 功能：按周期读取 RTC 并推进闹钟引擎，同时登记响铃状态变化。
 * 参数：now_ms，当前单调毫秒时间。
 * 返回：无。
 */
static void poll_engine(uint32_t now_ms) {
    alarm_events_t events;
    bool was_valid;
    if (now_ms - owner.last_engine_ms < 100U) {
        return;
    }
    owner.last_engine_ms = now_ms;
    was_valid = owner.time_valid;
    rtc_get_time(&owner.now);
    owner.time_valid = trusted_time(&owner.now);
    if (was_valid != owner.time_valid) {
        owner.dirty = true;
        if (!owner.time_valid) {
            clock_log_line("Alarm scheduled triggers inhibited: RTC unavailable");
        }
    }
    events = alarm_engine_step(&owner.engine, &owner.now, owner.time_valid, now_ms);
    if (events.started || events.stopped) {
        owner.dirty = true;
        if (events.started) {
            clock_log("Alarm started mask %02x\r\n", (unsigned)events.started);
        }
        if (events.stopped) {
            clock_log("Alarm duration stopped mask %02x\r\n", (unsigned)events.stopped);
        }
    }
}

/**
 * 功能：推进临时测试提醒，到期响铃并在一分钟后自动停止。
 * 参数：now_ms，当前单调毫秒时间。
 * 返回：无。
 */
static void poll_diagnostic(uint32_t now_ms) {
    if (owner.test_pending && now_ms - owner.test_due_ms < 0x80000000U) {
        owner.test_pending = false;
        owner.test_ringing = true;
        owner.test_started_ms = now_ms;
        owner.dirty = true;
        owner.message = "Test ringing";
        clock_log_line("Alarm diagnostic started");
    }
    if (owner.test_ringing && now_ms - owner.test_started_ms >= 60000U) {
        owner.test_ringing = false;
        owner.dirty = true;
        owner.message = "Test finished";
        clock_log_line("Alarm diagnostic duration stopped");
    }
}

/**
 * 功能：依据实际响铃状态生成每半秒切换一次的蜂鸣器节奏。
 * 参数：now_ms，当前单调毫秒时间。
 * 返回：无。
 */
static void update_buzzer(uint32_t now_ms) {
    bool active = owner.engine.ringing_mask != 0U || owner.test_ringing;
    bool on;
    if (active && !owner.beep_active) {
        owner.beep_started_ms = now_ms;
    }
    owner.beep_active = active;
    on = active && (((now_ms - owner.beep_started_ms) / 500U) % 2U == 0U);
    if (on != owner.buzzer_on) {
        owner.buzzer_on = on;
        buzzer_set(on);
    }
}

/**
 * 功能：协调屏幕所有权，并在状态改变或刷新周期到达时投递独立显示快照。
 * 参数：now_ms，当前单调毫秒时间。
 * 返回：无。
 */
static void publish_view(uint32_t now_ms) {
    view_snapshot_t snapshot;
    bool desired = owner.menu.view != ALARM_VIEW_MAIN || owner.engine.ringing_mask != 0U ||
                   owner.engine.snoozed_mask != 0U || owner.test_pending || owner.test_ringing;
    bool released = false;
    taskENTER_CRITICAL();
    if (owns_display != desired) {
        released = !desired;
        owns_display = desired;
        ++display_epoch;
        owner.dirty = true;
    }
    taskEXIT_CRITICAL();
    if (released) {
        app_request_main_redraw();
    }
    if (!owner.dirty && (!desired || now_ms - owner.last_view_ms < 1000U)) {
        return;
    }
    memset(&snapshot, 0, sizeof snapshot);
    snapshot.config = owner.engine.config;
    snapshot.menu = owner.menu;
    snapshot.now = owner.now;
    snapshot.revision = owner.revision;
    snapshot.display_epoch = display_epoch;
    snapshot.ringing_mask = owner.engine.ringing_mask;
    snapshot.snoozed_mask = owner.engine.snoozed_mask;
    snapshot.time_valid = owner.time_valid;
    snapshot.saving = owner.saving;
    snapshot.storage_ok = owner.storage_ok;
    snapshot.test_pending = owner.test_pending;
    snapshot.test_ringing = owner.test_ringing;
    snapshot.message = owner.message;
    (void)xQueueOverwrite(view_queue, &snapshot);
    owner.last_view_ms = now_ms;
    owner.dirty = false;
}

/**
 * 功能：以十毫秒周期协调保存、时间、按键、远程请求、蜂鸣器及快照发布。
 * 参数：argument，FreeRTOS 任务参数，当前未使用。
 * 返回：无，任务持续运行。
 */
static void alarm_task(void *argument) {
    TickType_t wake = xTaskGetTickCount();
    (void)argument;
    for (;;) {
        uint32_t now_ms = (uint32_t)xTaskGetTickCount();
        collect_save_result();
        if (alarm_service_maintenance_state() != ALARM_MAINTENANCE_ACTIVE) {
            poll_engine(now_ms);
            poll_diagnostic(now_ms);
            handle_input(board_keys_poll(now_ms), now_ms);
            collect_remote_request();
            collect_maintenance();
        } else {
            /* 丢弃维护期间的按键事件，防止失败恢复后执行积压的编辑动作。 */
            (void)board_keys_poll(now_ms);
        }
        update_buzzer(now_ms);
        publish_view(now_ms);
        publish_service_snapshot();
#if defined(CLOCK_OTA_ENABLED) && CLOCK_OTA_ENABLED
        ota_service_heartbeat(OTA_HEARTBEAT_ALARM);
#endif
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(10U));
    }
}

/**
 * 功能：释放初始化过程中创建的队列和显示互斥锁，并清空句柄。
 * 参数：无。
 * 返回：无。
 */
static void cleanup_resources(void) {
    if (remote_queue != NULL) {
        vQueueDelete(remote_queue);
    }
    if (view_queue != NULL) {
        vQueueDelete(view_queue);
    }
    if (result_queue != NULL) {
        vQueueDelete(result_queue);
    }
    if (save_queue != NULL) {
        vQueueDelete(save_queue);
    }
    if (display_mutex != NULL) {
        vSemaphoreDelete(display_mutex);
    }
    view_queue = NULL;
    result_queue = NULL;
    save_queue = NULL;
    display_mutex = NULL;
    remote_queue = NULL;
}

/**
 * 功能：在 RTC、界面和 I2C1 就绪后加载配置、创建队列及服务任务。
 * 参数：无。
 * 返回：无；资源创建失败时清理并保持服务不可用。
 */
void alarm_service_init(void) {
    alarm_config_t config;
    alarm_store_result_t loaded;
    alarm_store_io_t io = {read_eeprom, write_eeprom, NULL};
    TaskHandle_t store_task = NULL, view_task = NULL;
    uint32_t now_ms;
    if (initialized) {
        return;
    }
    memset(&owner, 0, sizeof owner);
    memset(&published, 0, sizeof published);
    owns_display = false;
    display_epoch = 0U;
    buzzer_init();
    board_keys_init();
    alarm_config_defaults(&config);
    alarm_store_init(&store, io);
#if defined(CLOCK_OTA_ENABLED) && CLOCK_OTA_ENABLED
    if (bl24c512_transaction_begin()) {
        loaded = alarm_store_load(&store, &config);
        bl24c512_transaction_end();
    } else {
        loaded = ALARM_STORE_IO_ERROR;
    }
#else
    loaded = alarm_store_load(&store, &config);
#endif
    owner.storage_ok = loaded == ALARM_STORE_OK;
    if (!owner.storage_ok) {
        alarm_config_defaults(&config);
    }
    if (loaded == ALARM_STORE_OK) {
        owner.message = "Loaded";
    } else if (loaded == ALARM_STORE_EMPTY) {
        owner.message = "No saved alarms";
    } else if (loaded == ALARM_STORE_CORRUPT) {
        owner.message = "Storage invalid";
    } else {
        owner.message = "EEPROM unavailable";
    }
    owner.revision = store.generation;
    owner.state_version = owner.revision + 1U;
    if (owner.state_version == 0U) {
        owner.state_version = 1U;
    }
    alarm_engine_init(&owner.engine, &config);
    alarm_menu_init(&owner.menu);
    rtc_get_time(&owner.now);
    owner.time_valid = trusted_time(&owner.now);
    if (!owner.time_valid) {
        clock_log_line("Alarm scheduled triggers inhibited: RTC unavailable");
    }
    clock_log("Alarm storage boot result %u, generation %lu\r\n", (unsigned)loaded,
              (unsigned long)owner.revision);
    display_mutex = xSemaphoreCreateMutex();
    save_queue = xQueueCreate(1U, (UBaseType_t)sizeof(save_job_t));
    result_queue = xQueueCreate(1U, (UBaseType_t)sizeof(save_result_t));
    view_queue = xQueueCreate(1U, (UBaseType_t)sizeof(view_snapshot_t));
    remote_queue = xQueueCreate(1U, (UBaseType_t)sizeof(remote_job_t));
    if (display_mutex == NULL || save_queue == NULL || result_queue == NULL || view_queue == NULL ||
        remote_queue == NULL) {
        clock_log_line("Alarm service unavailable: queue/mutex allocation failed");
        cleanup_resources();
        return;
    }
    now_ms = (uint32_t)xTaskGetTickCount();
    owner.last_engine_ms = now_ms - 100U;
    owner.last_view_ms = now_ms;
    owner.dirty = true;
    publish_service_snapshot();
    if (xTaskCreate(save_worker, "alarm-store", 1024U, NULL, 2U, &store_task) != pdPASS) {
        goto failed;
    }
    if (xTaskCreate(view_worker, "alarm-view", 1024U, NULL, 4U, &view_task) != pdPASS) {
        goto failed;
    }
    if (xTaskCreate(alarm_task, "alarm", 1536U, NULL, 9U, NULL) != pdPASS) {
        goto failed;
    }
    taskENTER_CRITICAL();
    initialized = true;
    taskEXIT_CRITICAL();
    return;
failed:
    if (view_task != NULL) {
        vTaskDelete(view_task);
    }
    if (store_task != NULL) {
        vTaskDelete(store_task);
    }
    clock_log_line("Alarm service unavailable: task allocation failed");
    cleanup_resources();
}
