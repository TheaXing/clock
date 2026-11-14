/* USER CODE BEGIN Header */
/**
 * @file main.c
 * @brief 应用程序主入口。
 ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* 头文件 */
#include "main.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* 内部头文件 */
/* USER CODE BEGIN Includes */
#include "st7789.h"
#include "FreeRTOS.h"
#include "task.h"
#include "workqueue.h"
#include "clock_config.h"
#include "clock_console.h"
#include "clock_rtc.h"
#include "i2c.h"
#include "app.h"
#include "ui.h"
#include "wifi.h"
#include "page.h"
#include "alarm_service.h"
#include "clock_web.h"
#include "web_session.h"
#include "onenet_service.h"
#include <stdio.h>
/* USER CODE END Includes */

/* 内部类型定义 */
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* 内部常量定义 */
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* 内部宏 */
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* 内部变量 */

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* 内部函数声明 */
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* 用户内部代码 */
/* USER CODE BEGIN 0 */

#if CLOCK_RGB_TEST
/**
 * @brief 功能：初始化液晶屏并循环显示红、绿、蓝测试画面。
 * 参数：
 * @param argument 任务参数，未使用。
 * @retval 返回：无，任务持续运行。
 */
static void display_task(void *argument) {
    (void)argument;
    const uint16_t colors[] = {0xF800, 0x07E0, 0x001F}; //红、绿、蓝

    st7789_init();

    for (;;) {
        for (uint32_t i = 0; i < 3; i++) {
            st7789_fill_color(0, 0, 239, 319, colors[i]);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

/**
 * @brief 功能：通过 USART1 输出工作队列运行提示。
 * 参数：
 * @param argument 工作项参数，未使用。
 * @retval 返回：无
 */
static void print_job(void *argument) {
    (void)argument; //这段代码是为了避免编译器警告，表示参数未使用
    uint8_t msg[] = "Workqueue running\r\n";
    HAL_UART_Transmit(&huart1, msg, sizeof(msg) - 1, 100);
}

/**
 * @brief 功能：每秒向工作队列提交串口输出测试工作项。
 * 参数：
 * @param argument 任务参数，未使用。
 * @retval 返回：无，任务持续运行。
 */
static void test_task(void *argument) {
    (void)argument;

    for (;;) {
        workqueue_run(print_job, NULL);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

#else
/**
 * @brief 功能：初始化时钟、界面、闹钟、网页和云服务，随后启动应用并删除自身。
 * 参数：
 * @param argument 任务参数，未使用。
 * @retval 返回：无
 */
static void clock_init_task(void *argument) {
    (void)argument;
    rtc_init();
    if (!clock_rtc_ready()) {
        clock_log_line("[RTC] Unavailable; check 32.768 kHz LSE crystal");
    }
    ui_init();
    welcome_page_display();
    vTaskDelay(pdMS_TO_TICKS(1000));
    main_page_display();
    main_page_redraw_wifi_ssid("connecting");
    /* 必须先启动本地控制及已恢复的闹钟计划，再等待网络连接。 */
    alarm_service_init();
    uint32_t web_session = clock_web_session_token();
    clock_web_init(web_session);

    onenet_service_init(); // 初始化 OneNET 服务

    if (!web_session) {
        clock_log_line(
            "[WEB] Session RNG unavailable; web control disabled, local functions continue");
    }

    app_init();
    vTaskDelete(NULL);
}
#endif

/* USER CODE END 0 */

/**
 * @brief 功能：初始化硬件与应用任务并启动 FreeRTOS 调度器。
 * 参数：无。
 * @retval 返回：正常运行时不返回；调度器启动失败时进入错误处理。
 */
int main(void) {

    /* USER CODE BEGIN 1 */

    /* USER CODE END 1 */

    /* 配置微控制器 */

    /* 复位所有外设，并初始化 Flash 接口与 SysTick。 */
    HAL_Init();

    /* USER CODE BEGIN Init */

    /* USER CODE END Init */

    /* 配置系统时钟 */
    SystemClock_Config();

    /* USER CODE BEGIN SysInit */

    /* USER CODE END SysInit */

    /* 初始化已配置的全部外设 */
    MX_GPIO_Init();
    MX_USART1_UART_Init();
    MX_SPI2_Init();

    /* USER CODE BEGIN 2 */
    MX_USART2_UART_Init();
    MX_I2C2_Init();
    MX_I2C1_Init();
    clock_console_init();
    setvbuf(
        stdout, NULL, _IONBF,
        0); //这段代码是为了禁用标准输出的缓冲区，使得printf等函数的输出能够立即显示在终端上，而不需要等待缓冲区满或程序结束。
    clock_log("[SYS] Clock_CubeMX %s; %s %s\r\n", APP_VERSION, __DATE__, __TIME__);
    workqueue_init();
#if CLOCK_RGB_TEST
    if (xTaskCreate(test_task, "Test", 256, NULL, 1, NULL) != pdPASS) {
        Error_Handler();
    }

    if (xTaskCreate(display_task, "LCD", 1024, NULL, 1, NULL) != pdPASS) {
        Error_Handler();
    }
#else
    if (xTaskCreate(clock_init_task, "init", 2048, NULL, 4, NULL) != pdPASS) {
        Error_Handler();
    }
#endif

    vTaskStartScheduler();

    /* 调度器正常启动后不会返回。 */
    Error_Handler(); //调度器启动失败时，返回到这里
    /* USER CODE END 2 */

    /* 持续循环 */
    /* USER CODE BEGIN WHILE */
    while (1) {
        /* USER CODE END WHILE */

        /* USER CODE BEGIN 3 */
        uint8_t msg[] = "Clock_CubeMX OK\r\n";
        HAL_UART_Transmit(&huart1, msg, sizeof(msg) - 1, 100);
        HAL_Delay(1000);
    } //这段代码是为了在调度器启动失败时，仍然能够输出信息到串口，方便调试。正常情况下，调度器启动后不会返回到这里。
    /* USER CODE END 3 */
}

/**
 * @brief 功能：使用 HSE 和 PLL 配置 168 MHz 系统时钟及 48 MHz RNG 时钟。
 * 参数：无。
 * @retval 返回：无
 */
void SystemClock_Config(void) {
    RCC_OscInitTypeDef rcc_osc_init = {0};
    RCC_ClkInitTypeDef rcc_clk_init = {0};

    /** 配置内部主稳压器的输出电压。 */
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    /** 根据 RCC_OscInitTypeDef 结构体指定的参数初始化 RCC 振荡器。 */
    rcc_osc_init.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    rcc_osc_init.HSEState = RCC_HSE_ON;
    rcc_osc_init.PLL.PLLState = RCC_PLL_ON;
    rcc_osc_init.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    rcc_osc_init.PLL.PLLM = 8;
    rcc_osc_init.PLL.PLLN = 336;
    rcc_osc_init.PLL.PLLP = RCC_PLLP_DIV2;
    rcc_osc_init.PLL.PLLQ = 7; /* RNG 时钟为 336/7=48 MHz，CPU 与 APB 时钟保持不变。 */
    if (HAL_RCC_OscConfig(&rcc_osc_init) != HAL_OK) {
        Error_Handler();
    }

    /** 初始化 CPU、AHB 和 APB 总线时钟。 */
    rcc_clk_init.ClockType =
        RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    rcc_clk_init.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    rcc_clk_init.AHBCLKDivider = RCC_SYSCLK_DIV1;
    rcc_clk_init.APB1CLKDivider = RCC_HCLK_DIV4;
    rcc_clk_init.APB2CLKDivider = RCC_HCLK_DIV2;

    if (HAL_RCC_ClockConfig(&rcc_clk_init, FLASH_LATENCY_5) != HAL_OK) {
        Error_Handler();
    }
}

/* USER CODE BEGIN 4 */
/**
 * @brief 功能：在 FreeRTOS 断言失败时进入统一错误处理。
 * 参数：
 * @param file 断言所在文件名，未使用。
 * @param line 断言所在行号，未使用。
 * @retval 返回：无
 */
void vAssertCalled(const char *file, int line) {
    (void)file;
    (void)line;
    Error_Handler();
}

/**
 * @brief 功能：在 FreeRTOS 内存分配失败时进入统一错误处理。
 * 参数：无。
 * @retval 返回：无
 */
void vApplicationMallocFailedHook(void) {
    Error_Handler();
}

/**
 * @brief 功能：在任务栈溢出时进入统一错误处理。
 * 参数：
 * @param task 发生栈溢出的任务，未使用。
 * @param name 任务名称，未使用。
 * @retval 返回：无
 */
void vApplicationStackOverflowHook(TaskHandle_t task, char *name) {
    (void)task;
    (void)name;
    Error_Handler();
}

/* USER CODE END 4 */

/**
 * @brief 功能：处理定时器周期回调；TIM7 更新中断通过 HAL_IncTick 递增 HAL 时基。
 * 参数：
 * @param htim 触发周期回调的定时器句柄。
 * @retval 返回：无
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
    /* USER CODE BEGIN Callback 0 */

    /* USER CODE END Callback 0 */
    if (htim->Instance == TIM7) {
        HAL_IncTick();
    }
    /* USER CODE BEGIN Callback 1 */

    /* USER CODE END Callback 1 */
}

/**
 * @brief 功能：关闭中断并停留在错误状态，供系统初始化失败时调用。
 * 参数：无。
 * @retval 返回：无，函数持续等待。
 */
void Error_Handler(void) {
    /* USER CODE BEGIN Error_Handler_Debug */
    /* 可在此补充 HAL 错误状态报告。 */
    __disable_irq();
    while (1) {
    }
    /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
 * @brief 功能：提供 HAL 参数断言失败的文件名和行号报告入口，当前未实现报告。
 * 参数：
 * @param file 断言所在源文件名。
 * @param line 断言所在源文件行号。
 * @retval 返回：无
 */
void assert_failed(uint8_t *file, uint32_t line) {
    /* USER CODE BEGIN 6 */
    /* 可在此补充断言所在文件名和行号的报告，例如调用 clock_log 输出 file 与 line。 */
    /* USER CODE END 6 */
}
#endif /* 条件结束：USE_FULL_ASSERT */
