#ifndef CLOCK_APP_H
#define CLOCK_APP_H
#define APP_VERSION "v1.2-ALARM"
void app_init(void);
/* Ask the normal clock task to restore all latest values after a menu closes. */
void app_request_main_redraw(void);
#endif
