#ifndef WIFI_FORM_H
#define WIFI_FORM_H
#include <stdbool.h>
#include <stdint.h>
bool wifi_form_parse(const char *body, uint32_t expected_session, char ssid[33], char password[64]);
#endif
