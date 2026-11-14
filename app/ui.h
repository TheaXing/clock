#ifndef __APP_UI_H__
#define __APP_UI_H__

#include <stdint.h>
#include "font.h"
#include "image.h"

#define UI_WIDTH 240
#define UI_HEIGHT 320
#define UI_RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

void ui_init(void);

/* 仅在任务上下文调用，队列满时阻塞生产者以保证绘制顺序。
 * 填色坐标包含两个端点；文本在返回前复制。字体、图片描述符及数据必须
 * 持续有效直至绘制完成，通常应为 Flash 中的常量资源。只有界面任务可调用屏幕驱动。 */
void ui_fill_color(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void ui_write_string(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg_color,
                     const font_t *font);
void ui_draw_image(uint16_t x, uint16_t y, const image_t *image);

#endif
