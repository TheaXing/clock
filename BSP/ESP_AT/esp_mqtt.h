#ifndef ESP_MQTT_H
#define ESP_MQTT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ESP_MQTT_TOPIC_CAPACITY 128U
#define ESP_MQTT_PAYLOAD_CAPACITY 1536U
#define ESP_MQTT_TOKEN_LIMIT 1024U
#define ESP_MQTT_CA_LIMIT 4096U

typedef enum {
    ESP_MQTT_ERROR_NONE,
    ESP_MQTT_ERROR_ARGUMENT,
    ESP_MQTT_ERROR_TRANSPORT,
    ESP_MQTT_ERROR_COMMAND,
    ESP_MQTT_ERROR_TLS_CA,
    ESP_MQTT_ERROR_QUEUE_OVERFLOW,
    ESP_MQTT_ERROR_DISCONNECTED
} esp_mqtt_error_t;

typedef struct {
    const char *step;  /* Fixed labels only; never command arguments or data. */
    const char *reply;
    uint32_t at_error; /* ESP SYSLOG code, 0 when unavailable/ambiguous. */
} esp_mqtt_diagnostic_t;

typedef struct {
    const char *host;
    uint16_t port;
    const char *client_id;
    const char *username;
    const char *password;
    const uint8_t *ca_pem;
    size_t ca_length;
    /* NULL: use host as SNI/certificate name. Empty: omit SNI; ESP-AT4.1
     * verifies CA/date without DNS-name matching. Only for a dedicated trusted
     * CA policy, never an automatic fallback from a failed verified handshake. */
    const char *tls_server_name;
} esp_mqtt_config_t;

typedef struct {
    char topic[ESP_MQTT_TOPIC_CAPACITY + 1U];
    uint32_t received_ms;
    uint16_t length;
    uint8_t data[ESP_MQTT_PAYLOAD_CAPACITY];
} esp_mqtt_message_t;

/* One network worker owns all calls. Configuration copies host/port and sends
 * credentials immediately; pointers need remain valid only for the call.
 * CA bytes are the exact certificate file bytes (without a C string NUL). CA is
 * read, compared, written only on mismatch, and read back before scheme 3.
 * SNTP must have synchronized ESP time before connect. No insecure fallback.
 * Configure waits are bounded at 3 seconds; publish allows 500 ms for the
 * local UART prompt and 1200 ms for +MQTTPUB:OK. A publish timeout faults the
 * transport, requiring esp_at_init before the next command. */
bool esp_at_mqtt_configure(const esp_mqtt_config_t *config);
bool esp_at_mqtt_connect(uint32_t timeout_ms);
bool esp_at_mqtt_subscribe(const char *topic, uint8_t qos);
bool esp_at_mqtt_publish(const char *topic, const void *data, size_t length, uint8_t qos, bool retain);
bool esp_at_mqtt_disconnect(void);
bool esp_at_mqtt_connected(void);
esp_mqtt_error_t esp_at_mqtt_last_error(void);
esp_mqtt_diagnostic_t esp_at_mqtt_diagnostic(void);
/* Pending includes an incomplete UART frame. Poll copies one complete frame;
 * ESP-AT may split a broker message into multiple frames. Application JSON
 * assembly must therefore reject incomplete documents until more frames arrive.
 * Queue overflow faults the transport and discards queued commands. */
bool esp_at_mqtt_pending(void);
bool esp_at_mqtt_poll(esp_mqtt_message_t *message);

#endif
