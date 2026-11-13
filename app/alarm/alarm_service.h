#ifndef CLOCK_ALARM_SERVICE_H
#define CLOCK_ALARM_SERVICE_H
#include <stdbool.h>
/* Call in the init task after RTC/UI/I2C1, before starting network work. */
void alarm_service_init(void);
/* Normal clock/weather producers hold this across a full UI update. False
 * means the alarm menu/ringing view currently owns the screen. */
bool alarm_service_display_begin(void);
void alarm_service_display_end(void);
#endif
