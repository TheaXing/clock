#ifndef TEST_LOGGER_REDIRECT_H
#define TEST_LOGGER_REDIRECT_H
/* Include MSVC's inline vprintf before renaming calls in the production file. */
#include <stdio.h>
#include <stdarg.h>
int clock_test_vprintf(const char *, va_list);
#define vprintf clock_test_vprintf
#endif
