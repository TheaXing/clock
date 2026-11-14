#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bl24c512.h"
#include "i2c.h"
#include "semphr.h"
#include "task.h"

I2C_HandleTypeDef hi2c1;
static uint8_t memory[65536];
static int scheduler = taskSCHEDULER_NOT_STARTED;
static unsigned suspended, read_calls, write_calls, ready_calls;
static unsigned fail_read_call, ready_failures;
static bool locked;
static uint32_t exception_number;
static uint32_t interrupt_mask;
static HAL_StatusTypeDef read_status = HAL_OK;
static HAL_StatusTypeDef write_status = HAL_OK;
static HAL_StatusTypeDef ready_status = HAL_OK;

/** 功能：提供测试用设备锁；参数：无；返回：唯一锁地址。 */
SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    return &locked;
}

/** 功能：获取测试用设备锁；参数：mutex，锁；wait，等待上限；返回：成功标志。 */
BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t wait) {
    assert(mutex == &locked && wait == portMAX_DELAY && !locked);
    locked = true;
    return pdTRUE;
}

/** 功能：释放测试用设备锁；参数：mutex，锁；返回：成功标志。 */
BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex) {
    assert(mutex == &locked && locked);
    locked = false;
    return pdTRUE;
}

/** 功能：提供调度器状态；参数：无；返回：当前模拟状态。 */
int xTaskGetSchedulerState(void) {
    return suspended != 0U ? taskSCHEDULER_SUSPENDED : scheduler;
}

/** 功能：模拟单次传输调度保护；参数：无；返回：无。 */
void vTaskSuspendAll(void) {
    assert(locked && suspended == 0U);
    suspended = 1U;
}

/** 功能：恢复任务并模拟其他总线使用者覆盖 HAL 状态；参数：无；返回：成功标志。 */
BaseType_t xTaskResumeAll(void) {
    assert(suspended == 1U);
    suspended = 0U;
    hi2c1.error = 0U;
    hi2c1.state = HAL_I2C_STATE_READY;
    return pdTRUE;
}

/** 功能：模拟轮询间让出处理器；参数：ticks，延时；返回：无。 */
void vTaskDelay(TickType_t ticks) {
    assert(ticks > 0U && suspended == 0U);
}

/** 功能：模拟启动前延时；参数：milliseconds，延时；返回：无。 */
void HAL_Delay(uint32_t milliseconds) {
    assert(milliseconds == 1U);
}

/** 功能：提供普通任务上下文；参数：无；返回：零。 */
uint32_t __get_IPSR(void) {
    return exception_number;
}

/** 功能：读取模拟中断屏蔽位；参数：无；返回：原始屏蔽位。 */
uint32_t __get_PRIMASK(void) {
    return interrupt_mask;
}

/** 功能：模拟关闭中断；参数：无；返回：无。 */
void __disable_irq(void) {
    interrupt_mask = 1U;
}

/** 功能：恢复调用前中断屏蔽状态；参数：value，原始屏蔽位；返回：无。 */
void __set_PRIMASK(uint32_t value) {
    interrupt_mask = value;
}

/** 功能：注入读取结果并保持实际分块行为；参数：同 HAL；返回：注入状态。 */
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *bus, uint16_t device, uint16_t address,
                                  uint16_t width, uint8_t *data, uint16_t count, uint32_t timeout) {
    assert(bus == &hi2c1 && device == 0xa0U && width == 0x10U && count <= 128U && timeout == 25U);
    assert(locked && suspended == 1U);
    ++read_calls;
    if (read_status != HAL_OK && read_calls == fail_read_call) {
        hi2c1.error = 0x20U;
        hi2c1.state = HAL_I2C_STATE_BUSY_RX;
        return read_status;
    }
    memcpy(data, memory + address, count);
    return HAL_OK;
}

/** 功能：注入写入结果并检查页边界；参数：同 HAL；返回：注入状态。 */
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *bus, uint16_t device, uint16_t address,
                                   uint16_t width, uint8_t *data, uint16_t count, uint32_t timeout) {
    assert(bus == &hi2c1 && device == 0xa0U && width == 0x10U && timeout == 25U);
    assert(locked && suspended == 1U && address % 128U + count <= 128U);
    ++write_calls;
    if (write_status != HAL_OK) {
        hi2c1.error = 0x04U;
        hi2c1.state = HAL_I2C_STATE_BUSY_TX;
        return write_status;
    }
    memcpy(memory + address, data, count);
    return HAL_OK;
}

/** 功能：注入应答探测结果并逐次更新错误；参数：同 HAL；返回：注入状态。 */
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *bus, uint16_t device,
                                      uint32_t trials, uint32_t timeout) {
    assert(bus == &hi2c1 && device == 0xa0U && trials == 1U && timeout == 2U);
    assert(locked && suspended == 1U);
    ++ready_calls;
    if (ready_calls <= ready_failures) {
        hi2c1.error = 0x100U + ready_calls;
        hi2c1.state = HAL_I2C_STATE_BUSY;
        return ready_status;
    }
    return HAL_OK;
}

/** 功能：提供失败发生时的 HAL 错误码；参数：bus，总线；返回：错误码。 */
uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *bus) {
    assert(bus == &hi2c1 && suspended == 1U);
    return bus->error;
}

/** 功能：提供失败发生时的 HAL 状态；参数：bus，总线；返回：状态值。 */
HAL_I2C_StateTypeDef HAL_I2C_GetState(I2C_HandleTypeDef *bus) {
    assert(bus == &hi2c1 && suspended == 1U);
    return (HAL_I2C_StateTypeDef)bus->state;
}

/** 功能：重置故障注入且保留驱动最近故障；参数：无；返回：无。 */
static void reset_injection(void) {
    read_calls = 0U;
    write_calls = 0U;
    ready_calls = 0U;
    fail_read_call = 0U;
    ready_failures = 0U;
    read_status = HAL_OK;
    write_status = HAL_OK;
    ready_status = HAL_OK;
}

/** 功能：检查对外故障快照与调用者中断状态；参数：预期字段；返回：无。 */
static void expect_failure(uint8_t operation, uint32_t address, uint16_t length,
                           uint32_t status, uint32_t error, uint32_t state) {
    bl24c512_diagnostic_t actual;
    for (uint32_t masked = 0U; masked <= 1U; ++masked) {
        interrupt_mask = masked;
        memset(&actual, 0, sizeof actual);
        bl24c512_get_diagnostic(&actual);
        assert(interrupt_mask == masked);
        assert(actual.valid && actual.operation == operation);
        assert(actual.address == address && actual.length == length);
        assert(actual.hal_status == status && actual.hal_error == error && actual.hal_state == state);
    }
    interrupt_mask = 0U;
}

/** 功能：验证成功读写不生成虚假故障；参数：无；返回：无。 */
static void test_success_has_no_failure(void) {
    uint8_t input[300], output[300];
    bl24c512_diagnostic_t actual;
    reset_injection();
    ready_status = HAL_ERROR;
    ready_failures = 1U;
    memset(input, 0x5a, sizeof input);
    assert(bl24c512_write(127U, input, sizeof input));
    assert(bl24c512_read(127U, output, sizeof output));
    assert(memcmp(input, output, sizeof input) == 0);
    bl24c512_get_diagnostic(&actual);
    assert(!actual.valid && actual.operation == BL24C512_OPERATION_NONE);
    bl24c512_get_diagnostic(NULL);
}

/** 功能：验证读取超时记录失败分块的位置及恢复调度前状态；参数：无；返回：无。 */
static void test_read_timeout_records_failed_chunk(void) {
    uint8_t output[300];
    reset_injection();
    read_status = HAL_TIMEOUT;
    fail_read_call = 2U;
    assert(!bl24c512_read(0x800U, output, sizeof output));
    assert(read_calls == 2U && interrupt_mask == 0U);
    expect_failure(BL24C512_OPERATION_READ, 0x880U, 128U, 3U, 0x20U, 0x22U);
}

/** 功能：验证写失败不继续探测且替换最近故障；参数：无；返回：无。 */
static void test_write_error_records_page_fragment(void) {
    uint8_t input[20] = {0};
    reset_injection();
    write_status = HAL_ERROR;
    interrupt_mask = 1U;
    assert(!bl24c512_write(127U, input, sizeof input));
    assert(write_calls == 1U && ready_calls == 0U && interrupt_mask == 1U);
    expect_failure(BL24C512_OPERATION_WRITE, 127U, 1U, 1U, 0x04U, 0x21U);
}

/** 功能：验证应答探测耗尽时保留最后失败及关联写页；参数：无；返回：无。 */
static void test_ready_busy_records_last_failure(void) {
    uint8_t input[128] = {0};
    reset_injection();
    ready_status = HAL_BUSY;
    ready_failures = 20U;
    assert(!bl24c512_write(0x980U, input, sizeof input));
    assert(write_calls == 1U && ready_calls == 20U);
    expect_failure(BL24C512_OPERATION_READY, 0x980U, 128U, 2U, 0x114U, 0x24U);
}

/** 功能：验证正常写周期的短暂非应答及成功不会覆盖最近故障；参数：无；返回：无。 */
static void test_recovered_success_keeps_failure(void) {
    uint8_t input[2] = {1U, 2U}, output[2];
    reset_injection();
    ready_status = HAL_ERROR;
    ready_failures = 1U;
    assert(bl24c512_write(0x900U, input, sizeof input));
    assert(ready_calls == 2U);
    expect_failure(BL24C512_OPERATION_READY, 0x980U, 128U, 2U, 0x114U, 0x24U);
    assert(bl24c512_read(0x900U, output, sizeof output));
    assert(memcmp(input, output, sizeof input) == 0);
    assert(!bl24c512_read(65536U, output, 1U));
    assert(!bl24c512_write(0U, NULL, 1U));
    expect_failure(BL24C512_OPERATION_READY, 0x980U, 128U, 2U, 0x114U, 0x24U);
}

/** 功能：执行真实驱动故障诊断回归；参数：无；返回：通过返回零。 */
int main(void) {
    assert(!bl24c512_transaction_begin());
    assert(bl24c512_transaction_init());
    assert(bl24c512_transaction_init());
    exception_number = 1U;
    assert(!bl24c512_transaction_begin());
    exception_number = 0U;
    scheduler = taskSCHEDULER_SUSPENDED;
    assert(!bl24c512_transaction_begin());
    scheduler = taskSCHEDULER_RUNNING;
    assert(bl24c512_transaction_begin());
    test_success_has_no_failure();
    test_read_timeout_records_failed_chunk();
    test_write_error_records_page_fragment();
    test_ready_busy_records_last_failure();
    test_recovered_success_keeps_failure();
    bl24c512_transaction_end();
    assert(!locked && suspended == 0U);
    puts("BL24C512 failure diagnostics: 5 scenarios passed");
    return 0;
}
