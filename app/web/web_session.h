#ifndef CLOCK_WEB_SESSION_H
#define CLOCK_WEB_SESSION_H
#include <stdint.h>
/* 每次启动获取新的硬件随机值，不依赖 RTC 或备份电源。0 表示不可用；
 * 不得使用确定性的后备值接受远程修改。 */
uint32_t clock_web_session_token(void);
#endif
