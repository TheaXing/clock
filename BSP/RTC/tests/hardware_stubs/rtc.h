#ifndef TEST_HARDWARE_RTC_H
#define TEST_HARDWARE_RTC_H
#include <stdint.h>
#include <stdbool.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_TIMEOUT } HAL_StatusTypeDef;
/** 提供 RTC 硬件初始化测试所需的最小寄存器布局。 */
typedef struct {
    uint32_t ISR; // 模拟 RTC 初始化状态寄存器
} RTC_TypeDef;
extern RTC_TypeDef test_rtc_instance;
#define RTC (&test_rtc_instance)
/** 模拟 HAL RTC 初始化参数，用于校验分频与输出配置。 */
typedef struct {
    uint32_t HourFormat;     // RTC 小时格式配置
    uint32_t AsynchPrediv;   // 异步预分频系数
    uint32_t SynchPrediv;    // 同步预分频系数
    uint32_t OutPut;         // RTC 输出选择
    uint32_t OutPutPolarity; // RTC 输出有效极性
    uint32_t OutPutType;     // RTC 输出电气类型
} RTC_InitTypeDef;
/** 组合模拟 RTC 寄存器实例与初始化参数。 */
typedef struct {
    RTC_TypeDef *Instance; // 模拟 RTC 寄存器实例
    RTC_InitTypeDef Init;  // RTC 初始化参数
} RTC_HandleTypeDef;
/** 模拟 HAL RTC 时间字段，用于硬件初始化测试。 */
typedef struct {
    uint8_t Hours;   // 小时字段
    uint8_t Minutes; // 分钟字段
    uint8_t Seconds; // 秒字段
} RTC_TimeTypeDef;
/** 模拟 HAL RTC 日期字段，年份相对于 2000 年。 */
typedef struct {
    uint8_t WeekDay; // 星期字段，星期一为 1，星期日为 7
    uint8_t Month;   // 月份字段，取值为 1～12
    uint8_t Date;    // 月内日期字段，从 1 开始
    uint8_t Year;    // 两位年份，相对于 2000 年
} RTC_DateTypeDef;
/** 模拟外部低速晶振及锁相环配置参数。 */
typedef struct {
    uint32_t OscillatorType; // 待配置的振荡器类型
    uint32_t LSEState;       // 外部低速晶振开关状态
    /** 保存模拟锁相环配置操作。 */
    struct {
        uint32_t PLLState; // 锁相环配置操作
    } PLL;                 // 锁相环配置参数
} RCC_OscInitTypeDef;
/** 模拟外设与 RTC 时钟源选择参数。 */
typedef struct {
    uint32_t PeriphClockSelection; // 待配置的外设时钟类型
    uint32_t RTCClockSelection;    // RTC 时钟源选择
} RCC_PeriphCLKInitTypeDef;
extern uint32_t test_clock_source;
extern bool test_rtc_enabled;
#define __HAL_RCC_PWR_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GET_RTC_SOURCE() test_clock_source
#define __HAL_RCC_RTC_ENABLE() (test_rtc_enabled = true)
#define __HAL_RCC_RTC_DISABLE() (test_rtc_enabled = false)
#define __HAL_RTC_IS_CALENDAR_INITIALIZED(handle) ((handle)->Instance->ISR != 0U)
#define RCC_RTCCLKSOURCE_LSE 1U
#define RCC_OSCILLATORTYPE_LSE 2U
#define RCC_LSE_ON 1U
#define RCC_PLL_NONE 0U
#define RCC_PERIPHCLK_RTC 1U
#define RTC_HOURFORMAT_24 0U
#define RTC_OUTPUT_DISABLE 0U
#define RTC_OUTPUT_POLARITY_HIGH 0U
#define RTC_OUTPUT_TYPE_OPENDRAIN 0U
#define RTC_MONTH_JANUARY 1U
#define RTC_WEEKDAY_SATURDAY 6U
#define RTC_FORMAT_BIN 0U
void HAL_PWR_EnableBkUpAccess(void);
HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef *);
HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *);
HAL_StatusTypeDef HAL_RTC_Init(RTC_HandleTypeDef *);
HAL_StatusTypeDef HAL_RTC_WaitForSynchro(RTC_HandleTypeDef *);
HAL_StatusTypeDef HAL_RTC_SetTime(RTC_HandleTypeDef *, RTC_TimeTypeDef *, uint32_t);
HAL_StatusTypeDef HAL_RTC_SetDate(RTC_HandleTypeDef *, RTC_DateTypeDef *, uint32_t);
void Error_Handler(void);
void MX_RTC_Init(void);
bool MX_RTC_IsReady(void);
extern RTC_HandleTypeDef hrtc;
#endif
