#include "boot_platform.h"
#include "boot_policy.h"
#include "ota_config.h"
#include "ota_image.h"
#include "clock_config.h"
#include <string.h>

static boot_net_t network;
static uint8_t release_key[32];
static ota_journal_io_t journal;
static bool trial_start_blocked;

/** 功能：完整验证内部应用描述及实际镜像；参数：输出描述；返回：可启动为真。 */
static bool installed_image(ota_manifest_t *manifest) {
    uint8_t footer[256];
    return boot_platform_read(NULL, OTA_FOOTER_BASE, footer, sizeof footer) &&
           ota_manifest_parse(footer, release_key, true, manifest) &&
           ota_image_verify(manifest, (const uint8_t *)(uintptr_t)OTA_APP_BASE);
}

/** 功能：尝试经认证的启动并持久化试启动次数；参数：是否允许保留请求启动旧版；返回：未跳转为假。 */
static bool start_application(bool allow_pending) {
    ota_manifest_t installed;
    ota_record_t record;
    if (trial_start_blocked) {
        return false;
    }
    if (!installed_image(&installed)) {
        return false;
    }
    memset(&record, 0, sizeof record);
    ota_store_result_t status = ota_journal_load(&journal, &record);
    boot_action_t action = boot_policy_action(status, &record, &installed, false);
    if (allow_pending && action == BOOT_RECOVER && status == OTA_STORE_OK &&
        (record.state == OTA_STATE_REQUESTED || record.state == OTA_STATE_INSTALLING)) {
        action = BOOT_START_UNTRACKED;
    }
    if (action == BOOT_RECOVER) {
        return false;
    }
    if (action == BOOT_REPAIR_TRIAL || action == BOOT_START_TRIAL) {
        if (!boot_policy_commit_trial(&journal, &record, &installed, action == BOOT_REPAIR_TRIAL)) {
            trial_start_blocked = true;
            boot_platform_log("[BOOT] Trial count not committed; recovery required\r\n");
            return false;
        }
    } else if (action == BOOT_START_UNTRACKED) {
        boot_platform_log("[BOOT] Authenticated App; state log unavailable or request retained\r\n");
    }
    boot_platform_log("[BOOT] Full image verified; starting App\r\n");
    boot_platform_jump();
    return false;
}

/** 功能：仅在错误编号变化时记录故障；参数：错误编号；返回：无。 */
static void record_error(uint32_t error) {
    ota_record_t record;
    if (ota_journal_load(&journal, &record) == OTA_STORE_OK && record.error != error) {
        record.error = error;
        (void)ota_journal_save(&journal, &record);
    }
}

/** 功能：阻止日志故障及同一失败镜像反复清零次数；参数：候选包和物理强制标志；返回：允许安装为真。 */
static bool candidate_allowed(const ota_manifest_t *candidate, bool forced) {
    ota_record_t record;
    ota_store_result_t result = ota_journal_load(&journal, &record);
    return result == OTA_STORE_EMPTY ||
           (result == OTA_STORE_OK && boot_policy_candidate_allowed(&record, candidate, forced));
}

/** 功能：运行独立恢复下载及安全启动；参数：无；返回：不返回。 */
int main(void) {
    if (!boot_platform_init()) {
        for (;;) {
        }
    }
    boot_platform_log("\r\n[BOOT] Clock single-slot OTA v1\r\n");
    journal = boot_platform_journal_io();
    bool forced = boot_platform_key();
    if (!ota_config_decode_hex(CLOCK_OTA_KEY_HEX, release_key)) {
        boot_platform_log("[BOOT] Release key invalid; recovery blocked\r\n");
        for (;;) {
            boot_platform_delay(1000U);
        }
    }
    if (!forced) {
        (void)start_application(false);
    }
    boot_platform_log("[BOOT] Recovery download; existing App may be unavailable\r\n");
    uint32_t recovery_start = boot_platform_now();
    uint32_t retry_ms = 2000U;
    for (;;) {
        boot_net_io_t net_io = boot_platform_net_io();
        uint8_t header[256];
        ota_manifest_t manifest;
        bool ready = boot_net_connect(&network, &net_io, CLOCK_WIFI_SSID, CLOCK_WIFI_PASSWORD,
                                      CLOCK_OTA_HOST, (uint16_t)CLOCK_OTA_PORT, CLOCK_OTA_PATH);
        if (ready && boot_net_manifest(&network, header) &&
            ota_manifest_parse(header, release_key, false, &manifest) &&
            network.total == OTA_HEADER_SIZE + manifest.image_size && candidate_allowed(&manifest, forced)) {
            boot_platform_log("[BOOT] Authenticated package; committing installation permit\r\n");
            ota_record_t record;
            memset(&record, 0, sizeof record);
            ota_install_io_t installer = {journal, &network, boot_platform_erase,
                boot_platform_program, boot_platform_read, boot_net_fetch};
            if (ota_install(&installer, header, release_key, &record)) {
                trial_start_blocked = false;
                boot_platform_log("[BOOT] Installation verified; trial boot\r\n");
                (void)start_application(false);
            }
            record_error(2U);
            boot_platform_log("[BOOT] Install incomplete; no unverified App will run\r\n");
        } else {
            record_error(1U);
            boot_platform_log("[BOOT] Network/package preflight failed; check server and AT\r\n");
        }
        if (!forced && (uint32_t)(boot_platform_now() - recovery_start) >= 60000U) {
            (void)start_application(true);
        }
        boot_platform_delay(retry_ms);
        if (retry_ms < 30000U) {
            retry_ms = retry_ms > 15000U ? 30000U : retry_ms * 2U;
        }
    }
}
