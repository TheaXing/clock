#ifndef ESP_MQTT_H
#define ESP_MQTT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ESP_MQTT_TOPIC_CAPACITY 128U
#define ESP_MQTT_PAYLOAD_CAPACITY 1536U
#define ESP_MQTT_TOKEN_LIMIT 1024U
#define ESP_MQTT_CA_LIMIT 4096U

typedef enum {
    ESP_MQTT_ERROR_NONE,
    ESP_MQTT_ERROR_ARGUMENT,
    ESP_MQTT_ERROR_TRANSPORT,
    ESP_MQTT_ERROR_COMMAND,
    ESP_MQTT_ERROR_TLS_CA,
    ESP_MQTT_ERROR_QUEUE_OVERFLOW,
    ESP_MQTT_ERROR_DISCONNECTED
} esp_mqtt_error_t;

/** 提供不包含凭据或原始载荷的 MQTT 诊断快照。 */
typedef struct {
    const char *step;  // 固定诊断步骤标签，禁止包含参数或数据
    const char *reply; // 固定应答类型标签
    uint32_t at_error; // ESP SYSLOG 错误码，不可用或有歧义时为 0
} esp_mqtt_diagnostic_t;

/** 提供 MQTT 代理、认证凭据与可信 CA 配置。 */
typedef struct {
    const char *host;      // MQTT 代理主机名
    uint16_t port;         // MQTT 代理端口
    const char *client_id; // 客户端标识字符串
    const char *username;  // 认证用户名
    const char *password;  // 认证密码或令牌
    const uint8_t *ca_pem; // CA 证书文件原始字节，不含字符串结束空字节
    size_t ca_length;      // CA 证书字节数
    /* 空指针使用 host 作为 SNI 与证书名；空字符串表示省略 SNI，此时 ESP-AT4.1
     * 仅验证 CA 和日期，不匹配域名。仅供专用可信 CA 策略使用，禁止作为已验证握手
     * 失败后的自动回退。 */
    const char *tls_server_name; // SNI 名称，空指针使用 host，空串表示省略 SNI
} esp_mqtt_config_t;

/** 保存一个完整的 MQTT 接收帧及接收时刻。 */
typedef struct {
    char topic[ESP_MQTT_TOPIC_CAPACITY + 1U]; // MQTT 主题，包含结尾空字节
    uint32_t received_ms;                     // 完整帧接收时的系统毫秒时刻
    uint16_t length;                          // 有效载荷字节数
    uint8_t data[ESP_MQTT_PAYLOAD_CAPACITY];  // 有效载荷字节缓冲区
} esp_mqtt_message_t;

/* 所有调用由单个网络工作任务拥有。配置时复制主机和端口并立即发送凭据，指针仅需在调用期间有效。CA 为证书文件原始字节，不含 C 字符串结尾空字节；先读取比较，仅不匹配时写入，并在使用方案 3 前回读验证。连接前须由 SNTP 同步 ESP 时间，不允许不安全回退。配置等待上限为 3 秒；发布允许等待本地串口提示符 500 毫秒、+MQTTPUB:OK 1200 毫秒。发布超时使传输失效，下一条命令前须调用 esp_at_init。 */
bool esp_at_mqtt_configure(const esp_mqtt_config_t *config);
bool esp_at_mqtt_connect(uint32_t timeout_ms);
bool esp_at_mqtt_subscribe(const char *topic, uint8_t qos);
bool esp_at_mqtt_publish(const char *topic, const void *data, size_t length, uint8_t qos,
                         bool retain);
bool esp_at_mqtt_disconnect(void);
bool esp_at_mqtt_connected(void);
esp_mqtt_error_t esp_at_mqtt_last_error(void);
esp_mqtt_diagnostic_t esp_at_mqtt_diagnostic(void);
/* 待处理状态包括尚未收全的串口帧；轮询每次复制一个完整帧。ESP-AT 可能将代理消息拆成多帧，应用组装 JSON 时须等待后续帧并拒绝未完整文档。队列溢出将使传输失效并丢弃已排队命令。 */
bool esp_at_mqtt_pending(void);
bool esp_at_mqtt_poll(esp_mqtt_message_t *message);

#endif
