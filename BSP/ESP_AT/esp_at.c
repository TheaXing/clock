#include "esp_at.h"
#include "esp_at_parse.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "usart.h"

#include <stdio.h>
#include <string.h>

#define ESP_RESPONSE_CAPACITY 2048U
#define ESP_COMMAND_CAPACITY  1024U
#define ESP_LOCK_TIMEOUT_MS   1000U

typedef enum {
    ACK_NONE, ACK_OK, ACK_ERROR, ACK_BUSY, ACK_READY, ACK_OVERFLOW, ACK_IO, ACK_TIMEOUT
} at_ack_t;

typedef enum { REPLY_OK, REPLY_HTTP, REPLY_READY } reply_kind_t;

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
static reply_kind_t reply_kind;
static bool http_header;
static unsigned http_digits;
static uint32_t http_length;
static uint32_t http_remaining;

static void transport_fault(void)
{
    synchronized = false;
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
    if (!receiving) return;
    if (response_length >= sizeof response - 1U || byte == '\0') {
        transport_fault();
        notify_from_isr(ACK_OVERFLOW, woken);
    } else {
        response[response_length++] = (char)byte;
    }
}

static void receive_byte(uint8_t byte, BaseType_t *woken)
{
    ++rx_sequence;
    /* HTTP payload bytes are length-framed. Terminal-looking payload lines
     * must never enter the line/ACK parser. */
    if (http_remaining != 0U) {
        append_response(byte, woken);
        --http_remaining;
        return;
    }
    append_response(byte, woken);
    if (http_header) {
        if (byte >= '0' && byte <= '9') {
            unsigned digit = (unsigned)(byte - '0');
            if (++http_digits > 5U || http_length > ESP_RESPONSE_CAPACITY / 10U ||
                (http_length == ESP_RESPONSE_CAPACITY / 10U && digit > ESP_RESPONSE_CAPACITY % 10U)) {
                transport_fault();
                http_header = false;
                notify_from_isr(ACK_OVERFLOW, woken);
            } else {
                http_length = http_length * 10U + digit;
            }
            return;
        }
        if (byte == ',' && http_digits != 0U) {
            http_remaining = http_length;
            http_header = false;
            line_length = 0U;
            return;
        }
        /* Malformed framing must not expose payload terminal-looking lines. */
        http_header = false;
        transport_fault();
        notify_from_isr(ACK_IO, woken);
    }
    if (line_length < sizeof line) line[line_length] = (char)byte;
    if (line_length <= sizeof line) ++line_length;
    if (receiving && reply_kind == REPLY_HTTP && line_length == sizeof "+HTTPCLIENT:" - 1U &&
        memcmp(line, "+HTTPCLIENT:", sizeof "+HTTPCLIENT:" - 1U) == 0) {
        http_header = true;
        http_digits = 0U;
        http_length = 0U;
        return;
    }
    if (byte != '\n') return;

    size_t length = line_length;
    if (length <= sizeof line) {
        if (length != 0U && line[length - 1U] == '\n') --length;
        if (length != 0U && line[length - 1U] == '\r') --length;
        if (length == 2U && memcmp(line, "OK", 2U) == 0 && reply_kind != REPLY_READY)
            notify_from_isr(ACK_OK, woken);
        else if ((length == 5U && memcmp(line, "ERROR", 5U) == 0) ||
                 (length == 4U && memcmp(line, "FAIL", 4U) == 0)) notify_from_isr(ACK_ERROR, woken);
        else if (length >= 4U && memcmp(line, "busy", 4U) == 0) notify_from_isr(ACK_BUSY, woken);
        else if (length == 5U && memcmp(line, "ready", 5U) == 0) {
            transport_fault(); /* Also notice an unsolicited module reboot. */
            notify_from_isr(ACK_READY, woken);
        }
    }
    line_length = 0U;
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

static at_ack_t exchange_locked(const char *command, uint32_t timeout_ms, bool http, bool wait_ready)
{
    if (!wait_ready && !synchronized) return ACK_IO;
    /* Discard unsolicited/stale lines before arming the next transaction. */
    if (!wait_quiet(20U, 300U)) { transport_fault(); return ACK_IO; }
    taskENTER_CRITICAL();
    /* A ready/error interrupt may have arrived during the quiet wait. */
    if (!wait_ready && !synchronized) {
        taskEXIT_CRITICAL();
        return ACK_IO;
    }
    receiving = false;
    while (xSemaphoreTake(response_semaphore, 0U) == pdTRUE) { }
    response_length = 0U;
    response_ack = ACK_NONE;
    line_length = 0U;
    http_header = false;
    http_remaining = 0U;
    reply_kind = wait_ready ? REPLY_READY : http ? REPLY_HTTP : REPLY_OK;
    receiving = true;
    taskEXIT_CRITICAL();

    /* TX storage is independent of the ISR-owned response. Reset and drain
     * happen before TX, because a response may arrive before TX returns. */
    HAL_StatusTypeDef tx = HAL_UART_Transmit(&huart2, (uint8_t *)(void *)command,
                                           (uint16_t)strlen(command), 1000U);
    bool signalled = tx == HAL_OK && xSemaphoreTake(response_semaphore, ticks(timeout_ms)) == pdTRUE;
    taskENTER_CRITICAL();
    receiving = false;
    at_ack_t ack = response_ack;
    taskEXIT_CRITICAL();
    response[response_length] = '\0';
    if (tx != HAL_OK) ack = ACK_IO;
    else if (!signalled || ack == ACK_NONE) ack = ACK_TIMEOUT;
    else if (!wait_ready && !synchronized) ack = ACK_IO;
    if (ack == ACK_TIMEOUT || ack == ACK_IO || ack == ACK_OVERFLOW || ack == ACK_READY) transport_fault();
    return ack;
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

bool esp_at_init(void)
{
    if (!lock_commands()) return false;
    taskENTER_CRITICAL();
    receiving = false;
    transport_fault();
    line_length = 0U;
    http_header = false;
    http_remaining = 0U;
    reply_kind = REPLY_OK;
    (void)HAL_UART_AbortReceive(&huart2);
    __HAL_UART_CLEAR_OREFLAG(&huart2);
    HAL_StatusTypeDef status = HAL_UART_Receive_IT(&huart2, &uart_rx_byte, 1U);
    taskEXIT_CRITICAL();
    bool ready = status == HAL_OK && wait_quiet(100U, 1000U);
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
