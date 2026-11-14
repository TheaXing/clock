#ifndef CLOCK_CONSOLE_H
#define CLOCK_CONSOLE_H

#ifdef __cplusplus
extern "C" {
#endif

/* 在 USART1 初始化后、调度器启动前调用一次。 */
void clock_console_init(void);

/* 串行化标准输出的格式化与发送，返回输出字符数，出错返回 -1。支持调度器启动前调用，拒绝中断或调度器已暂停时调用。 */
int clock_log(const char *format, ...);
int clock_log_line(const char *text);

#ifdef __cplusplus
}
#endif
#endif
