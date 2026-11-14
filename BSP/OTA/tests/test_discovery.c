#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ota_discovery.h"
#include "ota_crypto.h"

/** 功能：生成公开结构样本，刻意不提供发布认证；参数：header、version，输出和版本；返回：无。 */
static void fixture(uint8_t header[256], uint32_t version) {
    uint8_t digest[32];
    memset(header, 0, 256U);
    memcpy(header, "CLKOTA1", 7U);
    header[8] = 1U;
    header[13] = 1U;
    header[18] = 1U;
    header[19] = 8U;
    header[20] = 0U;
    header[21] = 2U;
    for (size_t i = 0U; i < 4U; ++i) {
        header[24U + i] = (uint8_t)(version >> (i * 8U));
    }
    memcpy(header + 32U, "Clock_CubeMX-STM32F407VE", 24U);
    memset(header + 252U, 255, 4U);
    ota_sha256(header, 96U, digest);
    memcpy(header + 96U, digest, 16U);
}

/** 功能：编码小写十六进制字符串；参数：输入、长度、输出；返回：无。 */
static void hex(const uint8_t *data, size_t size, char *out) {
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0U; i < size; ++i) {
        out[i * 2U] = digits[data[i] >> 4U];
        out[i * 2U + 1U] = digits[data[i] & 15U];
    }
    out[size * 2U] = '\0';
}

/** 功能：生成发现正文；参数：包头、版本和输出；返回：无。 */
static void json_for(const uint8_t header[256], uint32_t version, char out[1025]) {
    char identity[33];
    char encoded[513];
    hex(header + 96U, 16U, identity);
    hex(header, 256U, encoded);
    (void)snprintf(out, 1025U, "{\"header\":\"%s\",\"packageId\":\"%s\",\"version\":%lu,\"available\":true}", encoded, identity, (unsigned long)version);
}

/** 功能：验证严格且无发布认证权限的发现解析；参数：无；返回：通过为零。 */
int main(void) {
    uint8_t header[256];
    uint8_t key[32] = {1U};
    ota_manifest_t manifest;
    ota_hint_t result;
    ota_hint_t before;
    char json[1025];
    char malformed[1100];
    fixture(header, 18U);
    json_for(header, 18U, json);
    assert(ota_discovery_parse(json, &result));
    assert(result.available && result.manifest.version == 18U);
    assert(memcmp(result.manifest.raw, header, 256U) == 0);
    size_t valid_length = strlen(json);
    memset(json + valid_length, ' ', 1024U - valid_length);
    json[1024] = '\0';
    assert(ota_discovery_parse(json, &result));
    json_for(header, 18U, json);
    char *uppercase = strstr(json, "434c");
    assert(uppercase != NULL);
    uppercase[3] = 'C';
    assert(!ota_discovery_parse(json, &result));
    assert(ota_manifest_read_hint(header, &manifest));
    assert(!ota_manifest_parse(header, key, false, &manifest));
    assert(ota_discovery_parse(" \n{ \"available\" : false }\r", &result));
    assert(!result.available);
    memset(&result, 0x5a, sizeof result);
    before = result;
    const char *invalid[] = {"{}", "{\"available\":1}", "{\"available\":false,\"version\":1}",
        "{\"available\":true}", "{\"available\":false,\"available\":false}",
        "{\"available\":false}x", "{\"available\":false,}", "{\"unknown\":false}",
        "{\"available\":false,\"header\":\"\"}", "{\"availa\\u0062le\":false}"};
    for (size_t i = 0U; i < sizeof invalid / sizeof invalid[0]; ++i) {
        assert(!ota_discovery_parse(invalid[i], &result));
        assert(memcmp(&before, &result, sizeof result) == 0);
    }
    json_for(header, 17U, json);
    assert(!ota_discovery_parse(json, &result));
    json_for(header, 18U, json);
    char *id = strstr(json, "packageId\":\"");
    assert(id != NULL);
    id[12] = id[12] == '0' ? '1' : '0';
    assert(!ota_discovery_parse(json, &result));
    fixture(header, UINT32_MAX);
    json_for(header, UINT32_MAX, json);
    assert(ota_discovery_parse(json, &result));
    const char *versions[] = {"0", "-1", "01", "4294967296", "1.0", "1e1", "+1"};
    for (size_t i = 0U; i < sizeof versions / sizeof versions[0]; ++i) {
        char *version;
        json_for(header, UINT32_MAX, json);
        version = strstr(json, "4294967295");
        assert(version != NULL);
        (void)snprintf(version, (size_t)(json + sizeof json - version), "%s,\"available\":true}", versions[i]);
        assert(!ota_discovery_parse(json, &result));
    }
    memset(malformed, ' ', sizeof malformed);
    memcpy(malformed, "{\"available\":false}", 19U);
    malformed[sizeof malformed - 1U] = '\0';
    assert(!ota_discovery_parse(malformed, &result));
    fixture(header, 18U);
    header[32] ^= 1U;
    json_for(header, 18U, json);
    assert(!ota_discovery_parse(json, &result));
    assert(!ota_discovery_parse(NULL, &result));
    assert(!ota_discovery_parse("{}", NULL));
    puts("OTA discovery strict parsing passed");
    return 0;
}
