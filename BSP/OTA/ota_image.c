#include "ota_image.h"
#include "ota_crypto.h"
#include <string.h>

/** 功能：读取明确的小端字段。参数：四字节地址。返回：整数值。 */
static uint32_t read_u32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/** 功能：检查一段保留区是否全零。参数：数据和长度。返回：全零时为真。 */
static bool all_zero(const uint8_t *data, size_t length) {
    size_t index;
    uint8_t value = 0;
    for (index = 0; index < length; ++index) {
        value |= data[index];
    }
    return value == 0;
}

/**
 * 功能：严格读取包头结构与身份元数据，不验证发布者 HMAC。
 * 参数：header 为完整包头；installed 选择安装描述标记；out 为输出。供显式可信读取、未认证提示与认证接口共用，不能单独授权安装。
 * 返回：结构、范围和身份字段合法时为真，失败不修改输出；成功不代表发布者认证通过。
 */
static bool read_structure(const uint8_t header[256], bool installed, ota_manifest_t *out) {
    static const uint8_t magic[8] = {'C', 'L', 'K', 'O', 'T', 'A', '1', 0};
    static const uint8_t board[32] = "Clock_CubeMX-STM32F407VE";
    uint8_t digest[32];
    uint32_t image_size;
    if (header == NULL || out == NULL) {
        return false;
    }
    image_size = read_u32(header + 20);
    if (memcmp(header, magic, sizeof(magic)) != 0 || read_u32(header + 8) != 1 || read_u32(header + 12) != 256 ||
        read_u32(header + 16) != OTA_APP_BASE || image_size < 0x188u || image_size > OTA_IMAGE_MAX ||
        read_u32(header + 24) == 0 || read_u32(header + 28) != 0 || memcmp(header + 32, board, sizeof(board)) != 0 ||
        !all_zero(header + 112, 80) || !all_zero(header + 224, 28) ||
        read_u32(header + 252) != (installed ? OTA_FOOTER_COMMIT : 0xFFFFFFFFu)) {
        return false;
    }
    ota_sha256(header, 96, digest);
    if (!ota_constant_equal(digest, header + 96, 16)) {
        return false;
    }
    out->image_size = image_size;
    out->version = read_u32(header + 24);
    memcpy(out->digest, header + 64, 32);
    memcpy(out->id, header + 96, 16);
    memcpy(out->raw, header, 256);
    return true;
}

/** 功能：读取 Boot 已认证的当前安装描述；参数：包头、安装标志和输出；返回：结构合法为真，不执行 HMAC。 */
bool ota_manifest_read_trusted(const uint8_t header[256], bool installed, ota_manifest_t *out) {
    return read_structure(header, installed, out);
}

/** 功能：读取来自网络的未认证提示；参数：原始线上包头和输出；返回：仅结构及公开身份合法为真，严禁据此授权安装或擦除。 */
bool ota_manifest_read_hint(const uint8_t header[256], ota_manifest_t *out) {
    return read_structure(header, false, out);
}
/** 功能：严格解析并认证线上包头或已提交安装描述。参数：包头、发布密钥、安装标志和输出。返回：结构和 HMAC 均验证成功时为真，失败不修改输出。 */
bool ota_manifest_parse(const uint8_t header[256], const uint8_t key[32], bool installed, ota_manifest_t *out) {
    ota_manifest_t parsed;
    uint8_t digest[32];
    if (key == NULL || out == NULL || all_zero(key, 32) || !read_structure(header, installed, &parsed)) {
        return false;
    }
    ota_hmac_sha256(key, header, 192, digest);
    if (!ota_constant_equal(digest, header + 192, 32)) {
        return false;
    }
    *out = parsed;
    return true;
}

/** 功能：验证应用初始栈与精确复位入口。参数：前八字节和真实镜像长度。返回：向量合法时为真。 */
bool ota_image_vectors_valid(const uint8_t first[8], uint32_t image_size) {
    uint32_t stack;
    uint32_t reset;
    uint32_t entry;
    if (first == NULL || image_size < 0x188u || image_size > OTA_IMAGE_MAX) {
        return false;
    }
    stack = read_u32(first);
    reset = read_u32(first + 4);
    entry = reset & ~1u;
    return stack > 0x20000000u && stack <= 0x20020000u && (stack & 7u) == 0 &&
        (reset & 1u) != 0 && entry >= OTA_APP_BASE + 0x188u && entry < OTA_APP_BASE + image_size;
}

/** 功能：验证整个内存镜像摘要及向量。参数：已经认证的描述和完整镜像地址。返回：所有验证通过时为真。 */
bool ota_image_verify(const ota_manifest_t *manifest, const uint8_t *image) {
    uint8_t digest[32];
    if (manifest == NULL || image == NULL || !ota_image_vectors_valid(image, manifest->image_size)) {
        return false;
    }
    ota_sha256(image, manifest->image_size, digest);
    return ota_constant_equal(digest, manifest->digest, 32);
}
