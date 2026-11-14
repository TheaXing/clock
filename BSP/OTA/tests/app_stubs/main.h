#ifndef OTA_APP_TEST_MAIN_H
#define OTA_APP_TEST_MAIN_H
#include <stdint.h>
/** 提供复位原因适配器使用的虚拟 RCC 寄存器。 */
typedef struct {
    uint32_t CSR; // 复位标志快照寄存器
} ota_test_rcc_t;
extern ota_test_rcc_t *ota_test_rcc;
void ota_test_clear_reset_flags(void);
#define RCC ota_test_rcc
#define __HAL_RCC_CLEAR_RESET_FLAGS() ota_test_clear_reset_flags()
void NVIC_SystemReset(void);
void Error_Handler(void);
#endif
