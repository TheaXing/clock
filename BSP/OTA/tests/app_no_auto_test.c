#define main legacy_main
#include "test_ota_service.c"
#undef main

/** 功能：验证 OTA 固件显式关闭自动检查后仍能确认本地健康；参数：无；返回：通过返回零。 */
int main(void) {
    char *arguments[] = {"test", "6"};
    char status[512];
    assert(legacy_main(2, arguments) == 0);
    ota_service_autocheck_poll(true, tick);
    ota_service_status(status, sizeof status, 1U);
    assert(http_checks == 0U && saves == 1U);
    assert(strstr(status, "\"autoEnabled\":false") != NULL);
    assert(strstr(status, "\"autoState\":\"disabled\"") != NULL);
    puts("OTA automatic check explicit disable passed");
    return 0;
}
