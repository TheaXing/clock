/* USER CODE BEGIN Header */
/** I2C peripheral configuration. STM32CubeMX-compatible user sections. */
/* USER CODE END Header */
#ifndef __I2C_H__
#define __I2C_H__
#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

extern I2C_HandleTypeDef hi2c1;
extern I2C_HandleTypeDef hi2c2;
void MX_I2C1_Init(void);
void MX_I2C2_Init(void);

/* USER CODE BEGIN Prototypes */
/* USER CODE END Prototypes */
#ifdef __cplusplus
}
#endif
#endif
