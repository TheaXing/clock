#ifndef OTA_CRYPTO_H
#define OTA_CRYPTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** 保存流式 SHA-256 的中间状态。 */
typedef struct {
    uint32_t state[8]; // 八个摘要工作字
    uint64_t total; // 已输入的字节总数
    uint8_t block[64]; // 尚未压缩的数据块
    size_t used; // 数据块中有效字节数
} ota_sha256_t;

void ota_sha256_init(ota_sha256_t *context);
void ota_sha256_update(ota_sha256_t *context, const void *data, size_t length);
void ota_sha256_final(ota_sha256_t *context, uint8_t digest[32]);
void ota_sha256(const void *data, size_t length, uint8_t digest[32]);
void ota_hmac_sha256(const uint8_t key[32], const void *data, size_t length, uint8_t digest[32]);
bool ota_constant_equal(const void *left, const void *right, size_t length);

#endif
