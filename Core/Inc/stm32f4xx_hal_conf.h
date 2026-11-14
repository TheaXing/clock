/* USER CODE BEGIN Header */
/**
 * @file stm32f4xx_hal_conf.h
 * @author MCD 应用团队
 * @brief 配置 HAL 模块及其参数。
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
/* USER CODE END Header */

/* 防止头文件重复包含 */
#ifndef __STM32F4xx_HAL_CONF_H
#define __STM32F4xx_HAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

/* 对外类型 */
/* 对外常量 */

/* 模块选择 */
/** @brief HAL 驱动使用的模块列表。 */
#define HAL_MODULE_ENABLED

/* #define HAL_CRYP_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_ADC_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_CAN_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_CRC_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_CAN_LEGACY_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_DAC_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_DCMI_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_DMA2D_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_ETH_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_ETH_LEGACY_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_NAND_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_NOR_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_PCCARD_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_SRAM_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_SDRAM_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_HASH_MODULE_ENABLED // 取消本行块注释以启用。 */
#define HAL_I2C_MODULE_ENABLED
/* #define HAL_I2S_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_IWDG_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_LTDC_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_RNG_MODULE_ENABLED // 取消本行块注释以启用。 */
#define HAL_RTC_MODULE_ENABLED
/* #define HAL_SAI_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_SD_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_MMC_MODULE_ENABLED // 取消本行块注释以启用。 */
#define HAL_SPI_MODULE_ENABLED
#define HAL_TIM_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED
/* #define HAL_USART_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_IRDA_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_SMARTCARD_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_SMBUS_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_WWDG_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_PCD_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_HCD_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_DSI_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_QSPI_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_QSPI_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_CEC_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_FMPI2C_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_FMPSMBUS_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_SPDIFRX_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_DFSDM_MODULE_ENABLED // 取消本行块注释以启用。 */
/* #define HAL_LPTIM_MODULE_ENABLED // 取消本行块注释以启用。 */
#define HAL_GPIO_MODULE_ENABLED
#define HAL_EXTI_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED

/* 配置 HSE/HSI 参数 */
/** @brief 配置应用使用的外部高速振荡器 HSE 频率；当系统时钟直接或经 PLL 使用 HSE 时，RCC HAL 根据此值计算系统频率。 */
#if !defined(HSE_VALUE)
#define HSE_VALUE    8000000U /*!< 外部振荡器频率，单位为 Hz。 */
#endif /* 条件结束：HSE_VALUE */

#if !defined(HSE_STARTUP_TIMEOUT)
#define HSE_STARTUP_TIMEOUT    100U   /*!< HSE 启动超时时间，单位为 ms。 */
#endif /* 条件结束：HSE_STARTUP_TIMEOUT */

/** @brief 内部高速振荡器 HSI 频率；当系统时钟直接或经 PLL 使用 HSI 时，RCC HAL 根据此值计算系统频率。 */
#if !defined(HSI_VALUE)
#define HSI_VALUE    ((uint32_t)16000000U) /*!< 内部振荡器频率，单位为 Hz。 */
#endif /* 条件结束：HSI_VALUE */

/** @brief 内部低速振荡器 LSI 频率。 */
#if !defined(LSI_VALUE)
#define LSI_VALUE  32000U       /*!< LSI 典型频率，单位为 Hz。 */
#endif /* 条件结束：LSI_VALUE */ /*!< 内部低速振荡器频率，单位为 Hz；实际值随电压和温度变化。 */
/** @brief 外部低速振荡器 LSE 频率。 */
#if !defined(LSE_VALUE)
#define LSE_VALUE  32768U    /*!< 外部低速振荡器频率，单位为 Hz。 */
#endif /* 条件结束：LSE_VALUE */

#if !defined(LSE_STARTUP_TIMEOUT)
#define LSE_STARTUP_TIMEOUT    5000U   /*!< LSE 启动超时时间，单位为 ms。 */
#endif /* 条件结束：LSE_STARTUP_TIMEOUT */

/** @brief I2S 外设的外部时钟源频率；I2S HAL 根据此值计算由 I2S_CKIN 引脚直接输入的时钟频率。 */
#if !defined(EXTERNAL_CLOCK_VALUE)
#define EXTERNAL_CLOCK_VALUE    12288000U /*!< 外部音频时钟频率，单位为 Hz。 */
#endif /* 条件结束：EXTERNAL_CLOCK_VALUE */

/* 提示：可在工具链编译器的预处理选项中定义 HSE 频率，避免每次切换晶振都修改本文件。 */

/* 系统配置 */
/** @brief HAL 系统配置。 */
#define VDD_VALUE              3300U /*!< VDD 电压，单位为 mV。 */
#define TICK_INT_PRIORITY            15U   /*!< 时基中断优先级 */
#define USE_RTOS                     0U
#define PREFETCH_ENABLE              1U
#define INSTRUCTION_CACHE_ENABLE     1U
#define DATA_CACHE_ENABLE            1U

#define USE_HAL_ADC_REGISTER_CALLBACKS         0U /* ADC 回调注册功能已禁用。 */
#define USE_HAL_CAN_REGISTER_CALLBACKS         0U /* CAN 回调注册功能已禁用。 */
#define USE_HAL_CEC_REGISTER_CALLBACKS         0U /* CEC 回调注册功能已禁用。 */
#define USE_HAL_CRYP_REGISTER_CALLBACKS        0U /* CRYP 回调注册功能已禁用。 */
#define USE_HAL_DAC_REGISTER_CALLBACKS         0U /* DAC 回调注册功能已禁用。 */
#define USE_HAL_DCMI_REGISTER_CALLBACKS        0U /* DCMI 回调注册功能已禁用。 */
#define USE_HAL_DFSDM_REGISTER_CALLBACKS       0U /* DFSDM 回调注册功能已禁用。 */
#define USE_HAL_DMA2D_REGISTER_CALLBACKS       0U /* DMA2D 回调注册功能已禁用。 */
#define USE_HAL_DSI_REGISTER_CALLBACKS         0U /* DSI 回调注册功能已禁用。 */
#define USE_HAL_ETH_REGISTER_CALLBACKS         0U /* ETH 回调注册功能已禁用。 */
#define USE_HAL_HASH_REGISTER_CALLBACKS        0U /* HASH 回调注册功能已禁用。 */
#define USE_HAL_HCD_REGISTER_CALLBACKS         0U /* HCD 回调注册功能已禁用。 */
#define USE_HAL_I2C_REGISTER_CALLBACKS         0U /* I2C 回调注册功能已禁用。 */
#define USE_HAL_FMPI2C_REGISTER_CALLBACKS      0U /* FMPI2C 回调注册功能已禁用。 */
#define USE_HAL_FMPSMBUS_REGISTER_CALLBACKS    0U /* FMPSMBUS 回调注册功能已禁用。 */
#define USE_HAL_I2S_REGISTER_CALLBACKS         0U /* I2S 回调注册功能已禁用。 */
#define USE_HAL_IRDA_REGISTER_CALLBACKS        0U /* IRDA 回调注册功能已禁用。 */
#define USE_HAL_LPTIM_REGISTER_CALLBACKS       0U /* LPTIM 回调注册功能已禁用。 */
#define USE_HAL_LTDC_REGISTER_CALLBACKS        0U /* LTDC 回调注册功能已禁用。 */
#define USE_HAL_MMC_REGISTER_CALLBACKS         0U /* MMC 回调注册功能已禁用。 */
#define USE_HAL_NAND_REGISTER_CALLBACKS        0U /* NAND 回调注册功能已禁用。 */
#define USE_HAL_NOR_REGISTER_CALLBACKS         0U /* NOR 回调注册功能已禁用。 */
#define USE_HAL_PCCARD_REGISTER_CALLBACKS      0U /* PCCARD 回调注册功能已禁用。 */
#define USE_HAL_PCD_REGISTER_CALLBACKS         0U /* PCD 回调注册功能已禁用。 */
#define USE_HAL_QSPI_REGISTER_CALLBACKS        0U /* QSPI 回调注册功能已禁用。 */
#define USE_HAL_RNG_REGISTER_CALLBACKS         0U /* RNG 回调注册功能已禁用。 */
#define USE_HAL_RTC_REGISTER_CALLBACKS         0U /* RTC 回调注册功能已禁用。 */
#define USE_HAL_SAI_REGISTER_CALLBACKS         0U /* SAI 回调注册功能已禁用。 */
#define USE_HAL_SD_REGISTER_CALLBACKS          0U /* SD 回调注册功能已禁用。 */
#define USE_HAL_SMARTCARD_REGISTER_CALLBACKS   0U /* SMARTCARD 回调注册功能已禁用。 */
#define USE_HAL_SDRAM_REGISTER_CALLBACKS       0U /* SDRAM 回调注册功能已禁用。 */
#define USE_HAL_SRAM_REGISTER_CALLBACKS        0U /* SRAM 回调注册功能已禁用。 */
#define USE_HAL_SPDIFRX_REGISTER_CALLBACKS     0U /* SPDIFRX 回调注册功能已禁用。 */
#define USE_HAL_SMBUS_REGISTER_CALLBACKS       0U /* SMBUS 回调注册功能已禁用。 */
#define USE_HAL_SPI_REGISTER_CALLBACKS         0U /* SPI 回调注册功能已禁用。 */
#define USE_HAL_TIM_REGISTER_CALLBACKS         0U /* TIM 回调注册功能已禁用。 */
#define USE_HAL_UART_REGISTER_CALLBACKS        0U /* UART 回调注册功能已禁用。 */
#define USE_HAL_USART_REGISTER_CALLBACKS       0U /* USART 回调注册功能已禁用。 */
#define USE_HAL_WWDG_REGISTER_CALLBACKS        0U /* WWDG 回调注册功能已禁用。 */

/* 断言配置 */
/** @brief 取消下列宏定义的注释，以启用 HAL 驱动中的 assert_param 参数检查。 */
/* #define USE_FULL_ASSERT    1U // 取消本行块注释以启用。 */

/* 以太网外设配置 */

/* 第一节：以太网外设配置 */

/* MAC 地址：MAC_ADDR0:MAC_ADDR1:MAC_ADDR2:MAC_ADDR3:MAC_ADDR4:MAC_ADDR5 */
#define MAC_ADDR0   2U
#define MAC_ADDR1   0U
#define MAC_ADDR2   0U
#define MAC_ADDR3   0U
#define MAC_ADDR4   0U
#define MAC_ADDR5   0U

/* 以太网驱动缓冲区大小与数量 */
#define ETH_RX_BUF_SIZE                ETH_MAX_PACKET_SIZE /* 接收缓冲区大小 */
#define ETH_TX_BUF_SIZE                ETH_MAX_PACKET_SIZE /* 发送缓冲区大小 */
#define ETH_RXBUFNB                    4U       /* 四个大小为 ETH_RX_BUF_SIZE 的接收缓冲区 */
#define ETH_TXBUFNB                    4U       /* 四个大小为 ETH_TX_BUF_SIZE 的发送缓冲区 */

/* 第二节：PHY 配置 */

/* DP83848_PHY_ADDRESS 对应的 PHY 地址 */
#define DP83848_PHY_ADDRESS
/* PHY 复位延时，数值基于 1 ms 的 SysTick 中断。 */
#define PHY_RESET_DELAY                 0x000000FFU
/* PHY 配置延时 */
#define PHY_CONFIG_DELAY                0x00000FFFU

#define PHY_READ_TO                     0x0000FFFFU
#define PHY_WRITE_TO                    0x0000FFFFU

/* 第三节：通用 PHY 寄存器 */

#define PHY_BCR                         ((uint16_t)0x0000U)    /*!< 收发器基本控制寄存器 */
#define PHY_BSR                         ((uint16_t)0x0001U)    /*!< 收发器基本状态寄存器 */

#define PHY_RESET                       ((uint16_t)0x8000U)  /*!< 复位 PHY */
#define PHY_LOOPBACK                    ((uint16_t)0x4000U)  /*!< 选择环回模式 */
#define PHY_FULLDUPLEX_100M             ((uint16_t)0x2100U)  /*!< 设置 100 Mb/s 全双工模式 */
#define PHY_HALFDUPLEX_100M             ((uint16_t)0x2000U)  /*!< 设置 100 Mb/s 半双工模式 */
#define PHY_FULLDUPLEX_10M              ((uint16_t)0x0100U)  /*!< 设置 10 Mb/s 全双工模式 */
#define PHY_HALFDUPLEX_10M              ((uint16_t)0x0000U)  /*!< 设置 10 Mb/s 半双工模式 */
#define PHY_AUTONEGOTIATION             ((uint16_t)0x1000U)  /*!< 开启自动协商功能 */
#define PHY_RESTART_AUTONEGOTIATION     ((uint16_t)0x0200U)  /*!< 重新启动自动协商 */
#define PHY_POWERDOWN                   ((uint16_t)0x0800U)  /*!< 选择掉电模式 */
#define PHY_ISOLATE                     ((uint16_t)0x0400U)  /*!< 隔离 PHY 与 MII */

#define PHY_AUTONEGO_COMPLETE           ((uint16_t)0x0020U)  /*!< 自动协商过程已完成 */
#define PHY_LINKED_STATUS               ((uint16_t)0x0004U)  /*!< 已建立有效链路 */
#define PHY_JABBER_DETECTION            ((uint16_t)0x0002U)  /*!< 检测到异常长帧状态 */

/* 第四节：扩展 PHY 寄存器 */
#define PHY_SR                          ((uint16_t))    /*!< PHY 状态寄存器偏移 */

#define PHY_SPEED_STATUS                ((uint16_t))  /*!< PHY 速率掩码 */
#define PHY_DUPLEX_STATUS               ((uint16_t))  /*!< PHY 双工模式掩码 */

/* SPI 外设配置 */

/* CRC 功能控制：启用时保留 HAL SPI 驱动中的 CRC 代码，禁用时移除该代码。 */

#define USE_SPI_CRC                     0U

/* 头文件 */
/** @brief 包含对应模块的头文件。 */

#ifdef HAL_RCC_MODULE_ENABLED
#include "stm32f4xx_hal_rcc.h"
#endif /* 条件结束：HAL_RCC_MODULE_ENABLED */

#ifdef HAL_GPIO_MODULE_ENABLED
#include "stm32f4xx_hal_gpio.h"
#endif /* 条件结束：HAL_GPIO_MODULE_ENABLED */

#ifdef HAL_EXTI_MODULE_ENABLED
#include "stm32f4xx_hal_exti.h"
#endif /* 条件结束：HAL_EXTI_MODULE_ENABLED */

#ifdef HAL_DMA_MODULE_ENABLED
#include "stm32f4xx_hal_dma.h"
#endif /* 条件结束：HAL_DMA_MODULE_ENABLED */

#ifdef HAL_CORTEX_MODULE_ENABLED
#include "stm32f4xx_hal_cortex.h"
#endif /* 条件结束：HAL_CORTEX_MODULE_ENABLED */

#ifdef HAL_ADC_MODULE_ENABLED
#include "stm32f4xx_hal_adc.h"
#endif /* 条件结束：HAL_ADC_MODULE_ENABLED */

#ifdef HAL_CAN_MODULE_ENABLED
#include "stm32f4xx_hal_can.h"
#endif /* 条件结束：HAL_CAN_MODULE_ENABLED */

#ifdef HAL_CAN_LEGACY_MODULE_ENABLED
#include "stm32f4xx_hal_can_legacy.h"
#endif /* 条件结束：HAL_CAN_LEGACY_MODULE_ENABLED */

#ifdef HAL_CRC_MODULE_ENABLED
#include "stm32f4xx_hal_crc.h"
#endif /* 条件结束：HAL_CRC_MODULE_ENABLED */

#ifdef HAL_CRYP_MODULE_ENABLED
#include "stm32f4xx_hal_cryp.h"
#endif /* 条件结束：HAL_CRYP_MODULE_ENABLED */

#ifdef HAL_DMA2D_MODULE_ENABLED
#include "stm32f4xx_hal_dma2d.h"
#endif /* 条件结束：HAL_DMA2D_MODULE_ENABLED */

#ifdef HAL_DAC_MODULE_ENABLED
#include "stm32f4xx_hal_dac.h"
#endif /* 条件结束：HAL_DAC_MODULE_ENABLED */

#ifdef HAL_DCMI_MODULE_ENABLED
#include "stm32f4xx_hal_dcmi.h"
#endif /* 条件结束：HAL_DCMI_MODULE_ENABLED */

#ifdef HAL_ETH_MODULE_ENABLED
#include "stm32f4xx_hal_eth.h"
#endif /* 条件结束：HAL_ETH_MODULE_ENABLED */

#ifdef HAL_ETH_LEGACY_MODULE_ENABLED
#include "stm32f4xx_hal_eth_legacy.h"
#endif /* 条件结束：HAL_ETH_LEGACY_MODULE_ENABLED */

#ifdef HAL_FLASH_MODULE_ENABLED
#include "stm32f4xx_hal_flash.h"
#endif /* 条件结束：HAL_FLASH_MODULE_ENABLED */

#ifdef HAL_SRAM_MODULE_ENABLED
#include "stm32f4xx_hal_sram.h"
#endif /* 条件结束：HAL_SRAM_MODULE_ENABLED */

#ifdef HAL_NOR_MODULE_ENABLED
#include "stm32f4xx_hal_nor.h"
#endif /* 条件结束：HAL_NOR_MODULE_ENABLED */

#ifdef HAL_NAND_MODULE_ENABLED
#include "stm32f4xx_hal_nand.h"
#endif /* 条件结束：HAL_NAND_MODULE_ENABLED */

#ifdef HAL_PCCARD_MODULE_ENABLED
#include "stm32f4xx_hal_pccard.h"
#endif /* 条件结束：HAL_PCCARD_MODULE_ENABLED */

#ifdef HAL_SDRAM_MODULE_ENABLED
#include "stm32f4xx_hal_sdram.h"
#endif /* 条件结束：HAL_SDRAM_MODULE_ENABLED */

#ifdef HAL_HASH_MODULE_ENABLED
#include "stm32f4xx_hal_hash.h"
#endif /* 条件结束：HAL_HASH_MODULE_ENABLED */

#ifdef HAL_I2C_MODULE_ENABLED
#include "stm32f4xx_hal_i2c.h"
#endif /* 条件结束：HAL_I2C_MODULE_ENABLED */

#ifdef HAL_SMBUS_MODULE_ENABLED
#include "stm32f4xx_hal_smbus.h"
#endif /* 条件结束：HAL_SMBUS_MODULE_ENABLED */

#ifdef HAL_I2S_MODULE_ENABLED
#include "stm32f4xx_hal_i2s.h"
#endif /* 条件结束：HAL_I2S_MODULE_ENABLED */

#ifdef HAL_IWDG_MODULE_ENABLED
#include "stm32f4xx_hal_iwdg.h"
#endif /* 条件结束：HAL_IWDG_MODULE_ENABLED */

#ifdef HAL_LTDC_MODULE_ENABLED
#include "stm32f4xx_hal_ltdc.h"
#endif /* 条件结束：HAL_LTDC_MODULE_ENABLED */

#ifdef HAL_PWR_MODULE_ENABLED
#include "stm32f4xx_hal_pwr.h"
#endif /* 条件结束：HAL_PWR_MODULE_ENABLED */

#ifdef HAL_RNG_MODULE_ENABLED
#include "stm32f4xx_hal_rng.h"
#endif /* 条件结束：HAL_RNG_MODULE_ENABLED */

#ifdef HAL_RTC_MODULE_ENABLED
#include "stm32f4xx_hal_rtc.h"
#endif /* 条件结束：HAL_RTC_MODULE_ENABLED */

#ifdef HAL_SAI_MODULE_ENABLED
#include "stm32f4xx_hal_sai.h"
#endif /* 条件结束：HAL_SAI_MODULE_ENABLED */

#ifdef HAL_SD_MODULE_ENABLED
#include "stm32f4xx_hal_sd.h"
#endif /* 条件结束：HAL_SD_MODULE_ENABLED */

#ifdef HAL_SPI_MODULE_ENABLED
#include "stm32f4xx_hal_spi.h"
#endif /* 条件结束：HAL_SPI_MODULE_ENABLED */

#ifdef HAL_TIM_MODULE_ENABLED
#include "stm32f4xx_hal_tim.h"
#endif /* 条件结束：HAL_TIM_MODULE_ENABLED */

#ifdef HAL_UART_MODULE_ENABLED
#include "stm32f4xx_hal_uart.h"
#endif /* 条件结束：HAL_UART_MODULE_ENABLED */

#ifdef HAL_USART_MODULE_ENABLED
#include "stm32f4xx_hal_usart.h"
#endif /* 条件结束：HAL_USART_MODULE_ENABLED */

#ifdef HAL_IRDA_MODULE_ENABLED
#include "stm32f4xx_hal_irda.h"
#endif /* 条件结束：HAL_IRDA_MODULE_ENABLED */

#ifdef HAL_SMARTCARD_MODULE_ENABLED
#include "stm32f4xx_hal_smartcard.h"
#endif /* 条件结束：HAL_SMARTCARD_MODULE_ENABLED */

#ifdef HAL_WWDG_MODULE_ENABLED
#include "stm32f4xx_hal_wwdg.h"
#endif /* 条件结束：HAL_WWDG_MODULE_ENABLED */

#ifdef HAL_PCD_MODULE_ENABLED
#include "stm32f4xx_hal_pcd.h"
#endif /* 条件结束：HAL_PCD_MODULE_ENABLED */

#ifdef HAL_HCD_MODULE_ENABLED
#include "stm32f4xx_hal_hcd.h"
#endif /* 条件结束：HAL_HCD_MODULE_ENABLED */

#ifdef HAL_DSI_MODULE_ENABLED
#include "stm32f4xx_hal_dsi.h"
#endif /* 条件结束：HAL_DSI_MODULE_ENABLED */

#ifdef HAL_QSPI_MODULE_ENABLED
#include "stm32f4xx_hal_qspi.h"
#endif /* 条件结束：HAL_QSPI_MODULE_ENABLED */

#ifdef HAL_CEC_MODULE_ENABLED
#include "stm32f4xx_hal_cec.h"
#endif /* 条件结束：HAL_CEC_MODULE_ENABLED */

#ifdef HAL_FMPI2C_MODULE_ENABLED
#include "stm32f4xx_hal_fmpi2c.h"
#endif /* 条件结束：HAL_FMPI2C_MODULE_ENABLED */

#ifdef HAL_FMPSMBUS_MODULE_ENABLED
#include "stm32f4xx_hal_fmpsmbus.h"
#endif /* 条件结束：HAL_FMPSMBUS_MODULE_ENABLED */

#ifdef HAL_SPDIFRX_MODULE_ENABLED
#include "stm32f4xx_hal_spdifrx.h"
#endif /* 条件结束：HAL_SPDIFRX_MODULE_ENABLED */

#ifdef HAL_DFSDM_MODULE_ENABLED
#include "stm32f4xx_hal_dfsdm.h"
#endif /* 条件结束：HAL_DFSDM_MODULE_ENABLED */

#ifdef HAL_LPTIM_MODULE_ENABLED
#include "stm32f4xx_hal_lptim.h"
#endif /* 条件结束：HAL_LPTIM_MODULE_ENABLED */

#ifdef HAL_MMC_MODULE_ENABLED
#include "stm32f4xx_hal_mmc.h"
#endif /* 条件结束：HAL_MMC_MODULE_ENABLED */

/* 对外宏 */
#ifdef USE_FULL_ASSERT
/** @brief 使用 assert_param 宏检查函数参数。
 * @param expr 待检查的表达式；为假时调用 assert_failed 报告源文件名和行号，为真时不执行额外操作。
 * @retval 无。 */
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
/* 对外函数 */
void assert_failed(uint8_t *file, uint32_t line);
#else
#define assert_param(expr) ((void)0U)
#endif /* 条件结束：USE_FULL_ASSERT */

#ifdef __cplusplus
}
#endif

#endif /* 条件结束：__STM32F4xx_HAL_CONF_H */
