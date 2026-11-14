#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "wifi_store.h"

static unsigned char memory[65536];
static unsigned read_calls, write_calls, fail_read, fail_write, corrupt_read;
static size_t write_budget;
static bool ack_loss;

/** 功能：模拟 EEPROM 读取故障；参数：标准回调参数；返回：读取成功标志。 */
static bool read_memory(void *context, uint32_t address, void *data, size_t length) {
    (void)context;
    assert(address >= 0x1000U && address + length <= 0x1200U);
    ++read_calls;
    if (read_calls == fail_read) {
        return false;
    }
    memcpy(data, memory + address, length);
    if (read_calls == corrupt_read && length > 0U) {
        ((uint8_t *)data)[0] ^= 1U;
    }
    return true;
}

/** 功能：模拟页写、中断及应答丢失；参数：标准回调参数；返回：写入成功标志。 */
static bool write_memory(void *context, uint32_t address, const void *data, size_t length) {
    size_t count = length < write_budget ? length : write_budget;
    (void)context;
    assert(address >= 0x1000U && address + length <= 0x1200U);
    assert(address / 128U == (address + length - 1U) / 128U);
    ++write_calls;
    if (write_calls == fail_write && !ack_loss) {
        return false;
    }
    memcpy(memory + address, data, count);
    write_budget -= count;
    return count == length && !(write_calls == fail_write && ack_loss);
}

static const wifi_store_io_t io = {read_memory, write_memory, NULL};
static const wifi_credentials_t first = {"first", "12345678"};
static const wifi_credentials_t second = {"second", "abcdefgh"};

/** 功能：清除故障注入计数；参数：无；返回：无。 */
static void reset_io(void) {
    read_calls = 0U;
    write_calls = 0U;
    fail_read = 0U;
    fail_write = 0U;
    corrupt_read = 0U;
    write_budget = (size_t)-1;
    ack_loss = false;
}

/** 功能：验证字节边界、UTF-8 与密码规则；参数：无；返回：无。 */
static void test_validation(void) {
    wifi_credentials_t value = first;
    assert(wifi_credentials_valid(&first));
    assert(!wifi_credentials_valid(NULL));
    value.ssid[0] = '\0';
    assert(!wifi_credentials_valid(&value));
    memset(value.ssid, 's', 32U);
    value.ssid[32] = '\0';
    memset(value.password, '~', 63U);
    value.password[63] = '\0';
    assert(wifi_credentials_valid(&value));
    value.ssid[32] = 'x';
    assert(!wifi_credentials_valid(&value));
    value = first;
    memset(value.password, 'x', sizeof value.password);
    assert(!wifi_credentials_valid(&value));
    value = first;
    value.password[0] = '\0';
    assert(wifi_credentials_valid(&value));
    strcpy(value.password, "short");
    assert(!wifi_credentials_valid(&value));
    value = first;
    value.password[0] = '\x7f';
    assert(!wifi_credentials_valid(&value));
    value = first;
    strcpy(value.ssid, "\xe4\xb8\xad\xe6\x96\x87");
    assert(wifi_credentials_valid(&value));
    strcpy(value.ssid, "\xc2\xa3\xf0\x9f\x98\x80");
    assert(wifi_credentials_valid(&value));
    strcpy(value.ssid, "\xc0\x80");
    assert(!wifi_credentials_valid(&value));
    strcpy(value.ssid, "\xed\xa0\x80");
    assert(!wifi_credentials_valid(&value));
    strcpy(value.ssid, "\xf4\x90\x80\x80");
    assert(!wifi_credentials_valid(&value));
    strcpy(value.ssid, "\xe4\xb8");
    assert(!wifi_credentials_valid(&value));
    strcpy(value.ssid, "\x01");
    assert(!wifi_credentials_valid(&value));
}

/** 功能：验证空槽、保存、去重、损坏与读故障回退；参数：无；返回：无。 */
static void test_roundtrip(void) {
    wifi_credentials_t output = second;
    memset(memory, 0xff, sizeof memory);
    reset_io();
    assert(wifi_store_load(&io, &output) == WIFI_STORE_EMPTY);
    assert(strcmp(output.ssid, second.ssid) == 0);
    assert(wifi_store_save(&io, &first) == WIFI_STORE_OK);
    assert(wifi_store_load(&io, &output) == WIFI_STORE_OK);
    assert(strcmp(output.ssid, first.ssid) == 0 && strcmp(output.password, first.password) == 0);
    reset_io();
    assert(wifi_store_save(&io, &first) == WIFI_STORE_OK && write_calls == 0U);
    assert(wifi_store_save(&io, &second) == WIFI_STORE_OK);
    reset_io();
    fail_read = 2U;
    assert(wifi_store_save(&io, &first) == WIFI_STORE_IO_ERROR && write_calls == 0U);
    reset_io();
    memory[0x1110U] ^= 1U;
    assert(wifi_store_load(&io, &output) == WIFI_STORE_OK && strcmp(output.ssid, first.ssid) == 0);
    memory[0x1010U] ^= 1U;
    assert(wifi_store_load(&io, &output) == WIFI_STORE_CORRUPT);
    reset_io();
    fail_read = 1U;
    assert(wifi_store_load(&io, &output) == WIFI_STORE_IO_ERROR);
    assert(memory[0xfffU] == 0xffU && memory[0x1200U] == 0xffU);
}

/** 功能：枚举所有写字节中断点，验证旧记录始终可用；参数：无；返回：无。 */
static void test_power_cut(void) {
    unsigned char baseline[512];
    wifi_credentials_t output;
    memset(memory, 0xff, sizeof memory);
    reset_io();
    assert(wifi_store_save(&io, &first) == WIFI_STORE_OK);
    memcpy(baseline, memory + 0x1000U, sizeof baseline);
    for (size_t cut = 0U; cut <= 384U; ++cut) {
        memcpy(memory + 0x1000U, baseline, sizeof baseline);
        reset_io();
        write_budget = cut;
        (void)wifi_store_save(&io, &second);
        reset_io();
        assert(wifi_store_load(&io, &output) == WIFI_STORE_OK);
        assert(strcmp(output.ssid, first.ssid) == 0 || strcmp(output.ssid, second.ssid) == 0);
        assert(memory[0xfffU] == 0xffU && memory[0x1200U] == 0xffU);
    }
    memcpy(memory + 0x1000U, baseline, sizeof baseline);
    reset_io();
    fail_write = 3U;
    ack_loss = true;
    assert(wifi_store_save(&io, &second) == WIFI_STORE_OK);
    assert(wifi_store_load(&io, &output) == WIFI_STORE_OK && strcmp(output.ssid, second.ssid) == 0);
    /* 再次覆盖非活动槽，包含仍有旧提交标记的完整已用槽。 */
    memcpy(baseline, memory + 0x1000U, sizeof baseline);
    for (size_t cut = 0U; cut <= 384U; ++cut) {
        memcpy(memory + 0x1000U, baseline, sizeof baseline);
        reset_io();
        write_budget = cut;
        (void)wifi_store_save(&io, &first);
        reset_io();
        assert(wifi_store_load(&io, &output) == WIFI_STORE_OK);
        assert(strcmp(output.ssid, first.ssid) == 0 || strcmp(output.ssid, second.ssid) == 0);
    }
    for (unsigned failed = 1U; failed <= 6U; ++failed) {
        memcpy(memory + 0x1000U, baseline, sizeof baseline);
        reset_io();
        fail_read = failed;
        assert(wifi_store_save(&io, &first) == WIFI_STORE_IO_ERROR);
        if (failed <= 2U) {
            assert(write_calls == 0U);
        }
        reset_io();
        assert(wifi_store_load(&io, &output) == WIFI_STORE_OK);
    }
    for (unsigned corrupted = 3U; corrupted <= 4U; ++corrupted) {
        memcpy(memory + 0x1000U, baseline, sizeof baseline);
        reset_io();
        corrupt_read = corrupted;
        assert(wifi_store_save(&io, &first) == WIFI_STORE_IO_ERROR);
        reset_io();
        assert(wifi_store_load(&io, &output) == WIFI_STORE_OK && strcmp(output.ssid, second.ssid) == 0);
    }
}

/** 功能：构造格式规定的小端整数；参数：bytes，目标；value，整数；返回：无。 */
static void put_u32(uint8_t *bytes, uint32_t value) {
    for (unsigned i = 0U; i < 4U; ++i) {
        bytes[i] = (uint8_t)(value >> (8U * i));
    }
}

/** 功能：独立计算正文 CRC；参数：bytes，正文；返回：校验值。 */
static uint32_t record_crc(const uint8_t *bytes) {
    uint32_t crc = UINT32_MAX;
    for (unsigned i = 0U; i < 124U; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xedb88320UL : 0U);
        }
    }
    return ~crc;
}

/** 功能：以固定字节格式模拟历史代数；参数：slot，槽；generation，代数；返回：无。 */
static void set_generation(unsigned slot, uint32_t generation) {
    uint8_t *bytes = memory + 0x1000U + slot * 256U;
    uint32_t crc;
    put_u32(bytes + 8U, generation);
    crc = record_crc(bytes);
    put_u32(bytes + 124U, crc);
    put_u32(bytes + 132U, generation);
    put_u32(bytes + 136U, crc);
    put_u32(bytes + 140U, ~crc);
}

/** 功能：验证代数回绕、版本、提交及每字节损坏；参数：无；返回：无。 */
static void test_format_and_wrap(void) {
    unsigned char baseline[512];
    wifi_credentials_t output;
    memset(memory, 0xff, sizeof memory);
    reset_io();
    assert(wifi_store_save(&io, &first) == WIFI_STORE_OK);
    set_generation(0U, UINT32_MAX);
    assert(wifi_store_save(&io, &second) == WIFI_STORE_OK);
    assert(wifi_store_load(&io, &output) == WIFI_STORE_OK && strcmp(output.ssid, second.ssid) == 0);
    assert(memory[0x1108U] == 0U && memory[0x110bU] == 0U);
    memcpy(baseline, memory + 0x1000U, sizeof baseline);
    for (unsigned offset = 0U; offset < 144U; ++offset) {
        memcpy(memory + 0x1000U, baseline, sizeof baseline);
        memory[0x1100U + offset] ^= 1U;
        assert(wifi_store_load(&io, &output) == WIFI_STORE_OK);
        assert(strcmp(output.ssid, first.ssid) == 0);
    }
    memcpy(memory + 0x1000U, baseline, sizeof baseline);
    reset_io();
    fail_read = 2U;
    assert(wifi_store_load(&io, &output) == WIFI_STORE_OK && strcmp(output.ssid, first.ssid) == 0);
    reset_io();
    set_generation(1U, 0x7fffffffU);
    assert(wifi_store_load(&io, &output) == WIFI_STORE_CORRUPT);
    assert(wifi_store_save(&io, &first) == WIFI_STORE_CORRUPT && write_calls == 0U);
    memcpy(memory + 0x1000U, baseline, sizeof baseline);
    /* 版本未知但 CRC 与提交一致时，同样不得采用该记录。 */
    memory[0x1104U] = 2U;
    set_generation(1U, 0U);
    assert(wifi_store_load(&io, &output) == WIFI_STORE_OK && strcmp(output.ssid, first.ssid) == 0);
    memcpy(memory + 0x1000U, baseline, sizeof baseline);
    memory[0x110cU] = 33U;
    set_generation(1U, 0U);
    assert(wifi_store_load(&io, &output) == WIFI_STORE_OK && strcmp(output.ssid, first.ssid) == 0);
    memcpy(memory + 0x1000U, baseline, sizeof baseline);
    memory[0x1110U] = 0xc0U;
    set_generation(1U, 0U);
    assert(wifi_store_load(&io, &output) == WIFI_STORE_OK && strcmp(output.ssid, first.ssid) == 0);
    assert(wifi_store_load(NULL, &output) == WIFI_STORE_INVALID);
    assert(wifi_store_load(&io, NULL) == WIFI_STORE_INVALID);
    assert(wifi_store_save(&io, NULL) == WIFI_STORE_INVALID);
}

/** 功能：执行共享存储回归；参数：无；返回：通过时为零。 */
int main(void) {
    test_validation();
    test_roundtrip();
    test_power_cut();
    test_format_and_wrap();
    puts("Wi-Fi store: validation, wrap, 144 corruption, 6 read-fault and 770 power-cut scenarios passed");
    return 0;
}
