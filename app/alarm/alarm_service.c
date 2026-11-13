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
#include <string.h>

/* The BSP and engine both use wrapping 32-bit milliseconds. */
_Static_assert(configTICK_RATE_HZ == 1000, "Alarm service requires 1 kHz RTOS ticks");
_Static_assert(sizeof(TickType_t) == sizeof(uint32_t), "Alarm service requires 32-bit ticks");
#define REMOTE_RESULT_COUNT 8U

typedef struct {
    alarm_entry_t base;
    alarm_entry_t entry;
    uint16_t fields_mask; /* Zero retains physical/legacy diff merge. */
    uint8_t selected;
} save_job_t;

typedef struct {
    alarm_store_result_t result;
    uint32_t generation;
    alarm_config_t config;
} save_result_t;

typedef struct {
    alarm_entry_t entry;
    uint32_t expected_version;
    uint32_t request_id;
    uint16_t fields_mask;
    uint8_t selected;
} remote_job_t;

/* No pointers into owner state cross to the slow display task. */
typedef struct {
    alarm_config_t config;
    alarm_menu_t menu;
    rtc_date_time_t now;
    uint32_t revision;
    uint32_t display_epoch;
    uint8_t ringing_mask;
    uint8_t snoozed_mask;
    bool time_valid;
    bool saving;
    bool storage_ok;
    bool test_pending;
    bool test_ringing;
    const char *message; /* Static literals only. */
} view_snapshot_t;

static SemaphoreHandle_t display_mutex;
static QueueHandle_t save_queue, result_queue, view_queue, remote_queue;
static volatile bool owns_display;
static volatile uint32_t display_epoch;
static bool initialized;
/* Shared with network tasks only inside short RTOS critical sections. Neither
 * the engine nor the physical menu is ever touched by a network caller. */
static struct {
    alarm_service_snapshot_t snapshot;
    uint32_t pending_id;
    uint32_t next_id;
    struct {
        uint32_t id;
        alarm_remote_result_t result;
    } completed[REMOTE_RESULT_COUNT];
    uint8_t completed_next;
} published;
static alarm_store_t store;
static struct {
    alarm_engine_t engine;
    alarm_menu_t menu;
    rtc_date_time_t now;
    uint32_t revision;
    uint32_t state_version;
    uint32_t save_request_id; /* Zero denotes a physical-menu save. */
    uint32_t last_engine_ms;
    uint32_t last_view_ms;
    uint32_t test_due_ms;
    uint32_t test_started_ms;
    uint32_t beep_started_ms;
    bool time_valid;
    bool saving;
    bool storage_ok;
    bool test_pending;
    bool test_ringing;
    bool beep_active;
    bool buzzer_on;
    bool dirty;
    const char *message;
} owner;

static bool local_editor_active(void)
{
    return owner.menu.view == ALARM_VIEW_EDIT || owner.menu.view == ALARM_VIEW_CLOCK;
}

static void publish_service_snapshot(void)
{
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

bool alarm_service_snapshot(alarm_service_snapshot_t *snapshot)
{
    bool available;
    if (snapshot == NULL) return false;
    taskENTER_CRITICAL();
    available = initialized;
    if (available) *snapshot = published.snapshot;
    taskEXIT_CRITICAL();
    return available;
}

static alarm_remote_submit_t remote_submit(uint8_t index, const alarm_entry_t *entry,
    uint16_t fields_mask, uint32_t expected_version, uint32_t *request_id)
{
    remote_job_t job;
    alarm_remote_submit_t result;
    if (index >= ALARM_COUNT || entry == NULL || request_id == NULL) return ALARM_REMOTE_INVALID;
    job.entry = *entry;
    if (!alarm_entry_valid(&job.entry)) return ALARM_REMOTE_INVALID;
    job.selected = index;
    job.fields_mask = fields_mask;
    job.expected_version = expected_version;
    taskENTER_CRITICAL();
    if (!initialized) result = ALARM_REMOTE_UNAVAILABLE;
    else if (published.pending_id != 0U || published.snapshot.saving
             || published.snapshot.local_editing) result = ALARM_REMOTE_BUSY;
    else if (expected_version != published.snapshot.state_version) result = ALARM_REMOTE_CONFLICT;
    else {
        job.request_id = published.next_id + 1U;
        if (job.request_id == 0U) job.request_id = 1U;
        /* One reservation covers both a queued request and its EEPROM job. */
        if (xQueueSend(remote_queue, &job, 0U) != pdTRUE) result = ALARM_REMOTE_BUSY;
        else {
            published.next_id = job.request_id;
            published.pending_id = job.request_id;
            *request_id = job.request_id;
            result = ALARM_REMOTE_ACCEPTED;
        }
    }
    taskEXIT_CRITICAL();
    return result;
}

alarm_remote_submit_t alarm_service_remote_submit(uint8_t index, const alarm_entry_t *entry,
                                                  uint32_t expected_version, uint32_t *request_id)
{
    return remote_submit(index, entry, 0U, expected_version, request_id);
}

alarm_remote_result_t alarm_service_remote_result(uint32_t request_id)
{
    alarm_remote_result_t result = ALARM_REMOTE_UNKNOWN;
    unsigned i;
    taskENTER_CRITICAL();
    if (initialized && request_id != 0U) {
        if (request_id == published.pending_id) result = ALARM_REMOTE_PENDING;
        else for (i = 0; i < REMOTE_RESULT_COUNT; ++i) {
            if (request_id == published.completed[i].id) {
                result = published.completed[i].result;
                break;
            }
        }
    }
    taskEXIT_CRITICAL();
    return result;
}

alarm_remote_submit_t alarm_service_remote_submit_patch(uint8_t index, const alarm_entry_t *entry,
    uint16_t fields_mask, uint32_t expected_version, uint32_t *request_id)
{
    if (fields_mask == 0U || fields_mask > 0x01ffU) return ALARM_REMOTE_INVALID;
    return remote_submit(index, entry, fields_mask, expected_version, request_id);
}

static void complete_remote(uint32_t request_id, alarm_remote_result_t result)
{
    if (request_id == 0U) return;
    /* A caller observing SAVED must already be able to read the new config. */
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

static bool read_eeprom(void *context, uint32_t address, void *data, size_t length)
{
    (void)context;
    return bl24c512_read(address, data, length);
}

static bool write_eeprom(void *context, uint32_t address, const void *data, size_t length)
{
    (void)context;
    return bl24c512_write(address, data, length);
}

static bool trusted_time(const rtc_date_time_t *date)
{
    uint32_t seconds;
    return clock_rtc_ready() && date->year >= 2020U && alarm_calendar_to_seconds(date, &seconds);
}

bool alarm_service_display_begin(void)
{
    bool busy;
    if (display_mutex == NULL) return true;
    if (xSemaphoreTake(display_mutex, portMAX_DELAY) != pdTRUE) return false;
    taskENTER_CRITICAL();
    busy = owns_display;
    taskEXIT_CRITICAL();
    if (busy) {
        (void)xSemaphoreGive(display_mutex);
        return false;
    }
    return true;
}

void alarm_service_display_end(void)
{
    if (display_mutex != NULL) (void)xSemaphoreGive(display_mutex);
}

static void merge_edited_entry(alarm_entry_t *durable, const save_job_t *job)
{
    /* A toggle based on unavailable/stale boot data must not replace fields
     * the user never edited, even within the selected alarm. */
    /* Explicit cloud fields carry intent even when equal to the RAM base.
     * Other callers retain their existing edit-difference semantics. */
#define MERGE_FIELD(field, bit) do { \
    if (job->fields_mask != 0U ? (job->fields_mask & (1U << (bit))) != 0U \
        : job->entry.field != job->base.field) durable->field = job->entry.field; \
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

static void save_worker(void *argument)
{
    save_job_t job;
    save_result_t result;
    (void)argument;
    for (;;) {
        if (xQueueReceive(save_queue, &job, portMAX_DELAY) == pdTRUE) {
            memset(&result, 0, sizeof result);
            /* A failed boot read can leave RAM at defaults or at an older slot.
             * Reconcile only the user's edited entry onto a freshly verified
             * durable snapshot. An unread slot may contain a newer generation. */
            result.result = alarm_store_load_for_update(&store, &result.config);
            if (result.result == ALARM_STORE_EMPTY || result.result == ALARM_STORE_CORRUPT) {
                alarm_config_defaults(&result.config);
            }
            if (result.result == ALARM_STORE_OK || result.result == ALARM_STORE_EMPTY
                || result.result == ALARM_STORE_CORRUPT) {
                if (job.selected < ALARM_COUNT && alarm_entry_valid(&job.entry)
                    && job.fields_mask <= 0x01ffU) {
                    merge_edited_entry(&result.config.entries[job.selected], &job);
                    result.result = alarm_config_valid(&result.config)
                        ? alarm_store_save(&store, &result.config) : ALARM_STORE_INVALID;
                } else result.result = ALARM_STORE_INVALID;
            }
            result.generation = store.generation;
            /* One outstanding job means a result slot is always available. */
            (void)xQueueSend(result_queue, &result, portMAX_DELAY);
        }
    }
}

static void view_worker(void *argument)
{
    view_snapshot_t snapshot;
    uint32_t last_epoch = UINT32_MAX;
    (void)argument;
    for (;;) {
        bool visible;
        uint32_t epoch;
        alarm_page_model_t model;
        if (xQueueReceive(view_queue, &snapshot, portMAX_DELAY) != pdTRUE) continue;
        if (xSemaphoreTake(display_mutex, portMAX_DELAY) != pdTRUE) continue;
        taskENTER_CRITICAL();
        visible = owns_display;
        epoch = display_epoch;
        taskEXIT_CRITICAL();
        if (!visible || snapshot.display_epoch != epoch) {
            /* A queued menu cannot repaint after normal ownership is restored. */
            alarm_page_invalidate();
            last_epoch = UINT32_MAX;
            (void)xSemaphoreGive(display_mutex);
            continue;
        }
        if (last_epoch != epoch) alarm_page_invalidate();
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

static void collect_save_result(void)
{
    save_result_t result;
    if (xQueueReceive(result_queue, &result, 0U) != pdTRUE) return;
    if (!owner.saving) return;
    owner.saving = false;
    if (result.result == ALARM_STORE_OK && alarm_engine_apply(&owner.engine, &result.config)) {
        owner.storage_ok = true;
        owner.revision = result.generation;
        ++owner.state_version;
        if (owner.state_version == 0U) owner.state_version = 1U;
        owner.message = "Saved";
        if (owner.save_request_id == 0U) alarm_menu_saved(&owner.menu, true);
        complete_remote(owner.save_request_id, ALARM_REMOTE_SAVED);
        clock_log("Alarm configuration saved, generation %lu\r\n", (unsigned long)owner.revision);
    } else {
        owner.storage_ok = false;
        owner.message = "Save failed";
        if (owner.save_request_id == 0U) alarm_menu_saved(&owner.menu, false);
        complete_remote(owner.save_request_id, ALARM_REMOTE_FAILED);
        clock_log("Alarm save failed (%u); running configuration retained\r\n", (unsigned)result.result);
    }
    owner.save_request_id = 0U;
    owner.dirty = true;
}

static void request_save(void)
{
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

static void collect_remote_request(void)
{
    remote_job_t request;
    save_job_t job;
    if (xQueueReceive(remote_queue, &request, 0U) != pdTRUE) return;
    /* Keys run first: a physical draft/save created since submission wins.
     * Rechecking the version here also closes the publish/enqueue race. */
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

static void set_clock(void)
{
    uint32_t requested, readback;
    rtc_date_time_t date, actual;
    if (clock_rtc_ready() && owner.menu.clock_draft.year >= 2020U
        && alarm_calendar_to_seconds(&owner.menu.clock_draft, &requested)
        && alarm_seconds_to_calendar(requested, &date)) {
        rtc_set_time(&date);
        rtc_get_time(&actual);
        if (trusted_time(&actual) && alarm_calendar_to_seconds(&actual, &readback)
            && ((requested >= readback && requested - readback <= 2U)
                || (readback > requested && readback - requested <= 2U))) {
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

static void handle_input(uint32_t keys, uint32_t now_ms)
{
    alarm_input_t input;
    alarm_action_t action;
    bool active;
    uint8_t stopped;
    if (keys == 0U) return;
    /* Confirm wins simultaneous presses, keeping stop deterministic. */
    if (keys & BOARD_KEY1_LONG) input = ALARM_INPUT_CONFIRM_LONG;
    else if (keys & BOARD_KEY1_SHORT) input = ALARM_INPUT_CONFIRM;
    else if (keys & BOARD_KEY2_LONG) input = ALARM_INPUT_PLUS_LONG;
    else if (keys & BOARD_KEY2_SHORT) input = ALARM_INPUT_PLUS;
    else if (keys & BOARD_KEY3_LONG) input = ALARM_INPUT_MINUS_LONG;
    else if (keys & BOARD_KEY3_SHORT) input = ALARM_INPUT_MINUS;
    else return;
    active = owner.engine.ringing_mask != 0U || owner.engine.snoozed_mask != 0U
        || owner.test_pending || owner.test_ringing;
    action = alarm_menu_input(&owner.menu, input, &owner.engine.config, &owner.now, active, owner.saving);
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

static void poll_engine(uint32_t now_ms)
{
    alarm_events_t events;
    bool was_valid;
    if (now_ms - owner.last_engine_ms < 100U) return;
    owner.last_engine_ms = now_ms;
    was_valid = owner.time_valid;
    rtc_get_time(&owner.now);
    owner.time_valid = trusted_time(&owner.now);
    if (was_valid != owner.time_valid) {
        owner.dirty = true;
        if (!owner.time_valid) clock_log_line("Alarm scheduled triggers inhibited: RTC unavailable");
    }
    events = alarm_engine_step(&owner.engine, &owner.now, owner.time_valid, now_ms);
    if (events.started || events.stopped) {
        owner.dirty = true;
        if (events.started) clock_log("Alarm started mask %02x\r\n", (unsigned)events.started);
        if (events.stopped) clock_log("Alarm duration stopped mask %02x\r\n", (unsigned)events.stopped);
    }
}

static void poll_diagnostic(uint32_t now_ms)
{
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

static void update_buzzer(uint32_t now_ms)
{
    bool active = owner.engine.ringing_mask != 0U || owner.test_ringing;
    bool on;
    if (active && !owner.beep_active) owner.beep_started_ms = now_ms;
    owner.beep_active = active;
    on = active && (((now_ms - owner.beep_started_ms) / 500U) % 2U == 0U);
    if (on != owner.buzzer_on) {
        owner.buzzer_on = on;
        buzzer_set(on);
    }
}

static void publish_view(uint32_t now_ms)
{
    view_snapshot_t snapshot;
    bool desired = owner.menu.view != ALARM_VIEW_MAIN || owner.engine.ringing_mask != 0U
        || owner.engine.snoozed_mask != 0U || owner.test_pending || owner.test_ringing;
    bool released = false;
    taskENTER_CRITICAL();
    if (owns_display != desired) {
        released = !desired;
        owns_display = desired;
        ++display_epoch;
        owner.dirty = true;
    }
    taskEXIT_CRITICAL();
    if (released) app_request_main_redraw();
    if (!owner.dirty && (!desired || now_ms - owner.last_view_ms < 1000U)) return;
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

static void alarm_task(void *argument)
{
    TickType_t wake = xTaskGetTickCount();
    (void)argument;
    for (;;) {
        uint32_t now_ms = (uint32_t)xTaskGetTickCount();
        collect_save_result();
        poll_engine(now_ms);
        poll_diagnostic(now_ms);
        handle_input(board_keys_poll(now_ms), now_ms);
        collect_remote_request();
        update_buzzer(now_ms);
        publish_view(now_ms);
        publish_service_snapshot();
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(10U));
    }
}

static void cleanup_resources(void)
{
    if (remote_queue != NULL) vQueueDelete(remote_queue);
    if (view_queue != NULL) vQueueDelete(view_queue);
    if (result_queue != NULL) vQueueDelete(result_queue);
    if (save_queue != NULL) vQueueDelete(save_queue);
    if (display_mutex != NULL) vSemaphoreDelete(display_mutex);
    view_queue = NULL;
    result_queue = NULL;
    save_queue = NULL;
    display_mutex = NULL;
    remote_queue = NULL;
}

void alarm_service_init(void)
{
    alarm_config_t config;
    alarm_store_result_t loaded;
    alarm_store_io_t io = {read_eeprom, write_eeprom, NULL};
    TaskHandle_t store_task = NULL, view_task = NULL;
    uint32_t now_ms;
    if (initialized) return;
    memset(&owner, 0, sizeof owner);
    memset(&published, 0, sizeof published);
    owns_display = false;
    display_epoch = 0U;
    buzzer_init();
    board_keys_init();
    alarm_config_defaults(&config);
    alarm_store_init(&store, io);
    loaded = alarm_store_load(&store, &config);
    owner.storage_ok = loaded == ALARM_STORE_OK;
    if (!owner.storage_ok) alarm_config_defaults(&config);
    if (loaded == ALARM_STORE_OK) owner.message = "Loaded";
    else if (loaded == ALARM_STORE_EMPTY) owner.message = "No saved alarms";
    else if (loaded == ALARM_STORE_CORRUPT) owner.message = "Storage invalid";
    else owner.message = "EEPROM unavailable";
    owner.revision = store.generation;
    owner.state_version = owner.revision + 1U;
    if (owner.state_version == 0U) owner.state_version = 1U;
    alarm_engine_init(&owner.engine, &config);
    alarm_menu_init(&owner.menu);
    rtc_get_time(&owner.now);
    owner.time_valid = trusted_time(&owner.now);
    if (!owner.time_valid) clock_log_line("Alarm scheduled triggers inhibited: RTC unavailable");
    clock_log("Alarm storage boot result %u, generation %lu\r\n", (unsigned)loaded, (unsigned long)owner.revision);
    display_mutex = xSemaphoreCreateMutex();
    save_queue = xQueueCreate(1U, (UBaseType_t)sizeof(save_job_t));
    result_queue = xQueueCreate(1U, (UBaseType_t)sizeof(save_result_t));
    view_queue = xQueueCreate(1U, (UBaseType_t)sizeof(view_snapshot_t));
    remote_queue = xQueueCreate(1U, (UBaseType_t)sizeof(remote_job_t));
    if (display_mutex == NULL || save_queue == NULL || result_queue == NULL || view_queue == NULL
        || remote_queue == NULL) {
        clock_log_line("Alarm service unavailable: queue/mutex allocation failed");
        cleanup_resources();
        return;
    }
    now_ms = (uint32_t)xTaskGetTickCount();
    owner.last_engine_ms = now_ms - 100U;
    owner.last_view_ms = now_ms;
    owner.dirty = true;
    publish_service_snapshot();
    if (xTaskCreate(save_worker, "alarm-store", 1024U, NULL, 2U, &store_task) != pdPASS) goto failed;
    if (xTaskCreate(view_worker, "alarm-view", 1024U, NULL, 4U, &view_task) != pdPASS) goto failed;
    if (xTaskCreate(alarm_task, "alarm", 1536U, NULL, 9U, NULL) != pdPASS) goto failed;
    taskENTER_CRITICAL();
    initialized = true;
    taskEXIT_CRITICAL();
    return;
failed:
    if (view_task != NULL) vTaskDelete(view_task);
    if (store_task != NULL) vTaskDelete(store_task);
    clock_log_line("Alarm service unavailable: task allocation failed");
    cleanup_resources();
}
