#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "weather.h"

/**
 * 功能：跳过字符串当前位置连续的空白字符。
 * 参数：p，待扫描的字符位置。
 * 返回：首个非空白字符的位置。
 */
static const char *space(const char *p) {
    while (isspace((unsigned char)*p)) {
        ++p;
    }
    return p;
}

/**
 * 功能：校验 JSON 字符串及转义序列，并定位字符串结束位置。
 * 参数：p，指向字符串起始双引号的位置。
 * 返回：闭合双引号之后的位置；格式无效返回 NULL。
 */
static const char *string_end(const char *p) {
    if (*p++ != '"') {
        return NULL;
    }
    while (*p && *p != '"') {
        if ((unsigned char)*p < 0x20) {
            return NULL;
        }
        if (*p++ == '\\') {
            if (*p == 'u') {
                ++p;
                for (unsigned i = 0; i < 4; ++i) {
                    if (!isxdigit((unsigned char)*p)) {
                        return NULL;
                    }
                    ++p;
                }
            } else {
                if (!*p || !strchr("\"\\/bfnrt", *p)) {
                    return NULL;
                }
                ++p;
            }
        }
    }
    return *p == '"' ? p + 1 : NULL;
}

/* 查找字段前先拒绝串口截断的不完整响应。 */
/**
 * 功能：递归校验 JSON 值完整性，拒绝串口截断响应和过深嵌套。
 * 参数：p，待检查值的位置；depth，当前嵌套层数。
 * 返回：值结束后的位置；值不完整或格式无效返回 NULL。
 */
static const char *value_end(const char *p, unsigned depth) {
    p = space(p);
    if (depth > 12 || !*p) {
        return NULL;
    }
    if (*p == '"') {
        return string_end(p);
    }
    if (*p == '{' || *p == '[') {
        const char opening = *p++;
        const char closing = opening == '{' ? '}' : ']';
        p = space(p);
        if (*p == closing) {
            return p + 1;
        }
        for (;;) {
            if (opening == '{') {
                p = string_end(p);
                if (!p || *space(p) != ':') {
                    return NULL;
                }
                p = space(p) + 1;
            }
            p = value_end(p, depth + 1);
            if (!p) {
                return NULL;
            }
            p = space(p);
            if (*p == closing) {
                return p + 1;
            }
            if (*p != ',') {
                return NULL;
            }
            p = space(p + 1);
        }
    }
    const char *start = p;
    while (*p && !isspace((unsigned char)*p) && !strchr(",]}", *p)) {
        ++p;
    }
    if (p == start) {
        return NULL;
    }
    if ((p - start == 4 && memcmp(start, "true", 4) == 0) ||
        (p - start == 4 && memcmp(start, "null", 4) == 0) ||
        (p - start == 5 && memcmp(start, "false", 5) == 0)) {
        return p;
    }
    char *end;
    (void)strtod(start, &end);
    return end == p ? p : NULL;
}

/**
 * 功能：在 JSON 对象中查找指定成员并定位其值。
 * 参数：object，对象起始位置；key，不含双引号的成员名称。
 * 返回：成员值的起始位置；成员不存在或格式无效返回 NULL。
 */
static const char *member(const char *object, const char *key) {
    if (!object || *object != '{') {
        return NULL;
    }
    const char *p = space(object + 1);
    while (*p == '"') {
        const char *end = string_end(p);
        if (!end) {
            return NULL;
        }
        const char *value = space(end);
        if (*value != ':') {
            return NULL;
        }
        value = space(value + 1);
        if ((size_t)(end - p - 2) == strlen(key) && memcmp(p + 1, key, strlen(key)) == 0) {
            return value;
        }
        p = value_end(value, 0);
        if (!p || *space(p) != ',') {
            return NULL;
        }
        p = space(space(p) + 1);
    }
    return NULL;
}

/**
 * 功能：解码 JSON 字符串到指定缓冲区并检查容量。
 * 参数：value，字符串值的起始位置；out，输出缓冲区；size，缓冲区字节容量。
 * 返回：成功提取非空文本返回 true，否则返回 false。
 */
static bool text_value(const char *value, char *out, size_t size) {
    if (!value || *value++ != '"') {
        return false;
    }
    size_t n = 0;
    while (*value && *value != '"') {
        char ch = *value++;
        if (ch == '\\') {
            ch = *value++;
            /* 接口使用英文响应，以兼容原有 GB2312 字体渲染流程。 */
            if (ch == 'b') {
                ch = '\b';
            } else if (ch == 'f') {
                ch = '\f';
            } else if (ch == 'n') {
                ch = '\n';
            } else if (ch == 'r') {
                ch = '\r';
            } else if (ch == 't') {
                ch = '\t';
            } else if (!ch || !strchr("\"\\/", ch)) {
                return false;
            }
        }
        if (n + 1 >= size) {
            return false;
        }
        out[n++] = ch;
    }
    if (*value != '"' || n == 0) {
        return false;
    }
    out[n] = '\0';
    return true;
}

/**
 * 功能：解析心知天气首条结果，校验城市、天气代码和温度后写入输出。
 * 参数：response，包含 JSON 的响应文本；info，天气信息输出位置。
 * 返回：解析成功返回 true，参数或响应无效返回 false。
 */
bool parse_seniverse_response(const char *response, weather_info_t *info) {
    if (!response || !info) {
        return false;
    }
    const char *root = strchr(response, '{');
    if (!root || !value_end(root, 0)) {
        return false;
    }
    const char *results = member(root, "results");
    if (!results || *results != '[') {
        return false;
    }
    const char *first = space(results + 1);
    const char *location = member(first, "location");
    const char *now = member(first, "now");
    weather_info_t parsed = {0};
    char code[12], temperature[24];
    if (!text_value(member(location, "name"), parsed.city, sizeof(parsed.city)) ||
        !text_value(member(location, "path"), parsed.loaction, sizeof(parsed.loaction)) ||
        !text_value(member(now, "text"), parsed.weather, sizeof(parsed.weather)) ||
        !text_value(member(now, "code"), code, sizeof(code)) ||
        !text_value(member(now, "temperature"), temperature, sizeof(temperature))) {
        return false;
    }
    char *end;
    errno = 0;
    long number = strtol(code, &end, 10);
    if (errno || end == code || *end || number < 0 || number > 999) {
        return false;
    }
    parsed.weather_code = (int)number;
    errno = 0;
    parsed.temperature = strtof(temperature, &end);
    if (errno || end == temperature || *end || !isfinite(parsed.temperature)) {
        return false;
    }
    *info = parsed;
    return true;
}
