#include "wifi_form.h"
#include <stddef.h>
#include <string.h>

/** 功能：解码单个十六进制数字；参数：ch，字符；返回：数值或负一。 */
static int hex_value(unsigned char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    return -1;
}

/** 功能：解码表单字段并拒绝控制字节及越界；参数：begin、end，输入范围；output、capacity，输出缓冲区；返回：是否合法。 */
static bool decode(const char *begin, const char *end, char *output, size_t capacity) {
    size_t used = 0U;
    while (begin < end) {
        unsigned char ch = (unsigned char)*begin++;
        if (ch == '%') {
            if (end - begin < 2) {
                return false;
            }
            int high = hex_value((unsigned char)begin[0]);
            int low = hex_value((unsigned char)begin[1]);
            if (high < 0 || low < 0) {
                return false;
            }
            ch = (unsigned char)(high * 16 + low);
            begin += 2;
        } else if (ch == '+') {
            ch = ' ';
        }
        if (ch < 0x20U || ch == 0x7fU || used + 1U >= capacity) {
            return false;
        }
        output[used++] = (char)ch;
    }
    output[used] = '\0';
    return true;
}

/** 功能：校验完整且规范的 UTF-8，拒绝代理项、越界和控制码点；参数：text，字节串；返回：是否合法。 */
static bool valid_utf8(const char *text) {
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        uint32_t code = *p++;
        unsigned tail = 0U;
        uint32_t minimum = 0U;
        if (code >= 0xc2U && code <= 0xdfU) {
            code &= 0x1fU;
            tail = 1U;
            minimum = 0x80U;
        } else if (code >= 0xe0U && code <= 0xefU) {
            code &= 0x0fU;
            tail = 2U;
            minimum = 0x800U;
        } else if (code >= 0xf0U && code <= 0xf4U) {
            code &= 0x07U;
            tail = 3U;
            minimum = 0x10000U;
        } else if (code >= 0x80U) {
            return false;
        }
        for (unsigned i = 0U; i < tail; ++i) {
            if ((*p & 0xc0U) != 0x80U) {
                return false;
            }
            code = (code << 6U) | (*p++ & 0x3fU);
        }
        if (code < minimum || code > 0x10ffffU || (code >= 0xd800U && code <= 0xdfffU) ||
            code < 0x20U || (code >= 0x7fU && code <= 0x9fU)) {
            return false;
        }
    }
    return true;
}

/** 功能：严格解析唯一会话、名称与密码字段；参数：body，表单；expected_session，当前非零会话；ssid、password，凭据输出；返回：完整校验成功时为真。 */
bool wifi_form_parse(const char *body, uint32_t expected_session, char ssid[33], char password[64]) {
    char name[33] = {0}, secret[64] = {0}, session[11] = {0};
    unsigned seen = 0U;
    if (!body || !ssid || !password || !expected_session || !*body) {
        return false;
    }
    const char *cursor = body;
    do {
        const char *end = strchr(cursor, '&');
        if (!end) {
            end = cursor + strlen(cursor);
        }
        const char *equals = memchr(cursor, '=', (size_t)(end - cursor));
        if (!equals) {
            return false;
        }
        size_t length = (size_t)(equals - cursor);
        unsigned flag;
        char *output;
        size_t capacity;
        if (length == 7U && memcmp(cursor, "session", 7U) == 0) {
            flag = 1U;
            output = session;
            capacity = sizeof session;
        } else if (length == 4U && memcmp(cursor, "ssid", 4U) == 0) {
            flag = 2U;
            output = name;
            capacity = sizeof name;
        } else if (length == 8U && memcmp(cursor, "password", 8U) == 0) {
            flag = 4U;
            output = secret;
            capacity = sizeof secret;
        } else {
            return false;
        }
        if ((seen & flag) || !decode(equals + 1, end, output, capacity)) {
            return false;
        }
        seen |= flag;
        if (!*end) {
            break;
        }
        cursor = end + 1;
        if (!*cursor) {
            return false;
        }
    } while (true);
    if (seen != 7U || !*session || !*name || !valid_utf8(name)) {
        return false;
    }
    uint32_t value = 0U;
    for (const char *p = session; *p; ++p) {
        if (*p < '0' || *p > '9' || value > (UINT32_MAX - (uint32_t)(*p - '0')) / 10U) {
            return false;
        }
        value = value * 10U + (uint32_t)(*p - '0');
    }
    size_t length = strlen(secret);
    if (value != expected_session || (length && length < 8U)) {
        return false;
    }
    for (size_t i = 0U; i < length; ++i) {
        if ((unsigned char)secret[i] > 0x7eU) {
            return false;
        }
    }
    memcpy(ssid, name, sizeof name);
    memcpy(password, secret, sizeof secret);
    return true;
}
