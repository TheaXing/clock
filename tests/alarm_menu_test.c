#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "alarm_menu.h"

int main(void)
{
    alarm_config_t config = {0};
    for (unsigned i=0; i<ALARM_COUNT; ++i) {
        config.entries[i] = (alarm_entry_t){2026,60,300,11,13,7,30,31,0};
    }
    alarm_menu_t menu;
    rtc_date_time_t now = {2026,11,13,12,30,0,5};
    alarm_menu_init(&menu);
    assert(menu.view == ALARM_VIEW_MAIN);
    assert(alarm_menu_input(&menu,ALARM_INPUT_CONFIRM,&config,&now,false,false)==ALARM_ACTION_NONE);
    assert(menu.view == ALARM_VIEW_LIST);
    for(unsigned i=0;i<7;++i) alarm_menu_input(&menu,ALARM_INPUT_PLUS,&config,&now,false,false);
    assert(menu.selected==7);
    alarm_menu_input(&menu,ALARM_INPUT_PLUS,&config,&now,false,false);
    assert(menu.selected==0);
    assert(alarm_menu_input(&menu,ALARM_INPUT_CONFIRM,&config,&now,false,false)==ALARM_ACTION_SAVE);
    assert(menu.draft.enabled==1 && config.entries[0].enabled==0);
    assert(alarm_menu_input(&menu,ALARM_INPUT_PLUS,&config,&now,false,true)==ALARM_ACTION_NONE && menu.selected==0);
    alarm_menu_saved(&menu,true);
    alarm_menu_input(&menu,ALARM_INPUT_MINUS,&config,&now,false,false);
    assert(menu.view==ALARM_VIEW_EDIT && menu.field==0);
    alarm_menu_input(&menu,ALARM_INPUT_CONFIRM,&config,&now,false,false);
    assert(menu.field==1);
    menu.draft.hour=23;
    alarm_menu_input(&menu,ALARM_INPUT_PLUS,&config,&now,false,false);
    assert(menu.draft.hour==0);
    alarm_menu_input(&menu,ALARM_INPUT_MINUS,&config,&now,false,false);
    assert(menu.draft.hour==23);
    menu.field=7; menu.draft.repeat_mask=1;
    alarm_menu_input(&menu,ALARM_INPUT_PLUS,&config,&now,false,false);
    assert(menu.draft.repeat_mask==1); /* weekly cannot become empty/once by accident */
    menu.field=3;
    alarm_menu_input(&menu,ALARM_INPUT_PLUS,&config,&now,false,false);
    assert(menu.draft.repeat_mask==0 && menu.draft.year==2026 && menu.draft.month==11 && menu.draft.day==13);
    menu.field=4; menu.draft.year=2024; menu.draft.month=2; menu.draft.day=29;
    alarm_menu_input(&menu,ALARM_INPUT_PLUS,&config,&now,false,false);
    assert(menu.draft.year==2025 && menu.draft.day==28);
    menu.field=6;
    alarm_menu_input(&menu,ALARM_INPUT_CONFIRM,&config,&now,false,false);
    assert(menu.field==14); /* once hides weekday fields */
    assert(alarm_menu_input(&menu,ALARM_INPUT_CONFIRM_LONG,&config,&now,false,false)==ALARM_ACTION_SAVE);
    alarm_menu_saved(&menu,false);
    assert(menu.view==ALARM_VIEW_EDIT);
    alarm_menu_input(&menu,ALARM_INPUT_MINUS_LONG,&config,&now,false,false);
    assert(menu.view==ALARM_VIEW_LIST && config.entries[0].hour==7);
    alarm_menu_input(&menu,ALARM_INPUT_PLUS_LONG,&config,&now,false,false);
    assert(menu.view==ALARM_VIEW_CLOCK);
    menu.field=4; menu.clock_draft.minute=59;
    alarm_menu_input(&menu,ALARM_INPUT_PLUS,&config,&now,false,false);
    assert(menu.clock_draft.minute==0);
    assert(alarm_menu_input(&menu,ALARM_INPUT_CONFIRM_LONG,&config,&now,false,false)==ALARM_ACTION_SET_CLOCK);
    assert(alarm_menu_input(&menu,ALARM_INPUT_CONFIRM,&config,&now,true,true)==ALARM_ACTION_STOP);
    assert(alarm_menu_input(&menu,ALARM_INPUT_PLUS,&config,&now,true,true)==ALARM_ACTION_SNOOZE);
    alarm_menu_init(&menu);
    assert(alarm_menu_input(&menu,ALARM_INPUT_PLUS_LONG,&config,&now,false,false)==ALARM_ACTION_TEST);
    puts("alarm menu: selection/edit/cancel/save/clock/ringing precedence PASS");
    return 0;
}
