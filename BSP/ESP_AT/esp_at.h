#ifndef ESP_AT_H
#define ESP_AT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** 描述无线热点及当前连接状态。 */
typedef struct {
    char ssid[64];  // 热点名称字符串
    char bssid[18]; // 冒号分隔的热点 MAC 地址
    int channel;    // 无线信道编号
    int rssi;       // 接收信号强度，单位为 dBm
    bool connected; // 是否已经连接热点
} esp_wifi_info_t;

/** 保存经过校验的完整日历日期与时间。 */
typedef struct {
    uint16_t year;   // 完整年份，支持 2000～2099
    uint8_t month;   // 月份，取值为 1～12
    uint8_t day;     // 月内日期，从 1 开始
    uint8_t hour;    // 小时，取值为 0～23
    uint8_t minute;  // 分钟，取值为 0～59
    uint8_t second;  // 秒，取值为 0～59
    uint8_t weekday; // 星期编号，星期一为 1，星期日为 7
} esp_date_time_t;

/* 在 MX_USART2_UART_Init 完成且调度器启动后，从任务上下文调用。命令串行执行，所有等待均有有限超时。传输超时或错误后，继续发送命令前须再次调用 esp_at_init。初始化通过 AT+RST 软重启 ESP 并等待 ready，保留已存凭据和其他持久设置。 */
bool esp_at_init(void);

/* 供网络工作任务读取的诊断接口。固件信息包含完整多行 AT+GMR 应答及末尾 OK；失败时清空输出，容量须包含字符串结束空字节。 */
bool esp_at_get_firmware_info(char *output, size_t capacity);

typedef enum esp_mqtt_probe_result_t {
    ESP_MQTT_PROBE_SUPPORTED,
    ESP_MQTT_PROBE_REJECTED,
    ESP_MQTT_PROBE_TRANSPORT_ERROR
} esp_mqtt_probe_result_t;

/* 仅查询 AT+MQTTCONN?，不配置或连接 MQTT。SUPPORTED 表示状态查询获接受，不能保证全部 MQTT/TLS 功能可用。ERROR 或忙无法确定支持情况，且不破坏传输可用性。 */
esp_mqtt_probe_result_t esp_at_probe_mqtt(void);

bool esp_at_wifi_init(void);
bool esp_at_connect_wifi(const char *ssid, const char *pwd, const char *mac);
bool esp_at_get_wifi_info(esp_wifi_info_t *info);
bool wifi_is_connected(void);
bool esp_at_sntp_init(void);
bool esp_at_sntp_get_time(esp_date_time_t *date);

/* 返回按长度定界的多段 +HTTPCLIENT 载荷拼接成的 HTTP 正文，不包含 AT 头或末尾 OK，保留正文换行。独立快照在其他 AT 命令后仍有效，直到下一次 HTTP 获取调用；由单个网络工作任务调用并解析。 */
const char *esp_at_http_get(const char *url);
/* 使用 ESP-AT 4.1 HTTPCGET 并设置模块端网络超时，共享上述 HTTP 快照生命周期；timeout_ms 范围为 100～3000 毫秒。 */
const char *esp_at_http_get_bounded(const char *url, uint32_t timeout_ms);
#define ESP_HTTP_COMMAND_MAX 255U
#define ESP_HTTP_BOUNDED_URL_MAX_1000_MS (ESP_HTTP_COMMAND_MAX - 31U)

#define ESP_SERVER_CHUNK_CAPACITY 256U

typedef enum {
    ESP_SERVER_EVENT_DATA,
    ESP_SERVER_EVENT_CONNECTED,
    ESP_SERVER_EVENT_CLOSED
} esp_server_event_t;

/** 携带服务器连接事件或单个被动接收数据块。 */
typedef struct {
    esp_server_event_t event;                // 连接或数据事件类型
    uint8_t link;                            // 服务器连接编号
    uint32_t generation;                     // 当前连接非零代次，用于防止编号复用
    uint16_t length;                         // 有效载荷字节数
    uint8_t data[ESP_SERVER_CHUNK_CAPACITY]; // 有效载荷字节缓冲区
} esp_server_chunk_t;

/* 全部接口由同一网络工作任务调用，与 WiFi、SNTP、HTTPCLIENT 共用任务。每轮限制轮询次数，每次最多读取 256 字节，所有 AT 等待均有界。事件为连接快照，新代次使同编号的旧请求失效；发送和关闭时须回传事件代次，防止误操作复用编号。启动服务不复位模块，可选命令不支持时仍保留 WiFi、SNTP、HTTPCLIENT 传输可用性；ip 接收终端 IPv4 地址。 */
bool esp_at_is_ready(void);
bool esp_at_server_start(uint16_t port, char ip[16]);
bool esp_at_server_ready(void);
bool esp_at_server_poll(esp_server_chunk_t *chunk);
bool esp_at_server_send(uint8_t link, uint32_t generation, const void *data, size_t length);
bool esp_at_server_close(uint8_t link, uint32_t generation);

#endif
