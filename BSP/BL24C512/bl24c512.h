#ifndef BL24C512_H
#define BL24C512_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define BL24C512_CAPACITY 65536UL
#define BL24C512_PAGE_SIZE 128U
/* Sole EEPROM worker owns I2C1 after boot. Call before scheduler start or from
 * normal task context; an already-suspended scheduler is rejected. Each HAL
 * call suspends task scheduling only (never interrupts), then resumes it.
 * 128-byte chunks take about 12ms at 100kHz. HAL bus faults can block tasks
 * about 26ms per transfer, or up to 52ms in a ready probe with two BUSY waits.
 * Calls yield between chunks and while waiting for write ACK. */
bool bl24c512_read(uint32_t address, void *data, size_t length);
bool bl24c512_write(uint32_t address, const void *data, size_t length);
#endif
