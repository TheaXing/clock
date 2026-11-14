#include "esp_at.h"
#include "esp_at_parse.h"
#include "esp_mqtt.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "usart.h"

#include <stdio.h>
#include <string.h>

#define ESP_RESPONSE_CAPACITY 2048U
#define ESP_COMMAND_CAPACITY  1152U
#define ESP_LOCK_TIMEOUT_MS   1000U
#define ESP_SERVER_LINKS      5U
#define ESP_SERVER_SEND_LIMIT 1460U
#define ESP_DATA_ENTRY_LIMIT  ESP_MQTT_CA_LIMIT
#define ESP_MQTT_QUEUE_COUNT 3U
#define ESP_MQTT_WAIT_MS 2000U
#define ESP_MQTT_PUBLISH_PROMPT_MS 500U
#define ESP_MQTT_PUBLISH_DONE_MS 1200U

typedef enum {
    ACK_NONE,
    ACK_OK,
    ACK_ERROR,
    ACK_BUSY,
    ACK_READY,
    ACK_OVERFLOW,
    ACK_IO,
    ACK_TIMEOUT,
    ACK_PROMPT,
    ACK_SEND_OK,
    ACK_MQTT_PUB_OK
} at_ack_t;

typedef enum {
    REPLY_OK,
    REPLY_HTTP,
    REPLY_READY,
    REPLY_PASSIVE,
    REPLY_PROMPT,
    REPLY_SEND,
    REPLY_DRAIN,
    REPLY_MQTT_PUB,
    REPLY_DATA_OK,
    REPLY_CA
} reply_kind_t;
/** 保存服务器连接的活动状态、待读长度与事件代次快照。 */
typedef struct {
    volatile uint32_t generation;   // 当前连接非零代次，用于防止编号复用
    volatile uint32_t pending;      // 该连接待提取的字节数
    volatile uint32_t notification; // 收到待读通知的序列号
    volatile bool active;           // 连接当前是否活动
    uint32_t reported_generation;   // 上次上报事件的连接代次
    bool reported_active;           // 上次上报事件的活动状态
} server_link_t;

static SemaphoreHandle_t command_mutex;
static SemaphoreHandle_t response_semaphore;
static char response[ESP_RESPONSE_CAPACITY];
static char http_response[ESP_RESPONSE_CAPACITY];
static char command_buffer[ESP_COMMAND_CAPACITY];
static uint8_t uart_rx_byte;
static volatile bool receiving;
static volatile bool synchronized;
static volatile at_ack_t response_ack;
static volatile size_t response_length;
static volatile uint32_t rx_sequence;

/* 事务进行时仅由中断服务程序修改这些状态；任务发送新命令前在屏蔽中断的临界区内重置。 */
static char line[64];
static size_t line_length;
static size_t line_response_bytes;
static bool line_suppressed;
static reply_kind_t reply_kind;
static bool http_header;
static unsigned http_digits;
static uint32_t http_length;
static uint32_t http_remaining;
static bool passive_header;
static bool passive_seen;
static uint32_t passive_remaining;
static uint16_t passive_limit, passive_length, passive_stored;
static uint8_t passive_data[ESP_SERVER_CHUNK_CAPACITY];
static volatile bool server_ready;
static server_link_t server_links[ESP_SERVER_LINKS];
static uint8_t poll_cursor;
static uint16_t send_debt;
static bool send_recovery_pending;
static bool boot_resync_needed = true;
static bool valid_link(uint8_t id, uint32_t generation);

#if defined(__GNUC__) && (defined(__arm__) || defined(__thumb__))
#define MQTT_RAM __attribute__((section(".clock_mqtt_ram")))
#else
#define MQTT_RAM
#endif
/* 仅供处理器访问的 CCM 存储；链接段标记为 NOLOAD，因此必须显式初始化。串口逐字节接收到普通 SRAM，不直接写入这些槽位。 */
static esp_mqtt_message_t mqtt_queue[ESP_MQTT_QUEUE_COUNT] MQTT_RAM;
_Static_assert(sizeof mqtt_queue <= 5100U, "MQTT queue CCM budget exceeded");
static volatile uint8_t mqtt_head, mqtt_count;
static volatile bool mqtt_configured, mqtt_connected;
static volatile uint32_t mqtt_disconnect_sequence;
static volatile esp_mqtt_error_t mqtt_error;
static char mqtt_host[129];
static uint16_t mqtt_port;
typedef enum {
    MQTT_RX_IDLE,
    MQTT_RX_LINK,
    MQTT_RX_COMMA,
    MQTT_RX_QUOTE,
    MQTT_RX_TOPIC,
    MQTT_RX_SEPARATOR,
    MQTT_RX_LENGTH,
    MQTT_RX_DATA,
    MQTT_RX_BAD
} mqtt_rx_state_t;
static volatile mqtt_rx_state_t mqtt_rx_state;
static uint8_t mqtt_slot;
static uint16_t mqtt_topic_length, mqtt_stored;
static uint32_t mqtt_length, mqtt_remaining;
static bool mqtt_escape, mqtt_frame_valid;
static unsigned mqtt_digits;
static bool ca_header, ca_seen, ca_match;
static uint32_t ca_remaining, ca_total, ca_stored;
static const uint8_t *ca_expected;
static size_t ca_expected_length;

/**
 * 功能：清除 MQTT 配置、在线状态及待处理消息。
 * 参数：无。
 * 返回：无。
 */
static void mqtt_invalidate(void) {
    mqtt_configured = mqtt_connected = false;
    mqtt_count = mqtt_head = 0U;
    mqtt_frame_valid = false;
}

/**
 * 功能：使本地服务器及全部连接失效，并清空待读长度。
 * 参数：无。
 * 返回：无。
 */
static void invalidate_server(void) {
    server_ready = false;
    for (unsigned n = 0U; n < ESP_SERVER_LINKS; ++n) {
        server_links[n].active = false;
        server_links[n].pending = 0U;
    }
}

/**
 * 功能：标记 AT 传输失步并使服务器与 MQTT 状态失效。
 * 参数：无。
 * 返回：无。
 */
static void transport_fault(void) {
    synchronized = false;
    invalidate_server();
    mqtt_invalidate();
    if (mqtt_error != ESP_MQTT_ERROR_QUEUE_OVERFLOW) {
        mqtt_error = ESP_MQTT_ERROR_TRANSPORT;
    }
}

/**
 * 功能：将毫秒换算成至少一个调度节拍。
 * 参数：ms，待换算的毫秒数。
 * 返回：换算后的节拍数，最小为 1。
 */
static TickType_t ticks(uint32_t ms) {
    TickType_t result = pdMS_TO_TICKS(ms);
    return result != 0U ? result : 1U;
}

/**
 * 功能：在调度器运行时安全创建命令互斥锁与应答信号量。
 * 参数：无。
 * 返回：所需资源均已就绪返回 true，否则返回 false。
 */
static bool resources_ready(void) {
    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        return false;
    }
    /* 资源创建不能阻塞；临界区同时防止首次调用竞争产生两套独立互斥锁。 */
    taskENTER_CRITICAL();
    if (command_mutex == NULL) {
        command_mutex = xSemaphoreCreateMutex();
    }
    if (response_semaphore == NULL) {
        response_semaphore = xSemaphoreCreateBinary();
    }
    bool ready = command_mutex != NULL && response_semaphore != NULL;
    taskEXIT_CRITICAL();
    return ready;
}

/**
 * 功能：准备 AT 资源并在有限超时内获取命令互斥锁。
 * 参数：无。
 * 返回：取得锁返回 true，否则返回 false。
 */
static bool lock_commands(void) {
    return resources_ready() && xSemaphoreTake(command_mutex, ticks(ESP_LOCK_TIMEOUT_MS)) == pdTRUE;
}

/**
 * 功能：释放 AT 命令互斥锁。
 * 参数：无。
 * 返回：无。
 */
static void unlock_commands(void) {
    (void)xSemaphoreGive(command_mutex);
}

/**
 * 功能：在接收事务有效时记录应答并从中断唤醒等待任务。
 * 参数：ack，当前事务的应答类型；woken，累计是否唤醒更高优先级任务的地址。
 * 返回：无。
 */
static void notify_from_isr(at_ack_t ack, BaseType_t *woken) {
    if (!receiving) {
        return;
    }
    receiving = false;
    response_ack = ack;
    if (response_semaphore != NULL) {
        (void)xSemaphoreGiveFromISR(response_semaphore, woken);
    }
}

/**
 * 功能：将非被动接收应答字节加入缓冲区，溢出或空字节时报告故障。
 * 参数：byte，新收到的串口字节；woken，累计是否唤醒更高优先级任务的地址。
 * 返回：无。
 */
static void append_response(uint8_t byte, BaseType_t *woken) {
    if (!receiving || reply_kind == REPLY_PASSIVE) {
        return;
    }
    if (response_length >= sizeof response - 1U || byte == '\0') {
        transport_fault();
        notify_from_isr(ACK_OVERFLOW, woken);
    } else {
        response[response_length++] = (char)byte;
    }
}

/**
 * 功能：从应答缓冲中撤回当前主动通知行并停止继续存储该行。
 * 参数：无。
 * 返回：无。
 */
static void suppress_line(void) {
    if (receiving && response_length >= line_response_bytes) {
        response_length -= line_response_bytes;
    }
    line_response_bytes = 0U;
    line_suppressed = true;
}

/**
 * 功能：从接收行偏移处解析有上限的无符号十进制整数。
 * 参数：offset，接收行解析偏移的输入输出地址；
 *       length，传输或处理的字节数；
 *       limit，允许解析的最大数值；
 *       value，接收解析结果的地址。
 * 返回：解析成功返回 true 并推进偏移，否则返回 false。
 */
static bool line_number(size_t *offset, size_t length, uint32_t limit, uint32_t *value) {
    uint32_t result = 0U;
    size_t p = *offset;
    if (p == length || line[p] < '0' || line[p] > '9') {
        return false;
    }
    do {
        unsigned digit = (unsigned)(line[p] - '0');
        if (result > limit / 10U || (result == limit / 10U && digit > limit % 10U)) {
            return false;
        }
        result = result * 10U + digit;
        ++p;
    } while (p < length && line[p] >= '0' && line[p] <= '9');
    *offset = p;
    *value = result;
    return true;
}

/**
 * 功能：为指定服务器连接分配非零新代次并清空待读状态。
 * 参数：id，服务器连接编号。
 * 返回：无。
 */
static void connect_link(unsigned id) {
    server_link_t *link = &server_links[id];
    if (++link->generation == 0U) {
        ++link->generation;
    }
    link->active = true;
    link->pending = 0U;
}

/**
 * 功能：记录 MQTT 接收错误，使传输失效并唤醒等待事务。
 * 参数：error，失败时记录的错误类型；woken，累计是否唤醒更高优先级任务的地址。
 * 返回：无。
 */
static void mqtt_rx_fault(esp_mqtt_error_t error, BaseType_t *woken) {
    mqtt_error = error;
    transport_fault();
    notify_from_isr(ACK_OVERFLOW, woken);
}

/**
 * 功能：提交有效 MQTT 接收帧到队列并重置逐字节接收状态。
 * 参数：无。
 * 返回：无。
 */
static void mqtt_finish_frame(void) {
    if (mqtt_frame_valid && synchronized && mqtt_configured) {
        mqtt_queue[mqtt_slot].topic[mqtt_topic_length] = '\0';
        mqtt_queue[mqtt_slot].length = mqtt_stored;
        mqtt_queue[mqtt_slot].received_ms =
            (uint32_t)(xTaskGetTickCountFromISR() * portTICK_PERIOD_MS);
        ++mqtt_count;
    }
    mqtt_rx_state = MQTT_RX_IDLE;
    mqtt_frame_valid = false;
    line_length = line_response_bytes = 0U;
    line_suppressed = false;
}

/**
 * 功能：按 MQTT 接收状态机处理单字节，识别主题、长度及载荷。
 * 参数：byte，新收到的串口字节；woken，累计是否唤醒更高优先级任务的地址。
 * 返回：无。
 */
static void mqtt_receive(uint8_t byte, BaseType_t *woken) {
    switch (mqtt_rx_state) {
    case MQTT_RX_LINK:
        if (byte == '0') {
            mqtt_rx_state = MQTT_RX_COMMA;
        } else {
            goto malformed;
        }
        break;
    case MQTT_RX_COMMA:
        if (byte == ',') {
            mqtt_rx_state = MQTT_RX_QUOTE;
        } else {
            goto malformed;
        }
        break;
    case MQTT_RX_QUOTE:
        if (byte == '"') {
            mqtt_rx_state = MQTT_RX_TOPIC;
        } else {
            goto malformed;
        }
        break;
    case MQTT_RX_TOPIC:
        if (!mqtt_escape && byte == '"') {
            if (mqtt_topic_length == 0U) {
                goto malformed;
            }
            mqtt_rx_state = MQTT_RX_SEPARATOR;
        } else if (!mqtt_escape && byte == '\\') {
            mqtt_escape = true;
        } else {
            if (byte < 32U || byte == 127U || mqtt_topic_length >= ESP_MQTT_TOPIC_CAPACITY) {
                goto malformed;
            }
            if (mqtt_frame_valid) {
                mqtt_queue[mqtt_slot].topic[mqtt_topic_length] = (char)byte;
            }
            ++mqtt_topic_length;
            mqtt_escape = false;
        }
        break;
    case MQTT_RX_SEPARATOR:
        if (byte == ',') {
            mqtt_rx_state = MQTT_RX_LENGTH;
        } else {
            goto malformed;
        }
        break;
    case MQTT_RX_LENGTH:
        if (byte >= '0' && byte <= '9') {
            unsigned digit = (unsigned)(byte - '0');
            if (++mqtt_digits > 7U || mqtt_length > 1048576U / 10U ||
                (mqtt_length == 1048576U / 10U && digit > 1048576U % 10U)) {
                goto malformed;
            }
            mqtt_length = mqtt_length * 10U + digit;
        } else if (byte == ',' && mqtt_digits != 0U) {
            mqtt_remaining = mqtt_length;
            mqtt_rx_state = MQTT_RX_DATA;
            if (mqtt_length > ESP_MQTT_PAYLOAD_CAPACITY) {
                mqtt_rx_fault(ESP_MQTT_ERROR_QUEUE_OVERFLOW, woken);
            }
            if (mqtt_remaining == 0U) {
                mqtt_finish_frame();
            }
        } else {
            goto malformed;
        }
        break;
    case MQTT_RX_DATA:
        if (mqtt_frame_valid && mqtt_stored < ESP_MQTT_PAYLOAD_CAPACITY) {
            mqtt_queue[mqtt_slot].data[mqtt_stored++] = byte;
        }
        if (--mqtt_remaining == 0U) {
            mqtt_finish_frame();
        }
        break;
    case MQTT_RX_BAD:
        if (byte == '\n') {
            mqtt_rx_state = MQTT_RX_IDLE;
        }
        break;
    default:
        break;
    }
    return;
malformed:
    mqtt_rx_state = MQTT_RX_BAD;
    mqtt_rx_fault(ESP_MQTT_ERROR_QUEUE_OVERFLOW, woken);
}

/**
 * 功能：解析连接、待读数据及 MQTT 状态的主动通知行。
 * 参数：length，传输或处理的字节数；woken，累计是否唤醒更高优先级任务的地址。
 * 返回：识别为主动通知返回 true，否则返回 false。
 */
static bool unsolicited_line(size_t length, BaseType_t *woken) {
    if (length >= 5U && memcmp(line, "+IPD,", 5U) == 0) {
        size_t offset = 5U;
        uint32_t id, available;
        if (!line_number(&offset, length, ESP_SERVER_LINKS - 1U, &id) || offset >= length ||
            line[offset++] != ',' || !line_number(&offset, length, 0x7fffffffU, &available) ||
            offset != length) {
            transport_fault();
            notify_from_isr(ACK_IO, woken);
        } else {
            server_link_t *link = &server_links[id];
            if (!link->active) {
                connect_link((unsigned)id);
            }
            link->pending = available;
            ++link->notification;
        }
        return true;
    }
    if (length >= 2U && line[0] >= '0' && line[0] <= '4' && line[1] == ',') {
        unsigned id = (unsigned)(line[0] - '0');
        if (length == 9U && memcmp(line + 2U, "CONNECT", 7U) == 0) {
            connect_link(id);
        } else if (length == 8U && memcmp(line + 2U, "CLOSED", 6U) == 0) {
            server_links[id].active = false;
            server_links[id].pending = 0U;
        }
        return true;
    }
    if (length == 15U && memcmp(line, "WIFI DISCONNECT", 15U) == 0) {
        invalidate_server();
        mqtt_invalidate();
        mqtt_error = ESP_MQTT_ERROR_DISCONNECTED;
        return true;
    }
    if (length >= 18U && memcmp(line, "+MQTTDISCONNECTED:", 18U) == 0) {
        ++mqtt_disconnect_sequence;
        mqtt_connected = false;
        mqtt_count = 0U;
        mqtt_frame_valid = false;
        mqtt_error = ESP_MQTT_ERROR_DISCONNECTED;
        return true;
    }
    /* 连接主动通知仅供参考；只有经过协议方案、主机和端口核验的 MQTTCONN 状态查询才能确认应用在线。 */
    if (length >= 15U && memcmp(line, "+MQTTCONNECTED:", 15U) == 0) {
        return true;
    }
    return false;
}

/**
 * 功能：分流串口字节到定长载荷、主动通知或 AT 应答状态机。
 * 参数：byte，新收到的串口字节；woken，累计是否唤醒更高优先级任务的地址。
 * 返回：无。
 */
static void receive_byte(uint8_t byte, BaseType_t *woken) {
    ++rx_sequence;
    if (mqtt_rx_state != MQTT_RX_IDLE) {
        mqtt_receive(byte, woken);
        return;
    }
    if (ca_remaining != 0U) {
        if (ca_stored >= ca_expected_length || ca_expected[ca_stored] != byte) {
            ca_match = false;
        }
        ++ca_stored;
        --ca_remaining;
        return;
    }
    /* 两类载荷均使用长度定界；套接字数据中的空字节或类似结束应答的行不得进入行应答解析器或主动通知解析器。 */
    if (passive_remaining != 0U) {
        if (passive_stored < sizeof passive_data) {
            passive_data[passive_stored++] = byte;
        }
        --passive_remaining;
        return;
    }
    if (http_remaining != 0U) {
        append_response(byte, woken);
        --http_remaining;
        return;
    }
    if (!line_suppressed) {
        size_t before = response_length;
        /* 主动通知前缀在识别前暂时计入应答；应答容量耗尽时允许继续识别但不存储前缀尾部字节，完整识别后回退已存前缀。若匹配失败，仍按普通溢出路径报告故障。 */
        static const char mqtt_prefix[] = "+MQTTSUBRECV:";
        bool prefix_tail = response_length >= sizeof response - 1U &&
                           line_length < sizeof mqtt_prefix - 1U &&
                           memcmp(line, mqtt_prefix, line_length) == 0 &&
                           byte == (uint8_t)mqtt_prefix[line_length];
        if (!prefix_tail) {
            append_response(byte, woken);
        }
        line_response_bytes += response_length - before;
    }
    if (http_header || passive_header || ca_header) {
        if (byte >= '0' && byte <= '9') {
            unsigned digit = (unsigned)(byte - '0');
            uint32_t limit = ca_header        ? ESP_MQTT_CA_LIMIT
                             : passive_header ? passive_limit
                                              : ESP_RESPONSE_CAPACITY;
            if (++http_digits > 5U || http_length > limit / 10U ||
                (http_length == limit / 10U && digit > limit % 10U)) {
                transport_fault();
                http_header = passive_header = ca_header = false;
                notify_from_isr(ACK_OVERFLOW, woken);
            } else {
                http_length = http_length * 10U + digit;
            }
            return;
        }
        if (byte == ',' && http_digits != 0U) {
            if (ca_header) {
                ca_seen = true;
                ca_total = ca_remaining = http_length;
                ca_stored = 0U;
                ca_match = http_length == ca_expected_length;
            } else if (passive_header) {
                passive_seen = true;
                passive_remaining = http_length;
                passive_length = (uint16_t)http_length;
                passive_stored = 0U;
            } else {
                http_remaining = http_length;
            }
            http_header = passive_header = ca_header = false;
            line_length = 0U;
            line_response_bytes = 0U;
            if (reply_kind == REPLY_CA) {
                line_suppressed = false;
            }
            return;
        }
        /* 定界格式错误时也不得把载荷中类似结束应答的行交给应答解析。 */
        http_header = passive_header = ca_header = false;
        transport_fault();
        notify_from_isr(ACK_IO, woken);
    }

    if (byte == '>' && receiving && reply_kind == REPLY_PROMPT) {
        line_length = line_response_bytes = 0U;
        notify_from_isr(ACK_PROMPT, woken);
        return;
    }
    /* 部分固件在提示符后附带一个空格。 */
    if (line_length == 0U && byte == ' ') {
        return;
    }

    if (line_length < sizeof line) {
        line[line_length] = (char)byte;
    }
    if (line_length <= sizeof line) {
        ++line_length;
    }
    if (line_length == sizeof "+MQTTSUBRECV:" - 1U &&
        memcmp(line, "+MQTTSUBRECV:", sizeof "+MQTTSUBRECV:" - 1U) == 0) {
        suppress_line();
        mqtt_rx_state = MQTT_RX_LINK;
        mqtt_slot = (uint8_t)((mqtt_head + mqtt_count) % ESP_MQTT_QUEUE_COUNT);
        mqtt_topic_length = mqtt_stored = 0U;
        mqtt_length = mqtt_remaining = 0U;
        mqtt_digits = 0U;
        mqtt_escape = false;
        mqtt_frame_valid = synchronized && mqtt_configured && mqtt_count < ESP_MQTT_QUEUE_COUNT;
        if (mqtt_count >= ESP_MQTT_QUEUE_COUNT) {
            mqtt_rx_fault(ESP_MQTT_ERROR_QUEUE_OVERFLOW, woken);
        }
        return;
    }
    if ((line_length == sizeof "+MQTTCONNECTED:" - 1U &&
         memcmp(line, "+MQTTCONNECTED:", sizeof "+MQTTCONNECTED:" - 1U) == 0) ||
        (line_length == sizeof "+MQTTDISCONNECTED:" - 1U &&
         memcmp(line, "+MQTTDISCONNECTED:", sizeof "+MQTTDISCONNECTED:" - 1U) == 0)) {
        suppress_line();
    }
    if (receiving && reply_kind == REPLY_CA &&
        line_length == sizeof "+SYSMFG:\"mqtt_ca\",\"mqtt_ca\",8," - 1U &&
        memcmp(line, "+SYSMFG:\"mqtt_ca\",\"mqtt_ca\",8,",
               sizeof "+SYSMFG:\"mqtt_ca\",\"mqtt_ca\",8," - 1U) == 0) {
        suppress_line();
        ca_header = true;
        http_digits = 0U;
        http_length = 0U;
        return;
    }
    if ((line_length == 5U && memcmp(line, "+IPD,", 5U) == 0) ||
        (line_length == 2U && line[0] >= '0' && line[0] <= '4' && line[1] == ',')) {
        suppress_line();
    }
    if (line_suppressed && line_length >= 5U && memcmp(line, "+IPD,", 5U) == 0 && byte == ':') {
        /* 协商为被动接收模式后，收到主动模式数据应视为传输异常。 */
        transport_fault();
        notify_from_isr(ACK_IO, woken);
    }
    if (receiving && reply_kind == REPLY_HTTP &&
        ((line_length == sizeof "+HTTPCLIENT:" - 1U &&
          memcmp(line, "+HTTPCLIENT:", sizeof "+HTTPCLIENT:" - 1U) == 0) ||
         (line_length == sizeof "+HTTPCGET:" - 1U &&
          memcmp(line, "+HTTPCGET:", sizeof "+HTTPCGET:" - 1U) == 0))) {
        http_header = true;
        http_digits = 0U;
        http_length = 0U;
        return;
    }
    if (receiving && reply_kind == REPLY_PASSIVE && line_length == sizeof "+CIPRECVDATA:" - 1U &&
        memcmp(line, "+CIPRECVDATA:", sizeof "+CIPRECVDATA:" - 1U) == 0) {
        passive_header = true;
        http_digits = 0U;
        http_length = 0U;
        return;
    }
    if (byte != '\n') {
        return;
    }

    size_t length = line_length;
    if (length <= sizeof line) {
        if (length != 0U && line[length - 1U] == '\n') {
            --length;
        }
        if (length != 0U && line[length - 1U] == '\r') {
            --length;
        }
        if (unsolicited_line(length, woken)) {
            suppress_line();
        } else if (length == 2U && memcmp(line, "OK", 2U) == 0 && reply_kind != REPLY_READY &&
                   reply_kind != REPLY_PROMPT && reply_kind != REPLY_SEND &&
                   reply_kind != REPLY_DRAIN && reply_kind != REPLY_MQTT_PUB &&
                   (reply_kind != REPLY_PASSIVE || passive_seen)) {
            notify_from_isr(ACK_OK, woken);
        } else if (length == 11U && memcmp(line, "+MQTTPUB:OK", 11U) == 0 &&
                   (reply_kind == REPLY_MQTT_PUB || reply_kind == REPLY_DRAIN)) {
            notify_from_isr(ACK_MQTT_PUB_OK, woken);
        } else if (length == 13U && memcmp(line, "+MQTTPUB:FAIL", 13U) == 0 &&
                   (reply_kind == REPLY_MQTT_PUB || reply_kind == REPLY_DRAIN)) {
            notify_from_isr(ACK_ERROR, woken);
        } else if (length == 7U && memcmp(line, "SEND OK", 7U) == 0 &&
                   (reply_kind == REPLY_SEND || reply_kind == REPLY_DRAIN)) {
            notify_from_isr(ACK_SEND_OK, woken);
        } else if ((length == 5U && memcmp(line, "ERROR", 5U) == 0) ||
                   (length == 4U && memcmp(line, "FAIL", 4U) == 0) ||
                   (length == 9U && memcmp(line, "SEND FAIL", 9U) == 0)) {
            notify_from_isr(ACK_ERROR, woken);
        } else if (length >= 4U && memcmp(line, "busy", 4U) == 0) {
            notify_from_isr(ACK_BUSY, woken);
        } else if (length == 5U && memcmp(line, "ready", 5U) == 0) {
            transport_fault(); /* 同时识别模块的非请求重启。 */
            notify_from_isr(ACK_READY, woken);
        }
    }
    line_length = 0U;
    line_response_bytes = 0U;
    line_suppressed = false;
}

/**
 * 功能：处理 ESP 串口单字节接收完成并重新启动中断接收。
 * 参数：uart，触发回调的串口句柄。
 * 返回：无。
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart) {
    if (uart != &huart2) {
        return;
    }
    BaseType_t higher_priority_task_woken = pdFALSE;
    receive_byte(uart_rx_byte, &higher_priority_task_woken);
    /* 即使应答已满或已经完成，也始终重新开启接收。 */
    if (HAL_UART_Receive_IT(&huart2, &uart_rx_byte, 1U) != HAL_OK) {
        transport_fault();
        notify_from_isr(ACK_IO, &higher_priority_task_woken);
    }
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

/**
 * 功能：处理 ESP 串口接收错误，通知事务失败并恢复单字节接收。
 * 参数：uart，触发回调的串口句柄。
 * 返回：无。
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart) {
    if (uart != &huart2) {
        return;
    }
    BaseType_t higher_priority_task_woken = pdFALSE;
    transport_fault();
    notify_from_isr(ACK_IO, &higher_priority_task_woken);
    /* HAL 在溢出时停止接收；终止单字节中断接收（未使用接收 DMA），通过读取状态与数据寄存器清除帧、噪声和溢出错误，并立即恢复接收。 */
    (void)HAL_UART_AbortReceive(&huart2);
    __HAL_UART_CLEAR_OREFLAG(&huart2);
    (void)HAL_UART_Receive_IT(&huart2, &uart_rx_byte, 1U);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

/**
 * 功能：在总时限内等待串口持续静默达到指定时长。
 * 参数：quiet_ms，要求连续静默的毫秒数；limit_ms，允许等待的总毫秒数。
 * 返回：静默条件满足返回 true，超出总时限返回 false。
 */
static bool wait_quiet(uint32_t quiet_ms, uint32_t limit_ms) {
    TickType_t start = xTaskGetTickCount();
    TickType_t last_change = start;
    uint32_t sequence = rx_sequence;
    do {
        TickType_t now = xTaskGetTickCount();
        uint32_t current = rx_sequence;
        if (current != sequence) {
            sequence = current;
            last_change = now;
        }
        if ((TickType_t)(now - last_change) >= ticks(quiet_ms)) {
            return true;
        }
        if ((TickType_t)(now - start) >= ticks(limit_ms)) {
            return false;
        }
        vTaskDelay(ticks(10U));
    } while (true);
}

/**
 * 功能：在已持有命令锁时发送命令或载荷，并有界等待指定类型应答。
 * 参数：data，传输数据缓冲区；
 *       length，传输或处理的字节数；
 *       timeout_ms，操作超时毫秒数；
 *       kind，本次事务期望的应答类型；
 *       quiet，是否先等待串口静默；
 *       guard_link，需校验的连接编号，负数表示不检查；
 *       guard_generation，连接必须匹配的代次。
 * 返回：收到的应答类型，或超时、传输错误等失败类型。
 */
static at_ack_t transfer_locked(const void *data, uint16_t length, uint32_t timeout_ms,
                                reply_kind_t kind, bool quiet, int guard_link,
                                uint32_t guard_generation) {
    if (kind != REPLY_READY && kind != REPLY_DRAIN && !synchronized) {
        return ACK_IO;
    }
    /* 准备下一次事务前，等待主动通知和过期应答结束。 */
    if (quiet && !wait_quiet(20U, 300U)) {
        transport_fault();
        return ACK_IO;
    }
    /* 在代次检查与短 AT 命令发送之间暂停任务调度，缩短连接编号复用窗口；串口中断始终保持开启，载荷发送仍可被抢占。ESP 的数字编号协议无法保证命令字节在线路上传输时的编号复用具有原子性。 */
    bool suspend = guard_link >= 0 && kind != REPLY_SEND && length <= 64U;
    if (suspend) {
        vTaskSuspendAll();
    }
    taskENTER_CRITICAL();
    /* 等待静默期间可能已经收到就绪或错误中断。 */
    if (kind != REPLY_READY && kind != REPLY_DRAIN && !synchronized) {
        taskEXIT_CRITICAL();
        if (suspend) {
            (void)xTaskResumeAll();
        }
        return ACK_IO;
    }
    if (guard_link >= 0 && !valid_link((uint8_t)guard_link, guard_generation)) {
        taskEXIT_CRITICAL();
        if (suspend) {
            (void)xTaskResumeAll();
        }
        return ACK_ERROR;
    }
    receiving = false;
    while (xSemaphoreTake(response_semaphore, 0U) == pdTRUE) {
    }
    response_length = 0U;
    response_ack = ACK_NONE;
    /* 跨命令边界保留尚未接收完整的主动通知行。 */
    line_response_bytes = 0U;
    reply_kind = kind;
    passive_seen = false;
    passive_length = passive_stored = 0U;
    receiving = true;
    taskEXIT_CRITICAL();

    /* 恢复中断时可能收到关闭或连接通知；发送前必须再次检查，进入载荷模式时也不例外。 */
    if (guard_link >= 0 && !valid_link((uint8_t)guard_link, guard_generation)) {
        taskENTER_CRITICAL();
        receiving = false;
        taskEXIT_CRITICAL();
        if (suspend) {
            (void)xTaskResumeAll();
        }
        return ACK_ERROR;
    }

    /* 发送存储独立于中断持有的应答缓冲区。应答可能在发送返回前到达，因此必须先重置接收状态并清空信号量。 */
    HAL_StatusTypeDef tx = HAL_OK;
    if (kind == REPLY_DRAIN) {
        /* 使用中性填充结束状态不明的 CIPSEND 数据输入窗口，避免把过期应答泄露给复用连接。复用被动接收暂存区，并在发送首个零字节前准备完成应答接收。 */
        memset(passive_data, 0, sizeof passive_data);
        uint16_t remaining = length;
        while (remaining != 0U && tx == HAL_OK) {
            uint16_t amount =
                remaining > sizeof passive_data ? (uint16_t)sizeof passive_data : remaining;
            tx = HAL_UART_Transmit(&huart2, passive_data, amount, 1000U);
            remaining = (uint16_t)(remaining - amount);
        }
    } else {
        tx = HAL_UART_Transmit(&huart2, (uint8_t *)(void *)data, length, suspend ? 20U : 1000U);
    }
    if (tx == HAL_OK && (kind == REPLY_SEND || kind == REPLY_DRAIN || kind == REPLY_DATA_OK ||
                         kind == REPLY_MQTT_PUB)) {
        send_debt = 0U;
    }
    if (suspend) {
        (void)xTaskResumeAll();
    }
    bool signalled =
        tx == HAL_OK && xSemaphoreTake(response_semaphore, ticks(timeout_ms)) == pdTRUE;
    taskENTER_CRITICAL();
    receiving = false;
    at_ack_t ack = response_ack;
    taskEXIT_CRITICAL();
    response[response_length] = '\0';
    if (tx != HAL_OK) {
        ack = ACK_IO;
    } else if (!signalled || ack == ACK_NONE) {
        ack = ACK_TIMEOUT;
    } else if (kind != REPLY_READY && kind != REPLY_DRAIN && !synchronized) {
        ack = ACK_IO;
    }
    if (ack == ACK_TIMEOUT || ack == ACK_IO || ack == ACK_OVERFLOW || ack == ACK_READY) {
        transport_fault();
    }
    return ack;
}

/**
 * 功能：在已持有命令锁时发送 AT 字符串并选择普通、HTTP 或重启应答。
 * 参数：command，完整 AT 命令字符串；
 *       timeout_ms，操作超时毫秒数；
 *       http，是否启用 HTTP 定长载荷解析；
 *       wait_ready，是否等待模块重启就绪应答。
 * 返回：事务的实际应答类型。
 */
static at_ack_t exchange_locked(const char *command, uint32_t timeout_ms, bool http,
                                bool wait_ready) {
    return transfer_locked(command, (uint16_t)strlen(command), timeout_ms,
                           wait_ready ? REPLY_READY
                           : http     ? REPLY_HTTP
                                      : REPLY_OK,
                           true, -1, 0U);
}

/**
 * 功能：执行 AT 命令，仅对忙应答进行有限次数重试。
 * 参数：command，完整 AT 命令字符串；timeout_ms，操作超时毫秒数；http，是否启用 HTTP 定长载荷解析。
 * 返回：收到成功应答返回 true，否则返回 false。
 */
static bool command_locked(const char *command, uint32_t timeout_ms, bool http) {
    for (unsigned attempt = 0; attempt < 3U; ++attempt) {
        at_ack_t ack = exchange_locked(command, timeout_ms, http, false);
        if (ack == ACK_OK) {
            return true;
        }
        if (ack != ACK_BUSY) {
            return false;
        }
        vTaskDelay(ticks(250U));
    }
    return false;
}

/**
 * 功能：填充状态不明的数据输入窗口并结束残留命令行，使后续初始化可恢复。
 * 参数：无。
 * 返回：填充与换行发送已完成返回 true，否则返回 false。
 */
static bool recover_send_locked(void) {
    send_recovery_pending = true;
    if (send_debt != 0U) {
        (void)transfer_locked(NULL, send_debt, 2000U, REPLY_DRAIN, false, -1, 0U);
    }
    if (send_debt == 0U) {
        /* 若 CIPSEND 在观察到提示符前被拒绝，填充字节会进入命令解析器；发送 AT+RST 前先结束该行。 */
        static const uint8_t newline[] = "\r\n";
        if (HAL_UART_Transmit(&huart2, (uint8_t *)(void *)newline, 2U, 1000U) == HAL_OK) {
            send_recovery_pending = false;
        }
    }
    transport_fault();
    return !send_recovery_pending;
}

/**
 * 功能：恢复串口接收与残留输入状态，软重启 ESP 并关闭命令回显。
 * 参数：无。
 * 返回：模块重新同步且配置成功返回 true，否则返回 false。
 */
bool esp_at_init(void) {
    if (!lock_commands()) {
        return false;
    }
    taskENTER_CRITICAL();
    receiving = false;
    transport_fault();
    line_length = 0U;
    line_response_bytes = 0U;
    line_suppressed = false;
    http_header = false;
    http_remaining = 0U;
    passive_header = false;
    passive_remaining = 0U;
    mqtt_rx_state = MQTT_RX_IDLE;
    mqtt_error = ESP_MQTT_ERROR_NONE;
    ca_header = false;
    ca_remaining = 0U;
    memset(mqtt_queue, 0, sizeof mqtt_queue);
    reply_kind = REPLY_OK;
    (void)HAL_UART_AbortReceive(&huart2);
    __HAL_UART_CLEAR_OREFLAG(&huart2);
    HAL_StatusTypeDef status = HAL_UART_Receive_IT(&huart2, &uart_rx_byte, 1U);
    taskEXIT_CRITICAL();
    /* 仅复位微控制器会丢失 send_debt，但独立供电的 ESP 可能仍在等待 CIPSEND、MQTT 或 CA 字节。启动时填充最大 CA 长度的中性数据以覆盖未知输入窗口；若 ESP 已在命令模式，恢复过程的回车换行也会结束填充行。 */
    bool boot_drained = true;
    if (status == HAL_OK && boot_resync_needed) {
        if (send_debt == 0U) {
            send_debt = ESP_DATA_ENTRY_LIMIT;
        }
        boot_drained = recover_send_locked();
        if (boot_drained) {
            boot_resync_needed = false;
        }
    }
    /* AT+RST 与数据共用串口，否则可能被当作套接字载荷。清空失败状态须跨初始化重试保留，直到串口发送成功。 */
    bool drained = status == HAL_OK && boot_drained &&
                   ((send_debt == 0U && !send_recovery_pending) || recover_send_locked());
    bool ready = drained && wait_quiet(100U, 1000U);
    bool found = false;
    if (ready) {
        for (unsigned attempt = 0; attempt < 3U; ++attempt) {
            /* 迟到的旧 OK 无法标识新命令；重启模块并专门等待 ready，以清除旧的在途操作，期间有意忽略中间 OK。 */
            at_ack_t ack = exchange_locked("AT+RST\r\n", 5000U, false, true);
            if (ack == ACK_READY) {
                synchronized = true;
                found = true;
                break;
            }
            /* 此处仅对明确的命令拒绝安全重试；超时导致状态不明时，交由下一次初始化恢复。 */
            if (ack != ACK_BUSY && ack != ACK_ERROR) {
                break;
            }
            vTaskDelay(ticks(250U));
        }
    }
    /* 保留已保存凭据与设置，不恢复出厂配置。 */
    bool ok = found && command_locked("ATE0\r\n", 1000U, false);
    if (!ok) {
        synchronized = false;
    }
    unlock_commands();
    return ok;
}

/**
 * 功能：读取完整固件版本应答并复制到调用方缓冲区。
 * 参数：output，接收文本结果的缓冲区；capacity，输出缓冲区总容量，包含结束空字节。
 * 返回：读取成功且容量足够返回 true，否则返回 false。
 */
bool esp_at_get_firmware_info(char *output, size_t capacity) {
    if (output == NULL || capacity == 0U) {
        return false;
    }
    output[0] = '\0';
    if (!lock_commands()) {
        return false;
    }
    at_ack_t ack = exchange_locked("AT+GMR\r\n", 1000U, false, false);
    size_t length = response_length;
    bool ok = ack == ACK_OK && length < capacity;
    if (ok) {
        memcpy(output, response, length + 1U);
    }
    unlock_commands();
    return ok;
}

/**
 * 功能：只读探测模块是否接受 MQTT 连接状态查询。
 * 参数：无。
 * 返回：查询支持、明确拒绝或传输错误的探测结果。
 */
esp_mqtt_probe_result_t esp_at_probe_mqtt(void) {
    if (!lock_commands()) {
        return ESP_MQTT_PROBE_TRANSPORT_ERROR;
    }
    at_ack_t ack = exchange_locked("AT+MQTTCONN?\r\n", 1000U, false, false);
    unlock_commands();
    if (ack == ACK_OK) {
        return ESP_MQTT_PROBE_SUPPORTED;
    }
    if (ack == ACK_ERROR || ack == ACK_BUSY) {
        return ESP_MQTT_PROBE_REJECTED;
    }
    return ESP_MQTT_PROBE_TRANSPORT_ERROR;
}

/**
 * 功能：将 ESP 无线接口配置为终端模式。
 * 参数：无。
 * 返回：配置成功返回 true，否则返回 false。
 */
bool esp_at_wifi_init(void) {
    if (!lock_commands()) {
        return false;
    }
    bool ok = command_locked("AT+CWMODE=1\r\n", 2000U, false);
    unlock_commands();
    return ok;
}

/**
 * 功能：转义无线凭据并连接指定热点，可选锁定热点地址。
 * 参数：ssid，待连接热点名称；pwd，热点密码；mac，可选热点 MAC 地址，空指针表示不锁定。
 * 返回：连接命令成功返回 true，否则返回 false。
 */
bool esp_at_connect_wifi(const char *ssid, const char *pwd, const char *mac) {
    char escaped_ssid[65], escaped_pwd[129];
    if (!esp_at_quote(ssid, 32U, escaped_ssid, sizeof escaped_ssid) || escaped_ssid[0] == '\0' ||
        !esp_at_quote(pwd, 64U, escaped_pwd, sizeof escaped_pwd) ||
        (mac != NULL && !esp_at_valid_mac(mac)) || !lock_commands()) {
        return false;
    }
    int count;
    if (mac != NULL) {
        count = snprintf(command_buffer, sizeof command_buffer, "AT+CWJAP=\"%s\",\"%s\",\"%s\"\r\n",
                         escaped_ssid, escaped_pwd, mac);
    } else {
        count = snprintf(command_buffer, sizeof command_buffer, "AT+CWJAP=\"%s\",\"%s\"\r\n",
                         escaped_ssid, escaped_pwd);
    }
    bool ok = count > 0 && (size_t)count < sizeof command_buffer &&
              command_locked(command_buffer, 20000U, false);
    unlock_commands();
    return ok;
}

/**
 * 功能：查询无线连接状态，并在已连接时读取热点详细信息。
 * 参数：info，接收无线连接信息的地址。
 * 返回：查询及解析成功返回 true，否则返回 false。
 */
bool esp_at_get_wifi_info(esp_wifi_info_t *info) {
    if (info == NULL || !lock_commands()) {
        return false;
    }
    esp_wifi_info_t parsed = {0};
    bool ok =
        command_locked("AT+CWSTATE?\r\n", 2000U, false) && esp_at_parse_cwstate(response, &parsed);
    if (ok && parsed.connected) {
        ok = command_locked("AT+CWJAP?\r\n", 2000U, false) && esp_at_parse_cwjap(response, &parsed);
    }
    if (ok) {
        *info = parsed;
    }
    unlock_commands();
    return ok;
}

/**
 * 功能：查询并判断无线接口是否已经连接热点。
 * 参数：无。
 * 返回：查询成功且已连接返回 true，否则返回 false。
 */
bool wifi_is_connected(void) {
    esp_wifi_info_t info;
    return esp_at_get_wifi_info(&info) && info.connected;
}

/**
 * 功能：启用 ESP 网络校时并设置东八区。
 * 参数：无。
 * 返回：配置成功返回 true，否则返回 false。
 */
bool esp_at_sntp_init(void) {
    if (!lock_commands()) {
        return false;
    }
    bool ok = command_locked("AT+CIPSNTPCFG=1,8\r\n", 2000U, false);
    unlock_commands();
    return ok;
}

/**
 * 功能：查询并解析 ESP 网络校时结果。
 * 参数：date，接收解析日期时间的地址。
 * 返回：取得有效日期时间返回 true，否则返回 false。
 */
bool esp_at_sntp_get_time(esp_date_time_t *date) {
    if (date == NULL || !lock_commands()) {
        return false;
    }
    bool ok =
        command_locked("AT+CIPSNTPTIME?\r\n", 2000U, false) && esp_at_parse_sntp(response, date);
    unlock_commands();
    return ok;
}

/**
 * 功能：执行 HTTP GET 并保存去除 AT 定界的正文快照。
 * 参数：url，待请求的 HTTP 地址。
 * 返回：成功返回内部正文快照指针，失败返回 NULL。
 */
const char *esp_at_http_get(const char *url) {
    static const char prefix[] = "AT+HTTPCLIENT=2,1,\"";
    static const char suffix[] = "\",,,2\r\n";
    if (url == NULL || url[0] == '\0' || !lock_commands()) {
        return NULL;
    }
    http_response[0] = '\0';
    memcpy(command_buffer, prefix, sizeof prefix);
    bool ok = esp_at_quote(url, 512U, command_buffer + sizeof prefix - 1U,
                           sizeof command_buffer - sizeof prefix - sizeof suffix);
    if (ok) {
        memcpy(command_buffer + strlen(command_buffer), suffix, sizeof suffix);
        ok = command_locked(command_buffer, 20000U, true) &&
             esp_at_extract_http(response, http_response, sizeof http_response);
    }
    unlock_commands();
    return ok ? http_response : NULL;
}

/**
 * 功能：通过 HTTPCGET 按指定模块网络超时获取 HTTP 正文。
 * 参数：url，待请求的 HTTP 地址；timeout_ms，模块网络超时，取值为 100～3000 毫秒。
 * 返回：成功返回内部正文快照指针，失败返回 NULL。
 */
const char *esp_at_http_get_bounded(const char *url, uint32_t timeout_ms) {
    if (url == NULL || url[0] == '\0' || timeout_ms < 100U || timeout_ms > 3000U ||
        !lock_commands()) {
        return NULL;
    }
    http_response[0] = '\0';
    bool ok = esp_at_build_http_get_bounded(url, timeout_ms, command_buffer, sizeof command_buffer);
    if (ok) {
        ok = exchange_locked(command_buffer, timeout_ms + 1000U, true, false) == ACK_OK &&
             esp_at_extract_http(response, http_response, sizeof http_response);
    }
    unlock_commands();
    return ok ? http_response : NULL;
}

static const char *mqtt_step = "NONE";
static at_ack_t mqtt_reply;
static uint32_t mqtt_at_error;

/**
 * 功能：切换 MQTT 诊断步骤并清空旧应答与模块错误码。
 * 参数：step，固定诊断步骤标签。
 * 返回：无。
 */
static void mqtt_step_begin(const char *step) {
    mqtt_step = step;
    mqtt_reply = ACK_NONE;
    mqtt_at_error = 0U;
}

/* 只解析完整 SYSLOG 行，并仅暴露数字错误码；原始 AT 应答可能包含凭据，禁止写入应用日志。 */
/**
 * 功能：记录 MQTT 应答，仅从完整 SYSLOG 行提取无歧义数字错误码。
 * 参数：ack，当前事务的应答类型。
 * 返回：无。
 */
static void mqtt_capture_reply(at_ack_t ack) {
    static const char prefix[] = "ERR CODE:0x";
    mqtt_reply = ack;
    mqtt_at_error = 0U;
    bool seen = false;
    for (const char *p = response; *p != '\0';) {
        const char *end = p;
        while (*end != '\0' && *end != '\r' && *end != '\n') {
            ++end;
        }
        if ((size_t)(end - p) == sizeof prefix - 1U + 8U &&
            memcmp(p, prefix, sizeof prefix - 1U) == 0) {
            uint32_t code = 0U;
            for (const char *digit = p + sizeof prefix - 1U; digit < end; ++digit) {
                unsigned value;
                if (*digit >= '0' && *digit <= '9') {
                    value = (unsigned)(*digit - '0');
                } else if (*digit >= 'a' && *digit <= 'f') {
                    value = (unsigned)(*digit - 'a') + 10U;
                } else if (*digit >= 'A' && *digit <= 'F') {
                    value = (unsigned)(*digit - 'A') + 10U;
                } else {
                    mqtt_at_error = 0U;
                    return;
                }
                code = (code << 4U) | value;
            }
            if (seen) {
                mqtt_at_error = 0U;
                return;
            }
            seen = true;
            mqtt_at_error = code;
        }
        p = end;
        while (*p == '\r' || *p == '\n') {
            ++p;
        }
    }
}

/**
 * 功能：记录 MQTT 应答诊断，并将失败映射到 MQTT 错误状态。
 * 参数：ack，当前事务的应答类型；expected，预期应答类型。
 * 返回：实际应答符合预期且传输同步返回 true，否则返回 false。
 */
static bool mqtt_ack(at_ack_t ack, at_ack_t expected) {
    mqtt_capture_reply(ack);
    bool ok = ack == expected && synchronized;
    if (mqtt_error != ESP_MQTT_ERROR_QUEUE_OVERFLOW) {
        mqtt_error = ok             ? ESP_MQTT_ERROR_NONE
                     : synchronized ? ESP_MQTT_ERROR_COMMAND
                                    : ESP_MQTT_ERROR_TRANSPORT;
    }
    return ok;
}

/**
 * 功能：记录步骤并执行一条已加锁的 MQTT 配置命令。
 * 参数：command，完整 AT 命令字符串；step，固定诊断步骤标签。
 * 返回：取得预期成功应答返回 true，否则返回 false。
 */
static bool mqtt_command(const char *command, const char *step) {
    mqtt_step_begin(step);
    return mqtt_ack(exchange_locked(command, ESP_MQTT_WAIT_MS, false, false), ACK_OK);
}

/**
 * 功能：完成 MQTT 或证书命令的提示符与原始数据两阶段发送。
 * 参数：command，声明数据输入长度的 AT 命令；
 *       data，传输数据缓冲区；
 *       length，传输或处理的字节数；
 *       publish，是否按 MQTT 发布应答规则处理；
 *       prompt_step，等待提示符时使用的固定诊断标签；
 *       data_step，发送数据时使用的固定诊断标签。
 * 返回：两阶段成功且相关状态仍有效返回 true，否则返回 false。
 */
static bool mqtt_data(const char *command, const void *data, size_t length, bool publish,
                      const char *prompt_step, const char *data_step) {
    mqtt_step_begin(prompt_step);
    send_debt = (uint16_t)length;
    at_ack_t ack =
        transfer_locked(command, (uint16_t)strlen(command),
                        publish ? ESP_MQTT_PUBLISH_PROMPT_MS : 3000U, REPLY_PROMPT, true, -1, 0U);
    if (ack != ACK_PROMPT) {
        (void)mqtt_ack(ack, ACK_PROMPT);
        if (ack == ACK_ERROR || ack == ACK_BUSY || ack == ACK_READY) {
            send_debt = 0U;
        } else {
            (void)recover_send_locked();
        }
        return false;
    }
    /* 提示符之后仍可能发生模块重启或队列溢出；复位前先用中性填充结束状态不明的数据输入窗口。 */
    if (!synchronized || (publish && !mqtt_connected)) {
        (void)mqtt_ack(ACK_IO, ACK_OK);
        (void)recover_send_locked();
        return false;
    }
    mqtt_step_begin(data_step);
    ack = transfer_locked(data, (uint16_t)length, publish ? ESP_MQTT_PUBLISH_DONE_MS : 3000U,
                          publish ? REPLY_MQTT_PUB : REPLY_DATA_OK, false, -1, 0U);
    bool ok = mqtt_ack(ack, publish ? ACK_MQTT_PUB_OK : ACK_OK);
    if (send_debt != 0U) {
        (void)recover_send_locked();
    }
    if (publish && !mqtt_connected && synchronized) {
        mqtt_error = ESP_MQTT_ERROR_DISCONNECTED;
    }
    return ok && synchronized && (!publish || mqtt_connected);
}

/**
 * 功能：读取模块持有的 CA 字节并与预期证书逐字节比较。
 * 参数：certificate，预期 CA 证书原始字节；length，传输或处理的字节数；step，固定诊断步骤标签。
 * 返回：读取事务的实际应答类型。
 */
static at_ack_t mqtt_read_ca(const uint8_t *certificate, size_t length, const char *step) {
    mqtt_step_begin(step);
    ca_expected = certificate;
    ca_expected_length = length;
    ca_seen = ca_match = false;
    ca_total = ca_stored = 0U;
    static const char command[] = "AT+SYSMFG=1,\"mqtt_ca\",\"mqtt_ca\"\r\n";
    at_ack_t ack =
        transfer_locked(command, (uint16_t)(sizeof command - 1U), 3000U, REPLY_CA, true, -1, 0U);
    mqtt_capture_reply(ack);
    return ack;
}

/**
 * 功能：先比较已有 CA，仅不匹配时写入并回读验证。
 * 参数：certificate，预期 CA 证书原始字节；length，传输或处理的字节数。
 * 返回：证书已匹配或写入验证成功返回 true，否则返回 false。
 */
static bool mqtt_ca(const uint8_t *certificate, size_t length) {
    at_ack_t ack = mqtt_read_ca(certificate, length, "CA_READ");
    if (ack == ACK_OK && ca_seen && ca_match && ca_remaining == 0U && ca_total == length) {
        return true;
    }
    if (!synchronized || (ack != ACK_OK && ack != ACK_ERROR)) {
        return false;
    }
    (void)snprintf(command_buffer, sizeof command_buffer,
                   "AT+SYSMFG=2,\"mqtt_ca\",\"mqtt_ca\",8,%lu\r\n", (unsigned long)length);
    if (!mqtt_data(command_buffer, certificate, length, false, "CA_WRITE_PROMPT",
                   "CA_WRITE_DATA")) {
        return false;
    }
    ack = mqtt_read_ca(certificate, length, "CA_VERIFY");
    return ack == ACK_OK && ca_seen && ca_match && ca_remaining == 0U && ca_total == length;
}

/**
 * 功能：检查 MQTT 主机名非空、长度及允许字符。
 * 参数：host，待校验的主机名，最长 128 字符。
 * 返回：主机名有效返回 true，否则返回 false。
 */
static bool mqtt_host_valid(const char *host) {
    if (host == NULL || host[0] == '\0') {
        return false;
    }
    for (size_t n = 0U;; ++n) {
        unsigned char c = (unsigned char)host[n];
        if (c == '\0') {
            return n <= 128U;
        }
        if (n == 128U || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == '-' || c == '.')) {
            return false;
        }
    }
}

/**
 * 功能：验证 MQTT 配置，准备可信 CA、凭据和连接参数。
 * 参数：config，MQTT 连接及证书配置。
 * 返回：模块配置成功且传输同步返回 true，否则返回 false。
 */
bool esp_at_mqtt_configure(const esp_mqtt_config_t *config) {
    mqtt_step_begin("ARGUMENT");
    char client[129], username[129];
    size_t token_length = 0U;
    if (config == NULL || !mqtt_host_valid(config->host) || config->port == 0U ||
        !esp_at_quote(config->client_id, 64U, client, sizeof client) || client[0] == '\0' ||
        !esp_at_quote(config->username, 64U, username, sizeof username) || username[0] == '\0' ||
        config->password == NULL || config->ca_pem == NULL || config->ca_length == 0U ||
        config->ca_length > ESP_MQTT_CA_LIMIT) {
        mqtt_error = ESP_MQTT_ERROR_ARGUMENT;
        return false;
    }
    const char *server_name =
        config->tls_server_name != NULL ? config->tls_server_name : config->host;
    if (server_name[0] != '\0' && !mqtt_host_valid(server_name)) {
        mqtt_error = ESP_MQTT_ERROR_ARGUMENT;
        return false;
    }
    while (token_length <= ESP_MQTT_TOKEN_LIMIT && config->password[token_length] != '\0') {
        ++token_length;
    }
    if (token_length == 0U || token_length > ESP_MQTT_TOKEN_LIMIT) {
        mqtt_error = ESP_MQTT_ERROR_ARGUMENT;
        return false;
    }
    if (!lock_commands()) {
        mqtt_error = ESP_MQTT_ERROR_TRANSPORT;
        return false;
    }
    taskENTER_CRITICAL();
    mqtt_invalidate();
    mqtt_error = ESP_MQTT_ERROR_NONE;
    memset(mqtt_queue, 0, sizeof mqtt_queue);
    taskEXIT_CRITICAL();
    bool ok = mqtt_command("AT+SYSLOG=1\r\n", "SYSLOG");
    if (ok) {
        ok = mqtt_ca(config->ca_pem, config->ca_length);
        if (!ok && synchronized) {
            mqtt_error = ESP_MQTT_ERROR_TLS_CA;
        }
    }
    if (ok) {
        ok = mqtt_command("AT+MQTTCLEAN=0\r\n", "MQTTCLEAN");
        /* 官方 ESP32-C3 AT4.1.1 对新建或已清理客户端返回 ERROR/0x010a6003；只有此明确状态可视为幂等成功，其他错误、忙、超时或串口故障仍终止配置。 */
        if (!ok && synchronized && mqtt_reply == ACK_ERROR &&
            mqtt_at_error == UINT32_C(0x010a6003)) {
            ok = true;
        }
    }
    if (ok) {
        int count =
            snprintf(command_buffer, sizeof command_buffer,
                     "AT+MQTTUSERCFG=0,3,\"%s\",\"%s\",\"\",0,0,\"\"\r\n", client, username);
        ok = count > 0 && count < 256 && mqtt_command(command_buffer, "MQTTUSERCFG");
        if (!ok && count >= 256) {
            mqtt_error = ESP_MQTT_ERROR_ARGUMENT;
        }
    }
    if (ok) {
        (void)snprintf(command_buffer, sizeof command_buffer, "AT+MQTTLONGPASSWORD=0,%lu\r\n",
                       (unsigned long)token_length);
        ok = mqtt_data(command_buffer, config->password, token_length, false,
                       "MQTTLONGPASSWORD_PROMPT", "MQTTLONGPASSWORD_DATA");
    }
    if (ok && server_name[0] != '\0') {
        (void)snprintf(command_buffer, sizeof command_buffer, "AT+MQTTSNI=0,\"%s\"\r\n",
                       server_name);
        ok = mqtt_command(command_buffer, "MQTTSNI");
    }
    if (ok) {
        ok = mqtt_command("AT+MQTTCONNCFG=0,60,0,\"\",\"\",0,0\r\n", "MQTTCONNCFG");
    }
    if (ok) {
        memcpy(mqtt_host, config->host, strlen(config->host) + 1U);
        mqtt_port = config->port;
        mqtt_configured = synchronized;
    }
    unlock_commands();
    return ok && mqtt_configured;
}

/**
 * 功能：连接已配置代理，核验查询返回的协议、主机和端口，并确认期间未发生断连。
 * 参数：timeout_ms，连接超时，取值为 1～20000 毫秒。
 * 返回：确认连接有效返回 true，否则返回 false。
 */
bool esp_at_mqtt_connect(uint32_t timeout_ms) {
    mqtt_step_begin("MQTTCONN");
    if (timeout_ms == 0U || timeout_ms > 20000U) {
        mqtt_error = ESP_MQTT_ERROR_ARGUMENT;
        return false;
    }
    if (!lock_commands()) {
        return false;
    }
    bool ok = mqtt_configured && synchronized;
    mqtt_connected = false;
    if (ok) {
        (void)snprintf(command_buffer, sizeof command_buffer, "AT+MQTTCONN=0,\"%s\",%u,0\r\n",
                       mqtt_host, (unsigned)mqtt_port);
        ok = mqtt_ack(exchange_locked(command_buffer, timeout_ms, false, false), ACK_OK);
    }
    uint32_t sequence = mqtt_disconnect_sequence;
    if (ok) {
        ok = mqtt_command("AT+MQTTCONN?\r\n", "MQTTCONN_STATE");
    }
    if (ok) {
        const char *p = response;
        while (p[0] == '\r' && p[1] == '\n') {
            p += 2U;
        }
        size_t host_length = strlen(mqtt_host);
        static const char prefix[] = "+MQTTCONN:0,";
        ok = strncmp(p, prefix, sizeof prefix - 1U) == 0;
        if (ok) {
            p += sizeof prefix - 1U;
            ok = p[0] >= '4' && p[0] <= '6' && strncmp(p + 1U, ",3,\"", 4U) == 0;
            if (ok) {
                p += 5U;
                ok = strncmp(p, mqtt_host, host_length) == 0 && p[host_length] == '"' &&
                     p[host_length + 1U] == ',';
                if (ok) {
                    p += host_length + 2U;
                    /* AT4.1.1 给数字端口添加引号，同时兼容其他版本不带引号的格式。 */
                    bool quoted_port = *p == '"';
                    if (quoted_port) {
                        ++p;
                    }
                    uint32_t port = 0U;
                    unsigned digits = 0U;
                    while (*p >= '0' && *p <= '9' && digits < 6U) {
                        port = port * 10U + (unsigned)(*p++ - '0');
                        ++digits;
                    }
                    bool port_closed = !quoted_port || *p == '"';
                    if (quoted_port && port_closed) {
                        ++p;
                    }
                    ok = digits != 0U && digits <= 5U && port_closed && *p == ',' &&
                         port == mqtt_port;
                }
            }
        }
    }
    taskENTER_CRITICAL();
    mqtt_connected = ok && synchronized && mqtt_configured && sequence == mqtt_disconnect_sequence;
    ok = mqtt_connected;
    if (!ok && mqtt_error == ESP_MQTT_ERROR_NONE) {
        mqtt_error = ESP_MQTT_ERROR_DISCONNECTED;
    }
    taskEXIT_CRITICAL();
    unlock_commands();
    return ok;
}

/**
 * 功能：转义主题并订阅指定 MQTT 服务质量。
 * 参数：topic，MQTT 主题字符串；qos，MQTT 服务质量，取值为 0～2。
 * 返回：订阅成功且连接有效返回 true，否则返回 false。
 */
bool esp_at_mqtt_subscribe(const char *topic, uint8_t qos) {
    mqtt_step_begin("MQTTSUB");
    char escaped[257];
    if (qos > 2U || !esp_at_quote(topic, ESP_MQTT_TOPIC_CAPACITY, escaped, sizeof escaped) ||
        escaped[0] == '\0') {
        mqtt_error = ESP_MQTT_ERROR_ARGUMENT;
        return false;
    }
    if (!lock_commands()) {
        return false;
    }
    int count = snprintf(command_buffer, sizeof command_buffer, "AT+MQTTSUB=0,\"%s\",%u\r\n",
                         escaped, (unsigned)qos);
    bool ok = mqtt_connected && synchronized && count > 0 && count < 256 &&
              mqtt_command(command_buffer, "MQTTSUB") && mqtt_connected;
    if (count <= 0 || count >= 256) {
        mqtt_error = ESP_MQTT_ERROR_ARGUMENT;
    }
    if (!mqtt_connected && synchronized) {
        mqtt_error = ESP_MQTT_ERROR_DISCONNECTED;
    }
    unlock_commands();
    return ok;
}

/**
 * 功能：转义主题后通过原始数据模式发布 MQTT 载荷。
 * 参数：topic，MQTT 主题字符串；
 *       data，传输数据缓冲区；
 *       length，传输或处理的字节数；
 *       qos，MQTT 服务质量，取值为 0～2；
 *       retain，是否要求代理保留消息。
 * 返回：发布成功且传输与连接有效返回 true，否则返回 false。
 */
bool esp_at_mqtt_publish(const char *topic, const void *data, size_t length, uint8_t qos,
                         bool retain) {
    mqtt_step_begin("MQTTPUBRAW_ARGUMENT");
    char escaped[257];
    if (data == NULL || length == 0U || length > ESP_MQTT_PAYLOAD_CAPACITY || qos > 2U ||
        !esp_at_quote(topic, ESP_MQTT_TOPIC_CAPACITY, escaped, sizeof escaped) ||
        escaped[0] == '\0') {
        mqtt_error = ESP_MQTT_ERROR_ARGUMENT;
        return false;
    }
    if (!lock_commands()) {
        return false;
    }
    int count =
        snprintf(command_buffer, sizeof command_buffer, "AT+MQTTPUBRAW=0,\"%s\",%lu,%u,%u\r\n",
                 escaped, (unsigned long)length, (unsigned)qos, retain ? 1U : 0U);
    bool ok = mqtt_connected && synchronized && count > 0 && count < 256 &&
              mqtt_data(command_buffer, data, length, true, "MQTTPUBRAW_PROMPT", "MQTTPUBRAW_DATA");
    if (count <= 0 || count >= 256) {
        mqtt_error = ESP_MQTT_ERROR_ARGUMENT;
    }
    unlock_commands();
    return ok;
}

/**
 * 功能：清理模块 MQTT 客户端并清空本地 MQTT 状态。
 * 参数：无。
 * 返回：清理命令成功返回 true，否则返回 false。
 */
bool esp_at_mqtt_disconnect(void) {
    if (!lock_commands()) {
        return false;
    }
    bool ok = mqtt_command("AT+MQTTCLEAN=0\r\n", "MQTT_DISCONNECT");
    taskENTER_CRITICAL();
    mqtt_invalidate();
    taskEXIT_CRITICAL();
    unlock_commands();
    return ok;
}

/**
 * 功能：读取传输、配置和 MQTT 在线状态的组合结果。
 * 参数：无。
 * 返回：三者均有效返回 true，否则返回 false。
 */
bool esp_at_mqtt_connected(void) {
    return synchronized && mqtt_configured && mqtt_connected;
}
/**
 * 功能：取得最近记录的 MQTT 错误。
 * 参数：无。
 * 返回：最近一次 MQTT 错误类型。
 */
esp_mqtt_error_t esp_at_mqtt_last_error(void) {
    return mqtt_error;
}
/**
 * 功能：生成仅包含固定步骤、应答标签与数字错误码的诊断快照。
 * 参数：无。
 * 返回：当前 MQTT 诊断快照。
 */
esp_mqtt_diagnostic_t esp_at_mqtt_diagnostic(void) {
    static const char *const names[] = {"NONE",   "OK",       "ERROR",      "BUSY",
                                        "READY",  "OVERFLOW", "IO",         "TIMEOUT",
                                        "PROMPT", "SEND_OK",  "MQTT_PUB_OK"};
    esp_mqtt_diagnostic_t result = {mqtt_step, names[mqtt_reply], mqtt_at_error};
    return result;
}

/**
 * 功能：原子检查完整排队消息或尚未收全的 MQTT 串口帧。
 * 参数：无。
 * 返回：存在待处理数据返回 true，否则返回 false。
 */
bool esp_at_mqtt_pending(void) {
    taskENTER_CRITICAL();
    bool pending = mqtt_count != 0U || mqtt_rx_state != MQTT_RX_IDLE ||
                   (line_length != 0U && line_length <= sizeof "+MQTTSUBRECV:" - 1U &&
                    memcmp(line, "+MQTTSUBRECV:", line_length) == 0);
    taskEXIT_CRITICAL();
    return pending;
}

/**
 * 功能：原子取出一条完整 MQTT 接收帧。
 * 参数：message，接收完整 MQTT 帧的地址。
 * 返回：取出消息返回 true，否则返回 false。
 */
bool esp_at_mqtt_poll(esp_mqtt_message_t *message) {
    if (message == NULL) {
        return false;
    }
    taskENTER_CRITICAL();
    bool found = synchronized && mqtt_count != 0U;
    if (found) {
        *message = mqtt_queue[mqtt_head];
        mqtt_head = (uint8_t)((mqtt_head + 1U) % ESP_MQTT_QUEUE_COUNT);
        --mqtt_count;
    }
    taskEXIT_CRITICAL();
    return found;
}

/**
 * 功能：查询 AT 传输是否已经同步。
 * 参数：无。
 * 返回：传输同步返回 true，否则返回 false。
 */
bool esp_at_is_ready(void) {
    return synchronized;
}

/**
 * 功能：配置多连接被动接收服务器并查询终端 IPv4 地址。
 * 参数：port，服务器监听端口；ip，接收 IPv4 文本的十六字节缓冲区。
 * 返回：服务器建立且传输同步返回 true，否则返回 false。
 */
bool esp_at_server_start(uint16_t port, char ip[16]) {
    if (port == 0U || ip == NULL || !lock_commands()) {
        return false;
    }
    ip[0] = '\0';
    taskENTER_CRITICAL();
    invalidate_server();
    taskEXIT_CRITICAL();
    bool ok = command_locked("AT+CIPMUX=1\r\n", 2000U, false) &&
              command_locked("AT+CIPDINFO=0\r\n", 2000U, false);
    if (ok) {
        at_ack_t ack = exchange_locked("AT+CIPRECVMODE=1\r\n", 2000U, false, false);
        /* ESP-AT 4.x 重命名了该命令，并新增表示全部连接的编号 5；仅明确拒绝时允许回退，超时导致状态不明后禁止继续发送命令。 */
        ok = ack == ACK_OK ||
             (ack == ACK_ERROR && command_locked("AT+CIPRECVTYPE=5,1\r\n", 2000U, false));
    }
    if (ok) {
        (void)snprintf(command_buffer, sizeof command_buffer, "AT+CIPSERVER=1,%u\r\n",
                       (unsigned)port);
        ok = command_locked(command_buffer, 2000U, false) &&
             command_locked("AT+CIPSTO=60\r\n", 2000U, false) &&
             command_locked("AT+CIFSR\r\n", 2000U, false) && esp_at_parse_station_ip(response, ip);
    }
    taskENTER_CRITICAL();
    server_ready = ok && synchronized;
    ok = server_ready;
    taskEXIT_CRITICAL();
    unlock_commands();
    return ok;
}

/**
 * 功能：查询本地服务器与 AT 传输是否同时就绪。
 * 参数：无。
 * 返回：二者均就绪返回 true，否则返回 false。
 */
bool esp_at_server_ready(void) {
    return server_ready && synchronized;
}

/**
 * 功能：检查服务器连接编号、活动状态及代次是否仍然匹配。
 * 参数：id，服务器连接编号；generation，需要保持匹配的连接代次。
 * 返回：连接仍有效返回 true，否则返回 false。
 */
static bool valid_link(uint8_t id, uint32_t generation) {
    return id < ESP_SERVER_LINKS && server_ready && synchronized && server_links[id].active &&
           server_links[id].generation == generation;
}

/**
 * 功能：轮询连接快照，或从活动连接有界提取一个数据块。
 * 参数：chunk，接收连接事件或数据块的地址。
 * 返回：产生连接事件或非空数据块返回 true，否则返回 false。
 */
bool esp_at_server_poll(esp_server_chunk_t *chunk) {
    if (chunk == NULL || !esp_at_server_ready() || !lock_commands()) {
        return false;
    }
    bool found = false;
    uint8_t id = 0U;
    uint16_t requested = 0U;
    uint32_t generation = 0U, notification = 0U;
    taskENTER_CRITICAL();
    /* 使用快照合并快速关闭和重连，避免工作任务执行最长 20 秒 HTTPCLIENT 调用时通知队列溢出。 */
    for (unsigned n = 0U; n < ESP_SERVER_LINKS; ++n) {
        id = (uint8_t)((poll_cursor + n) % ESP_SERVER_LINKS);
        server_link_t *link = &server_links[id];
        if (link->generation != link->reported_generation ||
            link->active != link->reported_active) {
            chunk->event = link->active ? ESP_SERVER_EVENT_CONNECTED : ESP_SERVER_EVENT_CLOSED;
            chunk->link = id;
            chunk->generation = link->generation;
            chunk->length = 0U;
            link->reported_generation = link->generation;
            link->reported_active = link->active;
            poll_cursor = (uint8_t)((id + 1U) % ESP_SERVER_LINKS);
            found = true;
            break;
        }
    }
    if (!found) {
        for (unsigned n = 0U; n < ESP_SERVER_LINKS; ++n) {
            id = (uint8_t)((poll_cursor + n) % ESP_SERVER_LINKS);
            server_link_t *link = &server_links[id];
            if (link->active && link->pending != 0U) {
                requested = link->pending > ESP_SERVER_CHUNK_CAPACITY ? ESP_SERVER_CHUNK_CAPACITY
                                                                      : (uint16_t)link->pending;
                generation = link->generation;
                notification = link->notification;
                break;
            }
        }
    }
    taskEXIT_CRITICAL();
    if (found || requested == 0U) {
        unlock_commands();
        return found;
    }

    (void)snprintf(command_buffer, sizeof command_buffer, "AT+CIPRECVDATA=%u,%u\r\n", (unsigned)id,
                   (unsigned)requested);
    passive_limit = requested;
    at_ack_t ack = transfer_locked(command_buffer, (uint16_t)strlen(command_buffer), 2000U,
                                   REPLY_PASSIVE, true, id, generation);
    taskENTER_CRITICAL();
    if (valid_link(id, generation)) {
        server_link_t *link = &server_links[id];
        if (ack == ACK_OK && passive_seen && passive_length == passive_stored) {
            if (link->notification == notification) {
                link->pending =
                    passive_length >= link->pending ? 0U : link->pending - passive_length;
                if (passive_length == 0U) {
                    link->pending = 0U;
                }
            }
            chunk->event = ESP_SERVER_EVENT_DATA;
            chunk->link = id;
            chunk->generation = generation;
            chunk->length = passive_length;
            /* 只有工作任务发出命令后，中断才可能接收下一次被动数据，因此可在下方临界区外复制。 */
            found = passive_length != 0U;
        } else if (ack == ACK_ERROR) {
            link->pending = 0U;
        }
    }
    poll_cursor = (uint8_t)((id + 1U) % ESP_SERVER_LINKS);
    taskEXIT_CRITICAL();
    if (found) {
        memcpy(chunk->data, passive_data, passive_length);
    }
    unlock_commands();
    return found;
}

/**
 * 功能：按发送上限分块发送服务器应答，并在各阶段校验连接代次。
 * 参数：link，服务器连接编号；generation，需要保持匹配的连接代次；data，传输数据缓冲区；length，传输或处理的字节数。
 * 返回：全部发送成功且连接有效返回 true，否则返回 false。
 */
bool esp_at_server_send(uint8_t link, uint32_t generation, const void *data, size_t length) {
    if ((data == NULL && length != 0U) || !lock_commands()) {
        return false;
    }
    const uint8_t *bytes = data;
    bool ok = valid_link(link, generation);
    while (ok && length != 0U) {
        uint16_t amount = length > ESP_SERVER_SEND_LIMIT ? ESP_SERVER_SEND_LIMIT : (uint16_t)length;
        (void)snprintf(command_buffer, sizeof command_buffer, "AT+CIPSEND=%u,%u\r\n",
                       (unsigned)link, (unsigned)amount);
        send_debt = amount;
        at_ack_t ack = transfer_locked(command_buffer, (uint16_t)strlen(command_buffer), 2000U,
                                       REPLY_PROMPT, true, link, generation);
        ok = ack == ACK_PROMPT;
        if (!ok) {
            if (ack == ACK_ERROR || ack == ACK_BUSY || ack == ACK_READY) {
                send_debt = 0U;
            } else {
                (void)recover_send_locked();
            }
        }
        if (ok && !valid_link(link, generation)) {
            /* 模块此时可能处于数据输入模式，后续 AT 命令前必须重新初始化；禁止向复用了该编号的新连接发送旧应答。 */
            (void)recover_send_locked();
            ok = false;
        }
        if (ok) {
            ack = transfer_locked(bytes, amount, 2000U, REPLY_SEND, false, link, generation);
            ok = ack == ACK_SEND_OK && valid_link(link, generation);
            if (send_debt != 0U) {
                (void)recover_send_locked();
            }
        }
        bytes += amount;
        length -= amount;
    }
    unlock_commands();
    return ok;
}

/**
 * 功能：仅关闭仍匹配指定代次的服务器连接。
 * 参数：link，服务器连接编号；generation，需要保持匹配的连接代次。
 * 返回：关闭命令成功返回 true，否则返回 false。
 */
bool esp_at_server_close(uint8_t link, uint32_t generation) {
    if (!lock_commands()) {
        return false;
    }
    bool ok = valid_link(link, generation);
    if (ok) {
        (void)snprintf(command_buffer, sizeof command_buffer, "AT+CIPCLOSE=%u\r\n", (unsigned)link);
        ok = transfer_locked(command_buffer, (uint16_t)strlen(command_buffer), 2000U, REPLY_OK,
                             true, link, generation) == ACK_OK;
        taskENTER_CRITICAL();
        if (ok && server_links[link].generation == generation) {
            server_links[link].active = false;
            server_links[link].pending = 0U;
        }
        taskEXIT_CRITICAL();
    }
    unlock_commands();
    return ok;
}
