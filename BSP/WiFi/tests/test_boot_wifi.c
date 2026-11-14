#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "boot_platform.h"
#include "wifi_store.h"

#undef main
int boot_main(void);
static jmp_buf joined;
static uint8_t memory[65536];
static const char *expected_join;
static const char *response;
static size_t response_offset;
static uint32_t now_ms;
static bool wifi_read_failure;

/** 功能：模拟共享 EEPROM 读取；参数：标准回调；返回：成功标志。 */
static bool read_memory(void *context, uint32_t address, void *data, size_t length) {
    (void)context;
    assert(address + length <= sizeof memory);
    if (wifi_read_failure && address >= 0x1000U && address < 0x1200U) {
        return false;
    }
    memcpy(data, memory + address, length);
    return true;
}

/** 功能：模拟共享 EEPROM 写入；参数：标准回调；返回：成功标志。 */
static bool write_memory(void *context, uint32_t address, const void *data, size_t length) {
    (void)context;
    assert(address + length <= sizeof memory);
    memcpy(memory + address, data, length);
    return true;
}

/** 功能：截获真实 Boot 发出的网络加入命令；参数：标准串口回调；返回：成功标志。 */
static bool send_bytes(void *context, const void *data, size_t length) {
    const char *bytes = data;
    (void)context;
    if (length >= 9U && memcmp(bytes, "AT+CWJAP=", 9U) == 0) {
        assert(strlen(expected_join) == length && memcmp(bytes, expected_join, length) == 0);
        longjmp(joined, 1);
    }
    if (length == 8U && memcmp(bytes, "AT+RST\r\n", 8U) == 0) {
        response = "\r\nready\r\n";
    } else {
        response = "\r\nOK\r\n";
    }
    response_offset = 0U;
    return true;
}

/** 功能：模拟有界串口接收；参数：标准回调；返回：收到字节标志。 */
static bool receive_byte(void *context, uint8_t *byte, uint32_t limit) {
    (void)context;
    if (response != NULL && response[response_offset] != '\0') {
        *byte = (uint8_t)response[response_offset++];
        return true;
    }
    now_ms += limit;
    return false;
}

/** 功能：返回虚拟时间；参数：context，未用；返回：毫秒。 */
static uint32_t now(void *context) {
    (void)context;
    return now_ms;
}

/** 功能：推进虚拟时钟；参数：context，未用；ms，毫秒；返回：无。 */
static void delay(void *context, uint32_t ms) {
    (void)context;
    now_ms += ms;
}

/** 功能：提供正常裸机初始化；参数：无；返回：成功。 */
bool boot_platform_init(void) {
    return true;
}

/** 功能：丢弃与选择行为无关的日志；参数：message，消息；返回：无。 */
void boot_platform_log(const char *message) {
    (void)message;
}

/** 功能：强制进入恢复下载；参数：无；返回：真。 */
bool boot_platform_key(void) {
    return true;
}

/** 功能：提供系统毫秒；参数：无；返回：毫秒。 */
uint32_t boot_platform_now(void) {
    return now_ms;
}

/** 功能：阻止未到达加入命令的重试被掩盖；参数：ms，毫秒；返回：无。 */
void boot_platform_delay(uint32_t ms) {
    (void)ms;
    assert(false);
}

/** 功能：提供真实下载器的虚拟串口；参数：无；返回：接口。 */
boot_net_io_t boot_platform_net_io(void) {
    boot_net_io_t io = {send_bytes, receive_byte, now, delay, NULL, NULL};
    return io;
}

/** 功能：提供 Boot 原有 EEPROM 回调；参数：无；返回：接口。 */
ota_journal_io_t boot_platform_journal_io(void) {
    ota_journal_io_t io = {read_memory, write_memory, NULL};
    return io;
}

/** 功能：拒绝测试外擦除；参数：标准回调；返回：失败。 */
bool boot_platform_erase(void *context, unsigned sector) {
    (void)context;
    (void)sector;
    return false;
}

/** 功能：拒绝测试外编程；参数：标准回调；返回：失败。 */
bool boot_platform_program(void *context, uint32_t address, const void *data, size_t length) {
    (void)context;
    (void)address;
    (void)data;
    (void)length;
    return false;
}

/** 功能：模拟没有可启动应用；参数：标准回调；返回：失败。 */
bool boot_platform_read(void *context, uint32_t address, void *data, size_t length) {
    (void)context;
    (void)address;
    (void)data;
    (void)length;
    return false;
}

/** 功能：阻止测试中意外跳转；参数：无；返回：无。 */
void boot_platform_jump(void) {
    assert(false);
}

/** 功能：提供无 UART 故障的快照；参数：无；返回：零。 */
uint32_t boot_uart_errors(void) {
    return 0U;
}

/** 功能：运行真实 Boot 入口直到发送加入命令；参数：command，期望命令；返回：无。 */
static void expect_join(const char *command) {
    expected_join = command;
    response = NULL;
    now_ms = 0U;
    if (setjmp(joined) == 0) {
        (void)boot_main();
        assert(false);
    }
}

/** 功能：验证空记录、已保存优先、损坏回退及读取故障；参数：无；返回：通过为零。 */
int main(void) {
    const wifi_store_io_t io = {read_memory, write_memory, NULL};
    const wifi_credentials_t first = {"saved-network", "saved-password"};
    const wifi_credentials_t latest = {"latest-network", "latest-password"};
    memset(memory, 0xff, sizeof memory);
    expect_join("AT+CWJAP=\"compiled-default\",\"default-password\"\r\n");
    assert(wifi_store_save(&io, &first) == WIFI_STORE_OK);
    expect_join("AT+CWJAP=\"saved-network\",\"saved-password\"\r\n");
    assert(wifi_store_save(&io, &latest) == WIFI_STORE_OK);
    expect_join("AT+CWJAP=\"latest-network\",\"latest-password\"\r\n");
    memory[0x1110U] ^= 1U;
    expect_join("AT+CWJAP=\"saved-network\",\"saved-password\"\r\n");
    memory[0x1010U] ^= 1U;
    expect_join("AT+CWJAP=\"compiled-default\",\"default-password\"\r\n");
    wifi_read_failure = true;
    expect_join("AT+CWJAP=\"compiled-default\",\"default-password\"\r\n");
    puts("Boot Wi-Fi: 6 real-entry saved/default/fallback scenarios passed");
    return 0;
}
