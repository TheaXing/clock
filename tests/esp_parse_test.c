#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "esp_at_parse.h"

static void sntp_dates(void)
{
    esp_date_time_t date = {0};
    assert(esp_at_parse_sntp("\r\n+CIPSNTPTIME:Thu Feb 29 23:59:59 2024\r\n\r\nOK\r\n", &date));
    assert(date.year == 2024 && date.month == 2 && date.day == 29);
    assert(date.weekday == 4 && date.hour == 23 && date.minute == 59 && date.second == 59);
    assert(esp_at_parse_sntp("+CIPSNTPTIME:Sun Jul  7 04:05:06 2024\r\n", &date));
    assert(date.day == 7 && date.weekday == 7);
    const char *invalid[] = {
        "OK\r\n", "+CIPSNTPTIME:Wed Feb 29 12:00:00 2023\r\n",
        "+CIPSNTPTIME:Fri Apr 31 12:00:00 2026\r\n",
        "+CIPSNTPTIME:Fri Xxx 01 12:00:00 2026\r\n",
        "+CIPSNTPTIME:Xxx Jan 01 12:00:00 2026\r\n",
        "+CIPSNTPTIME:Fri Jan 00 12:00:00 2026\r\n",
        "+CIPSNTPTIME:Fri Jan 01 24:00:00 2026\r\n",
        "+CIPSNTPTIME:Fri Jan 01 12:60:00 2026\r\n",
        "+CIPSNTPTIME:Fri Jan 01 12:00:60 2026\r\n",
        "+CIPSNTPTIME:Thu Jan 01 00:00:00 1970\r\n",
        "+CIPSNTPTIME:Fri Jan 99999999999 12:00:00 2026\r\n",
        "+CIPSNTPTIME:Fri Jan 01 12:00:00 2026garbage\r\n"
    };
    for (size_t n = 0; n < sizeof invalid / sizeof invalid[0]; ++n) {
        esp_date_time_t before = date;
        assert(!esp_at_parse_sntp(invalid[n], &date));
        assert(memcmp(&date, &before, sizeof date) == 0);
    }
    assert(!esp_at_parse_sntp(NULL, &date));
    assert(!esp_at_parse_sntp("x", NULL));
}

static void wifi_fields(void)
{
    esp_wifi_info_t info = {0};
    assert(esp_at_parse_cwstate("+CWSTATE:2,\"Clock AP\"\r\nOK\r\n", &info));
    assert(info.connected && strcmp(info.ssid, "Clock AP") == 0);
    assert(esp_at_parse_cwjap("+CWJAP:\"Clock AP\",\"da:b5:3a:e3:2f:60\",9,-48,0,1,3,0,1\r\n", &info));
    assert(info.connected && info.channel == 9 && info.rssi == -48);
    assert(strcmp(info.bssid, "da:b5:3a:e3:2f:60") == 0);
    assert(esp_at_parse_cwstate("+CWSTATE:0,\"\"\r\n", &info));
    assert(!info.connected && info.ssid[0] == '\0' && info.bssid[0] == '\0');
    assert(!esp_at_parse_cwstate("+CWSTATE:9,\"AP\"\r\n", &info));
    assert(!esp_at_parse_cwjap("+CWJAP:\"AP\",\"not-a-mac\",9,-48\r\n", &info));
    assert(!esp_at_parse_cwjap("+CWJAP:\"AP\",\"da:b5:3a:e3:2f:60\",99999999999,-48\r\n", &info));
    assert(!esp_at_parse_cwjap("+CWJAP:\"AP\",\"da:b5:3a:e3:2f:60\",9,-48oops\r\n", &info));
    assert(!esp_at_parse_cwstate("echo+CWSTATE:2,\"AP\"\r\n", &info));
}

static void quoted_commands(void)
{
    char out[40];
    assert(esp_at_quote("a\"b\\c,d", 32, out, sizeof out));
    assert(strcmp(out, "a\\\"b\\\\c\\,d") == 0);
    assert(!esp_at_quote("a\r\nAT+RESTORE", 32, out, sizeof out));
    assert(!esp_at_quote("12345", 4, out, sizeof out));
    assert(!esp_at_quote("\\", 2, out, 2));
    assert(esp_at_valid_mac("00:ab:CD:12:34:ff"));
    assert(!esp_at_valid_mac("00:ab:CD:12:34:fg"));
    assert(!esp_at_valid_mac("00:ab:CD:12:34:ffjunk"));
}

static void http_frames(void)
{
    char body[16];
    assert(esp_at_extract_http("+HTTPCLIENT:3,abc\r\n+HTTPCLIENT:3,def\r\nOK\r\n", body, sizeof body));
    assert(strcmp(body, "abcdef") == 0);
    assert(esp_at_extract_http("+HTTPCLIENT:8,\r\nOK\r\nx!\r\nOK\r\n", body, sizeof body));
    assert(strcmp(body, "\r\nOK\r\nx!") == 0);
    assert(!esp_at_extract_http("+HTTPCLIENT:3,abc\r\n+HTTPCLIENT:3,def\r\nOK\r\n", body, 6));
    assert(esp_at_extract_http("+HTTPCLIENT:3,abc\r\n+HTTPCLIENT:3,def\r\nOK\r\n", body, 7));
    assert(!esp_at_extract_http("+HTTPCLIENT:20,abc\r\nOK\r\n", body, sizeof body));
    assert(!esp_at_extract_http("+HTTPCLIENT:3,abc\r\nOK\r\n", NULL, 16));
    assert(!esp_at_extract_http("+HTTPCLIENT:3,abc\r\nOK\r\n", body, 0));
    assert(esp_at_validate_http("+HTTPCLIENT:7,{\"a\":1}\r\nOK\r\n"));
    assert(esp_at_validate_http("+HTTPCLIENT:3,abc\r\n+HTTPCLIENT:3,def\r\nOK\r\n"));
    assert(esp_at_validate_http("+HTTPCLIENT:8,\r\nOK\r\nx!\r\nOK\r\n"));
    assert(!esp_at_validate_http("OK\r\n"));
    assert(!esp_at_validate_http("+HTTPCLIENT:20,abc\r\nOK\r\n"));
    assert(!esp_at_validate_http("+HTTPCLIENT:0,\r\nOK\r\n"));
    assert(!esp_at_validate_http("+HTTPCLIENT:9999999999999999,abc\r\nOK\r\n"));
}

int main(void)
{
    sntp_dates(); wifi_fields(); quoted_commands(); http_frames();
    puts("esp parser tests passed");
    return 0;
}
