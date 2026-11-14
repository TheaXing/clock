#ifndef OTA_IMAGE_H
#define OTA_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OTA_APP_BASE 0x08010000u
#define OTA_FOOTER_BASE 0x0807FF00u
#define OTA_IMAGE_MAX 458496u
#define OTA_HEADER_SIZE 256u
#define OTA_FOOTER_COMMIT 0x3141544Fu

/** 保存包头及其解析后的镜像身份，发布者认证状态由调用接口保证。 */
typedef struct {
    uint32_t image_size; // 应用原始二进制长度
    uint32_t version; // 发布版本编号
    uint8_t digest[32]; // 应用内容摘要
    uint8_t id[16]; // 包头身份摘要前十六字节
    uint8_t raw[256]; // 原始固定长度包头
} ota_manifest_t;

bool ota_manifest_parse(const uint8_t header[256], const uint8_t key[32], bool installed, ota_manifest_t *out);
/** 仅供已经由 Boot 完整认证后运行的应用读取元数据；此接口不验证 HMAC，严禁用于 Boot 或安装授权。 */
bool ota_manifest_read_trusted(const uint8_t header[256], bool installed, ota_manifest_t *out);
/** 仅解析未认证网络提示的结构与公开身份，绝不能作为安装授权。 */
bool ota_manifest_read_hint(const uint8_t header[256], ota_manifest_t *out);
bool ota_image_vectors_valid(const uint8_t first[8], uint32_t image_size);
bool ota_image_verify(const ota_manifest_t *manifest, const uint8_t *image);

#endif
