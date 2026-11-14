#ifndef BUZZER_H
#define BUZZER_H
#include <stdbool.h>
/* 默认使用无源蜂鸣器，PC7 的 TIM3_CH2 输出约 2730 Hz；直流有源蜂鸣器请设置为 1。 */
#ifndef CLOCK_BUZZER_ACTIVE
#define CLOCK_BUZZER_ACTIVE 0
#endif
void buzzer_init(void);
void buzzer_set(bool enabled);
#endif
