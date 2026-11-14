#define main legacy_main
#include "test_ota_service.c"
#undef main

/** 功能：验证组合发现 URL 的真实模块边界与运行时诊断；参数：无；返回：通过返回零。 */
int main(void) {
    char *arguments[] = {"test", "6"};
    assert(legacy_main(2, arguments) == 0);
    ota_service_autocheck_poll(true, tick);
#ifdef OTA_TEST_URL_ACCEPTED
    assert(http_checks == 1U);
    expect_state("\"autoState\":\"unreachable\"");
#else
    assert(http_checks == 0U);
    expect_state("\"autoState\":\"failed\"");
#endif
    assert(saves == 1U && resets == 0U);
    puts("OTA discovery combined URL runtime boundary passed");
    return 0;
}
