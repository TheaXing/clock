#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "alarm_service.h"
#include "alarm_engine.h"
#include "alarm_store.h"
#include "alarm_page.h"
#include "board_keys.h"
#include "ota_service.h"

/** 保存虚拟任务回调。 */
typedef struct {
    TaskFunction_t function; // 任务入口
    void *argument; // 任务参数
} test_task_t;

/** 保存容量为一的虚拟队列。 */
typedef struct {
    size_t size; // 单项长度
    bool full; // 是否已有元素
    uint8_t data[512]; // 队列元素缓冲
} test_queue_t;

static test_task_t tasks[3];
static test_queue_t queues[4];
static unsigned queue_count, writes, transactions;
static uint32_t tick, keys;
static bool locked, lock_failed, seed, buzzer;
static jmp_buf stopped;
static uint8_t eeprom[2048];
static rtc_date_time_t date = {2026U, 1U, 1U, 6U, 59U, 59U, 4U};
static unsigned owner_pulses;

/** 功能：检查真实闹钟所有者在完成一轮之后报告进展；参数：source，来源；返回：无。 */
void ota_service_heartbeat(uint8_t source) {
    assert(source == OTA_HEARTBEAT_ALARM);
    assert(!locked);
    ++owner_pulses;
}

/** 功能：运行指定任务直到一次阻塞；参数：index，任务编号；返回：无。 */
static void run_task(unsigned index) {
    assert(index < 3U && tasks[index].function != NULL);
    if (setjmp(stopped) == 0) {
        tasks[index].function(tasks[index].argument);
    }
}

/** 功能：注册真实服务创建的任务；参数：同 FreeRTOS；返回：成功。 */
BaseType_t xTaskCreate(TaskFunction_t function, const char *name, uint16_t depth,
                      void *argument, UBaseType_t priority, TaskHandle_t *handle) {
    unsigned index = priority == 9U ? 0U : priority == 2U ? 1U : 2U;
    (void)name;
    (void)depth;
    tasks[index].function = function;
    tasks[index].argument = argument;
    if (handle != NULL) {
        *handle = &tasks[index];
    }
    return pdPASS;
}

/** 功能：接受任务清理；参数：handle，任务句柄；返回：无。 */
void vTaskDelete(TaskHandle_t handle) {
    assert(handle != NULL);
}

/** 功能：读取虚拟节拍；参数：无；返回：毫秒。 */
TickType_t xTaskGetTickCount(void) {
    return tick;
}

/** 功能：完成一次所有者迭代并返回测试；参数：previous，上次节拍；increment，增量；返回：无。 */
void vTaskDelayUntil(TickType_t *previous, TickType_t increment) {
    *previous += increment;
    tick = *previous;
    assert(owner_pulses > 0U);
    longjmp(stopped, 1);
}

/** 功能：创建单项队列；参数：count，容量；size，长度；返回：句柄。 */
QueueHandle_t xQueueCreate(UBaseType_t count, UBaseType_t size) {
    test_queue_t *queue;
    assert(count == 1U && queue_count < 4U && size <= 512U);
    queue = &queues[queue_count++];
    queue->size = size;
    return queue;
}

/** 功能：发送队列元素；参数：同 FreeRTOS；返回：成功或队满。 */
BaseType_t xQueueSend(QueueHandle_t handle, const void *item, TickType_t wait) {
    test_queue_t *queue = handle;
    (void)wait;
    if (queue->full) {
        return pdFALSE;
    }
    memcpy(queue->data, item, queue->size);
    queue->full = true;
    return pdTRUE;
}

/** 功能：覆盖显示快照；参数：同 FreeRTOS；返回：成功。 */
BaseType_t xQueueOverwrite(QueueHandle_t handle, const void *item) {
    test_queue_t *queue = handle;
    queue->full = false;
    return xQueueSend(handle, item, 0U);
}

/** 功能：接收单项队列，阻塞时返回测试；参数：同 FreeRTOS；返回：是否收到。 */
BaseType_t xQueueReceive(QueueHandle_t handle, void *item, TickType_t wait) {
    test_queue_t *queue = handle;
    if (!queue->full) {
        if (wait == portMAX_DELAY) {
            longjmp(stopped, 1);
        }
        return pdFALSE;
    }
    memcpy(item, queue->data, queue->size);
    queue->full = false;
    return pdTRUE;
}

/** 功能：清理队列；参数：queue，句柄；返回：无。 */
void vQueueDelete(QueueHandle_t queue) {
    assert(queue != NULL);
}

/** 功能：创建显示锁；参数：无；返回：虚拟句柄。 */
SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    return queues;
}

/** 功能：取得显示锁；参数：同 FreeRTOS；返回：成功。 */
BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t wait) {
    (void)wait;
    assert(mutex == queues);
    return pdTRUE;
}

/** 功能：释放显示锁；参数：mutex，句柄；返回：成功。 */
BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex) {
    assert(mutex == queues);
    return pdTRUE;
}

/** 功能：删除显示锁；参数：mutex，句柄；返回：无。 */
void vSemaphoreDelete(SemaphoreHandle_t mutex) {
    assert(mutex == queues);
}

/** 功能：取得整个 EEPROM 事务；参数：无；返回：是否成功。 */
bool bl24c512_transaction_begin(void) {
    assert(!locked);
    if (lock_failed) {
        return false;
    }
    ++transactions;
    locked = true;
    return true;
}

/** 功能：释放整个 EEPROM 事务；参数：无；返回：无。 */
void bl24c512_transaction_end(void) {
    assert(locked);
    locked = false;
}

/** 功能：检查事务边界内的 EEPROM 读取；参数：同驱动；返回：成功。 */
bool bl24c512_read(uint32_t address, void *data, size_t length) {
    assert((locked || seed) && address + length <= sizeof eeprom);
    memcpy(data, eeprom + address, length);
    return true;
}

/** 功能：检查事务边界内的 EEPROM 写入；参数：同驱动；返回：成功。 */
bool bl24c512_write(uint32_t address, const void *data, size_t length) {
    assert((locked || seed) && address + length <= sizeof eeprom);
    ++writes;
    memcpy(eeprom + address, data, length);
    return true;
}

/** 功能：初始化虚拟按键；参数：无；返回：无。 */
void board_keys_init(void) {
}

/** 功能：取走本轮物理按键；参数：now_ms，毫秒；返回：按键位。 */
uint32_t board_keys_poll(uint32_t now_ms) {
    uint32_t result = keys;
    (void)now_ms;
    keys = 0U;
    return result;
}

/** 功能：初始化虚拟蜂鸣器；参数：无；返回：无。 */
void buzzer_init(void) {
    buzzer = false;
}

/** 功能：记录蜂鸣器输出；参数：on，输出；返回：无。 */
void buzzer_set(bool on) {
    buzzer = on;
}

/** 功能：提供虚拟日历；参数：output，输出；返回：无。 */
void rtc_get_time(rtc_date_time_t *output) {
    *output = date;
}

/** 功能：写入虚拟日历；参数：value，日历；返回：无。 */
void rtc_set_time(const rtc_date_time_t *value) {
    date = *value;
}

/** 功能：报告 RTC 可用；参数：无；返回：真。 */
bool clock_rtc_ready(void) {
    return true;
}

/** 功能：接收显示缓存失效；参数：无；返回：无。 */
void alarm_page_invalidate(void) {
}

/** 功能：接收完整显示快照；参数：model，显示模型；返回：无。 */
void alarm_page_render(const alarm_page_model_t *model) {
    assert(model != NULL);
}

/** 功能：接收主页面重绘请求；参数：无；返回：无。 */
void app_request_main_redraw(void) {
}

/** 功能：丢弃测试日志；参数：format，格式；返回：零。 */
int clock_log(const char *format, ...) {
    (void)format;
    return 0;
}

/** 功能：丢弃测试日志行；参数：text，日志；返回：零。 */
int clock_log_line(const char *text) {
    (void)text;
    return 0;
}

/** 功能：种子配置读取；参数：同日志接口；返回：成功。 */
static bool seed_read(void *context, uint32_t address, void *data, size_t length) {
    (void)context;
    return bl24c512_read(address, data, length);
}

/** 功能：种子配置写入；参数：同日志接口；返回：成功。 */
static bool seed_write(void *context, uint32_t address, const void *data, size_t length) {
    (void)context;
    return bl24c512_write(address, data, length);
}

/** 功能：执行维护准入与真实保存事务场景；参数：argc、argv，场景编号；返回：通过返回零。 */
int main(int argc, char **argv) {
    unsigned scenario = argc == 2 ? (unsigned)atoi(argv[1]) : 0U;
    alarm_service_snapshot_t snapshot = {0};
    alarm_store_t store;
    alarm_config_t config;
    alarm_store_io_t io = {seed_read, seed_write, NULL};
    uint32_t request_id = 0U;
    memset(eeprom, 0xff, sizeof eeprom);
    seed = true;
    alarm_store_init(&store, io);
    alarm_config_defaults(&config);
    config.entries[0].enabled = scenario == 4U || scenario == 1U;
    config.entries[0].hour = 7U;
    config.entries[0].minute = 0U;
    config.entries[0].repeat_mask = 127U;
    assert(alarm_store_save(&store, &config) == ALARM_STORE_OK);
    seed = false;
    writes = 0U;
    lock_failed = scenario == 7U;
    alarm_service_init();
    assert(!locked && alarm_service_snapshot(&snapshot));
    if (scenario != 7U) {
        assert(transactions == 1U);
    }
    if (scenario == 1U) {
        assert(alarm_service_maintenance_request());
        run_task(0U);
        assert(alarm_service_maintenance_state() == ALARM_MAINTENANCE_ACTIVE);
        keys = BOARD_KEY1_SHORT;
        run_task(0U);
        keys = BOARD_KEY3_SHORT;
        date.hour = 7U;
        date.minute = 0U;
        date.second = 0U;
        tick = 1000U;
        run_task(0U);
        assert(alarm_service_snapshot(&snapshot) && !snapshot.local_editing);
        assert(snapshot.ringing_mask == 0U && !buzzer && writes == 0U);
        assert(alarm_service_remote_submit(0U, &snapshot.config.entries[0],
                                           snapshot.state_version, &request_id) == ALARM_REMOTE_BUSY);
        alarm_service_maintenance_release();
        tick += 1000U;
        run_task(0U);
        assert(alarm_service_snapshot(&snapshot) && snapshot.ringing_mask == 1U);
    } else if (scenario == 2U || scenario == 3U) {
        keys = BOARD_KEY1_SHORT;
        run_task(0U);
        assert(alarm_service_maintenance_request());
        keys = scenario == 2U ? BOARD_KEY3_SHORT : BOARD_KEY1_SHORT;
        run_task(0U);
        assert(alarm_service_maintenance_state() == ALARM_MAINTENANCE_REJECTED);
        assert(alarm_service_snapshot(&snapshot));
        assert(scenario == 2U ? snapshot.local_editing : snapshot.saving);
        if (scenario == 3U) {
            run_task(1U);
            assert(!locked && transactions == 2U && writes > 0U);
            run_task(0U);
            assert(alarm_service_snapshot(&snapshot) && snapshot.config.entries[0].enabled);
        }
    } else if (scenario == 4U || scenario == 5U) {
        run_task(0U);
        assert(alarm_service_maintenance_request());
        if (scenario == 4U) {
            date.hour = 7U;
            date.minute = 0U;
            date.second = 0U;
            tick = 1000U;
        } else {
            keys = BOARD_KEY2_LONG;
        }
        run_task(0U);
        assert(alarm_service_maintenance_state() == ALARM_MAINTENANCE_REJECTED);
        assert(writes == 0U);
    } else if (scenario == 6U) {
        assert(alarm_service_remote_submit(0U, &snapshot.config.entries[0],
                                           snapshot.state_version, &request_id) == ALARM_REMOTE_ACCEPTED);
        assert(!alarm_service_maintenance_request());
        run_task(0U);
        run_task(1U);
        run_task(0U);
        assert(alarm_service_remote_result(request_id) == ALARM_REMOTE_SAVED);
        assert(!locked && transactions == 2U);
    } else if (scenario == 7U) {
        assert(!snapshot.storage_ok);
        keys = BOARD_KEY1_SHORT;
        run_task(0U);
        keys = BOARD_KEY1_SHORT;
        run_task(0U);
        run_task(1U);
        run_task(0U);
        assert(writes == 0U && !locked);
        assert(alarm_service_snapshot(&snapshot) && !snapshot.storage_ok && !snapshot.saving);
    } else {
        assert(false);
    }
    puts("OTA alarm owner scenario passed");
    return 0;
}
