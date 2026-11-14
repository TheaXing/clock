#ifndef CLOCK_ONENET_MODEL_H
#define CLOCK_ONENET_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "alarm_types.h"

#define ONENET_MODEL_MAX_PAYLOAD 1460U
#define ONENET_MODEL_ID_SIZE 14U
#define ONENET_PROPERTY_ALARM(index) ((uint16_t)(1U << (index)))
#define ONENET_PROPERTY_ALARMS ((uint16_t)0x00ffU)
#define ONENET_PROPERTY_TEMPERATURE ((uint16_t)0x0100U)
#define ONENET_PROPERTY_HUMIDITY ((uint16_t)0x0200U)
#define ONENET_PROPERTY_SENSOR_VALID ((uint16_t)0x0400U)
#define ONENET_PROPERTY_CONFIG_REVISION ((uint16_t)0x0800U)
#define ONENET_PROPERTY_RINGING_MASK ((uint16_t)0x1000U)
#define ONENET_PROPERTY_ALL ((uint16_t)0x1fffU)
#define ONENET_PROPERTY_TELEMETRY ((uint16_t)0x1f00U)

/* 字段位依次对应 enabled、hour、minute、repeat_mask、year、month、day、
 * ring_seconds、snooze_seconds。 */
typedef enum {
    ONENET_MODEL_OK,
    ONENET_MODEL_INVALID_JSON,
    ONENET_MODEL_INVALID_REQUEST,
    ONENET_MODEL_UNKNOWN_PROPERTY,
    ONENET_MODEL_INVALID_VALUE,
    ONENET_MODEL_MULTIPLE_ALARMS,
    ONENET_MODEL_TOO_LARGE
} onenet_model_status_t;

/** 保存一个经过解析和快照合并的单闹钟设置补丁。 */
typedef struct {
    char id[ONENET_MODEL_ID_SIZE]; // 平台消息编号，最多 13 位十进制数字
    uint8_t index;                 // 从零开始的本地闹钟编号
    alarm_entry_t entry;           // 与当前快照合并后的完整有效条目
    uint16_t fields_mask;          // 仅标记本命令显式包含的字段
    uint32_t fingerprint;          // 规范化补丁指纹，不作为凭据
} onenet_set_request_t;

/** 保存属性查询消息编号及请求的属性集合。 */
typedef struct {
    char id[ONENET_MODEL_ID_SIZE]; // 平台消息编号，最多 13 位十进制数字
    uint16_t property_mask;        // 请求读取的属性位掩码
} onenet_get_request_t;

/** 提供 OneNET 上报和属性应答所需的本地值快照。 */
typedef struct {
    const alarm_config_t *config; // 所选闹钟属性使用的只读配置指针
    float temperature;            // 室内摄氏温度
    float humidity;               // 室内相对湿度，单位为百分比
    bool sensor_valid;            // 温湿度快照有效标志
    uint32_t config_revision;     // 已验证的持久化配置代数
    uint8_t ringing_mask;         // 正在响铃的闹钟位掩码
} onenet_model_values_t;

/* 不进行动态分配、任务调用或写入，也不假定输入以空字符结尾。语义校验失败时仍
 * 保留有效封套编号；无效 JSON 或歧义、无效编号会令 id 为空。version 可省略，
 * OneJSON 默认版本为 1.0；提供时必须为字符串 1.0。 */
onenet_model_status_t onenet_model_decode_set(const char *data, size_t length,
                                              const alarm_config_t *current,
                                              onenet_set_request_t *request);
onenet_model_status_t onenet_model_decode_get(const char *data, size_t length,
                                              onenet_get_request_t *request);
/* 解析平台应答，不记录不可信的 msg 内容。 */
onenet_model_status_t onenet_model_decode_reply(const char *data, size_t length,
                                                char id[ONENET_MODEL_ID_SIZE], unsigned *code);

/* 指纹不是安全令牌。编号重复时还须比较 index、fields_mask 及掩码选中的条目字段。
 * 省略的快照字段发生变化不会影响规范化补丁指纹。 */
bool onenet_model_same_patch(const onenet_set_request_t *left, const onenet_set_request_t *right);

/* POST 属性值采用 OneJSON 的 value 包装；GET 应答包含 id、code、msg、data，
 * data 使用原始属性值；SET 应答包含 id、code、msg。所有编码器返回不含空结束符的
 * 字节数；失败返回 0 并清空输出。容量包含结束符，输出不超过 MAX_PAYLOAD。
 * 较大的上报掩码须拆分为多次发布；编号必须包含 1～13 位十进制数字。 */
size_t onenet_model_encode_report(const char *id, const onenet_model_values_t *values,
                                  uint16_t property_mask, char *out, size_t capacity);
size_t onenet_model_encode_get_reply(const char *id, const onenet_model_values_t *values,
                                     uint16_t property_mask, char *out, size_t capacity);
size_t onenet_model_encode_reply(const char *id, unsigned code, const char *message, char *out,
                                 size_t capacity);

#endif
