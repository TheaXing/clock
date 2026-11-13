#include "bl24c512.h"
#include "i2c.h"
#include "FreeRTOS.h"
#include "task.h"

#define EEPROM_ADDRESS ((uint16_t)(0x50U << 1))
#define EEPROM_TRANSFER_TIMEOUT_MS 25U
#define EEPROM_READY_TIMEOUT_MS 2U
#define EEPROM_READY_ATTEMPTS 20U

static bool valid_range(uint32_t address, const void *data, size_t length)
{
    /* Subtraction after checking address avoids both addition wrap and casts
     * truncating an out-of-range request into the chip's 16-bit address space. */
    return address <= BL24C512_CAPACITY &&
           length <= BL24C512_CAPACITY - address && (length == 0U || data != NULL);
}

static bool protect_transfer(void)
{
    bool running = xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
    if (running)
    {
        /* HAL's polling timeout covers the complete transfer. A UI task
         * preempting this low-priority worker can otherwise consume that
         * budget between bytes. Interrupts (including HAL TIM7) stay enabled. */
        vTaskSuspendAll();
    }
    return running;
}

static void finish_transfer(bool protected)
{
    if (protected)
    {
        /* Resume immediately after ONE HAL call, on both success and error. */
        (void)xTaskResumeAll();
    }
}

static void poll_delay(void)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        TickType_t ticks = pdMS_TO_TICKS(1U);
        vTaskDelay(ticks > 0U ? ticks : 1U);
    }
    else
    {
        HAL_Delay(1U);
    }
}

static bool wait_for_write(void)
{
    for (unsigned attempt = 0U; attempt < EEPROM_READY_ATTEMPTS; ++attempt)
    {
        bool protected = protect_transfer();
        HAL_StatusTypeDef status = HAL_I2C_IsDeviceReady(&hi2c1, EEPROM_ADDRESS, 1U,
                                                        EEPROM_READY_TIMEOUT_MS);
        finish_transfer(protected);
        if (status == HAL_OK)
        {
            return true;
        }
        if (attempt + 1U < EEPROM_READY_ATTEMPTS)
        {
            /* Yield between ACK probes, including when the device is absent. */
            poll_delay();
        }
    }
    return false;
}

bool bl24c512_read(uint32_t address, void *data, size_t length)
{
    uint8_t *bytes = data;
    if (!valid_range(address, data, length) ||
        xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED)
    {
        return false;
    }
    while (length > 0U)
    {
        /* At 100 kHz a 128-byte transfer fits the 25 ms HAL timeout. A
         * 65535-byte transfer would take seconds despite HAL's 16-bit Size. */
        uint16_t count = length > BL24C512_PAGE_SIZE ? BL24C512_PAGE_SIZE : (uint16_t)length;
        bool protected = protect_transfer();
        HAL_StatusTypeDef status = HAL_I2C_Mem_Read(&hi2c1, EEPROM_ADDRESS, (uint16_t)address,
                                                   I2C_MEMADD_SIZE_16BIT, bytes, count,
                                                   EEPROM_TRANSFER_TIMEOUT_MS);
        finish_transfer(protected);
        if (status != HAL_OK)
        {
            return false;
        }
        address += count;
        bytes += count;
        length -= count;
        if (length > 0U)
        {
            poll_delay();
        }
    }
    return true;
}

bool bl24c512_write(uint32_t address, const void *data, size_t length)
{
    const uint8_t *bytes = data;
    if (!valid_range(address, data, length) ||
        xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED)
    {
        return false;
    }
    while (length > 0U)
    {
        size_t page_remaining = BL24C512_PAGE_SIZE - (address % BL24C512_PAGE_SIZE);
        uint16_t count = (uint16_t)(length < page_remaining ? length : page_remaining);
        bool protected = protect_transfer();
        HAL_StatusTypeDef status = HAL_I2C_Mem_Write(&hi2c1, EEPROM_ADDRESS, (uint16_t)address,
                                                    I2C_MEMADD_SIZE_16BIT, (uint8_t *)bytes, count,
                                                    EEPROM_TRANSFER_TIMEOUT_MS);
        finish_transfer(protected);
        if (status != HAL_OK || !wait_for_write())
        {
            return false;
        }
        address += count;
        bytes += count;
        length -= count;
        if (length > 0U)
        {
            poll_delay();
        }
    }
    return true;
}
