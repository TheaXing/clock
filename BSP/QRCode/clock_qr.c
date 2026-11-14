#include "clock_qr.h"
#include "qrcodegen.h"
#include <string.h>

_Static_assert(CLOCK_QR_BUFFER_SIZE == qrcodegen_BUFFER_LEN_FOR_VERSION(4), "QR buffer budget");

/** 功能：以固定版本四、中纠错编码有界字节文本；参数：文本及输出位图；返回：成功为真。 */
bool clock_qr_encode(const char *text, clock_qr_t *output) {
    if (output == NULL) {
        return false;
    }
    memset(output, 0, sizeof *output);
    if (text == NULL) {
        return false;
    }
    size_t length = 0U;
    while (length <= CLOCK_QR_TEXT_LIMIT && text[length] != '\0') {
        ++length;
    }
    if (length == 0U || length > CLOCK_QR_TEXT_LIMIT) {
        return false;
    }
    uint8_t temporary[CLOCK_QR_BUFFER_SIZE] = {0};
    memcpy(temporary, text, length);
    return qrcodegen_encodeBinary(temporary, length, output->data, qrcodegen_Ecc_MEDIUM, 4, 4,
                                  qrcodegen_Mask_3, false);
}

/** 功能：读取经过成功编码的二维码尺寸；参数：二维码；返回：模块边长，空或无效时为零。 */
unsigned clock_qr_size(const clock_qr_t *qr) {
    return qr != NULL && qr->data[0] == CLOCK_QR_SIZE ? CLOCK_QR_SIZE : 0U;
}

/** 功能：读取有界模块颜色；参数：二维码、横纵模块坐标；返回：黑色为真，白色或越界为假。 */
bool clock_qr_module(const clock_qr_t *qr, unsigned x, unsigned y) {
    if (clock_qr_size(qr) == 0U || x >= CLOCK_QR_SIZE || y >= CLOCK_QR_SIZE) {
        return false;
    }
    return qrcodegen_getModule(qr->data, (int)x, (int)y);
}
