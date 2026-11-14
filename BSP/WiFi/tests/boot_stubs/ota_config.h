#ifndef WIFI_TEST_OTA_CONFIG_H
#define WIFI_TEST_OTA_CONFIG_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define CLOCK_OTA_KEY_HEX "test-only-key"
#define CLOCK_OTA_HOST "example.test"
#define CLOCK_OTA_PORT 8788U
#define CLOCK_OTA_PATH "/firmware.ota"
/** 功能：隔离测试不相关的密钥配置；参数：hex，文本；out，输出；返回：成功。 */
static inline bool ota_config_decode_hex(const char *hex, uint8_t out[32]) {
    (void)hex;
    memset(out, 1, 32U);
    return true;
}
#endif
