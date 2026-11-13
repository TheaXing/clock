#ifndef CLOCK_WEB_SESSION_H
#define CLOCK_WEB_SESSION_H
#include <stdint.h>
/* Fresh hardware RNG value per boot, independent of RTC/VBAT. Zero means
 * unavailable; do not accept remote mutations with a deterministic fallback. */
uint32_t clock_web_session_token(void);
#endif
