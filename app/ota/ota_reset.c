#include "ota_reset.h"
#include "ota_config.h"
#if CLOCK_OTA_ENABLED
#include "main.h"
#endif
static bool captured;
static uint32_t reset_flags;

/** 功能：保守判定 RCC 快照中的软件复位；参数：flags，CSR 原始值；返回：含软件标志即为真，混合粘滞标志不授权自动重试。 */
bool ota_reset_flags_are_software(uint32_t flags) {
    return (flags & (1UL << 28U)) != 0U;
}

/** 功能：仅允许明确的全新电源或外部复位重试历史失败；参数：flags，CSR 快照；返回：正常启动且无软件、看门狗或低功耗复位标志时为真。 */
bool ota_reset_flags_allow_retry(uint32_t flags) {
    const uint32_t physical = (1UL << 25U) | (1UL << 26U) | (1UL << 27U);
    const uint32_t other = (1UL << 28U) | (1UL << 29U) | (1UL << 30U) | (1UL << 31U);
    return (flags & physical) != 0U && (flags & other) == 0U;
}

/** 功能：仅首次受控读取并清除全部复位标志，初始化重试沿用原始快照；参数：无；返回：无。 */
static void capture_reset(void) {
    if (captured) {
        return;
    }
#if CLOCK_OTA_ENABLED
    reset_flags = RCC->CSR;
    __HAL_RCC_CLEAR_RESET_FLAGS();
#endif
    captured = true;
}

/** 功能：报告 App 初始化的复位快照是否含软件标志；参数：无；返回：是否软件复位，Boot 不清除标志。 */
bool ota_reset_was_software(void) {
    capture_reset();
    return ota_reset_flags_are_software(reset_flags);
}

/** 功能：报告 App 初始化是否为可重试失败请求的正常启动；参数：无；返回：明确正常启动为真。 */
bool ota_reset_is_normal_boot(void) {
    capture_reset();
    return ota_reset_flags_allow_retry(reset_flags);
}
