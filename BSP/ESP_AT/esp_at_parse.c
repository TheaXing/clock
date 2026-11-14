#include "esp_at_parse.h"

#include <string.h>
#include <stdio.h>

/** 功能：构造实际有界 HTTP 命令并校验模块的 255 字节上限；参数：URL、超时、输出和容量；返回：完整转义命令可用为真。 */
bool esp_at_build_http_get_bounded(const char *url, uint32_t timeout_ms, char *output, size_t capacity) {
    static const char prefix[] = "AT+HTTPCGET=\"";
    size_t length;
    int count;
    if (url == NULL || url[0] == '\0' || output == NULL || capacity <= sizeof prefix ||
        timeout_ms < 100U || timeout_ms > 3000U) {
        return false;
    }
    memcpy(output, prefix, sizeof prefix - 1U);
    if (!esp_at_quote(url, 512U, output + sizeof prefix - 1U, capacity - sizeof prefix + 1U)) {
        return false;
    }
    length = strlen(output);
    count = snprintf(output + length, capacity - length, "\",2048,2048,%lu\r\n", (unsigned long)timeout_ms);
    return count > 0 && (size_t)count < capacity - length && length + (size_t)count <= ESP_HTTP_COMMAND_MAX;
}

/**
 * 功能：在多行应答中查找位于行首的指定前缀。
 * 参数：text，待处理文本；prefix，需要匹配的行首前缀。
 * 返回：匹配前缀之后的位置，未找到或文本为空返回 NULL。
 */
static const char *find_line(const char *text, const char *prefix) {
    const char *p = text;
    if (text == NULL) {
        return NULL;
    }
    while ((p = strstr(p, prefix)) != NULL) {
        if (p == text || p[-1] == '\n') {
            return p + strlen(prefix);
        }
        ++p;
    }
    return NULL;
}

/**
 * 功能：从当前位置解析不超过上限的无符号十进制整数。
 * 参数：text，解析游标的输入输出地址；limit，允许解析的最大数值；value，接收解析结果的地址。
 * 返回：解析成功返回 true 并推进文本指针，否则返回 false。
 */
static bool number(const char **text, unsigned limit, unsigned *value) {
    const char *p = *text;
    unsigned n = 0;
    if (*p < '0' || *p > '9') {
        return false;
    }
    do {
        unsigned digit = (unsigned)(*p - '0');
        if (n > limit / 10U || (n == limit / 10U && digit > limit % 10U)) {
            return false;
        }
        n = n * 10U + digit;
        ++p;
    } while (*p >= '0' && *p <= '9');
    *text = p;
    *value = n;
    return true;
}

/**
 * 功能：判断当前位置是否为字符串结尾或合法换行。
 * 参数：p，待检查的文本位置。
 * 返回：位于行末返回 true，否则返回 false。
 */
static bool line_end(const char *p) {
    return *p == '\0' || *p == '\n' || (p[0] == '\r' && p[1] == '\n');
}

/**
 * 功能：从地址查询应答中提取非全零的合法 IPv4 地址。
 * 参数：response，待解析的完整 AT 应答；ip，接收 IPv4 文本的十六字节缓冲区。
 * 返回：解析成功返回 true，否则返回 false。
 */
bool esp_at_parse_station_ip(const char *response, char ip[16]) {
    const char *p = find_line(response, "+CIFSR:STAIP,");
    if (p == NULL || ip == NULL || *p++ != '"') {
        return false;
    }
    const char *start = p;
    unsigned octet, nonzero = 0U;
    for (unsigned n = 0U; n < 4U; ++n) {
        const char *part = p;
        if (!number(&p, 255U, &octet) || p - part > 3) {
            return false;
        }
        nonzero |= octet;
        if (n < 3U && *p++ != '.') {
            return false;
        }
    }
    size_t length = (size_t)(p - start);
    if (nonzero == 0U || length >= 16U || *p++ != '"' || !line_end(p)) {
        return false;
    }
    memcpy(ip, start, length);
    ip[length] = '\0';
    return true;
}

/**
 * 功能：解析带引号字符串并还原允许的反斜线转义。
 * 参数：text，解析游标的输入输出地址；output，接收文本结果的缓冲区；capacity，输出缓冲区总容量，包含结束空字节。
 * 返回：解析成功返回 true 并推进文本指针，否则返回 false。
 */
static bool quoted(const char **text, char *output, size_t capacity) {
    const char *p = *text;
    size_t n = 0;
    if (*p++ != '"') {
        return false;
    }
    while (*p != '"') {
        unsigned char c = (unsigned char)*p++;
        if (c < 32U || c == 127U) {
            return false;
        }
        if (c == '\\') {
            c = (unsigned char)*p++;
            if (c != '\\' && c != '"' && c != ',') {
                return false;
            }
        }
        if (n + 1U >= capacity) {
            return false;
        }
        output[n++] = (char)c;
    }
    output[n] = '\0';
    *text = p + 1;
    return true;
}

/**
 * 功能：判断字符是否为十六进制数字。
 * 参数：c，待检查字符。
 * 返回：属于十六进制数字返回 true，否则返回 false。
 */
static bool is_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/**
 * 功能：校验冒号分隔的六字节 MAC 地址文本。
 * 参数：mac，可选热点 MAC 地址，空指针表示不锁定。
 * 返回：格式有效返回 true，否则返回 false。
 */
bool esp_at_valid_mac(const char *mac) {
    if (mac == NULL || strlen(mac) != 17U) {
        return false;
    }
    for (size_t n = 0; n < 17U; ++n) {
        if (n % 3U == 2U ? mac[n] != ':' : !is_hex(mac[n])) {
            return false;
        }
    }
    return true;
}

/**
 * 功能：解析无线状态应答中的热点名称和连接状态。
 * 参数：response，待解析的完整 AT 应答；info，接收无线连接信息的地址。
 * 返回：解析成功返回 true 并更新结果，否则返回 false。
 */
bool esp_at_parse_cwstate(const char *response, esp_wifi_info_t *info) {
    esp_wifi_info_t parsed = {0};
    unsigned state;
    const char *p = find_line(response, "+CWSTATE:");
    if (info == NULL || p == NULL || !number(&p, 4U, &state) || *p++ != ',') {
        return false;
    }
    if (!quoted(&p, parsed.ssid, sizeof parsed.ssid) || !line_end(p)) {
        return false;
    }
    parsed.connected = state == 2U;
    *info = parsed;
    return true;
}

/**
 * 功能：解析已连接热点的名称、地址、信道与信号强度。
 * 参数：response，待解析的完整 AT 应答；info，接收无线连接信息的地址。
 * 返回：解析成功返回 true 并更新结果，否则返回 false。
 */
bool esp_at_parse_cwjap(const char *response, esp_wifi_info_t *info) {
    esp_wifi_info_t parsed;
    unsigned channel, magnitude;
    const char *p = find_line(response, "+CWJAP:");
    if (info == NULL || p == NULL) {
        return false;
    }
    parsed = *info;
    if (!quoted(&p, parsed.ssid, sizeof parsed.ssid) || *p++ != ',') {
        return false;
    }
    if (!quoted(&p, parsed.bssid, sizeof parsed.bssid) || !esp_at_valid_mac(parsed.bssid) ||
        *p++ != ',') {
        return false;
    }
    if (!number(&p, 14U, &channel) || channel == 0U || *p++ != ',') {
        return false;
    }
    bool negative = *p == '-';
    if (negative) {
        ++p;
    }
    if (!number(&p, 127U, &magnitude) || (!negative && magnitude != 0U)) {
        return false;
    }
    if (*p != ',' && !line_end(p)) {
        return false;
    }
    parsed.channel = (int)channel;
    parsed.rssi = -(int)magnitude;
    *info = parsed;
    return true;
}

/**
 * 功能：匹配三字符英文日期标记并跳过尾随空格。
 * 参数：text，日期标记游标的输入输出地址；table，三字符标记表；count，标记表条目数。
 * 返回：匹配项的一基序号，未匹配返回 0。
 */
static unsigned token(const char **text, const char *const *table, unsigned count) {
    const char *p = *text;
    if (strlen(p) < 4U || p[3] != ' ') {
        return 0;
    }
    for (unsigned n = 0; n < count; ++n) {
        if (memcmp(p, table[n], 3U) == 0) {
            *text = p + 4;
            return n + 1U;
        }
    }
    return 0;
}

/**
 * 功能：解析英文网络校时应答并校验日期、时间及支持年份。
 * 参数：response，待解析的完整 AT 应答；date，接收解析日期时间的地址。
 * 返回：解析成功返回 true，否则返回 false。
 */
bool esp_at_parse_sntp(const char *response, esp_date_time_t *date) {
    static const char *const weekdays[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    static const char *const months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    static const unsigned days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    unsigned weekday, month, day, hour, minute, second, year;
    const char *p = find_line(response, "+CIPSNTPTIME:");
    if (date == NULL || p == NULL) {
        return false;
    }
    weekday = token(&p, weekdays, 7U);
    month = token(&p, months, 12U);
    if (weekday == 0U || month == 0U) {
        return false;
    }
    while (*p == ' ') {
        ++p; /* ctime 格式使用空格填充一位数日期。 */
    }
    if (!number(&p, 31U, &day) || day == 0U || *p++ != ' ') {
        return false;
    }
    if (!number(&p, 23U, &hour) || *p++ != ':') {
        return false;
    }
    if (!number(&p, 59U, &minute) || *p++ != ':') {
        return false;
    }
    if (!number(&p, 59U, &second) || *p++ != ' ') {
        return false;
    }
    /* STM32 RTC 仅存储两位年份；1970 还表示 SNTP 尚未同步。 */
    if (!number(&p, 2099U, &year) || year < 2000U || !line_end(p)) {
        return false;
    }
    unsigned max_day = days[month - 1U];
    if (month == 2U && year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U)) {
        ++max_day;
    }
    if (day > max_day) {
        return false;
    }
    esp_date_time_t parsed = {(uint16_t)year,  (uint8_t)month,  (uint8_t)day,    (uint8_t)hour,
                              (uint8_t)minute, (uint8_t)second, (uint8_t)weekday};
    *date = parsed;
    return true;
}

/**
 * 功能：转义 AT 参数中的引号、反斜线与逗号，并拒绝控制字符。
 * 参数：input，待转义的输入文本；
 *       max_input，允许的最大输入字符数；
 *       output，接收文本结果的缓冲区；
 *       capacity，输出缓冲区总容量，包含结束空字节。
 * 返回：输入有效且输出容量足够返回 true，否则返回 false。
 */
bool esp_at_quote(const char *input, size_t max_input, char *output, size_t capacity) {
    size_t n = 0, written = 0;
    if (input == NULL || output == NULL || capacity == 0U) {
        return false;
    }
    while (input[n] != '\0') {
        unsigned char c = (unsigned char)input[n++];
        if (n > max_input || c < 32U || c == 127U) {
            return false;
        }
        if (c == '"' || c == '\\' || c == ',') {
            if (written + 1U >= capacity) {
                return false;
            }
            output[written++] = '\\';
        }
        if (written + 1U >= capacity) {
            return false;
        }
        output[written++] = (char)c;
    }
    output[written] = '\0';
    return true;
}

/**
 * 功能：校验定长 HTTP 报告，并可选拼接其正文。
 * 参数：response，待解析的完整 AT 应答；
 *       body，接收正文的缓冲区，空指针表示仅校验；
 *       capacity，输出缓冲区总容量，包含结束空字节。
 * 返回：至少接收一段非空合法载荷返回 true，否则返回 false。
 */
static bool http_body(const char *response, char *body, size_t capacity) {
    bool received = false;
    size_t written = 0U;
    const char *prefix = "+HTTPCLIENT:";
    const char *p = find_line(response, prefix);
    if (p == NULL) {
        prefix = "+HTTPCGET:";
        p = find_line(response, prefix);
    }
    while (p != NULL) {
        unsigned length;
        if (!number(&p, 2047U, &length) || *p++ != ',' || strlen(p) < length) {
            goto invalid;
        }
        if (body != NULL) {
            if (length > capacity - written - 1U) {
                goto invalid;
            }
            memcpy(body + written, p, length);
            written += length;
        }
        received = received || length != 0U;
        p += length;
        if (!line_end(p)) {
            goto invalid;
        }
        p = find_line(p, prefix);
    }
    if (body != NULL) {
        body[written] = '\0';
    }
    return received;

invalid:
    if (body != NULL) {
        body[0] = '\0';
    }
    return false;
}

/**
 * 功能：校验应答内 HTTP 定长报告的完整性及非空载荷。
 * 参数：response，待解析的完整 AT 应答。
 * 返回：存在完整非空正文返回 true，否则返回 false。
 */
bool esp_at_validate_http(const char *response) {
    return http_body(response, NULL, 0U);
}

/**
 * 功能：校验 HTTP 报告并将正文拼接到输出缓冲区。
 * 参数：response，待解析的完整 AT 应答；
 *       body，接收正文的缓冲区，空指针表示仅校验；
 *       capacity，输出缓冲区总容量，包含结束空字节。
 * 返回：提取完整非空正文成功返回 true，否则返回 false。
 */
bool esp_at_extract_http(const char *response, char *body, size_t capacity) {
    return body != NULL && capacity != 0U && http_body(response, body, capacity);
}
