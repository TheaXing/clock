#include "boot_net.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** 区分远端关闭通知与最后一次读取应答的先后顺序。 */
typedef enum {
    CLOSE_NOTICE_ON_PENDING,
    CLOSE_NOTICE_BEFORE_OK,
    CLOSE_NOTICE_AFTER_OK,
    CLOSE_NOTICE_OMITTED
} close_notice_t;

/** 区分关闭失败后查询连接状态的合法、异常及超时应答。 */
typedef enum {
    STATE_REPLY_EMPTY,
    STATE_REPLY_ACTIVE,
    STATE_REPLY_ERROR,
    STATE_REPLY_TIMEOUT,
    STATE_REPLY_MALFORMED,
    STATE_REPLY_UNKNOWN,
    STATE_REPLY_NO_OK,
    STATE_REPLY_NUL,
    STATE_REPLY_TOO_LONG
} state_reply_t;

/** 区分正文读完后的缓存查询应答。 */
typedef enum {
    PENDING_REPLY_NORMAL,
    PENDING_REPLY_EMPTY_SLOT,
    PENDING_REPLY_ERROR,
    PENDING_REPLY_TIMEOUT,
    PENDING_REPLY_CORRUPTED_CLOSED,
    PENDING_REPLY_INTACT_CLOSED
} pending_reply_t;

/** 模拟真实 AT 字节流和被动 TCP 缓存。 */
typedef struct {
    uint8_t queue[16384]; // 待接收 UART 字节
    size_t read; // UART 队列读取位置
    size_t used; // UART 队列写入位置
    uint8_t wire[8192]; // ESP 缓存的 HTTP 应答
    size_t consumed; // ESP 缓存消费位置
    size_t wire_size; // ESP 缓存总长度
    uint32_t time; // 模拟单调时钟
    bool changed; // 是否模拟服务端身份变化
    size_t input_remaining; // ESP 原始数据窗口尚需接收的字节数
    bool dirty_line; // 填充溢出后是否存在尚未结束的命令行
    unsigned reset_commands; // 实际成功解析的复位命令数
    uint32_t byte_interval; // UART 字节之间的模拟毫秒间隔
    uint32_t next_byte_at; // 下一字节允许到达的模拟时刻
    uint32_t package_total; // 模拟固件包总长，零表示原有的一千零二十四字节
    bool omit_tail_closed; // 仅末尾范围省略远端关闭通知
    bool tail_range; // 当前应答是否为固件正文的最后范围
    unsigned close_commands; // 已发送的主动关闭命令数
    unsigned close_acknowledgements; // 主动关闭成功应答数
    unsigned state_queries; // 已发送的连接状态查询数
    unsigned empty_state_acknowledgements; // 无连接查询的完整成功应答数
    close_notice_t close_notice; // 最后一次拉取附近的关闭通知顺序
    bool close_error; // 是否拒绝主动关闭命令
    bool close_without_notice; // 主动关闭成功时是否只应答成功行
    bool close_no_acknowledgement; // 主动关闭时是否完全不应答
    state_reply_t state_reply; // 关闭失败后的连接状态查询应答
    bool pad_tail_to_boundary; // 是否将末尾应答填充到二百五十六字节边界附近
    int tail_boundary_delta; // 应答总长相对于边界的字节差
    size_t tail_wire_size; // 最近末尾范围实际生成的应答总长度
    size_t truncated_tail_bytes; // 从最后正文末尾截断的字节数
    bool wrong_tail_range; // 是否篡改最后范围的起始偏移
    size_t extra_tail_bytes; // 最后范围正文后额外发送的字节数
    pending_reply_t final_pending_reply; // 最后范围耗尽缓存后的查询应答
    unsigned payload_timeout_after; // 非零时在指定正文接收调用处模拟一次超时
    unsigned payload_receives; // 最后范围正文接收调用次数
    bool payload_timeout_fired; // 是否已经触发正文超时且留下迟到数据
    bool fail_close_transport; // 关闭命令开始时模拟底层锁存故障
    bool transport_failed; // 本轮底层传输已失效
} fixture_t;

/** 功能：追加精确UART字节；参数：夹具和字节；返回：无。 */
static void queue(fixture_t *f, const void *bytes, size_t n) {
    if (f->read != 0U) {
        memmove(f->queue, f->queue + f->read, f->used - f->read);
        f->used -= f->read;
        f->read = 0U;
    }
    assert(f->used + n < sizeof f->queue);
    if (f->read == f->used) {
        f->next_byte_at = f->time + f->byte_interval;
    }
    memcpy(f->queue + f->used, bytes, n);
    f->used += n;
}

/** 功能：按实际命令生成应答；参数：夹具和发送内容；返回：成功为真。 */
static bool send_bytes(void *context, const void *data, size_t n) {
    fixture_t *f = context;
    const uint8_t *raw = data;
    if (f->input_remaining != 0U) {
        size_t take = n < f->input_remaining ? n : f->input_remaining;
        f->input_remaining -= take;
        raw += take;
        n -= take;
        if (f->input_remaining == 0U) {
            queue(f, "\r\nSEND OK\r\n", 11U);
        }
        if (n == 0U) {
            return true;
        }
    }
    if (f->dirty_line || raw[0] == ' ' || raw[0] == '\r' || raw[0] == '\n') {
        for (size_t i = 0U; i < n; ++i) {
            if (raw[i] == '\n') {
                if (f->dirty_line) {
                    queue(f, "\r\nERROR\r\n", 9U);
                }
                f->dirty_line = false;
            } else if (raw[i] != '\r') {
                f->dirty_line = true;
            }
        }
        return true;
    }
    char cmd[1024];
    assert(n < sizeof cmd);
    memcpy(cmd, raw, n);
    cmd[n] = '\0';
    if (strncmp(cmd, "GET ", 4) == 0) {
        const char *range = strstr(cmd, "Range: bytes=");
        assert(range != NULL);
        unsigned start, end;
        assert(sscanf_s(range, "Range: bytes=%u-%u", &start, &end) == 2);
        uint32_t total = f->package_total == 0U ? 1024U : f->package_total;
        assert(end >= start && end < total);
        f->tail_range = start != 0U && end + 1U == total;
        const char *tag = f->changed ? "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb" :
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        int count = sprintf_s((char *)f->wire, sizeof f->wire,
            "HTTP/1.1 206 Partial Content\r\nContent-Length: %u\r\nContent-Range: bytes %u-%u/%u\r\nETag: \"%s\"\r\n",
            end - start + 1, start + (unsigned)(f->tail_range && f->wrong_tail_range), end, total, tag);
        assert(count > 0);
        if (f->tail_range && f->pad_tail_to_boundary) {
            size_t body_length = (size_t)(end - start + 1U);
            size_t base_length = (size_t)count + 2U + body_length;
            size_t desired_length = ((base_length + 255U) / 256U) * 256U;
            if (f->tail_boundary_delta < 0) {
                --desired_length;
            } else if (f->tail_boundary_delta > 0) {
                ++desired_length;
            }
            size_t padding = desired_length - base_length;
            assert(padding >= 9U && padding <= 258U);
            memcpy(f->wire + count, "X-Pad: ", 7U);
            count += 7;
            memset(f->wire + count, 'p', padding - 9U);
            count += (int)(padding - 9U);
            memcpy(f->wire + count, "\r\n", 2U);
            count += 2;
        }
        memcpy(f->wire + count, "\r\n", 2U);
        count += 2;
        for (unsigned i = start; i <= end; ++i) {
            f->wire[count++] = (uint8_t)i;
        }
        f->consumed = 0;
        f->wire_size = (size_t)count;
        if (f->tail_range) {
            assert(f->truncated_tail_bytes < (size_t)(end - start + 1U));
            f->wire_size -= f->truncated_tail_bytes;
            assert(f->wire_size + f->extra_tail_bytes < sizeof f->wire);
            memset(f->wire + f->wire_size, 0xAA, f->extra_tail_bytes);
            f->wire_size += f->extra_tail_bytes;
            f->tail_wire_size = f->wire_size;
        }
        queue(f, "\r\nSEND OK\r\n", 11);
    } else if (strncmp(cmd, "AT+CIPSEND=", 11) == 0) {
        queue(f, "\r\n>", 3);
    } else if (strncmp(cmd, "AT+CIPRECVLEN?", 14) == 0) {
        if (f->tail_range && f->consumed == f->wire_size &&
            f->final_pending_reply != PENDING_REPLY_NORMAL) {
            if (f->final_pending_reply == PENDING_REPLY_EMPTY_SLOT) {
                const char *reply = "\r\n+CIPRECVLEN:,,,,,\r\nOK\r\n";
                queue(f, reply, strlen(reply));
            } else if (f->final_pending_reply == PENDING_REPLY_ERROR) {
                queue(f, "\r\nERROR\r\n", 9U);
            } else if (f->final_pending_reply == PENDING_REPLY_CORRUPTED_CLOSED) {
                const char *reply = "C+CIPRECVLEN:0\r\n\r\nOK\r\n";
                queue(f, reply, strlen(reply));
            } else if (f->final_pending_reply == PENDING_REPLY_INTACT_CLOSED) {
                const char *reply = "CLOSED\r\n+CIPRECVLEN:0\r\n\r\nOK\r\n";
                queue(f, reply, strlen(reply));
            }
            return true;
        }
        char reply[128];
        int count = sprintf_s(reply, sizeof reply, "\r\n+CIPRECVLEN:%u\r\n%sOK\r\n",
            (unsigned)(f->wire_size - f->consumed),
            f->consumed == f->wire_size && f->close_notice == CLOSE_NOTICE_ON_PENDING &&
                !(f->tail_range && f->omit_tail_closed) ? "CLOSED\r\n" : "");
        queue(f, reply, (size_t)count);
    } else if (strncmp(cmd, "AT+CIPRECVDATA=", 15) == 0) {
        size_t take = f->wire_size - f->consumed;
        if (take > 256) {
            take = 256;
        }
        char head[64];
        int count = sprintf_s(head, sizeof head, "\r\n+CIPRECVDATA:%u,", (unsigned)take);
        queue(f, head, (size_t)count);
        queue(f, f->wire + f->consumed, take);
        f->consumed += take;
        bool drained = f->consumed == f->wire_size && !(f->tail_range && f->omit_tail_closed);
        if (drained && f->close_notice == CLOSE_NOTICE_BEFORE_OK) {
            queue(f, "\r\nCLOSED\r\n", 10U);
        }
        queue(f, "\r\nOK\r\n", 6);
        if (drained && f->close_notice == CLOSE_NOTICE_AFTER_OK) {
            queue(f, "\r\nCLOSED\r\n", 10U);
        }
    } else if (strncmp(cmd, "AT+CIPCLOSE", 11) == 0) {
        ++f->close_commands;
        if (f->fail_close_transport) {
            f->transport_failed = true;
            return false;
        }
        if (f->close_no_acknowledgement) {
            return true;
        } else if (f->close_error) {
            queue(f, "\r\nERROR\r\n", 9U);
        } else {
            ++f->close_acknowledgements;
            if (f->close_without_notice) {
                queue(f, "\r\nOK\r\n", 6U);
            } else {
                queue(f, "\r\nCLOSED\r\nOK\r\n", 14U);
            }
        }
    } else if (strncmp(cmd, "AT+CIPSTATE?", 12) == 0) {
        ++f->state_queries;
        if (f->state_reply == STATE_REPLY_EMPTY) {
            ++f->empty_state_acknowledgements;
            queue(f, "\r\nOK\r\n", 6U);
        } else if (f->state_reply == STATE_REPLY_ACTIVE) {
            const char *reply = "\r\n+CIPSTATE:0,\"TCP\",\"192.168.1.10\",8788,49152,0\r\nOK\r\n";
            queue(f, reply, strlen(reply));
        } else if (f->state_reply == STATE_REPLY_ERROR) {
            queue(f, "\r\nERROR\r\n", 9U);
        } else if (f->state_reply == STATE_REPLY_MALFORMED) {
            const char *reply = "\r\n+CIPSTATE:\r\nOK\r\n";
            queue(f, reply, strlen(reply));
        } else if (f->state_reply == STATE_REPLY_UNKNOWN) {
            const char *reply = "\r\nUNKNOWN\r\nOK\r\n";
            queue(f, reply, strlen(reply));
        } else if (f->state_reply == STATE_REPLY_NO_OK) {
            const char *reply = "\r\n+CIPSTATE:";
            queue(f, reply, strlen(reply));
        } else if (f->state_reply == STATE_REPLY_NUL) {
            const uint8_t reply[] = {'\r', '\n', 0U, '\r', '\n', 'O', 'K', '\r', '\n'};
            queue(f, reply, sizeof reply);
        } else if (f->state_reply == STATE_REPLY_TOO_LONG) {
            uint8_t line[256];
            memset(line, 'x', sizeof line);
            queue(f, line, sizeof line);
            queue(f, "\r\nOK\r\n", 6U);
        }
    } else if (strncmp(cmd, "AT+RST", 6) == 0) {
        ++f->reset_commands;
        queue(f, "\r\nOK\r\nready\r\n", 13);
    } else {
        queue(f, "\r\nOK\r\n", 6);
    }
    return true;
}

/** 功能：读取模拟字节；参数：夹具、输出和超时；返回：有数据为真。 */
static bool receive_byte(void *context, uint8_t *out, uint32_t timeout) {
    fixture_t *f = context;
    if (f->tail_range && timeout == 100U && f->payload_timeout_after != 0U &&
        !f->payload_timeout_fired) {
        ++f->payload_receives;
        if (f->payload_receives == f->payload_timeout_after) {
            f->payload_timeout_fired = true;
            f->time += timeout;
            return false;
        }
    }
    if (f->byte_interval != 0U && f->read < f->used) {
        if (f->time < f->next_byte_at) {
            uint32_t wait = f->next_byte_at - f->time;
            if (wait > timeout) {
                f->time += timeout;
                return false;
            }
            f->time += wait;
        }
        *out = f->queue[f->read++];
        f->next_byte_at = f->time + f->byte_interval;
        return true;
    }
    ++f->time;
    if (f->read < f->used) {
        *out = f->queue[f->read++];
        return true;
    }
    f->time += timeout;
    return false;
}
/** 功能：读取模拟时钟；参数：夹具；返回：毫秒。 */
static uint32_t now(void *context) {
    return ((fixture_t *)context)->time;
}
/** 功能：推进模拟时钟；参数：夹具和毫秒；返回：无。 */
static void delay(void *context, uint32_t ms) {
    ((fixture_t *)context)->time += ms;
}

/** 功能：提供底层故障锁存状态；参数：夹具；返回：本轮无故障为真。 */
static bool healthy(void *context) {
    return !((fixture_t *)context)->transport_failed;
}

/** 功能：复现数据窗口结束后填充污染命令行并验证恢复；参数：无；返回：无。 */
static void test_input_window_recovery(void) {
    const size_t remaining[] = {64U, 4096U};
    for (size_t i = 0U; i < sizeof remaining / sizeof remaining[0]; ++i) {
        fixture_t f = {0};
        f.input_remaining = remaining[i];
        boot_net_t net;
        boot_net_io_t io = {send_bytes, receive_byte, now, delay, &f, healthy};
        assert(boot_net_connect(&net, &io, "wifi", "password", "192.168.1.10", 8788U, "/firmware.ota"));
        assert(f.input_remaining == 0U);
        assert(!f.dirty_line);
        assert(f.reset_commands == 1U);
        assert(f.time < 15000U);
    }
}

/** 功能：验证合法慢速应答跨越旧的一百毫秒边界时不丢失半行；参数：无；返回：无。 */
static void test_slow_response_lines(void) {
    fixture_t f = {0};
    f.byte_interval = 25U;
    boot_net_t net;
    boot_net_io_t io = {send_bytes, receive_byte, now, delay, &f, healthy};
    assert(boot_net_connect(&net, &io, "wifi", "password", "192.168.1.10", 8788U, "/firmware.ota"));
    assert(f.reset_commands == 1U);
    assert(f.time < 5000U);
}

/** 功能：拒绝超过ESP连接命令上限的主机名并接受边界值；参数：无；返回：无。 */
static void test_host_length_boundary(void) {
    fixture_t f = {0};
    boot_net_t net;
    boot_net_io_t io = {send_bytes, receive_byte, now, delay, &f, healthy};
    char host[66];
    memset(host, 'a', sizeof host);
    host[32] = '.';
    host[65] = '\0';
    assert(!boot_net_connect(&net, &io, "wifi", "password", host, 8788U, "/firmware.ota"));
    assert(f.time == 0U && f.used == 0U);
    host[64] = '\0';
    assert(boot_net_connect(&net, &io, "wifi", "password", host, 8788U, "/firmware.ota"));
}

/** 功能：下载真实大小包并逐块验证正文及关闭许可；参数：已配置的夹具；返回：无。 */
static void download_actual_image(fixture_t *f) {
    f->package_total = 272796U;
    boot_net_t net;
    boot_net_io_t io = {send_bytes, receive_byte, now, delay, f, healthy};
    uint8_t bytes[4096];
    assert(boot_net_connect(&net, &io, "wifi", "password", "192.168.1.10", 8788U, "/firmware.ota"));
    assert(boot_net_manifest(&net, bytes));
    assert(net.total == 272796U);
    for (uint32_t image_offset = 0U; image_offset < 272540U;) {
        uint32_t count = 272540U - image_offset;
        if (count > sizeof bytes) {
            count = sizeof bytes;
        }
        unsigned before_close = f->close_commands;
        unsigned before_acknowledgements = f->close_acknowledgements;
        unsigned before_empty_state = f->empty_state_acknowledgements;
        bool fetched = boot_net_fetch(&net, 256U + image_offset, bytes, count);
        if (!fetched) {
            fprintf(stderr, "Range fetch failed at image_offset=%u package_offset=%u length=%u complete=%u received=%zu close_commands=%u\n",
                image_offset, 256U + image_offset, count, (unsigned)net.http.complete,
                net.http.received, f->close_commands - before_close);
        }
        assert(fetched);
        assert(f->close_commands == before_close + 1U);
        assert(f->close_acknowledgements > before_acknowledgements ||
            f->empty_state_acknowledgements > before_empty_state);
        for (uint32_t i = 0U; i < count; ++i) {
            assert(bytes[i] == (uint8_t)(256U + image_offset + i));
        }
        image_offset += count;
    }
    assert(f->close_commands == 68U);
}

/** 功能：复现真实大小包的最后二千二百零四字节缺少关闭通知；参数：无；返回：无。 */
static void test_actual_image_tail_without_closed(void) {
    fixture_t f = {0};
    f.omit_tail_closed = true;
    download_actual_image(&f);
}

/** 功能：覆盖末尾应答边界及关闭通知顺序的组合；参数：无；返回：无。 */
static void test_actual_image_boundary_and_close_order(void) {
    const close_notice_t notices[] = {
        CLOSE_NOTICE_ON_PENDING,
        CLOSE_NOTICE_BEFORE_OK,
        CLOSE_NOTICE_AFTER_OK,
        CLOSE_NOTICE_OMITTED
    };
    for (size_t notice = 0U; notice < sizeof notices / sizeof notices[0]; ++notice) {
        for (int delta = -1; delta <= 1; ++delta) {
            fixture_t f = {0};
            f.close_notice = notices[notice];
            f.pad_tail_to_boundary = true;
            f.tail_boundary_delta = delta;
            download_actual_image(&f);
            assert(f.tail_wire_size == (size_t)(2560 + delta));
            assert(f.state_queries == 0U);
        }
    }
}

/** 功能：验证只有主动关闭成功行而没有关闭通知也可完成事务；参数：无；返回：无。 */
static void test_close_ok_without_closed_notice(void) {
    fixture_t f = {0};
    f.close_notice = CLOSE_NOTICE_OMITTED;
    f.close_without_notice = true;
    download_actual_image(&f);
    assert(f.close_acknowledgements == 68U);
    assert(f.state_queries == 0U);
}

/** 功能：初始化固定真实包身份供末尾范围测试；参数：夹具、网络状态和正文缓冲；返回：无。 */
static void connect_tail_fixture(fixture_t *f, boot_net_t *net, uint8_t *bytes) {
    f->package_total = 272796U;
    f->omit_tail_closed = true;
    boot_net_io_t io = {send_bytes, receive_byte, now, delay, f, healthy};
    assert(boot_net_connect(net, &io, "wifi", "password", "192.168.1.10", 8788U, "/firmware.ota"));
    assert(boot_net_manifest(net, bytes));
    assert(f->close_commands == 1U);
    assert(f->close_acknowledgements == 1U);
}

/** 功能：关闭拒绝时必须由完整无连接查询确认才可交付正文；参数：无；返回：无。 */
static void test_close_error_requires_empty_connection_state(void) {
    const state_reply_t replies[] = {
        STATE_REPLY_EMPTY,
        STATE_REPLY_ACTIVE,
        STATE_REPLY_ERROR,
        STATE_REPLY_TIMEOUT,
        STATE_REPLY_MALFORMED,
        STATE_REPLY_UNKNOWN,
        STATE_REPLY_NO_OK
    };
    for (size_t i = 0U; i < sizeof replies / sizeof replies[0]; ++i) {
        fixture_t f = {0};
        boot_net_t net;
        uint8_t bytes[4096];
        connect_tail_fixture(&f, &net, bytes);
        f.close_error = true;
        f.state_reply = replies[i];
        bool fetched = boot_net_fetch(&net, 270592U, bytes, 2204U);
        assert(fetched == (replies[i] == STATE_REPLY_EMPTY));
        assert(net.http.complete && !net.http.failed && net.http.received == 2204U);
        assert(f.close_commands == 2U);
        assert(f.close_acknowledgements == 1U);
        assert(f.state_queries == 1U);
        assert(f.empty_state_acknowledgements == (unsigned)(replies[i] == STATE_REPLY_EMPTY));
    }
}

/** 功能：主动关闭和状态查询均无应答时不得仅凭正文完整返回成功；参数：无；返回：无。 */
static void test_close_and_state_without_acknowledgement(void) {
    fixture_t f = {0};
    boot_net_t net;
    uint8_t bytes[4096];
    connect_tail_fixture(&f, &net, bytes);
    f.close_no_acknowledgement = true;
    f.state_reply = STATE_REPLY_TIMEOUT;
    assert(!boot_net_fetch(&net, 270592U, bytes, 2204U));
    assert(net.http.complete && net.http.received == 2204U);
    assert(f.close_commands == 2U && f.state_queries == 1U);
    assert(f.close_acknowledgements == 1U && f.empty_state_acknowledgements == 0U);
}

/** 功能：完整关闭不能掩盖正文截断、身份错误、范围错误或额外字节；参数：无；返回：无。 */
static void test_invalid_tail_responses(void) {
    for (unsigned scenario = 0U; scenario < 4U; ++scenario) {
        fixture_t f = {0};
        boot_net_t net;
        uint8_t bytes[4096];
        connect_tail_fixture(&f, &net, bytes);
        if (scenario == 0U) {
            f.truncated_tail_bytes = 1U;
        } else if (scenario == 1U) {
            f.changed = true;
        } else if (scenario == 2U) {
            f.wrong_tail_range = true;
        } else {
            f.extra_tail_bytes = 1U;
        }
        assert(!boot_net_fetch(&net, 270592U, bytes, 2204U));
        assert(!net.http.complete);
        assert(f.close_commands == 2U && f.close_acknowledgements == 2U);
        if (scenario == 0U) {
            assert(!net.http.failed && net.http.received == 2203U);
        } else {
            assert(net.http.failed);
        }
    }
}

/** 功能：拒绝下一拉取帧中的额外字节及成功行之前的非法状态行；参数：无；返回：无。 */
static void test_cross_frame_extra_and_invalid_state_lines(void) {
    const char *names[] = {"cross-frame extra byte", "NUL before state OK", "overlong line before state OK"};
    unsigned accepted_invalid = 0U;
    for (unsigned scenario = 0U; scenario < 3U; ++scenario) {
        fixture_t f = {0};
        boot_net_t net;
        uint8_t bytes[4096];
        connect_tail_fixture(&f, &net, bytes);
        if (scenario == 0U) {
            f.pad_tail_to_boundary = true;
            f.extra_tail_bytes = 1U;
        } else {
            f.close_error = true;
            f.state_reply = scenario == 1U ? STATE_REPLY_NUL : STATE_REPLY_TOO_LONG;
        }
        bool fetched = boot_net_fetch(&net, 270592U, bytes, 2204U);
        if (scenario == 0U) {
            assert(f.tail_wire_size == 2561U);
        } else {
            assert(f.state_queries == 1U);
        }
        assert(f.close_commands == 2U);
        if (fetched) {
            fprintf(stderr, "Invalid protocol accepted: %s complete=%u received=%zu\n",
                names[scenario], (unsigned)net.http.complete, net.http.received);
            ++accepted_invalid;
        }
    }
    assert(accepted_invalid == 0U);
}

/** 功能：验证诊断保存精确控制应答且没有混入正文；参数：记录和预期文本；返回：无。 */
static void assert_reply(const boot_net_reply_t *reply, const char *expected) {
    assert(reply->length == strlen(expected));
    assert(memcmp(reply->bytes, expected, reply->length) == 0);
    assert(!reply->truncated);
}

/** 功能：区分完整正文之后的查询失败和关闭成功；参数：无；返回：无。 */
static void test_final_pending_failure_diagnostics(void) {
    const pending_reply_t replies[] = {
        PENDING_REPLY_EMPTY_SLOT, PENDING_REPLY_ERROR, PENDING_REPLY_TIMEOUT
    };
    const char *expected[] = {"\r\n+CIPRECVLEN:,,,,,\r\n", "\r\nERROR\r\n", ""};
    for (size_t i = 0U; i < sizeof replies / sizeof replies[0]; ++i) {
        fixture_t f = {0};
        boot_net_t net;
        uint8_t bytes[4096];
        connect_tail_fixture(&f, &net, bytes);
        f.final_pending_reply = replies[i];
        f.close_without_notice = true;
        assert(!boot_net_fetch(&net, 270592U, bytes, 2204U));
        assert(net.http.complete && !net.http.failed && net.http.received == 2204U);
        assert(net.diagnostic.final_pending_checked);
        assert(!net.diagnostic.final_pending_ok);
        assert(net.diagnostic.close_ok);
        assert_reply(&net.diagnostic.pending_reply, expected[i]);
        assert_reply(&net.diagnostic.close_reply,
            replies[i] == PENDING_REPLY_EMPTY_SLOT ? "OK\r\n" : "\r\nOK\r\n");
        assert_reply(&net.diagnostic.state_reply, "");
    }
}

/** 功能：复现实板丢字节应答并验证只有完整通知和独立查询才可放行；参数：无；返回：无。 */
static void test_live_corrupted_close_response(void) {
    for (unsigned intact = 0U; intact < 2U; ++intact) {
        fixture_t f = {0};
        boot_net_t net;
        uint8_t bytes[4096];
        connect_tail_fixture(&f, &net, bytes);
        f.final_pending_reply = intact ? PENDING_REPLY_INTACT_CLOSED : PENDING_REPLY_CORRUPTED_CLOSED;
        f.close_error = true;
        f.state_reply = STATE_REPLY_EMPTY;
        assert(boot_net_fetch(&net, 270592U, bytes, 2204U) == (intact != 0U));
        assert(net.http.complete && !net.http.failed && net.http.received == 2204U);
        assert(net.diagnostic.final_pending_checked && net.diagnostic.close_ok);
        assert(net.diagnostic.final_pending_ok == (intact != 0U));
        assert(net.peer_closed == (intact != 0U));
        assert_reply(&net.diagnostic.pending_reply, intact ?
            "CLOSED\r\n+CIPRECVLEN:0\r\n\r\nOK\r\n" : "C+CIPRECVLEN:0\r\n\r\nOK\r\n");
        assert_reply(&net.diagnostic.close_reply, "\r\nERROR\r\n");
        assert_reply(&net.diagnostic.state_reply, intact ? "" : "\r\nOK\r\n");
    }
}

/** 功能：已有关闭通知不得掩盖最后阶段锁存的串口故障；参数：无；返回：无。 */
static void test_closed_notice_does_not_hide_transport_error(void) {
    fixture_t f = {0};
    boot_net_t net;
    uint8_t bytes[4096];
    connect_tail_fixture(&f, &net, bytes);
    f.omit_tail_closed = false;
    f.fail_close_transport = true;
    assert(!boot_net_fetch(&net, 270592U, bytes, 2204U));
    assert(net.http.complete && net.peer_closed && f.transport_failed);
}

/** 功能：区分零缓存后关闭查询的不同结果并验证下一请求清除旧证据；参数：无；返回：无。 */
static void test_close_diagnostics_and_reset(void) {
    const state_reply_t replies[] = {STATE_REPLY_EMPTY, STATE_REPLY_ACTIVE, STATE_REPLY_TIMEOUT};
    const char *expected[] = {
        "\r\nOK\r\n", "\r\n+CIPSTATE:0,\"TCP\",\"192.168.1.10\",8788,49152,0\r\nOK\r\n", ""
    };
    for (size_t i = 0U; i < sizeof replies / sizeof replies[0]; ++i) {
        fixture_t f = {0};
        boot_net_t net;
        uint8_t bytes[4096];
        connect_tail_fixture(&f, &net, bytes);
        f.close_error = true;
        f.state_reply = replies[i];
        bool fetched = boot_net_fetch(&net, 270592U, bytes, 2204U);
        assert(fetched == (replies[i] == STATE_REPLY_EMPTY));
        assert(net.diagnostic.final_pending_checked && net.diagnostic.final_pending_ok);
        assert(net.diagnostic.final_pending == 0U);
        assert(net.diagnostic.close_ok == fetched);
        assert_reply(&net.diagnostic.pending_reply, "\r\n+CIPRECVLEN:0\r\nOK\r\n");
        assert_reply(&net.diagnostic.close_reply, "\r\nERROR\r\n");
        assert_reply(&net.diagnostic.state_reply, expected[i]);
        f.close_error = false;
        f.close_without_notice = true;
        assert(boot_net_fetch(&net, 270592U, bytes, 2204U));
        assert(net.diagnostic.final_pending_checked && net.diagnostic.final_pending_ok);
        assert(net.diagnostic.close_ok);
        assert_reply(&net.diagnostic.close_reply, "\r\nOK\r\n");
        assert_reply(&net.diagnostic.state_reply, "");
    }
}

/** 功能：额外缓存字节在诊断中保留实际数量且继续拒绝下载；参数：无；返回：无。 */
static void test_extra_cache_diagnostics(void) {
    fixture_t f = {0};
    boot_net_t net;
    uint8_t bytes[4096];
    connect_tail_fixture(&f, &net, bytes);
    f.pad_tail_to_boundary = true;
    f.extra_tail_bytes = 1U;
    assert(!boot_net_fetch(&net, 270592U, bytes, 2204U));
    assert(net.http.failed && !net.http.complete);
    assert(net.diagnostic.final_pending_checked && net.diagnostic.final_pending_ok);
    assert(net.diagnostic.final_pending == 1U && net.diagnostic.close_ok);
    assert_reply(&net.diagnostic.pending_reply, "\r\n+CIPRECVLEN:1\r\nOK\r\n");
}

/** 功能：非法零字节和超长应答仍被拒绝且原始诊断有界；参数：无；返回：无。 */
static void test_raw_reply_diagnostic_bounds(void) {
    for (unsigned scenario = 0U; scenario < 2U; ++scenario) {
        fixture_t f = {0};
        boot_net_t net;
        uint8_t bytes[4096];
        connect_tail_fixture(&f, &net, bytes);
        f.close_error = true;
        f.state_reply = scenario == 0U ? STATE_REPLY_NUL : STATE_REPLY_TOO_LONG;
        assert(!boot_net_fetch(&net, 270592U, bytes, 2204U));
        assert(net.diagnostic.final_pending_ok && !net.diagnostic.close_ok);
        const boot_net_reply_t *reply = &net.diagnostic.state_reply;
        if (scenario == 0U) {
            const uint8_t expected[] = {'\r', '\n', 0U};
            assert(reply->length == sizeof expected);
            assert(memcmp(reply->bytes, expected, sizeof expected) == 0);
            assert(!reply->truncated);
        } else {
            assert(reply->length == sizeof reply->bytes && reply->truncated);
            for (size_t i = 0U; i < reply->length; ++i) {
                assert(reply->bytes[i] == 'x');
            }
        }
    }
}

/** 功能：正文帧内超时后迟到的载荷不得进入控制应答诊断；参数：无；返回：无。 */
static void test_partial_payload_is_not_recorded(void) {
    fixture_t f = {0};
    boot_net_t net;
    uint8_t bytes[4096];
    connect_tail_fixture(&f, &net, bytes);
    f.payload_timeout_after = 300U;
    assert(!boot_net_fetch(&net, 270592U, bytes, 2204U));
    assert(f.payload_timeout_fired);
    assert(!net.http.complete);
    assert(!net.diagnostic.final_pending_checked);
    assert_reply(&net.diagnostic.pending_reply, "");
    if (net.diagnostic.close_reply.length != 0U || net.diagnostic.state_reply.length != 0U) {
        fprintf(stderr, "Partial payload leaked into diagnostic: close=%zu state=%zu\n",
            net.diagnostic.close_reply.length, net.diagnostic.state_reply.length);
    }
    assert_reply(&net.diagnostic.close_reply, "");
    assert_reply(&net.diagnostic.state_reply, "");
}

/** 功能：运行真实下载器和二进制/身份测试；参数：无；返回：成功为零。 */
int main(void) {
    fixture_t f = {0};
    boot_net_t net;
    boot_net_io_t io = {send_bytes, receive_byte, now, delay, &f, healthy};
    uint8_t bytes[256];
    assert(boot_net_connect(&net, &io, "wifi", "password", "192.168.1.10", 8788, "/firmware.ota"));
    assert(boot_net_manifest(&net, bytes));
    for (unsigned i = 0; i < 256; ++i) {
        assert(bytes[i] == (uint8_t)i);
    }
    assert(boot_net_fetch(&net, 256, bytes, 256));
    assert(!boot_net_fetch(&net, 1020, bytes, 256));
    f.changed = true;
    assert(!boot_net_fetch(&net, 256, bytes, 256));
    test_input_window_recovery();
    test_slow_response_lines();
    test_host_length_boundary();
    test_actual_image_tail_without_closed();
    test_actual_image_boundary_and_close_order();
    test_close_ok_without_closed_notice();
    test_close_error_requires_empty_connection_state();
    test_close_and_state_without_acknowledgement();
    test_invalid_tail_responses();
    test_cross_frame_extra_and_invalid_state_lines();
    test_final_pending_failure_diagnostics();
    test_live_corrupted_close_response();
    test_closed_notice_does_not_hide_transport_error();
    test_close_diagnostics_and_reset();
    test_extra_cache_diagnostics();
    test_raw_reply_diagnostic_bounds();
    test_partial_payload_is_not_recorded();
    puts("Boot ESP passive binary download, close-before-write, range and identity tests passed");
    return 0;
}
