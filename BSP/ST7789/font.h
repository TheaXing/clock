#ifndef __FONT_H
#define __FONT_H

#include <stdint.h>

// 可打印 ASCII 字符表：!"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_`abcdefghijklmnopqrstuvwxyz{|}~

/** 将单个 GB2312 汉字与其单色字模关联。 */
typedef struct {
    const char *name;     // 以空字节结束的 GB2312 汉字，空指针标记表尾
    const uint8_t *model; // 按行存储的单色汉字字模
} font_chinese_t;

/** 描述字体尺寸以及 ASCII 和汉字的字模映射。 */
typedef struct {
    const uint8_t *ascii_model;    // 连续排列的 ASCII 字模数据
    const char *ascii_map;         // 可选 ASCII 字符映射表，空指针表示从空格起连续排列
    const font_chinese_t *chinese; // 汉字与字模对应表，空指针表示不含汉字
    uint16_t size;                 // 字体高度，单位为像素；ASCII 宽度为高度的一半
} font_t;

extern const font_t font16_maple;
extern const font_t font24_maple;
extern const font_t font24_maple_bold;
extern const font_t font32_maple;
extern const font_t font42_maple;
extern const font_t font54_maple_medium;
extern const font_t font62_maple_bold;
extern const font_t font76_maple_bold;

#endif /* __FONT_H 头文件结束 */
