#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bl24c512.h"
#include "i2c.h"
#include "task.h"
#include "semphr.h"

I2C_HandleTypeDef hi2c1;
static bool locked, allocation_failed;
static unsigned irq, creates, takes, gives, suspend_depth, delays;
static int scheduler = taskSCHEDULER_RUNNING;
static uint8_t memory[65536];

/** 功能：创建唯一设备锁并模拟内存不足；参数：无；返回：锁或空。 */
SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    ++creates;
    return allocation_failed ? NULL : &locked;
}

/** 功能：取得虚拟设备锁；参数：mutex，锁；wait，等待值；返回：是否取得。 */
BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t wait) {
    assert(mutex == &locked && wait == portMAX_DELAY && irq == 0U);
    ++takes;
    if (locked) {
        return pdFALSE;
    }
    locked = true;
    return pdTRUE;
}

/** 功能：释放虚拟设备锁；参数：mutex，锁；返回：成功。 */
BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex) {
    assert(mutex == &locked && locked);
    ++gives;
    locked = false;
    return pdTRUE;
}

/** 功能：返回虚拟中断上下文；参数：无；返回：异常编号。 */
uint32_t __get_IPSR(void) {
    return irq;
}

/** 功能：返回虚拟调度状态；参数：无；返回：调度状态。 */
int xTaskGetSchedulerState(void) {
    return suspend_depth != 0U ? taskSCHEDULER_SUSPENDED : scheduler;
}

/** 功能：保护一笔总线传输；参数：无；返回：无。 */
void vTaskSuspendAll(void) {
    assert(locked && suspend_depth == 0U);
    ++suspend_depth;
}

/** 功能：结束一笔总线传输保护；参数：无；返回：成功。 */
BaseType_t xTaskResumeAll(void) {
    assert(locked && suspend_depth == 1U);
    --suspend_depth;
    return pdTRUE;
}

/** 功能：验证分块让出处理器时仍持有完整事务锁；参数：ticks，等待；返回：无。 */
void vTaskDelay(TickType_t ticks) {
    assert(locked && suspend_depth == 0U && ticks > 0U);
    ++delays;
}

/** 功能：验证启动前有界延时；参数：milliseconds，等待；返回：无。 */
void HAL_Delay(uint32_t milliseconds) {
    assert(locked && milliseconds == 1U);
}

/** 功能：执行虚拟 HAL 读取并检查锁；参数：同 HAL；返回：成功。 */
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *bus, uint16_t device, uint16_t address,
                                  uint16_t width, uint8_t *data, uint16_t count, uint32_t timeout) {
    assert(bus == &hi2c1 && device == 0xa0U && width == 2U && count <= 128U && timeout <= 25U);
    assert(locked && suspend_depth == 1U);
    memcpy(data, memory + address, count);
    return HAL_OK;
}

/** 功能：执行虚拟 HAL 页写入并检查锁；参数：同 HAL；返回：成功。 */
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *bus, uint16_t device, uint16_t address,
                                   uint16_t width, uint8_t *data, uint16_t count, uint32_t timeout) {
    assert(bus == &hi2c1 && device == 0xa0U && width == 2U && timeout <= 25U);
    assert(locked && suspend_depth == 1U && address % 128U + count <= 128U);
    memcpy(memory + address, data, count);
    return HAL_OK;
}

/** 功能：模拟 EEPROM 写周期已结束；参数：同 HAL；返回：成功。 */
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *bus, uint16_t device,
                                      uint32_t trials, uint32_t timeout) {
    assert(bus == &hi2c1 && device == 0xa0U && trials == 1U && timeout <= 2U);
    assert(locked && suspend_depth == 1U);
    return HAL_OK;
}

/** 功能：验证完整事务生命周期及 ISR、暂停调度和分配失败拒绝；参数：无；返回：通过返回零。 */
int main(void) {
    uint8_t input[300], output[300];
    memset(input, 0x5a, sizeof input);
    assert(!bl24c512_transaction_init() && creates == 0U);
    scheduler = taskSCHEDULER_NOT_STARTED;
    allocation_failed = true;
    assert(!bl24c512_transaction_init() && creates == 1U);
    assert(!bl24c512_transaction_begin() && takes == 0U);
    allocation_failed = false;
    assert(bl24c512_transaction_init() && creates == 2U);
    assert(bl24c512_transaction_init() && creates == 2U);
    scheduler = taskSCHEDULER_RUNNING;
    irq = 1U;
    assert(!bl24c512_transaction_begin() && takes == 0U);
    irq = 0U;
    scheduler = taskSCHEDULER_SUSPENDED;
    assert(!bl24c512_transaction_begin() && takes == 0U);
    scheduler = taskSCHEDULER_RUNNING;
    assert(bl24c512_transaction_begin());
    assert(!bl24c512_transaction_begin());
    assert(bl24c512_write(127U, input, sizeof input));
    assert(bl24c512_read(127U, output, sizeof output));
    assert(memcmp(input, output, sizeof input) == 0 && locked && delays > 0U);
    assert(gives == 0U);
    bl24c512_transaction_end();
    assert(!locked && gives == 1U && suspend_depth == 0U);
    puts("OTA EEPROM transaction locking passed");
    return 0;
}
