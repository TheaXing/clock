#ifndef BOOT_TARGET_H
#define BOOT_TARGET_H
#include "ota_image.h"
#include "ota_journal.h"

/** 保存本次已选择的固件身份与固定主机下的路径。 */
typedef struct {
    ota_manifest_t expected; // 已认证的固定目标，别名选择时尚为空
    char path[193]; // 完整相对路径，包含固定身份查询
    bool pinned; // 是否必须匹配保存的包头
    bool automatic; // 是否为尚未开始安装的自动升级请求
} boot_target_t;

bool boot_target_select(ota_store_result_t status, const ota_record_t *record,
                        const ota_manifest_t *current, const uint8_t key[32],
                        bool forced, const char *base, boot_target_t *target);
bool boot_target_accept(const boot_target_t *target, const ota_manifest_t *candidate);
bool boot_target_pin(boot_target_t *target, const char *base, const ota_manifest_t *candidate);
#endif
