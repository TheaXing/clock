#ifndef CLOCK_ALARM_MENU_H
#define CLOCK_ALARM_MENU_H
#include "alarm_engine.h"

typedef enum { ALARM_VIEW_MAIN, ALARM_VIEW_LIST, ALARM_VIEW_EDIT, ALARM_VIEW_CLOCK } alarm_view_t;
typedef enum {
    ALARM_INPUT_CONFIRM, ALARM_INPUT_PLUS, ALARM_INPUT_MINUS,
    ALARM_INPUT_CONFIRM_LONG, ALARM_INPUT_PLUS_LONG, ALARM_INPUT_MINUS_LONG
} alarm_input_t;
typedef enum {
    ALARM_ACTION_NONE, ALARM_ACTION_SAVE, ALARM_ACTION_SET_CLOCK,
    ALARM_ACTION_STOP, ALARM_ACTION_SNOOZE, ALARM_ACTION_TEST
} alarm_action_t;
typedef struct {
    alarm_view_t view;
    uint8_t selected;
    uint8_t field;
    alarm_entry_t draft;
    rtc_date_time_t clock_draft;
} alarm_menu_t;

void alarm_menu_init(alarm_menu_t *menu);
alarm_action_t alarm_menu_input(alarm_menu_t *menu, alarm_input_t input,
                                const alarm_config_t *config, const rtc_date_time_t *now,
                                bool active, bool saving);
void alarm_menu_saved(alarm_menu_t *menu, bool success);
#endif
