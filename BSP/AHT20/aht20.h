#ifndef CLOCK_AHT20_H
#define CLOCK_AHT20_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 接口仅供任务上下文调用；首次使用前初始化 I2C2，传感器由单个任务独占。 */
typedef enum {
    AHT20_STAGE_NONE = 0,
    AHT20_STAGE_INITIAL_STATUS,
    AHT20_STAGE_CALIBRATION,
    AHT20_STAGE_CALIBRATION_STATUS,
    AHT20_STAGE_TRIGGER,
    AHT20_STAGE_WAIT_STATUS,
    AHT20_STAGE_DATA
} aht20_stage_t;

typedef enum {
    AHT20_ERROR_NONE = 0,
    AHT20_ERROR_TRANSMIT,
    AHT20_ERROR_RECEIVE,
    AHT20_ERROR_CALIBRATION_TIMEOUT,
    AHT20_ERROR_MEASUREMENT_TIMEOUT,
    AHT20_ERROR_NOT_CALIBRATED,
    AHT20_ERROR_INVALID_ARGUMENT,
    AHT20_ERROR_SENSOR_BUSY
} aht20_error_t;

/** 保存最近一次 AHT20 操作的阶段、错误及状态快照。 */
typedef struct {
    aht20_stage_t stage; // 最近操作阶段
    aht20_error_t error; // 最近传感器操作错误类型
    uint32_t hal_status; // 最近 HAL 传输状态
    uint32_t hal_error;  // 最近 HAL I2C 错误码，成功时为 0
    uint8_t status;      // 最近读取的传感器状态字节
    bool status_valid;   // 状态字节是否有效
} aht20_diagnostics_t;

bool aht20_init(void);
bool aht20_start_measurement(void);
bool aht20_wait_for_measurement(void);
bool aht20_read_measurement(float *temperature, float *humidity);
/* 由拥有传感器的任务读取最近一次操作快照。hal_status 为 HAL_StatusTypeDef 值，hal_error 来自 HAL_I2C_GetError()；仅在 status_valid 为真时 status 才有效。 */
aht20_diagnostics_t aht20_get_diagnostics(void);

#ifdef __cplusplus
}
#endif
#endif
