/**
 * @file syscalls.c
 * @author 由 STM32CubeMX 自动生成
 * @brief 提供最小系统调用桩；各 C 库函数依赖的底层接口见 Newlib 或 Picolibc 库手册。
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2020-2025 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */

/* 头文件 */
#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <sys/times.h>

/* 变量 */
extern int __io_putchar(int ch) __attribute__((weak));
extern int __io_getchar(void) __attribute__((weak));

char *__env[1] = {0};
char **environ = __env;

/* 函数 */
/**
 * @brief 功能：提供标准库监视器句柄初始化入口，当前无需初始化。
 * 参数：无。
 * @retval 返回：无
 */
void initialise_monitor_handles() {
}

/**
 * @brief 功能：为标准库提供固定的进程标识。
 * 参数：无。
 * @retval 返回：固定返回 1。
 */
int _getpid(void) {
    return 1;
}

/**
 * @brief 功能：提供不支持的进程信号接口并设置 EINVAL 错误。
 * 参数：
 * @param pid 目标进程标识，未使用。
 * @param sig 信号编号，未使用。
 * @retval 返回：固定返回 -1。
 */
int _kill(int pid, int sig) {
    (void)pid;
    (void)sig;
    errno = EINVAL;
    return -1;
}

/**
 * @brief 功能：调用进程终止桩接口后永久停留，防止返回调用方。
 * 参数：
 * @param status 传递给终止接口的状态值。
 * @retval 返回：无，函数持续等待。
 */
void _exit(int status) {
    _kill(status, -1);
    while (1) {
    } /* 确保程序停留在此处。 */
}

/**
 * @brief 功能：从底层字符输入接口读取指定数量的字符。
 * 参数：
 * @param file 文件描述符，未使用。
 * @param ptr 接收缓冲区。
 * @param len 要读取的字符数。
 * @retval 返回：返回请求读取的字符数 len。
 */
__attribute__((weak)) int _read(int file, char *ptr, int len) {
    (void)file;
    int data_index;

    for (data_index = 0; data_index < len; data_index++) {
        *ptr++ = __io_getchar();
    }

    return len;
}

/**
 * @brief 功能：通过底层字符输出接口发送指定数量的字符。
 * 参数：
 * @param file 文件描述符，未使用。
 * @param ptr 发送缓冲区。
 * @param len 要发送的字符数。
 * @retval 返回：返回请求发送的字符数 len。
 */
__attribute__((weak)) int _write(int file, char *ptr, int len) {
    (void)file;
    int data_index;

    for (data_index = 0; data_index < len; data_index++) {
        __io_putchar(*ptr++);
    }
    return len;
}

/**
 * @brief 功能：提供不支持的文件关闭接口。
 * 参数：
 * @param file 文件描述符，未使用。
 * @retval 返回：固定返回 -1。
 */
int _close(int file) {
    (void)file;
    return -1;
}

/**
 * @brief 功能：将文件描述符对应的对象报告为字符设备。
 * 参数：
 * @param file 文件描述符，未使用。
 * @param st 保存文件状态的结构体指针。
 * @retval 返回：固定返回 0。
 */
int _fstat(int file, struct stat *st) {
    (void)file;
    st->st_mode = S_IFCHR;
    return 0;
}

/**
 * @brief 功能：将文件描述符报告为终端设备。
 * 参数：
 * @param file 文件描述符，未使用。
 * @retval 返回：固定返回 1。
 */
int _isatty(int file) {
    (void)file;
    return 1;
}

/**
 * @brief 功能：提供无实际定位操作的文件偏移接口。
 * 参数：
 * @param file 文件描述符，未使用。
 * @param ptr 目标偏移，未使用。
 * @param dir 定位起点，未使用。
 * @retval 返回：固定返回 0。
 */
int _lseek(int file, int ptr, int dir) {
    (void)file;
    (void)ptr;
    (void)dir;
    return 0;
}

/**
 * @brief 功能：提供不支持的文件打开接口。
 * 参数：
 * @param path 文件路径，未使用。
 * @param flags 打开标志，未使用。
 * 可变参数为附加选项
 * @retval 返回：固定返回 -1。
 */
int _open(char *path, int flags, ...) {
    (void)path;
    (void)flags;
    /* 此桩接口始终报告失败。 */
    return -1;
}

/**
 * @brief 功能：提供不支持的子进程等待接口并设置 ECHILD 错误。
 * 参数：
 * @param status 子进程状态指针，未使用。
 * @retval 返回：固定返回 -1。
 */
int _wait(int *status) {
    (void)status;
    errno = ECHILD;
    return -1;
}

/**
 * @brief 功能：提供不支持的文件删除接口并设置 ENOENT 错误。
 * 参数：
 * @param name 文件名，未使用。
 * @retval 返回：固定返回 -1。
 */
int _unlink(char *name) {
    (void)name;
    errno = ENOENT;
    return -1;
}

/**
 * @brief 功能：提供不支持的进程计时接口。
 * 参数：
 * @param buf 进程计时结果指针，未使用。
 * @retval 返回：固定返回 -1。
 */
clock_t _times(struct tms *buf) {
    (void)buf;
    return -1;
}

/**
 * @brief 功能：将指定路径对应的对象报告为字符设备。
 * 参数：
 * @param file 文件路径，未使用。
 * @param st 保存文件状态的结构体指针。
 * @retval 返回：固定返回 0。
 */
int _stat(const char *file, struct stat *st) {
    (void)file;
    st->st_mode = S_IFCHR;
    return 0;
}

/**
 * @brief 功能：提供不支持的硬链接接口并设置 EMLINK 错误。
 * 参数：
 * @param old 原路径，未使用。
 * @param new 新路径，未使用。
 * @retval 返回：固定返回 -1。
 */
int _link(char *old, char *new) {
    (void)old;
    (void)new;
    errno = EMLINK;
    return -1;
}

/**
 * @brief 功能：提供不支持的进程创建接口并设置 EAGAIN 错误。
 * 参数：无。
 * @retval 返回：固定返回 -1。
 */
int _fork(void) {
    errno = EAGAIN;
    return -1;
}

/**
 * @brief 功能：提供不支持的程序执行接口并设置 ENOMEM 错误。
 * 参数：
 * @param name 程序名，未使用。
 * @param argv 参数数组，未使用。
 * @param env 环境数组，未使用。
 * @retval 返回：固定返回 -1。
 */
int _execve(char *name, char **argv, char **env) {
    (void)name;
    (void)argv;
    (void)env;
    errno = ENOMEM;
    return -1;
}

// Picolibc 专用配置
#if defined(__PICOLIBC__)

/**
 * @brief 功能：将 Picolibc 流的字符输出重定向到 __io_putchar。
 * 参数：
 * @param c 待输出字符。
 * @param file 标准库流指针，未使用。
 * @retval 返回：返回已输出字符。
 */
static int starm_putc(char c, FILE *file) {
    (void)file;
    __io_putchar(c);
    return c;
}

/**
 * @brief 功能：将 Picolibc 流的字符输入重定向到 __io_getchar。
 * 参数：
 * @param file 标准库流指针，未使用。
 * @retval 返回：返回读入字符，先转换为 unsigned char 再提升为 int。
 */
static int starm_getc(FILE *file) {
    unsigned char c;
    (void)file;
    c = __io_getchar();
    return c;
}

// 定义并初始化 Picolibc 标准输入输出流。
// FDEV_SETUP_STREAM 将 starm_putc、starm_getc 辅助函数关联到 FILE 结构体。
// _FDEV_SETUP_RW 表示该流可读写。
static FILE __stdio = FDEV_SETUP_STREAM(starm_putc, starm_getc, NULL, _FDEV_SETUP_RW);

// 将 stdin、stdout 和 stderr 标准流指针关联到已初始化的流。
// Picolibc 使用这些指针执行 printf、scanf 等标准输入输出操作。
FILE *const stdin = &__stdio;
__strong_reference(stdin, stdout);
__strong_reference(stdin, stderr);

// 创建强别名，将无前导下划线的标准 C 库函数名
// 映射到带下划线的系统调用桩函数。Picolibc 内部使用
// 标准名称，因此必须提供这些链接别名。
__strong_reference(_read, read);
__strong_reference(_write, write);
__strong_reference(_times, times);
__strong_reference(_execve, execve);
__strong_reference(_fork, fork);
__strong_reference(_link, link);
__strong_reference(_unlink, unlink);
__strong_reference(_stat, stat);
__strong_reference(_wait, wait);
__strong_reference(_open, open);
__strong_reference(_close, close);
__strong_reference(_lseek, lseek);
__strong_reference(_isatty, isatty);
__strong_reference(_fstat, fstat);
__strong_reference(_exit, exit);
__strong_reference(_kill, kill);
__strong_reference(_getpid, getpid);

#endif /* 条件结束：__PICOLIBC__ */
