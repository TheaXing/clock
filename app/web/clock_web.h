#ifndef CLOCK_WEB_H
#define CLOCK_WEB_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
/* 在唯一网络任务启动前初始化；session 是并发控制令牌，不提供身份认证。
 * 其余调用均属于该网络任务。 */
void clock_web_init(uint32_t session);
/* 同一网络任务的维护回调，在有界响应分段之间且未持有 ESP 命令互斥锁时调用。
 * 回调不得递归调用网页操作。 */
void clock_web_set_background_poll(void (*poll)(void));
/* 提交回调只排队，不执行联网；应答关闭连接后主网络循环再消费候选。
 * 状态回调输出不含密码的有界 JSON，两者仅在唯一网络任务调用。 */
void clock_web_set_wifi_handlers(int (*submit)(const char *, uint32_t),
                                 void (*status)(char *, size_t, uint32_t));
/* 只切换配网会话和路由，保留正常闹钟及 OTA 会话。 */
void clock_web_setup(bool enabled, uint32_t session);
void clock_web_network(bool connected, uint32_t now_ms);
void clock_web_poll(uint32_t now_ms);
#endif
