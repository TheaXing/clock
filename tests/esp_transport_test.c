#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "usart.h"
#include "esp_at.h"
#include "weather.h"

UART_HandleTypeDef huart2;
struct TestSemaphore { int tokens; };
static TickType_t tick;
static uint8_t *rx_target;
static unsigned arm_count;
static const char *expected_tx[32];
static const char *rx_reply[32];
static const char *rx_wait_reply[32];
static size_t expected_length[32], reply_length[32], wait_reply_length[32];
static const char *pending_reply;
static size_t pending_reply_length;
static unsigned script_length, script_index;
static bool inject_uart_error;
static void receive_text(const char *text);
static void receive_bytes(const void *bytes, size_t length);

TickType_t xTaskGetTickCount(void) { return tick; }
void vTaskDelay(TickType_t ticks)
{
    tick += ticks;
}
BaseType_t xTaskGetSchedulerState(void) { return taskSCHEDULER_RUNNING; }
SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    SemaphoreHandle_t s = calloc(1, sizeof *s);
    assert(s); s->tokens = 1; return s;
}
SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    SemaphoreHandle_t s = calloc(1, sizeof *s);
    assert(s); return s;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t wait)
{
    if (!s->tokens && wait && pending_reply) {
        const char *reply = pending_reply;
        pending_reply = NULL;
        receive_bytes(reply, pending_reply_length); /* Arrives after blocking TX returned. */
    }
    if (s->tokens) { --s->tokens; return pdTRUE; }
    tick += wait; return pdFALSE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t s) { s->tokens = 1; return pdTRUE; }
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t s, BaseType_t *woken)
{
    assert(*woken == pdFALSE); s->tokens = 1; return pdTRUE;
}
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *u, uint8_t *data, uint16_t length)
{
    assert(u == &huart2 && length == 1); rx_target = data; ++arm_count; return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *u) { assert(u == &huart2); rx_target = NULL; return HAL_OK; }
static void receive_text(const char *text)
{
    receive_bytes(text, strlen(text));
}
static void receive_bytes(const void *bytes, size_t length)
{
    const uint8_t *text = bytes;
    for (size_t n = 0; n < length; ++n) {
        assert(rx_target); *rx_target = text[n];
        HAL_UART_RxCpltCallback(&huart2);
    }
}
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *u, uint8_t *data, uint16_t length, uint32_t timeout)
{
    assert(u == &huart2 && timeout > 0 && timeout < 10000);
    assert(script_index < script_length);
    assert(expected_length[script_index] == length);
    assert(memcmp(expected_tx[script_index], data, length) == 0);
    const char *reply = rx_reply[script_index];
    size_t reply_size = reply_length[script_index];
    pending_reply_length = wait_reply_length[script_index];
    pending_reply = rx_wait_reply[script_index++];
    if (inject_uart_error) { inject_uart_error = false; HAL_UART_ErrorCallback(u); }
    /* The full reply may precede return from blocking TX: catches resetting
     * RX state or draining its semaphore after HAL_UART_Transmit. */
    if (reply) receive_bytes(reply, reply_size);
    return HAL_OK;
}
static void script(const char *tx, const char *reply)
{
    assert(script_length < 32); expected_tx[script_length] = tx; rx_reply[script_length] = reply;
    expected_length[script_length] = strlen(tx);
    reply_length[script_length] = reply ? strlen(reply) : 0U;
    wait_reply_length[script_length] = 0U;
    rx_wait_reply[script_length++] = NULL;
}
static void script_delayed(const char *tx, const char *early, const char *late)
{
    script(tx, early); rx_wait_reply[script_length - 1] = late;
    wait_reply_length[script_length - 1] = late ? strlen(late) : 0U;
}
static void script_done(void)
{
    assert(script_index == script_length && pending_reply == NULL); script_index = script_length = 0;
}
static void initialize(void)
{
    /* The first OK acknowledges RST; a second delayed OK may belong to an old
     * timed-out operation. Neither can complete the ready-only handshake. */
    script_delayed("AT+RST\r\n", "OK\r\n", "OK\r\nready\r\n");
    script("ATE0\r\n", "OK\r\n");
    assert(esp_at_init()); script_done();
}
int main(void)
{
    initialize();
    script_delayed("AT+RST\r\n", NULL, "OK\r\n");
    assert(!esp_at_init()); script_done(); /* A stale OK cannot synchronize. */
    initialize();
    script("AT+RST\r\n", "busy p...\r\n");
    script("AT+RST\r\n", "ERROR\r\n");
    script_delayed("AT+RST\r\n", "OK\r\n", "ready\r\n");
    script("ATE0\r\n", "OK\r\n");
    assert(esp_at_init()); script_done(); /* Explicit rejections retry finitely. */
    script("AT+CWMODE=1\r\n", "busy p...\r\n");
    script("AT+CWMODE=1\r\n", "OK\r\n");
    assert(esp_at_wifi_init()); script_done();
    script("AT+CWJAP=\"a\\\"b\",\"p\\\\q\",\"00:11:22:33:44:55\"\r\n", "WIFI CONNECTED\r\nWIFI GOT IP\r\nOK\r\n");
    assert(esp_at_connect_wifi("a\"b", "p\\q", "00:11:22:33:44:55")); script_done();
    assert(!esp_at_connect_wifi("AP\r\nAT+RESTORE", "pw", NULL));
    script("AT+CWSTATE?\r\n", "+CWSTATE:2,\"Clock AP\"\r\nOK\r\n");
    script("AT+CWJAP?\r\n", "+CWJAP:\"Clock AP\",\"00:11:22:33:44:55\",6,-48\r\nOK\r\n");
    esp_wifi_info_t wifi;
    assert(esp_at_get_wifi_info(&wifi)); assert(wifi.connected && wifi.channel == 6); script_done();
    script("AT+CIPSNTPCFG=1,8\r\n", "OK\r\n"); assert(esp_at_sntp_init()); script_done();
    script("AT+CIPSNTPTIME?\r\n", "+CIPSNTPTIME:Thu Feb 29 23:59:59 2024\r\nOK\r\n");
    esp_date_time_t date;
    assert(esp_at_sntp_get_time(&date)); assert(date.year == 2024 && date.day == 29); script_done();
    script("AT+HTTPCLIENT=2,1,\"https://example.com\",,,2\r\n", "+HTTPCLIENT:8,\r\nOK\r\nx!\r\nOK\r\n");
    const char *http = esp_at_http_get("https://example.com");
    assert(http && strcmp(http, "\r\nOK\r\nx!") == 0); script_done();
    script("AT+CWMODE=1\r\n", "OK\r\n"); assert(esp_at_wifi_init()); script_done();
    assert(strstr(http, "x!")); /* Non-HTTP commands cannot clobber it. */
    /* A normal firmware transfer may split inside JSON strings. The returned
     * body must join bytes before weather parsing, rather than insert headers. */
    const char first[] = "{\"results\":[{\"location\":{\"name\":\"Hefei\",\"path\":\"China,Hefei\"},\"now\":{\"text\":\"Cl";
    const char second[] = "oudy\",\"code\":\"4\",\"temperature\":\"23.5\"}}]}";
    char framed[512];
    (void)snprintf(framed, sizeof framed, "+HTTPCLIENT:%zu,%s\r\n+HTTPCLIENT:%zu,%s\r\nOK\r\n", strlen(first), first, strlen(second), second);
    script("AT+HTTPCLIENT=2,1,\"https://example.com\",,,2\r\n", framed);
    http = esp_at_http_get("https://example.com");
    assert(http && strcmp(http, "{\"results\":[{\"location\":{\"name\":\"Hefei\",\"path\":\"China,Hefei\"},\"now\":{\"text\":\"Cloudy\",\"code\":\"4\",\"temperature\":\"23.5\"}}]}") == 0);
    weather_info_t weather;
    assert(parse_seniverse_response(http, &weather));
    assert(strcmp(weather.city, "Hefei") == 0 && strcmp(weather.weather, "Cloudy") == 0);
    assert(weather.weather_code == 4 && weather.temperature == 23.5f);
    script_done();
    static char oversized[3000]; memset(oversized, 'x', sizeof oversized - 1);
    unsigned before = arm_count;
    script("AT+HTTPCLIENT=2,1,\"https://example.com\",,,2\r\n", oversized);
    assert(esp_at_http_get("https://example.com") == NULL); script_done();
    assert(arm_count - before >= sizeof oversized - 1); /* Overflow keeps RX armed. */
    assert(!esp_at_wifi_init()); /* Resynchronization required after overflow. */
    initialize();
    script("AT+CWMODE=1\r\n", NULL); inject_uart_error = true;
    before = arm_count; assert(!esp_at_wifi_init()); script_done();
    assert(arm_count > before); assert(!esp_at_wifi_init()); initialize();
    /* An unsolicited terminal line while idle must not satisfy a new command. */
    receive_text("OK\r\n");
    script("AT+CWMODE=1\r\n", NULL); assert(!esp_at_wifi_init()); script_done();
    assert(!esp_at_wifi_init()); initialize();
    receive_text("ready\r\n"); assert(!esp_at_wifi_init()); initialize();
    script("AT+CWMODE=1\r\n", "OK\r\nready\r\n");
    assert(!esp_at_wifi_init()); script_done();
    puts("esp transport tests passed");
    return 0;
}
