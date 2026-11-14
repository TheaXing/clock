#include "onenet_service.h"
#include "onenet_config.h"
#include "onenet_ca.h"
#include "onenet_model.h"
#include "alarm_engine.h"
#include "alarm_service.h"
#include "esp_at.h"
#include "esp_mqtt.h"
#include "clock_console.h"
#include "FreeRTOS.h"
#include "task.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define PREFIX "$sys/" CLOCK_ONENET_PRODUCT_ID "/" CLOCK_ONENET_DEVICE_NAME "/thing/property/"
static const char topic_set[] = PREFIX "set", topic_get[] = PREFIX "get";
static const char topic_set_reply[] = PREFIX "set_reply", topic_get_reply[] = PREFIX "get_reply";
static const char topic_post[] = PREFIX "post", topic_post_reply[] = PREFIX "post/reply";
#define CACHE_COUNT 4U
#define POST_COUNT 12U
#define COMMAND_MAX_AGE_MS 2500U

/** 缓存云端设置命令，支持去重、异步保存和延迟应答。 */
typedef struct {
    onenet_set_request_t request; // 原始规范化补丁及其合并结果
    uint32_t local_id;            // 本地闹钟服务分配的请求编号
    unsigned code;                // 应答平台的结果状态码
    const char *message;          // 应答平台的固定结果说明
    bool used;                    // 当前记录是否占用
    bool pending;                 // 是否等待本地保存完成
    bool reply_due;               // 是否需要发送最终应答
} command_cache_t;
/** 记录等待平台确认的一次属性上报。 */
typedef struct {
    uint32_t id;      // 本次上报的数字消息编号
    uint32_t sent_ms; // 发送时的单调毫秒时间
    uint16_t mask;    // 本次发送的属性位掩码
    bool used;        // 是否仍等待平台确认
} pending_post_t;

/** 保存 OneNET 连接、命令缓存、上报和退避状态。 */
static struct {
    command_cache_t cache[CACHE_COUNT]; // 设置请求去重和应答缓存
    pending_post_t posts[POST_COUNT];   // 等待平台确认的上报记录
    uint32_t next_post;                 // 最近分配的数字上报编号
    uint32_t last_attempt;              // 上次连接尝试的单调毫秒时刻
    uint32_t retry_ms;                  // 连接失败后的重试间隔，单位为毫秒
    uint32_t last_report;               // 上次发布属性的单调毫秒时刻
    uint32_t last_periodic;             // 上次触发周期遥测的单调毫秒时刻
    uint32_t last_revision;             // 最近已经观察到的配置代数
    uint32_t blocked_at[13];            // 各属性被平台拒绝的单调毫秒时刻
    uint16_t blocked;                   // 处于拒绝退避期的属性位掩码
    uint16_t reports;                   // 等待发送的属性位掩码
    unsigned cache_cursor;              // 查找可复用命令缓存槽位的起点
    bool initialized;                   // 服务是否已初始化
    bool online;                        // 服务是否已连接并完成主题订阅
    bool attempted;                     // 是否已经尝试连接
    bool wifi;                          // 最近获知的 Wi-Fi 连接状态
    bool failed;                        // 最近一次连接流程是否失败
    bool missing_logged;                // 是否已记录缺失或无效配置提示
    bool waiting_time_logged;           // 是否已记录等待 ESP 校时提示
    bool expired_logged;                // 是否已记录令牌过期提示
} cloud;
/** 保存传感器任务提供的室内温湿度快照。 */
static struct {
    float temperature; // 室内摄氏温度
    float humidity;    // 室内相对湿度，单位为百分比
    bool valid;        // 温湿度值是否有限且湿度在有效范围内
} indoor;
#if defined(__GNUC__) && defined(__arm__)
#define CLOUD_WORK_RAM __attribute__((section(".clock_onenet_ram"),aligned(4)))
#else
#define CLOUD_WORK_RAM
#endif
/** 保存 OneNET 网络任务专用的输出、接收分片和拼接工作区。 */
static struct {
    char output[ESP_MQTT_PAYLOAD_CAPACITY + 1U];      // 协议应答和上报的共享输出缓冲区
    char partial[ESP_MQTT_PAYLOAD_CAPACITY + 1U];     // 跨接收块拼接的 JSON 封套
    char partial_topic[ESP_MQTT_TOPIC_CAPACITY + 1U]; // 当前拼接封套所属主题
    esp_mqtt_message_t incoming;                      // 最近轮询收到的 MQTT 消息
    size_t partial_length;                            // 当前拼接封套已累积的字节数
    uint32_t partial_started;                         // 当前封套首片接收的单调毫秒时刻
} work CLOUD_WORK_RAM;

/**
 * 功能：检查 OneNET 令牌、产品、设备、服务器及 TLS 端口配置是否完整。
 * 参数：无。
 * 返回：所需配置有效且端口为 8883 时返回 true，否则返回 false。
 */
bool onenet_service_configured(void) {
    return CLOCK_ONENET_TOKEN[0] && CLOCK_ONENET_PRODUCT_ID[0] && CLOCK_ONENET_DEVICE_NAME[0] &&
           CLOCK_ONENET_MQTT_HOST[0] && CLOCK_ONENET_MQTT_PORT == 8883U;
}
/**
 * 功能：检查服务在线标志和底层 MQTT 实际连接是否同时有效。
 * 参数：无。
 * 返回：服务在线且传输层已连接时返回 true，否则返回 false。
 */
bool onenet_service_online(void) {
    return cloud.online && esp_at_mqtt_connected();
}
/**
 * 功能：检查在线服务是否有待保存、待应答的命令或未消费 MQTT 数据。
 * 参数：无。
 * 返回：存在待处理工作时返回 true，否则返回 false。
 */
bool onenet_service_busy(void) {
    if (!onenet_service_online()) {
        return false;
    }
    for (unsigned i = 0; i < CACHE_COUNT; ++i) {
        if (cloud.cache[i].used && (cloud.cache[i].pending || cloud.cache[i].reply_due)) {
            return true;
        }
    }
    return esp_at_mqtt_pending();
}
/**
 * 功能：在应用任务启动前清空服务工作区和传感器快照，并设置重试及首次上报状态。
 * 参数：无。
 * 返回：无。
 */
void onenet_service_init(void) {

    memset(&cloud, 0, sizeof cloud);
    memset(&work, 0, sizeof work);
    memset(&indoor, 0, sizeof indoor);
    cloud.retry_ms = 5000U;
    cloud.next_post = 100U;
    cloud.reports = ONENET_PROPERTY_ALL;
    cloud.initialized = true;
}
/**
 * 功能：校验并在短临界区内复制室内温湿度快照。
 * 参数：temperature，摄氏温度；humidity，相对湿度百分比。
 * 返回：无；非有限值或湿度越界时将快照标为无效并清零数值。
 */
void onenet_service_sensor(float temperature, float humidity) {
    bool valid = isfinite(temperature) && isfinite(humidity) && humidity >= 0 && humidity <= 100;
    taskENTER_CRITICAL();
    indoor.temperature = valid ? temperature : 0.0f;
    indoor.humidity = valid ? humidity : 0.0f;
    indoor.valid = valid;
    taskEXIT_CRITICAL();
}
/**
 * 功能：记录 MQTT 阶段故障，清空分片并按上限递增重连等待时间。
 * 参数：stage，固定的故障阶段说明。
 * 返回：无。
 */
static void network_failed(const char *stage) {
    esp_mqtt_diagnostic_t diagnostic = esp_at_mqtt_diagnostic();
    clock_log(
        "[ONENET] %s failed, transport error %u; step=%s; reply=%s; esp=0x%08lX; retry pending\r\n",
        stage, (unsigned)esp_at_mqtt_last_error(), diagnostic.step, diagnostic.reply,
        (unsigned long)diagnostic.at_error);
    cloud.online = false;
    cloud.failed = true;
    cloud.attempted = true;
    work.partial_length = 0U;
    cloud.last_attempt = (uint32_t)xTaskGetTickCount();
    if (cloud.retry_ms < 60000U) {
        cloud.retry_ms = cloud.retry_ms > 30000U ? 60000U : cloud.retry_ms * 2U;
    }
    if (esp_at_is_ready() && esp_at_mqtt_connected()) {
        (void)esp_at_mqtt_disconnect();
    }
}

/**
 * 功能：在网络和时间就绪后建立经过证书校验的 MQTT 连接并订阅所需主题。
 * 参数：wifi_connected，Wi-Fi 连接状态；esp_time_ready，ESP 校时完成标志；now_ms，当前单调毫秒时间。
 * 返回：无。
 */
void onenet_service_network(bool wifi_connected, bool esp_time_ready, uint32_t now_ms) {
    if (!cloud.initialized || !onenet_service_configured()) {
        if (cloud.initialized && !cloud.missing_logged) {
            clock_log_line(
                "[ONENET] Token missing or configuration invalid; local functions continue");
            cloud.missing_logged = true;
        }
        return;
    }
    if (!wifi_connected) {
        cloud.online = false;
        cloud.wifi = false;
        if (!cloud.failed) {
            cloud.attempted = false;
        }
        work.partial_length = 0U;
        return;
    }
    cloud.wifi = true;
    if (cloud.online && !esp_at_mqtt_connected()) {
        cloud.online = false;
        cloud.attempted = false;
        work.partial_length = 0U;
        clock_log_line("[ONENET] MQTT disconnected; reconnect pending");
    }
    if (cloud.online) {
        return;
    }
    if (!esp_time_ready) {
        if (!cloud.waiting_time_logged) {
            clock_log_line("[ONENET] Waiting for ESP SNTP before verified TLS");
            cloud.waiting_time_logged = true;
        }
        return;
    }
    if (!esp_at_is_ready() || (cloud.attempted && now_ms - cloud.last_attempt < cloud.retry_ms)) {
        return;
    }
#if CLOCK_ONENET_TOKEN_EXPIRES != 0
    alarm_service_snapshot_t snapshot;
    uint32_t seconds;
    if (alarm_service_snapshot(&snapshot) && snapshot.time_valid &&
        alarm_calendar_to_seconds(&snapshot.now, &seconds) &&
        (uint64_t)seconds + UINT64_C(946656000) >= CLOCK_ONENET_TOKEN_EXPIRES) {
        if (!cloud.expired_logged) {
            clock_log_line("[ONENET] Token expired; regenerate local Token, rebuild and flash");
            cloud.expired_logged = true;
        }
        return;
    }
#endif
    cloud.attempted = true;
    cloud.last_attempt = now_ms;
    /* OneNET 当前专用自签名 CA 或服务器证书不包含产品 DNS 名称。AT4.1 将非空 SNI
     * 用于证书名称匹配，因此此处显式省略；scheme3 仍校验证书 CA 和有效期。
     * 通用驱动对空指针或默认 SNI 仍保留主机名校验。 */
    const esp_mqtt_config_t config = {CLOCK_ONENET_MQTT_HOST,   (uint16_t)CLOCK_ONENET_MQTT_PORT,
                                      CLOCK_ONENET_DEVICE_NAME, CLOCK_ONENET_PRODUCT_ID,
                                      CLOCK_ONENET_TOKEN,       onenet_ca_pem,
                                      onenet_ca_pem_length,     ""};
    clock_log("[ONENET] Connecting TLS %s:%u; product %s; device %s\r\n", config.host,
              (unsigned)config.port, config.username, config.client_id);
    clock_log_line("[ONENET] TLS policy: OneNET CA/date verification; no SNI or DNS-name check");
    if (!esp_at_mqtt_configure(&config)) {
        network_failed("TLS/credential configuration");
        return;
    }
    if (!esp_at_mqtt_connect(20000U)) {
        network_failed("MQTT connection");
        return;
    }
    if (!esp_at_mqtt_subscribe(topic_set, 1U) || !esp_at_mqtt_subscribe(topic_get, 1U) ||
        !esp_at_mqtt_subscribe(topic_post_reply, 1U)) {
        network_failed("MQTT subscription");
        return;
    }
    cloud.online = true;
    cloud.failed = false;
    cloud.retry_ms = 5000U;
    cloud.waiting_time_logged = false;
    cloud.reports = ONENET_PROPERTY_ALL;
    cloud.blocked = 0U;
    work.partial_length = 0U;
    memset(cloud.posts, 0, sizeof cloud.posts);
    clock_log_line("[ONENET] MQTT connected and subscribed; property reports pending");
}

/**
 * 功能：将共享输出缓冲区的内容发布到指定主题，并处理发送故障。
 * 参数：topic，MQTT 目标主题；length，输出缓冲区中待发送的字节数。
 * 返回：非空消息发布成功返回 true，否则返回 false。
 */
static bool publish(const char *topic, size_t length) {
    if (!length) {
        return false;
    }
    if (!esp_at_mqtt_publish(topic, work.output, length, 0U, false)) {
        network_failed("MQTT publish");
        return false;
    }
    return true;
}
/**
 * 功能：编码并发布带消息编号、状态码和说明的协议应答。
 * 参数：topic，应答主题；id，请求编号；code，应答状态码；message，应答说明。
 * 返回：编码和发布成功返回 true，否则返回 false。
 */
static bool reply(const char *topic, const char *id, unsigned code, const char *message) {
    return publish(topic,
                   onenet_model_encode_reply(id, code, message, work.output, sizeof work.output));
}
/**
 * 功能：收集本地异步保存结果，生成云端应答并标记成功配置需要上报。
 * 参数：无。
 * 返回：无。
 */
static void collect_results(void) {
    for (unsigned i = 0; i < CACHE_COUNT; ++i) {
        command_cache_t *entry = &cloud.cache[i];
        if (!entry->used || !entry->pending) {
            continue;
        }
        alarm_remote_result_t result = alarm_service_remote_result(entry->local_id);
        if (result == ALARM_REMOTE_PENDING) {
            continue;
        }
        entry->pending = false;
        entry->reply_due = true;
        entry->code = result == ALARM_REMOTE_SAVED ? 200U : 500U;
        entry->message = result == ALARM_REMOTE_SAVED    ? "success"
                         : result == ALARM_REMOTE_FAILED ? "save_failed"
                                                         : "result_unknown";
        if (result == ALARM_REMOTE_SAVED) {
            cloud.reports |=
                ONENET_PROPERTY_ALARM(entry->request.index) | ONENET_PROPERTY_CONFIG_REVISION;
        }
        clock_log("[ONENET] Alarm %u request %s %s\r\n", (unsigned)entry->request.index + 1U,
                  entry->request.id, entry->message);
    }
}
/**
 * 功能：查找并尝试发送一个已完成且待应答的设置请求。
 * 参数：无。
 * 返回：发现并处理一个待应答项返回 true，无待应答项返回 false。
 */
static bool send_due_reply(void) {
    for (unsigned i = 0; i < CACHE_COUNT; ++i) {
        command_cache_t *entry = &cloud.cache[i];
        if (entry->used && !entry->pending && entry->reply_due) {
            if (reply(topic_set_reply, entry->request.id, entry->code, entry->message)) {
                entry->reply_due = false;
            }
            return true;
        }
    }
    return false;
}

/**
 * 功能：校验设置请求、去重和有效期后提交显式闹钟补丁，并缓存异步请求。
 * 参数：data，请求字节；length，输入长度；received_ms，首片接收时刻；now_ms，当前单调毫秒时间。
 * 返回：无。
 */
static void handle_set(const char *data, size_t length, uint32_t received_ms, uint32_t now_ms) {
    alarm_service_snapshot_t snapshot;
    onenet_set_request_t request;
    if (!alarm_service_snapshot(&snapshot)) {
        alarm_config_defaults(&snapshot.config);
        (void)onenet_model_decode_set(data, length, &snapshot.config, &request);
        if (request.id[0]) {
            (void)reply(topic_set_reply, request.id, 503U, "alarm_unavailable");
        }
        return;
    }
    onenet_model_status_t status =
        onenet_model_decode_set(data, length, &snapshot.config, &request);
    for (unsigned i = 0; i < CACHE_COUNT; ++i) {
        if (cloud.cache[i].used && strcmp(cloud.cache[i].request.id, request.id) == 0) {
            command_cache_t *entry = &cloud.cache[i];
            /* 重放校验必须使用原始已提交补丁的合并上下文：补丁成功后，省略的日期字段可能已经变化。 */
            alarm_config_t original = snapshot.config;
            original.entries[entry->request.index] = entry->request.entry;
            onenet_set_request_t replay;
            if (onenet_model_decode_set(data, length, &original, &replay) != ONENET_MODEL_OK ||
                !onenet_model_same_patch(&entry->request, &replay)) {
                (void)reply(topic_set_reply, request.id, 409U, "request_id_conflict");
            } else if (!entry->pending) {
                entry->reply_due = true;
            }
            return;
        }
    }
    if (status != ONENET_MODEL_OK) {
        if (request.id[0]) {
            (void)reply(topic_set_reply, request.id, 400U, "invalid_alarm_request");
        }
        return;
    }
    if (now_ms - received_ms >= COMMAND_MAX_AGE_MS) {
        (void)reply(topic_set_reply, request.id, 408U, "request_expired");
        return;
    }
    unsigned index = CACHE_COUNT;
    for (unsigned i = 0; i < CACHE_COUNT; ++i) {
        unsigned candidate = (cloud.cache_cursor + i) % CACHE_COUNT;
        if (!cloud.cache[candidate].used ||
            (!cloud.cache[candidate].pending && !cloud.cache[candidate].reply_due)) {
            index = candidate;
            break;
        }
    }
    if (index == CACHE_COUNT) {
        (void)reply(topic_set_reply, request.id, 409U, "busy");
        return;
    }
    uint32_t id = 0;
    alarm_remote_submit_t accepted = alarm_service_remote_submit_patch(
        request.index, &request.entry, request.fields_mask, snapshot.state_version, &id);
    if (accepted != ALARM_REMOTE_ACCEPTED) {
        (void)reply(topic_set_reply, request.id,
                    accepted == ALARM_REMOTE_INVALID       ? 400U
                    : accepted == ALARM_REMOTE_UNAVAILABLE ? 503U
                                                           : 409U,
                    "busy_or_conflict");
        return;
    }
    command_cache_t *entry = &cloud.cache[index];
    memset(entry, 0, sizeof *entry);
    entry->request = request;
    entry->local_id = id;
    entry->used = true;
    entry->pending = true;
    cloud.cache_cursor = (index + 1U) % CACHE_COUNT;
}
/**
 * 功能：读取闹钟服务和传感器快照，组成属性编码所需的值集合。
 * 参数：out，接收属性值，配置指针引用 snapshot；snapshot，承载闹钟快照的调用者存储区。
 * 返回：服务快照可用返回 true，否则返回 false。
 */
static bool values(onenet_model_values_t *out, alarm_service_snapshot_t *snapshot) {
    if (!alarm_service_snapshot(snapshot)) {
        return false;
    }
    memset(out, 0, sizeof *out);
    out->config = &snapshot->config;
    out->config_revision = snapshot->revision;
    out->ringing_mask = snapshot->ringing_mask;
    taskENTER_CRITICAL();
    out->temperature = indoor.temperature;
    out->humidity = indoor.humidity;
    out->sensor_valid = indoor.valid;
    taskEXIT_CRITICAL();
    return true;
}
/**
 * 功能：解析属性查询并返回快照，传感器或容量不足时应答相应错误。
 * 参数：data，请求字节；length，输入长度。
 * 返回：无。
 */
static void handle_get(const char *data, size_t length) {
    onenet_get_request_t request;
    if (onenet_model_decode_get(data, length, &request) != ONENET_MODEL_OK) {
        if (request.id[0]) {
            (void)reply(topic_get_reply, request.id, 400U, "invalid_get_request");
        }
        return;
    }
    alarm_service_snapshot_t snapshot;
    onenet_model_values_t current;
    if (!values(&current, &snapshot)) {
        (void)reply(topic_get_reply, request.id, 503U, "alarm_unavailable");
        return;
    }
    uint16_t mask = request.property_mask;
    if (!current.sensor_valid &&
        (mask & (ONENET_PROPERTY_TEMPERATURE | ONENET_PROPERTY_HUMIDITY))) {
        (void)reply(topic_get_reply, request.id, 503U, "sensor_unavailable");
        return;
    }
    size_t n =
        onenet_model_encode_get_reply(request.id, &current, mask, work.output, sizeof work.output);
    if (n) {
        (void)publish(topic_get_reply, n);
    } else {
        (void)reply(topic_get_reply, request.id, 413U, "reply_too_large");
    }
}
/**
 * 功能：处理上报确认，释放匹配项，并对被拒绝的属性安排延迟重报。
 * 参数：data，应答字节；length，输入长度。
 * 返回：无。
 */
static void handle_post_reply(const char *data, size_t length) {
    char id[ONENET_MODEL_ID_SIZE];
    unsigned code;
    if (onenet_model_decode_reply(data, length, id, &code) != ONENET_MODEL_OK) {
        return;
    }
    for (unsigned i = 0; i < POST_COUNT; ++i) {
        if (cloud.posts[i].used) {
            char expected[14];
            snprintf(expected, sizeof expected, "%lu", (unsigned long)cloud.posts[i].id);
            if (strcmp(id, expected) != 0) {
                continue;
            }
            cloud.posts[i].used = false;
            if (code == 200U) {
                clock_log("[ONENET] Cloud accepted report %s\r\n", id);
            } else {
                clock_log(
                    "[ONENET] Cloud rejected report %s mask 0x%04X, code %u; check OneJSON property names/types\r\n",
                    id, (unsigned)cloud.posts[i].mask, code);
                cloud.reports |= cloud.posts[i].mask;
                cloud.blocked |= cloud.posts[i].mask;
                for (unsigned bit = 0; bit < 13U; ++bit) {
                    if (cloud.posts[i].mask & (1U << bit)) {
                        cloud.blocked_at[bit] = (uint32_t)xTaskGetTickCount();
                    }
                }
            }
            return;
        }
    }
}
/* 分帧扫描仅用于跨 ESP 接收块寻找完整 JSON 封套；执行任何操作前，严格模型解析器会校验全部令牌和类型。 */
/**
 * 功能：扫描跨接收分片的 JSON 封套边界，只判断完整性并限制嵌套深度。
 * 参数：data，输入字节；length，输入长度。
 * 返回：封套完整返回 1，尚未完整返回 0，边界或字符无效返回 -1。
 */
static int envelope(const char *data, size_t length) {
    unsigned depth = 0;
    bool quoted = false, escaped = false, started = false, complete = false;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)data[i];
        if (c == 0U) {
            return -1;
        }
        if (quoted) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                quoted = false;
            }
            continue;
        }
        if (c == ' ' || c == '\r' || c == '\n' || c == '\t') {
            continue;
        }
        if (complete) {
            return -1;
        }
        if (!started) {
            if (c != '{') {
                return -1;
            }
            started = true;
        }
        if (c == '"') {
            quoted = true;
        } else if (c == '{' || c == '[') {
            if (++depth > 16U) {
                return -1;
            }
        } else if (c == '}' || c == ']') {
            if (!depth) {
                return -1;
            }
            if (--depth == 0U) {
                complete = true;
            }
        }
    }
    return complete ? 1 : 0;
}
/**
 * 功能：判断接收块是否可作为具有明确编号的独立完整协议消息。
 * 参数：message，待检查的 MQTT 消息。
 * 返回：存在完整封套和无歧义编号时返回 true，否则返回 false。
 */
static bool independent_message(const esp_mqtt_message_t *message) {
    const char *data = (const char *)message->data;
    if (envelope(data, message->length) != 1) {
        return false;
    }
    if (strcmp(message->topic, topic_set) == 0) {
        alarm_config_t base;
        onenet_set_request_t request;
        alarm_config_defaults(&base);
        (void)onenet_model_decode_set(data, message->length, &base, &request);
        /* 严格 JSON 解析及无歧义顶层编号共同确认一个新封套；稍后再针对真实快照校验其补丁。 */
        return request.id[0] != '\0';
    }
    if (strcmp(message->topic, topic_get) == 0) {
        onenet_get_request_t request;
        (void)onenet_model_decode_get(data, message->length, &request);
        return request.id[0] != '\0';
    }
    char id[14];
    unsigned code;
    return onenet_model_decode_reply(data, message->length, id, &code) == ONENET_MODEL_OK;
}
/**
 * 功能：按主题和时间限制拼接 MQTT 分片，并将完整封套分派到协议处理函数。
 * 参数：message，本次接收的 MQTT 消息；now_ms，当前单调毫秒时间。
 * 返回：无。
 */
static void consume(const esp_mqtt_message_t *message, uint32_t now_ms) {
    if (strcmp(message->topic, topic_set) != 0 && strcmp(message->topic, topic_get) != 0 &&
        strcmp(message->topic, topic_post_reply) != 0) {
        return;
    }
    if (message->length > ESP_MQTT_PAYLOAD_CAPACITY) {
        return;
    }
    /* 同一主题上，具有明确编号的完整封套取代损坏前缀。ESP-AT 未提供任意交错的
     * 代理分片消息的边界元数据，因此对此类交错采用保守拒绝策略。 */
    if (work.partial_length && independent_message(message)) {
        work.partial_length = 0U;
    }
    if (work.partial_length && (strcmp(work.partial_topic, message->topic) != 0 ||
                                message->received_ms - work.partial_started > 1000U)) {
        work.partial_length = 0U;
    }
    if (!work.partial_length) {
        strcpy(work.partial_topic, message->topic);
        work.partial_started = message->received_ms;
    }
    if (work.partial_length + message->length > ESP_MQTT_PAYLOAD_CAPACITY) {
        work.partial_length = 0U;
        return;
    }
    memcpy(work.partial + work.partial_length, message->data, message->length);
    work.partial_length += message->length;
    int complete = envelope(work.partial, work.partial_length);
    if (complete == 0) {
        return;
    }
    size_t length = work.partial_length;
    work.partial_length = 0U;
    if (complete < 0) {
        return;
    }
    if (strcmp(work.partial_topic, topic_set) == 0) {
        handle_set(work.partial, length, work.partial_started, now_ms);
    } else if (strcmp(work.partial_topic, topic_get) == 0) {
        handle_get(work.partial, length);
    } else {
        handle_post_reply(work.partial, length);
    }
}

/**
 * 功能：收集变更和定时遥测，处理确认超时及退避，并发布一个可上报属性。
 * 参数：now_ms，当前单调毫秒时间。
 * 返回：无。
 */
static void report(uint32_t now_ms) {
    alarm_service_snapshot_t snapshot;
    onenet_model_values_t current;
    if (!values(&current, &snapshot)) {
        return;
    }
    if (snapshot.revision != cloud.last_revision) {
        cloud.last_revision = snapshot.revision;
        cloud.reports |= ONENET_PROPERTY_ALARMS | ONENET_PROPERTY_CONFIG_REVISION;
    }
    if (now_ms - cloud.last_periodic >= 10000U) {
        cloud.last_periodic = now_ms;
        cloud.reports |= ONENET_PROPERTY_TELEMETRY;
    }
    for (unsigned i = 0; i < POST_COUNT; ++i) {
        if (cloud.posts[i].used && now_ms - cloud.posts[i].sent_ms >= 10000U) {
            cloud.reports |= cloud.posts[i].mask;
            cloud.posts[i].used = false;
            clock_log_line("[ONENET] Property report not confirmed; retry pending");
        }
    }
    for (unsigned bit = 0; bit < 13U; ++bit) {
        if ((cloud.blocked & (1U << bit)) && now_ms - cloud.blocked_at[bit] >= 30000U) {
            cloud.blocked &= (uint16_t)~(1U << bit);
        }
    }
    uint16_t eligible = cloud.reports & (uint16_t)~cloud.blocked;
    if (!eligible || now_ms - cloud.last_report < 250U) {
        return;
    }
    unsigned post = POST_COUNT;
    for (unsigned i = 0; i < POST_COUNT; ++i) {
        if (!cloud.posts[i].used) {
            post = i;
            break;
        }
    }
    if (post == POST_COUNT) {
        return;
    }
    uint16_t mask = (uint16_t)(eligible & (uint16_t)(0U - eligible));
    if (!current.sensor_valid) {
        mask &= (uint16_t)~(ONENET_PROPERTY_TEMPERATURE | ONENET_PROPERTY_HUMIDITY);
        cloud.reports &= (uint16_t)~(ONENET_PROPERTY_TEMPERATURE | ONENET_PROPERTY_HUMIDITY);
    }
    if (!mask) {
        return;
    }
    if (++cloud.next_post == 0U) {
        cloud.next_post = 1U;
    }
    char id[14];
    snprintf(id, sizeof id, "%lu", (unsigned long)cloud.next_post);
    size_t n = onenet_model_encode_report(id, &current, mask, work.output, sizeof work.output);
    if (!n) {
        clock_log_line("[ONENET] Property serialization failed");
        return;
    }
    if (publish(topic_post, n)) {
        cloud.reports &= (uint16_t)~mask;
        cloud.last_report = now_ms;
        cloud.posts[post] = (pending_post_t){cloud.next_post, now_ms, mask, true};
    }
}

/**
 * 功能：在唯一网络任务内收集保存结果、优先发送应答并有界处理输入及属性上报。
 * 参数：now_ms，调用方时间，当前实现会在可能阻塞的 AT 调用之后重新读取系统时钟。
 * 返回：无。
 */
void onenet_service_poll(uint32_t now_ms) {
    (void)now_ms; /* 每次可能较慢的 AT 调用之后重新读取中断维护的时间基准。 */
    if (!cloud.initialized || !onenet_service_configured()) {
        return;
    }
    collect_results();
    if (!onenet_service_online()) {
        return;
    }
    if (send_due_reply()) {
        return;
    }
    for (unsigned budget = 0; budget < 3U; ++budget) {
        if (!esp_at_mqtt_poll(&work.incoming)) {
            break;
        }
        consume(&work.incoming, (uint32_t)xTaskGetTickCount());
        if (!onenet_service_online()) {
            return;
        }
        collect_results();
        if (send_due_reply()) {
            return;
        }
    }
    for (unsigned i = 0; i < CACHE_COUNT; ++i) {
        if (cloud.cache[i].used && cloud.cache[i].pending) {
            return;
        }
    }
    if (!esp_at_mqtt_pending()) {
        report((uint32_t)xTaskGetTickCount());
    }
}
