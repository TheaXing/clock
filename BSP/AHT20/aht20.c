#include "aht20.h"
#include "i2c.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stddef.h>

#define AHT20_ADDRESS ((uint16_t)(0x38U << 1))
#define AHT20_IO_TIMEOUT_MS 25U

static aht20_diagnostics_t diagnostics;

aht20_diagnostics_t aht20_get_diagnostics(void)
{
    return diagnostics;
}

static void begin_stage(aht20_stage_t stage)
{
    diagnostics = (aht20_diagnostics_t){0};
    diagnostics.stage = stage;
}

static bool transfer_result(HAL_StatusTypeDef result, aht20_error_t error)
{
    diagnostics.hal_status = (uint32_t)result;
    diagnostics.hal_error = result == HAL_OK ? 0U : HAL_I2C_GetError(&hi2c2);
    if (result != HAL_OK)
    {
        diagnostics.error = error;
        return false;
    }
    return true;
}

static void sensor_delay(uint32_t milliseconds)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        TickType_t ticks = pdMS_TO_TICKS(milliseconds);
        /* vTaskDelay can return up to one tick early relative to wall time. */
        vTaskDelay(ticks + 1U);
    }
    else
    {
        HAL_Delay(milliseconds);
    }
}

static bool write_command(uint8_t *bytes, uint16_t length)
{
    return transfer_result(HAL_I2C_Master_Transmit(&hi2c2, AHT20_ADDRESS,
                           bytes, length, AHT20_IO_TIMEOUT_MS),
                           AHT20_ERROR_TRANSMIT);
}

static bool read_bytes(uint8_t *bytes, uint16_t length)
{
    diagnostics.status_valid = false;
    return transfer_result(HAL_I2C_Master_Receive(&hi2c2, AHT20_ADDRESS,
                           bytes, length, AHT20_IO_TIMEOUT_MS),
                           AHT20_ERROR_RECEIVE);
}

static bool read_status(uint8_t *status)
{
    /* 0x71 in the timing diagram is (0x38 << 1) | READ, not a command.
     * HAL supplies that address byte when starting this receive. */
    if (!read_bytes(status, 1U))
    {
        return false;
    }
    diagnostics.status = *status;
    diagnostics.status_valid = true;
    return true;
}

bool aht20_init(void)
{
    uint8_t status;
    begin_stage(AHT20_STAGE_INITIAL_STATUS);
    sensor_delay(40U);
    if (!read_status(&status))
    {
        return false;
    }
    if ((status & 0x08U) != 0U)
    {
        return true;
    }

    uint8_t initialize[] = {0xBEU, 0x08U, 0x00U};
    begin_stage(AHT20_STAGE_CALIBRATION);
    if (!write_command(initialize, sizeof initialize))
    {
        return false;
    }
    /* The initialization command also needs 10 ms before a status read. */
    begin_stage(AHT20_STAGE_CALIBRATION_STATUS);
    for (uint32_t attempt = 0U; attempt < 10U; ++attempt)
    {
        sensor_delay(10U);
        if (!read_status(&status))
        {
            return false;
        }
        if ((status & 0x08U) != 0U)
        {
            return true;
        }
    }
    diagnostics.error = AHT20_ERROR_CALIBRATION_TIMEOUT;
    return false;
}

bool aht20_start_measurement(void)
{
    uint8_t command[] = {0xACU, 0x33U, 0x00U};
    begin_stage(AHT20_STAGE_TRIGGER);
    return write_command(command, sizeof command);
}

bool aht20_wait_for_measurement(void)
{
    begin_stage(AHT20_STAGE_WAIT_STATUS);
    /* Aosong requires at least 80 ms conversion time before the first read. */
    sensor_delay(80U);
    for (uint32_t waited = 80U; waited <= 200U; waited += 10U)
    {
        uint8_t status;
        if (!read_status(&status))
        {
            return false;
        }
        if ((status & 0x80U) == 0U)
        {
            if ((status & 0x08U) == 0U)
            {
                diagnostics.error = AHT20_ERROR_NOT_CALIBRATED;
                return false;
            }
            return true;
        }
        if (waited < 200U)
        {
            sensor_delay(10U);
        }
    }
    diagnostics.error = AHT20_ERROR_MEASUREMENT_TIMEOUT;
    return false;
}

bool aht20_read_measurement(float *temperature, float *humidity)
{
    uint8_t data[6];
    begin_stage(AHT20_STAGE_DATA);
    if (temperature == NULL || humidity == NULL)
    {
        diagnostics.error = AHT20_ERROR_INVALID_ARGUMENT;
        return false;
    }
    if (!read_bytes(data, sizeof data))
    {
        return false;
    }
    diagnostics.status = data[0];
    diagnostics.status_valid = true;
    if ((data[0] & 0x80U) != 0U)
    {
        diagnostics.error = AHT20_ERROR_SENSOR_BUSY;
        return false;
    }
    if ((data[0] & 0x08U) == 0U)
    {
        diagnostics.error = AHT20_ERROR_NOT_CALIBRATED;
        return false;
    }

    uint32_t raw_humidity = ((uint32_t)data[1] << 12) |
                            ((uint32_t)data[2] << 4) |
                            ((uint32_t)data[3] >> 4);
    uint32_t raw_temperature = ((uint32_t)(data[3] & 0x0FU) << 16) |
                               ((uint32_t)data[4] << 8) | data[5];
    *humidity = (float)raw_humidity * (100.0f / 1048576.0f);
    *temperature = (float)raw_temperature * (200.0f / 1048576.0f) - 50.0f;
    return true;
}
