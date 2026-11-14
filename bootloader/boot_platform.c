#include "boot_platform.h"
#include "stm32f4xx_hal.h"
#include <string.h>

static UART_HandleTypeDef debug_uart;
static UART_HandleTypeDef esp_uart;
static I2C_HandleTypeDef eeprom_bus;

/** 功能：递增裸机毫秒时基；参数：无；返回：无。 */
void SysTick_Handler(void) {
    HAL_IncTick();
}

/** 功能：初始化已有串口、I2C和KEY1；参数：无；返回：就绪为真。 */
bool boot_platform_init(void) {
    HAL_Init();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3;
    gpio.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &gpio);
    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);
    debug_uart.Instance = USART1;
    debug_uart.Init.BaudRate = 115200U;
    debug_uart.Init.WordLength = UART_WORDLENGTH_8B;
    debug_uart.Init.StopBits = UART_STOPBITS_1;
    debug_uart.Init.Parity = UART_PARITY_NONE;
    debug_uart.Init.Mode = UART_MODE_TX_RX;
    debug_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    debug_uart.Init.OverSampling = UART_OVERSAMPLING_16;
    esp_uart = debug_uart;
    esp_uart.Instance = USART2;
    eeprom_bus.Instance = I2C1;
    eeprom_bus.Init.ClockSpeed = 100000U;
    eeprom_bus.Init.DutyCycle = I2C_DUTYCYCLE_2;
    eeprom_bus.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    eeprom_bus.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    eeprom_bus.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    eeprom_bus.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    return HAL_UART_Init(&debug_uart) == HAL_OK && HAL_UART_Init(&esp_uart) == HAL_OK &&
           HAL_I2C_Init(&eeprom_bus) == HAL_OK;
}

/** 功能：输出有界状态消息；参数：普通字符串；返回：无。 */
void boot_platform_log(const char *message) {
    (void)HAL_UART_Transmit(&debug_uart, (uint8_t *)message, (uint16_t)strlen(message), 200U);
}

/** 功能：检测KEY1连续按住两秒；参数：无；返回：强制恢复为真。 */
bool boot_platform_key(void) {
    uint32_t start = HAL_GetTick();
    while (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET) {
        if ((uint32_t)(HAL_GetTick() - start) >= 2000U) {
            return true;
        }
        HAL_Delay(10U);
    }
    return false;
}

/** 功能：读取裸机时间；参数：无；返回：毫秒。 */
uint32_t boot_platform_now(void) {
    return HAL_GetTick();
}

/** 功能：有界等待；参数：毫秒；返回：无。 */
void boot_platform_delay(uint32_t ms) {
    HAL_Delay(ms);
}

/** 功能：发送完整UART数据；参数：未用上下文、字节及长度；返回：成功为真。 */
static bool net_send(void *context, const void *data, size_t length) {
    (void)context;
    if (length > UINT16_MAX) {
        return false;
    }
    return HAL_UART_Transmit(&esp_uart, (uint8_t *)data, (uint16_t)length, 2000U) == HAL_OK;
}

/** 功能：读取UART单字节并清理硬件错误；参数：未用上下文、输出和超时；返回：成功为真。 */
static bool net_receive(void *context, uint8_t *out, uint32_t timeout) {
    (void)context;
    HAL_StatusTypeDef result = HAL_UART_Receive(&esp_uart, out, 1U, timeout);
    if (result != HAL_OK && result != HAL_TIMEOUT) {
        __HAL_UART_CLEAR_OREFLAG(&esp_uart);
        esp_uart.RxState = HAL_UART_STATE_READY;
    }
    return result == HAL_OK;
}

/** 功能：提供可替换时间回调；参数：未用上下文；返回：毫秒。 */
static uint32_t net_now(void *context) {
    (void)context;
    return HAL_GetTick();
}

/** 功能：提供可替换延迟回调；参数：未用上下文和毫秒；返回：无。 */
static void net_delay(void *context, uint32_t ms) {
    (void)context;
    HAL_Delay(ms);
}

/** 功能：返回网络硬件适配器；参数：无；返回：回调集合。 */
boot_net_io_t boot_platform_net_io(void) {
    boot_net_io_t io = {net_send, net_receive, net_now, net_delay, NULL};
    return io;
}

/** 功能：从EEPROM按页读取；参数：未用上下文、地址和缓冲；返回：成功为真。 */
static bool eeprom_read(void *context, uint32_t address, void *data, size_t length) {
    (void)context;
    uint8_t *bytes = data;
    if (address > 65536U || length > 65536U - address) {
        return false;
    }
    while (length != 0U) {
        uint16_t n = (uint16_t)(length > 128U ? 128U : length);
        if (HAL_I2C_Mem_Read(&eeprom_bus, 0xA0U, (uint16_t)address,
                             I2C_MEMADD_SIZE_16BIT, bytes, n, 100U) != HAL_OK) {
            return false;
        }
        bytes += n;
        address += n;
        length -= n;
    }
    return true;
}

/** 功能：按页边界写EEPROM并等待ACK；参数：未用上下文、地址和数据；返回：成功为真。 */
static bool eeprom_write(void *context, uint32_t address, const void *data, size_t length) {
    (void)context;
    const uint8_t *bytes = data;
    if (address < 0x0800U || address > 0x1000U || length > 0x1000U - address) {
        return false;
    }
    while (length != 0U) {
        size_t page = 128U - address % 128U;
        uint16_t n = (uint16_t)(length < page ? length : page);
        if (HAL_I2C_Mem_Write(&eeprom_bus, 0xA0U, (uint16_t)address,
                              I2C_MEMADD_SIZE_16BIT, (uint8_t *)bytes, n, 100U) != HAL_OK) {
            return false;
        }
        uint32_t start = HAL_GetTick();
        while (HAL_I2C_IsDeviceReady(&eeprom_bus, 0xA0U, 1U, 2U) != HAL_OK) {
            if ((uint32_t)(HAL_GetTick() - start) > 40U) {
                return false;
            }
            HAL_Delay(1U);
        }
        bytes += n;
        address += n;
        length -= n;
    }
    return true;
}

/** 功能：返回Boot独占的日志适配器；参数：无；返回：EEPROM回调。 */
ota_journal_io_t boot_platform_journal_io(void) {
    ota_journal_io_t io = {eeprom_read, eeprom_write, NULL};
    return io;
}

/** 功能：在RAM中等待硬件Flash完成；参数：无；返回：无，硬超时直接复位。 */
__attribute__((section(".RamFunc"), noinline))
static void flash_wait(void) {
    uint32_t start = DWT->CYCCNT;
    while ((FLASH->SR & FLASH_SR_BSY) != 0U) {
        if ((uint32_t)(DWT->CYCCNT - start) > 80000000U) {
            SCB->AIRCR = 0x05FA0004U;
            __DSB();
            for (;;) {
                __NOP();
            }
        }
    }
}

/** 功能：在RAM中完成单次擦写，等待不依赖中断；参数：扇区或地址/字值；返回：成功为真。 */
__attribute__((section(".RamFunc"), noinline))
static bool flash_operation(unsigned sector, uint32_t address, uint32_t word, bool erase) {
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    flash_wait();
    if ((FLASH->CR & FLASH_CR_LOCK) != 0U) {
        FLASH->KEYR = 0x45670123U;
        FLASH->KEYR = 0xCDEF89ABU;
    }
    FLASH->SR = FLASH_SR_EOP | FLASH_SR_OPERR | FLASH_SR_WRPERR |
                FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR;
    uint32_t control = FLASH->CR & ~(FLASH_CR_PG | FLASH_CR_SER | FLASH_CR_SNB | FLASH_CR_PSIZE);
    control |= FLASH_CR_PSIZE_1;
    if (erase) {
        FLASH->CR = control | FLASH_CR_SER | ((uint32_t)sector << FLASH_CR_SNB_Pos);
        FLASH->CR |= FLASH_CR_STRT;
    } else {
        FLASH->CR = control | FLASH_CR_PG;
        *(volatile uint32_t *)(uintptr_t)address = word;
    }
    flash_wait();
    bool good = (FLASH->SR & (FLASH_SR_OPERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR |
                             FLASH_SR_PGPERR | FLASH_SR_PGSERR)) == 0U;
    FLASH->CR &= ~(FLASH_CR_SER | FLASH_CR_PG | FLASH_CR_SNB);
    FLASH->CR |= FLASH_CR_LOCK;
    __set_PRIMASK(mask);
    return good;
}

/** 功能：清除Flash缓存，保证回读的是新字节；参数：无；返回：无。 */
static void flush_cache(void) {
    uint32_t saved = FLASH->ACR;
    FLASH->ACR &= ~(FLASH_ACR_ICEN | FLASH_ACR_DCEN);
    FLASH->ACR |= FLASH_ACR_ICRST | FLASH_ACR_DCRST;
    FLASH->ACR &= ~(FLASH_ACR_ICRST | FLASH_ACR_DCRST);
    FLASH->ACR = saved;
    __DSB();
    __ISB();
}

/** 功能：仅允许擦除App扇区；参数：未用上下文和扇区号；返回：成功为真。 */
bool boot_platform_erase(void *context, unsigned sector) {
    (void)context;
    if (sector < 4U || sector > 7U) {
        return false;
    }
    bool good = flash_operation(sector, 0U, 0U, true);
    flush_cache();
    return good;
}

/** 功能：仅允许向App范围按字编程并补齐尾部；参数：地址、数据和实际长度；返回：成功为真。 */
bool boot_platform_program(void *context, uint32_t address, const void *data, size_t length) {
    (void)context;
    const uint8_t *bytes = data;
    if (data == NULL || (address & 3U) != 0U || address < OTA_APP_BASE ||
        address > 0x08080000U || length > 0x08080000U - address) {
        return false;
    }
    for (size_t i = 0U; i < length; i += 4U) {
        uint32_t word = UINT32_MAX;
        size_t n = length - i < 4U ? length - i : 4U;
        memcpy(&word, bytes + i, n);
        if (!flash_operation(0U, address + (uint32_t)i, word, false)) {
            flush_cache();
            return false;
        }
    }
    flush_cache();
    return true;
}

/** 功能：从准确VE地址范围回读Flash；参数：地址、输出和长度；返回：成功为真。 */
bool boot_platform_read(void *context, uint32_t address, void *data, size_t length) {
    (void)context;
    if (data == NULL || address < OTA_APP_BASE || address > 0x08080000U ||
        length > 0x08080000U - address) {
        return false;
    }
    flush_cache();
    memcpy(data, (const void *)(uintptr_t)address, length);
    return true;
}

/** 功能：无C栈访问地切换到应用；参数：MSP与入口；返回：不返回。 */
__attribute__((naked, noreturn))
static void branch_app(uint32_t stack __attribute__((unused)), uint32_t entry __attribute__((unused))) {
    __asm volatile("msr msp, r0\n"
                   "movs r2, #0\n"
                   "msr control, r2\n"
                   "msr basepri, r2\n"
                   "msr faultmask, r2\n"
                   "isb\n"
                   "cpsie i\n"
                   "bx r1\n");
}

/** 功能：关闭Boot外设后安全交接，调用者须已验证镜像；参数：无；返回：不返回。 */
void boot_platform_jump(void) {
    (void)HAL_UART_DeInit(&esp_uart);
    (void)HAL_UART_DeInit(&debug_uart);
    (void)HAL_I2C_DeInit(&eeprom_bus);
    __disable_irq();
    SysTick->CTRL = 0U;
    SysTick->VAL = 0U;
    for (unsigned i = 0U; i < 8U; ++i) {
        NVIC->ICER[i] = UINT32_MAX;
        NVIC->ICPR[i] = UINT32_MAX;
    }
    SCB->ICSR = SCB_ICSR_PENDSVCLR_Msk | SCB_ICSR_PENDSTCLR_Msk;
    SCB->VTOR = OTA_APP_BASE;
    flush_cache();
    __DSB();
    __ISB();
    const uint32_t *vector = (const uint32_t *)(uintptr_t)OTA_APP_BASE;
    branch_app(vector[0], vector[1]);
}
