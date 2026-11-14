/**
 * @file sysmem.c
 * @author 由 STM32CubeMX 自动生成
 * @brief 提供标准库内存系统调用；各 C 库函数依赖的底层接口见 Newlib 或 Picolibc 库手册。
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2025 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */

/* 头文件 */
#include <errno.h>
#include <stdint.h>
#include <stddef.h>

/** 记录当前标准库堆使用高水位的指针。 */
static uint8_t *__sbrk_heap_end = NULL;

/**
 * @brief 功能：从链接符号 _end 开始调整标准库堆边界，并保护 MSP 预留栈空间。
 * 参数：
 * @param incr 堆边界增量，单位为字节。
 * @retval 返回：成功返回调整前的堆尾地址；超出预留栈边界时设置 ENOMEM 并返回 (void *)-1。
 * @verbatim
 *   .data | .bss | 标准库堆              | MSP 栈（_Min_Stack_Size 预留）
 *   RAM 起点     _end                  _estack（RAM 末尾）
 * @endverbatim
 * @note _end 是堆起点，_estack 是 RAM 末尾，_Min_Stack_Size 预留 MSP 栈空间；若运行时 MSP 栈超过预留大小，须增大该链接符号。
 */
void *_sbrk(ptrdiff_t incr) {
    extern uint8_t _end;             /* 链接脚本定义的符号 */
    extern uint8_t _estack;          /* 链接脚本定义的符号 */
    extern uint32_t _Min_Stack_Size; /* 链接脚本定义的符号 */
    const uint32_t stack_limit = (uint32_t)&_estack - (uint32_t)&_Min_Stack_Size;
    const uint8_t *max_heap = (uint8_t *)stack_limit;
    uint8_t *prev_heap_end;

    /* 首次调用时初始化堆尾指针。 */
    if (NULL == __sbrk_heap_end) {
        __sbrk_heap_end = &_end;
    }

    /* 防止堆增长进入 MSP 预留栈空间。 */
    if (__sbrk_heap_end + incr > max_heap) {
        errno = ENOMEM;
        return (void *)-1;
    }

    prev_heap_end = __sbrk_heap_end;
    __sbrk_heap_end += incr;

    return (void *)prev_heap_end;
}

#if defined(__PICOLIBC__)
// Picolibc 使用无前导下划线的系统调用名称。
// 创建强别名，使
// sbrk() 调用解析到本文件的 _sbrk() 实现。
__strong_reference(_sbrk, sbrk);
#endif
