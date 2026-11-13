#ifndef CLOCK_CONFIG_H
#define CLOCK_CONFIG_H

/* The local file is intentionally ignored by Git; copied from the legacy project. */
#ifdef CLOCK_LOCAL_CONFIG
#include "clock_config_local.h"
#endif
#ifndef CLOCK_WIFI_SSID
#define CLOCK_WIFI_SSID ""
#endif
#ifndef CLOCK_WIFI_PASSWORD
#define CLOCK_WIFI_PASSWORD ""
#endif
#ifndef CLOCK_WEATHER_KEY
#define CLOCK_WEATHER_KEY ""
#endif
#ifndef CLOCK_WEATHER_LOCATION
#define CLOCK_WEATHER_LOCATION "WTSQQYHVQ973"
#endif
#ifndef CLOCK_RGB_TEST
#define CLOCK_RGB_TEST 0
#endif

#define CLOCK_WIFI_INTERVAL_MS       5000U
#define CLOCK_SENSOR_INTERVAL_MS     3000U
#define CLOCK_WEATHER_INTERVAL_MS    60000U
#define CLOCK_SYNC_INTERVAL_MS       3600000U
#define CLOCK_SYNC_RETRY_MS          1000U

#endif
