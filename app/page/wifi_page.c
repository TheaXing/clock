#include "app.h"
#include "clock_qr.h"
#include "ui.h"
#include "wifi.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/**
 * 功能：绘制无线连接页面并居中显示配置的网络名称。
 * 参数：无。
 * 返回：无。
 */
void wifi_page_display(void) {
    static const char *ssid = WIFI_SSID;
    uint16_t ssid_startx = 0;
    size_t ssid_len = strlen(ssid) * font24_maple.size / 2U;
    if (ssid_len < UI_WIDTH) {
        ssid_startx = (uint16_t)((UI_WIDTH - ssid_len + 1U) / 2U);
    }
    const uint16_t color_bg = UI_RGB565(255, 255, 255);
    ui_fill_color(0, 0, UI_WIDTH - 1, UI_HEIGHT - 1, color_bg);
    ui_draw_image(20, 15, &img_wifi);
    ui_write_string(100, 191, "WiFi", UI_RGB565(0, 255, 234), color_bg, &font24_maple);
    ui_write_string(ssid_startx, 231, ssid, UI_RGB565(128, 128, 128), color_bg, &font24_maple);
    ui_write_string(94, 270, "连接中", UI_RGB565(148, 198, 255), color_bg, &font24_maple);
}

/** 功能：居中绘制可打印 ASCII 提示；参数：纵坐标、文字及字体；返回：无。 */
static void setup_text(uint16_t y, const char *text, const font_t *font) {
    size_t width = strlen(text) * font->size / 2U;
    uint16_t x = width < UI_WIDTH ? (uint16_t)((UI_WIDTH - width) / 2U) : 0U;
    ui_write_string(x, y, text, UI_RGB565(0, 0, 0), UI_RGB565(255, 255, 255), font);
}

/** 功能：以五像素模块绘制黑色水平游程，并保留四模块白静区；参数：二维码；返回：无。 */
static void setup_qr(const clock_qr_t *qr) {
    const unsigned scale = 5U;
    const unsigned left = 17U + 4U * scale;
    const unsigned top = 55U + 4U * scale;
    for (unsigned y = 0U; y < clock_qr_size(qr); ++y) {
        unsigned x = 0U;
        while (x < clock_qr_size(qr)) {
            if (!clock_qr_module(qr, x, y)) {
                ++x;
                continue;
            }
            unsigned start = x;
            do {
                ++x;
            } while (x < clock_qr_size(qr) && clock_qr_module(qr, x, y));
            ui_fill_color((uint16_t)(left + start * scale), (uint16_t)(top + y * scale),
                          (uint16_t)(left + x * scale - 1U),
                          (uint16_t)(top + (y + 1U) * scale - 1U), UI_RGB565(0, 0, 0));
        }
    }
}

/** 功能：检查生成的热点身份是否符合固定编码预算；参数：显示快照；返回：名称后缀和口令均为十六进制时为真。 */
static bool setup_identity_valid(const wifi_view_t *view) {
    if (memcmp(view->ap_ssid, "Clock-Setup-", 12U) != 0 || view->ap_ssid[20] != '\0' ||
        view->ap_password[16] != '\0') {
        return false;
    }
    /* 固定十六进制身份不含 WIFI 字段分隔符，无需混用 AT 或表单转义规则。 */
    for (unsigned n = 0U; n < 24U; ++n) {
        unsigned char c = n < 8U ? (unsigned char)view->ap_ssid[n + 12U]
                                 : (unsigned char)view->ap_password[n - 8U];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

/** 功能：绘制两步配网或模块错误页面；参数：网络任务复制的显示快照；返回：无。 */
void wifi_setup_page_display(const wifi_view_t *view) {
    if (view == NULL || !view->active) {
        return;
    }
    ui_fill_color(0U, 0U, UI_WIDTH - 1U, UI_HEIGHT - 1U, UI_RGB565(255, 255, 255));
    if (view->state == WIFI_SETUP_SAVED) {
        setup_text(100U, "Wi-Fi saved", &font24_maple);
        setup_text(145U, "Returning to clock...", &font16_maple);
        return;
    }
    if (!view->ready) {
        setup_text(90U, "Wi-Fi setup", &font24_maple);
        const char *message = "Connecting...";
        if (strcmp(view->error, "module") == 0) {
            message = "Module error - retrying";
        } else if (strcmp(view->error, "random") == 0) {
            message = "Random error - retrying";
        }
        setup_text(140U, message, &font16_maple);
        return;
    }
    char payload[128];
    if (view->client) {
        (void)snprintf(payload, sizeof payload, "http://192.168.4.1/setup");
    } else {
        if (!setup_identity_valid(view)) {
            setup_text(140U, "QR data error", &font16_maple);
            return;
        }
        int count = snprintf(payload, sizeof payload, "WIFI:T:WPA;S:%s;P:%s;;", view->ap_ssid,
                             view->ap_password);
        if (count < 0 || (size_t)count >= sizeof payload) {
            setup_text(140U, "QR data error", &font16_maple);
            return;
        }
    }
    clock_qr_t qr;
    if (!clock_qr_encode(payload, &qr)) {
        setup_text(140U, "QR data error", &font16_maple);
        return;
    }
    setup_text(5U, "Wi-Fi setup", &font24_maple);
    setup_text(34U, view->client ? "2 Scan to open setup page" : "1 Scan to connect Wi-Fi",
               &font16_maple);
    setup_qr(&qr);
    if (view->client) {
        setup_text(270U, "http://192.168.4.1/setup", &font16_maple);
        const char *status = "Or enter URL in browser";
        if (view->state == WIFI_SETUP_CONNECTING || view->state == WIFI_SETUP_VERIFYING) {
            status = "Connecting... reconnect Wi-Fi";
        } else if (view->state == WIFI_SETUP_SAVING) {
            status = "Saving verified Wi-Fi...";
        } else if (view->state == WIFI_SETUP_FAILED) {
            status = strcmp(view->error, "storage") == 0 ? "Save failed - retry in page"
                                                         : "Join failed - retry in page";
        }
        setup_text(294U, status, &font16_maple);
    } else {
        setup_text(270U, view->ap_ssid, &font16_maple);
        char password[32];
        (void)snprintf(password, sizeof password, "PW: %s", view->ap_password);
        setup_text(294U, password, &font16_maple);
    }
}
