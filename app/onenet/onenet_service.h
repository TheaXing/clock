#ifndef CLOCK_ONENET_SERVICE_H
#define CLOCK_ONENET_SERVICE_H
#include <stdbool.h>
#include <stdint.h>
/* Initialize before app tasks. Network/poll are called only by the existing
 * network worker; sensor() is a short copied update from the sensor task. */
void onenet_service_init(void);
bool onenet_service_configured(void);
void onenet_service_network(bool wifi_connected,bool esp_time_ready,uint32_t now_ms);
void onenet_service_poll(uint32_t now_ms);
void onenet_service_sensor(float temperature,float humidity);
bool onenet_service_online(void);
bool onenet_service_busy(void);
#endif
