#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx.h"
#include "st7789.h"
#include "font.h"
#include "image.h"
#include "main.h"
#include "spi.h"

// 时钟引脚：PB13
// 主机输出引脚：PC3
// 主机输入引脚：PC2
// 片选引脚：PE2
// 复位引脚：PE3
// 命令与数据选择引脚：PE4
// 背光引脚：PE5

#define CS_PORT     GPIOE
#define CS_PIN      GPIO_PIN_2
#define RESET_PORT  GPIOE
#define RESET_PIN   GPIO_PIN_3
#define DC_PORT     GPIOE
#define DC_PIN      GPIO_PIN_4
#define BL_PORT     GPIOE
#define BL_PIN      GPIO_PIN_5

static void st7789_init_display(void);

/**
 * 功能：初始化 ST7789 控制器并开启背光。
 * 参数：无。
 * 返回：无。
 */
void st7789_init(void) {
    st7789_init_display();
}

/**
 * 功能：通过 SPI 发送字节，发送失败时释放片选并报告错误。
 * 参数：data，传输数据缓冲区；length，传输或处理的字节数。
 * 返回：无。
 */
static void st7789_spi_send(const uint8_t *data, uint16_t length) {
    if (length == 0) {
        return;
    }

    if (HAL_SPI_Transmit(&hspi2, data, length, 100) != HAL_OK) {
        HAL_GPIO_WritePin(CS_PORT, CS_PIN, GPIO_PIN_SET);
        Error_Handler();
    }
}

/**
 * 功能：切换命令与数据电平，向屏幕寄存器发送命令及可选参数。
 * 参数：reg，屏幕寄存器命令字节；data，传输数据缓冲区；length，传输或处理的字节数。
 * 返回：无。
 */
static void st7789_write_register(uint8_t reg, uint8_t data[], uint16_t length) {
    /* CS低：选中屏幕 */
    HAL_GPIO_WritePin(CS_PORT, CS_PIN, GPIO_PIN_RESET);

    /* DC低：发送命令 */
    HAL_GPIO_WritePin(DC_PORT, DC_PIN, GPIO_PIN_RESET);
    st7789_spi_send(&reg, 1); // 发送寄存器命令字节。

    /* DC高：发送命令参数 */
    HAL_GPIO_WritePin(DC_PORT, DC_PIN, GPIO_PIN_SET);

    if (length > 0) {
        configASSERT(data != NULL);
        st7789_spi_send(data, length);
    }

    /* CS高：结束传输 */
    HAL_GPIO_WritePin(CS_PORT, CS_PIN, GPIO_PIN_SET);
}

/**
 * 功能：分块转换 RGB565 字节序并发送连续或重复颜色像素。
 * 参数：data，传输数据缓冲区；length，传输或处理的字节数；singlecolor，是否重复使用首个 RGB565 颜色。
 * 返回：无。
 */
static void st7789_write_gram(uint8_t data[], uint32_t length, bool singlecolor) {
    static uint8_t tx_buffer[256];

    if (length == 0) {
        return;
    }

    /* RGB565每个像素占2字节，长度必须是偶数 */
    if (data == NULL || (length & 1U) != 0U) {
        Error_Handler();
        return;
    }

    /* 选中屏幕，DC拉高表示发送像素数据 */
    HAL_GPIO_WritePin(CS_PORT, CS_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(DC_PORT, DC_PIN, GPIO_PIN_SET);

    while (length > 0) {
        /* 每次最多处理256字节 */
        uint16_t count = (uint16_t)(length > sizeof(tx_buffer) ? sizeof(tx_buffer) : length);

        /* 按单色或连续像素模式复制，并交换 RGB565 高低字节。 */
        for (uint16_t i = 0; i < count; i += 2) {
            /*
             * 单色：始终读取同一个颜色的两个字节。
             * 图片/字体：依次读取每个像素。
             */
            const uint8_t *pixel = singlecolor ? data : data + i;

            /* 旧缓冲低字节在前，屏幕需要高字节在前 */
            tx_buffer[i] = pixel[1];
            tx_buffer[i + 1] = pixel[0];
        }

        st7789_spi_send(tx_buffer, count);

        /* 非单色模式，源指针移动到下一块 */
        if (!singlecolor) {
            data += count;
        }

        length -= count;
    }

    /* 所有像素发送完成 */
    HAL_GPIO_WritePin(CS_PORT, CS_PIN, GPIO_PIN_SET);
}

/**
 * 功能：按屏幕要求拉低复位引脚并等待复位恢复。
 * 参数：无。
 * 返回：无。
 */
static void st7789_reset(void) {
    HAL_GPIO_WritePin(RESET_PORT, RESET_PIN, GPIO_PIN_RESET);
    vTaskDelay(pdMS_TO_TICKS(2)); // 复位低电平至少保持 20 微秒。
    HAL_GPIO_WritePin(RESET_PORT, RESET_PIN, GPIO_PIN_SET);
    vTaskDelay(pdMS_TO_TICKS(120));
}

/**
 * 功能：设置屏幕背光控制引脚。
 * 参数：on，是否开启背光。
 * 返回：无。
 */
static void st7789_set_backlight(bool on) {
    HAL_GPIO_WritePin(BL_PORT, BL_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/**
 * 功能：复位并配置屏幕寄存器，清为黑屏后开启背光。
 * 参数：无。
 * 返回：无。
 */
static void st7789_init_display(void) {
    st7789_reset();

    st7789_write_register(0x11, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(5));

    st7789_write_register(0x36, (uint8_t[]){0x00}, 1);
    st7789_write_register(0x3A, (uint8_t[]){0x55}, 1);
    st7789_write_register(0xB7, (uint8_t[]){0x46}, 1);
    st7789_write_register(0xBB, (uint8_t[]){0x1B}, 1);
    st7789_write_register(0xC0, (uint8_t[]){0x2C}, 1);
    st7789_write_register(0xC2, (uint8_t[]){0x01}, 1);
    st7789_write_register(0xC4, (uint8_t[]){0x20}, 1);
    st7789_write_register(0xC6, (uint8_t[]){0x0F}, 1);
    st7789_write_register(0xD0, (uint8_t[]){0xA4, 0xA1}, 2);
    st7789_write_register(0xD6, (uint8_t[]){0xA1}, 1);
    st7789_write_register(0xE0,
                          (uint8_t[]){0xF0, 0x00, 0x06, 0x04, 0x05, 0x05, 0x31, 0x44, 0x48, 0x36,
                                      0x12, 0x12, 0x2B, 0x34},
                          14);
    st7789_write_register(0xE1,
                          (uint8_t[]){0xF0, 0x0B, 0x0F, 0x0F, 0x0D, 0x26, 0x31, 0x43, 0x47, 0x38,
                                      0x14, 0x14, 0x2C, 0x32},
                          14);
    st7789_write_register(0x21, NULL, 0);
    st7789_write_register(0x29, NULL, 0);

    st7789_fill_color(0, 0, ST7789_WIDTH - 1, ST7789_HEIGHT - 1, 0x0000);
    st7789_set_backlight(true);
}

/**
 * 功能：校验包含结束点的屏幕矩形坐标范围与顺序。
 * 参数：x1，矩形起点横坐标；y1，矩形起点纵坐标；x2，矩形结束横坐标，包含在窗口内；y2，矩形结束纵坐标，包含在窗口内。
 * 返回：矩形完整位于屏幕内返回 true，否则返回 false。
 */
static bool in_screen_range(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    if (x1 >= ST7789_WIDTH || y1 >= ST7789_HEIGHT) {
        return false;
    }
    if (x2 >= ST7789_WIDTH || y2 >= ST7789_HEIGHT) {
        return false;
    }
    if (x1 > x2 || y1 > y2) {
        return false;
    }

    return true;
}

/**
 * 功能：校验起点与非零宽高组成的矩形是否位于屏幕内。
 * 参数：x，绘制起点横坐标；y，绘制起点纵坐标；width，矩形或字模宽度，单位为像素；height，矩形或字模高度，单位为像素。
 * 返回：矩形有效且完整位于屏幕内返回 true，否则返回 false。
 */
static bool in_screen_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    return x < ST7789_WIDTH && y < ST7789_HEIGHT && width != 0 && height != 0 &&
           width <= ST7789_WIDTH - x && height <= ST7789_HEIGHT - y;
}

/**
 * 功能：设置包含结束坐标的屏幕列行窗口并准备写入显存。
 * 参数：x1，矩形起点横坐标；y1，矩形起点纵坐标；x2，矩形结束横坐标，包含在窗口内；y2，矩形结束纵坐标，包含在窗口内。
 * 返回：无。
 */
static void st7789_set_range_and_prepare_gram(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    st7789_write_register(0x2A,
                          (uint8_t[]){(x1 >> 8) & 0xff, x1 & 0xff, (x2 >> 8) & 0xff, x2 & 0xff}, 4);
    st7789_write_register(0x2B,
                          (uint8_t[]){(y1 >> 8) & 0xff, y1 & 0xff, (y2 >> 8) & 0xff, y2 & 0xff}, 4);
    st7789_write_register(0x2C, NULL, 0);
}

/**
 * 功能：在有效矩形窗口内重复写入指定 RGB565 颜色。
 * 参数：x1，矩形起点横坐标；
 *       y1，矩形起点纵坐标；
 *       x2，矩形结束横坐标，包含在窗口内；
 *       y2，矩形结束纵坐标，包含在窗口内；
 *       color，RGB565 前景或填充颜色。
 * 返回：无。
 */
void st7789_fill_color(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color) {
    if (!in_screen_range(x1, y1, x2, y2)) {
        return;
    }

    st7789_set_range_and_prepare_gram(x1, y1, x2, y2);

    uint32_t pixels = (x2 - x1 + 1) * (y2 - y1 + 1);
    st7789_write_gram((uint8_t *)&color, pixels * 2, true);
}

/**
 * 功能：将单色字模展开为前景与背景 RGB565 像素并绘制。
 * 参数：x，绘制起点横坐标；
 *       y，绘制起点纵坐标；
 *       width，矩形或字模宽度，单位为像素；
 *       height，矩形或字模高度，单位为像素；
 *       model，逐行按位存储的单色字模；
 *       color，RGB565 前景或填充颜色；
 *       bg_color，RGB565 背景颜色。
 * 返回：无。
 */
static void st7789_draw_font(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                             const uint8_t *model, uint16_t color, uint16_t bg_color) {
    static uint8_t buff[72 * 72 * 2];
    uint32_t pixels = (uint32_t)width * height;
    if (model == NULL || !in_screen_rect(x, y, width, height) || pixels > sizeof(buff) / 2) {
        return;
    }

    uint16_t bytes_per_row = (width + 7) / 8;
    uint8_t *pbuf = buff;
    for (uint16_t row = 0; row < height; row++) {
        const uint8_t *row_data = model + row * bytes_per_row;
        for (uint16_t col = 0; col < width; col++) {
            uint8_t pixel = row_data[col / 8] & (1 << (7 - col % 8));
            uint16_t pixel_color = pixel ? color : bg_color;
            *pbuf++ = pixel_color & 0xff;
            *pbuf++ = (pixel_color >> 8) & 0xff;
        }
    }

    st7789_set_range_and_prepare_gram(x, y, x + width - 1, y + height - 1);
    st7789_write_gram(buff, pixels * 2, false);
}

/**
 * 功能：按字体映射或连续 ASCII 顺序查找字符字模。
 * 参数：ch，待绘制或检查的字符；font，字体映射与尺寸描述。
 * 返回：字模起始地址，未包含该字符时返回 NULL。
 */
static const uint8_t *ascii_get_model(const char ch, const font_t *font) {
    if (font->ascii_model == NULL) {
        return NULL;
    }

    uint16_t bytes_per_row = (font->size / 2 + 7) / 8;
    uint16_t bytes_per_char = font->size * bytes_per_row;

    if (font->ascii_map) {
        const char *map = font->ascii_map;
        for (; *map != '\0'; ++map) {
            if (*map == ch) {
                return font->ascii_model + (map - font->ascii_map) * bytes_per_char;
            }
        }
    } else {
        return font->ascii_model + (ch - ' ') * bytes_per_char;
    }

    return NULL;
}

/**
 * 功能：检查字体、可打印字符与屏幕范围后绘制 ASCII 字符。
 * 参数：x，绘制起点横坐标；
 *       y，绘制起点纵坐标；
 *       ch，待绘制或检查的字符；
 *       color，RGB565 前景或填充颜色；
 *       bg_color，RGB565 背景颜色；
 *       font，字体映射与尺寸描述。
 * 返回：无。
 */
static void st7789_write_ascii(uint16_t x, uint16_t y, char ch, uint16_t color, uint16_t bg_color,
                               const font_t *font) {
    if (font == NULL) {
        return;
    }

    uint16_t fheight = font->size, fwidth = font->size / 2;
    if (!in_screen_rect(x, y, fwidth, fheight)) {
        return;
    }

    if (ch < 0x20 || ch > 0x7E) {
        return;
    }

    const uint8_t *model = ascii_get_model(ch, font);
    if (model) {
        st7789_draw_font(x, y, fwidth, fheight, model, color, bg_color);
    }
}

/**
 * 功能：在字体的汉字映射表中查找并绘制一个 GB2312 汉字。
 * 参数：x，绘制起点横坐标；
 *       y，绘制起点纵坐标；
 *       ch，以空字节结束的双字节 GB2312 汉字；
 *       color，RGB565 前景或填充颜色；
 *       bg_color，RGB565 背景颜色；
 *       font，字体映射与尺寸描述。
 * 返回：无。
 */
static void st7789_write_chinese(uint16_t x, uint16_t y, const char *ch, uint16_t color,
                                 uint16_t bg_color, const font_t *font) {
    if (ch == NULL || font == NULL || font->chinese == NULL) {
        return;
    }

    uint16_t fheight = font->size, fwidth = font->size;
    if (!in_screen_rect(x, y, fwidth, fheight)) {
        return;
    }

    const font_chinese_t *c = font->chinese;
    for (; c->name != NULL; c++) {
        if (strcmp(c->name, ch) == 0) {
            break;
        }
    }
    if (c->name == NULL) {
        return;
    }

    st7789_draw_font(x, y, fwidth, fheight, c->model, color, bg_color);
}

/**
 * 功能：检查字节是否处于 GB2312 汉字首字节范围。
 * 参数：ch，待判断的汉字首字节。
 * 返回：位于首字节范围返回 true，否则返回 false。
 */
static bool is_gb2312(char ch) {
    return ((unsigned char)ch >= 0xA1 && (unsigned char)ch <= 0xF7);
}

/* 当前字符串接口按 GB2312 双字节编码识别汉字。 */

/**
 * 功能：按 ASCII 或 GB2312 编码逐字绘制字符串，抵达屏幕边界时停止。
 * 参数：x，绘制起点横坐标；
 *       y，绘制起点纵坐标；
 *       str，待绘制的 ASCII 与 GB2312 字符串；
 *       color，RGB565 前景或填充颜色；
 *       bg_color，RGB565 背景颜色；
 *       font，字体映射与尺寸描述。
 * 返回：无。
 */
void st7789_write_string(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg_color,
                         const font_t *font) {
    if (str == NULL || font == NULL || font->size < 2) {
        return;
    }

    uint32_t cursor_x = x;
    while (*str) {
        int len = is_gb2312(*str) ? 2 : 1;
        uint16_t width = len == 1 ? font->size / 2 : font->size;
        if (cursor_x >= ST7789_WIDTH || !in_screen_rect((uint16_t)cursor_x, y, width, font->size)) {
            break;
        }

        if (len == 1) {
            st7789_write_ascii((uint16_t)cursor_x, y, *str, color, bg_color, font);
            str++;
        } else {
            if (str[1] == '\0') {
                break;
            }
            if ((unsigned char)str[1] < 0xA1 || (unsigned char)str[1] > 0xFE) {
                ++str;
                continue;
            }
            char ch[3] = {str[0], str[1], '\0'};
            st7789_write_chinese((uint16_t)cursor_x, y, ch, color, bg_color, font);
            str += len;
        }
        cursor_x += width;
    }
}

/**
 * 功能：校验图片与显示范围后绘制 RGB565 图像。
 * 参数：x，绘制起点横坐标；y，绘制起点纵坐标；image，RGB565 图像描述。
 * 返回：无。
 */
void st7789_draw_image(uint16_t x, uint16_t y, const image_t *image) {
    if (image == NULL || image->data == NULL ||
        !in_screen_rect(x, y, image->width, image->height)) {
        return;
    }

    st7789_set_range_and_prepare_gram(x, y, x + image->width - 1, y + image->height - 1);
    st7789_write_gram((uint8_t *)image->data, (uint32_t)image->width * image->height * 2, false);
}
