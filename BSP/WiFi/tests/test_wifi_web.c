#define main ota_web_test_main
#include "../../OTA/tests/app_web_test.c"
#undef main
#include "wifi_form.h"

static unsigned wifi_submits, status_calls;
static int wifi_accepted = 202;

/** 功能：记录配网提交并按真实表单规则检查；参数：正文及会话；返回：测试状态。 */
static int submit_wifi(const char *body, uint32_t session) {
    char ssid[33], password[64];
    assert(session == 456U);
    assert(response_size == 0U);
    ++wifi_submits;
    if (!wifi_form_parse(body, session, ssid, password)) {
        return 400;
    }
    return wifi_accepted;
}

/** 功能：生成无密码的配网状态；参数：输出区、容量、会话；返回：无。 */
static void status_wifi(char *output, size_t capacity, uint32_t session) {
    assert(session == 456U);
    ++status_calls;
    (void)snprintf(output, capacity, "{\"session\":456,\"state\":\"waiting\",\"error\":\"none\"}");
}

/** 功能：经分片 HTTP 提交配网表单；参数：附加头、正文；返回：无。 */
static void wifi_post(const char *headers, const char *body) {
    char raw[1100];
    (void)snprintf(raw, sizeof raw,
                   "POST /api/wifi/config HTTP/1.1\r\n%sContent-Length: %zu\r\n\r\n%s",
                   headers, strlen(body), body);
    request(raw);
}

/** 功能：验证模式隔离、头部、完整表单和普通会话保留；参数：无；返回：通过时为零。 */
int main(void) {
    const char *headers = "Content-Type: application/x-www-form-urlencoded\r\nX-Clock-Request: 1\r\n";
    const char *body = "session=456&ssid=Home&password=12345678";
    clock_web_init(123U);
    clock_web_set_wifi_handlers(submit_wifi, status_wifi);
    clock_web_setup(true, 456U);
    clock_web_network(true, 0U);
    request("GET /setup HTTP/1.1\r\n\r\n");
    assert(strstr(response, "200 OK") != NULL && strstr(response, "Wi-Fi") != NULL);
    request("GET / HTTP/1.1\r\n\r\n");
    assert(strstr(response, "/api/wifi/config") != NULL);
    request("GET /api/alarms HTTP/1.1\r\n\r\n");
    assert(strstr(response, "404 Not Found") != NULL);
    request("GET /api/ota HTTP/1.1\r\n\r\n");
    assert(strstr(response, "404 Not Found") != NULL);
    post(headers);
    assert(strstr(response, "404 Not Found") != NULL && submits == 0U);
    request("GET /api/result?id=1&session=123 HTTP/1.1\r\n\r\n");
    assert(strstr(response, "404 Not Found") != NULL);
    request("GET /api/wifi/status HTTP/1.1\r\n\r\n");
    assert(strstr(response, "200 OK") != NULL && strstr(response, "\"session\":456") != NULL);
    assert(status_calls == 1U);
    wifi_post("Content-Type: application/x-www-form-urlencoded\r\n", body);
    assert(strstr(response, "403 Forbidden") != NULL && wifi_submits == 0U);
    wifi_post("X-Clock-Request: 1\r\n", body);
    assert(strstr(response, "415 Unsupported Media Type") != NULL && wifi_submits == 0U);
    wifi_post(headers, "session=455&ssid=Home&password=12345678");
    assert(strstr(response, "400 Bad Request") != NULL);
    wifi_post(headers, body);
    assert(strstr(response, "202 Accepted") != NULL && strstr(response, "connecting") != NULL);
    assert(strstr(response, "12345678") == NULL);
    {
        char maximum[400];
        memcpy(maximum, "session=%34%35%36&ssid=", sizeof "session=%34%35%36&ssid=");
        for (unsigned i = 0U; i < 32U; ++i) {
            memcpy(maximum + strlen(maximum), "%41", 4U);
        }
        memcpy(maximum + strlen(maximum), "&password=", 11U);
        for (unsigned i = 0U; i < 63U; ++i) {
            memcpy(maximum + strlen(maximum), "%21", 4U);
        }
        wifi_post(headers, maximum);
        assert(strstr(response, "202 Accepted") != NULL);
    }
    for (unsigned i = 0U; i < 4U; ++i) {
        const int codes[] = {400, 403, 409, 503};
        const char *texts[] = {"400 Bad Request", "403 Forbidden", "409 Conflict", "503 Service Unavailable"};
        wifi_accepted = codes[i];
        wifi_post(headers, body);
        assert(strstr(response, texts[i]) != NULL);
    }
    clock_web_set_wifi_handlers(NULL, NULL);
    wifi_post(headers, body);
    assert(strstr(response, "503 Service Unavailable") != NULL);
    request("GET /api/wifi/status HTTP/1.1\r\n\r\n");
    assert(strstr(response, "503 Service Unavailable") != NULL);
    clock_web_setup(false, 0U);
    request("GET /setup HTTP/1.1\r\n\r\n");
    assert(strstr(response, "404 Not Found") != NULL);
    request("GET /api/wifi/status HTTP/1.1\r\n\r\n");
    assert(strstr(response, "404 Not Found") != NULL);
    wifi_post(headers, body);
    assert(strstr(response, "404 Not Found") != NULL);
    request("GET /api/ota HTTP/1.1\r\n\r\n");
    assert(strstr(response, "200 OK") != NULL);
    {
        const char *first = "GET /api/ota HT";
        const char *last = "TP/1.1\r\n\r\n";
        memset(chunks, 0, sizeof chunks);
        ++generation;
        chunks[0].event = ESP_SERVER_EVENT_CONNECTED;
        chunks[0].generation = generation;
        chunks[1].event = ESP_SERVER_EVENT_DATA;
        chunks[1].generation = generation;
        chunks[1].length = (uint16_t)strlen(first);
        memcpy(chunks[1].data, first, strlen(first));
        consumed = 0U;
        count = 2U;
        response_size = 0U;
        clock_web_poll(1000U);
        clock_web_setup(false, 999U);
        chunks[2].event = ESP_SERVER_EVENT_DATA;
        chunks[2].generation = generation;
        chunks[2].length = (uint16_t)strlen(last);
        memcpy(chunks[2].data, last, strlen(last));
        count = 3U;
        clock_web_poll(1000U);
        assert(response_size > 0U && strstr(response, "200 OK") != NULL);
    }
    clock_web_init(0U);
    clock_web_set_wifi_handlers(submit_wifi, status_wifi);
    clock_web_setup(true, 456U);
    clock_web_network(true, 0U);
    request("GET /api/wifi/status HTTP/1.1\r\n\r\n");
    assert(strstr(response, "200 OK") != NULL);
    puts("Wi-Fi split HTTP routing and mode isolation tests passed");
    return 0;
}
