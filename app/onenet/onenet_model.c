#include "onenet_model.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define JSON_TOKEN_LIMIT 96U
#define JSON_DEPTH_LIMIT 8U

typedef enum {
    JSON_OBJECT,
    JSON_ARRAY,
    JSON_STRING,
    JSON_NUMBER,
    JSON_TRUE,
    JSON_FALSE,
    JSON_NULL
} json_kind_t;
/** 记录一个 JSON 值的类型、输入范围和后继令牌位置。 */
typedef struct {
    uint16_t start;  // 内容在输入字节中的起始偏移
    uint16_t length; // 内容字节数，字符串不含外侧引号
    uint16_t next;   // 当前值及其子项之后的首个令牌索引
    uint8_t kind;    // JSON 值类型
} json_token_t;
/** 保存有界 JSON 输入和递归解析产生的令牌。 */
typedef struct {
    const char *data;                      // 只读输入字节，无需空字符结尾
    size_t length;                         // 输入总字节数
    size_t position;                       // 下一次读取的输入字节偏移
    uint16_t count;                        // 已使用的令牌数量
    json_token_t tokens[JSON_TOKEN_LIMIT]; // 固定容量的令牌数组
} json_parser_t;

/** 记录有界 JSON 输出缓冲区及持续失败状态。 */
typedef struct {
    char *out;       // 输出字节缓冲区
    size_t capacity; // 输出缓冲区容量，包含结束符空间
    size_t used;     // 已经写入的字节数，不含结束符
    bool failed;     // 编码或容量检查失败标志
} json_writer_t;

static const char *const field_names[] = {"enabled",     "hour",         "minute",
                                          "repeat_mask", "year",         "month",
                                          "day",         "ring_seconds", "snooze_seconds"};
static const char *const telemetry_names[] = {"temperature", "humidity", "sensor_valid",
                                              "config_revision", "ringing_mask"};

/**
 * 功能：判断字符是否为十进制数字。
 * 参数：c，待检查字符。
 * 返回：字符位于 0～9 时返回 true，否则返回 false。
 */
static bool decimal(char c) {
    return c >= '0' && c <= '9';
}
/**
 * 功能：跳过 JSON 输入中当前位置起的空白字符。
 * 参数：p，待推进的解析器。
 * 返回：无。
 */
static void whitespace(json_parser_t *p) {
    while (p->position < p->length) {
        char c = p->data[p->position];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
        ++p->position;
    }
}
/**
 * 功能：将单个十六进制字符转换为数值。
 * 参数：c，待转换字符。
 * 返回：有效字符返回 0～15，无效字符返回 -1。
 */
static int hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}
/**
 * 功能：从当前位置读取 JSON Unicode 转义的四个十六进制数字。
 * 参数：p，待推进的解析器；value，接收码元值。
 * 返回：四位数字有效时返回 true，输入不足或字符无效返回 false。
 */
static bool unicode_quad(json_parser_t *p, unsigned *value) {
    unsigned i, n = 0;
    if (p->length - p->position < 4U) {
        return false;
    }
    for (i = 0; i < 4U; ++i) {
        int h = hex_digit(p->data[p->position++]);
        if (h < 0) {
            return false;
        }
        n = n * 16U + (unsigned)h;
    }
    *value = n;
    return true;
}
/**
 * 功能：读取并校验 UTF-8 多字节字符的后续字节和码点范围。
 * 参数：p，首字节之后的解析器；first，已经读取的 UTF-8 首字节。
 * 返回：完整编码有效时返回 true，否则返回 false。
 */
static bool utf8_tail(json_parser_t *p, unsigned char first) {
    unsigned tails, code, minimum, i;
    if (first >= 0xc2U && first <= 0xdfU) {
        tails = 1U;
        code = first & 0x1fU;
        minimum = 0x80U;
    } else if (first >= 0xe0U && first <= 0xefU) {
        tails = 2U;
        code = first & 0x0fU;
        minimum = 0x800U;
    } else if (first >= 0xf0U && first <= 0xf4U) {
        tails = 3U;
        code = first & 7U;
        minimum = 0x10000U;
    } else {
        return false;
    }
    if (p->length - p->position < tails) {
        return false;
    }
    for (i = 0; i < tails; ++i) {
        unsigned char next = (unsigned char)p->data[p->position++];
        if (next < 0x80U || next > 0xbfU) {
            return false;
        }
        code = (code << 6U) | (next & 0x3fU);
    }
    return code >= minimum && code <= 0x10ffffU && !(code >= 0xd800U && code <= 0xdfffU);
}
/**
 * 功能：解析 JSON 字符串，校验控制字符、转义、代理项及 UTF-8 编码。
 * 参数：p，位于起始引号的解析器；token，接收字符串内容范围。
 * 返回：找到合法结束引号时返回 true，否则返回 false。
 */
static bool string_token(json_parser_t *p, json_token_t *token) {
    size_t start = ++p->position;
    token->start = (uint16_t)start;
    while (p->position < p->length) {
        unsigned char c = (unsigned char)p->data[p->position++];
        if (c == '"') {
            token->length = (uint16_t)(p->position - start - 1U);
            return true;
        }
        if (c < 0x20U) {
            return false;
        }
        if (c >= 0x80U) {
            if (!utf8_tail(p, c)) {
                return false;
            }
        } else if (c == '\\') {
            unsigned first, second;
            if (p->position == p->length) {
                return false;
            }
            c = (unsigned char)p->data[p->position++];
            if (c == 'u') {
                if (!unicode_quad(p, &first)) {
                    return false;
                }
                if (first >= 0xd800U && first <= 0xdbffU) {
                    if (p->length - p->position < 6U || p->data[p->position] != '\\' ||
                        p->data[p->position + 1U] != 'u') {
                        return false;
                    }
                    p->position += 2U;
                    if (!unicode_quad(p, &second) || second < 0xdc00U || second > 0xdfffU) {
                        return false;
                    }
                } else if (first >= 0xdc00U && first <= 0xdfffU) {
                    return false;
                }
            } else if (c != '"' && c != '\\' && c != '/' && c != 'b' && c != 'f' && c != 'n' &&
                       c != 'r' && c != 't') {
                return false;
            }
        }
    }
    return false;
}
/**
 * 功能：解析符合 JSON 语法的数字，包括小数和指数。
 * 参数：p，位于数字起点的解析器。
 * 返回：数字语法有效时返回 true，否则返回 false。
 */
static bool number_token(json_parser_t *p) {
    if (p->data[p->position] == '-') {
        if (++p->position == p->length) {
            return false;
        }
    }
    if (p->data[p->position] == '0') {
        ++p->position;
    } else {
        if (p->data[p->position] < '1' || p->data[p->position] > '9') {
            return false;
        }
        do {
            ++p->position;
        } while (p->position < p->length && decimal(p->data[p->position]));
    }
    if (p->position < p->length && p->data[p->position] == '.') {
        ++p->position;
        if (p->position == p->length || !decimal(p->data[p->position])) {
            return false;
        }
        do {
            ++p->position;
        } while (p->position < p->length && decimal(p->data[p->position]));
    }
    if (p->position < p->length && (p->data[p->position] == 'e' || p->data[p->position] == 'E')) {
        ++p->position;
        if (p->position < p->length &&
            (p->data[p->position] == '+' || p->data[p->position] == '-')) {
            ++p->position;
        }
        if (p->position == p->length || !decimal(p->data[p->position])) {
            return false;
        }
        do {
            ++p->position;
        } while (p->position < p->length && decimal(p->data[p->position]));
    }
    return true;
}
/**
 * 功能：递归解析一个 JSON 值并建立有界令牌树。
 * 参数：p，JSON 解析器；depth，当前嵌套深度。
 * 返回：值完整有效且未超出深度或令牌上限时返回 true，否则返回 false。
 */
static bool parse_value(json_parser_t *p, unsigned depth) {
    uint16_t index;
    json_token_t *token;
    char c;
    if (depth > JSON_DEPTH_LIMIT || p->count >= JSON_TOKEN_LIMIT) {
        return false;
    }
    whitespace(p);
    if (p->position == p->length) {
        return false;
    }
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
        if (p->position < p->length && p->data[p->position] == end) {
            ++p->position;
        } else {
            for (;;) {
                if (object) {
                    if (p->position == p->length || p->data[p->position] != '"' ||
                        !parse_value(p, depth + 1U)) {
                        return false;
                    }
                    whitespace(p);
                    if (p->position == p->length || p->data[p->position++] != ':') {
                        return false;
                    }
                }
                if (!parse_value(p, depth + 1U)) {
                    return false;
                }
                whitespace(p);
                if (p->position == p->length) {
                    return false;
                }
                c = p->data[p->position++];
                if (c == end) {
                    break;
                }
                if (c != ',') {
                    return false;
                }
                whitespace(p);
            }
        }
    } else if (c == '"') {
        token->kind = JSON_STRING;
        if (!string_token(p, token)) {
            return false;
        }
    } else if (c == '-' || decimal(c)) {
        token->kind = JSON_NUMBER;
        if (!number_token(p)) {
            return false;
        }
    } else {
        const char *literal;
        size_t amount;
        if (c == 't') {
            literal = "true";
            token->kind = JSON_TRUE;
        } else if (c == 'f') {
            literal = "false";
            token->kind = JSON_FALSE;
        } else if (c == 'n') {
            literal = "null";
            token->kind = JSON_NULL;
        } else {
            return false;
        }
        amount = strlen(literal);
        if (p->length - p->position < amount ||
            memcmp(p->data + p->position, literal, amount) != 0) {
            return false;
        }
        p->position += amount;
    }
    if (token->kind != JSON_STRING) {
        token->length = (uint16_t)(p->position - token->start);
    }
    token->next = p->count;
    return true;
}
/* 标识符限定为 ASCII；解码 JSON 转义，避免转义形式的重复名称绕过与原始拼写相同的校验。 */
/**
 * 功能：将字符串令牌及 JSON 转义解码为以空字符结尾的 ASCII 标识符。
 * 参数：p，解析器；index，字符串令牌索引；out，输出缓冲区；capacity，含结束符的容量。
 * 返回：内容为非零 ASCII 且空间足够时返回 true，否则返回 false。
 */
static bool token_ascii(const json_parser_t *p, uint16_t index, char *out, size_t capacity) {
    const json_token_t *token = &p->tokens[index];
    size_t position = token->start, end = position + token->length, used = 0;
    if (token->kind != JSON_STRING || capacity == 0U) {
        return false;
    }
    while (position < end) {
        unsigned c = (unsigned char)p->data[position++];
        if (c == '\\') {
            c = (unsigned char)p->data[position++];
            if (c == 'u') {
                unsigned i;
                c = 0;
                for (i = 0; i < 4U; ++i) {
                    c = c * 16U + (unsigned)hex_digit(p->data[position++]);
                }
            } else if (c == 'b') {
                c = '\b';
            } else if (c == 'f') {
                c = '\f';
            } else if (c == 'n') {
                c = '\n';
            } else if (c == 'r') {
                c = '\r';
            } else if (c == 't') {
                c = '\t';
            }
        }
        if (c == 0U || c > 127U || used + 1U >= capacity) {
            return false;
        }
        out[used++] = (char)c;
    }
    out[used] = '\0';
    return true;
}
/**
 * 功能：校验消息编号是否由 1～13 位十进制数字组成。
 * 参数：id，以空字符结尾的消息编号。
 * 返回：编号有效时返回 true，否则返回 false。
 */
static bool valid_id(const char *id) {
    size_t i;
    if (id == NULL) {
        return false;
    }
    for (i = 0; i < ONENET_MODEL_ID_SIZE; ++i) {
        if (id[i] == '\0') {
            return i > 0U;
        }
        if (i == ONENET_MODEL_ID_SIZE - 1U || !decimal(id[i])) {
            return false;
        }
    }
    return false;
}
/**
 * 功能：严格解析 OneJSON 请求封套，校验唯一编号、可选版本和参数字段。
 * 参数：p，接收解析结果；data，输入字节；length，输入长度；id，接收明确有效的消息编号；params，接收参数令牌索引。
 * 返回：封套解析及语义校验状态；编号无效或有歧义时输出空编号。
 */
static onenet_model_status_t envelope(json_parser_t *p, const char *data, size_t length,
                                      char id[ONENET_MODEL_ID_SIZE], uint16_t *params) {
    unsigned seen = 0;
    uint16_t i;
    bool invalid = false, ambiguous_id = false;
    char key[20], text[ONENET_MODEL_ID_SIZE];
    id[0] = '\0';
    if (data == NULL) {
        return ONENET_MODEL_INVALID_REQUEST;
    }
    if (length > ONENET_MODEL_MAX_PAYLOAD) {
        return ONENET_MODEL_TOO_LARGE;
    }
    memset(p, 0, sizeof *p);
    p->data = data;
    p->length = length;
    if (!parse_value(p, 0U)) {
        return ONENET_MODEL_INVALID_JSON;
    }
    whitespace(p);
    if (p->position != length) {
        return ONENET_MODEL_INVALID_JSON;
    }
    if (p->tokens[0].kind != JSON_OBJECT) {
        return ONENET_MODEL_INVALID_REQUEST;
    }
    for (i = 1U; i < p->tokens[0].next;) {
        uint16_t value = (uint16_t)(i + 1U);
        unsigned bit = 0;
        if (!token_ascii(p, i, key, sizeof key)) {
            invalid = true;
        } else if (strcmp(key, "id") == 0) {
            bit = 1U;
            if ((seen & bit) != 0U) {
                ambiguous_id = true;
            }
            if (!token_ascii(p, value, text, sizeof text) || !valid_id(text)) {
                invalid = true;
                ambiguous_id = true;
            } else {
                memcpy(id, text, strlen(text) + 1U);
            }
        } else if (strcmp(key, "version") == 0) {
            bit = 2U;
            if (!token_ascii(p, value, text, sizeof text) || strcmp(text, "1.0") != 0) {
                invalid = true;
            }
        } else if (strcmp(key, "params") == 0) {
            bit = 4U;
            *params = value;
        } else {
            invalid = true;
        }
        if (bit != 0U && (seen & bit) != 0U) {
            invalid = true;
        }
        seen |= bit;
        i = p->tokens[value].next;
    }
    if (ambiguous_id) {
        id[0] = '\0';
    }
    if (invalid || (seen & 5U) != 5U || id[0] == '\0') {
        return ONENET_MODEL_INVALID_REQUEST;
    }
    return ONENET_MODEL_OK;
}
/**
 * 功能：根据协议字段名查找闹钟补丁字段序号。
 * 参数：name，以空字符结尾的字段名。
 * 返回：已知字段返回 0～8，未知字段返回 9。
 */
static unsigned field_index(const char *name) {
    unsigned i;
    for (i = 0; i < 9U; ++i) {
        if (strcmp(name, field_names[i]) == 0) {
            return i;
        }
    }
    return 9U;
}
/**
 * 功能：解析 alarm1～alarm8 属性名称为本地闹钟编号。
 * 参数：name，以空字符结尾的属性名。
 * 返回：从零开始的闹钟编号；名称无效返回 -1。
 */
static int alarm_index(const char *name) {
    if (strlen(name) == 6U && memcmp(name, "alarm", 5U) == 0 && name[5] >= '1' && name[5] <= '8') {
        return name[5] - '1';
    }
    return -1;
}
/**
 * 功能：将闹钟或遥测属性名称转换为协议属性位掩码。
 * 参数：name，以空字符结尾的属性名。
 * 返回：已知属性对应的单个位；未知属性返回 0。
 */
static uint16_t property_bit(const char *name) {
    int index = alarm_index(name);
    unsigned i;
    if (index >= 0) {
        return ONENET_PROPERTY_ALARM((unsigned)index);
    }
    for (i = 0; i < 5U; ++i) {
        if (strcmp(name, telemetry_names[i]) == 0) {
            return (uint16_t)(1U << (i + 8U));
        }
    }
    return 0;
}
/**
 * 功能：将仅含十进制数字的 JSON 数字令牌转换为 32 位无符号整数。
 * 参数：p，解析器；index，令牌索引；value，接收整数。
 * 返回：令牌类型、数字格式和范围均有效时返回 true，否则返回 false。
 */
static bool token_uint(const json_parser_t *p, uint16_t index, uint32_t *value) {
    const json_token_t *token = &p->tokens[index];
    uint32_t n = 0;
    unsigned i;
    if (token->kind != JSON_NUMBER) {
        return false;
    }
    for (i = 0; i < token->length; ++i) {
        char c = p->data[token->start + i];
        unsigned digit;
        if (!decimal(c)) {
            return false;
        }
        digit = (unsigned)(c - '0');
        if (n > (UINT32_MAX - digit) / 10U) {
            return false;
        }
        n = n * 10U + digit;
    }
    *value = n;
    return true;
}
/**
 * 功能：按协议字段序号读取闹钟配置值。
 * 参数：entry，闹钟配置；field，取值为 0～8 的字段序号。
 * 返回：对应字段的无符号整数值。
 */
static uint32_t entry_field(const alarm_entry_t *entry, unsigned field) {
    switch (field) {
    case 0:
        return entry->enabled;
    case 1:
        return entry->hour;
    case 2:
        return entry->minute;
    case 3:
        return entry->repeat_mask;
    case 4:
        return entry->year;
    case 5:
        return entry->month;
    case 6:
        return entry->day;
    case 7:
        return entry->ring_seconds;
    default:
        return entry->snooze_seconds;
    }
}
/**
 * 功能：按协议字段序号将数值写入闹钟条目。
 * 参数：entry，目标闹钟；field，取值为 0～8 的字段序号；value，已经校验的字段值。
 * 返回：无。
 */
static void assign_field(alarm_entry_t *entry, unsigned field, uint32_t value) {
    switch (field) {
    case 0:
        entry->enabled = (uint8_t)value;
        break;
    case 1:
        entry->hour = (uint8_t)value;
        break;
    case 2:
        entry->minute = (uint8_t)value;
        break;
    case 3:
        entry->repeat_mask = (uint8_t)value;
        break;
    case 4:
        entry->year = (uint16_t)value;
        break;
    case 5:
        entry->month = (uint8_t)value;
        break;
    case 6:
        entry->day = (uint8_t)value;
        break;
    case 7:
        entry->ring_seconds = (uint16_t)value;
        break;
    default:
        entry->snooze_seconds = (uint16_t)value;
        break;
    }
}
/**
 * 功能：校验云端闹钟字段的取值范围及贪睡整分钟约束。
 * 参数：field，取值为 0～8 的字段序号；value，待校验的值。
 * 返回：字段值符合协议要求时返回 true，否则返回 false。
 */
static bool valid_field(unsigned field, uint32_t value) {
    static const unsigned minimum[] = {0, 0, 0, 0, 2020, 1, 1, 5, 60};
    static const unsigned maximum[] = {1, 23, 59, 127, 2099, 12, 31, 600, 1800};
    return value >= minimum[field] && value <= maximum[field] && (field != 8U || value % 60U == 0U);
}
/**
 * 功能：校验完整云端闹钟条目的字段范围及实际日历日期。
 * 参数：entry，待校验的闹钟条目。
 * 返回：全部字段和日期有效时返回 true，否则返回 false。
 */
static bool valid_entry(const alarm_entry_t *entry) {
    static const uint8_t month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    unsigned i, days;
    for (i = 0; i < 9U; ++i) {
        if (!valid_field(i, entry_field(entry, i))) {
            return false;
        }
    }
    days = month_days[entry->month - 1U];
    if (entry->month == 2U && entry->year % 4U == 0U) {
        ++days;
    }
    return entry->day <= days;
}
/**
 * 功能：仅根据闹钟编号、字段掩码和显式字段计算规范化补丁指纹。
 * 参数：request，待计算的闹钟补丁。
 * 返回：用于去重辅助比较的指纹，不具备凭据或安全令牌用途。
 */
static uint32_t patch_fingerprint(const onenet_set_request_t *request) {
    uint32_t hash = 2166136261U;
    unsigned i, byte;
    hash = (hash ^ request->index) * 16777619U;
    hash = (hash ^ (request->fields_mask & 0xffU)) * 16777619U;
    hash = (hash ^ (request->fields_mask >> 8U)) * 16777619U;
    for (i = 0; i < 9U; ++i) {
        if ((request->fields_mask & (1U << i)) != 0U) {
            uint32_t value = entry_field(&request->entry, i);
            for (byte = 0; byte < 4U; ++byte) {
                hash = (hash ^ (value & 0xffU)) * 16777619U;
                value >>= 8U;
            }
        }
    }
    return hash;
}

/**
 * 功能：无动态分配地解析单个闹钟补丁，与当前快照合并后校验完整条目。
 * 参数：data，输入字节，无需空字符结尾；length，输入长度；current，当前完整配置；request，接收编号、合并条目、字段掩码及指纹。
 * 返回：解析和语义校验状态；语义失败时仍保留明确有效的编号。
 */
onenet_model_status_t onenet_model_decode_set(const char *data, size_t length,
                                              const alarm_config_t *current,
                                              onenet_set_request_t *request) {
    json_parser_t parser;
    uint16_t params = 0, key, object, cursor, fields = 0;
    int index;
    char name[24];
    alarm_entry_t entry;
    onenet_model_status_t status;
    if (request == NULL) {
        return ONENET_MODEL_INVALID_REQUEST;
    }
    memset(request, 0, sizeof *request);
    if (current == NULL) {
        return ONENET_MODEL_INVALID_REQUEST;
    }
    status = envelope(&parser, data, length, request->id, &params);
    if (status != ONENET_MODEL_OK) {
        return status;
    }
    if (parser.tokens[params].kind != JSON_OBJECT) {
        return ONENET_MODEL_INVALID_REQUEST;
    }
    key = (uint16_t)(params + 1U);
    if (key == parser.tokens[params].next) {
        return ONENET_MODEL_INVALID_REQUEST;
    }
    object = (uint16_t)(key + 1U);
    if (parser.tokens[object].next != parser.tokens[params].next) {
        return ONENET_MODEL_MULTIPLE_ALARMS;
    }
    if (!token_ascii(&parser, key, name, sizeof name) || (index = alarm_index(name)) < 0) {
        return ONENET_MODEL_UNKNOWN_PROPERTY;
    }
    if (parser.tokens[object].kind != JSON_OBJECT) {
        return ONENET_MODEL_INVALID_VALUE;
    }
    entry = current->entries[index];
    for (cursor = (uint16_t)(object + 1U); cursor < parser.tokens[object].next;) {
        unsigned field;
        uint16_t value = (uint16_t)(cursor + 1U);
        uint32_t number;
        if (!token_ascii(&parser, cursor, name, sizeof name) || (field = field_index(name)) == 9U) {
            return ONENET_MODEL_UNKNOWN_PROPERTY;
        }
        if ((fields & (1U << field)) != 0U) {
            return ONENET_MODEL_INVALID_REQUEST;
        }
        if (field == 0U) {
            uint8_t kind = parser.tokens[value].kind;
            if (kind != JSON_TRUE && kind != JSON_FALSE) {
                return ONENET_MODEL_INVALID_VALUE;
            }
            number = kind == JSON_TRUE ? 1U : 0U;
        } else if (!token_uint(&parser, value, &number)) {
            return ONENET_MODEL_INVALID_VALUE;
        }
        if (!valid_field(field, number)) {
            return ONENET_MODEL_INVALID_VALUE;
        }
        assign_field(&entry, field, number);
        fields = (uint16_t)(fields | (1U << field));
        cursor = parser.tokens[value].next;
    }
    if (fields == 0U) {
        return ONENET_MODEL_INVALID_REQUEST;
    }
    if (!valid_entry(&entry)) {
        return ONENET_MODEL_INVALID_VALUE;
    }
    request->index = (uint8_t)index;
    request->entry = entry;
    request->fields_mask = fields;
    request->fingerprint = patch_fingerprint(request);
    return ONENET_MODEL_OK;
}

/**
 * 功能：无动态分配地解析属性读取请求，拒绝未知或重复属性。
 * 参数：data，输入字节，无需空字符结尾；length，输入长度；request，接收编号和属性掩码。
 * 返回：解析和语义校验状态。
 */
onenet_model_status_t onenet_model_decode_get(const char *data, size_t length,
                                              onenet_get_request_t *request) {
    json_parser_t parser;
    uint16_t params = 0, cursor, mask = 0;
    char name[24];
    onenet_model_status_t status;
    if (request == NULL) {
        return ONENET_MODEL_INVALID_REQUEST;
    }
    memset(request, 0, sizeof *request);
    status = envelope(&parser, data, length, request->id, &params);
    if (status != ONENET_MODEL_OK) {
        return status;
    }
    if (parser.tokens[params].kind != JSON_ARRAY) {
        return ONENET_MODEL_INVALID_REQUEST;
    }
    for (cursor = (uint16_t)(params + 1U); cursor < parser.tokens[params].next;
         cursor = parser.tokens[cursor].next) {
        uint16_t bit;
        if (!token_ascii(&parser, cursor, name, sizeof name) || (bit = property_bit(name)) == 0U) {
            return ONENET_MODEL_UNKNOWN_PROPERTY;
        }
        if ((mask & bit) != 0U) {
            return ONENET_MODEL_INVALID_REQUEST;
        }
        mask = (uint16_t)(mask | bit);
    }
    if (mask == 0U) {
        return ONENET_MODEL_INVALID_REQUEST;
    }
    request->property_mask = mask;
    return ONENET_MODEL_OK;
}
/**
 * 功能：比较两个补丁的目标、字段掩码和显式字段值，忽略省略字段。
 * 参数：left，左侧补丁；right，右侧补丁。
 * 返回：两个规范化补丁相同时返回 true，否则返回 false。
 */
bool onenet_model_same_patch(const onenet_set_request_t *left, const onenet_set_request_t *right) {
    unsigned i;
    if (left == NULL || right == NULL || left->index != right->index ||
        left->fields_mask != right->fields_mask) {
        return false;
    }
    for (i = 0; i < 9U; ++i) {
        if ((left->fields_mask & (1U << i)) != 0U &&
            entry_field(&left->entry, i) != entry_field(&right->entry, i)) {
            return false;
        }
    }
    return true;
}

/**
 * 功能：严格解析平台应答的消息编号和状态码，不记录不可信消息文本。
 * 参数：data，输入字节，无需空字符结尾；length，输入长度；id，接收明确有效的编号；code，接收平台状态码。
 * 返回：解析和语义校验状态；无效或歧义编号保持为空。
 */
onenet_model_status_t onenet_model_decode_reply(const char *data, size_t length,
                                                char id[ONENET_MODEL_ID_SIZE], unsigned *code) {
    json_parser_t parser;
    uint16_t cursor;
    uint32_t number = 0;
    unsigned seen = 0;
    bool invalid = false, ambiguous_id = false;
    char name[20], text[ONENET_MODEL_ID_SIZE];
    if (id == NULL || code == NULL) {
        return ONENET_MODEL_INVALID_REQUEST;
    }
    id[0] = '\0';
    *code = 0;
    if (data == NULL) {
        return ONENET_MODEL_INVALID_REQUEST;
    }
    if (length > ONENET_MODEL_MAX_PAYLOAD) {
        return ONENET_MODEL_TOO_LARGE;
    }
    memset(&parser, 0, sizeof parser);
    parser.data = data;
    parser.length = length;
    if (!parse_value(&parser, 0U)) {
        return ONENET_MODEL_INVALID_JSON;
    }
    whitespace(&parser);
    if (parser.position != length) {
        return ONENET_MODEL_INVALID_JSON;
    }
    if (parser.tokens[0].kind != JSON_OBJECT) {
        return ONENET_MODEL_INVALID_REQUEST;
    }
    for (cursor = 1U; cursor < parser.tokens[0].next;) {
        uint16_t value = (uint16_t)(cursor + 1U);
        unsigned bit = 0;
        if (!token_ascii(&parser, cursor, name, sizeof name)) {
            invalid = true;
        } else if (strcmp(name, "id") == 0) {
            bit = 1U;
            if ((seen & bit) != 0U) {
                ambiguous_id = true;
            }
            if (!token_ascii(&parser, value, text, sizeof text) || !valid_id(text)) {
                invalid = true;
                ambiguous_id = true;
            } else {
                memcpy(id, text, strlen(text) + 1U);
            }
        } else if (strcmp(name, "code") == 0) {
            bit = 2U;
            if (!token_uint(&parser, value, &number) || (uint32_t)(unsigned)number != number) {
                invalid = true;
            }
        } else if (strcmp(name, "msg") == 0) {
            bit = 4U;
            if (parser.tokens[value].kind != JSON_STRING) {
                invalid = true;
            }
        } else if (strcmp(name, "data") == 0) {
            bit = 8U;
        } else {
            invalid = true;
        }
        if (bit != 0U && (seen & bit) != 0U) {
            invalid = true;
        }
        seen |= bit;
        cursor = parser.tokens[value].next;
    }
    if (ambiguous_id) {
        id[0] = '\0';
    }
    if (invalid || (seen & 3U) != 3U || id[0] == '\0') {
        return ONENET_MODEL_INVALID_REQUEST;
    }
    *code = (unsigned)number;
    return ONENET_MODEL_OK;
}

/**
 * 功能：创建受输出容量和协议载荷上限约束的 JSON 写入器。
 * 参数：out，输出缓冲区；capacity，包含结束符的缓冲区容量。
 * 返回：已初始化的写入器；参数无效时标记失败。
 */
static json_writer_t writer(char *out, size_t capacity) {
    json_writer_t value;
    value.out = out;
    value.capacity =
        capacity > ONENET_MODEL_MAX_PAYLOAD + 1U ? ONENET_MODEL_MAX_PAYLOAD + 1U : capacity;
    value.used = 0;
    value.failed = out == NULL || capacity == 0U;
    if (!value.failed) {
        out[0] = '\0';
    }
    return value;
}
/**
 * 功能：向 JSON 写入器追加格式化内容，失败后保持失败状态。
 * 参数：w，写入器；format，格式字符串；可变参数，待格式化的值。
 * 返回：无。
 */
static void append(json_writer_t *w, const char *format, ...) {
    int count;
    va_list args;
    if (w->failed) {
        return;
    }
    va_start(args, format);
    count = vsnprintf(w->out + w->used, w->capacity - w->used, format, args);
    va_end(args);
    if (count < 0 || (size_t)count >= w->capacity - w->used) {
        w->failed = true;
    } else {
        w->used += (size_t)count;
    }
}
/**
 * 功能：结束 JSON 编码，失败时清空可用输出。
 * 参数：w，待结束的写入器。
 * 返回：不含结束符的输出字节数；失败返回 0。
 */
static size_t finish(json_writer_t *w) {
    if (w->failed) {
        if (w->out != NULL && w->capacity != 0U) {
            w->out[0] = '\0';
        }
        return 0;
    }
    return w->used;
}
/**
 * 功能：输出带引号的 JSON 字符串并转义控制字符、引号和反斜杠。
 * 参数：w，写入器；text，以空字符结尾的输入字符串。
 * 返回：无。
 */
static void quoted(json_writer_t *w, const char *text) {
    const unsigned char *cursor = (const unsigned char *)text;
    append(w, "\"");
    while (*cursor != 0U && !w->failed) {
        unsigned char c = *cursor++;
        if (c == '"' || c == '\\') {
            append(w, "\\%c", (int)c);
        } else if (c == '\n') {
            append(w, "\\n");
        } else if (c == '\r') {
            append(w, "\\r");
        } else if (c == '\t') {
            append(w, "\\t");
        } else if (c < 0x20U) {
            append(w, "\\u%04x", (unsigned)c);
        } else {
            append(w, "%c", (int)c);
        }
    }
    append(w, "\"");
}
/**
 * 功能：将完整闹钟条目编码为 JSON 属性值对象。
 * 参数：w，写入器；entry，待编码闹钟。
 * 返回：无。
 */
static void alarm_value(json_writer_t *w, const alarm_entry_t *entry) {
    unsigned i;
    append(w, "{\"enabled\":%s", entry->enabled != 0U ? "true" : "false");
    for (i = 1U; i < 9U; ++i) {
        append(w, ",\"%s\":%lu", field_names[i], (unsigned long)entry_field(entry, i));
    }
    append(w, "}");
}
/**
 * 功能：按属性掩码编码闹钟和遥测值，并按需添加 OneJSON 值包装。
 * 参数：w，写入器；values，属性值来源；mask，待输出属性位掩码；wrappers，是否添加 value 包装。
 * 返回：无。
 */
static void properties(json_writer_t *w, const onenet_model_values_t *values, uint16_t mask,
                       bool wrappers) {
    unsigned i;
    bool comma = false;
    for (i = 0; i < 13U; ++i) {
        if ((mask & (1U << i)) == 0U) {
            continue;
        }
        if (comma) {
            append(w, ",");
        }
        comma = true;
        if (i < 8U) {
            append(w, "\"alarm%u\":", i + 1U);
        } else {
            append(w, "\"%s\":", telemetry_names[i - 8U]);
        }
        if (wrappers) {
            append(w, "{\"value\":");
        }
        if (i < 8U) {
            alarm_value(w, &values->config->entries[i]);
        } else if (i == 8U) {
            append(w, "%.2f", (double)values->temperature);
        } else if (i == 9U) {
            append(w, "%.2f", (double)values->humidity);
        } else if (i == 10U) {
            append(w, "%s", values->sensor_valid ? "true" : "false");
        } else if (i == 11U) {
            append(w, "%lu", (unsigned long)values->config_revision);
        } else {
            append(w, "%u", (unsigned)values->ringing_mask);
        }
        if (wrappers) {
            append(w, "}");
        }
    }
}
/**
 * 功能：检查属性掩码、配置指针以及将输出的温湿度是否有效。
 * 参数：values，待输出属性值；mask，待输出属性位掩码。
 * 返回：满足所选属性的编码条件时返回 true，否则返回 false。
 */
static bool valid_values(const onenet_model_values_t *values, uint16_t mask) {
    return values != NULL && mask != 0U && (mask & (uint16_t)~ONENET_PROPERTY_ALL) == 0U &&
           ((mask & ONENET_PROPERTY_ALARMS) == 0U || values->config != NULL) &&
           ((mask & ONENET_PROPERTY_TEMPERATURE) == 0U || isfinite(values->temperature)) &&
           ((mask & ONENET_PROPERTY_HUMIDITY) == 0U || isfinite(values->humidity));
}
/**
 * 功能：将选中属性编码为带 OneJSON 值包装的上报消息。
 * 参数：id，1～13 位十进制编号；values，属性值；mask，属性掩码；out，输出缓冲区；capacity，含结束符的容量。
 * 返回：不含结束符的字节数；失败返回 0 并清空可用输出。
 */
size_t onenet_model_encode_report(const char *id, const onenet_model_values_t *values,
                                  uint16_t mask, char *out, size_t capacity) {
    json_writer_t w = writer(out, capacity);
    if (!valid_id(id) || !valid_values(values, mask)) {
        w.failed = true;
    }
    if (!w.failed) {
        append(&w, "{\"id\":\"%s\",\"version\":\"1.0\",\"params\":{", id);
        properties(&w, values, mask, true);
        append(&w, "}}");
    }
    return finish(&w);
}
/**
 * 功能：将选中属性编码为包含原始属性值的成功读取应答。
 * 参数：id，1～13 位十进制编号；values，属性值；mask，属性掩码；out，输出缓冲区；capacity，含结束符的容量。
 * 返回：不含结束符的字节数；失败返回 0 并清空可用输出。
 */
size_t onenet_model_encode_get_reply(const char *id, const onenet_model_values_t *values,
                                     uint16_t mask, char *out, size_t capacity) {
    json_writer_t w = writer(out, capacity);
    if (!valid_id(id) || !valid_values(values, mask)) {
        w.failed = true;
    }
    if (!w.failed) {
        append(&w, "{\"id\":\"%s\",\"code\":200,\"msg\":\"success\",\"data\":{", id);
        properties(&w, values, mask, false);
        append(&w, "}}");
    }
    return finish(&w);
}
/**
 * 功能：将消息编号、状态码和经过转义的说明编码为通用应答。
 * 参数：id，1～13 位十进制编号；code，100～999 的状态码；message，应答说明；out，输出缓冲区；capacity，含结束符的容量。
 * 返回：不含结束符的字节数；失败返回 0 并清空可用输出。
 */
size_t onenet_model_encode_reply(const char *id, unsigned code, const char *message, char *out,
                                 size_t capacity) {
    json_writer_t w = writer(out, capacity);
    if (!valid_id(id) || message == NULL || code > 999U || code < 100U) {
        w.failed = true;
    }
    if (!w.failed) {
        append(&w, "{\"id\":\"%s\",\"code\":%u,\"msg\":", id, code);
        quoted(&w, message);
        append(&w, "}");
    }
    return finish(&w);
}
