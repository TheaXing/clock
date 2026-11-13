#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "weather.h"

int main(void)
{
    weather_info_t info;
    const char *good = "+HTTPCLIENT:200,{\"results\":[{\"location\":{\"name\":\"Hefei\",\"path\":\"Hefei,China\"},\"now\":{\"text\":\"Cloudy\",\"code\":\"4\",\"temperature\":\"-2.5\"}}]}\r\nOK\r\n";
    assert(parse_seniverse_response(good, &info));
    assert(strcmp(info.city, "Hefei") == 0);
    assert(strcmp(info.loaction, "Hefei,China") == 0);
    assert(strcmp(info.weather, "Cloudy") == 0);
    assert(info.weather_code == 4 && fabsf(info.temperature + 2.5f) < 0.001f);
    memset(&info, 0x5a, sizeof(info));
    weather_info_t before = info;
    assert(!parse_seniverse_response("{\"results\":[{\"location\":{},\"now\":{}}]}", &info));
    assert(memcmp(&before, &info, sizeof(info)) == 0);
    assert(!parse_seniverse_response("{\"results\":[]}", &info));
    assert(!parse_seniverse_response("{\"status\":\"error\"}", &info));
    assert(!parse_seniverse_response(NULL, &info));
    assert(!parse_seniverse_response(good, NULL));
    assert(!parse_seniverse_response("{\"results\":[{\"location\":{\"name\":\"x\",\"path\":\"y\"},\"now\":{\"text\":\"Clear\",\"code\":\"bad\",\"temperature\":\"12\"}}]}", &info));
    assert(!parse_seniverse_response("{\"results\":[{\"location\":{\"name\":\"x\",\"path\":\"y\"},\"now\":{\"text\":\"Clear\",\"code\":\"0\",\"temperature\":\"nan\"}}]}", &info));
    assert(!parse_seniverse_response("{\"results\":[{\"location\":{\"name\":\"x\",\"path\":\"y\"},\"now\":{\"text\":\"Clear\",\"code\":\"0\",\"temperature\":\"12oops\"}}]}", &info));
    assert(!parse_seniverse_response("{\"results\":[{\"location\":{\"name\":\"x\",\"path\":\"y\"},\"now\":{\"text\":\"Clear\",\"code\":\"0\",\"temperature\":\"12\"}", &info));
    assert(!parse_seniverse_response("{\"results\":[{\"location\":{\"name\":\"abcdefghijklmnopqrstuvwxyz0123456789\",\"path\":\"y\"},\"now\":{\"text\":\"Clear\",\"code\":\"0\",\"temperature\":\"12\"}}]}", &info));
    puts("weather: valid response, malformed fields, bounds, transactional output PASS");
    return 0;
}
