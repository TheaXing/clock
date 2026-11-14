/**
 * @file system_stm32f4xx.c
 * @author MCD 应用团队
 * @brief CMSIS Cortex-M4 系统源文件，提供启动初始化和内核时钟维护。
 * SystemInit() 由 startup_stm32f4xx.s 在复位后、进入 main 前调用。
 * SystemCoreClock 保存 HCLK 频率，供 SysTick 等外设参数配置使用。
 * SystemCoreClockUpdate() 在运行期间内核时钟变化后更新该变量。
 ******************************************************************************
  * @attention
  *
  * Copyright (c) 2017 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/** @addtogroup CMSIS
  * @{
  */

/** @addtogroup stm32f4xx_system
  * @{
  */

/** @addtogroup STM32F4xx_System_Private_Includes
  * @{
  */

#include "stm32f4xx.h"

#if !defined(HSE_VALUE)
#define HSE_VALUE    ((uint32_t)25000000) /*!< 外部振荡器默认频率，单位为 Hz。 */
#endif /* 条件结束：HSE_VALUE */

#if !defined(HSI_VALUE)
#define HSI_VALUE    ((uint32_t)16000000) /*!< 内部振荡器频率，单位为 Hz。 */
#endif /* 条件结束：HSI_VALUE */

/** @addtogroup STM32F4xx_System_Private_TypesDefinitions
  * @{
  */

/** @addtogroup STM32F4xx_System_Private_Defines
  * @{
  */

/** 其他配置 */
/*!< 如需使用外部 SRAM 或 SDRAM 作为数据存储器，取消下列宏定义的注释。 */
#if defined(STM32F405xx) || defined(STM32F415xx) || defined(STM32F407xx) ||                        \
    defined(STM32F417xx) || defined(STM32F427xx) || defined(STM32F437xx) ||                        \
    defined(STM32F429xx) || defined(STM32F439xx) || defined(STM32F469xx) ||                        \
    defined(STM32F479xx) || defined(STM32F412Zx) || defined(STM32F412Vx)
/* #define DATA_IN_ExtSRAM // 取消本行块注释以启用。 */
#endif /* 条件结束：STM32F40xxx || STM32F41xxx || STM32F42xxx || STM32F43xxx || STM32F469xx || STM32F479xx ||           STM32F412Zx || STM32F412Vx */

#if defined(STM32F427xx) || defined(STM32F437xx) || defined(STM32F429xx) ||                        \
    defined(STM32F439xx) || defined(STM32F446xx) || defined(STM32F469xx) || defined(STM32F479xx)
/* #define DATA_IN_ExtSDRAM // 取消本行块注释以启用。 */
#endif /* 条件结束：STM32F427xx || STM32F437xx || STM32F429xx || STM32F439xx || STM32F446xx || STM32F469xx ||           STM32F479xx */

/* 注意：以下向量表地址必须与链接器配置一致。 */
/*!< 如需将向量表重定位到 Flash 或 SRAM，取消下列宏定义的注释；否则保留启动地址的自动重映射位置。 */
/* #define USER_VECT_TAB_ADDRESS // 取消本行块注释以启用。 */

#if defined(USER_VECT_TAB_ADDRESS)
/*!< 如需将向量表重定位到 SRAM，取消下列宏定义的注释；否则在 Flash 中重映射。 */
/* #define VECT_TAB_SRAM // 取消本行块注释以启用。 */
#if defined(VECT_TAB_SRAM)
#define VECT_TAB_BASE_ADDRESS   SRAM_BASE       /*!< 向量表基地址，必须是 0x200 的整数倍。 */
#else
#define VECT_TAB_BASE_ADDRESS   FLASH_BASE      /*!< 向量表基地址，必须是 0x200 的整数倍。 */
#endif /* 条件结束：VECT_TAB_SRAM */
#if !defined(VECT_TAB_OFFSET)
#define VECT_TAB_OFFSET         0x00000000U     /*!< 向量表偏移量，必须是 0x200 的整数倍。 */
#endif /* 条件结束：VECT_TAB_OFFSET */
#endif /* 条件结束：USER_VECT_TAB_ADDRESS */

/** @addtogroup STM32F4xx_System_Private_Macros
  * @{
  */

/** @addtogroup STM32F4xx_System_Private_Variables
  * @{
  */
/* SystemCoreClock 可通过三种方式更新：调用 CMSIS 的 SystemCoreClockUpdate()、调用 HAL_RCC_GetHCLKFreq()，或调用 HAL_RCC_ClockConfig() 配置系统时钟。最后一种方式会自动更新该变量，无需再调用前两种函数。 */
uint32_t SystemCoreClock = 16000000;
const uint8_t AHBPrescTable[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 6, 7, 8, 9};
const uint8_t APBPrescTable[8] = {0, 0, 0, 0, 1, 2, 3, 4};

/** @addtogroup STM32F4xx_System_Private_FunctionPrototypes
  * @{
  */

#if defined(DATA_IN_ExtSRAM) || defined(DATA_IN_ExtSDRAM)
static void SystemInit_ExtMemCtl(void);
#endif /* 条件结束：DATA_IN_ExtSRAM || DATA_IN_ExtSDRAM */

/** @addtogroup STM32F4xx_System_Private_Functions
  * @{
  */

/**
 * @brief 功能：在复位后、进入 main 前设置 FPU、向量表位置及可选外部存储器。
 * 参数：无。
 * @retval 返回：无
 */
void SystemInit(void) {
/* 配置 FPU */
#if (__FPU_PRESENT == 1) && (__FPU_USED == 1)
    SCB->CPACR |= ((3UL << 10 * 2) | (3UL << 11 * 2)); /* 允许完全访问 CP10 和 CP11。 */
#endif

#if defined(DATA_IN_ExtSRAM) || defined(DATA_IN_ExtSDRAM)
    SystemInit_ExtMemCtl();
#endif /* 条件结束：DATA_IN_ExtSRAM || DATA_IN_ExtSDRAM */

    /* 配置向量表位置 */
#if defined(USER_VECT_TAB_ADDRESS)
    SCB->VTOR = VECT_TAB_BASE_ADDRESS | VECT_TAB_OFFSET; /* 按配置重定位向量表。 */
#endif                                                   /* 条件结束：USER_VECT_TAB_ADDRESS */
}

/**
 * @brief 功能：根据时钟寄存器和预定义振荡器频率更新内核时钟 SystemCoreClock。
 * 参数：无。
 * @retval 返回：无
 * @note 每次 HCLK 改变后必须调用本函数；基于旧时钟值配置的 SysTick 等参数将不准确。
 * @note 此结果由预定义常量和时钟源计算，并非芯片实际频率的测量值。HSI 源使用 HSI_VALUE，HSE 源使用 HSE_VALUE，PLL 源按其倍频与分频系数计算。
 * @note HSI_VALUE 在 stm32f4xx_hal_conf.h 中默认是 16 MHz，实际值随电压和温度变化。HSE_VALUE 必须与实际晶振频率一致；采用非整数频率晶振时，整数运算结果可能不准确。
 */
void SystemCoreClockUpdate(void) {
    uint32_t tmp, pllvco, pllp, pllsource, pllm;

    /* 读取 SYSCLK 时钟源 */
    tmp = RCC->CFGR & RCC_CFGR_SWS;

    switch (tmp) {
    case 0x00: /* 使用 HSI 作为系统时钟源 */
        SystemCoreClock = HSI_VALUE;
        break;
    case 0x04: /* 使用 HSE 作为系统时钟源 */
        SystemCoreClock = HSE_VALUE;
        break;
    case 0x08: /* 使用 PLL 作为系统时钟源 */

        /* 按所选 HSE_VALUE 或 HSI_VALUE 计算 PLL_VCO = 输入频率 / PLL_M * PLL_N，再计算 SYSCLK = PLL_VCO / PLL_P。 */
        pllsource = (RCC->PLLCFGR & RCC_PLLCFGR_PLLSRC) >> 22;
        pllm = RCC->PLLCFGR & RCC_PLLCFGR_PLLM;

        if (pllsource != 0) {
            /* 使用 HSE 作为 PLL 时钟源 */
            pllvco = (HSE_VALUE / pllm) * ((RCC->PLLCFGR & RCC_PLLCFGR_PLLN) >> 6);
        } else {
            /* 使用 HSI 作为 PLL 时钟源 */
            pllvco = (HSI_VALUE / pllm) * ((RCC->PLLCFGR & RCC_PLLCFGR_PLLN) >> 6);
        }

        pllp = (((RCC->PLLCFGR & RCC_PLLCFGR_PLLP) >> 16) + 1) * 2;
        SystemCoreClock = pllvco / pllp;
        break;
    default:
        SystemCoreClock = HSI_VALUE;
        break;
    }
    /* 计算 HCLK 频率 */
    /* 读取 HCLK 分频系数 */
    tmp = AHBPrescTable[((RCC->CFGR & RCC_CFGR_HPRE) >> 4)];
    /* 更新 HCLK 频率 */
    SystemCoreClock >>= tmp;
}

#if defined(DATA_IN_ExtSRAM) && defined(DATA_IN_ExtSDRAM)
#if defined(STM32F427xx) || defined(STM32F437xx) || defined(STM32F429xx) ||                        \
    defined(STM32F439xx) || defined(STM32F469xx) || defined(STM32F479xx)
/**
 * @brief 功能：在进入 main 前初始化外部 SRAM 或 SDRAM 控制器，供程序数据、堆和栈使用。
 * 参数：无。
 * @retval 返回：无
 */
void SystemInit_ExtMemCtl(void) {
    __IO uint32_t tmp = 0x00;

    register uint32_t tmpreg = 0, timeout = 0xFFFF;
    register __IO uint32_t index;

    /* 开启 GPIOC、GPIOD、GPIOE、GPIOF、GPIOG、GPIOH、GPIOI 接口时钟。 */
    RCC->AHB1ENR |= 0x000001F8;

    /* 开启 RCC 外设时钟后读取寄存器，等待时钟生效。 */
    tmp = READ_BIT(RCC->AHB1ENR, RCC_AHB1ENR_GPIOCEN);

    /* 将 PDx 引脚连接到 FMC 复用功能。 */
    GPIOD->AFR[0] = 0x00CCC0CC;
    GPIOD->AFR[1] = 0xCCCCCCCC;
    /* 将 PDx 引脚配置为复用模式。 */
    GPIOD->MODER = 0xAAAA0A8A;
    /* 将 PDx 引脚速度配置为 100 MHz。 */
    GPIOD->OSPEEDR = 0xFFFF0FCF;
    /* 将 PDx 引脚输出类型配置为推挽。 */
    GPIOD->OTYPER = 0x00000000;
    /* PDx 引脚不启用内部上拉或下拉。 */
    GPIOD->PUPDR = 0x00000000;

    /* 将 PEx 引脚连接到 FMC 复用功能。 */
    GPIOE->AFR[0] = 0xC00CC0CC;
    GPIOE->AFR[1] = 0xCCCCCCCC;
    /* 将 PEx 引脚配置为复用模式。 */
    GPIOE->MODER = 0xAAAA828A;
    /* 将 PEx 引脚速度配置为 100 MHz。 */
    GPIOE->OSPEEDR = 0xFFFFC3CF;
    /* 将 PEx 引脚输出类型配置为推挽。 */
    GPIOE->OTYPER = 0x00000000;
    /* PEx 引脚不启用内部上拉或下拉。 */
    GPIOE->PUPDR = 0x00000000;

    /* 将 PFx 引脚连接到 FMC 复用功能。 */
    GPIOF->AFR[0] = 0xCCCCCCCC;
    GPIOF->AFR[1] = 0xCCCCCCCC;
    /* 将 PFx 引脚配置为复用模式。 */
    GPIOF->MODER = 0xAA800AAA;
    /* 将 PFx 引脚速度配置为 50 MHz。 */
    GPIOF->OSPEEDR = 0xAA800AAA;
    /* 将 PFx 引脚输出类型配置为推挽。 */
    GPIOF->OTYPER = 0x00000000;
    /* PFx 引脚不启用内部上拉或下拉。 */
    GPIOF->PUPDR = 0x00000000;

    /* 将 PGx 引脚连接到 FMC 复用功能。 */
    GPIOG->AFR[0] = 0xCCCCCCCC;
    GPIOG->AFR[1] = 0xCCCCCCCC;
    /* 将 PGx 引脚配置为复用模式。 */
    GPIOG->MODER = 0xAAAAAAAA;
    /* 将 PGx 引脚速度配置为 50 MHz。 */
    GPIOG->OSPEEDR = 0xAAAAAAAA;
    /* 将 PGx 引脚输出类型配置为推挽。 */
    GPIOG->OTYPER = 0x00000000;
    /* PGx 引脚不启用内部上拉或下拉。 */
    GPIOG->PUPDR = 0x00000000;

    /* 将 PHx 引脚连接到 FMC 复用功能。 */
    GPIOH->AFR[0] = 0x00C0CC00;
    GPIOH->AFR[1] = 0xCCCCCCCC;
    /* 将 PHx 引脚配置为复用模式。 */
    GPIOH->MODER = 0xAAAA08A0;
    /* 将 PHx 引脚速度配置为 50 MHz。 */
    GPIOH->OSPEEDR = 0xAAAA08A0;
    /* 将 PHx 引脚输出类型配置为推挽。 */
    GPIOH->OTYPER = 0x00000000;
    /* PHx 引脚不启用内部上拉或下拉。 */
    GPIOH->PUPDR = 0x00000000;

    /* 将 PIx 引脚连接到 FMC 复用功能。 */
    GPIOI->AFR[0] = 0xCCCCCCCC;
    GPIOI->AFR[1] = 0x00000CC0;
    /* 将 PIx 引脚配置为复用模式。 */
    GPIOI->MODER = 0x0028AAAA;
    /* 将 PIx 引脚速度配置为 50 MHz。 */
    GPIOI->OSPEEDR = 0x0028AAAA;
    /* 将 PIx 引脚输出类型配置为推挽。 */
    GPIOI->OTYPER = 0x00000000;
    /* PIx 引脚不启用内部上拉或下拉。 */
    GPIOI->PUPDR = 0x00000000;

    /* 配置 FMC */
    /* 开启 FMC 接口时钟 */
    RCC->AHB3ENR |= 0x00000001;
    /* 开启 RCC 外设时钟后读取寄存器，等待时钟生效。 */
    tmp = READ_BIT(RCC->AHB3ENR, RCC_AHB3ENR_FMCEN);

    FMC_Bank5_6->SDCR[0] = 0x000019E4;
    FMC_Bank5_6->SDTR[0] = 0x01115351;

    /* SDRAM 初始化时序 */
    /* 发送时钟使能命令 */
    FMC_Bank5_6->SDCMR = 0x00000011;
    tmpreg = FMC_Bank5_6->SDSR & 0x00000020;
    while ((tmpreg != 0) && (timeout-- > 0)) {
        tmpreg = FMC_Bank5_6->SDSR & 0x00000020;
    }

    /* 延时 */
    for (index = 0; index < 1000; index++) {
        ;
    }

    /* 发送全部存储体预充电命令 */
    FMC_Bank5_6->SDCMR = 0x00000012;
    tmpreg = FMC_Bank5_6->SDSR & 0x00000020;
    timeout = 0xFFFF;
    while ((tmpreg != 0) && (timeout-- > 0)) {
        tmpreg = FMC_Bank5_6->SDSR & 0x00000020;
    }

    /* 发送自动刷新命令 */
    FMC_Bank5_6->SDCMR = 0x00000073;
    tmpreg = FMC_Bank5_6->SDSR & 0x00000020;
    timeout = 0xFFFF;
    while ((tmpreg != 0) && (timeout-- > 0)) {
        tmpreg = FMC_Bank5_6->SDSR & 0x00000020;
    }

    /* 配置模式寄存器 */
    FMC_Bank5_6->SDCMR = 0x00046014;
    tmpreg = FMC_Bank5_6->SDSR & 0x00000020;
    timeout = 0xFFFF;
    while ((tmpreg != 0) && (timeout-- > 0)) {
        tmpreg = FMC_Bank5_6->SDSR & 0x00000020;
    }

    /* 设置刷新计数 */
    tmpreg = FMC_Bank5_6->SDRTR;
    FMC_Bank5_6->SDRTR = (tmpreg | (0x0000027C << 1));

    /* 关闭写保护 */
    tmpreg = FMC_Bank5_6->SDCR[0];
    FMC_Bank5_6->SDCR[0] = (tmpreg & 0xFFFFFDFF);

#if defined(STM32F427xx) || defined(STM32F437xx) || defined(STM32F429xx) || defined(STM32F439xx)
    /* 配置并启用 Bank1_SRAM2 */
    FMC_Bank1->BTCR[2] = 0x00001011;
    FMC_Bank1->BTCR[3] = 0x00000201;
    FMC_Bank1E->BWTR[2] = 0x0fffffff;
#endif /* 条件结束：STM32F427xx || STM32F437xx || STM32F429xx || STM32F439xx */
#if defined(STM32F469xx) || defined(STM32F479xx)
    /* 配置并启用 Bank1_SRAM2 */
    FMC_Bank1->BTCR[2] = 0x00001091;
    FMC_Bank1->BTCR[3] = 0x00110212;
    FMC_Bank1E->BWTR[2] = 0x0fffffff;
#endif /* 条件结束：STM32F469xx || STM32F479xx */

    (void)(tmp);
}
#endif /* 条件结束：STM32F427xx || STM32F437xx || STM32F429xx || STM32F439xx || STM32F469xx || STM32F479xx */
#elif defined(DATA_IN_ExtSRAM) || defined(DATA_IN_ExtSDRAM)
/**
 * @brief 功能：在进入 main 前初始化外部 SRAM 或 SDRAM 控制器，供程序数据、堆和栈使用。
 * 参数：无。
 * @retval 返回：无
 */
void SystemInit_ExtMemCtl(void) {
    __IO uint32_t tmp = 0x00;
#if defined(STM32F427xx) || defined(STM32F437xx) || defined(STM32F429xx) ||                        \
    defined(STM32F439xx) || defined(STM32F446xx) || defined(STM32F469xx) || defined(STM32F479xx)
#if defined(DATA_IN_ExtSDRAM)
    register uint32_t tmpreg = 0, timeout = 0xFFFF;
    register __IO uint32_t index;

#if defined(STM32F446xx)
    /* 开启 GPIOA、GPIOC、GPIOD、GPIOE、GPIOF、GPIOG 接口时钟。 */
    RCC->AHB1ENR |= 0x0000007D;
#else
    /* 开启 GPIOC、GPIOD、GPIOE、GPIOF、GPIOG、GPIOH、GPIOI 接口时钟。 */
    RCC->AHB1ENR |= 0x000001F8;
#endif /* 条件结束：STM32F446xx */
    /* 开启 RCC 外设时钟后读取寄存器，等待时钟生效。 */
    tmp = READ_BIT(RCC->AHB1ENR, RCC_AHB1ENR_GPIOCEN);

#if defined(STM32F446xx)
    /* 将 PAx 引脚连接到 FMC 复用功能。 */
    GPIOA->AFR[0] |= 0xC0000000;
    GPIOA->AFR[1] |= 0x00000000;
    /* 将 PAx 引脚配置为复用模式。 */
    GPIOA->MODER |= 0x00008000;
    /* 将 PAx 引脚速度配置为 50 MHz。 */
    GPIOA->OSPEEDR |= 0x00008000;
    /* 将 PAx 引脚输出类型配置为推挽。 */
    GPIOA->OTYPER |= 0x00000000;
    /* PAx 引脚不启用内部上拉或下拉。 */
    GPIOA->PUPDR |= 0x00000000;

    /* 将 PCx 引脚连接到 FMC 复用功能。 */
    GPIOC->AFR[0] |= 0x00CC0000;
    GPIOC->AFR[1] |= 0x00000000;
    /* 将 PCx 引脚配置为复用模式。 */
    GPIOC->MODER |= 0x00000A00;
    /* 将 PCx 引脚速度配置为 50 MHz。 */
    GPIOC->OSPEEDR |= 0x00000A00;
    /* 将 PCx 引脚输出类型配置为推挽。 */
    GPIOC->OTYPER |= 0x00000000;
    /* PCx 引脚不启用内部上拉或下拉。 */
    GPIOC->PUPDR |= 0x00000000;
#endif /* 条件结束：STM32F446xx */

    /* 将 PDx 引脚连接到 FMC 复用功能。 */
    GPIOD->AFR[0] = 0x000000CC;
    GPIOD->AFR[1] = 0xCC000CCC;
    /* 将 PDx 引脚配置为复用模式。 */
    GPIOD->MODER = 0xA02A000A;
    /* 将 PDx 引脚速度配置为 50 MHz。 */
    GPIOD->OSPEEDR = 0xA02A000A;
    /* 将 PDx 引脚输出类型配置为推挽。 */
    GPIOD->OTYPER = 0x00000000;
    /* PDx 引脚不启用内部上拉或下拉。 */
    GPIOD->PUPDR = 0x00000000;

    /* 将 PEx 引脚连接到 FMC 复用功能。 */
    GPIOE->AFR[0] = 0xC00000CC;
    GPIOE->AFR[1] = 0xCCCCCCCC;
    /* 将 PEx 引脚配置为复用模式。 */
    GPIOE->MODER = 0xAAAA800A;
    /* 将 PEx 引脚速度配置为 50 MHz。 */
    GPIOE->OSPEEDR = 0xAAAA800A;
    /* 将 PEx 引脚输出类型配置为推挽。 */
    GPIOE->OTYPER = 0x00000000;
    /* PEx 引脚不启用内部上拉或下拉。 */
    GPIOE->PUPDR = 0x00000000;

    /* 将 PFx 引脚连接到 FMC 复用功能。 */
    GPIOF->AFR[0] = 0xCCCCCCCC;
    GPIOF->AFR[1] = 0xCCCCCCCC;
    /* 将 PFx 引脚配置为复用模式。 */
    GPIOF->MODER = 0xAA800AAA;
    /* 将 PFx 引脚速度配置为 50 MHz。 */
    GPIOF->OSPEEDR = 0xAA800AAA;
    /* 将 PFx 引脚输出类型配置为推挽。 */
    GPIOF->OTYPER = 0x00000000;
    /* PFx 引脚不启用内部上拉或下拉。 */
    GPIOF->PUPDR = 0x00000000;

    /* 将 PGx 引脚连接到 FMC 复用功能。 */
    GPIOG->AFR[0] = 0xCCCCCCCC;
    GPIOG->AFR[1] = 0xCCCCCCCC;
    /* 将 PGx 引脚配置为复用模式。 */
    GPIOG->MODER = 0xAAAAAAAA;
    /* 将 PGx 引脚速度配置为 50 MHz。 */
    GPIOG->OSPEEDR = 0xAAAAAAAA;
    /* 将 PGx 引脚输出类型配置为推挽。 */
    GPIOG->OTYPER = 0x00000000;
    /* PGx 引脚不启用内部上拉或下拉。 */
    GPIOG->PUPDR = 0x00000000;

#if defined(STM32F427xx) || defined(STM32F437xx) || defined(STM32F429xx) ||                        \
    defined(STM32F439xx) || defined(STM32F469xx) || defined(STM32F479xx)
    /* 将 PHx 引脚连接到 FMC 复用功能。 */
    GPIOH->AFR[0] = 0x00C0CC00;
    GPIOH->AFR[1] = 0xCCCCCCCC;
    /* 将 PHx 引脚配置为复用模式。 */
    GPIOH->MODER = 0xAAAA08A0;
    /* 将 PHx 引脚速度配置为 50 MHz。 */
    GPIOH->OSPEEDR = 0xAAAA08A0;
    /* 将 PHx 引脚输出类型配置为推挽。 */
    GPIOH->OTYPER = 0x00000000;
    /* PHx 引脚不启用内部上拉或下拉。 */
    GPIOH->PUPDR = 0x00000000;

    /* 将 PIx 引脚连接到 FMC 复用功能。 */
    GPIOI->AFR[0] = 0xCCCCCCCC;
    GPIOI->AFR[1] = 0x00000CC0;
    /* 将 PIx 引脚配置为复用模式。 */
    GPIOI->MODER = 0x0028AAAA;
    /* 将 PIx 引脚速度配置为 50 MHz。 */
    GPIOI->OSPEEDR = 0x0028AAAA;
    /* 将 PIx 引脚输出类型配置为推挽。 */
    GPIOI->OTYPER = 0x00000000;
    /* PIx 引脚不启用内部上拉或下拉。 */
    GPIOI->PUPDR = 0x00000000;
#endif /* 条件结束：STM32F427xx || STM32F437xx || STM32F429xx || STM32F439xx || STM32F469xx || STM32F479xx */

    /* 配置 FMC */
    /* 开启 FMC 接口时钟 */
    RCC->AHB3ENR |= 0x00000001;
    /* 开启 RCC 外设时钟后读取寄存器，等待时钟生效。 */
    tmp = READ_BIT(RCC->AHB3ENR, RCC_AHB3ENR_FMCEN);

    /* 配置并启用 SDRAM 存储体一 */
#if defined(STM32F446xx)
    FMC_Bank5_6->SDCR[0] = 0x00001954;
#else
    FMC_Bank5_6->SDCR[0] = 0x000019E4;
#endif /* 条件结束：STM32F446xx */
    FMC_Bank5_6->SDTR[0] = 0x01115351;

    /* SDRAM 初始化时序 */
    /* 发送时钟使能命令 */
    FMC_Bank5_6->SDCMR = 0x00000011;
    tmpreg = FMC_Bank5_6->SDSR & 0x00000020;
    while ((tmpreg != 0) && (timeout-- > 0)) {
        tmpreg = FMC_Bank5_6->SDSR & 0x00000020;
    }

    /* 延时 */
    for (index = 0; index < 1000; index++) {
        ;
    }

    /* 发送全部存储体预充电命令 */
    FMC_Bank5_6->SDCMR = 0x00000012;
    tmpreg = FMC_Bank5_6->SDSR & 0x00000020;
    timeout = 0xFFFF;
    while ((tmpreg != 0) && (timeout-- > 0)) {
        tmpreg = FMC_Bank5_6->SDSR & 0x00000020;
    }

    /* 发送自动刷新命令 */
#if defined(STM32F446xx)
    FMC_Bank5_6->SDCMR = 0x000000F3;
#else
    FMC_Bank5_6->SDCMR = 0x00000073;
#endif /* 条件结束：STM32F446xx */
    tmpreg = FMC_Bank5_6->SDSR & 0x00000020;
    timeout = 0xFFFF;
    while ((tmpreg != 0) && (timeout-- > 0)) {
        tmpreg = FMC_Bank5_6->SDSR & 0x00000020;
    }

    /* 配置模式寄存器 */
#if defined(STM32F446xx)
    FMC_Bank5_6->SDCMR = 0x00044014;
#else
    FMC_Bank5_6->SDCMR = 0x00046014;
#endif /* 条件结束：STM32F446xx */
    tmpreg = FMC_Bank5_6->SDSR & 0x00000020;
    timeout = 0xFFFF;
    while ((tmpreg != 0) && (timeout-- > 0)) {
        tmpreg = FMC_Bank5_6->SDSR & 0x00000020;
    }

    /* 设置刷新计数 */
    tmpreg = FMC_Bank5_6->SDRTR;
#if defined(STM32F446xx)
    FMC_Bank5_6->SDRTR = (tmpreg | (0x0000050C << 1));
#else
    FMC_Bank5_6->SDRTR = (tmpreg | (0x0000027C << 1));
#endif /* 条件结束：STM32F446xx */

    /* 关闭写保护 */
    tmpreg = FMC_Bank5_6->SDCR[0];
    FMC_Bank5_6->SDCR[0] = (tmpreg & 0xFFFFFDFF);
#endif /* 条件结束：DATA_IN_ExtSDRAM */
#endif /* 条件结束：STM32F427xx || STM32F437xx || STM32F429xx || STM32F439xx || STM32F446xx || STM32F469xx || STM32F479xx */

#if defined(STM32F405xx) || defined(STM32F415xx) || defined(STM32F407xx) ||                        \
    defined(STM32F417xx) || defined(STM32F427xx) || defined(STM32F437xx) ||                        \
    defined(STM32F429xx) || defined(STM32F439xx) || defined(STM32F469xx) ||                        \
    defined(STM32F479xx) || defined(STM32F412Zx) || defined(STM32F412Vx)

#if defined(DATA_IN_ExtSRAM)
    /* 配置 GPIO */
    /* 开启 GPIOD、GPIOE、GPIOF、GPIOG 接口时钟。 */
    RCC->AHB1ENR |= 0x00000078;
    /* 开启 RCC 外设时钟后读取寄存器，等待时钟生效。 */
    tmp = READ_BIT(RCC->AHB1ENR, RCC_AHB1ENR_GPIODEN);

    /* 将 PDx 引脚连接到 FMC 复用功能。 */
    GPIOD->AFR[0] = 0x00CCC0CC;
    GPIOD->AFR[1] = 0xCCCCCCCC;
    /* 将 PDx 引脚配置为复用模式。 */
    GPIOD->MODER = 0xAAAA0A8A;
    /* 将 PDx 引脚速度配置为 100 MHz。 */
    GPIOD->OSPEEDR = 0xFFFF0FCF;
    /* 将 PDx 引脚输出类型配置为推挽。 */
    GPIOD->OTYPER = 0x00000000;
    /* PDx 引脚不启用内部上拉或下拉。 */
    GPIOD->PUPDR = 0x00000000;

    /* 将 PEx 引脚连接到 FMC 复用功能。 */
    GPIOE->AFR[0] = 0xC00CC0CC;
    GPIOE->AFR[1] = 0xCCCCCCCC;
    /* 将 PEx 引脚配置为复用模式。 */
    GPIOE->MODER = 0xAAAA828A;
    /* 将 PEx 引脚速度配置为 100 MHz。 */
    GPIOE->OSPEEDR = 0xFFFFC3CF;
    /* 将 PEx 引脚输出类型配置为推挽。 */
    GPIOE->OTYPER = 0x00000000;
    /* PEx 引脚不启用内部上拉或下拉。 */
    GPIOE->PUPDR = 0x00000000;

    /* 将 PFx 引脚连接到 FMC 复用功能。 */
    GPIOF->AFR[0] = 0x00CCCCCC;
    GPIOF->AFR[1] = 0xCCCC0000;
    /* 将 PFx 引脚配置为复用模式。 */
    GPIOF->MODER = 0xAA000AAA;
    /* 将 PFx 引脚速度配置为 100 MHz。 */
    GPIOF->OSPEEDR = 0xFF000FFF;
    /* 将 PFx 引脚输出类型配置为推挽。 */
    GPIOF->OTYPER = 0x00000000;
    /* PFx 引脚不启用内部上拉或下拉。 */
    GPIOF->PUPDR = 0x00000000;

    /* 将 PGx 引脚连接到 FMC 复用功能。 */
    GPIOG->AFR[0] = 0x00CCCCCC;
    GPIOG->AFR[1] = 0x000000C0;
    /* 将 PGx 引脚配置为复用模式。 */
    GPIOG->MODER = 0x00085AAA;
    /* 将 PGx 引脚速度配置为 100 MHz。 */
    GPIOG->OSPEEDR = 0x000CAFFF;
    /* 将 PGx 引脚输出类型配置为推挽。 */
    GPIOG->OTYPER = 0x00000000;
    /* PGx 引脚不启用内部上拉或下拉。 */
    GPIOG->PUPDR = 0x00000000;

    /* 配置 FMC/FSMC */
    /* 开启 FMC/FSMC 接口时钟 */
    RCC->AHB3ENR |= 0x00000001;

#if defined(STM32F427xx) || defined(STM32F437xx) || defined(STM32F429xx) || defined(STM32F439xx)
    /* 开启 RCC 外设时钟后读取寄存器，等待时钟生效。 */
    tmp = READ_BIT(RCC->AHB3ENR, RCC_AHB3ENR_FMCEN);
    /* 配置并启用 Bank1_SRAM2 */
    FMC_Bank1->BTCR[2] = 0x00001011;
    FMC_Bank1->BTCR[3] = 0x00000201;
    FMC_Bank1E->BWTR[2] = 0x0fffffff;
#endif /* 条件结束：STM32F427xx || STM32F437xx || STM32F429xx || STM32F439xx */
#if defined(STM32F469xx) || defined(STM32F479xx)
    /* 开启 RCC 外设时钟后读取寄存器，等待时钟生效。 */
    tmp = READ_BIT(RCC->AHB3ENR, RCC_AHB3ENR_FMCEN);
    /* 配置并启用 Bank1_SRAM2 */
    FMC_Bank1->BTCR[2] = 0x00001091;
    FMC_Bank1->BTCR[3] = 0x00110212;
    FMC_Bank1E->BWTR[2] = 0x0fffffff;
#endif /* 条件结束：STM32F469xx || STM32F479xx */
#if defined(STM32F405xx) || defined(STM32F415xx) || defined(STM32F407xx) ||                        \
    defined(STM32F417xx) || defined(STM32F412Zx) || defined(STM32F412Vx)
    /* 开启 RCC 外设时钟后读取寄存器，等待时钟生效。 */
    tmp = READ_BIT(RCC->AHB3ENR, RCC_AHB3ENR_FSMCEN);
    /* 配置并启用 Bank1_SRAM2 */
    FSMC_Bank1->BTCR[2] = 0x00001011;
    FSMC_Bank1->BTCR[3] = 0x00000201;
    FSMC_Bank1E->BWTR[2] = 0x0FFFFFFF;
#endif /* 条件结束：STM32F405xx || STM32F415xx || STM32F407xx || STM32F417xx || STM32F412Zx || STM32F412Vx */

#endif /* 条件结束：DATA_IN_ExtSRAM */
#endif /* 条件结束：STM32F405xx || STM32F415xx || STM32F407xx || STM32F417xx || STM32F427xx || STM32F437xx ||           STM32F429xx || STM32F439xx || STM32F469xx || STM32F479xx || STM32F412Zx || STM32F412Vx */
    (void)(tmp);
}
#endif /* 条件结束：DATA_IN_ExtSRAM && DATA_IN_ExtSDRAM */
