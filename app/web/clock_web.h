#ifndef CLOCK_WEB_H
#define CLOCK_WEB_H
#include <stdbool.h>
#include <stdint.h>
/* 在唯一网络任务启动前初始化；session 是并发控制令牌，不提供身份认证。
 * 其余调用均属于该网络任务。 */
void clock_web_init(uint32_t session);
/* 同一网络任务的维护回调，在有界响应分段之间且未持有 ESP 命令互斥锁时调用。
 * 回调不得递归调用网页操作。 */
void clock_web_set_background_poll(void (*poll)(void));
void clock_web_network(bool connected, uint32_t now_ms);
void clock_web_poll(uint32_t now_ms);
#endif
