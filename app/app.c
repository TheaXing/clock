#include "clock_console.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "app.h"
#include "clock_config.h"
#include "clock_rtc.h"
#include "aht20.h"
#include "esp_at.h"
#include "page.h"
#include "weather.h"
#include "wifi.h"
#include "workqueue.h"
#include "alarm_service.h"
#include "clock_web.h"
#include "onenet_service.h"

static TaskHandle_t clock_task, sensor_task, network_task;
typedef struct {
    float temperature, humidity;
    weather_info_t weather;
    char ssid[64];
    bool indoor_valid, weather_valid, ssid_valid;
} main_values_t;
static main_values_t latest;
static bool main_redraw_requested;
static bool cloud_time_ready;
static void network_background(void){onenet_service_poll((uint32_t)xTaskGetTickCount());}

void app_request_main_redraw(void)
{
    taskENTER_CRITICAL();
    main_redraw_requested=true;
    taskEXIT_CRITICAL();
}

static void restore_main(const rtc_date_time_t *date)
{
    main_values_t values;
    taskENTER_CRITICAL();values=latest;taskEXIT_CRITICAL();
    main_page_display();
    if(values.ssid_valid)main_page_redraw_wifi_ssid(values.ssid);
    else main_page_redraw_wifi_ssid("connecting");
    if(date->year>=2020 && date->year<=2099) {
        rtc_date_time_t copy=*date;
        main_page_redraw_time(&copy);main_page_redraw_date(&copy);
    }
    if(values.indoor_valid) {
        main_page_redraw_inner_temperature(values.temperature);
        main_page_redraw_inner_humidity(values.humidity);
    }
    if(values.weather_valid) {
        main_page_redraw_outdoor_city(values.weather.city);
        main_page_redraw_outdoor_temperature(values.weather.temperature);
        main_page_redraw_outdoor_weather_icon(values.weather.weather_code);
    }
}

static void publish_indoor(float temperature,float humidity)
{
    onenet_service_sensor(temperature,humidity);
    taskENTER_CRITICAL();
    latest.temperature=temperature;latest.humidity=humidity;latest.indoor_valid=true;
    taskEXIT_CRITICAL();
    if(alarm_service_display_begin()) {
        main_page_redraw_inner_temperature(temperature);
        main_page_redraw_inner_humidity(humidity);
        alarm_service_display_end();
    }
}

static void publish_ssid(const char *ssid)
{
    taskENTER_CRITICAL();
    strncpy(latest.ssid,ssid,sizeof(latest.ssid)-1);
    latest.ssid[sizeof(latest.ssid)-1]='\0';latest.ssid_valid=true;
    taskEXIT_CRITICAL();
    if(alarm_service_display_begin()) {
        main_page_redraw_wifi_ssid(ssid);alarm_service_display_end();
    }
}

static bool elapsed(TickType_t now, TickType_t last, uint32_t milliseconds)
{
    return (TickType_t)(now - last) >= pdMS_TO_TICKS(milliseconds);
}

static void heartbeat(void *unused)
{
    (void)unused;
    clock_log("Workqueue running; heap=%lu min=%lu; stack clock/sensor/net=%lu/%lu/%lu words\r\n",
           (unsigned long)xPortGetFreeHeapSize(),
           (unsigned long)xPortGetMinimumEverFreeHeapSize(),
           (unsigned long)uxTaskGetStackHighWaterMark(clock_task),
           (unsigned long)uxTaskGetStackHighWaterMark(sensor_task),
           (unsigned long)uxTaskGetStackHighWaterMark(network_task));
}

static void clock_loop(void *unused)
{
    (void)unused;
    TickType_t wake = xTaskGetTickCount();
    uint32_t seconds = 0;
    for (;;)
    {
        rtc_date_time_t date = {0};
        rtc_get_time(&date);
        if(alarm_service_display_begin()) {
            bool redraw;
            taskENTER_CRITICAL();redraw=main_redraw_requested;main_redraw_requested=false;taskEXIT_CRITICAL();
            if(redraw)restore_main(&date);
            else if(date.year>=2020 && date.year<=2099) {
                main_page_redraw_time(&date);
                main_page_redraw_date(&date);
            }
            alarm_service_display_end();
        }
        if (++seconds % 10U == 0U) (void)workqueue_try_run(heartbeat, NULL);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(1000));
    }
}

static void sensor_loop(void *unused)
{
    (void)unused;
    bool ready = aht20_init();
    TickType_t wake = xTaskGetTickCount();
    for (;;)
    {
        float temperature = 0, humidity = 0;
        if (!ready) ready = aht20_init();
        if (ready && aht20_start_measurement() && aht20_wait_for_measurement() &&
            aht20_read_measurement(&temperature, &humidity))
        {
            publish_indoor(temperature,humidity);
            clock_log("[AHT20] T=%.1f C RH=%.1f %%\r\n", (double)temperature, (double)humidity);
        }
        else
        {
            ready = false;
            static const char *const stage_names[] = {
                "none", "initial-status", "calibration", "calibration-status",
                "trigger", "wait-status", "data"
            };
            aht20_diagnostics_t diagnostic = aht20_get_diagnostics();
            unsigned stage = (unsigned)diagnostic.stage;
            const char *name = stage < sizeof(stage_names) / sizeof(stage_names[0])
                             ? stage_names[stage] : "unknown";
            clock_log("[AHT20] failed stage=%s reason=%u HAL=%lu I2C=0x%08lX status_valid=%u status=0x%02X; retry in 3 seconds\r\n",
                      name, (unsigned)diagnostic.error, (unsigned long)diagnostic.hal_status,
                      (unsigned long)diagnostic.hal_error, (unsigned)diagnostic.status_valid,
                      (unsigned)diagnostic.status);
            publish_indoor(NAN,NAN);
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(CLOCK_SENSOR_INTERVAL_MS));
    }
}

static bool synchronize(void)
{
    esp_date_time_t time = {0};
    if (!esp_at_sntp_get_time(&time))
    {
        clock_log_line("[SNTP] Time not ready; retry pending");
        return false;
    }
    cloud_time_ready=time.year>=2020U;
    if (!clock_rtc_ready()) return false;
    rtc_date_time_t date = {0};
    date.year = time.year; date.month = time.month; date.day = time.day;
    date.hour = time.hour; date.minute = time.minute; date.second = time.second;
    date.weekday = time.weekday;
    rtc_set_time(&date);
    if (!clock_rtc_ready()) return false;
    clock_log("[SNTP] %04u-%02u-%02u %02u:%02u:%02u UTC+8\r\n",
           date.year, date.month, date.day, date.hour, date.minute, date.second);
    return true;
}

static void update_weather(void)
{
    if (!CLOCK_WEATHER_KEY[0]) return;
    char url[320];
    int n = snprintf(url, sizeof(url),
        "https://api.seniverse.com/v3/weather/now.json?key=%s&location=%s&language=en&unit=c",
        CLOCK_WEATHER_KEY, CLOCK_WEATHER_LOCATION);
    if (n < 0 || (size_t)n >= sizeof(url))
    {
        clock_log_line("[WEATHER] Configuration is too long");
        return;
    }
    const char *response = onenet_service_configured()?esp_at_http_get_bounded(url,1000U):esp_at_http_get(url);
    weather_info_t weather = {0};
    if (!response || !parse_seniverse_response(response, &weather))
    {
        clock_log_line("[WEATHER] Request/parse failed; keeping previous weather");
        return;
    }
    taskENTER_CRITICAL();latest.weather=weather;latest.weather_valid=true;taskEXIT_CRITICAL();
    if(alarm_service_display_begin()) {
        main_page_redraw_outdoor_city(weather.city);
        main_page_redraw_outdoor_temperature(weather.temperature);
        main_page_redraw_outdoor_weather_icon(weather.weather_code);
        alarm_service_display_end();
    }
    clock_log("[WEATHER] %s, %s, %.1f C\r\n", weather.city, weather.weather, weather.temperature);
}

/* All AT transactions belong to this worker after the startup task finishes. */
static void network_loop(void *unused)
{
    (void)unused;
    bool connected = false, synced = false, first_weather = true;
    char last_ssid[64] = {0};
    TickType_t now = xTaskGetTickCount();
    TickType_t last_wifi = now - pdMS_TO_TICKS(CLOCK_WIFI_INTERVAL_MS);
    TickType_t last_sync = now - pdMS_TO_TICKS(CLOCK_SYNC_RETRY_MS);
    TickType_t last_weather = now;
    unsigned query_failures = 0;
    for (;;)
    {
        now = xTaskGetTickCount();
        if(wifi_ready() && !esp_at_is_ready()) {
            wifi_mark_unready();connected=false;synced=false;cloud_time_ready=false;
            clock_web_network(false,(uint32_t)now);
        }
        if (elapsed(now, last_wifi, CLOCK_WIFI_INTERVAL_MS))
        {
            bool was_connected = connected;
            esp_wifi_info_t info = {0};
            if (!wifi_ready()) (void)wifi_init();
            bool queried = wifi_ready() && esp_at_get_wifi_info(&info);
            connected = queried && info.connected;
            if (queried) query_failures = 0;
            else if (++query_failures >= 3) { wifi_mark_unready(); query_failures = 0; }
            if (!connected && wifi_ready())
            {
                (void)wifi_wait_connect();
                connected = esp_at_get_wifi_info(&info) && info.connected;
            }
            if (connected)
            {
                if (!was_connected || strcmp(last_ssid, info.ssid) != 0)
                {
                    publish_ssid(info.ssid);
                    memcpy(last_ssid, info.ssid, sizeof(last_ssid));
                    last_ssid[sizeof(last_ssid) - 1] = '\0';
                }
                if (!was_connected) {
                    synced=false;cloud_time_ready=false;
                    /* A bounded weather timeout may restart ESP. Preserve its
                     * retry cadence so recovery does not immediately repeat
                     * the same failing request and tear down MQTT again. */
                    if(!onenet_service_configured())first_weather=true;
                }
            }
            else if (was_connected || !last_ssid[0])
            {
                publish_ssid("wifi lost");
                last_ssid[0] = '\0';
            }
            last_wifi = xTaskGetTickCount();
        }
        now = xTaskGetTickCount();
        clock_web_network(connected,(uint32_t)now);
        clock_web_poll((uint32_t)xTaskGetTickCount());
        if (connected && elapsed(now, last_sync, synced ? CLOCK_SYNC_INTERVAL_MS : CLOCK_SYNC_RETRY_MS))
        {
            synced = synchronize();
            last_sync = xTaskGetTickCount();
            clock_web_poll((uint32_t)xTaskGetTickCount());
        }
        onenet_service_network(connected,cloud_time_ready,(uint32_t)xTaskGetTickCount());
        onenet_service_poll((uint32_t)xTaskGetTickCount());
        if (connected && !onenet_service_busy() && (first_weather || elapsed(xTaskGetTickCount(), last_weather, CLOCK_WEATHER_INTERVAL_MS)))
        {
            update_weather();
            first_weather = false;
            last_weather = xTaskGetTickCount();
            clock_web_poll((uint32_t)xTaskGetTickCount());
            onenet_service_poll((uint32_t)xTaskGetTickCount());
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_init(void)
{
    clock_web_set_background_poll(network_background);
    BaseType_t created = xTaskCreate(clock_loop, "clock", 1024, NULL, 4, &clock_task);
    configASSERT(created == pdPASS);
    created = xTaskCreate(sensor_loop, "sensor", 1024, NULL, 3, &sensor_task);
    configASSERT(created == pdPASS);
    created = xTaskCreate(network_loop, "network", 2048, NULL, 3, &network_task);
    configASSERT(created == pdPASS);
}
