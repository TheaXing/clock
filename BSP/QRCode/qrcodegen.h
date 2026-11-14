/* 源自 Nayuki 二维码编码器 v1.8.0，提交 720f62bddb7226106071d4728c292cb1df519ceb。
 * 原作者版权及 MIT 许可完整保存在同目录 LICENSE；仅注释和排版作本地适配，算法与原接口保持一致。 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum qrcodegen_Ecc {

    qrcodegen_Ecc_LOW = 0,
    qrcodegen_Ecc_MEDIUM,
    qrcodegen_Ecc_QUARTILE,
    qrcodegen_Ecc_HIGH,
};

enum qrcodegen_Mask {

    qrcodegen_Mask_AUTO = -1,

    qrcodegen_Mask_0 = 0,
    qrcodegen_Mask_1,
    qrcodegen_Mask_2,
    qrcodegen_Mask_3,
    qrcodegen_Mask_4,
    qrcodegen_Mask_5,
    qrcodegen_Mask_6,
    qrcodegen_Mask_7,
};

enum qrcodegen_Mode {
    qrcodegen_Mode_NUMERIC = 0x1,
    qrcodegen_Mode_ALPHANUMERIC = 0x2,
    qrcodegen_Mode_BYTE = 0x4,
    qrcodegen_Mode_KANJI = 0x8,
    qrcodegen_Mode_ECI = 0x7,
};

/** 描述二维码中的一段数据及其编码方式。 */
struct qrcodegen_Segment {

    enum qrcodegen_Mode mode; // 段数据编码模式

    int numChars; // 原始字符或字节数量

    uint8_t *data; // 按高位在前存储的数据位

    int bitLength; // 有效数据位数量
};

#define qrcodegen_VERSION_MIN 1
#define qrcodegen_VERSION_MAX 40

#define qrcodegen_BUFFER_LEN_FOR_VERSION(n) ((((n) * 4 + 17) * ((n) * 4 + 17) + 7) / 8 + 1)

#define qrcodegen_BUFFER_LEN_MAX qrcodegen_BUFFER_LEN_FOR_VERSION(qrcodegen_VERSION_MAX)

bool qrcodegen_encodeText(const char *text, uint8_t tempBuffer[], uint8_t qrcode[],
                          enum qrcodegen_Ecc ecl, int minVersion, int maxVersion,
                          enum qrcodegen_Mask mask, bool boostEcl);

bool qrcodegen_encodeBinary(uint8_t dataAndTemp[], size_t dataLen, uint8_t qrcode[],
                            enum qrcodegen_Ecc ecl, int minVersion, int maxVersion,
                            enum qrcodegen_Mask mask, bool boostEcl);

bool qrcodegen_encodeSegments(const struct qrcodegen_Segment segs[], size_t len,
                              enum qrcodegen_Ecc ecl, uint8_t tempBuffer[], uint8_t qrcode[]);

bool qrcodegen_encodeSegmentsAdvanced(const struct qrcodegen_Segment segs[], size_t len,
                                      enum qrcodegen_Ecc ecl, int minVersion, int maxVersion,
                                      enum qrcodegen_Mask mask, bool boostEcl, uint8_t tempBuffer[],
                                      uint8_t qrcode[]);

bool qrcodegen_isNumeric(const char *text);

bool qrcodegen_isAlphanumeric(const char *text);

size_t qrcodegen_calcSegmentBufferSize(enum qrcodegen_Mode mode, size_t numChars);

struct qrcodegen_Segment qrcodegen_makeBytes(const uint8_t data[], size_t len, uint8_t buf[]);

struct qrcodegen_Segment qrcodegen_makeNumeric(const char *digits, uint8_t buf[]);

struct qrcodegen_Segment qrcodegen_makeAlphanumeric(const char *text, uint8_t buf[]);

struct qrcodegen_Segment qrcodegen_makeEci(long assignVal, uint8_t buf[]);

int qrcodegen_getSize(const uint8_t qrcode[]);

bool qrcodegen_getModule(const uint8_t qrcode[], int x, int y);

#ifdef __cplusplus
}
#endif
