#include "esp_at_parse.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/** 功能：验证 SNTP 日期及闰年边界；参数：无；返回：无。 */
static void sntp_dates(void) {
    esp_date_time_t date = {0};
    assert(esp_at_parse_sntp("\r\n+CIPSNTPTIME:Thu Feb 29 23:59:59 2024\r\n\r\nOK\r\n", &date));
    assert(date.year == 2024 && date.month == 2 && date.day == 29);
    assert(date.weekday == 4 && date.hour == 23 && date.minute == 59 && date.second == 59);
    assert(esp_at_parse_sntp("+CIPSNTPTIME:Sun Jul  7 04:05:06 2024\r\n", &date));
    assert(date.day == 7 && date.weekday == 7);
    const char *invalid[] = {"OK\r\n",
                             "+CIPSNTPTIME:Wed Feb 29 12:00:00 2023\r\n",
                             "+CIPSNTPTIME:Fri Apr 31 12:00:00 2026\r\n",
                             "+CIPSNTPTIME:Fri Xxx 01 12:00:00 2026\r\n",
                             "+CIPSNTPTIME:Xxx Jan 01 12:00:00 2026\r\n",
                             "+CIPSNTPTIME:Fri Jan 00 12:00:00 2026\r\n",
                             "+CIPSNTPTIME:Fri Jan 01 24:00:00 2026\r\n",
                             "+CIPSNTPTIME:Fri Jan 01 12:60:00 2026\r\n",
                             "+CIPSNTPTIME:Fri Jan 01 12:00:60 2026\r\n",
                             "+CIPSNTPTIME:Thu Jan 01 00:00:00 1970\r\n",
                             "+CIPSNTPTIME:Fri Jan 99999999999 12:00:00 2026\r\n",
                             "+CIPSNTPTIME:Fri Jan 01 12:00:00 2026garbage\r\n"};
    for (size_t n = 0; n < sizeof invalid / sizeof invalid[0]; ++n) {
        esp_date_time_t before = date;
        assert(!esp_at_parse_sntp(invalid[n], &date));
        assert(memcmp(&date, &before, sizeof date) == 0);
    }
    assert(!esp_at_parse_sntp(NULL, &date));
    assert(!esp_at_parse_sntp("x", NULL));
}

/** 功能：验证无线状态及热点信息字段；参数：无；返回：无。 */
static void wifi_fields(void) {
    esp_wifi_info_t info = {0};
    assert(esp_at_parse_cwstate("+CWSTATE:2,\"Clock AP\"\r\nOK\r\n", &info));
    assert(info.connected && strcmp(info.ssid, "Clock AP") == 0);
    assert(
        esp_at_parse_cwjap("+CWJAP:\"Clock AP\",\"da:b5:3a:e3:2f:60\",9,-48,0,1,3,0,1\r\n", &info));
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

/** 功能：验证 AT 参数转义和 MAC 地址校验；参数：无；返回：无。 */
static void quoted_commands(void) {
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

/** 功能：验证长度定界 HTTP 多帧正文；参数：无；返回：无。 */
static void http_frames(void) {
    char body[16];
    assert(
        esp_at_extract_http("+HTTPCLIENT:3,abc\r\n+HTTPCLIENT:3,def\r\nOK\r\n", body, sizeof body));
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

/** 功能：验证终端地址及非法 IPv4 拒绝；参数：无；返回：无。 */
static void station_ip(void) {
    char ip[16] = "unchanged";
    assert(esp_at_parse_station_ip(
        "+CIFSR:APIP,\"192.168.4.1\"\r\n+CIFSR:STAIP,\"192.168.1.55\"\r\nOK\r\n", ip));
    assert(strcmp(ip, "192.168.1.55") == 0);
    const char *invalid[] = {
        "+CIFSR:APIP,\"192.168.4.1\"\r\n",  "+CIFSR:STAIP,\"256.1.1.1\"\r\n",
        "+CIFSR:STAIP,\"0.0.0.0\"\r\n",     "+CIFSR:STAIP,\"1.2.3\"\r\n",
        "+CIFSR:STAIP,\"1.2.3.4.5\"\r\n",   "+CIFSR:STAIP,\"1.2.3.4\"oops\r\n",
        "echo+CIFSR:STAIP,\"1.2.3.4\"\r\n", "+CIFSR:STAIP,\"1.2.3.9999999999\"\r\n"};
    for (size_t n = 0U; n < sizeof invalid / sizeof invalid[0]; ++n) {
        assert(!esp_at_parse_station_ip(invalid[n], ip));
        assert(strcmp(ip, "192.168.1.55") == 0);
    }
    assert(!esp_at_parse_station_ip(NULL, ip));
    assert(!esp_at_parse_station_ip("x", NULL));
}

/** 功能：验证热点地址、DHCP 客户端及自动连接状态应答；参数：无；返回：无。 */
static void setup_fields(void) {
    char ip[16] = "unchanged";
    bool connected = false;
    assert(esp_at_parse_ap_ip("+CIFSR:APIP,\"192.168.4.1\"\r\n+CIFSR:STAIP,\"1.2.3.4\"\r\n", ip));
    assert(strcmp(ip, "192.168.4.1") == 0);
    assert(!esp_at_parse_ap_ip("+CIFSR:STAIP,\"1.2.3.4\"\r\n", ip));
    assert(!esp_at_parse_ap_ip("+CIFSR:APIP,\"0.0.0.0\"\r\n", ip));
    assert(esp_at_parse_setup_clients("\r\nOK\r\n", &connected) && !connected);
    assert(
        esp_at_parse_setup_clients("+CWLIF:192.168.4.2,aa:bb:cc:dd:ee:ff\r\nOK\r\n", &connected) &&
        connected);
    const char *invalid[] = {"+CWLIF:256.1.1.1,aa:bb:cc:dd:ee:ff\r\nOK\r\n",
                             "+CWLIF:0.0.0.0,aa:bb:cc:dd:ee:ff\r\nOK\r\n",
                             "+CWLIF:192.168.4.2,aa:bb:cc:dd:ee:ffjunk\r\nOK\r\n",
                             "+CWLIF:192.168.4.2,bad\r\nOK\r\n",
                             "echo+CWLIF:192.168.4.2,aa:bb:cc:dd:ee:ff\r\nOK\r\n",
                             "garbage\r\nOK\r\n",
                             "ERROR\r\n"};
    for (size_t n = 0U; n < sizeof invalid / sizeof invalid[0]; ++n) {
        assert(!esp_at_parse_setup_clients(invalid[n], &connected));
    }
    assert(!esp_at_parse_setup_clients(NULL, &connected));
    assert(!esp_at_parse_setup_clients("OK\r\n", NULL));
    assert(esp_at_parse_autoconnect("+CWAUTOCONN:1\r\nOK\r\n", &connected) && connected);
    assert(esp_at_parse_autoconnect("+CWAUTOCONN:0\r\nOK\r\n", &connected) && !connected);
    assert(!esp_at_parse_autoconnect("+CWAUTOCONN:10\r\nOK\r\n", &connected));
    assert(!esp_at_parse_autoconnect("OK\r\n", &connected));
}

/** 功能：执行全部可移植解析回归；参数：无；返回：对应模拟结果或测试退出码。 */
int main(void) {
    setup_fields();
    sntp_dates();
    wifi_fields();
    quoted_commands();
    http_frames();
    station_ip();
    puts("esp parser tests passed");
    return 0;
}
