#ifndef BOOT_POLICY_H
#define BOOT_POLICY_H

#include "ota_image.h"
#include "ota_journal.h"

typedef enum {
    BOOT_RECOVER,
    BOOT_START_CONFIRMED,
    BOOT_START_TRIAL,
    BOOT_REPAIR_TRIAL,
    BOOT_START_UNTRACKED
} boot_action_t;

boot_action_t boot_policy_action(ota_store_result_t store, const ota_record_t *record, const ota_manifest_t *installed, bool forced);
bool boot_policy_candidate_allowed(const ota_record_t *record, const ota_manifest_t *candidate, bool forced);
bool boot_policy_commit_trial(const ota_journal_io_t *io, ota_record_t *record, const ota_manifest_t *installed, bool repair);

#endif
