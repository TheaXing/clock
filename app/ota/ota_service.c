#include "ota_service.h"
#include "ota_config.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#if CLOCK_OTA_ENABLED
#include "ota_crypto.h"
#include "ota_image.h"
#include "ota_journal.h"
#include "ota_discovery.h"
#include "ota_reset.h"
#if CLOCK_OTA_AUTO_CHECK
#include "esp_at.h"
#endif
#include "alarm_service.h"
#include "bl24c512.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "main.h"

#define OTA_HEALTH_MS 10000U
#define OTA_RESET_GRACE_MS 2000U
#define OTA_ADMISSION_MS 1000U

typedef enum {
    OTA_JOB_REQUEST,
    OTA_JOB_CONFIRM,
    OTA_JOB_AUTO
} ota_job_kind_t;

/** 保存完整工作副本，排队后不引用网络缓冲区。 */
typedef struct {
    ota_job_kind_t kind; // 人工请求、健康确认或自动请求
    uint8_t manifest[256]; // 自动候选原始包头，其他工作保持全零
} ota_job_t;

typedef enum {
    OTA_AUTO_WAITING,
    OTA_AUTO_HEALTH,
    OTA_AUTO_CHECKING,
    OTA_AUTO_CURRENT,
    OTA_AUTO_BUSY,
    OTA_AUTO_QUEUED,
    OTA_AUTO_UNREACHABLE,
    OTA_AUTO_FAILED,
    OTA_AUTO_BLOCKED
} ota_auto_state_t;

typedef enum {
    OTA_REQUEST_IDLE,
    OTA_REQUEST_QUEUED,
    OTA_REQUEST_SAVED,
    OTA_REQUEST_FAILED
} ota_request_state_t;

/** 在短临界区内共享请求状态，存储任务不执行任何网络操作。 */
static struct {
    ota_request_state_t state; // 本次启动的网页请求阶段
    uint32_t id; // 本次启动内的请求编号
    uint32_t saved_ms; // REQUESTED 验证保存的毫秒时刻
    uint32_t healthy_ms; // 本地服务就绪的毫秒时刻
    uint32_t heartbeat_ms[3]; // 时钟、绘图和闹钟最近实际进展的毫秒时刻
    uint32_t error; // 不包含凭据的固定错误编号
    const char *health; // 最近健康确认阶段或失败原因，仅引用固定公开字符串
    const char *journal; // 最近健康确认的日志访问结果，尚未访问时为 unread
    bool available; // 工作任务与事务锁是否可用
    bool started; // 本地服务是否已经启动
    bool confirmation_queued; // 是否已经提交过健康检查工作
    bool reset_sent; // 是否已执行复位动作
    bool health_tracking; // 三个本地服务是否处于连续健康窗口
    uint8_t heartbeat_seen; // 本次健康窗口已经观测到的来源位
    ota_auto_state_t auto_state; // 与人工请求独立的开机检查阶段
    uint8_t auto_checks; // 本次启动已经执行的有界 HTTP 次数
    uint32_t auto_check_ms; // 上次 HTTP 完成的时刻
    bool auto_ready; // 已确认当前日志允许自动请求
    bool auto_finished; // 本次启动不再发现或提交自动请求
    bool auto_candidate; // 是否已经保留候选用于延后维护准入
    bool auto_job; // 当前持久化请求是否来自自动检查
    bool retry_previous; // 新正常启动是否可替换已明确预检失败的自动请求
    bool normal_reset; // 初始化快照是否明确来自电源或外部复位
} service;
static QueueHandle_t jobs;
static uint8_t admin_token_digest[32];
static bool admin_enabled;
static ota_manifest_t current_manifest;
static bool current_valid;
#if CLOCK_OTA_AUTO_CHECK
static ota_manifest_t auto_candidate;
#endif
static uint8_t retry_manifest[256];

/** 功能：在临界区内判断全部本地进展是否新鲜；参数：now_ms，当前毫秒；返回：全部满足时返回真。 */
static bool heartbeats_fresh(uint32_t now_ms) {
    return service.heartbeat_seen == OTA_HEARTBEAT_ALL &&
           now_ms - service.heartbeat_ms[0] <= 2500U &&
           now_ms - service.heartbeat_ms[1] <= 2500U &&
           now_ms - service.heartbeat_ms[2] <= 1000U;
}

/** 功能：在临界区内取时并检查连续十秒健康，缺失或过期时重启窗口；参数：无；返回：可确认时为真。 */
static bool local_health_ready(void) {
    bool ready;
    uint32_t now_ms;
    taskENTER_CRITICAL();
    now_ms = (uint32_t)xTaskGetTickCount();
    if (!service.started || !heartbeats_fresh(now_ms)) {
        service.health_tracking = false;
        service.healthy_ms = now_ms;
    } else if (!service.health_tracking) {
        service.health_tracking = true;
        service.healthy_ms = now_ms;
    }
    ready = service.started && service.health_tracking &&
            now_ms - service.healthy_ms >= OTA_HEALTH_MS;
    taskEXIT_CRITICAL();
    return ready;
}

/** 功能：由真实本地生产者报告进展并独立于网络等待检测失活；参数：source，单个来源位；返回：无。 */
void ota_service_heartbeat(uint8_t source) {
    unsigned index;
    uint32_t now_ms;
    if (source == OTA_HEARTBEAT_CLOCK) {
        index = 0U;
    } else if (source == OTA_HEARTBEAT_UI) {
        index = 1U;
    } else if (source == OTA_HEARTBEAT_ALARM) {
        index = 2U;
    } else {
        return;
    }
    taskENTER_CRITICAL();
    now_ms = (uint32_t)xTaskGetTickCount();
    if (service.available && service.started) {
        bool before = heartbeats_fresh(now_ms);
        service.heartbeat_ms[index] = now_ms;
        service.heartbeat_seen |= source;
        /* 更新前检查也覆盖本任务失活后先于其他任务恢复的情况。 */
        if (!before || !service.health_tracking) {
            service.healthy_ms = now_ms;
            service.health_tracking = heartbeats_fresh(now_ms);
        }
    }
    taskEXIT_CRITICAL();
}

/** 功能：将一个十六进制字符转换为数值；参数：value，输入字符；返回：数值或负一。 */
static int hex_digit(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

/** 功能：严格解析非零的三十二字节十六进制凭据；参数：text，字符串；output，输出；返回：是否有效。 */
static bool parse_key(const char *text, uint8_t output[32]) {
    uint8_t any = 0U;
    if (text == NULL || strlen(text) != 64U) {
        return false;
    }
    for (size_t i = 0U; i < 32U; ++i) {
        int high = hex_digit(text[2U * i]);
        int low = hex_digit(text[2U * i + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        output[i] = (uint8_t)((unsigned)high * 16U + (unsigned)low);
        any |= output[i];
    }
    return any != 0U;
}

/** 功能：解析严格的双字段表单并校验会话与独立管理令牌；参数：body，正文；session，会话；返回：是否通过。 */
static bool authorized(const char *body, uint32_t session) {
    bool have_session = false, have_token = false;
    uint32_t requested_session = 0U;
    uint8_t token[32] = {0};
    uint8_t digest[32];
    const char *position = body;
    if (body == NULL || session == 0U || strlen(body) > 383U) {
        return false;
    }
    while (*position != '\0') {
        if (strncmp(position, "session=", 8U) == 0 && !have_session) {
            position += 8;
            if (*position < '0' || *position > '9') {
                return false;
            }
            while (*position >= '0' && *position <= '9') {
                uint32_t digit = (uint32_t)(*position - '0');
                if (requested_session > (UINT32_MAX - digit) / 10U) {
                    return false;
                }
                requested_session = requested_session * 10U + digit;
                ++position;
            }
            have_session = true;
        } else if (strncmp(position, "token=", 6U) == 0 && !have_token) {
            char text[65];
            position += 6;
            if (strlen(position) < 64U) {
                return false;
            }
            memcpy(text, position, 64U);
            text[64] = '\0';
            if (!parse_key(text, token)) {
                return false;
            }
            position += 64;
            have_token = true;
        } else {
            return false;
        }
        if (*position == '\0') {
            break;
        }
        if (*position != '&' || position[1] == '\0') {
            return false;
        }
        ++position;
    }
    if (!have_session || !have_token || requested_session != session) {
        return false;
    }
    ota_sha256(token, sizeof token, digest);
    return ota_constant_equal(digest, admin_token_digest, sizeof digest);
}

/** 功能：转接事务锁内的日志读取；参数：同公共日志回调；返回：读取结果。 */
static bool read_eeprom(void *context, uint32_t address, void *data, size_t length) {
    (void)context;
    return bl24c512_read(address, data, length);
}

/** 功能：转接事务锁内的日志写入；参数：同公共日志回调；返回：写入结果。 */
static bool write_eeprom(void *context, uint32_t address, const void *data, size_t length) {
    (void)context;
    return bl24c512_write(address, data, length);
}

/** 功能：登记请求失败并恢复闹钟服务；参数：error，固定错误编号；返回：无。 */
static void fail_request(uint32_t error) {
    alarm_service_maintenance_release();
    taskENTER_CRITICAL();
    service.error = error;
    service.state = OTA_REQUEST_FAILED;
    if (service.auto_job) {
        service.auto_state = OTA_AUTO_FAILED;
        service.auto_finished = true;
    }
    taskEXIT_CRITICAL();
}

/** 功能：比较日志中的已安装身份与当前镜像；参数：record，日志；返回：完整安装身份一致为真。 */
static bool record_matches_current(const ota_record_t *record) {
    ota_manifest_t parsed;
    bool installed = record->manifest[252] == (uint8_t)(OTA_FOOTER_COMMIT & 0xffU);
    return current_valid && ota_manifest_read_trusted(record->manifest, installed, &parsed) &&
           ota_constant_equal(parsed.raw, current_manifest.raw, 252U) &&
           ota_constant_equal(parsed.id, current_manifest.id, sizeof parsed.id);
}

/** 功能：判断新正常启动能否重试明确预检失败的旧自动请求；参数：record，日志；返回：满足例外为真。 */
static bool previous_auto_failed(const ota_record_t *record) {
    ota_manifest_t previous;
    return service.normal_reset && current_valid && record->state == OTA_STATE_REQUESTED &&
           record->error == 1U && ota_manifest_read_hint(record->manifest, &previous) &&
           previous.version > current_manifest.version;
}

/** 功能：持锁重新校验自动请求的当前身份和失败重试例外；参数：record，日志；返回：允许提交为真。 */
static bool auto_record_allowed(const ota_record_t *record) {
    return (record->state == OTA_STATE_CONFIRMED && record_matches_current(record)) ||
           (service.retry_previous && previous_auto_failed(record) &&
            ota_constant_equal(record->manifest, retry_manifest, 256U));
}

/** 功能：等待维护准入并在完整事务中保存人工或自动请求；参数：job，独立候选副本；返回：无。 */
static void persist_request(const ota_job_t *job) {
    ota_record_t record;
    ota_store_result_t result;
    ota_journal_io_t io = {read_eeprom, write_eeprom, NULL};
    uint32_t started = (uint32_t)xTaskGetTickCount();
    alarm_maintenance_state_t state = alarm_service_maintenance_state();
    while (state == ALARM_MAINTENANCE_PENDING &&
           (uint32_t)xTaskGetTickCount() - started < OTA_ADMISSION_MS) {
        vTaskDelay(pdMS_TO_TICKS(10U));
        state = alarm_service_maintenance_state();
    }
    if (state != ALARM_MAINTENANCE_ACTIVE) {
        if (job->kind == OTA_JOB_AUTO) {
            alarm_service_maintenance_release();
            taskENTER_CRITICAL();
            service.state = OTA_REQUEST_IDLE;
            service.auto_state = OTA_AUTO_BUSY;
            service.auto_job = false;
            taskEXIT_CRITICAL();
            return;
        }
        fail_request(1U);
        return;
    }
    if (!bl24c512_transaction_begin()) {
        fail_request(2U);
        return;
    }
    result = ota_journal_load(&io, &record);
    if (result == OTA_STORE_OK && job->kind == OTA_JOB_AUTO) {
        ota_manifest_t candidate;
        if (!local_health_ready() ||
            alarm_service_maintenance_state() != ALARM_MAINTENANCE_ACTIVE ||
            !auto_record_allowed(&record) || !ota_manifest_read_hint(job->manifest, &candidate) ||
            candidate.version <= current_manifest.version) {
            result = OTA_STORE_INVALID;
        }
    }
    if (result == OTA_STORE_OK && record.state != OTA_STATE_INSTALLING) {
        record.state = OTA_STATE_REQUESTED;
        record.attempts = 0U;
        record.error = 0U;
        if (job->kind == OTA_JOB_AUTO) {
            memcpy(record.manifest, job->manifest, sizeof record.manifest);
        } else {
            memset(record.manifest, 0, sizeof record.manifest);
        }
        result = ota_journal_save(&io, &record);
    } else if (result == OTA_STORE_OK) {
        result = OTA_STORE_INVALID;
    }
    bl24c512_transaction_end();
    if (result != OTA_STORE_OK) {
        fail_request(3U);
        return;
    }
    taskENTER_CRITICAL();
    service.saved_ms = (uint32_t)xTaskGetTickCount();
    service.state = OTA_REQUEST_SAVED;
    service.error = 0U;
    if (job->kind == OTA_JOB_AUTO) {
        service.auto_state = OTA_AUTO_QUEUED;
        service.auto_finished = true;
    }
    taskEXIT_CRITICAL();
}

/** 功能：仅确认与当前认证安装描述完全匹配的 TRIAL，不覆盖任何升级请求；参数：无；返回：检查完成返回真，暂时性故障需稍后重试返回假。 */
static bool confirm_running_image(void) {
    static const char *const journal_names[] = {"ok", "empty", "corrupt", "io", "invalid"};
    ota_record_t record;
    ota_manifest_t trial;
    ota_journal_io_t io = {read_eeprom, write_eeprom, NULL};
    alarm_service_snapshot_t snapshot;
    bool valid;
    ota_store_result_t result;
    if (!current_valid) {
        taskENTER_CRITICAL();
        service.health = "identity";
        service.auto_state = OTA_AUTO_BLOCKED;
        service.auto_finished = true;
        taskEXIT_CRITICAL();
        return true;
    }
    if (!local_health_ready()) {
        taskENTER_CRITICAL();
        service.health = "stale";
        taskEXIT_CRITICAL();
        return false;
    }
    if (!alarm_service_snapshot(&snapshot)) {
        taskENTER_CRITICAL();
        service.health = "unavailable";
        taskEXIT_CRITICAL();
        return false;
    }
    if (!bl24c512_transaction_begin()) {
        taskENTER_CRITICAL();
        service.health = "lock";
        taskEXIT_CRITICAL();
        return false;
    }
    result = ota_journal_load(&io, &record);
    taskENTER_CRITICAL();
    service.journal = journal_names[(unsigned)result <= OTA_STORE_INVALID ? (unsigned)result : OTA_STORE_INVALID];
    service.health = result == OTA_STORE_IO ? "journal-read" : "blocked";
    taskEXIT_CRITICAL();
    valid = result == OTA_STORE_OK && record.state == OTA_STATE_TRIAL;
    if (valid) {
        /* 日志允许保存原始包头或已提交的安装描述；两者只有末尾提交字不同。 */
        bool installed = record.manifest[252] == (uint8_t)(OTA_FOOTER_COMMIT & 0xffU);
        valid = ota_manifest_read_trusted(record.manifest, installed, &trial) &&
                ota_constant_equal(trial.id, current_manifest.id, sizeof trial.id) &&
                ota_constant_equal(trial.raw, current_manifest.raw, 252U);
    }
    if (valid) {
        if (!local_health_ready()) {
            taskENTER_CRITICAL();
            service.health = "stale";
            taskEXIT_CRITICAL();
            bl24c512_transaction_end();
            return false;
        }
        record.state = OTA_STATE_CONFIRMED;
        record.error = 0U;
        result = ota_journal_save(&io, &record);
        taskENTER_CRITICAL();
        service.journal = journal_names[(unsigned)result <= OTA_STORE_INVALID ? (unsigned)result : OTA_STORE_INVALID];
        service.health = result == OTA_STORE_IO ? "journal-save" : "blocked";
        taskEXIT_CRITICAL();
    }
    if (result != OTA_STORE_IO) {
        bool confirmed = result == OTA_STORE_OK && record.state == OTA_STATE_CONFIRMED &&
                         record_matches_current(&record);
        bool retry = result == OTA_STORE_OK && previous_auto_failed(&record);
        taskENTER_CRITICAL();
        service.auto_ready = confirmed || retry;
        service.retry_previous = retry;
        service.health = confirmed ? "confirmed" : (retry ? "retry-pending" : "blocked");
        if (service.error == 5U) {
            service.error = 0U;
        }
        if (retry) {
            memcpy(retry_manifest, record.manifest, sizeof retry_manifest);
        }
        if (!service.auto_ready) {
            service.auto_state = OTA_AUTO_BLOCKED;
            service.auto_finished = true;
        }
        taskEXIT_CRITICAL();
    }
    bl24c512_transaction_end();
    return result != OTA_STORE_IO;
}

/** 功能：串行处理请求保存与健康确认工作，不访问 ESP 或复位硬件；参数：argument，未使用；返回：无。 */
static void storage_worker(void *argument) {
    ota_job_t job;
    (void)argument;
    for (;;) {
        if (xQueueReceive(jobs, &job, portMAX_DELAY) == pdTRUE) {
            if (job.kind == OTA_JOB_REQUEST || job.kind == OTA_JOB_AUTO) {
                persist_request(&job);
            } else if (!confirm_running_image()) {
                taskENTER_CRITICAL();
                service.confirmation_queued = false;
                service.healthy_ms = (uint32_t)xTaskGetTickCount();
                service.error = 5U;
                taskEXIT_CRITICAL();
            }
        }
    }
}

/** 功能：在调度器启动前创建 EEPROM 事务锁、队列和存储工作任务；参数：无；返回：无。 */
void ota_service_init(void) {
    if (service.available) {
        return;
    }
    memset(&service, 0, sizeof service);
    service.health = "waiting";
    service.journal = "unread";
    service.normal_reset = ota_reset_is_normal_boot();
    if (!bl24c512_transaction_init()) {
        return;
    }
    admin_enabled = parse_key(CLOCK_OTA_ADMIN_TOKEN_SHA256_HEX, admin_token_digest);
    /* 只有 Boot 完成 HMAC、全镜像和向量认证后才运行 App；此处不携带发布密钥。
     * 身份仅用于将健康确认绑定到 Boot 已认证的当前安装描述。 */
    current_valid = ota_manifest_read_trusted((const uint8_t *)(uintptr_t)OTA_FOOTER_BASE,
                                             true, &current_manifest);
    jobs = xQueueCreate(1U, (UBaseType_t)sizeof(ota_job_t));
    if (jobs == NULL) {
        return;
    }
    if (xTaskCreate(storage_worker, "ota-store", 1536U, NULL, 2U, NULL) != pdPASS) {
        vQueueDelete(jobs);
        jobs = NULL;
        return;
    }
    service.available = true;
}

/** 功能：本地界面、闹钟与网络任务创建成功后启动十秒健康窗口；参数：无；返回：无。 */
void ota_service_start(void) {
    alarm_service_snapshot_t snapshot;
    if (service.available && !service.started && alarm_service_snapshot(&snapshot)) {
        taskENTER_CRITICAL();
        service.healthy_ms = (uint32_t)xTaskGetTickCount();
        service.heartbeat_seen = 0U;
        service.health_tracking = false;
        service.health = current_valid ? "tracking" : "identity";
        service.started = true;
        taskEXIT_CRITICAL();
    }
}

/** 功能：验证表单并异步排队升级请求，相同请求在本次启动内去重；参数：body，正文；session，会话；返回：HTTP 状态。 */
int ota_service_request(const char *body, uint32_t session) {
    ota_job_t job = {OTA_JOB_REQUEST, {0}};
    if (!service.available || !service.started || !admin_enabled) {
        return 503;
    }
    if (!authorized(body, session)) {
        return 403;
    }
    taskENTER_CRITICAL();
    if (service.state == OTA_REQUEST_QUEUED || service.state == OTA_REQUEST_SAVED) {
        taskEXIT_CRITICAL();
        return 202;
    }
    taskEXIT_CRITICAL();
    if (!alarm_service_maintenance_request()) {
        return 409;
    }
    taskENTER_CRITICAL();
    service.state = OTA_REQUEST_QUEUED;
    service.error = 0U;
    service.auto_job = false;
    ++service.id;
    if (service.id == 0U) {
        service.id = 1U;
    }
    if (xQueueSend(jobs, &job, 0U) != pdTRUE) {
        service.state = OTA_REQUEST_FAILED;
        service.error = 4U;
        taskEXIT_CRITICAL();
        alarm_service_maintenance_release();
        return 409;
    }
    taskEXIT_CRITICAL();
    return 202;
}

/** 功能：在唯一网络任务安全边界执行健康排队及保存两秒后的复位；参数：now_ms，当前毫秒；返回：无。 */
void ota_service_network_poll(uint32_t now_ms) {
    ota_job_t job = {OTA_JOB_CONFIRM, {0}};
    bool reset = false;
    alarm_service_snapshot_t snapshot;
    taskENTER_CRITICAL();
    if (service.state == OTA_REQUEST_SAVED && !service.reset_sent &&
        now_ms - service.saved_ms >= OTA_RESET_GRACE_MS) {
        service.reset_sent = true;
        reset = true;
    }
    if (service.available && service.started && !service.confirmation_queued &&
        service.state != OTA_REQUEST_QUEUED && service.state != OTA_REQUEST_SAVED &&
        local_health_ready() && alarm_service_snapshot(&snapshot)) {
        if (xQueueSend(jobs, &job, 0U) == pdTRUE) {
            service.confirmation_queued = true;
        }
    }
    taskEXIT_CRITICAL();
    if (reset) {
        NVIC_SystemReset();
    }
}

/** 功能：由唯一网络所有者执行有界发现，并将完整候选交给存储工作者；参数：connected，联网状态；now_ms，当前毫秒；返回：无。 */
void ota_service_autocheck_poll(bool connected, uint32_t now_ms) {
#if CLOCK_OTA_AUTO_CHECK
    ota_job_t job = {OTA_JOB_AUTO, {0}};
    alarm_service_snapshot_t snapshot;
    bool blocked;
    taskENTER_CRITICAL();
    blocked = !service.available || !service.started || service.auto_finished ||
              service.state == OTA_REQUEST_QUEUED || service.state == OTA_REQUEST_SAVED;
    taskEXIT_CRITICAL();
    if (blocked) {
        return;
    }
    if (!connected) {
        service.auto_state = OTA_AUTO_WAITING;
        return;
    }
    if (!local_health_ready() || !service.auto_ready || !current_valid) {
        service.auto_state = OTA_AUTO_HEALTH;
        return;
    }
    if (!service.auto_candidate) {
        ota_hint_t hint;
        char url[384];
        const char *json;
        int length;
        uint32_t delay = service.auto_checks == 1U ? 5000U : 15000U;
        if (service.auto_checks != 0U && now_ms - service.auto_check_ms < delay) {
            return;
        }
        if (strlen(CLOCK_OTA_PATH) > 156U) {
            service.auto_state = OTA_AUTO_FAILED;
            service.auto_finished = true;
            return;
        }
        length = snprintf(url, sizeof url, "http://%s:%u%s?latest=1", CLOCK_OTA_HOST,
                          (unsigned)CLOCK_OTA_PORT, CLOCK_OTA_PATH);
        if (length < 0 || (size_t)length >= sizeof url || (size_t)length > ESP_HTTP_BOUNDED_URL_MAX_1000_MS) {
            service.auto_state = OTA_AUTO_FAILED;
            service.auto_finished = true;
            return;
        }
        service.auto_state = OTA_AUTO_CHECKING;
        ++service.auto_checks;
        json = esp_at_http_get_bounded(url, 1000U);
        service.auto_check_ms = (uint32_t)xTaskGetTickCount();
        if (!ota_discovery_parse(json, &hint)) {
            service.auto_state = OTA_AUTO_UNREACHABLE;
            service.auto_finished = service.auto_checks >= 3U;
            return;
        }
        if (!hint.available || hint.manifest.version <= current_manifest.version) {
            service.auto_state = OTA_AUTO_CURRENT;
            service.auto_finished = true;
            return;
        }
        auto_candidate = hint.manifest;
        service.auto_candidate = true;
    }
    if (!alarm_service_snapshot(&snapshot) || snapshot.local_editing || snapshot.saving ||
        snapshot.ringing_mask != 0U || snapshot.snoozed_mask != 0U || snapshot.test_ringing ||
        snapshot.test_pending || !alarm_service_maintenance_request()) {
        service.auto_state = OTA_AUTO_BUSY;
        return;
    }
    memcpy(job.manifest, auto_candidate.raw, sizeof job.manifest);
    taskENTER_CRITICAL();
    service.state = OTA_REQUEST_QUEUED;
    service.error = 0U;
    service.auto_state = OTA_AUTO_QUEUED;
    service.auto_job = true;
    ++service.id;
    if (service.id == 0U) {
        service.id = 1U;
    }
    if (xQueueSend(jobs, &job, 0U) != pdTRUE) {
        service.state = OTA_REQUEST_IDLE;
        service.auto_state = OTA_AUTO_BUSY;
        service.auto_job = false;
        taskEXIT_CRITICAL();
        alarm_service_maintenance_release();
        return;
    }
    taskEXIT_CRITICAL();
#else
    (void)connected;
    (void)now_ms;
#endif
}

/** 功能：输出不包含凭据的版本、身份与异步请求状态；参数：output，输出；capacity，容量；session，会话；返回：无。 */
void ota_service_status(char *output, size_t capacity, uint32_t session) {
    static const char *const names[] = {"idle", "queued", "saved", "failed"};
    static const char *const auto_names[] = {"waiting", "health", "checking", "current", "busy",
                                           "queued", "unreachable", "failed", "blocked"};
    char identity[33] = {0};
    ota_request_state_t state;
    uint32_t id, error;
    const char *health, *journal;
    ota_auto_state_t auto_state;
    uint8_t auto_checks;
    if (output == NULL || capacity == 0U) {
        return;
    }
    if (current_valid) {
        for (size_t i = 0U; i < 16U; ++i) {
            (void)snprintf(identity + i * 2U, 3U, "%02x", (unsigned)current_manifest.id[i]);
        }
    }
    taskENTER_CRITICAL();
    state = service.state;
    id = service.id;
    error = service.error;
    health = service.health != NULL ? service.health : "waiting";
    journal = service.journal != NULL ? service.journal : "unread";
    auto_state = service.auto_state;
    auto_checks = service.auto_checks;
    taskEXIT_CRITICAL();
    (void)snprintf(output, capacity,
                   "{\"enabled\":%s,\"session\":%lu,\"version\":%lu,\"identity\":\"%s\","
                   "\"state\":\"%s\",\"error\":%lu,\"saving\":%s,\"id\":%lu,"
                   "\"autoEnabled\":%s,\"autoState\":\"%s\",\"autoChecks\":%u,"
                   "\"health\":\"%s\",\"journal\":\"%s\"}",
                   service.available && service.started && admin_enabled ? "true" : "false",
                   (unsigned long)session,
                   (unsigned long)(current_valid ? current_manifest.version : CLOCK_OTA_VERSION),
                   identity,
                   names[state], (unsigned long)error,
                   state == OTA_REQUEST_QUEUED ? "true" : "false", (unsigned long)id,
                   CLOCK_OTA_AUTO_CHECK ? "true" : "false", CLOCK_OTA_AUTO_CHECK ? auto_names[auto_state] : "disabled",
                   (unsigned)auto_checks, health, journal);
}

#else

/** 功能：独立应用构建无需创建 OTA 资源；参数：无；返回：无。 */
void ota_service_init(void) {
}

/** 功能：独立应用构建无需启动 OTA；参数：无；返回：无。 */
void ota_service_start(void) {
}

/** 功能：独立应用构建无需记录 OTA 健康进展；参数：source，未使用；返回：无。 */
void ota_service_heartbeat(uint8_t source) {
    (void)source;
}

/** 功能：独立应用构建无需轮询 OTA；参数：now_ms，未使用；返回：无。 */
void ota_service_network_poll(uint32_t now_ms) {
    (void)now_ms;
}

/** 功能：独立构建无需自动发现；参数：connected、now_ms，未使用；返回：无。 */
void ota_service_autocheck_poll(bool connected, uint32_t now_ms) {
    (void)connected;
    (void)now_ms;
}

/** 功能：报告独立应用未启用 OTA；参数：output，输出；capacity，容量；session，会话；返回：无。 */
void ota_service_status(char *output, size_t capacity, uint32_t session) {
    if (output != NULL && capacity != 0U) {
        (void)snprintf(output, capacity,
                       "{\"enabled\":false,\"session\":%lu,\"version\":0,\"identity\":\"\","
                       "\"state\":\"disabled\",\"error\":0,\"saving\":false,\"id\":0,"
                       "\"autoEnabled\":false,\"autoState\":\"disabled\",\"autoChecks\":0,"
                       "\"health\":\"disabled\",\"journal\":\"unread\"}",
                       (unsigned long)session);
    }
}

/** 功能：拒绝独立应用的升级请求；参数：body、session，未使用；返回：HTTP 503。 */
int ota_service_request(const char *body, uint32_t session) {
    (void)body;
    (void)session;
    return 503;
}
#endif
