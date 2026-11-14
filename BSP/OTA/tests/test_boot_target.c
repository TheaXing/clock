#include "boot_target.h"
#include "ota_crypto.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t key[32];

/** 功能：写入小端测试字段；参数：四字节地址与数值；返回：无。 */
static void put32(uint8_t *out, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) {
        out[i] = (uint8_t)(n >> (i * 8U));
    }
}

/** 功能：生成完整认证测试候选；参数：版本与输出；返回：无。 */
static void candidate(uint32_t version, ota_manifest_t *manifest) {
    uint8_t header[256] = {0};
    uint8_t digest[32];
    memcpy(header, "CLKOTA1", 8);
    put32(header + 8, 1);
    put32(header + 12, 256);
    put32(header + 16, OTA_APP_BASE);
    put32(header + 20, 1024);
    put32(header + 24, version);
    memcpy(header + 32, "Clock_CubeMX-STM32F407VE", sizeof("Clock_CubeMX-STM32F407VE") - 1U);
    ota_sha256("published image", 15, header + 64);
    ota_sha256(header, 96, digest);
    memcpy(header + 96, digest, 16);
    ota_hmac_sha256(key, header, 192, header + 192);
    put32(header + 252, UINT32_MAX);
    assert(ota_manifest_parse(header, key, false, manifest));
}

/** 功能：验证人工、空白及强制恢复兼容；参数：无；返回：无。 */
static void test_alias_and_forced(void) {
    boot_target_t target;
    ota_record_t record = {0};
    record.state = OTA_STATE_REQUESTED;
    assert(boot_target_select(OTA_STORE_EMPTY, NULL, NULL, key, false, "/firmware.ota", &target));
    assert(!target.pinned && strcmp(target.path, "/firmware.ota") == 0);
    assert(boot_target_select(OTA_STORE_OK, &record, NULL, key, false, "/firmware.ota", &target));
    assert(!target.pinned && !target.automatic);
    record.manifest[0] = 1;
    assert(!boot_target_select(OTA_STORE_OK, &record, NULL, key, false, "/firmware.ota", &target));
    assert(boot_target_select(OTA_STORE_OK, &record, NULL, key, true, "/firmware.ota", &target));
    assert(!target.pinned);
    assert(!boot_target_select(OTA_STORE_IO, &record, NULL, key, true, "/firmware.ota", &target));
}

/** 功能：验证自动候选验签、严格增版和固定身份；参数：无；返回：无。 */
static void test_auto_target(void) {
    ota_manifest_t current;
    ota_manifest_t newer;
    ota_manifest_t changed;
    ota_record_t record = {0};
    boot_target_t target;
    candidate(1, &current);
    candidate(2, &newer);
    candidate(3, &changed);
    record.state = OTA_STATE_REQUESTED;
    memcpy(record.manifest, newer.raw, 256);
    assert(boot_target_select(OTA_STORE_OK, &record, &current, key, false, "/firmware.ota", &target));
    assert(target.pinned && target.automatic && strstr(target.path, "/firmware.ota?id=") == target.path);
    assert(boot_target_accept(&target, &newer));
    assert(!boot_target_accept(&target, &changed));
    assert(!boot_target_pin(&target, "/firmware.ota", &changed));
    assert(boot_target_pin(&target, "/firmware.ota", &newer));
    assert(!boot_target_select(OTA_STORE_OK, &record, &newer, key, false, "/firmware.ota", &target));
    assert(!boot_target_select(OTA_STORE_OK, &record, &changed, key, false, "/firmware.ota", &target));
    assert(boot_target_select(OTA_STORE_OK, &record, NULL, key, false, "/firmware.ota", &target));
    record.manifest[192] ^= 1U;
    assert(!boot_target_select(OTA_STORE_OK, &record, NULL, key, false, "/firmware.ota", &target));
}

/** 功能：验证已许可人工降级的恢复不会重新选最新版本；参数：无；返回：无。 */
static void test_installing_and_boundaries(void) {
    ota_manifest_t old;
    ota_manifest_t current;
    ota_record_t record = {0};
    boot_target_t target;
    candidate(1, &old);
    candidate(2, &current);
    record.state = OTA_STATE_INSTALLING;
    memcpy(record.manifest, old.raw, 256);
    assert(boot_target_select(OTA_STORE_OK, &record, &current, key, false, "/firmware.ota", &target));
    assert(target.pinned && !target.automatic && target.expected.version == 1);
    assert(!boot_target_accept(&target, &current));
    char path[158];
    memset(path, 'a', sizeof path);
    path[0] = '/';
    path[156] = '\0';
    assert(boot_target_select(OTA_STORE_OK, &record, NULL, key, false, path, &target));
    assert(strlen(target.path) == 192);
    path[156] = 'a';
    path[157] = '\0';
    assert(!boot_target_select(OTA_STORE_OK, &record, NULL, key, false, path, &target));
    assert(!boot_target_select(OTA_STORE_OK, &record, NULL, key, false, "/x?other=1", &target));
    assert(!boot_target_select(OTA_STORE_OK, &record, NULL, key, false, "/../secret", &target));
    assert(!boot_target_select(OTA_STORE_CORRUPT, &record, NULL, key, false, "/firmware.ota", &target));
}

/** 功能：运行Boot固定版本回归；参数：无；返回：成功为零。 */
int main(void) {
    for (unsigned i = 0; i < 32; ++i) {
        key[i] = (uint8_t)(i + 1U);
    }
    test_alias_and_forced();
    test_auto_target();
    test_installing_and_boundaries();
    puts("Boot authenticated immutable targets, automatic version guard and recovery tests passed");
    return 0;
}
