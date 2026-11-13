#ifndef CLOCK_ONENET_MODEL_H
#define CLOCK_ONENET_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "alarm_types.h"

#define ONENET_MODEL_MAX_PAYLOAD 1460U
#define ONENET_MODEL_ID_SIZE 14U
#define ONENET_PROPERTY_ALARM(index) ((uint16_t)(1U << (index)))
#define ONENET_PROPERTY_ALARMS ((uint16_t)0x00ffU)
#define ONENET_PROPERTY_TEMPERATURE ((uint16_t)0x0100U)
#define ONENET_PROPERTY_HUMIDITY ((uint16_t)0x0200U)
#define ONENET_PROPERTY_SENSOR_VALID ((uint16_t)0x0400U)
#define ONENET_PROPERTY_CONFIG_REVISION ((uint16_t)0x0800U)
#define ONENET_PROPERTY_RINGING_MASK ((uint16_t)0x1000U)
#define ONENET_PROPERTY_ALL ((uint16_t)0x1fffU)
#define ONENET_PROPERTY_TELEMETRY ((uint16_t)0x1f00U)

/* Field bits are enabled, hour, minute, repeat_mask, year, month, day,
 * ring_seconds, snooze_seconds, in that order. */
typedef enum {
    ONENET_MODEL_OK,
    ONENET_MODEL_INVALID_JSON,
    ONENET_MODEL_INVALID_REQUEST,
    ONENET_MODEL_UNKNOWN_PROPERTY,
    ONENET_MODEL_INVALID_VALUE,
    ONENET_MODEL_MULTIPLE_ALARMS,
    ONENET_MODEL_TOO_LARGE
} onenet_model_status_t;

typedef struct {
    char id[ONENET_MODEL_ID_SIZE];
    uint8_t index;                  /* Zero based local alarm index. */
    alarm_entry_t entry;             /* Complete entry merged with current. */
    uint16_t fields_mask;            /* Only fields present in this command. */
    uint32_t fingerprint;            /* Canonical patch; never credentials. */
} onenet_set_request_t;

typedef struct {
    char id[ONENET_MODEL_ID_SIZE];
    uint16_t property_mask;
} onenet_get_request_t;

typedef struct {
    const alarm_config_t *config;
    float temperature;
    float humidity;
    bool sensor_valid;
    uint32_t config_revision;
    uint8_t ringing_mask;
} onenet_model_values_t;

/* No allocation, task calls, writes, or NUL-termination assumptions. A valid
 * envelope ID remains available when semantic validation fails. Invalid JSON
 * or an ambiguous/invalid ID leaves id empty. Version is optional (OneJSON
 * defaults to 1.0); when supplied it must be the string "1.0". */
onenet_model_status_t onenet_model_decode_set(const char *data, size_t length,
    const alarm_config_t *current, onenet_set_request_t *request);
onenet_model_status_t onenet_model_decode_get(const char *data, size_t length,
    onenet_get_request_t *request);
/* Parses platform acknowledgements without logging untrusted msg contents. */
onenet_model_status_t onenet_model_decode_reply(const char *data, size_t length,
    char id[ONENET_MODEL_ID_SIZE], unsigned *code);

/* Fingerprints are not security tokens. For duplicate IDs, compare index,
 * fields_mask, and the entry fields selected by fields_mask as well. Changes
 * in omitted snapshot fields do not change the canonical patch fingerprint. */
bool onenet_model_same_patch(const onenet_set_request_t *left,
    const onenet_set_request_t *right);

/* POST property values use OneJSON {"value":...} wrappers. GET replies
 * contain {id,code,msg,data} with raw data values; SET replies {id,code,msg}.
 * All encoders
 * return the byte length excluding NUL, or zero and empty output on failure.
 * Capacity includes NUL; output never exceeds MAX_PAYLOAD. Split large report
 * masks across publishes. ID must contain 1..13 decimal digits. */
size_t onenet_model_encode_report(const char *id, const onenet_model_values_t *values,
    uint16_t property_mask, char *out, size_t capacity);
size_t onenet_model_encode_get_reply(const char *id, const onenet_model_values_t *values,
    uint16_t property_mask, char *out, size_t capacity);
size_t onenet_model_encode_reply(const char *id, unsigned code, const char *message,
    char *out, size_t capacity);

#endif
