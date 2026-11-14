#include "clock_console.h"
#include <stdio.h>
#include "wifi.h"
#include "esp_at.h"
#include "onenet_config.h"

static bool initialized;

/**
 * 功能：查询无线模块的应用层初始化状态。
 * 参数：无。
 * 返回：已完成初始化返回 true，否则返回 false。
 */
bool wifi_ready(void) {
    return initialized;
}
/**
 * 功能：清除无线模块初始化状态，触发后续重新初始化。
 * 参数：无。
 * 返回：无。
 */
void wifi_mark_unready(void) {
    initialized = false;
}

/**
 * 功能：初始化 ESP、检测 MQTT 能力并配置无线网络与网络校时。
 * 参数：无。
 * 返回：初始化成功返回 true，配置缺失或通信失败返回 false。
 */
bool wifi_init(void) {
    initialized = false;
    if (!CLOCK_WIFI_SSID[0]) {
        clock_log_line("[WIFI] Set CLOCK_WIFI_SSID in app/clock_config_local.h");
        return false;
    }
    if (!esp_at_init()) {
        clock_log_line("[WIFI] Initialization failed; local clock continues, retry pending");
        return false;
    }
    char firmware[768];
    if (esp_at_get_firmware_info(firmware, sizeof firmware)) {
        clock_log("[ESP] Firmware information:\r\n%s", firmware);
    } else {
        clock_log_line("[ESP] Firmware information unavailable");
    }
    esp_mqtt_probe_result_t probe = esp_at_probe_mqtt();
    if (probe == ESP_MQTT_PROBE_TRANSPORT_ERROR) {
        clock_log_line("[ONENET] MQTT probe transport failure; AT recovery will retry");
        return false;
    }
    if (probe == ESP_MQTT_PROBE_SUPPORTED) {
        clock_log_line("[ONENET] MQTT state query accepted; TLS MQTT setup follows WiFi/SNTP");
    } else {
        clock_log_line(
            "[ONENET] MQTT state query rejected; configured MQTT setup will be attempted after WiFi/SNTP");
    }
    clock_log(
        "[ONENET] Target %s:%u; product %s; device %s; token %s; connection follows WiFi/SNTP\r\n",
        CLOCK_ONENET_MQTT_HOST, (unsigned)CLOCK_ONENET_MQTT_PORT, CLOCK_ONENET_PRODUCT_ID,
        CLOCK_ONENET_DEVICE_NAME, CLOCK_ONENET_TOKEN[0] ? "configured" : "missing");
    if (!esp_at_wifi_init() || !esp_at_sntp_init()) {
        clock_log_line("[WIFI] Initialization failed; local clock continues, retry pending");
        return false;
    }
    initialized = true;
    clock_log_line("[WIFI] AT and SNTP initialized");
    return true;
}

/**
 * 功能：检查无线连接，必要时使用配置的网络名称和密码重新连接。
 * 参数：无。
 * 返回：查询确认已连接返回 true，否则返回 false。
 */
bool wifi_wait_connect(void) {
    if (!initialized) {
        return false;
    }
    esp_wifi_info_t info = {0};
    if (esp_at_get_wifi_info(&info) && info.connected) {
        return true;
    }
    if (!esp_at_connect_wifi(CLOCK_WIFI_SSID, CLOCK_WIFI_PASSWORD, NULL)) {
        clock_log_line("[WIFI] Connection failed; will retry");
        return false;
    }
    return esp_at_get_wifi_info(&info) && info.connected;
}
