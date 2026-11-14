#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "clock_web.h"
#include "esp_at.h"
#include "alarm_service.h"
#include "alarm_engine.h"
#include "ota_service.h"
#include "web_page.h"

static char response[30000];
static size_t response_size;
static esp_server_chunk_t chunks[16];
static unsigned count, consumed, submits, closes;
static uint32_t generation;
static int accepted = 202;
static const char *stage = "queued";

/** 功能：提供不含凭据的 OTA 公开状态；参数：同服务接口；返回：无。 */
void ota_service_status(char *output, size_t capacity, uint32_t session) {
    assert(session == 123U);
    (void)snprintf(output, capacity, "{\"enabled\":true,\"session\":123,\"id\":1,\"state\":\"%s\"}", stage);
}

/** 功能：接收严格路由后的 OTA 表单；参数：body，正文；session，会话；返回：模拟状态。 */
int ota_service_request(const char *body, uint32_t session) {
    assert(session == 123U && strcmp(body, "session=123&token=test") == 0);
    ++submits;
    return accepted;
}

/** 功能：报告闹钟服务暂不可用；参数：snapshot，输出；返回：假。 */
bool alarm_service_snapshot(alarm_service_snapshot_t *snapshot) {
    (void)snapshot;
    return false;
}

/** 功能：拒绝无关闹钟提交；参数：同闹钟接口；返回：不可用。 */
alarm_remote_submit_t alarm_service_remote_submit(uint8_t index, const alarm_entry_t *entry,
                                                  uint32_t version, uint32_t *id) {
    (void)index;
    (void)entry;
    (void)version;
    (void)id;
    return ALARM_REMOTE_UNAVAILABLE;
}

/** 功能：报告无关闹钟结果未知；参数：id，编号；返回：未知。 */
alarm_remote_result_t alarm_service_remote_result(uint32_t id) {
    (void)id;
    return ALARM_REMOTE_UNKNOWN;
}

/** 功能：接受无关闹钟条目；参数：entry，条目；返回：真。 */
bool alarm_entry_valid(const alarm_entry_t *entry) {
    (void)entry;
    return true;
}

/** 功能：报告 AT 已就绪；参数：无；返回：真。 */
bool esp_at_is_ready(void) {
    return true;
}

/** 功能：报告网页服务器已就绪；参数：无；返回：真。 */
bool esp_at_server_ready(void) {
    return true;
}

/** 功能：启动虚拟 HTTP 服务；参数：port，端口；ip，地址输出；返回：真。 */
bool esp_at_server_start(uint16_t port, char ip[16]) {
    assert(port == 80U);
    memcpy(ip, "127.0.0.1", 10U);
    return true;
}

/** 功能：逐块投递虚拟请求；参数：chunk，输出；返回：是否有数据。 */
bool esp_at_server_poll(esp_server_chunk_t *chunk) {
    if (consumed == count) {
        return false;
    }
    *chunk = chunks[consumed++];
    return true;
}

/** 功能：保存真实路由的 HTTP 响应；参数：同 ESP 接口；返回：真。 */
bool esp_at_server_send(uint8_t link, uint32_t connection, const void *data, size_t length) {
    assert(link == 0U && connection == generation && response_size + length < sizeof response);
    memcpy(response + response_size, data, length);
    response_size += length;
    response[response_size] = '\0';
    return true;
}

/** 功能：检查响应始终关闭连接；参数：同 ESP 接口；返回：真。 */
bool esp_at_server_close(uint8_t link, uint32_t connection) {
    assert(link == 0U && connection == generation);
    ++closes;
    return true;
}

/** 功能：丢弃测试日志；参数：format，格式；返回：零。 */
int clock_log(const char *format, ...) {
    (void)format;
    return 0;
}

/** 功能：丢弃测试日志行；参数：message，日志；返回：零。 */
int clock_log_line(const char *message) {
    (void)message;
    return 0;
}

/** 功能：经真实 HTTP 增量解析器发送请求；参数：raw，HTTP 字节串；返回：无。 */
static void request(const char *raw) {
    size_t length = strlen(raw);
    consumed = 0U;
    count = 1U;
    response_size = 0U;
    memset(chunks, 0, sizeof chunks);
    ++generation;
    chunks[0].event = ESP_SERVER_EVENT_CONNECTED;
    chunks[0].generation = generation;
    for (size_t offset = 0U; offset < length; offset += 97U) {
        esp_server_chunk_t *chunk = &chunks[count++];
        size_t piece = length - offset > 97U ? 97U : length - offset;
        assert(count <= 16U);
        chunk->event = ESP_SERVER_EVENT_DATA;
        chunk->generation = generation;
        chunk->length = (uint16_t)piece;
        memcpy(chunk->data, raw + offset, piece);
    }
    while (consumed < count) {
        clock_web_poll(1000U);
    }
}

/** 功能：提交 OTA 表单；参数：headers，附加头；返回：无。 */
static void post(const char *headers) {
    char raw[512];
    const char *body = "session=123&token=test";
    (void)snprintf(raw, sizeof raw,
                   "POST /api/ota HTTP/1.1\r\n%sContent-Length: %zu\r\n\r\n%s",
                   headers, strlen(body), body);
    request(raw);
}

/** 功能：验证 OTA 路由、认证头、异步状态及网页秘密隔离；参数：无；返回：通过返回零。 */
int main(void) {
    clock_web_init(123U);
    clock_web_network(true, 0U);
    request("GET /api/ota HTTP/1.1\r\n\r\n");
    assert(strstr(response, "200 OK") != NULL && strstr(response, "queued") != NULL);
    post("Content-Type: application/x-www-form-urlencoded\r\n");
    assert(strstr(response, "403 Forbidden") != NULL && submits == 0U);
    post("X-Clock-Request: 1\r\n");
    assert(strstr(response, "415 Unsupported Media Type") != NULL && submits == 0U);
    post("Content-Type: application/x-www-form-urlencoded\r\nX-Clock-Request: 1\r\n");
    assert(strstr(response, "202 Accepted") != NULL && strstr(response, "queued") != NULL);
    assert(submits == 1U);
    accepted = 409;
    post("Content-Type: application/x-www-form-urlencoded\r\nX-Clock-Request: 1\r\n");
    assert(strstr(response, "409 Conflict") != NULL);
    accepted = 403;
    post("Content-Type: application/x-www-form-urlencoded\r\nX-Clock-Request: 1\r\n");
    assert(strstr(response, "403 Forbidden") != NULL);
    accepted = 503;
    post("Content-Type: application/x-www-form-urlencoded\r\nX-Clock-Request: 1\r\n");
    assert(strstr(response, "503 Service Unavailable") != NULL);
    stage = "saved";
    request("GET /api/ota HTTP/1.1\r\n\r\n");
    assert(strstr(response, "saved") != NULL);
    assert(strstr(clock_web_page, "id=\"otaToken\"") != NULL);
    assert(strstr(clock_web_page, "type=\"password\"") != NULL);
    assert(strstr(clock_web_page, "localStorage") == NULL);
    assert(strstr(clock_web_page, "sessionStorage") == NULL);
    assert(closes == 8U);
    puts("OTA web routes passed");
    return 0;
}
