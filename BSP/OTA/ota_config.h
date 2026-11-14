#ifndef OTA_CONFIG_H
#define OTA_CONFIG_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#ifndef CLOCK_OTA_ENABLED
#define CLOCK_OTA_ENABLED 0
#endif
#if CLOCK_OTA_ENABLED
#if defined(__ICCARM__)
#error "OTA targets require the verified GCC/CMake linker and Bootloader build"
#endif
#ifdef CLOCK_OTA_CONFIG_HEADER
#include CLOCK_OTA_CONFIG_HEADER
#else
#include "ota_config_local.h"
#endif
#ifndef CLOCK_OTA_ADMIN_TOKEN_HEX
#define CLOCK_OTA_ADMIN_TOKEN_HEX ""
#endif
#ifndef CLOCK_OTA_ADMIN_TOKEN_SHA256_HEX
#define CLOCK_OTA_ADMIN_TOKEN_SHA256_HEX ""
#endif
#else
#define CLOCK_OTA_KEY_HEX ""
#define CLOCK_OTA_ADMIN_TOKEN_HEX ""
#define CLOCK_OTA_ADMIN_TOKEN_SHA256_HEX ""
#define CLOCK_OTA_HOST ""
#define CLOCK_OTA_PORT 0U
#define CLOCK_OTA_PATH ""
#define CLOCK_OTA_VERSION 0U
#endif
#ifndef CLOCK_OTA_AUTO_CHECK
#define CLOCK_OTA_AUTO_CHECK CLOCK_OTA_ENABLED
#endif
#if !CLOCK_OTA_ENABLED && CLOCK_OTA_AUTO_CHECK
#error "Automatic OTA requires an OTA application build"
#endif

/** 功能：解码非零32字节密钥；参数：十六进制文本和输出；返回：有效为真。 */
static inline bool ota_config_decode_hex(const char *hex, uint8_t out[32]) {
    uint8_t nonzero = 0U;
    if (hex == NULL || out == NULL || strlen(hex) != 64U) {
        return false;
    }
    for (size_t i = 0U; i < 32U; ++i) {
        unsigned value = 0U;
        for (size_t j = 0U; j < 2U; ++j) {
            unsigned char c = (unsigned char)hex[i * 2U + j];
            unsigned digit;
            if (c >= '0' && c <= '9') {
                digit = (unsigned)(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                digit = (unsigned)(c - 'a' + 10U);
            } else if (c >= 'A' && c <= 'F') {
                digit = (unsigned)(c - 'A' + 10U);
            } else {
                return false;
            }
            value = value * 16U + digit;
        }
        out[i] = (uint8_t)value;
        nonzero |= out[i];
    }
    return nonzero != 0U;
}
#endif
