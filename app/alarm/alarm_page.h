#ifndef CLOCK_ALARM_PAGE_H
#define CLOCK_ALARM_PAGE_H
#include "alarm_menu.h"
typedef struct {
    const alarm_config_t *config;
    const alarm_menu_t *menu;
    rtc_date_time_t now;
    uint32_t revision;
    uint8_t ringing_mask;
    uint8_t snoozed_mask;
    bool time_valid;
    bool saving;
    bool storage_ok;
    bool test_pending;
    bool test_ringing;
    const char *message;
} alarm_page_model_t;
/* Called by the view worker with the producer display lock held. */
void alarm_page_render(const alarm_page_model_t *model);
/* View worker calls this on returning screen ownership to the main page. */
void alarm_page_invalidate(void);
#endif
