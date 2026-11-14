#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "ota_reset.h"
#include "main.h"
static ota_test_rcc_t registers;
ota_test_rcc_t *ota_test_rcc = &registers;
static unsigned cleared;

/** 功能：模拟清理硬件复位标志；参数：无；返回：无。 */
void ota_test_clear_reset_flags(void) {
    registers.CSR = 0U;
    ++cleared;
}

/** 功能：验证软件回退与新物理启动分类及一次快照清除；参数：无；返回：通过为零。 */
int main(void) {
    assert(!ota_reset_flags_are_software(0U));
    assert(ota_reset_flags_are_software(1UL << 28U));
    assert(!ota_reset_flags_are_software(1UL << 26U));
    assert(ota_reset_flags_are_software((1UL << 28U) | (1UL << 27U)));
    assert(ota_reset_flags_are_software((1UL << 28U) | (1UL << 26U)));
    assert(ota_reset_flags_are_software((1UL << 28U) | (1UL << 25U)));
    assert(!ota_reset_flags_allow_retry(0U));
    assert(ota_reset_flags_allow_retry(1UL << 26U));
    assert(ota_reset_flags_allow_retry((1UL << 25U) | (1UL << 27U)));
    assert(!ota_reset_flags_allow_retry((1UL << 28U) | (1UL << 26U)));
    assert(!ota_reset_flags_allow_retry((1UL << 29U) | (1UL << 26U)));
    assert(!ota_reset_flags_allow_retry((1UL << 30U) | (1UL << 26U)));
    assert(!ota_reset_flags_allow_retry((1UL << 31U) | (1UL << 26U)));
    registers.CSR = 1UL << 28U;
    assert(ota_reset_was_software());
    assert(registers.CSR == 0U && cleared == 1U);
    registers.CSR = 1UL << 26U;
    assert(ota_reset_was_software());
    assert(!ota_reset_is_normal_boot());
    assert(cleared == 1U);
    puts("OTA reset snapshot adapter passed");
    return 0;
}
