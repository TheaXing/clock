#include "aht20.h"
#include "i2c.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stddef.h>

#define AHT20_ADDRESS ((uint16_t)(0x38U << 1))
#define AHT20_IO_TIMEOUT_MS 25U

static aht20_diagnostics_t diagnostics;

/**
 * 功能：取得最近一次传感器操作的诊断快照。
 * 参数：无。
 * 返回：最近操作的阶段、错误及状态信息。
 */
aht20_diagnostics_t aht20_get_diagnostics(void) {
    return diagnostics;
}

/**
 * 功能：清空旧诊断并记录即将进行的传感器阶段。
 * 参数：stage，即将开始的操作阶段。
 * 返回：无。
 */
static void begin_stage(aht20_stage_t stage) {
    diagnostics = (aht20_diagnostics_t){0};
    diagnostics.stage = stage;
}

/**
 * 功能：记录 I2C 传输结果及 HAL 错误，传输失败时设置阶段错误。
 * 参数：result，HAL 传输结果；error，失败时记录的错误类型。
 * 返回：传输成功返回 true，否则返回 false。
 */
static bool transfer_result(HAL_StatusTypeDef result, aht20_error_t error) {
    diagnostics.hal_status = (uint32_t)result;
    diagnostics.hal_error = result == HAL_OK ? 0U : HAL_I2C_GetError(&hi2c2);
    if (result != HAL_OK) {
        diagnostics.error = error;
        return false;
    }
    return true;
}

/**
 * 功能：根据调度器状态选择任务延时或 HAL 延时，并补偿节拍误差。
 * 参数：milliseconds，延时毫秒数。
 * 返回：无。
 */
static void sensor_delay(uint32_t milliseconds) {
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        TickType_t ticks = pdMS_TO_TICKS(milliseconds);
        /* 相对于实际时间，vTaskDelay 最多可能提前一个节拍返回。 */
        vTaskDelay(ticks + 1U);
    } else {
        HAL_Delay(milliseconds);
    }
}

/**
 * 功能：向 AHT20 发送命令并记录发送诊断。
 * 参数：bytes，传输字节缓冲区；length，传输或处理的字节数。
 * 返回：发送成功返回 true，否则返回 false。
 */
static bool write_command(uint8_t *bytes, uint16_t length) {
    return transfer_result(
        HAL_I2C_Master_Transmit(&hi2c2, AHT20_ADDRESS, bytes, length, AHT20_IO_TIMEOUT_MS),
        AHT20_ERROR_TRANSMIT);
}

/**
 * 功能：从 AHT20 接收指定数量的字节并更新接收诊断。
 * 参数：bytes，传输字节缓冲区；length，传输或处理的字节数。
 * 返回：接收成功返回 true，否则返回 false。
 */
static bool read_bytes(uint8_t *bytes, uint16_t length) {
    diagnostics.status_valid = false;
    return transfer_result(
        HAL_I2C_Master_Receive(&hi2c2, AHT20_ADDRESS, bytes, length, AHT20_IO_TIMEOUT_MS),
        AHT20_ERROR_RECEIVE);
}

/**
 * 功能：读取 AHT20 单字节状态并标记诊断状态有效。
 * 参数：status，接收传感器状态字节的地址。
 * 返回：读取成功返回 true，否则返回 false。
 */
static bool read_status(uint8_t *status) {
    /* 时序图中的 0x71 是 (0x38 << 1) | READ 地址字节，不是命令；HAL 在启动接收时自动发送该地址。 */
    if (!read_bytes(status, 1U)) {
        return false;
    }
    diagnostics.status = *status;
    diagnostics.status_valid = true;
    return true;
}

/**
 * 功能：检查传感器校准状态，必要时启动校准并有限次等待完成。
 * 参数：无。
 * 返回：已校准且通信成功返回 true，否则返回 false。
 */
bool aht20_init(void) {
    uint8_t status;
    begin_stage(AHT20_STAGE_INITIAL_STATUS);
    sensor_delay(40U);
    if (!read_status(&status)) {
        return false;
    }
    if ((status & 0x08U) != 0U) {
        return true;
    }

    uint8_t initialize[] = {0xBEU, 0x08U, 0x00U};
    begin_stage(AHT20_STAGE_CALIBRATION);
    if (!write_command(initialize, sizeof initialize)) {
        return false;
    }
    /* 初始化命令发送后也必须等待 10 毫秒再读取状态。 */
    begin_stage(AHT20_STAGE_CALIBRATION_STATUS);
    for (uint32_t attempt = 0U; attempt < 10U; ++attempt) {
        sensor_delay(10U);
        if (!read_status(&status)) {
            return false;
        }
        if ((status & 0x08U) != 0U) {
            return true;
        }
    }
    diagnostics.error = AHT20_ERROR_CALIBRATION_TIMEOUT;
    return false;
}

/**
 * 功能：发送 AHT20 温湿度转换触发命令。
 * 参数：无。
 * 返回：命令发送成功返回 true，否则返回 false。
 */
bool aht20_start_measurement(void) {
    uint8_t command[] = {0xACU, 0x33U, 0x00U};
    begin_stage(AHT20_STAGE_TRIGGER);
    return write_command(command, sizeof command);
}

/**
 * 功能：等待最短转换时间后，有界轮询传感器忙状态和校准状态。
 * 参数：无。
 * 返回：转换完成且已校准返回 true，否则返回 false。
 */
bool aht20_wait_for_measurement(void) {
    begin_stage(AHT20_STAGE_WAIT_STATUS);
    /* 奥松要求首次读取之前至少等待 80 毫秒以完成转换。 */
    sensor_delay(80U);
    for (uint32_t waited = 80U; waited <= 200U; waited += 10U) {
        uint8_t status;
        if (!read_status(&status)) {
            return false;
        }
        if ((status & 0x80U) == 0U) {
            if ((status & 0x08U) == 0U) {
                diagnostics.error = AHT20_ERROR_NOT_CALIBRATED;
                return false;
            }
            return true;
        }
        if (waited < 200U) {
            sensor_delay(10U);
        }
    }
    diagnostics.error = AHT20_ERROR_MEASUREMENT_TIMEOUT;
    return false;
}

/**
 * 功能：读取并校验六字节测量帧，将原始值转换为温湿度。
 * 参数：temperature，接收摄氏温度的地址；humidity，接收相对湿度百分比的地址。
 * 返回：有效测量读取成功返回 true，否则返回 false，失败时不写入温湿度。
 */
bool aht20_read_measurement(float *temperature, float *humidity) {
    uint8_t data[6];
    begin_stage(AHT20_STAGE_DATA);
    if (temperature == NULL || humidity == NULL) {
        diagnostics.error = AHT20_ERROR_INVALID_ARGUMENT;
        return false;
    }
    if (!read_bytes(data, sizeof data)) {
        return false;
    }
    diagnostics.status = data[0];
    diagnostics.status_valid = true;
    if ((data[0] & 0x80U) != 0U) {
        diagnostics.error = AHT20_ERROR_SENSOR_BUSY;
        return false;
    }
    if ((data[0] & 0x08U) == 0U) {
        diagnostics.error = AHT20_ERROR_NOT_CALIBRATED;
        return false;
    }

    uint32_t raw_humidity =
        ((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | ((uint32_t)data[3] >> 4);
    uint32_t raw_temperature =
        ((uint32_t)(data[3] & 0x0FU) << 16) | ((uint32_t)data[4] << 8) | data[5];
    *humidity = (float)raw_humidity * (100.0f / 1048576.0f);
    *temperature = (float)raw_temperature * (200.0f / 1048576.0f) - 50.0f;
    return true;
}
