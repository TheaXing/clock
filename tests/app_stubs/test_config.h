#ifndef TEST_APP_CONFIG_H
#define TEST_APP_CONFIG_H
/* Use fixed public fixtures, independent of any ignored local credentials. */
#include <stdio.h>
#define CLOCK_WIFI_SSID "fixture-ap"
#define CLOCK_WIFI_PASSWORD "fixture-password"
#define CLOCK_WEATHER_KEY "fixture-key"
#define CLOCK_WEATHER_LOCATION "fixture-location"
int app_test_printf(const char *format, ...);
int app_test_puts(const char *message);
#define printf app_test_printf
#define puts app_test_puts
#endif
