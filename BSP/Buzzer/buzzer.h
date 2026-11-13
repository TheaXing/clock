#ifndef BUZZER_H
#define BUZZER_H
#include <stdbool.h>
/* Default: passive buzzer, PC7 TIM3_CH2 ~2730 Hz. Set to 1 for DC active buzzer. */
#ifndef CLOCK_BUZZER_ACTIVE
#define CLOCK_BUZZER_ACTIVE 0
#endif
void buzzer_init(void);
void buzzer_set(bool enabled);
#endif
