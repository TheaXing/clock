#include "ui.h"


void welcome_page_display(void)
{
	const uint16_t color_bg = mkcolor(255, 255, 255);
	ui_fill_color(0, 0 , UI_WIDTH - 1, UI_HEIGHT - 1 ,color_bg);
	ui_draw_image (30,10,&img_star);
	ui_write_string (70,205,"ÏÄ¾´ÐÇ",mkcolor(237,128,147),color_bg,&font32_maple);
	ui_write_string (56,238,"ÌìÆøÊ±ÖÓ",mkcolor(86,168,255),color_bg,&font32_maple);
	ui_write_string (60,280,"loading...",mkcolor(0, 0, 0),color_bg,&font24_maple);
}
