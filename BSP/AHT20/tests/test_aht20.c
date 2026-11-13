/* Aosong AHT20 V1.2 section 5.4 / page 9 transport and timing model.
 * 0x71 is the read ADDRESS on the wire, not an accepted command payload.
 * Reject unsupported commands and premature access instead of always ACKing. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "i2c.h"
#include "aht20.h"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); \
} } while (0)

I2C_HandleTypeDef hi2c2;
static struct {
    uint8_t frame[6];
    bool calibrated;
    bool calibrating;
    bool measuring;
    uint32_t calibration_started;
    uint32_t measurement_started;
    uint32_t conversion_ms;
    uint32_t elapsed;
    uint32_t error;
    HAL_StatusTypeDef tx_status;
    HAL_StatusTypeDef rx_status;
    unsigned writes;
    unsigned reads;
    bool early_access;
    bool unknown_command;
    bool calibration_stuck;
    int scheduler_running;
} sensor;

static void reset_sensor(void)
{
    memset(&sensor, 0, sizeof sensor);
    sensor.calibrated = true;
    sensor.conversion_ms = 80;
    memcpy(sensor.frame, (uint8_t[]){0x08, 0x80, 0x00, 0x06, 0x00, 0x00}, 6);
}

int xTaskGetSchedulerState(void) { return sensor.scheduler_running ? 2 : 0; }
/* A delay begun just before a tick can last one tick less than its count. */
void vTaskDelay(uint32_t ticks) { CHECK(ticks > 0); sensor.elapsed += ticks - 1U; }
void HAL_Delay(uint32_t milliseconds) { sensor.elapsed += milliseconds; }
uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *i2c)
{
    CHECK(i2c == &hi2c2);
    return sensor.error;
}

HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *i2c, uint16_t addr,
    uint8_t *data, uint16_t len, uint32_t timeout)
{
    CHECK(i2c == &hi2c2 && addr == 0x70 && timeout > 0 && timeout < 1000);
    ++sensor.writes;
    if (sensor.tx_status != HAL_OK) return sensor.tx_status;
    if (len == 3 && data[0] == 0xBE && data[1] == 0x08 && data[2] == 0)
    {
        sensor.calibrating = true;
        sensor.calibration_started = sensor.elapsed;
        return HAL_OK;
    }
    if (len == 3 && data[0] == 0xAC && data[1] == 0x33 && data[2] == 0)
    {
        sensor.measuring = true;
        sensor.measurement_started = sensor.elapsed;
        return HAL_OK;
    }
    sensor.unknown_command = true;
    sensor.error = 4; /* HAL_I2C_ERROR_AF: undocumented payload is NACKed. */
    return HAL_ERROR;
}

HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *i2c, uint16_t addr,
    uint8_t *data, uint16_t len, uint32_t timeout)
{
    CHECK(i2c == &hi2c2 && addr == 0x70 && timeout > 0 && timeout < 1000);
    CHECK(len == 1 || len == 6);
    ++sensor.reads;
    if (sensor.rx_status != HAL_OK) return sensor.rx_status;
    if ((sensor.measuring && sensor.elapsed - sensor.measurement_started < 80) ||
        (sensor.calibrating && sensor.elapsed - sensor.calibration_started < 10))
    {
        sensor.early_access = true;
        sensor.error = 4;
        return HAL_ERROR;
    }
    if (sensor.calibrating && !sensor.calibration_stuck)
    {
        sensor.calibrated = true;
        sensor.calibrating = false;
    }
    uint8_t status = sensor.calibrated ? 0x08 : 0x00;
    if (sensor.measuring &&
        sensor.elapsed - sensor.measurement_started < sensor.conversion_ms)
        status |= 0x80;
    if (len == 1) data[0] = status;
    else
    {
        memcpy(data, sensor.frame, len);
        data[0] = status;
    }
    return HAL_OK;
}

static void test_status_uses_read_address_without_command_payload(void)
{
    reset_sensor();
    CHECK(aht20_init());
    CHECK(!sensor.unknown_command && sensor.writes == 0 && sensor.reads == 1);
}

static void test_waits_for_conversion_before_status_access(void)
{
    reset_sensor();
    sensor.scheduler_running = 1;
    CHECK(aht20_start_measurement());
    CHECK(aht20_wait_for_measurement());
    CHECK(!sensor.early_access);
    CHECK(sensor.elapsed >= 80 && sensor.elapsed <= 100);
    float temperature = -999, humidity = -999;
    CHECK(aht20_read_measurement(&temperature, &humidity));
    CHECK(fabsf(temperature - 25.0f) < 0.001f);
    CHECK(fabsf(humidity - 50.0f) < 0.001f);
}

static void test_calibration_waits_ten_milliseconds(void)
{
    reset_sensor();
    sensor.calibrated = false;
    CHECK(aht20_init());
    CHECK(!sensor.early_access && sensor.calibrated);
}

static void test_busy_polling_is_bounded(void)
{
    reset_sensor();
    sensor.scheduler_running = 1;
    sensor.conversion_ms = 1000;
    CHECK(aht20_start_measurement());
    CHECK(!aht20_wait_for_measurement());
    CHECK(!sensor.early_access && sensor.elapsed >= 200 && sensor.elapsed <= 250);
    float temperature = 12, humidity = 34;
    CHECK(!aht20_read_measurement(&temperature, &humidity));
    CHECK(temperature == 12 && humidity == 34);
}

static void test_calibration_failure_is_bounded(void)
{
    reset_sensor();
    sensor.calibrated = false;
    sensor.calibration_stuck = true;
    CHECK(!aht20_init());
    CHECK(!sensor.early_access && sensor.elapsed <= 300);
}

static void test_transport_failures_do_not_produce_measurements(void)
{
    reset_sensor();
    sensor.tx_status = HAL_ERROR;
    CHECK(!aht20_start_measurement());
    reset_sensor();
    sensor.rx_status = HAL_BUSY;
    CHECK(!aht20_init());
    reset_sensor();
    sensor.rx_status = HAL_TIMEOUT;
    CHECK(!aht20_wait_for_measurement());
    float temperature = 12, humidity = 34;
    CHECK(!aht20_read_measurement(&temperature, &humidity));
    CHECK(temperature == 12 && humidity == 34);
    CHECK(!aht20_read_measurement(NULL, &humidity));
}

static void test_shared_nibbles_and_uncalibrated_data(void)
{
    reset_sensor();
    memcpy(sensor.frame, (uint8_t[]){0x08, 0, 0, 0xF0, 0, 0}, 6);
    float temperature = 12, humidity = 34;
    CHECK(aht20_read_measurement(&temperature, &humidity));
    CHECK(temperature == -50.0f && fabsf(humidity - 0.0014305115f) < 0.0000001f);
    sensor.calibrated = false;
    CHECK(!aht20_read_measurement(&temperature, &humidity));
    CHECK(temperature == -50.0f && fabsf(humidity - 0.0014305115f) < 0.0000001f);
}

static void test_diagnostics_identify_failed_stage_and_hal_error(void)
{
    reset_sensor();
    sensor.tx_status = HAL_ERROR;
    sensor.error = 4;
    CHECK(!aht20_start_measurement());
    aht20_diagnostics_t diag = aht20_get_diagnostics();
    CHECK(diag.stage == AHT20_STAGE_TRIGGER && diag.error == AHT20_ERROR_TRANSMIT);
    CHECK(diag.hal_status == HAL_ERROR && diag.hal_error == 4 && !diag.status_valid);

    reset_sensor();
    sensor.rx_status = HAL_BUSY;
    sensor.error = 32;
    CHECK(!aht20_init());
    diag = aht20_get_diagnostics();
    CHECK(diag.stage == AHT20_STAGE_INITIAL_STATUS && diag.error == AHT20_ERROR_RECEIVE);
    CHECK(diag.hal_status == HAL_BUSY && diag.hal_error == 32 && !diag.status_valid);

    reset_sensor();
    CHECK(aht20_init());
    diag = aht20_get_diagnostics();
    CHECK(diag.error == AHT20_ERROR_NONE && diag.hal_status == HAL_OK && diag.hal_error == 0);
    CHECK(diag.status_valid && diag.status == 0x08);

    sensor.conversion_ms = 1000;
    CHECK(aht20_start_measurement());
    CHECK(!aht20_wait_for_measurement());
    diag = aht20_get_diagnostics();
    CHECK(diag.stage == AHT20_STAGE_WAIT_STATUS && diag.error == AHT20_ERROR_MEASUREMENT_TIMEOUT);
    CHECK(diag.hal_status == HAL_OK && diag.hal_error == 0 && diag.status_valid && diag.status == 0x88);

    float temperature = 12, humidity = 34;
    CHECK(!aht20_read_measurement(&temperature, &humidity));
    diag = aht20_get_diagnostics();
    CHECK(diag.stage == AHT20_STAGE_DATA && diag.error == AHT20_ERROR_SENSOR_BUSY);
    CHECK(diag.status_valid && diag.status == 0x88 && temperature == 12 && humidity == 34);
    CHECK(!aht20_read_measurement(NULL, &humidity));
    diag = aht20_get_diagnostics();
    CHECK(diag.error == AHT20_ERROR_INVALID_ARGUMENT && !diag.status_valid);

    reset_sensor();
    sensor.calibrated = false;
    sensor.calibration_stuck = true;
    CHECK(!aht20_init());
    diag = aht20_get_diagnostics();
    CHECK(diag.stage == AHT20_STAGE_CALIBRATION_STATUS && diag.error == AHT20_ERROR_CALIBRATION_TIMEOUT);
    CHECK(diag.status_valid && diag.status == 0);
    CHECK(!aht20_read_measurement(&temperature, &humidity));
    diag = aht20_get_diagnostics();
    CHECK(diag.error == AHT20_ERROR_NOT_CALIBRATED && diag.status_valid);
}

int main(void)
{
    test_status_uses_read_address_without_command_payload();
    test_waits_for_conversion_before_status_access();
    test_calibration_waits_ten_milliseconds();
    test_busy_polling_is_bounded();
    test_calibration_failure_is_bounded();
    test_transport_failures_do_not_produce_measurements();
    test_shared_nibbles_and_uncalibrated_data();
    test_diagnostics_identify_failed_stage_and_hal_error();
    puts("AHT20 protocol, timing, transport failure, timeout, and decoding tests passed");
    return 0;
}
