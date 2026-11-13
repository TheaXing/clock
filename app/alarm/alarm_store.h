#ifndef CLOCK_ALARM_STORE_H
#define CLOCK_ALARM_STORE_H
#include <stddef.h>
#include "alarm_types.h"

typedef bool (*alarm_store_read_t)(void *context, uint32_t address, void *data, size_t length);
typedef bool (*alarm_store_write_t)(void *context, uint32_t address, const void *data, size_t length);
typedef struct {
    alarm_store_read_t read;
    alarm_store_write_t write;
    void *context;
} alarm_store_io_t;
typedef enum {
    ALARM_STORE_OK = 0, ALARM_STORE_EMPTY, ALARM_STORE_IO_ERROR,
    ALARM_STORE_CORRUPT, ALARM_STORE_INVALID
} alarm_store_result_t;
typedef struct {
    alarm_store_io_t io;
    uint32_t generation;
    int active_slot;
    bool update_baseline;
} alarm_store_t;

/* Reserved EEPROM region: [0, 2048); each slot 1024 bytes. No writes elsewhere. */
void alarm_store_init(alarm_store_t *store, alarm_store_io_t io);
alarm_store_result_t alarm_store_load(alarm_store_t *store, alarm_config_t *config);
/* Strict read for read/merge/save: every slot must be readable and a remembered
 * generation cannot regress. OK supplies config; EMPTY/CORRUPT leaves config
 * unchanged and confirms that no valid baseline exists. These three outcomes
 * capture the baseline: save refuses any intervening generation change.
 * IO_ERROR/INVALID leave config and store unchanged. Boot load may fall back.
 */
alarm_store_result_t alarm_store_load_for_update(alarm_store_t *store, alarm_config_t *config);
alarm_store_result_t alarm_store_save(alarm_store_t *store, const alarm_config_t *config);
#endif

