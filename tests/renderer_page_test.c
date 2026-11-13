#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "page.h"

#define FONT(name, pixels) const font_t name = {NULL, NULL, NULL, pixels}
FONT(font16_maple, 16);
FONT(font24_maple, 24);
FONT(font24_maple_bold, 24);
FONT(font32_maple, 32);
FONT(font42_maple, 42);
FONT(font54_maple_medium, 54);
FONT(font62_maple_bold, 62);
FONT(font76_maple_bold, 76);
#define IMAGE(name) const image_t name = {1, 1, (const uint8_t *)"xx"}
IMAGE(icon_wifi);
IMAGE(icon_wenduji);
IMAGE(icon_qing);
IMAGE(icon_yueliang);
IMAGE(icon_yintian);
IMAGE(icon_duoyun);
IMAGE(icon_zhongyu);
IMAGE(icon_leizhenyu);
IMAGE(icon_zhongxue);
IMAGE(icon_na);

static char text[64];
static int city_clears;
static int numeric_clears, numeric_clear_pending;
static const font_t *last_font;
static int startup_mode, startup_time, startup_date, startup_inner_temperature;
static int startup_inner_humidity, startup_outdoor_temperature;
void ui_fill_color(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    if (startup_mode) return;
    if (x1 == 127 && y1 == 168)
    {
        assert(x2 == 222 && y2 == 191 && color == mkcolor(253, 135, 75));
        ++city_clears;
        text[0] = '\0';
    }
    else
    {
        if (x1 == 20)
            assert(y1 == 192 && x2 == 83 && y2 == 245 && color == mkcolor(136,217,234));
        else if (x1 == 25)
            assert(y1 == 239 && x2 == 87 && y2 == 300 && color == mkcolor(136,217,234));
        else
            assert(x1 == 127 && y1 == 192 && x2 == 189 && y2 == 245 && color == mkcolor(253,135,75));
        ++numeric_clears;
        numeric_clear_pending = 1;
    }
}
void ui_draw_image(uint16_t x, uint16_t y, const image_t *image)
{
    (void)x; (void)y; (void)image;
}
void ui_write_string(uint16_t x, uint16_t y, const char *str,
                     uint16_t color, uint16_t bg_color, const font_t *font)
{
    (void)color; (void)bg_color;
    assert(str != NULL);
    if (startup_mode)
    {
        if (x == 25 && y == 42) { assert(strcmp(str, "  :  ") == 0); ++startup_time; }
        if (x == 20 && y == 121) { assert(strncmp(str, "----/--/-- ", 11) == 0); ++startup_date; }
        if (x == 30 && y == 192) { assert(strcmp(str, "--") == 0); ++startup_inner_temperature; }
        if (x == 25 && y == 239) { assert(strcmp(str, "--") == 0); ++startup_inner_humidity; }
        if (x == 135 && y == 192) { assert(strcmp(str, "--") == 0); ++startup_outdoor_temperature; }
        return;
    }
    if (y == 192 || y == 198 || y == 239 || y == 249)
    {
        assert(numeric_clear_pending);
        numeric_clear_pending = 0;
        assert(strlen(str) <= 3);
        if (strlen(str) == 3)
            assert(font == &font42_maple && ((x == 20 && y == 198) || (x == 127 && y == 198) || (x == 25 && y == 249)));
        else if (y == 239)
            assert(x == 25 && font == &font62_maple_bold);
        else
            assert(y == 192 && (x == 30 || x == 135) && font == &font54_maple_medium);
        strcpy(text, str);
        last_font = font;
    }
    else if (y == 168)
    {
        assert(x == 127 + strlen(text) * 12);
        assert(font == ((unsigned char)*str >= 0xA1 ? &font24_maple_bold : &font24_maple));
        assert(strlen(str) + strlen(text) < sizeof(text));
        strcat(text, str);
    }
    else
    {
        assert((x == 25 && y == 42) || (x == 20 && y == 121));
        assert(strlen(str) < sizeof(text));
        strcpy(text, str);
    }
}

int main(void)
{
    main_page_redraw_inner_humidity(999.9f);
    assert(strcmp(text, "--") == 0);
    main_page_redraw_inner_humidity(NAN);
    assert(strcmp(text, "--") == 0);
    main_page_redraw_inner_humidity(42.0f);
    assert(strcmp(text, "42") == 0);
    main_page_redraw_inner_humidity(0.0f);
    assert(strcmp(text, " 0") == 0);
    main_page_redraw_inner_humidity(99.5f);
    assert(strcmp(text, "100") == 0 && last_font == &font42_maple);
    main_page_redraw_inner_humidity(100.0f);
    assert(strcmp(text, "100") == 0);
    main_page_redraw_inner_humidity(42.0f);
    assert(strcmp(text, "42") == 0 && last_font == &font62_maple_bold);
    main_page_redraw_inner_humidity(100.1f);
    assert(strcmp(text, "--") == 0);
    main_page_redraw_inner_temperature(25.0f);
    assert(strcmp(text, "25") == 0 && last_font == &font54_maple_medium);
    main_page_redraw_inner_temperature(-12.0f);
    assert(strcmp(text, "-12") == 0 && last_font == &font42_maple);
    main_page_redraw_inner_temperature(100.0f);
    assert(strcmp(text, "100") == 0);
    main_page_redraw_inner_temperature(25.0f);
    assert(strcmp(text, "25") == 0 && last_font == &font54_maple_medium);
    main_page_redraw_inner_temperature(NAN);
    assert(strcmp(text, "--") == 0);
    main_page_redraw_outdoor_temperature(-12.0f);
    assert(strcmp(text, "-12") == 0 && last_font == &font42_maple);
    main_page_redraw_outdoor_temperature(100.0f);
    assert(strcmp(text, "100") == 0);
    main_page_redraw_outdoor_temperature(25.0f);
    assert(strcmp(text, "25") == 0 && last_font == &font54_maple_medium);
    main_page_redraw_outdoor_temperature(INFINITY);
    assert(strcmp(text, "--") == 0);
    main_page_redraw_outdoor_temperature(-20.1f);
    assert(strcmp(text, "--") == 0);
    assert(numeric_clears == 18 && !numeric_clear_pending);

    startup_mode = 1;
    main_page_display();
    startup_mode = 0;
    assert(startup_time == 1 && startup_date == 1);
    assert(startup_inner_temperature == 1 && startup_inner_humidity == 1 && startup_outdoor_temperature == 1);

    /* One ASCII plus four GBK characters: the last pair cannot fit. */
    main_page_redraw_outdoor_city("A\xC4\xCF\xBE\xA9\xC4\xCF\xBE\xA9");
    assert(strcmp(text, "A\xC4\xCF\xBE\xA9\xC4\xCF") == 0);
    assert(city_clears == 1);
    main_page_redraw_outdoor_city("X");
    assert(strcmp(text, "X") == 0 && city_clears == 2);
    main_page_redraw_outdoor_city(NULL);
    assert(strcmp(text, "") == 0 && city_clears == 3);
    main_page_redraw_outdoor_city("Nanjing");
    assert(strcmp(text, "\xC4\xCF\xBE\xA9") == 0 && city_clears == 4);

    rtc_date_time_t time = {0};
    time.hour = 23; time.minute = 59; time.second = 58;
    main_page_redraw_time(&time);
    assert(strcmp(text, "23:59") == 0);
    time.second = 59;
    main_page_redraw_time(&time);
    assert(strcmp(text, "23 59") == 0);
    time.hour = 255;
    main_page_redraw_time(&time);
    assert(strcmp(text, "  :  ") == 0);
    main_page_redraw_time(NULL);
    assert(strcmp(text, "  :  ") == 0);

    time.year = 2024; time.month = 2; time.day = 29; time.weekday = 4;
    main_page_redraw_date(&time);
    assert(strcmp(text, "2024/02/29 \xD0\xC7\xC6\xDA\xCB\xC4") == 0);
    time.year = 2023;
    main_page_redraw_date(&time);
    assert(strcmp(text, "----/--/-- \xD0\xC7\xC6\xDA" "X") == 0);
    time.year = 65535; time.month = 255; time.day = 255;
    main_page_redraw_date(&time);
    assert(strcmp(text, "----/--/-- \xD0\xC7\xC6\xDA" "X") == 0);
    main_page_redraw_date(NULL);
    assert(strcmp(text, "----/--/-- \xD0\xC7\xC6\xDA" "X") == 0);
    puts("pages: all tests passed");
    return 0;
}
