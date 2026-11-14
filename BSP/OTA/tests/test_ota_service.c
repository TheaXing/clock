#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ota_service.h"
#include "ota_journal.h"
#include "ota_image.h"
#include "alarm_service.h"
#include "task.h"
#include "queue.h"
#include "ota_discovery.h"
#include "ota_config.h"

/** 保存虚拟任务的入口与参数。 */
typedef struct {
    TaskFunction_t function; // 工作任务入口
    void *argument; // 工作任务参数
} test_task_t;
static test_task_t worker;
static jmp_buf stopped;
static uint8_t queued[512];
static size_t queued_size;
static bool queue_full, lock_held, fail_store, reject_gate, unavailable, wrong_identity;
static bool lost_ack, fail_enqueue, fail_lock, fail_load;
static unsigned saves, resets, released;
static uint32_t tick;
static uint32_t load_delay;
static alarm_maintenance_state_t gate;
static ota_record_t persisted;
static bool software_reset, editing;
static uint8_t snapshot_busy;
static unsigned http_checks;
static const char *http_response;

/** 功能：模拟初始化时快照并清除复位原因；参数：无；返回：是否软件复位。 */
bool ota_reset_was_software(void) {
    return software_reset;
}

/** 功能：模拟明确正常启动的适配结果；参数：无；返回：是否正常复位。 */
bool ota_reset_is_normal_boot(void) {
    return !software_reset;
}

/** 功能：模拟唯一网络所有者的有界 HTTP；参数：URL 和超时；返回：公开正文或空指针。 */
const char *esp_at_http_get_bounded(const char *url, uint32_t timeout_ms) {
    assert(strcmp(url, "http://" CLOCK_OTA_HOST ":8788" CLOCK_OTA_PATH "?latest=1") == 0);
    assert(timeout_ms >= 100U && timeout_ms <= 3000U);
    assert(!lock_held);
    ++http_checks;
    return http_response;
}

/** 功能：运行工作任务直到再次等待队列；参数：无；返回：无。 */
static void run_worker(void) {
    if (setjmp(stopped) == 0) {
        worker.function(worker.argument);
    }
}

/** 功能：注册虚拟工作任务；参数：同 FreeRTOS；返回：成功。 */
BaseType_t xTaskCreate(TaskFunction_t function, const char *name, uint16_t depth,
                      void *argument, UBaseType_t priority, TaskHandle_t *handle) {
    (void)name;
    (void)priority;
    assert(depth >= 768U);
    worker.function = function;
    worker.argument = argument;
    if (handle != NULL) {
        *handle = &worker;
    }
    return pdPASS;
}

/** 功能：返回虚拟毫秒；参数：无；返回：当前时刻。 */
TickType_t xTaskGetTickCount(void) {
    return tick;
}

/** 功能：推进虚拟时间并由闹钟所有者完成维护准入；参数：delay，等待毫秒；返回：无。 */
void vTaskDelay(TickType_t delay) {
    tick += delay;
    if (gate == ALARM_MAINTENANCE_PENDING) {
        gate = reject_gate ? ALARM_MAINTENANCE_REJECTED : ALARM_MAINTENANCE_ACTIVE;
    }
}

/** 功能：创建容量为一的虚拟队列；参数：count，容量；size，单项长度；返回：队列句柄。 */
QueueHandle_t xQueueCreate(UBaseType_t count, UBaseType_t size) {
    assert(count == 1U && size <= sizeof queued);
    queued_size = size;
    return queued;
}

/** 功能：排队任务；参数：同 FreeRTOS；返回：是否成功。 */
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait) {
    (void)wait;
    assert(queue == queued);
    if (queue_full || fail_enqueue) {
        return pdFALSE;
    }
    memcpy(queued, item, queued_size);
    queue_full = true;
    return pdTRUE;
}

/** 功能：取出任务，空队列结束虚拟任务运行；参数：同 FreeRTOS；返回：成功。 */
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait) {
    assert(queue == queued);
    if (!queue_full) {
        assert(wait == portMAX_DELAY);
        longjmp(stopped, 1);
    }
    memcpy(item, queued, queued_size);
    queue_full = false;
    return pdTRUE;
}

/** 功能：删除虚拟队列；参数：queue，队列；返回：无。 */
void vQueueDelete(QueueHandle_t queue) {
    assert(queue == queued);
}

/** 功能：初始化虚拟事务锁；参数：无；返回：成功。 */
bool bl24c512_transaction_init(void) {
    return true;
}

/** 功能：取得整个 EEPROM 事务；参数：无；返回：成功。 */
bool bl24c512_transaction_begin(void) {
    assert(!lock_held);
    if (fail_lock) {
        return false;
    }
    lock_held = true;
    return true;
}

/** 功能：释放整个 EEPROM 事务；参数：无；返回：无。 */
void bl24c512_transaction_end(void) {
    assert(lock_held);
    lock_held = false;
}

/** 功能：模拟 EEPROM 读取边界；参数：同驱动；返回：是否位于事务中。 */
bool bl24c512_read(uint32_t address, void *data, size_t length) {
    (void)address;
    (void)data;
    (void)length;
    return lock_held;
}

/** 功能：模拟 EEPROM 写入边界；参数：同驱动；返回：是否位于事务中。 */
bool bl24c512_write(uint32_t address, const void *data, size_t length) {
    (void)address;
    (void)data;
    (void)length;
    return lock_held;
}

/** 功能：提供虚拟的健康本地服务；参数：snapshot，输出快照；返回：是否可用。 */
bool alarm_service_snapshot(alarm_service_snapshot_t *snapshot) {
    memset(snapshot, 0, sizeof *snapshot);
    snapshot->local_editing = editing;
    snapshot->saving = (snapshot_busy & 1U) != 0U;
    snapshot->ringing_mask = (snapshot_busy & 2U) != 0U ? 1U : 0U;
    snapshot->snoozed_mask = (snapshot_busy & 4U) != 0U ? 1U : 0U;
    snapshot->test_ringing = (snapshot_busy & 8U) != 0U;
    snapshot->test_pending = (snapshot_busy & 16U) != 0U;
    return !unavailable;
}

/** 功能：向虚拟闹钟所有者预留维护请求；参数：无；返回：是否排队。 */
bool alarm_service_maintenance_request(void) {
    if (unavailable || gate != ALARM_MAINTENANCE_NONE) {
        return false;
    }
    gate = ALARM_MAINTENANCE_PENDING;
    return true;
}

/** 功能：读取虚拟维护结果；参数：无；返回：维护状态。 */
alarm_maintenance_state_t alarm_service_maintenance_state(void) {
    return gate;
}

/** 功能：释放维护请求；参数：无；返回：无。 */
void alarm_service_maintenance_release(void) {
    gate = ALARM_MAINTENANCE_NONE;
    ++released;
}

/** 功能：读取内存日志并检查事务锁；参数：同公共日志接口；返回：读取成功。 */
ota_store_result_t ota_journal_load(const ota_journal_io_t *io, ota_record_t *record) {
    assert(lock_held && io->read != NULL && io->write != NULL);
    if (fail_load) {
        return OTA_STORE_IO;
    }
    tick += load_delay;
    *record = persisted;
    return OTA_STORE_OK;
}

/** 功能：保存内存日志并检查维护准入；参数：同公共日志接口；返回：模拟保存结果。 */
ota_store_result_t ota_journal_save(const ota_journal_io_t *io, ota_record_t *record) {
    (void)io;
    assert(lock_held);
    if (record->state == OTA_STATE_REQUESTED) {
        assert(gate == ALARM_MAINTENANCE_ACTIVE);
    }
    ++saves;
    if (fail_store) {
        return OTA_STORE_IO;
    }
    persisted = *record;
    ++persisted.generation;
    *record = persisted;
    return lost_ack ? OTA_STORE_IO : OTA_STORE_OK;
}

/** 功能：模拟已由公共模块认证的安装描述，隔离状态机边界；参数：同认证接口；返回：有效。 */
bool ota_manifest_read_trusted(const uint8_t header[256], bool installed,
                               ota_manifest_t *out) {
    (void)installed;
    if (header[0] != 42U) {
        return false;
    }
    memset(out, 0, sizeof *out);
    memcpy(out->raw, header, 256U);
    memcpy(out->id, header + 96U, 16U);
    out->version = header[24];
    return true;
}

/** 功能：模拟明确未认证的网络元数据读取；参数：包头和输出；返回：是否结构可读。 */
bool ota_manifest_read_hint(const uint8_t header[256], ota_manifest_t *out) {
    return ota_manifest_read_trusted(header, false, out);
}

/** 功能：记录网络任务触发的复位；参数：无；返回：无。 */
void NVIC_SystemReset(void) {
    ++resets;
}

/** 功能：检查公开状态字段与秘密隔离；参数：state，预期阶段；返回：无。 */
static void expect_state(const char *state) {
    char status[512];
    ota_service_status(status, sizeof status, 123U);
    assert(strstr(status, state) != NULL);
    assert(strstr(status, "abcdef0123456789") == NULL);
}

/** 功能：模拟真实本地生产者分别完成一次工作；参数：sources，参与的本地服务位；返回：无。 */
static void pulse_local(uint8_t sources) {
    if ((sources & OTA_HEARTBEAT_CLOCK) != 0U) {
        ota_service_heartbeat(OTA_HEARTBEAT_CLOCK);
    }
    if ((sources & OTA_HEARTBEAT_UI) != 0U) {
        ota_service_heartbeat(OTA_HEARTBEAT_UI);
    }
    if ((sources & OTA_HEARTBEAT_ALARM) != 0U) {
        ota_service_heartbeat(OTA_HEARTBEAT_ALARM);
    }
}

/** 功能：以二百五十毫秒间隔持续推进本地服务，不替代为网络健康证明；参数：end，目标毫秒；sources，参与服务位；返回：无。 */
static void advance_local(uint32_t end, uint8_t sources) {
    while (tick < end) {
        uint32_t remaining = end - tick;
        tick += remaining > 250U ? 250U : remaining;
        pulse_local(sources);
    }
}

/** 功能：执行独立进程隔离的状态机行为场景；参数：argc、argv，场景编号；返回：通过返回零。 */
int main(int argc, char **argv) {
    const char *valid = "session=123&token=abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";
    unsigned scenario = argc == 2 ? (unsigned)atoi(argv[1]) : 0U;
    uint8_t *flash = VirtualAlloc((void *)(uintptr_t)0x08010000U, 0x70000U,
                                  MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    uint8_t *footer = (uint8_t *)(uintptr_t)OTA_FOOTER_BASE;
    assert(flash != NULL);
    memset(footer, 0, 256U);
    footer[0] = 42U;
    footer[24] = scenario == 21U ? 18U : 17U;
    footer[96] = 7U;
    persisted.state = OTA_STATE_TRIAL;
    memcpy(persisted.manifest, footer, sizeof persisted.manifest);
    wrong_identity = scenario == 7U;
    if (wrong_identity) {
        persisted.manifest[96] = 8U;
    }
    if (scenario == 13U) {
        persisted.manifest[64] = 9U;
    }
    if (scenario == 14U) {
        unavailable = true;
    }
    if (scenario == 15U) {
        footer[0] = 0U;
    }
    ota_service_init();
    ota_service_start();
    assert(worker.function != NULL);
    if (scenario == 1U) {
        assert(ota_service_request("session=123&token=00", 123U) == 403);
        assert(ota_service_request("session=123&token=1111111111111111111111111111111111111111111111111111111111111111", 123U) == 403);
        assert(ota_service_request(valid, 999U) == 403);
        assert(ota_service_request("session=123&token=abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789&session=123", 123U) == 403);
        assert(saves == 0U && gate == ALARM_MAINTENANCE_NONE);
    } else if (scenario == 2U || scenario == 3U || scenario == 4U || scenario == 8U ||
               scenario == 18U || scenario == 19U) {
        fail_store = scenario == 3U;
        lost_ack = scenario == 8U;
        fail_lock = scenario == 18U;
        fail_load = scenario == 19U;
        reject_gate = scenario == 4U;
        assert(ota_service_request(valid, 123U) == 202);
        assert(saves == 0U);
        expect_state("queued");
        assert(ota_service_request(valid, 123U) == 202);
        run_worker();
        if (fail_store || reject_gate || lost_ack || fail_lock || fail_load) {
            expect_state("failed");
            assert(released == 1U && gate == ALARM_MAINTENANCE_NONE);
            ota_service_network_poll(20000U);
            assert(resets == 0U);
            if (lost_ack) {
                assert(persisted.state == OTA_STATE_REQUESTED);
            }
        } else {
            expect_state("saved");
            assert(saves == 1U && persisted.state == OTA_STATE_REQUESTED);
            assert(resets == 0U);
            ota_service_network_poll(tick + 1999U);
            assert(resets == 0U);
            ota_service_network_poll(tick + 2000U);
            assert(resets == 1U);
        }
    } else if (scenario == 5U) {
        unavailable = true;
        assert(ota_service_request(valid, 123U) == 409);
        assert(saves == 0U);
    } else if (scenario == 6U || scenario == 7U || scenario == 9U || scenario == 10U ||
               scenario == 13U || scenario == 15U) {
        if (scenario == 9U) {
            persisted.state = OTA_STATE_REQUESTED;
        }
        if (scenario == 10U) {
            unavailable = true;
        }
        pulse_local(OTA_HEARTBEAT_ALL);
        advance_local(9999U, OTA_HEARTBEAT_ALL);
        ota_service_network_poll(tick);
        assert(saves == 0U && !queue_full);
        advance_local(10000U, OTA_HEARTBEAT_ALL);
        ota_service_network_poll(tick);
        run_worker();
        if (scenario == 6U) {
            assert(saves == 1U && persisted.state == OTA_STATE_CONFIRMED);
        } else {
            assert(saves == 0U);
        }
    } else if (scenario == 11U) {
        gate = ALARM_MAINTENANCE_ACTIVE;
        assert(ota_service_request(valid, 123U) == 409);
        assert(saves == 0U);
    } else if (scenario == 12U) {
        assert(ota_service_request(valid, 123U) == 503);
    } else if (scenario == 14U) {
        tick = 10000U;
        ota_service_network_poll(tick);
        assert(!queue_full && saves == 0U);
        unavailable = false;
        ota_service_start();
        pulse_local(OTA_HEARTBEAT_ALL);
        advance_local(19999U, OTA_HEARTBEAT_ALL);
        ota_service_network_poll(tick);
        assert(!queue_full);
        advance_local(20000U, OTA_HEARTBEAT_ALL);
        ota_service_network_poll(tick);
        run_worker();
        assert(saves == 1U && persisted.state == OTA_STATE_CONFIRMED);
    } else if (scenario == 16U) {
        assert(ota_service_request(valid, 123U) == 202);
        ota_service_network_poll(20000U);
        run_worker();
        assert(saves == 1U && persisted.state == OTA_STATE_REQUESTED);
    } else if (scenario == 17U) {
        fail_enqueue = true;
        assert(ota_service_request(valid, 123U) == 409);
        assert(gate == ALARM_MAINTENANCE_NONE && released == 1U && saves == 0U);
    } else if (scenario == 20U) {
        fail_store = true;
        pulse_local(OTA_HEARTBEAT_ALL);
        advance_local(10000U, OTA_HEARTBEAT_ALL);
        ota_service_network_poll(tick);
        run_worker();
        assert(persisted.state == OTA_STATE_TRIAL && saves == 1U);
        fail_store = false;
        advance_local(20000U, OTA_HEARTBEAT_ALL);
        ota_service_network_poll(tick);
        run_worker();
        assert(persisted.state == OTA_STATE_CONFIRMED && saves == 2U);
    } else if (scenario == 21U) {
        expect_state("\"version\":18");
    } else if (scenario == 22U) {
        tick = 11000U;
        ota_service_network_poll(tick);
        run_worker();
        assert(persisted.state == OTA_STATE_TRIAL && saves == 0U);
    } else if (scenario >= 23U && scenario <= 25U) {
        uint8_t stale = scenario == 23U ? OTA_HEARTBEAT_CLOCK :
                        scenario == 24U ? OTA_HEARTBEAT_UI : OTA_HEARTBEAT_ALARM;
        uint8_t running = (uint8_t)(OTA_HEARTBEAT_ALL ^ stale);
        pulse_local(OTA_HEARTBEAT_ALL);
        advance_local(2500U, OTA_HEARTBEAT_ALL);
        while (tick < 15000U) {
            advance_local(tick + 250U, running);
            ota_service_network_poll(tick);
            run_worker();
            assert(persisted.state == OTA_STATE_TRIAL && saves == 0U);
        }
        pulse_local(OTA_HEARTBEAT_ALL);
        advance_local(24999U, OTA_HEARTBEAT_ALL);
        ota_service_network_poll(tick);
        run_worker();
        assert(persisted.state == OTA_STATE_TRIAL && saves == 0U);
        advance_local(25000U, OTA_HEARTBEAT_ALL);
        ota_service_network_poll(tick);
        run_worker();
        assert(persisted.state == OTA_STATE_CONFIRMED && saves == 1U);
    } else if (scenario == 26U || scenario == 30U) {
        pulse_local(OTA_HEARTBEAT_ALL);
        advance_local(10000U, OTA_HEARTBEAT_ALL);
        ota_service_network_poll(tick);
        assert(queue_full);
        if (scenario == 26U) {
            tick += 1001U;
        } else {
            load_delay = 1001U;
        }
        run_worker();
        load_delay = 0U;
        assert(persisted.state == OTA_STATE_TRIAL && saves == 0U);
        pulse_local(OTA_HEARTBEAT_ALL);
        advance_local(21000U, OTA_HEARTBEAT_ALL);
        ota_service_network_poll(tick);
        run_worker();
        assert(persisted.state == OTA_STATE_TRIAL && saves == 0U);
        advance_local(21001U, OTA_HEARTBEAT_ALL);
        ota_service_network_poll(tick);
        run_worker();
        assert(persisted.state == OTA_STATE_CONFIRMED && saves == 1U);
    } else if (scenario == 27U) {
        pulse_local(OTA_HEARTBEAT_ALL);
        advance_local(2500U, OTA_HEARTBEAT_ALL);
        advance_local(15000U, OTA_HEARTBEAT_CLOCK | OTA_HEARTBEAT_UI);
        pulse_local(OTA_HEARTBEAT_ALL);
        ota_service_network_poll(tick);
        run_worker();
        assert(persisted.state == OTA_STATE_TRIAL && saves == 0U);
        advance_local(24999U, OTA_HEARTBEAT_ALL);
        ota_service_network_poll(tick);
        run_worker();
        assert(persisted.state == OTA_STATE_TRIAL && saves == 0U);
        advance_local(25000U, OTA_HEARTBEAT_ALL);
        ota_service_network_poll(tick);
        run_worker();
        assert(persisted.state == OTA_STATE_CONFIRMED && saves == 1U);
    } else if (scenario == 28U) {
        pulse_local(OTA_HEARTBEAT_ALL);
        advance_local(5000U, OTA_HEARTBEAT_ALL);
        tick = 6500U;
        pulse_local(OTA_HEARTBEAT_ALARM);
        pulse_local(OTA_HEARTBEAT_ALL);
        advance_local(16499U, OTA_HEARTBEAT_ALL);
        ota_service_network_poll(tick);
        run_worker();
        assert(persisted.state == OTA_STATE_TRIAL && saves == 0U);
        advance_local(16500U, OTA_HEARTBEAT_ALL);
        ota_service_network_poll(tick);
        run_worker();
        assert(persisted.state == OTA_STATE_CONFIRMED && saves == 1U);
    } else {
        assert(false);
    }
    VirtualFree(flash, 0U, MEM_RELEASE);
    puts("OTA application scenario passed");
    return 0;
}
