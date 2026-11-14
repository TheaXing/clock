/* 依据奥松 AHT20 V1.2 第 5.4 节（第 9 页）建立传输与时序模型。0x71 是总线读地址，不能作为命令载荷；模型拒绝未支持命令与过早访问，而非始终应答成功。 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "i2c.h"
#include "aht20.h"

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
            exit(1); \
        } \
    } while (0)

I2C_HandleTypeDef hi2c2;
/** 保存 AHT20 协议与时序测试的模拟设备状态。 */
static struct {
    uint8_t frame[6];             // 模拟六字节测量帧
    bool calibrated;              // 模拟传感器是否已完成校准
    bool calibrating;             // 模拟传感器是否正在校准
    bool measuring;               // 模拟传感器是否正在转换
    uint32_t calibration_started; // 校准开始的模拟毫秒时刻
    uint32_t measurement_started; // 转换开始的模拟毫秒时刻
    uint32_t conversion_ms;       // 模拟转换所需毫秒数
    uint32_t elapsed;             // 累计经过的模拟毫秒数
    uint32_t error;               // 模拟 I2C 错误码
    HAL_StatusTypeDef tx_status;  // 注入的 HAL 发送状态
    HAL_StatusTypeDef rx_status;  // 注入的 HAL 接收状态
    unsigned writes;              // 累计命令发送次数
    unsigned reads;               // 累计数据读取次数
    bool early_access;            // 是否检测到过早访问
    bool unknown_command;         // 是否收到未支持的命令载荷
    bool calibration_stuck;       // 是否模拟校准始终不完成
    int scheduler_running;        // 是否模拟调度器运行
} sensor;

/**
 * 功能：重置模拟 AHT20，并装入已校准的标准温湿度帧。
 * 参数：无。
 * 返回：无。
 */
static void reset_sensor(void) {
    memset(&sensor, 0, sizeof sensor);
    sensor.calibrated = true;
    sensor.conversion_ms = 80;
    memcpy(sensor.frame, (uint8_t[]){0x08, 0x80, 0x00, 0x06, 0x00, 0x00}, 6);
}

/**
 * 功能：返回测试设定的调度器状态。
 * 参数：无。
 * 返回：调度器未启动、暂停或运行状态常量。
 */
int xTaskGetSchedulerState(void) {
    return sensor.scheduler_running ? 2 : 0;
}
/* 在节拍切换前开始的延时，实际持续时间可能比指定节拍数少一个节拍。 */
/**
 * 功能：模拟任务延时并累计传感器经过时间，覆盖少一个节拍的边界。
 * 参数：ticks，模拟延时节拍数。
 * 返回：无。
 */
void vTaskDelay(uint32_t ticks) {
    CHECK(ticks > 0);
    sensor.elapsed += ticks - 1U;
}
/**
 * 功能：模拟 HAL 毫秒延时并累计传感器经过时间。
 * 参数：milliseconds，延时毫秒数。
 * 返回：无。
 */
void HAL_Delay(uint32_t milliseconds) {
    sensor.elapsed += milliseconds;
}
/**
 * 功能：校验传感器总线句柄并返回注入的 I2C 错误。
 * 参数：i2c，待校验的 I2C 总线句柄。
 * 返回：模拟 I2C 错误码。
 */
uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *i2c) {
    CHECK(i2c == &hi2c2);
    return sensor.error;
}

/**
 * 功能：模拟 AHT20 校准与测量命令，拒绝未知载荷并记录发送次数。
 * 参数：i2c，待校验的 I2C 总线句柄；
 *       addr，左移一位后的传感器地址；
 *       data，传输数据缓冲区；
 *       len，本次收发字节数；
 *       timeout，等待超时值。
 * 返回：模拟发送状态，未知命令返回 HAL_ERROR。
 */
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *i2c, uint16_t addr, uint8_t *data,
                                          uint16_t len, uint32_t timeout) {
    CHECK(i2c == &hi2c2 && addr == 0x70 && timeout > 0 && timeout < 1000);
    ++sensor.writes;
    if (sensor.tx_status != HAL_OK) {
        return sensor.tx_status;
    }
    if (len == 3 && data[0] == 0xBE && data[1] == 0x08 && data[2] == 0) {
        sensor.calibrating = true;
        sensor.calibration_started = sensor.elapsed;
        return HAL_OK;
    }
    if (len == 3 && data[0] == 0xAC && data[1] == 0x33 && data[2] == 0) {
        sensor.measuring = true;
        sensor.measurement_started = sensor.elapsed;
        return HAL_OK;
    }
    sensor.unknown_command = true;
    sensor.error = 4; /* HAL_I2C_ERROR_AF：对未定义的命令载荷返回非应答。 */
    return HAL_ERROR;
}

/**
 * 功能：模拟 AHT20 状态与数据帧，校验转换或校准时序并记录读取次数。
 * 参数：i2c，待校验的 I2C 总线句柄；
 *       addr，左移一位后的传感器地址；
 *       data，传输数据缓冲区；
 *       len，本次收发字节数；
 *       timeout，等待超时值。
 * 返回：模拟接收状态，过早访问返回 HAL_ERROR。
 */
HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *i2c, uint16_t addr, uint8_t *data,
                                         uint16_t len, uint32_t timeout) {
    CHECK(i2c == &hi2c2 && addr == 0x70 && timeout > 0 && timeout < 1000);
    CHECK(len == 1 || len == 6);
    ++sensor.reads;
    if (sensor.rx_status != HAL_OK) {
        return sensor.rx_status;
    }
    if ((sensor.measuring && sensor.elapsed - sensor.measurement_started < 80) ||
        (sensor.calibrating && sensor.elapsed - sensor.calibration_started < 10)) {
        sensor.early_access = true;
        sensor.error = 4;
        return HAL_ERROR;
    }
    if (sensor.calibrating && !sensor.calibration_stuck) {
        sensor.calibrated = true;
        sensor.calibrating = false;
    }
    uint8_t status = sensor.calibrated ? 0x08 : 0x00;
    if (sensor.measuring && sensor.elapsed - sensor.measurement_started < sensor.conversion_ms) {
        status |= 0x80;
    }
    if (len == 1) {
        data[0] = status;
    } else {
        memcpy(data, sensor.frame, len);
        data[0] = status;
    }
    return HAL_OK;
}

/**
 * 功能：验证读取状态只发读地址而不发送伪命令载荷。
 * 参数：无。
 * 返回：无，检查失败时终止进程。
 */
static void test_status_uses_read_address_without_command_payload(void) {
    reset_sensor();
    CHECK(aht20_init());
    CHECK(!sensor.unknown_command && sensor.writes == 0 && sensor.reads == 1);
}

/**
 * 功能：验证任务节拍边界下仍等待足够转换时间，并正确解码温湿度。
 * 参数：无。
 * 返回：无，检查失败时终止进程。
 */
static void test_waits_for_conversion_before_status_access(void) {
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

/**
 * 功能：验证校准命令后至少等待十毫秒再访问状态。
 * 参数：无。
 * 返回：无，检查失败时终止进程。
 */
static void test_calibration_waits_ten_milliseconds(void) {
    reset_sensor();
    sensor.calibrated = false;
    CHECK(aht20_init());
    CHECK(!sensor.early_access && sensor.calibrated);
}

/**
 * 功能：验证持续忙状态的轮询有界且不覆盖调用方测量值。
 * 参数：无。
 * 返回：无，检查失败时终止进程。
 */
static void test_busy_polling_is_bounded(void) {
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

/**
 * 功能：验证校准不能完成时在有限时间内失败。
 * 参数：无。
 * 返回：无，检查失败时终止进程。
 */
static void test_calibration_failure_is_bounded(void) {
    reset_sensor();
    sensor.calibrated = false;
    sensor.calibration_stuck = true;
    CHECK(!aht20_init());
    CHECK(!sensor.early_access && sensor.elapsed <= 300);
}

/**
 * 功能：验证发送、接收及参数错误不会产生或覆盖温湿度结果。
 * 参数：无。
 * 返回：无，检查失败时终止进程。
 */
static void test_transport_failures_do_not_produce_measurements(void) {
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

/**
 * 功能：验证共享半字节的正确解码，并拒绝未校准数据。
 * 参数：无。
 * 返回：无，检查失败时终止进程。
 */
static void test_shared_nibbles_and_uncalibrated_data(void) {
    reset_sensor();
    memcpy(sensor.frame, (uint8_t[]){0x08, 0, 0, 0xF0, 0, 0}, 6);
    float temperature = 12, humidity = 34;
    CHECK(aht20_read_measurement(&temperature, &humidity));
    CHECK(temperature == -50.0f && fabsf(humidity - 0.0014305115f) < 0.0000001f);
    sensor.calibrated = false;
    CHECK(!aht20_read_measurement(&temperature, &humidity));
    CHECK(temperature == -50.0f && fabsf(humidity - 0.0014305115f) < 0.0000001f);
}

/**
 * 功能：验证诊断能够区分操作阶段、HAL 错误及传感器状态。
 * 参数：无。
 * 返回：无，检查失败时终止进程。
 */
static void test_diagnostics_identify_failed_stage_and_hal_error(void) {
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
    CHECK(diag.hal_status == HAL_OK && diag.hal_error == 0 && diag.status_valid &&
          diag.status == 0x88);

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
    CHECK(diag.stage == AHT20_STAGE_CALIBRATION_STATUS &&
          diag.error == AHT20_ERROR_CALIBRATION_TIMEOUT);
    CHECK(diag.status_valid && diag.status == 0);
    CHECK(!aht20_read_measurement(&temperature, &humidity));
    diag = aht20_get_diagnostics();
    CHECK(diag.error == AHT20_ERROR_NOT_CALIBRATED && diag.status_valid);
}

/**
 * 功能：运行 AHT20 协议、转换时序、失败诊断及温湿度解码回归测试。
 * 参数：无。
 * 返回：全部检查通过返回 0，检查失败时终止进程。
 */
int main(void) {
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
