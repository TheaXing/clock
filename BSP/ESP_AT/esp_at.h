#ifndef ESP_AT_H
#define ESP_AT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char ssid[64];
    char bssid[18];
    int channel;
    int rssi;
    bool connected;
} esp_wifi_info_t;

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t weekday; /* Monday = 1, Sunday = 7. */
} esp_date_time_t;

/* Call from task context after MX_USART2_UART_Init and scheduler startup.
 * Commands are serialized; every wait has a finite timeout. After a transport
 * timeout/error, call esp_at_init again before issuing further commands.
 * Initialization soft-restarts the ESP with AT+RST and waits for ready; saved
 * credentials and other persistent settings are preserved. */
bool esp_at_init(void);
bool esp_at_wifi_init(void);
bool esp_at_connect_wifi(const char *ssid, const char *pwd, const char *mac);
bool esp_at_get_wifi_info(esp_wifi_info_t *info);
bool wifi_is_connected(void);
bool esp_at_sntp_init(void);
bool esp_at_sntp_get_time(esp_date_time_t *date);

/* Returns the HTTP body, concatenated across length-framed +HTTPCLIENT reports,
 * without AT headers or terminal OK. Embedded payload newlines are preserved.
 * The independent snapshot survives other AT commands and is valid until the
 * next esp_at_http_get invocation. Use one network worker and parse it there. */
const char *esp_at_http_get(const char *url);

#endif
