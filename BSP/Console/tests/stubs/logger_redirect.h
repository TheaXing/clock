#ifndef TEST_LOGGER_REDIRECT_H
#define TEST_LOGGER_REDIRECT_H
/* 先包含 MSVC 的内联 vprintf 定义，再重命名产品源文件中的调用。 */
#include <stdio.h>
#include <stdarg.h>
int clock_test_vprintf(const char *, va_list);
#define vprintf clock_test_vprintf
#endif
