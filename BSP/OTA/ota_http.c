#include "ota_http.h"
#include <string.h>

/** 功能：比较不区分大小写的 ASCII 头名称；参数：两段字符串；返回：相同为真。 */
static bool same_name(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca + ('a' - 'A'));
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb + ('a' - 'A'));
        }
        if (ca != cb) {
            return false;
        }
    }
    return *a == *b;
}

/** 功能：解析有溢出保护的十进制整数；参数：游标和输出；返回：成功为真。 */
static bool number(const char **cursor, uint32_t *out) {
    const char *p = *cursor;
    uint32_t n = 0U;
    if (*p < '0' || *p > '9') {
        return false;
    }
    while (*p >= '0' && *p <= '9') {
        uint32_t digit = (uint32_t)(*p++ - '0');
        if (n > (UINT32_MAX - digit) / 10U) {
            return false;
        }
        n = n * 10U + digit;
    }
    *cursor = p;
    *out = n;
    return true;
}

/** 功能：检查固定长度的强摘要标识；参数：头值；返回：格式正确为真。 */
static bool valid_etag(const char *value) {
    if (strlen(value) != 66U || value[0] != '"' || value[65] != '"') {
        return false;
    }
    for (unsigned i = 1U; i < 65U; ++i) {
        if (!((value[i] >= '0' && value[i] <= '9') ||
              (value[i] >= 'a' && value[i] <= 'f'))) {
            return false;
        }
    }
    return true;
}

/** 功能：校验并消耗完整头部；参数：解析状态；返回：约束全部匹配为真。 */
static bool parse_headers(ota_http_t *parser) {
    char *line = parser->header;
    char *end = strstr(line, "\r\n");
    unsigned seen = 0U;
    if (end == NULL || (size_t)(end - line) > 256U) {
        return false;
    }
    *end = '\0';
    if (strncmp(line, "HTTP/1.1 206", 12U) != 0 ||
        (line[12] != '\0' && line[12] != ' ')) {
        return false;
    }
    line = end + 2;
    while (*line != '\0') {
        end = strstr(line, "\r\n");
        if (end == NULL || (size_t)(end - line) > 256U) {
            return false;
        }
        if (end == line) {
            break;
        }
        *end = '\0';
        char *colon = strchr(line, ':');
        if (colon == NULL || colon == line) {
            return false;
        }
        for (char *p = line; p < colon; ++p) {
            if (!( (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                   (*p >= '0' && *p <= '9') || *p == '-')) {
                return false;
            }
        }
        *colon = '\0';
        char *value = colon + 1;
        while (*value == ' ' || *value == '\t') {
            ++value;
        }
        char *tail = value + strlen(value);
        while (tail > value && (tail[-1] == ' ' || tail[-1] == '\t')) {
            *--tail = '\0';
        }
        unsigned bit = 0U;
        if (same_name(line, "Content-Length")) {
            uint32_t n;
            const char *p = value;
            bit = 1U;
            if (!number(&p, &n) || *p != '\0' || n != parser->length) {
                return false;
            }
        } else if (same_name(line, "Content-Range")) {
            uint32_t first, last, total;
            const char *p = value;
            bit = 2U;
            if (strncmp(p, "bytes ", 6U) != 0) {
                return false;
            }
            p += 6;
            if (!number(&p, &first) || *p++ != '-' || !number(&p, &last) ||
                *p++ != '/' || !number(&p, &total) || *p != '\0' ||
                first != parser->start || last < first ||
                last - first != parser->length - 1U || last >= total ||
                total > 458752U ||
                (parser->expected_total != 0U && total != parser->expected_total)) {
                return false;
            }
            parser->total = total;
        } else if (same_name(line, "ETag")) {
            bit = 4U;
            if (!valid_etag(value) || (parser->expected_etag[0] != '\0' &&
                strcmp(value, parser->expected_etag) != 0)) {
                return false;
            }
            memcpy(parser->etag, value, 67U);
        } else if (same_name(line, "Transfer-Encoding")) {
            return false;
        } else if (same_name(line, "Content-Encoding")) {
            bit = 8U;
            if (!same_name(value, "identity")) {
                return false;
            }
        } else if (same_name(line, "Content-Type")) {
            bit = 16U;
            char media_prefix[11] = {0};
            if (strlen(value) >= 10U) {
                memcpy(media_prefix, value, 10U);
            }
            if (same_name(media_prefix, "multipart/")) {
                return false;
            }
        }
        if (bit != 0U && (seen & bit) != 0U) {
            return false;
        }
        seen |= bit;
        line = end + 2;
    }
    return (seen & 7U) == 7U;
}

/** 功能：初始化响应状态；参数：请求约束及正文缓冲；返回：无。 */
void ota_http_init(ota_http_t *parser, uint32_t start, uint32_t length,
                   uint32_t total, const char *etag, uint8_t *body) {
    memset(parser, 0, sizeof *parser);
    parser->start = start;
    parser->length = length;
    parser->expected_total = total;
    parser->body = body;
    parser->failed = length == 0U || length > 4096U || body == NULL ||
                     start > UINT32_MAX - length;
    if (etag != NULL && valid_etag(etag)) {
        memcpy(parser->expected_etag, etag, strlen(etag) + 1U);
    } else if (etag != NULL) {
        parser->failed = true;
    }
}

/** 功能：增量接收严格 Range 响应；参数：状态和二进制输入；返回：尚未失败为真。 */
bool ota_http_feed(ota_http_t *parser, const void *bytes, size_t length) {
    const uint8_t *input = bytes;
    if (parser == NULL || parser->failed || (bytes == NULL && length != 0U)) {
        return false;
    }
    for (size_t i = 0U; i < length; ++i) {
        if (parser->complete) {
            parser->failed = true;
            parser->complete = false;
            return false;
        }
        if (!parser->headers_done) {
            uint8_t byte = input[i];
            if (parser->header_used >= 2048U || byte == 0U || byte > 126U ||
                (byte < 32U && byte != '\r' && byte != '\n' && byte != '\t')) {
                parser->failed = true;
                return false;
            }
            size_t n = parser->header_used;
            if ((byte == '\n' && (n == 0U || parser->header[n - 1U] != '\r')) ||
                (n != 0U && parser->header[n - 1U] == '\r' && byte != '\n')) {
                parser->failed = true;
                return false;
            }
            parser->header[n++] = (char)byte;
            parser->header[n] = '\0';
            parser->header_used = n;
            if (n >= 4U && memcmp(parser->header + n - 4U, "\r\n\r\n", 4U) == 0) {
                if (!parse_headers(parser)) {
                    parser->failed = true;
                    return false;
                }
                parser->headers_done = true;
            }
        } else {
            parser->body[parser->received++] = input[i];
            parser->complete = parser->received == parser->length;
        }
    }
    return true;
}
