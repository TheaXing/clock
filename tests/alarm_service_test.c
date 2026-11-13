/* Run the production task callbacks against a virtual RTOS/device boundary.
 * Real menu decisions, engine scheduling and EEPROM journal remain linked. */
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
#include "alarm_page.h"
#include "alarm_store.h"
#include "board_keys.h"

enum { OWNER, STORE, VIEW, TASK_COUNT };
typedef struct { TaskFunction_t function; void *argument; } task_slot_t;
typedef struct { size_t size; bool full; unsigned char bytes[512]; } queue_slot_t;
static task_slot_t tasks[TASK_COUNT];
static queue_slot_t queues[3];
static unsigned queue_count, created_tasks, key_polls, redraws, renders, writes, log_errors;
static unsigned deleted_tasks, fail_task_at, fail_queue_at;
static bool mutex_locked, fail_io, fail_writes, buzzer_on, rtc_ready = true, rtc_write_fail;
static bool bad_weekday, corrupt_boot, advance_owner_while_view_waits;
static bool seed_other, seed_latest, fail_boot_reads, fail_boot_newest;
static bool seed_selected_special, seed_day_conflict;
static unsigned critical_depth;
static int fail_read_slot = -1;
static uint8_t eeprom[2048];
static uint32_t tick, run_start, run_duration, rtc_seconds, rtc_base_tick;
static int running_task = -1;
static jmp_buf stopped[TASK_COUNT];
static struct { uint32_t tick, keys; } input[32];
static unsigned input_count, next_input;
static alarm_config_t shown_config;
static alarm_menu_t shown_menu;
static alarm_page_model_t shown;
static rtc_date_time_t written_time;

static void key_at(uint32_t when, uint32_t keys)
{
    assert(input_count < 32);
    input[input_count].tick = when; input[input_count++].keys = keys;
}

static void run_owner(uint32_t duration)
{
    int previous = running_task;
    run_start = tick; run_duration = duration; running_task = OWNER;
    if (setjmp(stopped[OWNER]) == 0) tasks[OWNER].function(tasks[OWNER].argument);
    running_task = previous;
}

void alarm_test_enter_critical(void) { ++critical_depth; }
void alarm_test_exit_critical(void)
{
    assert(critical_depth != 0U);
    --critical_depth;
}

static void run_blocking_task(unsigned index)
{
    assert(index != OWNER && tasks[index].function);
    running_task = (int)index;
    if (setjmp(stopped[index]) == 0) tasks[index].function(tasks[index].argument);
    running_task = -1;
}

BaseType_t xTaskCreate(TaskFunction_t function, const char *name, uint16_t depth,
                       void *argument, UBaseType_t priority, TaskHandle_t *handle)
{
    unsigned index;
    (void)name;
    if (priority == 9) { index = OWNER; assert(depth == 1536); }
    else if (priority == 2) { index = STORE; assert(depth == 1024); }
    else { assert(priority == 4 && depth == 1024); index = VIEW; }
    ++created_tasks;
    if (created_tasks == fail_task_at) return pdFALSE;
    tasks[index].function = function; tasks[index].argument = argument;
    if (handle) *handle = &tasks[index];
    return pdPASS;
}
void vTaskDelete(TaskHandle_t task) { assert(task); ++deleted_tasks; }
TickType_t xTaskGetTickCount(void) { return tick; }
void vTaskDelayUntil(TickType_t *previous, TickType_t increment)
{
    assert(running_task == OWNER && increment == 10);
    tick = *previous + increment; *previous = tick;
    if (tick - run_start >= run_duration) longjmp(stopped[OWNER], 1);
}
QueueHandle_t xQueueCreate(UBaseType_t count, UBaseType_t size)
{
    queue_slot_t *queue;
    assert(count == 1 && queue_count < 3 && size <= 512);
    queue = &queues[queue_count++]; queue->size = size;
    if (queue_count == fail_queue_at) return NULL;
    return queue;
}
BaseType_t xQueueSend(QueueHandle_t handle, const void *item, TickType_t wait)
{
    queue_slot_t *queue = handle;
    if (queue->full) { assert(wait == 0); return pdFALSE; }
    memcpy(queue->bytes, item, queue->size); queue->full = true;
    return pdTRUE;
}
BaseType_t xQueueOverwrite(QueueHandle_t handle, const void *item)
{
    queue_slot_t *queue = handle;
    assert(running_task == OWNER);
    memcpy(queue->bytes, item, queue->size); queue->full = true; return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t handle, void *item, TickType_t wait)
{
    queue_slot_t *queue = handle;
    if (!queue->full) {
        if (wait == portMAX_DELAY) longjmp(stopped[running_task], 1);
        return pdFALSE;
    }
    memcpy(item, queue->bytes, queue->size); queue->full = false; return pdTRUE;
}
void vQueueDelete(QueueHandle_t queue) { assert(queue); }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &mutex_locked; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t wait)
{
    assert(mutex == &mutex_locked && running_task != OWNER);
    if (mutex_locked && running_task == VIEW && advance_owner_while_view_waits) {
        advance_owner_while_view_waits = false;
        run_owner(1000); /* The fast owner runs while this view waits on SPI. */
        alarm_service_display_end(); /* Existing normal producer finishes. */
    }
    if (mutex_locked) {
        if (running_task == VIEW && wait == portMAX_DELAY) longjmp(stopped[VIEW], 1);
        return pdFALSE;
    }
    mutex_locked = true; return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex)
{
    assert(mutex == &mutex_locked && mutex_locked); mutex_locked = false; return pdTRUE;
}
void vSemaphoreDelete(SemaphoreHandle_t mutex) { assert(mutex); }
void board_keys_init(void) {}
uint32_t board_keys_poll(uint32_t now)
{
    assert(running_task == OWNER && now == tick); ++key_polls;
    if (next_input < input_count && input[next_input].tick <= tick) return input[next_input++].keys;
    return 0;
}
void buzzer_init(void) { buzzer_on = false; }
void buzzer_set(bool on) { assert(on != buzzer_on); buzzer_on = on; }
void rtc_get_time(rtc_date_time_t *date)
{
    if (!rtc_ready) { memset(date, 0, sizeof *date); return; }
    assert(alarm_seconds_to_calendar(rtc_seconds + (tick - rtc_base_tick) / 1000U, date));
    if (bad_weekday) date->weekday = 7;
}
bool clock_rtc_ready(void) { return rtc_ready; }
void rtc_set_time(const rtc_date_time_t *date)
{
    written_time = *date;
    if (!rtc_write_fail) {
        assert(alarm_calendar_to_seconds(date, &rtc_seconds)); rtc_base_tick = tick;
    }
}
bool bl24c512_read(uint32_t address, void *data, size_t length)
{
    assert(address + length <= sizeof eeprom);
    if (fail_io
        || (int)(address / 1024U) == fail_read_slot) return false;
    memcpy(data, eeprom + address, length); return true;
}
bool bl24c512_write(uint32_t address, const void *data, size_t length)
{
    assert(address + length <= sizeof eeprom); ++writes;
    if (fail_io || fail_writes) return false;
    memcpy(eeprom + address, data, length); return true;
}
int clock_log(const char *format, ...) { if (strstr(format, "fail") || strstr(format, "unavailable")) ++log_errors; return 0; }
int clock_log_line(const char *message) { return clock_log(message); }
void app_request_main_redraw(void) { ++redraws; }
void alarm_page_invalidate(void) { assert(running_task == VIEW); }
void alarm_page_render(const alarm_page_model_t *model)
{
    assert(running_task == VIEW && mutex_locked);
    shown = *model; shown_config = *model->config; shown_menu = *model->menu;
    shown.config = &shown_config; shown.menu = &shown_menu; ++renders;
}
static bool io_read(void *context, uint32_t address, void *data, size_t length)
{ (void)context; return bl24c512_read(address, data, length); }
static bool io_write(void *context, uint32_t address, const void *data, size_t length)
{ (void)context; return bl24c512_write(address, data, length); }
static void boot(bool stored)
{
    rtc_date_time_t now = {2026,1,1,6,59,59,4};
    memset(eeprom, 0xff, sizeof eeprom);
    if (corrupt_boot) eeprom[0] = 0;
    assert(alarm_calendar_to_seconds(&now, &rtc_seconds));
    if (stored) {
        alarm_config_t config; alarm_store_t store;
        alarm_store_io_t io = {io_read, io_write, NULL};
        alarm_config_defaults(&config); config.entries[0].enabled = 1;
        if (seed_selected_special) {
            config.entries[0].year = 2028; config.entries[0].month = 2; config.entries[0].day = 29;
            config.entries[0].hour = 9; config.entries[0].minute = 15; config.entries[0].repeat_mask = 0x60;
            config.entries[0].ring_seconds = 120; config.entries[0].snooze_seconds = 600;
        }
        if (seed_day_conflict) { config.entries[0].day = 28; config.entries[0].repeat_mask = 0; }
        if (seed_other) { config.entries[6].enabled = 1; config.entries[6].hour = 9; }
        alarm_store_init(&store, io); assert(alarm_store_save(&store, &config) == ALARM_STORE_OK);
        if (seed_latest) {
            config.entries[6].enabled = 1; config.entries[6].hour = 9;
            assert(alarm_store_save(&store, &config) == ALARM_STORE_OK);
        }
        if (seed_day_conflict) {
            config.entries[0].day = 31;
            assert(alarm_store_save(&store, &config) == ALARM_STORE_OK);
        }
    }
    if (fail_boot_reads) fail_io = true;
    if (fail_boot_newest) fail_read_slot = 1;
    writes = 0;
    assert(alarm_service_display_begin()); alarm_service_display_end();
    alarm_service_init();
}

static void test_boot_offline_and_stop(void)
{
    boot(true);
    run_owner(1010); run_blocking_task(VIEW);
    assert(shown.ringing_mask == 1 && shown.config->entries[0].enabled == 1);
    assert(shown.storage_ok && shown.revision == 1 && buzzer_on);
    assert(!alarm_service_display_begin());
    key_at(tick, BOARD_KEY1_SHORT); run_owner(10);
    assert(!buzzer_on && redraws == 1);
    assert(alarm_service_display_begin()); alarm_service_display_end();
}
static void test_save_failure_keeps_live_alarm(void)
{
    boot(true);
    key_at(10, BOARD_KEY1_SHORT); key_at(20, BOARD_KEY1_SHORT);
    run_owner(1010); run_blocking_task(VIEW);
    assert(shown.saving && shown.ringing_mask == 1 && shown.config->entries[0].enabled == 1 && writes == 0);
    key_at(tick, BOARD_KEY1_SHORT); run_owner(10); run_blocking_task(VIEW);
    assert(!buzzer_on && shown.saving && shown.ringing_mask == 0);
    fail_writes = true; run_blocking_task(STORE); run_owner(10); run_blocking_task(VIEW);
    assert(!shown.saving && !shown.storage_ok && strcmp(shown.message, "Save failed") == 0);
    assert(shown.config->entries[0].enabled == 1 && shown.revision == 1);
}
static void test_success_only_applies_after_worker(void)
{
    boot(false);
    key_at(10, BOARD_KEY1_SHORT); key_at(20, BOARD_KEY1_SHORT);
    run_owner(30); run_blocking_task(VIEW);
    assert(shown.saving && shown.config->entries[0].enabled == 0 && writes == 0);
    run_blocking_task(STORE); run_owner(10); run_blocking_task(VIEW);
    assert(!shown.saving && shown.storage_ok && shown.config->entries[0].enabled == 1);
    assert(shown.revision == 1 && strcmp(shown.message, "Saved") == 0 && writes > 0);
    assert(shown.menu->view == ALARM_VIEW_LIST);
}
static void test_diagnostic_without_time_and_snooze(void)
{
    boot(false); rtc_ready = false;
    key_at(0, BOARD_KEY2_LONG); run_owner(10); run_blocking_task(VIEW);
    assert(shown.test_pending && !shown.time_valid && writes == 0);
    run_owner(10000); run_blocking_task(VIEW);
    assert(shown.test_ringing && !shown.test_pending && buzzer_on);
    key_at(tick, BOARD_KEY2_SHORT); run_owner(10); run_blocking_task(VIEW);
    assert(shown.test_pending && !shown.test_ringing && !buzzer_on);
    run_owner(300000); run_blocking_task(VIEW);
    assert(shown.test_ringing);
    key_at(tick, BOARD_KEY1_SHORT); run_owner(10);
    assert(!buzzer_on && writes == 0 && redraws == 1);
}
static void test_view_blocked_owner_keeps_keys(void)
{
    boot(false);
    assert(alarm_service_display_begin()); /* An existing normal producer owns SPI UI. */
    key_at(0, BOARD_KEY1_SHORT); run_owner(10);
    key_at(tick, BOARD_KEY3_LONG);
    advance_owner_while_view_waits = true;
    run_blocking_task(VIEW); /* Resumes with its old menu after owner exits. */
    assert(key_polls == 101 && redraws == 1 && renders == 0);
    /* Any queued old menu must never overwrite the restored normal screen. */
    run_blocking_task(VIEW);
    assert(renders == 0 && alarm_service_display_begin()); alarm_service_display_end();
}
static void test_manual_clock_derives_weekday(void)
{
    bad_weekday = true;
    boot(false);
    key_at(0, BOARD_KEY1_SHORT); key_at(10, BOARD_KEY2_LONG);
    key_at(20, BOARD_KEY1_LONG); run_owner(30); run_blocking_task(VIEW);
    assert(written_time.year == 2026 && written_time.month == 1 && written_time.day == 1 && written_time.weekday == 4);
    assert(strcmp(shown.message, "Time set") == 0 && shown.menu->view == ALARM_VIEW_LIST && writes == 0);
}
static void test_allocation_failure_keeps_normal_ui(void)
{
    boot(true);
    assert(deleted_tasks == fail_task_at - 1 && !buzzer_on);
    assert(alarm_service_display_begin()); alarm_service_display_end();
}
static void test_missing_or_corrupt_storage(void)
{
    boot(false);
    key_at(0, BOARD_KEY1_SHORT); run_owner(10); run_blocking_task(VIEW);
    assert(!shown.storage_ok && !shown.saving && shown.config->entries[0].enabled == 0);
    assert(strcmp(shown.message, corrupt_boot ? "Storage invalid" : "EEPROM unavailable") == 0);
    assert(writes == 0 && !buzzer_on);
}
static void test_manual_clock_failure_stays_editable(void)
{
    boot(false); rtc_write_fail = true;
    key_at(0, BOARD_KEY1_SHORT); key_at(10, BOARD_KEY2_LONG);
    key_at(20, BOARD_KEY2_SHORT); key_at(30, BOARD_KEY1_LONG);
    run_owner(40); run_blocking_task(VIEW);
    assert(written_time.year == 2027 && shown.menu->view == ALARM_VIEW_CLOCK);
    assert(strcmp(shown.message, "RTC unavailable") == 0 && writes == 0);
}
static void test_diagnostic_auto_stop_and_tick_wrap(void)
{
    boot(false); rtc_ready = false;
    tick = UINT32_MAX - 5000U;
    key_at(tick, BOARD_KEY2_LONG); run_owner(10010); run_blocking_task(VIEW);
    assert(shown.test_ringing && buzzer_on);
    run_owner(60000);
    assert(!buzzer_on && redraws == 1 && writes == 0);
    assert(alarm_service_display_begin()); alarm_service_display_end();
}
static void open_and_toggle_first(void)
{
    key_at(tick, BOARD_KEY1_SHORT); key_at(tick + 10, BOARD_KEY1_SHORT);
    run_owner(20); run_blocking_task(VIEW);
    assert(shown.saving);
}
static void check_durable_config(unsigned enabled_first, uint32_t generation)
{
    alarm_store_t verify;
    alarm_config_t config;
    alarm_store_io_t io = {io_read, io_write, NULL};
    alarm_store_init(&verify, io);
    assert(alarm_store_load(&verify, &config) == ALARM_STORE_OK);
    assert(verify.generation == generation && config.entries[0].enabled == enabled_first);
    assert(config.entries[6].enabled == 1 && config.entries[6].hour == 9);
}
static void test_recovered_boot_storage_keeps_untouched_entries(void)
{
    seed_other = true; fail_boot_reads = true; boot(true);
    fail_io = false;
    open_and_toggle_first();
    assert(shown.config->entries[6].enabled == 0); /* Boot could not establish contents. */
    run_blocking_task(STORE); run_owner(10); run_blocking_task(VIEW);
    assert(strcmp(shown.message, "Saved") == 0 && shown.revision == 2);
    assert(shown.config->entries[0].enabled == 1);
    assert(shown.config->entries[6].enabled == 1 && shown.config->entries[6].hour == 9);
    check_durable_config(1, 2);
}
static void test_fallback_boot_merges_latest_durable_entries(void)
{
    seed_latest = true; fail_boot_newest = true; boot(true);
    fail_read_slot = -1;
    open_and_toggle_first();
    assert(shown.config->entries[6].enabled == 0 && shown.revision == 1);
    run_blocking_task(STORE); run_owner(10); run_blocking_task(VIEW);
    assert(strcmp(shown.message, "Saved") == 0 && shown.revision == 3);
    assert(shown.config->entries[0].enabled == 0);
    assert(shown.config->entries[6].enabled == 1 && shown.config->entries[6].hour == 9);
    check_durable_config(0, 3);
}
static void test_unreadable_newest_refuses_save_then_retries(void)
{
    seed_latest = true; fail_boot_newest = true; boot(true);
    open_and_toggle_first();
    run_blocking_task(STORE); run_owner(10); run_blocking_task(VIEW);
    assert(strcmp(shown.message, "Save failed") == 0 && writes == 0);
    assert(shown.config->entries[0].enabled == 1 && shown.config->entries[6].enabled == 0);
    assert(shown.menu->draft.enabled == 0); /* Keep requested edit available. */
    fail_read_slot = -1;
    check_durable_config(1, 2);
    key_at(tick, BOARD_KEY1_SHORT); run_owner(10);
    run_blocking_task(STORE); run_owner(10); run_blocking_task(VIEW);
    assert(strcmp(shown.message, "Saved") == 0 && shown.revision == 3);
    assert(shown.config->entries[0].enabled == 0 && shown.config->entries[6].enabled == 1);
    check_durable_config(0, 3);
}
static void test_real_snooze_keeps_physical_inputs_visible(void)
{
    boot(true); run_owner(1010); run_blocking_task(VIEW);
    assert(shown.ringing_mask == 1);
    key_at(tick, BOARD_KEY2_SHORT); run_owner(10); run_blocking_task(VIEW);
    assert(shown.ringing_mask == 0 && shown.snoozed_mask == 1 && !buzzer_on);
    assert(!alarm_service_display_begin() && redraws == 0);
    assert(strcmp(shown.message, "Snoozed") == 0 && shown.menu->view == ALARM_VIEW_MAIN);
    key_at(tick, BOARD_KEY3_SHORT); run_owner(10); run_blocking_task(VIEW);
    assert(shown.menu->view == ALARM_VIEW_MAIN && shown.snoozed_mask == 1);
    key_at(tick, BOARD_KEY2_SHORT); run_owner(10); run_blocking_task(VIEW);
    assert(shown.snoozed_mask == 1 && !alarm_service_display_begin());
    key_at(tick, BOARD_KEY1_SHORT); run_owner(10); run_blocking_task(VIEW);
    assert(redraws == 1 && alarm_service_display_begin()); alarm_service_display_end();
}
static void test_corrupt_journal_can_recover_with_first_save(void)
{
    corrupt_boot = true; boot(false);
    open_and_toggle_first();
    run_blocking_task(STORE); run_owner(10); run_blocking_task(VIEW);
    assert(strcmp(shown.message, "Saved") == 0 && shown.storage_ok && shown.revision == 1);
    assert(shown.config->entries[0].enabled == 1 && shown.config->entries[6].enabled == 0);
}
static void test_known_durable_baseline_loss_refuses_repair(void)
{
    boot(true); open_and_toggle_first();
    eeprom[0] ^= 1;
    run_blocking_task(STORE); run_owner(10); run_blocking_task(VIEW);
    assert(strcmp(shown.message, "Save failed") == 0 && writes == 0);
    assert(shown.config->entries[0].enabled == 1 && shown.menu->draft.enabled == 0);
}
static void test_toggle_after_boot_io_preserves_selected_unchanged_fields(void)
{
    seed_selected_special = true; fail_boot_reads = true; boot(true);
    fail_io = false; open_and_toggle_first();
    run_blocking_task(STORE); run_owner(10); run_blocking_task(VIEW);
    assert(strcmp(shown.message, "Saved") == 0 && shown.revision == 2);
    assert(shown.config->entries[0].enabled == 1);
    assert(shown.config->entries[0].hour == 9 && shown.config->entries[0].minute == 15);
    assert(shown.config->entries[0].repeat_mask == 0x60);
    assert(shown.config->entries[0].year == 2028 && shown.config->entries[0].month == 2 && shown.config->entries[0].day == 29);
    assert(shown.config->entries[0].ring_seconds == 120 && shown.config->entries[0].snooze_seconds == 600);
}
static void test_field_merge_invalid_calendar_preserves_draft(void)
{
    seed_day_conflict = true; fail_boot_newest = true; boot(true);
    key_at(0, BOARD_KEY1_SHORT); key_at(10, BOARD_KEY3_SHORT);
    key_at(20, BOARD_KEY1_SHORT); key_at(30, BOARD_KEY1_SHORT); key_at(40, BOARD_KEY1_SHORT);
    key_at(50, BOARD_KEY1_SHORT); key_at(60, BOARD_KEY1_SHORT);
    key_at(70, BOARD_KEY2_SHORT); key_at(80, BOARD_KEY1_LONG);
    run_owner(90); run_blocking_task(VIEW);
    assert(shown.saving && shown.menu->draft.month == 2 && shown.menu->draft.day == 28);
    fail_read_slot = -1;
    run_blocking_task(STORE); run_owner(10); run_blocking_task(VIEW);
    assert(strcmp(shown.message, "Save failed") == 0 && writes == 0 && shown.revision == 1);
    assert(shown.menu->view == ALARM_VIEW_EDIT && shown.menu->draft.month == 2 && shown.menu->draft.day == 28);
    assert(shown.config->entries[0].month == 1 && shown.config->entries[0].day == 28);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    switch (atoi(argv[1])) {
    case 1: test_boot_offline_and_stop(); break;
    case 2: test_save_failure_keeps_live_alarm(); break;
    case 3: test_success_only_applies_after_worker(); break;
    case 4: test_diagnostic_without_time_and_snooze(); break;
    case 5: test_view_blocked_owner_keeps_keys(); break;
    case 6: test_manual_clock_derives_weekday(); break;
    case 7: fail_task_at = 3; test_allocation_failure_keeps_normal_ui(); break;
    case 8: fail_task_at = 1; test_allocation_failure_keeps_normal_ui(); break;
    case 9: fail_task_at = 2; test_allocation_failure_keeps_normal_ui(); break;
    case 10: fail_io = true; test_missing_or_corrupt_storage(); break;
    case 11: corrupt_boot = true; test_missing_or_corrupt_storage(); break;
    case 12: test_manual_clock_failure_stays_editable(); break;
    case 13: test_diagnostic_auto_stop_and_tick_wrap(); break;
    case 14: test_recovered_boot_storage_keeps_untouched_entries(); break;
    case 15: test_fallback_boot_merges_latest_durable_entries(); break;
    case 16: test_unreadable_newest_refuses_save_then_retries(); break;
    case 17: test_real_snooze_keeps_physical_inputs_visible(); break;
    case 18: test_corrupt_journal_can_recover_with_first_save(); break;
    case 19: test_known_durable_baseline_loss_refuses_repair(); break;
    case 20: test_toggle_after_boot_io_preserves_selected_unchanged_fields(); break;
    case 21: test_field_merge_invalid_calendar_preserves_draft(); break;
    default: assert(0);
    }
    printf("alarm service scenario %s passed\n", argv[1]);
    return 0;
}
