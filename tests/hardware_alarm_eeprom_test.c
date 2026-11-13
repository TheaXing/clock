#include "bl24c512.h"
#include "i2c.h"
#include "task.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

I2C_HandleTypeDef hi2c1;
static uint8_t memory[65536];
static uint8_t full_read[65536];
static uint32_t reads, writes, polls, delays, boot_delays;
static uint32_t suspends, resumes, suspend_depth;
static unsigned busy, force_busy, read_error, write_error, busy_cycles;
static int scheduler = taskSCHEDULER_RUNNING;
static void check_bus(I2C_HandleTypeDef *bus, uint16_t address)
{
    assert(bus == &hi2c1 && address == 0xA0U);
    /* A UI wakeup during one blocking HAL call must not preempt the worker. */
    assert(suspend_depth == (scheduler == taskSCHEDULER_RUNNING ? 1U : 0U));
}
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *bus, uint16_t dev, uint16_t addr,
                                 uint16_t width, uint8_t *data, uint16_t count, uint32_t timeout)
{
    check_bus(bus, dev); assert(width == 2U && timeout > 0U && timeout <= 100U);
    ++reads; if (read_error) return HAL_ERROR;
    /* At 100kHz, even payload alone needs 9 clock pulses per byte. */
    assert((uint32_t)count * 9U <= timeout * 100U);
    assert(!busy && count && (uint32_t)addr + count <= 65536U);
    memcpy(data, memory + addr, count); return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *bus, uint16_t dev, uint16_t addr,
                                  uint16_t width, uint8_t *data, uint16_t count, uint32_t timeout)
{
    check_bus(bus, dev); assert(width == 2U && timeout > 0U && timeout <= 100U);
    ++writes; if (write_error) return HAL_ERROR;
    assert(!busy && count && (addr % 128U) + count <= 128U);
    memcpy(memory + addr, data, count); busy = busy_cycles; return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *bus, uint16_t dev, uint32_t trials, uint32_t timeout)
{
    check_bus(bus, dev); assert(trials == 1U && timeout > 0U && timeout <= 10U);
    ++polls; assert(polls <= 1000U);
    if (force_busy || busy) { if (busy) --busy; return HAL_ERROR; }
    return HAL_OK;
}
int xTaskGetSchedulerState(void) { return suspend_depth ? taskSCHEDULER_SUSPENDED : scheduler; }
void vTaskSuspendAll(void)
{ assert(scheduler == taskSCHEDULER_RUNNING && suspend_depth == 0U); ++suspend_depth; ++suspends; }
BaseType_t xTaskResumeAll(void)
{ assert(suspend_depth == 1U); --suspend_depth; ++resumes; return 1; }
void vTaskDelay(TickType_t ticks) { assert(ticks > 0U && suspend_depth == 0U); ++delays; }
void HAL_Delay(uint32_t ms) { assert(ms > 0U && suspend_depth == 0U); ++boot_delays; }
static void reset(void)
{
    memset(memory, 0xA5, sizeof memory); reads = writes = polls = delays = boot_delays = 0U;
    busy = force_busy = read_error = write_error = 0U; scheduler = taskSCHEDULER_RUNNING;
    busy_cycles = 2U;
    assert(suspend_depth == 0U); suspends = resumes = 0U;
}
int main(void)
{
    uint8_t data[260], output[260]; memset(data, 0x3C, sizeof data);
    reset();
    assert(bl24c512_write(127U, data, sizeof data));
    assert(writes == 4U && busy == 0U && delays >= 8U);
    assert(memory[126] == 0xA5U && memory[387] == 0xA5U);
    assert(bl24c512_read(127U, output, sizeof output));
    assert(memcmp(output, data, sizeof data) == 0);
    assert(bl24c512_write(65535U, data, 1U));
    assert(memory[65535] == 0x3CU && busy == 0U);
    assert(bl24c512_read(0U, full_read, sizeof full_read));
    assert(full_read[0] == 0xA5U && full_read[65535] == 0x3CU);
    assert(suspends == reads + writes + polls && resumes == suspends && suspend_depth == 0U);

    /* Reads also release the scheduler and delay between 128-byte chunks. */
    reset(); assert(bl24c512_read(0U, output, sizeof output));
    assert(reads == 3U && delays >= 2U && suspends == 3U && resumes == 3U);
    /* Fast ACKs must not turn a multi-page write into an unbroken CPU loop. */
    reset(); busy_cycles = 0U;
    assert(bl24c512_write(0U, data, sizeof data));
    assert(writes == 3U && delays >= 2U && suspends == 6U && resumes == 6U);

    reset();
    assert(!bl24c512_write(65535U, data, 2U));
    assert(!bl24c512_read(0x10000U, output, 1U));
    assert(!bl24c512_read(UINT32_MAX, output, 2U));
    assert(!bl24c512_write(2U, data, SIZE_MAX));
    assert(!bl24c512_write(0U, NULL, 1U));
    assert(bl24c512_write(65536U, NULL, 0U));
    assert(!bl24c512_read(65537U, NULL, 0U));
    assert(writes == 0U && reads == 0U && polls == 0U);

    /* Even the final page must finish, and a permanently absent part is bounded. */
    reset(); force_busy = 1U;
    assert(!bl24c512_write(0U, data, 1U));
    assert(writes == 1U && polls >= 2U && polls <= 32U && delays > 0U);
    assert(suspends == writes + polls && resumes == suspends);
    reset(); write_error = 1U; assert(!bl24c512_write(0U, data, 1U));
    assert(writes == 1U && polls == 0U);
    assert(suspends == 1U && resumes == 1U);
    reset(); read_error = 1U; assert(!bl24c512_read(0U, output, 1U));
    assert(suspends == 1U && resumes == 1U);
    reset(); scheduler = taskSCHEDULER_NOT_STARTED;
    assert(bl24c512_write(0U, data, 1U)); assert(boot_delays > 0U && delays == 0U);
    assert(suspends == 0U && resumes == 0U);
    reset(); scheduler = taskSCHEDULER_SUSPENDED;
    assert(!bl24c512_read(0U, output, 1U));
    assert(!bl24c512_write(0U, data, 1U));
    assert(reads == 0U && writes == 0U && suspends == 0U);
    puts("alarm hardware EEPROM: pass");
    return 0;
}
