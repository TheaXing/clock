#include "ota_discovery.h"
#include <string.h>

/** 功能：跳过 JSON 的四种空白；参数：position，游标；返回：无。 */
static void skip_space(const char **position) {
    while (**position == ' ' || **position == '\r' || **position == '\n' || **position == '\t') {
        ++*position;
    }
}

/** 功能：读取没有转义的固定模式字符串；参数：游标、输出和容量；返回：是否合法。 */
static bool read_string(const char **position, char *out, size_t capacity) {
    size_t length = 0U;
    if (**position != '"') {
        return false;
    }
    ++*position;
    while (**position != '"') {
        unsigned char value = (unsigned char)**position;
        if (value < 32U || value == '\\' || value > 126U || length + 1U >= capacity) {
            return false;
        }
        out[length++] = **position;
        ++*position;
    }
    ++*position;
    out[length] = '\0';
    return true;
}

/** 功能：解码固定长度的小写十六进制；参数：文本、输出和字节数；返回：是否合法。 */
static bool read_hex(const char *text, uint8_t *out, size_t length) {
    if (strlen(text) != length * 2U) {
        return false;
    }
    for (size_t i = 0U; i < length * 2U; ++i) {
        unsigned char c = (unsigned char)text[i];
        unsigned digit;
        if (c >= '0' && c <= '9') {
            digit = (unsigned)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = (unsigned)(c - 'a' + 10U);
        } else {
            return false;
        }
        if ((i & 1U) == 0U) {
            out[i / 2U] = (uint8_t)(digit << 4U);
        } else {
            out[i / 2U] |= (uint8_t)digit;
        }
    }
    return true;
}

/** 功能：读取严格正十进制 u32；参数：游标和输出；返回：是否有效。 */
static bool read_version(const char **position, uint32_t *out) {
    uint32_t value = 0U;
    if (**position < '1' || **position > '9') {
        return false;
    }
    while (**position >= '0' && **position <= '9') {
        uint32_t digit = (uint32_t)(**position - '0');
        if (value > (UINT32_MAX - digit) / 10U) {
            return false;
        }
        value = value * 10U + digit;
        ++*position;
    }
    *out = value;
    return true;
}

/** 功能：严格解析不超过 1KiB 的公开发现提示，不执行发布者认证；参数：json、out，正文和输出；返回：完整模式及身份一致时为真，失败不改输出。 */
bool ota_discovery_parse(const char *json, ota_hint_t *out) {
    ota_hint_t hint = {0};
    uint8_t header[256];
    uint8_t identity[16];
    uint32_t version = 0U;
    unsigned fields = 0U;
    size_t length = 0U;
    const char *position = json;
    if (json == NULL || out == NULL) {
        return false;
    }
    while (length <= OTA_DISCOVERY_MAX_JSON && json[length] != '\0') {
        ++length;
    }
    if (length > OTA_DISCOVERY_MAX_JSON) {
        return false;
    }
    skip_space(&position);
    if (*position++ != '{') {
        return false;
    }
    for (;;) {
        char name[16];
        unsigned bit;
        skip_space(&position);
        if (!read_string(&position, name, sizeof name)) {
            return false;
        }
        skip_space(&position);
        if (*position++ != ':') {
            return false;
        }
        skip_space(&position);
        if (strcmp(name, "available") == 0) {
            bit = 1U;
            if (strncmp(position, "true", 4U) == 0) {
                hint.available = true;
                position += 4U;
            } else if (strncmp(position, "false", 5U) == 0) {
                position += 5U;
            } else {
                return false;
            }
        } else if (strcmp(name, "version") == 0) {
            bit = 2U;
            if (!read_version(&position, &version)) {
                return false;
            }
        } else if (strcmp(name, "packageId") == 0 || strcmp(name, "header") == 0) {
            char encoded[513];
            bit = strcmp(name, "packageId") == 0 ? 4U : 8U;
            if (!read_string(&position, encoded, sizeof encoded) ||
                !read_hex(encoded, bit == 4U ? identity : header, bit == 4U ? 16U : 256U)) {
                return false;
            }
        } else {
            return false;
        }
        if ((fields & bit) != 0U) {
            return false;
        }
        fields |= bit;
        skip_space(&position);
        if (*position == '}') {
            ++position;
            break;
        }
        if (*position++ != ',') {
            return false;
        }
    }
    skip_space(&position);
    if (*position != '\0') {
        return false;
    }
    if (hint.available) {
        if (fields != 15U || !ota_manifest_read_hint(header, &hint.manifest) ||
            hint.manifest.version != version || memcmp(hint.manifest.id, identity, 16U) != 0) {
            return false;
        }
    } else if (fields != 1U) {
        return false;
    }
    *out = hint;
    return true;
}
