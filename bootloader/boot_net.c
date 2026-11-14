#include "boot_net.h"
#include <stdarg.h>
#include <string.h>

/** 功能：仅格式化固定AT/HTTP格式，不引入堆和标准流；参数：缓冲及受控格式；返回：长度或负一。 */
static int format_text(char *output, size_t capacity, const char *format, ...) {
    size_t used = 0U;
    va_list arguments;
    va_start(arguments, format);
    while (*format != '\0') {
        const char *part;
        char literal[2] = {0};
        char digits[24];
        size_t count = 0U;
        if (*format != '%') {
            literal[0] = *format++;
            part = literal;
            count = 1U;
        } else {
            ++format;
            if (*format == 's') {
                part = va_arg(arguments, const char *);
                ++format;
                count = strlen(part);
            } else {
                unsigned long value;
                if (*format == 'l' && format[1] == 'u') {
                    value = va_arg(arguments, unsigned long);
                    format += 2;
                } else if (*format == 'u') {
                    value = va_arg(arguments, unsigned);
                    ++format;
                } else {
                    va_end(arguments);
                    return -1;
                }
                do {
                    digits[count++] = (char)('0' + value % 10U);
                    value /= 10U;
                } while (value != 0U);
                for (size_t i = 0U; i < count / 2U; ++i) {
                    char swap = digits[i];
                    digits[i] = digits[count - 1U - i];
                    digits[count - 1U - i] = swap;
                }
                part = digits;
            }
        }
        if (used >= capacity || count >= capacity - used) {
            va_end(arguments);
            return -1;
        }
        memcpy(output + used, part, count);
        used += count;
    }
    va_end(arguments);
    output[used] = '\0';
    return (int)used;
}

/** 功能：检查单调超时；参数：下载器、起点和期限；返回：尚有时间为真。 */
static bool before(boot_net_t *net, uint32_t start, uint32_t limit) {
    return (uint32_t)(net->io.now(net->io.context) - start) < limit;
}

/** 功能：清理有上限的串口残留；参数：下载器；返回：无。 */
static void drain(boot_net_t *net) {
    uint8_t byte;
    uint32_t start = net->io.now(net->io.context);
    while (before(net, start, 500U) && net->io.receive(net->io.context, &byte, 20U)) {
    }
}

/** 功能：读取有界文本行，记录关闭通知；参数：状态、输出和期限；返回：完整行为真。 */
static bool line_read(boot_net_t *net, char *line, size_t capacity, uint32_t limit) {
    size_t used = 0U;
    uint32_t start = net->io.now(net->io.context);
    while (before(net, start, limit)) {
        uint8_t byte;
        if (!net->io.receive(net->io.context, &byte, 20U)) {
            continue;
        }
        if (byte == '\r') {
            continue;
        }
        if (byte == '\n') {
            if (used == 0U) {
                continue;
            }
            line[used] = '\0';
            if (strcmp(line, "CLOSED") == 0) {
                net->peer_closed = true;
            }
            return true;
        }
        if (byte == 0U || used + 1U >= capacity) {
            return false;
        }
        line[used++] = (char)byte;
    }
    return false;
}

/** 功能：等候独立应答行；参数：状态、预期行和期限；返回：匹配为真。 */
static bool wait_for(boot_net_t *net, const char *expected, uint32_t limit) {
    char line[256];
    uint32_t start = net->io.now(net->io.context);
    while (before(net, start, limit)) {
        uint32_t elapsed = (uint32_t)(net->io.now(net->io.context) - start);
        if (elapsed >= limit) {
            break;
        }
        /* 使用本次事务的剩余期限，避免人为切断并丢弃尚未结束的合法应答行。 */
        if (!line_read(net, line, sizeof line, limit - elapsed)) {
            continue;
        }
        if (strcmp(line, expected) == 0) {
            return true;
        }
        if (strcmp(line, "ERROR") == 0 || strncmp(line, "busy", 4U) == 0 ||
            (strcmp(line, "ready") == 0 && strcmp(expected, "ready") != 0)) {
            return false;
        }
    }
    return false;
}

/** 功能：发送含换行的文本命令；参数：下载器和命令；返回：发送成功为真。 */
static bool send_text(boot_net_t *net, const char *text) {
    return net->io.send(net->io.context, text, strlen(text));
}

/** 功能：执行有界 AT 命令；参数：下载器、命令和期限；返回：得到OK为真。 */
static bool command(boot_net_t *net, const char *text, uint32_t limit) {
    return send_text(net, text) && wait_for(net, "OK", limit);
}

/** 功能：转义AT引号参数并拒绝控制字符；参数：输入和输出容量；返回：成功为真。 */
static bool escape(const char *input, char *output, size_t capacity) {
    size_t used = 0U;
    while (*input != '\0') {
        unsigned char c = (unsigned char)*input++;
        if (c < 32U || c == 127U || used + 3U >= capacity) {
            return false;
        }
        if (c == '"' || c == '\\' || c == ',') {
            output[used++] = '\\';
        }
        output[used++] = (char)c;
    }
    output[used] = '\0';
    return true;
}

/** 功能：独立同步ESP并建立无线连接；参数：连接配置；返回：就绪为真。 */
bool boot_net_connect(boot_net_t *net, const boot_net_io_t *io, const char *ssid,
                      const char *password, const char *host, uint16_t port, const char *path) {
    if (net == NULL || io == NULL || io->send == NULL || io->receive == NULL ||
        io->now == NULL || io->delay == NULL || ssid == NULL || password == NULL ||
        host == NULL || path == NULL || ssid[0] == '\0' || host[0] == '\0' ||
        path[0] != '/' || port == 0U || strlen(ssid) > 32U || strlen(password) > 64U ||
        strlen(host) > 64U || strlen(path) > 192U) {
        return false;
    }
    for (const char *p = host; *p != '\0'; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '.' || *p == '-')) {
            return false;
        }
    }
    for (const char *p = path; *p != '\0'; ++p) {
        if ((unsigned char)*p <= 32U || (unsigned char)*p >= 127U || *p == '"') {
            return false;
        }
    }
    memset(net, 0, sizeof *net);
    net->io = *io;
    net->host = host;
    net->path = path;
    net->port = port;
    drain(net);
    if (!command(net, "AT\r\n", 500U)) {
        uint8_t padding[256];
        memset(padding, ' ', sizeof padding);
        for (unsigned i = 0U; i < 16U; ++i) {
            if (!net->io.send(net->io.context, padding, sizeof padding)) {
                return false;
            }
            drain(net);
        }
        drain(net);
        /* 超出原始输入窗口的填充会进入命令行，必须结束该行并排空拒绝应答。 */
        if (!send_text(net, "\r\n")) {
            return false;
        }
        drain(net);
    }
    if (!send_text(net, "AT+RST\r\n") || !wait_for(net, "ready", 5000U) ||
        !command(net, "ATE0\r\n", 1000U) ||
        !command(net, "AT+SYSSTORE=0\r\n", 1000U) ||
        !command(net, "AT+CWMODE=1\r\n", 1000U)) {
        return false;
    }
    char escaped_ssid[68];
    char escaped_password[132];
    char request[256];
    if (!escape(ssid, escaped_ssid, sizeof escaped_ssid) ||
        !escape(password, escaped_password, sizeof escaped_password)) {
        return false;
    }
    int n = format_text(request, sizeof request, "AT+CWJAP=\"%s\",\"%s\"\r\n",
                     escaped_ssid, escaped_password);
    if (n < 0 || (size_t)n >= sizeof request || !command(net, request, 30000U)) {
        return false;
    }
    return command(net, "AT+CIPMUX=0\r\n", 1000U) &&
           command(net, "AT+CIPDINFO=0\r\n", 1000U) &&
           command(net, "AT+CIPRECVTYPE=1\r\n", 1000U);
}

/** 功能：获取ESP实际缓存量；参数：状态和输出；返回：完整查询成功为真。 */
static bool pending(boot_net_t *net, uint32_t *count) {
    char line[256];
    bool found = false;
    uint32_t start = net->io.now(net->io.context);
    if (!send_text(net, "AT+CIPRECVLEN?\r\n")) {
        return false;
    }
    while (before(net, start, 1000U)) {
        uint32_t elapsed = (uint32_t)(net->io.now(net->io.context) - start);
        if (elapsed >= 1000U) {
            break;
        }
        if (!line_read(net, line, sizeof line, 1000U - elapsed)) {
            continue;
        }
        if (strncmp(line, "+CIPRECVLEN:", 12U) == 0) {
            const char *p = line + 12;
            uint32_t value = 0U;
            if (*p < '0' || *p > '9') {
                return false;
            }
            while (*p >= '0' && *p <= '9') {
                uint32_t digit = (uint32_t)(*p++ - '0');
                if (value > (UINT32_MAX - digit) / 10U) {
                    return false;
                }
                value = value * 10U + digit;
            }
            if (*p != '\0' && *p != ',') {
                return false;
            }
            *count = value;
            found = true;
        } else if (strcmp(line, "OK") == 0) {
            return found;
        } else if (strcmp(line, "ERROR") == 0 || strcmp(line, "ready") == 0) {
            return false;
        }
    }
    return false;
}

/** 功能：按定长帧拉取二进制，绝不解释载荷；参数：下载器；返回：成功为真。 */
static bool pull(boot_net_t *net) {
    char prefix[80];
    size_t used = 0U;
    uint32_t start = net->io.now(net->io.context);
    if (!send_text(net, "AT+CIPRECVDATA=256\r\n")) {
        return false;
    }
    while (before(net, start, 1500U)) {
        uint8_t byte;
        if (!net->io.receive(net->io.context, &byte, 20U)) {
            continue;
        }
        if (byte == '\r') {
            continue;
        }
        if (byte == '\n') {
            prefix[used] = '\0';
            if (strcmp(prefix, "CLOSED") == 0) {
                net->peer_closed = true;
            }
            if (strcmp(prefix, "ERROR") == 0 || strcmp(prefix, "ready") == 0) {
                return false;
            }
            used = 0U;
            continue;
        }
        if (byte == 0U || used + 1U >= sizeof prefix) {
            return false;
        }
        prefix[used++] = (char)byte;
        prefix[used] = '\0';
        if (byte == ',' && strncmp(prefix, "+CIPRECVDATA:", 13U) == 0) {
            uint32_t count = 0U;
            if (used <= 14U) {
                return false;
            }
            for (size_t i = 13U; i + 1U < used; ++i) {
                if (prefix[i] < '0' || prefix[i] > '9' || count > 256U) {
                    return false;
                }
                count = count * 10U + (uint32_t)(prefix[i] - '0');
            }
            if (count > 256U) {
                return false;
            }
            uint8_t bytes[256];
            for (uint32_t i = 0U; i < count; ++i) {
                if (!net->io.receive(net->io.context, bytes + i, 100U)) {
                    return false;
                }
            }
            return wait_for(net, "OK", 1000U) && ota_http_feed(&net->http, bytes, count);
        }
    }
    return false;
}

/** 功能：执行完整有限Range事务；参数：状态和目标范围；返回：完全校验并关闭为真。 */
static bool fetch_range(boot_net_t *net, uint32_t offset, void *data, size_t length, bool first) {
    char command_text[512];
    if (length == 0U || length > 4096U || data == NULL || offset > UINT32_MAX - length ||
        (!first && (net->total == 0U || offset + length > net->total))) {
        return false;
    }
    drain(net);
    net->peer_closed = false;
    int n = format_text(command_text, sizeof command_text, "AT+CIPSTART=\"TCP\",\"%s\",%u,,,5000\r\n",
                     net->host, (unsigned)net->port);
    if (n < 0 || (size_t)n >= sizeof command_text || !command(net, command_text, 6000U)) {
        return false;
    }
    char request[640];
    n = format_text(request, sizeof request,
        "GET %s HTTP/1.1\r\nHost: %s:%u\r\nRange: bytes=%lu-%lu\r\n%s%s%sAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
        net->path, net->host, (unsigned)net->port, (unsigned long)offset,
        (unsigned long)(offset + length - 1U), first ? "" : "If-Match: ",
        first ? "" : net->etag, first ? "" : "\r\n");
    if (n < 0 || (size_t)n >= sizeof request) {
        return false;
    }
    int request_length = n;
    (void)format_text(command_text, sizeof command_text, "AT+CIPSEND=%u\r\n", (unsigned)n);
    if (!send_text(net, command_text)) {
        return false;
    }
    uint8_t byte = 0U;
    uint32_t start = net->io.now(net->io.context);
    while (byte != '>' && before(net, start, 2000U)) {
        (void)net->io.receive(net->io.context, &byte, 20U);
    }
    if (byte != '>' || !net->io.send(net->io.context, request, (size_t)request_length) ||
        !wait_for(net, "SEND OK", 2000U)) {
        return false;
    }
    ota_http_init(&net->http, offset, (uint32_t)length, first ? 0U : net->total,
                  first ? NULL : net->etag, data);
    bool success = false;
    start = net->io.now(net->io.context);
    while (before(net, start, 15000U)) {
        uint32_t available;
        if (!pending(net, &available)) {
            break;
        }
        if (available != 0U) {
            if (!pull(net)) {
                break;
            }
        } else if (net->peer_closed) {
            success = net->http.complete;
            break;
        } else {
            net->io.delay(net->io.context, 10U);
        }
    }
    bool closed = command(net, "AT+CIPCLOSE\r\n", 1000U);
    drain(net);
    if (!success || (!closed && !net->peer_closed)) {
        return false;
    }
    if (first) {
        net->total = net->http.total;
        memcpy(net->etag, net->http.etag, sizeof net->etag);
    }
    return true;
}

/** 功能：读取并固定首次包头的HTTP身份；参数：状态和输出；返回：成功为真。 */
bool boot_net_manifest(boot_net_t *net, uint8_t header[256]) {
    return net != NULL && fetch_range(net, 0U, header, 256U, true);
}

/** 功能：按已固定包身份拉取一个块；参数：状态和范围；返回：成功为真。 */
bool boot_net_fetch(void *context, uint32_t offset, void *data, size_t length) {
    boot_net_t *net = context;
    return net != NULL && fetch_range(net, offset, data, length, false);
}
