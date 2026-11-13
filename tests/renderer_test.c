/* Capture actual LCD command/pixel traffic without requiring a display. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stm32f4xx.h"
#include "st7789.h"

SPI_HandleTypeDef hspi2;
static GPIO_PinState dc;
static uint8_t command;
static uint8_t address_x[4], address_y[4];
static uint8_t pixels[ST7789_WIDTH * ST7789_HEIGHT * 2];
static size_t pixel_count, transfers;

void vTaskDelay(uint32_t ticks) { (void)ticks; }
void Error_Handler(void) { assert(!"Unexpected HAL failure"); abort(); }

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    (void)port;
    if (pin == GPIO_PIN_4) dc = state;
}

int HAL_SPI_Transmit(SPI_HandleTypeDef *spi, const uint8_t *data,
                     uint16_t length, uint32_t timeout)
{
    (void)spi;
    (void)timeout;
    ++transfers;
    if (dc == GPIO_PIN_RESET)
    {
        assert(length == 1);
        command = *data;
    }
    else if (command == 0x2A)
    {
        assert(length == 4);
        memcpy(address_x, data, 4);
    }
    else if (command == 0x2B)
    {
        assert(length == 4);
        memcpy(address_y, data, 4);
    }
    else if (command == 0x2C)
    {
        assert(length <= 256);
        assert(pixel_count + length <= sizeof(pixels));
        memcpy(pixels + pixel_count, data, length);
        pixel_count += length;
    }
    return HAL_OK;
}

static void reset_capture(void)
{
    transfers = pixel_count = 0;
    memset(address_x, 0, sizeof(address_x));
    memset(address_y, 0, sizeof(address_y));
}

static void test_bottom_right_image_and_rgb565_order(void)
{
    const uint8_t data[] = {0x34, 0x12};
    const image_t image = {1, 1, data};
    reset_capture();
    st7789_draw_image(239, 319, &image);
    assert(pixel_count == 2);
    assert(memcmp(address_x, (uint8_t[]){0,239,0,239}, 4) == 0);
    assert(memcmp(address_y, (uint8_t[]){1,63,1,63}, 4) == 0);
    assert(memcmp(pixels, (uint8_t[]){0x12,0x34}, 2) == 0);
}

static void test_full_screen_image_crosses_spi_chunks(void)
{
    static uint8_t data[240 * 320 * 2];
    const image_t image = {240, 320, data};
    for (size_t i = 0; i < sizeof(data); ++i) data[i] = (uint8_t)(i * 37 + i / 256);
    reset_capture();
    st7789_draw_image(0, 0, &image);
    assert(pixel_count == 153600);
    assert(memcmp(address_x, (uint8_t[]){0,0,0,239}, 4) == 0);
    assert(memcmp(address_y, (uint8_t[]){0,0,1,63}, 4) == 0);
    for (size_t i = 0; i < sizeof(data); i += 2)
    {
        assert(pixels[i] == data[i + 1]);
        assert(pixels[i + 1] == data[i]);
    }
}

static void test_invalid_images_do_not_touch_lcd(void)
{
    const uint8_t data[] = {0x34, 0x12};
    const image_t empty = {0, 1, data};
    const image_t missing_data = {1, 1, NULL};
    const image_t too_wide = {241, 1, data};
    const image_t two_pixels = {2, 1, data};
    reset_capture();
    st7789_draw_image(0, 0, NULL);
    st7789_draw_image(0, 0, &empty);
    st7789_draw_image(0, 0, &missing_data);
    st7789_draw_image(0, 0, &too_wide);
    st7789_draw_image(239, 0, &two_pixels);
    st7789_draw_image(65535, 0, &two_pixels);
    assert(transfers == 0);
}

static void test_fill_uses_inclusive_end_coordinates(void)
{
    reset_capture();
    st7789_fill_color(238, 318, 239, 319, 0xABCD);
    assert(pixel_count == 8);
    assert(memcmp(pixels, (uint8_t[]){0xAB,0xCD,0xAB,0xCD,0xAB,0xCD,0xAB,0xCD}, 8) == 0);
    reset_capture();
    st7789_fill_color(2, 2, 1, 1, 0);
    assert(transfers == 0);
}

static void test_ascii_and_gbk_glyph_pixels(void)
{
    const uint8_t ascii_model[] = {0x80, 0x00};
    const uint8_t chinese_model[] = {0x80, 0x40};
    const font_chinese_t chinese[] = {{"\xD6\xD0", chinese_model}, {NULL, NULL}};
    const font_t font = {ascii_model, "A", chinese, 2};
    reset_capture();
    st7789_write_string(0, 0, "A", 0x1234, 0x5678, &font);
    assert(pixel_count == 4);
    assert(memcmp(pixels, (uint8_t[]){0x12,0x34,0x56,0x78}, 4) == 0);
    reset_capture();
    st7789_write_string(0, 0, "\xD6\xD0", 0x1234, 0x5678, &font);
    assert(pixel_count == 8);
    assert(memcmp(pixels, (uint8_t[]){0x12,0x34,0x56,0x78,0x56,0x78,0x12,0x34}, 8) == 0);
}

static void test_invalid_text_and_unsupported_fonts_do_not_touch_lcd(void)
{
    const uint8_t model[] = {0x80, 0x00};
    const font_t ascii_only = {model, "A", NULL, 2};
    const font_t empty_map = {model, "", NULL, 2};
    const font_t missing_ascii = {NULL, NULL, NULL, 2};
    const font_t oversized = {model, "A", NULL, 200};
    const font_t zero_size = {model, "A", NULL, 0};
    reset_capture();
    st7789_write_string(0, 0, NULL, 0, 0, &ascii_only);
    st7789_write_string(0, 0, "A", 0, 0, NULL);
    st7789_write_string(0, 0, "\xD6\xD0", 0, 0, &ascii_only);
    st7789_write_string(0, 0, "\xD6", 0, 0, &ascii_only);
    st7789_write_string(0, 0, "A", 0, 0, &empty_map);
    st7789_write_string(0, 0, "A", 0, 0, &missing_ascii);
    st7789_write_string(0, 0, "A", 0, 0, &oversized);
    st7789_write_string(0, 0, "A", 0, 0, &zero_size);
    assert(transfers == 0);
}

static void test_large_clock_ascii_fits_existing_scratch_buffer(void)
{
    static uint8_t model[5 * 76];
    const font_t font = {model, "0", NULL, 76};
    reset_capture();
    st7789_write_string(25, 42, "0", 0xFFFF, 0x1234, &font);
    assert(pixel_count == 5776);
    assert(memcmp(address_x, (uint8_t[]){0,25,0,62}, 4) == 0);
    assert(memcmp(address_y, (uint8_t[]){0,42,0,117}, 4) == 0);
    assert(pixels[0] == 0x12 && pixels[1] == 0x34);
}

static void test_three_character_reading_uses_real_font42(void)
{
    reset_capture();
    st7789_write_string(20, 198, "-12", 0, 0x55AA, &font42_maple);
    assert(pixel_count == 5292);
    assert(memcmp(address_x, (uint8_t[]){0,62,0,82}, 4) == 0);
    assert(memcmp(address_y, (uint8_t[]){0,198,0,239}, 4) == 0);
    size_t foreground_pixels = 0;
    for (size_t i = 0; i < pixel_count; i += 2)
        if (pixels[i] == 0 && pixels[i + 1] == 0) ++foreground_pixels;
    assert(foreground_pixels > 0 && foreground_pixels < 2646);
}

int main(void)
{
    test_bottom_right_image_and_rgb565_order();
    test_full_screen_image_crosses_spi_chunks();
    test_invalid_images_do_not_touch_lcd();
    test_fill_uses_inclusive_end_coordinates();
    test_ascii_and_gbk_glyph_pixels();
    test_invalid_text_and_unsupported_fonts_do_not_touch_lcd();
    test_large_clock_ascii_fits_existing_scratch_buffer();
    test_three_character_reading_uses_real_font42();
    puts("renderer: all tests passed");
    return 0;
}
