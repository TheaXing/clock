#ifndef CLOCK_AHT20_H
#define CLOCK_AHT20_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Task-context API. Initialize I2C2 before first use; one task owns the sensor. */
typedef enum
{
    AHT20_STAGE_NONE = 0,
    AHT20_STAGE_INITIAL_STATUS,
    AHT20_STAGE_CALIBRATION,
    AHT20_STAGE_CALIBRATION_STATUS,
    AHT20_STAGE_TRIGGER,
    AHT20_STAGE_WAIT_STATUS,
    AHT20_STAGE_DATA
} aht20_stage_t;

typedef enum
{
    AHT20_ERROR_NONE = 0,
    AHT20_ERROR_TRANSMIT,
    AHT20_ERROR_RECEIVE,
    AHT20_ERROR_CALIBRATION_TIMEOUT,
    AHT20_ERROR_MEASUREMENT_TIMEOUT,
    AHT20_ERROR_NOT_CALIBRATED,
    AHT20_ERROR_INVALID_ARGUMENT,
    AHT20_ERROR_SENSOR_BUSY
} aht20_error_t;

typedef struct
{
    aht20_stage_t stage;
    aht20_error_t error;
    uint32_t hal_status;
    uint32_t hal_error;
    uint8_t status;
    bool status_valid;
} aht20_diagnostics_t;

bool aht20_init(void);
bool aht20_start_measurement(void);
bool aht20_wait_for_measurement(void);
bool aht20_read_measurement(float *temperature, float *humidity);
/* Snapshot of the last operation; call from the task that owns the sensor.
 * hal_status is a HAL_StatusTypeDef value, hal_error is HAL_I2C_GetError().
 * status is meaningful only when status_valid is true. */
aht20_diagnostics_t aht20_get_diagnostics(void);

#ifdef __cplusplus
}
#endif
#endif
