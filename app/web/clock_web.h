#ifndef CLOCK_WEB_H
#define CLOCK_WEB_H
#include <stdbool.h>
#include <stdint.h>
/* Initialize before starting the sole network worker. Session is a concurrency
 * token, not authentication. All remaining calls belong to that worker. */
void clock_web_init(uint32_t session);
/* Same-worker maintenance hook, called between bounded response pieces with
 * no ESP command mutex held. Must not recursively call web operations. */
void clock_web_set_background_poll(void (*poll)(void));
void clock_web_network(bool connected, uint32_t now_ms);
void clock_web_poll(uint32_t now_ms);
#endif
