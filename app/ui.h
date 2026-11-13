#ifndef __APP_UI_H__
#define __APP_UI_H__

#include <stdint.h>
#include "font.h"
#include "image.h"

#define UI_WIDTH 240
#define UI_HEIGHT 320
#define mkcolor(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

void ui_init(void);

/* Task context only. Full queues block the producer to preserve draw order.
 * Fill coordinates include both end pixels. Text is copied before returning;
 * font and image descriptors/data must remain valid until rendered (normally
 * const resources in flash). Only the UI task may call the LCD driver. */
void ui_fill_color(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void ui_write_string(uint16_t x, uint16_t y, const char *str,
                     uint16_t color, uint16_t bg_color, const font_t *font);
void ui_draw_image(uint16_t x, uint16_t y, const image_t *image);

#endif
