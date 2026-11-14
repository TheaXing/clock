#include "boot_net.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
} fixture_t;

/** 功能：追加精确UART字节；参数：夹具和字节；返回：无。 */
static void queue(fixture_t *f, const void *bytes, size_t n) {
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
        assert(end >= start && end < 1024);
        const char *tag = f->changed ? "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb" :
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        int count = sprintf_s((char *)f->wire, sizeof f->wire,
            "HTTP/1.1 206 Partial Content\r\nContent-Length: %u\r\nContent-Range: bytes %u-%u/1024\r\nETag: \"%s\"\r\n\r\n",
            end - start + 1, start, end, tag);
        for (unsigned i = start; i <= end; ++i) {
            f->wire[count++] = (uint8_t)i;
        }
        f->consumed = 0;
        f->wire_size = (size_t)count;
        queue(f, "\r\nSEND OK\r\n", 11);
    } else if (strncmp(cmd, "AT+CIPSEND=", 11) == 0) {
        queue(f, "\r\n>", 3);
    } else if (strncmp(cmd, "AT+CIPRECVLEN?", 14) == 0) {
        char reply[128];
        int count = sprintf_s(reply, sizeof reply, "\r\n+CIPRECVLEN:%u\r\n%sOK\r\n",
            (unsigned)(f->wire_size - f->consumed), f->consumed == f->wire_size ? "CLOSED\r\n" : "");
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
        queue(f, "\r\nOK\r\n", 6);
        f->consumed += take;
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

/** 功能：复现数据窗口结束后填充污染命令行并验证恢复；参数：无；返回：无。 */
static void test_input_window_recovery(void) {
    const size_t remaining[] = {64U, 4096U};
    for (size_t i = 0U; i < sizeof remaining / sizeof remaining[0]; ++i) {
        fixture_t f = {0};
        f.input_remaining = remaining[i];
        boot_net_t net;
        boot_net_io_t io = {send_bytes, receive_byte, now, delay, &f};
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
    boot_net_io_t io = {send_bytes, receive_byte, now, delay, &f};
    assert(boot_net_connect(&net, &io, "wifi", "password", "192.168.1.10", 8788U, "/firmware.ota"));
    assert(f.reset_commands == 1U);
    assert(f.time < 5000U);
}

/** 功能：拒绝超过ESP连接命令上限的主机名并接受边界值；参数：无；返回：无。 */
static void test_host_length_boundary(void) {
    fixture_t f = {0};
    boot_net_t net;
    boot_net_io_t io = {send_bytes, receive_byte, now, delay, &f};
    char host[66];
    memset(host, 'a', sizeof host);
    host[32] = '.';
    host[65] = '\0';
    assert(!boot_net_connect(&net, &io, "wifi", "password", host, 8788U, "/firmware.ota"));
    assert(f.time == 0U && f.used == 0U);
    host[64] = '\0';
    assert(boot_net_connect(&net, &io, "wifi", "password", host, 8788U, "/firmware.ota"));
}
/** 功能：运行真实下载器和二进制/身份测试；参数：无；返回：成功为零。 */
int main(void) {
    fixture_t f = {0};
    boot_net_t net;
    boot_net_io_t io = {send_bytes, receive_byte, now, delay, &f};
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
    puts("Boot ESP passive binary download, close-before-write, range and identity tests passed");
    return 0;
}
