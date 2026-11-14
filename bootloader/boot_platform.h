#ifndef BOOT_PLATFORM_H
#define BOOT_PLATFORM_H
#include "boot_net.h"
#include "ota_installer.h"
#include "ota_image.h"

bool boot_platform_init(void);
void boot_platform_log(const char *message);
bool boot_platform_key(void);
uint32_t boot_platform_now(void);
void boot_platform_delay(uint32_t ms);
boot_net_io_t boot_platform_net_io(void);
ota_journal_io_t boot_platform_journal_io(void);
bool boot_platform_erase(void *context, unsigned sector);
bool boot_platform_program(void *context, uint32_t address, const void *data, size_t length);
bool boot_platform_read(void *context, uint32_t address, void *data, size_t length);
void boot_platform_jump(void);
#endif
