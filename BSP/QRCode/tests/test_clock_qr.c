#include "clock_qr.h"
#include "page.h"
#include "reference.h"
#include "ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t pixels[UI_HEIGHT][UI_WIDTH];
static unsigned fills;
static bool saw_saved;
static bool saw_module;
static bool saw_random;
static bool saw_connecting;
static const char *output_path;
const image_t img_wifi = {0};

/** 功能：记录真实页面的矩形并检查屏幕边界；参数：两个端点及颜色；返回：无。 */
void ui_fill_color(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color) {
    assert(x1 <= x2 && y1 <= y2 && x2 < UI_WIDTH && y2 < UI_HEIGHT);
    ++fills;
    for (unsigned y = y1; y <= y2; ++y) {
        for (unsigned x = x1; x <= x2; ++x) {
            pixels[y][x] = color;
        }
    }
}

/** 功能：以真实字模绘制 ASCII 标签并检查文字范围；参数：位置、文字、颜色及字体；返回：无。 */
void ui_write_string(uint16_t x, uint16_t y, const char *text, uint16_t color, uint16_t background,
                     const font_t *font) {
    size_t length = strlen(text);
    unsigned width = font->size / 2U;
    assert(x + length * width <= UI_WIDTH && y + font->size <= UI_HEIGHT);
    saw_saved |= strstr(text, "saved") != NULL;
    saw_module |= strstr(text, "Module") != NULL;
    saw_random |= strstr(text, "Random") != NULL;
    saw_connecting |= strstr(text, "Connecting") != NULL;
    for (size_t n = 0U; n < length; ++n) {
        unsigned c = (unsigned char)text[n];
        assert(c >= 32U && c <= 126U);
        const uint8_t *glyph = font->ascii_model + (c - 32U) * ((width + 7U) / 8U) * font->size;
        for (unsigned row = 0U; row < font->size; ++row) {
            for (unsigned column = 0U; column < width; ++column) {
                bool dark = (glyph[row * ((width + 7U) / 8U) + column / 8U] &
                             (0x80U >> (column % 8U))) != 0U;
                pixels[y + row][x + n * width + column] = dark ? color : background;
            }
        }
    }
}

/** 功能：禁止新页面依赖临时图片或像素缓冲区；参数：位置及图像；返回：无。 */
void ui_draw_image(uint16_t x, uint16_t y, const image_t *image) {
    (void)x;
    (void)y;
    (void)image;
    assert(false);
}

/** 功能：保存完整显示屏光栅用于独立解码和预览；参数：文件名称；返回：无。 */
static void save_page(const char *name) {
    char path[512];
    (void)snprintf(path, sizeof path, "%s/%s.ppm", output_path, name);
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    (void)fprintf(file, "P6\n240 320\n255\n");
    for (unsigned y = 0U; y < UI_HEIGHT; ++y) {
        for (unsigned x = 0U; x < UI_WIDTH; ++x) {
            uint16_t pixel = pixels[y][x];
            uint8_t rgb[] = {(uint8_t)(((pixel >> 11U) & 31U) * 255U / 31U),
                             (uint8_t)(((pixel >> 5U) & 63U) * 255U / 63U),
                             (uint8_t)((pixel & 31U) * 255U / 31U)};
            assert(fwrite(rgb, sizeof rgb, 1U, file) == 1U);
        }
    }
    assert(fclose(file) == 0);
}

/** 功能：独立参考位图逐模块验证实际编码；参数：载荷及参考矩阵；返回：无。 */
static void check_encoding(const char *text, const char *const reference[33]) {
    clock_qr_t qr;
    assert(clock_qr_encode(text, &qr) && clock_qr_size(&qr) == 33U);
    for (unsigned y = 0U; y < 33U; ++y) {
        for (unsigned x = 0U; x < 33U; ++x) {
            assert(clock_qr_module(&qr, x, y) == (reference[y][x] == '1'));
        }
    }
    assert(!clock_qr_module(&qr, 33U, 0U));
    assert(!clock_qr_module(&qr, 0U, 33U));
}

/** 功能：检查实际绘图的五倍模块及四模块静区；参数：独立矩阵；返回：无。 */
static void check_page_qr(const char *const reference[33]) {
    for (unsigned y = 0U; y < 205U; ++y) {
        for (unsigned x = 0U; x < 205U; ++x) {
            int module_x = (int)(x / 5U) - 4;
            int module_y = (int)(y / 5U) - 4;
            bool dark = module_x >= 0 && module_y >= 0 && module_x < 33 && module_y < 33 &&
                        reference[module_y][module_x] == '1';
            assert(pixels[y + 55U][x + 17U] == (dark ? 0U : 0xffffU));
        }
    }
}

/** 功能：验证两个配网步骤、错误和保存提示；参数：无；返回：无。 */
static void check_pages(void) {
    wifi_view_t view = {.active = true,
                        .ready = true,
                        .state = WIFI_SETUP_WAITING,
                        .ap_ssid = "Clock-Setup-1234ABCD",
                        .ap_password = "0123456789ABCDEF"};
    wifi_setup_page_display(&view);
    check_page_qr(wifi_reference);
    save_page("wifi-step1");
    (void)strcpy(view.ap_ssid, "Clock-Setup-AB;CDEFG");
    fills = 0U;
    wifi_setup_page_display(&view);
    assert(fills == 1U);
    (void)strcpy(view.ap_ssid, "Clock-Setup-1234ABCD");
    view.client = true;
    wifi_setup_page_display(&view);
    check_page_qr(url_reference);
    save_page("wifi-step2");
    const wifi_setup_state_t phases[] = {WIFI_SETUP_CONNECTING, WIFI_SETUP_VERIFYING,
                                         WIFI_SETUP_SAVING, WIFI_SETUP_FAILED};
    for (size_t n = 0U; n < sizeof phases / sizeof phases[0]; ++n) {
        view.state = phases[n];
        wifi_setup_page_display(&view);
        check_page_qr(url_reference);
    }
    view.ready = false;
    view.error[0] = '\0';
    fills = 0U;
    wifi_setup_page_display(&view);
    assert(fills == 1U && saw_connecting);
    save_page("wifi-connecting");
    (void)strcpy(view.error, "module");
    wifi_setup_page_display(&view);
    assert(saw_module);
    save_page("wifi-module-error");
    (void)strcpy(view.error, "random");
    wifi_setup_page_display(&view);
    assert(saw_random);
    save_page("wifi-random-error");
    view.state = WIFI_SETUP_SAVED;
    view.ready = true;
    fills = 0U;
    wifi_setup_page_display(&view);
    assert(saw_saved && fills == 1U);
    save_page("wifi-saved");
    view.state = WIFI_SETUP_WAITING;
    view.client = false;
    ui_fill_color(0U, 0U, UI_WIDTH - 1U, UI_HEIGHT - 1U, 0U);
    wifi_setup_page_display(&view);
    check_page_qr(wifi_reference);
    view.active = false;
    fills = 0U;
    wifi_setup_page_display(&view);
    wifi_setup_page_display(NULL);
    assert(fills == 0U);
}

/** 功能：执行编码和真实页面回归；参数：输出目录；返回：成功为零。 */
int main(int argc, char **argv) {
    assert(argc == 2);
    output_path = argv[1];
    check_encoding("WIFI:T:WPA;S:Clock-Setup-1234ABCD;P:0123456789ABCDEF;;", wifi_reference);
    check_encoding("http://192.168.4.1/setup", url_reference);
    clock_qr_t qr;
    char too_long[64];
    memset(too_long, 'x', sizeof too_long - 1U);
    too_long[sizeof too_long - 1U] = '\0';
    assert(!clock_qr_encode(too_long, &qr) && clock_qr_size(&qr) == 0U);
    assert(!clock_qr_encode(NULL, &qr) && clock_qr_size(&qr) == 0U);
    assert(!clock_qr_encode("text", NULL));
    too_long[62] = '\0';
    assert(clock_qr_encode(too_long, &qr));
    assert(!clock_qr_encode("", &qr) && clock_qr_size(&qr) == 0U);
    assert(clock_qr_size(NULL) == 0U && !clock_qr_module(NULL, 0U, 0U));
    check_pages();
    puts("QR reference matrices and page raster tests passed");
    return 0;
}
