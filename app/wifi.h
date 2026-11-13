#ifndef CLOCK_WIFI_H
#define CLOCK_WIFI_H
#include <stdbool.h>
#include "clock_config.h"
#define WIFI_SSID CLOCK_WIFI_SSID
#define WIFI_PASSWD CLOCK_WIFI_PASSWORD
bool wifi_init(void);
bool wifi_wait_connect(void);
bool wifi_ready(void);
void wifi_mark_unready(void);
#endif
