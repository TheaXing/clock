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

/* Read-only diagnostics for the network worker. Firmware output contains the
 * complete multiline AT+GMR reply, including its terminal OK. The output is
 * cleared on failure; capacity must include the terminating NUL. */
bool esp_at_get_firmware_info(char *output, size_t capacity);

typedef enum esp_mqtt_probe_result_t {
    ESP_MQTT_PROBE_SUPPORTED,
    ESP_MQTT_PROBE_REJECTED,
    ESP_MQTT_PROBE_TRANSPORT_ERROR
} esp_mqtt_probe_result_t;

/* Queries AT+MQTTCONN? without configuring or connecting MQTT. SUPPORTED means
 * this state query was accepted, not that every MQTT/TLS feature is available.
 * ERROR/busy is inconclusive and leaves the transport usable. */
esp_mqtt_probe_result_t esp_at_probe_mqtt(void);

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
/* ESP-AT 4.1 HTTPCGET with a module-side network timeout. Shares the HTTP
 * snapshot lifetime above. timeout_ms is restricted to 100..3000 ms. */
const char *esp_at_http_get_bounded(const char *url, uint32_t timeout_ms);

#define ESP_SERVER_CHUNK_CAPACITY 256U

typedef enum {
    ESP_SERVER_EVENT_DATA,
    ESP_SERVER_EVENT_CONNECTED,
    ESP_SERVER_EVENT_CLOSED
} esp_server_event_t;

typedef struct {
    esp_server_event_t event;
    uint8_t link;
    uint32_t generation;
    uint16_t length;
    uint8_t data[ESP_SERVER_CHUNK_CAPACITY];
} esp_server_chunk_t;

/* All functions belong to the same network worker as WiFi/SNTP/HTTPCLIENT.
 * Poll at most a fixed number of times per worker iteration. Each poll reads
 * at most 256 bytes and every AT wait is bounded. Events are connection
 * snapshots: a new generation invalidates every older request on that ID.
 * Pass the event generation back to send/close to avoid acting on a reused ID.
 * Start never resets the module; an unsupported optional command leaves the
 * WiFi/SNTP/HTTPCLIENT transport usable. IP receives the station IPv4 address. */
bool esp_at_is_ready(void);
bool esp_at_server_start(uint16_t port, char ip[16]);
bool esp_at_server_ready(void);
bool esp_at_server_poll(esp_server_chunk_t *chunk);
bool esp_at_server_send(uint8_t link, uint32_t generation, const void *data, size_t length);
bool esp_at_server_close(uint8_t link, uint32_t generation);

#endif
