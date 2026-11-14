#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "wifi_identity.h"

static uint32_t words[3];
static unsigned position;
/** 功能：提供硬件随机接口的确定性样本。参数：无。返回：下一样本。 */
uint32_t clock_web_session_token(void) {
    assert(position < 3U);
    return words[position++];
}
/** 功能：模拟设备唯一编号首字。参数：无。返回：测试编号。 */
uint32_t HAL_GetUIDw0(void) {
    return 0x12345678U;
}
/** 功能：模拟设备唯一编号中字。参数：无。返回：测试编号。 */
uint32_t HAL_GetUIDw1(void) {
    return 0x11111111U;
}
/** 功能：模拟设备唯一编号末字。参数：无。返回：测试编号。 */
uint32_t HAL_GetUIDw2(void) {
    return 0x22222222U;
}
/** 功能：验证随机失败不会生成公共默认密码。参数：无。返回：零。 */
int main(void) {
    char name[33], password[17];
    uint32_t session = 0U;
    words[0] = 0x12345678U;
    words[1] = 0x90abcdefU;
    words[2] = 77U;
    assert(wifi_setup_identity(name, password, &session));
    assert(strcmp(name, "Clock-Setup-2107654B") == 0);
    assert(strcmp(password, "1234567890abcdef") == 0 && session == 77U);
    for (unsigned index = 0U; index < 3U; ++index) {
        words[0] = words[1] = words[2] = 77U;
        words[index] = 0U;
        position = 0U;
        assert(!wifi_setup_identity(name, password, &session));
    }
    puts("Wi-Fi hardware identity adapter passed");
    return 0;
}
