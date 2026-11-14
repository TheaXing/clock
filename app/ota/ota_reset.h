#ifndef OTA_RESET_H
#define OTA_RESET_H
#include <stdbool.h>
#include <stdint.h>
bool ota_reset_flags_are_software(uint32_t flags);
bool ota_reset_flags_allow_retry(uint32_t flags);
bool ota_reset_was_software(void);
bool ota_reset_is_normal_boot(void);
#endif
