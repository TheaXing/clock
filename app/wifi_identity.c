#include "wifi_identity.h"
#include "web_session.h"
#include "main.h"
#include <stdio.h>

/** 功能：使用设备标识和硬件随机源生成热点身份。参数：名称、密码及会话输出。返回：全部采样成功为真。
 */
bool wifi_setup_identity(char ssid[33], char password[17], uint32_t *session) {
    uint32_t first = clock_web_session_token();
    uint32_t second = clock_web_session_token();
    uint32_t token = clock_web_session_token();
    if (first == 0U || second == 0U || token == 0U) {
        return false;
    }
    uint32_t id = HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetUIDw2();
    (void)snprintf(ssid, 33U, "Clock-Setup-%08lX", (unsigned long)id);
    (void)snprintf(password, 17U, "%08lx%08lx", (unsigned long)first, (unsigned long)second);
    *session = token;
    return true;
}
