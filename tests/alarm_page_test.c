#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "alarm_page.h"
#include "ui.h"

const font_t font16_maple={0};
static unsigned full_clears,text_writes;
static char lines[15][29];
void ui_fill_color(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2,uint16_t color)
{
    (void)color;
    assert(x1<=x2 && y1<=y2 && x2<240 && y2<320);
    if(x1==0 && y1==0 && x2==239 && y2==319)++full_clears;
}
void ui_write_string(uint16_t x,uint16_t y,const char *str,uint16_t color,uint16_t bg,const font_t *font)
{
    (void)color;(void)bg;
    assert(x==8 && y>=8 && (y-8)%20==0 && (y-8)/20<15);
    assert(font==&font16_maple && strlen(str)<=28);
    strcpy(lines[(y-8)/20],str);++text_writes;
}
int main(void)
{
    alarm_config_t config;
    alarm_config_defaults(&config);
    alarm_menu_t menu;
    alarm_menu_init(&menu);menu.view=ALARM_VIEW_LIST;
    alarm_page_model_t model={0};
    model.config=&config;model.menu=&menu;model.time_valid=true;model.storage_ok=true;
    model.now=(rtc_date_time_t){2026,11,13,12,30,0,5};
    alarm_page_render(&model);
    assert(full_clears==1 && text_writes==15 && strstr(lines[10],"8")!=NULL);
    alarm_page_render(&model);
    assert(full_clears==1 && text_writes==15);
    ++model.now.second;alarm_page_render(&model);
    assert(text_writes==16 && full_clears==1);
    menu.selected=1;alarm_page_render(&model);
    assert(text_writes==18);
    alarm_page_invalidate();alarm_page_render(&model);
    assert(full_clears==2 && text_writes==33);
    menu.view=ALARM_VIEW_EDIT;menu.draft=config.entries[1];
    for(menu.field=0;menu.field<16;++menu.field)alarm_page_render(&model);
    menu.view=ALARM_VIEW_CLOCK;
    for(menu.field=0;menu.field<6;++menu.field)alarm_page_render(&model);
    model.ringing_mask=255;alarm_page_render(&model);
    assert(strcmp(lines[0],"ALARM RINGING")==0 && strstr(lines[3],"8")!=NULL);
    assert(strcmp(lines[3],"Alarms: 1,2,3,4,5,6,7,8")==0);
    model.test_ringing=true;alarm_page_render(&model);
    assert(strstr(lines[3],"8")!=NULL && strcmp(lines[4],"Diagnostic alarm")==0);
    model.test_ringing=false;
    model.ringing_mask=0;model.snoozed_mask=255;alarm_page_render(&model);
    assert(strcmp(lines[0],"ALARM SNOOZED")==0 && strstr(lines[3],"8")!=NULL);
    assert(strcmp(lines[6],"K1: CANCEL ALL")==0);
    model.snoozed_mask=0;menu.view=ALARM_VIEW_MAIN;model.test_pending=true;
    alarm_page_render(&model);
    assert(strcmp(lines[0],"TEST ALARM PENDING")==0);
    menu.view=ALARM_VIEW_EDIT;alarm_page_render(&model);
    assert(strcmp(lines[0],"TEST ALARM PENDING")==0);
    model.test_pending=false;alarm_page_render(&model);
    menu.view=ALARM_VIEW_LIST;alarm_page_render(&model);
    assert(strcmp(lines[0],"LOCAL ALARMS")==0);
    puts("alarm pages: bounds, eight slots, incremental repaint and re-entry PASS");
    return 0;
}
