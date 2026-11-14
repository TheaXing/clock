#include "boot_target.h"
#include <string.h>

/** 功能：检查固定下载基础路径；参数：基础路径；返回：不含查询和目录跳转且可追加身份时为真。 */
static bool valid_base(const char *base) {
    if (base == NULL || base[0] != '/' || strlen(base) > 156U) {
        return false;
    }
    for (const char *p = base; *p != '\0'; ++p) {
        char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '/' || c == '_' || c == '-' || c == '.')) {
            return false;
        }
        if (c == '/' && (p[1] == '/' ||
            (p[1] == '.' && (p[2] == '/' || p[2] == '\0' ||
             (p[2] == '.' && (p[3] == '/' || p[3] == '\0')))))) {
            return false;
        }
    }
    return true;
}

/** 功能：识别尚未绑定包头的人工请求；参数：持久化包头；返回：全部字节为零时为真。 */
static bool empty_header(const uint8_t header[256]) {
    uint8_t combined = 0;
    for (unsigned i = 0; i < 256; ++i) {
        combined |= header[i];
    }
    return combined == 0;
}

/** 功能：选择并认证本次下载目标；参数：日志、已完整验证的当前镜像、密钥和路径；返回：可安全发起下载时为真。 */
bool boot_target_select(ota_store_result_t status, const ota_record_t *record,
                        const ota_manifest_t *current, const uint8_t key[32],
                        bool forced, const char *base, boot_target_t *target) {
    boot_target_t next;
    if (target == NULL || !valid_base(base) ||
        (status != OTA_STORE_EMPTY && status != OTA_STORE_OK) ||
        (status == OTA_STORE_OK && (record == NULL || (unsigned)record->state > OTA_STATE_FAILED))) {
        return false;
    }
    memset(&next, 0, sizeof next);
    memcpy(next.path, base, strlen(base) + 1U);
    if (!forced && status == OTA_STORE_OK &&
        (record->state == OTA_STATE_INSTALLING ||
         (record->state == OTA_STATE_REQUESTED && !empty_header(record->manifest)))) {
        if (key == NULL || !ota_manifest_parse(record->manifest, key, false, &next.expected)) {
            return false;
        }
        next.automatic = record->state == OTA_STATE_REQUESTED;
        if (next.automatic && current != NULL && next.expected.version <= current->version) {
            return false;
        }
        if (!boot_target_pin(&next, base, &next.expected)) {
            return false;
        }
    }
    *target = next;
    return true;
}

/** 功能：检查已认证候选是否匹配固定目标；参数：目标与候选；返回：别名或完整包头相同时为真。 */
bool boot_target_accept(const boot_target_t *target, const ota_manifest_t *candidate) {
    return target != NULL && candidate != NULL &&
        (!target->pinned || memcmp(target->expected.raw, candidate->raw, OTA_HEADER_SIZE) == 0);
}

/** 功能：将已认证候选绑定为不可变下载路径；参数：目标、基础路径和认证候选；返回：绑定成功为真。 */
bool boot_target_pin(boot_target_t *target, const char *base, const ota_manifest_t *candidate) {
    static const char hex[] = "0123456789abcdef";
    if (!valid_base(base) || !boot_target_accept(target, candidate)) {
        return false;
    }
    size_t size = strlen(base);
    memcpy(target->path, base, size);
    memcpy(target->path + size, "?id=", 4);
    for (unsigned i = 0; i < 16; ++i) {
        target->path[size + 4U + i * 2U] = hex[candidate->id[i] >> 4];
        target->path[size + 5U + i * 2U] = hex[candidate->id[i] & 15U];
    }
    target->path[size + 36U] = '\0';
    target->expected = *candidate;
    target->pinned = true;
    return true;
}
