#ifndef CLOCK_HTTP_H
#define CLOCK_HTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CLOCK_HTTP_TARGET_CAPACITY 64u
#define CLOCK_HTTP_BODY_MAX 384u
#define CLOCK_HTTP_LINE_CAPACITY 256u
#define CLOCK_HTTP_HEADERS_MAX 1536u

typedef enum {
    CLOCK_HTTP_NEED_MORE = 0,
    CLOCK_HTTP_READY,
    CLOCK_HTTP_BAD_REQUEST,
    CLOCK_HTTP_TOO_LARGE,
    CLOCK_HTTP_METHOD_NOT_ALLOWED
} clock_http_status_t;

typedef enum { CLOCK_HTTP_GET = 0, CLOCK_HTTP_POST } clock_http_method_t;

/* One request per connection. Public request fields are valid only at READY.
 * A terminal error is sticky. Header bytes include the request line and CRLFs.
 * Body and target are NUL terminated; body_length excludes the terminator.
 * Duplicate framing/security headers and all Transfer-Encoding are rejected.
 * Host is optional here; routing and authorization belong to the caller. */
typedef struct {
    char target[CLOCK_HTTP_TARGET_CAPACITY];
    char body[CLOCK_HTTP_BODY_MAX + 1u];
    char line[CLOCK_HTTP_LINE_CAPACITY];
    uint16_t body_length;
    uint16_t content_length;
    uint16_t header_bytes;
    uint16_t line_length;
    clock_http_status_t status;
    clock_http_method_t method;
    uint8_t state;
    uint8_t seen_headers;
    bool has_clock_request;
    bool form_content_type;
    bool line_overflow;
    bool line_colon;
    bool saw_cr;
} clock_http_parser_t;

void clock_http_init(clock_http_parser_t *parser);
clock_http_status_t clock_http_feed(clock_http_parser_t *parser,
                                    const uint8_t *data, size_t length);

#endif
