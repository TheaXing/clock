#ifndef OTA_DISCOVERY_H
#define OTA_DISCOVERY_H
#include "ota_image.h"
#define OTA_DISCOVERY_MAX_JSON 1024U

/** 保存后台公开且未经认证的候选提示，不授予任何安装权限。 */
typedef struct {
    bool available; // 后台是否有已发布包
    ota_manifest_t manifest; // 候选结构与公开身份，仅 available 为真时有效
} ota_hint_t;

bool ota_discovery_parse(const char *json, ota_hint_t *out);
#endif
