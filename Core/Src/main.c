/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
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
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

#if CLOCK_RGB_TEST
static void DisplayTask(void *argument)
{
    (void)argument;
    const uint16_t colors[] = {0xF800, 0x07E0, 0x001F};//红、绿、蓝

    st7789_init();

    for (;;)
    {
        for (uint32_t i = 0; i < 3; i++)
        {
            st7789_fill_color(0, 0, 239, 319, colors[i]);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

static void PrintJob(void *argument)
{
    (void)argument;//这段代码是为了避免编译器警告，表示参数未使用
    uint8_t msg[] = "Workqueue running\r\n";
    HAL_UART_Transmit(&huart1, msg, sizeof(msg) - 1, 100);
}



static void TestTask(void *argument)
{
    (void)argument;

    for (;;)
    {
        workqueue_run(PrintJob, NULL);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

#else
static void ClockInitTask(void *argument)
{
    (void)argument;
    rtc_init();
    if (!clock_rtc_ready()) clock_log_line("[RTC] Unavailable; check 32.768 kHz LSE crystal");
    ui_init();
    welcome_page_display();
    vTaskDelay(pdMS_TO_TICKS(1000));
    wifi_page_display();
    bool connected = wifi_init() && wifi_wait_connect();
    if (!connected)
    {
        error_page_display("WiFi unavailable; retrying");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    main_page_display();
    main_page_redraw_wifi_ssid(connected ? CLOCK_WIFI_SSID : "wifi lost");
    app_init();
    vTaskDelete(NULL);
}
#endif

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_SPI2_Init();

  /* USER CODE BEGIN 2 */
  MX_USART2_UART_Init();
  MX_I2C2_Init();
  clock_console_init();
  setvbuf(stdout, NULL, _IONBF, 0);
  clock_log("[SYS] Clock_CubeMX %s; %s %s\r\n", APP_VERSION, __DATE__, __TIME__);
  workqueue_init();
#if CLOCK_RGB_TEST
  if (xTaskCreate(TestTask, "Test", 256, NULL, 1, NULL) != pdPASS)
  {
    Error_Handler();
  }

  if (xTaskCreate(DisplayTask, "LCD", 1024, NULL, 1, NULL) != pdPASS)
  {
    Error_Handler();
  }
#else
  if (xTaskCreate(ClockInitTask, "init", 2048, NULL, 4, NULL) != pdPASS)
  {
    Error_Handler();
  }
#endif

  vTaskStartScheduler();

  /* 调度器正常启动后不会返回。 */
  Error_Handler();//调度器启动失败时，返回到这里
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    uint8_t msg[] = "Clock_CubeMX OK\r\n";//
    HAL_UART_Transmit(&huart1, msg, sizeof(msg) - 1, 100);
    HAL_Delay(1000);
  }//这段代码是为了在调度器启动失败时，仍然能够输出信息到串口，方便调试。正常情况下，调度器启动后不会返回到这里。
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
void vAssertCalled(const char *file, int line)
{
    (void)file;
    (void)line;
    Error_Handler();
}

void vApplicationMallocFailedHook(void)
{
    Error_Handler();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    (void)name;
    Error_Handler();
}

/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM7 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM7)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: clock_log("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
