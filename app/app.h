#ifndef CLOCK_APP_H
#define CLOCK_APP_H
#define APP_VERSION "v1.4.3-ONENET"
void app_init(void);
/* 菜单关闭后，请求正常时钟任务恢复最近缓存的所有显示值。 */
void app_request_main_redraw(void);
#endif
