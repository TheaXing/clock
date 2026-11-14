#ifndef __IMAGE_H__
#define __IMAGE_H__

#include <stdint.h>

/** 描述 RGB565 图像尺寸与低字节在前的像素数据。 */
typedef struct {
    uint16_t width;      // 图像宽度，单位为像素
    uint16_t height;     // 图像高度，单位为像素
    const uint8_t *data; // 低字节在前的连续 RGB565 像素数据
} image_t;

extern const image_t img_star;
extern const image_t img_error;
extern const image_t img_wifi;
extern const image_t icon_duoyun;
extern const image_t icon_leizhenyu;
extern const image_t icon_qing;
extern const image_t icon_yueliang;
extern const image_t icon_yintian;
extern const image_t icon_zhongxue;
extern const image_t icon_zhongyu;
extern const image_t icon_wenduji;
extern const image_t icon_wifi;
extern const image_t icon_na;

#endif /* __IMAGE_H__ 头文件结束 */
