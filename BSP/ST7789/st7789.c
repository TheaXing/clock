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

// CLK —? PB13
// MOSI —? PC3
// MISO —? PC2
// CS —? PE2
// RESET —? PE3
// DC —? PE4
// BL —? PE5

#define CS_PORT     GPIOE
#define CS_PIN      GPIO_PIN_2
#define RESET_PORT  GPIOE
#define RESET_PIN   GPIO_PIN_3
#define DC_PORT     GPIOE
#define DC_PIN      GPIO_PIN_4
#define BL_PORT     GPIOE
#define BL_PIN      GPIO_PIN_5


static void st7789_init_display(void);



void st7789_init(void)
{
    st7789_init_display();
}

static void st7789_spi_send(const uint8_t *data, uint16_t length)
{
    if (length == 0)
        return;

    if (HAL_SPI_Transmit(&hspi2, data, length, 100) != HAL_OK)
    {
        HAL_GPIO_WritePin(CS_PORT, CS_PIN, GPIO_PIN_SET);
        Error_Handler();
    }
}

static void st7789_write_register(uint8_t reg,
                                  uint8_t data[],
                                  uint16_t length)
{
    /* CS低：选中屏幕 */
    HAL_GPIO_WritePin(CS_PORT, CS_PIN, GPIO_PIN_RESET);

    /* DC低：发送命令 */
    HAL_GPIO_WritePin(DC_PORT, DC_PIN, GPIO_PIN_RESET);
    st7789_spi_send(&reg, 1);//这段代码是为了发送寄存器地址，告诉屏幕接下来要写入哪个寄存器。

    /* DC高：发送命令参数 */
    HAL_GPIO_WritePin(DC_PORT, DC_PIN, GPIO_PIN_SET);

    if (length > 0)
    {
        configASSERT(data != NULL);
        st7789_spi_send(data, length);
    }

    /* CS高：结束传输 */
    HAL_GPIO_WritePin(CS_PORT, CS_PIN, GPIO_PIN_SET);
}

static void st7789_write_gram(uint8_t data[],
                             uint32_t length,
                             bool singlecolor)
{
    static uint8_t tx_buffer[256];

    if (length == 0)
        return;

    /* RGB565每个像素占2字节，长度必须是偶数 */
    if (data == NULL || (length & 1U) != 0U)
    {
        Error_Handler();
        return;
    }

    /* 选中屏幕，DC拉高表示发送像素数据 */
    HAL_GPIO_WritePin(CS_PORT, CS_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(DC_PORT, DC_PIN, GPIO_PIN_SET);

    while (length > 0)
    {
        /* 每次最多处理256字节 */
        uint16_t count = (uint16_t)(
            length > sizeof(tx_buffer) ? sizeof(tx_buffer) : length
        );

        for (uint16_t i = 0; i < count; i += 2)//这段代码是为了将像素数据从源缓冲区复制到传输缓冲区，并根据是否为单色模式选择不同的像素数据源。
        {
            /*
             * 单色：始终读取同一个颜色的两个字节。
             * 图片/字体：依次读取每个像素。
             */
            const uint8_t *pixel = singlecolor ? data : data + i;

            /* 旧缓冲低字节在前，屏幕需要高字节在前 */
            tx_buffer[i]     = pixel[1];
            tx_buffer[i + 1] = pixel[0];
        }

        st7789_spi_send(tx_buffer, count);

        /* 非单色模式，源指针移动到下一块 */
        if (!singlecolor)
            data += count;

        length -= count;
    }

    /* 所有像素发送完成 */
    HAL_GPIO_WritePin(CS_PORT, CS_PIN, GPIO_PIN_SET);
}





static void st7789_reset(void)
{
    HAL_GPIO_WritePin(RESET_PORT, RESET_PIN, GPIO_PIN_RESET);
    vTaskDelay(pdMS_TO_TICKS(2)); // 20us at least
    HAL_GPIO_WritePin(RESET_PORT, RESET_PIN, GPIO_PIN_SET);
    vTaskDelay(pdMS_TO_TICKS(120));
}

static void st7789_set_backlight(bool on)
{
    HAL_GPIO_WritePin(BL_PORT, BL_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void st7789_init_display(void)
{
    st7789_reset();
    
    st7789_write_register(0x11, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS (5));
    
    st7789_write_register(0x36, (uint8_t[]){0x00}, 1);
    st7789_write_register(0x3A, (uint8_t[]){0x55}, 1);
    st7789_write_register(0xB7, (uint8_t[]){0x46}, 1);
    st7789_write_register(0xBB, (uint8_t[]){0x1B}, 1);
    st7789_write_register(0xC0, (uint8_t[]){0x2C}, 1);
    st7789_write_register(0xC2, (uint8_t[]){0x01}, 1);
    st7789_write_register(0xC4, (uint8_t[]){0x20}, 1);
    st7789_write_register(0xC6, (uint8_t[]){0x0F}, 1);
    st7789_write_register(0xD0, (uint8_t[]){0xA4,0xA1}, 2);
    st7789_write_register(0xD6, (uint8_t[]){0xA1}, 1);
    st7789_write_register(0xE0, (uint8_t[]){0xF0,0x00,0x06,0x04,0x05,0x05,0x31,0x44,0x48,0x36,0x12,0x12,0x2B,0x34}, 14);
    st7789_write_register(0xE1, (uint8_t[]){0xF0,0x0B,0x0F,0x0F,0x0D,0x26,0x31,0x43,0x47,0x38,0x14,0x14,0x2C,0x32}, 14);
    st7789_write_register(0x21, NULL, 0);
    st7789_write_register(0x29, NULL, 0);
    
    st7789_fill_color(0, 0, ST7789_WIDTH - 1, ST7789_HEIGHT - 1, 0x0000);
    st7789_set_backlight(true);
}

static bool in_screen_range(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    if (x1 >= ST7789_WIDTH || y1 >= ST7789_HEIGHT)
        return false;
    if (x2 >= ST7789_WIDTH || y2 >= ST7789_HEIGHT)
        return false;
    if (x1 > x2 || y1 > y2)
        return false;

    return true;
}

static bool in_screen_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    return x < ST7789_WIDTH && y < ST7789_HEIGHT &&
           width != 0 && height != 0 &&
           width <= ST7789_WIDTH - x && height <= ST7789_HEIGHT - y;
}

static void st7789_set_range_and_prepare_gram(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    st7789_write_register(0x2A, (uint8_t[]){(x1 >> 8) & 0xff, x1 & 0xff, (x2 >> 8) & 0xff, x2 & 0xff}, 4);
    st7789_write_register(0x2B, (uint8_t[]){(y1 >> 8) & 0xff, y1 & 0xff, (y2 >> 8) & 0xff, y2 & 0xff}, 4);
    st7789_write_register(0x2C, NULL, 0);
}

void st7789_fill_color(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    if (!in_screen_range(x1, y1, x2, y2))
        return;
    
    st7789_set_range_and_prepare_gram(x1, y1, x2, y2);
    
    uint32_t pixels = (x2 - x1 + 1) * (y2 - y1 + 1);
    st7789_write_gram((uint8_t *)&color, pixels * 2, true);
}

static void st7789_draw_font(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint8_t *model, uint16_t color, uint16_t bg_color)
{
    static uint8_t buff[72 * 72 * 2];
    uint32_t pixels = (uint32_t)width * height;
    if (model == NULL || !in_screen_rect(x, y, width, height) ||
        pixels > sizeof(buff) / 2)
        return;

    uint16_t bytes_per_row = (width + 7) / 8;
    uint8_t *pbuf = buff;
	for (uint16_t row = 0; row < height; row++)
	{
		const uint8_t *row_data = model + row * bytes_per_row;
		for (uint16_t col = 0; col < width; col++)
		{
			uint8_t pixel = row_data[col / 8] & (1 << (7 - col % 8));
            uint16_t pixel_color = pixel ? color : bg_color;
            *pbuf++ = pixel_color & 0xff;
            *pbuf++ = (pixel_color >> 8) & 0xff;
		}
	}
    
    st7789_set_range_and_prepare_gram(x, y, x + width - 1, y + height - 1);
    st7789_write_gram(buff, pixels * 2, false);
}

static const uint8_t *ascii_get_model(const char ch, const font_t *font)
{
    if (font->ascii_model == NULL)
        return NULL;

    uint16_t bytes_per_row = (font->size / 2 + 7) / 8;
    uint16_t bytes_per_char = font->size * bytes_per_row;
    
    if (font->ascii_map)
    {
        const char *map = font->ascii_map;
        for (; *map != '\0'; ++map)
        {
            if (*map == ch)
            {
                return font->ascii_model + (map - font->ascii_map) * bytes_per_char;
            }
        }
    }
    else
    {
        return font->ascii_model + (ch - ' ') * bytes_per_char;
    }
    
    return NULL;
}

static void st7789_write_ascii(uint16_t x, uint16_t y, char ch, uint16_t color, uint16_t bg_color, const font_t *font)
{
    if (font == NULL)
        return;
    
    uint16_t fheight = font->size, fwidth = font->size / 2;
    if (!in_screen_rect(x, y, fwidth, fheight))
        return;
    
    if (ch < 0x20 || ch > 0x7E)
        return;
    
	    const uint8_t *model = ascii_get_model(ch, font);
    if (model)
        st7789_draw_font(x, y, fwidth, fheight, model, color, bg_color);
}

static void st7789_write_chinese(uint16_t x, uint16_t y, const char *ch, uint16_t color, uint16_t bg_color, const font_t *font)
{
    if (ch == NULL || font == NULL || font->chinese == NULL)
        return;

    uint16_t fheight = font->size, fwidth = font->size;
    if (!in_screen_rect(x, y, fwidth, fheight))
        return;
    
    const font_chinese_t *c = font->chinese;
    for (; c->name != NULL; c++)
    {
        if (strcmp(c->name, ch) == 0)
            break;
    }
    if (c->name == NULL)
        return;
    
    st7789_draw_font(x, y, fwidth, fheight, c->model, color, bg_color);
}

static bool is_gb2312(char ch)
{
    return ((unsigned char)ch >= 0xA1 && (unsigned char)ch <= 0xF7);
}

//static int utf8_char_length(const char *str)
//{
//    if ((*str & 0x80) == 0) return 1; // 1 byte
//    if ((*str & 0xE0) == 0xC0) return 2; // 2 bytes
//    if ((*str & 0xF0) == 0xE0) return 3; // 3 bytes
//    if ((*str & 0xF8) == 0xF0) return 4; // 4 bytes
//    return -1; // Invalid UTF-8
//}

void st7789_write_string(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg_color, const font_t *font)
{
    if (str == NULL || font == NULL || font->size < 2)
        return;

    uint32_t cursor_x = x;
    while (*str)
    {
        int len = is_gb2312(*str) ? 2 : 1;
        uint16_t width = len == 1 ? font->size / 2 : font->size;
        if (cursor_x >= ST7789_WIDTH ||
            !in_screen_rect((uint16_t)cursor_x, y, width, font->size))
            break;

        if (len == 1)
        {
            st7789_write_ascii((uint16_t)cursor_x, y, *str, color, bg_color, font);
            str++;
        }
        else
        {
            if (str[1] == '\0')
                break;
            if ((unsigned char)str[1] < 0xA1 || (unsigned char)str[1] > 0xFE)
            {
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

void st7789_draw_image(uint16_t x, uint16_t y, const image_t *image)
{
    if (image == NULL || image->data == NULL ||
        !in_screen_rect(x, y, image->width, image->height))
        return;
    
    st7789_set_range_and_prepare_gram(x, y, x + image->width - 1, y + image->height - 1);
    st7789_write_gram((uint8_t *)image->data, (uint32_t)image->width * image->height * 2, false);
}


