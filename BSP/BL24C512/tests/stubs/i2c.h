#ifndef BL24C512_TEST_I2C_H
#define BL24C512_TEST_I2C_H
#include <stdint.h>
/** 保存真实 HAL 接口所需的诊断字段。 */
typedef struct {
    uint32_t error; // 当前 HAL 错误码
    uint32_t state; // 当前 HAL 状态值
} I2C_HandleTypeDef;
typedef enum {
    HAL_OK = 0,
    HAL_ERROR = 1,
    HAL_BUSY = 2,
    HAL_TIMEOUT = 3
} HAL_StatusTypeDef;
typedef enum {
    HAL_I2C_STATE_READY = 0x20U,
    HAL_I2C_STATE_BUSY = 0x24U,
    HAL_I2C_STATE_BUSY_TX = 0x21U,
    HAL_I2C_STATE_BUSY_RX = 0x22U
} HAL_I2C_StateTypeDef;
extern I2C_HandleTypeDef hi2c1;
#define I2C_MEMADD_SIZE_16BIT 0x10U
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *, uint16_t, uint16_t, uint16_t,
                                  uint8_t *, uint16_t, uint32_t);
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *, uint16_t, uint16_t, uint16_t,
                                   uint8_t *, uint16_t, uint32_t);
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *, uint16_t, uint32_t, uint32_t);
uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *bus);
HAL_I2C_StateTypeDef HAL_I2C_GetState(I2C_HandleTypeDef *bus);
void HAL_Delay(uint32_t milliseconds);
uint32_t __get_IPSR(void);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t value);
#endif
