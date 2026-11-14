#include <stdint.h>
#include <string.h>
#include "wifi.h"
#include "ui.h"
#include "app.h"

/**
 * 功能：绘制无线连接页面并居中显示配置的网络名称。
 * 参数：无。
 * 返回：无。
 */
void wifi_page_display(void) {
    static const char *ssid = WIFI_SSID;
    uint16_t ssid_startx = 0;
    int ssid_len = strlen(ssid) * font24_maple.size / 2;
    if (ssid_len < UI_WIDTH) {
        ssid_startx = (UI_WIDTH - ssid_len + 1) / 2;
    }
    const uint16_t color_bg = UI_RGB565(255, 255, 255);
    ui_fill_color(0, 0, UI_WIDTH - 1, UI_HEIGHT - 1, color_bg);
    ui_draw_image(20, 15, &img_wifi);
    ui_write_string(100, 191, "WiFi", UI_RGB565(0, 255, 234), color_bg, &font24_maple);
    ui_write_string(ssid_startx, 231, ssid, UI_RGB565(128, 128, 128), color_bg, &font24_maple);
    ui_write_string(94, 270, "连接中", UI_RGB565(148, 198, 255), color_bg, &font24_maple);
}
