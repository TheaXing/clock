#include "ota_http.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char tag[] = "\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"";
static const uint8_t payload[] = {0, 10, 13, 'O', 'K', 255, 3, 0};

/** 功能：构造独立的标准 Range 应答；参数：输出和状态码；返回：长度。 */
static size_t response(char *out, unsigned status, const char *extra) {
    int n = sprintf_s(out, 4096,
        "HTTP/1.1 %u Test\r\nContent-Length: 8\r\nContent-Range: bytes 256-263/264\r\nETag: %s\r\n%s\r\n",
        status, tag, extra);
    assert(n > 0);
    memcpy(out + n, payload, sizeof payload);
    return (size_t)n + sizeof payload;
}

/** 功能：确认任意分包和二进制字节能正确还原；参数：无；返回：无。 */
static void test_binary_splits(void) {
    char wire[4096];
    size_t n = response(wire, 206, "");
    for (size_t split = 0; split <= n; ++split) {
        ota_http_t p;
        uint8_t body[8];
        ota_http_init(&p, 256, 8, 264, tag, body);
        assert(ota_http_feed(&p, wire, split));
        assert(ota_http_feed(&p, wire + split, n - split));
        assert(p.complete && !p.failed && p.total == 264);
        assert(memcmp(body, payload, 8) == 0);
        assert(!ota_http_feed(&p, "x", 1));
    }
}

/** 功能：拒绝错误状态、重复头、编码和跨包身份；参数：无；返回：无。 */
static void test_rejections(void) {
    const char *bad[] = {"Content-Length: 8\r\n", "Transfer-Encoding: chunked\r\n",
        "Content-Encoding: gzip\r\n", "Content-Range: bytes 256-263/264\r\n",
        "ETag: \"bad\"\r\n", "Content-Type: multipart/byteranges\r\n",
        "Content-Type: Multipart/byteranges\r\n"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        char wire[4096];
        uint8_t body[8];
        ota_http_t p;
        size_t n = response(wire, 206, bad[i]);
        ota_http_init(&p, 256, 8, 264, tag, body);
        assert(!ota_http_feed(&p, wire, n));
        assert(p.failed && !p.complete);
    }
    char wire[4096];
    uint8_t body[8];
    ota_http_t p;
    size_t n = response(wire, 200, "");
    ota_http_init(&p, 256, 8, 264, tag, body);
    assert(!ota_http_feed(&p, wire, n));
    n = response(wire, 206, "");
    ota_http_init(&p, 256, 8, 265, tag, body);
    assert(!ota_http_feed(&p, wire, n));
    ota_http_init(&p, 255, 8, 264, tag, body);
    assert(!ota_http_feed(&p, wire, n));
    ota_http_init(&p, 256, 8, 264, NULL, body);
    assert(ota_http_feed(&p, wire, n - 1));
    assert(!p.complete);
}

/** 功能：运行 HTTP 解析回归；参数：无；返回：成功为零。 */
int main(void) {
    test_binary_splits();
    test_rejections();
    puts("OTA HTTP binary splits, exact ranges, identity and rejection tests passed");
    return 0;
}
