#ifndef CLOCK_ALARM_PAGE_H
#define CLOCK_ALARM_PAGE_H
#include "alarm_menu.h"
/** 提供一次闹钟页面绘制所需的只读模型。 */
typedef struct {
    const alarm_config_t *config; // 本次绘制使用的完整闹钟配置
    const alarm_menu_t *menu;     // 本次绘制使用的菜单状态
    rtc_date_time_t now;          // 当前日历时间
    uint32_t revision;            // 已验证的 EEPROM 日志代数
    uint8_t ringing_mask;         // 正在响铃的闹钟位掩码
    uint8_t snoozed_mask;         // 正在贪睡等待的闹钟位掩码
    bool time_valid;              // 当前日历时间是否可信
    bool saving;                  // 是否存在正在保存的任务
    bool storage_ok;              // 最近持久化操作是否成功
    bool test_pending;            // 是否存在待触发的临时测试提醒
    bool test_ringing;            // 临时测试提醒是否正在响铃
    const char *message;          // 可选状态说明文本
} alarm_page_model_t;
/* 显示任务持有页面生产者显示锁时调用。 */
void alarm_page_render(const alarm_page_model_t *model);
/* 显示任务将屏幕所有权交还主页面时调用。 */
void alarm_page_invalidate(void);
#endif
