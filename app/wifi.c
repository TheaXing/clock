#include "clock_console.h"
#include <stdio.h>
#include "wifi.h"
#include "esp_at.h"

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
    if (!esp_at_init() || !esp_at_wifi_init() || !esp_at_sntp_init())
    {
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
