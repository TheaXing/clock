#ifndef BOARD_KEYS_H
#define BOARD_KEYS_H
#include <stdint.h>
#define BOARD_KEY1_SHORT (1UL << 0)
#define BOARD_KEY2_SHORT (1UL << 1)
#define BOARD_KEY3_SHORT (1UL << 2)
#define BOARD_KEY1_LONG (1UL << 8)
#define BOARD_KEY2_LONG (1UL << 9)
#define BOARD_KEY3_LONG (1UL << 10)
/* Call once at startup, then poll every 10 ms with monotonic milliseconds. */
void board_keys_init(void);
uint32_t board_keys_poll(uint32_t now_ms);
#endif
