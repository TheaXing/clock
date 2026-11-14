#include "ui.h"

/**
 * 功能：绘制欢迎图片、时钟标题和加载提示。
 * 参数：无。
 * 返回：无。
 */
void welcome_page_display(void) {
    const uint16_t color_bg = UI_RGB565(255, 255, 255);
    ui_fill_color(0, 0, UI_WIDTH - 1, UI_HEIGHT - 1, color_bg);
    ui_draw_image(30, 10, &img_star);
    ui_write_string(70, 205, "夏敬星", UI_RGB565(237, 128, 147), color_bg, &font32_maple);
    ui_write_string(56, 238, "天气时钟", UI_RGB565(86, 168, 255), color_bg, &font32_maple);
    ui_write_string(60, 280, "loading...", UI_RGB565(0, 0, 0), color_bg, &font24_maple);
}
