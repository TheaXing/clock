#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "clock_rtc.h"
#include "wifi.h"
#include "ui.h"
#include "app.h"
#include "page.h"

static const uint16_t color_bg_time = UI_RGB565(255, 255, 255);
static const uint16_t color_bg_inner = UI_RGB565(136, 217, 234);
static const uint16_t color_bg_outdoor = UI_RGB565(253, 135, 75);

/**
 * 功能：绘制主页面各区域和初始占位内容。
 * 参数：无。
 * 返回：无。
 */
void main_page_display(void) {
    ui_fill_color(0, 0, UI_WIDTH - 1, UI_HEIGHT - 1, UI_RGB565(255, 255, 255));

    do {
        ui_fill_color(15, 15, 224, 154, color_bg_time);
        // 无线网络图标
        ui_draw_image(23, 20, &icon_wifi);
        // 无线网络名称
        main_page_redraw_wifi_ssid(WIFI_SSID);

        //时间
        main_page_redraw_time(NULL);

        //日期
        main_page_redraw_date(NULL);

    } while (0);

    do {

        ui_fill_color(15, 165, 114, 304, color_bg_inner);
        ui_write_string(16, 168, "室内环境", UI_RGB565(0, 0, 0), color_bg_inner,
                        &font24_maple_bold);
        ui_write_string(86, 191, "C", UI_RGB565(0, 0, 0), color_bg_inner, &font32_maple);
        ui_write_string(91, 262, "%", UI_RGB565(0, 0, 0), color_bg_inner, &font32_maple);
        main_page_redraw_inner_temperature(NAN);
        main_page_redraw_inner_humidity(NAN);

    } while (0);

    do {
        ui_fill_color(125, 165, 224, 304, color_bg_outdoor);
        ui_write_string(192, 190, "C", UI_RGB565(0, 0, 0), color_bg_outdoor, &font32_maple);
        ui_draw_image(139, 245, &icon_wenduji);
        main_page_redraw_outdoor_city("南京");
        main_page_redraw_outdoor_temperature(NAN);
        main_page_redraw_outdoor_weather_icon(-1);
    } while (0);
}

/**
 * 功能：刷新主页面中的无线网络名称。
 * 参数：ssid，以空字符结尾的无线网络名称。
 * 返回：无。
 */
void main_page_redraw_wifi_ssid(const char *ssid) {
    char str[21];
    snprintf(str, 21, "%20s", ssid);
    ui_write_string(50, 23, str, UI_RGB565(143, 143, 143), color_bg_time, &font16_maple);
}

/**
 * 功能：显示有效时间，并根据秒数交替显示冒号。
 * 参数：time，当前时间；为空或无效时显示占位内容。
 * 返回：无。
 */
void main_page_redraw_time(rtc_date_time_t *time) {
    char str[8] = "  :  ";
    if (time != NULL && time->hour <= 23 && time->minute <= 59 && time->second <= 59) {
        char comma = (time->second % 2 == 0) ? ':' : ' ';
        snprintf(str, sizeof(str), "%02u%c%02u", (unsigned)time->hour, comma,
                 (unsigned)time->minute);
    }
    ui_write_string(25, 42, str, UI_RGB565(0, 0, 0), color_bg_time, &font76_maple_bold);
}

/**
 * 功能：校验日期及星期是否处于支持范围，并处理闰年二月。
 * 参数：date，待检查的日期。
 * 返回：有效返回非零值，否则返回零。
 */
static int valid_date(const rtc_date_time_t *date) {
    static const uint8_t month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (date == NULL || date->year < 2000 || date->year > 2099 || date->month < 1 ||
        date->month > 12 || date->weekday < 1 || date->weekday > 7) {
        return 0;
    }
    unsigned days = month_days[date->month - 1];
    if (date->month == 2 && date->year % 4 == 0) {
        ++days;
    }
    return date->day >= 1 && date->day <= days;
}

/**
 * 功能：刷新日期和中文星期，日期无效时显示占位内容。
 * 参数：date，待显示的日期和星期。
 * 返回：无。
 */
void main_page_redraw_date(rtc_date_time_t *date) {
    char str[24] = "----/--/-- 星期X";
    if (valid_date(date)) {
        static const char *const weekdays[] = {"一", "二", "三", "四", "五", "六", "日"};
        snprintf(str, sizeof(str), "%04u/%02u/%02u 星期%s", (unsigned)date->year,
                 (unsigned)date->month, (unsigned)date->day, weekdays[date->weekday - 1]);
    }
    ui_write_string(20, 121, str, UI_RGB565(143, 143, 143), color_bg_time, &font24_maple);
}

/**
 * 功能：将范围内的有限读数格式化为整数文本，其余情况输出占位符。
 * 参数：value，待读数值；minimum、maximum，有效范围；str，至少四字节的输出缓冲区。
 * 返回：无。
 */
static void format_reading(float value, float minimum, float maximum, char str[4]) {
    memcpy(str, "--", 3);
    if (isfinite(value) && value >= minimum && value <= maximum) {
        snprintf(str, 4, "%2.0f", (double)value);
    }
}

/**
 * 功能：清除室内温度区域并选择合适字号显示读数。
 * 参数：temperature，摄氏温度；无效或越界时显示占位符。
 * 返回：无。
 */
void main_page_redraw_inner_temperature(float temperature) {
    char str[4];
    format_reading(temperature, -20.0f, 100.0f, str);
    int wide = strlen(str) == 3;
    ui_fill_color(20, 192, 83, 245, color_bg_inner);
    ui_write_string(wide ? 20 : 30, wide ? 198 : 192, str, UI_RGB565(0, 0, 0), color_bg_inner,
                    wide ? &font42_maple : &font54_maple_medium);
}

/**
 * 功能：清除室内湿度区域并选择合适字号显示读数。
 * 参数：humidity，相对湿度百分比；无效或越界时显示占位符。
 * 返回：无。
 */
void main_page_redraw_inner_humidity(float humidity) {
    char str[4];
    format_reading(humidity, 0.0f, 100.0f, str);
    int wide = strlen(str) == 3;
    ui_fill_color(25, 239, 87, 300, color_bg_inner);
    ui_write_string(25, wide ? 249 : 239, str, UI_RGB565(0, 0, 0), color_bg_inner,
                    wide ? &font42_maple : &font62_maple_bold);
}

/**
 * 功能：按字符边界截取城市名称并清除旧文字后逐字重绘。
 * 参数：city，城市名称；为空时使用空文本，中文按 GB2312 编码处理。
 * 返回：无。
 */
void main_page_redraw_outdoor_city(const char *city) {
    char str[9];
    size_t used = 0;
    if (city == NULL) {
        city = "";
    }

    /* 天气接口返回英文城市名称，本地城市标签仍使用 GB2312 编码。 */
    if (strcmp(city, "Nanjing") == 0) {
        city = "\xC4\xCF\xBE\xA9";
    }

    /* 最多显示四个汉字或八个 ASCII 字符，保持 GB2312 双字节字符完整。 */
    while (*city != '\0') {
        unsigned char lead = (unsigned char)*city;
        size_t bytes = (lead >= 0xA1 && lead <= 0xF7) ? 2 : 1;
        if (used + bytes >= sizeof(str) || (bytes == 2 && city[1] == '\0')) {
            break;
        }
        memcpy(str + used, city, bytes);
        used += bytes;
        city += bytes;
    }
    str[used] = '\0';
    /* 清除旧城市名称，防止新名称较短时残留原有字符。 */
    ui_fill_color(127, 168, 222, 191, color_bg_outdoor);
    for (size_t offset = 0; offset < used;) {
        unsigned char lead = (unsigned char)str[offset];
        size_t bytes = (lead >= 0xA1 && lead <= 0xF7) ? 2 : 1;
        char glyph[3] = {str[offset], 0, 0};
        if (bytes == 2) {
            glyph[1] = str[offset + 1];
        }
        /* 旧粗体字库只含中文，ASCII 字符使用常规字体。 */
        const font_t *font = bytes == 2 ? &font24_maple_bold : &font24_maple;
        ui_write_string((uint16_t)(127 + offset * 12), 168, glyph, UI_RGB565(0, 0, 0),
                        color_bg_outdoor, font);
        offset += bytes;
    }
}

/**
 * 功能：清除室外温度区域并选择合适字号显示读数。
 * 参数：temperature，摄氏温度；无效或越界时显示占位符。
 * 返回：无。
 */
void main_page_redraw_outdoor_temperature(float temperature) {
    char str[4];
    format_reading(temperature, -20.0f, 100.0f, str);
    int wide = strlen(str) == 3;
    ui_fill_color(127, 192, 189, 245, color_bg_outdoor);
    ui_write_string(wide ? 127 : 135, wide ? 198 : 192, str, UI_RGB565(0, 0, 0), color_bg_outdoor,
                    wide ? &font42_maple : &font54_maple_medium);
}

/**
 * 功能：根据天气代码选择并绘制天气图标。
 * 参数：code，心知天气代码；未匹配时使用缺省图标。
 * 返回：无。
 */
void main_page_redraw_outdoor_weather_icon(const int code) {
    const image_t *icon = NULL;
    if (code == 0 || code == 2 || code == 38) {
        icon = &icon_qing;
    } else if (code == 1 || code == 3) {
        icon = &icon_yueliang;
    } else if (code == 4 || code == 9 || code == 30) {
        icon = &icon_yintian;
    } else if (code == 5 || code == 6 || code == 7 || code == 8) {
        icon = &icon_duoyun;
    } else if (code == 10 || code == 13 || code == 14 || code == 15 || code == 16 || code == 17 ||
               code == 18 || code == 19) {
        icon = &icon_zhongyu;
    } else if (code == 11 || code == 12) {
        icon = &icon_leizhenyu;
    } else if (code == 20 || code == 21 || code == 22 || code == 23 || code == 24 || code == 25) {
        icon = &icon_zhongxue;
    } else {             // 扬沙、龙卷风等
        icon = &icon_na; // 未匹配的天气使用缺省图标
    }

    ui_draw_image(166, 240, icon);
}
