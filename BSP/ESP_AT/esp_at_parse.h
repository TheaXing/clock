#ifndef ESP_AT_PARSE_H
#define ESP_AT_PARSE_H

#include <stddef.h>
#include "esp_at.h"

/* 内部可移植解析与转义辅助接口，同时由主机测试验证。 */
bool esp_at_parse_cwstate(const char *response, esp_wifi_info_t *info);
bool esp_at_parse_cwjap(const char *response, esp_wifi_info_t *info);
bool esp_at_parse_sntp(const char *response, esp_date_time_t *date);
bool esp_at_validate_http(const char *response);
bool esp_at_extract_http(const char *response, char *body, size_t capacity);
bool esp_at_quote(const char *input, size_t max_input, char *output, size_t capacity);
bool esp_at_valid_mac(const char *mac);
bool esp_at_parse_station_ip(const char *response, char ip[16]);
bool esp_at_parse_ap_ip(const char *response, char ip[16]);
bool esp_at_parse_setup_clients(const char *response, bool *connected);
bool esp_at_parse_autoconnect(const char *response, bool *enabled);
bool esp_at_build_http_get_bounded(const char *url, uint32_t timeout_ms, char *output, size_t capacity);

#endif
