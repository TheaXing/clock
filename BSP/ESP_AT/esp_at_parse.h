#ifndef ESP_AT_PARSE_H
#define ESP_AT_PARSE_H

#include <stddef.h>
#include "esp_at.h"

/* Internal portable parsing/escaping helpers, also exercised by host tests. */
bool esp_at_parse_cwstate(const char *response, esp_wifi_info_t *info);
bool esp_at_parse_cwjap(const char *response, esp_wifi_info_t *info);
bool esp_at_parse_sntp(const char *response, esp_date_time_t *date);
bool esp_at_validate_http(const char *response);
bool esp_at_extract_http(const char *response, char *body, size_t capacity);
bool esp_at_quote(const char *input, size_t max_input, char *output, size_t capacity);
bool esp_at_valid_mac(const char *mac);

#endif
