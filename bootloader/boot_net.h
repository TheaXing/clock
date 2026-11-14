#ifndef BOOT_NET_H
#define BOOT_NET_H
#include "ota_http.h"

/** 提供裸机 UART 和单调时钟，测试时可替换为字节流。 */
typedef struct {
    bool (*send)(void *, const void *, size_t); // 有界发送完整数据
    bool (*receive)(void *, uint8_t *, uint32_t); // 在指定毫秒内读取单字节
    uint32_t (*now)(void *); // 返回单调毫秒计数
    void (*delay)(void *, uint32_t); // 有界等待
    void *context; // 底层上下文
} boot_net_io_t;

/** 保存有界控制应答，供失败后输出而不阻塞接收。 */
typedef struct {
    uint8_t bytes[96]; // 原始控制应答字节，不含下载正文
    size_t length; // 已保留的字节数
    bool truncated; // 应答超出记录容量
} boot_net_reply_t;

/** 区分正文结束后的缓存检查与关闭确认结果。 */
typedef struct {
    bool final_pending_checked; // 已执行正文结束后的缓存查询
    bool final_pending_ok; // 缓存查询完整且解析成功
    uint32_t final_pending; // 完整查询报告的剩余字节数
    bool close_ok; // 关闭命令或无连接查询已确认
    boot_net_reply_t pending_reply; // 末次缓存查询的原始应答
    boot_net_reply_t close_reply; // 主动关闭命令的原始应答
    boot_net_reply_t state_reply; // 无连接确认查询的原始应答
} boot_net_diagnostic_t;

/** 保存独立网络下载器，不依赖应用或实时操作系统。 */
typedef struct {
    boot_net_io_t io; // 裸机输入输出接口
    const char *host; // 固定固件服务器
    const char *path; // 固定包路径
    uint16_t port; // TCP 端口
    uint32_t total; // 已固定的完整包大小
    char etag[67]; // 已固定的强摘要标识
    bool peer_closed; // 远端关闭通知已收到
    ota_http_t http; // 有界 HTTP 增量解析器
    boot_net_diagnostic_t diagnostic; // 当前范围的结束确认诊断
    boot_net_reply_t *capture; // 当前正在记录的控制应答，空值表示关闭
} boot_net_t;

bool boot_net_connect(boot_net_t *net, const boot_net_io_t *io, const char *ssid,
                      const char *password, const char *host, uint16_t port, const char *path);
bool boot_net_manifest(boot_net_t *net, uint8_t header[256]);
bool boot_net_fetch(void *context, uint32_t offset, void *data, size_t length);
#endif
