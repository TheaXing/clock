#include "onenet_model.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define JSON_TOKEN_LIMIT 96U
#define JSON_DEPTH_LIMIT 8U

typedef enum { JSON_OBJECT, JSON_ARRAY, JSON_STRING, JSON_NUMBER, JSON_TRUE, JSON_FALSE, JSON_NULL } json_kind_t;
typedef struct {
    uint16_t start, length, next;
    uint8_t kind;
} json_token_t;
typedef struct {
    const char *data;
    size_t length, position;
    uint16_t count;
    json_token_t tokens[JSON_TOKEN_LIMIT];
} json_parser_t;

typedef struct { char *out; size_t capacity, used; bool failed; } json_writer_t;

static const char *const field_names[] = {
    "enabled", "hour", "minute", "repeat_mask", "year", "month", "day", "ring_seconds", "snooze_seconds"
};
static const char *const telemetry_names[] = {
    "temperature", "humidity", "sensor_valid", "config_revision", "ringing_mask"
};

static bool decimal(char c) { return c >= '0' && c <= '9'; }
static void whitespace(json_parser_t *p)
{
    while (p->position < p->length) {
        char c = p->data[p->position];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        ++p->position;
    }
}
static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static bool unicode_quad(json_parser_t *p, unsigned *value)
{
    unsigned i, n = 0;
    if (p->length - p->position < 4U) return false;
    for (i = 0; i < 4U; ++i) {
        int h = hex_digit(p->data[p->position++]);
        if (h < 0) return false;
        n = n * 16U + (unsigned)h;
    }
    *value = n;
    return true;
}
static bool utf8_tail(json_parser_t *p, unsigned char first)
{
    unsigned tails, code, minimum, i;
    if (first >= 0xc2U && first <= 0xdfU) { tails = 1U; code = first & 0x1fU; minimum = 0x80U; }
    else if (first >= 0xe0U && first <= 0xefU) { tails = 2U; code = first & 0x0fU; minimum = 0x800U; }
    else if (first >= 0xf0U && first <= 0xf4U) { tails = 3U; code = first & 7U; minimum = 0x10000U; }
    else return false;
    if (p->length - p->position < tails) return false;
    for (i = 0; i < tails; ++i) {
        unsigned char next = (unsigned char)p->data[p->position++];
        if (next < 0x80U || next > 0xbfU) return false;
        code = (code << 6U) | (next & 0x3fU);
    }
    return code >= minimum && code <= 0x10ffffU && !(code >= 0xd800U && code <= 0xdfffU);
}
static bool string_token(json_parser_t *p, json_token_t *token)
{
    size_t start = ++p->position;
    token->start = (uint16_t)start;
    while (p->position < p->length) {
        unsigned char c = (unsigned char)p->data[p->position++];
        if (c == '"') {
            token->length = (uint16_t)(p->position - start - 1U);
            return true;
        }
        if (c < 0x20U) return false;
        if (c >= 0x80U) { if (!utf8_tail(p, c)) return false; }
        else if (c == '\\') {
            unsigned first, second;
            if (p->position == p->length) return false;
            c = (unsigned char)p->data[p->position++];
            if (c == 'u') {
                if (!unicode_quad(p, &first)) return false;
                if (first >= 0xd800U && first <= 0xdbffU) {
                    if (p->length - p->position < 6U || p->data[p->position] != '\\' || p->data[p->position + 1U] != 'u') return false;
                    p->position += 2U;
                    if (!unicode_quad(p, &second) || second < 0xdc00U || second > 0xdfffU) return false;
                } else if (first >= 0xdc00U && first <= 0xdfffU) return false;
            } else if (c != '"' && c != '\\' && c != '/' && c != 'b' && c != 'f' && c != 'n' && c != 'r' && c != 't') return false;
        }
    }
    return false;
}
static bool number_token(json_parser_t *p)
{
    if (p->data[p->position] == '-') {
        if (++p->position == p->length) return false;
    }
    if (p->data[p->position] == '0') ++p->position;
    else {
        if (p->data[p->position] < '1' || p->data[p->position] > '9') return false;
        do { ++p->position; } while (p->position < p->length && decimal(p->data[p->position]));
    }
    if (p->position < p->length && p->data[p->position] == '.') {
        ++p->position;
        if (p->position == p->length || !decimal(p->data[p->position])) return false;
        do { ++p->position; } while (p->position < p->length && decimal(p->data[p->position]));
    }
    if (p->position < p->length && (p->data[p->position] == 'e' || p->data[p->position] == 'E')) {
        ++p->position;
        if (p->position < p->length && (p->data[p->position] == '+' || p->data[p->position] == '-')) ++p->position;
        if (p->position == p->length || !decimal(p->data[p->position])) return false;
        do { ++p->position; } while (p->position < p->length && decimal(p->data[p->position]));
    }
    return true;
}
static bool parse_value(json_parser_t *p, unsigned depth)
{
    uint16_t index;
    json_token_t *token;
    char c;
    if (depth > JSON_DEPTH_LIMIT || p->count >= JSON_TOKEN_LIMIT) return false;
    whitespace(p);
    if (p->position == p->length) return false;
    index = p->count++;
    token = &p->tokens[index];
    token->start = (uint16_t)p->position;
    c = p->data[p->position];
    if (c == '{' || c == '[') {
        bool object = c == '{';
        char end = object ? '}' : ']';
        token->kind = (uint8_t)(object ? JSON_OBJECT : JSON_ARRAY);
        ++p->position;
        whitespace(p);
        if (p->position < p->length && p->data[p->position] == end) ++p->position;
        else for (;;) {
            if (object) {
                if (p->position == p->length || p->data[p->position] != '"' || !parse_value(p, depth + 1U)) return false;
                whitespace(p);
                if (p->position == p->length || p->data[p->position++] != ':') return false;
            }
            if (!parse_value(p, depth + 1U)) return false;
            whitespace(p);
            if (p->position == p->length) return false;
            c = p->data[p->position++];
            if (c == end) break;
            if (c != ',') return false;
            whitespace(p);
        }
    } else if (c == '"') {
        token->kind = JSON_STRING;
        if (!string_token(p, token)) return false;
    } else if (c == '-' || decimal(c)) {
        token->kind = JSON_NUMBER;
        if (!number_token(p)) return false;
    } else {
        const char *literal;
        size_t amount;
        if (c == 't') { literal = "true"; token->kind = JSON_TRUE; }
        else if (c == 'f') { literal = "false"; token->kind = JSON_FALSE; }
        else if (c == 'n') { literal = "null"; token->kind = JSON_NULL; }
        else return false;
        amount = strlen(literal);
        if (p->length - p->position < amount || memcmp(p->data + p->position, literal, amount) != 0) return false;
        p->position += amount;
    }
    if (token->kind != JSON_STRING) token->length = (uint16_t)(p->position - token->start);
    token->next = p->count;
    return true;
}
/* Identifiers are ASCII. Decode JSON escapes so escaped duplicates cannot
 * bypass the same validation as their literal spelling. */
static bool token_ascii(const json_parser_t *p, uint16_t index, char *out, size_t capacity)
{
    const json_token_t *token = &p->tokens[index];
    size_t position = token->start, end = position + token->length, used = 0;
    if (token->kind != JSON_STRING || capacity == 0U) return false;
    while (position < end) {
        unsigned c = (unsigned char)p->data[position++];
        if (c == '\\') {
            c = (unsigned char)p->data[position++];
            if (c == 'u') {
                unsigned i;
                c = 0;
                for (i = 0; i < 4U; ++i) c = c * 16U + (unsigned)hex_digit(p->data[position++]);
            } else if (c == 'b') c = '\b';
            else if (c == 'f') c = '\f';
            else if (c == 'n') c = '\n';
            else if (c == 'r') c = '\r';
            else if (c == 't') c = '\t';
        }
        if (c == 0U || c > 127U || used + 1U >= capacity) return false;
        out[used++] = (char)c;
    }
    out[used] = '\0';
    return true;
}
static bool valid_id(const char *id)
{
    size_t i;
    if (id == NULL) return false;
    for (i = 0; i < ONENET_MODEL_ID_SIZE; ++i) {
        if (id[i] == '\0') return i > 0U;
        if (i == ONENET_MODEL_ID_SIZE - 1U || !decimal(id[i])) return false;
    }
    return false;
}
static onenet_model_status_t envelope(json_parser_t *p, const char *data, size_t length,
    char id[ONENET_MODEL_ID_SIZE], uint16_t *params)
{
    unsigned seen = 0;
    uint16_t i;
    bool invalid = false, ambiguous_id = false;
    char key[20], text[ONENET_MODEL_ID_SIZE];
    id[0] = '\0';
    if (data == NULL) return ONENET_MODEL_INVALID_REQUEST;
    if (length > ONENET_MODEL_MAX_PAYLOAD) return ONENET_MODEL_TOO_LARGE;
    memset(p, 0, sizeof *p);
    p->data = data;
    p->length = length;
    if (!parse_value(p, 0U)) return ONENET_MODEL_INVALID_JSON;
    whitespace(p);
    if (p->position != length) return ONENET_MODEL_INVALID_JSON;
    if (p->tokens[0].kind != JSON_OBJECT) return ONENET_MODEL_INVALID_REQUEST;
    for (i = 1U; i < p->tokens[0].next;) {
        uint16_t value = (uint16_t)(i + 1U);
        unsigned bit = 0;
        if (!token_ascii(p, i, key, sizeof key)) invalid = true;
        else if (strcmp(key, "id") == 0) {
            bit = 1U;
            if ((seen & bit) != 0U) ambiguous_id = true;
            if (!token_ascii(p, value, text, sizeof text) || !valid_id(text)) { invalid = true; ambiguous_id = true; }
            else memcpy(id, text, strlen(text) + 1U);
        } else if (strcmp(key, "version") == 0) {
            bit = 2U;
            if (!token_ascii(p, value, text, sizeof text) || strcmp(text, "1.0") != 0) invalid = true;
        } else if (strcmp(key, "params") == 0) {
            bit = 4U;
            *params = value;
        } else invalid = true;
        if (bit != 0U && (seen & bit) != 0U) invalid = true;
        seen |= bit;
        i = p->tokens[value].next;
    }
    if (ambiguous_id) id[0] = '\0';
    if (invalid || (seen & 5U) != 5U || id[0] == '\0') return ONENET_MODEL_INVALID_REQUEST;
    return ONENET_MODEL_OK;
}
static unsigned field_index(const char *name)
{
    unsigned i;
    for (i = 0; i < 9U; ++i) if (strcmp(name, field_names[i]) == 0) return i;
    return 9U;
}
static int alarm_index(const char *name)
{
    if (strlen(name) == 6U && memcmp(name, "alarm", 5U) == 0 && name[5] >= '1' && name[5] <= '8') return name[5] - '1';
    return -1;
}
static uint16_t property_bit(const char *name)
{
    int index = alarm_index(name);
    unsigned i;
    if (index >= 0) return ONENET_PROPERTY_ALARM((unsigned)index);
    for (i = 0; i < 5U; ++i) if (strcmp(name, telemetry_names[i]) == 0) return (uint16_t)(1U << (i + 8U));
    return 0;
}
static bool token_uint(const json_parser_t *p, uint16_t index, uint32_t *value)
{
    const json_token_t *token = &p->tokens[index];
    uint32_t n = 0;
    unsigned i;
    if (token->kind != JSON_NUMBER) return false;
    for (i = 0; i < token->length; ++i) {
        char c = p->data[token->start + i];
        unsigned digit;
        if (!decimal(c)) return false;
        digit = (unsigned)(c - '0');
        if (n > (UINT32_MAX - digit) / 10U) return false;
        n = n * 10U + digit;
    }
    *value = n;
    return true;
}
static uint32_t entry_field(const alarm_entry_t *entry, unsigned field)
{
    switch (field) {
        case 0: return entry->enabled;
        case 1: return entry->hour;
        case 2: return entry->minute;
        case 3: return entry->repeat_mask;
        case 4: return entry->year;
        case 5: return entry->month;
        case 6: return entry->day;
        case 7: return entry->ring_seconds;
        default: return entry->snooze_seconds;
    }
}
static void assign_field(alarm_entry_t *entry, unsigned field, uint32_t value)
{
    switch (field) {
        case 0: entry->enabled = (uint8_t)value; break;
        case 1: entry->hour = (uint8_t)value; break;
        case 2: entry->minute = (uint8_t)value; break;
        case 3: entry->repeat_mask = (uint8_t)value; break;
        case 4: entry->year = (uint16_t)value; break;
        case 5: entry->month = (uint8_t)value; break;
        case 6: entry->day = (uint8_t)value; break;
        case 7: entry->ring_seconds = (uint16_t)value; break;
        default: entry->snooze_seconds = (uint16_t)value; break;
    }
}
static bool valid_field(unsigned field, uint32_t value)
{
    static const unsigned minimum[] = { 0, 0, 0, 0, 2020, 1, 1, 5, 60 };
    static const unsigned maximum[] = { 1, 23, 59, 127, 2099, 12, 31, 600, 1800 };
    return value >= minimum[field] && value <= maximum[field] && (field != 8U || value % 60U == 0U);
}
static bool valid_entry(const alarm_entry_t *entry)
{
    static const uint8_t month_days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    unsigned i, days;
    for (i = 0; i < 9U; ++i) if (!valid_field(i, entry_field(entry, i))) return false;
    days = month_days[entry->month - 1U];
    if (entry->month == 2U && entry->year % 4U == 0U) ++days;
    return entry->day <= days;
}
static uint32_t patch_fingerprint(const onenet_set_request_t *request)
{
    uint32_t hash = 2166136261U;
    unsigned i, byte;
    hash = (hash ^ request->index) * 16777619U;
    hash = (hash ^ (request->fields_mask & 0xffU)) * 16777619U;
    hash = (hash ^ (request->fields_mask >> 8U)) * 16777619U;
    for (i = 0; i < 9U; ++i) if ((request->fields_mask & (1U << i)) != 0U) {
        uint32_t value = entry_field(&request->entry, i);
        for (byte = 0; byte < 4U; ++byte) {
            hash = (hash ^ (value & 0xffU)) * 16777619U;
            value >>= 8U;
        }
    }
    return hash;
}

onenet_model_status_t onenet_model_decode_set(const char *data, size_t length,
    const alarm_config_t *current, onenet_set_request_t *request)
{
    json_parser_t parser;
    uint16_t params = 0, key, object, cursor, fields = 0;
    int index;
    char name[24];
    alarm_entry_t entry;
    onenet_model_status_t status;
    if (request == NULL) return ONENET_MODEL_INVALID_REQUEST;
    memset(request, 0, sizeof *request);
    if (current == NULL) return ONENET_MODEL_INVALID_REQUEST;
    status = envelope(&parser, data, length, request->id, &params);
    if (status != ONENET_MODEL_OK) return status;
    if (parser.tokens[params].kind != JSON_OBJECT) return ONENET_MODEL_INVALID_REQUEST;
    key = (uint16_t)(params + 1U);
    if (key == parser.tokens[params].next) return ONENET_MODEL_INVALID_REQUEST;
    object = (uint16_t)(key + 1U);
    if (parser.tokens[object].next != parser.tokens[params].next) return ONENET_MODEL_MULTIPLE_ALARMS;
    if (!token_ascii(&parser, key, name, sizeof name) || (index = alarm_index(name)) < 0) return ONENET_MODEL_UNKNOWN_PROPERTY;
    if (parser.tokens[object].kind != JSON_OBJECT) return ONENET_MODEL_INVALID_VALUE;
    entry = current->entries[index];
    for (cursor = (uint16_t)(object + 1U); cursor < parser.tokens[object].next;) {
        unsigned field;
        uint16_t value = (uint16_t)(cursor + 1U);
        uint32_t number;
        if (!token_ascii(&parser, cursor, name, sizeof name) || (field = field_index(name)) == 9U) return ONENET_MODEL_UNKNOWN_PROPERTY;
        if ((fields & (1U << field)) != 0U) return ONENET_MODEL_INVALID_REQUEST;
        if (field == 0U) {
            uint8_t kind = parser.tokens[value].kind;
            if (kind != JSON_TRUE && kind != JSON_FALSE) return ONENET_MODEL_INVALID_VALUE;
            number = kind == JSON_TRUE ? 1U : 0U;
        } else if (!token_uint(&parser, value, &number)) return ONENET_MODEL_INVALID_VALUE;
        if (!valid_field(field, number)) return ONENET_MODEL_INVALID_VALUE;
        assign_field(&entry, field, number);
        fields = (uint16_t)(fields | (1U << field));
        cursor = parser.tokens[value].next;
    }
    if (fields == 0U) return ONENET_MODEL_INVALID_REQUEST;
    if (!valid_entry(&entry)) return ONENET_MODEL_INVALID_VALUE;
    request->index = (uint8_t)index;
    request->entry = entry;
    request->fields_mask = fields;
    request->fingerprint = patch_fingerprint(request);
    return ONENET_MODEL_OK;
}

onenet_model_status_t onenet_model_decode_get(const char *data, size_t length, onenet_get_request_t *request)
{
    json_parser_t parser;
    uint16_t params = 0, cursor, mask = 0;
    char name[24];
    onenet_model_status_t status;
    if (request == NULL) return ONENET_MODEL_INVALID_REQUEST;
    memset(request, 0, sizeof *request);
    status = envelope(&parser, data, length, request->id, &params);
    if (status != ONENET_MODEL_OK) return status;
    if (parser.tokens[params].kind != JSON_ARRAY) return ONENET_MODEL_INVALID_REQUEST;
    for (cursor = (uint16_t)(params + 1U); cursor < parser.tokens[params].next; cursor = parser.tokens[cursor].next) {
        uint16_t bit;
        if (!token_ascii(&parser, cursor, name, sizeof name) || (bit = property_bit(name)) == 0U) return ONENET_MODEL_UNKNOWN_PROPERTY;
        if ((mask & bit) != 0U) return ONENET_MODEL_INVALID_REQUEST;
        mask = (uint16_t)(mask | bit);
    }
    if (mask == 0U) return ONENET_MODEL_INVALID_REQUEST;
    request->property_mask = mask;
    return ONENET_MODEL_OK;
}
bool onenet_model_same_patch(const onenet_set_request_t *left, const onenet_set_request_t *right)
{
    unsigned i;
    if (left == NULL || right == NULL || left->index != right->index || left->fields_mask != right->fields_mask) return false;
    for (i = 0; i < 9U; ++i) if ((left->fields_mask & (1U << i)) != 0U && entry_field(&left->entry, i) != entry_field(&right->entry, i)) return false;
    return true;
}

onenet_model_status_t onenet_model_decode_reply(const char *data, size_t length,
    char id[ONENET_MODEL_ID_SIZE], unsigned *code)
{
    json_parser_t parser;
    uint16_t cursor;
    uint32_t number = 0;
    unsigned seen = 0;
    bool invalid = false, ambiguous_id = false;
    char name[20], text[ONENET_MODEL_ID_SIZE];
    if (id == NULL || code == NULL) return ONENET_MODEL_INVALID_REQUEST;
    id[0] = '\0';
    *code = 0;
    if (data == NULL) return ONENET_MODEL_INVALID_REQUEST;
    if (length > ONENET_MODEL_MAX_PAYLOAD) return ONENET_MODEL_TOO_LARGE;
    memset(&parser, 0, sizeof parser);
    parser.data = data;
    parser.length = length;
    if (!parse_value(&parser, 0U)) return ONENET_MODEL_INVALID_JSON;
    whitespace(&parser);
    if (parser.position != length) return ONENET_MODEL_INVALID_JSON;
    if (parser.tokens[0].kind != JSON_OBJECT) return ONENET_MODEL_INVALID_REQUEST;
    for (cursor = 1U; cursor < parser.tokens[0].next;) {
        uint16_t value = (uint16_t)(cursor + 1U);
        unsigned bit = 0;
        if (!token_ascii(&parser, cursor, name, sizeof name)) invalid = true;
        else if (strcmp(name, "id") == 0) {
            bit = 1U;
            if ((seen & bit) != 0U) ambiguous_id = true;
            if (!token_ascii(&parser, value, text, sizeof text) || !valid_id(text)) {
                invalid = true;
                ambiguous_id = true;
            } else memcpy(id, text, strlen(text) + 1U);
        } else if (strcmp(name, "code") == 0) {
            bit = 2U;
            if (!token_uint(&parser, value, &number) || (uint32_t)(unsigned)number != number) invalid = true;
        } else if (strcmp(name, "msg") == 0) {
            bit = 4U;
            if (parser.tokens[value].kind != JSON_STRING) invalid = true;
        } else if (strcmp(name, "data") == 0) bit = 8U;
        else invalid = true;
        if (bit != 0U && (seen & bit) != 0U) invalid = true;
        seen |= bit;
        cursor = parser.tokens[value].next;
    }
    if (ambiguous_id) id[0] = '\0';
    if (invalid || (seen & 3U) != 3U || id[0] == '\0') return ONENET_MODEL_INVALID_REQUEST;
    *code = (unsigned)number;
    return ONENET_MODEL_OK;
}

static json_writer_t writer(char *out, size_t capacity)
{
    json_writer_t value;
    value.out = out;
    value.capacity = capacity > ONENET_MODEL_MAX_PAYLOAD + 1U ? ONENET_MODEL_MAX_PAYLOAD + 1U : capacity;
    value.used = 0;
    value.failed = out == NULL || capacity == 0U;
    if (!value.failed) out[0] = '\0';
    return value;
}
static void append(json_writer_t *w, const char *format, ...)
{
    int count;
    va_list args;
    if (w->failed) return;
    va_start(args, format);
    count = vsnprintf(w->out + w->used, w->capacity - w->used, format, args);
    va_end(args);
    if (count < 0 || (size_t)count >= w->capacity - w->used) w->failed = true;
    else w->used += (size_t)count;
}
static size_t finish(json_writer_t *w)
{
    if (w->failed) {
        if (w->out != NULL && w->capacity != 0U) w->out[0] = '\0';
        return 0;
    }
    return w->used;
}
static void quoted(json_writer_t *w, const char *text)
{
    const unsigned char *cursor = (const unsigned char *)text;
    append(w, "\"");
    while (*cursor != 0U && !w->failed) {
        unsigned char c = *cursor++;
        if (c == '"' || c == '\\') append(w, "\\%c", (int)c);
        else if (c == '\n') append(w, "\\n");
        else if (c == '\r') append(w, "\\r");
        else if (c == '\t') append(w, "\\t");
        else if (c < 0x20U) append(w, "\\u%04x", (unsigned)c);
        else append(w, "%c", (int)c);
    }
    append(w, "\"");
}
static void alarm_value(json_writer_t *w, const alarm_entry_t *entry)
{
    unsigned i;
    append(w, "{\"enabled\":%s", entry->enabled != 0U ? "true" : "false");
    for (i = 1U; i < 9U; ++i) append(w, ",\"%s\":%lu", field_names[i], (unsigned long)entry_field(entry, i));
    append(w, "}");
}
static void properties(json_writer_t *w, const onenet_model_values_t *values, uint16_t mask, bool wrappers)
{
    unsigned i;
    bool comma = false;
    for (i = 0; i < 13U; ++i) {
        if ((mask & (1U << i)) == 0U) continue;
        if (comma) append(w, ",");
        comma = true;
        if (i < 8U) append(w, "\"alarm%u\":", i + 1U);
        else append(w, "\"%s\":", telemetry_names[i - 8U]);
        if (wrappers) append(w, "{\"value\":");
        if (i < 8U) alarm_value(w, &values->config->entries[i]);
        else if (i == 8U) append(w, "%.2f", (double)values->temperature);
        else if (i == 9U) append(w, "%.2f", (double)values->humidity);
        else if (i == 10U) append(w, "%s", values->sensor_valid ? "true" : "false");
        else if (i == 11U) append(w, "%lu", (unsigned long)values->config_revision);
        else append(w, "%u", (unsigned)values->ringing_mask);
        if (wrappers) append(w, "}");
    }
}
static bool valid_values(const onenet_model_values_t *values, uint16_t mask)
{
    return values != NULL && mask != 0U && (mask & (uint16_t)~ONENET_PROPERTY_ALL) == 0U
        && ((mask & ONENET_PROPERTY_ALARMS) == 0U || values->config != NULL)
        && ((mask & ONENET_PROPERTY_TEMPERATURE) == 0U || isfinite(values->temperature))
        && ((mask & ONENET_PROPERTY_HUMIDITY) == 0U || isfinite(values->humidity));
}
size_t onenet_model_encode_report(const char *id, const onenet_model_values_t *values,
    uint16_t mask, char *out, size_t capacity)
{
    json_writer_t w = writer(out, capacity);
    if (!valid_id(id) || !valid_values(values, mask)) w.failed = true;
    if (!w.failed) {
        append(&w, "{\"id\":\"%s\",\"version\":\"1.0\",\"params\":{", id);
        properties(&w, values, mask, true);
        append(&w, "}}");
    }
    return finish(&w);
}
size_t onenet_model_encode_get_reply(const char *id, const onenet_model_values_t *values,
    uint16_t mask, char *out, size_t capacity)
{
    json_writer_t w = writer(out, capacity);
    if (!valid_id(id) || !valid_values(values, mask)) w.failed = true;
    if (!w.failed) {
        append(&w, "{\"id\":\"%s\",\"code\":200,\"msg\":\"success\",\"data\":{", id);
        properties(&w, values, mask, false);
        append(&w, "}}");
    }
    return finish(&w);
}
size_t onenet_model_encode_reply(const char *id, unsigned code, const char *message, char *out, size_t capacity)
{
    json_writer_t w = writer(out, capacity);
    if (!valid_id(id) || message == NULL || code > 999U || code < 100U) w.failed = true;
    if (!w.failed) {
        append(&w, "{\"id\":\"%s\",\"code\":%u,\"msg\":", id, code);
        quoted(&w, message);
        append(&w, "}");
    }
    return finish(&w);
}
