#include "esp_at.h"
#include "esp_at_parse.h"
#include "esp_mqtt.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "usart.h"

#include <stdio.h>
#include <string.h>

#define ESP_RESPONSE_CAPACITY 2048U
#define ESP_COMMAND_CAPACITY  1152U
#define ESP_LOCK_TIMEOUT_MS   1000U
#define ESP_SERVER_LINKS      5U
#define ESP_SERVER_SEND_LIMIT 1460U
#define ESP_DATA_ENTRY_LIMIT  ESP_MQTT_CA_LIMIT
#define ESP_MQTT_QUEUE_COUNT 3U
#define ESP_MQTT_WAIT_MS 2000U
#define ESP_MQTT_PUBLISH_PROMPT_MS 500U
#define ESP_MQTT_PUBLISH_DONE_MS 1200U

typedef enum {
    ACK_NONE, ACK_OK, ACK_ERROR, ACK_BUSY, ACK_READY, ACK_OVERFLOW, ACK_IO, ACK_TIMEOUT,
    ACK_PROMPT, ACK_SEND_OK, ACK_MQTT_PUB_OK
} at_ack_t;

typedef enum { REPLY_OK, REPLY_HTTP, REPLY_READY, REPLY_PASSIVE, REPLY_PROMPT, REPLY_SEND,
               REPLY_DRAIN, REPLY_MQTT_PUB, REPLY_DATA_OK, REPLY_CA } reply_kind_t;
typedef struct {
    volatile uint32_t generation;
    volatile uint32_t pending;
    volatile uint32_t notification;
    volatile bool active;
    uint32_t reported_generation;
    bool reported_active;
} server_link_t;

static SemaphoreHandle_t command_mutex;
static SemaphoreHandle_t response_semaphore;
static char response[ESP_RESPONSE_CAPACITY];
static char http_response[ESP_RESPONSE_CAPACITY];
static char command_buffer[ESP_COMMAND_CAPACITY];
static uint8_t uart_rx_byte;
static volatile bool receiving;
static volatile bool synchronized;
static volatile at_ack_t response_ack;
static volatile size_t response_length;
static volatile uint32_t rx_sequence;

/* Only the ISR mutates these while a transaction is active. The task resets
 * them with interrupts masked before transmitting a new command. */
static char line[64];
static size_t line_length;
static size_t line_response_bytes;
static bool line_suppressed;
static reply_kind_t reply_kind;
static bool http_header;
static unsigned http_digits;
static uint32_t http_length;
static uint32_t http_remaining;
static bool passive_header;
static bool passive_seen;
static uint32_t passive_remaining;
static uint16_t passive_limit, passive_length, passive_stored;
static uint8_t passive_data[ESP_SERVER_CHUNK_CAPACITY];
static volatile bool server_ready;
static server_link_t server_links[ESP_SERVER_LINKS];
static uint8_t poll_cursor;
static uint16_t send_debt;
static bool send_recovery_pending;
static bool boot_resync_needed = true;
static bool valid_link(uint8_t id, uint32_t generation);

#if defined(__GNUC__) && (defined(__arm__) || defined(__thumb__))
#define MQTT_RAM __attribute__((section(".clock_mqtt_ram")))
#else
#define MQTT_RAM
#endif
/* CPU-only CCM storage: explicitly initialized because its linker section is
 * NOLOAD. UART receives one byte into ordinary SRAM, never into these slots. */
static esp_mqtt_message_t mqtt_queue[ESP_MQTT_QUEUE_COUNT] MQTT_RAM;
_Static_assert(sizeof mqtt_queue <= 5100U, "MQTT queue CCM budget exceeded");
static volatile uint8_t mqtt_head, mqtt_count;
static volatile bool mqtt_configured, mqtt_connected;
static volatile uint32_t mqtt_disconnect_sequence;
static volatile esp_mqtt_error_t mqtt_error;
static char mqtt_host[129];
static uint16_t mqtt_port;
typedef enum { MQTT_RX_IDLE, MQTT_RX_LINK, MQTT_RX_COMMA, MQTT_RX_QUOTE,
               MQTT_RX_TOPIC, MQTT_RX_SEPARATOR, MQTT_RX_LENGTH, MQTT_RX_DATA,
               MQTT_RX_BAD } mqtt_rx_state_t;
static volatile mqtt_rx_state_t mqtt_rx_state;
static uint8_t mqtt_slot;
static uint16_t mqtt_topic_length, mqtt_stored;
static uint32_t mqtt_length, mqtt_remaining;
static bool mqtt_escape, mqtt_frame_valid;
static unsigned mqtt_digits;
static bool ca_header, ca_seen, ca_match;
static uint32_t ca_remaining, ca_total, ca_stored;
static const uint8_t *ca_expected;
static size_t ca_expected_length;

static void mqtt_invalidate(void)
{
    mqtt_configured = mqtt_connected = false;
    mqtt_count = mqtt_head = 0U;
    mqtt_frame_valid = false;
}

static void invalidate_server(void)
{
    server_ready = false;
    for (unsigned n = 0U; n < ESP_SERVER_LINKS; ++n) {
        server_links[n].active = false;
        server_links[n].pending = 0U;
    }
}

static void transport_fault(void)
{
    synchronized = false;
    invalidate_server();
    mqtt_invalidate();
    if (mqtt_error != ESP_MQTT_ERROR_QUEUE_OVERFLOW) mqtt_error = ESP_MQTT_ERROR_TRANSPORT;
}

static TickType_t ticks(uint32_t ms)
{
    TickType_t result = pdMS_TO_TICKS(ms);
    return result != 0U ? result : 1U;
}

static bool resources_ready(void)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) return false;
    /* Creation cannot block; the critical section also prevents competing
     * first-time callers from publishing two independent mutexes. */
    taskENTER_CRITICAL();
    if (command_mutex == NULL) command_mutex = xSemaphoreCreateMutex();
    if (response_semaphore == NULL) response_semaphore = xSemaphoreCreateBinary();
    bool ready = command_mutex != NULL && response_semaphore != NULL;
    taskEXIT_CRITICAL();
    return ready;
}

static bool lock_commands(void)
{
    return resources_ready() && xSemaphoreTake(command_mutex, ticks(ESP_LOCK_TIMEOUT_MS)) == pdTRUE;
}

static void unlock_commands(void)
{
    (void)xSemaphoreGive(command_mutex);
}

static void notify_from_isr(at_ack_t ack, BaseType_t *woken)
{
    if (!receiving) return;
    receiving = false;
    response_ack = ack;
    if (response_semaphore != NULL) (void)xSemaphoreGiveFromISR(response_semaphore, woken);
}

static void append_response(uint8_t byte, BaseType_t *woken)
{
    if (!receiving || reply_kind == REPLY_PASSIVE) return;
    if (response_length >= sizeof response - 1U || byte == '\0') {
        transport_fault();
        notify_from_isr(ACK_OVERFLOW, woken);
    } else {
        response[response_length++] = (char)byte;
    }
}

static void suppress_line(void)
{
    if (receiving && response_length >= line_response_bytes) response_length -= line_response_bytes;
    line_response_bytes = 0U;
    line_suppressed = true;
}

static bool line_number(size_t *offset, size_t length, uint32_t limit, uint32_t *value)
{
    uint32_t result = 0U;
    size_t p = *offset;
    if (p == length || line[p] < '0' || line[p] > '9') return false;
    do {
        unsigned digit = (unsigned)(line[p] - '0');
        if (result > limit / 10U || (result == limit / 10U && digit > limit % 10U)) return false;
        result = result * 10U + digit;
        ++p;
    } while (p < length && line[p] >= '0' && line[p] <= '9');
    *offset = p; *value = result;
    return true;
}

static void connect_link(unsigned id)
{
    server_link_t *link = &server_links[id];
    if (++link->generation == 0U) ++link->generation;
    link->active = true;
    link->pending = 0U;
}

static void mqtt_rx_fault(esp_mqtt_error_t error, BaseType_t *woken)
{
    mqtt_error = error;
    transport_fault();
    notify_from_isr(ACK_OVERFLOW, woken);
}

static void mqtt_finish_frame(void)
{
    if (mqtt_frame_valid && synchronized && mqtt_configured) {
        mqtt_queue[mqtt_slot].topic[mqtt_topic_length] = '\0';
        mqtt_queue[mqtt_slot].length = mqtt_stored;
        mqtt_queue[mqtt_slot].received_ms = (uint32_t)(xTaskGetTickCountFromISR() * portTICK_PERIOD_MS);
        ++mqtt_count;
    }
    mqtt_rx_state = MQTT_RX_IDLE;
    mqtt_frame_valid = false;
    line_length = line_response_bytes = 0U;
    line_suppressed = false;
}

static void mqtt_receive(uint8_t byte, BaseType_t *woken)
{
    switch (mqtt_rx_state) {
    case MQTT_RX_LINK:
        if (byte == '0') mqtt_rx_state = MQTT_RX_COMMA;
        else goto malformed;
        break;
    case MQTT_RX_COMMA:
        if (byte == ',') mqtt_rx_state = MQTT_RX_QUOTE;
        else goto malformed;
        break;
    case MQTT_RX_QUOTE:
        if (byte == '"') mqtt_rx_state = MQTT_RX_TOPIC;
        else goto malformed;
        break;
    case MQTT_RX_TOPIC:
        if (!mqtt_escape && byte == '"') {
            if (mqtt_topic_length == 0U) goto malformed;
            mqtt_rx_state = MQTT_RX_SEPARATOR;
        } else if (!mqtt_escape && byte == '\\') mqtt_escape = true;
        else {
            if (byte < 32U || byte == 127U || mqtt_topic_length >= ESP_MQTT_TOPIC_CAPACITY) goto malformed;
            if (mqtt_frame_valid) mqtt_queue[mqtt_slot].topic[mqtt_topic_length] = (char)byte;
            ++mqtt_topic_length;
            mqtt_escape = false;
        }
        break;
    case MQTT_RX_SEPARATOR:
        if (byte == ',') mqtt_rx_state = MQTT_RX_LENGTH;
        else goto malformed;
        break;
    case MQTT_RX_LENGTH:
        if (byte >= '0' && byte <= '9') {
            unsigned digit = (unsigned)(byte - '0');
            if (++mqtt_digits > 7U || mqtt_length > 1048576U / 10U ||
                (mqtt_length == 1048576U / 10U && digit > 1048576U % 10U)) goto malformed;
            mqtt_length = mqtt_length * 10U + digit;
        } else if (byte == ',' && mqtt_digits != 0U) {
            mqtt_remaining = mqtt_length;
            mqtt_rx_state = MQTT_RX_DATA;
            if (mqtt_length > ESP_MQTT_PAYLOAD_CAPACITY) mqtt_rx_fault(ESP_MQTT_ERROR_QUEUE_OVERFLOW, woken);
            if (mqtt_remaining == 0U) mqtt_finish_frame();
        } else goto malformed;
        break;
    case MQTT_RX_DATA:
        if (mqtt_frame_valid && mqtt_stored < ESP_MQTT_PAYLOAD_CAPACITY) mqtt_queue[mqtt_slot].data[mqtt_stored++] = byte;
        if (--mqtt_remaining == 0U) mqtt_finish_frame();
        break;
    case MQTT_RX_BAD:
        if (byte == '\n') mqtt_rx_state = MQTT_RX_IDLE;
        break;
    default: break;
    }
    return;
malformed:
    mqtt_rx_state = MQTT_RX_BAD;
    mqtt_rx_fault(ESP_MQTT_ERROR_QUEUE_OVERFLOW, woken);
}

static bool unsolicited_line(size_t length, BaseType_t *woken)
{
    if (length >= 5U && memcmp(line, "+IPD,", 5U) == 0) {
        size_t offset = 5U;
        uint32_t id, available;
        if (!line_number(&offset, length, ESP_SERVER_LINKS - 1U, &id) ||
            offset >= length || line[offset++] != ',' ||
            !line_number(&offset, length, 0x7fffffffU, &available) || offset != length) {
            transport_fault(); notify_from_isr(ACK_IO, woken);
        } else {
            server_link_t *link = &server_links[id];
            if (!link->active) connect_link((unsigned)id);
            link->pending = available;
            ++link->notification;
        }
        return true;
    }
    if (length >= 2U && line[0] >= '0' && line[0] <= '4' && line[1] == ',') {
        unsigned id = (unsigned)(line[0] - '0');
        if (length == 9U && memcmp(line + 2U, "CONNECT", 7U) == 0) connect_link(id);
        else if (length == 8U && memcmp(line + 2U, "CLOSED", 6U) == 0) {
            server_links[id].active = false;
            server_links[id].pending = 0U;
        }
        return true;
    }
    if (length == 15U && memcmp(line, "WIFI DISCONNECT", 15U) == 0) {
        invalidate_server();
        mqtt_invalidate();
        mqtt_error = ESP_MQTT_ERROR_DISCONNECTED;
        return true;
    }
    if (length >= 18U && memcmp(line, "+MQTTDISCONNECTED:", 18U) == 0) {
        ++mqtt_disconnect_sequence;
        mqtt_connected = false;
        mqtt_count = 0U;
        mqtt_frame_valid = false;
        mqtt_error = ESP_MQTT_ERROR_DISCONNECTED;
        return true;
    }
    /* Connection URCs are informational. Only a scheme/host/port-verified
     * MQTTCONN state query may establish the application's online state. */
    if (length >= 15U && memcmp(line, "+MQTTCONNECTED:", 15U) == 0) return true;
    return false;
}

static void receive_byte(uint8_t byte, BaseType_t *woken)
{
    ++rx_sequence;
    if (mqtt_rx_state != MQTT_RX_IDLE) {
        mqtt_receive(byte, woken);
        return;
    }
    if (ca_remaining != 0U) {
        if (ca_stored >= ca_expected_length || ca_expected[ca_stored] != byte) ca_match = false;
        ++ca_stored;
        --ca_remaining;
        return;
    }
    /* Both payload types are length-framed. NUL and terminal-looking lines in
     * socket data must never enter the line/ACK or unsolicited parser. */
    if (passive_remaining != 0U) {
        if (passive_stored < sizeof passive_data) passive_data[passive_stored++] = byte;
        --passive_remaining;
        return;
    }
    if (http_remaining != 0U) {
        append_response(byte, woken);
        --http_remaining;
        return;
    }
    if (!line_suppressed) {
        size_t before = response_length;
        /* A URC prefix is temporarily part of the reply until identified.
         * Permit its last bytes past the reply capacity without storing them;
         * recognizing the full prefix below rolls back its stored bytes. A
         * mismatch still takes the ordinary overflow path and faults loudly. */
        static const char mqtt_prefix[] = "+MQTTSUBRECV:";
        bool prefix_tail = response_length >= sizeof response - 1U && line_length < sizeof mqtt_prefix - 1U &&
                           memcmp(line, mqtt_prefix, line_length) == 0 && byte == (uint8_t)mqtt_prefix[line_length];
        if (!prefix_tail) append_response(byte, woken);
        line_response_bytes += response_length - before;
    }
    if (http_header || passive_header || ca_header) {
        if (byte >= '0' && byte <= '9') {
            unsigned digit = (unsigned)(byte - '0');
            uint32_t limit = ca_header ? ESP_MQTT_CA_LIMIT : passive_header ? passive_limit : ESP_RESPONSE_CAPACITY;
            if (++http_digits > 5U || http_length > limit / 10U ||
                (http_length == limit / 10U && digit > limit % 10U)) {
                transport_fault();
                http_header = passive_header = ca_header = false;
                notify_from_isr(ACK_OVERFLOW, woken);
            } else {
                http_length = http_length * 10U + digit;
            }
            return;
        }
        if (byte == ',' && http_digits != 0U) {
            if (ca_header) {
                ca_seen = true;
                ca_total = ca_remaining = http_length;
                ca_stored = 0U;
                ca_match = http_length == ca_expected_length;
            } else if (passive_header) {
                passive_seen = true;
                passive_remaining = http_length;
                passive_length = (uint16_t)http_length;
                passive_stored = 0U;
            } else http_remaining = http_length;
            http_header = passive_header = ca_header = false;
            line_length = 0U;
            line_response_bytes = 0U;
            if (reply_kind == REPLY_CA) line_suppressed = false;
            return;
        }
        /* Malformed framing must not expose payload terminal-looking lines. */
        http_header = passive_header = ca_header = false;
        transport_fault();
        notify_from_isr(ACK_IO, woken);
    }

    if (byte == '>' && receiving && reply_kind == REPLY_PROMPT) {
        line_length = line_response_bytes = 0U;
        notify_from_isr(ACK_PROMPT, woken);
        return;
    }
    /* Some firmware follows its prompt with a space. */
    if (line_length == 0U && byte == ' ') return;

    if (line_length < sizeof line) line[line_length] = (char)byte;
    if (line_length <= sizeof line) ++line_length;
    if (line_length == sizeof "+MQTTSUBRECV:" - 1U && memcmp(line, "+MQTTSUBRECV:", sizeof "+MQTTSUBRECV:" - 1U) == 0) {
        suppress_line();
        mqtt_rx_state = MQTT_RX_LINK;
        mqtt_slot = (uint8_t)((mqtt_head + mqtt_count) % ESP_MQTT_QUEUE_COUNT);
        mqtt_topic_length = mqtt_stored = 0U;
        mqtt_length = mqtt_remaining = 0U;
        mqtt_digits = 0U;
        mqtt_escape = false;
        mqtt_frame_valid = synchronized && mqtt_configured && mqtt_count < ESP_MQTT_QUEUE_COUNT;
        if (mqtt_count >= ESP_MQTT_QUEUE_COUNT) mqtt_rx_fault(ESP_MQTT_ERROR_QUEUE_OVERFLOW, woken);
        return;
    }
    if ((line_length == sizeof "+MQTTCONNECTED:" - 1U && memcmp(line, "+MQTTCONNECTED:", sizeof "+MQTTCONNECTED:" - 1U) == 0) ||
        (line_length == sizeof "+MQTTDISCONNECTED:" - 1U && memcmp(line, "+MQTTDISCONNECTED:", sizeof "+MQTTDISCONNECTED:" - 1U) == 0)) suppress_line();
    if (receiving && reply_kind == REPLY_CA && line_length == sizeof "+SYSMFG:\"mqtt_ca\",\"mqtt_ca\",8," - 1U &&
        memcmp(line, "+SYSMFG:\"mqtt_ca\",\"mqtt_ca\",8,", sizeof "+SYSMFG:\"mqtt_ca\",\"mqtt_ca\",8," - 1U) == 0) {
        suppress_line();
        ca_header = true; http_digits = 0U; http_length = 0U;
        return;
    }
    if ((line_length == 5U && memcmp(line, "+IPD,", 5U) == 0) ||
        (line_length == 2U && line[0] >= '0' && line[0] <= '4' && line[1] == ',')) suppress_line();
    if (line_suppressed && line_length >= 5U && memcmp(line, "+IPD,", 5U) == 0 && byte == ':') {
        /* Active-mode data is unsafe after passive-mode negotiation. */
        transport_fault(); notify_from_isr(ACK_IO, woken);
    }
    if (receiving && reply_kind == REPLY_HTTP &&
        ((line_length == sizeof "+HTTPCLIENT:" - 1U && memcmp(line, "+HTTPCLIENT:", sizeof "+HTTPCLIENT:" - 1U) == 0) ||
         (line_length == sizeof "+HTTPCGET:" - 1U && memcmp(line, "+HTTPCGET:", sizeof "+HTTPCGET:" - 1U) == 0))) {
        http_header = true;
        http_digits = 0U;
        http_length = 0U;
        return;
    }
    if (receiving && reply_kind == REPLY_PASSIVE && line_length == sizeof "+CIPRECVDATA:" - 1U &&
        memcmp(line, "+CIPRECVDATA:", sizeof "+CIPRECVDATA:" - 1U) == 0) {
        passive_header = true;
        http_digits = 0U;
        http_length = 0U;
        return;
    }
    if (byte != '\n') return;

    size_t length = line_length;
    if (length <= sizeof line) {
        if (length != 0U && line[length - 1U] == '\n') --length;
        if (length != 0U && line[length - 1U] == '\r') --length;
        if (unsolicited_line(length, woken)) suppress_line();
        else if (length == 2U && memcmp(line, "OK", 2U) == 0 &&
                 reply_kind != REPLY_READY && reply_kind != REPLY_PROMPT && reply_kind != REPLY_SEND && reply_kind != REPLY_DRAIN && reply_kind != REPLY_MQTT_PUB &&
                 (reply_kind != REPLY_PASSIVE || passive_seen))
            notify_from_isr(ACK_OK, woken);
        else if (length == 11U && memcmp(line, "+MQTTPUB:OK", 11U) == 0 && (reply_kind == REPLY_MQTT_PUB || reply_kind == REPLY_DRAIN))
            notify_from_isr(ACK_MQTT_PUB_OK, woken);
        else if (length == 13U && memcmp(line, "+MQTTPUB:FAIL", 13U) == 0 && (reply_kind == REPLY_MQTT_PUB || reply_kind == REPLY_DRAIN))
            notify_from_isr(ACK_ERROR, woken);
        else if (length == 7U && memcmp(line, "SEND OK", 7U) == 0 && (reply_kind == REPLY_SEND || reply_kind == REPLY_DRAIN))
            notify_from_isr(ACK_SEND_OK, woken);
        else if ((length == 5U && memcmp(line, "ERROR", 5U) == 0) ||
                 (length == 4U && memcmp(line, "FAIL", 4U) == 0) ||
                 (length == 9U && memcmp(line, "SEND FAIL", 9U) == 0)) notify_from_isr(ACK_ERROR, woken);
        else if (length >= 4U && memcmp(line, "busy", 4U) == 0) notify_from_isr(ACK_BUSY, woken);
        else if (length == 5U && memcmp(line, "ready", 5U) == 0) {
            transport_fault(); /* Also notice an unsolicited module reboot. */
            notify_from_isr(ACK_READY, woken);
        }
    }
    line_length = 0U;
    line_response_bytes = 0U;
    line_suppressed = false;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart != &huart2) return;
    BaseType_t higher_priority_task_woken = pdFALSE;
    receive_byte(uart_rx_byte, &higher_priority_task_woken);
    /* Always rearm, even when the response was full or already completed. */
    if (HAL_UART_Receive_IT(&huart2, &uart_rx_byte, 1U) != HAL_OK) {
        transport_fault();
        notify_from_isr(ACK_IO, &higher_priority_task_woken);
    }
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    if (uart != &huart2) return;
    BaseType_t higher_priority_task_woken = pdFALSE;
    transport_fault();
    notify_from_isr(ACK_IO, &higher_priority_task_woken);
    /* HAL stops RX on overrun; abort the one-byte IT receive (no RX DMA),
     * clear FE/NE/ORE via SR/DR, and immediately restore reception. */
    (void)HAL_UART_AbortReceive(&huart2);
    __HAL_UART_CLEAR_OREFLAG(&huart2);
    (void)HAL_UART_Receive_IT(&huart2, &uart_rx_byte, 1U);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

static bool wait_quiet(uint32_t quiet_ms, uint32_t limit_ms)
{
    TickType_t start = xTaskGetTickCount();
    TickType_t last_change = start;
    uint32_t sequence = rx_sequence;
    do {
        TickType_t now = xTaskGetTickCount();
        uint32_t current = rx_sequence;
        if (current != sequence) { sequence = current; last_change = now; }
        if ((TickType_t)(now - last_change) >= ticks(quiet_ms)) return true;
        if ((TickType_t)(now - start) >= ticks(limit_ms)) return false;
        vTaskDelay(ticks(10U));
    } while (true);
}

static at_ack_t transfer_locked(const void *data, uint16_t length, uint32_t timeout_ms,
                                reply_kind_t kind, bool quiet, int guard_link, uint32_t guard_generation)
{
    if (kind != REPLY_READY && kind != REPLY_DRAIN && !synchronized) return ACK_IO;
    /* Discard unsolicited/stale lines before arming the next transaction. */
    if (quiet && !wait_quiet(20U, 300U)) { transport_fault(); return ACK_IO; }
    /* Keep task scheduling from opening a long reuse window between the
     * generation check and a short AT command. UART interrupts remain enabled
     * throughout; payload TX remains preemptible. ESP's numeric-ID protocol
     * cannot make an ID reuse during command bytes on the wire atomic. */
    bool suspend = guard_link >= 0 && kind != REPLY_SEND && length <= 64U;
    if (suspend) vTaskSuspendAll();
    taskENTER_CRITICAL();
    /* A ready/error interrupt may have arrived during the quiet wait. */
    if (kind != REPLY_READY && kind != REPLY_DRAIN && !synchronized) {
        taskEXIT_CRITICAL();
        if (suspend) (void)xTaskResumeAll();
        return ACK_IO;
    }
    if (guard_link >= 0 && !valid_link((uint8_t)guard_link, guard_generation)) {
        taskEXIT_CRITICAL();
        if (suspend) (void)xTaskResumeAll();
        return ACK_ERROR;
    }
    receiving = false;
    while (xSemaphoreTake(response_semaphore, 0U) == pdTRUE) { }
    response_length = 0U;
    response_ack = ACK_NONE;
    /* Preserve an incomplete unsolicited line across command boundaries. */
    line_response_bytes = 0U;
    reply_kind = kind;
    passive_seen = false;
    passive_length = passive_stored = 0U;
    receiving = true;
    taskEXIT_CRITICAL();

    /* An IRQ may have delivered CLOSED/CONNECT when interrupts were restored.
     * Recheck immediately before TX, including when entering payload mode. */
    if (guard_link >= 0 && !valid_link((uint8_t)guard_link, guard_generation)) {
        taskENTER_CRITICAL();
        receiving = false;
        taskEXIT_CRITICAL();
        if (suspend) (void)xTaskResumeAll();
        return ACK_ERROR;
    }

    /* TX storage is independent of the ISR-owned response. Reset and drain
     * happen before TX, because a response may arrive before TX returns. */
    HAL_StatusTypeDef tx = HAL_OK;
    if (kind == REPLY_DRAIN) {
        /* Complete an uncertain CIPSEND data-entry window without disclosing
         * a stale response to a reused connection. Reuse the passive scratch
         * buffer, and arm the completion ACK before the first zero byte. */
        memset(passive_data, 0, sizeof passive_data);
        uint16_t remaining = length;
        while (remaining != 0U && tx == HAL_OK) {
            uint16_t amount = remaining > sizeof passive_data ? (uint16_t)sizeof passive_data : remaining;
            tx = HAL_UART_Transmit(&huart2, passive_data, amount, 1000U);
            remaining = (uint16_t)(remaining - amount);
        }
    } else tx = HAL_UART_Transmit(&huart2, (uint8_t *)(void *)data, length, suspend ? 20U : 1000U);
    if (tx == HAL_OK && (kind == REPLY_SEND || kind == REPLY_DRAIN || kind == REPLY_DATA_OK || kind == REPLY_MQTT_PUB)) send_debt = 0U;
    if (suspend) (void)xTaskResumeAll();
    bool signalled = tx == HAL_OK && xSemaphoreTake(response_semaphore, ticks(timeout_ms)) == pdTRUE;
    taskENTER_CRITICAL();
    receiving = false;
    at_ack_t ack = response_ack;
    taskEXIT_CRITICAL();
    response[response_length] = '\0';
    if (tx != HAL_OK) ack = ACK_IO;
    else if (!signalled || ack == ACK_NONE) ack = ACK_TIMEOUT;
    else if (kind != REPLY_READY && kind != REPLY_DRAIN && !synchronized) ack = ACK_IO;
    if (ack == ACK_TIMEOUT || ack == ACK_IO || ack == ACK_OVERFLOW || ack == ACK_READY) transport_fault();
    return ack;
}

static at_ack_t exchange_locked(const char *command, uint32_t timeout_ms, bool http, bool wait_ready)
{
    return transfer_locked(command, (uint16_t)strlen(command), timeout_ms,
                           wait_ready ? REPLY_READY : http ? REPLY_HTTP : REPLY_OK, true, -1, 0U);
}

static bool command_locked(const char *command, uint32_t timeout_ms, bool http)
{
    for (unsigned attempt = 0; attempt < 3U; ++attempt) {
        at_ack_t ack = exchange_locked(command, timeout_ms, http, false);
        if (ack == ACK_OK) return true;
        if (ack != ACK_BUSY) return false;
        vTaskDelay(ticks(250U));
    }
    return false;
}

static bool recover_send_locked(void)
{
    send_recovery_pending = true;
    if (send_debt != 0U) {
        (void)transfer_locked(NULL, send_debt, 2000U, REPLY_DRAIN, false, -1, 0U);
    }
    if (send_debt == 0U) {
        /* If CIPSEND was rejected before its prompt was observed, the padding
         * went to the command parser. Terminate that line before AT+RST. */
        static const uint8_t newline[] = "\r\n";
        if (HAL_UART_Transmit(&huart2, (uint8_t *)(void *)newline, 2U, 1000U) == HAL_OK)
            send_recovery_pending = false;
    }
    transport_fault();
    return !send_recovery_pending;
}

bool esp_at_init(void)
{
    if (!lock_commands()) return false;
    taskENTER_CRITICAL();
    receiving = false;
    transport_fault();
    line_length = 0U;
    line_response_bytes = 0U;
    line_suppressed = false;
    http_header = false;
    http_remaining = 0U;
    passive_header = false;
    passive_remaining = 0U;
    mqtt_rx_state = MQTT_RX_IDLE;
    mqtt_error = ESP_MQTT_ERROR_NONE;
    ca_header = false; ca_remaining = 0U;
    memset(mqtt_queue, 0, sizeof mqtt_queue);
    reply_kind = REPLY_OK;
    (void)HAL_UART_AbortReceive(&huart2);
    __HAL_UART_CLEAR_OREFLAG(&huart2);
    HAL_StatusTypeDef status = HAL_UART_Receive_IT(&huart2, &uart_rx_byte, 1U);
    taskEXIT_CRITICAL();
    /* An MCU-only reset loses send_debt while the independently powered ESP
     * can still await CIPSEND/MQTT/CA bytes. One maximum-size CA-length
     * neutral fill on boot covers that unknown data-entry window. The CRLF
     * in recovery also flushes padding if the ESP was already in command mode. */
    bool boot_drained = true;
    if (status == HAL_OK && boot_resync_needed) {
        if (send_debt == 0U) send_debt = ESP_DATA_ENTRY_LIMIT;
        boot_drained = recover_send_locked();
        if (boot_drained) boot_resync_needed = false;
    }
    /* AT+RST is in-band and would otherwise be swallowed as socket payload.
     * Preserve a failed drain across init attempts until UART TX succeeds. */
    bool drained = status == HAL_OK && boot_drained &&
                   ((send_debt == 0U && !send_recovery_pending) || recover_send_locked());
    bool ready = drained && wait_quiet(100U, 1000U);
    bool found = false;
    if (ready) {
        for (unsigned attempt = 0; attempt < 3U; ++attempt) {
            /* A delayed prior OK cannot identify a new command. Restart the
             * module and wait specifically for ready, which clears any old
             * in-flight operation. Intermediate OK is deliberately ignored. */
            at_ack_t ack = exchange_locked("AT+RST\r\n", 5000U, false, true);
            if (ack == ACK_READY) {
                synchronized = true;
                found = true;
                break;
            }
            /* Only explicit command rejections are safe to retry here. After
             * an uncertain timeout, leave recovery to the next init call. */
            if (ack != ACK_BUSY && ack != ACK_ERROR) break;
            vTaskDelay(ticks(250U));
        }
    }
    /* Keep saved credentials/settings; never restore factory defaults. */
    bool ok = found && command_locked("ATE0\r\n", 1000U, false);
    if (!ok) synchronized = false;
    unlock_commands();
    return ok;
}

bool esp_at_get_firmware_info(char *output, size_t capacity)
{
    if (output == NULL || capacity == 0U) return false;
    output[0] = '\0';
    if (!lock_commands()) return false;
    at_ack_t ack = exchange_locked("AT+GMR\r\n", 1000U, false, false);
    size_t length = response_length;
    bool ok = ack == ACK_OK && length < capacity;
    if (ok) memcpy(output, response, length + 1U);
    unlock_commands();
    return ok;
}

esp_mqtt_probe_result_t esp_at_probe_mqtt(void)
{
    if (!lock_commands()) return ESP_MQTT_PROBE_TRANSPORT_ERROR;
    at_ack_t ack = exchange_locked("AT+MQTTCONN?\r\n", 1000U, false, false);
    unlock_commands();
    if (ack == ACK_OK) return ESP_MQTT_PROBE_SUPPORTED;
    if (ack == ACK_ERROR || ack == ACK_BUSY) return ESP_MQTT_PROBE_REJECTED;
    return ESP_MQTT_PROBE_TRANSPORT_ERROR;
}

bool esp_at_wifi_init(void)
{
    if (!lock_commands()) return false;
    bool ok = command_locked("AT+CWMODE=1\r\n", 2000U, false);
    unlock_commands();
    return ok;
}

bool esp_at_connect_wifi(const char *ssid, const char *pwd, const char *mac)
{
    char escaped_ssid[65], escaped_pwd[129];
    if (!esp_at_quote(ssid, 32U, escaped_ssid, sizeof escaped_ssid) || escaped_ssid[0] == '\0' ||
        !esp_at_quote(pwd, 64U, escaped_pwd, sizeof escaped_pwd) ||
        (mac != NULL && !esp_at_valid_mac(mac)) || !lock_commands()) return false;
    int count;
    if (mac != NULL) count = snprintf(command_buffer, sizeof command_buffer, "AT+CWJAP=\"%s\",\"%s\",\"%s\"\r\n", escaped_ssid, escaped_pwd, mac);
    else count = snprintf(command_buffer, sizeof command_buffer, "AT+CWJAP=\"%s\",\"%s\"\r\n", escaped_ssid, escaped_pwd);
    bool ok = count > 0 && (size_t)count < sizeof command_buffer && command_locked(command_buffer, 20000U, false);
    unlock_commands();
    return ok;
}

bool esp_at_get_wifi_info(esp_wifi_info_t *info)
{
    if (info == NULL || !lock_commands()) return false;
    esp_wifi_info_t parsed = {0};
    bool ok = command_locked("AT+CWSTATE?\r\n", 2000U, false) && esp_at_parse_cwstate(response, &parsed);
    if (ok && parsed.connected) ok = command_locked("AT+CWJAP?\r\n", 2000U, false) && esp_at_parse_cwjap(response, &parsed);
    if (ok) *info = parsed;
    unlock_commands();
    return ok;
}

bool wifi_is_connected(void)
{
    esp_wifi_info_t info;
    return esp_at_get_wifi_info(&info) && info.connected;
}

bool esp_at_sntp_init(void)
{
    if (!lock_commands()) return false;
    bool ok = command_locked("AT+CIPSNTPCFG=1,8\r\n", 2000U, false);
    unlock_commands();
    return ok;
}

bool esp_at_sntp_get_time(esp_date_time_t *date)
{
    if (date == NULL || !lock_commands()) return false;
    bool ok = command_locked("AT+CIPSNTPTIME?\r\n", 2000U, false) && esp_at_parse_sntp(response, date);
    unlock_commands();
    return ok;
}

const char *esp_at_http_get(const char *url)
{
    static const char prefix[] = "AT+HTTPCLIENT=2,1,\"";
    static const char suffix[] = "\",,,2\r\n";
    if (url == NULL || url[0] == '\0' || !lock_commands()) return NULL;
    http_response[0] = '\0';
    memcpy(command_buffer, prefix, sizeof prefix);
    bool ok = esp_at_quote(url, 512U, command_buffer + sizeof prefix - 1U,
                           sizeof command_buffer - sizeof prefix - sizeof suffix);
    if (ok) {
        memcpy(command_buffer + strlen(command_buffer), suffix, sizeof suffix);
        ok = command_locked(command_buffer, 20000U, true) &&
             esp_at_extract_http(response, http_response, sizeof http_response);
    }
    unlock_commands();
    return ok ? http_response : NULL;
}

const char *esp_at_http_get_bounded(const char *url, uint32_t timeout_ms)
{
    if (url == NULL || url[0] == '\0' || timeout_ms < 100U || timeout_ms > 3000U || !lock_commands()) return NULL;
    http_response[0] = '\0';
    static const char prefix[] = "AT+HTTPCGET=\"";
    memcpy(command_buffer, prefix, sizeof prefix);
    bool ok = esp_at_quote(url, 512U, command_buffer + sizeof prefix - 1U,
                           sizeof command_buffer - sizeof prefix - 40U);
    if (ok) {
        size_t length = strlen(command_buffer);
        int count = snprintf(command_buffer + length, sizeof command_buffer - length,
                             "\",2048,2048,%lu\r\n", (unsigned long)timeout_ms);
        ok = count > 0 && length + (size_t)count < 256U &&
             exchange_locked(command_buffer, timeout_ms + 1000U, true, false) == ACK_OK &&
             esp_at_extract_http(response, http_response, sizeof http_response);
    }
    unlock_commands();
    return ok ? http_response : NULL;
}

static const char *mqtt_step = "NONE";
static at_ack_t mqtt_reply;
static uint32_t mqtt_at_error;

static void mqtt_step_begin(const char *step)
{
    mqtt_step = step; mqtt_reply = ACK_NONE; mqtt_at_error = 0U;
}

/* Read only a whole SYSLOG line and expose only its numeric code. Raw AT
 * responses may contain credentials and must never reach application logs. */
static void mqtt_capture_reply(at_ack_t ack)
{
    static const char prefix[] = "ERR CODE:0x";
    mqtt_reply = ack; mqtt_at_error = 0U;
    bool seen = false;
    for (const char *p = response; *p != '\0';) {
        const char *end = p;
        while (*end != '\0' && *end != '\r' && *end != '\n') ++end;
        if ((size_t)(end - p) == sizeof prefix - 1U + 8U &&
            memcmp(p, prefix, sizeof prefix - 1U) == 0) {
            uint32_t code = 0U;
            for (const char *digit = p + sizeof prefix - 1U; digit < end; ++digit) {
                unsigned value;
                if (*digit >= '0' && *digit <= '9') value = (unsigned)(*digit - '0');
                else if (*digit >= 'a' && *digit <= 'f') value = (unsigned)(*digit - 'a') + 10U;
                else if (*digit >= 'A' && *digit <= 'F') value = (unsigned)(*digit - 'A') + 10U;
                else { mqtt_at_error = 0U; return; }
                code = (code << 4U) | value;
            }
            if (seen) { mqtt_at_error = 0U; return; }
            seen = true; mqtt_at_error = code;
        }
        p = end;
        while (*p == '\r' || *p == '\n') ++p;
    }
}

static bool mqtt_ack(at_ack_t ack, at_ack_t expected)
{
    mqtt_capture_reply(ack);
    bool ok = ack == expected && synchronized;
    if (mqtt_error != ESP_MQTT_ERROR_QUEUE_OVERFLOW)
        mqtt_error = ok ? ESP_MQTT_ERROR_NONE : synchronized ? ESP_MQTT_ERROR_COMMAND : ESP_MQTT_ERROR_TRANSPORT;
    return ok;
}

static bool mqtt_command(const char *command, const char *step)
{
    mqtt_step_begin(step);
    return mqtt_ack(exchange_locked(command, ESP_MQTT_WAIT_MS, false, false), ACK_OK);
}

static bool mqtt_data(const char *command, const void *data, size_t length, bool publish,
                      const char *prompt_step, const char *data_step)
{
    mqtt_step_begin(prompt_step);
    send_debt = (uint16_t)length;
    at_ack_t ack = transfer_locked(command, (uint16_t)strlen(command), publish ? ESP_MQTT_PUBLISH_PROMPT_MS : 3000U,
                                  REPLY_PROMPT, true, -1, 0U);
    if (ack != ACK_PROMPT) {
        (void)mqtt_ack(ack, ACK_PROMPT);
        if (ack == ACK_ERROR || ack == ACK_BUSY || ack == ACK_READY) send_debt = 0U;
        else (void)recover_send_locked();
        return false;
    }
    /* A module reboot/queue overflow can arrive after the prompt. Complete an
     * uncertain data-entry window using neutral padding before resetting. */
    if (!synchronized || (publish && !mqtt_connected)) {
        (void)mqtt_ack(ACK_IO, ACK_OK);
        (void)recover_send_locked();
        return false;
    }
    mqtt_step_begin(data_step);
    ack = transfer_locked(data, (uint16_t)length, publish ? ESP_MQTT_PUBLISH_DONE_MS : 3000U,
                          publish ? REPLY_MQTT_PUB : REPLY_DATA_OK, false, -1, 0U);
    bool ok = mqtt_ack(ack, publish ? ACK_MQTT_PUB_OK : ACK_OK);
    if (send_debt != 0U) (void)recover_send_locked();
    if (publish && !mqtt_connected && synchronized) mqtt_error = ESP_MQTT_ERROR_DISCONNECTED;
    return ok && synchronized && (!publish || mqtt_connected);
}

static at_ack_t mqtt_read_ca(const uint8_t *certificate, size_t length, const char *step)
{
    mqtt_step_begin(step);
    ca_expected = certificate; ca_expected_length = length;
    ca_seen = ca_match = false;
    ca_total = ca_stored = 0U;
    static const char command[] = "AT+SYSMFG=1,\"mqtt_ca\",\"mqtt_ca\"\r\n";
    at_ack_t ack = transfer_locked(command, (uint16_t)(sizeof command - 1U), 3000U, REPLY_CA, true, -1, 0U);
    mqtt_capture_reply(ack);
    return ack;
}

static bool mqtt_ca(const uint8_t *certificate, size_t length)
{
    at_ack_t ack = mqtt_read_ca(certificate, length, "CA_READ");
    if (ack == ACK_OK && ca_seen && ca_match && ca_remaining == 0U && ca_total == length) return true;
    if (!synchronized || (ack != ACK_OK && ack != ACK_ERROR)) return false;
    (void)snprintf(command_buffer, sizeof command_buffer,
                   "AT+SYSMFG=2,\"mqtt_ca\",\"mqtt_ca\",8,%lu\r\n", (unsigned long)length);
    if (!mqtt_data(command_buffer, certificate, length, false, "CA_WRITE_PROMPT", "CA_WRITE_DATA")) return false;
    ack = mqtt_read_ca(certificate, length, "CA_VERIFY");
    return ack == ACK_OK && ca_seen && ca_match && ca_remaining == 0U && ca_total == length;
}

static bool mqtt_host_valid(const char *host)
{
    if (host == NULL || host[0] == '\0') return false;
    for (size_t n = 0U; ; ++n) {
        unsigned char c = (unsigned char)host[n];
        if (c == '\0') return n <= 128U;
        if (n == 128U || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '.')) return false;
    }
}

bool esp_at_mqtt_configure(const esp_mqtt_config_t *config)
{
    mqtt_step_begin("ARGUMENT");
    char client[129], username[129];
    size_t token_length = 0U;
    if (config == NULL || !mqtt_host_valid(config->host) || config->port == 0U ||
        !esp_at_quote(config->client_id, 64U, client, sizeof client) || client[0] == '\0' ||
        !esp_at_quote(config->username, 64U, username, sizeof username) || username[0] == '\0' ||
        config->password == NULL || config->ca_pem == NULL || config->ca_length == 0U || config->ca_length > ESP_MQTT_CA_LIMIT) {
        mqtt_error = ESP_MQTT_ERROR_ARGUMENT;
        return false;
    }
    const char *server_name = config->tls_server_name != NULL ? config->tls_server_name : config->host;
    if (server_name[0] != '\0' && !mqtt_host_valid(server_name)) {
        mqtt_error = ESP_MQTT_ERROR_ARGUMENT;
        return false;
    }
    while (token_length <= ESP_MQTT_TOKEN_LIMIT && config->password[token_length] != '\0') ++token_length;
    if (token_length == 0U || token_length > ESP_MQTT_TOKEN_LIMIT) { mqtt_error = ESP_MQTT_ERROR_ARGUMENT; return false; }
    if (!lock_commands()) { mqtt_error = ESP_MQTT_ERROR_TRANSPORT; return false; }
    taskENTER_CRITICAL();
    mqtt_invalidate();
    mqtt_error = ESP_MQTT_ERROR_NONE;
    memset(mqtt_queue, 0, sizeof mqtt_queue);
    taskEXIT_CRITICAL();
    bool ok = mqtt_command("AT+SYSLOG=1\r\n", "SYSLOG");
    if (ok) {
        ok = mqtt_ca(config->ca_pem, config->ca_length);
        if (!ok && synchronized) mqtt_error = ESP_MQTT_ERROR_TLS_CA;
    }
    if (ok) {
        ok = mqtt_command("AT+MQTTCLEAN=0\r\n", "MQTTCLEAN");
        /* Official ESP32-C3 AT4.1.1 returns ERROR/0x010a6003 for a fresh or
         * already-clean client. Only this explicit state is idempotent;
         * other errors, busy, timeouts and UART faults still abort setup. */
        if (!ok && synchronized && mqtt_reply == ACK_ERROR &&
            mqtt_at_error == UINT32_C(0x010a6003)) ok = true;
    }
    if (ok) {
        int count = snprintf(command_buffer, sizeof command_buffer,
                             "AT+MQTTUSERCFG=0,3,\"%s\",\"%s\",\"\",0,0,\"\"\r\n", client, username);
        ok = count > 0 && count < 256 && mqtt_command(command_buffer, "MQTTUSERCFG");
        if (!ok && count >= 256) mqtt_error = ESP_MQTT_ERROR_ARGUMENT;
    }
    if (ok) {
        (void)snprintf(command_buffer, sizeof command_buffer, "AT+MQTTLONGPASSWORD=0,%lu\r\n", (unsigned long)token_length);
        ok = mqtt_data(command_buffer, config->password, token_length, false,
                       "MQTTLONGPASSWORD_PROMPT", "MQTTLONGPASSWORD_DATA");
    }
    if (ok && server_name[0] != '\0') {
        (void)snprintf(command_buffer, sizeof command_buffer, "AT+MQTTSNI=0,\"%s\"\r\n", server_name);
        ok = mqtt_command(command_buffer, "MQTTSNI");
    }
    if (ok) ok = mqtt_command("AT+MQTTCONNCFG=0,60,0,\"\",\"\",0,0\r\n", "MQTTCONNCFG");
    if (ok) {
        memcpy(mqtt_host, config->host, strlen(config->host) + 1U);
        mqtt_port = config->port;
        mqtt_configured = synchronized;
    }
    unlock_commands();
    return ok && mqtt_configured;
}

bool esp_at_mqtt_connect(uint32_t timeout_ms)
{
    mqtt_step_begin("MQTTCONN");
    if (timeout_ms == 0U || timeout_ms > 20000U) { mqtt_error = ESP_MQTT_ERROR_ARGUMENT; return false; }
    if (!lock_commands()) return false;
    bool ok = mqtt_configured && synchronized;
    mqtt_connected = false;
    if (ok) {
        (void)snprintf(command_buffer, sizeof command_buffer, "AT+MQTTCONN=0,\"%s\",%u,0\r\n", mqtt_host, (unsigned)mqtt_port);
        ok = mqtt_ack(exchange_locked(command_buffer, timeout_ms, false, false), ACK_OK);
    }
    uint32_t sequence = mqtt_disconnect_sequence;
    if (ok) ok = mqtt_command("AT+MQTTCONN?\r\n", "MQTTCONN_STATE");
    if (ok) {
        const char *p = response;
        while (p[0] == '\r' && p[1] == '\n') p += 2U;
        size_t host_length = strlen(mqtt_host);
        static const char prefix[] = "+MQTTCONN:0,";
        ok = strncmp(p, prefix, sizeof prefix - 1U) == 0;
        if (ok) {
            p += sizeof prefix - 1U;
            ok = p[0] >= '4' && p[0] <= '6' && strncmp(p + 1U, ",3,\"", 4U) == 0;
            if (ok) {
                p += 5U;
                ok = strncmp(p, mqtt_host, host_length) == 0 && p[host_length] == '"' && p[host_length + 1U] == ',';
                if (ok) {
                    p += host_length + 2U;
                    /* AT4.1.1 formats the numeric port inside quotes. Retain
                     * support for the unquoted form used by other versions. */
                    bool quoted_port = *p == '"';
                    if (quoted_port) ++p;
                    uint32_t port = 0U;
                    unsigned digits = 0U;
                    while (*p >= '0' && *p <= '9' && digits < 6U) { port = port * 10U + (unsigned)(*p++ - '0'); ++digits; }
                    bool port_closed = !quoted_port || *p == '"';
                    if (quoted_port && port_closed) ++p;
                    ok = digits != 0U && digits <= 5U && port_closed && *p == ',' && port == mqtt_port;
                }
            }
        }
    }
    taskENTER_CRITICAL();
    mqtt_connected = ok && synchronized && mqtt_configured && sequence == mqtt_disconnect_sequence;
    ok = mqtt_connected;
    if (!ok && mqtt_error == ESP_MQTT_ERROR_NONE) mqtt_error = ESP_MQTT_ERROR_DISCONNECTED;
    taskEXIT_CRITICAL();
    unlock_commands();
    return ok;
}

bool esp_at_mqtt_subscribe(const char *topic, uint8_t qos)
{
    mqtt_step_begin("MQTTSUB");
    char escaped[257];
    if (qos > 2U || !esp_at_quote(topic, ESP_MQTT_TOPIC_CAPACITY, escaped, sizeof escaped) || escaped[0] == '\0') {
        mqtt_error = ESP_MQTT_ERROR_ARGUMENT; return false;
    }
    if (!lock_commands()) return false;
    int count = snprintf(command_buffer, sizeof command_buffer, "AT+MQTTSUB=0,\"%s\",%u\r\n", escaped, (unsigned)qos);
    bool ok = mqtt_connected && synchronized && count > 0 && count < 256 && mqtt_command(command_buffer, "MQTTSUB") && mqtt_connected;
    if (count <= 0 || count >= 256) mqtt_error = ESP_MQTT_ERROR_ARGUMENT;
    if (!mqtt_connected && synchronized) mqtt_error = ESP_MQTT_ERROR_DISCONNECTED;
    unlock_commands();
    return ok;
}

bool esp_at_mqtt_publish(const char *topic, const void *data, size_t length, uint8_t qos, bool retain)
{
    mqtt_step_begin("MQTTPUBRAW_ARGUMENT");
    char escaped[257];
    if (data == NULL || length == 0U || length > ESP_MQTT_PAYLOAD_CAPACITY || qos > 2U ||
        !esp_at_quote(topic, ESP_MQTT_TOPIC_CAPACITY, escaped, sizeof escaped) || escaped[0] == '\0') {
        mqtt_error = ESP_MQTT_ERROR_ARGUMENT; return false;
    }
    if (!lock_commands()) return false;
    int count = snprintf(command_buffer, sizeof command_buffer, "AT+MQTTPUBRAW=0,\"%s\",%lu,%u,%u\r\n",
                         escaped, (unsigned long)length, (unsigned)qos, retain ? 1U : 0U);
    bool ok = mqtt_connected && synchronized && count > 0 && count < 256 &&
              mqtt_data(command_buffer, data, length, true, "MQTTPUBRAW_PROMPT", "MQTTPUBRAW_DATA");
    if (count <= 0 || count >= 256) mqtt_error = ESP_MQTT_ERROR_ARGUMENT;
    unlock_commands();
    return ok;
}

bool esp_at_mqtt_disconnect(void)
{
    if (!lock_commands()) return false;
    bool ok = mqtt_command("AT+MQTTCLEAN=0\r\n", "MQTT_DISCONNECT");
    taskENTER_CRITICAL(); mqtt_invalidate(); taskEXIT_CRITICAL();
    unlock_commands();
    return ok;
}

bool esp_at_mqtt_connected(void) { return synchronized && mqtt_configured && mqtt_connected; }
esp_mqtt_error_t esp_at_mqtt_last_error(void) { return mqtt_error; }
esp_mqtt_diagnostic_t esp_at_mqtt_diagnostic(void)
{
    static const char *const names[] = {"NONE", "OK", "ERROR", "BUSY", "READY", "OVERFLOW", "IO", "TIMEOUT",
                                       "PROMPT", "SEND_OK", "MQTT_PUB_OK"};
    esp_mqtt_diagnostic_t result = {mqtt_step, names[mqtt_reply], mqtt_at_error};
    return result;
}

bool esp_at_mqtt_pending(void)
{
    taskENTER_CRITICAL();
    bool pending = mqtt_count != 0U || mqtt_rx_state != MQTT_RX_IDLE ||
        (line_length != 0U && line_length <= sizeof "+MQTTSUBRECV:" - 1U &&
         memcmp(line, "+MQTTSUBRECV:", line_length) == 0);
    taskEXIT_CRITICAL();
    return pending;
}

bool esp_at_mqtt_poll(esp_mqtt_message_t *message)
{
    if (message == NULL) return false;
    taskENTER_CRITICAL();
    bool found = synchronized && mqtt_count != 0U;
    if (found) {
        *message = mqtt_queue[mqtt_head];
        mqtt_head = (uint8_t)((mqtt_head + 1U) % ESP_MQTT_QUEUE_COUNT);
        --mqtt_count;
    }
    taskEXIT_CRITICAL();
    return found;
}

bool esp_at_is_ready(void) { return synchronized; }

bool esp_at_server_start(uint16_t port, char ip[16])
{
    if (port == 0U || ip == NULL || !lock_commands()) return false;
    ip[0] = '\0';
    taskENTER_CRITICAL();
    invalidate_server();
    taskEXIT_CRITICAL();
    bool ok = command_locked("AT+CIPMUX=1\r\n", 2000U, false) &&
              command_locked("AT+CIPDINFO=0\r\n", 2000U, false);
    if (ok) {
        at_ack_t ack = exchange_locked("AT+CIPRECVMODE=1\r\n", 2000U, false, false);
        /* ESP-AT 4.x renamed this command and added an all-links ID of 5.
         * Only an explicit rejection permits fallback; an uncertain timeout
         * must never be followed by another command. */
        ok = ack == ACK_OK || (ack == ACK_ERROR &&
             command_locked("AT+CIPRECVTYPE=5,1\r\n", 2000U, false));
    }
    if (ok) {
        (void)snprintf(command_buffer, sizeof command_buffer, "AT+CIPSERVER=1,%u\r\n", (unsigned)port);
        ok = command_locked(command_buffer, 2000U, false) &&
             command_locked("AT+CIPSTO=60\r\n", 2000U, false) &&
             command_locked("AT+CIFSR\r\n", 2000U, false) && esp_at_parse_station_ip(response, ip);
    }
    taskENTER_CRITICAL();
    server_ready = ok && synchronized;
    ok = server_ready;
    taskEXIT_CRITICAL();
    unlock_commands();
    return ok;
}

bool esp_at_server_ready(void) { return server_ready && synchronized; }

static bool valid_link(uint8_t id, uint32_t generation)
{
    return id < ESP_SERVER_LINKS && server_ready && synchronized &&
           server_links[id].active && server_links[id].generation == generation;
}

bool esp_at_server_poll(esp_server_chunk_t *chunk)
{
    if (chunk == NULL || !esp_at_server_ready() || !lock_commands()) return false;
    bool found = false;
    uint8_t id = 0U;
    uint16_t requested = 0U;
    uint32_t generation = 0U, notification = 0U;
    taskENTER_CRITICAL();
    /* Snapshots coalesce rapid CLOSED/CONNECT sequences without a queue that
     * could overflow while the worker is inside a 20-second HTTPCLIENT call. */
    for (unsigned n = 0U; n < ESP_SERVER_LINKS; ++n) {
        id = (uint8_t)((poll_cursor + n) % ESP_SERVER_LINKS);
        server_link_t *link = &server_links[id];
        if (link->generation != link->reported_generation || link->active != link->reported_active) {
            chunk->event = link->active ? ESP_SERVER_EVENT_CONNECTED : ESP_SERVER_EVENT_CLOSED;
            chunk->link = id;
            chunk->generation = link->generation;
            chunk->length = 0U;
            link->reported_generation = link->generation;
            link->reported_active = link->active;
            poll_cursor = (uint8_t)((id + 1U) % ESP_SERVER_LINKS);
            found = true;
            break;
        }
    }
    if (!found) {
        for (unsigned n = 0U; n < ESP_SERVER_LINKS; ++n) {
            id = (uint8_t)((poll_cursor + n) % ESP_SERVER_LINKS);
            server_link_t *link = &server_links[id];
            if (link->active && link->pending != 0U) {
                requested = link->pending > ESP_SERVER_CHUNK_CAPACITY ? ESP_SERVER_CHUNK_CAPACITY : (uint16_t)link->pending;
                generation = link->generation;
                notification = link->notification;
                break;
            }
        }
    }
    taskEXIT_CRITICAL();
    if (found || requested == 0U) { unlock_commands(); return found; }

    (void)snprintf(command_buffer, sizeof command_buffer, "AT+CIPRECVDATA=%u,%u\r\n", (unsigned)id, (unsigned)requested);
    passive_limit = requested;
    at_ack_t ack = transfer_locked(command_buffer, (uint16_t)strlen(command_buffer), 2000U, REPLY_PASSIVE, true, id, generation);
    taskENTER_CRITICAL();
    if (valid_link(id, generation)) {
        server_link_t *link = &server_links[id];
        if (ack == ACK_OK && passive_seen && passive_length == passive_stored) {
            if (link->notification == notification) {
                link->pending = passive_length >= link->pending ? 0U : link->pending - passive_length;
                if (passive_length == 0U) link->pending = 0U;
            }
            chunk->event = ESP_SERVER_EVENT_DATA;
            chunk->link = id;
            chunk->generation = generation;
            chunk->length = passive_length;
            /* The ISR cannot start another passive transfer until this worker
             * issues a command. Copy after the critical section below. */
            found = passive_length != 0U;
        } else if (ack == ACK_ERROR) link->pending = 0U;
    }
    poll_cursor = (uint8_t)((id + 1U) % ESP_SERVER_LINKS);
    taskEXIT_CRITICAL();
    if (found) memcpy(chunk->data, passive_data, passive_length);
    unlock_commands();
    return found;
}

bool esp_at_server_send(uint8_t link, uint32_t generation, const void *data, size_t length)
{
    if ((data == NULL && length != 0U) || !lock_commands()) return false;
    const uint8_t *bytes = data;
    bool ok = valid_link(link, generation);
    while (ok && length != 0U) {
        uint16_t amount = length > ESP_SERVER_SEND_LIMIT ? ESP_SERVER_SEND_LIMIT : (uint16_t)length;
        (void)snprintf(command_buffer, sizeof command_buffer, "AT+CIPSEND=%u,%u\r\n", (unsigned)link, (unsigned)amount);
        send_debt = amount;
        at_ack_t ack = transfer_locked(command_buffer, (uint16_t)strlen(command_buffer), 2000U, REPLY_PROMPT, true, link, generation);
        ok = ack == ACK_PROMPT;
        if (!ok) {
            if (ack == ACK_ERROR || ack == ACK_BUSY || ack == ACK_READY) send_debt = 0U;
            else (void)recover_send_locked();
        }
        if (ok && !valid_link(link, generation)) {
            /* The module may now be in data-entry mode. Reinitialization is
             * required before any subsequent AT command; never send the old
             * response to a new connection that reused this numeric ID. */
            (void)recover_send_locked();
            ok = false;
        }
        if (ok) {
            ack = transfer_locked(bytes, amount, 2000U, REPLY_SEND, false, link, generation);
            ok = ack == ACK_SEND_OK && valid_link(link, generation);
            if (send_debt != 0U) (void)recover_send_locked();
        }
        bytes += amount;
        length -= amount;
    }
    unlock_commands();
    return ok;
}

bool esp_at_server_close(uint8_t link, uint32_t generation)
{
    if (!lock_commands()) return false;
    bool ok = valid_link(link, generation);
    if (ok) {
        (void)snprintf(command_buffer, sizeof command_buffer, "AT+CIPCLOSE=%u\r\n", (unsigned)link);
        ok = transfer_locked(command_buffer, (uint16_t)strlen(command_buffer), 2000U,
                             REPLY_OK, true, link, generation) == ACK_OK;
        taskENTER_CRITICAL();
        if (ok && server_links[link].generation == generation) {
            server_links[link].active = false;
            server_links[link].pending = 0U;
        }
        taskEXIT_CRITICAL();
    }
    unlock_commands();
    return ok;
}
