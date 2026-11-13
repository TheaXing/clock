#ifndef CLOCK_ALARM_SERVICE_H
#define CLOCK_ALARM_SERVICE_H
#include <stdbool.h>
#include "alarm_types.h"
#include "clock_rtc.h"

/* Copies are coherent and independent of the owner task's mutable state. */
typedef struct {
    alarm_config_t config;
    rtc_date_time_t now;
    uint32_t revision;      /* Verified EEPROM journal generation. */
    uint32_t state_version; /* Optimistic version within this boot session. */
    uint8_t ringing_mask;
    uint8_t snoozed_mask;
    bool time_valid;
    bool saving;
    bool storage_ok;
    bool test_pending;
    bool test_ringing;
    bool local_editing; /* Physical alarm or clock editor owns a draft. */
} alarm_service_snapshot_t;

typedef enum {
    ALARM_REMOTE_ACCEPTED, ALARM_REMOTE_BUSY, ALARM_REMOTE_CONFLICT,
    ALARM_REMOTE_INVALID, ALARM_REMOTE_UNAVAILABLE
} alarm_remote_submit_t;
typedef enum {
    ALARM_REMOTE_UNKNOWN, ALARM_REMOTE_PENDING, ALARM_REMOTE_SAVED, ALARM_REMOTE_FAILED
} alarm_remote_result_t;

/* Task context only; nonblocking. False until all service tasks are available.
 * state_version is seeded at boot and advances on every durable config apply;
 * callers must pair it with a separate boot/session token over the network. */
bool alarm_service_snapshot(alarm_service_snapshot_t *snapshot);
/* Submit a full entry from the current snapshot. The store merges only fields
 * changed from that snapshot, preserving unknown durable data after boot I/O
 * failure. Accepted means queued, never saved. Only ACCEPTED writes request_id.
 * The owner rechecks the version and local editor/save state; a physical input
 * that wins this race can make an accepted request complete as FAILED. */
alarm_remote_submit_t alarm_service_remote_submit(uint8_t index, const alarm_entry_t *entry,
                                                  uint32_t expected_version, uint32_t *request_id);
/* Explicit patch: fields_mask must be 1..0x01ff. Bits 0..8 select enabled,
 * hour, minute, repeat_mask, year, month, day, ring_seconds, snooze_seconds.
 * Selected fields apply even when equal to the RAM snapshot; omitted fields
 * come from a fresh durable read. entry is a full, valid snapshot-merged
 * entry. The entire durable merge is validated before saving. This preserves
 * explicit intent after unavailable boot data or an unacknowledged commit.
 * Queueing, version, editor, request ID and result semantics match submit. */
alarm_remote_submit_t alarm_service_remote_submit_patch(uint8_t index, const alarm_entry_t *entry,
    uint16_t fields_mask, uint32_t expected_version, uint32_t *request_id);
/* The current pending ID and last eight completed results are retained. Older
 * or unknown IDs return UNKNOWN. IDs are boot-scoped; network callers also
 * verify their session token. SAVED requires verified storage. */
alarm_remote_result_t alarm_service_remote_result(uint32_t request_id);

/* Call in the init task after RTC/UI/I2C1, before starting network work. */
void alarm_service_init(void);
/* Normal clock/weather producers hold this across a full UI update. False
 * means the alarm menu/ringing view currently owns the screen. */
bool alarm_service_display_begin(void);
void alarm_service_display_end(void);
#endif
