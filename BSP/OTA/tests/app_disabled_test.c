#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ota_service.h"

/** 功能：确认独立应用不依赖 RTOS 或凭据且拒绝升级；参数：无；返回：通过返回零。 */
int main(void) {
    char status[256];
    ota_service_init();
    ota_service_start();
    ota_service_heartbeat(OTA_HEARTBEAT_ALL);
    ota_service_network_poll(UINT32_MAX);
    ota_service_status(status, sizeof status, 99U);
    assert(strstr(status, "\"enabled\":false") != NULL);
    assert(strstr(status, "\"state\":\"disabled\"") != NULL);
    assert(ota_service_request("session=99&token=bad", 99U) == 503);
    puts("OTA disabled application passed");
    return 0;
}
