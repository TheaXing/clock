#ifndef CLOCK_WEB_PAGE_H
#define CLOCK_WEB_PAGE_H

#include <stddef.h>

/* const storage is linked into flash; callers stream directly from this array. */
extern const char clock_web_page[];
extern const size_t clock_web_page_length;

#endif
