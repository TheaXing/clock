#include "clock_http.h"
#include <string.h>

enum { REQUEST_LINE, HEADERS, BODY };
enum { SEEN_LENGTH = 1, SEEN_TYPE = 2, SEEN_CLOCK = 4, SEEN_HOST = 8 };

/**
 * 功能：判断字符能否用于 HTTP 方法或请求头名称。
 * 参数：ch，待检查的字符。
 * 返回：字符属于允许的令牌字符时返回 true，否则返回 false。
 */
static bool token_char(unsigned char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
           strchr("!#$%&'*+-.^_`|~", (int)ch) != NULL;
}

/**
 * 功能：将 ASCII 大写字母转换为小写。
 * 参数：ch，输入字符。
 * 返回：转换后的字符，其他字符原样返回。
 */
static unsigned char lower(unsigned char ch) {
    return ch >= 'A' && ch <= 'Z' ? (unsigned char)(ch + ('a' - 'A')) : ch;
}

/**
 * 功能：以忽略 ASCII 大小写的方式比较两个字符串。
 * 参数：a，第一个字符串；b，第二个字符串。
 * 返回：字符串相等返回 true，否则返回 false。
 */
static bool equal_ci(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        if (lower((unsigned char)*a++) != lower((unsigned char)*b++)) {
            return false;
        }
    }
    return *a == *b;
}

/**
 * 功能：解析并校验请求行，提取 GET 或 POST 方法及请求目标。
 * 参数：p，包含完整请求行的解析器。
 * 返回：需要继续接收、请求错误、目标过长或方法不支持的状态。
 */
static clock_http_status_t parse_request_line(clock_http_parser_t *p) {
    char *target, *version, *cursor;
    size_t target_length;
    target = strchr(p->line, ' ');
    if (target == NULL || target == p->line) {
        return CLOCK_HTTP_BAD_REQUEST;
    }
    *target++ = '\0';
    version = strchr(target, ' ');
    if (version == NULL || version == target) {
        return CLOCK_HTTP_BAD_REQUEST;
    }
    *version++ = '\0';
    if (strcmp(version, "HTTP/1.1") != 0 && strcmp(version, "HTTP/1.0") != 0) {
        return CLOCK_HTTP_BAD_REQUEST;
    }
    if (*target != '/') {
        return CLOCK_HTTP_BAD_REQUEST;
    }
    for (cursor = target; *cursor != '\0'; ++cursor) {
        if ((unsigned char)*cursor < 0x21u || (unsigned char)*cursor > 0x7eu || *cursor == '#') {
            return CLOCK_HTTP_BAD_REQUEST;
        }
    }
    target_length = strlen(target);
    if (target_length >= sizeof(p->target)) {
        return CLOCK_HTTP_TOO_LARGE;
    }
    if (strcmp(p->line, "GET") == 0) {
        p->method = CLOCK_HTTP_GET;
    } else if (strcmp(p->line, "POST") == 0) {
        p->method = CLOCK_HTTP_POST;
    } else {
        for (cursor = p->line; *cursor != '\0'; ++cursor) {
            if (!token_char((unsigned char)*cursor)) {
                return CLOCK_HTTP_BAD_REQUEST;
            }
        }
        return CLOCK_HTTP_METHOD_NOT_ALLOWED;
    }
    memcpy(p->target, target, target_length + 1u);
    p->state = HEADERS;
    return CLOCK_HTTP_NEED_MORE;
}

/**
 * 功能：解析一行请求头，拒绝重复关键头及不支持的传输编码。
 * 参数：p，包含完整请求头行的解析器。
 * 返回：需要继续接收、请求就绪或对应错误状态。
 */
static clock_http_status_t parse_header(clock_http_parser_t *p) {
    char *value, *end;
    uint8_t flag;
    if (p->line_length == 0u && !p->line_overflow) {
        if (p->method == CLOCK_HTTP_POST && (p->seen_headers & SEEN_LENGTH) == 0u) {
            return CLOCK_HTTP_BAD_REQUEST;
        }
        if (p->method == CLOCK_HTTP_GET && p->content_length != 0u) {
            return CLOCK_HTTP_BAD_REQUEST;
        }
        p->state = BODY;
        return p->content_length == 0u ? CLOCK_HTTP_READY : CLOCK_HTTP_NEED_MORE;
    }
    if (!p->line_colon) {
        return CLOCK_HTTP_BAD_REQUEST;
    }
    value = strchr(p->line, ':');
    /* 溢出的请求头名称不可能匹配任何已识别的短名称。 */
    if (value == NULL) {
        return CLOCK_HTTP_NEED_MORE;
    }
    *value++ = '\0';
    if (equal_ci(p->line, "Content-Length")) {
        flag = SEEN_LENGTH;
    } else if (equal_ci(p->line, "Content-Type")) {
        flag = SEEN_TYPE;
    } else if (equal_ci(p->line, "X-Clock-Request")) {
        flag = SEEN_CLOCK;
    } else if (equal_ci(p->line, "Host")) {
        flag = SEEN_HOST;
    } else if (equal_ci(p->line, "Transfer-Encoding")) {
        return CLOCK_HTTP_BAD_REQUEST;
    } else {
        return CLOCK_HTTP_NEED_MORE;
    }
    if ((p->seen_headers & flag) != 0u) {
        return CLOCK_HTTP_BAD_REQUEST;
    }
    if (p->line_overflow) {
        return CLOCK_HTTP_TOO_LARGE;
    }
    p->seen_headers |= flag;
    while (*value == ' ' || *value == '\t') {
        ++value;
    }
    end = value + strlen(value);
    while (end != value && (end[-1] == ' ' || end[-1] == '\t')) {
        --end;
    }
    *end = '\0';
    if (flag == SEEN_LENGTH) {
        unsigned length = 0u;
        if (*value == '\0') {
            return CLOCK_HTTP_BAD_REQUEST;
        }
        while (*value != '\0') {
            if (*value < '0' || *value > '9') {
                return CLOCK_HTTP_BAD_REQUEST;
            }
            length = length * 10u + (unsigned)(*value++ - '0');
            if (length > CLOCK_HTTP_BODY_MAX) {
                return CLOCK_HTTP_TOO_LARGE;
            }
        }
        p->content_length = (uint16_t)length;
    } else if (flag == SEEN_CLOCK) {
        p->has_clock_request = strcmp(value, "1") == 0;
    } else if (flag == SEEN_TYPE) {
        char *parameters = strchr(value, ';');
        if (parameters != NULL) {
            *parameters = '\0';
            end = parameters;
            while (end != value && (end[-1] == ' ' || end[-1] == '\t')) {
                --end;
            }
            *end = '\0';
        }
        p->form_content_type = equal_ci(value, "application/x-www-form-urlencoded");
    } else if (*value == '\0') {
        return CLOCK_HTTP_BAD_REQUEST;
    }
    return CLOCK_HTTP_NEED_MORE;
}

/**
 * 功能：清空单连接 HTTP 解析器，准备接收一个请求。
 * 参数：parser，待初始化的解析器。
 * 返回：无。
 */
void clock_http_init(clock_http_parser_t *parser) {
    memset(parser, 0, sizeof(*parser));
}

/**
 * 功能：增量接收 HTTP 字节，校验边界并在完整请求或终止错误时停止。
 * 参数：parser，解析器；data，本次输入字节；length，输入长度。
 * 返回：当前解析状态；终止错误保持，完整请求后的额外字节视为错误。
 */
clock_http_status_t clock_http_feed(clock_http_parser_t *parser, const uint8_t *data,
                                    size_t length) {
    size_t i;
    if (parser->status != CLOCK_HTTP_NEED_MORE) {
        if (parser->status == CLOCK_HTTP_READY && length != 0u) {
            parser->status = CLOCK_HTTP_BAD_REQUEST;
        }
        return parser->status;
    }
    if (data == NULL && length != 0u) {
        return parser->status = CLOCK_HTTP_BAD_REQUEST;
    }
    for (i = 0u; i < length; ++i) {
        unsigned char ch = data[i];
        if (ch == 0u) {
            return parser->status = CLOCK_HTTP_BAD_REQUEST;
        }
        if (parser->state == BODY) {
            parser->body[parser->body_length++] = (char)ch;
            if (parser->body_length == parser->content_length) {
                parser->body[parser->body_length] = '\0';
                parser->status = CLOCK_HTTP_READY;
            }
        } else {
            if (++parser->header_bytes > CLOCK_HTTP_HEADERS_MAX) {
                return parser->status = CLOCK_HTTP_TOO_LARGE;
            }
            if (parser->saw_cr) {
                if (ch != '\n') {
                    return parser->status = CLOCK_HTTP_BAD_REQUEST;
                }
                parser->line[parser->line_length] = '\0';
                parser->status = parser->state == REQUEST_LINE ? parse_request_line(parser)
                                                               : parse_header(parser);
                parser->line_length = 0u;
                parser->line_overflow = false;
                parser->line_colon = false;
                parser->saw_cr = false;
            } else if (ch == '\r') {
                parser->saw_cr = true;
            } else {
                if (ch == '\n' || ch == 0x7fu ||
                    (ch < 0x20u && !(parser->state == HEADERS && ch == '\t'))) {
                    return parser->status = CLOCK_HTTP_BAD_REQUEST;
                }
                if (parser->state == HEADERS && !parser->line_colon) {
                    if (ch == ':' && parser->line_length != 0u) {
                        parser->line_colon = true;
                    } else if (!token_char(ch)) {
                        return parser->status = CLOCK_HTTP_BAD_REQUEST;
                    }
                }
                if (parser->line_length + 1u < sizeof(parser->line)) {
                    parser->line[parser->line_length++] = (char)ch;
                } else {
                    parser->line_overflow = true;
                    if (parser->state == REQUEST_LINE) {
                        return parser->status = CLOCK_HTTP_TOO_LARGE;
                    }
                }
            }
        }
        if (parser->status != CLOCK_HTTP_NEED_MORE) {
            if (parser->status == CLOCK_HTTP_READY && i + 1u != length) {
                parser->status = CLOCK_HTTP_BAD_REQUEST;
            }
            return parser->status;
        }
    }
    return parser->status;
}
