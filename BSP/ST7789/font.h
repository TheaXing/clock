#ifndef __FONT_H
#define __FONT_H

#include <stdint.h>

// !"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_`abcdefghijklmnopqrstuvwxyz{|}~

typedef struct
{
    const char *name;
    const uint8_t *model;
} font_chinese_t;

typedef struct
{
    const uint8_t *ascii_model;
	const char *ascii_map;
    const font_chinese_t *chinese;
    uint16_t size;
} font_t;


extern const font_t font16_maple;
extern const font_t font24_maple;
extern const font_t font24_maple_bold;
extern const font_t font32_maple;
extern const font_t font42_maple;
extern const font_t font54_maple_medium;
extern const font_t font62_maple_bold;
extern const font_t font76_maple_bold;


#endif /* __FONT_H */
