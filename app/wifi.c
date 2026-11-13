#include "clock_console.h"
#include <stdio.h>
#include "wifi.h"
#include "esp_at.h"
#include "onenet_config.h"

static bool initialized;

bool wifi_ready(void) { return initialized; }
void wifi_mark_unready(void) { initialized = false; }

bool wifi_init(void)
{
    initialized = false;
    if (!CLOCK_WIFI_SSID[0])
    {
        clock_log_line("[WIFI] Set CLOCK_WIFI_SSID in app/clock_config_local.h");
        return false;
    }
    if (!esp_at_init())
    {
        clock_log_line("[WIFI] Initialization failed; local clock continues, retry pending");
        return false;
    }
    char firmware[768];
    if(esp_at_get_firmware_info(firmware,sizeof firmware))
        clock_log("[ESP] Firmware information:\r\n%s",firmware);
    else clock_log_line("[ESP] Firmware information unavailable");
    esp_mqtt_probe_result_t probe=esp_at_probe_mqtt();
    if(probe==ESP_MQTT_PROBE_TRANSPORT_ERROR) {
        clock_log_line("[ONENET] MQTT probe transport failure; AT recovery will retry");
        return false;
    }
    if(probe==ESP_MQTT_PROBE_SUPPORTED)
        clock_log_line("[ONENET] MQTT state query accepted; TLS MQTT setup follows WiFi/SNTP");
    else clock_log_line("[ONENET] MQTT state query rejected; configured MQTT setup will be attempted after WiFi/SNTP");
    clock_log("[ONENET] Target %s:%u; product %s; device %s; token %s; connection follows WiFi/SNTP\r\n",
        CLOCK_ONENET_MQTT_HOST,(unsigned)CLOCK_ONENET_MQTT_PORT,CLOCK_ONENET_PRODUCT_ID,
        CLOCK_ONENET_DEVICE_NAME,CLOCK_ONENET_TOKEN[0]?"configured":"missing");
    if (!esp_at_wifi_init() || !esp_at_sntp_init()) {
        clock_log_line("[WIFI] Initialization failed; local clock continues, retry pending");
        return false;
    }
    initialized = true;
    clock_log_line("[WIFI] AT and SNTP initialized");
    return true;
}

bool wifi_wait_connect(void)
{
    if (!initialized) return false;
    esp_wifi_info_t info = {0};
    if (esp_at_get_wifi_info(&info) && info.connected) return true;
    if (!esp_at_connect_wifi(CLOCK_WIFI_SSID, CLOCK_WIFI_PASSWORD, NULL))
    {
        clock_log_line("[WIFI] Connection failed; will retry");
        return false;
    }
    return esp_at_get_wifi_info(&info) && info.connected;
}
