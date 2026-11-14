#ifndef OTA_APP_TEST_I2C_H
#define OTA_APP_TEST_I2C_H
#include <stdint.h>
/** 保存主机端 HAL 句柄占位。 */
typedef struct {
    unsigned unused; // 句柄占位值
} I2C_HandleTypeDef;
typedef enum { HAL_OK = 0, HAL_ERROR = 1 } HAL_StatusTypeDef;
typedef enum { HAL_I2C_STATE_READY = 0x20U } HAL_I2C_StateTypeDef;
extern I2C_HandleTypeDef hi2c1;
#define I2C_MEMADD_SIZE_16BIT 2U
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *, uint16_t, uint16_t, uint16_t, uint8_t *, uint16_t, uint32_t);
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *, uint16_t, uint16_t, uint16_t, uint8_t *, uint16_t, uint32_t);
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *, uint16_t, uint32_t, uint32_t);
uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *);
HAL_I2C_StateTypeDef HAL_I2C_GetState(I2C_HandleTypeDef *);
void HAL_Delay(uint32_t);
uint32_t __get_IPSR(void);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t value);
#endif
