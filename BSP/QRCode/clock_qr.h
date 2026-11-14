#ifndef CLOCK_QR_H
#define CLOCK_QR_H

#include <stdbool.h>
#include <stdint.h>

#define CLOCK_QR_VERSION 4U
#define CLOCK_QR_SIZE 33U
#define CLOCK_QR_BUFFER_SIZE 138U
#define CLOCK_QR_TEXT_LIMIT 62U

/** 保存固定版本四二维码，不包含静区；每个模块占一个位。 */
typedef struct {
    uint8_t data[CLOCK_QR_BUFFER_SIZE]; // 编码器使用的尺寸及位图缓冲区
} clock_qr_t;

/* 文本上限为六十二字节，固定中等级纠错；失败时清空输出。 */
bool clock_qr_encode(const char *text, clock_qr_t *output);
unsigned clock_qr_size(const clock_qr_t *qr);
bool clock_qr_module(const clock_qr_t *qr, unsigned x, unsigned y);

#endif
