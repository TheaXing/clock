/* Execute the registered production tasks. Device/RTOS boundaries are faked;
 * application decisions, WiFi workflow and weather parsing remain real. */
#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "app.h"
#include "wifi.h"
#include "weather.h"
#include "clock_rtc.h"
#include "aht20.h"
#include "esp_at.h"
#include "page.h"
#include "workqueue.h"

enum { TASK_CLOCK, TASK_SENSOR, TASK_NETWORK, TASK_COUNT };
typedef struct { TaskFunction_t function; void *parameter; } registered_task_t;
static registered_task_t tasks[TASK_COUNT];
static unsigned created_tasks;
static int running_task;
static jmp_buf task_stopped;
static TickType_t tick, start_tick;
static uint32_t run_elapsed, stop_after;

typedef struct
{
    unsigned esp_init_failures, esp_init_calls, wifi_init_calls, sntp_init_calls;
    unsigned connect_calls, query_calls, sync_calls, http_calls, rtc_writes;
    unsigned sync_failures, bad_http_request;
    bool radio_connected, outage_enabled;
    TickType_t sync_ticks[8], http_ticks[128];
    rtc_date_time_t written_date;
    unsigned time_outputs, date_outputs, heartbeat_jobs;
    unsigned sensor_init_calls, sensor_start_calls, sensor_wait_calls, sensor_read_calls;
    unsigned sensor_diagnostic_calls;
    float indoor_temperature[8], indoor_humidity[8];
    unsigned indoor_temperature_count, indoor_humidity_count;
    unsigned city_outputs, outdoor_temperature_outputs, weather_icon_outputs;
    char city[32], ssid[64];
    unsigned connected_ssid_outputs, lost_ssid_outputs;
} observations_t;
static observations_t observed;
static bool display_blocked;
static unsigned main_redraws;
bool alarm_service_display_begin(void) {return !display_blocked;}
void alarm_service_display_end(void) {}
void main_page_display(void) {++main_redraws;}
int app_test_printf(const char *format, ...) { (void)format; return 0; }
int app_test_puts(const char *message) { (void)message; return 0; }
int clock_log(const char *format, ...) { (void)format; return 0; }
int clock_log_line(const char *message) { (void)message; return 0; }

static void reset_scenario(void)
{
    memset(&observed, 0, sizeof(observed));
    wifi_mark_unready();
}

static void run_task(unsigned task, TickType_t first_tick, uint32_t duration)
{
    assert(task < TASK_COUNT && tasks[task].function != NULL);
    running_task = (int)task;
    tick = start_tick = first_tick;
    run_elapsed = 0;
    stop_after = duration;
    if (setjmp(task_stopped) == 0)
        tasks[task].function(tasks[task].parameter);
    running_task = -1;
}

BaseType_t xTaskCreate(TaskFunction_t function, const char *name, uint16_t depth,
                       void *parameter, UBaseType_t priority, TaskHandle_t *handle)
{
    (void)depth; (void)priority;
    unsigned index;
    if (strcmp(name, "clock") == 0) index = TASK_CLOCK;
    else if (strcmp(name, "sensor") == 0) index = TASK_SENSOR;
    else { assert(strcmp(name, "network") == 0); index = TASK_NETWORK; }
    assert(tasks[index].function == NULL);
    tasks[index].function = function;
    tasks[index].parameter = parameter;
    if (handle) *handle = &tasks[index];
    ++created_tasks;
    return pdPASS;
}

TickType_t xTaskGetTickCount(void) { return tick; }
static void advance(TickType_t ticks)
{
    tick += ticks;
    run_elapsed += ticks;
    if (run_elapsed >= stop_after) longjmp(task_stopped, 1);
}
void vTaskDelay(TickType_t ticks)
{
    assert(running_task == TASK_NETWORK && ticks == 1000);
    advance(ticks);
}
void vTaskDelayUntil(TickType_t *previous, TickType_t increment)
{
    assert((running_task == TASK_CLOCK && increment == 1000) ||
           (running_task == TASK_SENSOR && increment == 3000));
    TickType_t next = *previous + increment;
    *previous = next;
    advance(next - tick);
}
size_t xPortGetFreeHeapSize(void) { return 32000; }
size_t xPortGetMinimumEverFreeHeapSize(void) { return 24000; }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task)
{
    assert(task != NULL);
    return 512;
}
bool workqueue_try_run(work_t work, void *parameter)
{
    assert(running_task == TASK_CLOCK);
    ++observed.heartbeat_jobs;
    work(parameter);
    return true;
}

void rtc_get_time(rtc_date_time_t *date)
{
    assert(running_task == TASK_CLOCK);
    *date = (rtc_date_time_t){2026, 11, 13, 12, 34, (uint8_t)((tick - start_tick) / 1000 % 60), 5};
}
bool clock_rtc_ready(void) { return true; }
void rtc_set_time(const rtc_date_time_t *date)
{
    assert(running_task == TASK_NETWORK);
    observed.written_date = *date;
    ++observed.rtc_writes;
}
bool aht20_init(void)
{
    assert(running_task == TASK_SENSOR);
    return ++observed.sensor_init_calls > 2;
}
bool aht20_start_measurement(void)
{
    ++observed.sensor_start_calls;
    return true;
}
bool aht20_wait_for_measurement(void)
{
    ++observed.sensor_wait_calls;
    return true;
}
bool aht20_read_measurement(float *temperature, float *humidity)
{
    if (++observed.sensor_read_calls == 2) return false;
    *temperature = 22.25f;
    *humidity = 55.5f;
    return true;
}

aht20_diagnostics_t aht20_get_diagnostics(void)
{
    assert(running_task == TASK_SENSOR);
    ++observed.sensor_diagnostic_calls;
    aht20_diagnostics_t diagnostic = {0};
    diagnostic.stage = AHT20_STAGE_INITIAL_STATUS;
    diagnostic.error = AHT20_ERROR_RECEIVE;
    diagnostic.hal_status = 1;
    diagnostic.hal_error = 4;
    return diagnostic;
}

bool esp_at_init(void)
{
    return ++observed.esp_init_calls > observed.esp_init_failures;
}
bool esp_at_wifi_init(void) { ++observed.wifi_init_calls; return true; }
bool esp_at_sntp_init(void) { ++observed.sntp_init_calls; return true; }
static bool outage(void)
{
    uint32_t elapsed = tick - start_tick;
    return observed.outage_enabled && elapsed >= 30000 && elapsed < 40000;
}
bool esp_at_get_wifi_info(esp_wifi_info_t *info)
{
    ++observed.query_calls;
    *info = (esp_wifi_info_t){0};
    info->connected = observed.radio_connected && !outage();
    strcpy(info->ssid, "fixture-ap");
    strcpy(info->bssid, "00:11:22:33:44:55");
    info->channel = 6;
    info->rssi = -42;
    return true;
}
bool esp_at_connect_wifi(const char *ssid, const char *password, const char *mac)
{
    assert(strcmp(ssid, "fixture-ap") == 0);
    assert(strcmp(password, "fixture-password") == 0 && mac == NULL);
    ++observed.connect_calls;
    if (outage()) return false;
    observed.radio_connected = true;
    return true;
}
bool esp_at_sntp_get_time(esp_date_time_t *date)
{
    assert(observed.sync_calls < sizeof(observed.sync_ticks) / sizeof(observed.sync_ticks[0]));
    observed.sync_ticks[observed.sync_calls++] = tick;
    if (observed.sync_calls <= observed.sync_failures) return false;
    *date = (esp_date_time_t){2026, 11, 13, 15, 16, 17, 5};
    return true;
}
const char *esp_at_http_get(const char *url)
{
    assert(strstr(url, "key=fixture-key&location=fixture-location&language=en&unit=c") != NULL);
    assert(observed.http_calls < sizeof(observed.http_ticks) / sizeof(observed.http_ticks[0]));
    observed.http_ticks[observed.http_calls++] = tick;
    if (observed.http_calls == observed.bad_http_request) return "{truncated";
    return "{\"results\":[{\"location\":{\"name\":\"Nanjing\",\"path\":\"Nanjing,Jiangsu,China\"},"
           "\"now\":{\"text\":\"Cloudy\",\"code\":\"4\",\"temperature\":\"23.5\"}}]}";
}

void main_page_redraw_time(rtc_date_time_t *date)
{
    assert(running_task == TASK_CLOCK && date->year == 2026 && date->hour == 12);
    assert(date->second == (tick - start_tick) / 1000 % 60);
    ++observed.time_outputs;
}
void main_page_redraw_date(rtc_date_time_t *date)
{
    assert(running_task == TASK_CLOCK && date->month == 11 && date->day == 13 && date->weekday == 5);
    ++observed.date_outputs;
}
void main_page_redraw_inner_temperature(float temperature)
{
    assert((running_task == TASK_SENSOR || running_task == TASK_CLOCK) && observed.indoor_temperature_count < 8);
    observed.indoor_temperature[observed.indoor_temperature_count++] = temperature;
}
void main_page_redraw_inner_humidity(float humidity)
{
    assert((running_task == TASK_SENSOR || running_task == TASK_CLOCK) && observed.indoor_humidity_count < 8);
    observed.indoor_humidity[observed.indoor_humidity_count++] = humidity;
}
void main_page_redraw_wifi_ssid(const char *ssid)
{
    assert(running_task == TASK_NETWORK || running_task == TASK_CLOCK);
    assert(strlen(ssid) < sizeof(observed.ssid));
    strcpy(observed.ssid, ssid);
    if (strcmp(ssid, "fixture-ap") == 0) ++observed.connected_ssid_outputs;
    else { assert(strcmp(ssid, "wifi lost") == 0); ++observed.lost_ssid_outputs; }
}
void main_page_redraw_outdoor_city(const char *city)
{
    assert(strcmp(city, "Nanjing") == 0);
    strcpy(observed.city, city);
    ++observed.city_outputs;
}
void main_page_redraw_outdoor_temperature(float temperature)
{
    assert(temperature == 23.5f);
    ++observed.outdoor_temperature_outputs;
}
void main_page_redraw_outdoor_weather_icon(const int code)
{
    assert(code == 4);
    ++observed.weather_icon_outputs;
}

static void test_local_tasks_continue_without_esp(void)
{
    reset_scenario();
    observed.esp_init_failures = 100;
    assert(!wifi_init() && !wifi_ready());
    run_task(TASK_CLOCK, 0, 11000);
    assert(observed.time_outputs == 11 && observed.date_outputs == 11);
    assert(observed.heartbeat_jobs == 1 && observed.esp_init_calls == 1);
    run_task(TASK_SENSOR, 0, 12000);
    assert(observed.sensor_init_calls == 4 && observed.sensor_read_calls == 3);
    assert(observed.sensor_start_calls == 3 && observed.sensor_wait_calls == 3);
    assert(observed.indoor_temperature_count == 4 && observed.indoor_humidity_count == 4);
    assert(isnan(observed.indoor_temperature[0]) && isnan(observed.indoor_humidity[0]));
    assert(observed.indoor_temperature[1] == 22.25f && observed.indoor_humidity[1] == 55.5f);
    assert(isnan(observed.indoor_temperature[2]) && isnan(observed.indoor_humidity[2]));
    assert(observed.indoor_temperature[3] == 22.25f && observed.indoor_humidity[3] == 55.5f);
    assert(observed.sensor_diagnostic_calls == 2);
    assert(observed.esp_init_calls == 1 && !wifi_ready());
}

static void test_network_retries_and_publishes_parsed_weather(void)
{
    reset_scenario();
    observed.esp_init_failures = 2;
    observed.sync_failures = 1;
    run_task(TASK_NETWORK, 0, 75000);
    assert(wifi_ready() && observed.esp_init_calls == 3);
    assert(observed.wifi_init_calls == 1 && observed.sntp_init_calls == 1 && observed.connect_calls == 1);
    assert(observed.sync_calls == 2 && observed.sync_ticks[0] == 10000 && observed.sync_ticks[1] == 11000);
    assert(observed.rtc_writes == 1 && observed.written_date.year == 2026);
    assert(observed.written_date.hour == 15 && observed.written_date.minute == 16 && observed.written_date.second == 17);
    assert(observed.http_calls == 2 && observed.http_ticks[0] == 10000 && observed.http_ticks[1] == 70000);
    assert(observed.city_outputs == 2 && observed.outdoor_temperature_outputs == 2 && observed.weather_icon_outputs == 2);
    assert(strcmp(observed.city, "Nanjing") == 0 && observed.connected_ssid_outputs == 1);
    assert(observed.lost_ssid_outputs >= 1);
}

static void test_reconnect_refreshes_time_and_weather(void)
{
    reset_scenario();
    observed.radio_connected = true;
    observed.outage_enabled = true;
    run_task(TASK_NETWORK, 0, 101000);
    assert(observed.connect_calls == 2);
    assert(observed.connected_ssid_outputs == 2 && observed.lost_ssid_outputs >= 1);
    assert(observed.sync_calls == 2 && observed.sync_ticks[0] == 0 && observed.sync_ticks[1] == 40000);
    assert(observed.rtc_writes == 2);
    assert(observed.http_calls == 3 && observed.http_ticks[0] == 0);
    assert(observed.http_ticks[1] == 40000 && observed.http_ticks[2] == 100000);
    assert(observed.city_outputs == 3 && observed.weather_icon_outputs == 3);
}

static void test_hourly_sync_does_not_repeat_every_loop(void)
{
    reset_scenario();
    observed.radio_connected = true;
    run_task(TASK_NETWORK, 0, 3661000);
    assert(observed.sync_calls == 2 && observed.rtc_writes == 2);
    assert(observed.sync_ticks[0] == 0 && observed.sync_ticks[1] == 3600000);
    assert(observed.http_calls == 62 && observed.city_outputs == 62);
    assert(observed.http_ticks[0] == 0 && observed.http_ticks[1] == 60000);
    assert(observed.http_ticks[60] == 3600000 && observed.http_ticks[61] == 3660000);
}

static void test_weather_cadence_survives_wrap_and_bad_response(void)
{
    reset_scenario();
    observed.radio_connected = true;
    observed.bad_http_request = 2;
    run_task(TASK_NETWORK, UINT32_MAX - 30000, 121000);
    assert(observed.sync_calls == 1 && observed.rtc_writes == 1);
    assert(observed.http_calls == 3);
    assert(observed.http_ticks[0] == UINT32_MAX - 30000);
    assert(observed.http_ticks[1] == 29999 && observed.http_ticks[2] == 89999);
    assert(observed.city_outputs == 2 && observed.outdoor_temperature_outputs == 2 && observed.weather_icon_outputs == 2);
    assert(observed.connected_ssid_outputs == 1 && observed.lost_ssid_outputs == 0);
}

static void test_menu_does_not_stop_sampling_and_restores_latest_values(void)
{
    reset_scenario();
    display_blocked=true;
    run_task(TASK_SENSOR,0,12000);
    assert(observed.sensor_read_calls==3);
    assert(observed.indoor_temperature_count==0 && observed.indoor_humidity_count==0);
    display_blocked=false;
    app_request_main_redraw();
    run_task(TASK_CLOCK,0,1000);
    assert(main_redraws==1);
    assert(observed.indoor_temperature_count==1 && observed.indoor_temperature[0]==22.25f);
    assert(observed.indoor_humidity_count==1 && observed.indoor_humidity[0]==55.5f);
}

int main(void)
{
    app_init();
    assert(created_tasks == TASK_COUNT);
    test_local_tasks_continue_without_esp();
    test_network_retries_and_publishes_parsed_weather();
    test_reconnect_refreshes_time_and_weather();
    test_hourly_sync_does_not_repeat_every_loop();
    test_weather_cadence_survives_wrap_and_bad_response();
    test_menu_does_not_stop_sampling_and_restores_latest_values();
    fprintf(stdout, "app tasks: 6 behavior scenarios passed\n");
    return 0;
}
