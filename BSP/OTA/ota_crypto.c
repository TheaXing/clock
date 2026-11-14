#include "ota_crypto.h"
#include <string.h>

/** 功能：循环右移工作字。参数：工作字和位数。返回：旋转结果。 */
static uint32_t rotate_right(uint32_t value, unsigned shift) {
    return (value >> shift) | (value << (32u - shift));
}

/** 功能：压缩一个 SHA-256 数据块。参数：上下文和六十四字节块。返回：无。 */
static void compress_block(ota_sha256_t *context, const uint8_t block[64]) {
    static const uint32_t constants[64] = {
        0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u, 0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
        0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u, 0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u,
        0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu, 0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
        0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u, 0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
        0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u, 0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
        0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u, 0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
        0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u, 0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
        0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u, 0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u
    };
    uint32_t words[64];
    uint32_t a = context->state[0];
    uint32_t b = context->state[1];
    uint32_t c = context->state[2];
    uint32_t d = context->state[3];
    uint32_t e = context->state[4];
    uint32_t f = context->state[5];
    uint32_t g = context->state[6];
    uint32_t h = context->state[7];
    unsigned index;
    for (index = 0; index < 16; ++index) {
        const uint8_t *part = block + index * 4;
        words[index] = ((uint32_t)part[0] << 24) | ((uint32_t)part[1] << 16) | ((uint32_t)part[2] << 8) | part[3];
    }
    for (index = 16; index < 64; ++index) {
        uint32_t x = words[index - 15];
        uint32_t y = words[index - 2];
        uint32_t sigma0 = rotate_right(x, 7) ^ rotate_right(x, 18) ^ (x >> 3);
        uint32_t sigma1 = rotate_right(y, 17) ^ rotate_right(y, 19) ^ (y >> 10);
        words[index] = words[index - 16] + sigma0 + words[index - 7] + sigma1;
    }
    for (index = 0; index < 64; ++index) {
        uint32_t sum1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        uint32_t choose = (e & f) ^ (~e & g);
        uint32_t temp1 = h + sum1 + choose + constants[index] + words[index];
        uint32_t sum0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

/** 功能：初始化 SHA-256。参数：输出上下文。返回：无。 */
void ota_sha256_init(ota_sha256_t *context) {
    static const uint32_t initial[8] = {
        0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au,
        0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u
    };
    memcpy(context->state, initial, sizeof(initial));
    context->total = 0;
    context->used = 0;
}

/** 功能：向 SHA-256 添加任意分块。参数：上下文、数据和字节数。返回：无。 */
void ota_sha256_update(ota_sha256_t *context, const void *data, size_t length) {
    const uint8_t *bytes = data;
    context->total += length;
    while (length != 0) {
        size_t count = sizeof(context->block) - context->used;
        if (count > length) {
            count = length;
        }
        memcpy(context->block + context->used, bytes, count);
        context->used += count;
        bytes += count;
        length -= count;
        if (context->used == sizeof(context->block)) {
            compress_block(context, context->block);
            context->used = 0;
        }
    }
}

/** 功能：完成 SHA-256 并清除上下文。参数：上下文和三十二字节输出。返回：无。 */
void ota_sha256_final(ota_sha256_t *context, uint8_t digest[32]) {
    uint64_t bits = context->total * 8u;
    unsigned index;
    context->block[context->used++] = 0x80;
    if (context->used > 56) {
        memset(context->block + context->used, 0, 64 - context->used);
        compress_block(context, context->block);
        context->used = 0;
    }
    memset(context->block + context->used, 0, 56 - context->used);
    for (index = 0; index < 8; ++index) {
        context->block[63 - index] = (uint8_t)(bits >> (index * 8));
    }
    compress_block(context, context->block);
    for (index = 0; index < 32; ++index) {
        digest[index] = (uint8_t)(context->state[index / 4] >> (24u - (index % 4) * 8u));
    }
    memset(context, 0, sizeof(*context));
}

/** 功能：一次计算 SHA-256。参数：数据、字节数和输出摘要。返回：无。 */
void ota_sha256(const void *data, size_t length, uint8_t digest[32]) {
    ota_sha256_t context;
    ota_sha256_init(&context);
    ota_sha256_update(&context, data, length);
    ota_sha256_final(&context, digest);
}

/** 功能：使用固定三十二字节密钥计算 HMAC。参数：密钥、数据、长度和摘要输出。返回：无。 */
void ota_hmac_sha256(const uint8_t key[32], const void *data, size_t length, uint8_t digest[32]) {
    uint8_t pad[64];
    uint8_t inner[32];
    ota_sha256_t context;
    unsigned index;
    memset(pad, 0x36, sizeof(pad));
    for (index = 0; index < 32; ++index) {
        pad[index] ^= key[index];
    }
    ota_sha256_init(&context);
    ota_sha256_update(&context, pad, sizeof(pad));
    ota_sha256_update(&context, data, length);
    ota_sha256_final(&context, inner);
    memset(pad, 0x5C, sizeof(pad));
    for (index = 0; index < 32; ++index) {
        pad[index] ^= key[index];
    }
    ota_sha256_init(&context);
    ota_sha256_update(&context, pad, sizeof(pad));
    ota_sha256_update(&context, inner, sizeof(inner));
    ota_sha256_final(&context, digest);
    memset(pad, 0, sizeof(pad));
    memset(inner, 0, sizeof(inner));
}

/** 功能：以固定遍历长度比较数据。参数：两个缓冲区及长度。返回：相同时为真。 */
bool ota_constant_equal(const void *left, const void *right, size_t length) {
    const uint8_t *a = left;
    const uint8_t *b = right;
    volatile uint8_t difference = 0;
    size_t index;
    for (index = 0; index < length; ++index) {
        difference = (uint8_t)(difference | (a[index] ^ b[index]));
    }
    return difference == 0;
}
