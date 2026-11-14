#ifndef CLOCK_WIFI_IDENTITY_H
#define CLOCK_WIFI_IDENTITY_H
#include <stdbool.h>
#include <stdint.h>
bool wifi_setup_identity(char ssid[33], char password[17], uint32_t *session);
#endif
