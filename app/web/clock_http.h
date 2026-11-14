#ifndef CLOCK_HTTP_H
#define CLOCK_HTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CLOCK_HTTP_TARGET_CAPACITY 64u
#define CLOCK_HTTP_BODY_MAX 384u
#define CLOCK_HTTP_LINE_CAPACITY 256u
#define CLOCK_HTTP_HEADERS_MAX 1536u

typedef enum {
    CLOCK_HTTP_NEED_MORE = 0,
    CLOCK_HTTP_READY,
    CLOCK_HTTP_BAD_REQUEST,
    CLOCK_HTTP_TOO_LARGE,
    CLOCK_HTTP_METHOD_NOT_ALLOWED
} clock_http_status_t;

typedef enum { CLOCK_HTTP_GET = 0, CLOCK_HTTP_POST } clock_http_method_t;

/* 每个连接只处理一个请求；公开请求字段仅在 READY 状态有效。
 * 终止错误保持不变；头部字节数包含请求行及回车换行。正文和目标以空字符结尾，
 * body_length 不包含结束符。重复的分帧或安全相关头部以及所有 Transfer-Encoding
 * 均被拒绝。此层允许省略 Host；路由和授权由调用者负责。 */
/** 保存单个连接的一次有界增量 HTTP 请求解析状态。 */
typedef struct {
    char target[CLOCK_HTTP_TARGET_CAPACITY]; // 以空字符结尾的请求目标
    char body[CLOCK_HTTP_BODY_MAX + 1u];     // 以空字符结尾的请求正文
    char line[CLOCK_HTTP_LINE_CAPACITY];     // 当前请求行或请求头行的工作缓冲区
    uint16_t body_length;                    // 已接收正文字节数，不含结束符
    uint16_t content_length;                 // 请求头声明的正文字节数
    uint16_t header_bytes;                   // 请求行和请求头累计字节数，包含回车换行
    uint16_t line_length;                    // 当前行已存入缓冲区的字节数
    clock_http_status_t status;              // 当前解析结果，终止错误保持不变
    clock_http_method_t method;              // 已识别的 GET 或 POST 请求方法
    uint8_t state;                           // 当前处于请求行、请求头或正文阶段
    uint8_t seen_headers;                    // 已经出现的关键请求头位掩码
    bool has_clock_request;                  // 是否收到值为 1 的 X-Clock-Request 请求头
    bool form_content_type;                  // 正文是否声明为表单编码类型
    bool line_overflow;                      // 当前请求头行是否超出工作缓冲区
    bool line_colon;                         // 当前请求头名称后是否已出现冒号
    bool saw_cr;                             // 是否已接收等待换行配对的回车字符
} clock_http_parser_t;

void clock_http_init(clock_http_parser_t *parser);
clock_http_status_t clock_http_feed(clock_http_parser_t *parser, const uint8_t *data,
                                    size_t length);

#endif
