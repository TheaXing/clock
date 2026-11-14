#ifndef __WEAHTER_H__
#define __WEAHTER_H__

#include <stdbool.h>
#include <stdint.h>

/** 保存心知天气接口返回的城市与实时天气信息。 */
typedef struct {
    char city[32];      // 城市名称，以空字符结尾
    char loaction[128]; // 城市完整位置路径，保留既有接口字段拼写
    char weather[16];   // 天气现象的文本描述
    int weather_code;   // 心知天气现象代码
    float temperature;  // 室外摄氏温度
} weather_info_t;

bool parse_seniverse_response(const char *response, weather_info_t *info);

#endif // 天气接口头文件保护结束
