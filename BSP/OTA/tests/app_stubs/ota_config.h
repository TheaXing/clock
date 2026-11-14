#ifndef OTA_APP_TEST_CONFIG_H
#define OTA_APP_TEST_CONFIG_H
#ifndef CLOCK_OTA_ENABLED
#define CLOCK_OTA_ENABLED 1
#endif
#ifndef CLOCK_OTA_ADMIN_TOKEN_SHA256_HEX
#ifdef OTA_TEST_NO_ADMIN
#define CLOCK_OTA_ADMIN_TOKEN_SHA256_HEX ""
#else
#define CLOCK_OTA_ADMIN_TOKEN_SHA256_HEX "5fe74bd8d11233a415702e9f19decc6ddc63a01f053eaeee3e1a5ac17e039d3d"
#endif
#endif
#define CLOCK_OTA_VERSION 17U
#ifndef CLOCK_OTA_AUTO_CHECK
#define CLOCK_OTA_AUTO_CHECK CLOCK_OTA_ENABLED
#endif
#if defined(OTA_TEST_LONG_URL)
#define CLOCK_OTA_HOST "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#if defined(OTA_TEST_URL_ACCEPTED)
#define CLOCK_OTA_PATH "/bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
#else
#define CLOCK_OTA_PATH "/bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
#endif
#else
#define CLOCK_OTA_HOST "192.0.2.1"
#define CLOCK_OTA_PATH "/firmware.ota"
#endif
#define CLOCK_OTA_PORT 8788U
#endif
