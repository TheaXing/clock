#include "bl24c512.h"
#include "i2c.h"
#include "FreeRTOS.h"
#include "task.h"
#if defined(CLOCK_OTA_ENABLED) && CLOCK_OTA_ENABLED
#include "semphr.h"
static SemaphoreHandle_t transaction_mutex;
#endif

/**
 * 功能：在调度器启动前创建整颗 EEPROM 的事务互斥锁。
 * 参数：无。
 * 返回：锁可用返回 true，创建失败或调用时机错误返回 false。
 */
bool bl24c512_transaction_init(void) {
#if defined(CLOCK_OTA_ENABLED) && CLOCK_OTA_ENABLED
    if (transaction_mutex != NULL) {
        return true;
    }
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        return false;
    }
    transaction_mutex = xSemaphoreCreateMutex();
    return transaction_mutex != NULL;
#else
    return true;
#endif
}

/**
 * 功能：取得覆盖扫描、写入及复查的 EEPROM 事务锁，拒绝中断和暂停调度上下文。
 * 参数：无。
 * 返回：可安全执行完整事务返回 true，否则返回 false。
 */
bool bl24c512_transaction_begin(void) {
#if defined(CLOCK_OTA_ENABLED) && CLOCK_OTA_ENABLED
    if (transaction_mutex == NULL || __get_IPSR() != 0U ||
        xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED) {
        return false;
    }
    return xSemaphoreTake(transaction_mutex, portMAX_DELAY) == pdTRUE;
#else
    return xTaskGetSchedulerState() != taskSCHEDULER_SUSPENDED;
#endif
}

/**
 * 功能：释放已取得的完整 EEPROM 事务锁。
 * 参数：无。
 * 返回：无。
 */
void bl24c512_transaction_end(void) {
#if defined(CLOCK_OTA_ENABLED) && CLOCK_OTA_ENABLED
    (void)xSemaphoreGive(transaction_mutex);
#endif
}

#define EEPROM_ADDRESS ((uint16_t)(0x50U << 1))
#define EEPROM_TRANSFER_TIMEOUT_MS 25U
#define EEPROM_READY_TIMEOUT_MS 2U
#define EEPROM_READY_ATTEMPTS 20U

/**
 * 功能：检查 EEPROM 请求范围与缓冲区有效性，避免地址溢出。
 * 参数：address，EEPROM 起始字节地址；data，传输数据缓冲区；length，传输或处理的字节数。
 * 返回：请求位于容量内且缓冲区有效返回 true，否则返回 false。
 */
static bool valid_range(uint32_t address, const void *data, size_t length) {
    /* 先检查地址再做减法，避免加法溢出，也避免类型转换把越界请求截断到芯片的 16 位地址空间。 */
    return address <= BL24C512_CAPACITY && length <= BL24C512_CAPACITY - address &&
           (length == 0U || data != NULL);
}

/**
 * 功能：调度器运行时暂停任务调度，保护单次 EEPROM 轮询传输。
 * 参数：无。
 * 返回：本次暂停了调度返回 true，否则返回 false。
 */
static bool protect_transfer(void) {
    bool running = xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
    if (running) {
        /* HAL 的轮询超时覆盖整次传输；界面任务抢占低优先级工作任务时，可能在字节间耗尽超时预算。此处仅暂停调度，保留包括 HAL TIM7 在内的中断。 */
        vTaskSuspendAll();
    }
    return running;
}

/**
 * 功能：在受保护的 EEPROM 传输结束后恢复任务调度。
 * 参数：protected，本次传输是否暂停了任务调度。
 * 返回：无。
 */
static void finish_transfer(bool protected) {
    if (protected) {
        /* 每次 HAL 调用结束后立即恢复调度，成功和失败路径均如此。 */
        (void)xTaskResumeAll();
    }
}

/**
 * 功能：在 EEPROM 分块或应答探测之间让出处理器。
 * 参数：无。
 * 返回：无。
 */
static void poll_delay(void) {
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        TickType_t ticks = pdMS_TO_TICKS(1U);
        vTaskDelay(ticks > 0U ? ticks : 1U);
    } else {
        HAL_Delay(1U);
    }
}

/**
 * 功能：有限次探测 EEPROM 写周期结束状态。
 * 参数：无。
 * 返回：设备恢复应答返回 true，探测耗尽返回 false。
 */
static bool wait_for_write(void) {
    for (unsigned attempt = 0U; attempt < EEPROM_READY_ATTEMPTS; ++attempt) {
        bool protected = protect_transfer();
        HAL_StatusTypeDef status =
            HAL_I2C_IsDeviceReady(&hi2c1, EEPROM_ADDRESS, 1U, EEPROM_READY_TIMEOUT_MS);
        finish_transfer(protected);
        if (status == HAL_OK) {
            return true;
        }
        if (attempt + 1U < EEPROM_READY_ATTEMPTS) {
            /* 在应答探测之间让出处理器，设备不存在时同样如此。 */
            poll_delay();
        }
    }
    return false;
}

/**
 * 功能：校验请求后分块读取 EEPROM，并在块间让出处理器。
 * 参数：address，EEPROM 起始字节地址；data，传输数据缓冲区；length，传输或处理的字节数。
 * 返回：全部读取成功或合法零长度请求返回 true，否则返回 false。
 */
bool bl24c512_read(uint32_t address, void *data, size_t length) {
    uint8_t *bytes = data;
    if (!valid_range(address, data, length) ||
        xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED) {
        return false;
    }
    while (length > 0U) {
        /* 在 100 kHz 总线下，128 字节传输可在 HAL 的 25 毫秒超时内完成；即使 HAL 长度为 16 位，65535 字节传输仍需数秒。 */
        uint16_t count = length > BL24C512_PAGE_SIZE ? BL24C512_PAGE_SIZE : (uint16_t)length;
        bool protected = protect_transfer();
        HAL_StatusTypeDef status =
            HAL_I2C_Mem_Read(&hi2c1, EEPROM_ADDRESS, (uint16_t)address, I2C_MEMADD_SIZE_16BIT,
                             bytes, count, EEPROM_TRANSFER_TIMEOUT_MS);
        finish_transfer(protected);
        if (status != HAL_OK) {
            return false;
        }
        address += count;
        bytes += count;
        length -= count;
        if (length > 0U) {
            poll_delay();
        }
    }
    return true;
}

/**
 * 功能：按 EEPROM 页边界写入，并等待每页写周期结束。
 * 参数：address，EEPROM 起始字节地址；data，传输数据缓冲区；length，传输或处理的字节数。
 * 返回：全部写入成功或合法零长度请求返回 true，否则返回 false。
 */
bool bl24c512_write(uint32_t address, const void *data, size_t length) {
    const uint8_t *bytes = data;
    if (!valid_range(address, data, length) ||
        xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED) {
        return false;
    }
    while (length > 0U) {
        size_t page_remaining = BL24C512_PAGE_SIZE - (address % BL24C512_PAGE_SIZE);
        uint16_t count = (uint16_t)(length < page_remaining ? length : page_remaining);
        bool protected = protect_transfer();
        HAL_StatusTypeDef status =
            HAL_I2C_Mem_Write(&hi2c1, EEPROM_ADDRESS, (uint16_t)address, I2C_MEMADD_SIZE_16BIT,
                              (uint8_t *)bytes, count, EEPROM_TRANSFER_TIMEOUT_MS);
        finish_transfer(protected);
        if (status != HAL_OK || !wait_for_write()) {
            return false;
        }
        address += count;
        bytes += count;
        length -= count;
        if (length > 0U) {
            poll_delay();
        }
    }
    return true;
}
