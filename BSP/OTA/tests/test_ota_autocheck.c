#define main legacy_main
#include "test_ota_service.c"
#undef main
static char discovery_json[1025];
static uint8_t candidate_header[256];

/** 功能：构造与虚拟元数据接口相符的候选发现正文；参数：version，候选版本；返回：无。 */
static void make_discovery(uint8_t version) {
    char encoded[513];
    char identity[33];
    memset(candidate_header, 0, sizeof candidate_header);
    candidate_header[0] = 42U;
    candidate_header[24] = version;
    candidate_header[96] = 8U;
    for (size_t i = 0U; i < 256U; ++i) {
        (void)snprintf(encoded + i * 2U, 3U, "%02x", (unsigned)candidate_header[i]);
    }
    for (size_t i = 0U; i < 16U; ++i) {
        (void)snprintf(identity + i * 2U, 3U, "%02x", (unsigned)candidate_header[96U + i]);
    }
    (void)snprintf(discovery_json, sizeof discovery_json,
                   "{\"available\":true,\"version\":%u,\"packageId\":\"%s\",\"header\":\"%s\"}",
                   (unsigned)version, identity, encoded);
    http_response = discovery_json;
}

/** 功能：隔离运行自动检查健康、事务及复位回退场景；参数：场景编号；返回：通过返回零。 */
int main(int argc, char **argv) {
    unsigned scenario = argc == 2 ? (unsigned)atoi(argv[1]) : 0U;
    uint8_t *flash = VirtualAlloc((void *)(uintptr_t)0x08010000U, 0x70000U,
                                  MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    uint8_t *footer = (uint8_t *)(uintptr_t)OTA_FOOTER_BASE;
    assert(flash != NULL);
    memset(footer, 0, 256U);
    footer[0] = 42U;
    footer[24] = 17U;
    footer[96] = 7U;
    persisted.state = OTA_STATE_CONFIRMED;
    memcpy(persisted.manifest, footer, 256U);
    make_discovery(scenario == 2U ? 17U : scenario == 3U ? 16U : 18U);
    if (scenario == 1U) {
        http_response = "{\"available\":false}";
    }
    if (scenario == 9U) {
        http_response = NULL;
    }
    if (scenario == 18U) {
        http_response = "{\"available\":true}";
    }
    if (scenario == 10U || scenario == 11U || scenario == 15U || scenario == 21U) {
        persisted.state = OTA_STATE_REQUESTED;
        persisted.error = scenario == 15U ? 2U : 1U;
        memcpy(persisted.manifest, candidate_header, 256U);
        software_reset = scenario == 10U;
        if (scenario == 21U) {
            persisted.manifest[0] = 0U;
        }
    }
    if (scenario == 12U) {
        persisted.state = OTA_STATE_TRIAL;
    }
    if (scenario == 13U) {
        persisted.state = OTA_STATE_REQUESTED;
        memset(persisted.manifest, 0, 256U);
    }
    if (scenario == 14U) {
        persisted.state = OTA_STATE_INSTALLING;
    }
    if (scenario == 20U) {
        persisted.manifest[96] = 9U;
    }
    ota_service_init();
    ota_service_start();
    ota_service_autocheck_poll(false, tick);
    assert(http_checks == 0U && saves == 0U);
    pulse_local(OTA_HEARTBEAT_ALL);
    advance_local(scenario == 5U ? 9999U : 10000U, OTA_HEARTBEAT_ALL);
    if (scenario == 5U) {
        ota_service_autocheck_poll(true, tick);
        assert(http_checks == 0U && saves == 0U);
    } else {
        ota_service_network_poll(tick);
        if (scenario == 12U) {
            ota_service_autocheck_poll(true, tick);
            assert(http_checks == 0U && persisted.state == OTA_STATE_TRIAL);
        }
        run_worker();
        unsigned confirmation_saves = saves;
        editing = scenario == 6U;
        snapshot_busy = scenario >= 22U && scenario <= 26U ? (uint8_t)(1U << (scenario - 22U)) : 0U;
        fail_enqueue = scenario == 19U;
        ota_service_autocheck_poll(true, tick);
        if (scenario == 10U || scenario == 13U || scenario == 14U || scenario == 15U ||
            scenario == 20U || scenario == 21U) {
            assert(http_checks == 0U && saves == 0U && !queue_full);
        } else if (scenario == 1U || scenario == 2U || scenario == 3U) {
            assert(http_checks == 1U && saves == 0U && !queue_full);
            ota_service_autocheck_poll(true, tick);
            assert(http_checks == 1U && saves == 0U);
            expect_state("\"autoState\":\"current\"");
        } else if (scenario == 9U || scenario == 18U) {
            assert(http_checks == 1U && saves == 0U);
            advance_local(14999U, OTA_HEARTBEAT_ALL);
            ota_service_autocheck_poll(true, tick);
            assert(http_checks == 1U);
            advance_local(15000U, OTA_HEARTBEAT_ALL);
            ota_service_autocheck_poll(true, tick);
            assert(http_checks == 2U);
            advance_local(29999U, OTA_HEARTBEAT_ALL);
            ota_service_autocheck_poll(true, tick);
            assert(http_checks == 2U);
            advance_local(30000U, OTA_HEARTBEAT_ALL);
            ota_service_autocheck_poll(true, tick);
            assert(http_checks == 3U);
            advance_local(100000U, OTA_HEARTBEAT_ALL);
            ota_service_autocheck_poll(true, tick);
            assert(http_checks == 3U && saves == 0U && resets == 0U);
            expect_state("\"autoState\":\"unreachable\"");
        } else {
            if (scenario == 6U || scenario == 19U || (scenario >= 22U && scenario <= 26U)) {
                assert(!queue_full && saves == confirmation_saves);
                editing = false;
                snapshot_busy = 0U;
                fail_enqueue = false;
                ota_service_autocheck_poll(true, tick);
            }
            assert(queue_full && http_checks == 1U);
            if (scenario == 7U) {
                reject_gate = true;
            }
            if (scenario == 8U) {
                lost_ack = true;
            }
            if (scenario == 16U) {
                persisted.state = OTA_STATE_TRIAL;
            }
            if (scenario == 17U) {
                memset(discovery_json, 'x', 800U);
            }
            if (scenario == 27U) {
                queued[sizeof(int) + 24U] = 16U;
            }
            if (scenario == 28U) {
                tick += 1001U;
            }
            run_worker();
            if (scenario == 7U) {
                assert(saves == confirmation_saves && gate == ALARM_MAINTENANCE_NONE);
                reject_gate = false;
                ota_service_autocheck_poll(true, tick);
                assert(queue_full && http_checks == 1U);
                run_worker();
            }
            if (scenario == 8U || scenario == 16U || scenario == 27U || scenario == 28U) {
                ota_service_network_poll(tick + 2000U);
                ota_service_autocheck_poll(true, tick + 2000U);
                assert(resets == 0U && !queue_full);
                expect_state("\"autoState\":\"failed\"");
            } else {
                assert(saves == confirmation_saves + 1U);
                assert(persisted.state == OTA_STATE_REQUESTED);
                assert(memcmp(persisted.manifest, candidate_header, 256U) == 0);
                ota_service_network_poll(tick + 1999U);
                assert(resets == 0U);
                ota_service_network_poll(tick + 2000U);
                assert(resets == 1U);
                expect_state("\"autoState\":\"queued\"");
            }
        }
    }
    VirtualFree(flash, 0U, MEM_RELEASE);
    puts("OTA automatic check scenario passed");
    return 0;
}
