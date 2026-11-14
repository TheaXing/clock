#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "wifi_form.h"
#include "clock_http.h"

/** 功能：检查非法表单不会被接受；参数：body，正文；返回：无。 */
static void reject(const char *body) {
    char ssid[33], password[64];
    assert(!wifi_form_parse(body, 123U, ssid, password));
}

/** 功能：测试严格解码和凭据边界；参数：无；返回：通过时为零。 */
int main(void) {
    char ssid[33], password[64], body[400];
    assert(wifi_form_parse("session=123&ssid=%E5%AE%B6%E4%B8%AD+WiFi&password=a%2Bb%26c%3Dd%25", 123U, ssid, password));
    assert(strcmp(ssid, "家中 WiFi") == 0 && strcmp(password, "a+b&c=d%") == 0);
    assert(wifi_form_parse("password=&ssid=Open&session=123", 123U, ssid, password));
    assert(wifi_form_parse("session=123&ssid=家家家家家家家家家家ab&password=", 123U, ssid, password));
    assert(strlen(ssid) == 32U);
    reject("session=123&ssid=家家家家家家家家家家家&password=");
    reject("session=124&ssid=x&password=");
    reject("session=123&ssid=x&password=1234567");
    reject("session=123&ssid=&password=");
    reject("session=123&ssid=x&password=&ssid=y");
    reject("session=123&ssid=x&password=&extra=y");
    reject("session=123&ssid=x");
    reject("session=123&ssid=x&password=&");
    reject("session=123&ssid=%00&password=");
    reject("session=123&ssid=%0a&password=");
    reject("session=123&ssid=%7f&password=");
    reject("session=123&ssid=%C0%AF&password=");
    reject("session=123&ssid=%ED%A0%80&password=");
    reject("session=123&ssid=%F4%90%80%80&password=");
    reject("session=123&ssid=%C2%85&password=");
    reject("session=123&ssid=%E5%AE&password=");
    reject("session=123&ssid=%GG&password=");
    reject("session=123&ssid=%&password=");
    reject("session=123&ssid=%A0&password=");
    reject("session=123&ssid=%F0%80%80%AF&password=");
    reject("session=123&ssid=%F5%80%80%80&password=");
    reject("session=4294967296&ssid=x&password=");
    reject("session=+123&ssid=x&password=");
    reject("session=123&ssid=x&password=1234567%0A");
    reject("session=123&ssid=x&password=1234567%C3%A9");
    memcpy(body, "session=%34%32%39%34%39%36%37%32%39%35&ssid=", 45U);
    for (unsigned i = 0U; i < 32U; ++i) {
        memcpy(body + strlen(body), "%41", 4U);
    }
    memcpy(body + strlen(body), "&password=", 11U);
    for (unsigned i = 0U; i < 63U; ++i) {
        memcpy(body + strlen(body), "%21", 4U);
    }
    assert(strlen(body) == 339U && strlen(body) <= CLOCK_HTTP_BODY_MAX);
    assert(wifi_form_parse(body, UINT32_MAX, ssid, password));
    assert(strlen(ssid) == 32U && strlen(password) == 63U);
    memcpy(body + strlen(body), "%21", 4U);
    assert(!wifi_form_parse(body, UINT32_MAX, ssid, password));
    reject("session=123&ssid=12345678901234567890123456789012345&password=");
    assert(!wifi_form_parse(NULL, 123U, ssid, password));
    assert(!wifi_form_parse("session=0&ssid=x&password=", 0U, ssid, password));
    puts("Wi-Fi form encoding and boundary tests passed");
    return 0;
}
