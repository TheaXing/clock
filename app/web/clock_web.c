#include "clock_web.h"
#include "clock_http.h"
#include "web_page.h"
#include "alarm_service.h"
#include "alarm_engine.h"
#include "esp_at.h"
#include "clock_console.h"
#if defined(CLOCK_OTA_ENABLED) && CLOCK_OTA_ENABLED
#include "ota_service.h"
#endif
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define WEB_LINKS 5U
#define WEB_RETRY_MS 30000U
#define WEB_REQUEST_MS 35000U
#if defined(CLOCK_OTA_ENABLED) && CLOCK_OTA_ENABLED
#define WEB_OTA_SUPPORTED "true"
#else
#define WEB_OTA_SUPPORTED "false"
#endif
/** 保存一个网页连接的解析器、连接代次和超时状态。 */
typedef struct {
    clock_http_parser_t parser; // 该连接的单请求 HTTP 解析器
    uint32_t generation;        // ESP 连接代次，用于隔离复用的连接编号
    uint32_t started_ms;        // 该请求开始接收的单调毫秒时刻
    bool active;                // 是否正在接收有效请求
} web_client_t;
/* 仅供 CPU 使用的工作缓冲区：串口使用轮询发送，不从这些数组进行 DMA。
 * 此 NOLOAD 区由初始化函数显式清空；现有启动代码仅清空常规 .bss。
 * 保持已经验证的 92 KiB RTOS 堆大小不变。 */
#if defined(__GNUC__) && defined(__arm__)
#define WEB_WORK_RAM __attribute__((section(".clock_web_ram"),aligned(4)))
#else
#define WEB_WORK_RAM
#endif
static web_client_t clients[WEB_LINKS] WEB_WORK_RAM;
static char json[2304] WEB_WORK_RAM;
static uint32_t session_token, last_attempt;
static bool network_connected, attempted;
static void (*background_poll)(void);
_Static_assert(sizeof(clock_http_parser_t) <= 768U, "HTTP parser RAM budget exceeded");

/**
 * 功能：向共享 JSON 缓冲区追加格式化文字，并更新已用长度。
 * 参数：used，已用字节数；format，格式字符串；可变参数，待格式化的值。
 * 返回：完整追加成功返回 true，空间不足或格式化失败返回 false。
 */
static bool append(size_t *used, const char *format, ...) {
    if (*used >= sizeof json) {
        return false;
    }
    va_list args;
    va_start(args, format);
    int n = vsnprintf(json + *used, sizeof json - *used, format, args);
    va_end(args);
    if (n < 0 || (size_t)n >= sizeof json - *used) {
        return false;
    }
    *used += (size_t)n;
    return true;
}

/**
 * 功能：发送 HTTP 响应头及分段正文，穿插后台维护并最终关闭连接。
 * 参数：link，连接编号；status，HTTP 状态文本；type，正文媒体类型；body，正文地址；length，正文字节数。
 * 返回：无。
 */
static void respond(unsigned link, const char *status, const char *type, const char *body,
                    size_t length) {
    web_client_t *client = &clients[link];
    char header[256];
    int n = snprintf(
        header, sizeof header,
        "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\n\r\n",
        status, type, (unsigned long)length);
    if (n > 0 && (size_t)n < sizeof header &&
        esp_at_server_send((uint8_t)link, client->generation, header, (size_t)n)) {
        while (length) {
            if (background_poll) {
                background_poll();
            }
            size_t piece = length > 512U ? 512U : length;
            if (!esp_at_server_send((uint8_t)link, client->generation, body, piece)) {
                break;
            }
            body += piece;
            length -= piece;
        }
        if (background_poll) {
            background_poll();
        }
    }
    (void)esp_at_server_close((uint8_t)link, client->generation);
    client->active = false;
}
/**
 * 功能：编码错误原因并向指定连接发送 JSON 错误响应。
 * 参数：link，连接编号；status，HTTP 状态文本；error，固定错误标识。
 * 返回：无。
 */
static void error_response(unsigned link, const char *status, const char *error) {
    int n = snprintf(json, sizeof json, "{\"error\":\"%s\"}", error);
    respond(link, status, "application/json; charset=utf-8", json, n > 0 ? (size_t)n : 0U);
}

/* 浏览器仅发送十进制表单字段；明确拒绝百分号转义、符号、未知或重复字段以及整数溢出。 */
/**
 * 功能：严格解析仅含十进制无符号整数的表单，拒绝未知、重复、转义或缺失字段。
 * 参数：body，表单正文；names，允许字段名称数组；count，字段数，不超过 31；values，按名称顺序接收数值。
 * 返回：所有字段恰好出现一次且无溢出时返回 true，否则返回 false。
 */
static bool form_numbers(const char *body, const char *const *names, unsigned count,
                         uint32_t *values) {
    uint32_t seen = 0U;
    const char *p = body;
    if (!p || !*p || count > 31U) {
        return false;
    }
    for (;;) {
        const char *key = p;
        while (*p && *p != '=' && *p != '&') {
            ++p;
        }
        if (*p != '=') {
            return false;
        }
        size_t length = (size_t)(p - key);
        unsigned index;
        for (index = 0; index < count; ++index) {
            if (strlen(names[index]) == length && memcmp(key, names[index], length) == 0) {
                break;
            }
        }
        if (index == count || (seen & (1UL << index))) {
            return false;
        }
        ++p;
        if (*p < '0' || *p > '9') {
            return false;
        }
        uint32_t value = 0U;
        do {
            uint32_t digit = (uint32_t)(*p - '0');
            if (value > (UINT32_MAX - digit) / 10U) {
                return false;
            }
            value = value * 10U + digit;
            ++p;
        } while (*p >= '0' && *p <= '9');
        values[index] = value;
        seen |= 1UL << index;
        if (!*p) {
            break;
        }
        if (*p++ != '&' || !*p) {
            return false;
        }
    }
    return seen == ((1UL << count) - 1UL);
}

/**
 * 功能：读取服务快照并返回闹钟列表、时间和并发版本等 JSON 状态。
 * 参数：link，接收响应的连接编号。
 * 返回：无。
 */
static void get_alarms(unsigned link) {
    alarm_service_snapshot_t data;
    if (!alarm_service_snapshot(&data)) {
        error_response(link, "503 Service Unavailable", "alarm_unavailable");
        return;
    }
    size_t used = 0;
    bool ok = append(&used,
                     "{\"session\":%lu,\"version\":%lu,\"revision\":%lu,\"time_valid\":%s,"
                     "\"ota_supported\":" WEB_OTA_SUPPORTED ","
                     "\"time\":\"%04u-%02u-%02u %02u:%02u:%02u\",\"saving\":%s,\"storage_ok\":%s,"
                     "\"ringing_mask\":%u,\"snoozed_mask\":%u,\"local_editing\":%s,\"alarms\":[",
                     (unsigned long)session_token, (unsigned long)data.state_version,
                     (unsigned long)data.revision, data.time_valid ? "true" : "false",
                     (unsigned)data.now.year, (unsigned)data.now.month, (unsigned)data.now.day,
                     (unsigned)data.now.hour, (unsigned)data.now.minute, (unsigned)data.now.second,
                     data.saving ? "true" : "false", data.storage_ok ? "true" : "false",
                     (unsigned)data.ringing_mask, (unsigned)data.snoozed_mask,
                     data.local_editing ? "true" : "false");
    for (unsigned i = 0; i < ALARM_COUNT && ok; ++i) {
        const alarm_entry_t *e = &data.config.entries[i];
        ok = append(
            &used,
            "%s{\"id\":%u,\"enabled\":%s,\"hour\":%u,\"minute\":%u,\"repeat_mask\":%u,"
            "\"year\":%u,\"month\":%u,\"day\":%u,\"ring_seconds\":%u,\"snooze_seconds\":%u}",
            i ? "," : "", i + 1U, e->enabled ? "true" : "false", (unsigned)e->hour,
            (unsigned)e->minute, (unsigned)e->repeat_mask, (unsigned)e->year, (unsigned)e->month,
            (unsigned)e->day, (unsigned)e->ring_seconds, (unsigned)e->snooze_seconds);
    }
    if (!ok || !append(&used, "]}")) {
        error_response(link, "500 Internal Server Error", "response_overflow");
        return;
    }
    respond(link, "200 OK", "application/json; charset=utf-8", json, used);
}

/**
 * 功能：校验查询参数和启动会话，返回异步闹钟保存请求的结果。
 * 参数：link，连接编号；query，包含请求编号及会话值的查询字符串。
 * 返回：无。
 */
static void get_result(unsigned link, const char *query) {
    static const char *const names[] = {"id", "session"};
    uint32_t values[2];
    if (!form_numbers(query, names, 2, values) || values[0] == 0U) {
        error_response(link, "400 Bad Request", "invalid_query");
        return;
    }
    if (values[1] != session_token) {
        error_response(link, "409 Conflict", "session_changed");
        return;
    }
    const char *state;
    switch (alarm_service_remote_result(values[0])) {
    case ALARM_REMOTE_PENDING:
        state = "pending";
        break;
    case ALARM_REMOTE_SAVED:
        state = "saved";
        break;
    case ALARM_REMOTE_FAILED:
        state = "failed";
        break;
    default:
        state = "unknown";
        break;
    }
    int n = snprintf(json, sizeof json, "{\"session\":%lu,\"state\":\"%s\"}",
                     (unsigned long)session_token, state);
    respond(link, "200 OK", "application/json; charset=utf-8", json, (size_t)n);
}

/**
 * 功能：校验来源请求头、表单、会话和版本后提交完整闹钟修改。
 * 参数：link，连接编号；index，从零开始的闹钟编号；request，完整 HTTP 请求。
 * 返回：无。
 */
static void post_alarm(unsigned link, unsigned index, const clock_http_parser_t *request) {
    static const char *const names[] = {"session", "version",      "enabled",       "hour",
                                        "minute",  "repeat_mask",  "year",          "month",
                                        "day",     "ring_seconds", "snooze_seconds"};
    uint32_t v[11], id = 0;
    if (!request->has_clock_request) {
        error_response(link, "403 Forbidden", "request_header_required");
        return;
    }
    if (!request->form_content_type) {
        error_response(link, "415 Unsupported Media Type", "form_required");
        return;
    }
    if (!form_numbers(request->body, names, 11, v) || v[2] > 1U || v[3] > 23U || v[4] > 59U ||
        v[5] > 127U || v[6] < 2020U || v[6] > 2099U || v[7] < 1U || v[7] > 12U || v[8] < 1U ||
        v[8] > 31U || v[9] < 5U || v[9] > 600U || v[10] < 60U || v[10] > 1800U ||
        v[10] % 60U != 0U) {
        error_response(link, "400 Bad Request", "invalid_fields");
        return;
    }
    if (v[0] != session_token) {
        error_response(link, "409 Conflict", "session_changed");
        return;
    }
    alarm_entry_t entry = {(uint16_t)v[6], (uint16_t)v[9], (uint16_t)v[10],
                           (uint8_t)v[7],  (uint8_t)v[8],  (uint8_t)v[3],
                           (uint8_t)v[4],  (uint8_t)v[5],  (uint8_t)v[2]};
    if (!alarm_entry_valid(&entry)) {
        error_response(link, "400 Bad Request", "invalid_date");
        return;
    }
    alarm_service_snapshot_t data;
    if (!alarm_service_snapshot(&data)) {
        error_response(link, "503 Service Unavailable", "alarm_unavailable");
        return;
    }
    if (v[1] != data.state_version) {
        error_response(link, "409 Conflict", "version_changed");
        return;
    }
    alarm_remote_submit_t accepted = alarm_service_remote_submit((uint8_t)index, &entry, v[1], &id);
    switch (accepted) {
    case ALARM_REMOTE_ACCEPTED: {
        int n =
            snprintf(json, sizeof json, "{\"id\":%lu,\"state\":\"pending\"}", (unsigned long)id);
        respond(link, "202 Accepted", "application/json; charset=utf-8", json, (size_t)n);
        break;
    }
    case ALARM_REMOTE_BUSY:
        error_response(link, "409 Conflict", "busy_or_local_editing");
        break;
    case ALARM_REMOTE_CONFLICT:
        error_response(link, "409 Conflict", "version_changed");
        break;
    case ALARM_REMOTE_INVALID:
        error_response(link, "400 Bad Request", "invalid_fields");
        break;
    default:
        error_response(link, "503 Service Unavailable", "alarm_unavailable");
        break;
    }
}

/**
 * 功能：返回不包含管理令牌或密钥的 OTA 阶段及本次启动请求编号。
 * 参数：link，接收响应的连接编号；status，HTTP 状态文本。
 * 返回：无。
 */
static void get_ota(unsigned link, const char *status) {
#if defined(CLOCK_OTA_ENABLED) && CLOCK_OTA_ENABLED
    ota_service_status(json, sizeof json, session_token);
#else
    (void)snprintf(json, sizeof json,
                   "{\"enabled\":false,\"session\":%lu,\"version\":0,\"identity\":\"\","
                   "\"state\":\"disabled\",\"error\":0,\"saving\":false,\"id\":0}",
                   (unsigned long)session_token);
#endif
    respond(link, status, "application/json; charset=utf-8", json, strlen(json));
}

/**
 * 功能：校验请求头后异步提交 OTA，202 只表示进入保存队列。
 * 参数：link，接收响应的连接编号；request，完整 HTTP 请求。
 * 返回：无。
 */
static void post_ota(unsigned link, const clock_http_parser_t *request) {
    int status = 503;
    if (!request->has_clock_request) {
        error_response(link, "403 Forbidden", "request_header_required");
        return;
    }
    if (!request->form_content_type) {
        error_response(link, "415 Unsupported Media Type", "form_required");
        return;
    }
#if defined(CLOCK_OTA_ENABLED) && CLOCK_OTA_ENABLED
    status = ota_service_request(request->body, session_token);
#endif
    if (status == 202) {
        get_ota(link, "202 Accepted");
    } else if (status == 403) {
        error_response(link, "403 Forbidden", "ota_authentication_failed");
    } else if (status == 409) {
        error_response(link, "409 Conflict", "ota_busy");
    } else {
        error_response(link, "503 Service Unavailable", "ota_unavailable");
    }
}

/**
 * 功能：根据完整 HTTP 请求的方法和目标选择页面或闹钟接口处理。
 * 参数：link，已完成请求解析的连接编号。
 * 返回：无。
 */
static void dispatch(unsigned link) {
    clock_http_parser_t *request = &clients[link].parser;
    if (request->method == CLOCK_HTTP_GET) {
        if (strcmp(request->target, "/") == 0 || strcmp(request->target, "/index.html") == 0) {
            respond(link, "200 OK", "text/html; charset=utf-8", clock_web_page,
                    clock_web_page_length);
        } else if (strcmp(request->target, "/api/alarms") == 0) {
            get_alarms(link);
        } else if (strcmp(request->target, "/api/ota") == 0) {
            get_ota(link, "200 OK");
        } else if (strncmp(request->target, "/api/result?", 12) == 0) {
            get_result(link, request->target + 12);
        } else if (strcmp(request->target, "/favicon.ico") == 0) {
            respond(link, "204 No Content", "image/x-icon", "", 0);
        } else {
            error_response(link, "404 Not Found", "not_found");
        }
    } else if (strcmp(request->target, "/api/ota") == 0) {
        post_ota(link, request);
    } else if (strncmp(request->target, "/api/alarms/", 12) == 0 && request->target[12] >= '1' &&
               request->target[12] <= '8' && request->target[13] == '\0') {
        post_alarm(link, (unsigned)(request->target[12] - '1'), request);
    } else {
        error_response(link, "404 Not Found", "not_found");
    }
}

/**
 * 功能：在唯一网络任务启动前清空工作区并设置启动会话。
 * 参数：session，本次启动的并发会话值，0 表示禁用远程修改服务。
 * 返回：无。
 */
void clock_web_init(uint32_t session) {
    memset(clients, 0, sizeof clients);
    memset(json, 0, sizeof json);
    session_token = session;
    network_connected = false;
    attempted = false;
    last_attempt = 0U;
    background_poll = NULL;
}
/**
 * 功能：设置分段响应之间执行的同一网络任务后台维护回调。
 * 参数：poll，维护回调；调用时不持有 ESP 命令互斥锁，且不得递归调用网页操作。
 * 返回：无。
 */
void clock_web_set_background_poll(void (*poll)(void)) {
    background_poll = poll;
}

/**
 * 功能：跟踪网络状态，在连接可用时按重试间隔启动 HTTP 服务。
 * 参数：connected，Wi-Fi 连接状态；now_ms，当前单调毫秒时间。
 * 返回：无。
 */
void clock_web_network(bool connected, uint32_t now_ms) {
    if (session_token == 0U) {
        return;
    }
    if (!connected) {
        memset(clients, 0, sizeof clients);
        network_connected = false;
        attempted = false;
        return;
    }
    bool newly_connected = !network_connected;
    network_connected = true;
    if ((newly_connected || !esp_at_server_ready()) && esp_at_is_ready() &&
        (!attempted || now_ms - last_attempt >= WEB_RETRY_MS || newly_connected)) {
        char ip[16];
        last_attempt = now_ms;
        attempted = true;
        memset(clients, 0, sizeof clients);
        if (esp_at_server_start(80U, ip)) {
            clock_log("[WEB] Open http://%s/ on the same WiFi\r\n", ip);
        } else {
            clock_log_line(
                "[WEB] Server unavailable; retry in 30 seconds (check ESP-AT TCP server support)");
        }
    }
}

/**
 * 功能：处理有界数量的服务器事件和请求数据，并清理超时连接。
 * 参数：now_ms，当前单调毫秒时间。
 * 返回：无。
 */
void clock_web_poll(uint32_t now_ms) {
    if (!network_connected || !esp_at_server_ready()) {
        return;
    }
    for (unsigned i = 0; i < WEB_LINKS; ++i) {
        if (clients[i].active && now_ms - clients[i].started_ms >= WEB_REQUEST_MS) {
            (void)esp_at_server_close((uint8_t)i, clients[i].generation);
            clients[i].active = false;
        }
    }
    for (unsigned budget = 0; budget < 4U; ++budget) {
        esp_server_chunk_t chunk;
        if (!esp_at_server_poll(&chunk)) {
            break;
        }
        if (chunk.link >= WEB_LINKS) {
            continue;
        }
        web_client_t *client = &clients[chunk.link];
        if (chunk.event == ESP_SERVER_EVENT_CLOSED) {
            if (client->generation == chunk.generation) {
                client->active = false;
            }
            continue;
        }
        if (chunk.event == ESP_SERVER_EVENT_CONNECTED || client->generation != chunk.generation) {
            clock_http_init(&client->parser);
            client->generation = chunk.generation;
            client->started_ms = now_ms;
            client->active = true;
        }
        if (chunk.event != ESP_SERVER_EVENT_DATA || !client->active) {
            continue;
        }
        clock_http_status_t status = clock_http_feed(&client->parser, chunk.data, chunk.length);
        if (status == CLOCK_HTTP_READY) {
            dispatch(chunk.link);
            return;
        } else if (status == CLOCK_HTTP_TOO_LARGE) {
            error_response(chunk.link, "413 Content Too Large", "request_too_large");
        } else if (status == CLOCK_HTTP_METHOD_NOT_ALLOWED) {
            error_response(chunk.link, "405 Method Not Allowed", "method_not_allowed");
        } else if (status != CLOCK_HTTP_NEED_MORE) {
            error_response(chunk.link, "400 Bad Request", "malformed_http");
        }
    }
}
