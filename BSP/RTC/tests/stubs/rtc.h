#ifndef TEST_RTC_H
#define TEST_RTC_H
#include <stdint.h>
#include <stdbool.h>
/** 为主机测试保留 HAL 句柄的最小占位布局。 */
typedef struct {
    int unused; // 测试句柄占位成员
} RTC_HandleTypeDef;
/** 模拟 HAL RTC 时间与附加配置字段。 */
typedef struct {
    uint8_t Hours;           // 小时字段
    uint8_t Minutes;         // 分钟字段
    uint8_t Seconds;         // 秒字段
    uint8_t TimeFormat;      // 十二小时制上下午标记
    uint32_t DayLightSaving; // 夏令时调整选项
    uint32_t StoreOperation; // RTC 保存操作选项
} RTC_TimeTypeDef;
/** 模拟 HAL RTC 日期字段，年份相对于 2000 年。 */
typedef struct {
    uint8_t WeekDay; // 星期字段，星期一为 1，星期日为 7
    uint8_t Month;   // 月份字段，取值为 1～12
    uint8_t Date;    // 月内日期字段，从 1 开始
    uint8_t Year;    // 两位年份，相对于 2000 年
} RTC_DateTypeDef;
typedef enum { HAL_OK = 0, HAL_ERROR = 1 } HAL_StatusTypeDef;
#define RTC_FORMAT_BIN 0U
#define RTC_HOURFORMAT12_AM 0U
#define RTC_DAYLIGHTSAVING_NONE 0U
#define RTC_STOREOPERATION_RESET 0U
extern RTC_HandleTypeDef hrtc;
void MX_RTC_Init(void);
bool MX_RTC_IsReady(void);
HAL_StatusTypeDef HAL_RTC_GetTime(RTC_HandleTypeDef *, RTC_TimeTypeDef *, uint32_t);
HAL_StatusTypeDef HAL_RTC_GetDate(RTC_HandleTypeDef *, RTC_DateTypeDef *, uint32_t);
HAL_StatusTypeDef HAL_RTC_SetTime(RTC_HandleTypeDef *, RTC_TimeTypeDef *, uint32_t);
HAL_StatusTypeDef HAL_RTC_SetDate(RTC_HandleTypeDef *, RTC_DateTypeDef *, uint32_t);
#endif
