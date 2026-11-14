#ifndef OTA_HTTP_H
#define OTA_HTTP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** 保存单次严格 HTTP Range 响应的增量解析状态。 */
typedef struct {
    char header[2049]; // 有界 HTTP 头缓冲区
    size_t header_used; // 已收到的头字节数
    size_t received; // 已复制的正文字节数
    uint32_t start; // 请求的起始偏移
    uint32_t length; // 请求的精确长度
    uint32_t total; // 响应中的完整包大小
    uint32_t expected_total; // 已知的包大小，零表示首次未知
    char etag[67]; // 含双引号的强摘要标识
    char expected_etag[67]; // 已固定的包身份，空表示首次获取
    uint8_t *body; // 调用方提供的正文缓冲区
    bool headers_done; // 是否已通过全部头部校验
    bool complete; // 是否已精确收齐正文
    bool failed; // 是否存在不可恢复的解析错误
} ota_http_t;

void ota_http_init(ota_http_t *parser, uint32_t start, uint32_t length,
                   uint32_t total, const char *etag, uint8_t *body);
bool ota_http_feed(ota_http_t *parser, const void *bytes, size_t length);
#endif
