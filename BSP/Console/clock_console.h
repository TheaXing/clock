#ifndef CLOCK_CONSOLE_H
#define CLOCK_CONSOLE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Call once after USART1 initialization and before starting the scheduler. */
void clock_console_init(void);

/* Serialize stdout formatting and transmission. Return count, or -1 on error.
 * Pre-scheduler calls are supported; ISR/suspended-scheduler calls are rejected.
 */
int clock_log(const char *format, ...);
int clock_log_line(const char *text);

#ifdef __cplusplus
}
#endif
#endif
