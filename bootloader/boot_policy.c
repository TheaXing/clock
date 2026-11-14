#include "boot_policy.h"
#include <string.h>

/**
 * 功能：依据可信镜像和持久化状态选择启动动作，不访问硬件也不修改记录。
 * 参数：store 为日志扫描结果；record 仅在扫描成功时必需；installed 必须已经通过包头认证、完整镜像摘要及向量验证，否则传空；forced 为强制恢复标志。
 * 返回：启动、修复试启动记录或保持恢复的策略动作，实际写日志及跳转由调用者负责。
 */
boot_action_t boot_policy_action(ota_store_result_t store, const ota_record_t *record, const ota_manifest_t *installed, bool forced) {
    bool matching;
    if (forced || installed == NULL) {
        return BOOT_RECOVER;
    }
    if (store == OTA_STORE_IO || store == OTA_STORE_CORRUPT) {
        return BOOT_START_UNTRACKED;
    }
    if (store == OTA_STORE_EMPTY) {
        return BOOT_REPAIR_TRIAL;
    }
    if (store != OTA_STORE_OK || record == NULL || (unsigned)record->state > OTA_STATE_FAILED) {
        return BOOT_RECOVER;
    }
    matching = memcmp(record->manifest, installed->raw, 252) == 0;
    switch (record->state) {
        case OTA_STATE_REQUESTED:
        case OTA_STATE_FAILED:
            return BOOT_RECOVER;
        case OTA_STATE_INSTALLING:
            return matching ? BOOT_REPAIR_TRIAL : BOOT_RECOVER;
        case OTA_STATE_TRIAL:
            if (!matching) {
                return BOOT_REPAIR_TRIAL;
            }
            return record->attempts < 3 ? BOOT_START_TRIAL : BOOT_RECOVER;
        case OTA_STATE_CONFIRMED:
            return matching ? BOOT_START_CONFIRMED : BOOT_REPAIR_TRIAL;
        case OTA_STATE_IDLE:
            return BOOT_REPAIR_TRIAL;
        default:
            return BOOT_RECOVER;
    }
}

/**
 * 功能：阻止自动恢复无限重装已经耗尽试启动次数的同一包。
 * 参数：record 为最近可信日志或空；candidate 为已认证候选包或空；forced 表示操作者显式强制恢复。
 * 返回：非空候选可安装时为真；未强制且同一包已试启动至少三次时为假。
 */
bool boot_policy_candidate_allowed(const ota_record_t *record, const ota_manifest_t *candidate, bool forced) {
    if (candidate == NULL) {
        return false;
    }
    if (forced) {
        return true;
    }
    return record == NULL || record->state != OTA_STATE_TRIAL || record->attempts < 3 ||
        memcmp(record->manifest, candidate->raw, 252) != 0;
}

/**
 * 功能：先提交试启动次数，再给予启动许可，失败不修改调用者记录。
 * 参数：io 为完整事务的日志接口；record 为已读取状态；installed 必须已完整验证；repair 表示策略要求建立新的试启动记录。
 * 返回：递增后的试启动记录已提交并回读成功时为真，任何失败或次数耗尽均为假。
 */
bool boot_policy_commit_trial(const ota_journal_io_t *io, ota_record_t *record, const ota_manifest_t *installed, bool repair) {
    ota_record_t next;
    if (io == NULL || record == NULL || installed == NULL) {
        return false;
    }
    if (repair) {
        memset(&next, 0, sizeof(next));
        next.state = OTA_STATE_TRIAL;
        memcpy(next.manifest, installed->raw, sizeof(next.manifest));
    } else {
        if (record->state != OTA_STATE_TRIAL || record->attempts >= 3 ||
            memcmp(record->manifest, installed->raw, 252) != 0) {
            return false;
        }
        next = *record;
    }
    ++next.attempts;
    if (ota_journal_save(io, &next) != OTA_STORE_OK) {
        return false;
    }
    *record = next;
    return true;
}
