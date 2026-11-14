#ifndef CLOCK_ONENET_SERVICE_H
#define CLOCK_ONENET_SERVICE_H
#include <stdbool.h>
#include <stdint.h>
/* 在应用任务启动前初始化；network 和 poll 仅由已有网络任务调用，
 * sensor 由传感器任务执行短时间的复制更新。 */
void onenet_service_init(void);
bool onenet_service_configured(void);
void onenet_service_network(bool wifi_connected, bool esp_time_ready, uint32_t now_ms);
void onenet_service_poll(uint32_t now_ms);
void onenet_service_sensor(float temperature, float humidity);
bool onenet_service_online(void);
bool onenet_service_busy(void);
#endif
