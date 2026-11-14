/* USER CODE BEGIN Header */
/** 使用 LSE 初始化 RTC，限制失败等待时间并保留备份域日历。 */
/* USER CODE END Header */
#include "rtc.h"

RTC_HandleTypeDef hrtc;

/* USER CODE BEGIN 0 */
static bool rtc_hardware_ready;

/**
 * @brief 功能：使用 LSE 初始化 RTC，保留备份域日历并在初始化失败时及时返回。
 * 参数：无。
 * @retval 返回：无，通过 rtc_hardware_ready 保存初始化结果。
 */
static void clock_rtc_hardware_init(void) {
    RCC_OscInitTypeDef oscillator = {0};
    RCC_PeriphCLKInitTypeDef peripheral_clock = {0};
    rtc_hardware_ready = false;

    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    uint32_t source = __HAL_RCC_GET_RTC_SOURCE();
    if (source != 0U && source != RCC_RTCCLKSOURCE_LSE) {
        /* 切换已有数据的备份域时钟源会清除日历。 */
        return;
    }

    oscillator.OscillatorType = RCC_OSCILLATORTYPE_LSE;
    oscillator.LSEState = RCC_LSE_ON;
    oscillator.PLL.PLLState = RCC_PLL_NONE;
    /* HAL 的 LSE_STARTUP_TIMEOUT 限制启动等待时间；缺少 LSE 时仍应继续显示。 */
    if (HAL_RCC_OscConfig(&oscillator) != HAL_OK) {
        return;
    }
    peripheral_clock.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    peripheral_clock.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
    if (HAL_RCCEx_PeriphCLKConfig(&peripheral_clock) != HAL_OK) {
        return;
    }
    __HAL_RCC_RTC_ENABLE();

    hrtc.Instance = RTC;
    bool retained_calendar = __HAL_RTC_IS_CALENDAR_INITIALIZED(&hrtc) != 0U;
    hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
    hrtc.Init.AsynchPrediv = 127;
    hrtc.Init.SynchPrediv = 255;
    hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
    hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
    if (HAL_RTC_Init(&hrtc) != HAL_OK || HAL_RTC_WaitForSynchro(&hrtc) != HAL_OK) {
        return;
    }
    if (!retained_calendar) {
        /* 仅首次上电设置初始日历，之后由网络时间替换。 */
        RTC_TimeTypeDef time = {0};
        RTC_DateTypeDef date = {0};
        date.Year = 0;
        date.Month = RTC_MONTH_JANUARY;
        date.Date = 1;
        date.WeekDay = RTC_WEEKDAY_SATURDAY;
        if (HAL_RTC_SetTime(&hrtc, &time, RTC_FORMAT_BIN) != HAL_OK ||
            HAL_RTC_SetDate(&hrtc, &date, RTC_FORMAT_BIN) != HAL_OK) {
            return;
        }
    }
    rtc_hardware_ready = true;
}

/**
 * @brief 功能：查询 RTC 硬件初始化是否完成。
 * 参数：无。
 * @retval 返回：初始化完成返回 true，否则返回 false。
 */
bool MX_RTC_IsReady(void) {
    return rtc_hardware_ready;
}
/* USER CODE END 0 */

/**
 * @brief 功能：调用保留备份日历的 RTC 初始化流程，并避开生成代码的默认日历配置。
 * 参数：无。
 * @retval 返回：无
 */
void MX_RTC_Init(void) {
    /* USER CODE BEGIN RTC_Init 0 */
    /* 保留此处提前返回，避免重新生成的 Cube 默认代码重置日期。 */
    clock_rtc_hardware_init();
    return;
    /* USER CODE END RTC_Init 0 */

    /* USER CODE BEGIN RTC_Init 1 */
    /* USER CODE END RTC_Init 1 */
    hrtc.Instance = RTC;
    hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
    hrtc.Init.AsynchPrediv = 127;
    hrtc.Init.SynchPrediv = 255;
    hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
    hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
    if (HAL_RTC_Init(&hrtc) != HAL_OK) {
        Error_Handler();
    }
    /* USER CODE BEGIN RTC_Init 2 */
    /* USER CODE END RTC_Init 2 */
}

/**
 * @brief 功能：在 RTC 底层初始化回调中开启 RTC 时钟。
 * 参数：
 * @param rtc_handle 待初始化的 RTC 句柄。
 * @retval 返回：无
 */
void HAL_RTC_MspInit(RTC_HandleTypeDef *rtc_handle) {
    if (rtc_handle->Instance == RTC) {
        /* USER CODE BEGIN RTC_MspInit 0 */
        /* USER CODE END RTC_MspInit 0 */
        __HAL_RCC_RTC_ENABLE();
        /* USER CODE BEGIN RTC_MspInit 1 */
        /* USER CODE END RTC_MspInit 1 */
    }
}

/**
 * @brief 功能：关闭 RTC 时钟并清除硬件就绪状态。
 * 参数：
 * @param rtc_handle 待反初始化的 RTC 句柄。
 * @retval 返回：无
 */
void HAL_RTC_MspDeInit(RTC_HandleTypeDef *rtc_handle) {
    if (rtc_handle->Instance == RTC) {
        /* USER CODE BEGIN RTC_MspDeInit 0 */
        /* USER CODE END RTC_MspDeInit 0 */
        __HAL_RCC_RTC_DISABLE();
        /* USER CODE BEGIN RTC_MspDeInit 1 */
        rtc_hardware_ready = false;
        /* USER CODE END RTC_MspDeInit 1 */
    }
}
/* USER CODE BEGIN 1 */
/* USER CODE END 1 */
