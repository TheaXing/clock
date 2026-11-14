#include "web_session.h"
#include "main.h"

/**
 * 功能：读取硬件随机数生成本次启动的非零会话值，并在结束时关闭随机数外设。
 * 参数：无。
 * 返回：非零硬件随机值；超时或随机数外设不可用时返回 0。
 */
uint32_t clock_web_session_token(void) {
    uint32_t result = 0U;
    __HAL_RCC_RNG_CLK_ENABLE();
    RNG->CR = RNG_CR_RNGEN;
    uint32_t start = HAL_GetTick();
    unsigned samples = 0U;
    for (;;) {
        uint32_t status = RNG->SR;
        if (status & (RNG_SR_CECS | RNG_SR_SECS | RNG_SR_CEIS | RNG_SR_SEIS)) {
            break;
        }
        if (status & RNG_SR_DRDY) {
            uint32_t word = RNG->DR; /* 在真实芯片上读取 DR 会清除 DRDY 标志。 */
            if (word != 0U) {
                result = word;
                break;
            }
            if (++samples == 4U) {
                break;
            }
        }
        if (HAL_GetTick() - start >= 20U) {
            break;
        }
    }
    RNG->CR = 0U;
    __HAL_RCC_RNG_CLK_DISABLE();
    return result;
}
