#ifndef CLOCK_WIFI_STORE_H
#define CLOCK_WIFI_STORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** 保存经过验证的无线网络凭据，字符串均以零结尾。 */
typedef struct {
    char ssid[33]; // 网络名称，最多 32 个 UTF-8 字节
    char password[64]; // 密码，空字符串或 8 至 63 个可打印 ASCII 字节
} wifi_credentials_t;

/** 绑定 EEPROM 字节读写回调；调用者负责覆盖完整事务的互斥。 */
typedef struct {
    bool (*read)(void *context, uint32_t address, void *data, size_t length); // 读取字节
    bool (*write)(void *context, uint32_t address, const void *data, size_t length); // 写入字节
    void *context; // 透传给回调的上下文
} wifi_store_io_t;

typedef enum {
    WIFI_STORE_OK = 0,
    WIFI_STORE_EMPTY,
    WIFI_STORE_CORRUPT,
    WIFI_STORE_IO_ERROR,
    WIFI_STORE_INVALID
} wifi_store_result_t;

/* 格式为小端：[0x1000,0x1200) 内两个 256 字节槽。
 * 正文页：0 魔数 WIFI，4 版本 1，8 模 2^32 代数，12/13 字符串长度，
 * 16 起 32 字节 SSID，48 起 63 字节密码，124 为前 124 字节的标准 CRC32。
 * 提交页：128 魔数 CMIT，132 同代数，136 同 CRC，140 反码，其余写零。
 * 加载允许可读有效旧槽回退；保存必须完整读到两个槽并最终严格重扫。
 * 调用者必须在同一事务锁内调用完整函数，回调不得访问其他存储区域。 */
bool wifi_credentials_valid(const wifi_credentials_t *credentials);
wifi_store_result_t wifi_store_load(const wifi_store_io_t *io, wifi_credentials_t *credentials);
wifi_store_result_t wifi_store_save(const wifi_store_io_t *io, const wifi_credentials_t *credentials);
#endif
