#include "esp_at_parse.h"

#include <string.h>

static const char *find_line(const char *text, const char *prefix)
{
    const char *p = text;
    if (text == NULL) return NULL;
    while ((p = strstr(p, prefix)) != NULL) {
        if (p == text || p[-1] == '\n') return p + strlen(prefix);
        ++p;
    }
    return NULL;
}

static bool number(const char **text, unsigned limit, unsigned *value)
{
    const char *p = *text;
    unsigned n = 0;
    if (*p < '0' || *p > '9') return false;
    do {
        unsigned digit = (unsigned)(*p - '0');
        if (n > limit / 10U || (n == limit / 10U && digit > limit % 10U)) return false;
        n = n * 10U + digit;
        ++p;
    } while (*p >= '0' && *p <= '9');
    *text = p;
    *value = n;
    return true;
}

static bool line_end(const char *p)
{
    return *p == '\0' || *p == '\n' || (p[0] == '\r' && p[1] == '\n');
}

static bool quoted(const char **text, char *output, size_t capacity)
{
    const char *p = *text;
    size_t n = 0;
    if (*p++ != '"') return false;
    while (*p != '"') {
        unsigned char c = (unsigned char)*p++;
        if (c < 32U || c == 127U) return false;
        if (c == '\\') {
            c = (unsigned char)*p++;
            if (c != '\\' && c != '"' && c != ',') return false;
        }
        if (n + 1U >= capacity) return false;
        output[n++] = (char)c;
    }
    output[n] = '\0';
    *text = p + 1;
    return true;
}

static bool is_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool esp_at_valid_mac(const char *mac)
{
    if (mac == NULL || strlen(mac) != 17U) return false;
    for (size_t n = 0; n < 17U; ++n) {
        if (n % 3U == 2U ? mac[n] != ':' : !is_hex(mac[n])) return false;
    }
    return true;
}

bool esp_at_parse_cwstate(const char *response, esp_wifi_info_t *info)
{
    esp_wifi_info_t parsed = {0};
    unsigned state;
    const char *p = find_line(response, "+CWSTATE:");
    if (info == NULL || p == NULL || !number(&p, 4U, &state) || *p++ != ',') return false;
    if (!quoted(&p, parsed.ssid, sizeof parsed.ssid) || !line_end(p)) return false;
    parsed.connected = state == 2U;
    *info = parsed;
    return true;
}

bool esp_at_parse_cwjap(const char *response, esp_wifi_info_t *info)
{
    esp_wifi_info_t parsed;
    unsigned channel, magnitude;
    const char *p = find_line(response, "+CWJAP:");
    if (info == NULL || p == NULL) return false;
    parsed = *info;
    if (!quoted(&p, parsed.ssid, sizeof parsed.ssid) || *p++ != ',') return false;
    if (!quoted(&p, parsed.bssid, sizeof parsed.bssid) || !esp_at_valid_mac(parsed.bssid) || *p++ != ',') return false;
    if (!number(&p, 14U, &channel) || channel == 0U || *p++ != ',') return false;
    bool negative = *p == '-';
    if (negative) ++p;
    if (!number(&p, 127U, &magnitude) || (!negative && magnitude != 0U)) return false;
    if (*p != ',' && !line_end(p)) return false;
    parsed.channel = (int)channel;
    parsed.rssi = -(int)magnitude;
    *info = parsed;
    return true;
}

static unsigned token(const char **text, const char *const *table, unsigned count)
{
    const char *p = *text;
    if (strlen(p) < 4U || p[3] != ' ') return 0;
    for (unsigned n = 0; n < count; ++n) {
        if (memcmp(p, table[n], 3U) == 0) {
            *text = p + 4;
            return n + 1U;
        }
    }
    return 0;
}

bool esp_at_parse_sntp(const char *response, esp_date_time_t *date)
{
    static const char *const weekdays[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    static const char *const months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    static const unsigned days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    unsigned weekday, month, day, hour, minute, second, year;
    const char *p = find_line(response, "+CIPSNTPTIME:");
    if (date == NULL || p == NULL) return false;
    weekday = token(&p, weekdays, 7U);
    month = token(&p, months, 12U);
    if (weekday == 0U || month == 0U) return false;
    while (*p == ' ') ++p; /* ctime uses a space-padded, one-digit day. */
    if (!number(&p, 31U, &day) || day == 0U || *p++ != ' ') return false;
    if (!number(&p, 23U, &hour) || *p++ != ':') return false;
    if (!number(&p, 59U, &minute) || *p++ != ':') return false;
    if (!number(&p, 59U, &second) || *p++ != ' ') return false;
    /* STM32 RTC stores a two-digit year; 1970 also denotes unsynchronized SNTP. */
    if (!number(&p, 2099U, &year) || year < 2000U || !line_end(p)) return false;
    unsigned max_day = days[month - 1U];
    if (month == 2U && year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U)) ++max_day;
    if (day > max_day) return false;
    esp_date_time_t parsed = {(uint16_t)year, (uint8_t)month, (uint8_t)day, (uint8_t)hour,
                              (uint8_t)minute, (uint8_t)second, (uint8_t)weekday};
    *date = parsed;
    return true;
}

bool esp_at_quote(const char *input, size_t max_input, char *output, size_t capacity)
{
    size_t n = 0, written = 0;
    if (input == NULL || output == NULL || capacity == 0U) return false;
    while (input[n] != '\0') {
        unsigned char c = (unsigned char)input[n++];
        if (n > max_input || c < 32U || c == 127U) return false;
        if (c == '"' || c == '\\' || c == ',') {
            if (written + 1U >= capacity) return false;
            output[written++] = '\\';
        }
        if (written + 1U >= capacity) return false;
        output[written++] = (char)c;
    }
    output[written] = '\0';
    return true;
}

static bool http_body(const char *response, char *body, size_t capacity)
{
    bool received = false;
    size_t written = 0U;
    const char *p = find_line(response, "+HTTPCLIENT:");
    while (p != NULL) {
        unsigned length;
        if (!number(&p, 2047U, &length) || *p++ != ',' || strlen(p) < length) goto invalid;
        if (body != NULL) {
            if (length > capacity - written - 1U) goto invalid;
            memcpy(body + written, p, length);
            written += length;
        }
        received = received || length != 0U;
        p += length;
        if (!line_end(p)) goto invalid;
        p = find_line(p, "+HTTPCLIENT:");
    }
    if (body != NULL) body[written] = '\0';
    return received;

invalid:
    if (body != NULL) body[0] = '\0';
    return false;
}

bool esp_at_validate_http(const char *response)
{
    return http_body(response, NULL, 0U);
}

bool esp_at_extract_http(const char *response, char *body, size_t capacity)
{
    return body != NULL && capacity != 0U && http_body(response, body, capacity);
}
