#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "esp_at_parse.h"

/** 功能：验证实际有界 HTTP 命令的模块长度边界；参数：无；返回：通过返回零。 */
int main(void) {
    char url[260];
    char command[300];
    memset(url, 'a', 224U);
    memcpy(url, "http://", 7U);
    url[224] = '\0';
    assert(esp_at_build_http_get_bounded(url, 1000U, command, sizeof command));
    assert(strlen(command) == 255U);
    assert(memcmp(command, "AT+HTTPCGET=\"", 13U) == 0);
    assert(strcmp(command + 237U, "\",2048,2048,1000\r\n") == 0);
    url[224] = 'a';
    url[225] = '\0';
    assert(!esp_at_build_http_get_bounded(url, 1000U, command, sizeof command));
    url[224] = '\0';
    assert(!esp_at_build_http_get_bounded(url, 1000U, command, 255U));
    assert(esp_at_build_http_get_bounded(url, 1000U, command, 256U));
    url[220] = '"';
    assert(!esp_at_build_http_get_bounded(url, 1000U, command, sizeof command));
    url[220] = 'a';
    assert(!esp_at_build_http_get_bounded("http://a/\r\nAT", 1000U, command, sizeof command));
    assert(!esp_at_build_http_get_bounded(url, 99U, command, sizeof command));
    assert(!esp_at_build_http_get_bounded(url, 3001U, command, sizeof command));
    assert(!esp_at_build_http_get_bounded(NULL, 1000U, command, sizeof command));
    assert(!esp_at_build_http_get_bounded(url, 1000U, NULL, sizeof command));
    puts("OTA discovery actual HTTP command boundary passed");
    return 0;
}
